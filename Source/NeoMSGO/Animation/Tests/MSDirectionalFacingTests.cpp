// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSProceduralPoseSolver.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDirectionalFacingSolverTest,
	"NeoMSGO.Animation.Facing.StrafeBackpedalAndActionPriority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDirectionalFacingRigTest,
	"NeoMSGO.Animation.Facing.ConnectedRigAndImportedMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSDirectionalFacingSolverTest::RunTest(const FString& Parameters)
{
	FMSProceduralPoseInputs Inputs;
	Inputs.bHasDirectionalFacing = true;
	Inputs.GroundSpeed = 2000.0f;
	Inputs.MovementYaw = 90.0f;
	Inputs.AimYaw = -25.0f;
	FMSProceduralPoseSettings Settings;
	FMSProceduralPoseState Pose;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 1.0f);
	TestTrue(TEXT("right strafe turns legs right without rotating the upper body with them"),
		Pose.LowerBodyYaw > 0.0f && Pose.LowerBodyYaw <= Settings.LegFacingLimit
		&& FMath::IsNearlyEqual(Pose.UpperBodyYaw, -25.0f));
	for (const float Yaw : {89.0f, 91.0f, 88.0f, 92.0f})
	{
		Inputs.MovementYaw = Yaw;
		FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 0.05f);
		TestFalse(TEXT("small heading changes around a strafe cannot flip stride"), Pose.bMovingBackward);
		TestTrue(TEXT("legs stay on the same side in the strafe dead band"), Pose.LowerBodyYaw > 0.0f);
	}
	Inputs.MovementYaw = 180.0f;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 1.0f);
	TestTrue(TEXT("backpedal reverses stride without turning the mech around"),
		Pose.bMovingBackward && Pose.WalkDirection < 0.0f && FMath::IsNearlyZero(Pose.LowerBodyYaw));
	Inputs.MovementYaw = -179.0f;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 0.1f);
	TestTrue(TEXT("the +/-180 boundary keeps legs near the forward axis"), FMath::Abs(Pose.LowerBodyYaw) < 2.0f);
	Inputs.MovementYaw = -90.0f;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 0.1f);
	TestTrue(TEXT("backward stride is also stable inside the dead band"), Pose.bMovingBackward);
	Inputs.MovementYaw = -45.0f;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 1.0f);
	TestTrue(TEXT("forward left diagonal exits backpedal and turns left"),
		!Pose.bMovingBackward && Pose.LowerBodyYaw < 0.0f && Pose.WalkDirection > 0.0f);

	FMSProceduralPoseSolver::StartMeleeMotion(Pose, Settings, EMSMeleeMotionType::Slash, 1000, false);
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 0.1f);
	TestTrue(TEXT("melee blends independent facing back toward its authored full-body pose"),
		FMath::Abs(Pose.LowerBodyYaw) < 45.0f && FMath::Abs(Pose.UpperBodyYaw) < 25.0f);
	Inputs.bIsDead = true;
	FMSProceduralPoseSolver::Update(Inputs, Settings, Pose, 1.0f);
	TestTrue(TEXT("death cancels direction offsets and restores the original kneel"),
		FMath::IsNearlyZero(Pose.LowerBodyYaw) && FMath::IsNearlyZero(Pose.UpperBodyYaw)
		&& !Pose.bMeleeMotionActive && Pose.CrouchPoseAlpha > 0.99f);
	return true;
}

bool FMSDirectionalFacingRigTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSDirectionalFacingTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("production character exists"), Unit)) return false;
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UMSMeshVisualComponent* Visual = Unit->FindComponentByClass<UMSMeshVisualComponent>();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/TAUrus/SK_TAUrus.SK_TAUrus"));
	if (!TestNotNull(TEXT("real imported TAUrus asset is required for transfer verification"), Mesh)
		|| !TestNotNull(TEXT("visual component exists"), Visual)) return false;
	UMSMobileSuitData* Data = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUrus.DA_MS_TAUrus"));
	UMSRangedWeaponData* Blaster = LoadObject<UMSRangedWeaponData>(nullptr, TEXT(
		"/Game/Data/Weapons/DA_Weapon_TAUrus_ChargeBlasterLauncher.DA_Weapon_TAUrus_ChargeBlasterLauncher"));
	if (!TestNotNull(TEXT("production TAUrus data exists"), Data)
		|| !TestNotNull(TEXT("production blaster data exists"), Blaster)) return false;
	Unit->InitializeUnit(Data);
	Unit->GetWeaponComponent()->InitializeSlots({Blaster});
	UMSWeaponVisualComponent* WeaponVisual = Unit->GetWeaponVisualComponent();
	WeaponVisual->RefreshFromWeaponComponent();
	UStaticMeshComponent* HeldWeapon = WeaponVisual->GetImportedHeldMeshComponent();
	if (!TestNotNull(TEXT("actual imported weapon is instantiated"), HeldWeapon)) return false;
	UPoseableMeshComponent* Poseable = Visual->GetPoseableMesh();
	if (!TestNotNull(TEXT("real poseable mesh activated"), Poseable)) return false;
	const FTransform ActorBefore = Unit->GetActorTransform();
	FMSProceduralPoseInputs Inputs;
	Inputs.bHasDirectionalFacing = true;
	Inputs.bHasRangedWeapon = Inputs.bIsAiming = Inputs.bIsInAir = true;
	Inputs.GroundSpeed = 2000.0f;
	auto Settle = [&]()
	{
		for (int32 Frame = 0; Frame < 120; ++Frame) Animator->EvaluatePose(Inputs, 1.0f / 60.0f);
		Visual->TransferPoseForAutomationTests();
	};
	Settle();
	const FQuat ChestBefore = Poseable->GetBoneTransformByName(TEXT("chest"), EBoneSpaces::WorldSpace).GetRotation();
	const FQuat PelvisBefore = Poseable->GetBoneTransformByName(TEXT("pelvis"), EBoneSpaces::WorldSpace).GetRotation();
	const FVector ShoulderBefore = Rig->GetJoint(TEXT("Shoulder_R"))->GetComponentLocation();
	const FVector MuzzleForwardBefore = Rig->GetJoint(TEXT("WeaponMount"))->GetForwardVector();
	const FQuat HeldRotationBefore = HeldWeapon->GetComponentQuat();
	const FVector MuzzleBefore = WeaponVisual->GetMuzzleWorldLocation();
	Inputs.MovementYaw = -60.0f;
	Inputs.AimYaw = 30.0f;
	Settle();
	const FQuat UpperTurn = FRotator(0.0f, 30.0f, 0.0f).Quaternion();
	const FQuat LegTurn = FRotator(0.0f, -55.0f, 0.0f).Quaternion();
	TestTrue(TEXT("visual facing never rotates or moves the actor/collision"), Unit->GetActorTransform().Equals(ActorBefore));
	TestTrue(TEXT("imported chest follows upper-body yaw"), Poseable->GetBoneTransformByName(TEXT("chest"), EBoneSpaces::WorldSpace).GetRotation()
		.Equals(UpperTurn * ChestBefore, 0.001f));
	TestTrue(TEXT("imported pelvis independently follows the legs"), Poseable->GetBoneTransformByName(TEXT("pelvis"), EBoneSpaces::WorldSpace).GetRotation()
		.Equals(LegTurn * PelvisBefore, 0.001f));
	const FVector RootLocation = Rig->GetRigRoot()->GetComponentLocation();
	TestTrue(TEXT("flat shoulder attachment orbits with the torso instead of separating"),
		Rig->GetJoint(TEXT("Shoulder_R"))->GetComponentLocation().Equals(
			RootLocation + UpperTurn.RotateVector(ShoulderBefore - RootLocation), 0.1f));
	TestTrue(TEXT("ghost weapon mount follows upper-body yaw, never lower-body yaw"),
		Rig->GetJoint(TEXT("WeaponMount"))->GetForwardVector().Equals(UpperTurn.RotateVector(MuzzleForwardBefore), 0.001f));
	TestTrue(TEXT("actual imported blaster follows the same upper-body turn"),
		HeldWeapon->GetComponentQuat().Equals(UpperTurn * HeldRotationBefore, 0.001f));
	TestTrue(TEXT("actual weapon grip remains on the imported right-hand socket"),
		HeldWeapon->GetAttachParent() == Poseable && HeldWeapon->GetAttachSocketName() == TEXT("weapon_mount_r")
		&& HeldWeapon->GetComponentLocation().Equals(Poseable->GetSocketLocation(TEXT("weapon_mount_r")), 0.1f));
	TestTrue(TEXT("firing muzzle and displayed barrel keep their direction aligned"),
		WeaponVisual->GetMuzzleWorldDirection(FVector::ForwardVector).Equals(HeldWeapon->GetUpVector(), 0.001f));
	for (int32 Cycle = 0; Cycle < 6; ++Cycle)
	{
		Inputs.MovementYaw = 60.0f; Inputs.AimYaw = -30.0f; Settle();
		Inputs.MovementYaw = 0.0f; Inputs.AimYaw = 0.0f; Settle();
	}
	TestTrue(TEXT("repeated facing cycles do not accumulate attachment offsets"),
		Rig->GetJoint(TEXT("Shoulder_R"))->GetComponentLocation().Equals(ShoulderBefore, 0.1f));
	TestTrue(TEXT("repeated facing cycles keep the muzzle's original direction"),
		Rig->GetJoint(TEXT("WeaponMount"))->GetForwardVector().Equals(MuzzleForwardBefore, 0.001f));
	TestTrue(TEXT("repeated facing cycles restore the actual weapon and firing muzzle"),
		HeldWeapon->GetComponentQuat().Equals(HeldRotationBefore, 0.001f)
		&& WeaponVisual->GetMuzzleWorldLocation().Equals(MuzzleBefore, 0.1f));

	UMSMobileSuitData* SwordSuit = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUsw.DA_MS_TAUsw"));
	UMSWeaponDataBase* DualSword = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_TAUsw_TwinHeatSword.DA_Weapon_TAUsw_TwinHeatSword"));
	if (!TestNotNull(TEXT("real TAU-sw data exists"), SwordSuit)
		|| !TestNotNull(TEXT("real dual sword data exists"), DualSword)) return false;
	Unit->InitializeUnit(SwordSuit);
	Unit->GetWeaponComponent()->InitializeSlots({DualSword});
	WeaponVisual->RefreshFromWeaponComponent();
	Poseable = Visual->GetPoseableMesh();
	UStaticMeshComponent* RightSword = WeaponVisual->GetRightHeatSwordComponent();
	UStaticMeshComponent* LeftSword = WeaponVisual->GetLeftHeatSwordComponent();
	if (!TestNotNull(TEXT("actual right sword exists"), RightSword)
		|| !TestNotNull(TEXT("actual left sword exists"), LeftSword)) return false;
	Inputs.bHasRangedWeapon = Inputs.bIsAiming = false;
	Settle();
	const FQuat RightSwordRest = RightSword->GetComponentQuat();
	const FQuat LeftSwordRest = LeftSword->GetComponentQuat();
	for (const float Yaw : {30.0f, -30.0f, 0.0f})
	{
		Inputs.AimYaw = Yaw; Inputs.MovementYaw = -Yaw; Settle();
		const FQuat Turn = FRotator(0.0f, Yaw, 0.0f).Quaternion();
		TestTrue(TEXT("both actual sword meshes follow the upper-body turn and return without drift"),
			RightSword->GetComponentQuat().Equals(Turn * RightSwordRest, 0.001f)
			&& LeftSword->GetComponentQuat().Equals(Turn * LeftSwordRest, 0.001f));
		TestTrue(TEXT("both grips remain connected to their own imported hand socket"),
			RightSword->GetAttachParent() == Poseable && RightSword->GetAttachSocketName() == TEXT("weapon_mount_r")
			&& LeftSword->GetAttachParent() == Poseable && LeftSword->GetAttachSocketName() == TEXT("weapon_mount_l")
			&& RightSword->GetComponentLocation().Equals(Poseable->GetSocketTransform(TEXT("weapon_mount_r"))
				.TransformPosition(RightSword->GetRelativeLocation()), 0.1f)
			&& LeftSword->GetComponentLocation().Equals(Poseable->GetSocketTransform(TEXT("weapon_mount_l"))
				.TransformPosition(LeftSword->GetRelativeLocation()), 0.1f));
	}

	// Use the authored timeline, whose Slash lasts well beyond the short procedural swing.
	TestTrue(TEXT("real TAU-sw authored motion data loads"), Rig->LoadDefaultTAUswMotionSet());
	Inputs.MovementYaw = 60.0f; Inputs.AimYaw = 30.0f; Settle();
	TestTrue(TEXT("authored slash starts from a turned posture"), Rig->PlayMotion(TEXT("Slash_Dual")));
	TestTrue(TEXT("authored start immediately restores flat attachment positions"),
		Rig->GetJoint(TEXT("Shoulder_R"))->GetRelativeLocation().Equals(Rig->GetJointRestRelativeLocation(TEXT("Shoulder_R")), 0.001f));
	float LastActiveTime = 0.0f;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		const bool bWasPlaying = Rig->IsMotionPlaying();
		Animator->EvaluatePose(Inputs, 1.0f / 60.0f);
		Visual->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		if (bWasPlaying)
		{
			LastActiveTime = Frame / 60.0f;
			TestTrue(TEXT("the entire authored slash keeps rotation and attachment positions in one frame"),
				FMath::IsNearlyZero(Animator->GetPoseState().UpperBodyYaw)
				&& FMath::IsNearlyZero(Animator->GetPoseState().LowerBodyYaw)
				&& Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeLocation().Equals(
					Rig->GetJointRestRelativeLocation(TEXT("UpperArm_R")), 0.001f));
		}
	}
	TestTrue(TEXT("the test covers the real slash after the procedural motion would end"), LastActiveTime > 1.1f);
	Rig->PlayMotion(TEXT("Slash_Dual"));
	Inputs.bIsDead = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	Visual->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("death cancels the authored slash before it can overwrite the kneel"), Rig->IsMotionPlaying());
	TestTrue(TEXT("death kneel survives the normal mesh update"), Animator->GetPoseState().CrouchPoseAlpha > 0.99f
		&& FMath::IsNearlyEqual(Rig->GetJoint(TEXT("Torso"))->GetRelativeRotation().Pitch,
			-Animator->GetPoseState().TorsoLeanPitch, 0.01f));
	Inputs.bIsDead = false;
	Rig->PlayMotion(TEXT("Slash_Single"));
	Inputs.bIsKnockedDown = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	Visual->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("knockdown also cancels the authored swing"), Rig->IsMotionPlaying());

	// At the same forward clock phase, reverse stepping negates hip swing and keeps
	// knee flexion. Negating both is just swapping left/right legs of a forward gait.
	AMSPlayerCharacter* BackUnit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(10000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("backward comparison unit exists"), BackUnit)) return false;
	UMSMechAnimatorComponent* BackAnimator = BackUnit->GetMechAnimatorComponent();
	UMSMechRigComponent* BackRig = BackUnit->GetMechRigComponent();
	// New actors keep independent phase/state, so compare one full cycle with identical dt.
	AMSPlayerCharacter* ForwardUnit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(20000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("forward comparison unit exists"), ForwardUnit)) return false;
	FMSProceduralPoseInputs ForwardInput;
	ForwardInput.bHasDirectionalFacing = true; ForwardInput.GroundSpeed = 1800.0f;
	FMSProceduralPoseInputs BackInput = ForwardInput;
	BackInput.MovementYaw = 180.0f;
	ForwardInput.bIsInAir = BackInput.bIsInAir = true;
	ForwardUnit->GetMechAnimatorComponent()->EvaluatePose(ForwardInput, 1.0f);
	BackAnimator->EvaluatePose(BackInput, 1.0f);
	ForwardInput.bIsInAir = BackInput.bIsInAir = false;
	bool bSawHipSwing = false;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		ForwardUnit->GetMechAnimatorComponent()->EvaluatePose(ForwardInput, 1.0f / 60.0f);
		BackAnimator->EvaluatePose(BackInput, 1.0f / 60.0f);
		const UMSMechRigComponent* ForwardRig = ForwardUnit->GetMechRigComponent();
		const float ForwardThigh = ForwardRig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch;
		bSawHipSwing |= FMath::Abs(ForwardThigh) > 30.0f;
		TestTrue(TEXT("backward hip swing reverses the same leg's trajectory"),
			FMath::IsNearlyEqual(BackRig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch, -ForwardThigh, 0.02f));
		TestTrue(TEXT("backward knee does not swap support and swing legs"),
			FMath::IsNearlyEqual(BackRig->GetJoint(TEXT("Shin_L"))->GetRelativeRotation().Pitch,
				ForwardRig->GetJoint(TEXT("Shin_L"))->GetRelativeRotation().Pitch, 0.02f));
	}
	TestTrue(TEXT("trajectory verification includes a visible complete stride"), bSawHipSwing);
	return true;
}

#endif
