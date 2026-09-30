// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-6 検証: ロックオン/ソフトエイム/ホーミングロック(UMSLockOnComponent)。
//  - 視野角: 通常30°(半角15°)内のみ候補、スコープ中は17°(半角8.5°)へ縮小(P3-5連携)
//  - スコア式: angleScore*0.35 + distanceScore*0.65(距離重視。近距離候補が遠方の正面候補に勝つ)
//  - LOS: 起点=自機胸元/終点=対象胸元。壁(MSStructure)遮蔽の候補は除外(B-5)
//  - dwell: 同一候補を0.8s注視でロック確定。候補切替でdwellリセット
//  - 自動解除: ロック中の遮蔽・対象のロック不可化(死亡相当)で解除。照準を外しただけでは維持
//  - 除外: 味方/中立/非ロック対象(占領拠点相当)は候補にならない(UMSTeamStatics::CanLockOn単一API)
//  - ソフトエイム: 基準は実カメラ位置(SpringArmピボット/自機胸元基準ではない)
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Player.LockOn"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Player.LockOn"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/MSAudioPlayback.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Components/SphereComponent.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "NeoMSGO.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSHUDRingWidget.h"
#include "UI/MSHUDTypes.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLockOnTest,
	"NeoMSGO.Player.LockOn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLockOnLookInputConflictTest,
	"NeoMSGO.Player.LockOnLookInputConflict",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRedLockEmergencyDodgeTest,
	"NeoMSGO.Player.RedLockEmergencyDodge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingReloadLockGateTest,
	"NeoMSGO.Player.HomingReloadLockGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingCircleProjectionTest,
	"NeoMSGO.Player.HomingCircleProjection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingCircleCaptureBoundaryTest,
	"NeoMSGO.Player.HomingCircleCaptureBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** 空間クエリ+BeginPlay(コンポーネントのカメラ解決)が必要なため使い捨てGameワールドを作る。
	 *  注意: Unityビルドで他テストcppと同一TUに結合され得るため、ヘルパーはファイル固有名にする。 */
	struct FLockOnTestWorld
	{
		UWorld* World = nullptr;

		FLockOnTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSLockOnTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FLockOnTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** WeaponTraceチャンネルにBlockする球コライダー(候補収集Overlap/LOSトレースの両方が拾う) */
	USphereComponent* AddLockOnTestSphere(AActor* Owner, const FName Name, const float SphereRadius,
		const FName ProfileName = NAME_None)
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(Owner, Name);
		Sphere->SetupAttachment(Owner->GetRootComponent());
		Sphere->SetSphereRadius(SphereRadius);
		if (ProfileName != NAME_None)
		{
			Sphere->SetCollisionProfileName(ProfileName);
		}
		else
		{
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
			Sphere->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
		}
		Sphere->RegisterComponent();
		return Sphere;
	}

	/** AMSTeamAgentActor の bLockOnTargetable をリフレクションで設定(P0-4テストと同手法)。
	 *  false へ戻すことで「死亡/破壊済み=CanBeLockedOn false」の状態遷移も模擬できる。 */
	void SetLockTargetable(AMSTeamAgentActor* Actor, const bool bLockable)
	{
		FProperty* Prop = AMSTeamAgentActor::StaticClass()->FindPropertyByName(TEXT("bLockOnTargetable"));
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue_InContainer(Actor, bLockable);
		}
	}

	/**
	 * ロックオン対象アクター(陣営+IMSLockOnTargetInterface+WeaponTraceコライダー)。
	 * AimPoint はインターフェース既定=Actor位置(AMSUnitBase の胸元1.4m相当は対象側実装の責務)。
	 * 本拠点コア相当は bLockable=true / 占領拠点・設置物相当は bLockable=false で模擬する。
	 */
	AMSTeamAgentActor* SpawnLockTarget(UWorld* World, const FGenericTeamId TeamId, const bool bLockable,
		const FVector& Location, const float ColliderRadius = 50.0f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSTeamAgentActor* Actor = World->SpawnActor<AMSTeamAgentActor>(
			AMSTeamAgentActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		Actor->SetGenericTeamId(TeamId);
		SetLockTargetable(Actor, bLockable);

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TargetRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		AddLockOnTestSphere(Actor, TEXT("TargetCollider"), ColliderRadius);
		return Actor;
	}

	/** 遮蔽壁(MSStructureプロファイル。WeaponTraceをBlock=LOSを遮る) */
	AActor* SpawnLockOnWall(UWorld* World, const FVector& Location, const float SphereRadius)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Wall, TEXT("WallRoot"));
		Wall->SetRootComponent(Root);
		Root->RegisterComponent();
		Wall->SetActorLocation(Location);
		AddLockOnTestSphere(Wall, TEXT("WallCollider"), SphereRadius, MSCollision::StructureProfileName);
		return Wall;
	}

	/** dwell計測を進める(0.1s刻みでTickComponentを直接駆動) */
	void TickLockOn(UMSLockOnComponent* LockOn, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > KINDA_SMALL_NUMBER)
		{
			const float Dt = FMath::Min(Seconds, Step);
			LockOn->TickComponent(Dt, LEVELTICK_All, nullptr);
			Seconds -= Dt;
		}
	}

	/**
	 * リロード連携テスト用: UMSWeaponComponent(ReloadRemaining等のカウントダウン)と
	 * UMSLockOnComponentを同一刻みで両方TickComponentする(0.1s刻み)。
	 * TickLockOnは誘導武器/リロード状態を持たないテストのために武器コンポーネントを
	 * 意図的に駆動しないため、リロード時間経過を検証するテストはこちらを使うこと。
	 */
	void TickWeaponAndLockOn(UMSWeaponComponent* Weapons, UMSLockOnComponent* LockOn, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > KINDA_SMALL_NUMBER)
		{
			const float Dt = FMath::Min(Seconds, Step);
			Weapons->TickComponent(Dt, LEVELTICK_All, nullptr);
			LockOn->TickComponent(Dt, LEVELTICK_All, nullptr);
			Seconds -= Dt;
		}
	}

	void DestroyLockOnActors(UWorld* World, std::initializer_list<AActor*> Actors)
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				World->DestroyActor(Actor);
			}
		}
	}
}

