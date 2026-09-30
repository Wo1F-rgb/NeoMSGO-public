// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-3: ダメージGameplayEffect群。
// 設計元: docs/UE5_Architecture.md §1.4 GE一覧(GE_Damage / GE_SplashDamage)。
// P2-2のMSVitalGameplayEffects(GE_InitStats/GE_Dead)と同様、ネイティブ定義とする。

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MSDamageGameplayEffects.generated.h"

/**
 * GE_Damage (Instant): Execution=UMSDamageExecution。
 * 全ダメージ経路(即着/可視弾遅延/格闘/照射tick/ミサイル/爆風)がこの1つに収束する(§1.4)。
 *
 * 呼び出し側の契約(P4-2/P4-6/P4-7/P4-10/P4-11が従う):
 *  - SetByCaller Data.Damage    : 必須。最終乗算済みの基礎威力
 *  - SetByCaller Data.DownPoint : 任意(未設定=0)
 *  - Context                    : FMSGameplayEffectContext を使用し、Instigator(発射者)を必ず設定。
 *                                 HS時は SetHeadshot(true)、方向依存処理には SetAttackerLocation()
 */
UCLASS()
class NEOMSGO_API UGE_Damage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Damage();
};

/**
 * GE_SplashDamage (Instant): GE_Damageと同一Execution(§1.4)。
 * 爆風ユーティリティ(UMSCombatStatics::ApplySplashDamage, P2-4)が
 * SetByCaller Data.SplashFalloff(0〜1.0)を追加設定して適用する。
 * Execution側はfalloff未設定=1.0として扱うため実体はGE_Damageと同一だが、
 * 経路の識別(ログ/デバッグ・将来のCue分岐)のためクラスを分けておく(§1.6)。
 */
UCLASS()
class NEOMSGO_API UGE_SplashDamage : public UGE_Damage
{
	GENERATED_BODY()

public:
	UGE_SplashDamage() = default;
};
