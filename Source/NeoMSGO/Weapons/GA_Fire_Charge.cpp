// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_Charge.h"

#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Combat/MSUnitBase.h"
#include "Engine/World.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponVisualComponent.h"
#include "Weapons/MSWeaponComponent.h"

namespace
{
	bool IsChargeShooterUnavailable(AActor* Shooter)
	{
		if (!Shooter || Shooter->IsActorBeingDestroyed())
		{
			return true;
		}

		if (const UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
		{
			return ASC->HasMatchingGameplayTag(MSTags::State_Dead);
		}

		return false;
	}

	void GetCurrentChargeOrigins(const AActor* Shooter, FVector& TraceOrigin,
		FVector& Direction, FVector& VisualOrigin)
	{
		TraceOrigin = Shooter->GetActorLocation();
		Direction = Shooter->GetActorForwardVector();
		VisualOrigin = TraceOrigin;

		if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(Shooter))
		{
			const UMSCameraComponent* Camera = Player->GetMSCamera();
			TraceOrigin = Camera ? Camera->GetComponentLocation() : TraceOrigin;
			Direction = MSOnline::IsEnabled() && Shooter->HasAuthority()
				? Player->GetBaseAimRotation().Vector()
				: Camera ? Camera->GetForwardVector().GetSafeNormal() : Direction;
		}
		if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
		{
			if (const UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent())
			{
				VisualOrigin = Visual->GetMuzzleWorldLocation();
			}
		}
	}
}

UGA_Fire_Charge::UGA_Fire_Charge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Fire_Charge::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Shooter)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FVector TraceOrigin, Direction, VisualOrigin;
	GetCurrentChargeOrigins(Shooter, TraceOrigin, Direction, VisualOrigin);
	if (!StartChargeFromOrigins(Shooter, TraceOrigin, Direction, VisualOrigin))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_WaitInputRelease* ReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	ReleaseTask->OnRelease.AddDynamic(this, &UGA_Fire_Charge::OnInputReleased);
	ReleaseTask->ReadyForActivation();
}

bool UGA_Fire_Charge::StartCharge(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	return StartChargeFromOrigins(Shooter, Origin, Direction, Origin);
}

bool UGA_Fire_Charge::StartChargeFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	if (!Shooter || !Shooter->HasAuthority() || bIsCharging || IsChargeShooterUnavailable(Shooter))
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	const int32 SlotIndex = Weapons ? Weapons->GetActiveSlotIndex() : INDEX_NONE;
	const UMSRangedWeaponData* Weapon = Weapons ? Cast<UMSRangedWeaponData>(Weapons->GetWeaponData(SlotIndex)) : nullptr;
	if (!Weapon || !Weapon->bIsChargeable || Weapon->MaxChargeTime <= 0.0f ||
		!Weapons->CanFire(SlotIndex))
	{
		return false;
	}

	ChargeShooter = Shooter;
	ChargeWeapon = Weapon;
	ChargeOrigin = Origin;
	ChargeVisualOrigin = VisualOrigin;
	ChargeDirection = Direction.GetSafeNormal();
	ChargeStartTime = Shooter->GetWorld()->GetTimeSeconds();
	ChargeSlotIndex = SlotIndex;
 const auto* Unit = Cast<AMSUnitBase>(Shooter);
 PublishedChargeSequence = Weapons->BeginChargeSnapshot(SlotIndex, Weapon->MaxChargeTime *
  (Unit ? Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::WeaponChargeTimeReduction) : 1.f));
 if (PublishedChargeSequence == 0) { ClearChargeState(); return false; }
	bIsCharging = true;
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_Charging, 1);
	}
	Weapons->OnWeaponSwitched.AddDynamic(this, &UGA_Fire_Charge::OnWeaponSwitched);
	return true;
}

