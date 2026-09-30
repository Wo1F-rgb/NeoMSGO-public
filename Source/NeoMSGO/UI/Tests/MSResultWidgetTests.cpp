// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AI/MSBotCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerState.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSResultWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSResultRegistryRankingTest, "NeoMSGO.UI.Result.RegistryRanking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSResultDataAndLayoutTest, "NeoMSGO.UI.Result.DataAndLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSResultEmptyParticipantsTest, "NeoMSGO.UI.Result.EmptyParticipants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSResultNextAndEscTest, "NeoMSGO.UI.Result.NextAndEscReturnToLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSResultWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSResultWidgetTestWorld"));
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

	FString CollectText(const UWidget* Widget)
	{
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget)) return Text->GetText().ToString();
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

	template <typename T>
	TStrongObjectPtr<T> MakeWidget(UWorld* World)
	{
		TStrongObjectPtr<T> Widget(NewObject<T>(World));
		Widget->Initialize();
		Widget->InitializeForAutomationTests();
		return Widget;
	}
}

bool FMSResultRegistryRankingTest::RunTest(const FString&)
{
	MSResultWidgetTests::FScopedWorld Scope;
	UMSUnitRegistrySubsystem* Registry = Scope.World->GetSubsystem<UMSUnitRegistrySubsystem>();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* FirstBot = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(), FTransform::Identity, SpawnParameters);
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), FTransform::Identity, SpawnParameters);
	for (int32 Index = 0; Index < 16; ++Index)
		Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(), FTransform::Identity, SpawnParameters);
	const TArray<FString> Names = UMSResultWidget::BuildRankingNames(Registry);
	TestEqual(TEXT("ranking is capped at sixteen registered units"), Names.Num(), 16);
	TestEqual(TEXT("player is moved to the first row regardless of registration order"), Names[0], Player->GetName());
	TestTrue(TEXT("registered BOT names are used"), Names.Contains(FirstBot->GetName()));
	TestTrue(TEXT("missing registry never fabricates a player row"), UMSResultWidget::BuildRankingNames(nullptr).IsEmpty());
	return true;
}

