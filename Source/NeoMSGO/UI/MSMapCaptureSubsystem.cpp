#include "UI/MSMapCaptureSubsystem.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "ContentStreaming.h"
#include "Engine/Scene.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Gameplay/MSCapturePoint.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "UI/MSMapBoundsActor.h"
#include "UI/MSMapTypes.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	constexpr int32 OverviewResolution = 1024;

	// Very high orthographic SceneCapture cameras lose useful depth precision and can
	// produce an entirely black FinalColor target.
	constexpr float MinimumCaptureHeight = 50000.0f;
	constexpr float MaximumCaptureHeight = 100000.0f;

	// Sanity limits reject tiny helper meshes and enormous sky shells. Daibao's actual
	// terrain half extent is 500,000 cm, comfortably inside this range.
	constexpr float MinimumMeasuredHalfExtent = 20000.0f;
	constexpr float MaximumMeasuredHalfExtent = 2000000.0f;
	constexpr float MinimumMeasuredMarginCm = 5000.0f;
	constexpr float MeasuredMarginRatio = 0.01f;
	constexpr float OverviewWarmupIntervalSeconds = 1.0f;
	constexpr float OverviewPostFallbackReadinessIntervalSeconds = 5.0f;
	constexpr int32 MaximumOverviewWarmupAttempts = 10;
	constexpr int32 MaximumOverviewCaptures = 2;
	const FName OverviewWaterMeshName(TEXT("SM_NYT_Water"));
	const FName OverviewWaterProxyTag(TEXT("MSMapOverviewWaterProxy"));
	const TCHAR* OverviewWaterMaterialPath = TEXT("/Game/NewYorkTaigan/Materials/M_NYT_MapWater.M_NYT_MapWater");

	bool IsUsableMapGeometry(const UStaticMeshComponent& Component)
	{
		const UStaticMesh* Mesh = Component.GetStaticMesh();
		if (!Mesh || !Component.IsRegistered())
		{
			return false;
		}

		const FBoxSphereBounds Bounds = Component.Bounds;
		const FVector Extent = Bounds.BoxExtent;
		if (Bounds.Origin.ContainsNaN() || Extent.ContainsNaN() ||
			Extent.X <= 1.0f || Extent.Y <= 1.0f ||
			Extent.X > MaximumMeasuredHalfExtent || Extent.Y > MaximumMeasuredHalfExtent)
		{
			return false;
		}

		// Sky domes and cloud shells intentionally exceed the playable world.
		const FString SearchableName =
			Component.GetName() + TEXT(" ") +
			(Component.GetOwner() ? Component.GetOwner()->GetName() : FString()) + TEXT(" ") +
			Mesh->GetPathName();
		return !SearchableName.Contains(TEXT("Sky"), ESearchCase::IgnoreCase) &&
			!SearchableName.Contains(TEXT("Cloud"), ESearchCase::IgnoreCase) &&
			!SearchableName.Contains(TEXT("Atmosphere"), ESearchCase::IgnoreCase);
	}
}