float UGA_Fire_Charge::GetChargeProgress01() const
{
	const AActor* Shooter = ChargeShooter.Get();
	const UMSRangedWeaponData* Weapon = ChargeWeapon.Get();
	if (!bIsCharging || !Shooter || !Weapon || Weapon->MaxChargeTime <= 0.0f)
	{
		return 0.0f;
	}
 const auto* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
 return Weapons && Weapons->IsChargeSnapshotCurrent(PublishedChargeSequence) ? Weapons->GetChargeProgress01() : 0.f;
}

float UGA_Fire_Charge::GetChargeMultiplier() const
{
	const UMSRangedWeaponData* Weapon = ChargeWeapon.Get();
	return Weapon ? Weapon->EvaluateChargeMultipliers(GetChargeProgress01()).Damage : 1.0f;
}

bool UGA_Fire_Charge::ReleaseChargeFromOrigins(const FVector& TraceOrigin, const FVector& Direction,
	const FVector& VisualOrigin)
{
	if (!bIsCharging)
	{
		return false;
	}
	// リリース時点の実位置/実方向で上書きしてから共通の発射経路へ
	ChargeOrigin = TraceOrigin;
	ChargeVisualOrigin = VisualOrigin;
	ChargeDirection = Direction.GetSafeNormal();
	return ReleaseCharge();
}

bool UGA_Fire_Charge::ReleaseCharge()
{
	AActor* Shooter = ChargeShooter.Get();
	const UMSRangedWeaponData* Weapon = ChargeWeapon.Get();
	UMSWeaponComponent* Weapons = Shooter ? Shooter->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const FMSChargeShotMultipliers Multipliers = Weapon
		? Weapon->EvaluateChargeMultipliers(GetChargeProgress01()) : FMSChargeShotMultipliers();
	if (!bIsCharging || !Shooter || !Weapon || !Weapons || !Weapons->IsChargeSnapshotCurrent(PublishedChargeSequence) ||
		IsChargeShooterUnavailable(Shooter) ||
		Weapons->GetActiveSlotIndex() != ChargeSlotIndex || Weapons->GetWeaponData(ChargeSlotIndex) != Weapon ||
		!Weapons->ConsumeAmmoAndStartCooldown(ChargeSlotIndex))
	{
		ClearChargeState();
		return false;
	}

	ResolveAndApplyPellet(Shooter, ChargeOrigin, ChargeDirection, Weapon,
		Multipliers.Damage, &ChargeVisualOrigin, true, &Multipliers);
	ClearChargeState();
	return true;
}

void UGA_Fire_Charge::CancelCharge()
{
	ClearChargeState();
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UGA_Fire_Charge::OnInputReleased(float TimeHeld)
{
	// GAS経由の経路もリリース時点の位置・向きを再取得する(チャージ中の移動・旋回へ追従)
	bool bFired = false;
	if (const AActor* Shooter = ChargeShooter.Get())
	{
		FVector TraceOrigin, Direction, VisualOrigin;
		GetCurrentChargeOrigins(Shooter, TraceOrigin, Direction, VisualOrigin);
		bFired = ReleaseChargeFromOrigins(TraceOrigin, Direction, VisualOrigin);
	}
	else
	{
		bFired = ReleaseCharge();
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bFired);
}

void UGA_Fire_Charge::OnWeaponSwitched(int32 NewSlotIndex)
{
	CancelCharge();
}

void UGA_Fire_Charge::ClearChargeState()
{
	if (AActor* Shooter = ChargeShooter.Get())
	{
		if (UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>())
		{
   Weapons->EndChargeSnapshot(PublishedChargeSequence);
			Weapons->OnWeaponSwitched.RemoveDynamic(this, &UGA_Fire_Charge::OnWeaponSwitched);
		}
	}
	bIsCharging = false;
	ChargeShooter.Reset();
	ChargeWeapon.Reset();
	ChargeSlotIndex = INDEX_NONE;
 PublishedChargeSequence = 0;
}

void UGA_Fire_Charge::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearChargeState();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
