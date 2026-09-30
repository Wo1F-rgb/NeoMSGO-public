// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSMeshVisualComponent.h"

#include "Animation/MSMechAnimatorComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamTypes.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMobHandVisuals.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Components/PoseableMeshComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/SkeletalMesh.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponComponent.h"

namespace
{
	// Cubeリグ実寸(MSMechRigComponent RigDefinitionsより): 足裏はRigRoot基準Z=-93.5cm
	constexpr float MeshRigRootToSoleCm = -93.5f;

	FQuat JointRelativeQuat(const UMSMechRigComponent& Rig, const FName JointName)
	{
		const USceneComponent* Joint = Rig.GetJoint(JointName);
		return Joint ? Joint->GetRelativeRotation().Quaternion() : FQuat::Identity;
	}

	bool HasImportedMaterial(const USkeletalMesh& Mesh)
	{
		const UMaterialInterface* DefaultSurface = UMaterial::GetDefaultMaterial(MD_Surface);
		for (const FSkeletalMaterial& Slot : Mesh.GetMaterials())
		{
			const UMaterialInterface* Material = Slot.MaterialInterface;
			if (Material && Material != DefaultSurface &&
				Material->GetPathName() != TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"))
			{
				return true;
			}
		}
		return false;
	}

	bool HasVectorParameter(const UMaterialInterface* Material, const FName Name)
	{
		FLinearColor Value;
		return Material && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Value);
	}
}

const FName UMSMeshVisualComponent::TeamPrimaryParameter(TEXT("MSTeamPrimary"));
const FName UMSMeshVisualComponent::RoleAccentParameter(TEXT("MSRoleAccent"));
const FName UMSMeshVisualComponent::SensorColorParameter(TEXT("MSSensorColor"));

TArray<UMaterialInstanceDynamic*> UMSMeshVisualComponent::ApplyAppearanceParameters(UMeshComponent& Mesh,
	UObject* Outer, const FLinearColor& TeamPrimary, const FLinearColor& RoleAccent, const FLinearColor& SensorColor)
{
	TArray<UMaterialInstanceDynamic*> Result;
	for (int32 SlotIndex = 0; SlotIndex < Mesh.GetNumMaterials(); ++SlotIndex)
	{
		UMaterialInterface* Material = Mesh.GetMaterial(SlotIndex);
		if (!HasVectorParameter(Material, TeamPrimaryParameter) && !HasVectorParameter(Material, RoleAccentParameter)
			&& !HasVectorParameter(Material, SensorColorParameter))
		{
			continue;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Material, Outer);
		Mid->SetVectorParameterValue(TeamPrimaryParameter, TeamPrimary);
		Mid->SetVectorParameterValue(RoleAccentParameter, RoleAccent);
		Mid->SetVectorParameterValue(SensorColorParameter, SensorColor);
		Mesh.SetMaterial(SlotIndex, Mid);
		Result.Add(Mid);
	}
	return Result;
}

FLinearColor UMSMeshVisualComponent::GetRoleAccentColor(const UMSMobileSuitData* Data)
{
	// Game armor keeps saturated role colors; the neutral grey palette belongs only to the workshop.
	return UMSMobileSuitPreviewRenderer::GetCategoryColor(Data);
}

UMSMeshVisualComponent::UMSMeshVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMSMeshVisualComponent::InitializeFromMobileSuit(const UMSMobileSuitData* Data)
{
	if (!Rig.IsValid())
	{
		Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	}
	if (!Data || Data->VisualMesh.IsNull() || !Rig.IsValid() || !Rig->GetRigRoot())
	{
		DestroyMeshVisual();
		return;
	}
	BuildMeshVisual(Data);
}

