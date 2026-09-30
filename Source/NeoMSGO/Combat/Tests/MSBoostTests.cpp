// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-2 検証: ブースト消費/回復/オーバーヒートのテスト。
//  - GE_InitStats: Data.Init.BoostCapacity でMaxBoost/Boost初期値注入
//  - 消費: ダッシュ入力保持で ConsumptionRate/s 減少(CMC IsConsumingBoost連動)
//  - 枯渇→強制OH: 既存OH回復を継続し80%で解除。通常は接地+待ち後に回復
//  - 強制OH API: UMSCombatStatics::ConsumeBoostOrOverheat(残量不足=全消費+OH、
//    残量ちょうど=Costも0到達でOH。Unity #150)
//  - しゃがみ/よろけ中はブースト入力無視(消費停止・回復は継続。Unity #131)
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Combat.Boost"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Combat.Boost"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSCombatStatics.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NeoMSGO.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostTest,
	"NeoMSGO.Combat.Boost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** GE適用・Duration失効・タイマー駆動にワールドが必要(P2-6テストと同一パターン) */
	struct FBoostTestWorld
	{
		UWorld* World = nullptr;

		FBoostTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBoostTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(100000, 100000, 100));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(0, 0, -100));

			// GameMode不在のテストワールドではBeginPlayを明示ディスパッチ(=BoostComponentタイマー起動)
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FBoostTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** GEのDuration失効・タイマー駆動を進める(GASのAutomationテスト慣例パターン) */
	void TickWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Seconds, Step));
			Seconds -= Step;
			GFrameCounter++;
		}
	}

	/**
	 * 実運用フルスタック(ASC+BoostSet+BoostComponent+UMSCharacterMovementComponent)の
	 * AMSPlayerCharacterをスポーンし、指定のブースト構成でInitializeUnitする。
	 */
	AMSPlayerCharacter* SpawnBoostUnit(UWorld* World, const FVector& Location,
		float Capacity, float ConsumeRate, float RechargeRate, float OverheatPenalty)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Character = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);

		// Controller非所持のCharacterはCMCがControlledCharacterMoveを実行しない
		// (bRunPhysicsWithNoController=false既定)ため、State.Boosting同期
		// (UpdateCharacterStateBeforeMovement)をテストで駆動できるよう有効化する
		Character->GetMSCharacterMovement()->bRunPhysicsWithNoController = true;


		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Character);
		Data->BoostCapacity = Capacity;
		Data->BoostConsumptionRate = ConsumeRate;
		Data->BoostRechargeRate = RechargeRate;
		Data->BoostOverheatPenalty = OverheatPenalty;
		Character->InitializeUnit(Data);
		// Refresh the real floor only AFTER placing the scaled capsule above it.
		// Entering Walking at the old Z=100 cached an initially penetrating floor;
		// teleporting afterward left that cache/base stale for the first simulated steps.
		Character->GetMSCharacterMovement()->SetMovementMode(MOVE_Falling);
		Character->SetActorLocation(FVector(Location.X, Location.Y, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.2f));
		Character->GetMSCharacterMovement()->SetMovementMode(MOVE_Walking);
		return Character;
	}
}

