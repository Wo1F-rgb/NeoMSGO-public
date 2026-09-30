#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MSMapWidgetBase.h"
#include "MSBriefingWidget.generated.h"

class UBorder;
class UButton;
class UGridPanel;
class UImage;
class UMSLobbyWidget;
class UMSSpawnSelectWidget;
class UMSSortieWidget;
class UMSHangarWidget;
class UTextBlock;

/** Compact battlefield map embedded in the briefing's left column. */
UCLASS()
class NEOMSGO_API UMSBriefingMiniMapWidget : public UMSMapWidgetBase
{
	GENERATED_BODY()

public:
	void ShowEmbedded();

protected:
	virtual FVector2D GetMapSize() const override { return FVector2D(313.0f, 204.0f); }
	virtual float GetMapEdgeMarginPx() const override { return 0.0f; }
	// Strategic preview: supply icons at a base would obscure its letter at this scale.
	virtual bool ShowPickupMarkers() const override { return false; }
};

/**
 * MSGO-style pre-operation briefing. Countdown is deliberately driven by the
 * Slate widget tick, not FTimerManager, so world pause does not stop it.
 */
UCLASS()
class NEOMSGO_API UMSBriefingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr float BriefingDurationSeconds = 60.0f;

	UFUNCTION(BlueprintCallable, Category="MS|Briefing") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Briefing") void Close();
	/** Closes without restoring the lobby while battle-end cleanup drains the foreground stack. */
	void CloseForBattleEnd();
	void SetLobbyWidget(UMSLobbyWidget* InLobbyWidget) { LobbyWidget = InLobbyWidget; }
	UFUNCTION(BlueprintCallable, Category="MS|Briefing") bool StartOperation();
	UFUNCTION(BlueprintCallable, Category="MS|Briefing") void SetRoomInformation(const FText& BattleRuleName, const FText& MapName);
	UFUNCTION(BlueprintPure, Category="MS|Briefing") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	UFUNCTION(BlueprintPure, Category="MS|Briefing") float GetRemainingSeconds() const { return RemainingSeconds; }
	FText GetSelectedBattleRuleName() const { return SelectedBattleRuleName; }

	int32 GetPilotCardCount() const { return PilotCardCount; }
	FText GetDisplayedPilotLabel(int32 Index) const;
	UMSSortieWidget* GetSortieWidget() const { return SortieWidget; }
	UMSSpawnSelectWidget* GetSpawnSelectWidget() const { return SpawnSelectWidget; }
	void HandleEquipmentClosed();
	UMSHangarWidget* GetEquipmentWidget() const { return EquipmentWidget; }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void AdvanceCountdownForAutomationTests(float DeltaTime);
	void SetSortieWidgetForTesting(UMSSortieWidget* InWidget) { SortieWidget = InWidget; }
	void SetSpawnSelectWidgetForTesting(UMSSpawnSelectWidget* InWidget) { SpawnSelectWidget = InWidget; }
	void RefreshPilotCardsForAutomationTests() { RefreshPilotCards(); }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void AdvanceCountdown(float DeltaTime);
	void RefreshCountdownText();
	void RefreshRoomInformation();
	void RefreshPilotCards();
	void SelectTeam(int32 TeamIndex);
	bool EnsureSortieWidget();
	bool EnsureSpawnSelectWidget();
	void CloseInternal(bool bReturnToLobby);

	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleSuitConfirmationClicked();
	UFUNCTION() void HandleEquipmentClicked();
	UFUNCTION() void HandleTeamASelected();
	UFUNCTION() void HandleTeamBSelected();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandlePreviousPilots();
	UFUNCTION() void HandleNextPilots();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountdownText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoomInformationText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedTeamText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TeamASummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TeamBSummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PilotPageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShellPilotText;
	UPROPERTY(Transient) TObjectPtr<UImage> ShellPilotAvatar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShellRosterText;
	UPROPERTY(Transient) TObjectPtr<UButton> PreviousPilotsButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextPilotsButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PilotNameTexts;
	UPROPERTY(Transient) TObjectPtr<UGridPanel> PilotGrid;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SuitConfirmationButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquipmentCaption;
	UPROPERTY(Transient) TObjectPtr<UMSHangarWidget> EquipmentWidget;
	UPROPERTY(Transient) TObjectPtr<UMSBriefingMiniMapWidget> MiniMap;
	UPROPERTY(Transient) TObjectPtr<UMSSortieWidget> SortieWidget;
	UPROPERTY(Transient) TObjectPtr<UMSSpawnSelectWidget> SpawnSelectWidget;
	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;

	FText SelectedBattleRuleName;
	FText SelectedMapName;
	float RemainingSeconds = BriefingDurationSeconds;
	int32 SelectedTeamIndex = 0;
	int32 PilotCardCount = 0;
	int32 PilotPage = 0;
	bool bTransitionStarted = false;
	bool bEquipmentOpen = false;
	float RosterRefreshElapsed = 0;
	FString LastRosterSignature;
};
