// Copyright NeoMSGO Project. All Rights Reserved.
//
// P6-1 検証: BOT AI(階級別Engage/Break式、状態遷移閾値、巡回、ストレイフ、
//            ユニットレジストリ、よろけ/ダウン/死亡ゲート)のテスト。
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.AI.BotAI.P6_1"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.AI.BotAI.P6_1"
//
// 注意: #include "Misc/AutomationTest.h" は #if WITH_DEV_AUTOMATION_TESTS より
// 前に置くこと。マクロはヘッダ(Misc/Build.h)側で定義されるため、includeより
// 先に #if を書くと未定義=0と評価されファイル全体が空になり、テストが
// 登録されない(ビルドは成功するため気づきにくい)。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "AI/MSBotStateTreeNodes.h"
#include "AI/GA_BotKick.h"
#include "Animation/MSAnimInstance.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"
#include "Data/MSWeaponData.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIP61Test, "NeoMSGO.AI.BotAI.P6_1",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotMovementRecoveryCollisionTest,
	"NeoMSGO.AI.BotAI.MovementRecovery.CapsuleClearGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotTaiganNavigationTest,
	"NeoMSGO.AI.BotAI.Taigan.NavigationGoals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotAITestP61
{
	struct FScopedBotWorld
	{
		UWorld* World = nullptr;
		explicit FScopedBotWorld(const TCHAR* WorldName = TEXT("MSBotAITestP61World"))
		{
			static int32 WorldSerial = 0;
			const FString PackageName = FString::Printf(TEXT("/Temp/%s_%d"),
				WorldName, ++WorldSerial);
			UPackage* WorldPackage = CreatePackage(*PackageName);
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(WorldName), WorldPackage);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedBotWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	AMSBotCharacter* SpawnBotIgnoringFixtureOverlap(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSBotCharacter>(
			AMSBotCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

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
}

bool FMSBotTaiganNavigationTest::RunTest(const FString&)
{
	TestTrue(TEXT("PIE-prefixed Taigan is opted in"),
		AMSBotAIController::IsTaiganNavigationMap(TEXT("UEDPIE_0_Lvl_NewYork_Taigan")));
	TestFalse(TEXT("older City remains outside Taigan routing"),
		AMSBotAIController::IsTaiganNavigationMap(TEXT("Lvl_Biome_City")));
	TestTrue(TEXT("subway capsule centre is underground"),
		AMSBotAIController::IsTaiganSubwayLocation(FVector(0, 0, -5570)));
	TestFalse(TEXT("high above-ground rooftop is not subway"),
		AMSBotAIController::IsTaiganSubwayLocation(FVector(0, 0, 6580)));
	TestTrue(TEXT("similar moving goal retains the current path"),
		AMSBotAIController::ShouldReuseTaiganMove(FVector(0, 0, -5500), FVector(200, 0, -5500),
			500.0f, 500.0f, false, false, true));
	TestFalse(TEXT("a changed floor never reuses a different nav layer"),
		AMSBotAIController::ShouldReuseTaiganMove(FVector(0, 0, -5500), FVector(0, 0, 0),
			500.0f, 500.0f, false, false, true));
	TestFalse(TEXT("a stopped path is requested again"),
		AMSBotAIController::ShouldReuseTaiganMove(FVector::ZeroVector, FVector::ZeroVector,
			500.0f, 500.0f, false, false, false));
	TestFalse(TEXT("acceptance/overlap policy changes force a new request"),
		AMSBotAIController::ShouldReuseTaiganMove(FVector::ZeroVector, FVector::ZeroVector,
			500.0f, 100.0f, false, true, true));
	TestFalse(TEXT("Taigan micro-jitter is not progress despite high instantaneous speed"),
		AMSBotAIController::HasMovementProgress(3.0f, 3900.0f, true));
	TestTrue(TEXT("actual displacement is progress"),
		AMSBotAIController::HasMovementProgress(26.0f, 0.0f, true));
	TestTrue(TEXT("other maps retain velocity-based progress"),
		AMSBotAIController::HasMovementProgress(3.0f, 3900.0f, false));
	const FVector EntranceWaypoint(38590, -77090, -6390);
	const FVector SurfaceHostile(5000, 500, 820);
	TestTrue(TEXT("the same surface target does not abort an underground entrance escape"),
		AMSBotAIController::ShouldKeepRecoveryForGoal(EntranceWaypoint, SurfaceHostile,
			SurfaceHostile + FVector(300, 0, 0), true));
	TestFalse(TEXT("a new distant target interrupts the entrance escape"),
		AMSBotAIController::ShouldKeepRecoveryForGoal(EntranceWaypoint, SurfaceHostile,
			FVector(-50000, 500, 820), true));
	TestTrue(TEXT("ordinary recovery still compares its actual movement goal"),
		AMSBotAIController::ShouldKeepRecoveryForGoal(EntranceWaypoint, SurfaceHostile,
			EntranceWaypoint, false));
	TestFalse(TEXT("ordinary recovery is not held by a different strategic goal"),
		AMSBotAIController::ShouldKeepRecoveryForGoal(EntranceWaypoint, SurfaceHostile,
			SurfaceHostile, false));
	TestTrue(TEXT("Taigan RVO sees neighbours well before capsules touch"),
		AMSBotAIController::CalculateTaiganRVOConsiderationRadius(317.0f, 5596.0f, 260.0f)
			>= 2500.0f);
	TestEqual(TEXT("Taigan RVO still honours a larger authored radius"),
		AMSBotAIController::CalculateTaiganRVOConsiderationRadius(317.0f, 1000.0f, 3000.0f),
		3000.0f);

	MSBotAITestP61::FScopedBotWorld Scope(TEXT("Lvl_NewYork_Taigan"));
	auto AddMarker = [World = Scope.World](const FVector& Location, const TCHAR* PairTag,
		const bool bBottom)
	{
		ATargetPoint* Marker = World->SpawnActor<ATargetPoint>(Location, FRotator::ZeroRotator);
		Marker->Tags.Add(FName(TEXT("NYT_NavEntrance")));
		Marker->Tags.Add(FName(PairTag));
		Marker->Tags.Add(bBottom ? FName(TEXT("NYT_NavBottom")) : FName(TEXT("NYT_NavTop")));
		return Marker;
	};
	const FVector Bottom1(10000, 0, -6390), Top1(-6000, 0, 10);
	AddMarker(Bottom1, TEXT("NYT_NavEntry_01"), true);
	AddMarker(Top1, TEXT("NYT_NavEntry_01"), false);
	AddMarker(FVector(100000, 0, -6390), TEXT("NYT_NavEntry_02"), true);
	AddMarker(FVector(84000, 0, 10), TEXT("NYT_NavEntry_02"), false);
	AddMarker(FVector(500, 0, -6390), TEXT("NYT_NavEntry_03"), true); // incomplete pair
	const FVector Underground(0, 0, -5500), SurfaceGoal(0, 0, 800);
	const TArray<FVector> Approach = AMSBotAIController::CollectTaiganEntranceWaypoints(
		Scope.World, Underground, SurfaceGoal);
	TestEqual(TEXT("only complete entrance pairs are offered"), Approach.Num(), 2);
	if (Approach.Num() >= 2)
	{
		TestEqual(TEXT("nearest reachable layer entry is first"), Approach[0], Bottom1);
		TestEqual(TEXT("second complete entrance remains available"), Approach[1], FVector(100000, 0, -6390));
	}
	const TArray<FVector> Climb = AMSBotAIController::CollectTaiganEntranceWaypoints(
		Scope.World, FVector(10010, 0, -5570), SurfaceGoal);
	if (TestTrue(TEXT("ramp top follows a reached bottom"), Climb.Num() > 0))
		TestEqual(TEXT("next waypoint is the matching top"), Climb[0], Top1);
	const TArray<FVector> Descend = AMSBotAIController::CollectTaiganEntranceWaypoints(
		Scope.World, FVector(-5990, 0, 820), FVector(0, 0, -5570));
	if (TestTrue(TEXT("surface-to-underground route exists"), Descend.Num() > 0))
		TestEqual(TEXT("reverse traversal uses matching bottom"), Descend[0], Bottom1);
	AMSBotCharacter* Bot = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector::ZeroVector);
	if (TestNotNull(TEXT("Taigan bot fixture spawns"), Bot))
	{
		const UCharacterMovementComponent* Movement = Bot->GetCharacterMovement();
		const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
		if (TestNotNull(TEXT("Taigan bot has movement"), Movement)
			&& TestNotNull(TEXT("Taigan bot has capsule"), Capsule))
		{
			TestTrue(TEXT("Taigan RVO remains enabled"), Movement->bUseRVOAvoidance);
			TestTrue(TEXT("runtime avoidance range exceeds collision diameter"),
				Movement->AvoidanceConsiderationRadius > Capsule->GetScaledCapsuleRadius() * 2.0f);
		}
	}
	return true;
}

bool FMSBotAIP61Test::RunTest(const FString&)
{
	const FMSBotAIConfig Config;

	// --- Unity準拠の既定値(docs/UE5_BotAI.md 数値表) ---
	TestEqual(TEXT("Config: Detection follows MSScale"), Config.DetectionRange, 2500.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: LoseSight follows MSScale"), Config.LoseSightRange, 3500.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: PatrolRadius 1000cm"), Config.PatrolRadius, 1000.0f);
	TestEqual(TEXT("Config: PatrolAcceptance follows MSScale"), Config.PatrolAcceptanceRadius, 50.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: MeleeEngage follows MSScale"), Config.MeleeEngageDistance, 350.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: BazookaPreferred follows MSScale"), Config.BazookaPreferredDistance, 1500.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: StrongEngageBonus follows MSScale"), Config.StrongEngageDistanceBonus, 500.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: StrafeFactor 0.65"), Config.StrafeDistanceFactor, 0.65f);
	TestEqual(TEXT("Config: StrafeReverseMin 1.6s"), Config.StrafeReverseMinInterval, 1.6f);
	TestEqual(TEXT("Config: StrafeReverseMax 3.2s"), Config.StrafeReverseMaxInterval, 3.2f);
	TestEqual(TEXT("Config: AttackRotationSpeed 5.0"), Config.AttackRotationInterpSpeed, 5.0f);
	TestEqual(TEXT("Config: TargetRefresh 0.5s"), Config.TargetRefreshInterval, 0.5f);
	TestEqual(TEXT("Config: SeparationRadius 320cm"), Config.SeparationRadius, 320.0f);
	TestEqual(TEXT("Config: SeparationMaxOffset 220cm"), Config.SeparationMaxOffset, 220.0f);
	TestEqual(TEXT("Config: RVOAvoidanceRadius 260cm"), Config.RVOAvoidanceRadius, 260.0f);

	// --- 階級別 Engage/Break 式 ---
	TestEqual(TEXT("Weak engage"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Weak, 2400.0f, Config), 2400.0f);
	TestEqual(TEXT("Weak break"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Weak, 2400.0f, Config), 2400.0f);
	TestEqual(TEXT("Medium engage"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Medium, 2400.0f, Config), Config.MeleeEngageDistance);
	TestEqual(TEXT("Medium break"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Medium, 2400.0f, Config), Config.MeleeEngageDistance * 1.5f);
	TestEqual(TEXT("Strong engage capped"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Strong, 24000.0f, Config), Config.BazookaPreferredDistance + Config.StrongEngageDistanceBonus);
	TestEqual(TEXT("Strong engage short weapon"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Strong, 1800.0f, Config), 1800.0f);
	TestEqual(TEXT("Strong break"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Strong, 2400.0f, Config), 2400.0f);

	// --- 巡回: スポーン中心・半径1000cm ---
	FRandomStream Random(6101);
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const FVector Point = FMSBotPatrolTask::MakeRandomPointInCircle(FVector(10, 20, 30), Config.PatrolRadius, Random);
		TestTrue(TEXT("Patrol point remains in radius"), FVector::Dist2D(Point, FVector(10, 20, 30)) <= Config.PatrolRadius + UE_KINDA_SMALL_NUMBER);
	}
	FVector Projected;
	TestFalse(TEXT("Navigation projection safely rejects null world"), FMSBotPatrolTask::ProjectPatrolPoint(nullptr, FVector::ZeroVector, Projected));
	TestEqual(TEXT("Navigation retry starts at 0.125s"), FMSBotPatrolTask::CalculateNavigationRetryDelay(1), 0.125f);
	TestEqual(TEXT("Navigation retry backs off exponentially"), FMSBotPatrolTask::CalculateNavigationRetryDelay(4), 1.0f);
	TestEqual(TEXT("Navigation retry is capped at 2s"), FMSBotPatrolTask::CalculateNavigationRetryDelay(300), 2.0f);
	TestTrue(TEXT("Daibao bypasses synchronous full-map Recast searches"),
		AMSBotAIController::ShouldUseDirectGroundSteering(TEXT("UEDPIE_0_Lvl_Biome_Daibao")));
	TestFalse(TEXT("City keeps normal NavMesh pathfinding"),
		AMSBotAIController::ShouldUseDirectGroundSteering(TEXT("Lvl_Biome_City")));
	TestFalse(TEXT("Grand Battlefield keeps normal NavMesh pathfinding"),
		AMSBotAIController::ShouldUseDirectGroundSteering(TEXT("Lvl_GrandBattlefield")));
	const FVector StrategicPatrolGoal = FMSBotPatrolTask::CalculatePatrolAdvanceCandidate(
		FVector::ZeroVector, FVector(500000.0f, 0.0f, 100.0f), Config.PatrolRadius,
		200.0f, true, false);
	TestTrue(TEXT("Ground NavMesh patrol gives A* the distant hostile X"),
		FMath::IsNearlyEqual(StrategicPatrolGoal.X, 500000.0));
	TestTrue(TEXT("Ground NavMesh patrol preserves formation offset"),
		FMath::IsNearlyEqual(StrategicPatrolGoal.Y, 200.0));
	const FVector DirectPatrolGoal = FMSBotPatrolTask::CalculatePatrolAdvanceCandidate(
		FVector::ZeroVector, FVector(500000.0f, 0.0f, 100.0f), Config.PatrolRadius,
		200.0f, false, false);
	TestTrue(TEXT("Direct steering patrol remains locally bounded"),
		FMath::IsNearlyEqual(DirectPatrolGoal.X, static_cast<double>(Config.PatrolRadius)));
	TestTrue(TEXT("Direct steering patrol keeps formation offset"),
		FMath::IsNearlyEqual(DirectPatrolGoal.Y, 200.0));
	const FVector LocalEscape = AMSBotAIController::CalculateLocalEscapeCandidate(
		FVector::ZeroVector, FVector(1000.0f, 0.0f, 0.0f), 600.0f, 1);
	TestTrue(TEXT("Stall escape stays local and lateral to the goal"),
		FMath::IsNearlyEqual(LocalEscape.X, 0.0f) && FMath::IsNearlyEqual(LocalEscape.Y, 600.0f));
	TestTrue(TEXT("Stall recovery requires an active grounded request with no progress"),
		AMSBotAIController::ShouldRecoverFromStall(2000.0f, 50.0f, 1.5f, 10.0f,
			true, false, true));
	TestFalse(TEXT("Stall recovery ignores an action-blocked bot"),
		AMSBotAIController::ShouldRecoverFromStall(2000.0f, 50.0f, 1.5f, 10.0f,
			true, true, true));
	TestFalse(TEXT("Stall recovery ignores airborne movement"),
		AMSBotAIController::ShouldRecoverFromStall(2000.0f, 50.0f, 1.5f, 10.0f,
			true, false, false));
	TestFalse(TEXT("Stall recovery ignores a bot that already displaced"),
		AMSBotAIController::ShouldRecoverFromStall(2000.0f, 50.0f, 1.5f, 25.0f,
			true, false, true));

	// --- 円周ストレイフ: Engage×0.65 の円周上に目的地、方向反転で左右が入れ替わる ---
	const FVector StrafeTarget(0, 0, 100);
	const FVector StrafeBot(1000, 0, 100);
	const float StrafeRadius = 2000.0f * Config.StrafeDistanceFactor;
	const FVector DestRight = FMSBotAttackTask::CalculateStrafeDestination(StrafeBot, StrafeTarget, StrafeRadius, 1);
	const FVector DestLeft = FMSBotAttackTask::CalculateStrafeDestination(StrafeBot, StrafeTarget, StrafeRadius, -1);
	TestTrue(TEXT("Strafe keeps engage*0.65 radius (dir +1)"), FMath::IsNearlyEqual(FVector::Dist2D(DestRight, StrafeTarget), StrafeRadius, 0.1f));
	TestTrue(TEXT("Strafe keeps engage*0.65 radius (dir -1)"), FMath::IsNearlyEqual(FVector::Dist2D(DestLeft, StrafeTarget), StrafeRadius, 0.1f));
	TestTrue(TEXT("Strafe direction flips tangent side"), DestRight.Y * DestLeft.Y < 0.0f);
	TestTrue(TEXT("Strafe destination advances tangentially"), !FMath::IsNearlyZero(DestRight.Y, 1.0f));
	const FVector AttackVelocity = FMSBotAttackTask::CalculateAttackMoveVelocity(StrafeBot, DestRight, 600.0f);
	TestTrue(TEXT("Ranged attack steering has non-zero movement speed while firing"), AttackVelocity.Size2D() > 0.0f);
	TestTrue(TEXT("Ranged attack steering preserves exact configured speed"), FMath::IsNearlyEqual(AttackVelocity.Size2D(), 600.0f, 0.1f));

	const FVector Separation = AMSBotAIController::CalculateSeparationOffset(
		FVector::ZeroVector, {FVector(80.0f, 0.0f, 0.0f), FVector(1000.0f, 0.0f, 0.0f)},
		Config.SeparationRadius, Config.SeparationMaxOffset);
	TestTrue(TEXT("Nearby ally produces separation away from it"), Separation.X < 0.0f);
	const float ExpectedSeparation = Config.SeparationMaxOffset * (1.0f - 80.0f / Config.SeparationRadius);
	TestTrue(TEXT("Separation uses the actual distance-weighted offset"),
		FMath::IsNearlyEqual(Separation.Size2D(), ExpectedSeparation, 0.1f));
	const FVector CoincidentSeparation = AMSBotAIController::CalculateSeparationOffset(
		FVector::ZeroVector, {FVector::ZeroVector}, Config.SeparationRadius,
		Config.SeparationMaxOffset, FVector::RightVector);
	TestTrue(TEXT("Exactly stacked BOTs still receive a non-zero separation force"),
		CoincidentSeparation.Size2D() > 0.0f);

	// --- StateTree遷移閾値(境界値含む) ---
	TestTrue(TEXT("Patrol to Chase inclusive"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::PatrolToChase, Config.DetectionRange, 500, 500, Config));
	TestFalse(TEXT("Patrol to Chase beyond"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::PatrolToChase, Config.DetectionRange + 1.0f, 500, 500, Config));
	TestTrue(TEXT("Chase to Attack inclusive"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::ChaseToAttack, 500, 500, 500, Config));
	TestTrue(TEXT("Attack to Chase strict"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::AttackToChase, 501, 500, 500, Config));
	TestFalse(TEXT("Attack to Chase at break"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::AttackToChase, 500, 500, 500, Config));
	TestTrue(TEXT("Chase to Patrol strict"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::ChaseToPatrol, Config.LoseSightRange + 1.0f, 500, 500, Config));
	TestFalse(TEXT("Chase to Patrol at loss range"), AMSBotAIController::TestDistanceRule(EMSBotDistanceRule::ChaseToPatrol, Config.LoseSightRange, 500, 500, Config));

	// --- レジストリ登録/最近傍敵対選定(UMSTeamStatics経由・全走査なし) ---
	MSBotAITestP61::FScopedBotWorld Scope;
	AMSBotCharacter* Bot = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector::ZeroVector);
	AMSBotCharacter* NearEnemy = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(100,0,0));
	AMSBotCharacter* FarEnemy = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(200,0,0));
	TestTrue(TEXT("Bot default team is enemy"), Bot->GetGenericTeamId() == MSTeam::EnemyTeamId());
	TestEqual(TEXT("Patrol center recorded at BeginPlay"), Bot->GetPatrolCenter(), Bot->GetActorLocation());
	Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	UMSUnitRegistrySubsystem* Registry = Scope.World->GetSubsystem<UMSUnitRegistrySubsystem>();
	TestTrue(TEXT("Bot auto registered"), Registry && Registry->IsRegistered(Bot));
	TestSamePtr(TEXT("Nearest hostile uses registry/team policy"), static_cast<AMSUnitBase*>(Registry->FindNearestHostileUnit(Bot, 1000.0f)), static_cast<AMSUnitBase*>(NearEnemy));
	Registry->UnregisterUnit(NearEnemy);
	TestFalse(TEXT("Bot unregister"), Registry->IsRegistered(NearEnemy));
	TestSamePtr(TEXT("Next hostile after unregister"), static_cast<AMSUnitBase*>(Registry->FindNearestHostileUnit(Bot, 1000.0f)), static_cast<AMSUnitBase*>(FarEnemy));

	// --- 怯み/よろけ/ダウン/起上がり/死亡の二重ゲート(タグ消失で自動復帰) ---
	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	TestNotNull(TEXT("Auto possess spawns AMSBotAIController"), Controller);
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	UAbilitySystemComponent* ASC = Bot->GetAbilitySystemComponent();
	ASC->SetLooseGameplayTagCount(MSTags::State_Flinched, 1);
	TestTrue(TEXT("Flinch gate blocks BOT actions"), Controller->IsActionBlocked());
	TestFalse(TEXT("Flinch blocks movement request"), Controller->CanIssueMovementRequest());
	TestFalse(TEXT("Flinch blocks GA"), Controller->CanActivateAttackAbility());
	ASC->SetLooseGameplayTagCount(MSTags::State_Flinched, 0);
	ASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 1);
	TestTrue(TEXT("Stagger gate blocks actions"), Controller->IsActionBlocked());
	TestFalse(TEXT("Stagger blocks movement request"), Controller->CanIssueMovementRequest());
	TestFalse(TEXT("Stagger blocks GA"), Controller->CanActivateAttackAbility());
	ASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 0); ASC->SetLooseGameplayTagCount(MSTags::State_KnockedDown, 1);
	TestTrue(TEXT("Knockdown gate"), Controller->IsActionBlocked());
	ASC->SetLooseGameplayTagCount(MSTags::State_KnockedDown, 0); ASC->SetLooseGameplayTagCount(MSTags::State_GettingUp, 1);
	TestTrue(TEXT("Getting-up gate blocks BOT actions"), Controller->IsActionBlocked());
	ASC->SetLooseGameplayTagCount(MSTags::State_GettingUp, 0); ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TestTrue(TEXT("Death gate"), Controller->IsActionBlocked());
	ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 0);
	TestFalse(TEXT("Tag removal restores actions"), Controller->IsActionBlocked());
	return true;
}

