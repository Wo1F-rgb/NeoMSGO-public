#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/MSMobileSuitData.h"
#include "MSSuitSwapWidget.generated.h"

class UButton;
class UCanvasPanel;
class UEditableTextBox;
class UImage;
class UMSDeckSubsystem;
class UMSCustomizeWidget;
class UMSMobileSuitPreviewRenderer;
class UMSMobileSuitData;
class UMSSuitDetailWidget;
class UMSUILockSubsystem;
class UOverlay;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

enum class EMSSuitSwapScreenMode : uint8
{
	Swap,
	OwnedList,
	Development
};

UCLASS()
class NEOMSGO_API UMSSuitSwapEntryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetEntry(UMSMobileSuitData* InPreset, class UMSSuitSwapWidget* InOwner);
	UButton* GetEntryButton() const { return EntryButton; }
protected:
	virtual void NativeOnInitialized() override;
private:
	UFUNCTION() void HandleClicked();
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> Preset;
	UPROPERTY(Transient) TObjectPtr<UMSSuitSwapWidget> Owner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
	UPROPERTY(Transient) TObjectPtr<UButton> EntryButton;
};

/** Two-phase catalog list and before/after deck comparison. */
UCLASS()
class NEOMSGO_API UMSSuitSwapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="MS|Suit Swap") void Open(int32 SlotIndex);
	UFUNCTION(BlueprintCallable, Category="MS|Suit Swap") void OpenOwnedList();
	UFUNCTION(BlueprintCallable, Category="MS|Suit Swap") void OpenDevelopment();
	UFUNCTION(BlueprintCallable, Category="MS|Suit Swap") bool Close();
	UFUNCTION(BlueprintPure, Category="MS|Suit Swap") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	bool IsSwapMode() const { return ScreenMode == EMSSuitSwapScreenMode::Swap; }
	bool IsOwnedListMode() const { return ScreenMode == EMSSuitSwapScreenMode::OwnedList; }
	bool IsDevelopmentMode() const { return ScreenMode == EMSSuitSwapScreenMode::Development; }
	void SetSuitDetailWidget(UMSSuitDetailWidget* InWidget) { SuitDetailWidget = InWidget; CustomizeWidget = nullptr; }
	void SetCustomizeWidget(UMSCustomizeWidget* InWidget) { CustomizeWidget = InWidget; SuitDetailWidget = nullptr; }
	void SelectCandidate(UMSMobileSuitData* Preset);
	int32 GetCatalogCount() const { return Catalog.Num(); }
	int32 GetFilteredCount() const { return FilteredCatalog.Num(); }
	bool IsComparisonPhase() const { return bComparisonPhase; }
	bool CanConfirmSwap() const;
	FText GetCandidateName() const;
	FText GetCurrentName() const;
	FText GetCurrentShieldTextForTesting() const;
	FText GetCandidateShieldTextForTesting() const;

#if WITH_DEV_AUTOMATION_TESTS
	bool IsCurrentPreviewShieldVisibleForTesting() const;
	void InitializeForAutomationTests();
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem) { DeckSubsystem = InSubsystem; }
	void SetCatalogForTesting(const TArray<UMSMobileSuitData*>& InCatalog);
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
	FLinearColor GetDeckCostTextColorForAutomationTests() const;
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void OpenScreen(EMSSuitSwapScreenMode InMode, int32 InSlotIndex);
	void RefreshModeLayout();
	void LoadCatalog();
	void ApplyFilters();
	void RebuildRows();
	void ShowListPhase();
	void ShowComparisonPhase();
	void RefreshCurrentDetails();
	void RefreshComparison();
	int32 GetDeckCostAfterSwap() const;
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	void SelectCategory(int32 CategoryIndex);
	UFUNCTION() void HandlePreviousPageClicked();
	UFUNCTION() void HandleNextPageClicked();
	UFUNCTION() void HandleSearchClicked();
	UFUNCTION() void HandleClearClicked();
	UFUNCTION() void HandleSortClicked();
	UFUNCTION() void HandleCategoryAllClicked();
	UFUNCTION() void HandleCategoryAssaultClicked();
	UFUNCTION() void HandleCategoryHeavyClicked();
	UFUNCTION() void HandleCategorySupportClicked();
	UFUNCTION() void HandleCategoryBombardmentClicked();
	UFUNCTION() void HandleCategorySniperClicked();
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleBackClicked();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderIdentityText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ListTitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DevelopmentInstructionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DevelopmentEmptyStateText;
	UPROPERTY(Transient) TObjectPtr<UOverlay> CatalogFrame;
	UPROPERTY(Transient) TObjectPtr<UOverlay> ListCurrentSummaryFrame;
	UPROPERTY(Transient) TObjectPtr<UOverlay> SearchFrame;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ListCanvas;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSSuitDetailWidget> SuitDetailWidget;
	UPROPERTY(Transient) TObjectPtr<UMSCustomizeWidget> CustomizeWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SearchInput;
	UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(Transient) TObjectPtr<UButton> PreviousPageButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextPageButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageNumberText;
	UPROPERTY(Transient) TObjectPtr<UImage> ListPreviewImage;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> ListPreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SortText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ListCurrentSummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CurrentNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CurrentStatsText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CurrentShieldText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CandidateNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CandidateStatsText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CandidateShieldText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostAfterText;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CatalogScrollBox;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ListPhase;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ComparisonPhase;
	UPROPERTY(Transient) TObjectPtr<UImage> CurrentPreviewImage;
	UPROPERTY(Transient) TObjectPtr<UImage> CandidatePreviewImage;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> CurrentPreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> CandidatePreviewRenderer;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitData>> Catalog;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitData>> FilteredCatalog;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> CandidatePreset;
	static constexpr int32 PageSize = 7;
	int32 PageIndex = 0;
	int32 SlotIndex = 0;
	int32 SelectedCategoryIndex = 0;
	bool bCostDescending = true;
	bool bComparisonPhase = false;
	bool bCatalogInjectedForTests = false;
	EMSSuitSwapScreenMode ScreenMode = EMSSuitSwapScreenMode::Swap;
};
