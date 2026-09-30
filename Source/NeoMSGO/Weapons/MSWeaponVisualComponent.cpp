// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/MSWeaponVisualComponent.h"

#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Weapons/MSShieldPresentation.h"
#include "Animation/MSMobHandVisuals.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSShieldComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"
#include "Weapons/MSChargeVisualComponent.h"
#include "Weapons/MSWeaponComponent.h"

namespace
{
	const FName WeaponColorParameter(TEXT("Color"));
	const FLinearColor Gunmetal(0.10f, 0.13f, 0.16f);
	const FLinearColor DarkMetal(0.035f, 0.045f, 0.055f);
	const FLinearColor BeamBlue(0.08f, 0.65f, 1.0f);
	const FLinearColor WarningRed(0.85f, 0.12f, 0.04f);
	const FName TaurusWeaponMountBone(TEXT("weapon_mount_r"));
	const FName TaurusWeaponMountLeftBone(TEXT("weapon_mount_l"));
	const FName TaurusForearmBone(TEXT("forearm_r"));
	const FName WeaponMuzzleSocket(TEXT("Muzzle"));
	const FName TaurusChargeBlasterWeapon(TEXT("DA_Weapon_TAUrus_ChargeBlasterLauncher"));
	const FName TaurusChargeBlasterMesh(TEXT("SM_TAUrus_ChargeBlasterLauncher"));

	bool TryGetImportedMuzzleSocket(const UStaticMeshComponent* ImportedMesh,
		FVector& OutLocation, FVector& OutDirection)
	{
		if (!ImportedMesh || !ImportedMesh->IsVisible()
			|| !ImportedMesh->GetStaticMesh()
			|| !ImportedMesh->DoesSocketExist(WeaponMuzzleSocket))
		{
			return false;
		}

		OutLocation = ImportedMesh->GetSocketLocation(WeaponMuzzleSocket);
		OutDirection = ImportedMesh->GetSocketRotation(WeaponMuzzleSocket).Vector().GetSafeNormal();
		return !OutDirection.IsNearlyZero();
	}

	bool UsesTaurusChargeBlasterLegacyMuzzle(const UMSWeaponDataBase* ActiveWeapon,
		const UStaticMeshComponent* ImportedMesh)
	{
		return ActiveWeapon && ActiveWeapon->GetFName() == TaurusChargeBlasterWeapon
			&& ActiveWeapon->bOverrideMuzzleTransform
			&& ImportedMesh && ImportedMesh->IsVisible()
			&& ImportedMesh->GetStaticMesh()
			&& ImportedMesh->GetStaticMesh()->GetFName() == TaurusChargeBlasterMesh
			&& !ImportedMesh->DoesSocketExist(WeaponMuzzleSocket);
	}

	// Imported weapon FBXs currently use local +Z as their barrel/blade axis.
	// WeaponMeshTransform supplies all weapon-specific grip, roll, and scale correction.
	// Heavy weapon exports use local +Z as the barrel and local +X as up. This
	// maps those authored axes to the rig's +X forward / +Z up basis.
	const FRotator ImportedWeaponCubeAxisRotation(90.0f, 180.0f, 0.0f);
	// Fist-worn meshes (knuckles): authored +Z strike axis continues the forearm toward the
	// hand (mount -Z) and authored +X faces the back of the hand (mount +X).
	const FRotator ImportedFistAxisRotation(0.0f, 0.0f, 180.0f);
	// Cube rigs have no hand geometry. WeaponMount is the gun-grip point 15 rig units ahead of
	// the forearm axis; a fist-worn mesh sits on that axis just past the forearm end instead.
	const FVector CubeFistOffsetFromWeaponMount(-15.0f, 0.0f, -6.0f);
	// Head module: same +Z-forward/+X-up authoring as held weapons, placed at the procedural
	// head-vulcan position (front face of the Head joint).
	const FVector HeadVulcanOffsetFromHead(18.0f, 0.0f, 4.0f);

	FTransform ApplyWeaponSkinTransform(const FTransform& Fit, const FTransform& Skin)
	{
		// Match the existing poseable attach path: skin translation is authored in
		// weapon units and is converted once, while the fit frame supplies the
		// measured PN1/GK3 world scale and orientation.
		return FTransform(
			(Fit.GetRotation() * Skin.GetRotation()).GetNormalized(),
			Fit.GetLocation() + Fit.GetRotation().RotateVector(Skin.GetLocation() * MSGO::MSScale),
			Fit.GetScale3D() * (Skin.GetScale3D() * MSGO::MSScale));
	}
	// #182 (2026-07-19): no fixed rotation constant can serve every TAU rig. The original
	// TAU-rus binds weapon_mount_r with local +X at component +Y (Roll -90 happened to work),
	// while the re-exported TAU-rus Assault binds it as a near-pure Roll (~-27 deg) whose
	// local +X already faces forward — there a roll-only offset can never point the weapon
	// FBX's +Z barrel forward (the shoulder-carry bug). The attach site now derives the
	// relative rotation from the bone's actual pose instead of using a constant.
}

UMSWeaponVisualComponent::UMSWeaponVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = MaterialFinder.Object;
}

void UMSWeaponVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	WeaponComponent = Unit ? Unit->GetWeaponComponent() : nullptr;
	RigComponent = Unit ? Unit->GetMechRigComponent() : nullptr;
	BuildProps();
	if (Unit)
	{
		if (UMSMeshVisualComponent* MeshVisual = Unit->GetMeshVisualComponent())
		{
			// The fitted fist is world-absolute, so refresh it only after the
			// poseable mesh has published its current component-space bones.
			AddTickPrerequisiteComponent(MeshVisual);
		}
	}
	if (WeaponComponent)
	{
		WeaponComponent->OnWeaponSwitched.AddUniqueDynamic(this, &UMSWeaponVisualComponent::HandleWeaponSwitched);
	}
	RefreshFromWeaponComponent();
	if (Unit && Unit->GetNetMode() != NM_DedicatedServer
		&& !Unit->FindComponentByClass<UMSChargeVisualComponent>())
	{
		UMSChargeVisualComponent* ChargeVisual = NewObject<UMSChargeVisualComponent>(
			Unit, TEXT("WeaponChargeVisualComponent"));
		Unit->AddInstanceComponent(ChargeVisual);
		ChargeVisual->RegisterComponent();
	}
}

void UMSWeaponVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WeaponComponent)
	{
		WeaponComponent->OnWeaponSwitched.RemoveDynamic(this, &UMSWeaponVisualComponent::HandleWeaponSwitched);
	}
	Super::EndPlay(EndPlayReason);
}

void UMSWeaponVisualComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateMountedMeshes();
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSShieldComponent* Shield = Unit ? Unit->GetShieldComponent() : nullptr;
	if (Shield && Shield->IsShieldEquipped())
	{
		RefreshImportedShieldMesh(Shield->GetShieldData(), Shield->IsShieldBroken());
	}
}

USceneComponent* UMSWeaponVisualComponent::CreateGroup(const FName Name, USceneComponent* Parent,
	const FVector& RelativeLocation)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Parent)
	{
		return nullptr;
	}
	USceneComponent* Group = NewObject<USceneComponent>(Owner, Name);
	Owner->AddInstanceComponent(Group);
	Group->SetupAttachment(Parent);
	Group->SetRelativeLocation(RelativeLocation);
	Group->RegisterComponent();
	return Group;
}

UStaticMeshComponent* UMSWeaponVisualComponent::AddCube(USceneComponent* Parent, const FName Name,
	const FVector& Location, const FVector& Dimensions, const FLinearColor& Color, const FRotator& Rotation)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Parent || !CubeMesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, Name);
	Owner->AddInstanceComponent(Part);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(CubeMesh);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Dimensions / 100.0f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetMobility(EComponentMobility::Movable);
	Part->RegisterComponent();
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Material->SetVectorParameterValue(WeaponColorParameter, Color);
		Part->SetMaterial(0, Material);
	}
	Parts.Add(Part);
	return Part;
}

void UMSWeaponVisualComponent::BuildProps()
{
	if (bBuilt || !RigComponent)
	{
		return;
	}
	USceneComponent* WeaponMount = RigComponent->GetJoint(TEXT("WeaponMount"));
	USceneComponent* Head = RigComponent->GetJoint(TEXT("Head"));
	USceneComponent* ShoulderL = RigComponent->GetJoint(TEXT("Shoulder_L"));
	USceneComponent* ForearmL = RigComponent->GetJoint(TEXT("Forearm_L"));
	if (!WeaponMount || !Head || !ShoulderL || !ForearmL)
	{
		return;
	}

	MachineGunGroup = CreateGroup(TEXT("WeaponVisual_MachineGun"), WeaponMount);
	AddCube(MachineGunGroup, TEXT("MG_Receiver"), {28,0,0}, {58,16,18}, Gunmetal);
	AddCube(MachineGunGroup, TEXT("MG_Barrel"), {72,0,3}, {42,7,7}, DarkMetal);
	AddCube(MachineGunGroup, TEXT("MG_Grip"), {20,0,-17}, {12,10,28}, DarkMetal, {0,12,0});
	AddCube(MachineGunGroup, TEXT("MG_Magazine"), {38,0,-17}, {16,13,26}, WarningRed, {0,-12,0});

	BeamRifleGroup = CreateGroup(TEXT("WeaponVisual_BeamRifle"), WeaponMount);
	AddCube(BeamRifleGroup, TEXT("BR_Core"), {36,0,0}, {82,12,14}, FLinearColor(0.68f,0.72f,0.76f));
	AddCube(BeamRifleGroup, TEXT("BR_Emitter"), {84,0,0}, {22,20,20}, BeamBlue);
	AddCube(BeamRifleGroup, TEXT("BR_TopRail"), {28,0,11}, {50,6,7}, BeamBlue);
	AddCube(BeamRifleGroup, TEXT("BR_Grip"), {10,0,-16}, {11,10,28}, DarkMetal);

	BazookaGroup = CreateGroup(TEXT("WeaponVisual_Bazooka"), WeaponMount, {0,0,10});
	AddCube(BazookaGroup, TEXT("BZ_Tube"), {42,0,0}, {112,25,25}, FLinearColor(0.18f,0.22f,0.16f));
	AddCube(BazookaGroup, TEXT("BZ_Muzzle"), {101,0,0}, {14,34,34}, DarkMetal);
	AddCube(BazookaGroup, TEXT("BZ_Rear"), {-18,0,0}, {18,31,31}, WarningRed);
	AddCube(BazookaGroup, TEXT("BZ_Grip"), {24,0,-24}, {13,12,32}, DarkMetal);

	MeleeGroup = CreateGroup(TEXT("WeaponVisual_Melee"), WeaponMount);
	AddCube(MeleeGroup, TEXT("ML_Handle"), {10,0,0}, {38,10,10}, DarkMetal);
	AddCube(MeleeGroup, TEXT("ML_Guard"), {31,0,0}, {8,30,18}, WarningRed);
	AddCube(MeleeGroup, TEXT("ML_Blade"), {82,0,0}, {95,8,16}, FLinearColor(0.25f,0.9f,1.0f));
	AddCube(MeleeGroup, TEXT("ML_Tip"), {132,0,0}, {18,5,9}, FLinearColor::White, {0,45,0});

	HeadVulcanGroup = CreateGroup(TEXT("WeaponVisual_HeadVulcan"), Head, {18,0,4});
	AddCube(HeadVulcanGroup, TEXT("HV_Left"), {14,-13,0}, {34,6,6}, DarkMetal);
	AddCube(HeadVulcanGroup, TEXT("HV_Right"), {14,13,0}, {34,6,6}, DarkMetal);
	AddCube(HeadVulcanGroup, TEXT("HV_Sensor"), {-1,0,7}, {12,20,8}, WarningRed);

	ShoulderPodGroup = CreateGroup(TEXT("WeaponVisual_ShoulderPod"), ShoulderL, {4,0,14});
	AddCube(ShoulderPodGroup, TEXT("SP_Case"), {5,0,8}, {42,30,28}, FLinearColor(0.20f,0.23f,0.18f));
	AddCube(ShoulderPodGroup, TEXT("SP_TubeTop"), {28,-8,14}, {28,8,8}, WarningRed);
	AddCube(ShoulderPodGroup, TEXT("SP_TubeBottom"), {28,8,14}, {28,8,8}, WarningRed);

	ShieldGroup = CreateGroup(TEXT("WeaponVisual_Shield"), ForearmL, {0,-18,-18});
	AddCube(ShieldGroup, TEXT("SH_Plate"), {0,-9,0}, {10,72,100}, FLinearColor(0.70f,0.74f,0.80f));
	AddCube(ShieldGroup, TEXT("SH_TrimTop"), {0,-9,48}, {14,76,10}, WarningRed);
	AddCube(ShieldGroup, TEXT("SH_TrimBottom"), {0,-9,-48}, {14,58,10}, WarningRed);
	AddCube(ShieldGroup, TEXT("SH_Emblem"), {-8,-10,0}, {8,24,24}, BeamBlue);

	ImportedHeldMeshComponent = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("WeaponVisual_ImportedHeldMesh"));
	GetOwner()->AddInstanceComponent(ImportedHeldMeshComponent);
	ImportedHeldMeshComponent->SetupAttachment(WeaponMount);
	ImportedHeldMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImportedHeldMeshComponent->SetGenerateOverlapEvents(false);
	ImportedHeldMeshComponent->SetCanEverAffectNavigation(false);
	ImportedHeldMeshComponent->SetMobility(EComponentMobility::Movable);
	ImportedHeldMeshComponent->SetVisibility(false, true);
	ImportedHeldMeshComponent->RegisterComponent();

	ImportedShoulderPodMeshComponent = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("WeaponVisual_ImportedShoulderPod"));
	GetOwner()->AddInstanceComponent(ImportedShoulderPodMeshComponent);
	ImportedShoulderPodMeshComponent->SetupAttachment(ShoulderL);
	ImportedShoulderPodMeshComponent->SetRelativeLocation(FVector(4.0f, 0.0f, 14.0f));
	ImportedShoulderPodMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImportedShoulderPodMeshComponent->SetGenerateOverlapEvents(false);
	ImportedShoulderPodMeshComponent->SetCanEverAffectNavigation(false);
	ImportedShoulderPodMeshComponent->SetMobility(EComponentMobility::Movable);
	ImportedShoulderPodMeshComponent->SetVisibility(false, true);
	ImportedShoulderPodMeshComponent->RegisterComponent();

	ImportedLeftHeldMeshComponent = CreateImportedMeshComponent(
		TEXT("WeaponVisual_ImportedHeldMesh_L"), RigComponent->GetJoint(TEXT("WeaponMount_L")));
	ImportedHeadVulcanMeshComponent = CreateImportedMeshComponent(TEXT("WeaponVisual_ImportedHeadVulcan"), Head);
	ImportedShieldMeshComponent = CreateImportedMeshComponent(TEXT("WeaponVisual_ImportedShield"), ForearmL);

	RightHeatSwordComponent = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("WeaponVisual_TAUswHeatSword_R"));
	GetOwner()->AddInstanceComponent(RightHeatSwordComponent);
	RightHeatSwordComponent->SetupAttachment(WeaponMount);
	RightHeatSwordComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightHeatSwordComponent->SetGenerateOverlapEvents(false);
	RightHeatSwordComponent->SetCanEverAffectNavigation(false);
	RightHeatSwordComponent->SetMobility(EComponentMobility::Movable);
	RightHeatSwordComponent->SetVisibility(false, true);
	RightHeatSwordComponent->RegisterComponent();

	LeftHeatSwordComponent = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("WeaponVisual_TAUswHeatSword_L"));
	GetOwner()->AddInstanceComponent(LeftHeatSwordComponent);
	LeftHeatSwordComponent->SetupAttachment(RigComponent->GetJoint(TEXT("WeaponMount_L")));
	LeftHeatSwordComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftHeatSwordComponent->SetGenerateOverlapEvents(false);
	LeftHeatSwordComponent->SetCanEverAffectNavigation(false);
	LeftHeatSwordComponent->SetMobility(EComponentMobility::Movable);
	LeftHeatSwordComponent->SetVisibility(false, true);
	LeftHeatSwordComponent->RegisterComponent();

	bBuilt = true;
	SetHeldKind(EMSWeaponVisualKind::None);
	HeadVulcanGroup->SetVisibility(false, true);
	ShoulderPodGroup->SetVisibility(false, true);
	ShieldGroup->SetVisibility(false, true);
}

