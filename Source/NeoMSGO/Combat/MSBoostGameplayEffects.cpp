// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSBoostGameplayEffects.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Core/MSGameplayTags.h"

namespace
{
	/** SetByCaller Additive Modifierの組み立てヘルパ(MSVitalGameplayEffects.cppと同パターン) */
	FGameplayModifierInfo MakeSetByCallerAdditive(const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		return Modifier;
	}
}

UGE_BoostConsume::UGE_BoostConsume()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Boost += Data.BoostConsume(負値)。下限0クランプ・0到達OHはBoostSet側。
	Modifiers.Add(MakeSetByCallerAdditive(UMSAttributeSet_Boost::GetBoostAttribute(), MSTags::Data_BoostConsume));
}

UGE_BoostRegen::UGE_BoostRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Boost += Data.BoostRegen(正値)。上限MaxBoostクランプはBoostSet側。
	Modifiers.Add(MakeSetByCallerAdditive(UMSAttributeSet_Boost::GetBoostAttribute(), MSTags::Data_BoostRegen));
}

UGE_Overheat::UGE_Overheat()
{
	// OHペナルティ(Unity: boostOverheatPenalty。機体別のためSetByCaller Duration)
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat DurationSetByCaller;
	DurationSetByCaller.DataTag = MSTags::Data_OverheatDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(DurationSetByCaller);

	// State.Overheated をターゲット(=自分自身)へ付与。
	// GEComponents登録はUGE_Deadの注記通り CreateDefaultSubobject 直接経路
	// (FindOrAddComponentはUObjectコンストラクタ内で使用不可)。
	FInheritedTagContainer TagChanges;
	TagChanges.AddTag(MSTags::State_Overheated);

	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(TagChanges);
	GEComponents.Add(TargetTags);
}
