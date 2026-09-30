// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Core/MSDummyNameLibrary.h"
#include "Core/MSGameInstance.h"
#include "Engine/World.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Gameplay/MSGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSCommunityWidget.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCommunityWidgetTest, "NeoMSGO.UI.Community.CreationTabsInvites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCommunityModalLockTest, "NeoMSGO.UI.Community.ModalLockEscAndClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCommunityNativeDestructLockTest, "NeoMSGO.UI.Community.NativeDestructReleasesOwnedLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSCommunityWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSCommunityWidgetTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);

		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			FURL URL;
			URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FScopedWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	template <typename T>
	TStrongObjectPtr<T> MakeWidget(UWorld* World)
	{
		TStrongObjectPtr<T> Widget(NewObject<T>(World));
		Widget->Initialize();
		Widget->InitializeForAutomationTests();
		return Widget;
	}

	FString CollectText(const UWidget* Widget)
	{
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget)) return Text->GetText().ToString();
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
			return UserWidget->WidgetTree ? CollectText(UserWidget->WidgetTree->RootWidget) : FString();
		FString Result;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (!Result.IsEmpty()) Result += TEXT("\n");
				Result += CollectText(Panel->GetChildAt(Index));
			}
		}
		else if (const UContentWidget* Content = Cast<UContentWidget>(Widget))
		{
			Result = CollectText(Content->GetContent());
		}
		return Result;
	}
}

bool FMSCommunityWidgetTest::RunTest(const FString& Parameters)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_CommunityAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Friends->SetSaveSlotForAutomationTests(Slot);
	Friends->ResetAndReloadForAutomationTests();
	Friends->SeedDefaultFriendsForAutomationTests();

	MSCommunityWidgetTests::FScopedWorld Scope;
	APlayerController* Controller = Scope.World->SpawnActor<APlayerController>();
	Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	TStrongObjectPtr<UMSCommunityWidget> Widget(CreateWidget<UMSCommunityWidget>(Controller));
	Widget->Initialize(); Widget->InitializeForAutomationTests(); Widget->SetFriendSubsystemForTesting(Friends.Get());
	TestFalse(TEXT("community starts closed"), Widget->IsOpen());
	Widget->Open();
	TestTrue(TEXT("community opens"), Widget->IsOpen());
	TestEqual(TEXT("all seeded friends have rows"), Widget->GetFriendRowCount(), Friends->GetFriends().Num());
	TestEqual(TEXT("friend tab is initial tab"), Widget->GetCurrentTab(), EMSCommunityTab::Friends);
	UScrollBox* FriendsRows = CastChecked<UScrollBox>(Widget->GetWidgetFromName(TEXT("FriendsScroll")));
	const FString SavedFriendPresentation = MSCommunityWidgetTests::CollectText(FriendsRows);
	TestTrue(TEXT("saved friend presence is explicitly unverified"), SavedFriendPresentation.Contains(TEXT("接続未確認")));
	TestTrue(TEXT("saved friend rate is explicitly unavailable"), SavedFriendPresentation.Contains(TEXT("RATE ---")));
	TestTrue(TEXT("unfetched friend rank is explicitly unavailable"), SavedFriendPresentation.Contains(TEXT("RANK ---")));
	TestFalse(TEXT("saved friend state is not presented as a live battle state"), SavedFriendPresentation.Contains(TEXT("戦闘中")));
	TestFalse(TEXT("saved friend rate is not presented as a live rate"), SavedFriendPresentation.Contains(TEXT("RATE 1680")));
	Widget->ShowTab(EMSCommunityTab::Squad);
	TestEqual(TEXT("squad tab can be selected"), Widget->GetCurrentTab(), EMSCommunityTab::Squad);

	// Persisted bIsSquadMember is historical data.  Reloading clears the
	// session confirmation, so it must not repopulate the current squad tab.
	Friends->ReloadFriends();
	TestEqual(TEXT("saved squad membership is not current participation"), Widget->GetSquadRowCount(), 0);

	// タスク#144: 招待送信 → 相手承認 → 自動入室のフロー
	const FString InviteTarget = MSDummyNames::MakeDummyName(3);
	TestTrue(TEXT("invite below capacity succeeds"), Widget->InviteFriend(InviteTarget));
	TestTrue(TEXT("invite marks the friend as pending, not joined"), Friends->HasPendingSquadInvite(InviteTarget));
	TestFalse(TEXT("friend does not join before accepting"), Friends->FindFriend(InviteTarget)->bIsSquadMember);
	TestTrue(TEXT("invite notification identifies local persistence and unknown connection"),
		Widget->GetLastNotification().Contains(TEXT("ローカル記録")) && Widget->GetLastNotification().Contains(TEXT("接続未確認")));
	TestFalse(TEXT("duplicate invite is rejected while pending"), Widget->InviteFriend(InviteTarget));

	// 相手のAcceptをシミュレート → 自動で小隊入室し、UIが手動リフレッシュなしで追従する
	Friends->FlushPendingAutoAcceptsForAutomationTests();
	TestTrue(TEXT("accepting the invite auto-joins the squad"), Friends->FindFriend(InviteTarget)->bIsSquadMember);
	TestFalse(TEXT("pending flag clears after acceptance"), Friends->HasPendingSquadInvite(InviteTarget));
	TestTrue(TEXT("acceptance notification identifies the local simulation"),
		Widget->GetLastNotification().Contains(TEXT("シミュレート")) && Widget->GetLastNotification().Contains(TEXT("小隊参加表示")));
	TestEqual(TEXT("only this session's one accepted invite enters the current squad"), Widget->GetSquadRowCount(), 1);
	TestTrue(TEXT("accepted invite is confirmed in this session"), Friends->IsSquadMembershipConfirmedThisSession(InviteTarget));

	for (const FMSFriendEntry& Entry : TArray<FMSFriendEntry>(Friends->GetFriends()))
	{
		if (Friends->GetSquadMemberCount() >= 5) break;
		Friends->SetFriendSquadMember(Entry.Name, true);
	}
	FString RejectedName;
	for (const FMSFriendEntry& Entry : Friends->GetFriends()) if (!Entry.bIsSquadMember) { RejectedName = Entry.Name; break; }
	TestFalse(TEXT("invite is rejected when five-member squad is full (self takes the sixth slot)"), Widget->InviteFriend(RejectedName));
	TestTrue(TEXT("full-squad notification explains rejection"), Widget->GetLastNotification().Contains(TEXT("招待できません")));
	TestFalse(TEXT("rejected friend remains outside squad"), Friends->FindFriend(RejectedName)->bIsSquadMember);

	TestTrue(TEXT("arbitrary friend name can be added"), Widget->AddFriendByName(TEXT("New Pilot")));
	TestNotNull(TEXT("new friend appears in data immediately"), Friends->FindFriend(TEXT("New Pilot")));
	Widget->Close();
	TestFalse(TEXT("close button behavior is testable through Close"), Widget->IsOpen());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