bool FMSBotMovementRecoveryCollisionTest::RunTest(const FString&)
{
	MSBotAITestP61::FScopedBotWorld Scope(TEXT("Lvl_Biome_Daibao"));
	const FVector GroundCenter(0.0f, 0.0f, -50.0f);
	const FVector GroundScale(20000.0f, 20000.0f, 100.0f);
	AStaticMeshActor* Ground = MSBotAITestP61::AddGroundSlab(
		Scope.World, GroundCenter, GroundScale);
	if (!TestNotNull(TEXT("collision fixture ground slab is available"), Ground))
	{
		return false;
	}
	TestTrue(TEXT("collision fixture uses Daibao direct ground steering"),
		AMSBotAIController::ShouldUseDirectGroundSteering(Scope.World->GetMapName()));

	AMSBotCharacter* Bot = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(
		Scope.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("collision fixture bot is available"), Bot))
	{
		return false;
	}
	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	if (!TestNotNull(TEXT("collision fixture controller is available"), Controller))
	{
		return false;
	}

	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	if (!TestNotNull(TEXT("collision fixture capsule is available"), Capsule))
	{
		return false;
	}
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float GroundTopZ = GroundCenter.Z + GroundScale.Z * 0.5f;
	Bot->SetActorLocation(FVector(0.0f, 0.0f, GroundTopZ + HalfHeight + 2.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	FNavigationSystem::AddNavigationSystemToWorld(*Scope.World, FNavigationSystemRunMode::GameMode);
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Scope.World);
	if (!TestNotNull(TEXT("direct movement requires navigation system"), Navigation)
		|| !TestNotNull(TEXT("direct movement requires abstract navigation data"), Navigation->GetAbstractNavData())) return false;
	const FVector ShortEscapeGoal = Bot->GetActorLocation() + FVector(350.0f, 0.0f, 0.0f);
	TestTrue(TEXT("recovery fixture requests exactly 350cm"),
		FMath::IsNearlyEqual(FVector::Dist2D(ShortEscapeGoal, Bot->GetActorLocation()), 350.0f));
	TestTrue(TEXT("Daibao recovery request accepts a 350cm goal"),
		Controller->MoveToLocationOrDirect(ShortEscapeGoal, 100.0f, false));
	TestTrue(FString::Printf(TEXT("350cm recovery goal is not collapsed by capsule overlap (result=%d map=%s)"),
		Controller->GetLastMoveRequestResultForTesting(), *Scope.World->GetMapName()),
		Controller->GetLastMoveRequestResultForTesting()
			!= static_cast<int32>(EPathFollowingRequestResult::AlreadyAtGoal));

	FVector EscapePoint = FVector::ZeroVector;
	const bool bResolved = Controller->FindLocalEscapePointForTesting(
		FVector(5000.0f, 0.0f, Bot->GetActorLocation().Z), 1, EscapePoint);
	TestTrue(TEXT("actual sized capsule resolves a local escape on flat ground"), bResolved);
	if (!bResolved)
	{
		return false;
	}

	TestTrue(TEXT("escape remains bounded to the local recovery radius"),
		FVector::Dist2D(EscapePoint, Bot->GetActorLocation()) <= 701.0f);
	TestTrue(TEXT("escape keeps the capsule above the floor with clearance"),
		EscapePoint.Z > GroundTopZ + HalfHeight);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotMovementRecoveryTest), false);
	QueryParams.AddIgnoredActor(Bot);
	const bool bEndpointBlocked = Scope.World->OverlapBlockingTestByChannel(
		EscapePoint, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), QueryParams);
	TestFalse(TEXT("escape endpoint is capsule-clear of the collision floor"), bEndpointBlocked);

	// A unit can arrive with a small initial floor penetration after a drop. The recovery
	// sweep must allow leaving that initial overlap while still validating the endpoint.
	Bot->SetActorLocation(FVector(0.0f, 0.0f, GroundTopZ + HalfHeight - 10.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	FVector PenetratingEscapePoint = FVector::ZeroVector;
	const bool bPenetratingResolved = Controller->FindLocalEscapePointForTesting(
		FVector(5000.0f, 0.0f, Bot->GetActorLocation().Z), -1, PenetratingEscapePoint);
	TestTrue(TEXT("initial capsule penetration still permits a validated escape"), bPenetratingResolved);
	if (bPenetratingResolved)
	{
		TestTrue(TEXT("penetration escape remains locally bounded"),
			FVector::Dist2D(PenetratingEscapePoint, Bot->GetActorLocation()) <= 701.0f);
		TestTrue(TEXT("penetration escape keeps the capsule above the floor"),
			PenetratingEscapePoint.Z > GroundTopZ + HalfHeight);
		const bool bPenetratingEndpointBlocked = Scope.World->OverlapBlockingTestByChannel(
			PenetratingEscapePoint, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), QueryParams);
		TestFalse(TEXT("penetration escape endpoint is capsule-clear"), bPenetratingEndpointBlocked);
	}
	return true;
}

