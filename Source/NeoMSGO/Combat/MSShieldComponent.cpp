// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSShieldComponent.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Net/UnrealNetwork.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

namespace
{
	// The shot cooldown alone can reach zero between automatic rounds. Keep the
	// forearm lowered briefly after the last round, including on trigger release.
	constexpr double GuardRecoveryAfterCooldownSeconds = 0.25;
}

UMSShieldComponent::UMSShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMSShieldComponent::BeginPlay()
{
	Super::BeginPlay();
	// The actor's InitializeUnit path normally runs before BeginPlay.  Keeping the
	// component tick active here is enough for late replicated state/tag updates.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SetGuardTag(false);
	}
}

void UMSShieldComponent::InitializeFromMobileSuit(const UMSMobileSuitData* SuitData)
{
	const bool bWasVisualActive = IsShieldVisualActive();
	GuardRecoveryWeapon.Reset();
	GuardRecoveryUntilWorldTime = 0.0;
	ShieldData = SuitData ? SuitData->GetNativeShield() : nullptr;
	bShieldRemovable = !SuitData || SuitData->bShieldRemovable;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!ShieldData)
	{
		bEquipped = false;
		bBroken = false;
		CurrentDurability = 0.0f;
		SetGuardTag(false);
		RefreshVisualIfShieldActivityChanged(bWasVisualActive);
		return;
	}

	bEquipped = bDeckShieldEnabled;
	bBroken = false;
	CurrentDurability = bEquipped ? GetMaxDurability() : 0.0f;
	SetGuardTag(false);
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
}

void UMSShieldComponent::SetDeckShieldEnabled(const bool bEnabled)
{
	const bool bWasVisualActive = IsShieldVisualActive();
	GuardRecoveryWeapon.Reset();
	GuardRecoveryUntilWorldTime = 0.0;
	bDeckShieldEnabled = bEnabled || !bShieldRemovable;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!bDeckShieldEnabled || !ShieldData)
	{
		bEquipped = false;
		bBroken = false;
		CurrentDurability = 0.0f;
		SetGuardTag(false);
		RefreshVisualIfShieldActivityChanged(bWasVisualActive);
		return;
	}

	// A deck equip operation occurs before play. Re-enabling an explicitly removed
	// shield starts its own full durability pool instead of reviving stale damage.
	bEquipped = true;
	bBroken = false;
	CurrentDurability = GetMaxDurability();
	SetGuardTag(false);
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
}

void UMSShieldComponent::ResetForRespawn()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const bool bWasVisualActive = IsShieldVisualActive();
	GuardRecoveryWeapon.Reset();
	GuardRecoveryUntilWorldTime = 0.0;
	bEquipped = bDeckShieldEnabled && ShieldData != nullptr;
	bBroken = false;
	CurrentDurability = bEquipped ? GetMaxDurability() : 0.0f;
	SetGuardTag(false);
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
}

float UMSShieldComponent::GetMaxDurability() const
{
	return ShieldData ? FMath::Max(0.0f, ShieldData->MaxDurability) : 0.0f;
}

float UMSShieldComponent::GetDurabilityRatio() const
{
	const float Max = GetMaxDurability();
	return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(CurrentDurability / Max, 0.0f, 1.0f) : 0.0f;
}

