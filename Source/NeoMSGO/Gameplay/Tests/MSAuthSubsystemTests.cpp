// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Gameplay/MSAuthSubsystem.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAuthSubsystemTest,
	"NeoMSGO.Gameplay.Authentication.PasswordlessRegistrationLoginAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSAuthSubsystemTest::RunTest(const FString& Parameters)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_AuthAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSAuthSubsystem> Auth(NewObject<UMSAuthSubsystem>(Instance.Get()));
	Auth->SetSaveSlotForAutomationTests(Slot);
	Auth->ResetAndReloadForAutomationTests();

	// パスワードレス: 明示登録は重複のみ拒否、ログインは未登録名を自動登録して常に受け入れる
	TestTrue(TEXT("authority/standalone registers an account by name alone"),
		Auth->RegisterAccount(TEXT("TestPilot")));
	TestFalse(TEXT("explicit re-registration is rejected case-insensitively"),
		Auth->RegisterAccount(TEXT("testpilot")));
	TestTrue(TEXT("existing name logs straight in"), Auth->Login(TEXT("TestPilot")));
	const int32 CountBeforeAutoRegister = Auth->GetAccountCountForAutomationTests();
	TestTrue(TEXT("unknown name auto-registers on login"), Auth->Login(TEXT("MissingPilot")));
	TestEqual(TEXT("auto-registration adds exactly one account"),
		Auth->GetAccountCountForAutomationTests(), CountBeforeAutoRegister + 1);
	TestTrue(TEXT("re-login with an existing name never reports the name as taken"),
		Auth->Login(TEXT("missingpilot")));
	TestEqual(TEXT("re-login does not duplicate the account"),
		Auth->GetAccountCountForAutomationTests(), CountBeforeAutoRegister + 1);
	TestFalse(TEXT("empty name is rejected"), Auth->Login(TEXT("   ")));

	// ゲストモード: 設定不要でゲストNの空き番号が採番される
	const FString Guest1 = Auth->LoginAsGuest();
	TestEqual(TEXT("first guest takes the first free number"), Guest1, FString(TEXT("ゲスト1")));
	const FString Guest2 = Auth->LoginAsGuest();
	TestEqual(TEXT("second guest takes the next free number"), Guest2, FString(TEXT("ゲスト2")));

	TStrongObjectPtr<UMSAuthSubsystem> Reloaded(NewObject<UMSAuthSubsystem>(Instance.Get()));
	Reloaded->SetSaveSlotForAutomationTests(Slot);
	TestTrue(TEXT("saved accounts reload after subsystem recreation"), Reloaded->ReloadAccounts());
	TestEqual(TEXT("all accounts persist per PC"), Reloaded->GetAccountCountForAutomationTests(),
		Auth->GetAccountCountForAutomationTests());
	TestTrue(TEXT("login succeeds after persistence reload"), Reloaded->Login(TEXT("testpilot")));

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginStateIntegrationTest,
	"NeoMSGO.Gameplay.Authentication.GameInstanceAndFriendSelfIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSLoginStateIntegrationTest::RunTest(const FString& Parameters)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_AuthStateAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSAuthSubsystem> Auth(NewObject<UMSAuthSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Auth->SetSaveSlotForAutomationTests(Slot);
	Auth->ResetAndReloadForAutomationTests();

	TestFalse(TEXT("new GameInstance starts logged out"), Instance->IsLoggedIn());
	TestFalse(TEXT("lobby gate is closed before login"), Instance->CanEnterLobby());
	TestTrue(TEXT("account registers"), Auth->RegisterAccount(TEXT("LobbyPilot")));
	TestFalse(TEXT("registration alone does not open lobby gate"), Instance->CanEnterLobby());
	TestTrue(TEXT("login succeeds"), Auth->Login(TEXT("lobbypilot")));
	TestTrue(TEXT("lobby gate opens after login"), Instance->CanEnterLobby());
	TestEqual(TEXT("canonical username is transient GameInstance state"), Instance->LoggedInUsername,
		FString(TEXT("LobbyPilot")));
	TestEqual(TEXT("friend subsystem self display uses logged-in username"), Friends->GetSelfDisplayName(),
		FString(TEXT("LobbyPilot")));
	Instance->Logout();
	TestFalse(TEXT("logout closes lobby gate"), Instance->CanEnterLobby());
	TestTrue(TEXT("self display clears on logout"), Friends->GetSelfDisplayName().IsEmpty());

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUnauthenticatedClientGuestLoginTest,
	"NeoMSGO.Gameplay.Authentication.UnauthenticatedClientGuestLogin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSUnauthenticatedClientGuestLoginTest::RunTest(const FString& Parameters)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_AuthClientAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSAuthSubsystem> Auth(NewObject<UMSAuthSubsystem>(Instance.Get()));
	Auth->SetSaveSlotForAutomationTests(Slot);
	Auth->SetNetModeForAutomationTests(NM_Client);
	Auth->ResetAndReloadForAutomationTests();

	TestFalse(TEXT("client starts without an authenticated identity"), Instance->IsLoggedIn());
	const FString Guest = Auth->LoginAsGuest();
	TestEqual(TEXT("an unauthenticated client can perform its initial guest login"),
		Guest, FString(TEXT("ゲスト1")));
	TestEqual(TEXT("guest login establishes the client identity"),
		Instance->LoggedInUsername, Guest);
	TestFalse(TEXT("an authenticated client cannot switch to another local account"),
		Auth->Login(TEXT("ImpersonatedPilot")));
	TestFalse(TEXT("an authenticated client cannot register another local account"),
		Auth->RegisterAccount(TEXT("ImpersonatedPilot")));
	TestTrue(TEXT("an authenticated client cannot replace its identity with another guest"),
		Auth->LoginAsGuest().IsEmpty());

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGuestNumberSkipsConnectedPlayersTest,
	"NeoMSGO.Gameplay.Authentication.GuestNumberSkipsConnectedPlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSGuestNumberSkipsConnectedPlayersTest::RunTest(const FString& Parameters)
{
	// #214: the second window has a fresh -UserDir account file, so its local scan alone would pick
	// "ゲスト1" again. Replicated presence must be taken into account before the name is displayed.
	const FString Slot = FString::Printf(TEXT("NeoMSGO_AuthGuestAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSAuthSubsystem> Auth(NewObject<UMSAuthSubsystem>(Instance.Get()));
	Auth->SetSaveSlotForAutomationTests(Slot);
	Auth->SetNetModeForAutomationTests(NM_Client);
	Auth->SetConnectedUsernamesForAutomationTests({TEXT("ゲスト1")});
	Auth->ResetAndReloadForAutomationTests();

	TestEqual(TEXT("a second window skips the guest number already connected"),
		Auth->LoginAsGuest(), FString(TEXT("ゲスト2")));
	TestEqual(TEXT("the local identity matches the skipped-ahead guest name"),
		Instance->LoggedInUsername, FString(TEXT("ゲスト2")));

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineAccountClientContractTest,
	"NeoMSGO.Gameplay.Authentication.OnlineAccountGenerationDeckAndTicketBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSOnlineAccountClientContractTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Instance.Get()));
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	Account->SetAuthBaseUrlForAutomationTests(TEXT("http://127.0.0.1:43127"));
	bool bPasswordWasSent = false;
	Account->SetRequestSenderForAutomationTests([&bPasswordWasSent](const FString&, const FString&, const FString& Body, const FString&)
	{
		bPasswordWasSent = Body.Contains(TEXT("password"));
		return true;
	});

	const FString DeckJson = TEXT(
		"\"deck\":{\"schema_version\":1,\"revision\":4,\"selected_slot\":0,\"slots\":["
		"{\"suit_id\":null,\"weapon_ids\":[null,null,null]},"
		"{\"suit_id\":null,\"weapon_ids\":[null,null,null]},"
		"{\"suit_id\":null,\"weapon_ids\":[null,null,null]},"
		"{\"suit_id\":null,\"weapon_ids\":[null,null,null]}]}" );
	const FString ResponseA = FString::Printf(TEXT(
		"{\"realm_id\":\"realm-a\",\"account_id\":\"account-a\",\"username\":\"pilot_a\","
		"\"display_name\":\"Pilot A\",\"is_guest\":false,\"token\":\"opaque-a\",%s}"), *DeckJson);
	Account->SetResponseForAutomationTests(200, ResponseA);
	bool bSucceeded = false;
	Account->Login(TEXT("pilot_a"), TEXT("password-that-is-never-saved"),
		[&bSucceeded](const bool bOk, const FString&) { bSucceeded = bOk; });
	TestTrue(TEXT("online login accepts a server envelope"), bSucceeded);
	TestTrue(TEXT("the password is sent only in the request body"), bPasswordWasSent);
	TestTrue(TEXT("online login updates the GameInstance gate"), Instance->CanEnterLobby());
	TestEqual(TEXT("server account identity reaches the GameInstance"), Instance->AccountId, FString(TEXT("account-a")));
	TestEqual(TEXT("server realm is retained with the identity"), Instance->RealmId, FString(TEXT("realm-a")));
	TestEqual(TEXT("the committed deck revision comes from the server"), Account->GetCommittedDeck().Revision, static_cast<int64>(4));
	TestFalse(TEXT("the synchronous name-only API cannot silently fall back online"),
		NewObject<UMSAuthSubsystem>(Instance.Get())->Login(TEXT("fallback")));

	const FString ResponseB = FString::Printf(TEXT(
		"{\"realm_id\":\"realm-a\",\"account_id\":\"account-b\",\"username\":\"pilot_b\","
		"\"display_name\":\"Pilot B\",\"is_guest\":false,\"token\":\"opaque-b\",%s}"), *DeckJson);
	Account->SetResponseForAutomationTests(200, ResponseB);
	const int64 GenerationA = Account->GetAuthGeneration();
	Account->Login(TEXT("pilot_b"), TEXT("another-password-that-is-never-saved"), nullptr);
	TestTrue(TEXT("a different account changes the GameInstance identity"), Instance->AccountId == TEXT("account-b"));
	TestTrue(TEXT("account changes advance the stale-response generation"), Account->GetAuthGeneration() > GenerationA);

	Account->SetResponseForAutomationTests(200,
		TEXT("{\"ticket\":\"opaque-ticket\",\"game_address\":\"127.0.0.1:7777\",\"server_id\":\"home\",\"expires_at\":4102444800000}"));
	TestTrue(TEXT("an authenticated client may request a game ticket"), Account->RequestGameTicket(TEXT("home")));
	FString Ticket;
	TestTrue(TEXT("the matching ticket is taken exactly once"), Account->TakePendingGameTicket(TEXT("home"), Ticket));
	TestTrue(TEXT("the opaque ticket is available only at the server handoff"), Ticket == TEXT("opaque-ticket"));
	TestFalse(TEXT("a consumed ticket cannot be replayed"), Account->TakePendingGameTicket(TEXT("home"), Ticket));
	return true;
}

#endif
