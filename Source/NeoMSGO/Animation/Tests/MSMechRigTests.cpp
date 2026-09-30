// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotCharacter.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSUnitBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechRigHierarchyTest,
	"NeoMSGO.Animation.MechRig.HierarchyAndRuntimeMaterials",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechAnimatorPoseTest,
	"NeoMSGO.Animation.MechRig.ProceduralJointPoses",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechCrouchAbsolutePoseTest,
	"NeoMSGO.Animation.MechRig.CrouchUsesAbsolutePose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechAimForwardTest,
	"NeoMSGO.Animation.MechRig.RangedAimPointsForward",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechAimRestTransitionTest,
	"NeoMSGO.Animation.MechRig.RangedRestDiffersFromAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechAppearanceTest,
	"NeoMSGO.Animation.MechRig.TeamAndTierAppearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechAimStimulusTest,
	"NeoMSGO.Animation.MechRig.AimOnlyWhileShooting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMechThrusterVisualTest,
	"NeoMSGO.Animation.MechRig.Review72.TexturedThrusterSmokeAndModuleOnlyAfterimage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleAfterimageOverrideTest,
	"NeoMSGO.Animation.MechRig.ModuleAfterimageOverride",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualModuleAfterimageTest,
	"NeoMSGO.Animation.MechRig.MeshVisualModuleAfterimage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMSMechTestWorld
	{
		UWorld* World = nullptr;
		FMSMechTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMechRigTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FMSMechTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	template<typename T>
	T* Spawn(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

bool FMSMechCrouchAbsolutePoseTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	if (!TestNotNull(TEXT("#183 rig exists"), Rig) || !TestNotNull(TEXT("#183 animator exists"), Animator))
	{
		return false;
	}

	FMSProceduralPoseInputs Standing;
	Animator->EvaluatePose(Standing, 1.0f);
	const float BaselineThigh = Rig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch;
	const float BaselineShin = Rig->GetJoint(TEXT("Shin_L"))->GetRelativeRotation().Pitch;
	const float BaselineRootZ = Rig->GetRigRoot()->GetRelativeLocation().Z;

	FMSProceduralPoseInputs Crouching;
	Crouching.bIsCrouching = true;
	for (int32 Cycle = 0; Cycle < 8; ++Cycle)
	{
		// One second exceeds both pose blending and the maximum 150 degree limb transition.
		Animator->EvaluatePose(Crouching, 1.0f);
		Animator->EvaluatePose(Standing, 1.0f);
	}

	TestTrue(TEXT("#183 repeated crouch cycles return thigh to the same absolute standing angle"),
		FMath::IsNearlyEqual(Rig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch, BaselineThigh, 0.01f));
	TestTrue(TEXT("#183 repeated crouch cycles return shin to the same absolute standing angle"),
		FMath::IsNearlyEqual(Rig->GetJoint(TEXT("Shin_L"))->GetRelativeRotation().Pitch, BaselineShin, 0.01f));
	TestTrue(TEXT("#183 repeated crouch cycles return root to the authored rest height"),
		FMath::IsNearlyEqual(Rig->GetRigRoot()->GetRelativeLocation().Z, BaselineRootZ, 0.01f));
	return true;
}

bool FMSMechRigHierarchyTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	TestNotNull(TEXT("Player owns runtime mech rig"), Rig);
	if (!Rig) return false;

	const TArray<FName> VisibleParts = {TEXT("Head"),TEXT("Torso"),TEXT("Hip"),TEXT("Backpack"),
		TEXT("Shoulder_L"),TEXT("Shoulder_R"),TEXT("UpperArm_L"),TEXT("UpperArm_R"),
		TEXT("Forearm_L"),TEXT("Forearm_R"),TEXT("Thigh_L"),TEXT("Thigh_R"),
		TEXT("Shin_L"),TEXT("Shin_R"),TEXT("Foot_L"),TEXT("Foot_R")};
	TestEqual(TEXT("Exactly sixteen prefab display parts are built"), Rig->GetPartNames().Num(), 16);
	for (const FName Name : VisibleParts)
	{
		UStaticMeshComponent* Part = Rig->GetPart(Name);
		TestNotNull(*FString::Printf(TEXT("Part %s exists"), *Name.ToString()), Part);
		TestNotNull(*FString::Printf(TEXT("Joint %s exists"), *Name.ToString()), Rig->GetJoint(Name));
		if (!Part) continue;
		TestTrue(TEXT("Uses Engine BasicShapes Cube"), Part->GetStaticMesh()
			&& Part->GetStaticMesh()->GetPathName() == TEXT("/Engine/BasicShapes/Cube.Cube"));
		TestEqual(TEXT("Visual collision is disabled"), Part->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Runtime material is a MID"), Part->GetMaterial(0)->IsA<UMaterialInstanceDynamic>());
	}
	TestNotNull(TEXT("WeaponMount attachment joint exists"), Rig->GetJoint(TEXT("WeaponMount")));
	TestNotNull(TEXT("WeaponMount_L attachment joint exists"), Rig->GetJoint(TEXT("WeaponMount_L")));
	TestTrue(TEXT("WeaponMount_L is parented to Forearm_L"),
		Rig->GetJoint(TEXT("WeaponMount_L"))->GetAttachParent() == Rig->GetJoint(TEXT("Forearm_L")));
	TestTrue(TEXT("Forearm L pivot is parented to UpperArm L pivot"),
		Rig->GetJoint(TEXT("Forearm_L"))->GetAttachParent() == Rig->GetJoint(TEXT("UpperArm_L")));
	TestTrue(TEXT("Shin R pivot is parented to Thigh R pivot"),
		Rig->GetJoint(TEXT("Shin_R"))->GetAttachParent() == Rig->GetJoint(TEXT("Thigh_R")));
	TestTrue(TEXT("Foot points toward Actor X+"), Rig->GetPart(TEXT("Foot_R"))->GetRelativeLocation().X > 0.0f);
	const FLinearColor TestMagenta(1.0f, 0.0f, 1.0f);
	TestTrue(TEXT("SetPartColor succeeds"), Rig->SetPartColor(TEXT("Torso"), TestMagenta));
	TestTrue(TEXT("SetPartColor can be queried"), Rig->GetPartColor(TEXT("Torso")).Equals(TestMagenta));

	const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();
	TestTrue(TEXT("capsule radius uses the common MS scale"),
		FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), 34.0f * MSGO::MSScale));
	TestTrue(TEXT("capsule half-height uses the common MS scale"),
		FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), 88.0f * MSGO::MSScale));
	TestTrue(TEXT("RigRoot uses the common MS scale"),
		Rig->GetRigRoot()->GetRelativeScale3D().Equals(FVector(MSGO::MSScale)));

	FBox CubeBounds(ForceInit);
	for (const FName Name : VisibleParts)
	{
		if (const UStaticMeshComponent* Part = Rig->GetPart(Name))
		{
			CubeBounds += Part->Bounds.GetBox();
		}
	}
	const float CubeHeight = CubeBounds.GetSize().Z;
	TestTrue(FString::Printf(TEXT("Cube rig height %.1fUU is within 1800UU +/-5%%"), CubeHeight),
		FMath::Abs(CubeHeight - 1800.0f) <= 1800.0f * 0.05f);
	TestTrue(TEXT("Cube rig soles align to the capsule bottom within 1% of unit height"),
		FMath::Abs((CubeBounds.Min.Z - Unit->GetActorLocation().Z)
			+ Capsule->GetScaledCapsuleHalfHeight()) <= 18.0f);

	USphereComponent* Head = nullptr;
	TInlineComponentArray<USphereComponent*> Spheres(Unit);
	for (USphereComponent* Sphere : Spheres)
	{
		if (Sphere && Sphere->GetFName() == AMSUnitBase::HeadHitboxComponentName)
		{
			Head = Sphere;
			break;
		}
	}
	if (TestNotNull(TEXT("scaled HeadHitbox exists"), Head))
	{
		TestTrue(TEXT("HeadHitbox radius uses the common MS scale"),
			FMath::IsNearlyEqual(Head->GetUnscaledSphereRadius(), 20.0f * MSGO::MSScale));
		TestTrue(TEXT("HeadHitbox remains centered at capsule top"),
			FMath::IsNearlyEqual(Head->GetRelativeLocation().Z, Capsule->GetUnscaledCapsuleHalfHeight()));
	}
	const float AimHeightFromBase = Unit->GetLockOnAimPoint().Z
		- (Unit->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight());
	TestTrue(TEXT("lock-on aim point uses the common MS scale"),
		FMath::IsNearlyEqual(AimHeightFromBase, 140.0f * MSGO::MSScale));
	return true;
}

bool FMSMechAnimatorPoseTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	TestNotNull(TEXT("Animator exists"), Animator);
	if (!Rig || !Animator) return false;

	FMSProceduralPoseInputs Inputs;
	Inputs.GroundSpeed = 180.0f * MSGO::MSScale;
	Animator->EvaluatePose(Inputs, 0.5f);
	const float LeftWalkPitch = Rig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch;
	const float RightWalkPitch = Rig->GetJoint(TEXT("Thigh_R"))->GetRelativeRotation().Pitch;
	TestTrue(TEXT("Walking thighs move in opposite phase"), LeftWalkPitch * RightWalkPitch < 0.0f);
	TestTrue(TEXT("Walking bends at least one knee"),
		!FMath::IsNearlyZero(Rig->GetJoint(TEXT("Shin_L"))->GetRelativeRotation().Pitch)
		|| !FMath::IsNearlyZero(Rig->GetJoint(TEXT("Shin_R"))->GetRelativeRotation().Pitch));

	Inputs.GroundSpeed = 0.0f;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Stopped thighs return to reference pose"),
		FMath::IsNearlyZero(Rig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch, 0.01f)
		&& FMath::IsNearlyZero(Rig->GetJoint(TEXT("Thigh_R"))->GetRelativeRotation().Pitch, 0.01f));

	Inputs.GroundSpeed = 300.0f * MSGO::MSScale; Inputs.bIsBoosting = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Boost leans torso forward"), Rig->GetJoint(TEXT("Torso"))->GetRelativeRotation().Pitch < 0.0f);
	TestTrue(TEXT("Boost shows backpack thrusters"), Animator->AreThrustersVisible());
	Inputs.bIsBoosting = false; Inputs.GroundSpeed = 0.0f;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestFalse(TEXT("Thrusters hide after boost"), Animator->AreThrustersVisible());

	Inputs.bIsCrouching = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Crouch lowers the rig root by a scaled body-relative distance"),
		Rig->GetRigRoot()->GetRelativeLocation().Z < Rig->GetRigRootRestRelativeZ() - 30.0f * MSGO::MSScale);
	TestFalse(TEXT("Crouch legs are asymmetric"), FMath::IsNearlyEqual(
		Rig->GetJoint(TEXT("Thigh_L"))->GetRelativeRotation().Pitch,
		Rig->GetJoint(TEXT("Thigh_R"))->GetRelativeRotation().Pitch));

	Inputs = FMSProceduralPoseInputs(); Inputs.bIsSwitchingWeapon = true; Inputs.WeaponSwitchProgress = 0.5f;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Weapon switch pulls the right arm back/up"),
		FMath::Abs(Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch) > 45.0f);
	Inputs = FMSProceduralPoseInputs(); Inputs.bIsAiming = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Ranged aim reaches full blend"), Animator->GetPoseState().AimPoseAlpha > 0.99f);

	for (const EMSMeleeMotionType Type : {EMSMeleeMotionType::Slash, EMSMeleeMotionType::Overhead,
		EMSMeleeMotionType::Thrust, EMSMeleeMotionType::Punch})
	{
		Animator->StartMeleeMotion(Type, 1000, false);
		Animator->EvaluatePose(FMSProceduralPoseInputs(), 0.08f);
		TestEqual(TEXT("Requested melee motion type is retained"), Animator->GetPoseState().ActiveMeleeMotionType, Type);
		TestTrue(TEXT("Melee produces a right-arm output"), Animator->GetPoseState().MeleeMotionAlpha > 0.0f);
	}

	Inputs = FMSProceduralPoseInputs(); Inputs.bIsStaggered = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Stagger applies forward torso pose"), Animator->GetPoseState().TorsoLeanPitch > 0.0f);
	Inputs = FMSProceduralPoseInputs(); Inputs.bIsKnockedDown = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Knockdown applies backward torso pose"), Animator->GetPoseState().TorsoLeanPitch < 0.0f);
	Inputs = FMSProceduralPoseInputs(); Inputs.bIsDead = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("Death applies kneel and root lowering"), Animator->GetPoseState().CrouchPoseAlpha > 0.99f
		&& Animator->GetPoseState().CrouchGroundOffset > 35.0f);
	return true;
}

