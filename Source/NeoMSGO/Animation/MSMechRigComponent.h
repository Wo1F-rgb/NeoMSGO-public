// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSMechRigComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Runtime-only Cube mobile-suit rig. No project uasset is required. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSMechRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSMechRigComponent();
	virtual void OnRegister() override;

	UFUNCTION(BlueprintPure, Category="MS|Visual|Rig")
	USceneComponent* GetJoint(FName PartName) const;
	/** Immutable authored offset, used when turning the rig's flat root joints together. */
	FVector GetJointRestRelativeLocation(FName PartName) const;

	UFUNCTION(BlueprintPure, Category="MS|Visual|Rig")
	UStaticMeshComponent* GetPart(FName PartName) const;

	UFUNCTION(BlueprintPure, Category="MS|Visual|Rig")
	FTransform GetJointTransform(FName PartName) const;

	UFUNCTION(BlueprintCallable, Category="MS|Visual|Appearance")
	bool SetPartColor(FName PartName, FLinearColor Color);

	UFUNCTION(BlueprintPure, Category="MS|Visual|Appearance")
	FLinearColor GetPartColor(FName PartName) const;

	/** Applies Unity MechAnimator.ApplyAppearance colors and optional BOT tier decoration. */
	UFUNCTION(BlueprintCallable, Category="MS|Visual|Appearance")
	void ApplyAppearance(uint8 InTeamId, bool bIsBot, int32 EnemyTierVariant = 0);

	UFUNCTION(BlueprintPure, Category="MS|Visual|Rig")
	TArray<FName> GetPartNames() const;

	/** Set visibility for every generated rig mesh, including the head decoration. */
	void SetRigGeometryVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category="MS|Visual|Appearance")
	FName GetHeadDecorationName() const;

	USceneComponent* GetRigRoot() const { return RigRoot; }
	/** カプセル底へCubeリグの足裏を合わせた、actor空間での静止Z。 */
	float GetRigRootRestRelativeZ() const { return RigRootRestRelativeZ; }

	/** Parse an msgo-mech-motion/1 JSON document into the runtime motion set. */
	bool LoadMotionSetFromJson(const FString& JsonText, FString& OutError);

	/** Load a motion JSON document from disk. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	bool LoadMotionSetFromFile(const FString& FilePath);

	/** Load Content/Motions/TAUsw/TAUsw_HeatSword_Motions.json. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	bool LoadDefaultTAUswMotionSet();

	/** Start or resume a named motion. Restarting begins at frame zero. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	bool PlayMotion(FName MotionName, bool bRestart = true);

	/** Stop advancing the current motion. Optionally reset authored joints to identity. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	void StopMotion(bool bResetPose = false);

	/** Seek the active motion in seconds and immediately evaluate its interpolated pose. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Motion")
	bool SetMotionPlaybackPosition(float TimeSeconds);

	UFUNCTION(BlueprintPure, Category="MS|Animation|Motion")
	bool IsMotionPlaying() const { return bMotionPlaying; }

	UFUNCTION(BlueprintPure, Category="MS|Animation|Motion")
	FName GetActiveMotionName() const { return ActiveMotionName; }

	UFUNCTION(BlueprintPure, Category="MS|Animation|Motion")
	float GetMotionPlaybackPosition() const { return MotionPlaybackSeconds; }

	UFUNCTION(BlueprintPure, Category="MS|Animation|Motion")
	TArray<FName> GetAvailableMotionNames() const;

	int32 GetMotionFrameCount(FName MotionName) const;

	/** Evaluated by MeshVisual after the procedural animator and before TransferPose. */
	void UpdateMotion(float DeltaSeconds);

private:
	struct FMSMechMotion
	{
		bool bLoop = false;
		int32 FrameCount = 0;
		float DurationSeconds = 0.0f;
		TMap<FName, TArray<FQuat>> JointFrames;
	};

	void BuildRig();
	bool EvaluateActiveMotion();
	void ResetMotionJoints(const FMSMechMotion& Motion);
	void AddHeadDecoration(FName DecorationName, const FVector& RelativeLocation,
		const FRotator& RelativeRotation, const FVector& CubeDimensionsCm, const FLinearColor& Color);
	void RemoveHeadDecoration();

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> RigRoot;
	float RigRootRestRelativeZ = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> OwnedJoints;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> OwnedParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeadDecoration;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ArmorMaterial;

	TMap<FName, USceneComponent*> JointMap;
	TMap<FName, UStaticMeshComponent*> PartMap;
	TMap<FName, FLinearColor> PartColors;
	TMap<FName, FMSMechMotion> Motions;
	FName ActiveMotionName = NAME_None;
	float MotionPlaybackSeconds = 0.0f;
	float MotionFramesPerSecond = 30.0f;
	bool bRigGeometryVisible = true;
	bool bMotionPlaying = false;
	bool bRigBuilt = false;
};
