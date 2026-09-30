// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSAnimInstance.h"
#include "Animation/MSProceduralPoseSolver.h"

#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Core/MSScale.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponComponent.h"

UMSAnimInstance::UMSAnimInstance()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> FootstepSoundFinder(
		TEXT("/Game/Audio/Imported/SFX_Footstep_Metal.SFX_Footstep_Metal"));
	FootstepSound = FootstepSoundFinder.Object;
}

void UMSAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateAnimationInputs(Cast<AMSUnitBase>(TryGetPawnOwner()));
	UpdateProceduralPose(DeltaSeconds);
}

void UMSAnimInstance::UpdateProceduralPose(const float DeltaSeconds)
{
	CopyPublicOutputsToSolver();
	const float PreviousPhase = SolverState.WalkPhaseAngle;
	const FMSProceduralPoseInputs Inputs = MakeSolverInputs();
	const FMSProceduralPoseSettings Settings = MakeSolverSettings();
	FMSProceduralPoseSolver::Update(Inputs, Settings, SolverState, DeltaSeconds);
	const float PhaseAdvance = GroundSpeed * 0.01f / MSGO::MSScale * StrideFrequency *
		FMath::Max(DeltaSeconds, 0.0f) * UE_PI;
	if (!bIsInAir && !bIsBoosting && GroundSpeed >= FootstepMinSpeed && CrossesFootstepBoundary(PreviousPhase, PhaseAdvance) && UnitOwner)
	{
		MSAudio::SpawnMetaSoundAtLocation(UnitOwner,
			MSAudioPreset::Resolve(EMSAudioUsage::Footstep, FootstepSound),
			UnitOwner->GetActorLocation(), 55.0f);
	}
	CopySolverOutputsToPublic();
}

bool UMSAnimInstance::CrossesFootstepBoundary(const float PreviousPhase, const float PhaseAdvance)
{
	if (PhaseAdvance <= 0.0f)
	{
		return false;
	}
	const int32 PreviousContact = FMath::FloorToInt(PreviousPhase / UE_PI);
	const int32 NewContact = FMath::FloorToInt((PreviousPhase + PhaseAdvance) / UE_PI);
	return NewContact > PreviousContact;
}

EMSMeleeMotionType UMSAnimInstance::ResolveMeleeMotionType(const EMSMeleeMotionType RequestedType, const int32 WeaponPower)
{
	return FMSProceduralPoseSolver::ResolveMeleeMotionType(RequestedType, WeaponPower);
}

float UMSAnimInstance::GetMeleeDuration(const EMSMeleeMotionType MotionType, const bool bDashMelee) const
{
	return FMSProceduralPoseSolver::GetMeleeDuration(MotionType, bDashMelee, MakeSolverSettings());
}

float UMSAnimInstance::GetMeleePrimaryAngle(const EMSMeleeMotionType MotionType, const bool bDashMelee) const
{
	return FMSProceduralPoseSolver::GetMeleePrimaryAngle(MotionType, bDashMelee, MakeSolverSettings());
}

void UMSAnimInstance::StartMeleeMotion(const EMSMeleeMotionType MotionType, const int32 WeaponPower, const bool bDashMelee)
{
	CopyPublicOutputsToSolver();
	FMSProceduralPoseSolver::StartMeleeMotion(SolverState, MakeSolverSettings(), MotionType, WeaponPower, bDashMelee);
	SolverState.bMeleeMotionActive = !(bIsStaggered || bIsKnockedDown || bIsDead);
	CopySolverOutputsToPublic();
}

void UMSAnimInstance::StartKickMotion()
{
	CopyPublicOutputsToSolver();
	FMSProceduralPoseSolver::StartKickMotion(SolverState, MakeSolverInputs());
	CopySolverOutputsToPublic();
}