void UMSMeshVisualComponent::BuildMeshVisual(const UMSMobileSuitData* Data)
{
	RestoreCrouchContactRoot();
	CrouchContacts.Reset();
	USkeletalMesh* Mesh = Data->VisualMesh.LoadSynchronous();
	if (!Mesh)
	{
		DestroyMeshVisual();
		return;
	}

	if (!PoseableMesh)
	{
		PoseableMesh = NewObject<UPoseableMeshComponent>(GetOwner(), TEXT("MeshVisualPoseable"));
		PoseableMesh->SetupAttachment(Rig->GetRigRoot());
		PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PoseableMesh->SetGenerateOverlapEvents(false);
		PoseableMesh->SetCanEverAffectNavigation(false);
		PoseableMesh->RegisterComponent();
	}
	if (PoseableMesh->GetSkinnedAsset() && PoseableMesh->GetSkinnedAsset() != Mesh)
	{
		// UE 5.7's Poseable AllocateTransformData only replaces BoneSpaceTransforms
		// and RequiredBones when the bone COUNT changes; bReinitPose is ignored by
		// the base SetSkinnedAssetAndUpdate. TAU/Mob/MPU share counts but have different
		// bind lengths/scales. Clear through the public API before changing assets so
		// a reused player never keeps the previous suit's local skeleton geometry.
		PoseableMesh->SetSkinnedAssetAndUpdate(nullptr, true);
	}
	PoseableMesh->SetSkinnedAssetAndUpdate(Mesh, true);
	// A reused component must not keep another suit's per-slot material overrides.
	PoseableMesh->EmptyOverrideMaterials();

	// 自動フィット: RigRootの共通倍率を含むワールドBounds高さ→目標全高。
	// 親倍率を除算し、Cube表示と実メッシュ表示のどちらも1800UUに揃える。
	const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
	const float MeshHeight = Bounds.BoxExtent.Z * 2.0f;
	const float RigWorldScaleZ = FMath::Abs(Rig->GetRigRoot()->GetComponentScale().Z);
	const float Scale = MeshHeight > KINDA_SMALL_NUMBER
		? Data->VisualMeshTargetHeightCm / (MeshHeight * FMath::Max(RigWorldScaleZ, UE_SMALL_NUMBER))
		: 1.0f;
	PoseableMesh->SetRelativeScale3D(FVector(Scale));

	// 接地: メッシュ下端(ローカル)を足裏(RigRoot基準-93.5cm)へ合わせる
	const float BottomLocal = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
	const float ZOffset = MeshRigRootToSoleCm - BottomLocal;
	const FRotator YawOffset(0.0f, Data->VisualMeshYawOffsetDeg, 0.0f);
	PoseableMesh->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, ZOffset), YawOffset);
	MeshRelativeQuat = YawOffset.Quaternion();

	// FBX由来の有効なマテリアルが1つでもあれば、全スロットのインポート割当をそのまま使う。
	// null/エンジン既定マテリアルしかない旧メッシュだけ、従来のチーム色MIDへフォールバックする。
	MeshMaterials.Reset();
	AppearanceMaterials.Reset();
	if (HasImportedMaterial(*Mesh))
	{
		AppliedColor = Rig->GetPartColor(TEXT("Torso"));
		AppliedRoleAccent = GetRoleAccentColor(Data);
		if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()); Unit && Unit->GetGenericTeamId().GetId() == MSTeam::TeamEnemy)
			AppliedRoleAccent = AppliedColor * 0.55f;
		for (UMaterialInstanceDynamic* Mid : ApplyAppearanceParameters(*PoseableMesh, this, AppliedColor, AppliedRoleAccent))
		{
			AppearanceMaterials.Add(Mid);
		}
	}
	else
	{
		AppliedColor = Rig->GetPartColor(TEXT("Torso"));
		AppliedRoleAccent = GetRoleAccentColor(Data);
		if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner()); Unit && Unit->GetGenericTeamId().GetId() == MSTeam::TeamEnemy)
			AppliedRoleAccent = AppliedColor * 0.55f;
		static const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
		if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath))
		{
			const int32 NumSlots = PoseableMesh->GetNumMaterials();
			for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMaterial, this);
				Mid->SetVectorParameterValue(TEXT("Color"), AppliedColor);
				PoseableMesh->SetMaterial(SlotIndex, Mid);
				MeshMaterials.Add(Mid);
			}
		}
	}

	SetRigCubesVisible(false);
	PoseableMesh->SetVisibility(true, true);
	if (!MobHandVisuals)
	{
		MobHandVisuals = NewObject<UMSMobHandVisuals>(GetOwner(), TEXT("MobLightHandVisuals"));
	}
	if (MobHandVisuals)
	{
		UMSWeaponComponent* Weapons = GetOwner()
			? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
		MobHandVisuals->Initialize(GetOwner(), PoseableMesh, Rig.Get(), Weapons, Mesh);
		MobHandVisuals->SetAppearance(AppliedColor, AppliedRoleAccent);
	}

	bBindPoseCached = false;
	CacheBindPose();
	LoadCrouchContacts(*Mesh);

	// 転写はMechAnimatorのApplyPoseToRig後に実行する(同フレーム順序保証)
	if (UMSMechAnimatorComponent* Animator =
		GetOwner() ? GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>() : nullptr)
	{
		AddTickPrerequisiteComponent(Animator);
		Animator->RefreshThrusterVisuals();
	}
	if (Mesh->GetFName() == FName(TEXT("SK_TAUsw"))
		&& Rig.IsValid() && Rig->GetAvailableMotionNames().IsEmpty())
	{
		Rig->LoadDefaultTAUswMotionSet();
	}
	SetComponentTickEnabled(true);
}