bool FMSLockOnTest::RunTest(const FString& Parameters)
{
	FLockOnTestWorld TestWorld;
	APhysicsVolume* WaterVolume = TestWorld.World->SpawnActor<APhysicsVolume>();
	WaterVolume->bWaterVolume = true;
	APhysicsVolume* DryVolume = TestWorld.World->SpawnActor<APhysicsVolume>();
	DryVolume->bWaterVolume = false;
	TestTrue(TEXT("#245: water gameplay volume is ignored as lock-on cover"),
		UMSLockOnComponent::ShouldIgnoreLineOfSightOccluder(WaterVolume));
	TestFalse(TEXT("#245: ordinary physics volume remains an occluder"),
		UMSLockOnComponent::ShouldIgnoreLineOfSightOccluder(DryVolume));
	UWorld* World = TestWorld.World;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0.0f, 0.0f, 300.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("AMSPlayerCharacterのスポーン"), Player))
	{
		return false;
	}
	APlayerController* LocalController = World->SpawnActor<APlayerController>(
		APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("local lock-on controller"), LocalController))
	{
		return false;
	}
	AddExpectedError(TEXT("CreateWidget"), EAutomationExpectedErrorFlags::Contains, 1);
	LocalController->SetAsLocalPlayerController();
	LocalController->Possess(Player);

	UMSLockOnComponent* LockOn = Player->GetLockOnComponent();
	UMSCameraComponent* Camera = Player->GetMSCamera();
	if (!TestNotNull(TEXT("UMSLockOnComponentが搭載されている"), LockOn) ||
		!TestNotNull(TEXT("UMSCameraComponentが搭載されている"), Camera))
	{
		return false;
	}

	// 円錐判定・スコアの基準=実カメラ位置(SpringArm SocketOffsetで肩越しにオフセット済み)
	const FVector CamLoc = Camera->GetComponentLocation();
	const FVector Fwd = Camera->GetForwardVector();
	const FVector Right = Camera->GetRightVector();

	/** 角度Deg・距離Distの位置(カメラ基準、水平面内で右方向へ振る) */
	auto AtAngle = [&](const float AngleDeg, const float Dist)
	{
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		return CamLoc + (Fwd * FMath::Cos(Rad) + Right * FMath::Sin(Rad)) * Dist;
	};

	// ------------------------------------------------------------------
	// 1. スコア式(純関数): angle*0.35 + distance*0.65(視野角は全角→半角換算)
	// ------------------------------------------------------------------
	{
		// 正面(角度0)・距離80%: 1.0*0.35 + 0.2*0.65 = 0.48
		TestEqual(TEXT("スコア: 正面・遠距離(8000/10000)=0.48"),
			UMSLockOnComponent::ComputeLockOnScore(0.0f, 30.0f, 8000.0f, 10000.0f), 0.48f, KINDA_SMALL_NUMBER);
		// 角度12°(半角15°の80%)・距離20%: 0.2*0.35 + 0.8*0.65 = 0.59
		TestEqual(TEXT("スコア: 角度12°・近距離(2000/10000)=0.59"),
			UMSLockOnComponent::ComputeLockOnScore(12.0f, 30.0f, 2000.0f, 10000.0f), 0.59f, KINDA_SMALL_NUMBER);
		// 距離重視(0.65): 近距離オフ軸 > 遠距離正面
		TestTrue(TEXT("スコア: 距離重視で近距離候補が遠距離正面候補に勝つ"),
			UMSLockOnComponent::ComputeLockOnScore(12.0f, 30.0f, 2000.0f, 10000.0f) >
			UMSLockOnComponent::ComputeLockOnScore(0.0f, 30.0f, 8000.0f, 10000.0f));
		// クランプ: 半角超過/最大距離超過は各スコア0
		TestEqual(TEXT("スコア: 半角超過+最大距離超過は0"),
			UMSLockOnComponent::ComputeLockOnScore(20.0f, 30.0f, 12000.0f, 10000.0f), 0.0f, KINDA_SMALL_NUMBER);
	}

	// ------------------------------------------------------------------
	// 2. 視野角30°(半角15°): 円錐内のみ候補
	// ------------------------------------------------------------------
	{
		AMSTeamAgentActor* EnemyAhead = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 3000.0f));
		AMSTeamAgentActor* EnemyOutside = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(25.0f, 3000.0f));

		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("視野角30°: 正面の敵が候補(円錐外25°は選ばれない)"),
			LockOn->GetCurrentCandidate(), Cast<AActor>(EnemyAhead));
		TestFalse(TEXT("視野角30°: dwell前はまだロックしない"), LockOn->IsLockedOn());

		DestroyLockOnActors(World, { EnemyAhead });
		TickLockOn(LockOn, 0.1f);
		TestNull(TEXT("視野角30°: 円錐外(25°)のみ残ると候補なし"), LockOn->GetCurrentCandidate());

		DestroyLockOnActors(World, { EnemyOutside });
	}

	// ------------------------------------------------------------------
	// 3. スコープ中は視野角17°(半角8.5°)へ縮小(P3-5 IsScoped連携)
	// ------------------------------------------------------------------
	{
		AMSTeamAgentActor* Enemy12Deg = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(12.0f, 3000.0f));

		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("通常時: 視野角=30°"), LockOn->GetActiveConeDegrees(), 30.0f);
		TestEqual(TEXT("通常時: 12°オフ軸は候補(半角15°内)"),
			LockOn->GetCurrentCandidate(), Cast<AActor>(Enemy12Deg));

		Camera->SetScoped(true, 12.0f, 0.35f);
		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("スコープ中: 視野角=17°"), LockOn->GetActiveConeDegrees(), 17.0f);
		TestNull(TEXT("スコープ中: 12°オフ軸は候補外(半角8.5°超)"), LockOn->GetCurrentCandidate());

		Camera->SetScoped(false, 0.0f, 0.0f);
		DestroyLockOnActors(World, { Enemy12Deg });
	}

	// ------------------------------------------------------------------
	// 4. スコア選定(ワールド): 近距離オフ軸12° > 遠距離正面(距離重視0.65)
	// ------------------------------------------------------------------
	{
		// 世界距離×MSScale(WorldScale W2)。LockOnRangeも×MSScaleのため距離比とスコアは不変。
		AMSTeamAgentActor* FarCentered = SpawnLockTarget(
			World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 8000.0f * MSGO::MSScale));
		AMSTeamAgentActor* NearOffAxis = SpawnLockTarget(
			World, MSTeam::EnemyTeamId(), true, AtAngle(12.0f, 2000.0f * MSGO::MSScale));

		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("スコア選定: 近距離オフ軸候補が最良(0.59 > 0.48)"),
			LockOn->GetCurrentCandidate(), Cast<AActor>(NearOffAxis));

		DestroyLockOnActors(World, { FarCentered, NearOffAxis });
	}

	// ------------------------------------------------------------------
	// 5. 除外: 味方/中立/非ロック対象(占領拠点・設置物相当)は候補外、
	//    ロック可能な敵対対象(本拠点コア相当)は候補(CanLockOn単一API。B-2/B-5)
	// ------------------------------------------------------------------
	{
		// 配置は正面のコアLOS線(自機胸元→コア)を遮らないよう±12°へ逃がす
		// (機体コライダーもWeaponTraceをBlockする=ロックLOSを遮るため。B-3/B-6)
		AMSTeamAgentActor* Ally = SpawnLockTarget(World, MSTeam::PlayerTeamId(), true, AtAngle(-12.0f, 1000.0f));
		AMSTeamAgentActor* Neutral = SpawnLockTarget(World, FGenericTeamId::NoTeam, true, AtAngle(12.0f, 1200.0f));
		AMSTeamAgentActor* EnemyDeploy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), false, AtAngle(-12.0f, 1300.0f));

		TickLockOn(LockOn, 0.1f);
		TestNull(TEXT("除外: 味方/中立/非ロック対象のみでは候補なし"), LockOn->GetCurrentCandidate());

		// 本拠点コア相当(敵対+CanBeLockedOn=true)は、より近い味方が居ても候補になる
		AMSTeamAgentActor* EnemyCore = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 4000.0f));
		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("本拠点コア相当(敵対・ロック可)は候補になる(B-5)"),
			LockOn->GetCurrentCandidate(), Cast<AActor>(EnemyCore));

		DestroyLockOnActors(World, { Ally, Neutral, EnemyDeploy, EnemyCore });
		TickLockOn(LockOn, 0.1f);
	}

	// ------------------------------------------------------------------
	// 6. LOS遮蔽: 壁(MSStructure)越しの敵は候補から除外(起点=自機胸元。B-5)
	// ------------------------------------------------------------------
	{
		const FVector EnemyLoc = AtAngle(0.0f, 3000.0f);
		AMSTeamAgentActor* Enemy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, EnemyLoc);

		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("LOS: 遮蔽なしなら候補"), LockOn->GetCurrentCandidate(), Cast<AActor>(Enemy));

		// 自機胸元→敵AimPointの線分中点に壁を置く(半径300で確実に遮る)
		const FVector ChestOrigin = Player->GetLockOnAimPoint();
		AActor* Wall = SpawnLockOnWall(World, (ChestOrigin + EnemyLoc) * 0.5f, 300.0f);
		TickLockOn(LockOn, 0.1f);
		TestNull(TEXT("LOS: 壁遮蔽の敵は候補から除外"), LockOn->GetCurrentCandidate());

		DestroyLockOnActors(World, { Enemy, Wall });
	}

	// ------------------------------------------------------------------
	// 6b. LOS割り込み: 他ユニットは透過し、その奥の壁は引き続き遮蔽する
	// ------------------------------------------------------------------
	{
		const FVector EnemyLoc = AtAngle(0.0f, 3000.0f);
		AMSTeamAgentActor* Enemy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, EnemyLoc);
		LockOn->ToggleLockOn();
		TestTrue(TEXT("LOS割り込み: 前提として敵をロック"), LockOn->IsLockedOn());

		// 実際のプレイヤー/BOTと同じAMSUnitBase系Pawnを胸元→対象の線分へ割り込ませる。
		AMSPlayerCharacter* Interloper = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), (Player->GetLockOnAimPoint() + EnemyLoc) * 0.5f,
			FRotator::ZeroRotator, Params);
		Interloper->SetGenericTeamId(MSTeam::PlayerTeamId());
		AddLockOnTestSphere(Interloper, TEXT("InterloperLOSBlocker"), 300.0f);
		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("LOS割り込み: 他ユニットが横切ってもロック維持"),
			LockOn->GetLockedTarget(), Cast<AActor>(Enemy));

		// 割り込みPawnを無視した先に壁がある場合、その壁までは透過してはならない。
		AActor* Wall = SpawnLockOnWall(World,
			FMath::Lerp(Player->GetLockOnAimPoint(), EnemyLoc, 0.75f), 300.0f);
		TickLockOn(LockOn, 0.1f);
		TestFalse(TEXT("LOS割り込み: 他ユニットの奥の壁では正しくロック解除"), LockOn->IsLockedOn());

		DestroyLockOnActors(World, { Enemy, Interloper, Wall });
		TickLockOn(LockOn, 0.1f);
	}

	// ------------------------------------------------------------------
	// 7. dwell 0.8s: 同一候補の注視継続でロック確定、候補切替でリセット
	// ------------------------------------------------------------------
	{
		// 世界距離×MSScale(WorldScale W2)。距離比を維持してスコア序列を変えない。
		AMSTeamAgentActor* EnemyA = SpawnLockTarget(
			World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 5000.0f * MSGO::MSScale));

		TickLockOn(LockOn, 0.5f);
		TestFalse(TEXT("dwell: 0.5sではまだロックしない"), LockOn->IsLockedOn());
		TestTrue(FString::Printf(TEXT("dwell: 進捗が中間値(%.2f)"), LockOn->GetDwellProgress()),
			LockOn->GetDwellProgress() > 0.4f && LockOn->GetDwellProgress() < 0.9f);

		// より高スコアの候補B(近距離)出現 → 候補切替でdwellリセット
		AMSTeamAgentActor* EnemyB = SpawnLockTarget(
			World, MSTeam::EnemyTeamId(), true, AtAngle(5.0f, 1500.0f * MSGO::MSScale));
		TickLockOn(LockOn, 0.1f);
		TestEqual(TEXT("dwell: 高スコア候補Bへ切替"), LockOn->GetCurrentCandidate(), Cast<AActor>(EnemyB));
		TickLockOn(LockOn, 0.5f);
		TestFalse(TEXT("dwell: 切替後0.6s(切替でリセット済み)ではまだロックしない"), LockOn->IsLockedOn());

		TickLockOn(LockOn, 0.3f);
		TestFalse(TEXT("0.8s以上注視しても通常ロックは自動発動しない"), LockOn->IsLockedOn());
		LockOn->ToggleLockOn();
		TestTrue(TEXT("手動トグル: dwell完了候補Bをロック確定"), LockOn->IsLockedOn());
		TestEqual(TEXT("dwell: ロック対象=候補B"), LockOn->GetLockedTarget(), Cast<AActor>(EnemyB));
		TestEqual(TEXT("dwell: ロック後の進捗=1.0"), LockOn->GetDwellProgress(), 1.0f);

		// --- 7b. ロック維持: 照準(円錐)から外れても維持(Unity: ロック後は照準を外しても維持) ---
		const float YawBeforeMove = LocalController->GetControlRotation().Yaw;
		EnemyB->SetActorLocation(AtAngle(60.0f, 1500.0f * MSGO::MSScale)); // 円錐外へ移動(LOSは通っている)
		TickLockOn(LockOn, 0.2f);
		TestEqual(TEXT("維持: 円錐外へ出てもロック維持(遮蔽以外では切れない)"),
			LockOn->GetLockedTarget(), Cast<AActor>(EnemyB));
		const float YawFollowed = FRotator::NormalizeAxis(LocalController->GetControlRotation().Yaw - YawBeforeMove);
		TestTrue(FString::Printf(TEXT("黄色ロックが大きく移動した敵にも有限速度で追従 (%.2f deg)"), YawFollowed),
			YawFollowed > 2.0f && YawFollowed < 20.0f);

		// --- 7c. 自動解除: 遮蔽(壁スポーン)で解除(自機コライダーは無視リスト=自己遮蔽しない) ---
		const FVector ChestOrigin = Player->GetLockOnAimPoint();
		AActor* Wall = SpawnLockOnWall(World, (ChestOrigin + EnemyB->GetActorLocation()) * 0.5f, 400.0f);
		FVector HiddenAimPoint;
		TestFalse(TEXT("黄色ロックは次のTick前でも遮蔽物越しの照準補正を止める"),
			LockOn->GetSoftAimTargetLocation(HiddenAimPoint));
		TickLockOn(LockOn, 0.1f);
		TestFalse(TEXT("自動解除: ロック対象の遮蔽で自動アンロック"), LockOn->IsLockedOn());

		DestroyLockOnActors(World, { EnemyA, EnemyB, Wall });
		TickLockOn(LockOn, 0.1f);
	}

	// ------------------------------------------------------------------
	// 8. トグルロック(Input_LockOn経路)+ロック不可化(死亡相当)での自動解除
	// ------------------------------------------------------------------
	{
		AMSTeamAgentActor* Enemy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 3000.0f));

		MSAudio::ResetRecordedMetaSoundRequest();
		LockOn->ToggleLockOn();
		TestTrue(TEXT("トグル: 押下で即時ロック(dwell不要)"), LockOn->IsLockedOn());
		TestEqual(TEXT("トグル: ロック対象=最良候補"), LockOn->GetLockedTarget(), Cast<AActor>(Enemy));
		const MSAudio::FRecordedMetaSoundRequest& LockAudio = MSAudio::GetRecordedMetaSoundRequest();
		TestEqual(TEXT("ロック確定イベントから音声ヘルパーまで1回到達"), LockAudio.RequestCount, 1);
		TestTrue(TEXT("ロック確定音は距離非依存の2D"), LockAudio.bIs2D);
		TestEqual(TEXT("ロック確定音の周波数"), LockAudio.Frequency, 1450.0f);

		AMSPlayerCharacter* RemotePlayer = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector(0.0f, 1000.0f, 300.0f),
			FRotator::ZeroRotator, Params);
		UMSLockOnComponent* RemoteLockOn = RemotePlayer->GetLockOnComponent();
		AMSTeamAgentActor* RemoteEnemy = SpawnLockTarget(
			World, MSTeam::EnemyTeamId(), true,
			RemotePlayer->GetMSCamera()->GetComponentLocation() +
			RemotePlayer->GetMSCamera()->GetForwardVector() * 3000.0f);
		MSAudio::ResetRecordedMetaSoundRequest();
		RemoteLockOn->ToggleLockOn();
		TestTrue(TEXT("remote pawn lock-on still changes gameplay state"), RemoteLockOn->IsLockedOn());
		TestEqual(TEXT("remote pawn lock-on does not play private audio"),
			MSAudio::GetRecordedMetaSoundRequest().RequestCount, 0);
		DestroyLockOnActors(World, { RemoteEnemy, RemotePlayer });

		LockOn->ToggleLockOn();
		TestFalse(TEXT("トグル: 再押下で解除"), LockOn->IsLockedOn());
		TestEqual(TEXT("トグル: 解除でdwellもリセット(即時再ロックしない)"), LockOn->GetDwellProgress(), 0.0f);

		// 死亡/破壊相当(CanBeLockedOn=false)で自動解除(UMSTeamStatics::CanLockOn経由)
		LockOn->ToggleLockOn();
		TestTrue(TEXT("トグル: 再ロック"), LockOn->IsLockedOn());
		SetLockTargetable(Enemy, false);
		TickLockOn(LockOn, 0.1f);
		TestFalse(TEXT("自動解除: 対象がロック不可(死亡/破壊相当)になったら解除"), LockOn->IsLockedOn());

		DestroyLockOnActors(World, { Enemy });
		TickLockOn(LockOn, 0.1f);
	}

	// ------------------------------------------------------------------
	// 9. ソフトエイム: 基準=実カメラ位置(自機胸元/ピボット基準ではない)
	// ------------------------------------------------------------------
	{
		// カメラは肩越しSocketOffset(Y=55)+後方450のため、胸元基準とはヨーが必ずずれる
		AMSTeamAgentActor* Enemy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, AtAngle(0.0f, 1000.0f));
		LockOn->ToggleLockOn();
		TestTrue(TEXT("ソフトエイム: ロック成立"), LockOn->IsLockedOn());

		FVector AimPoint = FVector::ZeroVector;
		TestTrue(TEXT("ソフトエイム: 目標点を取得できる"), LockOn->GetSoftAimTargetLocation(AimPoint));
		TestEqual(TEXT("ソフトエイム: 目標点=対象のGetLockOnAimPoint"), AimPoint, Enemy->GetLockOnAimPoint());

		FRotator SoftAimRot = FRotator::ZeroRotator;
		TestTrue(TEXT("ソフトエイム: 視線回転を計算できる"), LockOn->ComputeSoftAimLookRotation(SoftAimRot));

		const FRotator FromCamera = (AimPoint - Camera->GetComponentLocation()).Rotation();
		const FRotator FromChest = (AimPoint - Player->GetLockOnAimPoint()).Rotation();
		TestTrue(FString::Printf(TEXT("ソフトエイム: 実カメラ基準の回転と一致(%.3f°)"),
				FMath::Abs(FRotator::NormalizeAxis(SoftAimRot.Yaw - FromCamera.Yaw))),
			SoftAimRot.Equals(FromCamera, 0.01f));
		TestFalse(TEXT("ソフトエイム: 自機胸元基準の回転とは一致しない(ピボット基準禁止の検証)"),
			SoftAimRot.Equals(FromChest, 0.01f));

		LockOn->ClearLockOn();
		DestroyLockOnActors(World, { Enemy });
	}

	UE_LOG(LogNeoMSGO, Log,
		TEXT("[P3-6] ロックオン(視野角/スコア/LOS/dwell/自動解除/除外/ソフトエイム基準)テスト完了"));
	return true;
}

