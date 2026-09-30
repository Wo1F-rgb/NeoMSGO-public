// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-6 検証: DP閾値到達のよろけ/空中ダウン遷移とDP自然回復のテスト。
//  - 閾値100到達: 接地(IsMovingOnGround)→State.Staggered / 非接地→State.KnockedDown、DPは0リセット
//  - 時限解除: GE_Stagger 1.2s / GE_KnockedDown 2.5s のDuration失効でタグ除去
//  - よろけ中DP追加蓄積なし(実GE_Staggerタグ経由のend-to-end。P2-3のExecution側担保の回帰)
//  - DP自然回復: 最終被弾から2.5s無被弾で40/s回復、被弾で開始遅延リセット
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Combat.DownPoint"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Combat.DownPoint"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSDownPointRegenComponent.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NeoMSGO.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDownPointTest,
	"NeoMSGO.Combat.DownPoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** GE適用・Duration失効・コンポーネントTickにワールドが必要なため、テスト専用の使い捨てGameワールドを作る */
	struct FDownPointTestWorld
	{
		UWorld* World = nullptr;

		FDownPointTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSDownPointTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			// GameMode不在のテストワールドでは UWorld::BeginPlay が bBegunPlay を立てないため、
			// アクター/コンポーネントのBeginPlay(=回復タイマー起動)を明示的にディスパッチする
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FDownPointTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** GEのDuration失効・回復Tickを進める(GASのAutomationテスト慣例パターン)。
	 *  注意: 無名namespaceでもUnityビルドではMSBoostTests.cpp等と同一TUに結合され得るため、
	 *  TickWorldのような汎用名はファイル間で再定義衝突する。ファイル固有の名前を付けること。 */
	void TickDPWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Seconds, Step));
			Seconds -= Step;
			GFrameCounter++;
		}
	}

	/** 攻撃者(陣営のみ必要。P2-3テストと同一パターン) */
	AMSTeamAgentActor* SpawnAttacker(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSTeamAgentActor* Attacker = World->SpawnActor<AMSTeamAgentActor>(
			AMSTeamAgentActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Attacker->SetGenericTeamId(MSTeam::PlayerTeamId());
		return Attacker;
	}

	/**
	 * ACharacterベースのテストユニット(AMSUnitBaseはAbstractのため代替構成)。
	 * TeamはNoTeam(中立)のまま = Player陣営の攻撃者からCanDamage可(UMSTeamStatics規則)。
	 */
	struct FCharacterUnit
	{
		ACharacter* Character = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UMSAttributeSet_Vital* Vital = nullptr;
		UMSDownPointRegenComponent* Regen = nullptr;
	};

	FCharacterUnit SpawnCharacterUnit(UWorld* World, const EMovementMode MovementMode, const FVector& Location)
	{
		FCharacterUnit Unit;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Unit.Character = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);

		Unit.ASC = NewObject<UAbilitySystemComponent>(Unit.Character, TEXT("TestASC"));
		Unit.ASC->RegisterComponent();
		Unit.Vital = NewObject<UMSAttributeSet_Vital>(Unit.Character, TEXT("TestVital"));
		Unit.ASC->AddSpawnedAttribute(Unit.Vital);
		Unit.ASC->InitAbilityActorInfo(Unit.Character, Unit.Character);

		// 実運用ではAMSUnitBaseがデフォルトサブオブジェクトとして搭載(本テストでは手動追加)
		Unit.Regen = NewObject<UMSDownPointRegenComponent>(Unit.Character, TEXT("TestDPRegen"));
		Unit.Regen->RegisterComponent();

		// 接地判定はIsMovingOnGround(=MovementMode)に統一(計画書P2-6ガードレール)
		Unit.Character->GetCharacterMovement()->SetMovementMode(MovementMode);
		return Unit;
	}

	/** GE_Damage適用ヘルパ(実戦経路と同じ SetByCaller+GE_Damage を通す) */
	void ApplyDamage(AMSTeamAgentActor* Attacker, const FCharacterUnit& Target, const float Damage, const float DownPoint)
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Attacker, Attacker);
		const FGameplayEffectContextHandle ContextHandle(Context);

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), ContextHandle, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Damage);
		if (DownPoint > 0.0f)
		{
			Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, DownPoint);
		}

		Target.ASC->ApplyGameplayEffectSpecToTarget(Spec, Target.ASC);
	}
}

