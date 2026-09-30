#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Combat/MSAttributeSet_Vital.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Audio/MSBgmSubsystem.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "InputCoreTypes.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Sound/SoundWave.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/Tests/MSDeploymentMapTestListener.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectStructureAndSelectionTest, "NeoMSGO.UI.SpawnSelect.StructureAndSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectOnlineAdmissionClockTest, "NeoMSGO.UI.SpawnSelect.OnlineAdmissionClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectRespawnReadinessTest, "NeoMSGO.UI.SpawnSelect.RespawnReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectDeathTransitionTest, "NeoMSGO.UI.SpawnSelect.DeathTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectUILockTest, "NeoMSGO.UI.SpawnSelect.UILock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectDeployGateTest, "NeoMSGO.UI.SpawnSelect.DeployGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectDuplicateDeckIndexTest, "NeoMSGO.UI.SpawnSelect.DuplicateDeckIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectPurePointSelectionTest, "NeoMSGO.UI.SpawnSelect.PurePointSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSpawnSelectStaleDeckTest, "NeoMSGO.UI.SpawnSelect.StaleDeckCannotDeploy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSpawnSelectWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSpawnSelectWidgetTestWorld"));
		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	/** A transient GameInstance is required to exercise the shared UI-lock path. */
	struct FScopedUILockWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSpawnSelectUILockTestWorld"));
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		FScopedUILockWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedUILockWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	struct FScopedOnlineWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSpawnSelectOnlineClockWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);

		FScopedOnlineWorld()
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

		~FScopedOnlineWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	TStrongObjectPtr<UMSDeckSubsystem> MakeDeck()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		return TStrongObjectPtr<UMSDeckSubsystem>(NewObject<UMSDeckSubsystem>(GameInstance));
	}

	TStrongObjectPtr<UMSSpawnSelectWidget> MakeWidget(UObject* Outer, UMSDeckSubsystem* Deck)
	{
		TStrongObjectPtr<UMSSpawnSelectWidget> Widget(NewObject<UMSSpawnSelectWidget>(Outer));
		Widget->Initialize(); Widget->InitializeForAutomationTests(); Widget->SetDeckSubsystemForTesting(Deck); return Widget;
	}

	UMSMobileSuitData* MakeSuit(UObject* Outer, const TCHAR* Name, int32 Cost, int32 Armor)
	{
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Outer); Suit->UnitName = FText::FromString(Name); Suit->Cost = Cost; Suit->MaxArmor = Armor; return Suit;
	}

	void TickWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.1f); World->Tick(LEVELTICK_All, Step); Seconds -= Step; ++GFrameCounter;
		}
	}
}

