// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-2: Boost属性セット(ブースト/オーバーヒート)。
// 設計元: docs/UE5_Architecture.md §1.2 UMSAttributeSet_Boost ← Unity: PlayerController.cs ブースト系
//
// | Attribute        | 初期値ソース                                      | 意味 |
// |------------------|--------------------------------------------------|------|
// | Boost / MaxBoost | MobileSuitData.BoostCapacity (GE_InitStats)      | ブーストゲージ |
//
// オーバーヒート規則(Unity: HandleBoostState「currentBoost<=0でisOverheated=true」/
// ConsumeBoost「残量不足なら残り全消費+強制OH」#150):
//  - 消費(負のAdditive)でBoostが0に到達したら、GE_Overheat
//    (Duration=120sフェイルセーフ, State.Overheated付与)を自身へ適用する。
//  - この「0到達=強制OH」を本AttributeSetに一元化することで、周期消費
//    (UMSBoostComponent)とD格の即時消費(UMSCombatStatics::ConsumeBoostOrOverheat)の
//    どちらの経路でも同一の遷移になる(残量不足の全額請求→0クランプ→OH)。
//  - 回復(正のAdditive)ではOHは発生しない。回復GEで80%に到達した時点で
//    State.Overheatedを明示除去する。GE_OverheatのDuration=120sはフェイルセーフ。
//
// 消費/回復レートの駆動はUMSBoostComponent(P2-6 DP回復と同じタイマー+Instant GEパターン)。
// しゃがみ/よろけ中の入力無視はUMSCharacterMovementComponent::IsConsumingBoost(P3-1)が担う。

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MSAttributeSet_Boost.generated.h"

// Attribute標準アクセサ一式(MSAttributeSet_Vital.hと同一定義。トークン一致のため重複定義は無害)
#ifndef MS_ATTRIBUTE_ACCESSORS
#define MS_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)
#endif

/**
 * オーバーヒート開始通知(Unity: isOverheated=true遷移相当)。
 * @param Unit  OH遷移したユニット(AttributeSetのOwningActor)
 * 非OH→OHの遷移時に1回だけ発火する。HUD(P8-1)・アニメ/VFX・AIが購読する。
 * 終了は回復GEで80%到達時のState.Overheated明示除去で観測できる。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMSUnitOverheat, AActor*, Unit);

/**
 * Boost属性セット(Boost/MaxBoost)。AMSUnitBaseが保持する(§1.1。プレイヤー/BOT共通 B-10)。
 * 初期値はGE_InitStats(SetByCaller: Data.Init.BoostCapacity)で注入する(§1.4)。
 */
UCLASS()
class NEOMSGO_API UMSAttributeSet_Boost : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMSAttributeSet_Boost();

	//~ Begin UAttributeSet Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	//~ End UAttributeSet Interface

	/** ブースト残量(Unity: PlayerController.currentBoost。HUDは本属性を直接読む — 計画書P8-1) */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Boost, Category = "MS|Boost")
	FGameplayAttributeData Boost;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Boost, Boost)

	/** ブースト容量(Unity: PlayerController.MaxBoost。MobileSuitData.BoostCapacity由来) */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxBoost, Category = "MS|Boost")
	FGameplayAttributeData MaxBoost;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Boost, MaxBoost)

	/** オーバーヒート開始通知(非OH→OH遷移時のみ1回)。BP/C++両対応 */
	UPROPERTY(BlueprintAssignable, Category = "MS|Boost")
	FOnMSUnitOverheat OnOverheat;

	/**
	 * 強制オーバーヒート(公開API。ダッシュ格闘P4-7の残量不足時等)。
	 * GE_Overheat(Duration=120sフェイルセーフ, State.Overheated)を自身へ適用し、
	 * OnOverheatを発火する。既にState.Overheated保持中は何もしない(多重OHなし)。
	 * 通常はUMSCombatStatics::ConsumeBoostOrOverheat経由(消費とセット)で到達する。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Boost")
	void TriggerOverheat();

	/** OH解除となる回復率(ユーザー指定仕様: 80%回復までブースト使用不可・ゲージ赤) */
	static constexpr float OverheatReleaseRatio = 0.8f;

	UFUNCTION()
	void OnRep_Boost(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxBoost(const FGameplayAttributeData& OldValue);

private:
	/** Boost/MaxBoostの共通クランプ(現在値・基礎値の両経路から呼ぶ) */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