void UMSMeshVisualComponent::DestroyMeshVisual()
{
	RestoreCrouchContactRoot();
	CrouchContacts.Reset();
	if (MobHandVisuals)
	{
		MobHandVisuals->Reset();
	}
	if (PoseableMesh)
	{
		PoseableMesh->DestroyComponent();
		PoseableMesh = nullptr;
		MeshMaterials.Reset();
		AppearanceMaterials.Reset();
		SetRigCubesVisible(true);
	}
	SetComponentTickEnabled(false);
	bBindPoseCached = false;
	if (UMSMechAnimatorComponent* Animator =
		GetOwner() ? GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>() : nullptr)
	{
		Animator->RefreshThrusterVisuals();
	}
}

void UMSMeshVisualComponent::SetRigCubesVisible(const bool bVisible)
{
	if (!Rig.IsValid())
	{
		return;
	}
	Rig->SetRigGeometryVisible(bVisible);
}

void UMSMeshVisualComponent::CacheBindPose()
{
	if (!PoseableMesh || bBindPoseCached)
	{
		return;
	}
	// 実プレイ報告「関節が曲がった状態でスポーンし、しゃがみでさらに曲がる」対策:
	// 生成直後のGetBoneRotationByName実測はボーン変換の初期化タイミング次第で
	// 曲がったポーズを拾い、以後の全転写がそのオフセット分ズレる。ライブ実測ではなく
	// SkeletalMeshアセットのリファレンススケルトンから絶対バインドポーズを合成する。
	BindComponentRotations.Reset();
	const USkeletalMesh* SkeletalAsset = Cast<USkeletalMesh>(PoseableMesh->GetSkinnedAsset());
	if (!SkeletalAsset)
	{
		return;
	}
	const FReferenceSkeleton& RefSkeleton = SkeletalAsset->GetRefSkeleton();
	const TArray<FTransform>& LocalRefPose = RefSkeleton.GetRefBonePose();
	const int32 NumBones = RefSkeleton.GetNum();
	TArray<FQuat> ComponentSpaceRotations;
	ComponentSpaceRotations.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FQuat LocalQuat = LocalRefPose[BoneIndex].GetRotation();
		ComponentSpaceRotations[BoneIndex] = ParentIndex != INDEX_NONE
			? ComponentSpaceRotations[ParentIndex] * LocalQuat
			: LocalQuat;
		BindComponentRotations.Add(RefSkeleton.GetBoneName(BoneIndex), ComponentSpaceRotations[BoneIndex]);
	}
	bBindPoseCached = NumBones > 0;
}

void UMSMeshVisualComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Rig.IsValid())
	{
		// Authored motion overrides the procedural arm pose immediately before transfer.
		Rig->UpdateMotion(DeltaTime);
	}
	TransferPose();
	RefreshAppearanceParameters();
	if (UMSMechAnimatorComponent* Animator =
		GetOwner() ? GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>() : nullptr)
	{
		Animator->UpdateThrusterEffectsAfterMeshPose(DeltaTime);
	}
}

