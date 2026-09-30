#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AI/MSBotCharacter.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingLobbyTransitionTest, "NeoMSGO.UI.Briefing.LobbyTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingCountdownPausedTest, "NeoMSGO.UI.Briefing.CountdownContinuesWhilePaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingListenHostWorldProgressTest, "NeoMSGO.UI.Briefing.ListenHostDoesNotPauseWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingManualStartTest, "NeoMSGO.UI.Briefing.ManualStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingAutomaticStartTest, "NeoMSGO.UI.Briefing.WaitForManualStartAtZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingReferenceLayoutTest, "NeoMSGO.UI.Briefing.ReferenceLayoutAndRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingSortieModalTest, "NeoMSGO.UI.Briefing.SortieConfirmationModal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingRosterIdentityAndPagesTest, "NeoMSGO.UI.Briefing.RosterIdentityAndPages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingOnlinePlayerArrayRosterTest, "NeoMSGO.UI.Briefing.OnlinePlayerArrayRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingOnlineAdmissionClockTest, "NeoMSGO.UI.Briefing.OnlineAdmissionTeamAndClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBriefingEquipmentReturnTest, "NeoMSGO.UI.Briefing.EquipmentReturnPreservesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSBriefingWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBriefingWidgetTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		APlayerController* PlayerController = nullptr;

		explicit FScopedWorld(const ENetMode InNetMode = NM_Standalone)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			// UWorld derives the pre-net-driver mode from NextURL. A listen URL
			// gives this fixture the same mode seen by GameMode::InitGame without
			// opening a socket or requiring a PIE session.
			if (InNetMode == NM_ListenServer) World->NextURL = TEXT("/Engine/Maps/Entry?listen");
			World->SetGameInstance(GameInstance); GameInstance->Init();
			FURL URL; URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			PlayerController = World->SpawnActor<APlayerController>();
		}

		~FScopedWorld()
		{
			World->GetWorldSettings()->SetPauserPlayerState(nullptr);
			GameInstance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
		}
	};

	TStrongObjectPtr<UMSSortieWidget> MakeSortie(UObject* Outer)
	{
		TStrongObjectPtr<UMSSortieWidget> Result(NewObject<UMSSortieWidget>(Outer));
		Result->Initialize(); Result->InitializeForAutomationTests();
		return Result;
	}

	TStrongObjectPtr<UMSBriefingWidget> MakeBriefing(UObject* Outer, UMSSortieWidget* Sortie)
	{
		TStrongObjectPtr<UMSBriefingWidget> Result(NewObject<UMSBriefingWidget>(Outer));
		Result->Initialize(); Result->InitializeForAutomationTests(); Result->SetSortieWidgetForTesting(Sortie);
		return Result;
	}

	TStrongObjectPtr<UMSSpawnSelectWidget> MakeSpawnSelect(UObject* Outer)
	{
		TStrongObjectPtr<UMSSpawnSelectWidget> Result(NewObject<UMSSpawnSelectWidget>(Outer));
		Result->Initialize(); Result->InitializeForAutomationTests();
		return Result;
	}

	struct FScopedOnlineCommandLine
	{
		FString Previous = FCommandLine::Get();
		FScopedOnlineCommandLine() { FCommandLine::Append(TEXT(" -MSAuthMode=Online")); }
		~FScopedOnlineCommandLine() { FCommandLine::Set(*Previous); }
	};

	bool SetAuthenticatedTeam(FAutomationTestBase& Test, AMSPlayerController* PlayerController, const uint8 TeamId)
	{
		if (!Test.TestNotNull(TEXT("online briefing fixture has an AMSPlayerController"), PlayerController)) return false;
		FBoolProperty* AuthProperty = CastField<FBoolProperty>(PlayerController->GetClass()->FindPropertyByName(TEXT("bOnlineAuthenticated")));
		if (!Test.TestNotNull(TEXT("online authentication is a reflected controller field"), AuthProperty)) return false;
		AuthProperty->SetPropertyValue_InContainer(PlayerController, true);
		FByteProperty* TeamProperty = CastField<FByteProperty>(PlayerController->GetClass()->FindPropertyByName(TEXT("OnlineAssignedTeamId")));
		if (!Test.TestNotNull(TEXT("online assigned team is a reflected controller field"), TeamProperty)) return false;
		TeamProperty->SetPropertyValue_InContainer(PlayerController, TeamId);
		return true;
	}
}

bool FMSBriefingLobbyTransitionTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get());
	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World)); Lobby->Initialize(); Lobby->InitializeForAutomationTests();
	Lobby->SetBriefingWidgetForTesting(Briefing.Get()); Lobby->Open(); Lobby->SelectBattleRule(1);
	TestTrue(TEXT("applying the selected rule succeeds"), Lobby->ApplySelectedBattleRule());
	TestTrue(TEXT("operation decision opens briefing"), Briefing->IsOpen());
	TestFalse(TEXT("operation decision does not directly open sortie"), Sortie->IsOpen());
	TestFalse(TEXT("lobby closes after operation decision"), Lobby->IsOpen());
	return true;
}

bool FMSBriefingCountdownPausedTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get()); Briefing->Open();
	TestEqual(TEXT("countdown starts at exactly 60 seconds"), Briefing->GetRemainingSeconds(), 60.0f);
	APlayerState* Pauser = Scope.World->SpawnActor<APlayerState>();
	Scope.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	TestTrue(TEXT("test world is paused"), Scope.World->IsPaused());
	Briefing->AdvanceCountdownForAutomationTests(1.0f);
	TestEqual(TEXT("one real-time tick lowers the countdown by one second while paused"), Briefing->GetRemainingSeconds(), 59.0f);
	Briefing->AdvanceCountdownForAutomationTests(1.0f);
	TestEqual(TEXT("the next tick lowers it again"), Briefing->GetRemainingSeconds(), 58.0f);
	TestTrue(TEXT("countdown advancement does not unpause gameplay"), Scope.World->IsPaused());
	return true;
}

bool FMSBriefingListenHostWorldProgressTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope(NM_ListenServer);
	if (!TestTrue(TEXT("listen briefing fixture reports listen-server mode"), Scope.World->IsNetMode(NM_ListenServer))) return false;
	AMSGameMode* GameMode = Scope.World->GetAuthGameMode<AMSGameMode>();
	if (!TestNotNull(TEXT("listen briefing fixture has the production GameMode"), GameMode)) return false;

	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Scope.GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	// FScopedWorld already owns the first server PlayerController. Reuse it
	// instead of spawning a second primary candidate; GameMode initialized its
	// real PlayerState when the fixture brought the world up.
	APlayerController* Controller = Scope.PlayerController;
	if (!TestNotNull(TEXT("listen briefing fixture has a PlayerController"), Controller)) return false;
	Controller->SetPlayer(LocalPlayer);
	if (!TestNotNull(TEXT("listen briefing fixture has a PlayerState"), Controller->PlayerState.Get())) return false;
	Controller->PlayerState->SetOwner(Controller);
	if (!TestTrue(TEXT("listen briefing fixture resolves its primary local controller"),
		Scope.GameInstance->GetPrimaryPlayerController(false) == Controller)) return false;

	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing(NewObject<UMSBriefingWidget>(Scope.World));
	Briefing->SetOwningPlayer(Controller);
	Briefing->Initialize(); Briefing->InitializeForAutomationTests(); Briefing->SetSortieWidgetForTesting(Sortie.Get());
	Briefing->Open();
	if (!TestTrue(TEXT("listen host opens the real briefing widget"), Briefing->IsOpen())) return false;

	UMSUILockSubsystem* UILock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	if (!TestNotNull(TEXT("listen briefing fixture has the UI lock subsystem"), UILock)) return false;
	TestFalse(TEXT("briefing keeps gameplay input locked while open"), UILock->IsGameplayActive());
	TestTrue(TEXT("briefing keeps the local controller in the UI input mode"), Controller->bShowMouseCursor);
	TestFalse(TEXT("opening briefing does not pause a listen host world"), Scope.World->IsPaused());
	const double Before = Scope.World->GetTimeSeconds();
	Scope.World->Tick(LEVELTICK_All, 0.25f);
	const double After = Scope.World->GetTimeSeconds();
	TestTrue(TEXT("listen host world time advances while briefing is open"), After > Before + 0.1);
	TestFalse(TEXT("listen host remains unpaused after a world tick"), Scope.World->IsPaused());
	TestFalse(TEXT("GameMode rejects a pause request in listen mode"), GameMode->SetPause(Controller));
	TestFalse(TEXT("rejected listen pause request leaves the world running"), Scope.World->IsPaused());
	TestFalse(TEXT("briefing keeps gameplay input locked after world progress"), UILock->IsGameplayActive());
	Briefing->CloseForBattleEnd();
	TestTrue(TEXT("closing briefing releases the UI input lock"), UILock->IsGameplayActive());
	return true;
}