// ------------------------------------------------------------------
// 実プレイ報告バグ「ロックオン中に視点がバグる」回帰テスト:
// ソフトエイムのSetControlRotation(絶対値)とInput_Lookの加算入力が同一フレームで
// 競合しても、ControlRotationが発散/振動せず目標角へ収束することを検証する。
// 修正: ロックオン中はLook入力をLockedLookDamping(Unity: lockedLookDamping=0.25)で減衰。
// ------------------------------------------------------------------
bool FMSLockOnLookInputConflictTest::RunTest(const FString& Parameters)
{
	FLockOnTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0.0f, 0.0f, 300.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("AMSPlayerCharacterのスポーン"), Player))
	{
		return false;
	}
	APlayerController* PC = World->SpawnActor<APlayerController>(
		APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("APlayerControllerのスポーン"), PC))
	{
		return false;
	}
	// SpawnActor alone does not create a local player controller. This test drives
	// local mouse input, so establish the same local ownership as real player login.
	// The isolated world has no LocalPlayer/viewport for the possession-time HUD.
	AddExpectedError(TEXT("CreateWidget"), EAutomationExpectedErrorFlags::Contains, 1);
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	if (!TestTrue(TEXT("Look-input fixture is locally controlled"), Player->IsLocallyControlled()))
	{
		return false;
	}
	PC->SetControlRotation(FRotator::ZeroRotator);

	UMSLockOnComponent* LockOn = Player->GetLockOnComponent();
	UMSCameraComponent* Camera = Player->GetMSCamera();
	if (!TestNotNull(TEXT("UMSLockOnComponent"), LockOn) || !TestNotNull(TEXT("UMSCameraComponent"), Camera))
	{
		return false;
	}

	// ロック前の実効感度 = 基本感度(0.35)
	const float BaseSensitivity = Player->GetLookSensitivityScale();
	TestEqual(TEXT("非ロック時の実効感度は基本感度と一致"),
		Player->GetEffectiveLookSensitivity(), BaseSensitivity, KINDA_SMALL_NUMBER);

	// 正面の敵へトグルロック
	const FVector CamLoc = Camera->GetComponentLocation();
	const FVector EnemyLoc = CamLoc + Camera->GetForwardVector() * 3000.0f;
	AMSTeamAgentActor* Enemy = SpawnLockTarget(World, MSTeam::EnemyTeamId(), true, EnemyLoc);
	LockOn->ToggleLockOn();
	if (!TestTrue(TEXT("ロック成立"), LockOn->IsLockedOn()))
	{
		return false;
	}

	// ロック中の実効感度 = 基本感度 × LockedLookDamping(0.25)。
	// 加算入力系統がソフトエイムと競合しないよう減衰されていること(Input_Look配線の検証)。
	const float LockedSensitivity = Player->GetEffectiveLookSensitivity();
	TestEqual(TEXT("ロック中の実効感度は0.25倍へ減衰(Unity: lockedLookDamping)"),
		LockedSensitivity, BaseSensitivity * 0.25f, KINDA_SMALL_NUMBER);
	TestTrue(TEXT("減衰後も入力は完全遮断ではない(視点の微調整は可能)"), LockedSensitivity > 0.0f);

	// 競合シミュレーション: 毎フレーム「マウス右振り入力(実効感度適用済みの加算)」→
	// 「ソフトエイムTick(SetControlRotationによる絶対値上書き)」を120フレーム繰り返す。
	constexpr float FrameDt = 1.0f / 60.0f;
	constexpr int32 FrameCount = 120;
	constexpr float MouseUnitsPerFrame = 2.0f; // 一定速度でマウスを振り続ける想定
	float MaxAbsYawError = 0.0f;
	float PrevYawError = 0.0f;
	float MaxLateFrameDelta = 0.0f; // 収束後(後半40フレーム)の1フレーム変化量(振動検出)
	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		// 加算入力系統(Input_Look相当。AddControllerYawInputはPlayerTick非駆動の
		// テストワールドでは適用されないため、同じ角度加算をControlRotationへ直接適用)
		FRotator Added = PC->GetControlRotation();
		Added.Yaw += MouseUnitsPerFrame * Player->GetEffectiveLookSensitivity();
		PC->SetControlRotation(Added);

		// 絶対値上書き系統(ソフトエイム)
		TickLockOn(LockOn, FrameDt);

		FRotator TargetRot = FRotator::ZeroRotator;
		LockOn->ComputeSoftAimLookRotation(TargetRot);
		const float YawError = FMath::Abs(FRotator::NormalizeAxis(
			PC->GetControlRotation().Yaw - TargetRot.Yaw));
		MaxAbsYawError = FMath::Max(MaxAbsYawError, YawError);
		if (Frame >= FrameCount - 40)
		{
			MaxLateFrameDelta = FMath::Max(MaxLateFrameDelta, FMath::Abs(YawError - PrevYawError));
		}
		PrevYawError = YawError;
	}

	// 収束範囲: 減衰済み入力とRInterpTo(速度8)の定常偏差は数度以内に収まる。
	// 発散(数十度〜一回転)や振動(フレーム毎に大きく往復)しないことを検証する。
	FRotator FinalTarget = FRotator::ZeroRotator;
	LockOn->ComputeSoftAimLookRotation(FinalTarget);
	const float FinalYawError = FMath::Abs(FRotator::NormalizeAxis(
		PC->GetControlRotation().Yaw - FinalTarget.Yaw));
	TestTrue(FString::Printf(TEXT("最終ヨー偏差が収束範囲内(%.3f° < 5°)"), FinalYawError),
		FinalYawError < 5.0f);
	TestTrue(FString::Printf(TEXT("全期間で発散しない(最大偏差 %.3f° < 15°)"), MaxAbsYawError),
		MaxAbsYawError < 15.0f);
	TestTrue(FString::Printf(TEXT("収束後に振動しない(後半の1フレーム変化 %.4f° < 0.5°)"), MaxLateFrameDelta),
		MaxLateFrameDelta < 0.5f);

	// ピッチも同様に目標へ収束している(ソフトエイムはピッチ/ヨー両方を上書きする)
	const float FinalPitchError = FMath::Abs(FRotator::NormalizeAxis(
		PC->GetControlRotation().Pitch - FinalTarget.Pitch));
	TestTrue(FString::Printf(TEXT("最終ピッチ偏差が収束範囲内(%.3f° < 5°)"), FinalPitchError),
		FinalPitchError < 5.0f);

	LockOn->ClearLockOn();
	DestroyLockOnActors(World, { Enemy });
	UE_LOG(LogNeoMSGO, Log, TEXT("[実プレイ修正] ロックオン中Look入力競合の収束テスト完了"));
	return true;
}