void UMSMapCaptureSubsystem::ConfigureOverviewWater(USceneCaptureComponent2D* Capture)
{
	UWorld* World = GetWorld();
	if (!World || !Capture) return;
	UMaterialInterface* OpaqueWater = LoadObject<UMaterialInterface>(nullptr, OverviewWaterMaterialPath);
	if (!OpaqueWater) return; // Retain the original water until the map-only material is installed.
	// TActorIterator may visit actors spawned while it advances. Collect original
	// sources first, before adding any map-only proxy to this same world.
	TArray<TWeakObjectPtr<AStaticMeshActor>> Sources;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Source = *It;
		UStaticMeshComponent* SourceMesh = Source ? Source->GetStaticMeshComponent() : nullptr;
		if (!Source || Source->Tags.Contains(OverviewWaterProxyTag) ||
			Source->HasAnyFlags(RF_Transient) ||
			!SourceMesh || !SourceMesh->GetStaticMesh() ||
			SourceMesh->GetStaticMesh()->GetFName() != OverviewWaterMeshName) continue;
		Sources.Add(Source);
	}
	for (const TWeakObjectPtr<AStaticMeshActor>& SourceRef : Sources)
	{
		AStaticMeshActor* Source = SourceRef.Get();
		if (!IsValid(Source)) continue;
		Capture->HiddenActors.AddUnique(Source);
		if (const TWeakObjectPtr<AStaticMeshActor>* Existing = OverviewWaterProxyBySource.Find(TWeakObjectPtr<AStaticMeshActor>(Source));
			Existing && Existing->IsValid()) continue;
		UStaticMeshComponent* SourceMesh = Source->GetStaticMeshComponent();
		if (!SourceMesh || !SourceMesh->GetStaticMesh()) continue;
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Proxy = World->SpawnActor<AStaticMeshActor>(
			Source->GetActorLocation(), Source->GetActorRotation(), Params);
		if (!Proxy) continue;
		Proxy->Tags.AddUnique(OverviewWaterProxyTag);
		UStaticMeshComponent* ProxyMesh = Proxy->GetStaticMeshComponent();
		ProxyMesh->SetMobility(EComponentMobility::Movable);
		Proxy->SetActorTransform(Source->GetActorTransform());
		ProxyMesh->SetStaticMesh(SourceMesh->GetStaticMesh());
		ProxyMesh->SetRelativeTransform(SourceMesh->GetRelativeTransform());
		// The copied mesh's default collision profile can re-enable query collision
		// during component registration. Override the profile as well as the actor.
		ProxyMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		ProxyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proxy->SetActorEnableCollision(false);
		ProxyMesh->SetCastShadow(false);
		ProxyMesh->SetVisibleInSceneCaptureOnly(true);
		for (int32 Index = 0; Index < ProxyMesh->GetNumMaterials(); ++Index)
			ProxyMesh->SetMaterial(Index, OpaqueWater);
		OverviewWaterProxies.Add(Proxy);
		OverviewWaterProxyBySource.Add(TWeakObjectPtr<AStaticMeshActor>(Source), TWeakObjectPtr<AStaticMeshActor>(Proxy));
	}
}

void UMSMapCaptureSubsystem::ResolveBounds() const
{
	if (bBoundsResolved)
	{
		return;
	}
	bBoundsResolved = true;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AMSMapBoundsActor> It(World); It; ++It)
	{
		BoundsActor = *It;
		return; // Authored bounds are authoritative when present.
	}

	// #233: Daibao is a 3x3 terrain grid (10 km square) but has no authored bounds
	// actor. Measure loaded static-mesh geometry so future terrain scaling follows
	// automatically instead of reverting to the old 3.8 km fallback.
	FBox TerrainBounds(ForceInit);
	FBox GeneralMeshBounds(ForceInit);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(*It);
		for (const UStaticMeshComponent* Component : MeshComponents)
		{
			if (!Component || !IsUsableMapGeometry(*Component))
			{
				continue;
			}

			const FVector Origin = Component->Bounds.Origin;
			const FVector Extent = Component->Bounds.BoxExtent;
			const FBox ComponentBounds(
				FVector(Origin.X - Extent.X, Origin.Y - Extent.Y, 0.0f),
				FVector(Origin.X + Extent.X, Origin.Y + Extent.Y, 0.0f));
			GeneralMeshBounds += ComponentBounds;

			const FString MeshPath = Component->GetStaticMesh()->GetPathName();
			if (MeshPath.Contains(TEXT("/Environment/Terrain/"), ESearchCase::IgnoreCase) ||
				MeshPath.Contains(TEXT("SM_Terrain"), ESearchCase::IgnoreCase))
			{
				TerrainBounds += ComponentBounds;
			}
		}
	}

	// Prefer the dedicated terrain grid when available. The all-mesh box remains a
	// generic fallback for maps whose ground assets use a different content layout.
	FBox Measured = TerrainBounds.IsValid ? TerrainBounds : GeneralMeshBounds;

	// Strategic markers must fit even when an authored base sits just beyond terrain.
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
	{
		if (const AMSCapturePoint* Point = *It; IsValid(Point))
		{
			Measured += Point->GetActorLocation();
		}
	}

	if (!Measured.IsValid)
	{
		return;
	}

	MeasuredCenter = FVector(Measured.GetCenter().X, Measured.GetCenter().Y, 0.0f);
	const FVector2D ContentHalfExtents(Measured.GetExtent().X, Measured.GetExtent().Y);
	const float Margin = FMath::Max(MinimumMeasuredMarginCm,
		FMath::Max(ContentHalfExtents.X, ContentHalfExtents.Y) * MeasuredMarginRatio);
	MeasuredHalfExtents = FVector2D(
		FMath::Clamp(ContentHalfExtents.X + Margin, MinimumMeasuredHalfExtent, MaximumMeasuredHalfExtent),
		FMath::Clamp(ContentHalfExtents.Y + Margin, MinimumMeasuredHalfExtent, MaximumMeasuredHalfExtent));
	MeasuredHalfExtent = FMath::Max(MeasuredHalfExtents.X, MeasuredHalfExtents.Y);
}

