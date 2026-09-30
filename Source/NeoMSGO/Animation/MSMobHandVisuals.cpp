// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSMobHandVisuals.h"

#include "Animation/MSMechRigComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/UObjectGlobals.h"
#include "Weapons/MSWeaponComponent.h"

namespace
{
	const FString MobLightBodyPrefix(TEXT("/Game/Meshes/MobileSuits/MobLightHands/"));
	const TCHAR* PoseName(const EMSMobHandPose Pose)
	{
		switch (Pose)
		{
		case EMSMobHandPose::Open: return TEXT("Open");
		case EMSMobHandPose::Fist: return TEXT("Fist");
		case EMSMobHandPose::Trigger: return TEXT("Trigger");
		case EMSMobHandPose::Grip: return TEXT("Grip");
		default: return TEXT("Hidden");
		}
	}

	const FName TeamPrimaryParameter(TEXT("MSTeamPrimary"));
	const FName RoleAccentParameter(TEXT("MSRoleAccent"));
}

bool UMSMobHandVisuals::HasVectorParameter(const UMaterialInterface* Material, const FName Parameter)
{
	FLinearColor Value;
	return Material && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Parameter), Value);
}

bool UMSMobHandVisuals::IsMobLightBody(const USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}
	const FString Path = Mesh->GetPathName();
	return Mesh->GetName() == TEXT("SK_MobLightHands") && Path.StartsWith(MobLightBodyPrefix);
}

FTransform UMSMobHandVisuals::GetReferenceBoneTransform(const USkeletalMesh& Mesh, const FName BoneName)
{
	const FReferenceSkeleton& Reference = Mesh.GetRefSkeleton();
	const int32 BoneIndex = Reference.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	TArray<int32, TInlineAllocator<16>> Chain;
	for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
	{
		Chain.Add(Index);
	}

	FTransform Component = FTransform::Identity;
	for (int32 ChainIndex = Chain.Num() - 1; ChainIndex >= 0; --ChainIndex)
	{
		// NeoMSGO's imported skeleton path composes local transforms left-to-right,
		// matching the existing bind-pose calculation in MSMeshVisualComponent.
		Component = Reference.GetRefBonePose()[Chain[ChainIndex]] * Component;
	}
	return Component;
}

void UMSMobHandVisuals::Initialize(AActor* InOwner, UPoseableMeshComponent* InPoseable,
	UMSMechRigComponent* InRig, UMSWeaponComponent* InWeapons,
	USkeletalMesh* InBodyMesh)
{
	Reset();
	OwnerActor = InOwner;
	PoseableMesh = InPoseable;
	Rig = InRig;
	WeaponComponent = InWeapons;
	BodyMesh = InBodyMesh;
	bUsingMobLightHands = IsMobLightBody(InBodyMesh);
	CurrentPose = FMSMobHandPosePair();

	if (!bUsingMobLightHands || !InOwner || !InPoseable)
	{
		return;
	}

	CreateVariantComponents(true);
	CreateVariantComponents(false);
}

void UMSMobHandVisuals::Reset()
{
	for (TObjectPtr<UStaticMeshComponent>& Component : LeftVariants)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	for (TObjectPtr<UStaticMeshComponent>& Component : RightVariants)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	LeftVariants.Reset();
	RightVariants.Reset();
	OwnerActor.Reset();
	PoseableMesh.Reset();
	Rig.Reset();
	WeaponComponent.Reset();
	BodyMesh.Reset();
	bUsingMobLightHands = false;
	bAppearanceApplied = false;
	bPoseOverride = false;
	CurrentPose = FMSMobHandPosePair();
}

void UMSMobHandVisuals::CreateVariantComponents(const bool bLeft)
{
	AActor* Owner = OwnerActor.Get();
	UPoseableMeshComponent* Poseable = PoseableMesh.Get();
	if (!Owner || !Poseable)
	{
		return;
	}

	TArray<TObjectPtr<UStaticMeshComponent>>& Variants = bLeft ? LeftVariants : RightVariants;
	const TCHAR* Side = bLeft ? TEXT("L") : TEXT("R");
	for (const EMSMobHandPose Pose : {EMSMobHandPose::Open, EMSMobHandPose::Fist,
		EMSMobHandPose::Trigger, EMSMobHandPose::Grip})
	{
		const FString AssetName = FString::Printf(TEXT("SM_Hand_%s_%s"), Side, PoseName(Pose));
		const FString AssetPath = FString::Printf(
			TEXT("/Game/Meshes/MobileSuits/MobLightHands/%s.%s"), *AssetName, *AssetName);
		UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(
			UStaticMesh::StaticClass(), nullptr, *AssetPath));
		if (!Mesh)
		{
			// The body remains valid while hand imports are staged or absent in a cooked build.
			continue;
		}

		const FName ComponentName(*FString::Printf(TEXT("MobHand_%s_%s"), Side, PoseName(Pose)));
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, ComponentName);
		Owner->AddInstanceComponent(Component);
		Component->SetupAttachment(Poseable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetVisibility(false, true);
		Component->RegisterComponent();
		Variants.Add(Component);
	}
}

