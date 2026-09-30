// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/Cues/MSGameplayCue_Beam.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponVisualComponent.h"

AMSGameplayCue_Beam::AMSGameplayCue_Beam()
{
	PrimaryActorTick.bCanEverTick = true;
	GameplayCueTag = MSTags::Cue_Weapon_BeamLoop;
	GameplayCueName = GameplayCueTag.GetTagName();

	// ビームは入力保持中ずっとアクティブなので、明示的なOnRemove(WhileActiveの停止)で消える。
	// タイムアウトによる自動破棄は行わない。
	bAutoDestroyOnRemove = true;
	AutoDestroyDelay = 0.0f;

	// VisualRoot/BeamBody/BeamCore/GlowLightはEnsureVisualComponentsCreated()で初回Tick時に
	// 遅延生成する(コンストラクタ時点ではまだ武器データ・幅が不明なため)。ただし
	// ConstructorHelpers::FObjectFinderはコンストラクタ内でしか使えないため、
	// メッシュ/マテリアルの参照だけはここで取得してキャッシュしておく。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CachedCubeMesh = CubeFinder.Object;
	CachedCubeMaterial = MaterialFinder.Object;
}

const UMSRangedWeaponData* AMSGameplayCue_Beam::ResolveWeaponData(const FGameplayCueParameters& Parameters)
{
	return Cast<UMSRangedWeaponData>(Parameters.SourceObject.Get());
}

float AMSGameplayCue_Beam::ResolveBeamWidthMeters(const UMSRangedWeaponData* Weapon)
{
	if (const UMSContinuousBeamWeaponData* BeamWeapon = Cast<UMSContinuousBeamWeaponData>(Weapon))
	{
		if (BeamWeapon->BeamRadius > 0.0f)
		{
			return BeamWeapon->BeamRadius * 2.0f;
		}
	}
	if (Weapon)
	{
		return Weapon->TracerWidth;
	}
	return GetDefault<UMSRangedWeaponData>()->TracerWidth;
}

bool AMSGameplayCue_Beam::OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	WeaponData = ResolveWeaponData(Parameters);

	// 始点/終点の初期値はActivate時点で分かる情報から仮設定する。実際の値は
	// UpdateBeamEndpoints経由でGA_Fire_ContinuousBeamのtick毎に更新される(P4-13で配線)。
	bool bResolvedStart = false;
	if (const FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Parameters.EffectContext);
		MSContext && MSContext->HasAttackerLocation())
	{
		BeamStart = MSContext->GetAttackerLocation();
		bResolvedStart = true;
	}
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Parameters.Instigator.Get()))
	{
		if (const UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent())
		{
			BeamStart = Visual->GetMuzzleWorldLocation();
			bResolvedStart = true;
		}
	}
	if (!bResolvedStart)
	{
		if (const AActor* InstigatorActor = Parameters.Instigator.Get())
		{
			BeamStart = InstigatorActor->GetActorLocation();
		}
	}
	BeamEnd = FVector(Parameters.Location);
	bHasBeamPoints = !BeamStart.Equals(BeamEnd);

	return true;
}

bool AMSGameplayCue_Beam::WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	// ループ中も武器データ・終点の再解決を許す(Join-in-progress/多重呼び出しに対応)。
	if (!WeaponData.IsValid())
	{
		WeaponData = ResolveWeaponData(Parameters);
	}
	return true;
}

bool AMSGameplayCue_Beam::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	bHasBeamPoints = false;
	if (BeamBody)
	{
		BeamBody->SetVisibility(false);
	}
	if (BeamCore)
	{
		BeamCore->SetVisibility(false);
	}
	if (GlowLight)
	{
		GlowLight->SetIntensity(0.0f);
	}
	return true;
}