bool FMSMechAimForwardTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	TestNotNull(TEXT("Aim test rig exists"), Rig);
	TestNotNull(TEXT("Aim test animator exists"), Animator);
	if (!Rig || !Animator) return false;

	FMSProceduralPoseInputs Inputs;
	Inputs.bHasRangedWeapon = true;
	Inputs.bIsAiming = true;
	Animator->EvaluatePose(Inputs, 1.0f);

	const USceneComponent* UpperArm = Rig->GetJoint(TEXT("UpperArm_R"));
	const USceneComponent* Forearm = Rig->GetJoint(TEXT("Forearm_R"));
	const USceneComponent* WeaponMount = Rig->GetJoint(TEXT("WeaponMount"));
	TestNotNull(TEXT("Right upper-arm joint exists"), UpperArm);
	TestNotNull(TEXT("Right forearm joint exists"), Forearm);
	TestNotNull(TEXT("Weapon mount exists"), WeaponMount);
	if (!UpperArm || !Forearm || !WeaponMount) return false;

	// 腕キューブはローカル-Z方向へ伸びる。UEピッチ+θで-Z軸は(sinθ,0,-cosθ)=前方へ回るため、
	// 「前方構え」= 正の相対ピッチ(旧テストは-65以下=後方へ伸びた腕を検証していた回帰の再発防止)。
	TestTrue(FString::Printf(TEXT("Aim raises the arm forward to near-horizontal (pitch=%.1f)"),
			UpperArm->GetRelativeRotation().Pitch),
		UpperArm->GetRelativeRotation().Pitch >= 65.0f);
	// ワールド空間の実方向でも「腕が機体前方へ出ている」ことを直接検証する
	const FVector ArmWorldDirection = -UpperArm->GetUpVector(); // 腕ジオメトリの伸長方向(-Z)
	TestTrue(FString::Printf(TEXT("Aimed arm geometry points along actor forward (dot=%.2f)"),
			FVector::DotProduct(ArmWorldDirection, Unit->GetActorForwardVector())),
		FVector::DotProduct(ArmWorldDirection, Unit->GetActorForwardVector()) >= 0.9f);
	// ユーザー仕様: 前方へ上げるのは武器側(右)の手のみ。左手は下垂のまま
	TestTrue(FString::Printf(TEXT("support (left) arm keeps hanging during aim (pitch=%.1f)"),
			Rig->GetJoint(TEXT("UpperArm_L"))->GetRelativeRotation().Pitch),
		FMath::Abs(Rig->GetJoint(TEXT("UpperArm_L"))->GetRelativeRotation().Pitch) <= 5.0f);
	TestTrue(TEXT("Aim leaves forearm rig pitch near horizontal"),
		FMath::Abs(Forearm->GetRelativeRotation().Pitch) <= 0.5f);
	TestTrue(TEXT("Aimed weapon mount world-forward direction is horizontal"),
		FMath::Abs(WeaponMount->GetForwardVector().Z) <= 0.05f);
	TestTrue(TEXT("Aimed weapon mount points along actor forward"),
		FVector::DotProduct(WeaponMount->GetForwardVector(), Unit->GetActorForwardVector()) >= 0.95f);
	return true;
}

bool FMSMechAimRestTransitionTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechRigComponent* Rig = Unit ? Unit->GetMechRigComponent() : nullptr;
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	if (!TestNotNull(TEXT("Rest/aim test rig exists"), Rig)
		|| !TestNotNull(TEXT("Rest/aim test animator exists"), Animator)) return false;

	// 素手/格闘相当(射撃武器なし)の静止時は腕が真下に垂れる(実プレイ仕様: 通常時は腕を下げる)
	FMSProceduralPoseInputs Inputs;
	Animator->EvaluatePose(Inputs, 1.0f);
	TestTrue(TEXT("without a ranged weapon the idle upper arm hangs straight down"),
		FMath::Abs(Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch) <= 1.0f);

	// 射撃武器の携行(非構え): わずかな前方オフセットのみ(±15°以上)で、構えとは明確に異なる
	Inputs.bHasRangedWeapon = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	const float RestUpperPitch = Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch;
	const float RestForePitch = Rig->GetJoint(TEXT("Forearm_R"))->GetRelativeRotation().Pitch;
	TestTrue(TEXT("unaimed carry pose is visibly offset"), FMath::Abs(RestUpperPitch) >= 15.0f);
	TestTrue(TEXT("unaimed forearm is visibly distinct"), FMath::Abs(RestForePitch) >= 10.0f);
	TestTrue(TEXT("solver reports the rest pose at zero aim"), Animator->GetPoseState().RangedRestPoseAlpha > 0.99f);

	// 歩行中は携行姿勢のまま腕が前後にスイングし続ける(旧実装はスイングが完全に消えていた)
	Inputs.GroundSpeed = 180.0f * MSGO::MSScale;
	Animator->EvaluatePose(Inputs, 0.35f);
	const float WalkPitchA = Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch;
	Animator->EvaluatePose(Inputs, 0.35f);
	const float WalkPitchB = Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch;
	TestTrue(FString::Printf(TEXT("carrying arms keep swinging while walking (%.1f -> %.1f)"), WalkPitchA, WalkPitchB),
		FMath::Abs(WalkPitchA - WalkPitchB) > 0.5f);
	Inputs.GroundSpeed = 0.0f;

	Inputs.bIsAiming = true;
	Animator->EvaluatePose(Inputs, 1.0f);
	const float AimUpperPitch = Rig->GetJoint(TEXT("UpperArm_R"))->GetRelativeRotation().Pitch;
	const float AimForePitch = Rig->GetJoint(TEXT("Forearm_R"))->GetRelativeRotation().Pitch;
	const float AimLeftUpperPitch = Rig->GetJoint(TEXT("UpperArm_L"))->GetRelativeRotation().Pitch;
	TestTrue(TEXT("aim upper-arm pose differs from rest"), FMath::Abs(AimUpperPitch - RestUpperPitch) >= 15.0f);
	TestTrue(TEXT("aim forearm pose differs from rest"), FMath::Abs(AimForePitch - RestForePitch) >= 10.0f);
	// 前方構え=正の相対ピッチ(-65以下は「後ろへ伸びた腕」なので不合格)。
	// ユーザー仕様: 上げるのは武器側(右)のみで、左手は下垂+歩行スイングを維持する。
	TestTrue(FString::Printf(TEXT("weapon-side (right) arm rises clearly forward (R=%.1f)"), AimUpperPitch),
		AimUpperPitch >= 65.0f);
	TestTrue(FString::Printf(TEXT("support (left) arm stays lowered during aim (L=%.1f)"), AimLeftUpperPitch),
		FMath::Abs(AimLeftUpperPitch) <= 5.0f);
	TestTrue(TEXT("rest contribution is removed at full aim"), Animator->GetPoseState().RangedRestPoseAlpha < 0.01f);
	return true;
}

