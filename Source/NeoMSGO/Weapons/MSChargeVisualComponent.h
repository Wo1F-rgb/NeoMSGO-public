// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSChargeVisualComponent.generated.h"

class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class UMSRangedWeaponData;
class UMSWeaponComponent;
class UMSWeaponVisualComponent;

/** Local presentation of the authoritative charge snapshot at the equipped muzzle. */
UCLASS(ClassGroup=(MS))
class NEOMSGO_API UMSChargeVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSChargeVisualComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	bool IsGlowVisible() const { return bGlowVisible; }
	FVector GetGlowWorldLocation() const;
	float GetGlowScale() const { return GlowScale; }
	UMaterialBillboardComponent* GetGlowBillboard() const { return GlowBillboard; }

private:
	void HideGlow();
	void UpdateGlow();
	FLinearColor ResolveChargeColor(const UMSRangedWeaponData* Weapon) const;

	UPROPERTY(Transient) TObjectPtr<UMSWeaponComponent> WeaponComponent;
	UPROPERTY(Transient) TObjectPtr<UMSWeaponVisualComponent> WeaponVisual;
	UPROPERTY(Transient) TObjectPtr<UMaterialBillboardComponent> GlowBillboard;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CoreMaterial;
	TWeakObjectPtr<const UMSRangedWeaponData> DisplayedWeapon;
	float GlowScale = 0.0f;
	bool bGlowVisible = false;
};
