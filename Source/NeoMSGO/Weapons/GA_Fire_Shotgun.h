// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "GA_Fire_Shotgun.generated.h"

/** Fires all shotgun pellets immediately while consuming one magazine round. */
UCLASS()
class NEOMSGO_API UGA_Fire_Shotgun : public UGA_Fire_Hitscan
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool FireShotgun(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool FireShotgunFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

	/** Deterministic when supplied a seeded stream; exposed for focused automation tests. */
	static FVector MakePelletDirection(const FVector& AimDirection, float SpreadDegrees, FRandomStream& Random);
};