bool FMSCommunityModalLockTest::RunTest(const FString&)
{
	MSCommunityWidgetTests::FScopedWorld Scope;
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("fixture provides the UI lock subsystem"), Lock);
	if (!Lock) return false;

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSCommunityWidgetTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSCommunityWidget> Community = MSCommunityWidgetTests::MakeWidget<UMSCommunityWidget>(Scope.World);
	Community->SetLobbyWidget(Lobby.Get());
	Lobby->Open();
	Community->Open();
	Community->Open();

	TestTrue(TEXT("opening community hides the lobby"), Community->IsOpen() && !Lobby->IsOpen());
	TestTrue(TEXT("community is registered above the lobby lock"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("Esc request closes the community modal"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc leaves community closed"), Community->IsOpen());
	TestTrue(TEXT("Esc restores the lobby"), Lobby->IsOpen());
	TestTrue(TEXT("the lobby lock remains after community closes"), Lock->IsAnyScreenOpen());

	Community->Open();
	UButton* CloseButton = Cast<UButton>(Community->GetWidgetFromName(TEXT("CommunityCloseButton")));
	TestNotNull(TEXT("community close button exists"), CloseButton);
	if (CloseButton) CloseButton->OnClicked.Broadcast();
	TestFalse(TEXT("close button closes the community modal"), Community->IsOpen());
	TestTrue(TEXT("close button keeps the parent lobby lock"), Lock->IsAnyScreenOpen());

	Community->Open();
	const TSet<FKey> PressedButtons;
	const FPointerEvent RightClick(0, FVector2D::ZeroVector, FVector2D::ZeroVector, PressedButtons,
		EKeys::RightMouseButton, 0.0f, FModifierKeysState());
	TestTrue(TEXT("right-click is consumed by community"), Community->HandlePointerForAutomationTests(RightClick).IsEventHandled());
	TestFalse(TEXT("right-click closes the community modal"), Community->IsOpen());
	TestTrue(TEXT("right-click keeps the parent lobby lock"), Lock->IsAnyScreenOpen());

	// Close is idempotent, and opening twice must still produce one modal entry.
	Community->Open();
	Community->Open();
	Community->Close();
	Community->Close();
	TestFalse(TEXT("double close leaves community closed"), Community->IsOpen());
	TestTrue(TEXT("double close keeps the parent lobby lock"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("one subsequent Esc closes the lobby as the next top screen"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("the parent lobby is closed by the next Esc"), Lobby->IsOpen());
	TestFalse(TEXT("all modal locks are gone after the parent closes"), Lock->IsAnyScreenOpen());
	return true;
}

bool FMSCommunityNativeDestructLockTest::RunTest(const FString&)
{
	MSCommunityWidgetTests::FScopedWorld Scope;
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("fixture provides the UI lock subsystem"), Lock);
	if (!Lock) return false;

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSCommunityWidgetTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSCommunityWidget> Community = MSCommunityWidgetTests::MakeWidget<UMSCommunityWidget>(Scope.World);
	Community->SetLobbyWidget(Lobby.Get());
	Lobby->Open();
	Community->Open();
	TestTrue(TEXT("community owns a lock while open"), Lock->IsAnyScreenOpen());

	// This transient world has no GameViewport. Explicitly build the Slate object so
	// releasing it runs SObjectWidget's real NativeDestruct path while the UObject lives.
	TSharedPtr<SWidget> SlateCommunity = Community->TakeWidget();
	TestTrue(TEXT("community has a live Slate widget before destruction"), Community->GetCachedWidget().IsValid());
	Community->RemoveFromParent();
	SlateCommunity.Reset();
	TestFalse(TEXT("NativeDestruct closes the still-live community UObject"), Community->IsOpen());
	TestTrue(TEXT("NativeDestruct restores the lobby display"), Lobby->IsOpen());
	TestTrue(TEXT("parent lobby lock survives community destruction"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("the destroyed community leaves the lobby as the top screen"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("the parent lobby closes after the destroyed modal is gone"), Lobby->IsOpen());
	TestFalse(TEXT("destruction does not leak a community lock"), Lock->IsAnyScreenOpen());
	return true;
}

#endif