UTexture2D* UMSMapCaptureSubsystem::GetUndergroundOverviewTexture() const
{
	ResolveBounds();
	return IsValid(BoundsActor) && BoundsActor->bEnableUndergroundLayer
		? BoundsActor->UndergroundOverviewTexture.Get() : nullptr;
}

bool UMSMapCaptureSubsystem::ShouldUseUndergroundLayer(const FVector& WorldLocation, const bool bWasUnderground) const
{
	ResolveBounds();
	return IsValid(GetUndergroundOverviewTexture()) && IsValid(BoundsActor)
		&& BoundsActor->IsUndergroundLocation(WorldLocation, bWasUnderground);
}

FVector UMSMapCaptureSubsystem::GetMapWorldCenter() const
{
	ResolveBounds();
	if (IsValid(BoundsActor))
	{
		return BoundsActor->GetMapWorldCenter();
	}
	return MeasuredHalfExtent > 0.0f ? MeasuredCenter : FVector::ZeroVector;
}

float UMSMapCaptureSubsystem::GetMapWorldHalfExtent() const
{
	const FVector2D HalfExtents = GetMapWorldHalfExtents();
	return FMath::Max(HalfExtents.X, HalfExtents.Y);
}

FVector2D UMSMapCaptureSubsystem::GetMapWorldHalfExtents() const
{
	ResolveBounds();
	if (IsValid(BoundsActor)) return BoundsActor->GetMapWorldHalfExtents();
	return MeasuredHalfExtent > 0.0f ? MeasuredHalfExtents : FVector2D(MSMapPalette::WorldHalfExtentCm);
}

bool UMSMapCaptureSubsystem::AreMapResourcesReady() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->Scene)
	{
		return false;
	}

#if WITH_EDITOR
	// A material can have a valid UObject and bounds while its shaders are still
	// compiling. Waiting here prevents the first capture from recording the
	// transient checkerboard/default-material result.
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		return false;
	}
#endif

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(*It);
		for (const UStaticMeshComponent* Component : MeshComponents)
		{
			if (!Component || !IsUsableMapGeometry(*Component))
			{
				continue;
			}

			const UStaticMesh* Mesh = Component->GetStaticMesh();
			if (!Mesh || Mesh->HasPendingInitOrStreaming())
			{
				return false;
			}

#if WITH_EDITOR
			if (Mesh->IsCompiling() || Component->IsCompiling())
			{
				return false;
			}
#endif

			// A map mesh can be renderable before one of its material instances has
			// finished compiling. Check the component slots that the capture will use.
			for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
			{
				if (const UMaterialInterface* Material = Component->GetMaterial(MaterialIndex);
					Material && Material->IsCompiling())
				{
					return false;
				}
			}
		}
	}

	// This is intentionally a non-blocking snapshot. The initial warm-up has a
	// finite retry cap; any fallback checks after that are throttled below so
	// unrelated streaming requests never cause per-frame map work.
	return IStreamingManager::HasShutdown() ||
		IStreamingManager::Get().GetNumWantingResources() == 0;
}

void UMSMapCaptureSubsystem::ScheduleWarmupCapture()
{
	if (bCaptureCompleted || bWarmupActive || !OverviewTexture || !IsValid(CaptureActor))
	{
		return;
	}

	bWarmupActive = true;
	WarmupAttemptCount = 0;
	bFallbackCaptureAttempted = false;
	NextReadinessCheckTimeSeconds = 0.0;
	const TWeakObjectPtr<UMSMapCaptureSubsystem> WeakThis(this);
	WarmupTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("MSMapOverviewWarmup"), OverviewWarmupIntervalSeconds,
		[WeakThis](float DeltaTime)
		{
			if (UMSMapCaptureSubsystem* Subsystem = WeakThis.Get())
			{
				return Subsystem->TickWarmup(DeltaTime);
			}
			return false;
		});

	if (!WarmupTickerHandle.IsValid())
	{
		// The ticker is expected to be available during gameplay. Keep a usable
		// fallback for unusual shutdown/test environments rather than leaving a
		// permanently clear render target.
		bWarmupActive = false;
		if (CaptureOverview())
		{
			bCaptureCompleted = true;
		}
	}
}