FMSProceduralPoseInputs UMSAnimInstance::MakeSolverInputs() const
{
	FMSProceduralPoseInputs I;
	I.GroundSpeed = GroundSpeed; I.WeaponSwitchProgress = WeaponSwitchProgress;
	I.bIsInAir = bIsInAir; I.bIsBoosting = bIsBoosting; I.bIsCrouching = bIsCrouching;
	I.bHasRangedWeapon = bHasRangedWeapon;
	I.bIsAiming = bIsAiming; I.bIsGuarding = bIsGuarding; I.bIsSwitchingWeapon = bIsSwitchingWeapon;
	I.bIsStaggered = bIsStaggered; I.bIsKnockedDown = bIsKnockedDown; I.bIsDead = bIsDead;
	return I;
}

FMSProceduralPoseSettings UMSAnimInstance::MakeSolverSettings() const
{
	FMSProceduralPoseSettings S;
	S.LegSwingAngle=LegSwingAngle; S.ArmSwingAngle=ArmSwingAngle; S.LegSwingFullSpeed=LegSwingFullSpeed; S.StrideFrequency=StrideFrequency;
	S.KneeBendAngle=KneeBendAngle; S.FootLevelRatio=FootLevelRatio; S.ElbowRestAngle=ElbowRestAngle;
	S.DashLeanMaxAngle=DashLeanMaxAngle; S.DashLegTrailAngle=DashLegTrailAngle; S.DashKneeBendAngle=DashKneeBendAngle; S.PoseBlendSpeed=PoseBlendSpeed;
	S.AimArmAngle=AimArmAngle; S.AimElbowShare=AimElbowShare; S.AimBlendSpeed=AimBlendSpeed;
	S.WeaponDrawArmAngle=WeaponDrawArmAngle; S.WeaponDrawElbowAngle=WeaponDrawElbowAngle;
	S.GuardUpperArmAngle=GuardUpperArmAngle; S.GuardForearmAngle=GuardForearmAngle; S.GuardBlendSpeed=GuardBlendSpeed;
	S.CrouchThighAngle=CrouchThighAngle; S.CrouchKneeAngle=CrouchKneeAngle; S.KneelThighAngle=KneelThighAngle; S.KneelKneeAngle=KneelKneeAngle;
	S.CrouchLowerHeight=CrouchLowerHeight; S.CrouchBlendSpeed=CrouchBlendSpeed;
	S.LimbMoveSpeedDegPerSec=LimbMoveSpeedDegPerSec;
	S.MeleeSwingDuration=MeleeSwingDuration; S.MeleeSwingAngle=MeleeSwingAngle; S.MeleeSwingRaiseAngle=MeleeSwingRaiseAngle;
	S.HeavySwingDurationMultiplier=HeavySwingDurationMultiplier; S.HeavySwingAngleMultiplier=HeavySwingAngleMultiplier; S.HeavyWeaponPowerThreshold=HeavyWeaponPowerThreshold;
	S.ThrustDuration=ThrustDuration; S.ThrustShoulderAngle=ThrustShoulderAngle; S.ThrustElbowExtendAngle=ThrustElbowExtendAngle;
	S.PunchDuration=PunchDuration; S.PunchShoulderAngle=PunchShoulderAngle; S.PunchElbowExtendAngle=PunchElbowExtendAngle;
	S.DashSwingDurationMultiplier=DashSwingDurationMultiplier; S.DashSwingAngleMultiplier=DashSwingAngleMultiplier;
	S.MeleeLungeThighAngle=MeleeLungeThighAngle; S.MeleeLungeKneeAngle=MeleeLungeKneeAngle; S.MeleeTorsoTwistAngle=MeleeTorsoTwistAngle;
	S.KickDuration=KickDuration; S.KickThighAngle=KickThighAngle; S.KickShinExtendAngle=KickShinExtendAngle;
	S.StaggerLeanAngle=StaggerLeanAngle; S.StaggerKneeBendAngle=StaggerKneeBendAngle; S.KnockdownLeanAngle=KnockdownLeanAngle; S.KnockdownLegKickAngle=KnockdownLegKickAngle;
	S.StaggerBlendSpeed=StaggerBlendSpeed; S.DeathKneelLeanAngle=DeathKneelLeanAngle; S.DeathKneelLowerMultiplier=DeathKneelLowerMultiplier;
	S.bLeftKneels=KneelingSide==EMSKneelingSide::Left;
	return S;
}

