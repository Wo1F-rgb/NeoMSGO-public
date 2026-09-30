// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/Cues/MSTransientVfxActor.h"

#include "Animation/MSMeshVisualComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	// 寿命経過後、Tick停止からActor破棄までの猶予(破棄漏れ防止のバッファ)。
	constexpr float DestroyGraceSeconds = 0.05f;
	TAutoConsoleVariable<bool> CVarWaterSplashNiagaraEnabled(
		TEXT("ms.WaterSplash.NiagaraEnabled"), false,
		TEXT("Opt-in legacy Niagara detail on entry only; foam sheets render independently."), ECVF_Default);

	// #230: NS_MS_WaterFootSplashV2 is authored around a ~1 cm radius, which is
	// invisible next to an 18 m mobile suit. The right multiplier is a visual
	// judgement, so it is exposed as a CVar to allow A/B capture in one PIE run.
	TAutoConsoleVariable<float> CVarWaterSplashNiagaraScale(
		TEXT("ms.WaterSplash.NiagaraScale"),
		120.0f,
		TEXT("Uniform scale applied to the water foot splash Niagara component."),
		ECVF_Default);
	// Lets the splash be pointed at an alternative Niagara system for A/B capture
	// without a rebuild; the shipped default is the authored water system.
	TAutoConsoleVariable<FString> CVarWaterSplashNiagaraAsset(
		TEXT("ms.WaterSplash.NiagaraAsset"),
		TEXT("/Game/VFX/Water/NS_MS_WaterFootSplashV2.NS_MS_WaterFootSplashV2"),
		TEXT("Object path of the Niagara system used by the water foot splash."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarWaterSplashNiagaraVertical(
		TEXT("ms.WaterSplash.NiagaraVerticalFactor"),
		0.55f,
		TEXT("Vertical multiplier on top of ms.WaterSplash.NiagaraScale."),
		ECVF_Default);

	/**
	 * The first water contact must never resolve the Niagara package on the
	 * movement thread. Keep the request alive until it completes so a ready
	 * system can be reused by every short-lived splash actor. This state is
	 * intentionally process-wide: all calls happen on the game thread and the
	 * CVar can replace the requested path at runtime without a synchronous
	 * fallback.
	 */
	struct FWaterSplashAssetLoadState
	{
		FString RequestedPath;
		TSharedPtr<FStreamableHandle> Handle;
	};

	FWaterSplashAssetLoadState GWaterSplashAssetLoad;

	void EnsureWaterSplashAssetRequested()
	{
		const FString RequestedPath = CVarWaterSplashNiagaraAsset.GetValueOnGameThread();
		if (GWaterSplashAssetLoad.RequestedPath == RequestedPath &&
			GWaterSplashAssetLoad.Handle.IsValid())
		{
			return;
		}

		GWaterSplashAssetLoad.RequestedPath = RequestedPath;
		GWaterSplashAssetLoad.Handle.Reset();
		const FSoftObjectPath AssetPath(RequestedPath);
		if (!AssetPath.IsValid())
		{
			return;
		}

		UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
		if (!AssetManager)
		{
			return;
		}

		GWaterSplashAssetLoad.Handle = AssetManager->GetStreamableManager().RequestAsyncLoad(
			AssetPath,
			FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority,
			/*bManageActiveHandle=*/false,
			/*bStartStalled=*/false,
			TEXT("MSWaterFootSplash"));
	}

	UNiagaraSystem* GetReadyWaterSplashSystem()
	{
		EnsureWaterSplashAssetRequested();
		const TSharedPtr<FStreamableHandle>& Handle = GWaterSplashAssetLoad.Handle;
		if (!Handle.IsValid() || !Handle->HasLoadCompleted() || Handle->WasCanceled())
		{
			return nullptr;
		}

		TArray<UNiagaraSystem*> LoadedSystems;
		Handle->GetLoadedAssets(LoadedSystems);
		return LoadedSystems.IsValidIndex(0) ? LoadedSystems[0] : nullptr;
	}
}

AMSTransientVfxActor::AMSTransientVfxActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 見た目専用。コリジョンは一切持たない(AMSVisualProjectileと同方針)。
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SetActorEnableCollision(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	SphereMesh = SphereFinder.Object;
	BaseMaterial = MaterialFinder.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WaterPlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	WaterPlaneMesh = WaterPlaneFinder.Object;
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> WaterFoamFinder(
		TEXT("/Game/WaterRealism0929/VFX/M_WaterFoam.M_WaterFoam"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> WaterSprayFinder(
		TEXT("/Game/WaterRealism0929/VFX/M_WaterSpray.M_WaterSpray"));
	WaterFoamMaterial = WaterFoamFinder.Get();
	WaterSprayMaterial = WaterSprayFinder.Get();
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeMaterialFinder(
		TEXT("/Game/Textures/VFX/M_ThrusterSmoke.M_ThrusterSmoke"));
	SmokeMaterial = SmokeMaterialFinder.Object;
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ExplosionFinder(
		TEXT("/Game/Textures/VFX/ExplosionSheet/M_ExplosionSheet.M_ExplosionSheet"));
	ExplosionSheetMaterial = ExplosionFinder.Get();
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture0(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_00.T_ThrusterSmoke_00"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture1(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_01.T_ThrusterSmoke_01"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture2(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_02.T_ThrusterSmoke_02"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture3(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_03.T_ThrusterSmoke_03"));
	SmokeTextures = {
		SmokeTexture0.Object, SmokeTexture1.Object, SmokeTexture2.Object, SmokeTexture3.Object};

	// GlowLight強度は既定0。各ファクトリがConfigureLightで妥当な値
	// (バズーカ爆発でもIntensity=300前後。過剰発光の再発防止)を設定する。
	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(Root);
	GlowLight->SetIntensity(0.0f);
	GlowLight->SetAttenuationRadius(150.0f);
	GlowLight->SetCastShadows(false);
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnAt(UWorld* World, const FVector& Location, const FRotator& Rotation)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	return World->SpawnActor<AMSTransientVfxActor>(StaticClass(), Location, Rotation, Params);
}

UStaticMeshComponent* AMSTransientVfxActor::AddCubeElement(const FVector& RelLocation, const FRotator& RelRotation,
	const FVector& StartScale, const FVector& EndScale, const FLinearColor& Color,
	const FVector& VelocityCmPerSec, const FVector& AccelerationCmPerSecSq)
{
	UStaticMeshComponent* Cube = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Cube->SetupAttachment(Root);
	Cube->SetStaticMesh(CubeMesh);
	Cube->SetMaterial(0, BaseMaterial);
	Cube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cube->SetGenerateOverlapEvents(false);
	Cube->SetCastShadow(false);
	Cube->SetRelativeLocation(RelLocation);
	Cube->SetRelativeRotation(RelRotation);
	Cube->SetRelativeScale3D(StartScale);
	Cube->RegisterComponent();
	if (UMaterialInstanceDynamic* Material = Cube->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		ElementMaterials.Add(Material);
	}
	else
	{
		ElementMaterials.Add(nullptr);
	}

	CubeElements.Add(Cube);
	ElementBaseLocations.Add(RelLocation);
	ElementVelocities.Add(VelocityCmPerSec);
	ElementAccelerations.Add(AccelerationCmPerSecSq);
	ElementStartScales.Add(StartScale);
	ElementEndScales.Add(EndScale);
	ElementColors.Add(Color);
	return Cube;
}

UStaticMeshComponent* AMSTransientVfxActor::AddSphereElement(const FVector& RelLocation,
	const FVector& StartScale, const FVector& EndScale, const FLinearColor& Color,
	const FVector& VelocityCmPerSec, const FVector& AccelerationCmPerSecSq,
	const FRotator& RelRotation)
{
	UStaticMeshComponent* Element = AddCubeElement(
		RelLocation, RelRotation, StartScale, EndScale, Color,
		VelocityCmPerSec, AccelerationCmPerSecSq);
	if (Element && SphereMesh)
	{
		Element->SetStaticMesh(SphereMesh);
	}
	return Element;
}

UMaterialBillboardComponent* AMSTransientVfxActor::AddSmokeBillboard(const FVector& RelLocation,
	const float StartSizeCm, const float EndSizeCm, const FLinearColor& Color, const float Opacity,
	const FVector& VelocityCmPerSec, const int32 TextureVariant,
	const float DelaySeconds, const float LifetimeSeconds)
{
	if (!SmokeMaterial || SmokeTextures.IsEmpty())
	{
		return nullptr;
	}

	UMaterialBillboardComponent* Billboard = NewObject<UMaterialBillboardComponent>(this, NAME_None, RF_Transient);
	Billboard->SetupAttachment(Root);
	Billboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Billboard->SetGenerateOverlapEvents(false);
	Billboard->SetCastShadow(false);
	Billboard->SetRelativeLocation(RelLocation);

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(SmokeMaterial, this);
	const int32 SafeTextureIndex = FMath::Abs(TextureVariant) % SmokeTextures.Num();
	Material->SetTextureParameterValue(TEXT("SmokeTexture"), SmokeTextures[SafeTextureIndex]);
	Material->SetVectorParameterValue(TEXT("SmokeTint"), Color);
	Material->SetScalarParameterValue(TEXT("SmokeOpacity"), DelaySeconds > 0.0f ? 0.0f : Opacity);
	Billboard->AddElement(Material, nullptr, false, StartSizeCm, StartSizeCm, nullptr);
	Billboard->RegisterComponent();

	SmokeBillboards.Add(Billboard);
	SmokeBaseLocations.Add(RelLocation);
	SmokeVelocities.Add(VelocityCmPerSec);
	SmokeStartSizes.Add(StartSizeCm);
	SmokeEndSizes.Add(EndSizeCm);
	SmokeOpacities.Add(Opacity);
	SmokeDelays.Add(DelaySeconds);
	SmokeLifetimes.Add(LifetimeSeconds);
	SmokeMaterials.Add(Material);
	return Billboard;
}

void AMSTransientVfxActor::AddExplosionFlame(const FName Name, const FVector& Location,
	const float SizeCm, const float DelaySeconds, const float DurationSeconds,
	const float Opacity, const int32 FirstFrame)
{
	if (!ExplosionSheetMaterial) return;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ExplosionSheetMaterial, this);
	if (!Material) return;
	UMaterialBillboardComponent* Fire = NewObject<UMaterialBillboardComponent>(this, Name, RF_Transient);
	Fire->SetupAttachment(Root);
	Fire->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Fire->SetGenerateOverlapEvents(false);
	Fire->SetCastShadow(false);
	// This sheet's ignition is at pixel Y~96 of 128, not the tile center.
	// Compensate its authored pivot; a centered quad buried the hot core below
	// terrain on ground hits. BaseSize is a half extent in MaterialBillboard.
	const FVector PivotLocation = Location + FVector(0, 0, SizeCm * 0.5f);
	Fire->SetRelativeLocation(PivotLocation);
	Material->SetScalarParameterValue(TEXT("FrameX"), FirstFrame % 8);
	Material->SetScalarParameterValue(TEXT("FrameY"), FirstFrame / 8);
	Material->SetScalarParameterValue(TEXT("Opacity"), DelaySeconds > 0.0f ? 0.0f : Opacity);
	Fire->AddElement(Material, nullptr, false, SizeCm, SizeCm, nullptr);
	AddInstanceComponent(Fire);
	Fire->RegisterComponent();
	ExplosionSheetInstances.Add(Material);
	ExplosionFlames.Add(Fire);
	ExplosionFlameOrigins.Add(PivotLocation);
	ExplosionFlameDelays.Add(DelaySeconds);
	ExplosionFlameDurations.Add(DurationSeconds);
	ExplosionFlameOpacities.Add(Opacity);
	ExplosionFlameFirstFrames.Add(FirstFrame);
}

bool AMSTransientVfxActor::AddPoseableMeshAfterimage(UPoseableMeshComponent& SourceMesh,
	const FTransform& SourceActorTransform, const FLinearColor& Color)
{
	if (!BaseMaterial || !SourceMesh.GetSkinnedAsset() || !SourceMesh.IsVisible())
	{
		return false;
	}

	UPoseableMeshComponent* Ghost = NewObject<UPoseableMeshComponent>(this, NAME_None, RF_Transient);
	Ghost->SetupAttachment(Root);
	Ghost->SetSkinnedAssetAndUpdate(SourceMesh.GetSkinnedAsset(), true);
	Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ghost->SetGenerateOverlapEvents(false);
	Ghost->SetCanEverAffectNavigation(false);
	Ghost->SetCastShadow(false);
	Ghost->SetRelativeTransform(SourceMesh.GetComponentTransform().GetRelativeTransform(SourceActorTransform));
	Ghost->RegisterComponent();

	// UPoseableMeshComponent is not a USkeletalMeshComponent, so the engine's
	// CopyPoseFromSkeletalComponent API cannot be used here. Freeze every bone at
	// the source's current component-space transform instead.
	for (int32 BoneIndex = 0; BoneIndex < SourceMesh.GetNumBones(); ++BoneIndex)
	{
		const FName BoneName = SourceMesh.GetBoneName(BoneIndex);
		Ghost->SetBoneTransformByName(BoneName,
			const_cast<UPoseableMeshComponent&>(SourceMesh).GetBoneTransformByName(
				BoneName, EBoneSpaces::ComponentSpace),
			EBoneSpaces::ComponentSpace);
	}

	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	const int32 MaterialCount = FMath::Max(SourceMesh.GetNumMaterials(), 1);
	Materials.Reserve(MaterialCount);
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Ghost->SetMaterial(MaterialIndex, Material);
		}
		Materials.Add(Material);
	}
	Ghost->SetVisibility(true, true);

	PoseableMeshElements.Add(Ghost);
	PoseableMeshStartScales.Add(Ghost->GetRelativeScale3D());
	PoseableMeshColors.Add(Color);
	PoseableMeshMaterials.Add(MoveTemp(Materials));
	return true;
}

