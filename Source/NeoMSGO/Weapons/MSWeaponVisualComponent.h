// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MSWeaponVisualComponent.generated.h"

class UMaterialInterface;
class UMSMechRigComponent;
class UMSShieldWeaponData;
class UMSWeaponDataBase;
class UMSWeaponComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMSWeaponVisualKind : uint8
{
	None,
	MachineGun,
	BeamRifle,
	Bazooka,
	Melee
};

UENUM(BlueprintType)
enum class EMSHeatSwordMode : uint8
{
	Hidden,
	Single,
	Dual
};

/** Runtime-only Cube weapon props driven by UMSWeaponComponent. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSWeaponVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSWeaponVisualComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Re-scans three-slot weapon props and reads the native shield component separately. */
	UFUNCTION(BlueprintCallable, Category="MS|Weapon|Visual")
	void RefreshFromWeaponComponent();

	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	EMSWeaponVisualKind GetDisplayedWeaponKind() const { return DisplayedKind; }

	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	bool IsHeadVulcanVisible() const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	bool IsShoulderPodVisible() const;

	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	bool IsShieldVisible() const;

	/** True when the equipped native shield is broken and its damaged remnant is displayed. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	bool IsBrokenShieldVisible() const;

	/** True while a weapon-specific imported mesh replaces the legacy Cube prop. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	bool IsImportedHeldMeshVisible() const;

	/** Runtime imported-mesh component (exposed for visual regression tests). */
	UStaticMeshComponent* GetImportedHeldMeshComponent() const { return ImportedHeldMeshComponent; }

	/** Runtime imported shoulder-pod component (exposed for visual regression tests). */
	UStaticMeshComponent* GetImportedShoulderPodMeshComponent() const { return ImportedShoulderPodMeshComponent; }

	/** Left-hand copy of WeaponMesh for EMSWeaponMeshMount::DualHands weapons. */
	UStaticMeshComponent* GetImportedLeftHeldMeshComponent() const { return ImportedLeftHeldMeshComponent; }

	/** Built-in head module mesh (WeaponMesh of the built-in MachineGun) on the head. */
	UStaticMeshComponent* GetImportedHeadVulcanMeshComponent() const { return ImportedHeadVulcanMeshComponent; }

	/** Native shield mesh (UMSShieldWeaponData::WeaponMesh) on the left forearm. */
	UStaticMeshComponent* GetImportedShieldMeshComponent() const { return ImportedShieldMeshComponent; }

	/** Select TAU-sw's right-hand single sword or right+left dual swords. */
	UFUNCTION(BlueprintCallable, Category="MS|Weapon|Visual")
	void SetHeatSwordMode(EMSHeatSwordMode NewMode);

	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	EMSHeatSwordMode GetHeatSwordMode() const { return HeatSwordMode; }

	UStaticMeshComponent* GetRightHeatSwordComponent() const { return RightHeatSwordComponent; }
	UStaticMeshComponent* GetLeftHeatSwordComponent() const { return LeftHeatSwordComponent; }

	/** World-space muzzle used by firing cues. Falls back to the owner location. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	FVector GetMuzzleWorldLocation() const;

	/** Resolves a muzzle direction. Legacy weapons return DefaultWorldDirection unchanged apart from normalization. */
	UFUNCTION(BlueprintPure, Category="MS|Weapon|Visual")
	FVector GetMuzzleWorldDirection(const FVector& DefaultWorldDirection) const;

	static EMSWeaponVisualKind ResolveVisualKind(const FGameplayTag& CategoryTag);

private:
	void BuildProps();
	USceneComponent* CreateGroup(FName Name, USceneComponent* Parent, const FVector& RelativeLocation = FVector::ZeroVector);
	UStaticMeshComponent* AddCube(USceneComponent* Parent, FName Name, const FVector& Location,
		const FVector& Dimensions, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator);
	void SetHeldKind(EMSWeaponVisualKind NewKind);
	void RefreshImportedHeldMesh(const UMSWeaponDataBase* ActiveWeapon);
	void RefreshImportedShoulderPodMesh(const UMSWeaponDataBase* BuiltInShoulderPodWeapon);
	void RefreshImportedHeadVulcanMesh(const UMSWeaponDataBase* BuiltInHeadWeapon);
	void RefreshImportedShieldMesh(const UMSShieldWeaponData* ActiveShieldData, bool bBroken);
	UStaticMeshComponent* CreateImportedMeshComponent(FName Name, USceneComponent* Parent);
	void AttachHeldMesh(UStaticMeshComponent* Component, const UMSWeaponDataBase& Weapon,
		FName BoneName, FName RigJointName);
	void UpdateMountedMeshes();
	void AttachModuleMesh(UStaticMeshComponent* Component, FName RigJointName, FName BoneName,
		const FTransform& JointLocalTransform, bool bAnchorAtBone);
	void RefreshHeatSwordMeshes();
	void AttachHeatSword(UStaticMeshComponent* SwordComponent, FName BoneName, FName RigJointName);
	USceneComponent* GetGroup(EMSWeaponVisualKind Kind) const;
	const UMSWeaponDataBase* GetActiveWeaponData() const;
	const USceneComponent* GetWeaponMount() const;
	bool HasEquippedNativeShield() const;

	UFUNCTION()
	void HandleWeaponSwitched(int32 SlotIndex);

	UPROPERTY(Transient) TObjectPtr<UMSWeaponComponent> WeaponComponent;
	UPROPERTY(Transient) TObjectPtr<UMSMechRigComponent> RigComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> MachineGunGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BeamRifleGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BazookaGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> MeleeGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> HeadVulcanGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> ShoulderPodGroup;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> ShieldGroup;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ImportedHeldMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ImportedShoulderPodMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ImportedLeftHeldMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ImportedHeadVulcanMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ImportedShieldMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightHeatSwordComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftHeatSwordComponent;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	EMSWeaponVisualKind DisplayedKind = EMSWeaponVisualKind::None;
	EMSHeatSwordMode HeatSwordMode = EMSHeatSwordMode::Single;
	TWeakObjectPtr<const UMSWeaponDataBase> ShoulderPodWeapon;
	TWeakObjectPtr<const UMSWeaponDataBase> HeadVulcanWeapon;
	bool bBuilt = false;
	bool bFistWeaponVisualAttached = false;
};
