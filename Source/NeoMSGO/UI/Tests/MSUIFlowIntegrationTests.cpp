#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSFullscreenMapWidget.h"
#include "UI/MSMapTypes.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSResultWidget.h"
#include "UI/MSRoomCreateWidget.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSSuitSwapWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowBattleEndCleanupTest, "NeoMSGO.UI.Flow.BattleEndClosesFrontend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowCustomizeEscTest, "NeoMSGO.UI.Flow.Esc.CustomizeDetailSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowSwapComparisonEscTest, "NeoMSGO.UI.Flow.Esc.SwapComparisonRetainsStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowBriefingEscTest, "NeoMSGO.UI.Flow.Esc.BriefingConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowDeploymentEscTest, "NeoMSGO.UI.Flow.Esc.SpawnSelectDeployment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIFlowRoundTripTest, "NeoMSGO.UI.Flow.OperationDeployDeathRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFullscreenMapOverlayInputTest, "NeoMSGO.UI.Flow.FullscreenMap.GameplayOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomCreateCursorTest, "NeoMSGO.UI.Flow.RoomCreate.KeepsMouseCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSUIFlowIntegrationTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSUIFlowIntegrationTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		APlayerController* PlayerController = nullptr;

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
			PlayerController = World->SpawnActor<APlayerController>();
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
		TStrongObjectPtr<T> Result(NewObject<T>(World));
		Result->Initialize();
		Result->InitializeForAutomationTests();
		return Result;
	}

	UMSMobileSuitData* AddDeckSuit(UMSDeckSubsystem* Deck, const TCHAR* Name = TEXT("FLOW TEST SUIT"))
	{
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Deck);
		Suit->UnitName = FText::FromString(Name);
		Suit->Cost = 320;
		Suit->MaxArmor = 2400;
		Deck->AssignPreset(0, Suit);
		return Suit;
	}
}

bool FMSUIFlowBattleEndCleanupTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MakeWidget<UMSBriefingWidget>(Scope.World);
	TStrongObjectPtr<UMSSortieWidget> Sortie = MakeWidget<UMSSortieWidget>(Scope.World);
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect = MakeWidget<UMSSpawnSelectWidget>(Scope.World);
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment = MakeWidget<UMSDeploymentMapWidget>(Scope.World);
	TStrongObjectPtr<UMSFullscreenMapWidget> FullscreenMap = MakeWidget<UMSFullscreenMapWidget>(Scope.World);
	TStrongObjectPtr<UMSResultWidget> Result = MakeWidget<UMSResultWidget>(Scope.World);

	Lobby->SetBriefingWidgetForTesting(Briefing.Get());
	Lobby->SetResultWidgetForTesting(Result.Get());
	Briefing->SetSortieWidgetForTesting(Sortie.Get());
	Briefing->SetSpawnSelectWidgetForTesting(SpawnSelect.Get());
	Lobby->Open();
	Lobby->Close();
	Briefing->Open();
	Sortie->SetBriefingWidget(Briefing.Get());
	Sortie->Open();
	SpawnSelect->OpenPreOperation(30.0f);
	Deployment->Open();
	FullscreenMap->Open();

	AMSGameMode* GameMode = Scope.World->GetAuthGameMode<AMSGameMode>();
	TestNotNull(TEXT("test world has the authoritative game mode"), GameMode);
	if (!GameMode) return false;
	GameMode->EndBattle(true);

	TestFalse(TEXT("battle end closes deployment selection"), Deployment->IsOpen());
	TestFalse(TEXT("battle end closes the combat-map overlay"), FullscreenMap->IsOpen());
	TestFalse(TEXT("battle end closes mobile-suit selection"), SpawnSelect->IsOpen());
	TestFalse(TEXT("battle end closes sortie confirmation modal"), Sortie->IsOpen());
	TestFalse(TEXT("battle end closes briefing"), Briefing->IsOpen());
	TestFalse(TEXT("battle end does not skip directly to the lobby"), Lobby->IsOpen());
	TestTrue(TEXT("battle end opens the result screen"), Result->IsOpen());
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestTrue(TEXT("Esc closes the result screen"), Lock && Lock->RequestCloseTopScreen());
	TestFalse(TEXT("result is collapsed after Esc"), Result->IsOpen());
	TestTrue(TEXT("result Esc returns to lobby"), Lobby->IsOpen());
	return true;
}

bool FMSUIFlowCustomizeEscTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("shared deck subsystem exists"), Deck);
	TestNotNull(TEXT("shared UI lock exists"), Lock);
	if (!Deck || !Lock) return false;
	AddDeckSuit(Deck);

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MakeWidget<UMSCustomizeWidget>(Scope.World);
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MakeWidget<UMSSuitDetailWidget>(Scope.World);
	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MakeWidget<UMSSuitSwapWidget>(Scope.World);
	Lobby->SetCustomizeWidgetForTesting(Customize.Get());
	Customize->SetDeckSubsystemForTesting(Deck);
	Customize->SetSuitDetailWidgetForTesting(Detail.Get());
	Detail->SetDeckSubsystemForTesting(Deck);
	Detail->SetSwapWidgetForTesting(Swap.Get());
	Swap->SetDeckSubsystemForTesting(Deck);

	Lobby->Open();
	Lobby->Close();
	Customize->Open();
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard0")))->OnClicked.Broadcast();
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("SuitSwapButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("deepest swap screen is open"), Swap->IsOpen());

	TestTrue(TEXT("first Esc is consumed by swap"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("first Esc closes swap only"), Swap->IsOpen());
	TestTrue(TEXT("first Esc restores suit detail"), Detail->IsOpen());
	TestTrue(TEXT("second Esc is consumed by suit detail"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("second Esc closes suit detail"), Detail->IsOpen());
	TestTrue(TEXT("second Esc restores customize"), Customize->IsOpen());
	TestTrue(TEXT("third Esc is consumed by customize"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("third Esc closes customize"), Customize->IsOpen());
	TestTrue(TEXT("third Esc restores lobby"), Lobby->IsOpen());
	return true;
}

bool FMSUIFlowSwapComparisonEscTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("shared deck subsystem exists"), Deck);
	TestNotNull(TEXT("shared UI lock exists"), Lock);
	if (!Deck || !Lock) return false;
	AddDeckSuit(Deck, TEXT("CURRENT SUIT"));
	UMSMobileSuitData* Candidate = NewObject<UMSMobileSuitData>(Deck);
	Candidate->UnitName = FText::FromString(TEXT("CANDIDATE SUIT"));
	Candidate->Cost = 400;

	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MakeWidget<UMSSuitDetailWidget>(Scope.World);
	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MakeWidget<UMSSuitSwapWidget>(Scope.World);
	Detail->SetDeckSubsystemForTesting(Deck);
	Detail->SetSwapWidgetForTesting(Swap.Get());
	Swap->SetDeckSubsystemForTesting(Deck);
	Swap->SetCatalogForTesting({ Candidate });
	Detail->Open(0);
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("SuitSwapButton")))->OnClicked.Broadcast();
	Swap->SelectCandidate(Candidate);
	TestTrue(TEXT("swap enters comparison phase"), Swap->IsComparisonPhase());

	TestTrue(TEXT("first Esc is consumed by comparison"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("first Esc returns to the list phase"), Swap->IsComparisonPhase());
	TestTrue(TEXT("swap remains open and retained at the top of the stack"), Swap->IsOpen());
	TestFalse(TEXT("detail remains hidden behind swap"), Detail->IsOpen());

	TestTrue(TEXT("second Esc is consumed by the retained swap"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("second Esc closes swap"), Swap->IsOpen());
	TestTrue(TEXT("second Esc restores detail"), Detail->IsOpen());
	return true;
}

bool FMSUIFlowBriefingEscTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MakeWidget<UMSBriefingWidget>(Scope.World);
	TStrongObjectPtr<UMSSortieWidget> Sortie = MakeWidget<UMSSortieWidget>(Scope.World);
	Lobby->SetBriefingWidgetForTesting(Briefing.Get());
	Briefing->SetSortieWidgetForTesting(Sortie.Get());

	Lobby->Open();
	Lobby->Close();
	Briefing->Open();
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("SuitConfirmationButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("confirmation modal opens above briefing"), Sortie->IsOpen());
	TestTrue(TEXT("first Esc closes confirmation modal"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("confirmation modal is closed"), Sortie->IsOpen());
	TestTrue(TEXT("briefing remains after modal Esc"), Briefing->IsOpen());
	TestTrue(TEXT("second Esc closes briefing"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("briefing is closed"), Briefing->IsOpen());
	TestTrue(TEXT("briefing Esc restores lobby"), Lobby->IsOpen());
	return true;
}

bool FMSUIFlowDeploymentEscTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	AddDeckSuit(Deck);
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect = MakeWidget<UMSSpawnSelectWidget>(Scope.World);
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment = MakeWidget<UMSDeploymentMapWidget>(Scope.World);
	SpawnSelect->SetDeckSubsystemForTesting(Deck);
	SpawnSelect->SetDeploymentMapWidgetForTesting(Deployment.Get());
	SpawnSelect->OpenPreOperation(20.0f);
	CastChecked<UButton>(SpawnSelect->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("deployment transition closes spawn selection"), SpawnSelect->IsOpen());
	TestTrue(TEXT("deployment transition opens deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("Esc is consumed by deployment map"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc closes deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("Esc restores the same spawn selection"), SpawnSelect->IsOpen());
	return true;
}

bool FMSUIFlowRoundTripTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	UMSMobileSuitData* Suit = AddDeckSuit(Deck, TEXT("ROUND TRIP SUIT"));
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	// The real damage path builds a UMG damage overlay before death. Supply its
	// local-player ownership rather than suppressing the CreateWidget error.
	Scope.PlayerController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	Scope.PlayerController->Possess(Player);
	Scope.PlayerController->SetAsLocalPlayerController();
	if (!TestNotNull(TEXT("round-trip UI has a real local-player owner"),Scope.PlayerController->GetLocalPlayer())) return false;

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSBriefingWidget> Briefing = MakeWidget<UMSBriefingWidget>(Scope.World);
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect = MakeWidget<UMSSpawnSelectWidget>(Scope.World);
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment = MakeWidget<UMSDeploymentMapWidget>(Scope.World);
	Lobby->SetBriefingWidgetForTesting(Briefing.Get());
	Briefing->SetSpawnSelectWidgetForTesting(SpawnSelect.Get());
	SpawnSelect->SetDeckSubsystemForTesting(Deck);
	SpawnSelect->SetDeploymentMapWidgetForTesting(Deployment.Get());
	Deployment->SetRespawnComponentForTesting(Player->GetRespawnComponent());
	Player->SetSpawnSelectWidgetForTesting(SpawnSelect.Get());

	Lobby->Open();
	Lobby->SelectBattleRule(0);
	TestTrue(TEXT("lobby operation decision opens briefing"), Lobby->ApplySelectedBattleRule());
	TestTrue(TEXT("briefing starts operation into spawn selection"), Briefing->StartOperation());
	CastChecked<UButton>(SpawnSelect->GetWidgetFromName(TEXT("SpawnDeployButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("spawn selection advances to deployment map"), Deployment->IsOpen());
	Deployment->ConfirmSelectionForAutomationTests(FVector(1000.0, 2000.0, 100.0));
	TestEqual(TEXT("Deploy applies the selected deck suit"), Player->GetMobileSuitData(), Suit);
	TestFalse(TEXT("Deploy closes deployment map"), Deployment->IsOpen());

	// Follow the real lethal-damage path: alive recall and dead deployment are
	// distinct, so an OnDeath broadcast with positive armor is not a valid fixture.
	Player->SetGenericTeamId(FGenericTeamId(1));
	FActorSpawnParameters DamageSourceParams;
	DamageSourceParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* DamageSource=Scope.World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(100000,0,0),FRotator::ZeroRotator,DamageSourceParams);
	if (!TestNotNull(TEXT("round-trip lethal damage source"),DamageSource)) return false;
	DamageSource->SetGenericTeamId(FGenericTeamId(2));
	UAbilitySystemComponent* SourceASC=DamageSource->GetAbilitySystemComponent();
	FGameplayEffectContextHandle DamageContext=SourceASC->MakeEffectContext();
	DamageContext.AddInstigator(DamageSource,DamageSource);
	FGameplayEffectSpec DamageSpec(GetDefault<UGE_Damage>(),DamageContext,1.0f);
	DamageSpec.SetSetByCallerMagnitude(MSTags::Data_Damage,100000.0f);
	SourceASC->ApplyGameplayEffectSpecToTarget(DamageSpec,Player->GetAbilitySystemComponent());
	TestEqual(TEXT("round-trip lethal damage reaches zero armor"),Player->GetVitalSet()->GetArmor(),0.0f);
	TestTrue(TEXT("round-trip actual death applies State.Dead"),Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestFalse(TEXT("death initially keeps the battlefield visible without the selection screen"), SpawnSelect->IsOpen());
	TestTrue(TEXT("death enters its presentation phase"),Player->IsDeathPresentationActive());
	TestFalse(TEXT("death blocks gameplay input"),Player->IsGameplayInputAllowed());
	for(int32 Tick=0;Tick<75;++Tick) { Scope.World->Tick(LEVELTICK_All,.1f);++GFrameCounter; }
	TestTrue(TEXT("death wait completion returns to spawn selection"), SpawnSelect->IsOpen());
	TestEqual(TEXT("completed death wait uses respawn selection mode"), SpawnSelect->GetMode(), EMSSpawnSelectMode::Respawn);
	return true;
}

bool FMSFullscreenMapOverlayInputTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	Scope.PlayerController->Possess(Player);
	Scope.PlayerController->SetAsLocalPlayerController();
	Scope.PlayerController->SetInputMode(FInputModeGameOnly());
	Scope.PlayerController->bShowMouseCursor = false;

	TStrongObjectPtr<UMSFullscreenMapWidget> FullscreenMap = MakeWidget<UMSFullscreenMapWidget>(Scope.World);
	Player->SetFullscreenMapForTesting(FullscreenMap.Get());
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("shared UI lock exists"), Lock);
	if (!Lock) return false;

	Player->ToggleFullscreenMap();
	TestTrue(TEXT("the first Tab-equivalent toggle opens the combat map"), FullscreenMap->IsOpen());
	const AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	const UTextBlock* MapName = Cast<UTextBlock>(FullscreenMap->GetWidgetFromName(TEXT("FullscreenMapName")));
	const UTextBlock* Time = Cast<UTextBlock>(FullscreenMap->GetWidgetFromName(TEXT("FullscreenMapRemainingTime")));
	const UTextBlock* North = Cast<UTextBlock>(FullscreenMap->GetWidgetFromName(TEXT("FullscreenMapNorth")));
	TestNotNull(TEXT("full map header has a map name"), MapName);
	TestNotNull(TEXT("full map header has remaining time"), Time);
	TestNotNull(TEXT("full map header has player gauge"), Cast<UProgressBar>(FullscreenMap->GetWidgetFromName(TEXT("FullscreenMapPlayerGauge"))));
	TestNotNull(TEXT("full map header has enemy gauge"), Cast<UProgressBar>(FullscreenMap->GetWidgetFromName(TEXT("FullscreenMapEnemyGauge"))));
	TestEqual(TEXT("header uses the GameState map display name"), MapName ? MapName->GetText().ToString() : FString(), State ? State->MapDisplayName.ToString() : FString());
	TestEqual(TEXT("header uses the formatted GameState time"), Time ? Time->GetText().ToString() : FString(), State ? State->GetFormattedRemainingTime().ToString() : FString());
	TestEqual(TEXT("north indicator is N"), North ? North->GetText().ToString() : FString(), FString(TEXT("N")));
	if (const UCanvasPanelSlot* NorthSlot = North ? Cast<UCanvasPanelSlot>(North->Slot) : nullptr)
		TestTrue(TEXT("north indicator is on the map's upper edge"), NorthSlot->GetPosition().Y < MSMapPalette::FullscreenMapSizePx * 0.1f);
	TestEqual(TEXT("the open combat map is pointer-transparent"), FullscreenMap->GetVisibility(),
		ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("the combat map does not show the mouse cursor"), Scope.PlayerController->bShowMouseCursor);
	TestTrue(TEXT("the combat map leaves GameOnly gameplay input active"), Lock->IsGameplayActive());
	TestFalse(TEXT("the combat map is not registered in the UI-lock stack"), Lock->IsAnyScreenOpen());

	Player->ToggleFullscreenMap();
	TestFalse(TEXT("the second Tab-equivalent toggle closes the combat map"), FullscreenMap->IsOpen());
	TestFalse(TEXT("closing the combat map keeps the mouse cursor hidden"), Scope.PlayerController->bShowMouseCursor);
	TestTrue(TEXT("closing the combat map leaves the UI-lock stack empty"), Lock->IsGameplayActive());
	return true;
}

bool FMSRoomCreateCursorTest::RunTest(const FString&)
{
	using namespace MSUIFlowIntegrationTests;
	FScopedWorld Scope;
	Scope.PlayerController->SetAsLocalPlayerController();
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TestNotNull(TEXT("shared UI lock exists"), Lock);
	if (!Lock) return false;

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSRoomCreateWidget> RoomCreate = MakeWidget<UMSRoomCreateWidget>(Scope.World);
	RoomCreate->SetLobbyWidget(Lobby.Get());

	// The lobby hands off by closing itself before opening the room screen, so the
	// room screen must register itself or the stack empties and input reverts to GameOnly.
	Lobby->Open();
	Lobby->Close();
	RoomCreate->OpenCreate();
	TestTrue(TEXT("the room creation screen registers in the UI-lock stack"), Lock->IsAnyScreenOpen());
	TestFalse(TEXT("the room creation screen suspends gameplay input"), Lock->IsGameplayActive());

	RoomCreate->Close();
	TestFalse(TEXT("closing the room creation screen clears the UI-lock stack"), Lock->IsAnyScreenOpen());

	RoomCreate->OpenJoin();
	TestTrue(TEXT("the room join screen registers in the UI-lock stack"), Lock->IsAnyScreenOpen());
	TestFalse(TEXT("the room join screen suspends gameplay input"), Lock->IsGameplayActive());

	// Esc / right-click must route back to the lobby rather than dropping to gameplay.
	TestTrue(TEXT("the room screen answers a close request"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("backing out of the room screen reopens the lobby"), Lobby->IsOpen());
	TestFalse(TEXT("backing out of the room screen still holds the UI lock"), Lock->IsGameplayActive());
	return true;
}

#endif