void AMSTransientVfxActor::ConfigureLight(const FLinearColor& Color, const float Intensity, const float AttenuationRadius)
{
	if (!GlowLight)
	{
		return;
	}
	InitialLightIntensity = Intensity;
	GlowLight->SetLightColor(Color);
	GlowLight->SetIntensity(Intensity);
	GlowLight->SetAttenuationRadius(AttenuationRadius);
}

void AMSTransientVfxActor::SetEffectLifetime(const float Seconds)
{
	EffectLifetime = FMath::Max(0.02f, Seconds);
	SetLifeSpan(EffectLifetime + DestroyGraceSeconds);
}

void AMSTransientVfxActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedSeconds += DeltaSeconds;
	const float Alpha = FMath::Clamp(ElapsedSeconds / EffectLifetime, 0.0f, 1.0f);
	for (int32 Index = 0; Index < ExplosionSheetInstances.Num(); ++Index)
	{
		const float Age = ElapsedSeconds - ExplosionFlameDelays[Index];
		const float Progress = FMath::Clamp(Age / ExplosionFlameDurations[Index], 0.0f, 1.0f);
		const int32 FirstFrame = ExplosionFlameFirstFrames[Index];
		// The source spends half its frames on a small white ignition kernel.
		// Compress that phase into the first ~150 ms, then let turbulent fire decay.
		// Tails begin after the baked white kernel; play those fire frames evenly.
		const float AtlasProgress = FirstFrame >= 40 ? Progress
			: Progress < 0.18f ? Progress / 0.18f * 0.5f
			: 0.5f + (Progress - 0.18f) / 0.82f * 0.5f;
		const int32 Frame = FMath::Clamp(FirstFrame + FMath::FloorToInt(AtlasProgress * (64 - FirstFrame)), FirstFrame, 63);
		UMaterialInstanceDynamic* Material = ExplosionSheetInstances[Index];
		Material->SetScalarParameterValue(TEXT("FrameX"), Frame % 8);
		Material->SetScalarParameterValue(TEXT("FrameY"), Frame / 8);
		Material->SetScalarParameterValue(TEXT("Opacity"), Age < 0.0f ? 0.0f :
			ExplosionFlameOpacities[Index] * FMath::Clamp((1.0f - Progress) / 0.3f, 0.0f, 1.0f));
		ExplosionFlames[Index]->SetVisibility(Age >= 0.0f && Progress < 1.0f);
		// The atlas contains expansion; only buoyancy is added here, not a growing ball.
		const float Size = ExplosionFlames[Index]->Elements[0].BaseSizeY;
		ExplosionFlames[Index]->SetRelativeLocation(ExplosionFlameOrigins[Index] +
			FVector(0.0f, 0.0f, Size * 0.14f * Progress));
	}

	for (int32 Index = 0; Index < CubeElements.Num(); ++Index)
	{
		UStaticMeshComponent* Cube = CubeElements[Index];
		if (!Cube)
		{
			continue;
		}
		Cube->SetRelativeLocation(
			ElementBaseLocations[Index] + ElementVelocities[Index] * ElapsedSeconds +
			0.5f * ElementAccelerations[Index] * FMath::Square(ElapsedSeconds));
		Cube->SetRelativeScale3D(FMath::Lerp(ElementStartScales[Index], ElementEndScales[Index], Alpha));
		if (bFadeOut && ElementMaterials.IsValidIndex(Index) && ElementMaterials[Index])
		{
			FLinearColor FadedColor = ElementColors[Index];
			FadedColor.A *= FMath::Square(1.0f - Alpha);
			ElementMaterials[Index]->SetVectorParameterValue(TEXT("Color"), FadedColor);
		}
	}
	for (int32 Index = 0; Index < SmokeBillboards.Num(); ++Index)
	{
		UMaterialBillboardComponent* Billboard = SmokeBillboards[Index];
		if (!Billboard)
		{
			continue;
		}
		const float Age = FMath::Max(0.0f, ElapsedSeconds - SmokeDelays[Index]);
		const bool bTimed = SmokeLifetimes[Index] > 0.0f;
		const float SmokeAlpha = bTimed ? FMath::Clamp(Age / SmokeLifetimes[Index], 0.0f, 1.0f) : Alpha;
		Billboard->SetVisibility(ElapsedSeconds >= SmokeDelays[Index] && SmokeAlpha < 1.0f);
		Billboard->SetRelativeLocation(SmokeBaseLocations[Index] + SmokeVelocities[Index] * Age);
		const float StartSize = FMath::Max(SmokeStartSizes[Index], UE_KINDA_SMALL_NUMBER);
		const float Scale = FMath::Lerp(1.0f, SmokeEndSizes[Index] / StartSize, SmokeAlpha);
		if (bTimed && !Billboard->Elements.IsEmpty())
		{
			// MaterialBillboard builds world-size camera axes; component scale is
			// canceled by its inverse transform. Change the actual sprite dimensions.
			FMaterialSpriteElement& Sprite = Billboard->Elements[0];
			const float Aspect = Sprite.BaseSizeY / FMath::Max(Sprite.BaseSizeX, UE_KINDA_SMALL_NUMBER);
			Sprite.BaseSizeX = StartSize * Scale;
			Sprite.BaseSizeY = Sprite.BaseSizeX * Aspect;
			Billboard->MarkRenderStateDirty();
		}
		else Billboard->SetRelativeScale3D(FVector(Scale));
		if (bFadeOut && SmokeMaterials.IsValidIndex(Index) && SmokeMaterials[Index])
		{
			const float Fade = bTimed
				? FMath::Clamp(Age / 0.12f, 0.0f, 1.0f) * FMath::Square(1.0f - SmokeAlpha)
				: FMath::Square(1.0f - Alpha);
			SmokeMaterials[Index]->SetScalarParameterValue(TEXT("SmokeOpacity"), SmokeOpacities[Index] * Fade);
		}
	}
	for (int32 Index = 0; Index < PoseableMeshElements.Num(); ++Index)
	{
		UPoseableMeshComponent* Ghost = PoseableMeshElements[Index];
		if (!Ghost)
		{
			continue;
		}
		Ghost->SetRelativeScale3D(PoseableMeshStartScales[Index] * FMath::Lerp(1.0f, 0.6f, Alpha));
		if (bFadeOut && PoseableMeshMaterials.IsValidIndex(Index))
		{
			FLinearColor FadedColor = PoseableMeshColors[Index];
			FadedColor.A *= FMath::Square(1.0f - Alpha);
			for (UMaterialInstanceDynamic* Material : PoseableMeshMaterials[Index])
			{
				if (Material)
				{
					Material->SetVectorParameterValue(TEXT("Color"), FadedColor);
				}
			}
		}
	}
	if (GlowLight)
	{
		GlowLight->SetIntensity(bFadeOut
			? FMath::Lerp(InitialLightIntensity, 0.0f, Alpha) : InitialLightIntensity);
	}

	if (Alpha >= 1.0f)
	{
		// 寿命到達: 即非表示にしてTick停止。破棄はSetLifeSpanが保証する。
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
	}
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnTracer(UWorld* World, const FVector& Start, const FVector& End,
	const FLinearColor& Color, const float WidthCm, const float LifetimeSeconds)
{
	const FVector Delta = End - Start;
	const float LengthCm = Delta.Size();
	if (LengthCm <= UE_KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}
	AMSTransientVfxActor* Vfx = SpawnAt(World, Start + Delta * 0.5f, Delta.GetSafeNormal().Rotation());
	if (!Vfx)
	{
		return nullptr;
	}

	// Cubeメッシュは一辺100cm。X=弾道全長、Y/Z=トレーサー幅にスケールし、寿命で幅方向に痩せて消える。
	const float LengthScale = LengthCm / 100.0f;
	const float HaloScale = FMath::Max(WidthCm, 2.0f * MSGO::MSScale) / 100.0f;
	const float CoreScale = FMath::Max(WidthCm * 0.35f, 1.0f * MSGO::MSScale) / 100.0f;
	Vfx->AddCubeElement(FVector::ZeroVector, FRotator::ZeroRotator,
		FVector(LengthScale, HaloScale, HaloScale), FVector(LengthScale, 0.001f, 0.001f), Color);
	// 白コア+広い色付きハローで、ビーム/マシンガンどちらの短寿命トレーサーも視認しやすくする。
	Vfx->AddCubeElement(FVector::ZeroVector, FRotator::ZeroRotator,
		FVector(LengthScale, CoreScale, CoreScale), FVector(LengthScale, 0.001f, 0.001f), FLinearColor::White);
	Vfx->ConfigureLight(Color, 200.0f, 160.0f * MSGO::MSScale);
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnMuzzleFlash(UWorld* World, const FVector& Location, const FVector& Forward)
{
	AMSTransientVfxActor* Vfx = SpawnAt(World, Location, Forward.GetSafeNormal().Rotation());
	if (!Vfx)
	{
		return nullptr;
	}

	const FLinearColor FlashColor(1.0f, 0.92f, 0.47f);
	// 銃口の発光コア。
	Vfx->AddCubeElement(FVector::ZeroVector, FRotator::ZeroRotator,
		FVector(0.16f) * MSGO::MSScale, FVector(0.02f) * MSGO::MSScale, FLinearColor::White);
	// 前方に伸びる主ペタルと、斜め上下の副ペタル(旧DrawDebugLineの3本構成を踏襲)。
	Vfx->AddCubeElement(FVector(19.0f, 0.0f, 0.0f) * MSGO::MSScale, FRotator::ZeroRotator,
		FVector(0.38f, 0.05f, 0.05f) * MSGO::MSScale,
		FVector(0.05f, 0.008f, 0.008f) * MSGO::MSScale, FLinearColor::White);
	Vfx->AddCubeElement(FVector(12.0f, 6.0f, 4.0f) * MSGO::MSScale, FRotator(25.0f, 18.0f, 0.0f),
		FVector(0.25f, 0.03f, 0.03f) * MSGO::MSScale,
		FVector(0.03f, 0.005f, 0.005f) * MSGO::MSScale, FlashColor);
	Vfx->AddCubeElement(FVector(12.0f, -6.0f, -4.0f) * MSGO::MSScale, FRotator(-25.0f, -18.0f, 0.0f),
		FVector(0.25f, 0.03f, 0.03f) * MSGO::MSScale,
		FVector(0.03f, 0.005f, 0.005f) * MSGO::MSScale, FlashColor);
	Vfx->ConfigureLight(FlashColor, 300.0f, 150.0f * MSGO::MSScale);
	Vfx->SetEffectLifetime(0.08f);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnImpactSparks(UWorld* World, const FVector& Location,
	const FLinearColor& Color, const bool bStrong, const float LifetimeSeconds,
	const EMSImpactVisualStyle Style)
{
	int32 ActiveImpactCount = 0;
	for (TActorIterator<AMSTransientVfxActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("MSVfx.Impact")) && ++ActiveImpactCount >= 12)
		{
			return nullptr;
		}
	}
	AMSTransientVfxActor* Vfx = SpawnAt(World, Location, FRotator::ZeroRotator);
	if (!Vfx)
	{
		return nullptr;
	}
	Vfx->Tags.Add(TEXT("MSVfx.Impact"));
	const bool bExplosive = Style == EMSImpactVisualStyle::Explosive;
	const bool bBeam = Style == EMSImpactVisualStyle::Beam;
	const float StyleScale = bExplosive ? 1.55f : (bBeam ? 1.2f : 1.0f);
	Vfx->AddSphereElement(FVector::ZeroVector,
		FVector((bStrong ? 0.38f : 0.28f) * StyleScale) * MSGO::MSScale,
		FVector(0.02f) * MSGO::MSScale, Color);

	// A solid white cube at a unit impact reads as a missing-texture box.
	// Keep the same brief core size, with a round flash rather than square faces.
	Vfx->AddSphereElement(FVector::ZeroVector,
		FVector(bStrong ? 0.18f : 0.12f) * MSGO::MSScale,
		FVector(0.02f) * MSGO::MSScale, FLinearColor::White);
	// 放射状スパーク(旧DrawDebugLineの8方向構成を踏襲): 外向きに飛散しながら縮んで消える。
	const int32 SparkCount = bExplosive ? 24 : (bStrong ? 20 : 16);
	const float SparkSpeed = (bStrong ? 470.0f : 350.0f) * StyleScale * MSGO::MSScale;
	for (int32 Index = 0; Index < SparkCount; ++Index)
	{
		const float Angle = UE_TWO_PI * static_cast<float>(Index) / static_cast<float>(SparkCount);
		const float Vertical = bBeam ? 0.15f + 0.5f * (Index % 3) : ((Index & 1) ? 0.65f : -0.15f);
		const FVector Ray = FVector(FMath::Cos(Angle), FMath::Sin(Angle), Vertical).GetSafeNormal();
		Vfx->AddCubeElement(Ray * 8.0f * MSGO::MSScale, Ray.Rotation(),
			FVector((bStrong ? 0.38f : 0.29f) * StyleScale, 0.035f, 0.035f) * MSGO::MSScale,
			FVector(0.02f, 0.004f, 0.004f) * MSGO::MSScale,
			Color, Ray * SparkSpeed);
	}
	const int32 FragmentCount = bExplosive ? 10 : 6;
	for (int32 Index = 0; Index < FragmentCount; ++Index)
	{
		const float Angle = UE_TWO_PI * (static_cast<float>(Index) + 0.35f) / FragmentCount;
		const FVector Ray(FMath::Cos(Angle), FMath::Sin(Angle), 0.25f + 0.12f * (Index % 3));
		Vfx->AddCubeElement(Ray * 6.0f * MSGO::MSScale, Ray.Rotation(),
			FVector(0.11f, 0.055f, 0.035f) * MSGO::MSScale,
			FVector(0.035f, 0.02f, 0.012f) * MSGO::MSScale,
			FLinearColor(0.48f, 0.5f, 0.54f, 1.0f),
			Ray.GetSafeNormal() * SparkSpeed * 0.46f,
			FVector(0.0f, 0.0f, -980.0f * MSGO::MSScale));
	}
	Vfx->ConfigureLight(Color, bExplosive ? 2200.0f : 1500.0f,
		360.0f * StyleScale * MSGO::MSScale);
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

void AMSTransientVfxActor::PreloadWaterFootSplash(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (CVarWaterSplashNiagaraEnabled.GetValueOnGameThread()) EnsureWaterSplashAssetRequested();
}

UStaticMeshComponent* AMSTransientVfxActor::AddWaterSheet(const FVector& RelLocation,
	const FRotator& Rotation, const FVector& StartScale, const FVector& EndScale,
	UMaterialInterface* Material, const float Opacity, const FVector& Velocity,
	const FVector& Acceleration)
{
	if (!WaterPlaneMesh || !Material) return nullptr;
	const FLinearColor Tint(0.84f, 0.94f, 0.98f, Opacity);
	UStaticMeshComponent* Sheet = AddCubeElement(RelLocation, Rotation, StartScale, EndScale,
		Tint, Velocity, Acceleration);
	Sheet->SetStaticMesh(WaterPlaneMesh);
	Sheet->SetMaterial(0, Material);
	UMaterialInstanceDynamic* Instance = Sheet->CreateAndSetMaterialInstanceDynamic(0);
	if (Instance) Instance->SetVectorParameterValue(TEXT("Color"), Tint);
	ElementMaterials.Last() = Instance;
	Sheet->ComponentTags.Add(TEXT("MSVfx.WaterSheet"));
	Sheet->SetReceivesDecals(false);
	return Sheet;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnWaterFootSplash(UWorld* World,
	const FVector& Location, const float IntensityScale, const bool bEntrySplash,
	const float LifetimeSeconds)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return nullptr;
	// Bounded cosmetic budget. Nearby water interactions take priority over distant bots.
	APlayerController* LocalController = World->GetFirstPlayerController();
	const bool bLocalView = LocalController && LocalController->IsLocalController();
	const FVector ViewLocation = bLocalView && LocalController->PlayerCameraManager
		? LocalController->PlayerCameraManager->GetCameraLocation() : Location;
	const float NewDistanceSq = FVector::DistSquared(ViewLocation, Location);
	if (bLocalView && NewDistanceSq > FMath::Square(60000.f)) return nullptr;
	int32 ActiveWaterCount = 0;
	AMSTransientVfxActor* Farthest = nullptr;
	float FarthestDistanceSq = -1.f;
	for (TActorIterator<AMSTransientVfxActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("MSVfx.WaterFoot")) || It->IsActorBeingDestroyed()) continue;
		++ActiveWaterCount;
		const float DistanceSq = FVector::DistSquared(ViewLocation, It->GetActorLocation());
		if (DistanceSq > FarthestDistanceSq) { Farthest = *It; FarthestDistanceSq = DistanceSq; }
	}
	if (ActiveWaterCount >= 24)
	{
		if (!bLocalView || !Farthest || FarthestDistanceSq <= NewDistanceSq) return nullptr;
		Farthest->Destroy();
	}
	AMSTransientVfxActor* Vfx = SpawnAt(World, Location, FRotator::ZeroRotator);
	if (!Vfx) return nullptr;
	Vfx->Tags.Add(TEXT("MSVfx.WaterFoot"));
	Vfx->Tags.Add(bEntrySplash ? TEXT("MSVfx.WaterEntry") : TEXT("MSVfx.WaterSurface"));
	if (bEntrySplash && CVarWaterSplashNiagaraEnabled.GetValueOnGameThread())
	{
		if (UNiagaraSystem* System = GetReadyWaterSplashSystem())
		{
			Vfx->WaterNiagara = NewObject<UNiagaraComponent>(Vfx);
			Vfx->WaterNiagara->SetupAttachment(Vfx->Root);
			Vfx->WaterNiagara->SetAsset(System);
			const float DetailScale = CVarWaterSplashNiagaraScale.GetValueOnGameThread();
			Vfx->WaterNiagara->SetRelativeScale3D(FVector(DetailScale, DetailScale,
				DetailScale * CVarWaterSplashNiagaraVertical.GetValueOnGameThread()));
			Vfx->WaterNiagara->RegisterComponent();
			Vfx->WaterNiagara->Activate(true);
		}
	}
	const float Scale = FMath::Clamp(IntensityScale, 0.75f, 1.8f);
	const float Phase = FMath::FRandRange(0.f, 360.f);
	if (Vfx->WaterFoamMaterial && Vfx->WaterSprayMaterial)
	{
		// Horizontal sheets stay on the water plane; layered, irregular alpha breaks the
		// outline. They expand slowly, instead of rendering a solid mound of spheres.
		for (int32 I = 0; I < 3; ++I)
		{
			const float Size = (4.5f + I * 1.25f) * Scale;
			Vfx->AddWaterSheet(FVector(0, 0, 10.f + I * 4.f), FRotator(0, Phase + I * 117.f, 0),
				FVector(Size, Size * (0.90f + I * 0.06f), 1.f), FVector(Size * 1.7f, Size * 1.7f, 1.f),
				Vfx->WaterFoamMaterial, bEntrySplash ? 0.68f : 0.42f, FVector::ZeroVector, FVector::ZeroVector);
		}
		const int32 SprayCount = bEntrySplash ? 10 : 8;
		for (int32 I = 0; I < SprayCount; ++I)
		{
			const float Angle = FMath::DegreesToRadians(Phase) + UE_TWO_PI * (I + 0.17f) / SprayCount;
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			const float Variation = FMath::FRandRange(0.8f, 1.2f);
			const float Height = (bEntrySplash ? 155.f : 60.f) * Scale * Variation;
			const float Size = (bEntrySplash ? 3.2f : 1.35f) * Scale * Variation;
			Vfx->AddWaterSheet(Radial * (190.f * Scale) + FVector(0, 0, Height),
				FRotator(62.f, FMath::RadiansToDegrees(Angle), 0),
				FVector(Size, Size * 0.9f, 1), FVector(Size * 1.6f, Size * 1.3f, 1),
				Vfx->WaterSprayMaterial, bEntrySplash ? 0.72f : 0.42f,
				Radial * ((bEntrySplash ? 340.f : 100.f) * Scale) + FVector(0, 0, bEntrySplash ? 170.f : 65.f),
				FVector(0, 0, bEntrySplash ? -440.f : -170.f));
		}
		if (bEntrySplash)
		{
			for (int32 I = 0; I < 8; ++I)
			{
				const float Angle = FMath::DegreesToRadians(Phase + I * 45.f + 13.f);
				const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
				Vfx->AddWaterSheet(Radial * (240.f * Scale) + FVector(0, 0, 235.f * Scale),
					FRotator(72.f, FMath::RadiansToDegrees(Angle), 0), FVector(0.8f, 1.5f, 1) * Scale,
					FVector(0.4f, 0.65f, 1) * Scale, Vfx->WaterSprayMaterial, 0.65f,
					Radial * (430.f * Scale) + FVector(0, 0, 250.f), FVector(0, 0, -650.f));
			}
		}
	}
	else
	{
		// Missing content stays soft/translucent; never fall back to opaque white blocks.
		for (int32 I = 0; I < 6; ++I)
		{
			const float Angle = UE_TWO_PI * I / 6.f;
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			Vfx->AddSmokeBillboard(Radial * 180.f + FVector(0, 0, bEntrySplash ? 110.f : 25.f),
				100.f * Scale, 280.f * Scale, FLinearColor(0.84f, 0.94f, 0.98f), 0.30f,
				Radial * 90.f, I % 3, 0.f, LifetimeSeconds);
		}
	}
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnBoostAfterimage(UWorld* World, const AActor* SourceActor,
	const FLinearColor& Color, const float LifetimeSeconds)
{
	if (!World || !SourceActor)
	{
		return nullptr;
	}
	AMSTransientVfxActor* Vfx = SpawnAt(World, SourceActor->GetActorLocation(), SourceActor->GetActorRotation());
	if (!Vfx)
	{
		return nullptr;
	}
	Vfx->Tags.Add(TEXT("MSVfx.BoostAfterimage"));

	// VisualMesh機体は可視Cubeリグではなく、表示中のPoseableMeshを現在ポーズごと複製する。
	if (const UMSMeshVisualComponent* MeshVisual =
		SourceActor->FindComponentByClass<UMSMeshVisualComponent>())
	{
		UPoseableMeshComponent* PoseableMesh = MeshVisual->GetPoseableMesh();
		if (MeshVisual->IsMeshVisualActive() && PoseableMesh && PoseableMesh->GetSkinnedAsset()
			&& PoseableMesh->IsVisible())
		{
			if (!Vfx->AddPoseableMeshAfterimage(*PoseableMesh, SourceActor->GetActorTransform(), Color))
			{
				Vfx->Destroy();
				return nullptr;
			}
			Vfx->ConfigureLight(Color, 0.0f, 100.0f);
			Vfx->SetEffectLifetime(LifetimeSeconds);
			return Vfx;
		}
	}

	// 機体を構成する可視StaticMesh(Cubeリグの各部位+装備)のワールド姿勢をアクター相対へ
	// 変換して複製する。寿命で60%まで縮みながら消える=フェードするゴースト。
	const FTransform ActorTransform = SourceActor->GetActorTransform();
	TInlineComponentArray<UStaticMeshComponent*> MeshComponents(SourceActor);
	int32 CopiedCount = 0;
	for (const UStaticMeshComponent* Mesh : MeshComponents)
	{
		if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->IsVisible())
		{
			continue;
		}
		const FTransform Relative = Mesh->GetComponentTransform().GetRelativeTransform(ActorTransform);
		UStaticMeshComponent* Ghost = Vfx->AddCubeElement(Relative.GetLocation(), Relative.Rotator(),
			Relative.GetScale3D(), Relative.GetScale3D() * 0.6f, Color);
		if (Ghost)
		{
			Ghost->SetStaticMesh(Mesh->GetStaticMesh());
			++CopiedCount;
			if (CopiedCount >= 8)
			{
				break;
			}
		}
	}
	if (CopiedCount == 0)
	{
		Vfx->Destroy();
		return nullptr;
	}
	// 残像は高頻度で残るためポイントライトは点灯しない(過剰発光/負荷の防止)。
	Vfx->ConfigureLight(Color, 0.0f, 100.0f);
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnThrusterSmokePuff(UWorld* World,
	const FVector& Location, const FVector& Backward, const FVector& Up,
	const int32 Variant, const float LifetimeSeconds)
{
	const FVector SafeBackward = Backward.GetSafeNormal(SMALL_NUMBER, -FVector::ForwardVector);
	const FVector SafeUp = Up.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(SafeBackward, SafeUp).Rotator();
	AMSTransientVfxActor* Vfx = SpawnAt(World, Location, Rotation);
	if (!Vfx)
	{
		return nullptr;
	}
	Vfx->Tags.Add(TEXT("MSVfx.ThrusterSmoke"));

	const float Jitter = static_cast<float>((Variant % 3) - 1);
	const FLinearColor Tints[] = {
		FLinearColor(1.0f, 0.72f, 0.45f),
		FLinearColor(0.82f, 0.84f, 0.86f),
		FLinearColor(0.58f, 0.61f, 0.64f)};
	const float Opacities[] = {0.24f, 0.19f, 0.15f};
	const int32 TintIndex = FMath::Abs(Variant) % static_cast<int32>(UE_ARRAY_COUNT(Tints));
	Vfx->AddSmokeBillboard(FVector(7.0f, Jitter * 3.0f, 1.0f) * MSGO::MSScale,
		22.0f * MSGO::MSScale, 62.0f * MSGO::MSScale, Tints[TintIndex], Opacities[TintIndex],
		FVector(52.0f, Jitter * 8.0f, 27.0f) * MSGO::MSScale, Variant);
	Vfx->ConfigureLight(FLinearColor::White, 0.0f, 80.0f * MSGO::MSScale);
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnRecallRing(
	UWorld* World, const FVector& Location, const float LifetimeSeconds)
{
	AMSTransientVfxActor* Vfx = SpawnAt(World, Location, FRotator::ZeroRotator);
	if (!Vfx)
	{
		return nullptr;
	}
	Vfx->Tags.Add(TEXT("MSVfx.EmergencyRecall"));
	Vfx->bFadeOut = false;

	constexpr int32 ElementCount = 20;
	constexpr float RadiusCm = 250.0f * MSGO::MSScale;
	const FLinearColor RecallColor(1.0f, 0.45f, 0.05f);
	const FVector ElementScale = FVector(0.06f, 0.55f, 0.04f) * MSGO::MSScale;
	for (int32 Index = 0; Index < ElementCount; ++Index)
	{
		const float Angle = UE_TWO_PI * static_cast<float>(Index) / static_cast<float>(ElementCount);
		const FVector Position(RadiusCm * FMath::Cos(Angle), RadiusCm * FMath::Sin(Angle),
			4.0f * MSGO::MSScale);
		Vfx->AddCubeElement(Position, FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f),
			ElementScale, ElementScale, RecallColor);
	}
	Vfx->ConfigureLight(RecallColor, 900.0f, 420.0f * MSGO::MSScale);
	Vfx->SetEffectLifetime(LifetimeSeconds);
	return Vfx;
}