void UMSMeshVisualComponent::RefreshAppearanceParameters()
{
	if (!Rig.IsValid())
	{
		return;
	}
	const FLinearColor TeamPrimary = Rig->GetPartColor(TEXT("Torso"));
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const bool bEnemy = Unit && Unit->GetGenericTeamId().GetId() == MSTeam::TeamEnemy;
	const FLinearColor Accent = bEnemy ? Rig->GetPartColor(TEXT("Backpack"))
		: GetRoleAccentColor(Unit ? Unit->GetMobileSuitData() : nullptr);
	const FLinearColor Sensor = bEnemy ? FLinearColor(1.0f, 0.025f, 0.22f) : FLinearColor(0.05f, 0.9f, 0.28f);
	const bool bSensorChanged = !Sensor.Equals(AppliedSensorColor);
	AppliedSensorColor = Sensor;
	const bool bAccentChanged = !Accent.Equals(AppliedRoleAccent);
	AppliedRoleAccent = Accent;
	if (AppearanceMaterials.IsEmpty())
	{
		if (MobHandVisuals)
		{
			MobHandVisuals->SetAppearance(TeamPrimary, AppliedRoleAccent);
		}
		return;
	}
	if (TeamPrimary.Equals(AppliedColor) && !bAccentChanged && !bSensorChanged)
	{
		if (MobHandVisuals)
		{
			MobHandVisuals->SetAppearance(AppliedColor, AppliedRoleAccent);
		}
		return;
	}
	AppliedColor = TeamPrimary;
	for (UMaterialInstanceDynamic* Mid : AppearanceMaterials)
	{
		if (Mid)
		{
			Mid->SetVectorParameterValue(TeamPrimaryParameter, TeamPrimary);
			Mid->SetVectorParameterValue(RoleAccentParameter, AppliedRoleAccent);
			Mid->SetVectorParameterValue(SensorColorParameter, AppliedSensorColor);
		}
	}
	if (MobHandVisuals)
	{
		MobHandVisuals->SetAppearance(AppliedColor, AppliedRoleAccent);
	}
}

void UMSMeshVisualComponent::TransferPose()
{
	if (!PoseableMesh || !Rig.IsValid() || !bBindPoseCached)
	{
		return;
	}
	const UMSMechRigComponent& RigRef = *Rig.Get();
	RestoreCrouchContactRoot();

	// 対応表(docs/TAURUS_MeshTrial_Design.md、新FBXの実ボーン名):
	//  Head→head / Torso→chest / Hip→pelvis / Shoulder→shoulder_l,r
	//  UpperArm→upperarm_l,r / Forearm→forearm_l,r
	//  Thigh→thigh_l,r / Shin→shin_l,r / Thigh∘Shin∘Foot(累積)→foot_l,r
	//  UpperArm_R∘Forearm_R∘WeaponMount(累積)→weapon_mount_r。Backpackは転写なし。
	// Cubeリグは腕・脚関節がRigRoot直下のフラット階層のため、相対回転=RigRoot空間回転。
	// 親(chest等)の影響を受けないよう、全ボーンをComponentSpaceで親優先順に上書きする。
	const FQuat TorsoQ = JointRelativeQuat(RigRef, TEXT("Torso"));
	const FQuat HipQ = JointRelativeQuat(RigRef, TEXT("Hip"));
	const FQuat HeadQ = JointRelativeQuat(RigRef, TEXT("Head"));

	ApplyBoneRotation(TEXT("pelvis"), HipQ);
	ApplyBoneRotation(TEXT("chest"), TorsoQ);
	ApplyBoneRotation(TEXT("head"), HeadQ);

	const struct { const TCHAR* Side; const TCHAR* Suffix; } Sides[] = {
		{TEXT("_L"), TEXT("_l")}, {TEXT("_R"), TEXT("_r")}};
	for (const auto& S : Sides)
	{
		const FQuat ShoulderQ = JointRelativeQuat(RigRef, FName(FString(TEXT("Shoulder")) + S.Side));
		const FQuat UpperQ = JointRelativeQuat(RigRef, FName(FString(TEXT("UpperArm")) + S.Side));
		const FQuat ForeQ = JointRelativeQuat(RigRef, FName(FString(TEXT("Forearm")) + S.Side));
		const FQuat ThighQ = JointRelativeQuat(RigRef, FName(FString(TEXT("Thigh")) + S.Side));
		const FQuat ShinQ = JointRelativeQuat(RigRef, FName(FString(TEXT("Shin")) + S.Side));
		const FQuat FootQ = JointRelativeQuat(RigRef, FName(FString(TEXT("Foot")) + S.Side));

		ApplyBoneRotation(FName(FString(TEXT("shoulder")) + S.Suffix), ShoulderQ);
		ApplyBoneRotation(FName(FString(TEXT("upperarm")) + S.Suffix), UpperQ);
		ApplyBoneRotation(FName(FString(TEXT("forearm")) + S.Suffix), UpperQ * ForeQ);
		ApplyBoneRotation(FName(FString(TEXT("thigh")) + S.Suffix), ThighQ);
		ApplyBoneRotation(FName(FString(TEXT("shin")) + S.Suffix), ThighQ * ShinQ);
		ApplyBoneRotation(FName(FString(TEXT("foot")) + S.Suffix), ThighQ * ShinQ * FootQ);

	}

	const FQuat UpperQ_R = JointRelativeQuat(RigRef, TEXT("UpperArm_R"));
	const FQuat ForeQ_R = JointRelativeQuat(RigRef, TEXT("Forearm_R"));
	const FQuat WeaponMountQ_R = JointRelativeQuat(RigRef, TEXT("WeaponMount"));
	ApplyBoneRotation(TEXT("weapon_mount_r"), UpperQ_R * ForeQ_R * WeaponMountQ_R);

	const FQuat UpperQ_L = JointRelativeQuat(RigRef, TEXT("UpperArm_L"));
	const FQuat ForeQ_L = JointRelativeQuat(RigRef, TEXT("Forearm_L"));
	const FQuat WeaponMountQ_L = JointRelativeQuat(RigRef, TEXT("WeaponMount_L"));
	ApplyBoneRotation(TEXT("weapon_mount_l"), UpperQ_L * ForeQ_L * WeaponMountQ_L);
	// SetBoneRotationByName only marks a pending refresh. Publish the completed pose
	// and update socket-attached weapons now, before this frame's muzzle/thruster reads.
	PoseableMesh->RefreshBoneTransforms();
	if (MobHandVisuals)
	{
		MobHandVisuals->Update();
	}
	ApplyCrouchContact();
}

