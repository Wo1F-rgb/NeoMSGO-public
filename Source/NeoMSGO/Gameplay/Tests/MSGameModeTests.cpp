// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMapDefinitionRow.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSDestructibleTarget.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/Tests/MSGameModeTestListener.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/MSLobbyWidget.h"
#include "Data/MSMobileSuitData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameModeP73Test, "NeoMSGO.Gameplay.GameMode.P7_3",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMatchTimerP74Test, "NeoMSGO.Gameplay.MatchTimer.P7_4",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameModeMapResolutionTest, "NeoMSGO.Gameplay.GameMode.MapResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMultipleHomeBaseVictoryTest,
	"NeoMSGO.Gameplay.GameMode.MultipleHomeBaseVictory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHostedRoomStartGateTest,
	"NeoMSGO.Gameplay.GameMode.HostedRoomStartGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCostGaugeTest,
	"NeoMSGO.Gameplay.GameMode.CostGauge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSGameModeTestP73
{
	struct FScopedBattleWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSGameModeTestP73World"));
		AMSGameMode* GameMode = nullptr;
		AMSHomeBase* PlayerHomeBase = nullptr;

		FScopedBattleWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			GameMode = World->SpawnActor<AMSGameMode>();
			PlayerHomeBase = World->SpawnActor<AMSHomeBase>(FVector::ZeroVector, FRotator::ZeroRotator);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			GameMode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			GameMode->StartPlay();
		}

		~FScopedBattleWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

namespace MSMatchTimerTestP74
{
	struct FScopedTimerWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMatchTimerTestP74World"));
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		AMSGameMode* GameMode = nullptr;

		explicit FScopedTimerWorld(const bool bHostedRoom = false)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			FURL URL;
			URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			if (bHostedRoom) URL.AddOption(TEXT("RoomHost=1"));
			World->SetGameMode(URL);
			GameMode = World->GetAuthGameMode<AMSGameMode>();
			if (bHostedRoom && GameMode) GameMode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			if (bHostedRoom && GameMode) GameMode->StartPlay();
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.0f);
		}

		void TickFor(float Seconds) const
		{
			while (Seconds > 0.0f)
			{
				const float Step = FMath::Min(Seconds, 0.1f);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
				Seconds -= Step;
			}
		}

		~FScopedTimerWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

namespace MSMultipleHomeBaseTest
{
	struct FScopedBattleWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMultipleHomeBaseWorld"));
		AMSGameMode* GameMode = nullptr;
		TArray<AMSHomeBase*> PlayerBases;

		FScopedBattleWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			GameMode = World->SpawnActor<AMSGameMode>();
			for (int32 Index = 0; Index < 3; ++Index)
			{
				PlayerBases.Add(World->SpawnActor<AMSHomeBase>(
					FVector(Index * 30000.0f, 0.0f, 0.0f), FRotator::ZeroRotator));
			}
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			GameMode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			GameMode->StartPlay();
		}

		~FScopedBattleWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSGameModeP73Test::RunTest(const FString&)
{
	MSGameModeTestP73::FScopedBattleWorld Scope;
	AMSGameMode* GameMode = Scope.GameMode;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	TestNotNull(TEXT("AMSGameMode is authoritative game mode"), GameMode);
	TestNotNull(TEXT("AMSGameState is configured game state"), State);
	if (!GameMode || !State || !Scope.PlayerHomeBase) return false;

	GameMode->ApplyRules(123.0f, 456.0f, 7890.0f);
	TestEqual(TEXT("ApplyRules updates kill damage"), GameMode->GetKillDamage(), 123.0f);
	TestEqual(TEXT("ApplyRules updates home-base damage"), GameMode->GetHomeBaseDestroyDamage(), 456.0f);
	TestEqual(TEXT("ApplyRules updates max gauge"), GameMode->GetConfiguredMaxGauge(), 7890.0f);
	TestEqual(TEXT("Player gauge resets full"), State->PlayerGauge, 7890.0f);
	TestEqual(TEXT("Enemy gauge resets full"), State->EnemyGauge, 7890.0f);
	TestFalse(TEXT("ApplyRules reopens battle"), State->bBattleEnded);

	GameMode->ApplyRules(400.0f, 2500.0f, 10000.0f);
	FActorSpawnParameters UnitSpawnParams;
	UnitSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* PlayerUnit = Scope.World->SpawnActor<AMSBotCharacter>(
		AMSBotCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, UnitSpawnParams);
	AMSBotCharacter* EnemyUnit = Scope.World->SpawnActor<AMSBotCharacter>(
		AMSBotCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, UnitSpawnParams);
	PlayerUnit->SetGenericTeamId(MSTeam::PlayerTeamId());
	EnemyUnit->SetGenericTeamId(MSTeam::EnemyTeamId());
	PlayerUnit->GetVitalSet()->OnDeath.Broadcast(PlayerUnit, nullptr);
	TestEqual(TEXT("Player-team death damages player gauge by 400"), State->PlayerGauge, 9600.0f);
	TestEqual(TEXT("Player-team death leaves enemy gauge unchanged"), State->EnemyGauge, 10000.0f);
	TestFalse(TEXT("One ordinary player-team death does not end battle"), State->bBattleEnded);
	EnemyUnit->GetVitalSet()->OnDeath.Broadcast(EnemyUnit, nullptr);
	TestEqual(TEXT("Enemy-team death damages enemy gauge by 400"), State->EnemyGauge, 9600.0f);

	GameMode->ApplyRules(400.0f, 2500.0f, 10000.0f);
	Scope.PlayerHomeBase->OnHomeBaseDestroyed.Broadcast(Scope.PlayerHomeBase, EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Player home-base destruction damages player gauge by 2500"), State->PlayerGauge, 7500.0f);
	TestFalse(TEXT("Synthetic non-destroyed home-base notification does not trigger annihilation"), State->bBattleEnded);

	UMSGameModeTestListener* Listener = NewObject<UMSGameModeTestListener>();
	State->OnBattleEnded.AddDynamic(Listener, &UMSGameModeTestListener::HandleBattleEnded);
	GameMode->ApplyRules(400.0f, 2500.0f, 400.0f);
	PlayerUnit->GetVitalSet()->OnDeath.Broadcast(PlayerUnit, nullptr);
	TestTrue(TEXT("Player gauge zero ends battle"), State->bBattleEnded);
	TestFalse(TEXT("Player gauge zero is a defeat"), State->bPlayerWon);
	TestEqual(TEXT("Battle-ended delegate fires once"), Listener->BroadcastCount, 1);

	const float EndedPlayerGauge = State->PlayerGauge;
	const float EndedEnemyGauge = State->EnemyGauge;
	EnemyUnit->GetVitalSet()->OnDeath.Broadcast(EnemyUnit, nullptr);
	Scope.PlayerHomeBase->OnHomeBaseDestroyed.Broadcast(Scope.PlayerHomeBase, EMSCaptureOwner::TeamPlayer);
	GameMode->NotifyMatchTimeExpired();
	GameMode->EndBattle(true);
	TestEqual(TEXT("Post-battle events cannot change player gauge"), State->PlayerGauge, EndedPlayerGauge);
	TestEqual(TEXT("Post-battle events cannot change enemy gauge"), State->EnemyGauge, EndedEnemyGauge);
	TestEqual(TEXT("Post-battle events cannot rebroadcast battle end"), Listener->BroadcastCount, 1);
	TestFalse(TEXT("Post-battle EndBattle cannot overwrite result"), State->bPlayerWon);

	GameMode->ApplyRules(400.0f, 2500.0f, 10000.0f);
	GameMode->ApplyGaugeDamage(MSTeam::PlayerTeamId(), 1000.0f);
	GameMode->ApplyGaugeDamage(MSTeam::EnemyTeamId(), 1000.0f);
	GameMode->NotifyMatchTimeExpired();
	TestTrue(TEXT("Time-expired tie is a player victory"), State->bPlayerWon);
	TestEqual(TEXT("Second battle end broadcasts once"), Listener->BroadcastCount, 2);
	TestTrue(TEXT("Listener receives winning result"), Listener->bLastPlayerWon);
	return true;
}

bool FMSMatchTimerP74Test::RunTest(const FString&)
{
	MSMatchTimerTestP74::FScopedTimerWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	TestNotNull(TEXT("Timer uses AMSGameState"), State);
	if (!State || !Scope.GameMode) return false;

	State->SetDuration(600.0f);
	TestEqual(TEXT("SetDuration updates remaining time"), State->RemainingTime, 600.0f);
	Scope.TickFor(1.1f);
	TestEqual(TEXT("Timer decrements after one second"), State->RemainingTime, 599.0f);

	State->SetDuration(754.0f);
	TestEqual(TEXT("754 seconds formats as 12 minutes 34 seconds"),
		State->GetFormattedRemainingTime().ToString(), FString(TEXT("12'34")));

	UMSGameModeTestListener* Listener = NewObject<UMSGameModeTestListener>();
	State->OnBattleEnded.AddDynamic(Listener, &UMSGameModeTestListener::HandleBattleEnded);
	State->SetDuration(2.0f);
	Scope.TickFor(2.2f);
	TestEqual(TEXT("Expired timer clamps at zero"), State->RemainingTime, 0.0f);
	TestEqual(TEXT("Zero formats as 00 minutes 00 seconds"),
		State->GetFormattedRemainingTime().ToString(), FString(TEXT("00'00")));
	TestEqual(TEXT("Expiry ends battle exactly once"), Listener->BroadcastCount, 1);
	Scope.TickFor(2.0f);
	TestEqual(TEXT("Stopped expired timer does not notify again"), Listener->BroadcastCount, 1);
	TestEqual(TEXT("Expired timer never becomes negative"), State->RemainingTime, 0.0f);

	Scope.GameMode->ApplyRules(400.0f, 2500.0f, 10000.0f);
	State->SetDuration(10.0f);
	Scope.GameMode->EndBattle(true);
	const float TimeAtBattleEnd = State->RemainingTime;
	Scope.TickFor(2.0f);
	TestEqual(TEXT("Battle end stops remaining-time changes"), State->RemainingTime, TimeAtBattleEnd);
	return true;
}

bool FMSHostedRoomStartGateTest::RunTest(const FString&)
{
	MSMatchTimerTestP74::FScopedTimerWorld Scope(/*bHostedRoom=*/true);
	AMSGameMode* Mode = Scope.GameMode;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("hosted room has GameMode"), Mode) ||
		!TestNotNull(TEXT("hosted room has GameState"), State)) return false;
	TestTrue(TEXT("room waits for first successful human sortie"), Mode->IsWaitingForRoomDeployment());
	State->SetDuration(120.0f);
	Scope.TickFor(2.2f);
	TestEqual(TEXT("briefing does not consume match time"), State->RemainingTime, 120.0f);
	Mode->ApplyGaugeDamage(MSTeam::PlayerTeamId(), 400.0f);
	Mode->NotifyMatchTimeExpired();
	TestEqual(TEXT("briefing damage cannot deplete the gauge"), State->PlayerGauge, State->MaxGauge);
	TestFalse(TEXT("briefing cannot finish the match"), State->bBattleEnded);

	Mode->NotifyRoomDeployment();
	TestFalse(TEXT("first sortie opens the room battle"), Mode->IsWaitingForRoomDeployment());
	Scope.TickFor(1.2f);
	TestEqual(TEXT("battle clock advances after first sortie"), State->RemainingTime, 119.0f);
	Mode->ApplyGaugeDamage(MSTeam::PlayerTeamId(), 400.0f);
	TestEqual(TEXT("battle damage applies once the room starts"), State->PlayerGauge, 9600.0f);
	const float TimeBeforeDuplicate = State->RemainingTime;
	Mode->NotifyRoomDeployment();
	TestEqual(TEXT("guest/redeployment does not refill either gauge"), State->PlayerGauge, 9600.0f);
	TestEqual(TEXT("duplicate start does not reset the clock"), State->RemainingTime, TimeBeforeDuplicate);
	Scope.TickFor(1.1f);
	TestTrue(TEXT("duplicate start leaves the running clock active"), State->RemainingTime < TimeBeforeDuplicate);
	return true;
}

bool FMSGameModeMapResolutionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>());
	Instance->SelectedMapId = TEXT("UnrealizerColony");

	const FMSMapDefinitionRow* Definition = AMSGameMode::ResolveMapDefinition(
		TEXT("/Game/Maps/Lvl_GrandBattlefield"), TEXT("MapId=NewYark"), Instance.Get());
	TestNotNull(TEXT("valid MapId option resolves a definition"), Definition);
	if (Definition) TestEqual(TEXT("MapId option has highest priority"), Definition->MapId, FName(TEXT("NewYark")));

	// World名解決はOptionsに次ぐ第2優先(ロビー経由のOpenLevelは常に?MapIdを付与するため、
	// ここに到達するのは直接PIE/エディタ起動時のみ。実際にロードされたレベルの定義を
	// GameInstanceの既定選択より優先しないと、直接起動時に常にUnrealizerColonyへ
	// 誤解決してしまう)。
	Definition = AMSGameMode::ResolveMapDefinition(
		TEXT("/Game/Maps/Lvl_Biome_City"), TEXT(""), Instance.Get());
	TestNotNull(TEXT("missing option falls back to the loaded world"), Definition);
	if (Definition) TestEqual(TEXT("world name has second priority"), Definition->MapId, FName(TEXT("NewYark")));

	Definition = AMSGameMode::ResolveMapDefinition(
		TEXT("/Game/Maps/Lvl_Biome_City"), TEXT("MapId=NotARealMap"), Instance.Get());
	TestNotNull(TEXT("invalid option falls back to the loaded world"), Definition);
	if (Definition) TestEqual(TEXT("invalid option does not mask world resolution"), Definition->MapId, FName(TEXT("NewYark")));

	Instance->SelectedMapId = TEXT("NewYark");
	Definition = AMSGameMode::ResolveMapDefinition(
		TEXT("/Game/Maps/UnknownTestLevel"), TEXT("MapId=AlsoInvalid"), Instance.Get());
	TestNotNull(TEXT("invalid option and unresolvable world fall back to GameInstance"), Definition);
	if (Definition) TestEqual(TEXT("GameInstance resolves last"), Definition->MapId, FName(TEXT("NewYark")));

	Instance->SelectedMapId = TEXT("NotARealMap");
	Definition = AMSGameMode::ResolveMapDefinition(TEXT("UnknownLevel"), TEXT("MapId=Invalid"), Instance.Get());
	TestNull(TEXT("unresolvable inputs preserve the GameState default"), Definition);
	return true;
}

