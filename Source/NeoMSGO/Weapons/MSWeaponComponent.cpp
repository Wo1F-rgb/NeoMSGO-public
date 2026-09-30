// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/MSWeaponComponent.h"

#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSHitReactionComponent.h"
#include "Combat/MSShieldComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Native shields (by class or by Shield category) are suit equipment, never a weapon slot. */
	bool IsNativeShieldData(const UMSWeaponDataBase* Weapon)
	{
		return Weapon && (Weapon->IsA<UMSShieldWeaponData>()
			|| Weapon->CategoryTag == MSTags::Weapon_Category_Shield);
	}
}

UMSWeaponComponent::UMSWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	static ConstructorHelpers::FObjectFinder<USoundBase> ReloadSoundFinder(
		TEXT("/Game/Audio/Imported/SFX_Reload_MetalLatch.SFX_Reload_MetalLatch"));
	ReloadSound = ReloadSoundFinder.Object;
}

void UMSWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshStateTags();
}

void UMSWeaponComponent::InitializeFromMobileSuit(const UMSMobileSuitData* MobileSuitData)
{
	if (!HasAuthorityForMutation()) return;
	TArray<UMSWeaponDataBase*> Weapons;
	if (MobileSuitData)
	{
		const int32 Count = FMath::Min(MobileSuitData->EquippedWeapons.Num(), MaxWeaponSlots);
		Weapons.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			UMSWeaponDataBase* Weapon = MobileSuitData->EquippedWeapons[Index].LoadSynchronous();
			// Legacy assets may still contain their shield in EquippedWeapons. Keep
			// the slot number as an empty entry while the native shield component
			// resolves it independently.
			Weapons[Index] = IsNativeShieldData(Weapon) ? nullptr : Weapon;
		}
	}
	SetLoadout(Weapons);
}

void UMSWeaponComponent::InitializeSlots(const TArray<UMSWeaponDataBase*>& Weapons)
{
	SetLoadout(Weapons);
}

bool UMSWeaponComponent::SetLoadout(const TArray<UMSWeaponDataBase*>& Weapons)
{
	if (!HasAuthorityForMutation()) return false;
	ResetChargeSnapshot();
	Slots.Reset(FMath::Min(Weapons.Num(), MaxWeaponSlots));
	for (int32 Index = 0; Index < Weapons.Num() && Index < MaxWeaponSlots; ++Index)
	{
		FMSWeaponSlotState& Slot = Slots.AddDefaulted_GetRef();
		// Final boundary for every caller (deck deploy, bots, fixtures): an unnormalized
		// loadout keeps the slot number but never puts a native shield into weapon state.
		if (IsNativeShieldData(Weapons[Index]))
		{
			UE_LOG(LogTemp, Warning, TEXT("MSWeaponComponent: native shield %s left weapon slot %d empty."),
				*GetNameSafe(Weapons[Index]), Index + 1);
			continue;
		}
		Slot.WeaponData = Weapons[Index];
		if (Weapons[Index])
		{
			Slot.MagazineAmmo = FMath::Max(0, Weapons[Index]->MagazineSize);
			Slot.ReserveAmmo = GetReserveCapacity(Weapons[Index]);
		}
	}
	ActiveSlotIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].WeaponData)
		{
			ActiveSlotIndex = Index;
			break;
		}
	}
	FireCooldownRemaining = SwitchRemaining = SwitchDuration = 0.0f;
	FireCooldownEndWorldTime = 0.0;
	SwitchEndServerTime = 0.0f;
	RefreshStateTags();
	OnWeaponStateReplicated.Broadcast();
	ForceOwnerNetUpdate();
	return true;
}

void UMSWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!HasAuthorityForMutation())
	{
		RefreshStateTags();
		return;
	}
	FireCooldownRemaining = FireCooldownEndWorldTime > 0.0
		? GetFireCooldownRemaining()
		: FMath::Max(0.0f, FireCooldownRemaining - DeltaTime);
	SwitchRemaining = UsesSynchronizedServerClock()
		? GetSwitchRemaining()
		: FMath::Max(0.0f, SwitchRemaining - DeltaTime);
	for (FMSWeaponSlotState& Slot : Slots)
	{
		if (!Slot.WeaponData) continue;
		if (UsesSynchronizedServerClock() && Slot.ReloadEndServerTime > 0.0f)
		{
			Slot.ReloadRemaining = GetReloadRemainingInternal(Slot);
			if (Slot.ReloadRemaining <= 0.0f) CompleteReload(Slot);
		}
		else if (Slot.ReloadRemaining > 0.0f)
		{
			Slot.ReloadRemaining = FMath::Max(0.0f, Slot.ReloadRemaining - DeltaTime);
			if (Slot.ReloadRemaining <= 0.0f) CompleteReload(Slot);
		}
	}
	RefreshStateTags();
}

bool UMSWeaponComponent::SwitchToSlot(int32 SlotIndex)
{
	if (UMSHitReactionComponent::BlocksCombat(ResolveASC())) return false;
	if (!HasAuthorityForMutation()) return false;
	if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].WeaponData || SlotIndex == ActiveSlotIndex) return false;
	// Unity 4-8 contract: changing the held weapon always drops the guard pose.
	if (UAbilitySystemComponent* ASC = ResolveASC())
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_Guarding, 0);
	}
	ResetChargeSnapshot();
	ActiveSlotIndex = SlotIndex;
	// B-4: no fire interval may carry across a switch, and switching never starts reload.
	FireCooldownRemaining = 0.0f;
	FireCooldownEndWorldTime = 0.0;
	SwitchDuration = FMath::Max(0.0f, Slots[SlotIndex].WeaponData->GetEffectiveWeaponSwitchDuration());
	if (const auto* Unit = Cast<AMSUnitBase>(GetOwner())) SwitchDuration *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::WeaponSwitchTimeReduction);
	SwitchRemaining = SwitchDuration;
	SwitchEndServerTime = GetServerWorldTimeSeconds() + SwitchDuration;
	RefreshStateTags();
	OnWeaponSwitched.Broadcast(SlotIndex);
	ForceOwnerNetUpdate();
	return true;
}

bool UMSWeaponComponent::IsSwitching() const
{
	return GetSwitchRemaining() > 0.0f;
}

float UMSWeaponComponent::GetSwitchRemaining() const
{
	if (UsesSynchronizedServerClock() && SwitchEndServerTime > 0.0f)
	{
		return FMath::Max(0.0f, SwitchEndServerTime - GetServerWorldTimeSeconds());
	}
	return FMath::Max(0.0f, SwitchRemaining);
}

UMSWeaponDataBase* UMSWeaponComponent::GetWeaponData(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].WeaponData.Get() : nullptr;
}

float UMSWeaponComponent::GetSwitchProgress01() const
{
	return SwitchDuration <= 0.0f ? 1.0f : 1.0f - FMath::Clamp(GetSwitchRemaining() / SwitchDuration, 0.0f, 1.0f);
}

bool UMSWeaponComponent::CanFire(int32 SlotIndex) const
{
	if (UMSHitReactionComponent::BlocksCombat(ResolveASC())) return false;
	if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].WeaponData || SlotIndex != ActiveSlotIndex || IsSwitching() || GetFireCooldownRemaining() > 0.0f || IsReloading(SlotIndex)) return false;
	const FMSWeaponSlotState& Slot = Slots[SlotIndex];
	return Slot.WeaponData->HasInfiniteAmmo() || Slot.MagazineAmmo > 0;
}

