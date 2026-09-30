// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/MSBattleRuleRow.h"
#include "MSLobbyWidget.generated.h"

class AMSGameMode;
class AMSGameState;
class UBorder;
class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UMSBriefingWidget;
class UMSCommunityWidget;
class UMSCustomizeWidget;
class UMSFriendSubsystem;
class UMSLoginWidget;
class UMSResultWidget;
class UMSRoomCreateWidget;
class UMSSettingsWidget;
class UMSShopWidget;
class UMSUnitRegistrySubsystem;
class UMSGameInstance;
class UScaleBox;
class USizeBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
struct FMSFriendEntry;

UENUM()
enum class EMSLobbyPanel : uint8
{
	Lobby,
	Squad,
	MissionChoice,
	Operation
};

/** P8-7 code-only bridge/lobby, squad and operation-selection screen. */
UCLASS()
class NEOMSGO_API UMSLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Lobby") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void ShowPanel(EMSLobbyPanel Panel);
	/** Opens the same customization flow as the lobby card; also used by rendered PIE acceptance. */
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void OpenCustomizeScreen();
	/** Opens the result overlay through the normal lobby ownership path. */
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void OpenResultScreen();
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") void SelectBattleRule(int32 RuleIndex);
	UFUNCTION(BlueprintCallable, Category="MS|Lobby") bool ApplySelectedBattleRule();
	void RefreshPilotList() { RefreshRoster(); }

	static const TArray<FMSBattleRuleRow>& GetBattleRules();
	static bool ApplyBattleRule(const FMSBattleRuleRow& Rule, AMSGameMode* GameMode, AMSGameState* GameState);
	/** Resolves a selected map change and stores the state needed after OpenLevel. */
	static bool PrepareMapTravel(UMSGameInstance* GameInstance, int32 RuleIndex,
		const FString& CurrentLevelName, FString& OutDestinationLevelName);
	/** Applies and consumes a pending rule after map travel. */
	static bool ConsumePendingBattleRule(UMSGameInstance* GameInstance, AMSGameMode* GameMode,
		AMSGameState* GameState, FText& OutRuleName);
	static TArray<FString> BuildRosterNames(const UMSUnitRegistrySubsystem* Registry);
	static TArray<FMSFriendEntry> BuildPilotListEntries(const UMSFriendSubsystem* Friends);

	int32 GetSelectedBattleRuleIndex() const { return SelectedRuleIndex; }
	int32 GetDisplayedRosterCount() const;
	int32 GetPartySlotCount() const;
	int32 GetMapCardCount() const { return MapCardButtons.Num(); }
	int32 GetCurrentMapPage() const { return CurrentMapPage; }
	int32 GetMapPageCount() const;
	bool IsMapCardSelected(int32 MapIndex) const;
	FLinearColor GetBackgroundColor() const;
	UMSShopWidget* GetShopWidget() const { return ShopWidget; }
	UMSCustomizeWidget* GetCustomizeWidget() const { return CustomizeWidget; }
	UMSBriefingWidget* GetBriefingWidget() const { return BriefingWidget; }
	UMSResultWidget* GetResultWidget() const { return ResultWidget; }
	UMSCommunityWidget* GetCommunityWidget() const { return CommunityWidget; }
	UMSRoomCreateWidget* GetRoomCreateWidget() const { return RoomCreateWidget; }
	UButton* GetQuickSortieButton() const { return QuickSortieButton; }
	UButton* GetCreateRoomButton() const { return CreateRoomButton; }
	UButton* GetJoinRoomButton() const { return JoinRoomButton; }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	UButton* GetLogoutButtonForTesting() const { return LogoutButton; }
	void SetLoginWidgetForTesting(UMSLoginWidget* InWidget) { LoginWidget = InWidget; }
	void SetShopWidgetForTesting(UMSShopWidget* InWidget);
	void SetCustomizeWidgetForTesting(UMSCustomizeWidget* InWidget);
	void SetBriefingWidgetForTesting(UMSBriefingWidget* InWidget);
	void SetResultWidgetForTesting(UMSResultWidget* InWidget);
	void SetCommunityWidgetForTesting(UMSCommunityWidget* InWidget);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildReferenceLayout();
	void RefreshPersistentData();
	void RefreshRoster();
	void RefreshRuleSelection();
	void RefreshMapSelection();
	void SelectMap(int32 MapIndex);
	void RestoreBottomStripLayout();
	void SelectEmblem(int32 Index);
	void BindBattleEnded();
	UFUNCTION() void RefreshRoomStatus();
	bool EnsureShopWidget();
	bool EnsureCustomizeWidget();
	bool EnsureResultWidget();
	bool EnsureCommunityWidget();
	bool EnsureRoomCreateWidget();
	void RefreshParty();
	/** フレンド/小隊データ変更時に小隊6スロットパネルとパイロット一覧を自動更新する(タスク#144) */
	void BindFriendsChanged();
	void HandleFriendsChangedNative();
	class UWidget* BuildPartySelfSlot();
	class UWidget* BuildPartyMemberSlot(const FMSFriendEntry& Entry);
	class UWidget* BuildPartyEmptySlot();

	UFUNCTION() void HandleLobbyTabClicked();
	UFUNCTION() void HandleSquadTabClicked();
	UFUNCTION() void HandleOperationTabClicked();
	UFUNCTION(BlueprintCallable, Category="MS|Lobby", meta=(AllowPrivateAccess="true"))
	void HandleMissionClicked();
	UFUNCTION() void HandleQuickSortieClicked();
	UFUNCTION() void HandleCreateRoomClicked();
	UFUNCTION() void HandleJoinRoomClicked();
	UFUNCTION() void HandleShopClicked();
	UFUNCTION(BlueprintCallable, Category="MS|Lobby", meta=(AllowPrivateAccess="true"))
	void HandleCustomizeClicked();
	UFUNCTION() void HandleCommunityClicked();
	/** Opens the shared Settings screen (camera/keybinds/audio) from the global rail. */
	UFUNCTION() void HandleSettingsClicked();
	/** EXIT on the global rail: clears the login state and returns to the login screen. */
	UFUNCTION() void HandleLogoutClicked();
	UFUNCTION() void HandleRule0Clicked();
	UFUNCTION() void HandleRule1Clicked();
	UFUNCTION() void HandleRule2Clicked();
	UFUNCTION() void HandlePreviousMapPageClicked();
	UFUNCTION() void HandleNextMapPageClicked();
	UFUNCTION() void HandleMap0Clicked();
	UFUNCTION() void HandleMap1Clicked();
	UFUNCTION() void HandleMap2Clicked();
	UFUNCTION() void HandleMap3Clicked();
	UFUNCTION() void HandleMap4Clicked();
	UFUNCTION() void HandleMap5Clicked();
	UFUNCTION() void HandleMap6Clicked();
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleSquadNameChanged(const FText& Text);
	UFUNCTION() void HandleEmblem0Clicked();
	UFUNCTION() void HandleEmblem1Clicked();
	UFUNCTION() void HandleEmblem2Clicked();
	UFUNCTION() void HandleEmblem3Clicked();
	UFUNCTION() void HandleEmblem4Clicked();
	UFUNCTION() void HandleEmblem5Clicked();
	UFUNCTION() void HandleBattleEnded(bool bPlayerWon);
	UFUNCTION() void HandlePartyCommentChanged(const FText& Text);
	UFUNCTION() void HandlePartyInviteClicked();
	UFUNCTION() void HandlePartyInfoClicked();
	UFUNCTION() void HandlePartyLeaveClicked();
	UFUNCTION() void HandleResumeSavedPartyClicked();

	UPROPERTY(Transient) TObjectPtr<UBorder> Background;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BreadcrumbText;
	UPROPERTY(Transient) TObjectPtr<UButton> ResumeSavedPartyButton;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LobbyPanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SquadPanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> MissionChoicePanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> OperationPanel;
	/** Fixed 1920-design-space frames for secondary pages; the authored lobby surface owns scaling. */
	UPROPERTY(Transient) TObjectPtr<UScaleBox> SquadPageScale;
	UPROPERTY(Transient) TObjectPtr<UScaleBox> MissionChoicePageScale;
	UPROPERTY(Transient) TObjectPtr<UScaleBox> OperationPageScale;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SquadSummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PilotStatsText;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PilotAvatarBox;
	UPROPERTY(Transient) TObjectPtr<UImage> PilotAvatarImage;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SquadNameInput;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmblemSelectionText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RosterBox;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> LobbyRosterBox;
	UPROPERTY(Transient) TObjectPtr<USizeBox> BottomStripSizeBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> RuleCards;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MapCardButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> MapCardBorders;
	UPROPERTY(Transient) TObjectPtr<UButton> PreviousMapPageButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextMapPageButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapPageLabel;
	UPROPERTY(Transient) TObjectPtr<UMSBriefingWidget> BriefingWidget;
	UPROPERTY(Transient) TObjectPtr<UMSShopWidget> ShopWidget;
	UPROPERTY(Transient) TObjectPtr<UMSCustomizeWidget> CustomizeWidget;
	UPROPERTY(Transient) TObjectPtr<UMSResultWidget> ResultWidget;
	UPROPERTY(Transient) TObjectPtr<UMSCommunityWidget> CommunityWidget;
	UPROPERTY(Transient) TObjectPtr<UMSRoomCreateWidget> RoomCreateWidget;
	UPROPERTY(Transient) TObjectPtr<UButton> QuickSortieButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CreateRoomButton;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinRoomButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LogoutButton;
	UPROPERTY(Transient) TObjectPtr<UMSSettingsWidget> SettingsWidget;
	UPROPERTY(Transient) TObjectPtr<UMSLoginWidget> LoginWidget;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> PartyCommentInput;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PartyRowTop;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PartyRowBottom;
	UPROPERTY(Transient) TObjectPtr<UBorder> LobbyNoticeFrame;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LobbyNoticeTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LobbyNoticeBody;
	UPROPERTY(Transient) TObjectPtr<UBorder> LobbyPartyFrame;
	TWeakObjectPtr<AMSGameState> BoundGameState;
	TWeakObjectPtr<UMSFriendSubsystem> BoundFriendSubsystem;
	FDelegateHandle FriendsChangedHandle;
	int32 SelectedRuleIndex = 0;
	int32 CurrentMapPage = 0;
	bool bUpdatingSquadName = false;
	bool bUpdatingPartyComment = false;
};