UStaticMeshComponent* UMSWeaponVisualComponent::CreateImportedMeshComponent(const FName Name, USceneComponent* Parent)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Parent)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, Name);
	Owner->AddInstanceComponent(Component);
	Component->SetupAttachment(Parent);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetVisibility(false, true);
	Component->RegisterComponent();
	return Component;
}

EMSWeaponVisualKind UMSWeaponVisualComponent::ResolveVisualKind(const FGameplayTag& CategoryTag)
{
	if (CategoryTag == MSTags::Weapon_Category_BeamRifle || CategoryTag == MSTags::Weapon_Category_ContinuousBeam)
	{
		return EMSWeaponVisualKind::BeamRifle;
	}
	if (CategoryTag == MSTags::Weapon_Category_Bazooka || CategoryTag == MSTags::Weapon_Category_Homing)
	{
		return EMSWeaponVisualKind::Bazooka;
	}
	if (CategoryTag == MSTags::Weapon_Category_Melee)
	{
		return EMSWeaponVisualKind::Melee;
	}
	if (CategoryTag == MSTags::Weapon_Category_Shield || CategoryTag == MSTags::Weapon_Category_Supply)
	{
		return EMSWeaponVisualKind::None;
	}
	return EMSWeaponVisualKind::MachineGun;
}

USceneComponent* UMSWeaponVisualComponent::GetGroup(const EMSWeaponVisualKind Kind) const
{
	switch (Kind)
	{
	case EMSWeaponVisualKind::MachineGun: return MachineGunGroup;
	case EMSWeaponVisualKind::BeamRifle: return BeamRifleGroup;
	case EMSWeaponVisualKind::Bazooka: return BazookaGroup;
	case EMSWeaponVisualKind::Melee: return MeleeGroup;
	default: return nullptr;
	}
}