bool UMSWeaponComponent::ConsumeAmmoAndStartCooldown(int32 SlotIndex)
{
	if (!HasAuthorityForMutation()) return false;
	if (!CanFire(SlotIndex)) return false;
	FMSWeaponSlotState& Slot = Slots[SlotIndex];
	if (!Slot.WeaponData->HasInfiniteAmmo()) --Slot.MagazineAmmo;

	// P6-2: BOT弱体化のfireIntervalMultiplier(射撃5倍/格闘2倍)をShooter側から注入する。
	// プレイヤーはAMSUnitBase既定のGetCombatModifiers()=中立値のため挙動は変わらない。
	float IntervalMultiplier = 1.0f;
	if (const AMSUnitBase* Owner = Cast<AMSUnitBase>(GetOwner()))
	{
		const FMSCombatModifiers Modifiers = Owner->GetCombatModifiers();
		IntervalMultiplier = Cast<UMSMeleeWeaponData>(Slot.WeaponData.Get())
			? Modifiers.MeleeFireIntervalMultiplier
			: Modifiers.FireIntervalMultiplier;
	}
	FireCooldownRemaining = FMath::Max(0.0f, Slot.WeaponData->FireInterval) * FMath::Max(0.0f, IntervalMultiplier);
	FireCooldownEndWorldTime = GetWorld() && FireCooldownRemaining > 0.0f
		? static_cast<double>(GetWorld()->GetTimeSeconds()) + FireCooldownRemaining : 0.0;
	if (AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()))
	{
		if (UMSShieldComponent* Shield = Unit->GetShieldComponent()) Shield->NotifyWeaponFired(Slot.WeaponData.Get());
	}
	// A finite magazine starts reloading on the same authoritative consumption path used by every
	// ranged ability (hitscan/shotgun/burst/homing/charge/continuous beam).
	if (!Slot.WeaponData->HasInfiniteAmmo() && Slot.MagazineAmmo <= 0)
	{
		StartReload(SlotIndex);
	}
	ForceOwnerNetUpdate();
	return true;
}

bool UMSWeaponComponent::ConsumeAmmoDuringBurst(int32 SlotIndex)
{
	if (UMSHitReactionComponent::BlocksCombat(ResolveASC())) return false;
	if (!HasAuthorityForMutation()) return false;
	if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].WeaponData || SlotIndex != ActiveSlotIndex || IsSwitching() || IsReloading(SlotIndex))
	{
		return false;
	}

	FMSWeaponSlotState& Slot = Slots[SlotIndex];
	if (!Slot.WeaponData->HasInfiniteAmmo())
	{
		if (Slot.MagazineAmmo <= 0)
		{
			return false;
		}
		--Slot.MagazineAmmo;
		if (Slot.MagazineAmmo <= 0)
		{
			StartReload(SlotIndex);
		}
	}
	if (AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()))
	{
		if (UMSShieldComponent* Shield = Unit->GetShieldComponent()) Shield->NotifyWeaponFired(Slot.WeaponData.Get());
	}
	ForceOwnerNetUpdate();
	return true;
}

void UMSWeaponComponent::EnsureFireCooldown(float MinimumRemaining)
{
	if (!HasAuthorityForMutation()) return;
	FireCooldownRemaining = FMath::Max(GetFireCooldownRemaining(), FMath::Max(0.0f, MinimumRemaining));
	FireCooldownEndWorldTime = GetWorld() && FireCooldownRemaining > 0.0f
		? static_cast<double>(GetWorld()->GetTimeSeconds()) + FireCooldownRemaining : 0.0;
}

float UMSWeaponComponent::GetFireCooldownRemaining() const
{
	if (FireCooldownEndWorldTime > 0.0 && GetWorld())
	{
		const double Remaining = FireCooldownEndWorldTime - static_cast<double>(GetWorld()->GetTimeSeconds());
		// Only discard float clock roundoff, not a missed simulation frame.
		return Remaining <= 0.000001 ? 0.0f : static_cast<float>(Remaining);
	}
	return FireCooldownRemaining;
}