bool FMSResultDataAndLayoutTest::RunTest(const FString&)
{
	// This is specifically the legacy Mode A color-regression assertion.
	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeA, false);
	using namespace MSResultWidgetTests;
	FScopedWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	State->bPlayerWon = true;
	State->PlayerGauge = 2500.0f;
	State->EnemyGauge = 4164.0f;
	State->MaxGauge = 10000.0f;
	State->MatchDuration = 1200.0f;
	State->RemainingTime = 1.0f;
	State->MapDisplayName = FText::FromString(TEXT("アンリアライザコロニー"));
	// GameMode creates the local controller's PlayerState during world setup.
	// Reuse that real participant instead of adding a synthetic third row to the
	// GameState PlayerArray.  This also makes the owner/team perspective below
	// read the same state that the result screen is displaying.
	AMSPlayerState* FirstPlayerState = Scope.PlayerController->GetPlayerState<AMSPlayerState>();
	if (!FirstPlayerState)
	{
		if (APlayerState* UntypedState = Scope.PlayerController->GetPlayerState<APlayerState>())
		{
			State->RemovePlayerState(UntypedState);
			UntypedState->Destroy();
		}
		FirstPlayerState = Scope.World->SpawnActor<AMSPlayerState>();
		Scope.PlayerController->SetPlayerState(FirstPlayerState);
		if (!State->PlayerArray.Contains(FirstPlayerState)) State->AddPlayerState(FirstPlayerState);
	}
	if (!TestNotNull(TEXT("the local controller has a real AMSPlayerState participant"), FirstPlayerState)) return false;
	// Result perspective is derived from the local owner, so model a client
	// controller instead of the headless world-only controller.
	Scope.PlayerController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	Scope.PlayerController->SetPlayerState(FirstPlayerState);
	FirstPlayerState->SetOwner(Scope.PlayerController);
	TestTrue(TEXT("result fixture has a local owning controller"), Scope.PlayerController->IsLocalController());
	FirstPlayerState->SetLoginUsername(TEXT("Pilot-A"));
	FirstPlayerState->SetPlayerName(TEXT("Pilot-A"));
	FirstPlayerState->SetScore(37.0f);
	AMSPlayerState* SecondPlayerState = Scope.World->SpawnActor<AMSPlayerState>();
	SecondPlayerState->SetLoginUsername(TEXT("Pilot-B"));
	SecondPlayerState->SetPlayerName(TEXT("Pilot-B"));
	SecondPlayerState->SetScore(5.0f);
	State->AddPlayerState(SecondPlayerState);
	// A connected server PlayerState can exist before its online identity has
	// completed.  Presence alone must not create a result row or pilot card.
	AMSPlayerState* PendingPlayerState = Scope.World->SpawnActor<AMSPlayerState>();
	PendingPlayerState->SetPlayerName(TEXT("Pending"));
	State->AddPlayerState(PendingPlayerState);
	// The viewer is deliberately second in PlayerArray.  Result must identify
	// the owner from OwningPlayer->PlayerState, never from array position.
	State->PlayerArray.Remove(FirstPlayerState);
	State->PlayerArray.Add(FirstPlayerState);
	TStrongObjectPtr<UMSResultWidget> Result = MakeWidget<UMSResultWidget>(Scope.World);
	Result->SetOwningPlayer(Scope.PlayerController);
	Scope.PlayerController->SetPlayerState(FirstPlayerState);
	Result->RefreshResultDataForAutomationTests();

	TestEqual(TEXT("result map name comes from GameState"),
		CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultMapNameText")))->GetText().ToString(),
		FString(TEXT("大規模戦　アンリアライザコロニー")));
	TestEqual(TEXT("winner text comes from GameState"),
		CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultOutcomeText")))->GetText().ToString(), FString(TEXT("WIN")));
	State->bPlayerWon = false;
	Result->RefreshResultDataForAutomationTests();
	TestEqual(TEXT("loser text comes from GameState"),
		CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultOutcomeText")))->GetText().ToString(), FString(TEXT("LOSE")));
	const FString Gauge = CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultGaugeText")))->GetText().ToString();
	TestTrue(TEXT("player gauge uses two decimals"), Gauge.Contains(TEXT("25.00%")));
	TestTrue(TEXT("enemy gauge uses two decimals"), Gauge.Contains(TEXT("41.64%")));
	TestTrue(TEXT("elapsed battle time uses mm'ss"),
		CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultDurationText")))->GetText().ToString().Contains(TEXT("19'59")));

	UVerticalBox* Rows = CastChecked<UVerticalBox>(Result->GetWidgetFromName(TEXT("ResultRankingRows")));
	TestEqual(TEXT("one row is built for every verified battle participant"), Rows->GetChildrenCount(), 2);
	UBorder* OtherRow = CastChecked<UBorder>(Rows->GetChildAt(0));
	UBorder* PlayerRow = CastChecked<UBorder>(Rows->GetChildAt(1));
	TestNotEqual(TEXT("player row has a distinct highlight"), PlayerRow->GetBrushColor(), OtherRow->GetBrushColor());
	TestEqual(TEXT("ranking row contains exactly seven columns"),
		CastChecked<UHorizontalBox>(PlayerRow->GetContent())->GetChildrenCount(), 7);
	UHorizontalBox* PlayerCells = CastChecked<UHorizontalBox>(PlayerRow->GetContent());
	UHorizontalBox* OtherCells = CastChecked<UHorizontalBox>(OtherRow->GetContent());
	TestEqual(TEXT("owner account display name comes from AMSPlayerState"),
		CastChecked<UTextBlock>(PlayerCells->GetChildAt(1))->GetText().ToString(), FString(TEXT("Pilot-A")));
	TestEqual(TEXT("owner score comes from APlayerState::Score"),
		CastChecked<UTextBlock>(PlayerCells->GetChildAt(2))->GetText().ToString(), FString(TEXT("37")));
	TestEqual(TEXT("other account display name comes from AMSPlayerState"),
		CastChecked<UTextBlock>(OtherCells->GetChildAt(1))->GetText().ToString(), FString(TEXT("Pilot-B")));
	TestEqual(TEXT("other score comes from APlayerState::Score"),
		CastChecked<UTextBlock>(OtherCells->GetChildAt(2))->GetText().ToString(), FString(TEXT("5")));
	for (int32 Column = 0; Column < 7; ++Column)
	{
		if (Column == 1 || Column == 2) continue;
		TestEqual(FString::Printf(TEXT("unavailable result column %d is explicit"), Column),
			CastChecked<UTextBlock>(PlayerCells->GetChildAt(Column))->GetText().ToString(), FString(TEXT("未集計")));
	}
	UVerticalBox* PilotCards = CastChecked<UVerticalBox>(Result->GetWidgetFromName(TEXT("ResultPilotCards")));
	TestEqual(TEXT("pilot cards exclude the pending unverified participant"), PilotCards->GetChildrenCount(), 2);
	const FString PilotCardText = MSResultWidgetTests::CollectText(PilotCards);
	TestTrue(TEXT("pilot cards contain the first account name and score"), PilotCardText.Contains(TEXT("Pilot-A")) && PilotCardText.Contains(TEXT("SCORE 37")));
	TestFalse(TEXT("pilot cards do not duplicate the single viewer outcome"), PilotCardText.Contains(TEXT("WIN")) || PilotCardText.Contains(TEXT("LOSE")));
	TestFalse(TEXT("pilot cards contain no fabricated team label"), PilotCardText.Contains(TEXT("TEAM-A")));

	State->bPlayerWon = true;
	FirstPlayerState->SetSelectedFaction(MSTeam::EnemyTeamId());
	Result->RefreshResultDataForAutomationTests();
	TestEqual(TEXT("enemy faction sees the inverted result"),
		CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultOutcomeText")))->GetText().ToString(), FString(TEXT("LOSE")));
	const FString EnemyGauge = CastChecked<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultGaugeText")))->GetText().ToString();
	TestTrue(TEXT("enemy faction sees its gauge as self gauge"), EnemyGauge.Contains(TEXT("自軍 41.64%")));
	TestTrue(TEXT("enemy faction sees the opposing gauge second"), EnemyGauge.Contains(TEXT("敵軍 25.00%")));
	TestFalse(TEXT("enemy outcome is kept in the single result header"), MSResultWidgetTests::CollectText(PilotCards).Contains(TEXT("WIN")) || MSResultWidgetTests::CollectText(PilotCards).Contains(TEXT("LOSE")));
	TestNotNull(TEXT("Next button exists"), Cast<UButton>(Result->GetWidgetFromName(TEXT("ResultNextButton"))));
	TestEqual(TEXT("result return action is labelled for the lobby"),
		CastChecked<UTextBlock>(CastChecked<UButton>(Result->GetWidgetFromName(TEXT("ResultNextButton")))->GetContent())->GetText().ToString(),
		FString(TEXT("ロビーへ戻る")));
	return true;
}

bool FMSResultEmptyParticipantsTest::RunTest(const FString&)
{
	MSResultWidgetTests::FScopedWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	State->PlayerArray.Reset();
	TStrongObjectPtr<UMSResultWidget> Result = MSResultWidgetTests::MakeWidget<UMSResultWidget>(Scope.World);
	Result->SetOwningPlayer(Scope.PlayerController);
	Result->RefreshResultDataForAutomationTests();

	UVerticalBox* Rows = CastChecked<UVerticalBox>(Result->GetWidgetFromName(TEXT("ResultRankingRows")));
	UVerticalBox* PilotCards = CastChecked<UVerticalBox>(Result->GetWidgetFromName(TEXT("ResultPilotCards")));
	TestEqual(TEXT("empty participant data produces one explanatory ranking row"), Rows->GetChildrenCount(), 1);
	TestTrue(TEXT("empty participant row explains missing data"), MSResultWidgetTests::CollectText(Rows).Contains(TEXT("参加者データなし")));
	TestEqual(TEXT("empty participant data does not create pilot cards"), PilotCards->GetChildrenCount(), 0);
	return true;
}

bool FMSResultNextAndEscTest::RunTest(const FString&)
{
	using namespace MSResultWidgetTests;
	FScopedWorld Scope;
	UMSUILockSubsystem* Lock = Scope.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSResultWidget> Result = MakeWidget<UMSResultWidget>(Scope.World);
	Result->SetLobbyWidget(Lobby.Get());

	Result->Open();
	Result->Open();
	TestTrue(TEXT("result opens and locks gameplay"), Result->IsOpen() && !Lock->IsGameplayActive());
	TestTrue(TEXT("Esc is consumed by Result"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc collapses Result"), Result->IsOpen());
	TestTrue(TEXT("Esc returns to lobby"), Lobby->IsOpen());
	Lobby->Close();

	Result->Open();
	CastChecked<UButton>(Result->GetWidgetFromName(TEXT("ResultNextButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("Next collapses Result"), Result->IsOpen());
	TestTrue(TEXT("Next returns to lobby"), Lobby->IsOpen());
	return true;
}

#endif
