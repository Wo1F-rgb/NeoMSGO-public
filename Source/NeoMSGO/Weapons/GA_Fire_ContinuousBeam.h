// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-6: 照射型ビーム(ContinuousBeam「ゲロビ」)。
// 設計元: docs/UE5_MIGRATION_PLAN.md P4-6 / ガードレールB-6、docs/ArchitectureMap.md §4-6
//         ← Unity: WeaponController.HandleContinuousBeamFire
//
// トリガーを押している間、BeamTickInterval間隔で共通トレース関数
// UMSCombatStatics::ResolveHitscan(専用トレース実装は禁止)を再実行し、
// tick毎に (BeamDamagePerSecond * BeamTickInterval) のダメージと
// (BeamDownPointPerSecond * BeamTickInterval) のダウンポイントを与える。
// トリガーを離すと即座にビームが停止する。

#pragma once

#include "CoreMinimal.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "GA_Fire_ContinuousBeam.generated.h"

class UMSContinuousBeamWeaponData;

/** Holds fire while the trigger is held: retraces + reapplies tick damage/DP every BeamTickInterval, stops instantly on release. */
UCLASS()
class NEOMSGO_API UGA_Fire_ContinuousBeam : public UGA_Fire_Hitscan
{
	GENERATED_BODY()

public:
	UGA_Fire_ContinuousBeam();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Starts the beam for the currently active ContinuousBeam slot. Fires the first tick synchronously. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool StartBeam(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool StartBeamFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

	/** Immediately stops the beam and clears its tick timer (trigger release / weapon switch / cancel). */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	void StopBeam();

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsBeamActive() const { return bBeamActive; }

	/** Number of damage ticks applied since StartBeam. */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetTickCount() const { return TickCount; }

	/** Weapon slot the active beam was started from (INDEX_NONE when inactive). */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetBeamSlotIndex() const { return BeamSlotIndex; }

	/**
	 * Forward-offset point (1.2m) used as the beam's visual start and as the trace origin
	 * (ArchitectureMap.md 4-6: "見た目のビーム始点は aimOrigin + direction*1.2f"). No visual
	 * asset is spawned or referenced by this ability (P4-6 AC4) — only the position is computed.
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	static FVector GetBeamVisualOrigin(const FVector& Origin, const FVector& Direction);

protected:
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	UFUNCTION()
	void OnWeaponSwitched(int32 NewSlotIndex);

	void FireTick();

	TWeakObjectPtr<AActor> BeamShooter;
	TWeakObjectPtr<const UMSContinuousBeamWeaponData> BeamWeapon;
	FVector BeamOrigin = FVector::ZeroVector;
	FVector BeamVisualOrigin = FVector::ZeroVector;
	FVector BeamDirection = FVector::ForwardVector;
	/** 開始時の機体Transform(照射中の起点を機体・カメラへ追従させる基準。実プレイ報告バグ対策) */
	FTransform BeamStartTransform = FTransform::Identity;
	FTimerHandle BeamTickTimer;
	int32 BeamSlotIndex = INDEX_NONE;
	int32 TickCount = 0;
	bool bBeamActive = false;

	/** Beam visual/trace-origin forward offset (Unity: aimOrigin + direction*1.2f = 120cm). */
	static constexpr float BeamVisualForwardOffsetCm = 120.0f;
};
