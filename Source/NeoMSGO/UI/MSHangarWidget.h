#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "UI/MSDeckSubsystem.h"
#include "MSHangarWidget.generated.h"

class UButton;
class UScrollBox;
class UMSMobileSuitData;
class UMSMobileSuitPreviewRenderer;
class UMSSortieWidget;
class UMSSuitDetailWidget;
class UMSBriefingWidget;
class UMSWeaponDataBase;
class UMSShieldWeaponData;
class UTextBlock;
class UImage;
class UBorder;
class UProgressBar;

UCLASS()
class NEOMSGO_API UMSWeaponCatalogEntryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetWeapon(UMSWeaponDataBase* InWeapon, class UMSHangarWidget* InOwningHangar);
	void SetSelected(bool bInSelected);
protected:
	virtual void NativeOnInitialized() override;
	private:
	UFUNCTION() void HandleClicked();
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeaponText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeightText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquippedStateText;
	UPROPERTY(Transient) TObjectPtr<UBorder> RowBorder;
	/** カテゴリ別シルエットアイコン(略号テキストから移行。実プレイ報告対応) */
	UPROPERTY(Transient) TObjectPtr<class UMSWeaponIconWidget> WeaponIconWidget;
	UPROPERTY(Transient) TObjectPtr<UMSWeaponDataBase> Weapon;
	UPROPERTY(Transient) TObjectPtr<class UMSHangarWidget> OwningHangar;
};

/** P8-9: session-only mobile-suit loadout customization screen. */
UCLASS()
class NEOMSGO_API UMSHangarWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	static constexpr int32 CategoryCount = 8;
	static constexpr int32 WeaponPageSize = 5;

	UFUNCTION(BlueprintCallable, Category="MS|Hangar") void Open(UMSMobileSuitData* Preset);
	/** Opens the immutable catalog suit with the mutable loadout belonging to one deck slot. */
	UFUNCTION(BlueprintCallable, Category="MS|Hangar") void OpenForDeckSlot(int32 DeckSlotIndex);
	/** The admitted match loadout is distinct from the account's next-match deck. */
	void OpenReadOnlyLoadout(UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout);
	bool IsReadOnly() const { return bReadOnlyLoadout; }
	UFUNCTION(BlueprintCallable, Category="MS|Hangar") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Hangar") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	void SetReturnSortieWidget(UMSSortieWidget* InWidget) { ReturnSortieWidget = InWidget; if (InWidget) { ReturnSuitDetailWidget = nullptr; ReturnBriefingWidget = nullptr; } }
	void SetReturnSuitDetailWidget(UMSSuitDetailWidget* InWidget) { ReturnSuitDetailWidget = InWidget; if (InWidget) { ReturnSortieWidget = nullptr; ReturnBriefingWidget = nullptr; } }
	void SetReturnBriefingWidget(UMSBriefingWidget* InWidget) { ReturnBriefingWidget = InWidget; if (InWidget) { ReturnSortieWidget = nullptr; ReturnSuitDetailWidget = nullptr; } }
	void SetWeaponCatalog(const TArray<UMSWeaponDataBase*>& Weapons);
	void SelectCategory(FGameplayTag Category);
	void SelectWeaponForSlot(int32 SlotIndex, UMSWeaponDataBase* Weapon);
	int32 GetCategoryTabCount() const { return CategoryButtons.Num(); }
	int32 GetFilteredWeaponCount() const { return FilteredItems.Num(); }
	int32 GetCurrentWeaponPage() const { return CurrentPageIndex + 1; }
	int32 GetWeaponPageCount() const { return PageCount; }
	UScrollBox* GetWeaponListView() const { return WeaponListView; }
	void ChooseCatalogWeapon(UMSWeaponDataBase* Weapon);
	void ConfirmSelectedWeapon();
	void ClearSelectedSlot();
	UFUNCTION(BlueprintCallable, Category="MS|Hangar|Shield") void ToggleNativeShield();
	UFUNCTION(BlueprintPure, Category="MS|Hangar|Shield") bool IsNativeShieldSupported() const;
	UFUNCTION(BlueprintPure, Category="MS|Hangar|Shield") bool IsNativeShieldEquipped() const;
	UMSWeaponDataBase* GetFilteredWeapon(int32 Index) const;
	UMSWeaponDataBase* GetSelectedWeapon() const { return SelectedWeapon; }
	int32 GetEquippedSlot(const UMSWeaponDataBase* Weapon) const;
	FGameplayTag GetSelectedCategory() const { return SelectedCategory; }
	int32 GetCurrentLoad() const { return CurrentLoad; }
	int32 GetPreviewLoad() const { return PreviewLoad; }
	int32 GetSelectedSlotIndex() const { return PickerSlotIndex; }
	bool IsOverCapacity() const { return bOverCapacity; }
	FSlateColor GetLoadTextColor() const;
