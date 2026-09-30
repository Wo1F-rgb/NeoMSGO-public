// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/MSRoomDescriptor.h"
#include "MSRoomCreateWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UMSLobbyWidget;
class UMSRoomListEntryWidget;
class UMSRoomRegistrySubsystem;
class UScrollBox;
class USpinBox;
class UVerticalBox;

/** Code-only room creation and public room browser (with offline direct-IP fallback). */
UCLASS()
class NEOMSGO_API UMSRoomCreateWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MS|Room") void OpenCreate();
	/** Opens the create form with a map card's map preselected. */
	void OpenCreateForMap(FName MapId);
	UFUNCTION(BlueprintCallable, Category = "MS|Room") void OpenJoin();
	UFUNCTION(BlueprintCallable, Category = "MS|Room") void Close();
	void SetLobbyWidget(UMSLobbyWidget* InLobby) { LobbyWidget = InLobby; }

	UButton* GetRoomStartButton() const { return RoomStartButton; }
	UButton* GetJoinButton() const { return JoinButton; }
	UButton* GetRefreshButton() const { return RefreshButton; }
	UButton* GetAdvancedJoinToggle() const { return AdvancedJoinToggle; }
	USpinBox* GetBotCountSpinBox() const { return BotCountSpinBox; }
	UComboBoxString* GetDifficultyComboBox() const { return DifficultyComboBox; }
	UCheckBox* GetFillTeamsWithBotsCheckBox() const { return FillTeamsWithBotsCheckBox; }
	UEditableTextBox* GetRoomNameInput() const { return RoomNameInput; }
	UEditableTextBox* GetJoinAddressInput() const { return JoinAddressInput; }
	UEditableTextBox* GetSearchTextBox() const { return SearchTextBox; }
	UScrollBox* GetRoomListScrollBox() const { return RoomListScrollBox; }
	int32 GetDisplayedRoomCount() const;
	UMSRoomListEntryWidget* GetDisplayedRoomEntry(int32 Index) const;

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetSearchTextForAutomationTests(const FString& Search);
	void SetJoinHandlerForAutomationTests(TFunction<void(const FString&)> InHandler)
	{
		JoinHandlerForAutomationTests = MoveTemp(InHandler);
	}
	int32 GetPushScreenCountForAutomationTests() const { return PushScreenCountForAutomationTests; }
	int32 GetPopScreenCountForAutomationTests() const { return PopScreenCountForAutomationTests; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildLayout();
	void ShowCreate(bool bCreate);
	void StartRoomPolling();
	void StopRoomPolling();
	void RebuildRoomList();
	void BindRoomStatus();
	void UnbindRoomStatus();
	void RefreshStatusPresentation();

	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleJoinClicked();
	UFUNCTION() void HandleOnlineRequestFinished(FName Operation, bool bSuccess, FText Message, int64 Generation);
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> OnlineStatusText;
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleRefreshClicked();
	UFUNCTION() void HandleAdvancedJoinClicked();
	UFUNCTION() void HandleSearchTextChanged(const FText& Text);
	UFUNCTION() void HandleRoomListUpdated(TArray<FMSRoomDescriptor> Rooms);
	UFUNCTION() void HandleRoomListStatusChanged();
	UFUNCTION() void HandleRoomStatusChanged();
	UFUNCTION() void HandleRoomSelected(FMSRoomDescriptor Room);
	UFUNCTION() void HandleFillTeamsWithBotsChanged(bool bIsChecked);

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> CreatePanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> JoinPanel;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> RoomNameInput;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> MapComboBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> RuleComboBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> BotCountSpinBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> FillTeamsWithBotsCheckBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> DifficultyComboBox;
	UPROPERTY(Transient) TObjectPtr<UButton> RoomStartButton;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SearchTextBox;
	UPROPERTY(Transient) TObjectPtr<UButton> RefreshButton;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> RoomListScrollBox;
	UPROPERTY(Transient) TObjectPtr<UButton> AdvancedJoinToggle;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> AdvancedJoinPanel;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> JoinAddressInput;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSRoomListEntryWidget>> DisplayedRoomEntries;
	UPROPERTY(Transient) TArray<FMSRoomDescriptor> LatestRooms;
	TArray<FName> PlayableMapIds;
	FString RegistryAddress = TEXT("http://127.0.0.1:17788");
	bool bAdvancedJoinExpanded = false;
	bool bScreenRegistered = false;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void(const FString&)> JoinHandlerForAutomationTests;
	int32 PushScreenCountForAutomationTests = 0;
	int32 PopScreenCountForAutomationTests = 0;
#endif
};
