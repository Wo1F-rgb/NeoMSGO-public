// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-2: ブースト関連のネイティブGameplayEffect群。
// 設計元: docs/UE5_Architecture.md §1.2/§1.4(GE_BoostConsume / GE_BoostRegen / GE_Overheat)
//         ← Unity: PlayerController.HandleBoostState / ConsumeBoost
//
// 実装判断(§1.2からの変更点。P2-6 GE_DPRecoveryの前例踏襲):
//  §1.2はInfinite+Periodic GEを示すが、消費/回復の有効条件(ダッシュ/ホバー入力・
//  しゃがみ/よろけ・OH・残量)は毎フレーム変わるためPeriodic GEの付け外し管理が煩雑になる。
//  DP自然回復と同じ「タイマーコンポーネント(UMSBoostComponent)+Instant GE」で駆動し、
//  レートはSetByCallerで毎ステップ渡す(rate×Δt)。GE名・責務は§1.4の一覧に一致させる。

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MSBoostGameplayEffects.generated.h"

/**
 * GE_BoostConsume (Instant): ブースト消費(§1.4)。
 * SetByCaller入力: Data.BoostConsume = -ConsumptionRate×Δt(周期消費)または -コスト(D格即時消費)。負値。
 * 下限0クランプと「0到達=強制OH」遷移はUMSAttributeSet_Boost側が担う。
 */
UCLASS()
class NEOMSGO_API UGE_BoostConsume : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_BoostConsume();
};

/**
 * GE_BoostRegen (Instant): ブースト回復(§1.4)。
 * SetByCaller入力: Data.BoostRegen = +RechargeRate×Δt(正値)。
 * 「消費中・OH中は回復しない」の条件判定はUMSBoostComponent側の責務。
 * 上限MaxBoostクランプはUMSAttributeSet_Boost側。
 */
UCLASS()
class NEOMSGO_API UGE_BoostRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_BoostRegen();
};

/**
 * GE_Overheat (HasDuration): State.Overheated を付与(§1.4)。
 * Duration = SetByCaller Data.OverheatDuration(MobileSuitData.BoostOverheatPenalty。Unity既定3s)。
 * 適用はUMSAttributeSet_Boost::TriggerOverheat(消費で0到達/強制OH API)のみ。
 * OH中のダッシュ/ホバーBlockはUMSCharacterMovementComponent::CanUseBoost、
 * D格BlockはGA_Melee(P4-7)のActivationBlockedTagsがState.Overheatedを参照する。
 * 除去はDuration失効またはリスポーン AMSUnitBase::InitializeUnit。
 */
UCLASS()
class NEOMSGO_API UGE_Overheat : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Overheat();
};