void UMSWeaponVisualComponent::SetHeldKind(const EMSWeaponVisualKind NewKind)
{
	DisplayedKind = NewKind;
	for (const EMSWeaponVisualKind Kind : {EMSWeaponVisualKind::MachineGun, EMSWeaponVisualKind::BeamRifle,
		EMSWeaponVisualKind::Bazooka, EMSWeaponVisualKind::Melee})
	{
		if (USceneComponent* Group = GetGroup(Kind))
		{
			Group->SetVisibility(Kind == NewKind, true);
		}
	}
}

void UMSWeaponVisualComponent::RefreshFromWeaponComponent()
{
	if (!WeaponComponent || !RigComponent)
	{
		AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
		WeaponComponent = Unit ? Unit->GetWeaponComponent() : nullptr;
		RigComponent = Unit ? Unit->GetMechRigComponent() : nullptr;
	}
	BuildProps();
	if (!bBuilt || !WeaponComponent)
	{
		return;
	}
	// Bone-attached props read the live bone pose; make it match the ghost rig first (a
	// freshly built mesh is still in its reference pose until the next tick).
	if (const AMSUnitBase* PoseOwner = Cast<AMSUnitBase>(GetOwner()))
	{
		if (UMSMeshVisualComponent* MeshVisual = PoseOwner->GetMeshVisualComponent())
		{
			MeshVisual->SyncPoseToRig();
		}
	}
	bool bHeadVulcan = false;
	bool bShoulderPod = false;
	// Native machine shield is independent of the three selectable weapon slots.
	// Legacy shield entries remain loadable for migration, but must not create a
	// second forearm prop or make a foreign slot look like an equipped shield.
	const AMSUnitBase* OwnerUnit = Cast<AMSUnitBase>(GetOwner());
	const UMSShieldComponent* NativeShield = OwnerUnit ? OwnerUnit->GetShieldComponent() : nullptr;
	const bool bShieldEquipped = NativeShield && NativeShield->IsShieldEquipped();
	const bool bShieldBroken = bShieldEquipped && NativeShield->IsShieldBroken();
	const UMSWeaponDataBase* BuiltInShoulderPodWeapon = nullptr;
	const UMSWeaponDataBase* BuiltInHeadWeapon = nullptr;
	for (int32 Index = 0; Index < WeaponComponent->GetSlotCount(); ++Index)
	{
		const UMSWeaponDataBase* Weapon = WeaponComponent->GetWeaponData(Index);
		if (!Weapon)
		{
			continue;
		}
		if (Weapon->bIsBuiltIn)
		{
			if (Weapon->CategoryTag == MSTags::Weapon_Category_MachineGun)
			{
				bHeadVulcan = true;
				BuiltInHeadWeapon = BuiltInHeadWeapon ? BuiltInHeadWeapon : Weapon;
			}
			if (Weapon->CategoryTag == MSTags::Weapon_Category_Bazooka)
			{
				bShoulderPod = true;
				BuiltInShoulderPodWeapon = Weapon;
			}
		}
	}
	HeadVulcanWeapon = BuiltInHeadWeapon;
	RefreshImportedHeadVulcanMesh(BuiltInHeadWeapon);
	HeadVulcanGroup->SetVisibility(bHeadVulcan
		&& !(ImportedHeadVulcanMeshComponent && ImportedHeadVulcanMeshComponent->IsVisible()), true);
	ShoulderPodWeapon = BuiltInShoulderPodWeapon;
	RefreshImportedShoulderPodMesh(BuiltInShoulderPodWeapon);
	if (!ImportedShoulderPodMeshComponent || !ImportedShoulderPodMeshComponent->IsVisible())
	{
		ShoulderPodGroup->SetVisibility(bShoulderPod, true);
	}
	// Keep an equipped broken shield attached so its remnant follows the lowered forearm.
	RefreshImportedShieldMesh(bShieldEquipped ? NativeShield->GetShieldData() : nullptr, bShieldBroken);
	ShieldGroup->SetVisibility(bShieldEquipped && !bShieldBroken
		&& !(ImportedShieldMeshComponent && ImportedShieldMeshComponent->IsVisible()), true);

	const UMSWeaponDataBase* Active = WeaponComponent->GetWeaponData(WeaponComponent->GetActiveSlotIndex());
	SetHeldKind(Active && !Active->bIsBuiltIn ? ResolveVisualKind(Active->CategoryTag) : EMSWeaponVisualKind::None);
	HeatSwordMode = Active && Active->IsA<UMSDualHeatSwordWeaponData>()
		? EMSHeatSwordMode::Dual
		: (Active && Active->IsA<UMSMeleeWeaponData>()
			? EMSHeatSwordMode::Single : EMSHeatSwordMode::Hidden);
	RefreshImportedHeldMesh(Active);
	RefreshHeatSwordMeshes();
	SetComponentTickEnabled(bFistWeaponVisualAttached || bShieldEquipped);
}

void UMSWeaponVisualComponent::RefreshImportedShoulderPodMesh(const UMSWeaponDataBase* BuiltInShoulderPodWeapon)
{
	if (!ImportedShoulderPodMeshComponent || !RigComponent)
	{
		return;
	}

	UStaticMesh* SelectedMesh = BuiltInShoulderPodWeapon
		? BuiltInShoulderPodWeapon->WeaponMesh.LoadSynchronous() : nullptr;
	const bool bShowImportedMesh = BuiltInShoulderPodWeapon && BuiltInShoulderPodWeapon->bIsBuiltIn
		&& SelectedMesh;
	ImportedShoulderPodMeshComponent->SetStaticMesh(SelectedMesh);
	ImportedShoulderPodMeshComponent->SetVisibility(bShowImportedMesh, true);
	if (!bShowImportedMesh)
	{
		// Hidden/meshless/built-in slots still need to clear the absolute fit flags
		// so a later ordinary weapon can restore its normal parent-relative contract.
		ImportedHeldMeshComponent->SetAbsolute(false, false, true);
		return;
	}

	// Shoulder_L is a Cube-rig joint below the already MSScale'd rig root. Keep the
	// authored mesh dimensions in world centimetres: ignore the rig root's inherited
	// MSScale for this non-deform static mesh instead of multiplying the mesh twice.
	const FTransform& SkinTransform = BuiltInShoulderPodWeapon->WeaponMeshTransform;
	const FQuat AxisCorrection = ImportedWeaponCubeAxisRotation.Quaternion();
	ImportedShoulderPodMeshComponent->SetAbsolute(false, false, true);
	ImportedShoulderPodMeshComponent->AttachToComponent(
		RigComponent->GetJoint(TEXT("Shoulder_L")), FAttachmentTransformRules::KeepRelativeTransform);
	ImportedShoulderPodMeshComponent->SetRelativeTransform(FTransform(
		AxisCorrection * SkinTransform.GetRotation(),
		FVector(4.0f, 0.0f, 14.0f) + SkinTransform.GetLocation(),
		SkinTransform.GetScale3D() * MSGO::MSScale));
	ShoulderPodGroup->SetVisibility(false, true);
}