bool UMSShieldComponent::CanProvideGuard() const
{
	if (!IsShieldVisualActive())
	{
		return false;
	}

	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	if (!Unit)
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	const UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	const UMSWeaponDataBase* Active = Weapons
		? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	FGameplayTagContainer DisabledStates;
	DisabledStates.AddTag(MSTags::State_Dead);
	DisabledStates.AddTag(MSTags::State_EmergencyRecall);
	DisabledStates.AddTag(MSTags::State_Staggered);
	DisabledStates.AddTag(MSTags::State_Flinched);
	DisabledStates.AddTag(MSTags::State_GettingUp);
	DisabledStates.AddTag(MSTags::State_KnockedDown);
	DisabledStates.AddTag(MSTags::State_Switching);
	DisabledStates.AddTag(MSTags::State_Charging);
	DisabledStates.AddTag(MSTags::State_Firing_Beam);
	DisabledStates.AddTag(MSTags::State_MeleeRecovery);
	if (ASC->HasAnyMatchingGameplayTags(DisabledStates))
	{
		// A melee weapon may enter charge state by contract, and a data-authorized
		// shield-integrated beam may fire while guarding. Neither exception bypasses
		// dead/stun/down/switch/recovery states.
		// Evaluate the hard states without the two exception states themselves.
		const bool bHardDisabled = ASC->HasMatchingGameplayTag(MSTags::State_Dead)
			|| ASC->HasMatchingGameplayTag(MSTags::State_Flinched)
			|| ASC->HasMatchingGameplayTag(MSTags::State_GettingUp)
			|| ASC->HasMatchingGameplayTag(MSTags::State_EmergencyRecall)
			|| ASC->HasMatchingGameplayTag(MSTags::State_Staggered)
			|| ASC->HasMatchingGameplayTag(MSTags::State_KnockedDown)
			|| ASC->HasMatchingGameplayTag(MSTags::State_Switching)
			|| ASC->HasMatchingGameplayTag(MSTags::State_MeleeRecovery);
		const bool bChargeException = ASC->HasMatchingGameplayTag(MSTags::State_Charging)
			&& Active
			&& (Active->bCanChargeWithNativeShield || Active->IsA<UMSMeleeWeaponData>());
		const bool bBeamException = ASC->HasMatchingGameplayTag(MSTags::State_Firing_Beam)
			&& Active && Active->bCanFireWithNativeShield;
		if (bHardDisabled
			|| (ASC->HasMatchingGameplayTag(MSTags::State_Charging) && !bChargeException)
			|| (ASC->HasMatchingGameplayTag(MSTags::State_Firing_Beam) && !bBeamException))
		{
			return false;
		}
	}

	if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(Unit))
	{
		const UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
		if (Respawn && Respawn->IsOnlineRecallAwaitingDeploy())
		{
			return false;
		}
	}

	if (Active && Active->bDisablesNativeShield)
	{
		return false;
	}
	if (Weapons && Weapons->GetFireCooldownRemaining() > 0.0f
		&& (!Active || !Active->bCanFireWithNativeShield))
	{
		return false;
	}
	if (Active && !Active->bCanFireWithNativeShield)
	{
		// The authoritative trigger timer remains live between shots. Do not put
		// the shield up for one frame whenever fireInterval reaches zero.
		if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(Unit);
			Player && Player->IsAutomaticFireActive())
		{
			return false;
		}
		if (Active == GuardRecoveryWeapon.Get() && GetWorld()
			&& GetWorld()->GetTimeSeconds() < GuardRecoveryUntilWorldTime)
		{
			return false;
		}
	}
	return true;
}

void UMSShieldComponent::NotifyWeaponFired(UMSWeaponDataBase* Weapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsShieldVisualActive()
		|| !Weapon || Weapon->bCanFireWithNativeShield || !GetWorld())
	{
		return;
	}
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSWeaponComponent* Weapons = Unit ? Unit->GetWeaponComponent() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	const double RecoveryEnd = Now + (Weapons ? FMath::Max(0.0f, Weapons->GetFireCooldownRemaining()) : 0.0f)
		+ GuardRecoveryAfterCooldownSeconds;
	GuardRecoveryUntilWorldTime = GuardRecoveryWeapon.Get() == Weapon
		? FMath::Max(GuardRecoveryUntilWorldTime, RecoveryEnd) : RecoveryEnd;
	GuardRecoveryWeapon = Weapon;
	SetGuardTag(false);
}