void AMSGameplayCue_Beam::EnsureVisualComponentsCreated()
{
	if (bVisualComponentsCreated)
	{
		return;
	}
	bVisualComponentsCreated = true;

	// AGameplayCueNotify_Actorはデフォルトでルートコンポーネントを持たないため明示的に生成する
	// (AMSVisualProjectileと同じRoot+Static Mesh子コンポーネント構成)。
	VisualRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	VisualRoot->RegisterComponent();
	SetRootComponent(VisualRoot);

	BeamBody = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	BeamBody->SetupAttachment(VisualRoot);
	BeamBody->SetStaticMesh(CachedCubeMesh);
	BeamBody->SetMaterial(0, CachedCubeMaterial);
	BeamBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamBody->SetGenerateOverlapEvents(false);
	BeamBody->SetCastShadow(false);
	BeamBody->RegisterComponent();

	BeamCore = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	BeamCore->SetupAttachment(VisualRoot);
	BeamCore->SetStaticMesh(CachedCubeMesh);
	BeamCore->SetMaterial(0, CachedCubeMaterial);
	BeamCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamCore->SetGenerateOverlapEvents(false);
	BeamCore->SetCastShadow(false);
	BeamCore->RegisterComponent();
	if (UMaterialInstanceDynamic* CoreMaterial = BeamCore->CreateAndSetMaterialInstanceDynamic(0))
	{
		CoreMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
	}

	GlowLight = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	GlowLight->SetupAttachment(VisualRoot);
	GlowLight->SetIntensity(0.0f);
	GlowLight->SetAttenuationRadius(150.0f * MSGO::MSScale);
	GlowLight->SetCastShadows(false);
	GlowLight->RegisterComponent();
}

void AMSGameplayCue_Beam::UpdateBeamEndpoints(const FVector& InStart, const FVector& InEnd)
{
	BeamStart = InStart;
	BeamEnd = InEnd;
	bHasBeamPoints = true;
}

void AMSGameplayCue_Beam::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bHasBeamPoints)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	EnsureVisualComponentsCreated();
	if (!BeamBody || !BeamCore || !GlowLight)
	{
		return;
	}

	// 恒久実装: 始点/終点を結ぶ発光Cube(色付きハロー+白コア)を毎tick再配置・再スケールする。
	// DrawDebug系と異なりShipping相当でも描画される。
	const FLinearColor Color = WeaponData.IsValid() ? WeaponData->TracerColor : GetDefault<UMSRangedWeaponData>()->TracerColor;
	const float WidthMeters = ResolveBeamWidthMeters(WeaponData.Get());
	const float WidthCm = WidthMeters * 100.0f * MSGO::MSScale;
	const FVector Delta = BeamEnd - BeamStart;
	const float LengthCm = Delta.Size();
	if (LengthCm <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Midpoint = BeamStart + Delta * 0.5f;
	const FRotator BeamRotation = Delta.GetSafeNormal().Rotation();
	VisualRoot->SetWorldLocationAndRotation(Midpoint, BeamRotation);

	// Cubeメッシュは一辺100cm。X=ビーム全長、Y/Z=幅にスケールする。
	const float LengthScale = LengthCm / 100.0f;
	const float HaloScale = FMath::Max(WidthCm, 2.0f) / 100.0f;
	const float CoreScale = FMath::Max(WidthCm * 0.35f, 1.0f) / 100.0f;
	BeamBody->SetVisibility(true);
	BeamBody->SetRelativeScale3D(FVector(LengthScale, HaloScale, HaloScale));
	if (UMaterialInstanceDynamic* BodyMaterial = Cast<UMaterialInstanceDynamic>(BeamBody->GetMaterial(0)))
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	else if (UMaterialInstanceDynamic* NewBodyMaterial = BeamBody->CreateAndSetMaterialInstanceDynamic(0))
	{
		NewBodyMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}

	BeamCore->SetVisibility(true);
	BeamCore->SetRelativeScale3D(FVector(LengthScale, CoreScale, CoreScale));

	GlowLight->SetLightColor(Color);
	GlowLight->SetIntensity(180.0f);
	GlowLight->SetAttenuationRadius(FMath::Max(100.0f * MSGO::MSScale, WidthCm * 4.0f));
}