void UMSWeaponVisualComponent::AttachModuleMesh(UStaticMeshComponent* Component, const FName RigJointName,
	const FName BoneName, const FTransform& JointLocalTransform, const bool bAnchorAtBone)
{
	USceneComponent* Joint = RigComponent ? RigComponent->GetJoint(RigJointName) : nullptr;
	if (!Component || !Joint)
	{
		return;
	}
	AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	UPoseableMeshComponent* Poseable = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
	if (Poseable && Poseable->GetBoneIndex(BoneName) != INDEX_NONE)
	{
		// Orient from the pose-driving ghost joint and follow the imported bone rigidly. Limb
		// props anchor at the bone itself (arm placement differs per mesh); the head module stays
		// on the ghost head centre, which every mesh fitted to 1800cm shares closely. Absolute
		// scale ignores the imported skeleton's FBX root scale (~100).
		const FTransform JointWorld = Joint->GetComponentTransform();
		FTransform World = JointLocalTransform * JointWorld;
		if (bAnchorAtBone)
		{
			World.SetLocation(Poseable->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace)
				+ JointWorld.TransformVector(JointLocalTransform.GetLocation()));
		}
		Component->SetAbsolute(false, false, true);
		Component->AttachToComponent(Poseable, FAttachmentTransformRules::KeepRelativeTransform, BoneName);
		Component->SetWorldLocationAndRotation(World.GetLocation(), World.GetRotation());
		Component->SetWorldScale3D(World.GetScale3D());
		return;
	}
	// Cube joints inherit MSScale from RigRoot; the authored 1/MSScale cancels it once.
	Component->SetAbsolute(false, false, false);
	Component->AttachToComponent(Joint, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetRelativeTransform(JointLocalTransform);
}

void UMSWeaponVisualComponent::RefreshImportedHeadVulcanMesh(const UMSWeaponDataBase* BuiltInHeadWeapon)
{
	if (!ImportedHeadVulcanMeshComponent || !RigComponent)
	{
		return;
	}
	UStaticMesh* SelectedMesh = BuiltInHeadWeapon && BuiltInHeadWeapon->bIsBuiltIn
		? BuiltInHeadWeapon->WeaponMesh.LoadSynchronous() : nullptr;
	ImportedHeadVulcanMeshComponent->SetStaticMesh(SelectedMesh);
	ImportedHeadVulcanMeshComponent->SetVisibility(SelectedMesh != nullptr, true);
	if (!SelectedMesh)
	{
		return;
	}
	const FTransform& SkinTransform = BuiltInHeadWeapon->WeaponMeshTransform;
	AttachModuleMesh(ImportedHeadVulcanMeshComponent, TEXT("Head"), TEXT("head"), FTransform(
		ImportedWeaponCubeAxisRotation.Quaternion() * SkinTransform.GetRotation(),
		HeadVulcanOffsetFromHead + SkinTransform.GetLocation(), SkinTransform.GetScale3D()), false);
	HeadVulcanGroup->SetVisibility(false, true);
}

void UMSWeaponVisualComponent::RefreshImportedShieldMesh(const UMSShieldWeaponData* ActiveShieldData,
	const bool bBroken)
{
	if (!ImportedShieldMeshComponent || !RigComponent)
	{
		return;
	}
	UStaticMesh* SelectedMesh = ActiveShieldData ? ActiveShieldData->WeaponMesh.LoadSynchronous() : nullptr;
	if (ImportedShieldMeshComponent->GetStaticMesh() != SelectedMesh) ImportedShieldMeshComponent->EmptyOverrideMaterials();
	if (ImportedShieldMeshComponent->GetStaticMesh() != SelectedMesh)
	{
		ImportedShieldMeshComponent->SetStaticMesh(SelectedMesh);
	}
	ImportedShieldMeshComponent->SetVisibility(SelectedMesh != nullptr && !bBroken, false);
	if (!SelectedMesh)
	{
		MSShieldPresentation::UpdateBrokenAppearance(ImportedShieldMeshComponent, false);
		return;
	}
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	MSShieldPresentation::ApplyTeamColor(ImportedShieldMeshComponent,
		Unit && Unit->GetGenericTeamId().GetId() == MSTeam::TeamEnemy
			? RigComponent->GetPartColor(TEXT("Torso"))
			: UMSMeshVisualComponent::GetRoleAccentColor(Unit ? Unit->GetMobileSuitData() : nullptr), true);
	const UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	MSShieldPresentation::Attach(ImportedShieldMeshComponent, RigComponent->GetJoint(TEXT("Forearm_L")),
		MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr, RigComponent->GetRigRoot()->GetComponentTransform(),
		ActiveShieldData->WeaponMeshTransform, bBroken ? 0.0f
			: (Animator ? Animator->GetPoseState().GuardPoseAlpha : 0.0f),
		Animator ? Animator->GetPoseState().UpperBodyYaw : 0.0f);
	MSShieldPresentation::UpdateBrokenAppearance(ImportedShieldMeshComponent, bBroken);
	ShieldGroup->SetVisibility(false, true);
}