bool UMSMapCaptureSubsystem::TickWarmup(float DeltaTime)
{
	if (!bWarmupActive)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!FApp::CanEverRender() || !World || !World->Scene || !OverviewTexture || !IsValid(CaptureActor))
	{
		bWarmupActive = false;
		return false;
	}

	if (!bFallbackCaptureAttempted)
	{
		++WarmupAttemptCount;
		const bool bReady = AreMapResourcesReady();
		if (bReady)
		{
			const bool bCaptured = CaptureOverview();
			bCaptureCompleted = bCaptured;
			bWarmupActive = !bCaptured;
			UE_LOG(LogTemp, Display,
				TEXT("Map overview warmup final capture: attempt=%d ready=true delta=%.3fs"),
				WarmupAttemptCount, DeltaTime);
			return bWarmupActive;
		}

		if (WarmupAttemptCount < MaximumOverviewWarmupAttempts)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("Map overview warmup waiting for render resources: attempt=%d streaming=%d"),
				WarmupAttemptCount,
				IStreamingManager::HasShutdown() ? 0 : IStreamingManager::Get().GetNumWantingResources());
			return true;
		}

		// Keep this first fallback visible to the user, but do not mark it final:
		// long texture streaming can still complete during the paused briefing.
		bFallbackCaptureAttempted = true;
		bCaptureCompleted = false;
		NextReadinessCheckTimeSeconds = FPlatformTime::Seconds() + OverviewPostFallbackReadinessIntervalSeconds;
		const bool bFallbackCaptured = CaptureOverview();
		UE_LOG(LogTemp, Display,
			TEXT("Map overview warmup fallback capture: attempt=%d captured=%s; continuing readiness checks every %.0fs"),
			WarmupAttemptCount, bFallbackCaptured ? TEXT("true") : TEXT("false"),
			OverviewPostFallbackReadinessIntervalSeconds);
		return true;
	}

	const double CurrentTimeSeconds = FPlatformTime::Seconds();
	if (CurrentTimeSeconds < NextReadinessCheckTimeSeconds)
	{
		return true;
	}
	NextReadinessCheckTimeSeconds = CurrentTimeSeconds + OverviewPostFallbackReadinessIntervalSeconds;

	const bool bReady = AreMapResourcesReady();
	UE_LOG(LogTemp, Verbose,
		TEXT("Map overview warmup post-fallback readiness check: ready=%s streaming=%d"),
		bReady ? TEXT("true") : TEXT("false"),
		IStreamingManager::HasShutdown() ? 0 : IStreamingManager::Get().GetNumWantingResources());
	if (bReady)
	{
		const bool bCaptured = CaptureOverview();
		bCaptureCompleted = bCaptured;
		if (bCaptured)
		{
			bWarmupActive = false;
			UE_LOG(LogTemp, Display,
				TEXT("Map overview warmup final capture after fallback: ready=true captures=%d"),
				OverviewCaptureCount);
			return false;
		}
	}

	return true;
}

bool UMSMapCaptureSubsystem::CaptureOverview()
{
	UWorld* World = GetWorld();
	USceneCaptureComponent2D* Capture =
		IsValid(CaptureActor) ? CaptureActor->GetCaptureComponent2D() : nullptr;
	if (!FApp::CanEverRender() || !World || !World->Scene || !OverviewTexture ||
		!OverviewTexture->GetResource() || !Capture || OverviewCaptureCount >= MaximumOverviewCaptures)
	{
		return false;
	}

	++OverviewCaptureCount;
	Capture->CaptureScene();
	UE_LOG(LogTemp, Display,
		TEXT("Map overview capture: center=%s halfExtent=%.0fcm requestedHeight=%.0fcm captureHeight=%.0fcm"),
		*GetMapWorldCenter().ToCompactString(), Capture->OrthoWidth * 0.5f,
		IsValid(BoundsActor) ? BoundsActor->GetCaptureHeight() : Capture->GetComponentLocation().Z - GetMapWorldCenter().Z,
		Capture->GetComponentLocation().Z - GetMapWorldCenter().Z);
	return true;
}