bool UMSWeaponComponent::StartReload(int32 SlotIndex)
{
	if (!HasAuthorityForMutation()) return false;
	if (!Slots.IsValidIndex(SlotIndex)) return false;
	FMSWeaponSlotState& Slot = Slots[SlotIndex];
	if (!Slot.WeaponData || Slot.WeaponData->HasInfiniteAmmo() || IsReloading(SlotIndex) || Slot.ReserveAmmo <= 0 || Slot.MagazineAmmo >= Slot.WeaponData->MagazineSize) return false;
	Slot.ReloadDuration = FMath::Max(Slot.WeaponData->ReloadTime, UE_KINDA_SMALL_NUMBER);
	if (const auto* Unit = Cast<AMSUnitBase>(GetOwner())) Slot.ReloadDuration *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::ReloadTimeReduction);
	Slot.ReloadRemaining = Slot.ReloadDuration;
	Slot.ReloadEndServerTime = GetServerWorldTimeSeconds() + Slot.ReloadDuration;
	if (AActor* Owner = GetOwner())
	{
		MSAudio::SpawnMetaSoundAtLocation(Owner,
			MSAudioPreset::Resolve(EMSAudioUsage::Reload, ReloadSound), Owner->GetActorLocation(), 900.0f);
	}
	RefreshStateTags();
	ForceOwnerNetUpdate();
	return true;
}

bool UMSWeaponComponent::IsReloading(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].WeaponData
		&& GetReloadRemainingInternal(Slots[SlotIndex]) > 0.0f;
}

float UMSWeaponComponent::GetReloadRemaining(const int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].WeaponData
		? GetReloadRemainingInternal(Slots[SlotIndex]) : 0.0f;
}

float UMSWeaponComponent::GetReloadDuration(const int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].WeaponData
		? FMath::Max(0.0f, Slots[SlotIndex].ReloadDuration) : 0.0f;
}

float UMSWeaponComponent::GetReloadEndServerTime(const int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].WeaponData
		? FMath::Max(0.0f, Slots[SlotIndex].ReloadEndServerTime) : 0.0f;
}

float UMSWeaponComponent::GetReloadProgress01(const int32 SlotIndex) const
{
	const float Duration = GetReloadDuration(SlotIndex);
	if (Duration <= UE_KINDA_SMALL_NUMBER || !IsReloading(SlotIndex)) return 0.0f;
	return FMath::Clamp(1.0f - GetReloadRemaining(SlotIndex) / Duration, 0.0f, 1.0f);
}

bool UMSWeaponComponent::IsEmpty(int32 SlotIndex) const
{
	if (!Slots.IsValidIndex(SlotIndex)) return false;
	const FMSWeaponSlotState& Slot = Slots[SlotIndex];
	return !Slot.WeaponData || (!Slot.WeaponData->HasInfiniteAmmo() && Slot.MagazineAmmo <= 0 && Slot.ReserveAmmo <= 0);
}

FText UMSWeaponComponent::GetAmmoDisplay(int32 SlotIndex) const
{
	if (!Slots.IsValidIndex(SlotIndex)) return FText::GetEmpty();
	if (!Slots[SlotIndex].WeaponData) return FText::FromString(TEXT("EMPTY"));
	if (Slots[SlotIndex].WeaponData->HasInfiniteAmmo()) return FText::FromString(TEXT("--"));
	if (IsEmpty(SlotIndex)) return FText::FromString(TEXT("EMPTY"));
	return FText::FromString(FString::Printf(TEXT("%d+%d"), Slots[SlotIndex].MagazineAmmo, Slots[SlotIndex].ReserveAmmo));
}

int32 UMSWeaponComponent::GetReserveCapacity(const UMSWeaponDataBase* Weapon) const
{
 if (!Weapon || Weapon->ReserveAmmo <= 0 || Weapon->HasInfiniteAmmo()) return Weapon ? FMath::Max(0, Weapon->ReserveAmmo) : 0;
 const auto* Unit = Cast<AMSUnitBase>(GetOwner());
 const float Percent = Unit ? Unit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ReserveAmmoIncrease) : 0.f;
 return FMath::CeilToInt(Weapon->ReserveAmmo * (1.f + Percent * .01f) - UE_KINDA_SMALL_NUMBER);
}

int32 UMSWeaponComponent::GetMagazineAmmo(int32 SlotIndex) const { return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].MagazineAmmo : 0; }
int32 UMSWeaponComponent::GetReserveAmmo(int32 SlotIndex) const { return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].ReserveAmmo : 0; }

