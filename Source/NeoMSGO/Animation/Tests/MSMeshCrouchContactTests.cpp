// Copyright NeoMSGO Project. All Rights Reserved.
//
// MPU01 ThreeJSの実メッシュ接地を、実運用と同じゴーストリグ経路
// (EvaluatePose -> MeshVisual.TransferPoseForAutomationTests)で検査する。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshCrouchContactAcceptanceTest,
	"NeoMSGO.Animation.MeshVisual.CrouchContactAcceptance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	constexpr const TCHAR* MeshCrouchMPU01DataPath =
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS");
	constexpr float GroundToleranceUU = 1.0f; // 1cm in the project's 1uu=1cm rig space.
	constexpr float StableToleranceUU = 0.01f;
	constexpr float RestToleranceUU = 0.1f;

	struct FMSMeshCrouchContactWorld
	{
		UWorld* World = nullptr;

		FMSMeshCrouchContactWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false,
				TEXT("MSMeshCrouchContactAcceptanceWorld"));
			if (!World)
			{
				return;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMSMeshCrouchContactWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FComponentSnapshot
	{
		FTransform ActorTransform = FTransform::Identity;
		FVector CapsuleRelativeLocation = FVector::ZeroVector;
		FRotator CapsuleRelativeRotation = FRotator::ZeroRotator;
		FVector CapsuleRelativeScale = FVector::OneVector;
		float CapsuleRadius = 0.0f;
		float CapsuleHalfHeight = 0.0f;
	};

	FComponentSnapshot CaptureComponentState(const AMSPlayerCharacter& Unit,
		const UCapsuleComponent& Capsule)
	{
		FComponentSnapshot Result;
		Result.ActorTransform = Unit.GetActorTransform();
		Result.CapsuleRelativeLocation = Capsule.GetRelativeLocation();
		Result.CapsuleRelativeRotation = Capsule.GetRelativeRotation();
		Result.CapsuleRelativeScale = Capsule.GetRelativeScale3D();
		Result.CapsuleRadius = Capsule.GetUnscaledCapsuleRadius();
		Result.CapsuleHalfHeight = Capsule.GetUnscaledCapsuleHalfHeight();
		return Result;
	}

	void TestComponentStateUnchanged(FAutomationTestBase& Test,
		const FComponentSnapshot& Before, const AMSPlayerCharacter& Unit,
		const UCapsuleComponent& Capsule, const TCHAR* Prefix)
	{
		const FComponentSnapshot After = CaptureComponentState(Unit, Capsule);
		Test.TestTrue(FString::Printf(TEXT("%s actor transform is unchanged"), Prefix),
			After.ActorTransform.Equals(Before.ActorTransform, KINDA_SMALL_NUMBER));
		Test.TestTrue(FString::Printf(TEXT("%s capsule relative location is unchanged"), Prefix),
			After.CapsuleRelativeLocation.Equals(Before.CapsuleRelativeLocation, KINDA_SMALL_NUMBER));
		Test.TestTrue(FString::Printf(TEXT("%s capsule relative rotation is unchanged"), Prefix),
			After.CapsuleRelativeRotation.Equals(Before.CapsuleRelativeRotation, KINDA_SMALL_NUMBER));
		Test.TestTrue(FString::Printf(TEXT("%s capsule relative scale is unchanged"), Prefix),
			After.CapsuleRelativeScale.Equals(Before.CapsuleRelativeScale, KINDA_SMALL_NUMBER));
		Test.TestTrue(FString::Printf(TEXT("%s capsule radius is unchanged"), Prefix),
			FMath::IsNearlyEqual(After.CapsuleRadius, Before.CapsuleRadius, KINDA_SMALL_NUMBER));
		Test.TestTrue(FString::Printf(TEXT("%s capsule half height is unchanged"), Prefix),
			FMath::IsNearlyEqual(After.CapsuleHalfHeight, Before.CapsuleHalfHeight, KINDA_SMALL_NUMBER));
	}

	AMSPlayerCharacter* SpawnUnit(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(),
			FVector(1200.0f, -340.0f, 125.0f),
			FRotator(0.0f, 23.0f, 0.0f),
			Params);
	}

	struct FCrouchContactFrameProfile
	{
		const TCHAR* Name = TEXT("unknown");
		const float* Deltas = nullptr;
		int32 DeltaCount = 0;
	};

	bool RunContactProfile(FAutomationTestBase& Test, const FCrouchContactFrameProfile& Profile,
		UMSMobileSuitData& MobileSuitData)
	{
		FMSMeshCrouchContactWorld TestWorld;
		AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
		const FString Prefix = FString::Printf(TEXT("%s: "), Profile.Name);
		if (!Test.TestNotNull(Prefix + TEXT("test world is available"), TestWorld.World)
			|| !Test.TestNotNull(Prefix + TEXT("MPU01 ghost unit spawns"), Unit))
		{
			return false;
		}

		Unit->InitializeUnit(&MobileSuitData);
		UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
		UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
		UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
		UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();
		if (!Test.TestNotNull(Prefix + TEXT("animator exists"), Animator)
			|| !Test.TestNotNull(Prefix + TEXT("ghost rig exists"), Rig)
			|| !Test.TestNotNull(Prefix + TEXT("mesh visual exists"), Visual)
			|| !Test.TestNotNull(Prefix + TEXT("capsule exists"), Capsule))
		{
			return false;
		}
		if (!Test.TestTrue(Prefix + TEXT("MPU01 real mesh visual is active"), Visual->IsMeshVisualActive())
			|| !Test.TestTrue(Prefix + TEXT("MPU01 has a required crouch contact profile"),
				Visual->HasCrouchContactProfile()))
		{
			return false;
		}

		const FComponentSnapshot ComponentBefore = CaptureComponentState(*Unit, *Capsule);
		const float RestRootZ = Rig->GetRigRootRestRelativeZ();
		USceneComponent* RigRoot = Rig->GetRigRoot();
		if (!Test.TestNotNull(Prefix + TEXT("rig root exists"), RigRoot))
		{
			return false;
		}

		int32 FrameIndex = 0;
		auto NextDelta = [&]()
		{
			const float Delta = Profile.Deltas[FrameIndex % Profile.DeltaCount];
			++FrameIndex;
			return Delta;
		};
		auto EvaluateAndTransfer = [&](const FMSProceduralPoseInputs& Inputs, float Delta)
		{
			Animator->EvaluatePose(Inputs, Delta);
			Visual->TransferPoseForAutomationTests();
		};
		auto RunFor = [&](const FMSProceduralPoseInputs& Inputs, float Duration)
		{
			float Elapsed = 0.0f;
			while (Elapsed < Duration)
			{
				const float Delta = NextDelta();
				EvaluateAndTransfer(Inputs, Delta);
				Elapsed += Delta;
			}
		};

		// Establish the sole standing contact reference through the same transfer path.
		const FMSProceduralPoseInputs Standing;
		RunFor(Standing, 0.25f);
		const double StandingGroundZ = Visual->GetCrouchContactMinWorldZ();
		Test.TestTrue(Prefix + TEXT("standing contact baseline is finite"),
			FMath::IsFinite(StandingGroundZ));

		FMSProceduralPoseInputs Crouching;
		Crouching.bIsCrouching = true;
		float MaxContactError = 0.0f;
		float CrouchRootZ = RestRootZ;
		bool bReachedContactPhase = false;
		float Elapsed = 0.0f;
		while (Elapsed < 2.0f)
		{
			const float Delta = NextDelta();
			EvaluateAndTransfer(Crouching, Delta);
			Elapsed += Delta;
			const float Alpha = Animator->GetPoseState().CrouchPoseAlpha;
			if (Alpha >= 0.1f)
			{
				bReachedContactPhase = true;
				const float Error = FMath::Abs(static_cast<float>(Visual->GetCrouchContactMinWorldZ()
					- StandingGroundZ));
				MaxContactError = FMath::Max(MaxContactError, Error);
			}
		}
		CrouchRootZ = RigRoot->GetRelativeLocation().Z;
		Test.TestTrue(Prefix + TEXT("crouch reaches contact-correction phase"), bReachedContactPhase);
		Test.TestTrue(FString::Printf(TEXT("%s crouch contact remains on standing ground (max %.4fUU)"),
			*Prefix, MaxContactError), bReachedContactPhase && MaxContactError <= GroundToleranceUU);

		// Once the pose is settled, repeated TransferPose calls must be idempotent.
		const float HeldRootZ = RigRoot->GetRelativeLocation().Z;
		const double HeldContactZ = Visual->GetCrouchContactMinWorldZ();
		float MaxRepeatedRootStep = 0.0f;
		float MaxRepeatedContactStep = 0.0f;
		for (int32 Repeat = 0; Repeat < 24; ++Repeat)
		{
			const float BeforeRootZ = RigRoot->GetRelativeLocation().Z;
			const double BeforeContactZ = Visual->GetCrouchContactMinWorldZ();
			Visual->TransferPoseForAutomationTests();
			MaxRepeatedRootStep = FMath::Max(MaxRepeatedRootStep,
				FMath::Abs(RigRoot->GetRelativeLocation().Z - BeforeRootZ));
			MaxRepeatedContactStep = FMath::Max(MaxRepeatedContactStep,
				FMath::Abs(static_cast<float>(Visual->GetCrouchContactMinWorldZ() - BeforeContactZ)));
		}
		Test.TestTrue(FString::Printf(TEXT("%s repeated transfer does not accumulate root offset (max %.5fUU)"),
			*Prefix, MaxRepeatedRootStep), MaxRepeatedRootStep <= StableToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s repeated transfer does not accumulate contact offset (max %.5fUU)"),
			*Prefix, MaxRepeatedContactStep), MaxRepeatedContactStep <= StableToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s settled crouch root is stable (drift %.5fUU)"),
			*Prefix, FMath::Abs(RigRoot->GetRelativeLocation().Z - HeldRootZ)),
			FMath::Abs(RigRoot->GetRelativeLocation().Z - HeldRootZ) <= StableToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s settled crouch contact is stable (drift %.5fUU)"),
			*Prefix, FMath::Abs(static_cast<float>(Visual->GetCrouchContactMinWorldZ() - HeldContactZ))),
			FMath::Abs(static_cast<float>(Visual->GetCrouchContactMinWorldZ() - HeldContactZ)) <= StableToleranceUU);

		// Rebuilding the mesh during crouch must preserve the same corrected root after
		// the next real transfer, rather than applying a second correction.
		const float RootBeforeReinitialize = RigRoot->GetRelativeLocation().Z;
		Visual->InitializeFromMobileSuit(&MobileSuitData);
		Visual->TransferPoseForAutomationTests();
		Test.TestTrue(FString::Printf(TEXT("%s reinitializing during crouch preserves corrected root"), *Prefix),
			FMath::Abs(RigRoot->GetRelativeLocation().Z - RootBeforeReinitialize) <= RestToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s reinitializing during crouch preserves ground contact"), *Prefix),
			FMath::Abs(static_cast<float>(Visual->GetCrouchContactMinWorldZ() - StandingGroundZ)) <= GroundToleranceUU);

		// Inputs which explicitly make contact correction unsafe leave the solver's
		// crouch root untouched: rest - CrouchGroundOffset.
		struct FContactGate
		{
			const TCHAR* Name;
			void (*Set)(FMSProceduralPoseInputs&);
		};
		const FContactGate Gates[] = {
			{TEXT("boost"), [](FMSProceduralPoseInputs& I) { I.bIsBoosting = true; }},
			{TEXT("airborne"), [](FMSProceduralPoseInputs& I) { I.bIsInAir = true; }},
			{TEXT("dead"), [](FMSProceduralPoseInputs& I) { I.bIsDead = true; }},
			{TEXT("authored motion"), [](FMSProceduralPoseInputs& I) { I.bHasAuthoredMotion = true; }},
		};
		for (const FContactGate& Gate : Gates)
		{
			FMSProceduralPoseInputs GatedCrouching = Crouching;
			Gate.Set(GatedCrouching);
			EvaluateAndTransfer(GatedCrouching, NextDelta());
			const float ExpectedRootZ = RestRootZ - Animator->GetPoseState().CrouchGroundOffset;
			Test.TestTrue(FString::Printf(TEXT("%s %s disables contact correction"), *Prefix, Gate.Name),
				FMath::Abs(RigRoot->GetRelativeLocation().Z - ExpectedRootZ) <= RestToleranceUU);
		}

		// Leave the mesh in a standing pose and verify both the root and feet return
		// to the original reference without touching gameplay transforms.
		RunFor(Standing, 2.0f);
		const float FinalRootZ = RigRoot->GetRelativeLocation().Z;
		const double FinalContactZ = Visual->GetCrouchContactMinWorldZ();
		Test.TestTrue(FString::Printf(TEXT("%s stand-up restores rig root (error %.5fUU)"),
			*Prefix, FMath::Abs(FinalRootZ - RestRootZ)), FMath::Abs(FinalRootZ - RestRootZ) <= RestToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s stand-up restores foot contact (error %.5fUU)"),
			*Prefix, FMath::Abs(static_cast<float>(FinalContactZ - StandingGroundZ))),
			FMath::Abs(static_cast<float>(FinalContactZ - StandingGroundZ)) <= RestToleranceUU);
		Test.TestTrue(FString::Printf(TEXT("%s crouch root moved during the test"), *Prefix),
			FMath::Abs(CrouchRootZ - RestRootZ) > GroundToleranceUU);

		TestComponentStateUnchanged(Test, ComponentBefore, *Unit, *Capsule, *Prefix);
		return true;
	}
}