void UMSWeaponVisualComponent::AttachHeldMesh(UStaticMeshComponent* Component, const UMSWeaponDataBase& Weapon,
	const FName BoneName, const FName RigJointName)
{
	AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	UPoseableMeshComponent* Poseable = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
	const FTransform& SkinTransform = Weapon.WeaponMeshTransform;
	const bool bFist = Weapon.WeaponMeshMount == EMSWeaponMeshMount::Fist;
	const bool bLeft = BoneName == TaurusWeaponMountLeftBone;
	if (Component && MeshVisual && MeshVisual->GetMobHandVisuals())
	{
		FTransform FistWorld;
		if (MeshVisual->GetMobHandVisuals()->TryGetFistWeaponWorldTransform(Weapon, bLeft, FistWorld)
			&& Poseable && Poseable->GetBoneIndex(TaurusForearmBone) != INDEX_NONE)
		{
			Component->AttachToComponent(Poseable,
				FAttachmentTransformRules::KeepWorldTransform, TaurusForearmBone);
			Component->SetAbsolute(true, true, true);
			Component->SetWorldTransform(ApplyWeaponSkinTransform(FistWorld, SkinTransform), false,
				nullptr, ETeleportType::TeleportPhysics);
			if (!bLeft)
			{
				bFistWeaponVisualAttached = true;
				SetComponentTickEnabled(true);
			}
			return;
		}
	}

	if (Poseable && Poseable->GetBoneIndex(BoneName) != INDEX_NONE)
	{
		// The imported non-deform bone carries an FBX root scale (~100). The weapon
		// must follow its location/rotation but not inherit that scale.
		Component->SetAbsolute(false, false, true);
		if (!bLeft)
		{
			bFistWeaponVisualAttached = false;
			SetComponentTickEnabled(HasEquippedNativeShield());
		}
		Component->AttachToComponent(Poseable,
			FAttachmentTransformRules::KeepRelativeTransform, BoneName);
		// weapon_mount_r's bind orientation differs per TAU rig, so solve the offset from
		// the bone's live pose: weapon local +Z (authored barrel) must face the ghost
		// WeaponMount's +X (or the actor's forward), with the actor's up steadying roll.
		// Fist-worn meshes instead continue the ghost forearm axis (mount -Z).
		const FQuat BoneWorld = Poseable->GetBoneQuaternion(BoneName, EBoneSpaces::WorldSpace);
		const USceneComponent* GhostMount = RigComponent ? RigComponent->GetJoint(RigJointName) : nullptr;
		const FVector BarrelDir = GhostMount
			? GhostMount->GetForwardVector() : GetOwner()->GetActorForwardVector();
		const FQuat DesiredWorld = bFist
			? FRotationMatrix::MakeFromZX(
				GhostMount ? -GhostMount->GetUpVector() : -GetOwner()->GetActorUpVector(), BarrelDir).ToQuat()
			: FRotationMatrix::MakeFromZX(BarrelDir, GetOwner()->GetActorUpVector()).ToQuat();
		const FQuat RelativeRotation =
			BoneWorld.Inverse() * DesiredWorld * SkinTransform.GetRotation();
		Component->SetRelativeLocationAndRotation(
			FVector::ZeroVector, RelativeRotation);
		Component->SetRelativeScale3D(
			SkinTransform.GetScale3D() * MSGO::MSScale);
		const FVector MountWorld = Poseable->GetBoneLocation(
			BoneName, EBoneSpaces::WorldSpace);
		Component->SetWorldLocation(
			MountWorld + DesiredWorld.RotateVector(
				SkinTransform.GetLocation() * MSGO::MSScale));
	}
	else if (USceneComponent* WeaponMount = RigComponent->GetJoint(RigJointName))
	{
		// Cube rigs already carry MSScale on RigRoot, so authored weapon scale stays 1.
		Component->SetAbsolute(false, false, false);
		if (!bLeft)
		{
			bFistWeaponVisualAttached = false;
			SetComponentTickEnabled(HasEquippedNativeShield());
		}
		Component->AttachToComponent(WeaponMount, FAttachmentTransformRules::KeepRelativeTransform);
		const FQuat AxisCorrection = bFist
			? ImportedFistAxisRotation.Quaternion() : ImportedWeaponCubeAxisRotation.Quaternion();
		Component->SetRelativeTransform(FTransform(
			AxisCorrection * SkinTransform.GetRotation(),
			(bFist ? CubeFistOffsetFromWeaponMount : FVector::ZeroVector) + SkinTransform.GetLocation(),
			SkinTransform.GetScale3D()));
	}
}

void UMSWeaponVisualComponent::UpdateMountedMeshes()
{
	if (!bFistWeaponVisualAttached || !ImportedHeldMeshComponent || !WeaponComponent)
	{
		return;
	}

	const UMSWeaponDataBase* ActiveWeapon = GetActiveWeaponData();
	AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	UMSMobHandVisuals* HandVisuals = MeshVisual ? MeshVisual->GetMobHandVisuals() : nullptr;
	FTransform FistWorld;
	if (!ActiveWeapon || !HandVisuals
		|| !HandVisuals->TryGetFistWeaponWorldTransform(*ActiveWeapon, false, FistWorld))
	{
		bFistWeaponVisualAttached = false;
		SetComponentTickEnabled(HasEquippedNativeShield());
		ImportedHeldMeshComponent->SetAbsolute(false, false, true);
		return;
	}

	ImportedHeldMeshComponent->SetAbsolute(true, true, true);
	ImportedHeldMeshComponent->SetWorldTransform(
		ApplyWeaponSkinTransform(FistWorld, ActiveWeapon->WeaponMeshTransform), false,
		nullptr, ETeleportType::TeleportPhysics);
}