// P6-2 検証: 敵階級パラメータ(EnemyTier実データ注入)のテスト。
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.AI.BotAI.P6_2"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.AI.BotAI.P6_2"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIP62Test, "NeoMSGO.AI.BotAI.P6_2",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotAIP62Test::RunTest(const FString&)
{
	const FMSBotAIConfig Config;

	// --- 実データ既定値(docs/ArchitectureMap.md §16) ---
	TestEqual(TEXT("Config: BoostBurstTrigger follows MSScale"), Config.BoostBurstTriggerDistance, 1200.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: MediumBoostTriggerFactor 1.2"), Config.MediumBoostTriggerFactor, 1.2f);
	TestEqual(TEXT("Config: MediumBoostCooldownMultiplier 0.4"), Config.MediumBoostCooldownMultiplier, 0.4f);
	TestEqual(TEXT("Config: MediumMeleeStopDistance follows MSScale"), Config.MediumMeleeStopDistance, 240.0f * MSGO::MSScale);
	TestEqual(TEXT("Config: KickRange 300cm"), Config.KickRange, 300.0f);

	// --- 階級別Engage/Break式(P6-1式の再検証。武器射程2400cmの機体) ---
	TestEqual(TEXT("Weak engage = weapon range"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Weak, 2400.0f, Config), 2400.0f);
	TestEqual(TEXT("Weak break = weapon range"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Weak, 2400.0f, Config), 2400.0f);
	TestEqual(TEXT("Medium engage follows MSScale"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Medium, 2400.0f, Config), Config.MeleeEngageDistance);
	TestEqual(TEXT("Medium break follows MSScale"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Medium, 2400.0f, Config), Config.MeleeEngageDistance * 1.5f);
	TestEqual(TEXT("Strong engage = min(range, scaled preference+bonus)"), AMSBotAIController::CalculateEngageDistance(EMSEnemyTier::Strong, 24000.0f, Config), Config.BazookaPreferredDistance + Config.StrongEngageDistanceBonus);
	TestEqual(TEXT("Strong break = weapon range"), AMSBotAIController::CalculateBreakDistance(EMSEnemyTier::Strong, 2400.0f, Config), 2400.0f);

	// --- ブースト頻発条件の階級差 ---
	TestEqual(TEXT("Weak boost-burst trigger follows MSScale"), AMSBotAIController::CalculateBoostBurstTriggerDistance(EMSEnemyTier::Weak, Config), Config.BoostBurstTriggerDistance);
	TestEqual(TEXT("Medium boost-burst trigger follows scaled melee engage"), AMSBotAIController::CalculateBoostBurstTriggerDistance(EMSEnemyTier::Medium, Config), Config.MeleeEngageDistance * Config.MediumBoostTriggerFactor);
	TestEqual(TEXT("Strong boost-burst trigger follows MSScale"), AMSBotAIController::CalculateBoostBurstTriggerDistance(EMSEnemyTier::Strong, Config), Config.BoostBurstTriggerDistance);
	TestEqual(TEXT("Weak boost-burst cooldown mult = 1.0 (normal)"), AMSBotAIController::CalculateBoostBurstCooldownMultiplier(EMSEnemyTier::Weak, Config), 1.0f);
	TestEqual(TEXT("Medium boost-burst cooldown mult = 0.4"), AMSBotAIController::CalculateBoostBurstCooldownMultiplier(EMSEnemyTier::Medium, Config), 0.4f);
	TestEqual(TEXT("Strong boost-burst cooldown mult = 1.0 (normal)"), AMSBotAIController::CalculateBoostBurstCooldownMultiplier(EMSEnemyTier::Strong, Config), 1.0f);

	// --- Strong専用の武器持ち替え閾値(バズーカ優先) ---
	TestFalse(TEXT("Weak never prefers bazooka"), AMSBotAIController::ShouldPreferBazooka(EMSEnemyTier::Weak, 2000.0f, Config));
	TestFalse(TEXT("Medium never prefers bazooka"), AMSBotAIController::ShouldPreferBazooka(EMSEnemyTier::Medium, 2000.0f, Config));
	TestFalse(TEXT("Strong below scaled threshold keeps machine gun"), AMSBotAIController::ShouldPreferBazooka(EMSEnemyTier::Strong, Config.BazookaPreferredDistance - 1.0f, Config));
	TestTrue(TEXT("Strong at scaled threshold prefers bazooka"), AMSBotAIController::ShouldPreferBazooka(EMSEnemyTier::Strong, Config.BazookaPreferredDistance, Config));

	// --- 弱体化パラメータ(全階級共通) ---
	const FMSBotWeakeningParams Weakening;
	TestEqual(TEXT("Weakening: damageMultiplier 0.3"), Weakening.DamageMultiplier, 0.3f);
	TestEqual(TEXT("Weakening: aimSpreadDegrees 4"), Weakening.AimSpreadDegrees, 4.0f);
	TestEqual(TEXT("Weakening: fireIntervalMultiplier 5"), Weakening.FireIntervalMultiplier, 5.0f);
	TestEqual(TEXT("Weakening: meleeFireIntervalFactor 0.4"), Weakening.MeleeFireIntervalFactor, 0.4f);
	TestEqual(TEXT("Weakening: effective melee fire interval mult = 2.0 (not 5.0)"), Weakening.GetMeleeFireIntervalMultiplier(), 2.0f);

	// --- 外見差分(enemyTierVariant) ---
	TestEqual(TEXT("Weak variant = 0"), AMSBotCharacter::CalculateEnemyTierVariant(EMSEnemyTier::Weak), 0);
	TestEqual(TEXT("Medium variant = 1"), AMSBotCharacter::CalculateEnemyTierVariant(EMSEnemyTier::Medium), 1);
	TestEqual(TEXT("Strong variant = 2"), AMSBotCharacter::CalculateEnemyTierVariant(EMSEnemyTier::Strong), 2);

	// --- BOT側からGetCombatModifiers()経由で弱体化パラメータが供給される(GA側の共通乗算箇所) ---
	MSBotAITestP61::FScopedBotWorld Scope;
	AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("Bot spawned"), Bot);
	if (Bot)
	{
		TestEqual(TEXT("BeginPlay caches EnemyTierVariant for default tier(Weak)"), Bot->GetEnemyTierVariant(), 0);
		const FMSCombatModifiers Modifiers = Bot->GetCombatModifiers();
		TestEqual(TEXT("GetCombatModifiers: DamageMultiplier 0.3"), Modifiers.DamageMultiplier, 0.3f);
		TestEqual(TEXT("GetCombatModifiers: AimSpreadDegrees 4"), Modifiers.AimSpreadDegrees, 4.0f);
		TestEqual(TEXT("GetCombatModifiers: FireIntervalMultiplier 5"), Modifiers.FireIntervalMultiplier, 5.0f);
		TestEqual(TEXT("GetCombatModifiers: MeleeFireIntervalMultiplier 2 (not 5)"), Modifiers.MeleeFireIntervalMultiplier, 2.0f);
		TestTrue(TEXT("BOT CharacterMovement enables RVO avoidance"), Bot->GetCharacterMovement()->bUseRVOAvoidance);
		TestEqual(TEXT("BOT RVO radius follows AI config"), Bot->GetCharacterMovement()->AvoidanceConsiderationRadius,
			Config.RVOAvoidanceRadius);
	}

	// 注: AMSUnitBase::GetCombatModifiers()の既定(中立値)はGA_Fire_Hitscan/GA_Melee/
	// UMSWeaponComponentのコード読解で確認済み(AMSUnitBaseはAbstractのため直接スポーン不可)。

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIP63Test, "NeoMSGO.AI.BotAI.P6_3",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotAIP63Test::RunTest(const FString&)
{
	// The visual key preserves both dimensions without introducing a second faction state.
	TestEqual(TEXT("Enemy weak visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Weak, false), 0);
	TestEqual(TEXT("Enemy medium visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Medium, false), 1);
	TestEqual(TEXT("Enemy strong visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Strong, false), 2);
	TestEqual(TEXT("Ally weak visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Weak, true), 3);
	TestEqual(TEXT("Ally medium visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Medium, true), 4);
	TestEqual(TEXT("Ally strong visual variant"), AMSBotCharacter::CalculateVisualVariantId(EMSEnemyTier::Strong, true), 5);

	MSBotAITestP61::FScopedBotWorld Scope;
	// These deliberately close actor locations exercise registry ordering, not physical placement.
	// Ignore fixture overlaps now that the production capsule is 18m-scale.
	AMSBotCharacter* AllyQuerier = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(0, 0, 0));
	AMSBotCharacter* AllyPeer = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(50, 0, 0));
	AMSBotCharacter* EnemyQuerier = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(100, 0, 0));
	AMSBotCharacter* EnemyPeer = MSBotAITestP61::SpawnBotIgnoringFixtureOverlap(Scope.World, FVector(150, 0, 0));
	UMSUnitRegistrySubsystem* Registry = Scope.World->GetSubsystem<UMSUnitRegistrySubsystem>();
	TestNotNull(TEXT("P6-3 unit registry exists"), Registry);
	if (!Registry || !AllyQuerier || !AllyPeer || !EnemyQuerier || !EnemyPeer) return false;

	AllyQuerier->SetGenericTeamId(MSTeam::PlayerTeamId());
	AllyPeer->SetGenericTeamId(MSTeam::PlayerTeamId());
	TestTrue(TEXT("TeamPlayer selects ally visual"), AllyQuerier->IsAllyVisualVariant());
	TestEqual(TEXT("TeamPlayer updates combined visual immediately"), AllyQuerier->GetVisualVariantId(), 3);
	TestSamePtr(TEXT("Ally detects nearest enemy and ignores nearer ally"),
		static_cast<AMSUnitBase*>(Registry->FindNearestHostileUnit(AllyQuerier, 1000.0f)), static_cast<AMSUnitBase*>(EnemyQuerier));
	TestSamePtr(TEXT("Enemy detects nearest ally and ignores nearer enemy"),
		static_cast<AMSUnitBase*>(Registry->FindNearestHostileUnit(EnemyQuerier, 1000.0f)), static_cast<AMSUnitBase*>(AllyPeer));

	// A TeamId-only flip reverses targeting and visuals; registry membership is unchanged.
	AllyQuerier->SetGenericTeamId(MSTeam::EnemyTeamId());
	TestFalse(TEXT("TeamEnemy clears ally visual"), AllyQuerier->IsAllyVisualVariant());
	TestEqual(TEXT("TeamEnemy updates combined visual immediately"), AllyQuerier->GetVisualVariantId(), 0);
	TestSamePtr(TEXT("Faction flip reverses hostile selection"),
		static_cast<AMSUnitBase*>(Registry->FindNearestHostileUnit(AllyQuerier, 1000.0f)), static_cast<AMSUnitBase*>(AllyPeer));
	TestEqual(TEXT("Registry counts player team after flip"), Registry->CountUnitsOfTeam(MSTeam::PlayerTeamId()), 1);
	TestEqual(TEXT("Registry counts enemy team after flip"), Registry->CountUnitsOfTeam(MSTeam::EnemyTeamId()), 3);

	AllyQuerier->SetGenericTeamId(MSTeam::PlayerTeamId());
	AllyPeer->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TestEqual(TEXT("Team count excludes dead by default"), Registry->CountUnitsOfTeam(MSTeam::PlayerTeamId()), 1);
	TestEqual(TEXT("Team count optionally includes dead"), Registry->CountUnitsOfTeam(MSTeam::PlayerTeamId(), false), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIP64Test, "NeoMSGO.AI.BotAI.P6_4",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotAIP64Test::RunTest(const FString&)
{
	MSBotAITestP61::FScopedBotWorld Scope;
	auto SpawnControlledBot = [&Scope]()
	{
		AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		AMSBotAIController* Controller = Bot ? Cast<AMSBotAIController>(Bot->GetController()) : nullptr;
		if (Bot && !Controller)
		{
			Controller = Scope.World->SpawnActor<AMSBotAIController>();
			Controller->Possess(Bot);
		}
		return TPair<AMSBotCharacter*, AMSBotAIController*>(Bot, Controller);
	};

	const FMSBotAIConfig Config;
	TestEqual(TEXT("Burst speed multiplier"), Config.BoostBurstSpeedMultiplier, 2.6f);
	TestEqual(TEXT("Burst duration"), Config.BoostBurstDuration, 1.1f);
	TestEqual(TEXT("Base cooldown"), Config.BoostBurstCooldown, 2.5f);

	const EMSEnemyTier TierCases[] = {
		EMSEnemyTier::Weak, EMSEnemyTier::Medium, EMSEnemyTier::Strong};
	for (const EMSEnemyTier Tier : TierCases)
	{
		auto [Bot, Controller] = SpawnControlledBot();
		TestNotNull(TEXT("Tier burst bot"), Bot);
		TestNotNull(TEXT("Tier burst controller"), Controller);
		if (!Bot || !Controller) continue;
		Bot->SetEnemyTier(Tier);
		const float TriggerDistance = AMSBotAIController::CalculateBoostBurstTriggerDistance(Tier, Config);
		TestFalse(TEXT("At trigger distance does not burst"), Controller->TryStartBoostBurst(
			TriggerDistance));
		TestTrue(TEXT("Beyond tier trigger starts burst"), Controller->TryStartBoostBurst(TriggerDistance + 1.0f));
	}

	auto [Bot, Controller] = SpawnControlledBot();
	if (!Bot || !Controller) return false;
	UCharacterMovementComponent* Movement = Bot->GetCharacterMovement();
	UAbilitySystemComponent* ASC = Bot->GetAbilitySystemComponent();
	const float BaseSpeed = Movement->MaxWalkSpeed;
	const float BaseAcceleration = Movement->MaxAcceleration;
	const float WeakTriggerDistance = AMSBotAIController::CalculateBoostBurstTriggerDistance(
		EMSEnemyTier::Weak, Config);
	TestTrue(TEXT("Weak burst starts"), Controller->TryStartBoostBurst(WeakTriggerDistance + 1.0f));
	TestTrue(TEXT("Burst state API"), Controller->IsBoostBursting());
	TestTrue(TEXT("MaxWalkSpeed raised"), Movement->MaxWalkSpeed > BaseSpeed);
	TestTrue(TEXT("MaxAcceleration raised"), Movement->MaxAcceleration > BaseAcceleration);
	TestTrue(TEXT("State.Boosting added"), ASC->HasMatchingGameplayTag(MSTags::State_Boosting));
	UMSAnimInstance* Anim = NewObject<UMSAnimInstance>(Bot->GetMesh());
	Anim->UpdateAnimationInputs(Bot);
	TestTrue(TEXT("Anim input reads explicit boost tag"), Anim->bIsBoosting);

	Controller->Tick(Config.BoostBurstDuration);
	TestFalse(TEXT("Burst ends after 1.1 seconds"), Controller->IsBoostBursting());
	TestEqual(TEXT("MaxWalkSpeed restored"), Movement->MaxWalkSpeed, BaseSpeed);
	TestEqual(TEXT("MaxAcceleration restored"), Movement->MaxAcceleration, BaseAcceleration);
	TestFalse(TEXT("State.Boosting removed"), ASC->HasMatchingGameplayTag(MSTags::State_Boosting));
	TestEqual(TEXT("Weak cooldown is 2.5 seconds"), Controller->GetBoostBurstCooldownRemaining(), 2.5f);
	TestFalse(TEXT("Weak cooldown prevents restart"), Controller->TryStartBoostBurst(WeakTriggerDistance + 1.0f));

	auto [MediumBot, MediumController] = SpawnControlledBot();
	if (!MediumBot || !MediumController) return false;
	MediumBot->SetEnemyTier(EMSEnemyTier::Medium);
	const float MediumTriggerDistance = AMSBotAIController::CalculateBoostBurstTriggerDistance(
		EMSEnemyTier::Medium, Config);
	TestTrue(TEXT("Medium burst starts beyond scaled trigger"),
		MediumController->TryStartBoostBurst(MediumTriggerDistance + 1.0f));
	MediumController->Tick(Config.BoostBurstDuration);
	TestEqual(TEXT("Medium cooldown is 1.0 second"), MediumController->GetBoostBurstCooldownRemaining(), 1.0f);
	TestFalse(TEXT("Medium cooldown prevents restart"),
		MediumController->TryStartBoostBurst(MediumTriggerDistance + 1.0f));

	const FGameplayTag BlockingTags[] = {
		MSTags::State_Flinched.GetTag(), MSTags::State_Staggered.GetTag(),
		MSTags::State_KnockedDown.GetTag(), MSTags::State_GettingUp.GetTag(), MSTags::State_Dead.GetTag()};
	for (const FGameplayTag& BlockingTag : BlockingTags)
	{
		auto [BlockedBot, BlockedController] = SpawnControlledBot();
		if (!BlockedBot || !BlockedController) return false;
		BlockedBot->GetAbilitySystemComponent()->AddLooseGameplayTag(BlockingTag);
		TestFalse(TEXT("Full-body disabled state prevents burst"),
			BlockedController->TryStartBoostBurst(WeakTriggerDistance + 1.0f));
	}

	auto [InterruptedBot, InterruptedController] = SpawnControlledBot();
	if (!InterruptedBot || !InterruptedController) return false;
	TestTrue(TEXT("Interrupt test burst starts"),
		InterruptedController->TryStartBoostBurst(WeakTriggerDistance + 1.0f));
	InterruptedBot->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Staggered);
	InterruptedController->Tick(0.01f);
	TestFalse(TEXT("Stagger immediately interrupts active burst"), InterruptedController->IsBoostBursting());
	TestFalse(TEXT("Interrupted burst tag removed"), InterruptedBot->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Boosting));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIP65Test, "NeoMSGO.AI.BotAI.P6_5",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotAITestP65
{
	UMSMeleeWeaponData* MakeP65MeleeWeapon(UObject* Outer)
	{
		UMSMeleeWeaponData* Weapon = NewObject<UMSMeleeWeaponData>(Outer);
		Weapon->Range = 1.5f;
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->FireInterval = 1.0f;
		Weapon->MagazineSize = 0;
		return Weapon;
	}

	UGameplayAbility* FindPrimaryAbilityInstance(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> Class)
	{
		const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromClass(Class) : nullptr;
		return Spec ? Spec->GetPrimaryInstance() : nullptr;
	}
}

bool FMSBotAIP65Test::RunTest(const FString&)
{
	MSBotAITestP61::FScopedBotWorld Scope;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AMSBotCharacter* Enemy = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector(1600.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	AMSBotCharacter* Ally = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector(1600.0f, 50.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	if (!Bot || !Enemy || !Ally) return false;
	Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	Enemy->SetGenericTeamId(MSTeam::EnemyTeamId());
	Ally->SetGenericTeamId(MSTeam::PlayerTeamId());
	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	if (!Controller) return false;
	Controller->Tick(0.0f); // refreshes CurrentTarget through the shared registry/team policy

	UAbilitySystemComponent* ASC = Bot->GetAbilitySystemComponent();
	UMSMeleeWeaponData* MeleeWeapon = MSBotAITestP65::MakeP65MeleeWeapon(Bot);
	Bot->GetWeaponComponent()->InitializeSlots({MeleeWeapon});
	const float EffectiveMeleeReach = UGA_Melee::GetMeleeReachCm(MeleeWeapon->Range, false);
	TestEqual(TEXT("Melee trigger test starts at 1600cm"), Controller->GetTargetDistance(), 1600.0f);
	TestEqual(TEXT("Equipped melee reach follows MSScale"), EffectiveMeleeReach,
		150.0f * MSGO::MSScale, 0.01f);
	TestEqual(TEXT("BOT legacy melee approach range matches the attack reach"),
		Controller->GetCurrentWeaponRangeCm(), EffectiveMeleeReach, 0.01f);
	MeleeWeapon->RangeWorldScale = 1.0f;
	TestEqual(TEXT("BOT physical-metre melee approach range follows the equipped weapon"),
		Controller->GetCurrentWeaponRangeCm(), 150.0f, 0.01f);
	MeleeWeapon->RangeWorldScale = MSGO::MSScale;
	TestNotNull(TEXT("BOT is granted shared GA_Melee through ASC"), ASC->FindAbilitySpecFromClass(UGA_Melee::StaticClass()));
	TestFalse(TEXT("Melee does not trigger outside the scaled reach"), Controller->TryActivateMeleeAbility());
	Enemy->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	TestEqual(TEXT("Melee trigger target is moved to 1000cm"), Controller->GetTargetDistance(), 1000.0f);
	TestTrue(TEXT("Target is inside the scaled melee reach"), Controller->GetTargetDistance() <= EffectiveMeleeReach);
	TestTrue(TEXT("Attack melee in stop distance activates shared GA_Melee through ASC"), Controller->TryActivateMeleeAbility());
	TestTrue(TEXT("Shared GA_Melee consumed the weapon fire interval"), Bot->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestNotNull(TEXT("GA_Melee primary instance exists after ASC activation"),
		MSBotAITestP65::FindPrimaryAbilityInstance(ASC, UGA_Melee::StaticClass()));

	const FMSBotAIConfig Config;
	TestEqual(TEXT("Kick range uses FMSBotAIConfig 300cm"), Config.KickRange, 300.0f);
	TestEqual(TEXT("Kick power is 350"), UGA_BotKick::KickPower, 350.0f);
	TestEqual(TEXT("Kick down point is 40"), UGA_BotKick::KickDownPoint, 40.0f);
	TestEqual(TEXT("Kick overlap radius is 120cm"), UGA_BotKick::KickRadiusCm, 120.0f);
	TestEqual(TEXT("Kick cooldown is 2.8 seconds"), UGA_BotKick::KickCooldownSeconds, 2.8f);
	Enemy->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));

	Bot->SetEnemyTier(EMSEnemyTier::Weak);
	TestFalse(TEXT("Weak BOT cannot kick"), Controller->TryActivateKick());
	Bot->SetEnemyTier(EMSEnemyTier::Medium);
	TestFalse(TEXT("Medium BOT cannot kick"), Controller->TryActivateKick());
	Bot->SetEnemyTier(EMSEnemyTier::Strong);
	const float EnemyArmorBefore = Enemy->GetVitalSet()->GetArmor();
	const float EnemyDPBefore = Enemy->GetVitalSet()->GetDownPoint();
	const float AllyArmorBefore = Ally->GetVitalSet()->GetArmor();
	TestTrue(TEXT("Strong BOT can kick within KickRange"), Controller->TryActivateKick());
	TestEqual(TEXT("Kick starts exact 2.8 second cooldown"), Controller->GetKickCooldownRemaining(), 2.8f);
	TestFalse(TEXT("Kick cannot repeat during cooldown"), Controller->TryActivateKick());
	TestTrue(TEXT("Kick power travels through UGE_Damage"),
		FMath::IsNearlyEqual(EnemyArmorBefore - Enemy->GetVitalSet()->GetArmor(), UGA_BotKick::KickPower, 0.5f));
	TestTrue(TEXT("Kick DP travels through UGE_Damage"),
		FMath::IsNearlyEqual(Enemy->GetVitalSet()->GetDownPoint() - EnemyDPBefore, UGA_BotKick::KickDownPoint, 0.5f));
	TestEqual(TEXT("Kick prefilters friendly fire with CanDamage and GE execution remains final gate"),
		Ally->GetVitalSet()->GetArmor(), AllyArmorBefore);
	Controller->Tick(2.79f);
	TestFalse(TEXT("Cooldown still blocks at 2.79 seconds"), Controller->TryActivateKick());
	Controller->Tick(0.01f);
	TestTrue(TEXT("Kick is available when 2.8 second cooldown expires"), Controller->CanUseKick());

	Enemy->SetActorLocation(FVector(Config.KickRange + 1.0f, 0.0f, 0.0f));
	Controller->Tick(Config.TargetRefreshInterval);
	TestFalse(TEXT("Kick is rejected beyond configured 300cm range"), Controller->CanUseKick());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotCombatMobilityTest,
	"NeoMSGO.AI.BotAI.Review.CombatMobilityWhileFiring",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotCombatMobilityTest::RunTest(const FString&)
{
	MSBotAITestP61::FScopedBotWorld Scope;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AMSBotCharacter* Enemy = Scope.World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	if (!Bot || !Enemy) return false;
	Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	Enemy->SetGenericTeamId(MSTeam::EnemyTeamId());
	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	if (!Controller) return false;
	Controller->Tick(0.0f);

	UMSRangedWeaponData* Gun = NewObject<UMSRangedWeaponData>(Bot);
	Gun->Range = 20.0f;
	Gun->Power = 100;
	Gun->MagazineSize = 10;
	Gun->ReserveAmmo = 10;
	Gun->FireInterval = 0.2f;
	Bot->GetWeaponComponent()->InitializeSlots({Gun});
	TestTrue(TEXT("BOT is in an attack-capable engagement"), Controller->CanActivateAttackAbility());

	const float PreferredDistance = Controller->GetEngageDistance() * Bot->GetAIConfig().StrafeDistanceFactor;
	const FVector Destination = FMSBotAttackTask::CalculateStrafeDestination(
		Bot->GetActorLocation(), Enemy->GetActorLocation(),
		FMath::Min(Controller->GetTargetDistance(), PreferredDistance), 1);
	const float MaxWalkSpeed = Bot->GetCharacterMovement()->MaxWalkSpeed;
	const FVector MoveVelocity = FMSBotAttackTask::CalculateAttackMoveVelocity(
		Bot->GetActorLocation(), Destination, MaxWalkSpeed);
	TestTrue(TEXT("Ranged fire ability triggers during the engagement"), Controller->TryActivateCurrentWeaponAbility());
	TestTrue(TEXT("Firing consumed the weapon interval"), Bot->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestTrue(TEXT("Engagement movement speed is not zero while the weapon is firing"), MoveVelocity.Size2D() > 0.0f);
	TestTrue(TEXT("Engagement movement uses the actual CharacterMovement max speed"),
		FMath::IsNearlyEqual(MoveVelocity.Size2D(), MaxWalkSpeed, 0.1f));
	return true;
}

// ユーザーレビュー#69: BOT同士が対峙したまま攻撃も移動もしなくなるフリーズの対策。
//  1) 格闘BOTの停止距離がTryActivateMeleeAbilityの発動距離(240cm)より遠く、
//     永遠に攻撃できないまま静止する膠着の修正(全階級で発動距離まで詰める)
//  2) 発砲タイムアウト時の位置取り直し先(反転+内側へ回り込み)
//  3) 完全弾切れ武器を構えたままにならないスロット持ち替え
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotAIReview69Test, "NeoMSGO.AI.BotAI.Review69.StareFreezeRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotAIReview69Test::RunTest(const FString&)
{
	const FMSBotAIConfig Config;
	TestEqual(TEXT("Config: AttackRepositionTimeout 2.5s"), Config.AttackRepositionTimeout, 2.5f);
	TestEqual(TEXT("Config: AttackRepositionDuration 1.2s"), Config.AttackRepositionDuration, 1.2f);

	// --- 1) 格闘停止距離: 全階級で発動条件(MediumMeleeStopDistance)以内まで詰める ---
	// 旧実装ではWeak/StrongがEngageDistance(武器射程=600cm等)で静止し、240cm以内でしか
	// 発動しない格闘を一生撃てない=見つめ合いフリーズになっていた。
	const float MeleeEngage = 600.0f;
	for (const EMSEnemyTier Tier : {EMSEnemyTier::Weak, EMSEnemyTier::Medium, EMSEnemyTier::Strong})
	{
		const float StopDistance = FMSBotAttackTask::CalculateMeleeStopDistance(Tier, Config, MeleeEngage);
		TestTrue(FString::Printf(TEXT("Tier%d melee stop distance reaches activation range"), static_cast<int32>(Tier)),
			StopDistance <= Config.MediumMeleeStopDistance);
	}
	TestEqual(TEXT("Short engage distance is preserved"),
		FMSBotAttackTask::CalculateMeleeStopDistance(EMSEnemyTier::Weak, Config, 150.0f), 150.0f);

	// --- 2) 位置取り直し先: 距離を詰めつつ大きく回り込む(方向で左右反転) ---
	const FVector Target(0.0f, 0.0f, 100.0f);
	const FVector Bot(1000.0f, 0.0f, 100.0f);
	const FVector RepoRight = FMSBotAttackTask::CalculateRepositionDestination(Bot, Target, 1000.0f, 1);
	const FVector RepoLeft = FMSBotAttackTask::CalculateRepositionDestination(Bot, Target, 1000.0f, -1);
	TestTrue(TEXT("Reposition closes distance to break the stalemate"),
		FVector::Dist2D(RepoRight, Target) < 1000.0f);
	TestTrue(TEXT("Reposition has a strong lateral component"), FMath::Abs(RepoRight.Y) > 100.0f);
	TestTrue(TEXT("Reposition side follows strafe direction"), RepoRight.Y * RepoLeft.Y < 0.0f);
	TestTrue(TEXT("Reposition keeps a minimum standoff"),
		FVector::Dist2D(FMSBotAttackTask::CalculateRepositionDestination(Bot, Target, 100.0f, 1), Target) >= 149.0f);

	// --- 3) 完全弾切れ武器からの持ち替え ---
	MSBotAITestP61::FScopedBotWorld Scope;
	AMSBotCharacter* Bot2 = Scope.World->SpawnActor<AMSBotCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AMSBotAIController* Controller = Bot2 ? Cast<AMSBotAIController>(Bot2->GetController()) : nullptr;
	if (Bot2 && !Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot2);
	}
	if (!Bot2 || !Controller) return false;

	UMSRangedWeaponData* EmptyGun = NewObject<UMSRangedWeaponData>(Bot2);
	EmptyGun->MagazineSize = 1;
	EmptyGun->ReserveAmmo = 0;
	EmptyGun->FireInterval = 0.0f;
	EmptyGun->WeaponSwitchDuration = 0.0f;
	UMSRangedWeaponData* LoadedGun = NewObject<UMSRangedWeaponData>(Bot2);
	LoadedGun->MagazineSize = 10;
	LoadedGun->ReserveAmmo = 10;
	LoadedGun->WeaponSwitchDuration = 0.0f;
	UMSWeaponComponent* Weapons = Bot2->GetWeaponComponent();
	Weapons->InitializeSlots({EmptyGun, LoadedGun});

	TestFalse(TEXT("Ammo remaining: no slot change"), Controller->SelectUsableWeaponSlot());
	TestTrue(TEXT("Setup: consume the only round"), Weapons->ConsumeAmmoAndStartCooldown(0));
	TestTrue(TEXT("Setup: slot0 is now completely empty"), Weapons->IsEmpty(0));
	TestTrue(TEXT("Empty weapon switches to a usable slot"), Controller->SelectUsableWeaponSlot());
	TestEqual(TEXT("Active slot moved to the loaded weapon"), Weapons->GetActiveSlotIndex(), 1);
	TestFalse(TEXT("Usable weapon does not switch again"), Controller->SelectUsableWeaponSlot());
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