bool FMSRedLockEmergencyDodgeTest::RunTest(const FString& Parameters)
{
	const float StandardRedLockDistance = 30000.0f * MSGO::MSScale;
	TestTrue(TEXT("300 m scaled threshold includes a target immediately inside"),
		UMSLockOnComponent::IsWithinRedLockDistance(StandardRedLockDistance - 1.0f, StandardRedLockDistance));
	TestFalse(TEXT("300 m scaled threshold excludes a target immediately outside"),
		UMSLockOnComponent::IsWithinRedLockDistance(StandardRedLockDistance + 1.0f, StandardRedLockDistance));
	TestFalse(TEXT("zero red-lock distance disables red lock"),
		UMSLockOnComponent::IsWithinRedLockDistance(0.0f, 0.0f));
	FLockOnTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMSPlayerCharacter* Attacker = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	PC->Possess(Attacker);
	PC->SetControlRotation(FRotator::ZeroRotator);
	Attacker->SetGenericTeamId(MSTeam::PlayerTeamId());

	UMSRangedWeaponData* Ranged = NewObject<UMSRangedWeaponData>(Attacker);
	Ranged->RedLockDistance = 4000.0f;
	Attacker->GetWeaponComponent()->InitializeSlots({Ranged});
	UMSLockOnComponent* LockOn = Attacker->GetLockOnComponent();
	const FVector TargetLocation = Attacker->GetMSCamera()->GetComponentLocation()
		+ Attacker->GetMSCamera()->GetForwardVector() * 3000.0f;
	AMSPlayerCharacter* Target = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), TargetLocation, FRotator::ZeroRotator, Params);
	Target->SetGenericTeamId(MSTeam::EnemyTeamId());
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Target);
	Suit->BoostCapacity = 100.0f;
	Suit->BoostConsumptionRate = 20.0f;
	Suit->BoostRechargeRate = 0.0f;
	Target->InitializeUnit(Suit);

	TickLockOn(LockOn, 0.1f);
	LockOn->ToggleLockOn();
	TestTrue(TEXT("赤ロック距離内ではIsRedLocked=true"), LockOn->IsRedLocked());
	Target->SetActorLocation(TargetLocation + FVector(10000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	TickLockOn(LockOn, 0.1f);
	TestFalse(TEXT("距離外へ出ると通常ロックへ戻る"), LockOn->IsRedLocked());
	Target->SetActorLocation(TargetLocation);
	TickLockOn(LockOn, 0.1f);
	TestTrue(TEXT("再接近で赤ロックへ戻る"), LockOn->IsRedLocked());
	AActor* Cover = SpawnLockOnWall(World,
		(Attacker->GetLockOnAimPoint() + Target->GetLockOnAimPoint()) * 0.5f, 300.0f);
	TickLockOn(LockOn, 0.1f);
	FVector OccludedAimPoint;
	FRotator OccludedRotation;
	TestTrue(TEXT("赤ロックの短い遮蔽猶予中は対象だけ保持"), LockOn->IsLockedOn());
	TestFalse(TEXT("壁の奥では赤誘導表示を維持しない"), LockOn->IsRedLocked());
	TestFalse(TEXT("壁の奥へカメラ補正目標を渡さない"),
		LockOn->GetSoftAimTargetLocation(OccludedAimPoint) || LockOn->ComputeSoftAimLookRotation(OccludedRotation));
	TestEqual(TEXT("遮蔽猶予中はLook入力も通常感度へ戻す"),
		Attacker->GetEffectiveLookSensitivity(), Attacker->GetLookSensitivityScale(), KINDA_SMALL_NUMBER);
	World->DestroyActor(Cover);
	TickLockOn(LockOn, 0.1f);
	TestTrue(TEXT("短い遮蔽解除後は同じ対象の赤ロックへ戻る"), LockOn->IsRedLocked());
	TestTrue(TEXT("視線復帰でLook入力のロック減衰も復帰"),
		Attacker->GetEffectiveLookSensitivity() < Attacker->GetLookSensitivityScale());
	Cover = SpawnLockOnWall(World,
		(Attacker->GetLockOnAimPoint() + Target->GetLockOnAimPoint()) * 0.5f, 300.0f);
	TickLockOn(LockOn, 0.4f);
	TestFalse(TEXT("遮蔽猶予を過ぎると赤ロック対象を解除"), LockOn->IsLockedOn());
	World->DestroyActor(Cover);
	LockOn->ToggleLockOn();
	TestTrue(TEXT("遮蔽解除後の再取得で赤ロックが成立"), LockOn->IsRedLocked());

	const FRotator Desired(0, 6, 0);
	const float NormalCorrection = UMSLockOnComponent::ComputeLimitedAimRotation(
		FRotator::ZeroRotator, Desired, 1.0f / 60.0f, 14.0f, 4.0f).Yaw;
	const float RedCorrection = UMSLockOnComponent::ComputeLimitedAimRotation(
		FRotator::ZeroRotator, Desired, 1.0f / 60.0f, 22.4f, 8.0f).Yaw;
	TestTrue(TEXT("赤ロック時の補正角は通常より大きい"), RedCorrection > NormalCorrection);
	const float FarNormalCorrection = UMSLockOnComponent::ComputeLimitedAimRotation(
		FRotator::ZeroRotator, FRotator(0, 20, 0), 1.0f / 60.0f, 14.0f, 4.0f).Yaw;
	const float FarRedCorrection = UMSLockOnComponent::ComputeLimitedAimRotation(
		FRotator::ZeroRotator, FRotator(0, 20, 0), 1.0f / 60.0f, 22.4f, 8.0f).Yaw;
	TestTrue(TEXT("20-degree error still tracks in yellow, with red faster and neither snapping to the target"),
		FarNormalCorrection > 0.0f && FarNormalCorrection < FarRedCorrection && FarRedCorrection < 2.0f);

	const float BeforeBoost = Target->GetBoostSet()->GetBoost();
	TestTrue(TEXT("緊急回避は十分なブーストで発動"), Target->TryEmergencyDodge(FVector::RightVector));
	TestEqual(TEXT("緊急回避は固定量のブーストを消費"), Target->GetBoostSet()->GetBoost(),
		BeforeBoost - AMSPlayerCharacter::EmergencyDodgeBoostCost);
	TestFalse(TEXT("緊急回避で攻撃側の赤ロックだけ解除"), LockOn->IsRedLocked());
	TestTrue(TEXT("緊急回避後も通常ロックは維持"), LockOn->IsLockedOn());
	TestFalse(TEXT("3秒クールタイム中の再発動は拒否"), Target->TryEmergencyDodge(FVector::RightVector));

	AMSPlayerCharacter* LowBoost = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 10000, 300), FRotator::ZeroRotator, Params);
	UMSMobileSuitData* LowSuit = NewObject<UMSMobileSuitData>(LowBoost);
	LowSuit->BoostCapacity = 10.0f;
	LowSuit->BoostRechargeRate = 0.0f;
	LowBoost->InitializeUnit(LowSuit);
	TestFalse(TEXT("残量不足では緊急回避不可"), LowBoost->TryEmergencyDodge(FVector::ForwardVector));
	TestEqual(TEXT("残量不足時はブーストを消費しない"), LowBoost->GetBoostSet()->GetBoost(), 10.0f);

	AMSPlayerCharacter* Overheated = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 15000, 300), FRotator::ZeroRotator, Params);
	UMSMobileSuitData* HotSuit = NewObject<UMSMobileSuitData>(Overheated);
	HotSuit->BoostCapacity = 100.0f;
	HotSuit->BoostRechargeRate = 0.0f;
	Overheated->InitializeUnit(HotSuit);
	Overheated->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Overheated, 1);
	TestFalse(TEXT("OH中は残量があっても緊急回避不可"), Overheated->TryEmergencyDodge(FVector::ForwardVector));

	AMSPlayerCharacter* DoubleTap = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 20000, 300), FRotator::ZeroRotator, Params);
	UMSMobileSuitData* TapSuit = NewObject<UMSMobileSuitData>(DoubleTap);
	TapSuit->BoostCapacity = 100.0f;
	TapSuit->BoostRechargeRate = 0.0f;
	DoubleTap->InitializeUnit(TapSuit);
	DoubleTap->ProcessMovementInputForTesting(FVector2D(0, 1));
	DoubleTap->ProcessMovementInputForTesting(FVector2D::ZeroVector);
	World->Tick(LEVELTICK_All, 0.1f);
	DoubleTap->ProcessMovementInputForTesting(FVector2D(0, 1));
	TestEqual(TEXT("移動キーのダブルタップで固定ブーストを消費して回避"),
		DoubleTap->GetBoostSet()->GetBoost(), 75.0f);

	const UInputAction* DodgeAction = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/Input/Actions/MS/IA_EmergencyDodge.IA_EmergencyDodge"));
	const UInputMappingContext* InputContext = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
	TestNotNull(TEXT("緊急回避InputActionが生成済み"), DodgeAction);
	TestTrue(TEXT("IMC_MSPlayerで緊急回避がXキーに割り当て済み"), InputContext && DodgeAction &&
		InputContext->GetMappings().ContainsByPredicate([DodgeAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == DodgeAction && Mapping.Key == EKeys::X;
		}));

	UMSMeleeWeaponData* Melee = NewObject<UMSMeleeWeaponData>(Attacker);
	Melee->RedLockDistance = 0.0f;
	Attacker->GetWeaponComponent()->InitializeSlots({Melee});
	LockOn->ClearLockOn();
	TickLockOn(LockOn, 0.1f);
	LockOn->ToggleLockOn();
	TestFalse(TEXT("格闘武器(RedLockDistance=0)は常に通常ロック"), LockOn->IsRedLocked());
	return true;
}

