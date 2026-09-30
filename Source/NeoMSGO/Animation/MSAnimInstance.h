// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/MSProceduralPoseSolver.h"
#include "Data/MSWeaponData.h"
#include "MSAnimInstance.generated.h"

class AMSUnitBase;
class USoundBase;

/** Selects the leg that contacts the ground in the asymmetric one-knee pose. */
UENUM(BlueprintType)
enum class EMSKneelingSide : uint8
{
	Left,
	Right
};

/**
 * Player/BOT共通のAnimation Blueprint入力データ層。
 *
 * Unity版MechAnimatorはAnimationClipを使わず、sin波歩行、ダッシュ前傾などを
 * 全関節へ直接書き込んでいた。UE5でもその方針を維持し、このクラスが収集した値を
 * ABPのState MachineからControl Rigへ渡してボーンを直接駆動する。Control Rig
 * グラフとSkeletonアセットはエディタ作業となるためP5-1の対象外。
 */
UCLASS(Blueprintable, BlueprintType)
class NEOMSGO_API UMSAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMSAnimInstance();
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Updates the P5-2 procedural pose values consumed by an AnimBP/Control Rig. */
	void UpdateProceduralPose(float DeltaSeconds);

	/**
	 * Unitから共通入力を収集する。NativeUpdateAnimationの実処理でありAutomationテストからも使用する。
	 * P6-1のBOT固有値(Blackboardの武器カテゴリ、攻撃意図等)はBOT Character/AnimInstance側で
	 * GameplayTagまたは本関数後の拡張プロパティへ同期し、AnimBPからBlackboardを直接参照しない。
	 */
	void UpdateAnimationInputs(AMSUnitBase* Unit);

	/** Starts a procedural melee swing using the resolved weapon data. */
	UFUNCTION(BlueprintCallable, Category = "MS|Animation|Procedural|Melee")
	void StartMeleeMotion(EMSMeleeMotionType MotionType, int32 WeaponPower, bool bDashMelee);

	/** Starts the section 24 kick pose (used by strong BOT attacks). */
	UFUNCTION(BlueprintCallable, Category = "MS|Animation|Procedural|Melee")
	void StartKickMotion();

	UFUNCTION(BlueprintPure, Category = "MS|Animation|Procedural|Melee")
	static EMSMeleeMotionType ResolveMeleeMotionType(EMSMeleeMotionType RequestedType, int32 WeaponPower);

	UFUNCTION(BlueprintPure, Category = "MS|Animation|Procedural|Melee")
	float GetMeleeDuration(EMSMeleeMotionType MotionType, bool bDashMelee) const;

	UFUNCTION(BlueprintPure, Category = "MS|Animation|Procedural|Melee")
	float GetMeleePrimaryAngle(EMSMeleeMotionType MotionType, bool bDashMelee) const;

	/** True when a walking phase advance crosses either foot's contact boundary. */
	static bool CrossesFootstepBoundary(float PreviousPhase, float PhaseAdvance);
	USoundBase* GetFootstepSound() const { return FootstepSound; }

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	TObjectPtr<AMSUnitBase> UnitOwner;

	/** ワールド速度(cm/s)。Control Rigの歩行位相はGroundSpeedを入力にして計算する。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input", meta = (Units = "cm/s"))
	float GroundSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input", meta = (Units = "cm/s"))
	float VerticalSpeed = 0.0f;

	/** Actor前方=0、右=+90、左=-90度の移動方向。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input", meta = (Units = "deg"))
	float MovementDirection = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	float BoostRatio = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	float WeaponSwitchProgress = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bShouldMove = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsBoosting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsOverheated = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsCrouching = false;

	/** 射撃武器(非格闘/非盾)を装備中か。携行ポーズのオフセットに使用する。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bHasRangedWeapon = false;

	/** 照準中(発射直後/チャージ中)のみtrue。装備しているだけでは構えない。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsAiming = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsGuarding = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsSwitchingWeapon = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsStaggered = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsKnockedDown = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Input")
	bool bIsDead = false;

	// Unity MechAnimator weight/motion tuning. UE speed inputs are converted from cm/s to m/s.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking")
	float LegSwingAngle = 62.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking")
	float ArmSwingAngle = 34.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking", meta = (Units = "m/s", ClampMin = "0.01"))
	float LegSwingFullSpeed = 1.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking", meta = (ClampMin = "0.0"))
	float StrideFrequency = 0.55f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking", meta = (Units = "cm/s", ClampMin = "0.0"))
	float FootstepMinSpeed = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking", meta = (Units = "deg/s"))
	float LimbMoveSpeedDegPerSec = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking")
	float KneeBendAngle = 55.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking")
	float FootLevelRatio = 0.65f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Walking")
	float ElbowRestAngle = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Dash", meta = (Units = "deg"))
	float DashLeanMaxAngle = 18.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Dash", meta = (Units = "deg"))
	float DashLegTrailAngle = 35.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Dash", meta = (Units = "deg"))
	float DashKneeBendAngle = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Dash")
	float PoseBlendSpeed = 8.0f;

	/** 負のUnity角=腕が前方へ上がる(MSProceduralPoseSolver.h AimArmAngleの符号規約参照) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Aim", meta = (Units = "deg"))
	float AimArmAngle = -75.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Aim")
	float AimElbowShare = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Aim")
	float AimBlendSpeed = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Weapon Switch", meta = (Units = "deg"))
	float WeaponDrawArmAngle = 95.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Weapon Switch", meta = (Units = "deg"))
	float WeaponDrawElbowAngle = 55.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Guard", meta = (Units = "deg"))
	float GuardUpperArmAngle = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Guard", meta = (Units = "deg"))
	float GuardForearmAngle = 95.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Guard")
	float GuardBlendSpeed = 6.0f;

	// One-knee stance from Unity MechAnimator section 22, with user-tuned 90-degree knees.
	// Keep the defaults in sync with FMSProceduralPoseSettings.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (Units = "deg"))
	float CrouchThighAngle = 88.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (Units = "deg"))
	float CrouchKneeAngle = 90.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (Units = "deg"))
	float KneelThighAngle = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (Units = "deg"))
	float KneelKneeAngle = 90.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (Units = "m", ClampMin = "0.0"))
	float CrouchLowerHeight = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch", meta = (ClampMin = "0.0"))
	float CrouchBlendSpeed = 5.0f;

	// Unity MechAnimator section 24 melee/kick tuning.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "s"))
	float MeleeSwingDuration = 0.25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float MeleeSwingAngle = 130.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float MeleeSwingRaiseAngle = 60.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee")
	float HeavySwingDurationMultiplier = 1.3f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee")
	float HeavySwingAngleMultiplier = 1.35f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee")
	int32 HeavyWeaponPowerThreshold = 8000;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "s"))
	float ThrustDuration = 0.22f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float ThrustShoulderAngle = 55.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float ThrustElbowExtendAngle = 70.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "s"))
	float PunchDuration = 0.16f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float PunchShoulderAngle = 25.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float PunchElbowExtendAngle = 85.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee")
	float DashSwingDurationMultiplier = 1.4f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee")
	float DashSwingAngleMultiplier = 1.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float MeleeLungeThighAngle = 32.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float MeleeLungeKneeAngle = 42.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee", meta = (Units = "deg"))
	float MeleeTorsoTwistAngle = 22.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Kick", meta = (Units = "s"))
	float KickDuration = 0.32f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Kick", meta = (Units = "deg"))
	float KickThighAngle = 75.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Procedural|Kick", meta = (Units = "deg"))
	float KickShinExtendAngle = 35.0f;

	// Unity MechAnimator section 25 stagger/airborne knockdown tuning.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Stagger", meta = (Units = "deg"))
	float StaggerLeanAngle = 22.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Stagger", meta = (Units = "deg"))
	float StaggerKneeBendAngle = 25.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Stagger", meta = (Units = "deg"))
	float KnockdownLeanAngle = 60.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Stagger", meta = (Units = "deg"))
	float KnockdownLegKickAngle = 35.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Stagger", meta = (ClampMin = "0.0"))
	float StaggerBlendSpeed = 8.0f;

	// Unity MechAnimator section 26 death kneel tuning. The asymmetric leg angles and
	// blend speeds are shared with the crouch/stagger poses above.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Death", meta = (Units = "deg"))
	float DeathKneelLeanAngle = 12.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Animation|Death", meta = (ClampMin = "0.0"))
	float DeathKneelLowerMultiplier = 1.15f;

	/** Unity defaults to the left kneeling leg and right supporting leg. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Animation|Procedural|Crouch")
	EMSKneelingSide KneelingSide = EMSKneelingSide::Left;

	/** Wrapped walking phase in radians. It intentionally stops while airborne. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "rad"))
	float WalkPhaseAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float WalkIntensity = 0.0f;

	/** Signed sin-wave sample used for opposing left/right limb transforms. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float WalkSwingAlpha = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "deg"))
	float DashLeanAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float AimPoseAlpha = 0.0f;

	/** sin(PI * WeaponSwitchProgress): zero at both ends and one at progress 0.5. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float WeaponSwitchPoseAlpha = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float GuardPoseAlpha = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output")
	float CrouchPoseAlpha = 0.0f;

	/** Root lowering in UE centimeters, already weighted by CrouchPoseAlpha. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "cm"))
	float CrouchGroundOffset = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "deg"))
	float LeftCrouchThighAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "deg"))
	float LeftCrouchKneeAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "deg"))
	float RightCrouchThighAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Output", meta = (Units = "deg"))
	float RightCrouchKneeAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output")
	bool bMeleeMotionActive = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output")
	bool bKickMotionActive = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output")
	EMSMeleeMotionType ActiveMeleeMotionType = EMSMeleeMotionType::Slash;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output")
	float MeleeMotionAlpha = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeShoulderAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeElbowAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeRaiseAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeLungeThighAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeLungeKneeAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Melee|Output", meta = (Units = "deg"))
	float MeleeTorsoTwistAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Kick|Output", meta = (Units = "deg"))
	float KickThighAngleOutput = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Procedural|Kick|Output", meta = (Units = "deg"))
	float KickShinAngleOutput = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Stagger|Output", meta = (Units = "deg"))
	float TorsoLeanPitch = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Stagger|Output", meta = (Units = "deg"))
	float ThighKickAngle = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Stagger|Output", meta = (Units = "deg"))
	float ShinKickAngle = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MS|Animation|Stagger|Output", meta = (Units = "deg"))
	float ArmSlackAngle = 0.0f;

private:
	FMSProceduralPoseInputs MakeSolverInputs() const;
	FMSProceduralPoseSettings MakeSolverSettings() const;
	void CopyPublicOutputsToSolver();
	void CopySolverOutputsToPublic();

	UPROPERTY()
	TObjectPtr<USoundBase> FootstepSound;

	FMSProceduralPoseState SolverState;

	/** 照準刺激の保持時間(UMSMechAnimatorComponent::AimHoldSecondsと同値) */
	float AimHoldSeconds = 1.5f;

	/** 最後に照準刺激を観測したワールド時刻 */
	double LastAimStimulusTime = -1.0e9;
};
