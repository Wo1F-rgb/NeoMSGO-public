#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockPushPopTest, "NeoMSGO.UI.Lock.PushPop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockIdempotencyTest, "NeoMSGO.UI.Lock.Idempotency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockOrderIndependenceTest, "NeoMSGO.UI.Lock.OrderIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockCloseTopTest, "NeoMSGO.UI.Lock.CloseTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockPlayerDeadTest, "NeoMSGO.UI.Lock.PlayerDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockStateDelegateTest, "NeoMSGO.UI.Lock.StateDelegate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockDeathActivationTest, "NeoMSGO.UI.Lock.DeathActivationCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUILockMapTravelResetTest, "NeoMSGO.UI.Lock.MapTravelReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSUILockSubsystemTests
{
	UMSUILockSubsystem* MakeSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UMSUILockSubsystem>(GameInstance);
	}
}

bool FMSUILockPushPopTest::RunTest(const FString&)
{
	UMSUILockSubsystem* UILock = MSUILockSubsystemTests::MakeSubsystem();
	TestFalse(TEXT("the stack starts empty"), UILock->IsAnyScreenOpen());
	UILock->PushScreen(TEXT("Settings"));
	TestTrue(TEXT("PushScreen opens the aggregate UI state"), UILock->IsAnyScreenOpen());
	TestFalse(TEXT("an open screen disables gameplay"), UILock->IsGameplayActive());
	UILock->PopScreen(TEXT("Settings"));
	TestFalse(TEXT("PopScreen empties the aggregate UI state"), UILock->IsAnyScreenOpen());
	TestTrue(TEXT("gameplay resumes after the final screen closes"), UILock->IsGameplayActive());
	return true;
}

bool FMSUILockIdempotencyTest::RunTest(const FString&)
{
	UMSUILockSubsystem* UILock = MSUILockSubsystemTests::MakeSubsystem();
	UILock->PushScreen(TEXT("Settings"));
	UILock->PushScreen(TEXT("Settings"));
	UILock->PopScreen(TEXT("Settings"));
	TestFalse(TEXT("a duplicate push does not require a duplicate pop"), UILock->IsAnyScreenOpen());
	UILock->PopScreen(TEXT("Settings"));
	UILock->PopScreen(TEXT("Missing"));
	TestTrue(TEXT("duplicate and missing pops leave gameplay active"), UILock->IsGameplayActive());
	return true;
}

bool FMSUILockOrderIndependenceTest::RunTest(const FString&)
{
	UMSUILockSubsystem* First = MSUILockSubsystemTests::MakeSubsystem();
	First->PushScreen(TEXT("Settings"));
	First->PushScreen(TEXT("DeploymentMap"));
	First->PopScreen(TEXT("Settings"));
	TestTrue(TEXT("closing the lower entry keeps the upper entry open"), First->IsAnyScreenOpen());
	First->PopScreen(TEXT("DeploymentMap"));
	TestFalse(TEXT("lower-then-upper close order empties the stack"), First->IsAnyScreenOpen());

	UMSUILockSubsystem* Second = MSUILockSubsystemTests::MakeSubsystem();
	Second->PushScreen(TEXT("Settings"));
	Second->PushScreen(TEXT("DeploymentMap"));
	Second->PopScreen(TEXT("DeploymentMap"));
	TestTrue(TEXT("closing the upper entry keeps the lower entry open"), Second->IsAnyScreenOpen());
	Second->PopScreen(TEXT("Settings"));
	TestFalse(TEXT("upper-then-lower close order empties the stack"), Second->IsAnyScreenOpen());
	return true;
}

