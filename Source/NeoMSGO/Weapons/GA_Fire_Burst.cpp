// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_Burst.h"

#include "Combat/MSCombatStatics.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Weapons/MSWeaponComponent.h"

UGA_Fire_Burst::UGA_Fire_Burst()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Fire_Burst::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Shooter || !StartBurst(Shooter, Shooter->GetActorLocation(), Shooter->GetActorForwardVector()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

bool UGA_Fire_Burst::StartBurst(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	return StartBurstFromOrigins(Shooter, Origin, Direction, Origin);
}

bool UGA_Fire_Burst::StartBurstFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	if (!Shooter || bBurstActive)
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	const int32 SlotIndex = Weapons ? Weapons->GetActiveSlotIndex() : INDEX_NONE;
	const UMSRangedWeaponData* Weapon = Weapons ? Cast<UMSRangedWeaponData>(Weapons->GetWeaponData(SlotIndex)) : nullptr;
	if (!Weapon || Weapon->BurstCount <= 0 || !Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	BurstShooter = Shooter;
	BurstWeapon = Weapon;
	BurstOrigin = Origin;
	BurstVisualOrigin = VisualOrigin;
	BurstDirection = Direction.GetSafeNormal();
	BurstStartTransform = Shooter->GetActorTransform();
	BurstSlotIndex = SlotIndex;
	TargetRoundCount = FMath::Max(1, Weapon->BurstCount);
	RoundsFired = 1;
	bBurstActive = true;

	ResolveAndApplyPellet(Shooter, BurstOrigin, BurstDirection, Weapon, 1.0f, &BurstVisualOrigin);
	if (TargetRoundCount <= 1 || Weapon->BurstInterval <= 0.0f)
	{
		while (RoundsFired < TargetRoundCount && Weapons->ConsumeAmmoDuringBurst(SlotIndex))
		{
			ResolveAndApplyPellet(Shooter, BurstOrigin, BurstDirection, Weapon, 1.0f, &BurstVisualOrigin);
			++RoundsFired;
		}
		FinishBurst();
		return true;
	}

	// A new trigger cannot overlap this burst even when FireInterval is shorter than its duration.
	Weapons->EnsureFireCooldown(FMath::Max(Weapon->FireInterval, (TargetRoundCount - 1) * Weapon->BurstInterval));
	Shooter->GetWorldTimerManager().SetTimer(BurstTimer, this, &UGA_Fire_Burst::FireNextRound,
		Weapon->BurstInterval, true, Weapon->BurstInterval);
	return true;
}

void UGA_Fire_Burst::FireNextRound()
{
	AActor* Shooter = BurstShooter.Get();
	const UMSRangedWeaponData* Weapon = BurstWeapon.Get();
	UMSWeaponComponent* Weapons = Shooter ? Shooter->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const auto* AbilityOwner = Cast<IAbilitySystemInterface>(Shooter);
	const auto* ASC = AbilityOwner ? AbilityOwner->GetAbilitySystemComponent() : nullptr;
	if (!Shooter || Shooter->IsActorBeingDestroyed() || (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead)) || !Weapon || !Weapons || RoundsFired >= TargetRoundCount ||
		!Weapons->ConsumeAmmoDuringBurst(BurstSlotIndex))
	{
		FinishBurst();
		return;
	}

	// 実プレイ報告バグ修正: バースト中に機体が移動・旋回しても、各弾は「発射時点」の
	// 銃口位置・カメラ方向(BOTは機体追従の相対再適用)から出る。開始時の使い回し禁止。
	FVector TraceOrigin;
	FVector Direction;
	FVector VisualOrigin;
	UMSCombatStatics::ResolveCurrentFireOrigins(Shooter, BurstStartTransform,
		BurstOrigin, BurstDirection, BurstVisualOrigin, TraceOrigin, Direction, VisualOrigin);
	ResolveAndApplyPellet(Shooter, TraceOrigin, Direction, Weapon, 1.0f, &VisualOrigin);
	++RoundsFired;
	if (RoundsFired >= TargetRoundCount)
	{
		FinishBurst();
	}
}

void UGA_Fire_Burst::FinishBurst()
{
	if (AActor* Shooter = BurstShooter.Get())
	{
		Shooter->GetWorldTimerManager().ClearTimer(BurstTimer);
	}
	bBurstActive = false;
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_Fire_Burst::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bBurstActive)
	{
		FinishBurst();
		return;
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Fire_Burst::CancelBurst()
{
 FinishBurst();
}
