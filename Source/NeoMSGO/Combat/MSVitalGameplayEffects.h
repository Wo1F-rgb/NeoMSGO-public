// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-2/P2-6: Vital関連のネイティブGameplayEffect群。
// 設計元: docs/UE5_Architecture.md §1.4 GE一覧(GE_InitStats / GE_Dead / GE_Stagger /
//         GE_KnockedDown / GE_DPRecovery)、§5.1 命名規約(GE_*)。
// BPアセットでなくネイティブ定義とし、C++コードのみで初期値注入と死亡遷移が完結するようにする
// (AMSUnitBase::InitStatsEffectClass のデフォルトもUGE_InitStatsを指す。BPでの差し替えは可能)。

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MSVitalGameplayEffects.generated.h"

/**
 * GE_InitStats (Instant): MobileSuitDataからAttribute初期値を注入(スポーン/リスポーン時。§1.4)。
 * SetByCaller入力(AMSUnitBase::FillInitStatsSpec が書き込む):
 *  - Data.Init.MaxArmor       → MaxArmor / Armor を Override(全快で開始)
 *  - Data.Init.MaxDownPoint   → MaxDownPoint を Override(DownPointは0リセット)
 *  - Data.Init.BoostCapacity  → MaxBoost / Boost を Override(満タンで開始。P3-2)
 * Modifier順はMax系→現在値の順を厳守(現在値クランプがMaxを参照するため)。
 */
UCLASS()
class NEOMSGO_API UGE_InitStats : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_InitStats();
};

/**
 * GE_Dead (Infinite): State.Dead を付与(§1.4)。
 * 適用はUMSAttributeSet_Vital::HandleDeath(Armor 0到達の生存→死亡遷移時)のみ。
 * 除去はリスポーン経路 AMSUnitBase::InitializeUnit(RemoveActiveEffectsWithGrantedTags(State.Dead))。
 */
UCLASS()
class NEOMSGO_API UGE_RepairPod : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UGE_RepairPod();
};

UCLASS()
class NEOMSGO_API UGE_Dead : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Dead();
};

/**
 * GE_Stagger (Duration 1.2s): State.Staggered を付与(§1.4)。
 * 適用はUMSAttributeSet_Vital::HandleDownPointThreshold(DP閾値到達かつ接地時)のみ。
 * よろけ中のAbility Block/Cancelは§1.3マトリクス(各GA側のActivationBlockedTags等)が参照する。
 * よろけ中のDP追加蓄積0はUMSDamageExecution(P2-3)側で担保済み。
 */
UCLASS()
class NEOMSGO_API UGE_Stagger : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Stagger();

	/** よろけ持続時間(Unity: Health.staggerDuration = 1.2f) */
	static constexpr float StaggerDuration = 1.2f;
};

/**
 * GE_KnockedDown (Duration 2.5s): State.KnockedDown を付与(§1.4)。
 * 適用はUMSAttributeSet_Vital::HandleDownPointThreshold(DP閾値到達かつ非接地時)のみ。
 * Block/Cancel対象はGE_Staggerと同一(§1.3マトリクス「同上」)。
 */
UCLASS()
class NEOMSGO_API UGE_KnockedDown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_KnockedDown();

	/** 空中ダウン持続時間(Unity: Health.knockdownDuration = 2.5f。よろけより長い) */
	static constexpr float KnockdownDuration = 2.5f;
};

/**
 * GE_DPRecovery (Instant): DP自然回復の減算(§1.2/§1.4)。
 * SetByCaller入力: Data.DPRecovery = -RecoveryPerSecond×Δt(負値。UMSDownPointRegenComponentが書き込む)。
 * 「最終被弾から2.5s無被弾で開始」の適用条件判定は UMSDownPointRegenComponent 側の責務
 * (Periodic GEのみでは被弾でリセットされる開始遅延を表現できないための実装判断。§1.2)。
 */
UCLASS()
class NEOMSGO_API UGE_DPRecovery : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_DPRecovery();
};