bool FMSMechAppearanceTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Player = Spawn<AMSPlayerCharacter>(TestWorld.World);
	AMSBotCharacter* Bot = Spawn<AMSBotCharacter>(TestWorld.World);
	UMSMechRigComponent* PlayerRig = Player ? Player->GetMechRigComponent() : nullptr;
	UMSMechRigComponent* BotRig = Bot ? Bot->GetMechRigComponent() : nullptr;
	if (!PlayerRig || !BotRig) return false;
	TestFalse(TEXT("Player and enemy torso colors differ"),
		PlayerRig->GetPartColor(TEXT("Torso")).Equals(BotRig->GetPartColor(TEXT("Torso")), 0.001f));
	TestTrue(TEXT("Weak enemy receives sensor spike"), BotRig->GetHeadDecorationName() == TEXT("SensorSpike"));
	Bot->SetEnemyTier(EMSEnemyTier::Medium);
	TestTrue(TEXT("Medium enemy receives command horn"), BotRig->GetHeadDecorationName() == TEXT("CommandHorn"));
	const FLinearColor MediumColor = BotRig->GetPartColor(TEXT("Torso"));
	Bot->SetEnemyTier(EMSEnemyTier::Strong);
	TestTrue(TEXT("Strong tier changes enemy color"), !MediumColor.Equals(BotRig->GetPartColor(TEXT("Torso")), 0.001f));
	Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	TestTrue(TEXT("Ally BOT has no enemy head decoration"), BotRig->GetHeadDecorationName().IsNone());
	TestTrue(TEXT("Team API yields a third ally-BOT color"),
		!BotRig->GetPartColor(TEXT("Torso")).Equals(PlayerRig->GetPartColor(TEXT("Torso")), 0.001f));
	return true;
}

