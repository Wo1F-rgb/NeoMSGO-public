// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechMotionPlaybackTest,
	"NeoMSGO.Animation.TAUsw.MotionJsonPlayback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTAUswV3CompositionTest,
	"NeoMSGO.Animation.TAUsw.V3WeaponMountComposition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMSTask222TestWorld
	{
		UWorld* World = nullptr;
		FMSTask222TestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSTask222TestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FMSTask222TestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnMechMotionTestUnit(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

}

bool FMSMechMotionPlaybackTest::RunTest(const FString& Parameters)
{
	FMSTask222TestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMechMotionTestUnit(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	if (!TestNotNull(TEXT("TAU-sw motion test rig exists"), Rig))
	{
		return false;
	}

	TestTrue(TEXT("authoritative TAU-sw JSON loads"), Rig->LoadDefaultTAUswMotionSet());
	const TArray<FName> ExpectedNames = {
		TEXT("Draw"), TEXT("Idle_Guard"), TEXT("Sheathe"), TEXT("Slash_Dual"), TEXT("Slash_Single")};
	TestEqual(TEXT("all five authored motions are available"), Rig->GetAvailableMotionNames(), ExpectedNames);
	TestEqual(TEXT("Idle_Guard frame count"), Rig->GetMotionFrameCount(TEXT("Idle_Guard")), 61);
	TestEqual(TEXT("Draw frame count"), Rig->GetMotionFrameCount(TEXT("Draw")), 41);
	TestEqual(TEXT("Sheathe frame count"), Rig->GetMotionFrameCount(TEXT("Sheathe")), 41);
	TestEqual(TEXT("Slash_Single frame count"), Rig->GetMotionFrameCount(TEXT("Slash_Single")), 33);
	TestEqual(TEXT("Slash_Dual frame count"), Rig->GetMotionFrameCount(TEXT("Slash_Dual")), 37);

	const FString SyntheticJson = TEXT(R"JSON(
	{
	  "format":"msgo-mech-motion/1", "fps":30,
	  "motions":{
	    "Interp":{"loop":false,"frameCount":2,"durationSec":0.0666667,
	      "joints":{"Torso":[[1,0,0,0],[0,0,0,1]]}},
	    "Loop":{"loop":true,"frameCount":2,"durationSec":0.0666667,
	      "joints":{"Torso":[[1,0,0,0],[0,0,0,1]]}}
	  }
	})JSON");
	FString Error;
	TestTrue(TEXT("synthetic interpolation fixture parses"),
		Rig->LoadMotionSetFromJson(SyntheticJson, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TestTrue(TEXT("interpolation motion starts"), Rig->PlayMotion(TEXT("Interp")));
	TestTrue(TEXT("half-frame seek evaluates"), Rig->SetMotionPlaybackPosition(1.0f / 60.0f));
	const float HalfFrameAngle = FMath::RadiansToDegrees(
		Rig->GetJoint(TEXT("Torso"))->GetRelativeRotation().Quaternion().GetAngle());
	TestTrue(FString::Printf(TEXT("Slerp produces a 90-degree half frame (%.4f)"), HalfFrameAngle),
		FMath::IsNearlyEqual(HalfFrameAngle, 90.0f, 0.05f));

	TestTrue(TEXT("loop motion starts"), Rig->PlayMotion(TEXT("Loop")));
	Rig->UpdateMotion(0.075f);
	TestTrue(TEXT("loop motion remains playing after duration"), Rig->IsMotionPlaying());
	TestTrue(TEXT("loop playback wraps inside duration"),
		Rig->GetMotionPlaybackPosition() >= 0.0f
		&& Rig->GetMotionPlaybackPosition() < 0.0666667f);
	Rig->StopMotion(true);
	TestFalse(TEXT("StopMotion halts playback"), Rig->IsMotionPlaying());
	TestTrue(TEXT("StopMotion reset returns authored joint to identity"),
		Rig->GetJoint(TEXT("Torso"))->GetRelativeRotation().IsNearlyZero(0.01f));
	return true;
}

bool FMSTAUswV3CompositionTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/TAUsw/SK_TAUsw.SK_TAUsw"));
	if (!TestNotNull(TEXT("TAU-sw v3 skeletal mesh exists"), Mesh))
	{
		return false;
	}
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	// UE imports Blender's Armature object as one wrapper bone. The handoff's 22
	// authored bones must all exist beneath it (FBX importer log: 23 digested).
	const TArray<FName> RequiredBones = {
		TEXT("root"), TEXT("pelvis"), TEXT("chest"), TEXT("head"),
		TEXT("shoulder_l"), TEXT("upperarm_l"), TEXT("forearm_l"), TEXT("weapon_mount_l"),
		TEXT("shoulder_r"), TEXT("upperarm_r"), TEXT("forearm_r"), TEXT("weapon_mount_r"),
		TEXT("thruster_back_l"), TEXT("thruster_back_r"),
		TEXT("thigh_l"), TEXT("shin_l"), TEXT("foot_l"), TEXT("thruster_foot_l"),
		TEXT("thigh_r"), TEXT("shin_r"), TEXT("foot_r"), TEXT("thruster_foot_r")};
	for (const FName BoneName : RequiredBones)
	{
		TestTrue(*FString::Printf(TEXT("handoff bone %s exists"), *BoneName.ToString()),
			RefSkeleton.FindBoneIndex(BoneName) != INDEX_NONE);
	}
	const int32 LeftMountIndex = RefSkeleton.FindBoneIndex(TEXT("weapon_mount_l"));
	const int32 RightMountIndex = RefSkeleton.FindBoneIndex(TEXT("weapon_mount_r"));
	if (!TestTrue(TEXT("weapon_mount_l exists"), LeftMountIndex != INDEX_NONE)
		|| !TestTrue(TEXT("weapon_mount_r exists"), RightMountIndex != INDEX_NONE))
	{
		return false;
	}

	FMSTask222TestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMechMotionTestUnit(TestWorld.World);
	if (!TestNotNull(TEXT("TAU-sw v3 transfer unit spawns"), Unit))
	{
		return false;
	}
	UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(GetTransientPackage());
	Data->VisualMesh = Mesh;
	Data->VisualMeshTargetHeightCm = 1800.0f;
	Data->VisualMeshYawOffsetDeg = -90.0f;
	Unit->InitializeUnit(Data);

	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UPoseableMeshComponent* Poseable = Visual ? Visual->GetPoseableMesh() : nullptr;
	if (!TestNotNull(TEXT("TAU-sw v3 poseable is active"), Poseable))
	{
		return false;
	}

	const FQuat UpperQ(FVector::ForwardVector, FMath::DegreesToRadians(20.0f));
	const FQuat ForeQ(FVector::RightVector, FMath::DegreesToRadians(30.0f));
	const FQuat MountQ(FVector::UpVector, FMath::DegreesToRadians(40.0f));
	Rig->GetJoint(TEXT("UpperArm_L"))->SetRelativeRotation(FQuat::Identity);
	Rig->GetJoint(TEXT("Forearm_L"))->SetRelativeRotation(FQuat::Identity);
	Rig->GetJoint(TEXT("WeaponMount_L"))->SetRelativeRotation(FQuat::Identity);
	Visual->TransferPoseForAutomationTests();
	const FQuat IdentityOutput =
		Poseable->GetBoneRotationByName(TEXT("weapon_mount_l"), EBoneSpaces::ComponentSpace).Quaternion();
	Rig->GetJoint(TEXT("UpperArm_L"))->SetRelativeRotation(UpperQ);
	Rig->GetJoint(TEXT("Forearm_L"))->SetRelativeRotation(ForeQ);
	Rig->GetJoint(TEXT("WeaponMount_L"))->SetRelativeRotation(MountQ);
	Visual->TransferPoseForAutomationTests();

	const FQuat MeshRelative = Poseable->GetRelativeRotation().Quaternion();
	const FQuat Expected = (MeshRelative.Inverse() * (UpperQ * ForeQ * MountQ) * MeshRelative)
		* IdentityOutput;
	const FQuat Actual =
		Poseable->GetBoneRotationByName(TEXT("weapon_mount_l"), EBoneSpaces::ComponentSpace).Quaternion();
	const float DifferenceDegrees = FMath::RadiansToDegrees(Expected.AngularDistance(Actual));
	const float AppliedDeltaDegrees =
		FMath::RadiansToDegrees(IdentityOutput.AngularDistance(Actual));
	AddInfo(FString::Printf(TEXT("weapon_mount_l applied delta %.6f deg; expected delta %.6f deg"),
		AppliedDeltaDegrees, FMath::RadiansToDegrees(IdentityOutput.AngularDistance(Expected))));
	TestTrue(FString::Printf(TEXT("left weapon mount composition matches handoff (%.6f deg)"),
		DifferenceDegrees), DifferenceDegrees <= 0.01f);
	return true;
}

#endif