// ------------------------------------------------------------------
// ロックオン×リロード連携: 誘導武器はリロード完了までロック不可(捕捉/dwell/確定/手動ロック)。
// 非誘導武器のリロードは通常ロックに影響を与えない(回帰確認)。
// ------------------------------------------------------------------
bool FMSHomingReloadLockGateTest::RunTest(const FString& Parameters)
{
	FLockOnTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMSPlayerCharacter* Attacker = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	PC->Possess(Attacker);
	PC->SetControlRotation(FRotator::ZeroRotator);
	Attacker->SetGenericTeamId(MSTeam::PlayerTeamId());

	UMSHomingWeaponData* Homing = NewObject<UMSHomingWeaponData>(Attacker);
	Homing->CategoryTag = MSTags::Weapon_Category_Homing;
	Homing->MagazineSize = 1;
	Homing->ReserveAmmo = 1;
	Homing->ReloadTime = 1.0f;
	Homing->FireInterval = 0.0f;
	Homing->Range = 30.0f;
	Homing->LockOnRange = 30.0f;
	UMSWeaponComponent* Weapons = Attacker->GetWeaponComponent();
	Weapons->InitializeSlots({Homing});
	UMSLockOnComponent* LockOn = Attacker->GetLockOnComponent();

	const FVector TargetLocation = Attacker->GetMSCamera()->GetComponentLocation()
		+ Attacker->GetMSCamera()->GetForwardVector() * 3000.0f;
	AMSPlayerCharacter* Target = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), TargetLocation, FRotator::ZeroRotator, Params);
	Target->SetGenericTeamId(MSTeam::EnemyTeamId());
	// タスク#220: 誘導捕捉サークルはHUDの円と同じ狭い円錐(FOV60°で全角約16°=半角8.04°)に
	// なった。対象の照準点(胸元)はActor位置より上にあるため、そのままだと視線軸から
	// 約9.2°ずれて円の外になる。照準点そのものを視線軸上へ合わせる。
	Target->SetActorLocation(TargetLocation + (TargetLocation - Target->GetLockOnAimPoint()));

	// 前提: リロード前は通常通り捕捉→dwell→確定できる(0.1s刻みで9ステップ=0.9s > LockOnDwellTime 0.8s)
	for (int32 Step = 0; Step < 9; ++Step)
	{
		TickWeaponAndLockOn(Weapons, LockOn, 0.1f);
	}
	TestEqual(TEXT("リロード前: dwell完了(0.8s)で誘導捕捉が確定する"),
		LockOn->GetHomingCapturedTarget(), Cast<AActor>(Target));

	// 最後の一発を撃って弾切れ→自動リロード開始(本番と同じ経路: ConsumeAmmoAndStartCooldown)
	TestTrue(TEXT("最終弾を消費(自動リロード開始のトリガー)"), Weapons->ConsumeAmmoAndStartCooldown(0));
	TestTrue(TEXT("消費直後はリロード中"), Weapons->IsReloading(0));

	// リロード中は捕捉が即座に解除され、以後dwellが一切進行しない
	TickWeaponAndLockOn(Weapons, LockOn, 0.05f);
	TestNull(TEXT("リロード開始直後: 誘導捕捉は解除される"), LockOn->GetHomingCapturedTarget());
	TestNull(TEXT("リロード中: 誘導候補も選定されない"), LockOn->GetHomingCandidate());
	TestEqual(TEXT("リロード中: dwell進捗は0のまま"), LockOn->GetHomingDwellProgress(), 0.0f);

	TickWeaponAndLockOn(Weapons, LockOn, 0.5f);
	TestTrue(TEXT("リロード継続中であること(前提確認)"), Weapons->IsReloading(0));
	TestNull(TEXT("リロード中(0.55s経過)は捕捉が進行しない"), LockOn->GetHomingCapturedTarget());
	TestEqual(TEXT("リロード中(0.55s経過)もdwell進捗は0のまま"), LockOn->GetHomingDwellProgress(), 0.0f);

	// 誘導武器装備中はリロード完了まで手動ロック(ToggleLockOn)も弾かれる
	LockOn->ToggleLockOn();
	TestFalse(TEXT("リロード中: 手動ロック(ToggleLockOn)は不発"), LockOn->IsLockedOn());

	// リロード完了後は通常通り再スタートできる(捕捉→dwell→確定)
	TickWeaponAndLockOn(Weapons, LockOn, 0.6f); // 合計約1.15s > ReloadTime(1.0s)でリロード完了
	TestFalse(TEXT("リロード完了(前提確認)"), Weapons->IsReloading(0));
	TestNull(TEXT("リロード完了直後はまだ捕捉していない(再度dwellが必要)"), LockOn->GetHomingCapturedTarget());

	TickWeaponAndLockOn(Weapons, LockOn, 0.9f);
	TestEqual(TEXT("リロード完了後: dwell完了で誘導捕捉が再度確定する"),
		LockOn->GetHomingCapturedTarget(), Cast<AActor>(Target));

	// リロード完了後は手動ロックも正常に機能する
	LockOn->ToggleLockOn();
	TestTrue(TEXT("リロード完了後: 手動ロック(ToggleLockOn)が機能する"), LockOn->IsLockedOn());
	LockOn->ClearLockOn();

	// ---- 回帰確認: 非誘導武器のリロードは通常ロックに一切影響しない ----
	UMSRangedWeaponData* Ranged = NewObject<UMSRangedWeaponData>(Attacker);
	Ranged->CategoryTag = MSTags::Weapon_Category_MachineGun;
	Ranged->MagazineSize = 1;
	Ranged->ReserveAmmo = 1;
	Ranged->ReloadTime = 1.0f;
	Ranged->FireInterval = 0.0f;
	Weapons->InitializeSlots({Ranged});
	LockOn->ClearLockOn();

	TestTrue(TEXT("非誘導武器: 最終弾を消費(自動リロード開始)"), Weapons->ConsumeAmmoAndStartCooldown(0));
	TestTrue(TEXT("非誘導武器: 消費直後はリロード中"), Weapons->IsReloading(0));

	TickWeaponAndLockOn(Weapons, LockOn, 0.1f);
	LockOn->ToggleLockOn();
	TestTrue(TEXT("非誘導武器: リロード中でも手動ロックは通常通り機能する(回帰)"), LockOn->IsLockedOn());

	DestroyLockOnActors(World, { Target });
	return true;
}

