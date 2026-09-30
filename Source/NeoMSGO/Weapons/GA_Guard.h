// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Guard.generated.h"

/** Toggles the hand-held shield guard state. Damage mitigation remains in UMSDamageExecution. */
UCLASS()
class NEOMSGO_API UGA_Guard : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Guard();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Shared direct entry point for player/BOT dispatchers and automation tests. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool ToggleGuard(AActor* GuardOwner);
};
