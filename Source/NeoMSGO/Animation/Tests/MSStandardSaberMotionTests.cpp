// Copyright NeoMSGO Project. All Rights Reserved.
//
// GM N beam-saber motion invariants.  These tests intentionally avoid timing
// snapshots: the authored seconds are tuning values until an original-game
// measurement is available.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberDirectionTest,
	"NeoMSGO.Animation.StandardSaber.DirectionInvariants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberSamplerInvariantTest,
	"NeoMSGO.Animation.StandardSaber.SamplerInvariants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberBladeFKTest,
	"NeoMSGO.Animation.StandardSaber.BladeForwardKinematics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberAnimatorPathTest,
	"NeoMSGO.Animation.StandardSaber.AnimatorPreviewPath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberMPU01MeshDirectionTest,
	"NeoMSGO.Animation.StandardSaber.MPU01MeshDirection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	constexpr const TCHAR* StandardSaberMPU01DataPath =
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS");

	FVector GetBladeDirection(const FMSStandardSaberPose& Pose)
	{
		return (Pose.UpperArm * Pose.Forearm * Pose.WeaponMount)
			.RotateVector(FVector::ForwardVector).GetSafeNormal();
	}

	void TestPoseNear(FAutomationTestBase& Test, const FMSStandardSaberPose& Actual,
		const FMSStandardSaberPose& Expected, const FString& Prefix)
	{
		constexpr float QuatToleranceRadians = 0.00002f;
		Test.TestTrue(Prefix + TEXT(" torso quaternion matches"),
			Actual.Torso.AngularDistance(Expected.Torso) <= QuatToleranceRadians);
		Test.TestTrue(Prefix + TEXT(" upper-arm quaternion matches"),
			Actual.UpperArm.AngularDistance(Expected.UpperArm) <= QuatToleranceRadians);
		Test.TestTrue(Prefix + TEXT(" forearm quaternion matches"),
			Actual.Forearm.AngularDistance(Expected.Forearm) <= QuatToleranceRadians);
		Test.TestTrue(Prefix + TEXT(" weapon-mount quaternion matches"),
			Actual.WeaponMount.AngularDistance(Expected.WeaponMount) <= QuatToleranceRadians);
		Test.TestTrue(Prefix + TEXT(" body yaw matches"),
			FMath::IsNearlyEqual(Actual.BodyYaw, Expected.BodyYaw, 0.0001f));
		Test.TestTrue(Prefix + TEXT(" left thigh matches"),
			FMath::IsNearlyEqual(Actual.ThighLeft, Expected.ThighLeft, 0.0001f));
		Test.TestTrue(Prefix + TEXT(" right thigh matches"),
			FMath::IsNearlyEqual(Actual.ThighRight, Expected.ThighRight, 0.0001f));
		Test.TestTrue(Prefix + TEXT(" left knee matches"),
			FMath::IsNearlyEqual(Actual.KneeLeft, Expected.KneeLeft, 0.0001f));
		Test.TestTrue(Prefix + TEXT(" right knee matches"),
			FMath::IsNearlyEqual(Actual.KneeRight, Expected.KneeRight, 0.0001f));
	}

	struct FMSStandardSaberTestWorld
	{
		UWorld* World = nullptr;

		FMSStandardSaberTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false,
				TEXT("MSStandardSaberMotionTestWorld"));
			if (!World)
			{
				return;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMSStandardSaberTestWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnStandardSaberTestUnit(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	FQuat GetRigRotation(const UMSMechRigComponent& Rig, const FName JointName)
	{
		const USceneComponent* Joint = Rig.GetJoint(JointName);
		return Joint ? Joint->GetRelativeRotation().Quaternion() : FQuat::Identity;
	}

	int32 SideSign(const float Value)
	{
		return Value > 0.025f ? 1 : Value < -0.025f ? -1 : 0;
	}
}

bool FMSStandardSaberDirectionTest::RunTest(const FString& Parameters)
{
	for (int32 AttackIndex = 0; AttackIndex <= 2; ++AttackIndex)
	{
		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
		const FMSStandardSaberPose StartPose =
			FMSStandardSaberMotion::Sample(AttackIndex, Timing.ActiveStart);
		const FMSStandardSaberPose EndPose =
			FMSStandardSaberMotion::Sample(AttackIndex, Timing.ActiveEnd);
		const FVector StartDirection = GetBladeDirection(StartPose);
		const FVector EndDirection = GetBladeDirection(EndPose);
		const FString Prefix = FString::Printf(TEXT("attack %d"), AttackIndex);

		if (AttackIndex == 0)
		{
			TestTrue(Prefix + TEXT(" starts high on the right"),
				StartDirection.Y > 0.05f && StartDirection.Z > 0.05f);
			TestTrue(Prefix + TEXT(" ends low on the left"),
				EndDirection.Y < -0.05f && EndDirection.Z < -0.05f);
		}
		else
		{
			TestTrue(Prefix + TEXT(" starts on the left"), StartDirection.Y < -0.5f);
			TestTrue(Prefix + TEXT(" ends on the right"), EndDirection.Y > 0.5f);
			TestTrue(Prefix + TEXT(" starts as a horizontal cut"), FMath::Abs(StartDirection.Z) < 0.45f);
			TestTrue(Prefix + TEXT(" ends as a horizontal cut"), FMath::Abs(EndDirection.Z) < 0.45f);
		}

		float MinimumAdjacentDot = 1.0f;
		for (int32 SampleIndex = 1; SampleIndex <= 120; ++SampleIndex)
		{
			const float PreviousTime = Timing.Duration * (SampleIndex - 1) / 120.0f;
			const float CurrentTime = Timing.Duration * SampleIndex / 120.0f;
			const FVector PreviousDirection = GetBladeDirection(
				FMSStandardSaberMotion::Sample(AttackIndex, PreviousTime));
			const FVector CurrentDirection = GetBladeDirection(
				FMSStandardSaberMotion::Sample(AttackIndex, CurrentTime));
			MinimumAdjacentDot = FMath::Min(MinimumAdjacentDot,
				PreviousDirection | CurrentDirection);
		}
		TestTrue(FString::Printf(TEXT("%s has no abrupt opposite-side quaternion path (min dot %.4f)"),
			*Prefix, MinimumAdjacentDot), MinimumAdjacentDot > 0.90f);

		int32 SideCrossings = 0;
		int32 PreviousSide = SideSign(StartDirection.Y);
		for (int32 SampleIndex = 1; SampleIndex <= 64; ++SampleIndex)
		{
			const float Time = FMath::Lerp(Timing.ActiveStart, Timing.ActiveEnd,
				static_cast<float>(SampleIndex) / 64.0f);
			const int32 CurrentSide = SideSign(GetBladeDirection(
				FMSStandardSaberMotion::Sample(AttackIndex, Time)).Y);
			if (CurrentSide != 0 && PreviousSide != 0 && CurrentSide != PreviousSide)
			{
				++SideCrossings;
			}
			if (CurrentSide != 0)
			{
				PreviousSide = CurrentSide;
			}
		}
		TestEqual(FString::Printf(TEXT("%s crosses the center line once during its hit window"), *Prefix),
			SideCrossings, 1);
	}
	return true;
}

bool FMSStandardSaberSamplerInvariantTest::RunTest(const FString& Parameters)
{
	static const float FrameSchedules[][4] = {
		{1.0f / 30.0f, 1.0f / 30.0f, 1.0f / 30.0f, 1.0f / 30.0f},
		{1.0f / 60.0f, 1.0f / 60.0f, 1.0f / 60.0f, 1.0f / 60.0f},
		{1.0f / 120.0f, 1.0f / 120.0f, 1.0f / 120.0f, 1.0f / 120.0f},
		{0.011f, 0.019f, 0.033f, 0.008f},
	};

	for (int32 AttackIndex = 0; AttackIndex <= 2; ++AttackIndex)
	{
		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
		const float TargetTime = Timing.ActiveStart + (Timing.ActiveEnd - Timing.ActiveStart) * 0.37f;
		const FMSStandardSaberPose Expected = FMSStandardSaberMotion::Sample(AttackIndex, TargetTime);
		for (int32 ScheduleIndex = 0; ScheduleIndex < UE_ARRAY_COUNT(FrameSchedules); ++ScheduleIndex)
		{
			float Elapsed = 0.0f;
			int32 Frame = 0;
			FMSStandardSaberPose Actual = FMSStandardSaberMotion::Sample(AttackIndex, 0.0f);
			while (Elapsed < TargetTime)
			{
				const float Delta = FrameSchedules[ScheduleIndex][Frame % 4];
				++Frame;
				Elapsed = FMath::Min(TargetTime, Elapsed + Delta);
				Actual = FMSStandardSaberMotion::Sample(AttackIndex, Elapsed);
			}
			TestPoseNear(*this, Actual, Expected, FString::Printf(
				TEXT("attack %d schedule %d"), AttackIndex, ScheduleIndex));
		}

		const FMSStandardSaberPose ReadyPose = FMSStandardSaberMotion::Sample(INDEX_NONE, 0.0f);
		const FMSStandardSaberPose Terminal = FMSStandardSaberMotion::Sample(
			AttackIndex, Timing.Duration + 10.0f);
		TestPoseNear(*this, Terminal, ReadyPose, FString::Printf(
			TEXT("attack %d returns to ready at terminal time"), AttackIndex));
	}
	return true;
}

bool FMSStandardSaberBladeFKTest::RunTest(const FString& Parameters)
{
	const int32 AttackIndex = 1;
	const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
	const float Time = Timing.ActiveStart + (Timing.ActiveEnd - Timing.ActiveStart) * 0.5f;
	const float BladeLength = 123.0f;
	const FMSStandardSaberPose Pose = FMSStandardSaberMotion::Sample(AttackIndex, Time);

	FVector ActualStart = FVector::ZeroVector;
	FVector ActualEnd = FVector::ZeroVector;
	FMSStandardSaberMotion::SampleBlade(AttackIndex, Time, BladeLength, ActualStart, ActualEnd);
	const FQuat Body = FRotator(0.0f, Pose.BodyYaw, 0.0f).Quaternion();
	const FVector Shoulder = Body.RotateVector(FVector(0.0f, 42.0f, 45.0f));
	const FVector Elbow = Shoulder + Pose.UpperArm.RotateVector(FVector(0.0f, 0.0f, -32.0f));
	const FQuat Forearm = Pose.UpperArm * Pose.Forearm;
	const FVector ExpectedStart = Elbow + Forearm.RotateVector(FVector(15.0f, 0.0f, -28.0f));
	const FQuat Blade = Forearm * Pose.WeaponMount;
	const FVector ExpectedEnd = ExpectedStart + Blade.RotateVector(FVector(BladeLength, 0.0f, 0.0f));

	TestTrue(TEXT("SampleBlade start matches arm/elbow/wrist FK"),
		ActualStart.Equals(ExpectedStart, 0.001f));
	TestTrue(TEXT("SampleBlade end matches arm/elbow/wrist FK"),
		ActualEnd.Equals(ExpectedEnd, 0.001f));
	TestTrue(TEXT("SampleBlade length is preserved"),
		FMath::IsNearlyEqual((ActualEnd - ActualStart).Size(), BladeLength, 0.001f));
	return true;
}

bool FMSStandardSaberAnimatorPathTest::RunTest(const FString& Parameters)
{
	FMSStandardSaberTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnStandardSaberTestUnit(TestWorld.World);
	if (!TestNotNull(TEXT("standard saber preview world exists"), TestWorld.World)
		|| !TestNotNull(TEXT("standard saber preview unit spawns"), Unit))
	{
		return false;
	}
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	if (!TestNotNull(TEXT("standard saber animator exists"), Animator)
		|| !TestNotNull(TEXT("standard saber ghost rig exists"), Rig))
	{
		return false;
	}

	for (int32 AttackIndex = 0; AttackIndex <= 2; ++AttackIndex)
	{
		Animator->StartStandardSaberMotion(AttackIndex);
		TestEqual(FString::Printf(TEXT("attack %d starts through animator API"), AttackIndex),
			Animator->GetStandardSaberAttackIndex(), AttackIndex);
		Animator->StopStandardSaberMotion();
		TestEqual(FString::Printf(TEXT("attack %d stop returns to ready"), AttackIndex),
			Animator->GetStandardSaberAttackIndex(), INDEX_NONE);

		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
		const float Time = Timing.ActiveStart + (Timing.ActiveEnd - Timing.ActiveStart) * 0.5f;
		const FMSStandardSaberPose Expected = FMSStandardSaberMotion::Sample(AttackIndex, Time);
		Animator->PreviewStandardSaberPose(AttackIndex, Time);
		TestTrue(FString::Printf(TEXT("attack %d preview applies upper-arm FK"), AttackIndex),
			GetRigRotation(*Rig, TEXT("UpperArm_R")).AngularDistance(Expected.UpperArm) <= 0.00002f);
		TestTrue(FString::Printf(TEXT("attack %d preview applies forearm FK"), AttackIndex),
			GetRigRotation(*Rig, TEXT("Forearm_R")).AngularDistance(Expected.Forearm) <= 0.00002f);
		TestTrue(FString::Printf(TEXT("attack %d preview applies wrist FK"), AttackIndex),
			GetRigRotation(*Rig, TEXT("WeaponMount")).AngularDistance(Expected.WeaponMount) <= 0.00002f);
	}
	return true;
}

bool FMSStandardSaberMPU01MeshDirectionTest::RunTest(const FString& Parameters)
{
	UMSMobileSuitData* MobileSuitData = LoadObject<UMSMobileSuitData>(nullptr, StandardSaberMPU01DataPath);
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
	TestTrue(TEXT("MPU01 real mesh has the right weapon mount bone"),
		Mesh->GetRefSkeleton().FindBoneIndex(TEXT("weapon_mount_r")) != INDEX_NONE);

	FMSStandardSaberTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnStandardSaberTestUnit(TestWorld.World);
	if (!TestNotNull(TEXT("MPU01 mesh test world exists"), TestWorld.World)
		|| !TestNotNull(TEXT("MPU01 mesh test unit spawns"), Unit))
	{
		return false;
	}
	Unit->InitializeUnit(MobileSuitData);
	// Exercise the real mesh FK under a non-zero actor yaw.  The sampler must
	// include the component/world transform rather than assuming yaw zero.
	Unit->SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UPoseableMeshComponent* Poseable = Visual ? Visual->GetPoseableMesh() : nullptr;
	if (!TestNotNull(TEXT("MPU01 animator exists"), Animator)
		|| !TestNotNull(TEXT("MPU01 ghost rig exists"), Rig)
		|| !TestNotNull(TEXT("MPU01 mesh visual exists"), Visual)
		|| !TestNotNull(TEXT("MPU01 poseable mesh is active"), Poseable))
	{
		return false;
	}

	const FName MountBone(TEXT("weapon_mount_r"));
	const FName HeadBone(TEXT("head"));
	Visual->TransferPoseForAutomationTests();
	for (int32 AttackIndex = 0; AttackIndex <= 2; ++AttackIndex)
	{
		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
		const float Times[] = {
			Timing.ActiveStart,
			(Timing.ActiveStart + Timing.ActiveEnd) * 0.5f,
			Timing.ActiveEnd};
		for (int32 TimeIndex = 0; TimeIndex < UE_ARRAY_COUNT(Times); ++TimeIndex)
		{
			const float Time = Times[TimeIndex];
			const FString Prefix = FString::Printf(TEXT("MPU01 yaw90 attack %d time %d"),
				AttackIndex, TimeIndex);
			const FTransform BeforeSample = Poseable->GetBoneTransformByName(
				MountBone, EBoneSpaces::WorldSpace);
			FTransform SampledMount = FTransform::Identity;
			TestTrue(Prefix + TEXT(" real-bone sampler succeeds"),
				Visual->SampleStandardSaberMountWorld(AttackIndex, Time, SampledMount));
			const FTransform AfterSample = Poseable->GetBoneTransformByName(
				MountBone, EBoneSpaces::WorldSpace);
			TestTrue(Prefix + TEXT(" sampler leaves weapon mount position unchanged"),
				BeforeSample.GetLocation().Equals(AfterSample.GetLocation(), 0.001f));
			TestTrue(Prefix + TEXT(" sampler leaves weapon mount rotation unchanged"),
				BeforeSample.GetRotation().AngularDistance(AfterSample.GetRotation()) <= 0.00001f);

			Animator->PreviewStandardSaberPose(AttackIndex, Time);
			Visual->TransferPoseForAutomationTests();
			const FTransform ActualMount = Poseable->GetBoneTransformByName(
				MountBone, EBoneSpaces::WorldSpace);
			TestTrue(Prefix + TEXT(" real weapon mount position matches sampled FK"),
				ActualMount.GetLocation().Equals(SampledMount.GetLocation(), 0.05f));
			TestTrue(Prefix + TEXT(" real weapon mount rotation matches sampled FK"),
				ActualMount.GetRotation().AngularDistance(SampledMount.GetRotation()) <= 0.001f);

			// A second read after TransferPose verifies the API remains observational
			// even when the real poseable mesh is already at the sampled pose.
			const FTransform BeforeSecondSample = ActualMount;
			FTransform DiscardedSample = FTransform::Identity;
			Visual->SampleStandardSaberMountWorld(AttackIndex, Time, DiscardedSample);
			const FTransform AfterSecondSample = Poseable->GetBoneTransformByName(
				MountBone, EBoneSpaces::WorldSpace);
			TestTrue(Prefix + TEXT(" post-transfer sample leaves weapon mount unchanged"),
				BeforeSecondSample.GetLocation().Equals(AfterSecondSample.GetLocation(), 0.001f)
				&& BeforeSecondSample.GetRotation().AngularDistance(
					AfterSecondSample.GetRotation()) <= 0.00001f);

			if ((AttackIndex == 1 || AttackIndex == 2) && TimeIndex == 1)
			{
				const FTransform Head = Poseable->GetBoneTransformByName(
					HeadBone, EBoneSpaces::WorldSpace);
				TestTrue(Prefix + TEXT(" horizontal/D-stage wrist stays below the head"),
					ActualMount.GetLocation().Z < Head.GetLocation().Z);
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