bool UMSMeshVisualComponent::SampleStandardSaberMountWorld(const int32 AttackIndex,
	const float TimeSeconds, FTransform& OutMount, const bool bLeft) const
{
	const USkeletalMesh* Mesh = PoseableMesh ? Cast<USkeletalMesh>(PoseableMesh->GetSkinnedAsset()) : nullptr;
	if (!Mesh || !bBindPoseCached || !Rig.IsValid()) return false;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 MountIndex = Ref.FindBoneIndex(bLeft ? TEXT("weapon_mount_l") : TEXT("weapon_mount_r"));
	if (MountIndex == INDEX_NONE) return false;
	TArray<int32, TInlineAllocator<16>> Chain;
	for (int32 Index = MountIndex; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index)) Chain.Add(Index);
	const FMSStandardSaberPose Pose = FMSStandardSaberMotion::Sample(AttackIndex, TimeSeconds);
	const FQuat Body = FRotator(0, Pose.BodyYaw, 0).Quaternion();
	FTransform Component = FTransform::Identity;
	for (int32 ChainIndex = Chain.Num() - 1; ChainIndex >= 0; --ChainIndex)
	{
		const int32 Index = Chain[ChainIndex];
		const FName Name = Ref.GetBoneName(Index);
		Component = Ref.GetRefBonePose()[Index] * Component;
		FQuat RigRotation;
		bool bOverride = true;
		if (Name == TEXT("pelvis")) RigRotation = AttackIndex == FMSStandardSaberMotion::DualDash
			? Body : JointRelativeQuat(*Rig, TEXT("Hip"));
		else if (Name == TEXT("chest")) RigRotation = Pose.Torso;
		else if (Name == TEXT("shoulder_r")) RigRotation = Body;
		else if (Name == TEXT("upperarm_r")) RigRotation = Pose.UpperArm;
		else if (Name == TEXT("forearm_r")) RigRotation = Pose.UpperArm * Pose.Forearm;
		else if (Name == TEXT("weapon_mount_r")) RigRotation = Pose.UpperArm * Pose.Forearm * Pose.WeaponMount;
		else if (Name == TEXT("shoulder_l")) RigRotation = Body;
		else if (Name == TEXT("upperarm_l")) RigRotation = Pose.UpperArmLeft;
		else if (Name == TEXT("forearm_l")) RigRotation = Pose.UpperArmLeft * Pose.ForearmLeft;
		else if (Name == TEXT("weapon_mount_l")) RigRotation = Pose.UpperArmLeft * Pose.ForearmLeft * Pose.WeaponMountLeft;
		else bOverride = false;
		if (const FQuat* Bind = BindComponentRotations.Find(Name); bOverride && Bind)
		{
			// Same component-space rotation rule as TransferPose; the child position
			// is propagated through this mesh's actual bind offsets and import scale.
			Component.SetRotation((MeshRelativeQuat.Inverse() * RigRotation * MeshRelativeQuat * *Bind).GetNormalized());
		}
	}
	OutMount = Component * PoseableMesh->GetComponentTransform();
	return true;
}

