// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSAbilitySystemGlobals.h"

#include "Combat/Cues/MSGameplayCue_Beam.h"
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Combat/Cues/MSGameplayCue_Fire.h"
#include "Combat/Cues/MSGameplayCue_Impact.h"
#include "Combat/Cues/MSGameplayCue_KillConfirm.h"
#include "Combat/Cues/MSGameplayCue_Tracer.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"

FGameplayEffectContext* UMSAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FMSGameplayEffectContext();
}

UGameplayCueManager* UMSAbilitySystemGlobals::GetGameplayCueManager()
{
	UGameplayCueManager* Manager = Super::GetGameplayCueManager();
	if (Manager && !bNativeGameplayCuesRegistered)
	{
		if (UGameplayCueSet* RuntimeCueSet = Manager->GetRuntimeCueSet())
		{
			// AddCues can re-enter this getter. Guard the operation, but roll back if the
			// runtime set was not ready so a later real-play call can retry registration.
			bNativeGameplayCuesRegistered = true;
			// ネイティブC++のGameplayCueNotifyクラスは、GameplayCueManagerの
			// アセットレジストリ走査(Blueprint GeneratedClassタグ)では発見されないため、
			// タグ→クラスの対応をここで明示登録する(ヘッダのGetGameplayCueManagerコメント参照)。
			TArray<FGameplayCueReferencePair> NativeCues;
			NativeCues.Emplace(MSTags::Cue_Weapon_Fire, FSoftObjectPath(UMSGameplayCue_Fire::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Weapon_Tracer, FSoftObjectPath(UMSGameplayCue_Tracer::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Weapon_BeamLoop, FSoftObjectPath(AMSGameplayCue_Beam::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Impact_Normal, FSoftObjectPath(UMSGameplayCue_Impact_Normal::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Impact_Headshot, FSoftObjectPath(UMSGameplayCue_Impact_Headshot::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Explosion, FSoftObjectPath(UMSGameplayCue_Explosion::StaticClass()));
			NativeCues.Emplace(MSTags::Cue_Kill_Confirm, FSoftObjectPath(UMSGameplayCue_KillConfirm::StaticClass()));
			RuntimeCueSet->AddCues(NativeCues);
		}
	}
	return Manager;
}