bool UMSShieldComponent::TryAbsorbDamage(const float IncomingDamage, const bool bExplosive,
	const FVector& AttackerLocation, float& OutArmorDamage, float& OutShieldDamage, bool& OutGuarded, const float ShieldDamageScale)
{
	// A rejected guard must preserve the original Armor packet. DamageExecution
	// passes its live ArmorDamage by reference even when this function returns false.
	OutArmorDamage = FMath::Max(0.0f, IncomingDamage);
	OutShieldDamage = 0.0f;
	OutGuarded = false;
	if (!GetOwner() || !GetOwner()->HasAuthority() || IncomingDamage <= 0.0f || !CanProvideGuard())
	{
		return false;
	}

	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	if (!Unit || !ShieldData)
	{
		return false;
	}
	FVector TargetForward = Unit->GetActorForwardVector();
	TargetForward.Z = 0.0f;
	FVector ToAttacker = AttackerLocation - Unit->GetActorLocation();
	ToAttacker.Z = 0.0f;
	if (!TargetForward.Normalize() || !ToAttacker.Normalize())
	{
		return false;
	}
	const float AngleDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(FVector::DotProduct(TargetForward, ToAttacker), -1.0f, 1.0f)));
	if (AngleDegrees > ShieldData->GuardFrontalAngleDegrees)
	{
		return false;
	}

	const bool bWasVisualActive = IsShieldVisualActive();
	OutGuarded = true;
	OutArmorDamage = 0.0f;
	const float ShieldPacket = FMath::Max(0.f, IncomingDamage) * FMath::Max(0.f, ShieldDamageScale);
 OutShieldDamage = FMath::Min(ShieldPacket, CurrentDurability);
 CurrentDurability = FMath::Max(0.f, CurrentDurability - ShieldPacket);
	if (bExplosive)
	{
		OutArmorDamage = FMath::Max(0.0f, IncomingDamage * ShieldData->ExplosionChipMultiplier);
	}
	if (CurrentDurability <= KINDA_SMALL_NUMBER)
	{
		CurrentDurability = 0.0f;
		bBroken = true;
	}
	SetGuardTag(false);
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
	return true;
}

void UMSShieldComponent::Repair(const float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsShieldEquipped() || Amount <= 0.0f)
	{
		return;
	}
	const bool bWasVisualActive = IsShieldVisualActive();
	CurrentDurability = FMath::Clamp(CurrentDurability + Amount, 0.0f, GetMaxDurability());
	if (bBroken && CurrentDurability >= GetMaxDurability() - KINDA_SMALL_NUMBER)
	{
		bBroken = false;
	}
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
}

void UMSShieldComponent::RepairToFull()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ShieldData || !bDeckShieldEnabled)
	{
		return;
	}
	const bool bWasVisualActive = IsShieldVisualActive();
	bEquipped = true;
	CurrentDurability = GetMaxDurability();
	bBroken = false;
	RefreshVisualIfShieldActivityChanged(bWasVisualActive);
}

void UMSShieldComponent::SetGuardTag(const bool bShouldGuard)
{
	const bool bChanged = bGuarding != bShouldGuard;
	bGuarding = bShouldGuard;
	if (AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()))
	{
		if (UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent())
		{
			ASC->SetLooseGameplayTagCount(MSTags::State_Guarding, bShouldGuard ? 1 : 0);
		}
	}
	if (bChanged && GetOwner() && GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
}

void UMSShieldComponent::RefreshVisualIfShieldActivityChanged(const bool bWasVisualActive)
{
	if (bWasVisualActive != IsShieldVisualActive())
	{
		// RepNotifies run on clients only.  A listen-server pawn must update its
		// forearm prop at the same state transition instead of waiting for a slot
		// switch to trigger RefreshFromWeaponComponent().
		OnRep_ShieldState();
	}
}

void UMSShieldComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner())
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		SetGuardTag(CanProvideGuard());
	}
}

void UMSShieldComponent::OnRep_ShieldState()
{
	if (AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()))
	{
		if (UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent())
		{
			Visual->RefreshFromWeaponComponent();
		}
	}
}

void UMSShieldComponent::OnRep_Guarding()
{
	// The server's decision is authoritative because remote weapon cooldowns and
	// state tags are not guaranteed to be locally complete.
	SetGuardTag(bGuarding);
}

void UMSShieldComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMSShieldComponent, CurrentDurability);
	DOREPLIFETIME(UMSShieldComponent, bBroken);
	DOREPLIFETIME(UMSShieldComponent, bEquipped);
	DOREPLIFETIME(UMSShieldComponent, ShieldData);
	DOREPLIFETIME(UMSShieldComponent, bGuarding);
}