// 実プレイ報告バグ「射撃武器装備中は常時構え(しかも腕が後方)」の回帰テスト:
// 装備しただけでは構えず、発射(クールダウン発生)を照準刺激として構えに入ることを検証する。
bool FMSMechAimStimulusTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	UMSWeaponComponent* Weapons = Unit ? Unit->GetWeaponComponent() : nullptr;
	if (!TestNotNull(TEXT("animator exists"), Animator) || !TestNotNull(TEXT("weapons exist"), Weapons))
	{
		return false;
	}

	UMSRangedWeaponData* Rifle = NewObject<UMSRangedWeaponData>(Unit);
	Rifle->MagazineSize = 5;
	Rifle->FireInterval = 0.5f;
	Weapons->InitializeSlots({Rifle});

	Animator->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("ranged weapon equip is reported"), Animator->GetPoseInputs().bHasRangedWeapon);
	TestFalse(TEXT("equipping a ranged weapon alone does not enter the aim pose"),
		Animator->GetPoseInputs().bIsAiming);

	// 発射(クールダウン開始)= 照準刺激 → 構えに入る
	TestTrue(TEXT("fire consumes ammo and starts cooldown"), Weapons->ConsumeAmmoAndStartCooldown(0));
	Animator->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("firing raises the aim pose"), Animator->GetPoseInputs().bIsAiming);

	// 格闘装備では構えも携行もしない
	UMSMeleeWeaponData* Sword = NewObject<UMSMeleeWeaponData>(Unit);
	Weapons->InitializeSlots({Sword});
	Animator->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("melee weapon reports no ranged carry"), Animator->GetPoseInputs().bHasRangedWeapon);
	return true;
}

// 実プレイレビュー: 単色Sphere煙をCC0テクスチャBillboardへ変更し、通常ブースト残像を廃止する。
bool FMSMechThrusterVisualTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	if (!TestNotNull(TEXT("animator exists"), Animator))
	{
		return false;
	}

	// ブースト開始(EvaluatePose内でスラスター可視化+煙スポーンが走る)
	FMSProceduralPoseInputs Inputs;
	Inputs.bIsBoosting = true;
	Inputs.GroundSpeed = 600.0f;
	Animator->EvaluatePose(Inputs, 0.1f);
	TestTrue(TEXT("Boost shows thrusters"), Animator->AreThrustersVisible());

	// --- 常設基部: 単色プリミティブを使わず、CC0煙テクスチャBillboardを左右3枚ずつ ---
	int32 SmokeBaseCount = 0;
	TInlineComponentArray<UMaterialBillboardComponent*> SmokeBases(Unit);
	for (const UMaterialBillboardComponent* SmokeBase : SmokeBases)
	{
		if (!SmokeBase || !SmokeBase->GetName().StartsWith(TEXT("ThrusterSmoke_")))
		{
			continue;
		}
		++SmokeBaseCount;
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(SmokeBase->GetMaterial(0));
		if (TestNotNull(TEXT("Smoke base uses a dynamic translucent material"), Material))
		{
			const UTexture* SmokeTexture = Material->K2_GetTextureParameterValue(TEXT("SmokeTexture"));
			TestTrue(TEXT("Smoke base samples an imported thruster-smoke texture"),
				SmokeTexture && SmokeTexture->GetPathName().StartsWith(TEXT("/Game/Textures/VFX/T_ThrusterSmoke_")));
		}
	}
	TestEqual(TEXT("Two thrusters build three textured smoke bases each"), SmokeBaseCount, 6);

	auto CountTaggedVfx = [&TestWorld](const FName Tag)
	{
		int32 Count = 0;
		for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
		{
			if (It->ActorHasTag(Tag)) ++Count;
		}
		return Count;
	};
	TestEqual(TEXT("One low-frequency boost update spawns one smoke pair"),
		CountTaggedVfx(TEXT("MSVfx.ThrusterSmoke")), 2);
	AMSTransientVfxActor* SampleSmoke = nullptr;
	for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("MSVfx.ThrusterSmoke")))
		{
			SampleSmoke = *It;
			break;
		}
	}
	if (TestNotNull(TEXT("A spawned smoke puff is inspectable"), SampleSmoke))
	{
		TInlineComponentArray<UMaterialBillboardComponent*> PuffBillboards(SampleSmoke);
		TestEqual(TEXT("Each puff is one lightweight textured billboard"), PuffBillboards.Num(), 1);
		if (!PuffBillboards.IsEmpty())
		{
			UMaterialInstanceDynamic* Material =
				Cast<UMaterialInstanceDynamic>(PuffBillboards[0]->GetMaterial(0));
			if (TestNotNull(TEXT("Moving puff uses the smoke material"), Material))
			{
				const UTexture* SmokeTexture = Material->K2_GetTextureParameterValue(TEXT("SmokeTexture"));
				TestTrue(TEXT("Moving puff uses a CC0 smoke texture variant"),
					SmokeTexture && SmokeTexture->GetPathName().StartsWith(TEXT("/Game/Textures/VFX/T_ThrusterSmoke_")));
			}
			const FVector StartLocation = PuffBillboards[0]->GetRelativeLocation();
			const FVector StartScale = PuffBillboards[0]->GetRelativeScale3D();
			SampleSmoke->Tick(0.2f);
			TestTrue(TEXT("Smoke rises while trailing backward"),
				PuffBillboards[0]->GetRelativeLocation().Z > StartLocation.Z
				&& PuffBillboards[0]->GetRelativeLocation().X > StartLocation.X);
			TestTrue(TEXT("Smoke diffuses outward during its fade"),
				PuffBillboards[0]->GetRelativeScale3D().GetMax() > StartScale.GetMax());
		}
	}

	// --- 残像: モジュール未発動の通常ブーストでは、速度・経過時間に関係なく一切出ないことを保証する。 ---
	const int32 BeforeMoreBoost = CountTaggedVfx(TEXT("MSVfx.BoostAfterimage"));
	TestEqual(TEXT("Normal boost movement does not spawn an afterimage"), BeforeMoreBoost, 0);
	Animator->EvaluatePose(Inputs, 0.1f);
	Animator->EvaluatePose(Inputs, 0.5f);
	Animator->EvaluatePose(Inputs, 1.0f);
	TestEqual(TEXT("Normal boost remains afterimage-free over multiple spawn intervals"),
		CountTaggedVfx(TEXT("MSVfx.BoostAfterimage")), 0);

	// ブースト停止でスラスター非表示(従来仕様の回帰確認)
	Inputs.bIsBoosting = false;
	Animator->EvaluatePose(Inputs, 0.1f);
	TestFalse(TEXT("Thrusters hide after boost"), Animator->AreThrustersVisible());
	return true;
}

