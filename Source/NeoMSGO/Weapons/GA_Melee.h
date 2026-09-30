// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-7: 格闘+ダッシュ格闘(D格)アビリティ。
// 設計元: docs/ArchitectureMap.md §4-7 ← Unity: WeaponController.MeleeAttack(weapon, chargeMultiplier)
//
// Unity版仕様の再現:
//  - 通常格闘: radius=1.5m, reach=weapon.range
//  - D格(IsDashing時): radius=2.8m, reach=weapon.range*1.6
//  - damageMultiplier = (isDashMelee ? 3f : 1f) * mobileSuitData.meleeCorrection * chargeMultiplier
//  - DPは格闘補正・チャージ倍率を反映するが、D格のHP威力3倍は掛けない
//  - dashMeleeBoostCost=武器DataAsset指定(既定30。残量不足時は残り全消費+強制OH=UMSCombatStatics::ConsumeBoostOrOverheat。
//    OHしても突進・攻撃自体は実行される)
//  - 突進: 26m/s × 0.35s の直線移動
//  - 攻撃後硬直 0.45s(State.MeleeRecovery。次の攻撃・移動キャンセル不可。
//    移動側のブロックは UMSCharacterMovementComponent::IsIncapacitated が本タグを参照)
//  - しゃがみ中(State.Crouching)は IsDashing=false 相当のためD格不可(通常格闘へ降格)
//
// 責務境界:
//  - 当たり判定は WeaponTraceチャンネルの SphereOverlap(MSCollision::WeaponTraceChannel。B-3)
//  - ダメージ/DPは通常の UGE_Damage(SetByCaller: Data.Damage / Data.DownPoint)経路。
//    FF判定は UMSDamageExecution 手順1が単一実装(B-2。ここでは事前除外しない)

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Melee.generated.h"

class UAbilitySystemComponent;
class UMSMeleeWeaponData;
class UMSWeaponComponent;

/** Melee + dash-melee ability. Hit detection is a WeaponTrace-channel sphere overlap. */
UCLASS()
class NEOMSGO_API UGA_Melee : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Melee();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Shared entry point for player/BOT callers and automation tests.
	 * @param bWantsDashMelee  Caller-side dash intent (player: CMC::IsBoostDashing). Demoted to a
	 *                         normal melee while State.Crouching (Unity: IsDashing=false while crouching).
	 * @param ChargeMultiplier P4-5 charge multiplier hook (1.0 when uncharged).
	 * @return true when the swing executed (even with zero targets in range, and even when the
	 *         dash boost cost forced an overheat — the lunge and the swing still happen).
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool PerformMelee(AActor* Attacker, const FVector& Origin, const FVector& Direction,
		bool bWantsDashMelee, float ChargeMultiplier = 1.0f);

	/**
	 * Cancels only an in-progress standard saber stage or legacy dash lunge.
	 * Releasing the fire button intentionally calls StopCurrentWeaponFire instead,
	 * so an ordinary release does not interrupt a swing.
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	void CancelMelee();

	// ---- Unity版定数(ArchitectureMap.md §4-7) ----

	/** 通常格闘の判定半径 cm(Unity: radius=1.5f) */
	static constexpr float NormalMeleeRadiusCm = 150.0f;
	/** D格の判定半径 cm(Unity: radius=2.8f) */
	static constexpr float DashMeleeRadiusCm = 280.0f;
	/** D格のリーチ倍率(Unity: reach=weapon.range*1.6f。通常格闘は×1.0=weapon.range) */
	static constexpr float DashMeleeReachMultiplier = 1.6f;
	/** D格の威力倍率(Unity: dashMeleeDamageMultiplier=3f) */
	static constexpr float DashMeleeDamageMultiplier = 3.0f;
	/** D格突進速度 cm/s(Unity: dashMeleeLungeSpeed=26f m/s) */
	static constexpr float DashMeleeLungeSpeedCms = 2600.0f * MSGO::MSScale;
	/** D格突進時間 秒(Unity: dashMeleeLungeDuration=0.35f) */
	static constexpr float DashMeleeLungeDurationSeconds = 0.35f;
	/** 攻撃後硬直 秒(Unity: dashMeleeRecoveryTime=0.45f。この間 State.MeleeRecovery) */
	static constexpr float MeleeRecoverySeconds = 0.45f;
	/** 突進の移動刻み 秒(実装定数。26m/s×0.35sの合計移動量は刻み幅に依存しない) */
	static constexpr float LungeTickIntervalSeconds = 0.02f;

	/** 判定半径 cm(通常=150 / D格=280) */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	static float GetMeleeRadiusCm(bool bDashMelee)
	{
		return (bDashMelee ? DashMeleeRadiusCm : NormalMeleeRadiusCm) * MSGO::MSScale;
	}

	/** リーチ cm(通常=weapon.range / D格=weapon.range*1.6。判定球の中心は Origin + Direction*リーチ) */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	static float GetMeleeReachCm(float WeaponRangeMeters, bool bDashMelee)
	{
		return WeaponRangeMeters * 100.0f * MSGO::MSScale
			* (bDashMelee ? DashMeleeReachMultiplier : 1.0f);
	}

	/** Runtime reach follows the weapon's authored range unit; dash motion distance stays independent. */
	static float GetMeleeReachCmForWeapon(const UMSMeleeWeaponData* Weapon, bool bDashMelee);

	/** HP damage multiplier = (isDashMelee ? 3 : 1) * meleeCorrection * chargeMultiplier。DPにD格3倍は掛けない。 */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	static float CalcDamageMultiplier(bool bDashMelee, float MeleeCorrection, float ChargeMultiplier)
	{
		return (bDashMelee ? DashMeleeDamageMultiplier : 1.0f)
			* CalcDownPointMultiplier(MeleeCorrection, ChargeMultiplier);
	}

	/** DPの補正・チャージ倍率。D格のHP威力倍率を含まない。 */
	static float CalcDownPointMultiplier(float MeleeCorrection, float ChargeMultiplier)
	{
		return FMath::Max(0.0f, MeleeCorrection) * FMath::Max(0.0f, ChargeMultiplier);
	}

	/** 直近の攻撃がD格として実行されたか(しゃがみ降格の観測用) */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool WasLastAttackDashMelee() const { return bLastAttackWasDash; }

	/** 直近の攻撃でGE_Damageを適用した対象数(FFでダメージ0になった適用も含む) */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetLastHitCount() const { return LastHitCount; }

	/** モジュール硬直短縮用の有効な敵命中数。FFで拒否される味方へのGE適用は含めない。 */
	int32 GetLastSuccessfulHitCount() const { return LastSuccessfulHitCount; }

	/** 最後の段。GM:0,1/2=D、ガンダム二刀流:10..13/14=D。 */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	int32 GetLastStandardSaberAttackIndex() const { return LastStandardSaberAttackIndex; }

