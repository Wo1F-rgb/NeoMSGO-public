// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSProceduralPoseSolver.h"

#include "Core/MSScale.h"

void FMSProceduralPoseSolver::Update(const FMSProceduralPoseInputs& I, const FMSProceduralPoseSettings& S,
	FMSProceduralPoseState& O, const float DeltaSeconds)
{
	const float Dt = FMath::Max(DeltaSeconds, 0.0f);
	const bool bFullBodyDisabled = I.bIsFlinched || I.bIsStaggered || I.bIsKnockedDown || I.bIsDead;
	const float TargetTorso = I.bIsDead ? S.DeathKneelLeanAngle
		: I.bIsKnockedDown ? (I.bManagedHitReaction?0.f:-S.KnockdownLeanAngle)
		: I.bIsStaggered ? S.StaggerLeanAngle : I.bIsFlinched ? 10.f : 0.0f;
	const float TargetThigh = I.bIsDead ? 0.0f
		: I.bIsKnockedDown ? -S.KnockdownLegKickAngle * 0.6f : I.bIsStaggered ? (I.bManagedHitReaction?0.f:S.StaggerKneeBendAngle * 0.5f) : 0.0f;
	const float TargetShin = I.bIsDead ? 0.0f
		: I.bIsKnockedDown ? S.KnockdownLegKickAngle * 0.3f : I.bIsStaggered ? (I.bManagedHitReaction?0.f:S.StaggerKneeBendAngle) : 0.0f;
	const float TargetSlack = I.bIsDead ? 0.0f : I.bIsKnockedDown ? -30.0f : I.bIsStaggered ? 10.0f : 0.0f;
	// On recovery, draw a knee under the torso and bring the arms forward before standing.
	const bool bProneRecovery=I.bManagedHitReaction && I.bIsKnockedDown && !I.bIsDead
		&& I.GetUpSupportAlpha>0.f;
	const float RecoveryProgress=1.f-FMath::Clamp(I.HitReactionPoseWeight,0.f,1.f);
	// Draw the support leg under the body early, then extend before control returns.
	// A full sine plus another interpolation lag kept the knee folded at 0.7/0.8 s.
	const float SupportAlpha=bProneRecovery ? .45f*(RecoveryProgress<.35f
		? FMath::SmoothStep(0.f,.35f,RecoveryProgress)
		: 1.f-FMath::SmoothStep(.35f,.85f,RecoveryProgress)) : 0.f;
	O.TorsoLeanPitch = FMath::FInterpTo(O.TorsoLeanPitch, TargetTorso*I.HitReactionPoseWeight+22.f*SupportAlpha, Dt, S.StaggerBlendSpeed);
	O.ThighKickAngle = FMath::FInterpTo(O.ThighKickAngle, TargetThigh*I.HitReactionPoseWeight, Dt, S.StaggerBlendSpeed);
	O.ShinKickAngle = FMath::FInterpTo(O.ShinKickAngle, TargetShin*I.HitReactionPoseWeight, Dt, S.StaggerBlendSpeed);
	O.ArmSlackAngle = FMath::FInterpTo(O.ArmSlackAngle, TargetSlack*I.HitReactionPoseWeight-40.f*SupportAlpha, Dt, S.StaggerBlendSpeed);

	// GroundSpeed is scaled world cm/s. Normalize back to the Unity-equivalent m/s
	// before driving stride amplitude/frequency, otherwise 18 m rigs take ~9.33 tiny
	// steps per intended stride and visibly vibrate (WorldScale W0).
	const float SpeedMeters = I.GroundSpeed * 0.01f / MSGO::MSScale;
	const bool bFacingAllowed = I.bHasDirectionalFacing && !bFullBodyDisabled
		&& !I.bIsCrouching && !I.bHasAuthoredMotion && !O.bMeleeMotionActive && !O.bKickMotionActive;
	float TargetLegYaw = 0.0f;
	if (bFacingAllowed && SpeedMeters > 0.1f)
	{
		const float MoveYaw = FRotator::NormalizeAxis(I.MovementYaw);
		// A dead band around pure strafing prevents tiny camera/velocity changes from
		// repeatedly flipping the forward/backward stride and both legs' facing.
		if (FMath::Abs(MoveYaw) > 100.0f) O.bMovingBackward = true;
		else if (FMath::Abs(MoveYaw) < 80.0f) O.bMovingBackward = false;
		const float LegYaw = O.bMovingBackward
			? FRotator::NormalizeAxis(MoveYaw - 180.0f) : MoveYaw;
		TargetLegYaw = FMath::Clamp(LegYaw, -S.LegFacingLimit, S.LegFacingLimit);
	}
	else
	{
		O.bMovingBackward = false;
	}
	O.LowerBodyYaw = FMath::FInterpConstantTo(O.LowerBodyYaw, TargetLegYaw, Dt, S.FacingDegreesPerSecond);
	O.UpperBodyYaw = FMath::FInterpConstantTo(O.UpperBodyYaw, bFacingAllowed
		? FMath::Clamp(FRotator::NormalizeAxis(I.AimYaw), -S.UpperFacingLimit, S.UpperFacingLimit) : 0.0f,
		Dt, S.FacingDegreesPerSecond);
	if (I.bHasAuthoredMotion)
	{
		// Authored JSON uses absolute joint rotations; it cannot retain part of a cosmetic yaw.
		O.LowerBodyYaw = O.UpperBodyYaw = 0.0f;
	}
	O.WalkDirection = FMath::FInterpTo(O.WalkDirection, O.bMovingBackward ? -1.0f : 1.0f,
		Dt, S.PoseBlendSpeed);
	O.WalkIntensity = bFullBodyDisabled ? 0.0f : FMath::Clamp(SpeedMeters / FMath::Max(S.LegSwingFullSpeed, UE_SMALL_NUMBER), 0.0f, 1.0f);
	if (!bFullBodyDisabled && !I.bIsInAir && Dt > 0.0f)
	{
		O.WalkPhaseAngle = FMath::Fmod(O.WalkPhaseAngle + SpeedMeters * S.StrideFrequency * Dt * UE_PI, 2.0f * UE_PI);
	}
	O.WalkSwingAlpha = FMath::Sin(O.WalkPhaseAngle) * O.WalkIntensity * O.WalkDirection;

	const bool bDash = !bFullBodyDisabled && I.bIsBoosting && SpeedMeters > 0.5f;
	O.DashPoseAlpha = FMath::FInterpTo(O.DashPoseAlpha, bDash ? 1.0f : 0.0f, Dt, S.PoseBlendSpeed);
	O.DashLeanAngle = S.DashLeanMaxAngle * O.DashPoseAlpha;
	O.AimPoseAlpha = FMath::FInterpTo(O.AimPoseAlpha, !bFullBodyDisabled && I.bIsAiming ? 1.0f : 0.0f, Dt, S.AimBlendSpeed);
	// 射撃武器の「携行」オフセットは装備中のみ。歩行スイングへ加算され(置換ではない)、
	// 構え(Aim)へ入るときは同じアルファで取り除かれる。非装備(格闘/盾/素手)では0=純粋な歩行スイング。
	O.RangedRestPoseAlpha = !bFullBodyDisabled && I.bHasRangedWeapon ? 1.0f - O.AimPoseAlpha : 0.0f;
	O.GuardPoseAlpha = FMath::FInterpTo(O.GuardPoseAlpha, !bFullBodyDisabled && I.bIsGuarding ? 1.0f : 0.0f, Dt, S.GuardBlendSpeed);
	const float RequestedCrouchAlpha = bProneRecovery ? SupportAlpha : FMath::FInterpTo(O.CrouchPoseAlpha,
		I.bIsDead || (!bFullBodyDisabled && I.bIsCrouching) ? 1.0f
		: (I.bManagedHitReaction && I.bIsStaggered) ? I.HitReactionPoseWeight : SupportAlpha, Dt, S.CrouchBlendSpeed);
	// The root and all crouch angles must follow the same timeline. Otherwise the
	// root rises immediately while the independently speed-limited knees remain
	// folded, and the feet drop into place later. Bound alpha's rate by the largest
	// crouch joint derivative, including the ankle's alpha * (1-alpha) curve.
	// Walking, dash and melee can independently add other joint rotations.
	const float MaxThigh = FMath::Max(FMath::Abs(S.CrouchThighAngle), FMath::Abs(S.KneelThighAngle));
	const float MaxKnee = FMath::Max(FMath::Abs(S.CrouchKneeAngle), FMath::Abs(S.KneelKneeAngle));
	const float MaxJointDerivative = FMath::Max(FMath::Max(MaxThigh, MaxKnee),
		(MaxThigh + MaxKnee) * FMath::Abs(S.FootLevelRatio));
	const float MaxAlphaStep = FMath::Max(S.LimbMoveSpeedDegPerSec, 0.0f) * Dt
		/ FMath::Max(MaxJointDerivative, UE_SMALL_NUMBER);
	O.CrouchPoseAlpha += FMath::Clamp(RequestedCrouchAlpha - O.CrouchPoseAlpha, -MaxAlphaStep, MaxAlphaStep);
	O.LeftCrouchThighAngle = (S.bLeftKneels ? S.KneelThighAngle : S.CrouchThighAngle) * O.CrouchPoseAlpha;
	O.LeftCrouchKneeAngle = (S.bLeftKneels ? S.KneelKneeAngle : S.CrouchKneeAngle) * O.CrouchPoseAlpha;
	O.RightCrouchThighAngle = (S.bLeftKneels ? S.CrouchThighAngle : S.KneelThighAngle) * O.CrouchPoseAlpha;
	O.RightCrouchKneeAngle = (S.bLeftKneels ? S.CrouchKneeAngle : S.KneelKneeAngle) * O.CrouchPoseAlpha;
	O.CrouchGroundOffset = S.CrouchLowerHeight * (I.bIsDead ? S.DeathKneelLowerMultiplier : 1.0f)
		* 100.0f * MSGO::MSScale * O.CrouchPoseAlpha;
	O.WeaponSwitchPoseAlpha = !bFullBodyDisabled && I.bIsSwitchingWeapon
		? FMath::Sin(UE_PI * FMath::Clamp(I.WeaponSwitchProgress, 0.0f, 1.0f)) : 0.0f;

	if (bFullBodyDisabled)
	{
		O.bMeleeMotionActive = O.bKickMotionActive = false;
	}
	O.MeleeMotionAlpha = O.MeleeShoulderAngle = O.MeleeElbowAngle = O.MeleeRaiseAngle = 0.0f;
	O.MeleeLungeThighAngle = O.MeleeLungeKneeAngle = O.MeleeTorsoTwistAngle = 0.0f;
	O.KickThighAngle = O.KickShinAngle = 0.0f;
	if (O.bMeleeMotionActive)
	{
		O.MeleeMotionElapsed += Dt;
		const float Progress = FMath::Clamp(O.MeleeMotionElapsed / FMath::Max(O.ActiveMeleeDuration, UE_SMALL_NUMBER), 0.0f, 1.0f);
		const float OutFraction = O.ActiveMeleeMotionType == EMSMeleeMotionType::Punch ? 0.30f
			: O.ActiveMeleeMotionType == EMSMeleeMotionType::Thrust ? 0.40f
			: O.ActiveMeleeMotionType == EMSMeleeMotionType::Overhead ? 0.35f : 0.50f;
		O.MeleeMotionAlpha = Progress < OutFraction ? Progress / OutFraction : (1.0f - Progress) / (1.0f - OutFraction);
		O.MeleeMotionAlpha = FMath::Sin(FMath::Clamp(O.MeleeMotionAlpha, 0.0f, 1.0f) * UE_HALF_PI);
		const float Primary = GetMeleePrimaryAngle(O.ActiveMeleeMotionType, false, S) * O.ActiveMeleeAngleMultiplier * O.MeleeMotionAlpha;
		if (O.ActiveMeleeMotionType == EMSMeleeMotionType::Slash || O.ActiveMeleeMotionType == EMSMeleeMotionType::Overhead)
		{
			O.MeleeShoulderAngle = Primary * 0.7f;
			O.MeleeElbowAngle = Primary * 0.3f;
			O.MeleeRaiseAngle = S.MeleeSwingRaiseAngle * O.ActiveMeleeAngleMultiplier * O.MeleeMotionAlpha;
		}
		else
		{
			O.MeleeShoulderAngle = (O.ActiveMeleeMotionType == EMSMeleeMotionType::Thrust ? S.ThrustShoulderAngle : S.PunchShoulderAngle) * O.ActiveMeleeAngleMultiplier * O.MeleeMotionAlpha;
			O.MeleeElbowAngle = Primary;
		}
		O.MeleeLungeThighAngle = S.MeleeLungeThighAngle * O.MeleeMotionAlpha;
		O.MeleeLungeKneeAngle = S.MeleeLungeKneeAngle * O.MeleeMotionAlpha;
		O.MeleeTorsoTwistAngle = S.MeleeTorsoTwistAngle * (O.ActiveMeleeMotionType == EMSMeleeMotionType::Slash ? -1.0f : 1.0f) * O.MeleeMotionAlpha;
		O.bMeleeMotionActive = Progress < 1.0f;
	}
	if (O.bKickMotionActive)
	{
		O.KickMotionElapsed += Dt;
		const float Progress = FMath::Clamp(O.KickMotionElapsed / FMath::Max(S.KickDuration, UE_SMALL_NUMBER), 0.0f, 1.0f);
		const float Alpha = FMath::Clamp(Progress < 0.4f ? Progress / 0.4f : (1.0f - Progress) / 0.6f, 0.0f, 1.0f);
		O.KickThighAngle = S.KickThighAngle * Alpha;
		O.KickShinAngle = S.KickShinExtendAngle * Alpha;
		O.bKickMotionActive = Progress < 1.0f;
	}
}

