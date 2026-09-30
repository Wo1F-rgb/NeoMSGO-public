// Copyright NeoMSGO Project. All Rights Reserved.
//
// タスク#209「ダイバオでBOTが攻撃しない」の回帰テスト。
//
// 実測で判明した停止段階は「段階1: 配備」だった。Daibaoの5km Recastグラフは試合開始
// フレームではまだ生成されておらず、ProjectPointToNavigation が全候補で失敗するため
// BOTが0体しか配備されず、そもそも攻撃する主体が存在しなかった(City/GrandBattlefieldは
// 同一セッションで12/12配備される)。加えて巡回(Patrol)の目的地決定も同じNavMesh射影に
// 依存しており、配備できた少数のBOTも目的地を得られず棒立ちになっていた。
//
// ここでは新設した2つのNavMesh非依存フォールバックと、攻撃チェーンの終端(発射)を
// 決定論的に固定する。
//
// 実行方法:
//   -ExecCmds="Automation RunTests NeoMSGO.AI.BotAI.Task209"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AI/MSBotSpawnPoint.h"
#include "AI/MSBotStateTreeNodes.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotPatrolGroundFallbackTest,
	"NeoMSGO.AI.BotAI.Task209.PatrolResolvesDestinationWithoutNavMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotDeploysIntoObstructedZoneTest,
	"NeoMSGO.AI.BotAI.Task209.DeploymentSurvivesObstructedSpawnZone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotFiresAtHostileTest,
	"NeoMSGO.AI.BotAI.Task209.BotFiresAtAcquiredHostile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotEngagementTest
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBotTask209World"));
		AMSGameMode* GameMode = nullptr;

		explicit FScopedWorld(const bool bWithGameMode = false)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			if (bWithGameMode)
			{
				GameMode = World->SpawnActor<AMSGameMode>();
			}
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** 100uuのエンジン標準Cubeを拡大した「地面」。NavMeshは無いがWorldStaticトレースは当たる。 */
	AStaticMeshActor* AddGroundSlab(UWorld* World, const FVector& Center, const FVector& ScaleUU)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Slab = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Center, FRotator::ZeroRotator, Params);
		if (!Slab)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Slab->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetWorldScale3D(ScaleUU / 100.0f);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		return Slab;
	}

	UMSMobileSuitData* MakeBotSuitData()
	{
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(GetTransientPackage());
		Suit->UnitName = FText::FromString(TEXT("Task209TestSuit"));
		Suit->MaxArmor = 3000;
		Suit->MaxDownPoint = 100;
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Suit);
		Suit->EquippedWeapons.Add(TSoftObjectPtr<UMSWeaponDataBase>(Weapon));
		return Suit;
	}

	AMSBotCharacter* SpawnBot(UWorld* World, const FVector& Location, const FGenericTeamId TeamId)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSBotCharacter* Bot = World->SpawnActor<AMSBotCharacter>(
			AMSBotCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Bot)
		{
			Bot->SetGenericTeamId(TeamId);
		}
		return Bot;
	}
}

bool FMSBotPatrolGroundFallbackTest::RunTest(const FString&)
{
	// NavMeshが一切存在しないワールドでも、Patrolは地表トレースで目的地を得られること。
	// これが無いと Data.bHasDestination が永久にfalseで、BOTは配備地点で棒立ちになり
	// 敵との距離が DetectionRange まで縮まらず Chase/Attack へ遷移できない(#209の段階4)。
	MSBotEngagementTest::FScopedWorld Scope;
	const FVector SlabCenter(0.0f, 0.0f, -50.0f);
	AStaticMeshActor* Slab = MSBotEngagementTest::AddGroundSlab(
		Scope.World, SlabCenter, FVector(20000.0f, 20000.0f, 100.0f));
	if (!TestNotNull(TEXT("engine cube ground slab is available"), Slab))
	{
		return false;
	}
	const float SlabTopZ = SlabCenter.Z + 50.0f;

	// WorldContextは実運用と同じくBOT本体(自分自身はトレースから除外される)。
	AMSBotCharacter* Bot = MSBotEngagementTest::SpawnBot(
		Scope.World, FVector(0.0f, 0.0f, 500.0f), MSTeam::EnemyTeamId());
	if (!TestNotNull(TEXT("patrol context bot spawned"), Bot))
	{
		return false;
	}

	// 旧実装(NavMesh射影のみ)は false を返し、目的地が得られない。
	FVector NavOnly = FVector::ZeroVector;
	TestFalse(TEXT("NavMesh projection alone fails without nav data"),
		FMSBotPatrolTask::ProjectPatrolPoint(Bot, FVector(1200.0f, -800.0f, 3000.0f), NavOnly));

	FVector Resolved = FVector::ZeroVector;
	TestTrue(TEXT("patrol destination resolves through the ground-trace fallback"),
		FMSBotPatrolTask::ResolvePatrolPoint(Bot, FVector(1200.0f, -800.0f, 3000.0f), 4000.0f, Resolved));
	TestTrue(TEXT("resolved destination sits on the ground slab"),
		FMath::IsNearlyEqual(Resolved.Z, SlabTopZ, 5.0f));
	TestTrue(TEXT("resolved destination keeps the requested XY"),
		FVector::Dist2D(Resolved, FVector(1200.0f, -800.0f, 0.0f)) < 1.0f);

	// 地形も無い完全な虚空では従来どおり失敗する(無条件に成功させてはいない)。
	FVector Void = FVector::ZeroVector;
	TestFalse(TEXT("no navmesh and no geometry still reports failure"),
		FMSBotPatrolTask::ResolvePatrolPoint(Bot, FVector(500000.0f, 500000.0f, 0.0f), 4000.0f, Void));
	return true;
}