AMSTransientVfxActor* AMSTransientVfxActor::SpawnExplosion(UWorld* World, const FVector& Location,
	const float RadiusCm, const float LifetimeSeconds)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return nullptr;
	AMSTransientVfxActor* Flash = SpawnAt(World, Location, FRotator::ZeroRotator);
	if (!Flash) return nullptr;
	Flash->Tags.Add(TEXT("MSVfx.ExplosionFlash"));
	const float Radius = FMath::Max(RadiusCm, 30.0f) * MSGO::MSScale;
	const float FlashDuration = FMath::Clamp(LifetimeSeconds, 0.02f, 0.06f);
	// Even the first frame uses the imported silhouette. An opaque orange sphere
	// previously hid this texture at impact, making the whole effect read as a ball.
	Flash->AddExplosionFlame(TEXT("ExplosionFlash"), FVector::ZeroVector,
		Radius * 0.52f, 0.0f, FlashDuration, 1.0f, 5);
	Flash->ConfigureLight(FLinearColor(1.0f, 0.65f, 0.3f), 300.0f, Radius * 2.0f);
	Flash->SetEffectLifetime(FlashDuration);

	AMSTransientVfxActor* Tail = SpawnAt(World, Location, FRotator::ZeroRotator);
	if (!Tail) return Flash;
	Tail->Tags.Add(TEXT("MSVfx.ExplosionTail"));
	bool bDistant = false;
	float NearestViewSq = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get(); PC && PC->IsLocalController())
		{
			FVector ViewLocation; FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			NearestViewSq = FMath::Min(NearestViewSq, FVector::DistSquared(ViewLocation, Location));
		}
	}
	// No local controller (automation/pre-login) uses the full presentation.
	if (NearestViewSq < TNumericLimits<float>::Max())
		bDistant = NearestViewSq > FMath::Square(30000.0f);
	Tail->Tags.Add(bDistant ? TEXT("MSVfx.ExplosionDistant") : TEXT("MSVfx.ExplosionNear"));
	// The 128 px source cells contain a baked white core, not recoverable HDR detail.
	// Keep that flash brief; smaller late-frame tails reduce additive saturation.
	// Smoke retains the blast envelope. This does not pretend to upscale the atlas.
	Tail->AddExplosionFlame(TEXT("ExplosionFire"), FVector(0, 0, Radius * 0.12f),
		Radius * 0.665f, 0.0f, 0.82f, 0.45f, 44);
	if (!bDistant)
	{
		Tail->AddExplosionFlame(TEXT("ExplosionFireLobeA"), FVector(Radius * 0.29f, -Radius * 0.13f, Radius * 0.23f),
			Radius * 0.406f, 0.045f, 0.64f, 0.34f, 46);
		Tail->AddExplosionFlame(TEXT("ExplosionFireLobeB"), FVector(-Radius * 0.2f, Radius * 0.17f, Radius * 0.4f),
			Radius * 0.329f, 0.1f, 0.72f, 0.30f, 48);
	}
	// Dark opaque-looking alpha smoke replaces the former single weak puff.
	// Delayed rise lets the hot phase read clearly before smoke takes over.
	const int32 SmokeCount = bDistant ? 2 : 4;
	for (int32 Index = 0; Index < SmokeCount; ++Index)
	{
		const float Angle = Index * 2.39996f + 0.4f;
		const FVector Side(FMath::Cos(Angle), FMath::Sin(Angle), 0);
		UMaterialBillboardComponent* Smoke = Tail->AddSmokeBillboard(
			Side * Radius * 0.18f + FVector(0, 0, Radius * (0.15f + Index * 0.1f)),
			Radius * 0.28f, Radius * (0.7f + Index * 0.04f),
			FLinearColor(0.11f + Index * 0.018f, 0.105f + Index * 0.018f, 0.095f + Index * 0.02f),
			0.72f, Side * Radius * 0.12f + FVector(0, 0, Radius * (0.25f + Index * 0.045f)),
			Index, 0.11f + Index * 0.045f, 1.45f);
		if (Smoke) Smoke->ComponentTags.Add(TEXT("ExplosionSmoke"));
	}
	// Dust is tied to an actual nearby surface; mid-air explosions don't leave a
	// floating ground ring. WorldStatic avoids grounding the dust on a mobile suit.
	FHitResult GroundHit;
	FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic);
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(ExplosionPresentationGround), false);
	if (!bDistant && World->LineTraceSingleByObjectType(GroundHit,
		Location + FVector(0, 0, 10), Location - FVector(0, 0, Radius * 0.45f), GroundObjects, GroundQuery)
		&& GroundHit.ImpactNormal.Z > 0.5f)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Angle = Index * UE_TWO_PI / 3.0f + 0.3f;
			const FVector Side(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			UMaterialBillboardComponent* Dust = Tail->AddSmokeBillboard(
				GroundHit.ImpactPoint - Location + FVector(0, 0, Radius * 0.08f) + Side * Radius * 0.13f,
				Radius * 0.36f, Radius * 0.95f, FLinearColor(0.27f, 0.245f, 0.2f),
				0.34f, Side * Radius * 0.5f, Index + 1, 0.025f, 0.7f);
			if (Dust)
			{
				Dust->ComponentTags.Add(TEXT("ExplosionGroundDust"));
				Dust->Elements[0].BaseSizeY *= 0.26f;
				Dust->MarkRenderStateDirty();
			}
		}
	}
	// Small irregular ballistic fragments live beyond the 60 ms flash. They are
	// secondary accents, never the radius-sized geometric body of the explosion.
	if (!bDistant)
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const float Angle = Index * 2.39996f;
			const FVector Direction = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.4f + (Index % 3) * 0.28f).GetSafeNormal();
			const float Width = Radius / 100.0f * (0.009f + Index * 0.001f);
			Tail->AddCubeElement(Direction * Radius * 0.15f, Direction.Rotation(),
				FVector(Width * 3.0f, Width, Width), FVector::ZeroVector,
				FLinearColor(0.28f, 0.19f, 0.075f), Direction * Radius * (0.65f + Index * 0.09f),
				FVector(0, 0, -Radius * 1.25f));
		}
	}
	Tail->SetEffectLifetime(1.8f);
	return Flash;
}
FVector AMSTransientVfxActor::CalculateExplosionOuterInitialScale(const float RadiusCm)
{
	return CalculateExplosionOuterMaximumScale(RadiusCm) * 0.25f;
}

FVector AMSTransientVfxActor::CalculateExplosionOuterMaximumScale(const float RadiusCm)
{
	return FVector(FMath::Max(RadiusCm, 30.0f) * MSGO::MSScale * 2.0f / 100.0f);
}
