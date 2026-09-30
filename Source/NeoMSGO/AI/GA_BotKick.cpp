#include "AI/GA_BotKick.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/MSBotCharacter.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

UGA_BotKick::UGA_BotKick()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_BotKick::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AMSBotCharacter* Bot = ActorInfo ? Cast<AMSBotCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const bool bKicked = Bot && Bot->GetEnemyTier() == EMSEnemyTier::Strong
		&& PerformKick(Bot, Bot->GetAIConfig().KickRange);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bKicked);
}

FVector UGA_BotKick::CalculateKickCenter(const AActor* Attacker, const float KickRangeCm)
{
	return Attacker ? Attacker->GetActorLocation() + Attacker->GetActorForwardVector() * (KickRangeCm * 0.5f)
		: FVector::ZeroVector;
}

bool UGA_BotKick::PerformKick(AActor* Attacker, const float KickRangeCm)
{
	AMSBotCharacter* Bot = Cast<AMSBotCharacter>(Attacker);
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!Bot || Bot->GetEnemyTier() != EMSEnemyTier::Strong || !World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotKickOverlap), false);
	QueryParams.AddIgnoredActor(Attacker);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, CalculateKickCenter(Attacker, KickRangeCm), FQuat::Identity,
		MSCollision::WeaponTraceChannel, FCollisionShape::MakeSphere(KickRadiusCm), QueryParams);

	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Attacker, Attacker);
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
	{
		MSContext->SetAttackerLocation(Attacker->GetActorLocation());
	}

	TSet<AActor*> AppliedActors;
	LastHitCount = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target || AppliedActors.Contains(Target) || !UMSTeamStatics::CanDamage(Attacker, Target)) continue;
		AppliedActors.Add(Target);
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!TargetASC) continue;

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, KickPower);
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, KickDownPoint);
		SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
		++LastHitCount;
	}
	return true;
}
