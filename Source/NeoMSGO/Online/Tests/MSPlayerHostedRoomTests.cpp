#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/NetworkVersion.h"
#include "Online/MSRoomSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSRoomRegistrySubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Online/Tests/MSRoomTestListener.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
const FString TestRoomId = TEXT("aaaaaaaa-1111-4222-8333-444444444444");
const FString TestInstanceId = TEXT("bbbbbbbb-1111-4222-8333-444444444444");
FString RoomAuthJson()
{
	return TEXT("{\"realm_id\":\"room-tests\",\"account_id\":\"room-test-account\",\"display_name\":\"test\",\"is_guest\":false,")
		TEXT("\"token\":\"synthetic-room-session\",\"catalog_hash\":\"room-catalog\",\"deck\":{\"schema_version\":1,\"revision\":22,\"selected_slot\":1,\"slots\":[")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]},{\"suit_id\":null,\"weapon_ids\":[null,null,null]},")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]},{\"suit_id\":null,\"weapon_ids\":[null,null,null]}]}}");
}
FString RoomTicketJson(const FString& Instance)
{
	return FString::Printf(TEXT("{\"server_id\":\"%s\",\"instance_id\":\"%s\",\"game_address\":\"192.0.2.10:7777\",\"ticket\":\"synthetic-room-ticket-123456789\",\"expires_at\":%lld}"),
		*TestRoomId, *Instance, (FDateTime::UtcNow().ToUnixTimestamp() + 60) * 1000);
}
FString RoomListJson(const TCHAR* BuildId = MSRoomProtocol::BuildId)
{
	return FString::Printf(TEXT("[{\"room_id\":\"%s\",\"instance_id\":\"%s\",\"name\":\"Test room\",\"host_name\":\"test\",\"game_address\":\"192.0.2.10:7777\",")
		TEXT("\"map_id\":\"Daibao\",\"rule_id\":2,\"bot_count\":7,\"difficulty\":1,\"players\":2,\"max_players\":12,\"status\":\"playing\",\"build_id\":\"%s\",\"catalog_hash\":\"room-catalog\"}]"),
		*TestRoomId, *TestInstanceId, BuildId);
}
FString RoomListJsonWithFillToken(const TCHAR* FillToken, const int32 MaxPlayers = MSRoomPopulation::MaxCombatants, const int32 Players = 49)
{
	return FString::Printf(TEXT("[{\"room_id\":\"%s\",\"instance_id\":\"%s\",\"name\":\"Filled room\",\"host_name\":\"test\",\"game_address\":\"192.0.2.10:7777\",")
		TEXT("\"map_id\":\"Daibao\",\"rule_id\":2,\"bot_count\":0,\"difficulty\":1,\"players\":%d,\"max_players\":%d,\"fill_teams_with_bots\":%s,\"status\":\"ready\",\"build_id\":\"%s\",\"catalog_hash\":\"room-catalog\"}]"),
		*TestRoomId, *TestInstanceId, Players, MaxPlayers, FillToken, MSRoomProtocol::BuildId);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomTicketCommitBoundaryTest, "NeoMSGO.Online.Room.TicketCommitAndInstanceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSRoomTicketCommitBoundaryTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Instance.Get()));
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	FString LastPath, LastBody; int32 Requests = 0;
	Account->SetRequestSenderForAutomationTests([&](const FString&, const FString& Path, const FString& Body, const FString&)
	{ LastPath = Path; LastBody = Body; ++Requests; return true; });
	Account->SetResponseForAutomationTests(200, RoomAuthJson());
	Account->Login(TEXT("room-test-account"), TEXT("synthetic"), nullptr);
	TestTrue(TEXT("test account authenticated normally through response parser"), Account->HasOnlineSession());
	Account->SetResponseForAutomationTests(200, RoomTicketJson(TEXT("cccccccc-1111-4222-8333-444444444444")));
	TestTrue(TEXT("ticket request starts"), Account->RequestRoomTicket(TestRoomId, TestInstanceId));
	FString Ticket;
	TestFalse(TEXT("ticket for a replaced room instance is not usable"), Account->TakePendingGameTicket(TestRoomId, Ticket));
	TestTrue(TEXT("request carries committed revision 22"), LastBody.Contains(TEXT("\"expected_revision\": 22")) || LastBody.Contains(TEXT("\"expected_revision\":22")));
	TestTrue(TEXT("room-specific ticket endpoint is used"), LastPath.EndsWith(TEXT("/v1/rooms/") + TestRoomId + TEXT("/tickets")));
	Account->SetResponseForAutomationTests(200, RoomTicketJson(TestInstanceId));
	Account->RequestRoomTicket(TestRoomId, TestInstanceId);
	TestFalse(TEXT("another room cannot consume the pending ticket"), Account->TakePendingGameTicket(TEXT("home"), Ticket));
	TestTrue(TEXT("matching room consumes once"), Account->TakePendingGameTicket(TestRoomId, Ticket));
	TestFalse(TEXT("consumed ticket cannot be reused"), Account->TakePendingGameTicket(TestRoomId, Ticket));
	Account->SetDeferredResponsesForAutomationTests(true);
	Account->PutDeck(Account->GetCommittedDeck(), nullptr);
	const int32 Before = Requests;
	TestFalse(TEXT("joining is blocked while deck save is uncommitted"), Account->RequestRoomTicket(TestRoomId, TestInstanceId));
	TestEqual(TEXT("blocked join sends no request"), Requests, Before);
	Account->Logout();
	Account->CompleteNextResponseForAutomationTests(200, RoomAuthJson());
	TestFalse(TEXT("logout does not restore a late account operation"), Account->HasOnlineSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomListFailureBoundaryTest, "NeoMSGO.Online.Room.ListFailureAndLateResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSRoomListFailureBoundaryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSPlayerHostedRoomTest"));
	UMSGameInstance* Instance = NewObject<UMSGameInstance>(GEngine);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.OwningGameInstance = Instance; Context.SetCurrentWorld(World); World->SetGameInstance(Instance);
	Instance->Init(); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	auto* Account = Instance->GetSubsystem<UMSOnlineAccountSubsystem>();
	auto* Registry = Instance->GetSubsystem<UMSRoomRegistrySubsystem>();
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	Account->SetRequestSenderForAutomationTests([](const FString&, const FString&, const FString&, const FString&) { return true; });
	Account->SetResponseForAutomationTests(200, RoomAuthJson()); Account->Login(TEXT("room-test-account"), TEXT("synthetic"), nullptr);
	Account->SetResponseForAutomationTests(200, RoomListJson()); Registry->StartPolling(TEXT("127.0.0.1:7778"));
	const auto Rooms = Registry->GetLastKnownRooms();
	TestEqual(TEXT("online snake-case descriptor parses"), Rooms.Num(), 1);
	if (Rooms.Num() == 1)
	{
		TestEqual(TEXT("human occupancy is independent of seven bots"), Rooms[0].CurrentPlayerCount, 2);
		TestEqual(TEXT("host-selected map is retained"), Rooms[0].Settings.MapId, FName(TEXT("Daibao")));
		TestEqual(TEXT("host-selected bot count is retained"), Rooms[0].Settings.BotCount, 7);
		TestFalse(TEXT("missing fill flag is treated as a legacy manual room"), Rooms[0].Settings.bFillTeamsWithBots);
		TestEqual(TEXT("legacy room keeps twelve combatant seats"), Rooms[0].MaxPlayerCount, MSRoomPopulation::LegacyMaxCombatants);
		TestEqual(TEXT("instance id is retained for joining"), Rooms[0].InstanceId, TestInstanceId);
		TestEqual(TEXT("playing status maps to the in-progress room state"), Rooms[0].State, EMSRoomState::InProgress);
	}
	Account->SetResponseForAutomationTests(200, RoomListJsonWithFillToken(TEXT("true")));
	Registry->StartPolling(TEXT("unused"));
	const auto FilledRooms = Registry->GetLastKnownRooms();
	TestEqual(TEXT("filled room response is accepted"), FilledRooms.Num(), 1);
	if (FilledRooms.Num() == 1)
	{
		TestEqual(TEXT("filled room exposes fifty combatant seats"), FilledRooms[0].MaxPlayerCount, MSRoomPopulation::MaxCombatants);
		TestTrue(TEXT("filled room flag is retained"), FilledRooms[0].Settings.bFillTeamsWithBots);
		TestEqual(TEXT("filled room remains open until all fifty human seats are occupied"),
			FilledRooms[0].State, EMSRoomState::Waiting);
	}
	Account->SetResponseForAutomationTests(200, RoomListJsonWithFillToken(TEXT("invalid")));
	Registry->StartPolling(TEXT("unused"));
	const auto AfterMalformedFill = Registry->GetLastKnownRooms();
	TestTrue(TEXT("malformed fill flag is rejected without replacing the cached room"),
		AfterMalformedFill.Num() == 1 && AfterMalformedFill[0].Settings.bFillTeamsWithBots);
	Account->SetResponseForAutomationTests(200, RoomListJson(TEXT("neomsgo-rooms-1"))); Registry->StartPolling(TEXT("unused"));
	TestEqual(TEXT("old build remains an incompatible descriptor"), Registry->GetLastKnownRooms().Num(), 1);
	if (Registry->GetLastKnownRooms().Num() == 1)
		TestEqual(TEXT("old movement protocol cannot be joined"), Registry->GetLastKnownRooms()[0].State, EMSRoomState::Unresponsive);
	Account->SetResponseForAutomationTests(200, RoomListJson()); Registry->StartPolling(TEXT("unused"));
	Account->SetResponseForAutomationTests(503, TEXT("{}"));
	TestTrue(TEXT("failure request actually runs without the manual refresh throttle"), Registry->RefreshNow());
	TestEqual(TEXT("server failure keeps the last known room"), Registry->GetLastKnownRooms().Num(), 1);
	TestFalse(TEXT("failure is not presented as zero rooms"), Registry->GetLastErrorText().IsEmpty());
	TestFalse(TEXT("failed request releases request gate"), Registry->IsRequestPending());
	Account->SetDeferredResponsesForAutomationTests(true); Registry->StartPolling(TEXT("unused"));
	TestTrue(TEXT("new search is pending"), Registry->IsRequestPending()); Registry->StopPolling();
	Account->CompleteNextResponseForAutomationTests(200, TEXT("[]"));
	TestEqual(TEXT("closed search ignores a late response"), Registry->GetLastKnownRooms().Num(), 1);
	Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomTicketLateResponseTest, "NeoMSGO.Online.Room.OldTicketCannotReplaceNewOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSRoomTicketLateResponseTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSRoomTestListener> Listener(NewObject<UMSRoomTestListener>());
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	Account->SetRequestSenderForAutomationTests([](const FString&, const FString&, const FString&, const FString&) { return true; });
	Account->SetResponseForAutomationTests(200, RoomAuthJson()); Account->Login(TEXT("room-test-account"), TEXT("synthetic"), nullptr);
	Account->OnRequestFinished.AddDynamic(Listener.Get(), &UMSRoomTestListener::HandleRequest);
	for (int32 OldResponseCode : {200, 503})
	{
		Account->SetDeferredResponsesForAutomationTests(true);
		Account->RequestRoomTicket(TestRoomId, TestInstanceId);
		Account->CancelPendingGameTicket(); // The actual leave boundary.
		Account->SetDeferredResponsesForAutomationTests(false);
		const FString NewInstance = TEXT("cccccccc-1111-4222-8333-444444444444");
		Account->SetResponseForAutomationTests(200, RoomTicketJson(NewInstance));
		Account->RequestRoomTicket(TestRoomId, NewInstance);
		const int32 Completed = Listener->TicketCompletions;
		Account->CompleteNextResponseForAutomationTests(OldResponseCode, RoomTicketJson(TestInstanceId));
		TestEqual(TEXT("late old success/failure emits no completion into the new operation"), Listener->TicketCompletions, Completed);
		FString Ticket;
		TestTrue(TEXT("new operation ticket survives old response"), Account->TakePendingGameTicket(TestRoomId, Ticket));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomFinalSeatTest, "NeoMSGO.Online.Room.FinalSeatIsAtomicWithBots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSRoomFinalSeatTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSRoomCapacityTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	AMSGameState* State = World->SpawnActor<AMSGameState>(); World->SetGameState(State);
	State->bPlayerHostedRoom = true;
	State->HostedRoomSettings.bFillTeamsWithBots = false;
	State->HostedRoomSettings.BotCount = 10;
	auto* Server = World->GetSubsystem<UMSOnlineServerSubsystem>();
	auto* Host = World->SpawnActor<AMSPlayerController>();
	auto* FirstGuest = World->SpawnActor<AMSPlayerController>();
	auto* SecondGuest = World->SpawnActor<AMSPlayerController>();
	FMSOnlineIdentity HostId; HostId.RealmId = TEXT("room-test"); HostId.AccountId = TEXT("host");
	FMSOnlineIdentity Guest1 = HostId; Guest1.AccountId = TEXT("guest-1");
	FMSOnlineIdentity Guest2 = HostId; Guest2.AccountId = TEXT("guest-2");
	FMSOnlineDeckSnapshot Deck;
	TestTrue(TEXT("host occupies first of two human seats"), Server->Admit(Host, HostId, Deck));
	TestTrue(TEXT("first of two previously issued tickets gets final seat"), Server->Admit(FirstGuest, Guest1, Deck));
	TestFalse(TEXT("second simultaneous ticket cannot overfill with ten bots"), Server->Admit(SecondGuest, Guest2, Deck));
	TestTrue(TEXT("retrying same admitted controller does not consume another seat"), Server->Admit(FirstGuest, Guest1, Deck));
	FirstGuest->Destroy();
	TestTrue(TEXT("disconnected seat becomes available"), Server->Admit(SecondGuest, Guest2, Deck));
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomNetworkProtocolTest, "NeoMSGO.Online.Room.MovementWireCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSRoomNetworkProtocolTest::RunTest(const FString&)
{
	TestEqual(TEXT("module configures game wire protocol before connections"), FNetworkVersion::GetGameNetworkProtocolVersion(), MSRoomProtocol::NetworkVersion);
	const uint32 CurrentChecksum = FNetworkVersion::GetLocalNetworkVersion();
	// Previous shipped builds used the Engine default game protocol (zero).
	FNetworkVersion::SetGameNetworkProtocolVersion(0);
	const uint32 PreviousChecksum = FNetworkVersion::GetLocalNetworkVersion();
	FNetworkVersion::SetGameNetworkProtocolVersion(MSRoomProtocol::NetworkVersion);
	TestFalse(TEXT("Engine handshake rejects the old packed movement format"), FNetworkVersion::IsNetworkCompatible(CurrentChecksum, PreviousChecksum));
	TestTrue(TEXT("same game protocol remains compatible"), FNetworkVersion::IsNetworkCompatible(CurrentChecksum, FNetworkVersion::GetLocalNetworkVersion()));
	return true;
}
#endif
