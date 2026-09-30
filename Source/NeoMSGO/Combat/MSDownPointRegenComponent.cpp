// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSDownPointRegenComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "TimerManager.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Core/MSGameplayTags.h"

UMSDownPointRegenComponent::UMSDownPointRegenComponent()
{
	// タイマー駆動のためTick不要(§1.2「軽量Tick(またはタイマー)」のタイマー側を採用)
	PrimaryComponentTick.bCanEverTick = false;
}

void UMSDownPointRegenComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		LastStepTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(
			RegenTimerHandle, this, &UMSDownPointRegenComponent::RegenStep, RegenStepInterval, /*bLoop=*/true);
	}
}

void UMSDownPointRegenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegenTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

bool UMSDownPointRegenComponent::ResolveOwnerAbilitySystem()
{
	if (CachedASC.IsValid() && CachedVitalSet.IsValid())
	{
		return true;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!ASC)
	{
		return false;
	}

	const UMSAttributeSet_Vital* VitalSet = ASC->GetSet<UMSAttributeSet_Vital>();
	if (!VitalSet)
	{
		return false;
	}

	CachedASC = ASC;
	CachedVitalSet = VitalSet;
	return true;
}

void UMSDownPointRegenComponent::RegenStep()
{
	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Δtはワールド時刻の実差分(タイマー起床間隔の揺らぎ・時間膨張に頑健)
	const double Now = World->GetTimeSeconds();
	const float DeltaTime = static_cast<float>(Now - LastStepTime);
	LastStepTime = Now;

	if (DeltaTime <= 0.0f || RecoveryPerSecond <= 0.0f || !ResolveOwnerAbilitySystem())
	{
		return;
	}

	UAbilitySystemComponent* ASC = CachedASC.Get();
	const UMSAttributeSet_Vital* VitalSet = CachedVitalSet.Get();

	// 条件1: 回復すべきDPがある
	if (VitalSet->GetDownPoint() <= 0.0f)
	{
		return;
	}

	// 条件2: 死亡/よろけ/空中ダウン中は回復しない(Unity: stagger中でなければDP自然減衰)
	FGameplayTagContainer BlockedTags;
	BlockedTags.AddTag(MSTags::State_Dead);
	BlockedTags.AddTag(MSTags::State_Staggered);
	BlockedTags.AddTag(MSTags::State_KnockedDown);
	BlockedTags.AddTag(MSTags::State_GettingUp);
	if (ASC->HasAnyMatchingGameplayTags(BlockedTags))
	{
		return;
	}

	// 条件3: 最終被弾から RecoveryDelay(2.5s)以上無被弾
	if ((Now - VitalSet->GetLastDamageTime()) < RecoveryDelay)
	{
		return;
	}

	// GE_DPRecovery(Instant)で -RecoveryPerSecond×Δt を適用(§1.2/§1.4。下限0クランプはVitalSet側)
	const FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGE_DPRecovery::StaticClass(), 1.0f, Context);
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_DPRecovery,
			-RecoveryPerSecond * (VitalSet->GetMaxDownPoint()/100.f) * DeltaTime);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}