void UMSWeaponVisualComponent::RefreshImportedHeldMesh(const UMSWeaponDataBase* ActiveWeapon)
{
	if (!ImportedHeldMeshComponent || !RigComponent)
	{
		return;
	}
	bFistWeaponVisualAttached = false;
	SetComponentTickEnabled(HasEquippedNativeShield());

	// Native shields own the forearm visual. A legacy shield entry left in a weapon slot must
	// never show its shield mesh in the hand.
	const bool bShieldData = ActiveWeapon && ActiveWeapon->IsA<UMSShieldWeaponData>();
	UStaticMesh* SelectedWeaponMesh = ActiveWeapon && !bShieldData
		? ActiveWeapon->WeaponMesh.LoadSynchronous() : nullptr;
	const bool bDualWeapon = ActiveWeapon && ActiveWeapon->IsA<UMSDualHeatSwordWeaponData>();
	const bool bShowImportedMesh = ActiveWeapon && !ActiveWeapon->bIsBuiltIn
		&& SelectedWeaponMesh && !bDualWeapon;
	const bool bShowLeftMesh = bShowImportedMesh && ImportedLeftHeldMeshComponent
		&& ActiveWeapon->WeaponMeshMount == EMSWeaponMeshMount::DualHands;
	ImportedHeldMeshComponent->SetStaticMesh(SelectedWeaponMesh);
	ImportedHeldMeshComponent->SetVisibility(bShowImportedMesh, true);
	if (ImportedLeftHeldMeshComponent)
	{
		// Clear the left copy whenever it is not in use so a switch never leaves a ghost.
		ImportedLeftHeldMeshComponent->SetStaticMesh(bShowLeftMesh ? SelectedWeaponMesh : nullptr);
		ImportedLeftHeldMeshComponent->SetVisibility(bShowLeftMesh, true);
	}
	if (!bShowImportedMesh)
	{
		// A hidden/meshless weapon must not retain the fitted world-space pose.
		ImportedHeldMeshComponent->SetAbsolute(false, false, true);
		return;
	}

	AttachHeldMesh(ImportedHeldMeshComponent, *ActiveWeapon, TaurusWeaponMountBone, TEXT("WeaponMount"));
	if (bShowLeftMesh)
	{
		AttachHeldMesh(ImportedLeftHeldMeshComponent, *ActiveWeapon, TaurusWeaponMountLeftBone, TEXT("WeaponMount_L"));
	}

	// Any weapon-owned mesh replaces its category Cube. An unset mesh leaves that fallback intact.
	if (USceneComponent* LegacyGroup = GetGroup(DisplayedKind))
	{
		LegacyGroup->SetVisibility(false, true);
	}
}

void UMSWeaponVisualComponent::SetHeatSwordMode(const EMSHeatSwordMode NewMode)
{
	HeatSwordMode = NewMode;
	RefreshHeatSwordMeshes();
}

void UMSWeaponVisualComponent::RefreshHeatSwordMeshes()
{
	if (!RightHeatSwordComponent || !LeftHeatSwordComponent || !RigComponent)
	{
		return;
	}

	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	UPoseableMeshComponent* Poseable = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
	const UMSWeaponDataBase* ActiveWeapon = GetActiveWeaponData();
	UStaticMesh* WeaponMesh = ActiveWeapon
		? ActiveWeapon->WeaponMesh.LoadSynchronous() : nullptr;
	const bool bMeleeVisible = Poseable && DisplayedKind == EMSWeaponVisualKind::Melee
		&& HeatSwordMode == EMSHeatSwordMode::Dual && WeaponMesh;
	RightHeatSwordComponent->SetStaticMesh(WeaponMesh);
	LeftHeatSwordComponent->SetStaticMesh(WeaponMesh);

	RightHeatSwordComponent->SetVisibility(bMeleeVisible, true);
	LeftHeatSwordComponent->SetVisibility(
		bMeleeVisible && HeatSwordMode == EMSHeatSwordMode::Dual, true);
	if (!bMeleeVisible)
	{
		return;
	}

	AttachHeatSword(RightHeatSwordComponent, TaurusWeaponMountBone, TEXT("WeaponMount"));
	AttachHeatSword(LeftHeatSwordComponent, TaurusWeaponMountLeftBone, TEXT("WeaponMount_L"));
	if (MeleeGroup)
	{
		MeleeGroup->SetVisibility(false, true);
	}
}

void UMSWeaponVisualComponent::AttachHeatSword(UStaticMeshComponent* SwordComponent,
	const FName BoneName, const FName RigJointName)
{
	AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	UPoseableMeshComponent* Poseable = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
	if (!SwordComponent || !Poseable || Poseable->GetBoneIndex(BoneName) == INDEX_NONE)
	{
		return;
	}

	SwordComponent->SetAbsolute(false, false, true);
	SwordComponent->AttachToComponent(Poseable, FAttachmentTransformRules::KeepRelativeTransform, BoneName);
	const FQuat BoneWorld = Poseable->GetBoneQuaternion(BoneName, EBoneSpaces::WorldSpace);
	const USceneComponent* GhostMount = RigComponent ? RigComponent->GetJoint(RigJointName) : nullptr;
	const FVector BladeDirection = GhostMount
		? GhostMount->GetForwardVector() : GetOwner()->GetActorForwardVector();
	const FQuat DesiredWorld =
		FRotationMatrix::MakeFromZX(BladeDirection, GetOwner()->GetActorUpVector()).ToQuat();
	SwordComponent->SetRelativeLocationAndRotation(
		FVector::ZeroVector, BoneWorld.Inverse() * DesiredWorld);

	const FVector MeshSize = SwordComponent->GetStaticMesh()
		? SwordComponent->GetStaticMesh()->GetBounds().BoxExtent * 2.0f : FVector::ZeroVector;
	const float ScaleToNineMeters = MeshSize.Z > KINDA_SMALL_NUMBER ? 900.5f / MeshSize.Z : 1.0f;
	SwordComponent->SetRelativeScale3D(FVector(ScaleToNineMeters));
}

void UMSWeaponVisualComponent::HandleWeaponSwitched(const int32 SlotIndex)
{
	RefreshFromWeaponComponent();
}

bool UMSWeaponVisualComponent::IsHeadVulcanVisible() const
{
	return (HeadVulcanGroup && HeadVulcanGroup->IsVisible())
		|| (ImportedHeadVulcanMeshComponent && ImportedHeadVulcanMeshComponent->IsVisible());
}

bool UMSWeaponVisualComponent::IsShoulderPodVisible() const
{
	return (ShoulderPodGroup && ShoulderPodGroup->IsVisible())
		|| (ImportedShoulderPodMeshComponent && ImportedShoulderPodMeshComponent->IsVisible());
}

bool UMSWeaponVisualComponent::IsShieldVisible() const
{
	return (ShieldGroup && ShieldGroup->IsVisible())
		|| (ImportedShieldMeshComponent && ImportedShieldMeshComponent->IsVisible())
		|| IsBrokenShieldVisible();
}