EMSMeleeMotionType FMSProceduralPoseSolver::ResolveMeleeMotionType(const EMSMeleeMotionType Type, const int32 Power, const int32 Threshold)
{
	return Type == EMSMeleeMotionType::Slash && Power > Threshold ? EMSMeleeMotionType::Overhead : Type;
}

float FMSProceduralPoseSolver::GetMeleeDuration(const EMSMeleeMotionType Type, const bool bDash, const FMSProceduralPoseSettings& S)
{
	float Duration = Type == EMSMeleeMotionType::Thrust ? S.ThrustDuration : Type == EMSMeleeMotionType::Punch ? S.PunchDuration : S.MeleeSwingDuration;
	if (Type == EMSMeleeMotionType::Overhead) Duration *= S.HeavySwingDurationMultiplier;
	return Duration * (bDash ? S.DashSwingDurationMultiplier : 1.0f);
}

float FMSProceduralPoseSolver::GetMeleePrimaryAngle(const EMSMeleeMotionType Type, const bool bDash, const FMSProceduralPoseSettings& S)
{
	float Angle = Type == EMSMeleeMotionType::Thrust ? S.ThrustElbowExtendAngle : Type == EMSMeleeMotionType::Punch ? S.PunchElbowExtendAngle : S.MeleeSwingAngle;
	if (Type == EMSMeleeMotionType::Overhead) Angle *= S.HeavySwingAngleMultiplier;
	return Angle * (bDash ? S.DashSwingAngleMultiplier : 1.0f);
}

void FMSProceduralPoseSolver::StartMeleeMotion(FMSProceduralPoseState& O, const FMSProceduralPoseSettings& S,
	const EMSMeleeMotionType Type, const int32 Power, const bool bDash)
{
	O.ActiveMeleeMotionType = ResolveMeleeMotionType(Type, Power, S.HeavyWeaponPowerThreshold);
	O.MeleeMotionElapsed = 0.0f;
	O.ActiveMeleeDuration = GetMeleeDuration(O.ActiveMeleeMotionType, bDash, S);
	O.ActiveMeleeAngleMultiplier = bDash ? S.DashSwingAngleMultiplier : 1.0f;
	O.bKickMotionActive = false;
	O.bMeleeMotionActive = true;
}

void FMSProceduralPoseSolver::StartKickMotion(FMSProceduralPoseState& O, const FMSProceduralPoseInputs& I)
{
	O.KickMotionElapsed = 0.0f;
	O.bMeleeMotionActive = false;
	O.bKickMotionActive = !(I.bIsFlinched || I.bIsStaggered || I.bIsKnockedDown || I.bIsDead);
}
