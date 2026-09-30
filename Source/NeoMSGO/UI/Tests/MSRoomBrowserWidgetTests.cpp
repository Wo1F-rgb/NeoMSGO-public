// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSRoomRegistrySubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "UI/MSRoomCreateWidget.h"
#include "UI/MSRoomListEntryWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomBrowserWidgetTest,
	"NeoMSGO.UI.RoomBrowser.FilterRefreshSelectionAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSRoomBrowserWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, false, TEXT("MSRoomBrowserWidgetTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);

		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	FMSRoomDescriptor MakeRoom(
		const TCHAR* RoomName, const TCHAR* HostName, const TCHAR* Address,
		const FName MapId, const EMSRoomState State, const int32 Players,
		const int32 BotCount = 0, const bool bFillTeamsWithBots = false)
	{
		FMSRoomDescriptor Room;
		Room.RoomId = FGuid::NewGuid();
		Room.InstanceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Room.BuildId = MSRoomProtocol::BuildId;
		Room.Settings.RoomName = RoomName;
		Room.Settings.MapId = MapId;
		Room.Settings.BotCount = BotCount;
		Room.Settings.bFillTeamsWithBots = bFillTeamsWithBots;
		Room.HostDisplayName = HostName;
		Room.Address = Address;
		Room.CurrentPlayerCount = Players;
		Room.MaxPlayerCount = Room.Settings.GetMaxCombatants();
		Room.State = State;
		return Room;
	}
}