UTextureRenderTarget2D* UMSMapCaptureSubsystem::GetOverviewTexture()
{
	if (bCaptureAttempted)
	{
		if (OverviewTexture && IsValid(CaptureActor) && !bCaptureCompleted)
		{
			ScheduleWarmupCapture();
		}
		return OverviewTexture;
	}
	bCaptureAttempted = true;
	++CaptureRequestCount;

	UWorld* World = GetWorld();
	if (!World || !World->Scene)
	{
		return nullptr;
	}

	OverviewTexture = NewObject<UTextureRenderTarget2D>(this, TEXT("MSMapOverviewRT"), RF_Transient);
	OverviewTexture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	OverviewTexture->ClearColor = MSMapPalette::Background;
	const FVector2D HalfExtents = GetMapWorldHalfExtents();
	const float MaxHalfExtent = FMath::Max(HalfExtents.X, HalfExtents.Y);
	const int32 CaptureWidth = FMath::Max(1, FMath::RoundToInt(OverviewResolution * HalfExtents.Y / MaxHalfExtent));
	const int32 CapturePixelHeight = FMath::Max(1, FMath::RoundToInt(OverviewResolution * HalfExtents.X / MaxHalfExtent));
	OverviewTexture->InitAutoFormat(CaptureWidth, CapturePixelHeight);
	OverviewTexture->UpdateResourceImmediate(true);
	if (!OverviewTexture->GetResource())
	{
		OverviewTexture = nullptr;
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("MSMapOverviewCapture");
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CaptureActor = World->SpawnActor<ASceneCapture2D>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	USceneCaptureComponent2D* Capture =
		IsValid(CaptureActor) ? CaptureActor->GetCaptureComponent2D() : nullptr;
	if (!Capture)
	{
		OverviewTexture = nullptr;
		return nullptr;
	}

	ResolveBounds();
	const float HalfExtent = GetMapWorldHalfExtent();
	const float RequestedCaptureHeight = IsValid(BoundsActor)
		? BoundsActor->GetCaptureHeight()
		: HalfExtent * 0.5f;
	const float CaptureHeight = FMath::Clamp(
		RequestedCaptureHeight, MinimumCaptureHeight, MaximumCaptureHeight);
	CaptureActor->SetActorLocation(
		GetMapWorldCenter() + FVector(0.0f, 0.0f, CaptureHeight));

	// With Pitch=-90 and Yaw=180, screen up is world -X (geographic north)
	// and screen right is world -Y (east). Marker projection uses the same yaw.
	CaptureActor->SetActorRotation(
		FRotator(-90.0f, MSMapPalette::WorldNorthYawDeg, 0.0f));
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	// The camera's horizontal axis is world Y at yaw 180. Match the render
	// target aspect to the playable X/Y rectangle so no off-map strips appear.
	Capture->OrthoWidth = HalfExtents.Y * 2.0f;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->TextureTarget = OverviewTexture;
	ConfigureOverviewWater(Capture);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->MaxViewDistanceOverride = CaptureHeight * 2.0f;
	if (IsValid(BoundsActor) && BoundsActor->UsesUnlitOverview())
	{
		// On Taigan the lit orthographic capture is nearly black even after
		// increasing the cull distance. Unlit retains actual terrain/material
		// boundaries while removing scene-light dependence from the tactical UI.
		Capture->ShowFlags.SetLighting(false);
		Capture->PostProcessSettings.bOverride_VignetteIntensity = true;
		Capture->PostProcessSettings.VignetteIntensity = 0.0f;
		Capture->PostProcessBlendWeight = 1.0f;
		Capture->MaxViewDistanceOverride = FMath::Max(CaptureHeight * 2.0f,
			FVector(HalfExtent, HalfExtent, CaptureHeight).Size() + 5000.0f);
	}
	ScheduleWarmupCapture();
	return OverviewTexture;
}

void UMSMapCaptureSubsystem::Deinitialize()
{
	if (WarmupTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WarmupTickerHandle);
		WarmupTickerHandle.Reset();
	}
	bWarmupActive = false;
	if (IsValid(CaptureActor))
	{
		CaptureActor->Destroy();
	}
	for (AStaticMeshActor* Proxy : OverviewWaterProxies)
		if (IsValid(Proxy)) Proxy->Destroy();
	OverviewWaterProxies.Reset();
	OverviewWaterProxyBySource.Reset();
	CaptureActor = nullptr;
	OverviewTexture = nullptr;
	BoundsActor = nullptr;
	Super::Deinitialize();
}
