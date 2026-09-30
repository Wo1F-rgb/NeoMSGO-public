// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "GA_Fire_Charge.generated.h"

class UMSRangedWeaponData;

/** Holds on activation, then fires one shared-path hitscan with a 1x..2x charge multiplier on release. */
UCLASS()
class NEOMSGO_API UGA_Fire_Charge : public UGA_Fire_Hitscan
{
	GENERATED_BODY()

public:
	UGA_Fire_Charge();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Begins charging the currently active chargeable hitscan weapon without consuming ammunition. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool StartCharge(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool StartChargeFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

	/** Confirms the current multiplier and fires. Returns false when switching/cancellation invalidated the charge. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool ReleaseCharge();

	/**
	 * リリース(発射)時点の銃口位置・カメラ方向で発射する(実プレイ報告バグ修正:
	 * StartCharge時に保存したOrigin/Directionの使い回しだと、チャージ中に移動・旋回した場合
	 * 「チャージ開始地点」から撃ってしまう)。プレイヤー経路はこちらを使うこと。
	 */
	bool ReleaseChargeFromOrigins(const FVector& TraceOrigin, const FVector& Direction, const FVector& VisualOrigin);

	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	void CancelCharge();

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsCharging() const { return bIsCharging; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	float GetChargeProgress01() const;

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	float GetChargeMultiplier() const;

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

	void ClearChargeState();

	TWeakObjectPtr<AActor> ChargeShooter;
	TWeakObjectPtr<const UMSRangedWeaponData> ChargeWeapon;
	FVector ChargeOrigin = FVector::ZeroVector;
	FVector ChargeVisualOrigin = FVector::ZeroVector;
	FVector ChargeDirection = FVector::ForwardVector;
	double ChargeStartTime = 0.0;
	int32 ChargeSlotIndex = INDEX_NONE;
 int32 PublishedChargeSequence = 0;
	bool bIsCharging = false;
};