bool FMSRoomBrowserWidgetTest::RunTest(const FString&)
{
	using namespace MSRoomBrowserWidgetTests;
	FScopedWorld Scope;
	Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>()->SetAuthModeForAutomationTests(EMSOnlineAuthMode::OfflineLocal);
	UMSRoomRegistrySubsystem* Registry =
		Scope.GameInstance->GetSubsystem<UMSRoomRegistrySubsystem>();
	UMSUILockSubsystem* Lock =
		Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("registry subsystem exists"), Registry);
	TestNotNull(TEXT("UI lock subsystem exists"), Lock);
	if (!Registry || !Lock) return false;

	int32 Requests = 0;
	Registry->SetRequestSenderForAutomationTests(
		[&Requests](const FString&) { ++Requests; return true; });
	TStrongObjectPtr<UMSRoomCreateWidget> Widget(
		NewObject<UMSRoomCreateWidget>(Scope.World));
	Widget->Initialize();
	Widget->InitializeForAutomationTests();
	const UComboBoxString* MapOptions = Cast<UComboBoxString>(
		Widget->GetWidgetFromName(TEXT("RoomMapComboBox")));
	TestNotNull(TEXT("room creation has a map selector"), MapOptions);
	if (MapOptions)
	{
		TestEqual(TEXT("room creation offers the three current playable maps"), MapOptions->GetOptionCount(), 3);
		TestEqual(TEXT("new Taigan is offered in place of archived NewYark"),
			MapOptions->GetOptionAtIndex(1), FString(TEXT("ニューヤーク -対岸の銃火-")));
		bool bArchivedOptionPresent = false;
		for (int32 Index = 0; Index < MapOptions->GetOptionCount(); ++Index)
		{
			bArchivedOptionPresent |= MapOptions->GetOptionAtIndex(Index) == TEXT("ニューヤーク");
		}
		TestFalse(TEXT("archived NewYark is absent from room creation"), bArchivedOptionPresent);
	}

	FString JoinedAddress;
	Widget->SetJoinHandlerForAutomationTests(
		[&JoinedAddress](const FString& Address) { JoinedAddress = Address; });
	Widget->OpenJoin();
	TestEqual(TEXT("OpenJoin starts with one immediate registry request"), Requests, 1);
	TestTrue(TEXT("OpenJoin starts the polling timer"), Registry->IsPolling());
	TestTrue(TEXT("OpenJoin pushes exactly one UI screen"), Lock->IsAnyScreenOpen());
	TestEqual(TEXT("the screen records one PushScreen call"),
		Widget->GetPushScreenCountForAutomationTests(), 1);
	TestEqual(TEXT("no PopScreen occurs before close"),
		Widget->GetPopScreenCountForAutomationTests(), 0);

	// Same display name is intentional (#214 avoidance): identity and selection stay descriptor-based.
	const FMSRoomDescriptor Alpha = MakeRoom(
		TEXT("Alpha Squad"), TEXT("DuplicateHost"), TEXT("192.0.2.11:7777"),
		TEXT("NewYark"), EMSRoomState::Waiting, 3, 4);
	const FMSRoomDescriptor Bravo = MakeRoom(
		TEXT("Bravo Team"), TEXT("DuplicateHost"), TEXT("192.0.2.12:7777"),
		TEXT("Daibao"), EMSRoomState::InProgress, 7, 2, true);
	Registry->SetLastKnownRoomsForAutomationTests({ Alpha, Bravo });
	TestEqual(TEXT("duplicate host names still render as two independent rows"),
		Widget->GetDisplayedRoomCount(), 2);

	UMSRoomListEntryWidget* FirstEntry = Widget->GetDisplayedRoomEntry(0);
	TestNotNull(TEXT("room row is dynamically created"), FirstEntry);
	if (FirstEntry)
	{
		UHorizontalBox* HeaderRow = Cast<UHorizontalBox>(
			Widget->GetWidgetFromName(TEXT("RoomListHeader")));
		TestNotNull(TEXT("room list header is present"), HeaderRow);
		if (HeaderRow)
		{
			TestEqual(TEXT("room list keeps separate human and enemy-bot columns"),
				HeaderRow->GetChildrenCount(), 6);
			const TArray<FString> ExpectedHeaders = {
				TEXT("ルーム名"), TEXT("マップ"), TEXT("人間"),
				TEXT("BOT"), TEXT("状態"), TEXT("ホスト")};
			for (int32 Index = 0; Index < ExpectedHeaders.Num(); ++Index)
			{
				const USizeBox* HeaderSize = Cast<USizeBox>(HeaderRow->GetChildAt(Index));
				const UTextBlock* HeaderText = HeaderSize
					? Cast<UTextBlock>(HeaderSize->GetContent()) : nullptr;
				TestEqual(FString::Printf(TEXT("room list header %d names its column"), Index),
					HeaderText ? HeaderText->GetText().ToString() : FString(), ExpectedHeaders[Index]);
			}
		}

		const UTextBlock* MapText = Cast<UTextBlock>(
			FirstEntry->GetWidgetFromName(TEXT("RoomEntryMap")));
		const UTextBlock* StateText = Cast<UTextBlock>(
			FirstEntry->GetWidgetFromName(TEXT("RoomEntryState")));
		const UTextBlock* PlayerText = Cast<UTextBlock>(
			FirstEntry->GetWidgetFromName(TEXT("RoomEntryPlayers")));
		const UTextBlock* BotText = Cast<UTextBlock>(
			FirstEntry->GetWidgetFromName(TEXT("RoomEntryBots")));
		TestEqual(TEXT("map ID is rendered with its display name"),
			MapText ? MapText->GetText().ToString() : FString(), FString(TEXT("ニューヤーク")));
		TestEqual(TEXT("room state is localized"),
			StateText ? StateText->GetText().ToString() : FString(), FString(TEXT("待機中")));
		TestEqual(TEXT("human count value is rendered under the human column"),
			PlayerText ? PlayerText->GetText().ToString() : FString(), FString(TEXT("3/8")));
		TestEqual(TEXT("enemy bot count value is rendered under the enemy-bot column"),
			BotText ? BotText->GetText().ToString() : FString(), FString(TEXT("4")));
	}
	UMSRoomListEntryWidget* FilledEntry = Widget->GetDisplayedRoomEntry(1);
	TestNotNull(TEXT("filled room row exists"), FilledEntry);
	if (FilledEntry)
	{
		const UTextBlock* FilledPlayers = Cast<UTextBlock>(
			FilledEntry->GetWidgetFromName(TEXT("RoomEntryPlayers")));
		const UTextBlock* FilledBots = Cast<UTextBlock>(
			FilledEntry->GetWidgetFromName(TEXT("RoomEntryBots")));
		TestEqual(TEXT("filled room advertises fifty human seats"),
			FilledPlayers ? FilledPlayers->GetText().ToString() : FString(), FString(TEXT("7/50")));
		TestTrue(TEXT("filled room advertises automatic BOT replenishment"),
			FilledBots && FilledBots->GetText().ToString().Contains(TEXT("BOT自動補充")));
	}

	const int32 RequestsBeforeSearch = Requests;
	Widget->SetSearchTextForAutomationTests(TEXT("ALPHA"));
	TestEqual(TEXT("room-name search is case-insensitive"), Widget->GetDisplayedRoomCount(), 1);
	TestEqual(TEXT("search does not generate an HTTP request"), Requests, RequestsBeforeSearch);
	Widget->SetSearchTextForAutomationTests(TEXT("duplicatehost"));
	TestEqual(TEXT("host-name search returns both duplicate-host rows"),
		Widget->GetDisplayedRoomCount(), 2);
	TestEqual(TEXT("host search remains client-side"), Requests, RequestsBeforeSearch);

	UMSRoomListEntryWidget* SecondEntry = Widget->GetDisplayedRoomEntry(1);
	TestNotNull(TEXT("second duplicate-host row exists"), SecondEntry);
	if (SecondEntry && SecondEntry->GetSelectButton())
	{
		SecondEntry->GetSelectButton()->OnClicked.Broadcast();
	}
	TestEqual(TEXT("clicking the second duplicate-host row joins its own address"),
		JoinedAddress, FString(TEXT("192.0.2.12:7777")));

	const int32 RequestsBeforeRefresh = Requests;
	Widget->GetRefreshButton()->OnClicked.Broadcast();
	TestEqual(TEXT("Refresh performs an immediate GET"), Requests, RequestsBeforeRefresh + 1);
	Widget->GetRefreshButton()->OnClicked.Broadcast();
	TestEqual(TEXT("Refresh is throttled to one request per second"),
		Requests, RequestsBeforeRefresh + 1);

	UVerticalBox* AdvancedPanel = Cast<UVerticalBox>(
		Widget->GetWidgetFromName(TEXT("AdvancedJoinPanel")));
	TestEqual(TEXT("direct-IP fallback starts collapsed"),
		AdvancedPanel ? AdvancedPanel->GetVisibility() : ESlateVisibility::Visible,
		ESlateVisibility::Collapsed);
	Widget->GetAdvancedJoinToggle()->OnClicked.Broadcast();
	TestEqual(TEXT("advanced toggle reveals the direct-IP fallback"),
		AdvancedPanel ? AdvancedPanel->GetVisibility() : ESlateVisibility::Collapsed,
		ESlateVisibility::Visible);
	TestEqual(TEXT("legacy direct-IP default is preserved"),
		Widget->GetJoinAddressInput()->GetText().ToString(), FString(TEXT("127.0.0.1")));
	JoinedAddress.Reset();
	Widget->GetJoinButton()->OnClicked.Broadcast();
	TestEqual(TEXT("legacy direct-IP join button remains wired"),
		JoinedAddress, FString(TEXT("127.0.0.1")));

	Widget->Close();
	const int32 RequestsAtClose = Requests;
	TestFalse(TEXT("closing the screen stops polling"), Registry->IsPolling());
	TestFalse(TEXT("closing the screen clears its polling timer"),
		Registry->IsPollingTimerActiveForAutomationTests());
	Registry->FirePollingTimerForAutomationTests();
	TestEqual(TEXT("a closed room browser performs no further HTTP requests"),
		Requests, RequestsAtClose);
	TestFalse(TEXT("Close pops the room screen from the UI lock"), Lock->IsAnyScreenOpen());
	TestEqual(TEXT("PushScreen and PopScreen calls are one-to-one"),
		Widget->GetPushScreenCountForAutomationTests(),
		Widget->GetPopScreenCountForAutomationTests());
	Widget->Close();
	TestEqual(TEXT("duplicate Close does not add another PopScreen"),
		Widget->GetPopScreenCountForAutomationTests(), 1);
	return true;
}

#endif
