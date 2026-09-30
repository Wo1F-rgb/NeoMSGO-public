#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSSpawnSelectWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class AMSUnitBase;
class UMSDeckSubsystem;
class UMSDeploymentMapWidget;
class UMSMobileSuitData;
class UMSMobileSuitPreviewRenderer;
class UMSRespawnComponent;
class UProgressBar;
class UTextBlock;

UENUM(BlueprintType)
enum class EMSSpawnSelectMode : uint8
{
	PreOperation,
	Respawn
};

/** MSGO-style four-card mobile-suit selection shown before deployment and after death. */
UCLASS()
class NEOMSGO_API UMSSpawnSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 SlotCount = 4;
	static constexpr int32 StatusRowCount = 6;

	/** Opens after briefing and continues its remaining operation-start countdown. Zero never auto-deploys. */
	UFUNCTION(BlueprintCallable, Category="MS|Spawn Select")
	void OpenPreOperation(float RemainingOperationSeconds);

	/** Opens after death. All four cards display the owner's shared respawn countdown. */
	UFUNCTION(BlueprintCallable, Category="MS|Spawn Select")
	void OpenRespawn(UMSRespawnComponent* InRespawnComponent);

	UFUNCTION(BlueprintCallable, Category="MS|Spawn Select") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Spawn Select") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	UFUNCTION(BlueprintCallable, Category="MS|Spawn Select") bool SelectSlot(int32 SlotIndex);

	EMSSpawnSelectMode GetMode() const { return Mode; }
	int32 GetSelectedSlotIndex() const { return SelectedSlotIndex; }
	UMSMobileSuitData* GetSelectedPreset() const;
	FText GetDisplayedSuitName() const;
	FText GetDisplayedCountdownText() const;
	FText GetCardRespawnText(int32 SlotIndex) const;
	bool IsSlotEnabled(int32 SlotIndex) const;
	bool IsDeployEnabled() const;
	int32 GetDeploymentRequestCount() const { return DeploymentRequestCount; }
	UMSDeploymentMapWidget* GetDeploymentMapWidget() const { return DeploymentMapWidget; }
	void SetMissionRuleName(const FText& InRuleName) { MissionRuleName = InRuleName; }
	FText GetMissionRuleName() const { return MissionRuleName; }
	/** Restores this same selection state after the deployment map's back button. */
	void ResumeFromDeployment();

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	void SetRespawnComponentForTesting(UMSRespawnComponent* InComponent);
	void SetDeploymentMapWidgetForTesting(UMSDeploymentMapWidget* InWidget) { DeploymentMapWidget = InWidget; }
	FReply HandlePointerForAutomationTests(const FPointerEvent& InMouseEvent)
	{
		return NativeOnMouseButtonDown(FGeometry(), InMouseEvent);
	}
	void AdvanceForAutomationTests(float DeltaTime);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void BindDeckSubsystem();
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	void RefreshAll();
	void RefreshCard(int32 SlotIndex);
	void RefreshSelection();
	void RefreshDetails();
	void RefreshCountdownAndDeployState(float DeltaTime);
	bool EnsureDeploymentMapWidget();

	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);
	UFUNCTION() void HandleSlot0Clicked();
	UFUNCTION() void HandleSlot1Clicked();
	UFUNCTION() void HandleSlot2Clicked();
	UFUNCTION() void HandleSlot3Clicked();
	UFUNCTION(BlueprintCallable, Category="MS|Spawn Select", meta=(AllowPrivateAccess="true"))
	void HandleDeployClicked();
	UFUNCTION() void HandleRespawnReady(AMSUnitBase* Unit);

	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSRespawnComponent> RespawnComponent;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> PreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UMSDeploymentMapWidget> DeploymentMapWidget;
	UPROPERTY(Transient) TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OperationCountdownText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuitNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostTimeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LoadoutText;
	UPROPERTY(Transient) TObjectPtr<UButton> DeployButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitPreviewRenderer>> SlotPreviewRenderers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotPreviewImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotEquipmentTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SlotButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SlotBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotStatsTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RespawnTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> StatusBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatusValueTexts;

	EMSSpawnSelectMode Mode = EMSSpawnSelectMode::PreOperation;
	int32 SelectedSlotIndex = INDEX_NONE;
	float OperationRemainingSeconds = 0.0f;
	int32 DeploymentRequestCount = 0;
	FText MissionRuleName;
};