// ============================================================================
// タスク#220: ミサイル誘導サークル(HUDの円)とロック判定円の一致
// ----------------------------------------------------------------------------
// 旧実装: HomingConeDegrees = LockOnConeDegrees * 2 = 60°固定(半角30°)。
//         1920x1080/FOV60° では半角30°は画面上の半径 960px に相当し、
//         実際に描かれている円の半径 235px の 4.09 倍。つまり円の外どころか
//         画面端の敵にもロックが掛かっていた(ユーザー報告)。
// 新実装: 円錐半角 = atan(円の実描画半径px / 投影焦点距離px)。
//         1920x1080/FOV60° で全角 16.089°(半角 8.044°)。
// ============================================================================
bool FMSHomingCircleProjectionTest::RunTest(const FString& Parameters)
{
	// HUDリングが実際に描く半径(Slate単位)。パレット定数と描画実装が一致していること。
	const float RingRadius = UMSHUDRingWidget::ComputeDrawRadius(
		MSHUDPalette::HomingCursorSize, MSHUDPalette::HomingCursorThickness);
	TestEqual(TEXT("誘導カーソル円の実描画半径 = 480/2 - 10/2 = 235 スレート単位"), RingRadius, 235.0f, 0.001f);
	TestEqual(TEXT("パレット定数 HomingCursorDrawRadius がリング描画実装と一致する"),
		MSHUDPalette::HomingCursorDrawRadius, RingRadius, 0.001f);

	// ---- 1. 投影焦点距離: 16:9 では X/Y どちらの軸拘束でも同値(ピクセルは正方) ----
	const float Focal1080Y = UMSLockOnComponent::ComputeFocalLengthPixels(
		1920.0f, 1080.0f, 60.0f, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
	const float Focal1080X = UMSLockOnComponent::ComputeFocalLengthPixels(
		1920.0f, 1080.0f, 60.0f, 16.0f / 9.0f, AspectRatio_MaintainXFOV);
	TestEqual(TEXT("1920x1080/FOV60°の焦点距離 = 960/tan30° = 1662.77px"), Focal1080Y, 1662.7688f, 0.01f);
	TestEqual(TEXT("16:9ではX軸拘束/Y軸拘束で焦点距離が一致する"), Focal1080X, Focal1080Y, 0.01f);

	// ---- 2. 現行の判定円: 円の描画半径から導かれる全角 ----
	const float Cone1080 = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, Focal1080Y);
	TestEqual(TEXT("1920x1080/FOV60°の誘導判定円錐 = 16.089°(半角8.044°)"), Cone1080, 16.0887f, 0.01f);

	// ---- 3. 旧実装(60°固定)がどれだけ緩かったかを数値で固定する(回帰防止) ----
	const float LegacyRadiusPx = UMSLockOnComponent::ComputeScreenRadiusForConeDegrees(60.0f, Focal1080Y);
	TestEqual(TEXT("旧固定60°は画面半径960px相当(画面半幅ぜんぶ)"), LegacyRadiusPx, 960.0f, 0.01f);
	TestTrue(TEXT("旧判定は描画円の4倍以上外側まで拾っていた"), LegacyRadiusPx > RingRadius * 4.0f);
	TestTrue(TEXT("修正後の判定全角は旧60°より確実に狭い"), Cone1080 < 60.0f);

	// ---- 4. FOV変更(設定#18のスライダー)に判定が追随する ----
	// リングは画面上で固定サイズなので、FOVが広がる=同じピクセル半径がより広い角度に対応する。
	const float Focal90 = UMSLockOnComponent::ComputeFocalLengthPixels(
		1920.0f, 1080.0f, 90.0f, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
	const float Focal30 = UMSLockOnComponent::ComputeFocalLengthPixels(
		1920.0f, 1080.0f, 30.0f, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
	const float Cone90 = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, Focal90);
	const float Cone30 = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, Focal30);
	TestEqual(TEXT("FOV90°では判定全角27.510°"), Cone90, 27.5101f, 0.01f);
	TestEqual(TEXT("FOV30°(スコープ相当)では判定全角7.506°"), Cone30, 7.5055f, 0.01f);
	TestTrue(TEXT("FOVが広いほど判定円錐も広い(単調)"), Cone30 < Cone1080 && Cone1080 < Cone90);

	// 逆変換の往復: どのFOVでも「判定円錐→画面半径」が描画半径に戻る=描画と判定が同一円。
	for (const float FOV : {30.0f, 60.0f, 75.0f, 90.0f, 110.0f})
	{
		const float Focal = UMSLockOnComponent::ComputeFocalLengthPixels(
			1920.0f, 1080.0f, FOV, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
		const float Cone = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, Focal);
		TestEqual(*FString::Printf(TEXT("FOV%.0f°: 判定円錐を画面半径へ戻すと描画半径に一致する"), FOV),
			UMSLockOnComponent::ComputeScreenRadiusForConeDegrees(Cone, Focal), RingRadius, 0.01f);
	}

	// ---- 5. アスペクト比/解像度が変わっても判定と描画が一致し続ける ----
	// 5-a. ウルトラワイド(2560x1080): Y軸拘束なので垂直FOV据え置き=焦点距離不変。
	const float FocalUltraWide = UMSLockOnComponent::ComputeFocalLengthPixels(
		2560.0f, 1080.0f, 60.0f, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
	TestEqual(TEXT("2560x1080(21:9)でも縦基準の焦点距離は1080pと同じ"), FocalUltraWide, Focal1080Y, 0.01f);
	TestEqual(TEXT("21:9でも判定全角は16.089°のまま(円は画面上で同じ大きさ)"),
		UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, FocalUltraWide), Cone1080, 0.01f);

	// 5-b. 高解像度(2560x1440, DPIスケール4/3): リング半径pxも焦点距離も同率で伸びる。
	const float Focal1440 = UMSLockOnComponent::ComputeFocalLengthPixels(
		2560.0f, 1440.0f, 60.0f, 16.0f / 9.0f, AspectRatio_MaintainYFOV);
	const float Cone1440 = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(
		RingRadius * (4.0f / 3.0f), Focal1440);
	TestEqual(TEXT("2560x1440(DPI4/3)でも判定全角は1080pと一致する"), Cone1440, Cone1080, 0.01f);

	// 5-c. X軸拘束のウルトラワイド: 水平FOV固定なので焦点距離は横基準で伸びる。
	//      それでも「描画半径→円錐→描画半径」の往復は保たれる。
	const float FocalUltraX = UMSLockOnComponent::ComputeFocalLengthPixels(
		2560.0f, 1080.0f, 60.0f, 16.0f / 9.0f, AspectRatio_MaintainXFOV);
	const float ConeUltraX = UMSLockOnComponent::ComputeConeDegreesForScreenRadius(RingRadius, FocalUltraX);
	TestEqual(TEXT("X軸拘束21:9でも往復で描画半径へ戻る"),
		UMSLockOnComponent::ComputeScreenRadiusForConeDegrees(ConeUltraX, FocalUltraX), RingRadius, 0.01f);

	return true;
}

