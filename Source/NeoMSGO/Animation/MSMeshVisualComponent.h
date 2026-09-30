// Copyright NeoMSGO Project. All Rights Reserved.
//
// TAURUS実メッシュ試験導入(docs/TAURUS_MeshTrial_Plan.md)。
// 方式: ゴーストリグ共存 — Cubeリグ17関節は従来どおり生成・駆動を継続し、
// VisualMesh指定機体のみ表示Cubeを非表示化して、関節回転を毎Tick
// UPoseableMeshComponentのボーンへ読み取り専用で転写する。
// 通常武器ビジュアル/銃口はゴースト関節を使用する。個別対応済みの実武器は
// UMSWeaponVisualComponentが実メッシュのweapon_mount_rへアタッチできる。
// スラスターはthruster_*ボーンがあれば転写後のWorld姿勢、なければゴーストBackpackを使用する。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSMeshVisualComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class UMSMechAnimatorComponent;
class UMSMechRigComponent;
class UMSMobHandVisuals;
class UMSMobileSuitData;
class UPoseableMeshComponent;
class USkeletalMesh;

/**
 * 実メッシュ表示コンポーネント。MobileSuitData->VisualMesh が有効な場合のみ
 * PoseableMeshを生成し、Cubeリグのポーズをボーンへ転写する。
 * 未設定機体では一切何もしない(従来と完全一致)。
 */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSMeshVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSMeshVisualComponent();

	/** InitializeUnit経路の単一入口。VisualMesh未設定ならメッシュを破棄しCube表示へ戻す。 */
	void InitializeFromMobileSuit(const UMSMobileSuitData* Data);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** メッシュモードが有効か(テスト用) */
	bool IsMeshVisualActive() const { return PoseableMesh != nullptr; }

	UPoseableMeshComponent* GetPoseableMesh() const { return PoseableMesh; }
	/** Optional MobLight static hand overlays; null/empty for MPU and TAU paths. */
	UFUNCTION(BlueprintPure, Category="MS|Visual")
	UMSMobHandVisuals* GetMobHandVisuals() const { return MobHandVisuals; }
	bool HasCrouchContactProfile() const { return !CrouchContacts.IsEmpty(); }
	double GetCrouchContactMinWorldZ() const;

	/** Sample either real hand's skeleton without changing the rendered pose. */
	bool SampleStandardSaberMountWorld(int32 AttackIndex, float TimeSeconds, FTransform& OutMount, bool bLeft = false) const;

	/** Applies the current ghost-rig pose to the bones now (same path as Tick). */
	void SyncPoseToRig() { TransferPose(); }

	/** 転写を1回明示実行(Automationテスト用。Tickと同一経路) */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	void TransferPoseForAutomationTests() { TransferPose(); }

	/** 単色フォールバック時に適用した色(テスト用) */
	FLinearColor GetAppliedColorForAutomationTests() const { return AppliedColor; }

	/** Imported materials may expose these vector parameters for team and role tinting. */
	static const FName TeamPrimaryParameter;
	static const FName RoleAccentParameter;
	static const FName SensorColorParameter;

	/**
	 * Gives every slot whose material exposes MSTeamPrimary or MSRoleAccent a MID with those
	 * colors. Slots without the parameters keep their imported material unchanged (MPU01/TAU).
	 */
	static TArray<UMaterialInstanceDynamic*> ApplyAppearanceParameters(UMeshComponent& Mesh, UObject* Outer,
		const FLinearColor& TeamPrimary, const FLinearColor& RoleAccent,
		const FLinearColor& SensorColor = FLinearColor(0.05f, 0.9f, 0.28f));

	/** Role color (UnitType, same palette as the hangar preview) muted for armor accents. */
	static FLinearColor GetRoleAccentColor(const UMSMobileSuitData* Data);

	int32 GetAppearanceMaterialCountForAutomationTests() const { return AppearanceMaterials.Num(); }
	UMaterialInstanceDynamic* GetAppearanceMaterialForAutomationTests(const int32 Index) const
	{
		return AppearanceMaterials.IsValidIndex(Index) ? AppearanceMaterials[Index].Get() : nullptr;
	}
	void RefreshAppearanceForAutomationTests() { RefreshAppearanceParameters(); }
	FLinearColor GetAppliedRoleAccentForAutomationTests() const { return AppliedRoleAccent; }

private:
	void BuildMeshVisual(const UMSMobileSuitData* Data);
	void DestroyMeshVisual();
	void SetRigCubesVisible(bool bVisible);
	void CacheBindPose();
	void TransferPose();
	void LoadCrouchContacts(const USkeletalMesh& Mesh);
	void RestoreCrouchContactRoot();
	void ApplyCrouchContact();
	/** Follows later team/tier appearance changes (e.g. SetGenericTeamId after InitializeUnit). */
	void RefreshAppearanceParameters();

	/** RigRoot空間の転写回転をボーンのコンポーネント空間回転へ適用 */
	void ApplyBoneRotation(const FName& BoneName, const FQuat& RigSpaceRotation);

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> PoseableMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> AppearanceMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMSMobHandVisuals> MobHandVisuals;
	FLinearColor AppliedRoleAccent = FLinearColor::White;
	FLinearColor AppliedSensorColor = FLinearColor::Transparent;

	TWeakObjectPtr<UMSMechRigComponent> Rig;
	TMap<FName, FQuat> BindComponentRotations;
	FQuat MeshRelativeQuat = FQuat::Identity;
	FLinearColor AppliedColor = FLinearColor::White;
	bool bBindPoseCached = false;
	struct FCrouchContactBone
	{
		FName Bone;
		TArray<FVector> Points;
	};
	TArray<FCrouchContactBone> CrouchContacts;
	double ReferenceContactMinZ = 0.0;
	bool bCrouchContactApplied = false;
};
