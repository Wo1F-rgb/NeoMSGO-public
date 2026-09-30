// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/MSWeaponData.h"

/** AnimInstance-independent inputs shared by skeletal compatibility and the Cube rig. */
struct NEOMSGO_API FMSProceduralPoseInputs
{
	float GroundSpeed = 0.0f;
	float WeaponSwitchProgress = 0.0f;
	/** Cosmetic facing in actor space; previews without movement leave this disabled. */
	bool bHasDirectionalFacing = false;
	bool bHasAuthoredMotion = false;
	float MovementYaw = 0.0f;
	float AimYaw = 0.0f;
	bool bIsInAir = false;
	bool bIsBoosting = false;
	bool bIsCrouching = false;
	/** 射撃武器(非格闘/非盾)を装備中か。非構え時の携行(ranged rest)ポーズにのみ影響する。 */
	bool bHasRangedWeapon = false;
	/**
	 * 照準中(発射直後/チャージ中/スコープ中)のみtrue。装備しているだけでは構えない
	 * (実プレイ報告: 通常時は腕を下げ歩行スイング、構え時のみ両腕前方水平)。
	 */
	bool bIsAiming = false;
	bool bIsGuarding = false;
	bool bIsSwitchingWeapon = false;
	bool bIsStaggered = false;
	bool bIsFlinched = false;
	bool bManagedHitReaction = false;
	float HitReactionPoseWeight = 1.f;
	float GetUpSupportAlpha = 0.f;
	bool bIsKnockedDown = false;
	bool bIsDead = false;
};

/** Numeric values copied from Unity MechAnimator.cs. */
struct NEOMSGO_API FMSProceduralPoseSettings
{
	float LegSwingAngle = 62.0f, ArmSwingAngle = 34.0f, LegSwingFullSpeed = 1.8f, StrideFrequency = 0.55f;
	float KneeBendAngle = 55.0f, FootLevelRatio = 0.65f, ElbowRestAngle = 15.0f;
	float DashLeanMaxAngle = 18.0f, DashLegTrailAngle = 35.0f, DashKneeBendAngle = 30.0f, PoseBlendSpeed = 8.0f;
	/**
	 * 構え(照準)時の上腕角。ApplyPitchはUnity角をUEピッチへ「符号反転」して適用し、腕キューブは
	 * ローカル-Z方向へ伸びるため、UEピッチ+θで腕の-Z軸は(sinθ,0,-cosθ)=前方へ回る。
	 * つまり「負のUnity角=腕が機体前方へ上がる」。-75°でUEピッチ+75°=水平近くの前方構え。
	 * (回帰バグ修正: 旧値+75はUEピッチ-75=腕が後方へ伸びていた)
	 */
	float AimArmAngle = -75.0f;
	/**
	 * 大型携行武器(TAU-rusチャージブラスター等のインポート実メッシュ)用の脇抱え構え上腕角。
	 * -75°の水平構えだと武器が肩の高さに担がれて見えるため、ユーザー指定で
	 * 「肩ではなく脇に抱える」低い構えへ戻す(WeaponMountが腕ピッチを相殺して銃身は水平のまま)。
	 */
	float UnderarmAimArmAngle = -30.0f;
	float AimElbowShare = 0.25f, AimBlendSpeed = 6.0f;
	/** 射撃武器携行時に歩行スイングへ加算するオフセット(負=わずかに前方へ持ち上げる)。 */
	float RangedRestUpperArmAngle = -28.0f, RangedRestForearmAngle = -18.0f;
	float WeaponDrawArmAngle = 95.0f, WeaponDrawElbowAngle = 55.0f;
	float GuardUpperArmAngle = 20.0f, GuardForearmAngle = 95.0f, GuardBlendSpeed = 6.0f;
	// Keep the one-knee stance, with a right-angle knee bend requested by the user.
	float CrouchThighAngle = 88.0f, CrouchKneeAngle = 90.0f, KneelThighAngle = 12.0f, KneelKneeAngle = 90.0f;
	float CrouchLowerHeight = 0.35f, CrouchBlendSpeed = 5.0f;
	// Match the rig's angular speed so the shared crouch/root timeline cannot outrun a joint.
	float LimbMoveSpeedDegPerSec = 200.0f;
	float MeleeSwingDuration = 0.25f, MeleeSwingAngle = 130.0f, MeleeSwingRaiseAngle = 60.0f;
	float HeavySwingDurationMultiplier = 1.3f, HeavySwingAngleMultiplier = 1.35f;
	int32 HeavyWeaponPowerThreshold = 8000;
	float ThrustDuration = 0.22f, ThrustShoulderAngle = 55.0f, ThrustElbowExtendAngle = 70.0f;
	float PunchDuration = 0.16f, PunchShoulderAngle = 25.0f, PunchElbowExtendAngle = 85.0f;
	float DashSwingDurationMultiplier = 1.4f, DashSwingAngleMultiplier = 1.7f;
	float MeleeLungeThighAngle = 32.0f, MeleeLungeKneeAngle = 42.0f, MeleeTorsoTwistAngle = 22.0f;
	float KickDuration = 0.32f, KickThighAngle = 75.0f, KickShinExtendAngle = 35.0f;
	float StaggerLeanAngle = 22.0f, StaggerKneeBendAngle = 25.0f, KnockdownLeanAngle = 60.0f, KnockdownLegKickAngle = 35.0f;
	float StaggerBlendSpeed = 8.0f, DeathKneelLeanAngle = 12.0f, DeathKneelLowerMultiplier = 1.15f;
	bool bLeftKneels = true;
	/** Provisional visual tuning, not claimed as original-game measured values. */
	float LegFacingLimit = 55.0f, UpperFacingLimit = 60.0f, FacingDegreesPerSecond = 240.0f;
};