bool FMSDownPointTest::RunTest(const FString& Parameters)
{
	FDownPointTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSTeamAgentActor* Attacker = SpawnAttacker(World);
	const float MaxArmor = 5000.0f; // UMSAttributeSet_Vital コンストラクタ既定値(MaxDownPoint=100)

	// --- 1. 接地よろけ: 閾値100到達 → State.Staggered付与+DP=0リセット ---
	{
		const FCharacterUnit Unit = SpawnCharacterUnit(World, MOVE_Walking, FVector(0, 1000, 100));
		ApplyDamage(Attacker, Unit, 100.0f, 60.0f);
		TestEqual(TEXT("接地: 閾値未満(60)ではよろけない"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 0);
		TestEqual(TEXT("接地: DP=60蓄積"), Unit.Vital->GetDownPoint(), 60.0f);

		ApplyDamage(Attacker, Unit, 100.0f, 40.0f); // 60+40=100到達
		TestEqual(TEXT("接地: 閾値100到達でState.Staggered付与"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 1);
		TestEqual(TEXT("接地: 空中ダウンにはならない"), Unit.ASC->GetTagCount(MSTags::State_KnockedDown), 0);
		TestEqual(TEXT("接地: 閾値到達でDPは0リセット"), Unit.Vital->GetDownPoint(), 0.0f);

		// --- よろけ中はDP追加蓄積なし(実GE_Staggerタグでのend-to-end) ---
		ApplyDamage(Attacker, Unit, 100.0f, 50.0f);
		TestEqual(TEXT("よろけ中: ダメージは通る"), Unit.Vital->GetArmor(), MaxArmor - 300.0f);
		TestEqual(TEXT("よろけ中: DP追加蓄積なし"), Unit.Vital->GetDownPoint(), 0.0f);
		TestEqual(TEXT("よろけ中: 再閾値到達しない(重複付与なし)"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 1);

		// --- 時限解除: 1.2s失効 ---
		TickDPWorld(World, 0.9f);
		TestEqual(TEXT("よろけ: 0.9s経過ではまだ解除されない"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 1);
		TickDPWorld(World, 0.5f); // 計1.4s > 1.2s
		TestEqual(TEXT("よろけ: 1.2s経過で時限解除"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 0);
	}

	// --- 2. 空中ダウン: 非接地(MOVE_Falling)で閾値到達 → State.KnockedDown(2.5s) ---
	{
		const FCharacterUnit Unit = SpawnCharacterUnit(World, MOVE_Falling, FVector(0, 2000, 10000));
		ApplyDamage(Attacker, Unit, 100.0f, 100.0f);
		TestEqual(TEXT("空中: 閾値100到達でState.KnockedDown付与"), Unit.ASC->GetTagCount(MSTags::State_KnockedDown), 1);
		TestEqual(TEXT("空中: よろけにはならない"), Unit.ASC->GetTagCount(MSTags::State_Staggered), 0);
		TestEqual(TEXT("空中: DPは0リセット"), Unit.Vital->GetDownPoint(), 0.0f);

		TickDPWorld(World, 2.2f);
		TestEqual(TEXT("空中ダウン: 2.2s経過ではまだ解除されない(よろけ1.2sより長い)"),
			Unit.ASC->GetTagCount(MSTags::State_KnockedDown), 1);
		TickDPWorld(World, 0.5f); // 計2.7s > 2.5s
		TestEqual(TEXT("空中ダウン: 2.5s経過で時限解除"), Unit.ASC->GetTagCount(MSTags::State_KnockedDown), 0);
	}

	// --- 3. DP自然回復: 被弾後2.5sは回復せず、以降40/sで減衰 ---
	{
		const FCharacterUnit Unit = SpawnCharacterUnit(World, MOVE_Walking, FVector(0, 3000, 100));
		ApplyDamage(Attacker, Unit, 100.0f, 50.0f);
		TestEqual(TEXT("回復: 被弾直後 DP=50"), Unit.Vital->GetDownPoint(), 50.0f);

		TickDPWorld(World, 2.0f);
		TestEqual(TEXT("回復: 被弾後2.0s(<2.5s)では回復開始しない"), Unit.Vital->GetDownPoint(), 50.0f);

		TickDPWorld(World, 1.0f); // 計3.0s。回復稼働 ~0.5s → 約 40*0.5=20 回復(Tick境界±1ステップ許容)
		const float DPAfterRegen = Unit.Vital->GetDownPoint();
		TestTrue(FString::Printf(TEXT("回復: 2.5s経過後に40/sで回復開始(DP=%.1f, 期待~30)"), DPAfterRegen),
			DPAfterRegen >= 22.0f && DPAfterRegen <= 38.0f);

		TickDPWorld(World, 1.5f); // 残り<=38は40/sで1.0s以内に0へ
		TestEqual(TEXT("回復: DPは0まで回復(下限クランプ)"), Unit.Vital->GetDownPoint(), 0.0f);
	}

	// --- 4. 被弾で回復開始遅延がリセットされる(DP加算なしのArmor被弾でもリセット) ---
	{
		const FCharacterUnit Unit = SpawnCharacterUnit(World, MOVE_Walking, FVector(0, 4000, 100));
		ApplyDamage(Attacker, Unit, 100.0f, 60.0f);
		TickDPWorld(World, 2.0f);
		TestEqual(TEXT("リセット: 2.0s時点 DP=60のまま"), Unit.Vital->GetDownPoint(), 60.0f);

		ApplyDamage(Attacker, Unit, 100.0f, 0.0f); // DPなしの被弾 → タイマーのみリセット
		TickDPWorld(World, 2.0f); // 再被弾から2.0s(<2.5s)
		TestEqual(TEXT("リセット: 再被弾から2.5s未満は回復しない(合計4.0s経過でも)"), Unit.Vital->GetDownPoint(), 60.0f);

		TickDPWorld(World, 1.0f); // 再被弾から3.0s → 回復開始
		TestTrue(TEXT("リセット: 再被弾から2.5s経過で回復再開"), Unit.Vital->GetDownPoint() < 60.0f);
	}

	// --- 5. 非Characterの保持者は非接地=空中ダウン扱い(フォールバック仕様の文書化) ---
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSTeamAgentActor* Plain = World->SpawnActor<AMSTeamAgentActor>(
			AMSTeamAgentActor::StaticClass(), FVector(0, 5000, 100), FRotator::ZeroRotator, Params);
		Plain->SetGenericTeamId(MSTeam::EnemyTeamId());
		UAbilitySystemComponent* PlainASC = NewObject<UAbilitySystemComponent>(Plain, TEXT("TestASC"));
		PlainASC->RegisterComponent();
		UMSAttributeSet_Vital* PlainVital = NewObject<UMSAttributeSet_Vital>(Plain, TEXT("TestVital"));
		PlainASC->AddSpawnedAttribute(PlainVital);
		PlainASC->InitAbilityActorInfo(Plain, Plain);

		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Attacker, Attacker);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), FGameplayEffectContextHandle(Context), 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, 100.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 100.0f);
		PlainASC->ApplyGameplayEffectSpecToTarget(Spec, PlainASC);

		TestEqual(TEXT("非Character: 閾値到達は空中ダウン扱い"), PlainASC->GetTagCount(MSTags::State_KnockedDown), 1);
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P2-6] DP閾値遷移/時限解除/自然回復テスト完了 (よろけ1.2s/空中ダウン2.5s/2.5s遅延40/s回復)"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
