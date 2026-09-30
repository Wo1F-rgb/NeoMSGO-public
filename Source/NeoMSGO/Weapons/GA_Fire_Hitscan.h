// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Fire_Hitscan.generated.h"

struct FMSChargeShotMultipliers;

struct FMSHitscanResult;

/** Standard ranged-fire ability. Dispatches instant shots or delayed visual projectiles from weapon data. */
UCLASS()
class NEOMSGO_API UGA_Fire_Hitscan : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Fire_Hitscan();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Shared entry point for player/BOT callers and automation tests. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool Fire(AActor* Shooter, const FVector& Origin, const FVector& Direction);
	bool FireFromOrigins(AActor* Shooter, const FVector& TraceOrigin, const FVector& Direction,
		const FVector& VisualOrigin);

protected:
	/** Fires one pellet, using the immediate GAS path or AMSVisualProjectile according to Weapon->IsHitscan(). */
	bool ResolveAndApplyPellet(AActor* Shooter, const FVector& Origin, const FVector& Direction,
		const class UMSRangedWeaponData* Weapon, float DamageMultiplier = 1.0f,
		const FVector* VisualOrigin = nullptr, bool bEmitFireCue = true,
		const FMSChargeShotMultipliers* ChargeMultipliers = nullptr) const;

	/**
	 * Resolves one trace and applies an explicit damage/down-point pair through the shared
	 * FF/headshot GAS path. ResolveAndApplyPellet delegates here; tick-based weapons whose
	 * per-application magnitude is not Weapon->Power-derived (e.g. GA_Fire_ContinuousBeam's
	 * BeamDamagePerSecond * BeamTickInterval) call this directly instead of duplicating the
	 * trace/GE-application logic.
	 */
	bool ResolveAndApplyDamage(AActor* Shooter, const FVector& Origin, const FVector& Direction,
		const class UMSRangedWeaponData* Weapon, float Damage, float DownPointAmount,
		const FVector* VisualOrigin = nullptr, bool bEmitFireCue = true) const;
};