bool FMSMultipleHomeBaseVictoryTest::RunTest(const FString&)
{
	MSMultipleHomeBaseTest::FScopedBattleWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	TestNotNull(TEXT("Multiple-base battle uses AMSGameState"), State);
	TestEqual(TEXT("Three player home bases are registered"), Scope.PlayerBases.Num(), 3);
	if (!State || Scope.PlayerBases.Contains(nullptr)) return false;

	for (int32 Index = 0; Index < Scope.PlayerBases.Num(); ++Index)
	{
		AMSDestructibleTarget* Core = Scope.PlayerBases[Index]->GetCoreTarget();
		TestNotNull(FString::Printf(TEXT("Player base %d has a core"), Index), Core);
		if (!Core) return false;
		Core->GetVitalSet()->OnDeath.Broadcast(Core, nullptr);
		if (Index < Scope.PlayerBases.Num() - 1)
		{
			TestFalse(FString::Printf(TEXT("Destroying %d of 3 bases does not end battle"), Index + 1),
				State->bBattleEnded);
		}
	}

	TestTrue(TEXT("Destroying all three player bases ends battle"), State->bBattleEnded);
	TestFalse(TEXT("Losing all player bases is a defeat"), State->bPlayerWon);
	return true;
}

bool FMSCostGaugeTest::RunTest(const FString&)
{
	MSMatchTimerTestP74::FScopedTimerWorld Scope(true);
	AMSGameMode* Mode = Scope.GameMode;
	UMSGameInstance* Instance = NewObject<UMSGameInstance>();
	Instance->bPendingSortie = true;
	Instance->PendingRuleIndex = 0;
	FText RuleName;
	TestTrue(TEXT("Real pending-sortie rule path applies"), UMSLobbyWidget::ConsumePendingBattleRule(
		Instance, Mode, Scope.World->GetGameState<AMSGameState>(), RuleName));
	TestTrue(TEXT("COST 200 is about 0.33 percent of full gauge"),
		FMath::IsNearlyEqual(Mode->GetDeathGaugeDamage(200) / Mode->GetConfiguredMaxGauge(), 1.f / 300.f));
	TestTrue(TEXT("COST 400 loses twice the COST 200 gauge"),
		FMath::IsNearlyEqual(Mode->GetDeathGaugeDamage(400), Mode->GetDeathGaugeDamage(200) * 2.f));
	TestTrue(TEXT("A single team wipe no longer exhausts the entire gauge"),
		Mode->GetDeathGaugeDamage(400) * 25.f < Mode->GetConfiguredMaxGauge());
	Mode->NotifyRoomDeployment();
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	float PreviousLoss = 0.f;
	for (const int32 Cost : {200, 400})
	{
		AMSBotCharacter* Unit = Scope.World->SpawnActor<AMSBotCharacter>(
			AMSBotCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Unit);
		Suit->Cost = Cost;
		Unit->InitializeUnit(Suit);
		Unit->SetGenericTeamId(MSTeam::EnemyTeamId());
		const float Before = State->EnemyGauge;
		Unit->GetVitalSet()->OnDeath.Broadcast(Unit, nullptr);
		const float Loss = Before - State->EnemyGauge;
		TestTrue(TEXT("Actual unit death charges its COST amount"),
			FMath::IsNearlyEqual(Loss, Mode->GetDeathGaugeDamage(Cost), .01f));
		if (Cost == 400) TestTrue(TEXT("Actual COST 400 death loses twice COST 200"),
			FMath::IsNearlyEqual(Loss, PreviousLoss * 2.f, .01f));
		PreviousLoss = Loss;
	}
	Mode->ApplyRules(500.f, 0.f, 10000.f);
	TestEqual(TEXT("Custom deathmatch retains its flat kill rule"), Mode->GetDeathGaugeDamage(400), 500.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