void UMSMeshVisualComponent::LoadCrouchContacts(const USkeletalMesh& Mesh)
{
	// Small, baked rigid contact hulls also work in cooked builds without keeping
	// render-LOD vertices on the CPU. They never change the mesh or bind pose.
	const FString Path = FPaths::ProjectContentDir() / TEXT("Motions/MeshContacts")
		/ (Mesh.GetName() + TEXT(".mscontacts"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) return;
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
	FString Format, MeshName;
	if (!Json->TryGetStringField(TEXT("format"), Format) || Format != TEXT("msgo-mesh-contacts/1")
		|| !Json->TryGetStringField(TEXT("mesh"), MeshName) || MeshName != Mesh.GetName()) return;
	auto ReadVector = [](const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (!Value.IsValid() || !Value->TryGetArray(Values) || Values->Num() != 3) return false;
		double X, Y, Z;
		if (!(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y)
			|| !(*Values)[2]->TryGetNumber(Z) || !FMath::IsFinite(X)
			|| !FMath::IsFinite(Y) || !FMath::IsFinite(Z)) return false;
		Out = FVector(X, Y, Z);
		return true;
	};
	FVector BoundsMin, BoundsMax;
	const FBoxSphereBounds Bounds = Mesh.GetImportedBounds();
	const double Tolerance = FMath::Max(0.01, Bounds.BoxExtent.GetAbsMax() * 0.001);
	if (!ReadVector(Json->TryGetField(TEXT("boundsMin")), BoundsMin)
		|| !ReadVector(Json->TryGetField(TEXT("boundsMax")), BoundsMax)
		|| !BoundsMin.Equals(Bounds.Origin - Bounds.BoxExtent, Tolerance)
		|| !BoundsMax.Equals(Bounds.Origin + Bounds.BoxExtent, Tolerance)) return;
	const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
	TArray<FTransform> BindTransforms;
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		const int32 Parent = Ref.GetParentIndex(Index);
		BindTransforms.Add(Parent == INDEX_NONE ? Ref.GetRefBonePose()[Index]
			: Ref.GetRefBonePose()[Index] * BindTransforms[Parent]);
	}
	const TArray<TSharedPtr<FJsonValue>>* Bones;
	if (!Json->TryGetArrayField(TEXT("bones"), Bones) || Bones->Num() != 4) return;
	TArray<FCrouchContactBone> Contacts;
	TSet<FName> Seen;
	double Lowest = TNumericLimits<double>::Max();
	for (const auto& Value : *Bones)
	{
		const TSharedPtr<FJsonObject>* BoneObject;
		if (!Value->TryGetObject(BoneObject)) return;
		FString Name;
		if (!(*BoneObject)->TryGetStringField(TEXT("name"), Name)) return;
		const FName BoneName(*Name);
		if (Name != TEXT("foot_l") && Name != TEXT("foot_r")
			&& Name != TEXT("shin_l") && Name != TEXT("shin_r")) return;
		if (Seen.Contains(BoneName)) return;
		Seen.Add(BoneName);
		const int32 Index = Ref.FindBoneIndex(BoneName);
		FVector ReferencePosition;
		if (Index == INDEX_NONE || !ReadVector((*BoneObject)->TryGetField(TEXT("referencePosition")), ReferencePosition)
			|| !ReferencePosition.Equals(BindTransforms[Index].GetTranslation(), Tolerance)) return;
		const TArray<TSharedPtr<FJsonValue>>* Points;
		if (!(*BoneObject)->TryGetArrayField(TEXT("points"), Points) || Points->IsEmpty() || Points->Num() > 8192) return;
		FCrouchContactBone Contact;
		Contact.Bone = BoneName;
		for (const auto& PointValue : *Points)
		{
			FVector Point;
			if (!ReadVector(PointValue, Point)) return;
			Contact.Points.Add(Point);
			Lowest = FMath::Min(Lowest, BindTransforms[Index].TransformPosition(Point).Z);
		}
		Contacts.Add(MoveTemp(Contact));
	}
	ReferenceContactMinZ = Lowest;
	CrouchContacts = MoveTemp(Contacts);
}

