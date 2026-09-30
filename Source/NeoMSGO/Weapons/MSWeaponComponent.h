// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSWeaponComponent.generated.h"

class UAbilitySystemComponent;
class UMSMobileSuitData;
class UMSWeaponDataBase;
class USoundBase;

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSWeaponSlotState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UMSWeaponDataBase> WeaponData;

	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	int32 MagazineAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	int32 ReserveAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	float ReloadRemaining = 0.0f;

	/** Duration captured when this reload started; never inferred by the HUD. */
	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	float ReloadDuration = 0.0f;

	/** Server-world timestamp at which this slot's reload completes. */
	UPROPERTY(BlueprintReadOnly, Category="Weapon")
	float ReloadEndServerTime = 0.0f;

	bool IsReloading() const { return ReloadRemaining > 0.0f; }
};

/** Server-owned charge clock. Epoch excludes a stale prior deployment. */
USTRUCT()
struct FMSWeaponChargeSnapshot
{
 GENERATED_BODY()
 UPROPERTY() bool bActive = false;
 UPROPERTY() int32 SlotIndex = INDEX_NONE;
 UPROPERTY() int32 SpawnEpoch = 0;
 UPROPERTY() int32 Sequence = 0;
 UPROPERTY() float StartServerTime = 0.f;
 UPROPERTY() float Duration = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSWeaponSlotChanged, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSWeaponStateReplicated);

/** Three-slot weapon, switching, fire-interval and per-slot ammunition state. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxWeaponSlots = 3;

	UMSWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Rebuilds all slots at full ammunition. Soft weapon references are synchronously resolved here. */
	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	void InitializeFromMobileSuit(const UMSMobileSuitData* MobileSuitData);

	/** Runtime/test entry point. Only the first three entries are accepted. */
	void InitializeSlots(const TArray<UMSWeaponDataBase*>& Weapons);

	/**
	 * Replaces the authoritative positional loadout. Clients cannot write the
	 * loadout; they receive it through component replication instead.
	 */
	bool SetLoadout(const TArray<UMSWeaponDataBase*>& Weapons);

	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	bool SwitchToSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	int32 GetSlotCount() const { return Slots.Num(); }

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	UMSWeaponDataBase* GetWeaponData(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	bool IsSwitching() const;

	/** Remaining switch time derived from the server-world end timestamp. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetSwitchRemaining() const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetSwitchEndServerTime() const { return SwitchEndServerTime; }

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetSwitchProgress01() const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	bool CanFire(int32 SlotIndex) const;

	/** Consumes one magazine round and starts fireInterval. Infinite-ammo weapons consume nothing. */
	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	bool ConsumeAmmoAndStartCooldown(int32 SlotIndex);

	/** Consumes a follow-up round belonging to an already-authorized burst. Ignores fire cooldown only. */
	bool ConsumeAmmoDuringBurst(int32 SlotIndex);

	/** Keeps a trigger-level cooldown alive for at least the supplied burst duration. */
	void EnsureFireCooldown(float MinimumRemaining);

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetFireCooldownRemaining() const;

	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	bool StartReload(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	bool IsReloading(int32 SlotIndex) const;

	/** Authoritative per-slot reload clock values for HUDs and other observers. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetReloadRemaining(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetReloadDuration(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetReloadEndServerTime(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	float GetReloadProgress01(int32 SlotIndex) const;

	/** True only when a finite-ammo weapon has neither magazine nor reserve ammunition. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	bool IsEmpty(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	FText GetAmmoDisplay(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	int32 GetMagazineAmmo(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon")
	int32 GetReserveAmmo(int32 SlotIndex) const;
 int32 GetReserveCapacity(const UMSWeaponDataBase* Weapon) const;

	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	void RefillAllAmmo();

	UFUNCTION(BlueprintCallable, Category="MS|Weapon")
	void RestoreAmmoTick(float Fraction01);

	UPROPERTY(BlueprintAssignable, Category="MS|Weapon")
	FMSWeaponSlotChanged OnWeaponSwitched;

	UPROPERTY(BlueprintAssignable, Category="MS|Weapon")
	FMSWeaponStateReplicated OnWeaponStateReplicated;

	USoundBase* GetReloadSound() const { return ReloadSound; }
 int32 BeginChargeSnapshot(int32 SlotIndex, float Duration);
 bool EndChargeSnapshot(int32 ExpectedSequence);
 bool IsChargeSnapshotActive() const;
 bool IsChargeSnapshotCurrent(int32 Sequence) const;
 float GetChargeProgress01() const;
 const FMSWeaponChargeSnapshot& GetChargeSnapshot() const { return ChargeSnapshot; }

private:
 UPROPERTY(ReplicatedUsing=OnRep_ChargeSnapshot)
 FMSWeaponChargeSnapshot ChargeSnapshot;
 UFUNCTION() void OnRep_ChargeSnapshot();
 void ResetChargeSnapshot();
	void CompleteReload(FMSWeaponSlotState& Slot);
	void RefreshStateTags();
	UAbilitySystemComponent* ResolveASC() const;
	bool HasAuthorityForMutation() const;
	void ForceOwnerNetUpdate() const;
	bool UsesSynchronizedServerClock() const;
	float GetServerWorldTimeSeconds() const;
	float GetReloadRemainingInternal(const FMSWeaponSlotState& Slot) const;

	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION()
	void OnRep_ActiveSlotIndex();

	UFUNCTION()
	void OnRep_SwitchState();

	UPROPERTY(ReplicatedUsing=OnRep_Slots)
	TArray<FMSWeaponSlotState> Slots;

	/** Local presentation cache; never replicated and never used as gameplay state. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMSWeaponDataBase>> LastNotifiedSlotWeapons;

	UPROPERTY()
	TObjectPtr<USoundBase> ReloadSound;

	UPROPERTY(ReplicatedUsing=OnRep_ActiveSlotIndex)
	int32 ActiveSlotIndex = INDEX_NONE;
	float FireCooldownRemaining = 0.0f;
	/** Server-local deadline keeps weapon readiness independent of actor/timer tick order. */
	double FireCooldownEndWorldTime = 0.0;
	float SwitchRemaining = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_SwitchState)
	float SwitchDuration = 0.0f;

	/** Absolute server-world time; clients derive countdowns from GameState time. */
	UPROPERTY(ReplicatedUsing=OnRep_SwitchState)
	float SwitchEndServerTime = 0.0f;
};