#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests(UMSMobileSuitData* Preset);
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	/** Injects the account delegate source used by the deterministic sync-state UI tests. */
	void SetOnlineAccountForTesting(UMSOnlineAccountSubsystem* InSubsystem);
	/** Starts the same pending visual state as an online CommitLoadout request. */
	void BeginOnlineSaveForAutomationTests(const FMSDeckLoadout& Candidate, int32 DeckSlotIndex, int64 AccountGeneration);
	FText GetWeaponParametersTextForTesting() const;
	float GetReloadBarPercentForTesting(bool bSelected) const;
	float GetPowerBarPercentForTesting(bool bSelected) const;
	FText GetEquippedComparisonTextForTesting() const;
	FText GetSaveStatusTextForTesting() const;
	FSlateColor GetSaveStatusColorForTesting() const;
	FText GetNativeShieldStatusTextForTesting() const;
	FText GetNativeShieldDurabilityTextForTesting() const;
	FText GetNativeShieldToggleTextForTesting() const;
	bool IsNativeShieldToggleEnabledForTesting() const;
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BindDeckSubsystem();
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	void OpenInternal(UMSMobileSuitData* Preset, int32 DeckSlotIndex);
	bool CommitWorkingLoadout(const FMSDeckLoadout& Candidate);
	UFUNCTION() void HandleDeckSyncStateChanged(EMSOnlineDeckSyncState State);
	UFUNCTION() void HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation);
	UFUNCTION() void HandleOnlineRequestFinished(FName Operation, bool bSuccess, FText Message, int64 Generation);
	void SetSavePendingVisual(bool bPending);
	FMSDeckLoadout BuildSelectedWeaponLoadout() const;
	void ShowSaveFailureMessage(const FString& Detail = FString());
	FMSDeckLoadout GetWorkingLoadout() const;
	UMSWeaponDataBase* GetWorkingWeapon(int32 SlotIndex) const;
	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);
	void RefreshCatalogFromAssetManager();
	void RefreshFilteredWeapons(bool bSyncSelectedWeaponPage = false);
	void RefreshLoadout();
	void RefreshNativeShield();
	void RefreshSelectionPreview();
	bool IsWeaponAvailableForActiveSuit(const UMSWeaponDataBase* Weapon) const;
	void BeginPickSlot(int32 SlotIndex);
	UFUNCTION() void HandleCategory0Clicked();
	UFUNCTION() void HandleCategory1Clicked();
	UFUNCTION() void HandleCategory2Clicked();
	UFUNCTION() void HandleCategory3Clicked();
	UFUNCTION() void HandleCategory4Clicked();
	UFUNCTION() void HandleCategory5Clicked();
	UFUNCTION() void HandleCategory6Clicked();
	UFUNCTION() void HandleCategory7Clicked();
	UFUNCTION() void HandleSlot0Clicked();
	UFUNCTION() void HandleSlot1Clicked();
	UFUNCTION() void HandleSlot2Clicked();
	UFUNCTION() void HandlePreviousPageClicked();
	UFUNCTION() void HandleNextPageClicked();
	UFUNCTION() void HandleEquipClicked();
	UFUNCTION() void HandleRemoveClicked();
	UFUNCTION() void HandleNativeShieldClicked();
	UFUNCTION() void HandleCloseClicked();

	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> ActivePreset;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSSortieWidget> ReturnSortieWidget;
	UPROPERTY(Transient) TObjectPtr<UMSSuitDetailWidget> ReturnSuitDetailWidget;
	UPROPERTY(Transient) TObjectPtr<UMSBriefingWidget> ReturnBriefingWidget;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSWeaponDataBase>> WeaponCatalog;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSWeaponDataBase>> FilteredItems;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CategoryButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotWeaponTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SlotCardBorders;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LoadText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NativeShieldStatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NativeShieldDurabilityText;
	UPROPERTY(Transient) TObjectPtr<UButton> NativeShieldToggleButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NativeShieldToggleText;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> LoadBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PickerTitleText;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> WeaponListView;
	UPROPERTY(Transient) TObjectPtr<UButton> PreviousPageButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextPageButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageText;
	UPROPERTY(Transient) TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewNameText;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> PreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeaponParametersText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquippedComparisonText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedComparisonText;
	/** Header/icon pair for the two stacked weapon comparison cards. */
	UPROPERTY(Transient) TObjectPtr<class UMSWeaponIconWidget> EquippedComparisonIcon;
	UPROPERTY(Transient) TObjectPtr<class UMSWeaponIconWidget> SelectedComparisonIcon;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EquippedStatTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SelectedStatTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmptySlotGuideText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquipButtonText;
	UPROPERTY(Transient) TObjectPtr<UButton> EquipButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RemoveButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> EquippedStatBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> SelectedStatBars;
	UPROPERTY(Transient) TObjectPtr<UMSWeaponDataBase> SelectedWeapon;
	/** Candidate edits stay here until Equip/Remove commits them through the deck API. */
	FMSDeckLoadout WorkingLoadout;
	TArray<FGameplayTag> Categories;
	TSet<FGameplayTag> ActiveSuitCategories;
	FGameplayTag SelectedCategory;
	int32 PickerSlotIndex = 0;
	int32 CurrentPageIndex = 0;
	int32 PageCount = 1;
	int32 ActiveDeckSlotIndex = INDEX_NONE;
	int32 CurrentLoad = 0;
	int32 PreviewLoad = 0;
	bool bOverCapacity = false;
	bool bUsingDeckLoadout = false;
	bool bReadOnlyLoadout = false;
	bool bOnlineSavePending = false;
	bool bOnlineSaveStateObserved = false;
	int64 PendingAccountGeneration = -1;
	int32 PendingDeckSlotIndex = INDEX_NONE;
	FMSDeckLoadout PendingLoadout;
	FString PendingSaveFailureMessage;
	TWeakObjectPtr<UMSOnlineAccountSubsystem> OnlineAccountSubsystem;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SaveStatusText;
	UPROPERTY(Transient) TArray<TObjectPtr<class UMSWeaponIconWidget>> SlotWeaponIcons;
};
