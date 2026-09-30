// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSModuleGameplayEffects.h"

#include "Core/MSGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace
{
	void ConfigureDuration(UGameplayEffect& Effect, const FGameplayTag& DurationTag)
	{
		Effect.DurationPolicy = EGameplayEffectDurationType::HasDuration;
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DurationTag;
		Effect.DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	}

}

UGE_ModuleEffect::UGE_ModuleEffect()
{
	ConfigureDuration(*this, MSTags::Data_Module_Duration);
	FInheritedTagContainer Changes;
	Changes.AddTag(MSTags::State_Module_EffectActive);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(Changes);
	GEComponents.Add(TargetTags);
}

UGE_ModuleForcedOverheat::UGE_ModuleForcedOverheat()
{
	// State.Overheated intentionally shares the normal recovery path. When boost reaches
	// 80%, MSAttributeSet_Boost removes this effect (and therefore both granted tags).
	// The data-driven 90s duration is only the longer failsafe.
	ConfigureDuration(*this, MSTags::Data_Module_OverheatDuration);
	FInheritedTagContainer Changes;
	Changes.AddTag(MSTags::State_Overheated);
	Changes.AddTag(MSTags::State_Module_ForcedOverheat);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(Changes);
	GEComponents.Add(TargetTags);
}

UGE_ModuleCooldown::UGE_ModuleCooldown()
{
	ConfigureDuration(*this, MSTags::Data_Module_Cooldown);
	FInheritedTagContainer Changes;
	Changes.AddTag(MSTags::State_Module_Cooldown);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	TargetTags->SetAndApplyTargetTagChanges(Changes);
	GEComponents.Add(TargetTags);
}