void UMSAnimInstance::CopyPublicOutputsToSolver()
{
	SolverState.WalkPhaseAngle=WalkPhaseAngle; SolverState.WalkIntensity=WalkIntensity; SolverState.WalkSwingAlpha=WalkSwingAlpha;
	SolverState.DashLeanAngle=DashLeanAngle; SolverState.DashPoseAlpha=DashLeanMaxAngle>UE_SMALL_NUMBER?DashLeanAngle/DashLeanMaxAngle:0.0f;
	SolverState.AimPoseAlpha=AimPoseAlpha; SolverState.GuardPoseAlpha=GuardPoseAlpha; SolverState.WeaponSwitchPoseAlpha=WeaponSwitchPoseAlpha;
	SolverState.CrouchPoseAlpha=CrouchPoseAlpha; SolverState.CrouchGroundOffset=CrouchGroundOffset;
	SolverState.LeftCrouchThighAngle=LeftCrouchThighAngle; SolverState.LeftCrouchKneeAngle=LeftCrouchKneeAngle;
	SolverState.RightCrouchThighAngle=RightCrouchThighAngle; SolverState.RightCrouchKneeAngle=RightCrouchKneeAngle;
	SolverState.bMeleeMotionActive=bMeleeMotionActive; SolverState.bKickMotionActive=bKickMotionActive; SolverState.ActiveMeleeMotionType=ActiveMeleeMotionType;
	SolverState.MeleeMotionAlpha=MeleeMotionAlpha; SolverState.MeleeShoulderAngle=MeleeShoulderAngleOutput; SolverState.MeleeElbowAngle=MeleeElbowAngleOutput;
	SolverState.MeleeRaiseAngle=MeleeRaiseAngleOutput; SolverState.MeleeLungeThighAngle=MeleeLungeThighAngleOutput; SolverState.MeleeLungeKneeAngle=MeleeLungeKneeAngleOutput;
	SolverState.MeleeTorsoTwistAngle=MeleeTorsoTwistAngleOutput; SolverState.KickThighAngle=KickThighAngleOutput; SolverState.KickShinAngle=KickShinAngleOutput;
	SolverState.TorsoLeanPitch=TorsoLeanPitch; SolverState.ThighKickAngle=ThighKickAngle; SolverState.ShinKickAngle=ShinKickAngle; SolverState.ArmSlackAngle=ArmSlackAngle;
}

void UMSAnimInstance::CopySolverOutputsToPublic()
{
	WalkPhaseAngle=SolverState.WalkPhaseAngle; WalkIntensity=SolverState.WalkIntensity; WalkSwingAlpha=SolverState.WalkSwingAlpha; DashLeanAngle=SolverState.DashLeanAngle;
	AimPoseAlpha=SolverState.AimPoseAlpha; GuardPoseAlpha=SolverState.GuardPoseAlpha; WeaponSwitchPoseAlpha=SolverState.WeaponSwitchPoseAlpha;
	CrouchPoseAlpha=SolverState.CrouchPoseAlpha; CrouchGroundOffset=SolverState.CrouchGroundOffset;
	LeftCrouchThighAngle=SolverState.LeftCrouchThighAngle; LeftCrouchKneeAngle=SolverState.LeftCrouchKneeAngle;
	RightCrouchThighAngle=SolverState.RightCrouchThighAngle; RightCrouchKneeAngle=SolverState.RightCrouchKneeAngle;
	bMeleeMotionActive=SolverState.bMeleeMotionActive; bKickMotionActive=SolverState.bKickMotionActive; ActiveMeleeMotionType=SolverState.ActiveMeleeMotionType;
	MeleeMotionAlpha=SolverState.MeleeMotionAlpha; MeleeShoulderAngleOutput=SolverState.MeleeShoulderAngle; MeleeElbowAngleOutput=SolverState.MeleeElbowAngle;
	MeleeRaiseAngleOutput=SolverState.MeleeRaiseAngle; MeleeLungeThighAngleOutput=SolverState.MeleeLungeThighAngle; MeleeLungeKneeAngleOutput=SolverState.MeleeLungeKneeAngle;
	MeleeTorsoTwistAngleOutput=SolverState.MeleeTorsoTwistAngle; KickThighAngleOutput=SolverState.KickThighAngle; KickShinAngleOutput=SolverState.KickShinAngle;
	TorsoLeanPitch=SolverState.TorsoLeanPitch; ThighKickAngle=SolverState.ThighKickAngle; ShinKickAngle=SolverState.ShinKickAngle; ArmSlackAngle=SolverState.ArmSlackAngle;
}

