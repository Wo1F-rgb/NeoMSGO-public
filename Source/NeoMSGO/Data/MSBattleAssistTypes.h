#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "MSBattleAssistTypes.generated.h"

UENUM(BlueprintType)
enum class EMSModuleCategory : uint8 { Attack, Defense, Movement, Special, Assist };
UENUM(BlueprintType)
enum class EMSModuleEvidence : uint8 { Confirmed, Historical, Estimated, Unknown };
UENUM(BlueprintType)
enum class EMSModuleSupport : uint8 { Implemented, RequiresFoundation, UnresolvedValue };

/** Values retain source units. Consumers convert percent values (e.g. 10) once. */
UENUM(BlueprintType)
enum class EMSBattleAssistEffectKind : uint8
{
	BlastRadiusIncrease, RecoilReduction, DashMeleeBoostReduction, MissileLockTimeReduction,
	MissileLockConeIncrease, MeleeHitRecoveryReduction, WeaponChargeTimeReduction,
	MovingAccuracyPenaltyRecovery, HeadshotDamageReduction, BackshotDamageReduction,
	BallisticDamageReduction, BeamDamageReduction, ExplosiveDamageReduction, UnderwaterDamageReduction,
	MeleeDamageReduction, IncomingLockDirection, IncomingLockDirectionDistance, IncomingMissilePrelockWarning,
	SpaceInaptitudeMitigation, GroundInaptitudeMitigation, WaterInaptitudeSpeedRatio, DashBoostEfficiency,
	JumpBoostReduction, KnockdownRecoveryReduction, WalkSpeedIncrease, SpaceFallMitigation,
	LandingRecoveryReduction, DisabledBodyDamageReduction, SpotTimeReduction, EmplacedWeaponReloadReduction,
	PickupDeployMotionSpeedIncrease, ThrowMotionSpeedIncrease, StatusDurationReductionBurn,
	RepairTorchShieldEnable, StatusDurationReductionSlow, StatusDurationReductionStun,
	StatusDurationReductionJam, StatusDurationReductionFlash, ShieldDamageIncrease,
	SmallInstallationDamageIncrease, ThrownProjectileSpeedIncrease, DestructibleObstacleDamageIncrease,
	FuseExtension, ReceivedShieldRepairIncrease, MortarLandingMinimap, StaggerGaugeIncrease,
	OverheatRecoveryReduction, RecallInterruptRequiresStagger, KnockdownGaugeIncrease,
	WeaponSwitchTimeReduction, ReloadTimeReduction, ReceivedRepairIncrease, AmmoResupplyRateIncrease,
	CameraGunRevealExtension, ReserveAmmoIncrease
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistLoadout
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FPrimaryAssetId> ModuleIds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FPrimaryAssetId> ACModuleIds;
	bool IsEmpty() const { return ModuleIds.IsEmpty() && ACModuleIds.IsEmpty(); }
	bool operator==(const FMSBattleAssistLoadout& Other) const { return ModuleIds == Other.ModuleIds && ACModuleIds == Other.ACModuleIds; }
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistProgress
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FPrimaryAssetId> DevelopedModuleIds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FName, int32> Materials;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int64 GP = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int64 Revision = 0;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistEffect
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMSBattleAssistEffectKind Kind = EMSBattleAssistEffectKind::ReloadTimeReduction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Value = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Unit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName StackGroup;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistValidationResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) FName Code;
	UPROPERTY(BlueprintReadOnly) FPrimaryAssetId OffendingModuleId;
	UPROPERTY(BlueprintReadOnly) FText Reason;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistResolvedLoadout
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 UsedSlots = 0;
	UPROPERTY(BlueprintReadOnly) int32 UsedACSlots = 0;
	UPROPERTY(BlueprintReadOnly) float EffectiveReuseSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly) TArray<FPrimaryAssetId> EffectiveModuleIds;
	UPROPERTY(BlueprintReadOnly) TArray<FMSBattleAssistEffect> Effects;
	UPROPERTY(BlueprintReadOnly) int32 RulesRevision = 1;
};
