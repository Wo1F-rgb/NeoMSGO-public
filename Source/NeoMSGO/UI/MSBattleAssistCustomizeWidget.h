#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/MSBattleAssistTypes.h"
#include "Online/MSOnlineTypes.h"
#include "MSBattleAssistCustomizeWidget.generated.h"

class UButton;
class UMSBattleAssistModuleData;
class UMSBattleAssistProgressSubsystem;
class UMSDeckSubsystem;
class UMSMobileSuitData;
class UMSVectorSkinWidget;
class UTextBlock;
class UImage;
class UMSMobileSuitPreviewRenderer;
class UMSOnlineAccountSubsystem;
class UMSSuitDetailWidget;

namespace MSBattleAssistUI
{
	NEOMSGO_API FString ResolveEffectiveTime(const UMSMobileSuitData* Suit,
		const FMSBattleAssistLoadout& Loadout, const FMSBattleAssistProgress* Progress);
}

/**
 * Four-tab Battle Assist loadout screen.  This is deliberately separate from
 * the legacy Transzomm/固有能力 screen: Battle Assist data is account-wide,
 * validated by MSBattleAssistRules, and committed through UMSDeckSubsystem.
 */
UCLASS()
class NEOMSGO_API UMSBattleAssistCustomizeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 TabCount = 4;
	static constexpr int32 RowsPerPage = 6;
	static constexpr int32 SlotDisplayCount = 10;

	UFUNCTION(BlueprintCallable, Category="MS|Battle Assist")
	void Open(UMSMobileSuitData* InSuit, int32 InSlotIndex);
	UFUNCTION(BlueprintCallable, Category="MS|Battle Assist")
	void Close();
	void SetReturnSuitDetailWidget(UMSSuitDetailWidget* InWidget) { ReturnSuitDetailWidget = InWidget; }
	UFUNCTION(BlueprintPure, Category="MS|Battle Assist")
	bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	FText GetDisplayedTimeText() const;
	FText GetDisplayedReasonText() const;
	int32 GetDisplayedModuleCount() const { return FilteredModules.Num(); }
	int32 GetDisplayedTab() const { return ActiveTab; }
	int32 GetDisplayedSelectedRow() const { return SelectedRow; }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	void SetProgressSubsystemForTesting(UMSBattleAssistProgressSubsystem* InSubsystem);
	void SetOnlineAccountForTesting(UMSOnlineAccountSubsystem* InSubsystem);
	void SetSuitForTesting(UMSMobileSuitData* InSuit, int32 InSlotIndex);
	void BeginPendingCommitForAutomationTests(const FMSBattleAssistLoadout& Candidate, int64 AccountGeneration);
	void CompletePendingCommitForAutomationTests(bool bSuccess);
	bool IsCommitPendingForAutomationTests() const { return bPendingCommit; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void BindSubsystems();
	void RefreshAll();
	void RefreshCatalog();
	void RefreshRows();
	void RefreshSelection();
	void RefreshSlotStrip();
	void RefreshActions();
	void RefreshTime();
	void RefreshProgressState();
	UMSBattleAssistModuleData* GetSelectedModule() const;
	void SetTab(int32 TabIndex);
	void SelectRow(int32 RowIndex);
	void CyclePage(int32 Direction);
	void SelectEquippedSlot(int32 Index);
	void TryEquipSelected();
	void RemoveSelected();
	void ClearLoadout();
	void DevelopSelected();
	bool IsModuleSupported(const UMSBattleAssistModuleData* Module) const;
	bool IsModuleDeveloped(const UMSBattleAssistModuleData* Module) const;
	FString ModuleStateText(const UMSBattleAssistModuleData* Module) const;
	FString ModuleRequirementsText(const UMSBattleAssistModuleData* Module) const;
	FText ValidationReason(const FMSBattleAssistValidationResult& Result) const;
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UMSBattleAssistProgressSubsystem* ResolveProgressSubsystem() const;
	void CommitCandidate(const FMSBattleAssistLoadout& Candidate);
	void HandleCommitCompletion(const FMSBattleAssistLoadout& Candidate, uint64 RequestGeneration,
		int32 RequestSlotIndex, TWeakObjectPtr<UMSMobileSuitData> RequestSuit,
		int64 RequestAccountGeneration, bool bSuccess);
	void CancelPendingAccountChange();
	int64 GetCurrentAccountGeneration() const;

	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleSlot0Clicked();
	UFUNCTION() void HandleSlot1Clicked();
	UFUNCTION() void HandleSlot2Clicked();
	UFUNCTION() void HandleSlot3Clicked();
	UFUNCTION() void HandleSlot4Clicked();
	UFUNCTION() void HandleSlot5Clicked();
	UFUNCTION() void HandleSlot6Clicked();
	UFUNCTION() void HandleSlot7Clicked();
	UFUNCTION() void HandleSlot8Clicked();
	UFUNCTION() void HandleSlot9Clicked();
	UFUNCTION() void HandleTab0Clicked();
	UFUNCTION() void HandleTab1Clicked();
	UFUNCTION() void HandleTab2Clicked();
	UFUNCTION() void HandleTab3Clicked();
	UFUNCTION() void HandleRow0Clicked();
	UFUNCTION() void HandleRow1Clicked();
	UFUNCTION() void HandleRow2Clicked();
	UFUNCTION() void HandleRow3Clicked();
	UFUNCTION() void HandleRow4Clicked();
	UFUNCTION() void HandleRow5Clicked();
	UFUNCTION() void HandlePreviousPageClicked();
	UFUNCTION() void HandleNextPageClicked();
	UFUNCTION() void HandleEquipClicked();
	UFUNCTION() void HandleRemoveClicked();
	UFUNCTION() void HandleClearClicked();
	UFUNCTION() void HandleDevelopClicked();
	UFUNCTION() void HandleProgressChanged();
	UFUNCTION() void HandleOperationCompleted(bool bSuccess, FText Message);
	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);
	UFUNCTION() void HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation);

	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> ModuleButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSVectorSkinWidget>> ModuleSkins;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSVectorSkinWidget>> ModuleIconSkins;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ModuleNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ModuleStateTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSVectorSkinWidget>> SlotSkins;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SlotButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSVectorSkinWidget>> SlotFrames;
	TArray<FPrimaryAssetId> SlotModuleIds;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitPreviewRenderer> PreviewRenderer;
	UPROPERTY(Transient) TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WalletText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuitText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SlotText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ReasonText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageText;
	UPROPERTY(Transient) TObjectPtr<UButton> EquipButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RemoveButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ClearButton;
	UPROPERTY(Transient) TObjectPtr<UButton> DevelopButton;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSBattleAssistProgressSubsystem> ProgressSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> Suit;
	UPROPERTY(Transient) TObjectPtr<UMSSuitDetailWidget> ReturnSuitDetailWidget;
	TWeakObjectPtr<UMSOnlineAccountSubsystem> OnlineAccountSubsystem;

	TArray<TObjectPtr<UMSBattleAssistModuleData>> FilteredModules;
	FMSBattleAssistLoadout WorkingLoadout;
	int32 SlotIndex = INDEX_NONE;
	int32 ActiveTab = 0;
	int32 PageIndex = 0;
	int32 SelectedRow = INDEX_NONE;
	bool bPendingCommit = false;
	uint64 OperationGeneration = 0;
	int64 PendingAccountGeneration = INDEX_NONE;
#if WITH_DEV_AUTOMATION_TESTS
	FMSBattleAssistLoadout TestPendingCandidate;
	uint64 TestPendingOperationGeneration = 0;
#endif
};
