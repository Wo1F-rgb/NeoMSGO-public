// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSProceduralPoseSolver.h"
#include "Components/SceneComponent.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechCrouchQuaternionContinuityTest,
	"NeoMSGO.Animation.MechRig.CrouchQuaternionContinuity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMSMechCrouchTestWorld
	{
		UWorld* World = nullptr;

		FMSMechCrouchTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMechCrouchContinuityWorld"));
			if (!World || !GEngine)
			{
				return;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMSMechCrouchTestWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	struct FJointExpectation
	{
		FName Name;
		float CrouchPitch = 0.0f;
	};

	struct FFrameProfile
	{
		const TCHAR* Name = TEXT("unknown");
		const float* Deltas = nullptr;
		int32 DeltaCount = 0;
		bool bLegacyKneeAngles = false;
	};

	float QuaternionDistanceDegrees(const FQuat& A, const FQuat& B)
	{
		const FQuat ANormalized = A.GetNormalized();
		const FQuat BNormalized = B.GetNormalized();
		const float Dot = FMath::Clamp(FMath::Abs(ANormalized | BNormalized), 0.0f, 1.0f);
		return FMath::RadiansToDegrees(2.0f * FMath::Acos(Dot));
	}

	// UE Pitch rotates around local Y. A pure pitch therefore leaves the local
	// right axis at (0, 1, 0); yaw/roll leakage changes that axis.
	float PitchPlaneDeviationDegrees(const FQuat& Rotation)
	{
		const FVector LocalRight = Rotation.GetNormalized().RotateVector(FVector::RightVector);
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(LocalRight.Y, -1.0f, 1.0f)));
	}

	float ExpectedCrouchPitch(const FMSProceduralPoseSettings& Settings, const int32 JointIndex,
		const float CrouchAlpha)
	{
		switch (JointIndex)
		{
		case 0: return -Settings.KneelThighAngle * CrouchAlpha;
		case 1: return Settings.CrouchThighAngle * CrouchAlpha;
		case 2: return -Settings.KneelKneeAngle * CrouchAlpha;
		case 3: return -Settings.CrouchKneeAngle * CrouchAlpha;
		case 4: return (Settings.KneelThighAngle + Settings.KneelKneeAngle)
			* CrouchAlpha * (1.0f - CrouchAlpha) * Settings.FootLevelRatio;
		case 5: return (Settings.CrouchKneeAngle - Settings.CrouchThighAngle)
			* CrouchAlpha * Settings.FootLevelRatio;
		default: return 0.0f;
		}
	}

	bool RunProfile(FAutomationTestBase& Test, const FFrameProfile& Profile)
	{
		FMSMechCrouchTestWorld TestWorld;
		AMSPlayerCharacter* Unit = SpawnPlayer(TestWorld.World);
		UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
		UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: player exists"), Profile.Name), Unit)
			|| !Test.TestNotNull(FString::Printf(TEXT("%s: rig exists"), Profile.Name), Rig)
			|| !Test.TestNotNull(FString::Printf(TEXT("%s: animator exists"), Profile.Name), Animator))
		{
			return false;
		}

		if (Profile.bLegacyKneeAngles)
		{
			Animator->SetCrouchKneeAnglesForAutomationTests(150.0f, 145.0f);
		}
		FMSProceduralPoseSettings Settings;
		if (Profile.bLegacyKneeAngles)
		{
			Settings.CrouchKneeAngle = 150.0f;
			Settings.KneelKneeAngle = 145.0f;
		}
		const float ExpectedCrouchKnee = Profile.bLegacyKneeAngles ? 150.0f : 90.0f;
		const float ExpectedKneelKnee = Profile.bLegacyKneeAngles ? 145.0f : 90.0f;
		Test.TestTrue(FString::Printf(TEXT("%s: crouch knee setting is %.0f degrees"), Profile.Name, ExpectedCrouchKnee),
			FMath::IsNearlyEqual(Settings.CrouchKneeAngle, ExpectedCrouchKnee));
		Test.TestTrue(FString::Printf(TEXT("%s: kneeling knee setting is %.0f degrees"), Profile.Name, ExpectedKneelKnee),
			FMath::IsNearlyEqual(Settings.KneelKneeAngle, ExpectedKneelKnee));
		const FJointExpectation Expectations[] = {
			{TEXT("Thigh_L"), -Settings.KneelThighAngle},
			{TEXT("Thigh_R"), Settings.CrouchThighAngle},
			{TEXT("Shin_L"), -Settings.KneelKneeAngle},
			{TEXT("Shin_R"), -Settings.CrouchKneeAngle},
			{TEXT("Foot_L"), 0.0f},
			{TEXT("Foot_R"), (Settings.CrouchKneeAngle - Settings.CrouchThighAngle) * Settings.FootLevelRatio},
		};
		TArray<USceneComponent*> Joints;
		TArray<FQuat> RestRotations;
		TArray<FQuat> CrouchTargets;
		for (const FJointExpectation& Expectation : Expectations)
		{
			USceneComponent* Joint = Rig->GetJoint(Expectation.Name);
			if (!Test.TestNotNull(FString::Printf(TEXT("%s: joint %s exists"), Profile.Name,
				*Expectation.Name.ToString()), Joint))
			{
				return false;
			}
			Joints.Add(Joint);
			RestRotations.Add(Joint->GetRelativeRotation().Quaternion());
			CrouchTargets.Add(FRotator(Expectation.CrouchPitch, 0.0f, 0.0f).Quaternion());
		}

		const float LimbSpeedDegreesPerSecond = 200.0f;
		const float SpeedToleranceDegrees = 0.25f;
		const float PlaneToleranceDegrees = 1.0f;
		const float PoseToleranceDegrees = 1.0f;
		const FMSProceduralPoseInputs Standing;
		FMSProceduralPoseInputs Crouching;
		Crouching.bIsCrouching = true;

		float MaxSpeedExcess = 0.0f;
		float MaxPlaneDeviation = 0.0f;
		float MaxCrouchTargetError = 0.0f;
		float MaxFrameTargetError = 0.0f;
		float MaxCrouchHoldStep = 0.0f;
		float MaxHoldFootWorldStep = 0.0f;
		float CrouchRootDrop = 0.0f;
		bool bAnimatorKneeOutputsMatchDefaultSettings = false;
		float RestRootZ = Rig->GetRigRoot()->GetRelativeLocation().Z;
		TArray<FQuat> PreviousRotations = RestRotations;
		FVector PreviousHoldFootL = FVector::ZeroVector;
		FVector PreviousHoldFootR = FVector::ZeroVector;
		bool bHaveHoldFootLocations = false;

		auto EvaluateFrame = [&](const FMSProceduralPoseInputs& Inputs, const float DeltaSeconds,
			const bool bTrackHold)
		{
			const float SafeDelta = FMath::Max(DeltaSeconds, UE_SMALL_NUMBER);
			Animator->EvaluatePose(Inputs, DeltaSeconds);
			const float CrouchAlpha = FMath::Clamp(Animator->GetPoseState().CrouchPoseAlpha, 0.0f, 1.0f);
			for (int32 Index = 0; Index < Joints.Num(); ++Index)
			{
				const FQuat Current = Joints[Index]->GetRelativeRotation().Quaternion();
				const FQuat Expected = FRotator(ExpectedCrouchPitch(Settings, Index, CrouchAlpha),
					0.0f, 0.0f).Quaternion();
				const float Step = QuaternionDistanceDegrees(PreviousRotations[Index], Current);
				MaxSpeedExcess = FMath::Max(MaxSpeedExcess,
					Step - LimbSpeedDegreesPerSecond * SafeDelta);
				MaxPlaneDeviation = FMath::Max(MaxPlaneDeviation, PitchPlaneDeviationDegrees(Current));
				MaxFrameTargetError = FMath::Max(MaxFrameTargetError,
					QuaternionDistanceDegrees(Current, Expected));
				if (bTrackHold)
				{
					MaxCrouchHoldStep = FMath::Max(MaxCrouchHoldStep, Step);
				}
				PreviousRotations[Index] = Current;
			}
			if (bTrackHold)
			{
				const FVector HoldFootL = Joints[4]->GetComponentLocation();
				const FVector HoldFootR = Joints[5]->GetComponentLocation();
				if (bHaveHoldFootLocations)
				{
					MaxHoldFootWorldStep = FMath::Max(MaxHoldFootWorldStep,
						FVector::Dist(HoldFootL, PreviousHoldFootL));
					MaxHoldFootWorldStep = FMath::Max(MaxHoldFootWorldStep,
						FVector::Dist(HoldFootR, PreviousHoldFootR));
				}
				PreviousHoldFootL = HoldFootL;
				PreviousHoldFootR = HoldFootR;
				bHaveHoldFootLocations = true;
			}
		};

		auto NextDelta = [Profile, FrameIndex = 0]() mutable
		{
			const float Result = Profile.Deltas[FrameIndex % Profile.DeltaCount];
			++FrameIndex;
			return Result;
		};
		auto RunForDuration = [&](const FMSProceduralPoseInputs& Inputs, const float DurationSeconds,
			const bool bTrackHold)
		{
			float Elapsed = 0.0f;
			while (Elapsed < DurationSeconds)
			{
				const float DeltaSeconds = NextDelta();
				EvaluateFrame(Inputs, DeltaSeconds, bTrackHold);
				Elapsed += DeltaSeconds;
			}
		};

		// Prime a genuine multi-frame standing pose so the test covers the same
		// component path as runtime and records the authored rest quaternions.
		RunForDuration(Standing, 1.0f, false);
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			RestRotations[Index] = Joints[Index]->GetRelativeRotation().Quaternion();
		}
		PreviousRotations = RestRotations;
		RestRootZ = Rig->GetRigRoot()->GetRelativeLocation().Z;

		// Crouch for several seconds. The hold is long enough for both the
		// solver's blend and the 200 deg/s joint limiter to settle.
		RunForDuration(Crouching, 3.5f, false);
		CrouchRootDrop = RestRootZ - Rig->GetRigRoot()->GetRelativeLocation().Z;
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			MaxCrouchTargetError = FMath::Max(MaxCrouchTargetError,
				QuaternionDistanceDegrees(Joints[Index]->GetRelativeRotation().Quaternion(), CrouchTargets[Index]));
		}
		const FMSProceduralPoseState& CrouchState = Animator->GetPoseState();
		bAnimatorKneeOutputsMatchDefaultSettings =
			FMath::IsNearlyEqual(CrouchState.LeftCrouchKneeAngle, Settings.KneelKneeAngle, 0.01f)
			&& FMath::IsNearlyEqual(CrouchState.RightCrouchKneeAngle, Settings.CrouchKneeAngle, 0.01f);

		// Hold the input through another multi-second interval. The old Euler
		// conversion repeatedly enters the +/-90 degree singularity here and
		// produces yaw/roll flips instead of a stationary knee quaternion.
		RunForDuration(Crouching, 2.0f, true);
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			MaxCrouchTargetError = FMath::Max(MaxCrouchTargetError,
				QuaternionDistanceDegrees(Joints[Index]->GetRelativeRotation().Quaternion(), CrouchTargets[Index]));
		}

		// Release crouch and continue sampling until the pose returns to its
		// original quaternions. This catches a one-shot endpoint test that would
		// miss transient pitch-plane violations during the stand-up transition.
		RunForDuration(Standing, 3.5f, false);
		float MaxRestError = 0.0f;
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			MaxRestError = FMath::Max(MaxRestError,
				QuaternionDistanceDegrees(Joints[Index]->GetRelativeRotation().Quaternion(), RestRotations[Index]));
		}

		const float ExpectedRootDrop = Settings.CrouchLowerHeight * 100.0f * MSGO::MSScale;
		const float RootDropTolerance = FMath::Max(0.5f, ExpectedRootDrop * 0.05f);
		const FString Prefix = FString::Printf(TEXT("%s: "), Profile.Name);
		Test.TestTrue(Prefix + FString::Printf(TEXT("crouch knees/feet converge to target quaternions (max error %.3f deg)"),
			MaxCrouchTargetError), MaxCrouchTargetError <= PoseToleranceDegrees);
		const FString KneeOutputMessage = Profile.bLegacyKneeAngles
			? TEXT("animator pose outputs use the explicit legacy knee angles")
			: TEXT("animator pose outputs use the 90 degree default knees");
		Test.TestTrue(Prefix + KneeOutputMessage,
			bAnimatorKneeOutputsMatchDefaultSettings);
		Test.TestTrue(Prefix + FString::Printf(TEXT("every frame tracks its alpha-derived target (max error %.3f deg)"),
			MaxFrameTargetError), MaxFrameTargetError <= PoseToleranceDegrees);
		Test.TestTrue(Prefix + FString::Printf(TEXT("crouch hold stays stationary (max frame %.3f deg)"),
			MaxCrouchHoldStep), MaxCrouchHoldStep <= 0.25f);
		Test.TestTrue(Prefix + FString::Printf(TEXT("held feet stay within 0.1UU per frame (max %.4fUU)"),
			MaxHoldFootWorldStep), MaxHoldFootWorldStep <= 0.1f);
		Test.TestTrue(Prefix + FString::Printf(TEXT("crouch root drop matches %.2fUU (actual %.2fUU)"),
			ExpectedRootDrop, CrouchRootDrop), FMath::Abs(CrouchRootDrop - ExpectedRootDrop) <= RootDropTolerance);
		Test.TestTrue(Prefix + FString::Printf(TEXT("all leg and foot frames stay in the pitch plane (max deviation %.3f deg)"),
			MaxPlaneDeviation), MaxPlaneDeviation <= PlaneToleranceDegrees);
		Test.TestTrue(Prefix + FString::Printf(TEXT("joint angular step respects 200 deg/s (max excess %.3f deg)"),
			MaxSpeedExcess), MaxSpeedExcess <= SpeedToleranceDegrees);
		Test.TestTrue(Prefix + FString::Printf(TEXT("stand-up returns to the rest quaternions (max error %.3f deg)"),
			MaxRestError), MaxRestError <= PoseToleranceDegrees);
		return MaxCrouchTargetError <= PoseToleranceDegrees
			&& bAnimatorKneeOutputsMatchDefaultSettings
			&& MaxFrameTargetError <= PoseToleranceDegrees
			&& MaxCrouchHoldStep <= 0.25f
			&& MaxHoldFootWorldStep <= 0.1f
			&& FMath::Abs(CrouchRootDrop - ExpectedRootDrop) <= RootDropTolerance
			&& MaxPlaneDeviation <= PlaneToleranceDegrees
			&& MaxSpeedExcess <= SpeedToleranceDegrees
			&& MaxRestError <= PoseToleranceDegrees;
	}
}

bool FMSMechCrouchQuaternionContinuityTest::RunTest(const FString& Parameters)
{
	static const float Delta30[] = {1.0f / 30.0f};
	static const float Delta60[] = {1.0f / 60.0f};
	static const float Delta120[] = {1.0f / 120.0f};
	static const float VariableDelta[] = {
		0.011f, 0.019f, 0.033f, 0.008f, 0.025f, 0.0167f, 0.029f, 0.0125f, 0.35f,
	};
	const FFrameProfile Profiles[] = {
		{TEXT("30fps"), Delta30, UE_ARRAY_COUNT(Delta30)},
		{TEXT("60fps"), Delta60, UE_ARRAY_COUNT(Delta60)},
		{TEXT("120fps"), Delta120, UE_ARRAY_COUNT(Delta120)},
		{TEXT("variable-delta"), VariableDelta, UE_ARRAY_COUNT(VariableDelta)},
		{TEXT("legacy-60fps-145-150"), Delta60, UE_ARRAY_COUNT(Delta60), true},
	};

	bool bAllProfilesPassed = true;
	for (const FFrameProfile& Profile : Profiles)
	{
		bAllProfilesPassed &= RunProfile(*this, Profile);
	}

	return bAllProfilesPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
