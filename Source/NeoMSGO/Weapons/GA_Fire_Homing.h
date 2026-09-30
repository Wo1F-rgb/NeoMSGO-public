// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Fire_Homing.generated.h"

class UMSHomingWeaponData;

/**
 * Fires a visual missile which turns toward a locked or aim-cone target.
 *
 * ユーザーレビュー#68: 連装(多連装ミサイル等)武器は「1クリック(1トリガー)で
 * マガジン内の全弾が発射間隔ごとに自動連続発射される」仕様。押しっぱなしは不要。
 * 初弾を同期発射した後、残弾をタイマーで自動発射する(GA_Fire_Burstのタイマー機構を踏襲)。
 * 発射中の武器切替・死亡・リロード開始で中断する。
 */
UCLASS()
class NEOMSGO_API UGA_Fire_Homing : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Fire_Homing();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool FireHoming(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool FireHomingFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

	/** True while the one-trigger full-magazine salvo timer is still firing rounds. */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsSalvoActive() const { return bSalvoActive; }
	void CancelSalvo();

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetSalvoRoundsFired() const { return SalvoRoundsFired; }

	/** BurstInterval未設定(<=0)の連装武器が使う既定の連続発射間隔 秒 */
	static constexpr float DefaultSalvoIntervalSeconds = 0.15f;

protected:
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	AActor* SelectHomingTarget(AActor* Shooter, const FVector& Origin,
		const FVector& Direction, float RangeCm) const;

	/** 1発分の発射処理(Cue実行+誘導弾スポーン)。弾消費は呼び出し側の責務。 */
	bool LaunchHomingRound(AActor* Shooter, const UMSHomingWeaponData* Weapon,
		const FVector& TraceOrigin, const FVector& Direction, const FVector& VisualOrigin,
		AActor* HomingTarget) const;

	void FireNextSalvoRound();
	void FinishSalvo();

	TWeakObjectPtr<AActor> SalvoShooter;
	TWeakObjectPtr<const UMSHomingWeaponData> SalvoWeapon;
	/** Target captured by the first round; later rounds do not re-evaluate the HUD capture circle. */
	TWeakObjectPtr<AActor> SalvoTarget;
	FVector SalvoOrigin = FVector::ZeroVector;
	FVector SalvoVisualOrigin = FVector::ZeroVector;
	FVector SalvoDirection = FVector::ForwardVector;
	/** 開始時の機体Transform(2発目以降の起点を機体へ追従させる基準。GA_Fire_Burstと同方式) */
	FTransform SalvoStartTransform = FTransform::Identity;
	FTimerHandle SalvoTimer;
	int32 SalvoSlotIndex = INDEX_NONE;
	int32 SalvoRoundsFired = 0;
	bool bSalvoActive = false;
};
