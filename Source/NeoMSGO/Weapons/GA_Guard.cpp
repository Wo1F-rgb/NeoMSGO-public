// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Guard.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Core/MSGameplayTags.h"

UGA_Guard::UGA_Guard()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Guard::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* GuardOwner = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bToggled = ToggleGuard(GuardOwner);

	// This ability represents a button toggle, not the lifetime of the guard state. Ending it
	// immediately permits the next press to activate the same spec and toggle the tag back off.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bToggled);
}

bool UGA_Guard::ToggleGuard(AActor* GuardOwner)
{
	// Guard is a machine-native, automatic frontal state. Keeping this entry
	// inert prevents a client from manufacturing State.Guarding by pressing a
	// legacy three-slot shield route or invoking GA_Guard directly.
	return false;
}