/** Persistent solver state and the complete pose output. Angles use Unity signs until applied to UE joints. */
struct NEOMSGO_API FMSProceduralPoseState
{
	float WalkPhaseAngle = 0.0f, WalkIntensity = 0.0f, WalkSwingAlpha = 0.0f;
	float DashPoseAlpha = 0.0f, DashLeanAngle = 0.0f, AimPoseAlpha = 0.0f, RangedRestPoseAlpha = 1.0f,
		GuardPoseAlpha = 0.0f;
	float WeaponSwitchPoseAlpha = 0.0f, CrouchPoseAlpha = 0.0f, CrouchGroundOffset = 0.0f;
	float LeftCrouchThighAngle = 0.0f, LeftCrouchKneeAngle = 0.0f;
	float RightCrouchThighAngle = 0.0f, RightCrouchKneeAngle = 0.0f;
	bool bMeleeMotionActive = false, bKickMotionActive = false;
	EMSMeleeMotionType ActiveMeleeMotionType = EMSMeleeMotionType::Slash;
	float MeleeMotionAlpha = 0.0f, MeleeShoulderAngle = 0.0f, MeleeElbowAngle = 0.0f, MeleeRaiseAngle = 0.0f;
	float MeleeLungeThighAngle = 0.0f, MeleeLungeKneeAngle = 0.0f, MeleeTorsoTwistAngle = 0.0f;
	float KickThighAngle = 0.0f, KickShinAngle = 0.0f;
	float TorsoLeanPitch = 0.0f, ThighKickAngle = 0.0f, ShinKickAngle = 0.0f, ArmSlackAngle = 0.0f;
	float MeleeMotionElapsed = 0.0f, ActiveMeleeDuration = 0.0f, ActiveMeleeAngleMultiplier = 1.0f;
	float KickMotionElapsed = 0.0f;
	float LowerBodyYaw = 0.0f, UpperBodyYaw = 0.0f, WalkDirection = 1.0f;
	bool bMovingBackward = false;
};

/** Pure calculation layer; it owns no UObject and can be consumed by any visual representation. */
class NEOMSGO_API FMSProceduralPoseSolver
{
public:
	static void Update(const FMSProceduralPoseInputs& Inputs, const FMSProceduralPoseSettings& Settings,
		FMSProceduralPoseState& State, float DeltaSeconds);
	static EMSMeleeMotionType ResolveMeleeMotionType(EMSMeleeMotionType RequestedType, int32 WeaponPower, int32 Threshold = 8000);
	static float GetMeleeDuration(EMSMeleeMotionType MotionType, bool bDashMelee, const FMSProceduralPoseSettings& Settings);
	static float GetMeleePrimaryAngle(EMSMeleeMotionType MotionType, bool bDashMelee, const FMSProceduralPoseSettings& Settings);
	static void StartMeleeMotion(FMSProceduralPoseState& State, const FMSProceduralPoseSettings& Settings,
		EMSMeleeMotionType MotionType, int32 WeaponPower, bool bDashMelee);
	static void StartKickMotion(FMSProceduralPoseState& State, const FMSProceduralPoseInputs& Inputs);
};
