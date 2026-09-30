// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSVitalGameplayEffects.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSGameplayTags.h"

namespace
{
	/** SetByCaller Modifierの組み立てヘルパ(GE_InitStats: Override / GE_DPRecovery: Additive) */
	FGameplayModifierInfo MakeSetByCallerModifier(const FGameplayAttribute& Attribute, const FGameplayTag& DataTag, const EGameplayModOp::Type Op)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = Op;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		return Modifier;
	}

	FGameplayModifierInfo MakeSetByCallerOverride(const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		return MakeSetByCallerModifier(Attribute, DataTag, EGameplayModOp::Override);
	}

}

UGE_InitStats::UGE_InitStats()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Max系を先に確定させてから現在値をOverrideする(クランプ整合。ヘッダの順序厳守コメント参照)
	Modifiers.Add(MakeSetByCallerOverride(UMSAttributeSet_Vital::GetMaxArmorAttribute(), MSTags::Data_Init_MaxArmor));
	Modifiers.Add(MakeSetByCallerOverride(UMSAttributeSet_Vital::GetArmorAttribute(), MSTags::Data_Init_MaxArmor));
	Modifiers.Add(MakeSetByCallerOverride(UMSAttributeSet_Vital::GetMaxDownPointAttribute(), MSTags::Data_Init_MaxDownPoint));

	// DownPointは0リセット(Unity: ResetHealth相当。リスポーン時にDP持ち越しなし)
	FGameplayModifierInfo DownPointReset;
	DownPointReset.Attribute = UMSAttributeSet_Vital::GetDownPointAttribute();
	DownPointReset.ModifierOp = EGameplayModOp::Override;
	DownPointReset.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(0.0f));
	Modifiers.Add(DownPointReset);

	// P3-2: Boost=BoostCapacity満タンで開始(Max→現在値の順序厳守はVitalと同じ)。
	// BoostSet未保持のASC(AMSDestructibleTarget等 P7-2)ではGAS側がModifierをスキップする。
	Modifiers.Add(MakeSetByCallerOverride(UMSAttributeSet_Boost::GetMaxBoostAttribute(), MSTags::Data_Init_BoostCapacity));
	Modifiers.Add(MakeSetByCallerOverride(UMSAttributeSet_Boost::GetBoostAttribute(), MSTags::Data_Init_BoostCapacity));
}

UGE_RepairPod::UGE_RepairPod()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UMSAttributeSet_Vital::GetArmorAttribute(), MSTags::Data_RepairPod, EGameplayModOp::Additive));
}

UGE_Dead::UGE_Dead()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	// State.Dead をターゲット(=自分自身)へ付与。Block/Cancelマトリクス(§1.3)は各GA側の
	// ActivationBlockedTags等がこのタグを参照する。
	FInheritedTagContainer TagChanges;
	TagChanges.AddTag(MSTags::State_Dead);

	// 注意: UObjectコンストラクタ内で FindOrAddComponent は使用不可。
	// 内部の AddComponent が無名NewObjectを行うため、CDO構築時に
	// 「NewObject with empty name can't be used to create default subobjects」でFatalになる
	// (P2-3のAutomationテスト実行で顕在化)。ネイティブGEのコンストラクタでは
	// CreateDefaultSubobject + GEComponents 直接登録が正しい経路。
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(TagChanges);
	GEComponents.Add(TargetTags);
}

UGE_Stagger::UGE_Stagger()
{
	// よろけ(接地DP閾値)。1.2秒間 State.Staggered を付与(§1.4)。
	// GEComponents登録はUGE_Deadの注記通り CreateDefaultSubobject 直接経路
	// (FindOrAddComponentはUObjectコンストラクタ内で使用不可)。
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(StaggerDuration));

	FInheritedTagContainer TagChanges;
	TagChanges.AddTag(MSTags::State_Staggered);

	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(TagChanges);
	GEComponents.Add(TargetTags);
}

UGE_KnockedDown::UGE_KnockedDown()
{
	// 空中ダウン(非接地DP閾値)。2.5秒間 State.KnockedDown を付与(§1.4)。
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(KnockdownDuration));

	FInheritedTagContainer TagChanges;
	TagChanges.AddTag(MSTags::State_KnockedDown);

	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(TagChanges);
	GEComponents.Add(TargetTags);
}

UGE_DPRecovery::UGE_DPRecovery()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// DownPoint += Data.DPRecovery(負値)。下限0クランプはVitalSet側(PreAttributeBaseChange)。
	Modifiers.Add(MakeSetByCallerModifier(
		UMSAttributeSet_Vital::GetDownPointAttribute(), MSTags::Data_DPRecovery, EGameplayModOp::Additive));
}