void UMSAnimInstance::UpdateAnimationInputs(AMSUnitBase* Unit)
{
	UnitOwner = Unit;
	Velocity = Unit ? Unit->GetVelocity() : FVector::ZeroVector;
	GroundSpeed = Velocity.Size2D();
	VerticalSpeed = Velocity.Z;
	bShouldMove = GroundSpeed > 1.0f;

	MovementDirection = 0.0f;
	if (Unit && bShouldMove)
	{
		const FVector LocalVelocity = Unit->GetActorTransform().InverseTransformVectorNoScale(Velocity);
		MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	}

	const UCharacterMovementComponent* Movement = Unit ? Unit->GetCharacterMovement() : nullptr;
	bIsInAir = Movement && Movement->IsFalling();

	const UAbilitySystemComponent* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
	auto HasState = [ASC](const FGameplayTag& Tag)
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	};
	bIsBoosting = HasState(MSTags::State_Boosting);
	bIsOverheated = HasState(MSTags::State_Overheated);
	bIsCrouching = HasState(MSTags::State_Crouching);
	bIsGuarding = HasState(MSTags::State_Guarding);
	bIsStaggered = HasState(MSTags::State_Staggered);
	bIsKnockedDown = HasState(MSTags::State_KnockedDown);
	bIsDead = HasState(MSTags::State_Dead);

	BoostRatio = 0.0f;
	if (const UMSAttributeSet_Boost* BoostSet = Unit ? Unit->GetBoostSet() : nullptr)
	{
		BoostRatio = BoostSet->GetMaxBoost() > UE_SMALL_NUMBER
			? FMath::Clamp(BoostSet->GetBoost() / BoostSet->GetMaxBoost(), 0.0f, 1.0f)
			: 0.0f;
	}

	const UMSWeaponComponent* Weapon = Unit ? Unit->GetWeaponComponent() : nullptr;
	bIsSwitchingWeapon = Weapon && Weapon->IsSwitching();
	WeaponSwitchProgress = bIsSwitchingWeapon ? Weapon->GetSwitchProgress01() : 0.0f;
	const UMSWeaponDataBase* ActiveWeapon = Weapon ? Weapon->GetWeaponData(Weapon->GetActiveSlotIndex()) : nullptr;
	bHasRangedWeapon = ActiveWeapon && !ActiveWeapon->IsA<UMSMeleeWeaponData>() && !ActiveWeapon->IsA<UMSShieldWeaponData>();

	// 構え(照準)判定はUMSMechAnimatorComponent::GatherInputsと同一方針:
	// 装備しているだけでは構えず、発射直後(クールダウン)/チャージ中のみ照準刺激とみなす。
	const bool bAimStimulus = (Weapon && Weapon->GetFireCooldownRemaining() > 0.0f)
		|| HasState(MSTags::State_Charging);
	const double Now = Unit && Unit->GetWorld() ? Unit->GetWorld()->GetTimeSeconds() : 0.0;
	if (bAimStimulus)
	{
		LastAimStimulusTime = Now;
	}
	bIsAiming = bHasRangedWeapon && (bAimStimulus || (Now - LastAimStimulusTime) < AimHoldSeconds)
		&& !bIsStaggered && !bIsKnockedDown && !bIsDead;
}