bool FMSMeshCrouchContactAcceptanceTest::RunTest(const FString& Parameters)
{
	UMSMobileSuitData* MobileSuitData = LoadObject<UMSMobileSuitData>(nullptr, MeshCrouchMPU01DataPath);
	if (!TestNotNull(TEXT("MPU01 ThreeJS data asset exists"), MobileSuitData) || !MobileSuitData)
	{
		return false;
	}
	TestTrue(TEXT("MPU01 ThreeJS data asset references a real skeletal mesh"),
		!MobileSuitData->VisualMesh.IsNull());
	if (MobileSuitData->VisualMesh.IsNull())
	{
		return false;
	}
	USkeletalMesh* Mesh = MobileSuitData->VisualMesh.LoadSynchronous();
	if (!TestNotNull(TEXT("MPU01 ThreeJS skeletal mesh loads"), Mesh) || !Mesh)
	{
		return false;
	}

	static const float Delta30[] = {1.0f / 30.0f};
	static const float Delta60[] = {1.0f / 60.0f};
	static const float Delta120[] = {1.0f / 120.0f};
	static const float VariableDelta[] = {0.011f, 0.019f, 0.033f, 0.008f, 0.025f, 0.0167f, 0.029f, 0.0125f};
	const FCrouchContactFrameProfile Profiles[] = {
		{TEXT("30fps"), Delta30, UE_ARRAY_COUNT(Delta30)},
		{TEXT("60fps"), Delta60, UE_ARRAY_COUNT(Delta60)},
		{TEXT("120fps"), Delta120, UE_ARRAY_COUNT(Delta120)},
		{TEXT("variable-delta"), VariableDelta, UE_ARRAY_COUNT(VariableDelta)},
	};

	bool bAllProfilesPassed = true;
	for (const FCrouchContactFrameProfile& Profile : Profiles)
	{
		bAllProfilesPassed &= RunContactProfile(*this, Profile, *MobileSuitData);
	}
	return bAllProfilesPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