void UMSWeaponComponent::CompleteReload(FMSWeaponSlotState& Slot)
{
	if (!Slot.WeaponData)
	{
		Slot.ReloadRemaining = Slot.ReloadDuration = Slot.ReloadEndServerTime = 0.0f;
		ForceOwnerNetUpdate();
		return;
	}
	const int32 Needed = FMath::Max(0, Slot.WeaponData->MagazineSize - Slot.MagazineAmmo);
	const int32 Loaded = FMath::Min(Needed, Slot.ReserveAmmo);
	Slot.MagazineAmmo += Loaded;
	Slot.ReserveAmmo -= Loaded;
	Slot.ReloadRemaining = Slot.ReloadDuration = Slot.ReloadEndServerTime = 0.0f;
	ForceOwnerNetUpdate();
}

void UMSWeaponComponent::RefillAllAmmo()
{
	if (!HasAuthorityForMutation()) return;
	for (FMSWeaponSlotState& Slot : Slots)
	{
		if (Slot.WeaponData && !Slot.WeaponData->HasInfiniteAmmo()) { Slot.MagazineAmmo = Slot.WeaponData->MagazineSize; Slot.ReserveAmmo = GetReserveCapacity(Slot.WeaponData); }
		Slot.ReloadRemaining = Slot.ReloadDuration = Slot.ReloadEndServerTime = 0.0f;
	}
	RefreshStateTags();
	ForceOwnerNetUpdate();
}

void UMSWeaponComponent::RestoreAmmoTick(float Fraction01)
{
	if (!HasAuthorityForMutation()) return;
	const float Fraction = FMath::Clamp(Fraction01, 0.0f, 1.0f);
	for (FMSWeaponSlotState& Slot : Slots)
	{
		if (Slot.WeaponData && !Slot.WeaponData->HasInfiniteAmmo())
		{
			const int32 MaxReserve = GetReserveCapacity(Slot.WeaponData);
			Slot.ReserveAmmo = FMath::Min(MaxReserve,
				Slot.ReserveAmmo + FMath::CeilToInt(static_cast<float>(MaxReserve) * Fraction - UE_KINDA_SMALL_NUMBER));
		}
	}
	ForceOwnerNetUpdate();
}

void UMSWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMSWeaponComponent, Slots);
	DOREPLIFETIME(UMSWeaponComponent, ActiveSlotIndex);
	DOREPLIFETIME(UMSWeaponComponent, SwitchDuration);
	DOREPLIFETIME(UMSWeaponComponent, SwitchEndServerTime);
	// Charging at the muzzle is world-visible: observers need the same compact
	// start time/duration snapshot to animate its glow on their local copy.
	DOREPLIFETIME(UMSWeaponComponent, ChargeSnapshot);
}

int32 UMSWeaponComponent::BeginChargeSnapshot(int32 SlotIndex, float Duration)
{
 if (!HasAuthorityForMutation() || SlotIndex != ActiveSlotIndex || !Slots.IsValidIndex(SlotIndex) || Duration <= 0.f) return 0;
 const auto* Unit = Cast<AMSUnitBase>(GetOwner());
 ++ChargeSnapshot.Sequence;
 ChargeSnapshot.bActive = true;
 ChargeSnapshot.SlotIndex = SlotIndex;
 ChargeSnapshot.SpawnEpoch = Unit ? Unit->GetBattleAssistEpoch() : 0;
 ChargeSnapshot.StartServerTime = GetServerWorldTimeSeconds();
 ChargeSnapshot.Duration = Duration;
 ForceOwnerNetUpdate();
 return ChargeSnapshot.Sequence;
}
bool UMSWeaponComponent::EndChargeSnapshot(int32 ExpectedSequence)
{
 if (!HasAuthorityForMutation() || ExpectedSequence != ChargeSnapshot.Sequence) return false;
 ResetChargeSnapshot(); ForceOwnerNetUpdate(); return true;
}
void UMSWeaponComponent::ResetChargeSnapshot()
{
 if (!HasAuthorityForMutation()) return;
 const int32 Next = ChargeSnapshot.Sequence + 1;
 ChargeSnapshot = FMSWeaponChargeSnapshot(); ChargeSnapshot.Sequence = Next;
 if (auto* ASC = ResolveASC()) ASC->SetLooseGameplayTagCount(MSTags::State_Charging,0);
}
bool UMSWeaponComponent::IsChargeSnapshotActive() const
{
 const auto* Unit = Cast<AMSUnitBase>(GetOwner());
 const auto* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
 return ChargeSnapshot.bActive && ChargeSnapshot.SlotIndex == ActiveSlotIndex && ChargeSnapshot.Duration > 0.f
  && (!Unit || ChargeSnapshot.SpawnEpoch == Unit->GetBattleAssistEpoch())
  && (!ASC || !ASC->HasMatchingGameplayTag(MSTags::State_Dead));
}
bool UMSWeaponComponent::IsChargeSnapshotCurrent(int32 Sequence) const
{
 return IsChargeSnapshotActive() && ChargeSnapshot.Sequence == Sequence;
}
float UMSWeaponComponent::GetChargeProgress01() const
{
 return IsChargeSnapshotActive() ? FMath::Clamp((GetServerWorldTimeSeconds()-ChargeSnapshot.StartServerTime)/ChargeSnapshot.Duration,0.f,1.f) : 0.f;
}
void UMSWeaponComponent::OnRep_ChargeSnapshot()
{
 OnWeaponStateReplicated.Broadcast();
}

