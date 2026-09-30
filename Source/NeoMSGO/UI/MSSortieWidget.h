#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSSortieWidget.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UMSBriefingWidget;
class UMSDeckSubsystem;
class UMSMobileSuitData;
class UMSSortieWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

UENUM(BlueprintType)
enum class EMSSortieCatalogCategory : uint8
{
	All,
	Assault,
	Heavy,
	Support,
	Bombardment,
	Sniper
};

/**
 * Code-only entry widget for one mobile-suit catalog row.
 * UListView requires EntryWidgetClass to be a Blueprint-generated class in non-cooked builds
 * (see UListViewBase::RebuildWidget's "is not a Blueprint class" check), which a pure C++
 * UUserWidget can never satisfy. The catalog is therefore built as a plain UScrollBox of these
 * rows (see UMSSortieWidget::RefreshCatalogFromAssetManager) instead of a virtualized UListView.
 */
UCLASS()
class NEOMSGO_API UMSMobileSuitCatalogEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPreset(UMSMobileSuitData* InPreset, UMSSortieWidget* InOwningSortieWidget);

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UMSMobileSuitData> Preset;

	UPROPERTY(Transient)
	TObjectPtr<UMSSortieWidget> OwningSortieWidget;
};

/** MSGO-style briefing modal for confirming and editing the shared four-suit sortie deck. */
UCLASS()
class NEOMSGO_API UMSSortieWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 SortieSlotCount = 4;

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void Open();

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void Close();

	UFUNCTION(BlueprintPure, Category="MS|Sortie")
	bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void SetSelectedSlotIndex(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void AssignPresetToSelectedSlot(UMSMobileSuitData* Preset);

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void SelectCatalogPreset(UMSMobileSuitData* Preset);

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void SelectCatalogCategory(EMSSortieCatalogCategory Category);

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	void SetBriefingWidget(UMSBriefingWidget* InBriefingWidget);

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	bool InsertSelectedCatalogPreset();

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	bool ReplaceWithSelectedCatalogPreset();

	UFUNCTION(BlueprintCallable, Category="MS|Sortie")
	bool RemovePresetFromSelectedSlot();

	int32 GetSortieSlotCount() const;
	int32 GetSelectedSlotIndex() const;
	UMSDeckSubsystem* GetDeckSubsystem() const;
	UMSMobileSuitData* GetPresetInSlot(int32 SlotIndex) const;
	UScrollBox* GetCatalogListView() const { return CatalogListView; }
	int32 GetCatalogItemCount() const;
	int32 GetFilteredCatalogItemCount() const { return FilteredCatalogItems.Num(); }
	UMSMobileSuitData* GetFilteredCatalogPreset(int32 Index) const;
	int32 GetCurrentDeckCost() const;
	int32 GetDeckCostLimit() const;
	bool IsDeckOverCostLimit() const;
	EMSSortieCatalogCategory GetSelectedCatalogCategory() const { return SelectedCatalogCategory; }
	UMSMobileSuitData* GetSelectedCatalogPreset() const { return SelectedCatalogPreset; }
	FText GetDisplayedCountdownText() const;
	/** Legacy return hook retained only so pre-T8 hangar callers remain source-compatible. */
	void HandleHangarClosed();

	void SetCatalogPresets(const TArray<UMSMobileSuitData*>& Presets);
	void RefreshBriefingCountdown();
#if WITH_DEV_AUTOMATION_TESTS
	/** Native C++ widgets normally initialize with a local-player context; headless tests supply none. */
	void InitializeForAutomationTests();
	/** 指定スロットカードに現在表示中の武器名テキスト一覧(問題1「クラス名表示」回帰テスト用) */
	TArray<FText> GetSlotWeaponNameTextsForTests(int32 SlotIndex) const;
	FText GetSlotShieldTextForTests(int32 SlotIndex) const;
	FLinearColor GetDeckCostTextColorForAutomationTests() const;
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InDeckSubsystem);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void RefreshCatalogFromAssetManager();
	bool IsAdmittedLoadoutView() const;
	void RefreshFilteredCatalog();
	void RefreshSlotCard(int32 SlotIndex);
	void RefreshSelectionStyle();
	void RefreshDeckCost();
	int32 GetProspectiveDeckCost(UMSMobileSuitData* Preset) const;
	bool WouldExceedCostLimit(UMSMobileSuitData* Preset) const;
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);

	UFUNCTION() void HandleSlot0Clicked();
	UFUNCTION() void HandleSlot1Clicked();
	UFUNCTION() void HandleSlot2Clicked();
	UFUNCTION() void HandleSlot3Clicked();
	UFUNCTION() void HandleCategoryAllClicked();
	UFUNCTION() void HandleCategoryAssaultClicked();
	UFUNCTION() void HandleCategoryHeavyClicked();
	UFUNCTION() void HandleCategorySupportClicked();
	UFUNCTION() void HandleCategoryBombardmentClicked();
	UFUNCTION() void HandleCategorySniperClicked();
	UFUNCTION() void HandleInsertClicked();
	UFUNCTION() void HandleRemoveClicked();
	UFUNCTION() void HandleReplaceClicked();
	UFUNCTION() void HandleSearchClicked();
	UFUNCTION() void HandleSearchClearClicked();
	UFUNCTION() void HandleCostSortClicked();
	UFUNCTION() void HandleCloseClicked();

	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitData>> CatalogItems;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitData>> FilteredCatalogItems;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SlotBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotStatsTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UVerticalBox>> SlotWeaponBoxes;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotShieldTexts;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CatalogListView;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeckCostText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostWarningText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CatalogSelectionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountdownText;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SearchInput;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostSortText;
	UPROPERTY(Transient) TObjectPtr<UMSBriefingWidget> BriefingWidget;
	UPROPERTY(Transient) mutable TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> SelectedCatalogPreset;
	int32 SelectedSlotIndex = 0;
	EMSSortieCatalogCategory SelectedCatalogCategory = EMSSortieCatalogCategory::All;
	bool bCostDescending = true;
};
