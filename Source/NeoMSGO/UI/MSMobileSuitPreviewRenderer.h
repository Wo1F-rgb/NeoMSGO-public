#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MSMobileSuitPreviewRenderer.generated.h"

class AActor;
class UImage;
class UMSMechRigComponent;
class UMSMobHandVisuals;
class UMaterialInstanceDynamic;
class UMSMobileSuitData;
class UPoseableMeshComponent;
class UPointLightComponent;
class USceneComponent;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UTexture2D;
class UTextureCube;

/** Runtime-only suit preview: VisualMesh when supplied, with the Cube rig as a fallback. */
UCLASS()
class NEOMSGO_API UMSMobileSuitPreviewRenderer : public UObject
{
	GENERATED_BODY()

public:
	UMSMobileSuitPreviewRenderer();
	bool Initialize(UWorld* World, UImage* TargetImage);
	/** Applies the SpawnSelect composition without changing suit selection or enabling per-frame capture. */
	void ConfigureSortieView(bool bPortrait);
	/** Portrait, front three-quarter capture with transparent pixels for the vector hangar. */
	void ConfigureCustomizeView();
	/** One coherent room and floor, rendered behind the detail controls. */
	void ConfigureHangarView();
	/** Wide upper-body capture for the four 248 px deck cards. */
	void ConfigureDeckCardView();
	/** Low, screen-left-facing chest close-up for the deployment map's short MS LIST rows. */
	void ConfigureDeploymentListView();
	void SetMobileSuit(const UMSMobileSuitData* MobileSuit, bool bShieldEquipped = true);
	bool IsInitialized() const;
	void RotatePreview(float DeltaYawDegrees);
	float GetPreviewYaw() const { return PreviewYaw; }
	void Shutdown();
	virtual void BeginDestroy() override;

	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
	AActor* GetPreviewActor() const { return PreviewActor; }
	UPoseableMeshComponent* GetVisualMeshComponent() const { return VisualMesh; }
	UMSMobHandVisuals* GetMobHandVisuals() const { return MobHandVisuals; }
	USceneCaptureComponent2D* GetCaptureComponent() const { return Capture; }
	bool IsUsingVisualMesh() const;
	bool IsNativeShieldVisible() const;
	bool AreRigPartsVisible() const;
	static FLinearColor GetCategoryColor(const UMSMobileSuitData* MobileSuit);

private:
	void BuildSortieStudio(USceneComponent* Root);
	void BuildHangarStudio(USceneComponent* Root);
	void SetHangarBackdropVisible(bool bVisible);
	void SetStudioBackdropVisible(bool bVisible);
	void SetSortieLightingEnabled(bool bEnabled);
	void SetPreviewLightingChannels(bool bSortieProfile);
	void SetSortieViewFlags(bool bSortieProfile);
	UStaticMeshComponent* AddStudioCube(USceneComponent* Root, UStaticMesh* CubeMesh,
		const FVector& DesignLocation, const FVector& DesignDimensions, const FRotator& Rotation,
		const FLinearColor& Color);
	UPointLightComponent* AddStudioLight(USceneComponent* Root, const FVector& DesignLocation,
		float DesignIntensity, float AttenuationRadius, const FLinearColor& Color);
	void CapturePreview();
	void ApplyCloseupFraming();
	enum class ECloseupProfile : uint8 { None, DeckCard, DeploymentList };
	ECloseupProfile CloseupProfile = ECloseupProfile::None;
	bool bCustomizeView = false;
	float PreviewYaw = 0.0f;
	TWeakObjectPtr<const UMSMobileSuitData> CurrentMobileSuit;
	UPROPERTY() TObjectPtr<UTextureCube> StudioAmbientCubemap;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CustomizeTexture;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> StudioBackdrop;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> HangarBackdrop;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> NativeShieldMesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> StudioMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> StudioLights;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> PreviewKeyLight;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CustomizePlatform;
	UPROPERTY(Transient) TObjectPtr<UImage> OutputImage;
	UPROPERTY(Transient) TObjectPtr<AActor> PreviewActor;
	UPROPERTY(Transient) TObjectPtr<UMSMechRigComponent> Rig;
	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> VisualMesh;
	UPROPERTY(Transient) TObjectPtr<UMSMobHandVisuals> MobHandVisuals;
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RenderTarget;
};
