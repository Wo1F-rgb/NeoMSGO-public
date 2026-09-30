// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-2: ブースト消費/回復駆動コンポーネント。
// 設計元: docs/UE5_Architecture.md §1.2 ← Unity: PlayerController.HandleBoostState
//   - 消費: (wantsDash || wantsHover) かつ 非OH・非しゃがみ・非よろけ・残量あり
//           → currentBoost -= boostConsumptionRate × Δt
//   - 通常回復: 非消費・接地・待ち終了後に機体容量/charge比に従って回復
//   - しゃがみ/よろけ中は消費停止・回復は継続(#131バグ修正の踏襲)
//
// 実装判断: P2-6 UMSDownPointRegenComponent と同じ「タイマー+Instant GE」パターン
// (§1.2のPeriodic GEからの変更理由はMSBoostGameplayEffects.h冒頭コメント参照)。
// 消費条件の判定は UMSCharacterMovementComponent::IsConsumingBoost(P3-1)へ委譲する
// (入力意図・しゃがみ/よろけ/OHタグ・残量チェックを含む)。加えてCMCの
// BoostAvailableDelegate へ残量問い合わせ(Boost>0)をバインドする責務を持つ。
// AMSUnitBase がデフォルトサブオブジェクトとして搭載する(プレイヤー/BOT共通。B-10)。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/MSBoostMovementPrediction.h"
#include "MSBoostComponent.generated.h"

class UAbilitySystemComponent;
class UMSAttributeSet_Boost;
class UMSCharacterMovementComponent;
class AController;
struct FMSBoostMovementState;

/**
 * ブースト消費/回復駆動(Unity: PlayerController.HandleBoostState)。
 * ネットワーク人間はCMCの受理済み移動dt、Standalone/BotはBoostStepIntervalで駆動。
 * AuthorityがGEを適用し、所有clientの移動予測は別の作業状態をCMCで積分する。
 * simulated proxyは複製されたGAS状態を表示し、ローカルGEを重ねない:
 *  - CMC(UMSCharacterMovementComponent)がIsConsumingBoost()なら GE_BoostConsume(-rate×Δt)
 *  - 非消費中: 通常は接地後の待ちを経て回復。OHは既存レートで空中も回復する。
 *    (しゃがみ/よろけ中も回復は継続 = Unity #131)
 * OH遷移(0到達)はUMSAttributeSet_Boost::PostGameplayEffectExecuteが担う。
 * CMCを持たないOwnerでは接地を確認できないため通常回復を許可しない。
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSBoostComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSBoostComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/**
	 * 消費/回復レートを機体データから設定する(AMSUnitBase::InitializeUnit経由の単一入口)。
	 * Unity: boostConsumptionRate / boostRechargeRate(機体プリセット依存)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Boost")
	void SetRates(float InConsumptionPerSecond, float InRechargePerSecond);

	UFUNCTION(BlueprintCallable, Category = "MS|Boost")
	void SetModuleConsumptionMultiplier(float InMultiplier)
	{
		ModuleConsumptionMultiplier = FMath::Max(0.0f, InMultiplier);
	}

	float GetModuleConsumptionMultiplier() const { return ModuleConsumptionMultiplier; }
	float GetEffectiveConsumptionPerSecond() const;
	float GetRechargePerSecond() const { return RechargePerSecond; }
	float GetEffectiveRechargePerSecond() const;
	/** Network human movement owns the clock; standalone/Bots retain the timer. */
	bool UsesMovementClock() const;
	bool ReadMovementState(FMSBoostMovementState& OutState);
	void AdvanceMovementClock(float DeltaSeconds, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd);
	/** Reset at takeoff/landing, including transitions between timer samples. */
	void NotifyGroundingChanged(bool bGrounded);
	void RefreshMovementDriver();

protected:
	/** ブースト消費速度 /s(Unity: boostConsumptionRate=20) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.0"))
	float ConsumptionPerSecond = 20.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Boost")
	float ModuleConsumptionMultiplier = 1.0f;

	/** ブースト回復速度 /s(Unity: boostRechargeRate=15) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.0"))
	float RechargePerSecond = 15.0f;

	/** Provisional landing/consumption-end delay; original exact duration remains unverified. */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.0", Units = "s"))
	float RecoveryDelaySeconds = 0.25f;

	/** Normal fraction/s at the existing standard effective recharge rate (15 x 3 = 45). */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.0"))
	float NormalRecoveryFractionPerSecond = 0.7f;

	/** 駆動周期(秒)。DP回復(P2-6)と同じ0.1s粒度で十分(容量100・20/s消費=2/step) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.01", Units = "s"))
	float BoostStepInterval = 0.1f;

private:
	/** タイマー駆動の消費/回復ステップ */
	void BoostStep();
	void ApplyPeriodicStep(float DeltaSeconds, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd);
	FMSBoostMovementState RecoveryState;

	/**
	 * OwnerのASC/BoostSetを解決してキャッシュする(未解決ならfalse)。
	 * 解決時、OwnerのCMCがUMSCharacterMovementComponentならBoostAvailableDelegateをバインドする。
	 * (P2-6と同様、テスト・BP構成を考慮したステップごとの遅延解決)
	 */
	bool ResolveOwnerAbilitySystem();

	/** CMCのBoostAvailableDelegate用: 残量あり=Boost>0(Unity: IsDashingの「残量>0」条件) */
	bool HasBoostRemaining() const;

	/** 消費/回復ステップタイマー */
	FTimerHandle BoostTimerHandle;

	/** 前回ステップのワールド時刻(Δt算出用) */
	double LastStepTime = 0.0;

	/** キャッシュ済みASC */
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;

	/** キャッシュ済みBoostSet */
	TWeakObjectPtr<const UMSAttributeSet_Boost> CachedBoostSet;

	/** キャッシュ済みCMC(UMSCharacterMovementComponent搭載Ownerのみ有効) */
	TWeakObjectPtr<UMSCharacterMovementComponent> CachedMoveComp;
	TWeakObjectPtr<AController> LastMovementController;
	uint32 InitializationGeneration = 1;
	ENetRole LastMovementRole = ROLE_None;
	bool bMovementDriverKnown = false;
	bool bLastUsedMovementClock = false;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMSBoostMovementClockTest;
	friend class FMSBoostRecoveryTest;
	friend class FMSBoostMovementReplayTest;
	friend class FMSBoostMovementResponseTest;
	TOptional<bool> MovementClockOverrideForTest;
#endif
};