bool FMSSpawnSelectStructureAndSelectionTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedUILockWorld Scope;
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	UMSMobileSuitData* Alpha = MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("ALPHA"), 320, 2200);
	UMSMobileSuitData* Bravo = MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("BRAVO"), 480, 3600);
	Alpha->EmergencyRecallPerformance = 120;
	Bravo->EmergencyRecallPerformance = 880;
	Deck->AssignPreset(0, Alpha); Deck->AssignPreset(1, Bravo);
	TStrongObjectPtr<UMSSpawnSelectWidget> Widget = MSSpawnSelectWidgetTests::MakeWidget(Scope.World, Deck.Get());
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests(); Deployment->SetDeckSubsystemForTesting(Deck.Get());
	Widget->SetDeploymentMapWidgetForTesting(Deployment.Get()); Widget->SetMissionRuleName(FText::FromString(TEXT("拠点制圧戦")));
	Widget->OpenPreOperation(17.2f);

	TestEqual(TEXT("four deck cards are represented"), UMSSpawnSelectWidget::SlotCount, 4);
	TestNotNull(TEXT("left 2x2 deck grid exists"), Cast<UGridPanel>(Widget->GetWidgetFromName(TEXT("SpawnDeckGrid"))));
	TestNotNull(TEXT("central large preview exists"), Cast<UImage>(Widget->GetWidgetFromName(TEXT("SpawnSuitPreview"))));
	TestNotNull(TEXT("deploy button exists"), Cast<UButton>(Widget->GetWidgetFromName(TEXT("SpawnDeployButton"))));
	TestNotNull(TEXT("right suit detail name exists"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("SpawnSuitDetailNameText"))));
	TestNotNull(TEXT("right-bottom active parts grid exists"), Cast<UGridPanel>(Widget->GetWidgetFromName(TEXT("ActivePartsGrid"))));
	TestNull(TEXT("fixed dummy MU cost is not presented as active data"), Widget->GetWidgetFromName(TEXT("DummyMuCostText")));
	TestTrue(TEXT("first occupied card starts selected"), Widget->GetDisplayedSuitName().ToString().Contains(TEXT("ALPHA")));
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnSlot1Button")))->OnClicked.Broadcast();
	TestEqual(TEXT("card click changes selected slot"), Widget->GetSelectedSlotIndex(), 1);
	TestTrue(TEXT("right panel changes to clicked suit"), Widget->GetDisplayedSuitName().ToString().Contains(TEXT("BRAVO")));
	TestFalse(TEXT("empty slot card is disabled"), Widget->IsSlotEnabled(2));
	TestFalse(TEXT("empty slot cannot be selected"), Widget->SelectSlot(2));
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnSlot0Button")))->OnClicked.Broadcast();
	TestEqual(TEXT("selection returns to ALPHA before opening the map"), Widget->GetSelectedSlotIndex(), 0);
	TestEqual(TEXT("operation countdown continues with ceiling seconds"), Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("作戦開始まで 18秒")));
	Widget->AdvanceForAutomationTests(20.0f);
	TestEqual(TEXT("zero remains visible and does not auto-deploy"), Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("作戦開始まで 0秒")));
	TestTrue(TEXT("zero operation countdown still permits manual deploy"), Widget->IsDeployEnabled());
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("deploy requests the deployment map once"), Widget->GetDeploymentRequestCount(), 1);
	TestFalse(TEXT("spawn selection closes while choosing a point"), Widget->IsOpen());
	TestTrue(TEXT("deployment map opens"), Deployment->IsOpen());
	TestEqual(TEXT("selected preset is forwarded"), Deployment->GetDeployPreset(), Alpha);
	TestEqual(TEXT("selected battle rule is forwarded"), Deployment->GetMissionRuleName().ToString(), FString(TEXT("拠点制圧戦")));
	CastChecked<UButton>(Deployment->GetWidgetFromName(TEXT("DeploymentMSListSlot1Button")))->OnClicked.Broadcast();
	TestEqual(TEXT("map MS LIST changes the shared deck selection"), Deployment->GetSelectedDeckSlotIndex(), 1);
	TestEqual(TEXT("map MS LIST selects BRAVO"), Deployment->GetDeployPreset(), Bravo);

	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("the structure fixture owns the shared UI lock"), Lock);
	TestTrue(TEXT("deployment map is the top locked screen before Esc"), Lock && Lock->IsAnyScreenOpen());
	TestTrue(TEXT("Esc closes the deployment map through its close delegate"), Lock && Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc closes the deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("Esc restores the spawn selection screen"), Widget->IsOpen());
	TestEqual(TEXT("Esc keeps the restored BRAVO selection"), Widget->GetSelectedSlotIndex(), 1);
	TestTrue(TEXT("Esc keeps the restored BRAVO suit details"), Widget->GetDisplayedSuitName().ToString().Contains(TEXT("BRAVO")));

	// The restored screen must forward its currently selected deck entry again.
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("re-deploy increments the deployment request count"), Widget->GetDeploymentRequestCount(), 2);
	TestTrue(TEXT("re-deploy opens the deployment map"), Deployment->IsOpen());
	TestEqual(TEXT("re-deploy forwards BRAVO"), Deployment->GetDeployPreset(), Bravo);
	TestEqual(TEXT("re-deploy forwards the selected BRAVO slot"), Deployment->GetSelectedDeckSlotIndex(), 1);

	const TSet<FKey> PressedButtons;
	const FPointerEvent RightClick(0, FVector2D::ZeroVector, FVector2D::ZeroVector, PressedButtons,
		EKeys::RightMouseButton, 0.0f, FModifierKeysState());
	TestTrue(TEXT("right-click closes the deployment map through its real pointer handler"),
		Deployment->HandlePointerForAutomationTests(RightClick).IsEventHandled());
	TestFalse(TEXT("right-click closes the deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("right-click restores spawn selection"), Widget->IsOpen());
	TestEqual(TEXT("right-click keeps the restored BRAVO selection"), Widget->GetSelectedSlotIndex(), 1);

	// Keep the button route covered after the same round-trip state as well.
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("second re-deploy increments the deployment request count"), Widget->GetDeploymentRequestCount(), 3);
	TestTrue(TEXT("second re-deploy opens the deployment map"), Deployment->IsOpen());
	TestEqual(TEXT("second re-deploy forwards BRAVO"), Deployment->GetDeployPreset(), Bravo);
	CastChecked<UButton>(Deployment->GetWidgetFromName(TEXT("ReturnToSuitSelectionButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("return button closes deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("return button restores spawn selection"), Widget->IsOpen());
	TestEqual(TEXT("return synchronizes the selected deck index"), Widget->GetSelectedSlotIndex(), 1);
	TestTrue(TEXT("return synchronizes the selected suit name"), Widget->GetDisplayedSuitName().ToString().Contains(TEXT("BRAVO")));
	TestEqual(TEXT("return synchronizes the emergency recall value"),
		CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("SpawnSuitStatusValue5")))->GetText().ToString(), FString(TEXT("880")));
	TestTrue(TEXT("return synchronizes the emergency recall bar"),
		FMath::IsNearlyEqual(CastChecked<UProgressBar>(Widget->GetWidgetFromName(TEXT("SpawnSuitStatusBar5")))->GetPercent(), 0.88f));
	Widget->Close();
	return true;
}