bool FMSBoostTest::RunTest(const FString& Parameters)
{
	FBoostTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	// --- 1. GE_InitStats: Data.Init.BoostCapacity注入(MaxBoost/Boost=容量満タン開始) ---
	{
		AMSPlayerCharacter* Unit = SpawnBoostUnit(World, FVector(0, 1000, 100),
			/*Capacity*/50.0f, /*Consume*/20.0f, /*Recharge*/15.0f, /*Penalty*/3.0f);
		TestEqual(TEXT("初期化: MaxBoost=BoostCapacity(50)"), Unit->GetBoostSet()->GetMaxBoost(), 50.0f);
		TestEqual(TEXT("初期化: Boostは満タンで開始"), Unit->GetBoostSet()->GetBoost(), 50.0f);
	}

	// --- 2. 消費→枯渇→強制OH→80%回復で解除(Unity: HandleBoostState) ---
	{
		AMSPlayerCharacter* Unit = SpawnBoostUnit(World, FVector(0, 2000, 100),
			/*Capacity*/100.0f, /*Consume*/40.0f, /*Recharge*/10.0f, /*Penalty*/2.0f);
		UMSCharacterMovementComponent* MoveComp = Unit->GetMSCharacterMovement();
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();

		// ダッシュ入力保持 → 40/s消費
		MoveComp->SetBoostInputHeld(true);
		TickWorld(World, 1.0f);
		const float BoostAfter1s = Unit->GetBoostSet()->GetBoost();
		TestTrue(FString::Printf(TEXT("消費: 1.0sで40/s消費(Boost=%.1f, 期待~60)"), BoostAfter1s),
			BoostAfter1s >= 52.0f && BoostAfter1s <= 68.0f);
		TestTrue(TEXT("消費: IsConsumingBoost=true"), MoveComp->IsConsumingBoost());
		TestEqual(TEXT("消費: State.Boosting LooseTag付与(§2.2)"), ASC->GetTagCount(MSTags::State_Boosting), 1);
		TestEqual(TEXT("消費: まだOHしていない"), ASC->GetTagCount(MSTags::State_Overheated), 0);

		// 枯渇まで保持(100/40=2.5s) → 0到達で強制OH。
		// ユーザー指定仕様(実プレイ4回目の修正): 0到達直後から回復が始まる(赤ゲージが伸びる)ため、
		// 枯渇後のBoostは0とは限らない(OHタグと80%未満であることを検証する)
		TickWorld(World, 2.0f);
		TestEqual(TEXT("枯渇: State.Overheated付与"), ASC->GetTagCount(MSTags::State_Overheated), 1);
		TestTrue(FString::Printf(TEXT("枯渇直後: 80%%未満(Boost=%.1f)"), Unit->GetBoostSet()->GetBoost()),
			Unit->GetBoostSet()->GetBoost() < 80.0f);
		TestFalse(TEXT("枯渇: OH中はIsBoostDashing=false"), MoveComp->IsBoostDashing());
		TestFalse(TEXT("枯渇: OH中はIsConsumingBoost=false(空押し消費なし)"), MoveComp->IsConsumingBoost());

		// OH中も回復は継続し、80%到達までState.Overheatedは解除されない(時限解除は廃止)
		MoveComp->SetBoostInputHeld(false);
		const float BoostAtOverheat = Unit->GetBoostSet()->GetBoost();
		TickWorld(World, 1.0f);
		const float BoostDuringOH = Unit->GetBoostSet()->GetBoost();
		TestTrue(FString::Printf(TEXT("OH中: 回復は継続する(%.1f → %.1f)"), BoostAtOverheat, BoostDuringOH),
			BoostDuringOH > BoostAtOverheat + 5.0f);
		TestTrue(TEXT("OH中: 80%%未満では解除されない(時限解除なし)"),
			BoostDuringOH >= 80.0f || ASC->GetTagCount(MSTags::State_Overheated) == 1);

		// 80%到達で解除(回復10/sなので十分な時間を回す)
		TickWorld(World, 8.0f);
		TestEqual(TEXT("OH解除: 80%%回復で解除"), ASC->GetTagCount(MSTags::State_Overheated), 0);
		TestTrue(FString::Printf(TEXT("解除時点の残量は80%%以上(Boost=%.1f)"), Unit->GetBoostSet()->GetBoost()),
			Unit->GetBoostSet()->GetBoost() >= 80.0f);
	}

	// --- 3. 強制OH API: ConsumeBoostOrOverheat(Unity: ConsumeBoost #150。D格P4-7用) ---
	{
		AMSPlayerCharacter* Unit = SpawnBoostUnit(World, FVector(0, 3000, 100),
			/*Capacity*/100.0f, /*Consume*/20.0f, /*Recharge*/0.0f, /*Penalty*/3.0f);
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();

		// 残量十分: 通常消費、OHなし
		TestTrue(TEXT("即時消費: 残量十分(100>=30)はOHなしで成功"),
			UMSCombatStatics::ConsumeBoostOrOverheat(ASC, 30.0f));
		TestEqual(TEXT("即時消費: 30消費でBoost=70"), Unit->GetBoostSet()->GetBoost(), 70.0f);
		TestEqual(TEXT("即時消費: OHしない"), ASC->GetTagCount(MSTags::State_Overheated), 0);

		// 残量不足: 残り全消費+強制OH
		TestFalse(TEXT("残量不足: 70<100はfalse(強制OH)"),
			UMSCombatStatics::ConsumeBoostOrOverheat(ASC, 100.0f));
		TestEqual(TEXT("残量不足: 残り全消費でBoost=0"), Unit->GetBoostSet()->GetBoost(), 0.0f);
		TestEqual(TEXT("残量不足: State.Overheated付与"), ASC->GetTagCount(MSTags::State_Overheated), 1);
	}

	// --- 4. 残量ちょうど=Costも0到達で強制OH(Unity: currentBoost<=0規則) ---
	{
		AMSPlayerCharacter* Unit = SpawnBoostUnit(World, FVector(0, 4000, 100),
			/*Capacity*/30.0f, /*Consume*/20.0f, /*Recharge*/0.0f, /*Penalty*/3.0f);
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();

		TestFalse(TEXT("等値消費: 30==30も0到達で強制OH"),
			UMSCombatStatics::ConsumeBoostOrOverheat(ASC, 30.0f));
		TestEqual(TEXT("等値消費: Boost=0"), Unit->GetBoostSet()->GetBoost(), 0.0f);
		TestEqual(TEXT("等値消費: State.Overheated付与"), ASC->GetTagCount(MSTags::State_Overheated), 1);
	}

	// --- 5. しゃがみ/よろけ中はブースト入力無視・回復は継続(Unity #131) ---
	{
		AMSPlayerCharacter* Unit = SpawnBoostUnit(World, FVector(0, 5000, 100),
			/*Capacity*/100.0f, /*Consume*/40.0f, /*Recharge*/10.0f, /*Penalty*/3.0f);
		UMSCharacterMovementComponent* MoveComp = Unit->GetMSCharacterMovement();
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();

		// This case measures one grounded second, not spawn/teleport landing time.
		TickWorld(World, 0.3f);
		TestTrue(TEXT("回復測定前: 実際の床で接地が安定"), MoveComp->IsMovingOnGround() && MoveComp->CurrentFloor.IsWalkableFloor());
		TestTrue(TEXT("回復測定前: 床距離は通常の接地範囲"), MoveComp->CurrentFloor.FloorDist >= 0.0f && MoveComp->CurrentFloor.FloorDist <= 3.0f);
		TestTrue(TEXT("回復測定前: 鉛直移動なし"), FMath::IsNearlyZero(MoveComp->Velocity.Z));
		TestEqual(TEXT("回復測定前: 残量満タン"), Unit->GetBoostSet()->GetBoost(), 100.0f);

		// 事前に50消費(OHなし)して回復余地を作る
		UMSCombatStatics::ConsumeBoostOrOverheat(ASC, 50.0f);
		TestEqual(TEXT("前提: Boost=50"), Unit->GetBoostSet()->GetBoost(), 50.0f);

		// しゃがみ中: ダッシュ入力を保持しても消費されず、回復が継続する
		ASC->SetLooseGameplayTagCount(MSTags::State_Crouching, 1);
		MoveComp->SetBoostInputHeld(true);
		TestFalse(TEXT("しゃがみ中: IsBoostDashing=false"), MoveComp->IsBoostDashing());
		TestFalse(TEXT("しゃがみ中: IsConsumingBoost=false"), MoveComp->IsConsumingBoost());
		TickWorld(World, 1.0f);
		const float BoostCrouch = Unit->GetBoostSet()->GetBoost();
		// Existing charge 10/15 => normal recovery 46.667/s after provisional 0.25s wait.
		// Timer first-ground observation may conservatively discard one 0.1s interval.
		TestTrue(TEXT("しゃがみ回復は実際に接地した機体で検証"), MoveComp->IsMovingOnGround());
		TestTrue(FString::Printf(TEXT("しゃがみ中: 待ち後46.667/s回復(Boost=%.1f, 期待80..85)"), BoostCrouch),
			BoostCrouch >= 77.0f && BoostCrouch <= 89.0f);
		ASC->SetLooseGameplayTagCount(MSTags::State_Crouching, 0);

		// よろけ中: 同様に入力無視・回復継続
		ASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 1);
		TestFalse(TEXT("よろけ中: IsConsumingBoost=false"), MoveComp->IsConsumingBoost());
		TickWorld(World, 0.5f);
		const float BoostStagger = Unit->GetBoostSet()->GetBoost();
		TestTrue(FString::Printf(TEXT("よろけ中: 消費なし+回復継続(Boost=%.1f > %.1f)"), BoostStagger, BoostCrouch),
			BoostStagger >= BoostCrouch);
		ASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 0);

		// タグ解除後: 入力保持のままなら消費再開
		TickWorld(World, 1.0f);
		TestTrue(TEXT("タグ解除後: 消費再開(Boost減少)"), Unit->GetBoostSet()->GetBoost() < BoostStagger);
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P3-2] ブースト消費/回復/OH遷移/強制OH API/しゃがみ・よろけ入力無視テスト完了"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
