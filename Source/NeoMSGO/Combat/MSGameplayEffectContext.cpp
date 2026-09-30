// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSGameplayEffectContext.h"

bool FMSGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	Ar << ShieldDamageScale;
	Ar << bForceKnockdown;
	Ar << bIsHeadshot;
	Ar << bHasAttackerLocation;
	if (bHasAttackerLocation)
	{
		Ar << AttackerLocation;
	}

	bOutSuccess = true;
	return true;
}

const FMSGameplayEffectContext* FMSGameplayEffectContext::Get(const FGameplayEffectContextHandle& Handle)
{
	const FGameplayEffectContext* Context = Handle.Get();
	if (Context && Context->GetScriptStruct()->IsChildOf(FMSGameplayEffectContext::StaticStruct()))
	{
		return static_cast<const FMSGameplayEffectContext*>(Context);
	}
	return nullptr;
}

FMSGameplayEffectContext* FMSGameplayEffectContext::Get(FGameplayEffectContextHandle& Handle)
{
	FGameplayEffectContext* Context = Handle.Get();
	if (Context && Context->GetScriptStruct()->IsChildOf(FMSGameplayEffectContext::StaticStruct()))
	{
		return static_cast<FMSGameplayEffectContext*>(Context);
	}
	return nullptr;
}