bool FMSBriefingManualStartTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get()); Briefing->Open();
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect = MSBriefingWidgetTests::MakeSpawnSelect(Scope.World);
	Briefing->SetSpawnSelectWidgetForTesting(SpawnSelect.Get());
	UButton* Start = CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("StartOperationButton"))); Start->OnClicked.Broadcast();
	TestFalse(TEXT("manual start closes briefing"), Briefing->IsOpen());
	TestTrue(TEXT("manual start opens spawn selection"), SpawnSelect->IsOpen());
	TestFalse(TEXT("manual start does not open the deck-confirmation modal"), Sortie->IsOpen());
	TestFalse(TEXT("repeated start request is rejected"), Briefing->StartOperation());
	TestTrue(TEXT("repeated start keeps the same spawn selection open"), SpawnSelect->IsOpen());
	TestEqual(TEXT("repeated start does not replace the spawn-selection instance"), Briefing->GetSpawnSelectWidget(), SpawnSelect.Get());
	UGridPanel* DeckGrid = Cast<UGridPanel>(SpawnSelect->GetWidgetFromName(TEXT("SpawnDeckGrid")));
	TestNotNull(TEXT("started spawn selection contains the four-card deck grid"), DeckGrid);
	if (DeckGrid) TestEqual(TEXT("started spawn selection contains four deck cards"), DeckGrid->GetChildrenCount(), 4);
	TestNotNull(TEXT("started spawn selection contains the selected-suit preview"), Cast<UImage>(SpawnSelect->GetWidgetFromName(TEXT("SpawnSuitPreview"))));
	TestNotNull(TEXT("started spawn selection contains the deploy button"), Cast<UButton>(SpawnSelect->GetWidgetFromName(TEXT("SpawnDeployButton"))));
	return true;
}

bool FMSBriefingAutomaticStartTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get()); Briefing->Open();
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect = MSBriefingWidgetTests::MakeSpawnSelect(Scope.World);
	Briefing->SetSpawnSelectWidgetForTesting(SpawnSelect.Get());
	Briefing->AdvanceCountdownForAutomationTests(60.0f);
	TestEqual(TEXT("countdown clamps at zero"), Briefing->GetRemainingSeconds(), 0.0f);
	TestTrue(TEXT("zero countdown keeps briefing open for explicit start"), Briefing->IsOpen());
	TestFalse(TEXT("zero countdown does not open spawn selection"), SpawnSelect->IsOpen());
	TestEqual(TEXT("zero countdown reports the actual waiting state"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingCountdownText")))->GetText().ToString(), FString(TEXT("作戦開始待ち")));
	return true;
}

bool FMSBriefingReferenceLayoutTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>(); Player->SetGenericTeamId(MSTeam::PlayerTeamId());
	AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(); Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get()); Briefing->Open();
	TestTrue(TEXT("briefing shows the current map display name"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("RoomInformationText")))->GetText().ToString().Contains(TEXT("アンリアライザコロニー")));
	TestNotNull(TEXT("suit confirmation button exists"), Cast<UButton>(Briefing->GetWidgetFromName(TEXT("SuitConfirmationButton"))));
	TestNotNull(TEXT("embedded MSMapWidgetBase minimap exists"), Cast<UMSBriefingMiniMapWidget>(Briefing->GetWidgetFromName(TEXT("BriefingMiniMap"))));
	TestNotNull(TEXT("three-column pilot grid exists"), Cast<UGridPanel>(Briefing->GetWidgetFromName(TEXT("PilotCardGrid"))));
	TestNotNull(TEXT("1920x1080 design canvas exists"), Briefing->GetWidgetFromName(TEXT("BriefingDesignCanvas")));
	TestNotNull(TEXT("explicit back button exists"), Cast<UButton>(Briefing->GetWidgetFromName(TEXT("BriefingBackButton"))));
	TestTrue(TEXT("pilot grid contains the player and at least one registry BOT"), Briefing->GetPilotCardCount() >= 2);
	TestTrue(TEXT("equipment entry is available through the existing hangar"), CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("EquipmentChangeButton")))->GetIsEnabled());
	return true;
}

bool FMSBriefingSortieModalTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSSortieWidget> Sortie = MSBriefingWidgetTests::MakeSortie(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MSBriefingWidgetTests::MakeBriefing(Scope.World, Sortie.Get()); Briefing->Open();
	Briefing->AdvanceCountdownForAutomationTests(7.0f);
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("SuitConfirmationButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("suit confirmation opens the sortie-deck modal"), Sortie->IsOpen());
	TestTrue(TEXT("briefing remains open behind the modal"), Briefing->IsOpen());
	TestEqual(TEXT("modal countdown initially matches briefing remaining seconds"), Sortie->GetDisplayedCountdownText().ToString(), FString(TEXT("作戦開始まで 53秒")));
	Briefing->AdvanceCountdownForAutomationTests(2.0f);
	TestEqual(TEXT("modal countdown stays synchronized while open"), Sortie->GetDisplayedCountdownText().ToString(), FString(TEXT("作戦開始まで 51秒")));
	CastChecked<UButton>(Sortie->GetWidgetFromName(TEXT("SortieModalCloseButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("close button closes only the modal"), Sortie->IsOpen());
	TestTrue(TEXT("briefing is operable again after modal close"), Briefing->IsOpen());
	TestTrue(TEXT("briefing operation control is enabled again after modal close"), CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("StartOperationButton")))->GetIsEnabled());
	return true;
}

bool FMSBriefingRosterIdentityAndPagesTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	FActorSpawnParameters SpawnParams; SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Local = Scope.World->SpawnActor<AMSPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AMSPlayerCharacter* Remote = Scope.World->SpawnActor<AMSPlayerCharacter>(FVector(5000,0,0), FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("local participant spawned"), Local) || !TestNotNull(TEXT("remote participant spawned"), Remote)) return false;
	Local->SetGenericTeamId(MSTeam::PlayerTeamId()); Remote->SetGenericTeamId(MSTeam::PlayerTeamId());
	// ULocalPlayer is Within=Engine; a GameInstance outer violates its engine owner contract.
	Scope.PlayerController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	Scope.PlayerController->Possess(Local);
	for (int32 Index = 0; Index < 15; ++Index)
	{
		AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(FVector((Index+2)*5000.0,0,0), FRotator::ZeroRotator, SpawnParams);
		if (!TestNotNull(FString::Printf(TEXT("participant bot %d spawned"), Index), Bot)) return false;
		Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	}
	TStrongObjectPtr<UMSBriefingWidget> Briefing(CreateWidget<UMSBriefingWidget>(Scope.PlayerController));
	if (!TestNotNull(TEXT("owned briefing was created"), Briefing.Get())) return false;
	Briefing->InitializeForAutomationTests(); Briefing->Open();
	int32 SelfLabels = 0;
	for (int32 Index = 0; Index < 12; ++Index) if (Briefing->GetDisplayedPilotLabel(Index).ToString().Contains(TEXT("（自分）"))) ++SelfLabels;
	TestEqual(TEXT("only the owning pawn is marked as self, not the other player"), SelfLabels, 1);
	const int32 Total = Briefing->GetPilotCardCount();
	TestEqual(TEXT("all seventeen actual participants are counted"), Total, 17);
	UButton* Next = CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("BriefingNextPilots")));
	TestTrue(TEXT("more than twelve participants can be browsed"), Total > 12 && Next->GetIsEnabled());
	const FString FirstPage = CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingPilotPageText")))->GetText().ToString();
	Next->OnClicked.Broadcast();
	TestNotEqual(TEXT("next page changes the visible participant range"), CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingPilotPageText")))->GetText().ToString(), FirstPage);
	TestEqual(TEXT("paging preserves the actual team count"), Briefing->GetPilotCardCount(), Total);
	TestFalse(TEXT("self is not repeated on the second page"), Briefing->GetDisplayedPilotLabel(0).ToString().Contains(TEXT("（自分）")));
	Remote->Destroy(); Briefing->RefreshPilotCardsForAutomationTests();
	TestEqual(TEXT("departed participant is removed when the roster refreshes"), Briefing->GetPilotCardCount(), 16);
	return true;
}

