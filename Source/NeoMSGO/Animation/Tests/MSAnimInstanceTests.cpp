// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Animation/MSAnimInstance.h"
#include "Core/MSScale.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceInputTest,
	"NeoMSGO.Animation.AnimInstance.Inputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceProceduralPoseTest,
	"NeoMSGO.Animation.AnimInstance.ProceduralPose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceCrouchPoseP53Test,
	"NeoMSGO.Animation.AnimInstance.CrouchPoseP53",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceMeleePoseP54Test,
	"NeoMSGO.Animation.AnimInstance.MeleePoseP54",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceStaggerPoseP55Test,
	"NeoMSGO.Animation.AnimInstance.StaggerPoseP55",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAnimInstanceDeathKneelPoseP56Test,
	"NeoMSGO.Animation.AnimInstance.DeathKneelPoseP56",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMSAnimInputTestWorld
	{
		UWorld* World = nullptr;

		FMSAnimInputTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSAnimInputTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMSAnimInputTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

bool FMSAnimInstanceInputTest::RunTest(const FString& Parameters)
{
	FMSAnimInputTestWorld TestWorld;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator(0.0f, 90.0f, 0.0f), SpawnParams);
	TestNotNull(TEXT("共通基底AMSUnitBase派生を生成できる"), Unit);
	if (!Unit)
	{
		return false;
	}

	Unit->GetCharacterMovement()->Velocity = FVector(0.0f, 300.0f, 40.0f);
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	ASC->SetLooseGameplayTagCount(MSTags::State_Boosting, 1);
	ASC->SetLooseGameplayTagCount(MSTags::State_Crouching, 1);
	ASC->SetLooseGameplayTagCount(MSTags::State_Guarding, 1);
	Unit->GetBoostSet()->SetMaxBoost(200.0f);
	Unit->GetBoostSet()->SetBoost(50.0f);

	// UAnimInstanceはWithin=USkeletalMeshComponent。実運用と同じくCharacter MeshをOuterにする。
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(Unit->GetMesh());
	Anim->UpdateAnimationInputs(Unit);

	TestEqual(TEXT("速度ベクトルを取得"), Anim->Velocity, FVector(0.0f, 300.0f, 40.0f));
	TestEqual(TEXT("地上速度を取得"), Anim->GroundSpeed, 300.0f);
	TestEqual(TEXT("垂直速度を取得"), Anim->VerticalSpeed, 40.0f);
	TestTrue(TEXT("移動フラグを更新"), Anim->bShouldMove);
	TestTrue(TEXT("Actor前方に対する移動方向を更新"), FMath::IsNearlyZero(Anim->MovementDirection, 0.01f));
	TestTrue(TEXT("State.Boostingを取得"), Anim->bIsBoosting);
	TestTrue(TEXT("State.Crouchingを取得"), Anim->bIsCrouching);
	TestTrue(TEXT("State.Guardingを取得"), Anim->bIsGuarding);
	TestTrue(TEXT("Boost比率を取得"), FMath::IsNearlyEqual(Anim->BoostRatio, 0.25f));

	Anim->UpdateAnimationInputs(nullptr);
	TestEqual(TEXT("Ownerなしでは速度をリセット"), Anim->GroundSpeed, 0.0f);
	TestFalse(TEXT("Ownerなしでは状態をリセット"), Anim->bIsBoosting || Anim->bIsCrouching || Anim->bIsGuarding);

	const TArray<FName> RequiredProperties = {
		TEXT("Velocity"), TEXT("GroundSpeed"), TEXT("bIsBoosting"), TEXT("BoostRatio"),
		TEXT("bIsCrouching"), TEXT("bIsAiming"), TEXT("bIsGuarding")};
	for (const FName PropertyName : RequiredProperties)
	{
		const FProperty* Property = UMSAnimInstance::StaticClass()->FindPropertyByName(PropertyName);
		TestNotNull(*FString::Printf(TEXT("AnimBP入力 %s を公開"), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s はBlueprint公開"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
			TestTrue(*FString::Printf(TEXT("%s はBlueprintReadOnly"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
		}
	}

	return true;
}

bool FMSAnimInstanceProceduralPoseTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(Mesh);
	TestNotNull(TEXT("Create procedural AnimInstance"), Anim);
	if (!Anim)
	{
		return false;
	}

	TestTrue(TEXT("Unity stride frequency is preserved"), FMath::IsNearlyEqual(Anim->StrideFrequency, 0.55f));
	// 18 m world speed must normalize to the same 1.8 m/s solver input (W0).
	Anim->GroundSpeed = 180.0f * MSGO::MSScale;
	Anim->UpdateProceduralPose(1.0f / Anim->StrideFrequency);
	TestTrue(TEXT("Walk phase uses the Unity speed/frequency sin-wave"),
		FMath::IsNearlyEqual(Anim->WalkPhaseAngle, 1.8f * UE_PI, 0.001f));
	TestTrue(TEXT("Walk intensity reaches one at full speed"), FMath::IsNearlyEqual(Anim->WalkIntensity, 1.0f));
	TestTrue(TEXT("Walk swing is the phase sine"),
		FMath::IsNearlyEqual(Anim->WalkSwingAlpha, FMath::Sin(Anim->WalkPhaseAngle), 0.001f));

	const float GroundedPhase = Anim->WalkPhaseAngle;
	Anim->bIsInAir = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Walk phase stops in air"), FMath::IsNearlyEqual(Anim->WalkPhaseAngle, GroundedPhase));

	Anim->bIsInAir = false;
	Anim->bIsBoosting = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Dash lean reaches the configured forward angle"),
		FMath::IsNearlyEqual(Anim->DashLeanAngle, Anim->DashLeanMaxAngle));

	Anim->bIsAiming = true;
	Anim->bIsGuarding = true;
	Anim->bIsSwitchingWeapon = true;
	Anim->WeaponSwitchProgress = 0.5f;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Aim state produces full pose weight"), FMath::IsNearlyEqual(Anim->AimPoseAlpha, 1.0f));
	TestTrue(TEXT("Guard state produces full pose weight"), FMath::IsNearlyEqual(Anim->GuardPoseAlpha, 1.0f));
	TestTrue(TEXT("Weapon switch peaks at progress 0.5"), FMath::IsNearlyEqual(Anim->WeaponSwitchPoseAlpha, 1.0f));

	Anim->WeaponSwitchProgress = 1.0f;
	Anim->UpdateProceduralPose(0.0f);
	TestTrue(TEXT("Weapon switch returns to zero at completion"), FMath::IsNearlyZero(Anim->WeaponSwitchPoseAlpha, 0.001f));

	const TArray<FName> ProceduralOutputs = {
		TEXT("WalkPhaseAngle"), TEXT("DashLeanAngle"), TEXT("AimPoseAlpha"),
		TEXT("WeaponSwitchPoseAlpha"), TEXT("GuardPoseAlpha")};
	for (const FName PropertyName : ProceduralOutputs)
	{
		const FProperty* Property = UMSAnimInstance::StaticClass()->FindPropertyByName(PropertyName);
		TestNotNull(*FString::Printf(TEXT("Procedural output %s is reflected"), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s is Blueprint visible"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		}
	}

	return true;
}

bool FMSAnimInstanceCrouchPoseP53Test::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(Mesh);
	TestNotNull(TEXT("Create crouch pose AnimInstance"), Anim);
	if (!Anim)
	{
		return false;
	}

	TestTrue(TEXT("Unity crouch support thigh angle is preserved"), FMath::IsNearlyEqual(Anim->CrouchThighAngle, 88.0f));
	TestTrue(TEXT("Default crouch support knee angle is 90 degrees"), FMath::IsNearlyEqual(Anim->CrouchKneeAngle, 90.0f));
	TestTrue(TEXT("Unity kneeling thigh angle is preserved"), FMath::IsNearlyEqual(Anim->KneelThighAngle, 12.0f));
	TestTrue(TEXT("Default kneeling knee angle is 90 degrees"), FMath::IsNearlyEqual(Anim->KneelKneeAngle, 90.0f));
	TestTrue(TEXT("Unity crouch lowering is preserved"), FMath::IsNearlyEqual(Anim->CrouchLowerHeight, 0.35f));
	TestTrue(TEXT("Unity crouch blend speed is preserved"), FMath::IsNearlyEqual(Anim->CrouchBlendSpeed, 5.0f));

	Anim->bIsCrouching = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Crouching reaches full pose weight"), FMath::IsNearlyEqual(Anim->CrouchPoseAlpha, 1.0f));
	TestTrue(TEXT("Default left leg receives kneeling thigh angle"), FMath::IsNearlyEqual(Anim->LeftCrouchThighAngle, 12.0f));
	TestTrue(TEXT("Default right leg receives support thigh angle"), FMath::IsNearlyEqual(Anim->RightCrouchThighAngle, 88.0f));
	TestTrue(TEXT("AnimInstance output applies the 90 degree kneeling knee"), FMath::IsNearlyEqual(Anim->LeftCrouchKneeAngle, 90.0f));
	TestTrue(TEXT("AnimInstance output applies the 90 degree support knee"), FMath::IsNearlyEqual(Anim->RightCrouchKneeAngle, 90.0f));
	TestTrue(TEXT("Ground offset converts Unity meters to scaled UE centimeters"),
		FMath::IsNearlyEqual(Anim->CrouchGroundOffset, 35.0f * MSGO::MSScale));

	Anim->KneelingSide = EMSKneelingSide::Right;
	Anim->UpdateProceduralPose(0.0f);
	TestTrue(TEXT("Right selection swaps the kneeling leg"), FMath::IsNearlyEqual(Anim->RightCrouchThighAngle, 12.0f));
	TestTrue(TEXT("Right selection swaps the supporting leg"), FMath::IsNearlyEqual(Anim->LeftCrouchThighAngle, 88.0f));
	TestTrue(TEXT("Right selection keeps both configured knee outputs at 90 degrees"),
		FMath::IsNearlyEqual(Anim->LeftCrouchKneeAngle, 90.0f) && FMath::IsNearlyEqual(Anim->RightCrouchKneeAngle, 90.0f));

	// Keep an explicit legacy-angle path so the quaternion/easing implementation
	// remains covered for the former >90 degree values even after the defaults move
	// to the requested 90 degree symmetric crouch.
	// Restore the fixture's original left-kneeling side: the assertions below
	// intentionally expect the support (right) leg to use CrouchKneeAngle and
	// the kneeling (left) leg to use KneelKneeAngle.
	Anim->KneelingSide = EMSKneelingSide::Left;
	Anim->bIsCrouching = false;
	Anim->UpdateProceduralPose(1.0f);
	Anim->CrouchKneeAngle = 150.0f;
	Anim->KneelKneeAngle = 145.0f;
	Anim->bIsCrouching = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Explicit legacy crouch knee angle still reaches 150 degrees"), FMath::IsNearlyEqual(Anim->RightCrouchKneeAngle, 150.0f));
	TestTrue(TEXT("Explicit legacy kneeling knee angle still reaches 145 degrees"), FMath::IsNearlyEqual(Anim->LeftCrouchKneeAngle, 145.0f));

	Anim->bIsCrouching = false;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Leaving crouch clears pose weight"), FMath::IsNearlyZero(Anim->CrouchPoseAlpha));
	TestTrue(TEXT("Leaving crouch clears ground offset"), FMath::IsNearlyZero(Anim->CrouchGroundOffset));

	const TArray<FName> CrouchOutputs = {
		TEXT("CrouchPoseAlpha"), TEXT("CrouchGroundOffset"), TEXT("LeftCrouchThighAngle"),
		TEXT("LeftCrouchKneeAngle"), TEXT("RightCrouchThighAngle"), TEXT("RightCrouchKneeAngle")};
	for (const FName PropertyName : CrouchOutputs)
	{
		const FProperty* Property = UMSAnimInstance::StaticClass()->FindPropertyByName(PropertyName);
		TestNotNull(*FString::Printf(TEXT("Crouch output %s is reflected"), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s is Blueprint visible"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		}
	}

	return true;
}

bool FMSAnimInstanceMeleePoseP54Test::RunTest(const FString& Parameters)
{
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(NewObject<USkeletalMeshComponent>());
	TestEqual(TEXT("Heavy slash resolves to overhead"),
		UMSAnimInstance::ResolveMeleeMotionType(EMSMeleeMotionType::Slash, 8001), EMSMeleeMotionType::Overhead);
	TestEqual(TEXT("Threshold itself stays slash"),
		UMSAnimInstance::ResolveMeleeMotionType(EMSMeleeMotionType::Slash, 8000), EMSMeleeMotionType::Slash);
	TestTrue(TEXT("Slash duration preserved"), FMath::IsNearlyEqual(Anim->GetMeleeDuration(EMSMeleeMotionType::Slash, false), 0.25f));
	TestTrue(TEXT("Overhead duration multiplier preserved"), FMath::IsNearlyEqual(Anim->GetMeleeDuration(EMSMeleeMotionType::Overhead, false), 0.25f * 1.3f));
	TestTrue(TEXT("Thrust duration preserved"), FMath::IsNearlyEqual(Anim->GetMeleeDuration(EMSMeleeMotionType::Thrust, false), 0.22f));
	TestTrue(TEXT("Punch duration preserved"), FMath::IsNearlyEqual(Anim->GetMeleeDuration(EMSMeleeMotionType::Punch, false), 0.16f));
	TestTrue(TEXT("Dash duration is 1.4x"), FMath::IsNearlyEqual(Anim->GetMeleeDuration(EMSMeleeMotionType::Slash, true), 0.25f * 1.4f));
	TestTrue(TEXT("Dash angle is 1.7x"), FMath::IsNearlyEqual(Anim->GetMeleePrimaryAngle(EMSMeleeMotionType::Slash, true), 130.0f * 1.7f));

	Anim->StartMeleeMotion(EMSMeleeMotionType::Slash, 9000, true);
	TestEqual(TEXT("Start resolves heavy weapon"), Anim->ActiveMeleeMotionType, EMSMeleeMotionType::Overhead);
	Anim->UpdateProceduralPose(0.1f);
	TestTrue(TEXT("Melee computes limb output"), Anim->MeleeShoulderAngleOutput > 0.0f && Anim->MeleeLungeThighAngleOutput > 0.0f);
	Anim->bIsStaggered = true;
	Anim->UpdateProceduralPose(0.0f);
	TestFalse(TEXT("Stagger forcibly interrupts melee"), Anim->bMeleeMotionActive);

	return true;
}

bool FMSAnimInstanceStaggerPoseP55Test::RunTest(const FString& Parameters)
{
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(NewObject<USkeletalMeshComponent>());
	TestTrue(TEXT("Unity stagger lean is preserved"), FMath::IsNearlyEqual(Anim->StaggerLeanAngle, 22.0f));
	TestTrue(TEXT("Unity knockdown lean is preserved"), FMath::IsNearlyEqual(Anim->KnockdownLeanAngle, 60.0f));

	Anim->bIsStaggered = true;
	Anim->bIsCrouching = Anim->bIsAiming = Anim->bIsGuarding = Anim->bIsSwitchingWeapon = true;
	Anim->WeaponSwitchProgress = 0.5f;
	Anim->StartMeleeMotion(EMSMeleeMotionType::Slash, 1000, false);
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Stagger leans forward"), FMath::IsNearlyEqual(Anim->TorsoLeanPitch, 22.0f));
	TestTrue(TEXT("Stagger thigh uses half knee bend"), FMath::IsNearlyEqual(Anim->ThighKickAngle, 12.5f));
	TestTrue(TEXT("Stagger shin uses full knee bend"), FMath::IsNearlyEqual(Anim->ShinKickAngle, 25.0f));
	TestTrue(TEXT("Stagger arm has dedicated slack"), FMath::IsNearlyEqual(Anim->ArmSlackAngle, 10.0f));
	TestTrue(TEXT("Stagger suppresses competing pose outputs"), FMath::IsNearlyZero(Anim->WeaponSwitchPoseAlpha)
		&& FMath::IsNearlyZero(Anim->CrouchPoseAlpha) && !Anim->bMeleeMotionActive);

	const float StaggerThigh = Anim->ThighKickAngle;
	Anim->bIsStaggered = false;
	Anim->bIsKnockedDown = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Knockdown falls backward"), FMath::IsNearlyEqual(Anim->TorsoLeanPitch, -60.0f));
	TestTrue(TEXT("Knockdown thigh kicks backward"), FMath::IsNearlyEqual(Anim->ThighKickAngle, -21.0f));
	TestTrue(TEXT("Knockdown shin has its dedicated angle"), FMath::IsNearlyEqual(Anim->ShinKickAngle, 10.5f));
	TestFalse(TEXT("Knockdown leg differs from stagger"), FMath::IsNearlyEqual(Anim->ThighKickAngle, StaggerThigh));
	TestTrue(TEXT("Knockdown arms trail backward"), FMath::IsNearlyEqual(Anim->ArmSlackAngle, -30.0f));

	Anim->bIsKnockedDown = false;
	Anim->bIsCrouching = Anim->bIsAiming = Anim->bIsGuarding = Anim->bIsSwitchingWeapon = false;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Recovery returns stagger outputs to neutral"), FMath::IsNearlyZero(Anim->TorsoLeanPitch)
		&& FMath::IsNearlyZero(Anim->ThighKickAngle) && FMath::IsNearlyZero(Anim->ShinKickAngle)
		&& FMath::IsNearlyZero(Anim->ArmSlackAngle));

	const TArray<FName> StaggerOutputs = {TEXT("TorsoLeanPitch"), TEXT("ThighKickAngle"), TEXT("ShinKickAngle"), TEXT("ArmSlackAngle")};
	for (const FName PropertyName : StaggerOutputs)
	{
		const FProperty* Property = UMSAnimInstance::StaticClass()->FindPropertyByName(PropertyName);
		TestNotNull(*FString::Printf(TEXT("Stagger output %s is reflected"), *PropertyName.ToString()), Property);
		if (Property) TestTrue(*FString::Printf(TEXT("%s is Blueprint visible"), *PropertyName.ToString()), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}
	return true;
}

bool FMSAnimInstanceDeathKneelPoseP56Test::RunTest(const FString& Parameters)
{
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(NewObject<USkeletalMeshComponent>());
	Anim->bIsDead = true;
	Anim->bIsCrouching = Anim->bIsAiming = Anim->bIsGuarding = Anim->bIsSwitchingWeapon = true;
	Anim->bIsBoosting = true;
	Anim->GroundSpeed = 180.0f;
	Anim->WeaponSwitchProgress = 0.5f;
	Anim->StartMeleeMotion(EMSMeleeMotionType::Slash, 1000, false);
	Anim->StartKickMotion();
	Anim->UpdateProceduralPose(1.0f);

	TestTrue(TEXT("Death kneel reaches full asymmetric crouch weight"), FMath::IsNearlyEqual(Anim->CrouchPoseAlpha, 1.0f));
	TestTrue(TEXT("Death kneel leans torso forward twelve degrees"), FMath::IsNearlyEqual(Anim->TorsoLeanPitch, 12.0f));
	TestTrue(TEXT("Death kneel lowers root by crouch height times 1.15"),
		FMath::IsNearlyEqual(Anim->CrouchGroundOffset,
			Anim->CrouchLowerHeight * 1.15f * 100.0f * MSGO::MSScale, 0.001f));
	TestTrue(TEXT("Death kneel keeps the configured asymmetric thigh angles"),
		FMath::IsNearlyEqual(Anim->LeftCrouchThighAngle, Anim->KneelThighAngle)
		&& FMath::IsNearlyEqual(Anim->RightCrouchThighAngle, Anim->CrouchThighAngle)
		&& !FMath::IsNearlyEqual(Anim->LeftCrouchThighAngle, Anim->RightCrouchThighAngle));
	TestTrue(TEXT("Death kneel keeps the configured knee angles"),
		FMath::IsNearlyEqual(Anim->LeftCrouchKneeAngle, Anim->KneelKneeAngle)
		&& FMath::IsNearlyEqual(Anim->RightCrouchKneeAngle, Anim->CrouchKneeAngle));
	TestTrue(TEXT("Death suppresses every competing procedural pose"), FMath::IsNearlyZero(Anim->WalkIntensity)
		&& FMath::IsNearlyZero(Anim->DashLeanAngle) && FMath::IsNearlyZero(Anim->AimPoseAlpha)
		&& FMath::IsNearlyZero(Anim->GuardPoseAlpha) && FMath::IsNearlyZero(Anim->WeaponSwitchPoseAlpha)
		&& !Anim->bMeleeMotionActive && !Anim->bKickMotionActive);

	Anim->bIsStaggered = Anim->bIsKnockedDown = true;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Death torso wins over simultaneous stagger and knockdown"), FMath::IsNearlyEqual(Anim->TorsoLeanPitch, 12.0f));
	TestTrue(TEXT("Death legs win over simultaneous stagger and knockdown"), FMath::IsNearlyZero(Anim->ThighKickAngle)
		&& FMath::IsNearlyZero(Anim->ShinKickAngle) && FMath::IsNearlyEqual(Anim->RightCrouchThighAngle, 88.0f));

	Anim->bIsDead = Anim->bIsStaggered = Anim->bIsKnockedDown = false;
	Anim->bIsCrouching = Anim->bIsAiming = Anim->bIsGuarding = Anim->bIsSwitchingWeapon = false;
	Anim->bIsBoosting = false;
	Anim->GroundSpeed = 0.0f;
	Anim->UpdateProceduralPose(1.0f);
	TestTrue(TEXT("Clearing death returns torso and death kneel to neutral"), FMath::IsNearlyZero(Anim->TorsoLeanPitch)
		&& FMath::IsNearlyZero(Anim->CrouchPoseAlpha) && FMath::IsNearlyZero(Anim->CrouchGroundOffset)
		&& FMath::IsNearlyZero(Anim->LeftCrouchThighAngle) && FMath::IsNearlyZero(Anim->RightCrouchThighAngle));

	return true;
}

#endif
