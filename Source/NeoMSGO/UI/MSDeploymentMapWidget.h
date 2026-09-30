#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "UI/MSMapWidgetBase.h"
#include "MSDeploymentMapWidget.generated.h"

class AMSCapturePoint;
class AMSBotSpawnPoint;
class UBorder;
class UButton;
class UImage;
class UMSDeckSubsystem;
class UMSMobileSuitData;
class UMSMobileSuitPreviewRenderer;
class UMSRespawnComponent;
class UMSSpawnSelectWidget;
class UTextBlock;

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSDeployPointSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) FVector2D MapLocal = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadWrite) FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite) float MarkerSizePx = 0.0f;
	/** The same text passed to ConstructCapturePointMarker for capture points. */
	UPROPERTY(BlueprintReadWrite) FText Label;
	/** True when the drawn marker is a rotated diamond (home base or spawn point). */
	UPROPERTY(BlueprintReadWrite) bool bDiamondMarker = false;
	UPROPERTY(BlueprintReadWrite) bool bCanDeploy = false;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSMapClickResolution
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bResolved = false;
	UPROPERTY(BlueprintReadOnly) bool bDirectPoint = false;
	UPROPERTY(BlueprintReadOnly) FVector WorldLocation = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnSpawnPointSelected, FTransform, SpawnTransform);

UCLASS()
class NEOMSGO_API UMSDeploymentMapWidget : public UMSMapWidgetBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="MS|Map")
	static FMSMapClickResolution ResolveMapClick(const FVector2D& Local,
		const TArray<FMSDeployPointSnapshot>& PointSnapshots, bool bHasNearestDeployablePoint,
		float NearestDeployableDistanceCm, float FreeDeployRangeCm, float WorldHalfExtentCm,
		const FVector2D& MapSizePx, float EdgeMarginPx);

	UFUNCTION(BlueprintCallable, Category="MS|Map")
	void ClearSelection();

	/** Confirms an already validated deployment location. Also used by headless UI-flow verification. */
	UFUNCTION(BlueprintCallable, Category="MS|Map")
	void ConfirmSelectionAtWorldLocation(const FVector& WorldLocation) { ConfirmSelection(WorldLocation); }

	/** Adds only neutral/friendly AMSBotSpawnPoint actors to a click-resolution snapshot. */
	static void AppendTeamSpawnPointSnapshots(UWorld* World, FGenericTeamId TeamId,
		TArray<FMSDeployPointSnapshot>& InOutSnapshots, const FVector2D& MapSizePx);

	virtual void Open() override;
	virtual void Close() override;

	UPROPERTY(BlueprintAssignable, Category="MS|Map")
	FMSOnSpawnPointSelected OnSpawnPointSelected;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|Map")
	bool bDeployOnSelect = true;

	/** Optional P8-8 preset forwarded by the sortie screen. Null preserves the current suit. */
	void SetDeployPreset(UMSMobileSuitData* Preset);
	UMSMobileSuitData* GetDeployPreset() const { return DeployPreset; }
	void SetMissionRuleName(const FText& InRuleName);
	FText GetMissionRuleName() const;
	bool HasSelectedSpawnTransform() const { return SelectedSpawnTransform.IsSet(); }
	bool SelectDeckSlot(int32 SlotIndex);
	int32 GetSelectedDeckSlotIndex() const { return SelectedDeckSlotIndex; }
	int32 GetFriendlySpawnPointCount() const { return FriendlySpawnPoints.Num(); }
	void SetSourceSpawnSelectWidget(UMSSpawnSelectWidget* InWidget) { SourceSpawnSelectWidget = InWidget; }
	UMSSpawnSelectWidget* GetSourceSpawnSelectWidget() const { return SourceSpawnSelectWidget; }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	void SetRespawnComponentForTesting(UMSRespawnComponent* InComponent) { RespawnComponentOverride = InComponent; }
	void ConfirmSelectionForAutomationTests(const FVector& WorldLocation) { ConfirmSelection(WorldLocation); }
	FReply HandlePointerForAutomationTests(const FPointerEvent& InMouseEvent)
	{
		return NativeOnMouseButtonDown(FGeometry(), InMouseEvent);
	}
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FVector2D GetMapSize() const override;

private:
	void BuildReferenceFrame();
	void BindDeckSubsystem();
	void CacheFriendlySpawnPointMarkers();
	void RefreshFriendlySpawnPointMarkers();
	void RefreshMSList();
	void RefreshSelectedMarker();
	UMSRespawnComponent* ResolveRespawnComponent() const;
	FGenericTeamId ResolveDeploymentTeam() const;
	void CloseFrontendAfterDeploy();
	void ConfirmSelection(const FVector& WorldLocation);
	bool RequestOnlineSelection(const FVector& WorldLocation);
	UFUNCTION() void HandleOnlineDeploymentResult(bool bSuccess, FText Message);
	UFUNCTION() void HandleMSListSlot0Clicked();
	UFUNCTION() void HandleMSListSlot1Clicked();
	UFUNCTION() void HandleMSListSlot2Clicked();
	UFUNCTION() void HandleMSListSlot3Clicked();
	UFUNCTION() void HandleReturnToSuitSelectionClicked();
	void HandleCloseRequested();
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MissionRuleText;
	UPROPERTY(Transient) TObjectPtr<UImage> SelectedDeployMarker;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSRespawnComponent> RespawnComponentOverride;
	UPROPERTY(Transient) TObjectPtr<UMSSpawnSelectWidget> SourceSpawnSelectWidget;
	UPROPERTY(Transient) TArray<TObjectPtr<AMSBotSpawnPoint>> FriendlySpawnPoints;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> FriendlySpawnPointMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MSListButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> MSListBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MSListNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MSListRespawnTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MSListPortraits;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitPreviewRenderer>> MSListRenderers;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitData>> RenderedPresets;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> BackdropRenderer;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> BackdropPreset;
	TOptional<FTransform> SelectedSpawnTransform;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> DeployPreset;
	FText MissionRuleName;
	int32 SelectedDeckSlotIndex = INDEX_NONE;
	bool bOnlineDeploymentPending = false;
};