protected:
	/**
	 * SphereOverlap(WeaponTrace)で対象を収集し、アクター単位に1回だけ UGE_Damage を適用する。
	 * GA_Fire_Hitscan::ResolveAndApplyDamage と同じ SetByCaller(Data.Damage/Data.DownPoint)+
	 * Context(Instigator/AttackerLocation)構成。FF判定はExecution側(B-2)。
	 * @return GEを適用した対象数
	 */
	int32 ApplyMeleeHits(AActor* Attacker, const FVector& TraceStart, const FVector& TraceDirection,
		float ReachCm, float RadiusCm,
		float Damage, float DownPointAmount, const UMSMeleeWeaponData* Weapon,
		bool bForceKnockdown, int32& OutSuccessfulHitCount) const;

private:
	friend class FMSStandardSaberImportedBladeTest;
	friend class FMSGundamDualSaberTest;
	friend class FMSRadarDamagePathsTest;
	bool PerformStandardSaberMelee(AActor* Attacker, const FVector& Origin, const FVector& Direction,
		bool bWantsDashMelee, float ChargeMultiplier, UAbilitySystemComponent* ASC,
		UMSWeaponComponent* Weapons, int32 SlotIndex, const UMSMeleeWeaponData* Weapon);
	bool StartStandardSaberStage(int32 AttackIndex);
	void StandardSaberTick();
 float GetStandardSaberStageDuration() const;
	void ProcessStandardSaberInterval(float FromSeconds, float ToSeconds);
	void SampleStandardSaberBlade(float TimeSeconds, FVector& OutStart, FVector& OutEnd, bool bLeft = false) const;
	void ApplyStandardSaberBladeSegment(const FVector& SegmentStart, const FVector& SegmentEnd);
	void FinishStandardSaberStage();
	void ResetStandardSaberCombo(bool bStopMotion, bool bClearRecovery);
	bool IsStandardSaberInterrupted() const;
	void ClearStandardSaberRecovery();
	FVector TransformStandardSaberPoint(AActor* Attacker, const FVector& RigPoint) const;

	void StartLunge(AActor* Attacker, const FVector& Direction);
	void LungeTick();
	void StartRecovery(AActor* Attacker, UAbilitySystemComponent* ASC, float Seconds);
	void EndRecovery();
	void FinishActiveAbilityAfterMelee();

	/** 突進対象(D格発動者) */
	TWeakObjectPtr<AActor> LungeActor;
	FVector LungeDirection = FVector::ForwardVector;
	float LungeElapsedSeconds = 0.0f;
	FTimerHandle LungeTimer;

	/** 硬直タグ(State.MeleeRecovery)の解除先ASC */
	TWeakObjectPtr<UAbilitySystemComponent> RecoveryASC;
	FTimerHandle RecoveryTimer;
	bool bEndingMeleeAbility = false;

	bool bLastAttackWasDash = false;
	int32 LastHitCount = 0;
	int32 LastSuccessfulHitCount = 0;
	int32 LastStandardSaberAttackIndex = INDEX_NONE;

	/** 標準ビーム・サーベルのサーバー側モーション／判定状態。 */
	TWeakObjectPtr<AActor> StandardSaberActor;
	TWeakObjectPtr<UMSWeaponComponent> StandardSaberWeapons;
	TWeakObjectPtr<UAbilitySystemComponent> StandardSaberASC;
	TWeakObjectPtr<UMSMeleeWeaponData> StandardSaberWeapon;
	int32 StandardSaberSlotIndex = INDEX_NONE;
	int32 StandardSaberAttackIndex = INDEX_NONE;
	float StandardSaberStartWorldTime = 0.0f;
	float StandardSaberLastProcessedSeconds = 0.0f;
	float StandardSaberComboDeadlineWorldTime = -1.0f;
	bool bStandardSaberStageActive = false;
	bool bStandardSaberPendingFollowUp = false;
	bool bStandardSaberDash = false;
	float StandardSaberDamage = 0.0f;
	float StandardSaberDownPoint = 0.0f;
	FVector StandardSaberDirection = FVector::ForwardVector;
	FVector StandardSaberFireOrigin = FVector::ZeroVector;
	TSet<AActor*> StandardSaberHitActors;
	FTimerHandle StandardSaberTimer;
};