FMSMobHandPosePair UMSMobHandVisuals::ResolvePoseForWeapon(const UMSWeaponDataBase* Weapon)
{
	FMSMobHandPosePair Result;
	Result.Left = EMSMobHandPose::Open;
	Result.Right = EMSMobHandPose::Open;
	if (!Weapon)
	{
		// A valid MobLight with no selected slot still shows relaxed open hands.
		return Result;
	}

	const bool bShield = Cast<UMSShieldWeaponData>(Weapon) != nullptr
		|| Weapon->CategoryTag == MSTags::Weapon_Category_Shield;
	if (Weapon->bIsBuiltIn || bShield)
	{
		Result.Left = EMSMobHandPose::Open;
		Result.Right = EMSMobHandPose::Open;
		return Result;
	}

	if (Weapon->WeaponMeshMount == EMSWeaponMeshMount::DualHands)
	{
		Result.Left = EMSMobHandPose::Grip;
		Result.Right = EMSMobHandPose::Grip;
		return Result;
	}

	if (Weapon->WeaponMeshMount == EMSWeaponMeshMount::Fist)
	{
		Result.Right = EMSMobHandPose::Fist;
		return Result;
	}

	const bool bMelee = Cast<UMSMeleeWeaponData>(Weapon) != nullptr
		|| Weapon->CategoryTag == MSTags::Weapon_Category_Melee;
	const bool bSupply = Cast<UMSSupplyWeaponData>(Weapon) != nullptr
		|| Weapon->CategoryTag == MSTags::Weapon_Category_Supply;
	// These two authored throwable bodies have no pistol trigger. Their gameplay
	// class is ranged, so select the visual grip from the presentation asset.
	const FString VisualName = Weapon->WeaponMesh.ToSoftObjectPath().GetAssetName();
	const bool bThrowableBody = VisualName == TEXT("SM_GR_HandGrenade")
		|| VisualName == TEXT("SM_SMK_SmokeGrenade");
	if (bMelee || bSupply || bThrowableBody)
	{
		Result.Right = EMSMobHandPose::Grip;
		return Result;
	}

	// Ranged Hand mounts use the trigger variant. Unknown handheld categories use
	// the same presentation fallback as UMSWeaponVisualComponent (a gun hand).
	Result.Right = EMSMobHandPose::Trigger;
	return Result;
}

