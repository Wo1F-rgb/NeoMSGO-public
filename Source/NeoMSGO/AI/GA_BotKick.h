#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_BotKick.generated.h"

/** Strong BOT-only kick. Damage still flows through UGE_Damage/UMSDamageExecution. */
UCLASS()
class NEOMSGO_API UGA_BotKick : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BotKick();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	static constexpr float KickPower = 350.0f;
	static constexpr float KickCooldownSeconds = 2.8f;
	static constexpr float KickDownPoint = 40.0f;
	static constexpr float KickRadiusCm = 120.0f;

	/** Executes the overlap and applies UGE_Damage once per hostile actor. */
	bool PerformKick(AActor* Attacker, float KickRangeCm);
	int32 GetLastHitCount() const { return LastHitCount; }
	static FVector CalculateKickCenter(const AActor* Attacker, float KickRangeCm);

private:
	int32 LastHitCount = 0;
};