void UMSWeaponComponent::OnRep_Slots()
{
	// Replication callbacks only notify presentation. No initialization, recovery,
	// or gameplay effect is rerun on a client.
	bool bWeaponDataChanged = LastNotifiedSlotWeapons.Num() != Slots.Num();
	if (!bWeaponDataChanged)
	{
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (LastNotifiedSlotWeapons[Index] != Slots[Index].WeaponData)
			{
				bWeaponDataChanged = true;
				break;
			}
		}
	}
	LastNotifiedSlotWeapons.Reset(Slots.Num());
	for (const FMSWeaponSlotState& Slot : Slots) LastNotifiedSlotWeapons.Add(Slot.WeaponData);
	OnWeaponStateReplicated.Broadcast();
	if (bWeaponDataChanged) OnWeaponSwitched.Broadcast(ActiveSlotIndex);
}

void UMSWeaponComponent::OnRep_ActiveSlotIndex()
{
	OnWeaponStateReplicated.Broadcast();
	OnWeaponSwitched.Broadcast(ActiveSlotIndex);
}

void UMSWeaponComponent::OnRep_SwitchState()
{
	OnWeaponStateReplicated.Broadcast();
}

bool UMSWeaponComponent::HasAuthorityForMutation() const
{
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

void UMSWeaponComponent::ForceOwnerNetUpdate() const
{
	if (AActor* Owner = GetOwner()) Owner->ForceNetUpdate();
}

float UMSWeaponComponent::GetServerWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.0f;
	const AGameStateBase* GameState = World->GetGameState<AGameStateBase>();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float UMSWeaponComponent::GetReloadRemainingInternal(const FMSWeaponSlotState& Slot) const
{
	if (UsesSynchronizedServerClock() && Slot.ReloadEndServerTime > 0.0f)
	{
		return FMath::Max(0.0f, Slot.ReloadEndServerTime - GetServerWorldTimeSeconds());
	}
	return FMath::Max(0.0f, Slot.ReloadRemaining);
}

bool UMSWeaponComponent::UsesSynchronizedServerClock() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Standalone;
}

UAbilitySystemComponent* UMSWeaponComponent::ResolveASC() const
{
	const IAbilitySystemInterface* Interface = Cast<IAbilitySystemInterface>(GetOwner());
	return Interface ? Interface->GetAbilitySystemComponent() : nullptr;
}

void UMSWeaponComponent::RefreshStateTags()
{
	if (UAbilitySystemComponent* ASC = ResolveASC())
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_Switching, IsSwitching() ? 1 : 0);
		ASC->SetLooseGameplayTagCount(MSTags::State_Reloading, IsReloading(ActiveSlotIndex) ? 1 : 0);
	}
}
