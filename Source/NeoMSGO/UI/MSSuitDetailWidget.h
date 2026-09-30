#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSSuitDetailWidget.generated.h"

class UButton;
class UImage;
class UMSCustomizeWidget;
class UMSDeckSubsystem;
class UMSHangarWidget;
class UMSMobileSuitData;
class UMSMobileSuitPreviewRenderer;
class UMSModuleCustomizeWidget;
class UMSBattleAssistCustomizeWidget;
class UMSSuitSwapWidget;
class UMSUILockSubsystem;
class UProgressBar;
class UTextBlock;
class UMSWeaponIconWidget;

/** MSGO-style selected deck mobile-suit details and customization gateway. */
UCLASS()
class NEOMSGO_API UMSSuitDetailWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 StatusRowCount = 6;
	static constexpr int32 DisabledActionCount = 6;

	UFUNCTION(BlueprintCallable, Category="MS|Suit Detail") void Open(int32 SlotIndex);
	UFUNCTION(BlueprintCallable, Category="MS|Suit Detail") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Suit Detail") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	void SetCustomizeWidget(UMSCustomizeWidget* InWidget) { CustomizeWidget = InWidget; }
	/** Invalidates a pending open/slot acknowledgement when its owner screen closes. */
	void CancelPendingSelection();
	void OpenWeaponCustomization(int32 SlotIndex);
	void OpenModuleCustomization(int32 SlotIndex);
	void OpenBattleAssistCustomization(int32 SlotIndex);
	void HandleHangarClosed();
	void HandleModuleCustomizeChanged();
	void HandleModuleCustomizeClosed();
	void HandleBattleAssistCustomizeClosed();
	void HandleSwapClosed();
	FText GetDisplayedSuitName() const;
	FText GetDisplayedShieldText() const;
	int32 GetDisplayedSlotIndex() const { return DisplayedSlotIndex; }
	int32 GetStatusRowCount() const { return StatusBars.Num(); }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	float TickPreviewRotationForTesting(float DeltaSeconds, bool bActuallyPressed) { return TickPreviewRotation(DeltaSeconds, bActuallyPressed); }
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	void SetHangarWidgetForTesting(UMSHangarWidget* InWidget) { HangarWidget = InWidget; }
	void SetModuleCustomizeWidgetForTesting(UMSModuleCustomizeWidget* InWidget);
	void SetBattleAssistCustomizeWidgetForTesting(UMSBattleAssistCustomizeWidget* InWidget);
	void SetSwapWidgetForTesting(UMSSuitSwapWidget* InWidget);
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void BindDeckSubsystem();
	void OpenInternal(int32 SlotIndex, TFunction<void(bool bSucceeded)> Completion);
	void Refresh();
	void EnsurePreviewRenderer();
	float TickPreviewRotation(float DeltaSeconds, bool bActuallyPressed);
	UFUNCTION() void HandleRotateLeftPressed();
	UFUNCTION() void HandleRotateRightPressed();
	UFUNCTION() void StopPreviewRotation();
	UFUNCTION() void HandleDetailVisibilityChanged(ESlateVisibility NewVisibility);
	void SetDisplayedSlot(int32 SlotIndex);
	int32 FindOccupiedSlot(int32 Direction) const;
	bool EnsureHangarWidget();
	bool EnsureModuleCustomizeWidget();
	bool EnsureBattleAssistCustomizeWidget();
	bool EnsureSwapWidget();
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandlePreviousClicked();
	UFUNCTION() void HandleNextClicked();
	UFUNCTION() void HandleWeaponCustomizeClicked();
	UFUNCTION() void HandleModuleCustomizeClicked();
	UFUNCTION() void HandleBattleAssistCustomizeClicked();
	UFUNCTION() void HandleSwapClicked();
	UFUNCTION() void HandleRemoveClicked();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderIdentityText;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSCustomizeWidget> CustomizeWidget;
	UPROPERTY(Transient) TObjectPtr<UMSHangarWidget> HangarWidget;
	UPROPERTY(Transient) TObjectPtr<UMSModuleCustomizeWidget> ModuleCustomizeWidget;
	UPROPERTY(Transient) TObjectPtr<UMSBattleAssistCustomizeWidget> BattleAssistCustomizeWidget;
	UPROPERTY(Transient) TObjectPtr<UMSSuitSwapWidget> SwapWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	UPROPERTY(Transient) TObjectPtr<UButton> RotateLeftButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RotateRightButton;
	int32 PreviewRotationDirection = 0;
	bool bPreviewRotationTickObserved = false;
	float PreviewRotationPendingSeconds = 0.0f;
	UPROPERTY(Transient) TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> PreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuitNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RightNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostTimeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuitTypeBadgeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EnhancementCountText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeckNumberText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LoadoutText;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSWeaponIconWidget>> WeaponIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WeaponNameTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NativeShieldText;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> StatusBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatusValueTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusBonusSummaryText;
	int32 DisplayedSlotIndex = 0;
	uint64 SelectionRequestGeneration = 0;
};
