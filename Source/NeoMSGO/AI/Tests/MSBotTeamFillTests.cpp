#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AI/MSBotSpawnPoint.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameSession.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Misc/CommandLine.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotTeamFillTest, "NeoMSGO.AI.BotSpawn.TeamFill25v25",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFilledRoomAdmissionTest, "NeoMSGO.Online.Room.TeamFillAdmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFilledRoomSessionLimitTest, "NeoMSGO.Online.Room.TeamFillSessionLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotTeamFillTests
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TeamFillTestWorld"));
		FWorldScope()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FWorldScope()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSBotTeamFillTest::RunTest(const FString&)
{
	using namespace MSBotTeamFillTests;
	FWorldScope Scope;
	AMSGameMode* Mode = Scope.World->SpawnActor<AMSGameMode>();
	auto* BattleState = Scope.World->SpawnActor<AMSGameState>();
	Scope.World->SetGameState(BattleState);
	Mode->ApplyRules(400, 2500, 10000);
	UMSBotSpawnManagerComponent* Manager = Mode->GetBotSpawnManager();
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Manager);
	Suit->MaxArmor = 3000;
	Manager->SetBotMobileSuitDataOverride(Suit);
	Manager->SetRoomBotConfiguration(0, EMSEnemyTier::Weak, true);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		auto* Point = Scope.World->SpawnActor<AMSBotSpawnPoint>();
		Point->SetSpawnTeam(Team == 0 ? EMSCaptureOwner::TeamPlayer : EMSCaptureOwner::TeamEnemy);
		Point->SetActorLocation(FVector(Team == 0 ? -60000 : 60000, 0, 0));
	}
	auto* Host = Scope.World->SpawnActor<APlayerController>();
	Host->PlayerState = Scope.World->SpawnActor<AMSPlayerState>();
	CastChecked<AMSPlayerState>(Host->PlayerState)->SetSelectedFaction(MSTeam::PlayerTeamId());
	Manager->SpawnInitialBots();
	TestEqual(TEXT("one human reserves a seat: 24 friendly BOTs"), Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 24);
	TestEqual(TEXT("empty enemy team fills to 25 BOTs"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 25);
	const float FriendlyGauge = BattleState->PlayerGauge;
	const float EnemyGauge = BattleState->EnemyGauge;
	auto* Guest = Scope.World->SpawnActor<APlayerController>();
	Guest->PlayerState = Scope.World->SpawnActor<AMSPlayerState>();
	CastChecked<AMSPlayerState>(Guest->PlayerState)->SetSelectedFaction(MSTeam::EnemyTeamId());
	Manager->RefreshTeamPopulation();
	TestEqual(TEXT("joining human replaces only an enemy BOT"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 24);
	TestEqual(TEXT("friendly team remains full"), Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 24);
	Manager->RefreshTeamPopulation();
	TestEqual(TEXT("repeated reconcile does not duplicate BOTs"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 24);
	AMSBotCharacter* DeadBot = nullptr;
	for (const auto& Weak : Manager->GetSpawnedBots())
		if (Weak.IsValid() && Weak->GetGenericTeamId() == MSTeam::EnemyTeamId()) { DeadBot = Weak.Get(); break; }
	if (!TestNotNull(TEXT("enemy BOT exists for respawn-seat test"), DeadBot)) return false;
	DeadBot->GetVitalSet()->SetArmor(0);
	Manager->RefreshTeamPopulation();
	TestEqual(TEXT("dead BOT keeps its seat while waiting for respawn"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 24);
	Guest->Destroy();
	Manager->RefreshTeamPopulation();
	TestEqual(TEXT("departed human is replaced by a BOT"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 25);
	TestEqual(TEXT("replacing a seat does not drain friendly gauge"), BattleState->PlayerGauge, FriendlyGauge);
	TestEqual(TEXT("replacing a seat does not drain enemy gauge"), BattleState->EnemyGauge, EnemyGauge);
	return true;
}

bool FMSFilledRoomAdmissionTest::RunTest(const FString&)
{
	using namespace MSBotTeamFillTests;
	FWorldScope Scope;
	auto* State = Scope.World->SpawnActor<AMSGameState>();
	Scope.World->SetGameState(State);
	State->bPlayerHostedRoom = true;
	State->HostedRoomSettings.bFillTeamsWithBots = true;
	auto* Server = Scope.World->GetSubsystem<UMSOnlineServerSubsystem>();
	TArray<AMSPlayerController*> Players;
	TArray<FMSOnlineIdentity> Identities;
	FMSOnlineDeckSnapshot Deck;
	for (int32 I = 0; I < 50; ++I)
	{
		auto* PC = Scope.World->SpawnActor<AMSPlayerController>();
		FMSOnlineIdentity Identity; Identity.RealmId = TEXT("filled-room-test"); Identity.AccountId = FString::FromInt(I);
		TestTrue(FString::Printf(TEXT("human seat %d replaces a BOT"), I + 1), Server->Admit(PC, Identity, Deck));
		Players.Add(PC); Identities.Add(Identity);
	}
	TestEqual(TEXT("first team admits at most 25 humans"), Server->CountActivePlayers(MSTeam::PlayerTeamId()), 25);
	TestEqual(TEXT("second team admits at most 25 humans"), Server->CountActivePlayers(MSTeam::EnemyTeamId()), 25);
	auto* Extra = Scope.World->SpawnActor<AMSPlayerController>();
	FMSOnlineIdentity ExtraId = Identities[0]; ExtraId.AccountId = TEXT("extra");
	TestFalse(TEXT("51st human cannot overfill"), Server->Admit(Extra, ExtraId, Deck));
	TestTrue(TEXT("same admitted controller retry does not consume a seat"), Server->Admit(Players[0], Identities[0], Deck));
	auto* Retained = Scope.World->SpawnActor<AMSPlayerCharacter>();
	Retained->SetGenericTeamId(MSTeam::PlayerTeamId());
	Retained->GetVitalSet()->SetArmor(123);
	Retained->SetActorHiddenInGame(false);
	Retained->SetActorEnableCollision(true);
	Players[0]->Possess(Retained);
	Server->FindAccount(Identities[0])->bHasDeployed = true;
	Server->Release(Players[0]);
	Players[0]->UnPossess();
	TestTrue(TEXT("disconnected retained pawn is withdrawn beside the replacement BOT"),
		Retained->IsHidden() && !Retained->GetActorEnableCollision()
		&& Retained->GetGenericTeamId() == FGenericTeamId::NoTeam);
	TestEqual(TEXT("released human seat is free before actor destruction"), Server->CountActivePlayers(MSTeam::PlayerTeamId()), 24);
	TestTrue(TEXT("new human takes the vacated team's seat"), Server->Admit(Extra, ExtraId, Deck));
	TestEqual(TEXT("new arrival balances active humans, not historical account count"),
		Server->FindAccount(ExtraId)->TeamId, MSTeam::PlayerTeamId().GetId());
	TestFalse(TEXT("old account cannot overfill its now-full team on reconnect"), Server->Admit(Players[0], Identities[0], Deck));
	Server->Release(Extra);
	TestTrue(TEXT("old account can reconnect once its team has a free seat"), Server->Admit(Players[0], Identities[0], Deck));
	TestTrue(TEXT("retained pawn restores its previous participation on reconnect"),
		!Retained->IsHidden() && Retained->GetActorEnableCollision()
		&& Retained->GetGenericTeamId() == MSTeam::PlayerTeamId());
	TestEqual(TEXT("BOT replacement/reconnect does not heal retained armor"), Retained->GetVitalSet()->GetArmor(), 123.0f);
	return true;
}

bool FMSFilledRoomSessionLimitTest::RunTest(const FString&)
{
	for (const bool bFill : {true, false})
	{
		MSBotTeamFillTests::FWorldScope Scope;
		auto* Mode = Scope.World->SpawnActor<AMSGameMode>();
		FString Error;
		Mode->InitGame(TEXT("/Game/Maps/Lvl_Biome_Daibao"),
			FString::Printf(TEXT("?MapId=Daibao?RoomHost=1?FillTeamsWithBots=%d?BotCount=11"), bFill ? 1 : 0), Error);
		TestTrue(TEXT("room game mode initializes"), Error.IsEmpty());
		if (!TestNotNull(TEXT("native UE game session exists"), Mode->GameSession.Get())) return false;
		TestEqual(TEXT("native connection cap matches replaceable human seats"),
			Mode->GameSession->MaxPlayers, bFill ? 50 : 1);
	}
	return true;
}
#endif