bool UMSMobHandVisuals::TryGetFistWeaponWorldTransform(const UMSWeaponDataBase& Weapon,
	const bool bLeft, FTransform& Out) const
{
	Out = FTransform::Identity;
	if (bLeft || Weapon.WeaponMeshMount != EMSWeaponMeshMount::Fist
		|| !IsMobLightBody(BodyMesh.Get()))
	{
		return false;
	}

	const FString MeshPath = Weapon.WeaponMesh.ToSoftObjectPath().ToString();
	if (MeshPath != TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/SM_PN1_PowerKnuckle.SM_PN1_PowerKnuckle")
		&& MeshPath != TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/SM_GK3_GrappleKnuckle.SM_GK3_GrappleKnuckle"))
	{
		return false;
	}

	UPoseableMeshComponent* Poseable = PoseableMesh.Get();
	const USkeletalMesh* Mesh = BodyMesh.Get();
	if (!Poseable || !Mesh || Poseable->GetBoneIndex(TEXT("forearm_r")) == INDEX_NONE
		|| Mesh->GetRefSkeleton().FindBoneIndex(TEXT("forearm_r")) == INDEX_NONE)
	{
		return false;
	}

	const FTransform ReferenceForearm = GetReferenceBoneTransform(*Mesh, TEXT("forearm_r"));
	const FTransform LiveForearm = Poseable->GetBoneTransformByName(
		TEXT("forearm_r"), EBoneSpaces::ComponentSpace);
	// The fit is authored in the body-world centimetre frame. Keep the measured
	// weapon scale independent from WeaponMeshTransform's import-unit correction;
	// the weapon visual applies that data transform once after this helper returns.
	const FTransform WeaponReference(FQuat(FVector::YAxisVector, PI),
		FVector(-370.0f, 12.0f, 715.0f), FVector(1.18f, 1.55f, 1.15f));
	Out = WeaponReference * (ReferenceForearm.Inverse() * LiveForearm)
		* Poseable->GetComponentTransform();
	return !Out.ContainsNaN();
}

void UMSMobHandVisuals::SetAppearance(const FLinearColor& TeamPrimary, const FLinearColor& RoleAccent)
{
	if (bAppearanceApplied && AppliedTeamPrimary.Equals(TeamPrimary) && AppliedRoleAccent.Equals(RoleAccent))
	{
		return;
	}
	AppliedTeamPrimary = TeamPrimary;
	AppliedRoleAccent = RoleAccent;
	bAppearanceApplied = true;
	for (TObjectPtr<UStaticMeshComponent>& Component : LeftVariants)
	{
		if (Component) ApplyAppearanceToComponent(*Component);
	}
	for (TObjectPtr<UStaticMeshComponent>& Component : RightVariants)
	{
		if (Component) ApplyAppearanceToComponent(*Component);
	}
}

void UMSMobHandVisuals::ApplyAppearanceToComponent(UStaticMeshComponent& Component)
{
	for (int32 SlotIndex = 0; SlotIndex < Component.GetNumMaterials(); ++SlotIndex)
	{
		UMaterialInterface* Material = Component.GetMaterial(SlotIndex);
		const bool bTeam = HasVectorParameter(Material, TeamPrimaryParameter);
		const bool bRole = HasVectorParameter(Material, RoleAccentParameter);
		if (!bTeam && !bRole)
		{
			continue;
		}
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material);
		if (!MID)
		{
			MID = UMaterialInstanceDynamic::Create(Material, this);
		}
		if (!MID)
		{
			continue;
		}
		if (bTeam) MID->SetVectorParameterValue(TeamPrimaryParameter, AppliedTeamPrimary);
		if (bRole) MID->SetVectorParameterValue(RoleAccentParameter, AppliedRoleAccent);
		Component.SetMaterial(SlotIndex, MID);
	}
}

void UMSMobHandVisuals::SetPoseOverride(const EMSMobHandPose Left, const EMSMobHandPose Right)
{
	bPoseOverride = true;
	PoseOverride.Left = Left;
	PoseOverride.Right = Right;
}

void UMSMobHandVisuals::ClearPoseOverride()
{
	bPoseOverride = false;
}

