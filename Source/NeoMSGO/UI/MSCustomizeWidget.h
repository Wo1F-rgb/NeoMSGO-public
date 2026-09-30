#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSCustomizeWidget.generated.h"

class UButton;
class UEditableTextBox;
class UImage;
class UScrollBox;
class UMSDeckSubsystem;
class UMSLobbyWidget;
class UMSMobileSuitData;
class UMSMobileSuitPreviewRenderer;
class UMSSuitDetailWidget;
class UMSSuitSwapWidget;
class UMSUILockSubsystem;
class UTextBlock;
class UMSWeaponIconWidget;
class UProgressBar;
class UMSVectorSkinWidget;

/** Four-card deck overview, distinct from the individual suit customization screen. */
UCLASS()
class NEOMSGO_API UMSCustomizeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 CardCount = 4;

	UFUNCTION(BlueprintCallable, Category="MS|Customize") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Customize") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Customize") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	void SetLobbyWidget(UMSLobbyWidget* InLobbyWidget) { LobbyWidget = InLobbyWidget; }
	/** Called by the detail screen only after its selected-slot request succeeds. */
	void HandleSuitDetailOpened(int32 SlotIndex);
	void HandleSuitDetailClosed();
	void HandleSuitSwapClosed();
	int32 GetSuitCardCount() const { return SlotNameTexts.Num(); }
	FText GetDisplayedSlotName(int32 SlotIndex) const;
	FText GetDisplayedShieldText(int32 SlotIndex) const;
	FText GetDisplayedDeckCost() const;

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem);
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
	void SetSuitDetailWidgetForTesting(UMSSuitDetailWidget* InWidget);
	void SetSuitSwapWidgetForTesting(UMSSuitSwapWidget* InWidget);
	FLinearColor GetDeckCostTextColorForAutomationTests() const;
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void CloseImmediate();
	void BuildReferenceLayout();
	void BindDeckSubsystem();
	void RefreshAll();
	void RefreshCard(int32 SlotIndex);
	void RefreshDeckCost();
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	UFUNCTION() void HandleDeckChanged(int32 ChangedSlotIndex);
	UFUNCTION(BlueprintCallable, Category="MS|Customize", meta=(AllowPrivateAccess="true"))
	void HandleBackClicked();
	UFUNCTION() void HandleCard0Clicked();
	UFUNCTION() void HandleCard1Clicked();
	UFUNCTION() void HandleCard2Clicked();
	UFUNCTION() void HandleCard3Clicked();
	UFUNCTION() void HandleOwnedClicked();
	UFUNCTION() void HandleDevelopClicked();
	void HandleCardClicked(int32 SlotIndex);
	bool EnsureSuitDetailWidget();
	bool EnsureSuitSwapWidget();

	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CardButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PreviewImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotNumberTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSVectorSkinWidget>> CardSkins;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSWeaponIconWidget>> WeaponIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotStatsTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ModuleTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ShieldTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSMobileSuitPreviewRenderer>> PreviewRenderers;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeckCostText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderIdentityText;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> DeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;
	UPROPERTY(Transient) TObjectPtr<UMSSuitDetailWidget> SuitDetailWidget;
	UPROPERTY(Transient) TObjectPtr<UMSSuitSwapWidget> SuitSwapWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	int32 SelectedSlotIndex = 0;
	uint64 SelectionRequestGeneration = 0;
	int32 PendingDetailSlotIndex = INDEX_NONE;
};