bool FMSBotDeploysIntoObstructedZoneTest::RunTest(const FString&)
{
	// スポーン領域がジオメトリで完全に塞がれていても、BOTは1体残らず配備されること。
	// #209以前は ResolveSpawnLocation が全リトライ失敗で0体配備となり、「BOTが攻撃しない」の
	// 直接原因になっていた。上空DropSpawnHeightからの降下+押し出し着地に委ねる。
	MSBotEngagementTest::FScopedWorld Scope(/*bWithGameMode=*/true);
	if (!TestNotNull(TEXT("GameMode spawned"), Scope.GameMode))
	{
		return false;
	}

	const FVector ZoneCenter(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters PointParams;
	PointParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotSpawnPoint* Point = Scope.World->SpawnActor<AMSBotSpawnPoint>(
		AMSBotSpawnPoint::StaticClass(), ZoneCenter, FRotator::ZeroRotator, PointParams);
	Point->SetSpawnTeam(EMSCaptureOwner::TeamEnemy);

	// 探索半径(1500cm*MSScale)を丸ごと覆う塊。どの候補点でもカプセル検査がブロックされる。
	UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
	if (!TestNotNull(TEXT("GameMode carries a bot spawn manager"), Manager))
	{
		return false;
	}
	const float ZoneExtent = Manager->GetSpawnSearchRadius() * 4.0f;
	if (!TestNotNull(TEXT("obstruction slab is available"),
		MSBotEngagementTest::AddGroundSlab(Scope.World, ZoneCenter,
			FVector(ZoneExtent, ZoneExtent, Manager->GetDropSpawnHeight() * 2.0f))))
	{
		return false;
	}

	UMSMobileSuitData* Suit = MSBotEngagementTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	Manager->SetBotMobileSuitDataOverride(Suit);
	Manager->SetTeamBotCounts(0, 12);

	Scope.GameMode->StartPlay();

	TestEqual(TEXT("every requested bot is deployed even inside blocking geometry"),
		Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 12);
	return true;
}

bool FMSBotFiresAtHostileTest::RunTest(const FString&)
{
	// 攻撃チェーン終端の固定: 索敵 → 敵対判定 → 射撃アビリティ活性化 → 弾消費。
	MSBotEngagementTest::FScopedWorld Scope;
	AMSBotCharacter* Bot = MSBotEngagementTest::SpawnBot(
		Scope.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSBotCharacter* Hostile = MSBotEngagementTest::SpawnBot(
		Scope.World, FVector(1500.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	if (!TestNotNull(TEXT("bot spawned"), Bot) || !TestNotNull(TEXT("hostile spawned"), Hostile))
	{
		return false;
	}

	UMSMobileSuitData* Suit = MSBotEngagementTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	Bot->InitializeUnit(Suit);
	Hostile->InitializeUnit(Suit);

	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	if (!TestNotNull(TEXT("bot is possessed by the AI controller"), Controller))
	{
		return false;
	}

	// 段階2: 索敵
	Controller->Tick(0.6f);
	TestSamePtr(TEXT("stage 2 - the hostile is acquired as the current target"),
		static_cast<AMSUnitBase*>(Controller->GetCurrentTarget()), static_cast<AMSUnitBase*>(Hostile));

	// 段階3/4: 交戦可能距離であること(既定武器射程 100m*MSScale は 1500cm を大きく上回る)
	TestTrue(TEXT("stage 4 - hostile is inside the engage distance"),
		Controller->GetTargetDistance() <= Controller->GetEngageDistance());
	TestTrue(TEXT("stage 5 - attack ability activation is permitted"),
		Controller->CanActivateAttackAbility());

	// 段階5: 実際に射撃アビリティが起動し、弾を消費する
	UMSWeaponComponent* Weapons = Bot->GetWeaponComponent();
	if (!TestNotNull(TEXT("bot carries a weapon component"), Weapons))
	{
		return false;
	}
	const int32 AmmoBefore = Weapons->GetMagazineAmmo(Weapons->GetActiveSlotIndex());
	TestTrue(TEXT("stage 5 - the equipped weapon fire ability activates"),
		Controller->TryActivateCurrentWeaponAbility());
	TestTrue(TEXT("stage 5 - firing consumed ammo or started the fire cooldown"),
		Weapons->GetFireCooldownRemaining() > 0.0f ||
		Weapons->GetMagazineAmmo(Weapons->GetActiveSlotIndex()) < AmmoBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