void UMSMobHandVisuals::ApplyWorldTransform(const bool bLeft, const EMSMobHandPose Pose,
	UStaticMeshComponent& Component)
{
	UPoseableMeshComponent* Poseable = PoseableMesh.Get();
	const USkeletalMesh* Mesh = BodyMesh.Get();
	UMSMechRigComponent* MechRig = Rig.Get();
	if (!Poseable || !Mesh)
	{
		return;
	}

	const FName ForearmBone = bLeft ? FName(TEXT("forearm_l")) : FName(TEXT("forearm_r"));
	const FName MountBone = bLeft ? FName(TEXT("weapon_mount_l")) : FName(TEXT("weapon_mount_r"));
	const FName GhostMountName = bLeft ? FName(TEXT("WeaponMount_L")) : FName(TEXT("WeaponMount"));
	const bool bUseMountFrame = Pose == EMSMobHandPose::Trigger || Pose == EMSMobHandPose::Grip;
	const FName ReferenceBone = bUseMountFrame ? MountBone : ForearmBone;
	const int32 ReferenceIndex = Mesh->GetRefSkeleton().FindBoneIndex(ReferenceBone);
	if (ReferenceIndex == INDEX_NONE)
	{
		return;
	}

	FTransform TargetComponent;
	if (bUseMountFrame && MechRig)
	{
		// Match the exact canonical basis used by AttachHeldMesh: the hand assets
		// were authored with source front +Y/up +Z, while held meshes use local +Z
		// as their barrel/grip axis. The real poseable mount supplies translation;
		// the ghost mount supplies the same stabilized weapon orientation.
		if (const USceneComponent* GhostMount = MechRig->GetJoint(GhostMountName))
		{
			const AActor* Owner = OwnerActor.Get();
			const FVector ActorUp = Owner ? Owner->GetActorUpVector() : FVector::UpVector;
			const FQuat DesiredWorld = FRotationMatrix::MakeFromZX(
				GhostMount->GetForwardVector(), ActorUp).ToQuat();
			const FQuat CanonicalRef = FRotationMatrix::MakeFromZX(
				FVector::YAxisVector, FVector::UpVector).ToQuat();
			const FQuat AssetRotation = (DesiredWorld * CanonicalRef.Inverse()).GetNormalized();
			const FVector LiveMountWorld = Poseable->GetBoneLocationByName(
				MountBone, EBoneSpaces::WorldSpace);
			const FTransform ReferenceMount = GetReferenceBoneTransform(*Mesh, MountBone);
			const FVector ScaledReferenceOffset = ReferenceMount.GetTranslation()
				* Poseable->GetComponentScale();
			const FVector Translation = LiveMountWorld - AssetRotation.RotateVector(ScaledReferenceOffset);
			Component.SetWorldTransform(FTransform(AssetRotation, Translation,
				Poseable->GetComponentScale()), false, nullptr, ETeleportType::TeleportPhysics);
			return;
		}
		TargetComponent = Poseable->GetBoneTransformByName(MountBone, EBoneSpaces::ComponentSpace);
	}
	else
	{
		TargetComponent = Poseable->GetBoneTransformByName(ForearmBone, EBoneSpaces::ComponentSpace);
	}

	const FTransform ReferenceComponent = GetReferenceBoneTransform(*Mesh, ReferenceBone);
	// Hand assets are authored in the same body-world coordinates as SK_MobLightHands.
	// Move the imported reference frame to the live frame, then inherit the poseable
	// component's existing yaw, scale, and actor transform.
	const FTransform AssetToComponent = ReferenceComponent.Inverse() * TargetComponent;
	Component.SetWorldTransform(AssetToComponent * Poseable->GetComponentTransform(), false,
		nullptr, ETeleportType::TeleportPhysics);
}

void UMSMobHandVisuals::ApplyPoseToSide(const bool bLeft, const EMSMobHandPose Pose)
{
	TArray<TObjectPtr<UStaticMeshComponent>>& Variants = bLeft ? LeftVariants : RightVariants;
	for (TObjectPtr<UStaticMeshComponent>& Variant : Variants)
	{
		if (Variant)
		{
			const bool bSelected = Variant->GetStaticMesh()
				&& Variant->GetName().EndsWith(FString::Printf(TEXT("_%s"), PoseName(Pose)))
				&& Pose != EMSMobHandPose::Hidden;
			Variant->SetVisibility(bSelected, true);
			if (bSelected)
			{
				ApplyWorldTransform(bLeft, Pose, *Variant);
			}
		}
	}
}

void UMSMobHandVisuals::Update()
{
	if (!bUsingMobLightHands || !PoseableMesh.IsValid())
	{
		return;
	}

	if (bPoseOverride)
	{
		CurrentPose = PoseOverride;
	}
	else
	{
		const UMSWeaponDataBase* ActiveWeapon = nullptr;
		if (UMSWeaponComponent* Weapons = WeaponComponent.Get())
		{
			ActiveWeapon = Weapons->GetWeaponData(Weapons->GetActiveSlotIndex());
		}
		CurrentPose = ResolvePoseForWeapon(ActiveWeapon);
	}

	ApplyPoseToSide(true, CurrentPose.Left);
	ApplyPoseToSide(false, CurrentPose.Right);
}

int32 UMSMobHandVisuals::GetVisibleComponentCountForAutomationTests() const
{
	int32 Count = 0;
	for (const TObjectPtr<UStaticMeshComponent>& Component : LeftVariants)
	{
		if (Component && Component->IsVisible()) ++Count;
	}
	for (const TObjectPtr<UStaticMeshComponent>& Component : RightVariants)
	{
		if (Component && Component->IsVisible()) ++Count;
	}
	return Count;
}

UStaticMeshComponent* UMSMobHandVisuals::GetHandComponentForAutomationTests(
	const bool bLeft, const EMSMobHandPose Pose) const
{
	const TArray<TObjectPtr<UStaticMeshComponent>>& Variants = bLeft ? LeftVariants : RightVariants;
	const FString Suffix = FString::Printf(TEXT("_%s"), PoseName(Pose));
	for (const TObjectPtr<UStaticMeshComponent>& Component : Variants)
	{
		if (Component && Component->GetName().EndsWith(Suffix)) return Component;
	}
	return nullptr;
}
