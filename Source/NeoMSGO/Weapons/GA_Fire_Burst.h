// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "GA_Fire_Burst.generated.h"

class UMSRangedWeaponData;

/** Fires one hitscan immediately, then the remaining burst rounds at BurstInterval. */
UCLASS()
class NEOMSGO_API UGA_Fire_Burst : public UGA_Fire_Hitscan
{
	GENERATED_BODY()

public:
	UGA_Fire_Burst();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Starts a complete trigger-level burst. The first round is fired synchronously. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool StartBurst(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool StartBurstFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetRoundsFired() const { return RoundsFired; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsBurstActive() const { return bBurstActive; }
	/** Cancels only future rounds; already resolved damage remains authoritative. */
	void CancelBurst();

protected:
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void FireNextRound();
	void FinishBurst();

	TWeakObjectPtr<AActor> BurstShooter;
	TWeakObjectPtr<const UMSRangedWeaponData> BurstWeapon;
	FVector BurstOrigin = FVector::ZeroVector;
	FVector BurstVisualOrigin = FVector::ZeroVector;
	FVector BurstDirection = FVector::ForwardVector;
	/** 開始時の機体Transform(実プレイ報告バグ対策: 2発目以降の起点を機体へ追従させる基準) */
	FTransform BurstStartTransform = FTransform::Identity;
	FTimerHandle BurstTimer;
	int32 BurstSlotIndex = INDEX_NONE;
	int32 TargetRoundCount = 0;
	int32 RoundsFired = 0;
	bool bBurstActive = false;
};