bool FMSBriefingOnlinePlayerArrayRosterTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online account subsystem is available"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	FMSOnlineIdentity LocalIdentity;
	LocalIdentity.AccountId = TEXT("guest-account");
	LocalIdentity.RealmId = TEXT("home");
	LocalIdentity.DisplayName = TEXT("ゲスト1");
	LocalIdentity.bIsGuest = true;
	Online->SetIdentityForAutomationTests(LocalIdentity);
	Scope.GameInstance->LoggedInUsername = TEXT("guest_8c09bcd1-4645-4347-9abc");
	Scope.GameInstance->DisplayName = LocalIdentity.DisplayName;

	AMSGameState* GameState = Scope.World->GetGameState<AMSGameState>();
	AMSPlayerState* LocalState = Scope.World->SpawnActor<AMSPlayerState>();
	LocalState->SetVerifiedOnlineIdentity(LocalIdentity);
	LocalState->SetSelectedFaction(MSTeam::PlayerTeamId());
	// UUserWidget's owning controller is local only after a LocalPlayer is
	// attached.  Keep the fixture's self marker equivalent to a real client.
	Scope.PlayerController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	Scope.PlayerController->SetPlayerState(LocalState);
	LocalState->SetOwner(Scope.PlayerController);
	TestTrue(TEXT("online briefing fixture has a local owning controller"), Scope.PlayerController->IsLocalController());
	GameState->AddPlayerState(LocalState);
	FMSOnlineIdentity RemoteIdentity;
	RemoteIdentity.AccountId = TEXT("account-two");
	RemoteIdentity.RealmId = TEXT("home");
	RemoteIdentity.DisplayName = TEXT("Pilot-2");
	AMSPlayerState* RemoteState = Scope.World->SpawnActor<AMSPlayerState>();
	RemoteState->SetVerifiedOnlineIdentity(RemoteIdentity);
	RemoteState->SetSelectedFaction(MSTeam::EnemyTeamId());
	GameState->AddPlayerState(RemoteState);
	// A connected but unauthenticated PlayerState must not become a briefing row.
	AMSPlayerState* PendingState = Scope.World->SpawnActor<AMSPlayerState>();
	PendingState->SetPlayerName(TEXT("guest_pending_internal"));
	GameState->AddPlayerState(PendingState);

	TStrongObjectPtr<UMSBriefingWidget> Briefing(NewObject<UMSBriefingWidget>(Scope.World));
	Briefing->SetOwningPlayer(Scope.PlayerController);
	Briefing->Initialize(); Briefing->InitializeForAutomationTests();
	Briefing->Open();
	TestEqual(TEXT("online briefing counts the assigned local participant"), Briefing->GetPilotCardCount(), 1);
	TestEqual(TEXT("online briefing displays the public local identity"),
		Briefing->GetDisplayedPilotLabel(0).ToString(), FString(TEXT("ゲスト1（自分）")));
	TestFalse(TEXT("online briefing does not expose the backend username"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingShellPilotText")))->GetText().ToString().Contains(TEXT("guest_8c09bcd1")));
	TestTrue(TEXT("player team summary uses assigned online participants"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingTeamASummaryText")))->GetText().ToString().Contains(TEXT("参加者 1")));
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("BriefingTeamBButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("online briefing lists the assigned remote participant on its team"), Briefing->GetPilotCardCount(), 1);
	TestEqual(TEXT("remote participant keeps the public display name"),
		Briefing->GetDisplayedPilotLabel(0).ToString(), FString(TEXT("Pilot-2")));
	TestTrue(TEXT("enemy team summary uses assigned online participants"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingTeamBSummaryText")))->GetText().ToString().Contains(TEXT("参加者 1")));
	return true;
}

bool FMSBriefingOnlineAdmissionClockTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedOnlineCommandLine OnlineCommandLine;
	MSBriefingWidgetTests::FScopedWorld Scope;
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online briefing fixture has an account subsystem"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);

	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Scope.GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	// The generic controller in FScopedWorld is kept for the legacy offline
	// cases. Use the production controller here so the fixture exercises the
	// same online admission API as a connected client.
	AMSPlayerController* OnlineController = Scope.World->SpawnActor<AMSPlayerController>();
	if (!TestNotNull(TEXT("online briefing fixture spawned an AMSPlayerController"), OnlineController)) return false;
	OnlineController->PlayerState = Scope.World->SpawnActor<AMSPlayerState>();
	OnlineController->SetPlayer(LocalPlayer);
	if (!MSBriefingWidgetTests::SetAuthenticatedTeam(*this, OnlineController, MSTeam::EnemyTeamId().GetId())) return false;

	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("online briefing fixture has a real GameState"), State)) return false;
	State->bBattleEnded = false;
	State->MatchDuration = 1200.0f;
	State->RemainingTime = State->MatchDuration;

	TStrongObjectPtr<UMSBriefingWidget> Briefing(NewObject<UMSBriefingWidget>(Scope.World));
	Briefing->SetOwningPlayer(OnlineController);
	Briefing->Initialize(); Briefing->InitializeForAutomationTests();
	Briefing->Open();
	TestEqual(TEXT("authenticated team two opens the ORDER tab"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("SelectedBriefingTeamText")))->GetText().ToString(),
		FString(TEXT("ORDER PILOTS")));
	TestEqual(TEXT("online briefing does not start a local sixty second countdown"), Briefing->GetRemainingSeconds(), 0.0f);
	TestEqual(TEXT("online match at its full duration shows deployment readiness"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingCountdownText")))->GetText().ToString(),
		FString(TEXT("出撃準備")));

	State->RemainingTime = 734.0f;
	Briefing->AdvanceCountdownForAutomationTests(0.1f);
	TestEqual(TEXT("online briefing presents the authoritative remaining match time"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingCountdownText")))->GetText().ToString(),
		FString(TEXT("戦闘中 12'14")));

	State->bBattleEnded = true;
	Briefing->AdvanceCountdownForAutomationTests(0.1f);
	TestEqual(TEXT("battle end replaces the stale local countdown text"),
		CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("BriefingCountdownText")))->GetText().ToString(),
		FString(TEXT("戦闘終了")));
	return true;
}

bool FMSBriefingEquipmentReturnTest::RunTest(const FString&)
{
	MSBriefingWidgetTests::FScopedWorld Scope;
	Scope.PlayerController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	if (!TestNotNull(TEXT("briefing equipment has the production deck"), Deck)) return false;
	if (!TestNotNull(TEXT("selected equipment slot has a catalog suit"), Deck->GetPresetInSlot(Deck->GetSelectedSlotIndex()))) return false;
	const FMSDeckLoadout Before = Deck->GetLoadoutInSlot(Deck->GetSelectedSlotIndex());
	TStrongObjectPtr<UMSBriefingWidget> Briefing(CreateWidget<UMSBriefingWidget>(Scope.PlayerController));
	if (!TestNotNull(TEXT("owned equipment briefing was created"), Briefing.Get())) return false;
	Briefing->InitializeForAutomationTests(); Briefing->Open();
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("BriefingTeamBButton")))->OnClicked.Broadcast();
	Briefing->AdvanceCountdownForAutomationTests(8.0f);
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("EquipmentChangeButton")))->OnClicked.Broadcast();
	UMSHangarWidget* Equipment = Briefing->GetEquipmentWidget();
	if (!TestNotNull(TEXT("equipment action creates the real hangar"), Equipment)) return false;
	TestTrue(TEXT("equipment action opens the real hangar"), Equipment->IsOpen());
	TestFalse(TEXT("offline equipment remains editable"), Equipment->IsReadOnly());
	TestEqual(TEXT("briefing no longer receives clicks behind equipment"), Briefing->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("operation cannot start over the open equipment screen"), Briefing->StartOperation());
	Briefing->AdvanceCountdownForAutomationTests(5.0f);
	Equipment->Close();
	TestEqual(TEXT("return preserves the elapsed thirteen seconds"), Briefing->GetRemainingSeconds(), 47.0f);
	TestEqual(TEXT("return restores interactive briefing"), Briefing->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("return preserves the selected team"), CastChecked<UTextBlock>(Briefing->GetWidgetFromName(TEXT("SelectedBriefingTeamText")))->GetText().ToString(), FString(TEXT("ORDER PILOTS")));
	const FMSDeckLoadout After = Deck->GetLoadoutInSlot(Deck->GetSelectedSlotIndex());
	TestTrue(TEXT("cancel preserves each weapon"), Before.EquippedWeapons == After.EquippedWeapons);
	TestTrue(TEXT("cancel preserves the module"), Before.EquippedModule == After.EquippedModule);
	Briefing->CloseForBattleEnd();
	return true;
}

#endif