bool FMSModuleAfterimageOverrideTest::RunTest(const FString& Parameters)
{
	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	if (!TestNotNull(TEXT("animator exists"), Animator))
	{
		return false;
	}

	auto FindAfterimage = [&TestWorld]() -> AMSTransientVfxActor*
	{
		for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("MSVfx.BoostAfterimage")))
			{
				return *It;
			}
		}
		return nullptr;
	};
	auto DestroyAfterimages = [&TestWorld]()
	{
		for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("MSVfx.BoostAfterimage")))
			{
				It->Destroy();
			}
		}
	};
	auto ReadGhostColor = [this](AMSTransientVfxActor* Afterimage, FLinearColor& OutColor)
	{
		if (!Afterimage)
		{
			return false;
		}
		TInlineComponentArray<UStaticMeshComponent*> GhostMeshes(Afterimage);
		if (!TestTrue(TEXT("afterimage contains copied ghost meshes"), !GhostMeshes.IsEmpty()))
		{
			return false;
		}
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(GhostMeshes[0]->GetMaterial(0));
		if (!TestNotNull(TEXT("ghost uses a dynamic material"), Material))
		{
			return false;
		}
		OutColor = Material->K2_GetVectorParameterValue(TEXT("Color"));
		return true;
	};

	const FLinearColor ModuleColor(1.0f, 0.08f, 0.24f, 0.18f);
	FMSProceduralPoseInputs Inputs;
	Inputs.bIsBoosting = false;
	Inputs.GroundSpeed = 600.0f;
	Animator->SetModuleAfterimageOverride(true, ModuleColor);
	Animator->EvaluatePose(Inputs, 0.3f);
	AMSTransientVfxActor* ModuleGhost = FindAfterimage();
	TestNotNull(TEXT("module effect spawns an afterimage during non-boost movement above the threshold"), ModuleGhost);
	FLinearColor SpawnedModuleColor = FLinearColor::Transparent;
	if (ReadGhostColor(ModuleGhost, SpawnedModuleColor))
	{
		TestTrue(TEXT("module afterimage uses the configured red-pink color"),
			SpawnedModuleColor.Equals(ModuleColor, 0.001f));
	}

	DestroyAfterimages();
	Animator->SetModuleAfterimageOverride(false, FLinearColor::Transparent);
	Inputs.bIsBoosting = true;
	Animator->EvaluatePose(Inputs, 0.3f);
	// モジュール終了後は、ブースト中でも青残像へフォールバックしないことを保証する。
	TestNull(TEXT("normal boost stays afterimage-free after the module effect ends"), FindAfterimage());

	DestroyAfterimages();
	Animator->SetModuleAfterimageOverride(true, ModuleColor);
	Inputs.bIsBoosting = false;
	Inputs.GroundSpeed = 0.0f;
	Animator->EvaluatePose(Inputs, 0.3f);
	TestNull(TEXT("module override does not spawn an afterimage while stationary"), FindAfterimage());
	Inputs.GroundSpeed = 600.0f;
	Inputs.bIsDead = true;
	Animator->EvaluatePose(Inputs, 0.3f);
	TestNull(TEXT("module override does not spawn an afterimage while dead"), FindAfterimage());
	return true;
}

bool FMSMeshVisualModuleAfterimageTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/TAUrus/SK_TAUrus.SK_TAUrus"));
	if (!Mesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping mesh afterimage test"));
		return true;
	}

	FMSMechTestWorld TestWorld;
	AMSPlayerCharacter* Unit = Spawn<AMSPlayerCharacter>(TestWorld.World);
	if (!TestNotNull(TEXT("visual-mesh unit spawns"), Unit))
	{
		return false;
	}

	UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>();
	Data->VisualMesh = Mesh;
	Data->VisualMeshTargetHeightCm = 1800.0f;
	Unit->InitializeUnit(Data);

	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UPoseableMeshComponent* SourcePoseable = Visual ? Visual->GetPoseableMesh() : nullptr;
	if (!TestTrue(TEXT("VisualMesh creates an active poseable mesh"),
		Visual && Visual->IsMeshVisualActive() && SourcePoseable && SourcePoseable->IsVisible())
		|| !TestNotNull(TEXT("animator exists"), Animator))
	{
		return false;
	}

	// Give one bone a distinctive current pose so the test verifies an actual pose snapshot.
	const FName SnapshotBone = SourcePoseable->GetBoneIndex(TEXT("chest")) != INDEX_NONE
		? FName(TEXT("chest")) : SourcePoseable->GetBoneName(0);
	SourcePoseable->SetBoneRotationByName(SnapshotBone, FRotator(11.0f, 23.0f, 7.0f),
		EBoneSpaces::ComponentSpace);
	const FTransform ExpectedBoneTransform = SourcePoseable->GetBoneTransformByName(
		SnapshotBone, EBoneSpaces::ComponentSpace);

	const FLinearColor ModuleColor(1.0f, 0.08f, 0.24f, 0.18f);
	Animator->SetModuleAfterimageOverride(true, ModuleColor);
	FMSProceduralPoseInputs Inputs;
	Inputs.GroundSpeed = 600.0f;
	Animator->EvaluatePose(Inputs, 0.3f);

	TArray<AMSTransientVfxActor*> Afterimages;
	for (TActorIterator<AMSTransientVfxActor> It(TestWorld.World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("MSVfx.BoostAfterimage")))
		{
			Afterimages.Add(*It);
		}
	}
	if (!TestEqual(TEXT("module movement spawns exactly one afterimage actor"), Afterimages.Num(), 1))
	{
		return false;
	}

	TInlineComponentArray<UPoseableMeshComponent*> GhostMeshes(Afterimages[0]);
	TestEqual(TEXT("afterimage actor contains exactly one poseable ghost mesh"), GhostMeshes.Num(), 1);
	if (GhostMeshes.IsEmpty())
	{
		return false;
	}
	UPoseableMeshComponent* Ghost = GhostMeshes[0];
	TestTrue(TEXT("poseable ghost is registered and visible"),
		Ghost->IsRegistered() && Ghost->IsVisible() && !Ghost->bHiddenInGame);
	TestTrue(TEXT("poseable ghost uses the source visual mesh"),
		Ghost->GetSkinnedAsset() == SourcePoseable->GetSkinnedAsset());
	TestEqual(TEXT("poseable ghost copies every source bone"), Ghost->GetNumBones(), SourcePoseable->GetNumBones());
	TestTrue(TEXT("poseable ghost freezes the source's current bone pose"),
		Ghost->GetBoneTransformByName(SnapshotBone, EBoneSpaces::ComponentSpace)
			.Equals(ExpectedBoneTransform, 0.01f));

	UMaterialInstanceDynamic* GhostMaterial = Cast<UMaterialInstanceDynamic>(Ghost->GetMaterial(0));
	if (TestNotNull(TEXT("poseable ghost uses a dynamic afterimage material"), GhostMaterial))
	{
		TestTrue(TEXT("poseable ghost uses the configured red-pink color"),
			GhostMaterial->K2_GetVectorParameterValue(TEXT("Color")).Equals(ModuleColor, 0.001f));
	}
	return true;
}

#endif