bool UMSWeaponVisualComponent::IsBrokenShieldVisible() const
{
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSShieldComponent* Shield = Unit ? Unit->GetShieldComponent() : nullptr;
	return Shield && Shield->IsShieldEquipped() && Shield->IsShieldBroken()
		&& MSShieldPresentation::IsBrokenAppearanceVisible(ImportedShieldMeshComponent);
}

bool UMSWeaponVisualComponent::HasEquippedNativeShield() const
{
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSShieldComponent* Shield = Unit ? Unit->GetShieldComponent() : nullptr;
	return Shield && Shield->IsShieldEquipped();
}

bool UMSWeaponVisualComponent::IsImportedHeldMeshVisible() const
{
	return ImportedHeldMeshComponent && ImportedHeldMeshComponent->IsVisible();
}

const UMSWeaponDataBase* UMSWeaponVisualComponent::GetActiveWeaponData() const
{
	return WeaponComponent ? WeaponComponent->GetWeaponData(WeaponComponent->GetActiveSlotIndex()) : nullptr;
}

const USceneComponent* UMSWeaponVisualComponent::GetWeaponMount() const
{
	return RigComponent ? RigComponent->GetJoint(TEXT("WeaponMount")) : nullptr;
}

FVector UMSWeaponVisualComponent::GetMuzzleWorldLocation() const
{
	const UMSWeaponDataBase* ActiveWeapon = GetActiveWeaponData();
	const USceneComponent* WeaponMount = GetWeaponMount();
	FVector ImportedMuzzleLocation = FVector::ZeroVector;
	FVector ImportedMuzzleDirection = FVector::ZeroVector;
	if (TryGetImportedMuzzleSocket(ImportedHeldMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleLocation;
	}
	const bool bActiveShoulderPod = ActiveWeapon && ShoulderPodWeapon.IsValid()
		&& ActiveWeapon == ShoulderPodWeapon.Get();
	if (bActiveShoulderPod && TryGetImportedMuzzleSocket(ImportedShoulderPodMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleLocation;
	}
	const bool bActiveHeadVulcan = ActiveWeapon && HeadVulcanWeapon.IsValid()
		&& ActiveWeapon == HeadVulcanWeapon.Get();
	if (bActiveHeadVulcan && TryGetImportedMuzzleSocket(ImportedHeadVulcanMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleLocation;
	}
	// This one legacy override was authored as ghost WeaponMount +X before the
	// displayed TAU-rus FBX was attached to the poseable weapon_mount_r bone.
	// Its barrel is imported along local +Z, so resolve the tip from that mesh.
	if (UsesTaurusChargeBlasterLegacyMuzzle(ActiveWeapon, ImportedHeldMeshComponent))
	{
		return ImportedHeldMeshComponent->GetComponentTransform().TransformPosition(
			FVector::UpVector * ActiveWeapon->MuzzleOffsetOverride.X);
	}
	if (ActiveWeapon && ActiveWeapon->bOverrideMuzzleTransform && WeaponMount)
	{
		return WeaponMount->GetComponentTransform().TransformPosition(ActiveWeapon->MuzzleOffsetOverride);
	}

	const USceneComponent* Group = GetGroup(DisplayedKind);
	float ForwardOffset = 95.0f;
	if (DisplayedKind == EMSWeaponVisualKind::MachineGun) ForwardOffset = 96.0f;
	else if (DisplayedKind == EMSWeaponVisualKind::Bazooka) ForwardOffset = 110.0f;
	else if (DisplayedKind == EMSWeaponVisualKind::Melee) ForwardOffset = 142.0f;
	if (Group)
	{
		return Group->GetComponentTransform().TransformPosition(FVector(ForwardOffset, 0, 0));
	}
	if (HeadVulcanGroup && (HeadVulcanGroup->IsVisible()
		|| (bActiveHeadVulcan && ImportedHeadVulcanMeshComponent
			&& ImportedHeadVulcanMeshComponent->IsVisible())))
	{
		return HeadVulcanGroup->GetComponentTransform().TransformPosition(FVector(32, 0, 0));
	}
	if (ShoulderPodGroup && (ShoulderPodGroup->IsVisible()
		|| (bActiveShoulderPod && ImportedShoulderPodMeshComponent
			&& ImportedShoulderPodMeshComponent->IsVisible())))
	{
		return ShoulderPodGroup->GetComponentTransform().TransformPosition(FVector(44, 0, 14));
	}
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FVector UMSWeaponVisualComponent::GetMuzzleWorldDirection(const FVector& DefaultWorldDirection) const
{
	const UMSWeaponDataBase* ActiveWeapon = GetActiveWeaponData();
	const USceneComponent* WeaponMount = GetWeaponMount();
	FVector ImportedMuzzleLocation = FVector::ZeroVector;
	FVector ImportedMuzzleDirection = FVector::ZeroVector;
	if (TryGetImportedMuzzleSocket(ImportedHeldMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleDirection;
	}
	const bool bActiveShoulderPod = ActiveWeapon && ShoulderPodWeapon.IsValid()
		&& ActiveWeapon == ShoulderPodWeapon.Get();
	if (bActiveShoulderPod && TryGetImportedMuzzleSocket(ImportedShoulderPodMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleDirection;
	}
	const bool bActiveHeadVulcan = ActiveWeapon && HeadVulcanWeapon.IsValid()
		&& ActiveWeapon == HeadVulcanWeapon.Get();
	if (bActiveHeadVulcan && TryGetImportedMuzzleSocket(ImportedHeadVulcanMeshComponent,
		ImportedMuzzleLocation, ImportedMuzzleDirection))
	{
		return ImportedMuzzleDirection;
	}
	if (UsesTaurusChargeBlasterLegacyMuzzle(ActiveWeapon, ImportedHeldMeshComponent))
	{
		return ImportedHeldMeshComponent->GetUpVector().GetSafeNormal();
	}
	if (ActiveWeapon && ActiveWeapon->bOverrideMuzzleTransform && WeaponMount)
	{
		FVector LocalDirection = ActiveWeapon->MuzzleDirectionOverride.GetSafeNormal();
		if (LocalDirection.IsNearlyZero())
		{
			LocalDirection = FVector::ForwardVector;
		}
		return WeaponMount->GetComponentTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
	}
	return DefaultWorldDirection.GetSafeNormal();
}