bool FMSSpawnSelectOnlineAdmissionClockTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedOnlineWorld Scope;
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online spawn-select fixture has an account subsystem"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);

	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("online spawn-select fixture has a real GameState"), State)) return false;
	State->bBattleEnded = false;
	State->MatchDuration = 1200.0f;
	State->RemainingTime = State->MatchDuration;

	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	Deck->AssignPreset(0, MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("ONLINE CLOCK"), 300, 2000));
	TStrongObjectPtr<UMSSpawnSelectWidget> Widget = MSSpawnSelectWidgetTests::MakeWidget(Scope.World, Deck.Get());
	Widget->OpenPreOperation(30.0f);
	TestEqual(TEXT("full-duration online match shows deployment readiness instead of local zero seconds"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("出撃準備")));

	State->RemainingTime = 734.0f;
	Widget->AdvanceForAutomationTests(0.0f);
	TestEqual(TEXT("online spawn-select uses authoritative battle time"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("戦闘中 12'14")));

	State->bBattleEnded = true;
	Widget->AdvanceForAutomationTests(0.0f);
	TestEqual(TEXT("online spawn-select reflects battle completion"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("戦闘終了")));
	Widget->Close();
	return true;
}

bool FMSSpawnSelectRespawnReadinessTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent(); Respawn->SetRespawnTimings(0.1f, 1.0f); Respawn->SetAutoRespawnEnabled(false);
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	Deck->AssignPreset(0, MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("READY TEST"), 300, 2000));
	TStrongObjectPtr<UMSSpawnSelectWidget> Widget = MSSpawnSelectWidgetTests::MakeWidget(Scope.World, Deck.Get());
	Player->GetVitalSet()->OnDeath.Broadcast(Player, nullptr);
	Widget->OpenRespawn(Respawn);
	TestFalse(TEXT("deploy is disabled while respawn readiness is false"), Widget->IsDeployEnabled());
	TestTrue(TEXT("occupied card shows the shared respawn countdown"), Widget->GetCardRespawnText(0).ToString().Contains(TEXT("再出撃まで\nあと")));
	TestTrue(TEXT("empty card has no countdown"), Widget->GetCardRespawnText(1).IsEmpty());
	// TimerManager establishes its internal time on the first world tick, so leave the same
	// post-deadline margin used by the existing respawn-component tests.
	MSSpawnSelectWidgetTests::TickWorld(Scope.World, 1.5f);
	TestTrue(TEXT("deploy becomes enabled when CanDeploy becomes true"), Widget->IsDeployEnabled());
	TestTrue(TEXT("respawn countdown overlay extinguishes at zero"), Widget->GetCardRespawnText(0).IsEmpty());
	return true;
}

bool FMSSpawnSelectDeathTransitionTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	APlayerController* PC = Scope.World->SpawnActor<APlayerController>();
	// Lethal damage also creates the real received-damage overlay. A local flag
	// alone cannot own UMG widgets; attach a transient local player like runtime.
	PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); PC->Possess(Player); PC->SetAsLocalPlayerController();
	if (!TestNotNull(TEXT("death UI has a real local-player owner"),PC->GetLocalPlayer())) return false;
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	Deck->AssignPreset(0, MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("DEATH TEST"), 300, 2000));
	TStrongObjectPtr<UMSSpawnSelectWidget> Widget = MSSpawnSelectWidgetTests::MakeWidget(Scope.World, Deck.Get());
	Player->SetSpawnSelectWidgetForTesting(Widget.Get());
	TestFalse(TEXT("spawn selection starts closed"), Widget->IsOpen());
	// A real death changes armor and State.Dead before OnDeath. Broadcasting
	// the delegate on a living pawn would now correctly enter emergency recall.
	Player->InitializeUnit(Deck->GetPresetInSlot(0));
	Player->SetGenericTeamId(FGenericTeamId(1));
	FActorSpawnParameters DamageSourceParams;
	DamageSourceParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* DamageSource=Scope.World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(100000,0,0),FRotator::ZeroRotator,DamageSourceParams);
	if (!TestNotNull(TEXT("lethal damage source"),DamageSource)) return false;
	DamageSource->SetGenericTeamId(FGenericTeamId(2));
	UAbilitySystemComponent* SourceASC=DamageSource->GetAbilitySystemComponent();
	FGameplayEffectContextHandle DamageContext=SourceASC->MakeEffectContext();
	DamageContext.AddInstigator(DamageSource,DamageSource);
	FGameplayEffectSpec DamageSpec(GetDefault<UGE_Damage>(),DamageContext,1.0f);
	DamageSpec.SetSetByCallerMagnitude(MSTags::Data_Damage,100000.0f);
	SourceASC->ApplyGameplayEffectSpecToTarget(DamageSpec,Player->GetAbilitySystemComponent());
	TestEqual(TEXT("lethal GAS damage reduces armor to zero"),Player->GetVitalSet()->GetArmor(),0.0f);
	TestTrue(TEXT("actual death applies State.Dead"),Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestEqual(TEXT("death starts the battle presentation without a menu request"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::None);
	TestTrue(TEXT("death presentation is active"), Player->IsDeathPresentationActive());
	TestFalse(TEXT("death stops gameplay input"), Player->IsGameplayInputAllowed());
	TestFalse(TEXT("death does not immediately open spawn selection"), Widget->IsOpen());
	MSSpawnSelectWidgetTests::TickWorld(Scope.World, 5.5f);
	TestTrue(TEXT("explosion occurs during the death presentation"), Player->GetRespawnComponent()->HasExplosionFired());
	TestFalse(TEXT("explosion alone does not open spawn selection"), Widget->IsOpen());
	MSSpawnSelectWidgetTests::TickWorld(Scope.World, 2.0f);
	TestEqual(TEXT("respawn readiness requests spawn selection"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::SpawnSelect);
	TestFalse(TEXT("respawn readiness ends death presentation"), Player->IsDeathPresentationActive());
	TestTrue(TEXT("respawn readiness opens spawn selection"), Widget->IsOpen());
	TestEqual(TEXT("ready transition opens respawn mode"), Widget->GetMode(), EMSSpawnSelectMode::Respawn);
	return true;
}

bool FMSSpawnSelectUILockTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedUILockWorld Scope;
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	Deck->AssignPreset(0, MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("LOCK TEST"), 300, 2000));
	TStrongObjectPtr<UMSSpawnSelectWidget> Widget(NewObject<UMSSpawnSelectWidget>(Scope.World));
	Widget->Initialize(); Widget->InitializeForAutomationTests(); Widget->SetDeckSubsystemForTesting(Deck.Get());
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	if (!TestNotNull(TEXT("transient GameInstance owns the UI lock"), Lock)) return false;

	Widget->OpenPreOperation(12.0f);
	TestTrue(TEXT("pre-operation opens the spawn screen lock"), Lock->IsAnyScreenOpen());
	const TSet<FKey> PressedButtons;
	const FPointerEvent RightClick(0, FVector2D::ZeroVector, FVector2D::ZeroVector, PressedButtons,
		EKeys::RightMouseButton, 0.0f, FModifierKeysState());
	TestTrue(TEXT("right-click is consumed by spawn selection"), Widget->HandlePointerForAutomationTests(RightClick).IsEventHandled());
	TestTrue(TEXT("right-click does not close pre-operation spawn selection"), Widget->IsOpen());
	TestTrue(TEXT("Esc close request is consumed"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("Esc does not close pre-operation spawn selection"), Widget->IsOpen());
	TestTrue(TEXT("Esc keeps gameplay locked"), Lock->IsAnyScreenOpen());
	Widget->Close();

	Widget->OpenRespawn(nullptr);
	TestTrue(TEXT("respawn opens the spawn screen lock"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("Esc close request is consumed in respawn mode"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("Esc does not close respawn spawn selection"), Widget->IsOpen());
	Widget->Close();

	Widget->OpenPreOperation(12.0f);
	Widget->ResumeFromDeployment();
	TestTrue(TEXT("resume restores the spawn screen lock"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("Esc close request is consumed after resume"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("Esc does not close resumed spawn selection"), Widget->IsOpen());
	Widget->Close();
	TestFalse(TEXT("programmatic Close still releases the UI lock"), Lock->IsAnyScreenOpen());
	return true;
}

bool FMSSpawnSelectDeployGateTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedUILockWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("deploy gate test player spawned"), Player)) return false;
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	Respawn->SetRespawnTimings(0.1f, 1.0f);
	Respawn->SetAutoRespawnEnabled(false);
	Player->GetVitalSet()->OnDeath.Broadcast(Player, nullptr);

	UMSMobileSuitData* Preset = MSSpawnSelectWidgetTests::MakeSuit(Scope.World, TEXT("GATE TEST"), 300, 2000);
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	Deck->AssignPreset(0, Preset);
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests();
	Deployment->SetDeckSubsystemForTesting(Deck.Get());
	Deployment->SetRespawnComponentForTesting(Respawn); Deployment->SetDeployPreset(Preset);
	Deployment->SetVisibility(ESlateVisibility::Visible);
	UMSBgmSubsystem* Bgm = Scope.GameInstance->GetSubsystem<UMSBgmSubsystem>();
	if (!TestNotNull(TEXT("transient GameInstance owns the BGM subsystem"), Bgm)) return false;
	USoundWave* BattleTrack = NewObject<USoundWave>(Scope.GameInstance);
	Bgm->SetPlaylistForTesting(EMSBgmScene::Battle, { BattleTrack });
	TStrongObjectPtr<UMSDeploymentMapTestListener> Listener(NewObject<UMSDeploymentMapTestListener>(Deployment.Get()));
	Deployment->OnSpawnPointSelected.AddDynamic(Listener.Get(), &UMSDeploymentMapTestListener::HandleSpawnPointSelected);
	const FVector Target(1200.0f, -3400.0f, 180.0f);
	Deployment->ConfirmSelectionForAutomationTests(Target);
	TestEqual(TEXT("blocked deploy emits no spawn selection notification"), Listener->BroadcastCount, 0);
	TestFalse(TEXT("blocked deploy does not retain a selected transform"), Deployment->HasSelectedSpawnTransform());
	TestTrue(TEXT("blocked deploy keeps the map open"), Deployment->IsOpen());
	TestTrue(TEXT("blocked deploy leaves BGM unchanged"), Bgm->GetCurrentScene() == EMSBgmScene::None);

	MSSpawnSelectWidgetTests::TickWorld(Scope.World, 1.5f);
	TestTrue(TEXT("the same point becomes deployable after respawn wait"), Respawn->CanDeploy());
	Deployment->ConfirmSelectionForAutomationTests(Target);
	TestEqual(TEXT("allowed deploy emits exactly one spawn selection notification"), Listener->BroadcastCount, 1);
	TestFalse(TEXT("allowed deploy closes the map frontend"), Deployment->IsOpen());
	TestTrue(TEXT("allowed deploy changes the battle BGM scene"), Bgm->GetCurrentScene() == EMSBgmScene::Battle);
	return true;
}

bool FMSSpawnSelectDuplicateDeckIndexTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	UMSMobileSuitData* SharedPreset = MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("SHARED PRESET"), 300, 2000);
	Deck->AssignPreset(0, SharedPreset);
	Deck->AssignPreset(1, SharedPreset);
	Deck->SetSelectedSlotIndex(1);

	TStrongObjectPtr<UMSSpawnSelectWidget> Widget = MSSpawnSelectWidgetTests::MakeWidget(Scope.World, Deck.Get());
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests(); Deployment->SetDeckSubsystemForTesting(Deck.Get());
	Widget->SetDeploymentMapWidgetForTesting(Deployment.Get());
	Widget->OpenPreOperation(0.0f);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();

	TestEqual(TEXT("deployment map preserves the selected duplicate slot index"), Deployment->GetSelectedDeckSlotIndex(), 1);
	TestEqual(TEXT("deployment map preserves the shared preset"), Deployment->GetDeployPreset(), SharedPreset);
	return true;
}

bool FMSSpawnSelectPurePointSelectionTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedWorld Scope;
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests();
	Deployment->bDeployOnSelect = false;
	Deployment->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSDeploymentMapTestListener> Listener(NewObject<UMSDeploymentMapTestListener>(Deployment.Get()));
	Deployment->OnSpawnPointSelected.AddDynamic(Listener.Get(), &UMSDeploymentMapTestListener::HandleSpawnPointSelected);

	Deployment->ConfirmSelectionForAutomationTests(FVector(80.0f, 120.0f, 0.0f));
	TestEqual(TEXT("pure point selection keeps its notification contract"), Listener->BroadcastCount, 1);
	TestTrue(TEXT("pure point selection retains the selected transform"), Deployment->HasSelectedSpawnTransform());
	TestTrue(TEXT("pure point selection leaves the map open"), Deployment->IsOpen());
	return true;
}

bool FMSSpawnSelectStaleDeckTest::RunTest(const FString&)
{
	MSSpawnSelectWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	TStrongObjectPtr<UMSDeckSubsystem> Deck = MSSpawnSelectWidgetTests::MakeDeck();
	UMSMobileSuitData* Original = MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("ORIGINAL"), 300, 2000);
	UMSMobileSuitData* Replacement = MSSpawnSelectWidgetTests::MakeSuit(Deck.Get(), TEXT("REPLACEMENT"), 320, 3000);
	Deck->AssignPreset(0, Original);
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests();
	Deployment->SetDeckSubsystemForTesting(Deck.Get()); Deployment->SetRespawnComponentForTesting(Player->GetRespawnComponent());
	Deployment->SetDeployPreset(Original); Deployment->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSDeploymentMapTestListener> Listener(NewObject<UMSDeploymentMapTestListener>(Deployment.Get()));
	Deployment->OnSpawnPointSelected.AddDynamic(Listener.Get(), &UMSDeploymentMapTestListener::HandleSpawnPointSelected);
	const FVector Before = Player->GetActorLocation();
	Deck->RemovePreset(0);
	Deployment->ConfirmSelectionForAutomationTests(FVector(40000, 30000, 100));
	TestEqual(TEXT("removed deck slot emits no deployment event"), Listener->BroadcastCount, 0);
	TestTrue(TEXT("removed slot cannot move the player"), Player->GetActorLocation().Equals(Before));
	TestFalse(TEXT("removed slot cannot retain a chosen spawn transform"), Deployment->HasSelectedSpawnTransform());
	Deck->AssignPreset(0, Replacement);
	Deployment->ConfirmSelectionForAutomationTests(FVector(40000, 30000, 100));
	TestEqual(TEXT("replacement requires a fresh suit selection"), Listener->BroadcastCount, 0);
	TestTrue(TEXT("stale original cannot deploy after replacement"), Player->GetActorLocation().Equals(Before));
	TestTrue(TEXT("stale selection leaves the map open"), Deployment->IsOpen());
	return true;
}

#endif