double UMSMeshVisualComponent::GetCrouchContactMinWorldZ() const
{
	double Lowest = TNumericLimits<double>::Max();
	if (!PoseableMesh) return Lowest;
	for (const FCrouchContactBone& Contact : CrouchContacts)
	{
		const FTransform BoneWorld = PoseableMesh->GetBoneTransformByName(Contact.Bone, EBoneSpaces::WorldSpace);
		for (const FVector& Point : Contact.Points)
			Lowest = FMath::Min(Lowest, BoneWorld.TransformPosition(Point).Z);
	}
	return Lowest;
}

void UMSMeshVisualComponent::RestoreCrouchContactRoot()
{
	if (!bCrouchContactApplied || !Rig.IsValid() || !Rig->GetRigRoot()) return;
	const UMSMechAnimatorComponent* Animator = GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>();
	if (Animator)
	{
		FVector Base = Rig->GetRigRoot()->GetRelativeLocation();
		Base.Z = Rig->GetRigRootRestRelativeZ() - Animator->GetPoseState().CrouchGroundOffset;
		Rig->GetRigRoot()->SetRelativeLocation(Base);
	}
	bCrouchContactApplied = false;
}

void UMSMeshVisualComponent::ApplyCrouchContact()
{
	if (CrouchContacts.IsEmpty() || !Rig.IsValid() || !Rig->GetRigRoot()) return;
	const UMSMechAnimatorComponent* Animator = GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>();
	if (!Animator) return;
	const auto& Inputs = Animator->GetPoseInputs();
	const auto& Pose = Animator->GetPoseState();
	if (Pose.CrouchPoseAlpha <= UE_SMALL_NUMBER || Inputs.bIsInAir || Inputs.bIsBoosting
		|| Inputs.bIsDead || (Inputs.bIsStaggered && !Inputs.bManagedHitReaction) || Inputs.bIsKnockedDown
		|| Inputs.bHasAuthoredMotion || Rig->IsMotionPlaying() || Pose.bMeleeMotionActive || Pose.bKickMotionActive) return;
	USceneComponent* Root = Rig->GetRigRoot();
	const FTransform ParentWorld = Root->GetAttachParent()
		? Root->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
	FTransform StandingRoot = Root->GetRelativeTransform();
	FVector StandingLocation = StandingRoot.GetTranslation();
	StandingLocation.Z = Rig->GetRigRootRestRelativeZ();
	StandingRoot.SetTranslation(StandingLocation);
	const FTransform StandingMesh = PoseableMesh->GetRelativeTransform() * StandingRoot * ParentWorld;
	const double GroundZ = StandingMesh.TransformPosition(FVector(0.0, 0.0, ReferenceContactMinZ)).Z;
	const double ContactZ = GetCrouchContactMinWorldZ();
	// Fade only the last fraction of stand-up so any residual walking pose cannot
	// keep correcting the root once crouch has ended. Full crouch and most of the
	// transition use the current rendered contact geometry, not an extra lag filter.
	const float ContactWeight = FMath::SmoothStep(0.0f, 0.1f, Pose.CrouchPoseAlpha);
	const FVector LocalCorrection = ParentWorld.InverseTransformVector(FVector(0.0, 0.0,
		(GroundZ - ContactZ) * ContactWeight));
	Root->SetRelativeLocation(Root->GetRelativeLocation() + LocalCorrection);
	bCrouchContactApplied = true;
}

void UMSMeshVisualComponent::ApplyBoneRotation(const FName& BoneName, const FQuat& RigSpaceRotation)
{
	const FQuat* BindQuat = BindComponentRotations.Find(BoneName);
	if (!BindQuat)
	{
		return;
	}
	// メッシュコンポーネント自体がYaw補正で回っているため、RigRoot空間回転を
	// コンポーネント空間へ共役変換してからバインドポーズへ合成する
	const FQuat ComponentSpaceRotation =
		MeshRelativeQuat.Inverse() * RigSpaceRotation * MeshRelativeQuat;
	PoseableMesh->SetBoneRotationByName(BoneName,
		(ComponentSpaceRotation * *BindQuat).Rotator(), EBoneSpaces::ComponentSpace);
}
