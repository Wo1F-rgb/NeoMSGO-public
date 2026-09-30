// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSShieldComponent.generated.h"

class UMSMobileSuitData;
class UMSShieldWeaponData;
class UMSWeaponDataBase;

/**
 * Machine-native shield state.  A shield is a suit component rather than one of
 * the three selectable weapon slots.  The server owns durability and the guard
 * decision; clients receive the compact state and use it for presentation.
 */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSShieldComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSShieldComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Resolve the suit-native shield.  Authority also resets durability for a new unit. */
	void InitializeFromMobileSuit(const UMSMobileSuitData* SuitData);

	/** Apply the validated deck choice before play.  This is an authority operation. */
	UFUNCTION(BlueprintCallable, Category="MS|Shield")
	void SetDeckShieldEnabled(bool bEnabled);

	/** Restore a deployed unit to a full, usable native shield. */
	void ResetForRespawn();

	/**
	 * Consume one damage packet when the hit is a valid frontal guard.
	 * OutArmorDamage is explosion chip only; a direct hit is fully consumed even
	 * when it breaks the shield.  OutGuarded reports the DP reduction decision.
	 */
	bool TryAbsorbDamage(float IncomingDamage, bool bExplosive, const FVector& AttackerLocation,
		float& OutArmorDamage, float& OutShieldDamage, bool& OutGuarded, float ShieldDamageScale = 1.f);

	/** Repair amount in durability points. A broken shield becomes active only at full. */
	UFUNCTION(BlueprintCallable, Category="MS|Shield")
	void Repair(float Amount);
	UFUNCTION(BlueprintCallable, Category="MS|Shield")
	void RepairToFull();

	UFUNCTION(BlueprintPure, Category="MS|Shield")
	bool IsShieldEquipped() const { return bEquipped && ShieldData != nullptr; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	bool IsNativeShieldEquipped() const { return IsShieldEquipped(); }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	bool IsShieldBroken() const { return bBroken; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	bool IsShieldVisualActive() const { return IsShieldEquipped() && !bBroken; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	bool IsGuarding() const { return bGuarding; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	float GetCurrentDurability() const { return CurrentDurability; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	float GetShieldDurability() const { return CurrentDurability; }
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	float GetMaxDurability() const;
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	float GetDurabilityRatio() const;
	UFUNCTION(BlueprintPure, Category="MS|Shield")
	UMSShieldWeaponData* GetShieldData() const { return ShieldData; }

	/** True only for an equipped, intact, currently valid automatic frontal guard. */
	bool CanProvideGuard() const;
	/** Called only after an authoritative round is consumed. Keeps the guard down through firing and a short recovery. */
	void NotifyWeaponFired(UMSWeaponDataBase* Weapon);

protected:
	UFUNCTION()
	void OnRep_ShieldState();
	UFUNCTION()
	void OnRep_Guarding();

	/** Current durability is authoritative and replicated to every observing client. */
	UPROPERTY(ReplicatedUsing=OnRep_ShieldState, VisibleInstanceOnly, BlueprintReadOnly, Category="MS|Shield")
	float CurrentDurability = 0.0f;
	UPROPERTY(ReplicatedUsing=OnRep_ShieldState, VisibleInstanceOnly, BlueprintReadOnly, Category="MS|Shield")
	bool bBroken = false;
	UPROPERTY(ReplicatedUsing=OnRep_ShieldState, VisibleInstanceOnly, BlueprintReadOnly, Category="MS|Shield")
	bool bEquipped = false;
	/** Asset identity is replicated so remote observers can resolve the native visual/HUD max. */
	UPROPERTY(ReplicatedUsing=OnRep_ShieldState, VisibleInstanceOnly, BlueprintReadOnly, Category="MS|Shield")
	TObjectPtr<UMSShieldWeaponData> ShieldData;

private:
	void SetGuardTag(bool bShouldGuard);
	void RefreshVisualIfShieldActivityChanged(bool bWasVisualActive);

	/** Deck choice is intentionally not a weapon-slot state. */
	bool bDeckShieldEnabled = true;
	bool bShieldRemovable = true;
	/** Presentation/guard recovery only; never delays weapon cooldown or changes ammunition. */
	TWeakObjectPtr<UMSWeaponDataBase> GuardRecoveryWeapon;
	double GuardRecoveryUntilWorldTime = 0.0;
	UPROPERTY(ReplicatedUsing=OnRep_Guarding, VisibleInstanceOnly, BlueprintReadOnly, Category="MS|Shield", meta=(AllowPrivateAccess="true"))
	bool bGuarding = false;
};