bool FMSHomingCircleCaptureBoundaryTest::RunTest(const FString& Parameters)
{
	FLockOnTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMSPlayerCharacter* Attacker = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	PC->Possess(Attacker);
	PC->SetControlRotation(FRotator::ZeroRotator);
	Attacker->SetGenericTeamId(MSTeam::PlayerTeamId());

	UMSHomingWeaponData* Homing = NewObject<UMSHomingWeaponData>(Attacker);
	Homing->CategoryTag = MSTags::Weapon_Category_Homing;
	Homing->MagazineSize = 8;
	Homing->ReserveAmmo = 8;
	Homing->ReloadTime = 1.0f;
	Homing->FireInterval = 0.0f;
	Homing->Range = 60.0f;
	Homing->LockOnRange = 60.0f;
	UMSWeaponComponent* Weapons = Attacker->GetWeaponComponent();
	Weapons->InitializeSlots({Homing});
	UMSLockOnComponent* LockOn = Attacker->GetLockOnComponent();

	// 判定円錐は「HUDに描かれている円」から導出される。ビューポート未生成のテスト環境では
	// フォールバック解像度(1920x1080)が使われるため、FOV60°で約16.09°になる。
	const float ConeDegrees = LockOn->GetHomingConeDegrees();
	const float HalfCone = ConeDegrees * 0.5f;
	AddInfo(FString::Printf(TEXT("誘導判定円錐 = %.4f° (半角 %.4f°)"), ConeDegrees, HalfCone));
	TestTrue(TEXT("判定円錐がHUD円由来の妥当な範囲(10°〜25°)にある"), ConeDegrees > 10.0f && ConeDegrees < 25.0f);
	TestTrue(TEXT("判定円錐は旧固定60°より確実に狭い(=円外ロックの修正)"), ConeDegrees < 60.0f);

	const UMSCameraComponent* Camera = Attacker->GetMSCamera();
	constexpr float TargetDistance = 3000.0f;
	// AimPoint = Actor位置(AMSTeamAgentActor)なので、カメラ前方をヨー回転させた方向へ置けば
	// 「カメラ前方からのなす角 = 指定ヨー角」がそのまま成立する。
	auto PlaceAtYaw = [&](AActor* Actor, const float YawDegrees)
	{
		const FVector Direction = FRotator(0.0f, YawDegrees, 0.0f)
			.RotateVector(Camera->GetForwardVector());
		Actor->SetActorLocation(Camera->GetComponentLocation() + Direction * TargetDistance);
	};

	AMSTeamAgentActor* Target = SpawnLockTarget(
		World, MSTeam::EnemyTeamId(), true, FVector(TargetDistance, 0, 300));

	// 対象を移動した直後の1Tickは、まだ候補として拾われないことがある(空間クエリの更新遅れ)。
	// dwell完了(0.8s)の検証には余裕を持たせて1.2s進める。
	constexpr float CaptureTickSeconds = 1.2f;

	// ---- 1. 回帰: 円の中心(正面)の敵は従来どおり dwell 0.8s で捕捉できる ----
	PlaceAtYaw(Target, 0.0f);
	TickWeaponAndLockOn(Weapons, LockOn, CaptureTickSeconds);
	TestEqual(TEXT("円の中心にいる敵は従来どおり捕捉できる(回帰なし)"),
		LockOn->GetHomingCapturedTarget(), Cast<AActor>(Target));

	// ---- 2. 円の内側ぎりぎり(半角-1.5°)でも捕捉できる ----
	LockOn->ClearLockOn();
	PlaceAtYaw(Target, HalfCone - 1.5f);
	TickWeaponAndLockOn(Weapons, LockOn, CaptureTickSeconds);
	TestEqual(TEXT("円の内側ぎりぎり(半角-1.5°)の敵は捕捉できる"),
		LockOn->GetHomingCapturedTarget(), Cast<AActor>(Target));

	// ---- 3. 円の外側ぎりぎり(半角+1.5°)ではロックが進行しない ----
	//       捕捉済みの敵が円外へ出たら即座に捕捉解除されること。
	PlaceAtYaw(Target, HalfCone + 1.5f);
	TickWeaponAndLockOn(Weapons, LockOn, 0.1f);
	TestNull(TEXT("捕捉済みの敵が円外へ出たら捕捉が解除される"), LockOn->GetHomingCapturedTarget());
	TickWeaponAndLockOn(Weapons, LockOn, 2.0f);
	TestNull(TEXT("円の外側(半角+1.5°)ではdwellが進行せず捕捉できない"),
		LockOn->GetHomingCapturedTarget());
	TestNull(TEXT("円の外側では誘導候補にもならない"), LockOn->GetHomingCandidate());
	TestEqual(TEXT("円の外側ではdwell進捗0のまま"), LockOn->GetHomingDwellProgress(), 0.0f);

	// ---- 4. 本件の再現ケース: 円の外だが旧判定(半角30°)の内側 → ロックされてはならない ----
	PlaceAtYaw(Target, 25.0f);
	TickWeaponAndLockOn(Weapons, LockOn, 2.0f);
	TestNull(TEXT("画面上の円の外(25°)にいる敵にはロックが掛からない(#220の報告ケース)"),
		LockOn->GetHomingCapturedTarget());
	TestNull(TEXT("25°の敵は誘導候補にもならない"), LockOn->GetHomingCandidate());

	// ---- 5. 円内へ戻せば再び捕捉できる(片方向に壊れていないことの確認) ----
	PlaceAtYaw(Target, HalfCone * 0.5f);
	TickWeaponAndLockOn(Weapons, LockOn, CaptureTickSeconds);
	TestEqual(TEXT("円内へ戻せば再び捕捉できる"),
		LockOn->GetHomingCapturedTarget(), Cast<AActor>(Target));

	DestroyLockOnActors(World, { Target });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