bool FMSUILockCloseTopTest::RunTest(const FString&)
{
	UMSUILockSubsystem* UILock = MSUILockSubsystemTests::MakeSubsystem();
	int32 CloseOrder = 0;
	bool bTopCanClose = false;
	UILock->PushScreen(TEXT("Settings"), FMSUICloseRequestDelegate::CreateLambda([&CloseOrder]()
	{
		CloseOrder = CloseOrder * 10 + 1;
		return true;
	}));
	UILock->PushScreen(TEXT("DeploymentMap"), FMSUICloseRequestDelegate::CreateLambda([&CloseOrder, &bTopCanClose]()
	{
		CloseOrder = CloseOrder * 10 + 2;
		return bTopCanClose;
	}));

	TestTrue(TEXT("the top close request is consumed"), UILock->RequestCloseTopScreen());
	TestEqual(TEXT("the last-pushed screen receives the first close request"), CloseOrder, 2);
	TestTrue(TEXT("a screen that reports incomplete close remains on the stack"), UILock->IsAnyScreenOpen());
	bTopCanClose = true;
	TestTrue(TEXT("the remaining close request is consumed"), UILock->RequestCloseTopScreen());
	TestEqual(TEXT("the retained top screen receives the second close request"), CloseOrder, 22);
	TestTrue(TEXT("closing only the top screen leaves the lower screen open"), UILock->IsAnyScreenOpen());
	TestTrue(TEXT("the lower close request is consumed"), UILock->RequestCloseTopScreen());
	TestEqual(TEXT("the lower screen receives the third close request"), CloseOrder, 221);
	TestFalse(TEXT("both requested screens are popped"), UILock->IsAnyScreenOpen());
	TestFalse(TEXT("a close request on an empty stack is not consumed"), UILock->RequestCloseTopScreen());
	return true;
}

bool FMSUILockPlayerDeadTest::RunTest(const FString&)
{
	UMSUILockSubsystem* UILock = MSUILockSubsystemTests::MakeSubsystem();
	UILock->SetPlayerDead(true);
	TestFalse(TEXT("death disables gameplay"), UILock->IsGameplayActive());
	TestFalse(TEXT("Esc cannot close the death marker"), UILock->RequestCloseTopScreen());
	UILock->PushScreen(TEXT("Settings"));
	UILock->PopScreen(TEXT("Settings"));
	TestFalse(TEXT("closing all normal screens does not resume gameplay while dead"), UILock->IsGameplayActive());
	UILock->SetPlayerDead(false);
	TestTrue(TEXT("clearing death resumes gameplay when no normal screen is open"), UILock->IsGameplayActive());
	UILock->SetPlayerDead(false);
	TestTrue(TEXT("clearing death twice is idempotent"), UILock->IsGameplayActive());
	return true;
}

bool FMSUILockStateDelegateTest::RunTest(const FString&)
{
	UMSUILockSubsystem* UILock = MSUILockSubsystemTests::MakeSubsystem();
	TArray<bool> States;
	UILock->OnGameplayActiveChanged.AddLambda([&States](const bool bGameplayActive)
	{
		States.Add(bGameplayActive);
	});

	UILock->PushScreen(TEXT("Settings"));
	UILock->PushScreen(TEXT("Settings"));
	UILock->PushScreen(TEXT("DeploymentMap"));
	UILock->PopScreen(TEXT("Missing"));
	UILock->PopScreen(TEXT("Settings"));
	UILock->PopScreen(TEXT("DeploymentMap"));
	UILock->PopScreen(TEXT("DeploymentMap"));
	TestEqual(TEXT("only the inactive and active transitions broadcast"), States.Num(), 2);
	if (States.Num() == 2)
	{
		TestFalse(TEXT("the first transition reports gameplay inactive"), States[0]);
		TestTrue(TEXT("the second transition reports gameplay active"), States[1]);
	}

	UILock->SetPlayerDead(true);
	UILock->SetPlayerDead(true);
	UILock->SetPlayerDead(false);
	UILock->SetPlayerDead(false);
	TestEqual(TEXT("idempotent death updates add only two transitions"), States.Num(), 4);
	return true;
}

bool FMSUILockDeathActivationTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("Slate activation events are available"), FSlateApplication::IsInitialized())) return false;
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	// InitializeStandalone sets the GameInstance's own WorldContext. Creating a
	// separate context by hand would leave GetWorld null and skip input application.
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	ON_SCOPE_EXIT
	{
		GameInstance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		GameInstance->RemoveFromRoot();
	};
	// Controller registration happens during actor initialization, not merely
	// CreateWorld. Without this the primary-controller assertion must fail.
	World->InitializeActorsForPlay(FURL());
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	PlayerController->PlayerState = World->SpawnActor<APlayerState>();
	PlayerController->SetPlayer(LocalPlayer);
	if (!TestEqual(TEXT("the input policy resolves this real controller"),
		GameInstance->GetPrimaryPlayerController(false), PlayerController)) return false;
	UMSUILockSubsystem* UILock = GameInstance->GetSubsystem<UMSUILockSubsystem>();
	if (!TestNotNull(TEXT("the initialized subsystem receives Slate activation"), UILock)) return false;

	UILock->SetPlayerDead(true);
	TestFalse(TEXT("death disables gameplay"), UILock->IsGameplayActive());
	TestFalse(TEXT("death keeps keyboard input without a UI cursor"), PlayerController->bShowMouseCursor);
	FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(false);
	FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(true);
	TestFalse(TEXT("returning to the app does not unlock death"), UILock->IsGameplayActive());
	TestFalse(TEXT("returning to the app preserves death's hidden cursor"), PlayerController->bShowMouseCursor);

	UILock->PushScreen(TEXT("SpawnSelect"));
	TestFalse(TEXT("SpawnSelect above death still blocks gameplay"), UILock->IsGameplayActive());
	TestTrue(TEXT("a real menu gets a cursor without an active-state transition"), PlayerController->bShowMouseCursor);
	UILock->PopScreen(TEXT("SpawnSelect"));
	TestFalse(TEXT("closing the menu retains the death lock"), UILock->IsGameplayActive());
	TestFalse(TEXT("closing the menu restores death keyboard input"), PlayerController->bShowMouseCursor);

	FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(false);
	UILock->PushScreen(TEXT("SpawnSelect"));
	TestFalse(TEXT("input-mode changes are deferred while the app is inactive"), PlayerController->bShowMouseCursor);
	FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(true);
	TestTrue(TEXT("activation applies a menu opened while inactive"), PlayerController->bShowMouseCursor);
	UILock->PopScreen(TEXT("SpawnSelect"));
	UILock->SetPlayerDead(false);
	TestTrue(TEXT("respawning restores gameplay"), UILock->IsGameplayActive());
	TestFalse(TEXT("respawning leaves game-only input"), PlayerController->bShowMouseCursor);
	return true;
}

bool FMSUILockMapTravelResetTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	ON_SCOPE_EXIT
	{
		if (World && World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GameInstance->Shutdown();
		if (World && GEngine) GEngine->DestroyWorldContext(World);
		if (World) World->DestroyWorld(false);
		GameInstance->RemoveFromRoot();
	};
	if (!TestNotNull(TEXT("map travel fixture has a world"), World)) return false;
	World->InitializeActorsForPlay(FURL());
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("map travel fixture has a local controller"), PlayerController)) return false;
	PlayerController->SetPlayer(LocalPlayer);
	// UGameInstance::GetPrimaryPlayerController(false) still requires a real
	// PlayerState; without it PushScreen cannot apply the UI input mode.
	PlayerController->PlayerState = World->SpawnActor<APlayerState>();
	UMSUILockSubsystem* UILock = GameInstance->GetSubsystem<UMSUILockSubsystem>();
	if (!TestNotNull(TEXT("map travel fixture has an initialized UI lock"), UILock)) return false;

	UILock->PushScreen(TEXT("RoomCreate"));
	UILock->SetPlayerDead(true);
	TestTrue(TEXT("the old world starts with a modal and death marker"), UILock->IsAnyScreenOpen());
	TestFalse(TEXT("the old world is not gameplay-active"), UILock->IsGameplayActive());
	TestTrue(TEXT("the old modal enables the cursor"), PlayerController->bShowMouseCursor);

	// This is the engine travel boundary used by ClientTravel/OpenLevel.  The
	// test calls the real global delegate so the regression covers registration,
	// stack cleanup, and GameOnly input re-application together.
	const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
	if (!TestNotNull(TEXT("map travel fixture has a world context"), WorldContext)) return false;
	FCoreUObjectDelegates::PreLoadMapWithContext.Broadcast(
		*WorldContext, TEXT("/Game/Maps/Lvl_Biome_Daibao"));
	TestFalse(TEXT("travel clears old-world screen entries"), UILock->IsAnyScreenOpen());
	TestTrue(TEXT("travel clears the old death marker"), UILock->IsGameplayActive());
	TestFalse(TEXT("travel hides the stale modal cursor"), PlayerController->bShowMouseCursor);

	FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
	TestTrue(TEXT("new-world policy remains gameplay-active"), UILock->IsGameplayActive());
	TestFalse(TEXT("new-world policy keeps the cursor hidden"), PlayerController->bShowMouseCursor);
	return true;
}

#endif
