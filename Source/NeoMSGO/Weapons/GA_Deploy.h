// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Deploy.generated.h"

/** Deploys the active supply weapon on the ground three metres in front of the avatar. */
UCLASS()
class NEOMSGO_API UGA_Deploy : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Deploy();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Shared entry point for player/BOT callers and automation tests. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool DeployItem(AActor* Deployer, const FVector& Origin, const FVector& ForwardDirection);

	/** Unity DeployItem searches for ground at a point three metres ahead. */
	static constexpr float DeployDistanceCm = 300.0f;
	/** Long enough for elevated terrain while remaining a strictly downward trace. */
	static constexpr float GroundTraceDepthCm = 10000.0f;
};
