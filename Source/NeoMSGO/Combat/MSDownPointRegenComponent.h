// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-6: DP自然回復コンポーネント。
// 設計元: docs/UE5_Architecture.md §1.2「DP自然回復」← Unity: Health.cs
//   - downPointRecoveryDelay     = 2.5f (最後の被弾からこの秒数、無被弾で回復開始)
//   - RecoveryPerSecond = 40f (容量の40%/秒。旧100DPでの回復時間を維持する暫定値)
//
// 実装判断(§1.2): Periodic GEのみでは「被弾でリセットされる開始遅延」を表現できないため、
// 被弾タイムスタンプはVitalSet(LastDamageTime)が保持し、本コンポーネントの軽量タイマー
// (FTimerHandle, RegenStepInterval周期)が条件成立時のみ GE_DPRecovery(Instant,
// -RecoveryPerSecond×MaxDownPoint/100×Δt)を適用する。原作後期の時間実測値ではない。
// AMSUnitBase がデフォルトサブオブジェクトとして搭載する(プレイヤー/BOT共通経路。B-10)。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSDownPointRegenComponent.generated.h"

class UAbilitySystemComponent;
class UMSAttributeSet_Vital;

/**
 * DP自然回復(Unity: Health.Update のDP減衰部)。
 * RegenStepInterval周期で以下の全条件を満たす場合のみ GE_DPRecovery を自身へ適用する:
 *  1. DownPoint > 0
 *  2. State.Dead / State.Staggered / State.KnockedDown を保持していない
 *     (Unity: stagger中でなければ回復。死亡中の回復はリスポーンのGE_InitStatsが担う)
 *  3. 最終被弾(VitalSet::LastDamageTime)から RecoveryDelay 秒以上経過
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSDownPointRegenComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSDownPointRegenComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

protected:
	/** 回復開始までの無被弾時間(秒)。Unity: downPointRecoveryDelay = 2.5f */
	UPROPERTY(EditDefaultsOnly, Category = "MS|DownPoint", meta = (ClampMin = "0.0", Units = "s"))
	float RecoveryDelay = 2.5f;

	/** 容量に対する回復率(%/秒)。旧試作と同じ全回復時間を保つ暫定値。 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|DownPoint", meta = (ClampMin = "0.0"))
	float RecoveryPerSecond = 40.0f; // Percent of capacity/second; prototype timing, not original measurement.

	/** 回復判定の駆動周期(秒)。最大100DPを40/sで回復する用途に0.1sで十分な粒度 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|DownPoint", meta = (ClampMin = "0.01", Units = "s"))
	float RegenStepInterval = 0.1f;

private:
	/** タイマー駆動の回復ステップ(条件成立時のみGE_DPRecovery適用) */
	void RegenStep();

	/**
	 * OwnerのASC/VitalSetを解決してキャッシュする(未解決ならfalse)。
	 * AMSUnitBase以外(テスト・BP構成)でも動くよう、IAbilitySystemInterface/コンポーネント検索で辿る。
	 * VitalSetが後からAddSpawnedAttributeされる構成を考慮し、ステップごとの遅延解決とする。
	 */
	bool ResolveOwnerAbilitySystem();

	/** 回復ステップタイマー */
	FTimerHandle RegenTimerHandle;

	/** 前回ステップのワールド時刻(Δt算出用) */
	double LastStepTime = 0.0;

	/** キャッシュ済みASC */
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;

	/** キャッシュ済みVitalSet */
	TWeakObjectPtr<const UMSAttributeSet_Vital> CachedVitalSet;
};
