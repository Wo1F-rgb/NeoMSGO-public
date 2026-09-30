// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUnderwaterMovementSpeedTest,
	"NeoMSGO.Player.Movement.UnderwaterSpeedPenalty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWaterFootRippleBranchesTest,
	"NeoMSGO.Player.Movement.WaterFootRippleBranches",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSUnderwaterMovementTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSUnderwaterMovementTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnUnit(UWorld* World, const FVector& Location, bool bAmphibious)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);

		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Unit);
		Data->WalkSpeed = 7.5f;
		Data->BoostSpeed = 15.0f;
		Data->bAmphibious = bAmphibious;
		Unit->InitializeUnit(Data);
		return Unit;
	}
}

bool FMSUnderwaterMovementSpeedTest::RunTest(const FString& Parameters)
{
	using namespace MSUnderwaterMovementTests;

	FTestWorld TestWorld;
	AMSPlayerCharacter* NonAmphibiousUnit = SpawnUnit(TestWorld.World, FVector(0.0, 0.0, 100.0), false);
	AMSPlayerCharacter* AmphibiousUnit = SpawnUnit(TestWorld.World, FVector(0.0, 500.0, 100.0), true);
	UMSCharacterMovementComponent* NonAmphibiousMovement = NonAmphibiousUnit->GetMSCharacterMovement();
	UMSCharacterMovementComponent* AmphibiousMovement = AmphibiousUnit->GetMSCharacterMovement();

	TestFalse(TEXT("mobile suit data defaults to no amphibious capability"),
		GetDefault<UMSMobileSuitData>()->bAmphibious);
	TestFalse(TEXT("InitializeUnit applies no amphibious capability"), NonAmphibiousMovement->IsAmphibious());
	TestTrue(TEXT("InitializeUnit applies amphibious capability"), AmphibiousMovement->IsAmphibious());
	TestEqual(TEXT("water depth threshold follows the scaled chest reference"),
		UMSCharacterMovementComponent::UnderwaterDepthThresholdCm, 12.0f * MSGO::MSScale);
	TestEqual(TEXT("shallow splash uses a foot contact tolerance independent from chest depth"),
		UMSCharacterMovementComponent::WaterSurfaceContactToleranceCm, 1.5f * MSGO::MSScale);
	TestEqual(TEXT("stationary water ripple interval is throttled to 0.5 seconds"),
		UMSCharacterMovementComponent::ComputeWaterRippleInterval(0.0f, 1000.0f), 0.5f);
	TestEqual(TEXT("maximum-speed water ripple interval shortens to 0.3 seconds"),
		UMSCharacterMovementComponent::ComputeWaterRippleInterval(1000.0f, 1000.0f), 0.3f);

	// Use an explicit 750 cm/s swimming baseline so all three requirements assert concrete values.
	NonAmphibiousMovement->MaxSwimSpeed = 750.0f;
	AmphibiousMovement->MaxSwimSpeed = 750.0f;
	NonAmphibiousMovement->SetMovementMode(MOVE_Swimming);
	AmphibiousMovement->SetMovementMode(MOVE_Swimming);
	TestTrue(TEXT("MOVE_Swimming is detected as underwater"), NonAmphibiousMovement->IsUnderwater());
	TestEqual(TEXT("non-amphibious underwater speed is 80 percent"),
		NonAmphibiousMovement->GetMaxSpeed(), 600.0f);
	TestEqual(TEXT("amphibious underwater speed has no penalty"),
		AmphibiousMovement->GetMaxSpeed(), 750.0f);

	NonAmphibiousMovement->SetMovementMode(MOVE_Walking);
	AmphibiousMovement->SetMovementMode(MOVE_Walking);
	TestEqual(TEXT("non-amphibious ground speed has no penalty"),
		NonAmphibiousMovement->GetMaxSpeed(), 750.0f * MSGO::MSScale, 0.01f);
	TestEqual(TEXT("amphibious ground speed has no penalty"),
		AmphibiousMovement->GetMaxSpeed(), 750.0f * MSGO::MSScale, 0.01f);

	const AMSBotCharacter* BotCDO = GetDefault<AMSBotCharacter>();
	TestNotNull(TEXT("bots share the underwater-aware movement component"),
		Cast<UMSCharacterMovementComponent>(BotCDO->GetCharacterMovement()));
	return true;
}

