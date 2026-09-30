// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MSMobHandVisuals.generated.h"

class AActor;
class UMSMechRigComponent;
class UMSWeaponComponent;
class UMSWeaponDataBase;
class UPoseableMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;

/** Presentation-only static hand shape. The body skeleton remains the MobLight 22-bone contract. */
UENUM(BlueprintType)
enum class EMSMobHandPose : uint8
{
	Hidden,
	Open,
	Fist,
	Trigger,
	Grip,
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSMobHandPosePair
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="MS|Animation")
	EMSMobHandPose Left = EMSMobHandPose::Hidden;

	UPROPERTY(BlueprintReadOnly, Category="MS|Animation")
	EMSMobHandPose Right = EMSMobHandPose::Hidden;
};

/**
 * Owns optional MobLight static hand overlays and follows the live poseable forearms.
 *
 * The helper is deliberately asset-gated: MPU/TAU meshes never create a hand component,
 * so their existing skeletons, materials, and tick path remain unchanged.
 */
UCLASS(BlueprintType)
class NEOMSGO_API UMSMobHandVisuals : public UObject
{
	GENERATED_BODY()

public:
	/** Set the current body/rig and create the imported static overlays when available. */
	void Initialize(AActor* InOwner, UPoseableMeshComponent* InPoseable,
		UMSMechRigComponent* InRig, UMSWeaponComponent* InWeapons,
		USkeletalMesh* InBodyMesh);

	/** Destroy any overlay components and clear all references. */
	void Reset();

	/** Update visibility and world transforms after the poseable mesh has refreshed its bones. */
	void Update();

	/** Apply the same imported team/role material parameters used by the body mesh. */
	void SetAppearance(const FLinearColor& TeamPrimary, const FLinearColor& RoleAccent);

	/** Preview-only pose override. Combat callers should leave this disabled. */
	void SetPoseOverride(EMSMobHandPose Left, EMSMobHandPose Right);
	void ClearPoseOverride();

	UFUNCTION(BlueprintPure, Category="MS|Animation")
	bool IsUsingMobLightHands() const { return bUsingMobLightHands; }

	UFUNCTION(BlueprintPure, Category="MS|Animation")
	FMSMobHandPosePair GetCurrentPose() const { return CurrentPose; }

	/** Runtime/automation inspection without exposing asset ownership. */
	int32 GetVisibleComponentCountForAutomationTests() const;
	UStaticMeshComponent* GetHandComponentForAutomationTests(bool bLeft, EMSMobHandPose Pose) const;

	/** Stable resolver used by runtime and unit tests; no gameplay state is changed. */
	static FMSMobHandPosePair ResolvePoseForWeapon(const UMSWeaponDataBase* Weapon);
	static bool IsMobLightBody(const USkeletalMesh* Mesh);

	/**
	 * Returns the authored PN1/GK3 fist weapon frame in the live MobLight body space.
	 * This is presentation-only; callers still apply the weapon data's normal skin transform.
	 */
	bool TryGetFistWeaponWorldTransform(const UMSWeaponDataBase& Weapon, bool bLeft,
		FTransform& Out) const;

private:
	void CreateVariantComponents(bool bLeft);
	void ApplyPoseToSide(bool bLeft, EMSMobHandPose Pose);
	void ApplyWorldTransform(bool bLeft, EMSMobHandPose Pose, UStaticMeshComponent& Component);
	void ApplyAppearanceToComponent(UStaticMeshComponent& Component);
	static FTransform GetReferenceBoneTransform(const USkeletalMesh& Mesh, FName BoneName);
	static bool HasVectorParameter(const UMaterialInterface* Material, FName Parameter);

	TWeakObjectPtr<AActor> OwnerActor;
	TWeakObjectPtr<UPoseableMeshComponent> PoseableMesh;
	TWeakObjectPtr<UMSMechRigComponent> Rig;
	TWeakObjectPtr<UMSWeaponComponent> WeaponComponent;
	TWeakObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LeftVariants;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RightVariants;

	FMSMobHandPosePair CurrentPose;
	FLinearColor AppliedTeamPrimary = FLinearColor::Transparent;
	FLinearColor AppliedRoleAccent = FLinearColor::Transparent;
	bool bAppearanceApplied = false;
	bool bUsingMobLightHands = false;
	bool bPoseOverride = false;
	FMSMobHandPosePair PoseOverride;
};
