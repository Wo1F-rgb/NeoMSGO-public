// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "MSCommunityWidget.generated.h"

class UBorder;
class APlayerController;
class UButton;
class UEditableTextBox;
class UImage;
class UMSFriendSubsystem;
class UMSLobbyWidget;
class UMSUILockSubsystem;
class UScrollBox;
class USizeBox;
class UTextBlock;

UENUM()
enum class EMSCommunityTab : uint8
{
	Friends,
	Squad
};

UCLASS()
class NEOMSGO_API UMSCommunityFriendRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(const FMSFriendEntry& Entry, class UMSCommunityWidget* InOwner, bool bInviteVisible,
		bool bSessionMembershipConfirmed);
	const FString& GetFriendName() const { return FriendName; }

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION() void HandleInviteClicked();
	void RefreshPresentation();

	UPROPERTY(Transient) TObjectPtr<class UMSCommunityWidget> OwnerWidget;
	UPROPERTY(Transient) TObjectPtr<UBorder> RowBackground;
	UPROPERTY(Transient) TObjectPtr<USizeBox> AvatarBox;
	UPROPERTY(Transient) TObjectPtr<UImage> AvatarImage;
	UPROPERTY(Transient) TObjectPtr<UBorder> StateDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RankText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RateText;
	UPROPERTY(Transient) TObjectPtr<UButton> InviteButton;
	FString FriendName;
	FMSFriendEntry CachedEntry;
	bool bShowInvite = true;
	bool bSessionMembershipConfirmed = false;
};

/** Code-only friend and squad modal opened from the lobby community controls. */
UCLASS()
class NEOMSGO_API UMSCommunityWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Creates the native widget through UE's supported CreateWidget path. */
	UFUNCTION(BlueprintCallable, Category="MS|Community")
	static UMSCommunityWidget* CreateForPlayer(APlayerController* OwningPlayer);
	UFUNCTION(BlueprintCallable, Category="MS|Community") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Community") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Community") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	void SetLobbyWidget(UMSLobbyWidget* InLobbyWidget) { LobbyWidget = InLobbyWidget; }
	void ShowTab(EMSCommunityTab Tab);
	EMSCommunityTab GetCurrentTab() const { return CurrentTab; }
	bool InviteFriend(const FString& FriendName);
	bool AddFriendByName(const FString& FriendName);
	int32 GetFriendRowCount() const;
	int32 GetSquadRowCount() const;
	FString GetLastNotification() const;

	static FText GetStateText(EMSFriendState State);
	static FLinearColor GetStateColor(EMSFriendState State);

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetFriendSubsystemForTesting(UMSFriendSubsystem* InSubsystem) { FriendSubsystemOverride = InSubsystem; EnsureSubsystemBinding(); }
	FReply HandlePointerForAutomationTests(const FPointerEvent& InMouseEvent)
	{
		return NativeOnMouseButtonDown(FGeometry(), InMouseEvent);
	}
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildLayout();
	void RefreshLists();
	UMSFriendSubsystem* ResolveFriendSubsystem() const;
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	void AddNotification(const FString& Message);
	/** サブシステムの変更/承認デリゲートへ購読する(相手承認時の自動入室をUIへ反映 — タスク#144) */
	void EnsureSubsystemBinding();
	void UnbindSubsystemDelegates();
	void HandleFriendsChanged();
	void HandleSquadInviteAccepted(const FString& FriendName);

	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleFriendsTabClicked();
	UFUNCTION() void HandleSquadTabClicked();
	UFUNCTION() void HandleAddFriendClicked();

	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;
	UPROPERTY(Transient) TObjectPtr<UMSFriendSubsystem> FriendSubsystemOverride;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> FriendsScroll;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> SquadScroll;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> FriendNameInput;
	UPROPERTY(Transient) TObjectPtr<UButton> FriendsTabButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SquadTabButton;
	EMSCommunityTab CurrentTab = EMSCommunityTab::Friends;
	TWeakObjectPtr<UMSUILockSubsystem> RegisteredUILock;
	TWeakObjectPtr<UMSFriendSubsystem> BoundFriendSubsystem;
	FDelegateHandle FriendsChangedHandle;
	FDelegateHandle SquadInviteAcceptedHandle;
	bool bScreenRegistered = false;
};