bool FMSWaterFootRippleBranchesTest::RunTest(const FString& Parameters)
{
	using namespace MSUnderwaterMovementTests;

	FTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World, FVector(0.0, 0.0, 100.0), false);
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APhysicsVolume* WaterVolume = TestWorld.World->SpawnActor<APhysicsVolume>(
		APhysicsVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	WaterVolume->bWaterVolume = true;
	UBoxComponent* WaterBounds = NewObject<UBoxComponent>(WaterVolume, TEXT("WaterRippleTestBounds"));
	WaterVolume->AddInstanceComponent(WaterBounds);
	WaterBounds->SetBoxExtent(FVector(500.0f, 500.0f, 100.0f));
	WaterBounds->RegisterComponent();
	Capsule->SetPhysicsVolume(WaterVolume, false);

	const float InitialWaterSurfaceZ = WaterVolume->GetComponentsBoundingBox(true).Max.Z;
	Unit->SetActorLocation(FVector(
		0.0f, 0.0f, InitialWaterSurfaceZ + Capsule->GetScaledCapsuleHalfHeight()));
	const float FootZ = Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	const float WaterSurfaceZ = WaterVolume->GetComponentsBoundingBox(true).Max.Z;
	TestTrue(TEXT("test places capsule feet directly on the water surface"),
		FMath::IsNearlyEqual(FootZ, WaterSurfaceZ, 1.0f));
	// Use the production mesh/material path instead of overriding the detector:
	// the half-immersed case must resolve a visible water surface at this XY.
	UStaticMesh* WaterPlaneMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* WaterPlaneMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Environment/Water/M_River_P7.M_River_P7"));
	if (!TestNotNull(TEXT("production water test plane mesh exists"), WaterPlaneMesh) ||
		!TestNotNull(TEXT("production water material exists"), WaterPlaneMaterial))
	{
		return false;
	}
	AStaticMeshActor* RenderedWater = TestWorld.World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), FVector(0.0f, 0.0f, WaterSurfaceZ),
		FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("rendered water actor spawns"), RenderedWater))
	{
		return false;
	}
	UStaticMeshComponent* RenderedWaterMesh = RenderedWater->GetStaticMeshComponent();
	RenderedWaterMesh->SetMobility(EComponentMobility::Movable);
	if (!TestTrue(TEXT("rendered water plane accepts its static mesh"),
		RenderedWaterMesh->SetStaticMesh(WaterPlaneMesh)))
	{
		return false;
	}
	RenderedWaterMesh->SetMaterial(0, WaterPlaneMaterial);
	RenderedWaterMesh->SetWorldScale3D(FVector(10.0f, 10.0f, 1.0f));

	// Branch 1: first surface contact spawns immediately, even while stationary.
	Movement->Velocity = FVector::ZeroVector;
	Movement->bWasTouchingWater = false;
	Movement->WaterRippleElapsed = 0.0f;
	Movement->WaterRippleSpawnCountForTest = 0;
	Movement->WaterRippleNetModeOverrideForTest = NM_Standalone;
	Movement->UpdateWaterFootRipple(0.0f);
	TestEqual(TEXT("entry contact immediately spawns one ripple"),
		Movement->WaterRippleSpawnCountForTest, 1);
	AMSTransientVfxActor* Splash = nullptr;
	for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("MSVfx.WaterFoot")))
		{
			Splash = *It;
			break;
		}
	}
	if (TestNotNull(TEXT("entry contact creates the transient mesh splash"), Splash))
	{
		TInlineComponentArray<UStaticMeshComponent*> SplashMeshes(Splash);
		TInlineComponentArray<UMaterialBillboardComponent*> SplashBillboards(Splash);
		TestTrue(TEXT("entry splash has a visible mesh fallback with bounded component count"),
			SplashMeshes.Num() > 0 && SplashMeshes.Num() <= 32);
		TestTrue(TEXT("spray billboard count stays bounded"), SplashBillboards.Num() <= 6);
		// Niagara is requested before first contact, but the first frame must not
		// wait for it. Keep the mesh splash as the deterministic fallback while
		// the async request is in flight; once ready, the optional detail pass is
		// still validated here.
		TInlineComponentArray<UNiagaraComponent*> SplashNiagara(Splash);
		TestTrue(TEXT("splash keeps at most one Niagara detail pass"), SplashNiagara.Num() <= 1);
		if (SplashNiagara.Num() == 1)
		{
			TestNotNull(TEXT("Niagara pass has the water splash system assigned"),
				SplashNiagara[0]->GetAsset());
			TestTrue(TEXT("Niagara pass is registered with the world"),
				SplashNiagara[0]->IsRegistered());
			// Activation needs a world FX system, which -nullrhi automation runs do
			// not have. Assert it wherever the RHI can render, so the "component
			// attached but never activated" failure mode is still caught; the nullrhi
			// path is covered by the registration check above plus the PIE evidence
			// in Docs/evidence/Issue230_Niagara.
			const bool bNullRHI = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
			if (!bNullRHI)
			{
				TestTrue(TEXT("Niagara pass is activated, not merely attached"),
					SplashNiagara[0]->IsActive());
			}
		}
		else
		{
			TestTrue(TEXT("mesh splash remains available while Niagara streams"), SplashMeshes.Num() > 0);
		}
		FVector BoundsOrigin;
		FVector BoundsExtent;
		Splash->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		const float HorizontalExtent = FMath::Max(BoundsExtent.X, BoundsExtent.Y);
		const float VerticalRatio = HorizontalExtent > 0.0f
			? BoundsExtent.Z / HorizontalExtent : 0.0f;
		TestTrue(TEXT("mesh splash starts wider than the required anti-pillar ratio"),
			HorizontalExtent > 0.0f && VerticalRatio < 0.70f);
	}

	// Branch 2: a stationary mech keeps a restrained surface loop after the entry effect expires.
	Movement->UpdateWaterFootRipple(2.0f);
	TestEqual(TEXT("stationary contact spawns one bounded wake after a long frame"),
		Movement->WaterRippleSpawnCountForTest, 2);
	TestEqual(TEXT("long frame resets cadence without a burst"), Movement->WaterRippleElapsed, 0.0f);

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	Unit->SetActorLocation(FVector(0.0f, 0.0f, WaterSurfaceZ + HalfHeight * 0.25f));
	FVector DetectedSurface = FVector::ZeroVector;
	TestTrue(TEXT("production water mesh is detected through the partly immersed capsule"),
		Movement->FindIntersectingWaterSurface(DetectedSurface));
	TestTrue(TEXT("detected partly immersed surface is near the visible plane"),
		FMath::Abs(DetectedSurface.Z - WaterSurfaceZ) < 10.0f);
	Movement->UpdateWaterFootRipple(0.49f);
	TestEqual(TEXT("partly immersed idle mech waits for the surface interval"),
		Movement->WaterRippleSpawnCountForTest, 2);
	Movement->UpdateWaterFootRipple(0.02f);
	TestEqual(TEXT("partly immersed idle mech continues making surface foam"),
		Movement->WaterRippleSpawnCountForTest, 3);

	// A fixture without movement ticks remains MOVE_None, whose max speed is zero.
	// Enter the actual submerged movement mode before exercising moving cadence.
	Movement->SetMovementMode(MOVE_Swimming);
	TestTrue(TEXT("moving fixture has a nonzero swimming speed"), Movement->GetMaxSpeed() > 0.f);
	Movement->Velocity = FVector(Movement->GetMaxSpeed(), 0.0f, 0.0f);
	Movement->UpdateWaterFootRipple(0.31f);
	TestEqual(TEXT("partly immersed moving mech generates the faster wake"),
		Movement->WaterRippleSpawnCountForTest, 4);
	Movement->UpdateWaterFootRipple(20.0f);
	TestEqual(TEXT("movement hitch emits at most one additional wake"),
		Movement->WaterRippleSpawnCountForTest, 5);

	Unit->SetActorLocation(FVector(0.0f, 0.0f, WaterSurfaceZ - HalfHeight - 10.0f));
	Movement->UpdateWaterFootRipple(1.0f);
	TestFalse(TEXT("fully submerged capsule ends surface contact"), Movement->bWasTouchingWater);
	TestEqual(TEXT("fully submerged capsule produces no surface wake"),
		Movement->WaterRippleSpawnCountForTest, 5);
	Unit->SetActorLocation(FVector(0.0f, 0.0f,
		WaterSurfaceZ + HalfHeight + UMSCharacterMovementComponent::WaterSurfaceContactToleranceCm + 10.0f));
	Movement->UpdateWaterFootRipple(1.0f);
	TestEqual(TEXT("capsule entirely out of water produces no wake"),
		Movement->WaterRippleSpawnCountForTest, 5);
	RenderedWaterMesh->SetVisibility(false);
	Capsule->SetPhysicsVolume(TestWorld.World->GetDefaultPhysicsVolume(), false);
	Unit->SetActorLocation(FVector(0.0f, 0.0f, WaterSurfaceZ + HalfHeight * 0.25f));
	Movement->UpdateWaterFootRipple(1.0f);
	TestEqual(TEXT("volume bounds alone cannot create a half-immersed wake on dry land"),
		Movement->WaterRippleSpawnCountForTest, 5);
	Unit->SetActorLocation(FVector(0.0f, 0.0f, WaterSurfaceZ + HalfHeight));
	Movement->UpdateWaterFootRipple(1.0f);
	TestEqual(TEXT("off-volume shallow AABB candidate needs a visible water mesh"),
		Movement->WaterRippleSpawnCountForTest, 5);
	RenderedWaterMesh->SetVisibility(true);
	UMaterialInterface* LandMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/WaterRealism0929/Environment/Materials/M_WR_Concrete.M_WR_Concrete"));
	if (!TestNotNull(TEXT("land material under WaterRealism exists for classification regression"), LandMaterial)) return false;
	RenderedWaterMesh->SetMaterial(0, LandMaterial);
	Unit->SetActorLocation(FVector(0.f, 0.f, WaterSurfaceZ + HalfHeight * 0.25f));
	TestFalse(TEXT("WaterRealism folder name does not classify concrete as a water surface"),
		Movement->FindIntersectingWaterSurface(DetectedSurface));
	RenderedWaterMesh->SetMaterial(0, WaterPlaneMaterial);

	Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	Movement->UpdateWaterFootRipple(1.0f);
	TestFalse(TEXT("death clears surface contact"), Movement->bWasTouchingWater);
	TestEqual(TEXT("dead mech produces no wake"), Movement->WaterRippleSpawnCountForTest, 5);
	Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 0);

	// Branch 3: dedicated servers reset state and never spawn the splash.
	Movement->bWasTouchingWater = false;
	Movement->WaterRippleNetModeOverrideForTest = NM_DedicatedServer;
	Movement->UpdateWaterFootRipple(1.0f);
	TestEqual(TEXT("dedicated server excludes ripple spawning"),
		Movement->WaterRippleSpawnCountForTest, 5);
	TestFalse(TEXT("dedicated server clears water-contact state"), Movement->bWasTouchingWater);
	Movement->WaterRippleNetModeOverrideForTest.Reset();

	// Branch 4: shallow water is found at the feet even when CharacterMovement still reports the
	// default (non-water) physics volume because the capsule centre has not entered the volume.
	Capsule->SetPhysicsVolume(TestWorld.World->GetDefaultPhysicsVolume(), false);
	const float ShallowSurfaceZ = WaterVolume->GetComponentsBoundingBox(true).Max.Z;
	Unit->SetActorLocation(FVector(0.0f, 0.0f,
		ShallowSurfaceZ + HalfHeight + UMSCharacterMovementComponent::WaterSurfaceContactToleranceCm * 0.5f));
	Movement->bWasTouchingWater = false;
	Movement->WaterRippleNetModeOverrideForTest = NM_Standalone;
	Movement->UpdateWaterFootRipple(0.0f);
	TestEqual(TEXT("foot intersection spawns in shallow water without active bWaterVolume"),
		Movement->WaterRippleSpawnCountForTest, 6);

	// Taigan's slanted bank can leave an AABB candidate on dry ground. This
	// test volume has broad BoxComponent bounds but no brush at X=450; the
	// tagged river path must reject it instead of producing a false ripple.
	WaterVolume->Tags.AddUnique(FName(TEXT("NewYorkTaiganGenerated")));
	const FVector DryBankFoot(450.0f, 0.0f, ShallowSurfaceZ - 1.0f);
	TestFalse(TEXT("dry-bank probe is outside the actual volume brush"),
		WaterVolume->EncompassesPoint(DryBankFoot));
	Unit->SetActorLocation(FVector(DryBankFoot.X, DryBankFoot.Y,
		ShallowSurfaceZ + HalfHeight));
	Movement->bWasTouchingWater = false;
	Movement->UpdateWaterFootRipple(0.0f);
	TestEqual(TEXT("Taigan dry-bank AABB candidate does not spawn water ripple"),
		Movement->WaterRippleSpawnCountForTest, 6);
	Movement->WaterRippleNetModeOverrideForTest.Reset();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
