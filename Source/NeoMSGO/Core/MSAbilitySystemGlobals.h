// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-3: AbilitySystemGlobals差し替え。
// 全経路の EffectContext を FMSGameplayEffectContext(bIsHeadshot/AttackerLocation拡張)にする。
// 登録: Config/DefaultGame.ini [/Script/GameplayAbilities.AbilitySystemGlobals]
//       AbilitySystemGlobalsClassName=/Script/NeoMSGO.MSAbilitySystemGlobals

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"
#include "MSAbilitySystemGlobals.generated.h"

UCLASS(Config = Game)
class NEOMSGO_API UMSAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	//~ Begin UAbilitySystemGlobals Interface
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;

	/**
	 * ネイティブC++のGameplayCueNotifyクラス登録込みでCueManagerを返す。
	 * GameplayCueManagerのアセットレジストリ走査はBlueprint GeneratedClassタグのみを対象と
	 * するため、/Script/NeoMSGO のネイティブCueクラス(MSGameplayCue_Tracer等)は自動発見
	 * されない。未登録だとExecuteGameplayCueがルーティングされても処理するNotifyが存在せず、
	 * トレーサー/マズルフラッシュ/着弾/爆発などの武器VFX・SEが一切表示されない
	 * (実プレイ報告バグ「弾頭が全く表示されない」の根本原因)。
	 */
	virtual UGameplayCueManager* GetGameplayCueManager() override;
	//~ End UAbilitySystemGlobals Interface

private:
	/** ネイティブGameplayCueNotifyクラスのランタイムCueSetへの登録済みフラグ(初回のみ実行) */
	bool bNativeGameplayCuesRegistered = false;
};
