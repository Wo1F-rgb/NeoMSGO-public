// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-3 検証: UMSDamageExecution の計算手順(§1.6 6ステップ)テスト。
// FF判定 / SplashFalloff / HS×1.5 / 盾正面65°×0.15 / Armor減算(最低1) / DP同時加算 /
// よろけ中DP0 / 死亡遷移の単一性(GE_Dead一回のみ)を検証する。
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Combat.DamageExecution"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Combat.DamageExecution"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSShieldComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "NeoMSGO.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDamageExecutionTest,
	"NeoMSGO.Combat.DamageExecution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHostedRoomPreparationDamageTest,
	"NeoMSGO.Combat.DamageExecution.HostedRoomPreparation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** GE適用にはワールド登録済みASCが必要なため、テスト専用の使い捨てGameワールドを作る */
	struct FDamageExecTestWorld
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;

		explicit FDamageExecTestWorld(const bool bHostedRoom = false)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSDamageExecutionTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			if (bHostedRoom)
			{
				GameInstance = NewObject<UGameInstance>(GEngine);
				WorldContext.OwningGameInstance = GameInstance;
				World->SetGameInstance(GameInstance);
				GameInstance->Init();
			}
			FURL URL;
			if (bHostedRoom)
			{
				URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
				URL.AddOption(TEXT("RoomHost=1"));
				World->SetGameMode(URL);
				if (AMSGameMode* Mode = World->GetAuthGameMode<AMSGameMode>())
					Mode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			}
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			if (bHostedRoom)
				if (AMSGameMode* Mode = World->GetAuthGameMode<AMSGameMode>()) Mode->StartPlay();
		}

		~FDamageExecTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				if (GameInstance) GameInstance->Shutdown();
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** 陣営+ASC+VitalSetを持つ最小テストユニット(AMSUnitBaseはAbstractのため代替構成) */
	struct FDamageExecTestUnit
	{
		AMSPlayerCharacter* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UMSAttributeSet_Vital* Vital = nullptr;
		UMSShieldComponent* Shield = nullptr;
	};

	FDamageExecTestUnit SpawnUnit(UWorld* World, const FGenericTeamId TeamId, const FVector& Location, const FRotator& Rotation,
		const float ShieldDurability = 0.0f)
	{
		FDamageExecTestUnit Unit;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Unit.Actor = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), Location, Rotation, Params);
		Unit.Actor->SetGenericTeamId(TeamId);
		Unit.Actor->SetActorLocationAndRotation(Location, Rotation);

		// Use the actor's production ASC/Vital pair. Creating a second transient
		// pair here makes GAS execute against one pair while the assertions read
		// the other, which hides the real damage result behind unchanged 5000.
		Unit.ASC = Unit.Actor->GetAbilitySystemComponent();
		Unit.Vital = Unit.Actor->GetVitalSet();
		if (Unit.ASC)
		{
			Unit.ASC->InitAbilityActorInfo(Unit.Actor, Unit.Actor);
		}
		if (Unit.Vital)
		{
			Unit.Vital->SetMaxArmor(5000.0f);
			Unit.Vital->SetArmor(5000.0f);
		}
		Unit.Shield = Unit.Actor->GetShieldComponent();
		if (ShieldDurability > 0.0f && Unit.Shield)
		{
			UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Unit.Actor, TEXT("NativeShieldSuit"));
			UMSShieldWeaponData* ShieldData = NewObject<UMSShieldWeaponData>(Suit, TEXT("NativeShieldData"));
			ShieldData->MaxDurability = ShieldDurability;
			Suit->NativeShield = ShieldData;
			Unit.Shield->InitializeFromMobileSuit(Suit);
		}

		return Unit;
	}

	constexpr float NoAttackerLocation = TNumericLimits<float>::Max();

	/** GE_Damage適用ヘルパ(実戦経路と同じ SetByCaller+FMSGameplayEffectContext を通す) */
	void ApplyDamage(const FDamageExecTestUnit& Source, const FDamageExecTestUnit& Target, const float Damage, const float DownPoint,
		const bool bHeadshot = false, const float SplashFalloff = -1.0f, const bool bExplosive = false,
		const FVector& AttackerLocation = FVector(NoAttackerLocation))
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Source.Actor, Source.Actor);
		Context->SetHeadshot(bHeadshot);
		if (AttackerLocation.X != NoAttackerLocation)
		{
			Context->SetAttackerLocation(AttackerLocation);
		}
		const FGameplayEffectContextHandle ContextHandle(Context);

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), ContextHandle, 1.0f);
		if (bExplosive)
		{
			Spec = FGameplayEffectSpec(GetDefault<UGE_SplashDamage>(), ContextHandle, 1.0f);
		}
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Damage);
		if (DownPoint > 0.0f)
		{
			Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, DownPoint);
		}
		if (SplashFalloff > 0.0f)
		{
			Spec.SetSetByCallerMagnitude(MSTags::Data_SplashFalloff, SplashFalloff);
		}

		Source.ASC->ApplyGameplayEffectSpecToTarget(Spec, Target.ASC);
	}
}

bool FMSDamageExecutionTest::RunTest(const FString& Parameters)
{
	FDamageExecTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	// --- Context差し替えの配線確認(DefaultGame.ini → UMSAbilitySystemGlobals) ---
	{
		TUniquePtr<FGameplayEffectContext> Allocated(UAbilitySystemGlobals::Get().AllocGameplayEffectContext());
		TestTrue(TEXT("Globals: AllocGameplayEffectContext が FMSGameplayEffectContext を返す(ini配線)"),
			Allocated->GetScriptStruct()->IsChildOf(FMSGameplayEffectContext::StaticStruct()));
	}

	const FDamageExecTestUnit Attacker = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector::ZeroVector, FRotator::ZeroRotator);
	const float MaxArmor = 5000.0f; // UMSAttributeSet_Vital コンストラクタ既定値

	// --- 手順1: FF判定(同陣営はDamage=0/DP=0) ---
	{
		const FDamageExecTestUnit Ally = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Ally, 1000.0f, 30.0f);
		TestEqual(TEXT("FF: 味方へのダメージは0(Armor不変)"), Ally.Vital->GetArmor(), MaxArmor);
		TestEqual(TEXT("FF: 味方へのDPも0"), Ally.Vital->GetDownPoint(), 0.0f);
	}

	// --- 基本: Armor減算+DP同時加算 ---
	{
		const FDamageExecTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Enemy, 1000.0f, 30.0f);
		TestEqual(TEXT("基本: Armor 5000-1000=4000"), Enemy.Vital->GetArmor(), 4000.0f);
		TestEqual(TEXT("基本: DP同時加算 0+30=30"), Enemy.Vital->GetDownPoint(), 30.0f);
	}

	// --- 手順2: SplashFalloff(P2-4接続点) ---
	{
		const FDamageExecTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Enemy, 1000.0f, 0.0f, false, /*SplashFalloff=*/0.5f);
		TestEqual(TEXT("爆風: falloff 0.5 → 500ダメージ"), Enemy.Vital->GetArmor(), MaxArmor - 500.0f);
	}

	// --- 手順3: HS×1.5(P2-5接続点) ---
	{
		const FDamageExecTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Enemy, 1000.0f, 0.0f, /*bHeadshot=*/true);
		TestEqual(TEXT("HS: 1000×1.5=1500ダメージ"), Enemy.Vital->GetArmor(), MaxArmor - 1500.0f);
	}

	// --- 手順4: 機体固有盾(正面吸収/背面透過/DP/破壊/爆風チップ) ---
	{
		const FDamageExecTestUnit Guarding = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 5000.0f);
		ApplyDamage(Attacker, Guarding, 1000.0f, 30.0f, false, -1.0f, false, FVector(1000, 0, 0));
		TestEqual(TEXT("盾: 正面0°は全量を盾が吸収"), Guarding.Vital->GetArmor(), MaxArmor);
		TestEqual(TEXT("盾: DPは85%"), Guarding.Vital->GetDownPoint(), 25.5f);
		TestEqual(TEXT("盾: 耐久1000消費"), Guarding.Shield->GetCurrentDurability(), 4000.0f);

		const FDamageExecTestUnit Guarding60 = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 5000.0f);
		ApplyDamage(Attacker, Guarding60, 1000.0f, 0.0f, false, -1.0f, false, FVector(500.0f, 866.0f, 0.0f));
		TestEqual(TEXT("盾: 60°(65°以内)も吸収"), Guarding60.Shield->GetCurrentDurability(), 4000.0f);

		const FDamageExecTestUnit Guarding70 = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 5000.0f);
		ApplyDamage(Attacker, Guarding70, 1000.0f, 0.0f, false, -1.0f, false, FVector(342.0f, 940.0f, 0.0f));
		TestEqual(TEXT("盾: 70°(65°超)は素通し"), Guarding70.Vital->GetArmor(), MaxArmor - 1000.0f);

		const FDamageExecTestUnit Guarding2 = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 5000.0f);
		ApplyDamage(Attacker, Guarding2, 1000.0f, 0.0f, false, -1.0f, false, FVector(-1000, 0, 0));
		TestEqual(TEXT("盾: 背面は素通し"), Guarding2.Vital->GetArmor(), MaxArmor - 1000.0f);
		ApplyDamage(Attacker, Guarding2, 1000.0f, 0.0f);
		TestEqual(TEXT("盾: AttackerLocation不明経路は素通し"), Guarding2.Vital->GetArmor(), MaxArmor - 2000.0f);

		const FDamageExecTestUnit Broken = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 1000.0f);
		ApplyDamage(Attacker, Broken, 1000.0f, 0.0f, false, -1.0f, false, FVector(1000, 0, 0));
		TestTrue(TEXT("盾: 破壊フラグ"), Broken.Shield->IsShieldBroken());
		TestEqual(TEXT("盾: 破壊ヒットも本体へ溢れない"), Broken.Vital->GetArmor(), MaxArmor);
		ApplyDamage(Attacker, Broken, 1000.0f, 0.0f, false, -1.0f, false, FVector(1000, 0, 0));
		TestEqual(TEXT("盾: 破壊後の次弾は本体へ通る"), Broken.Vital->GetArmor(), MaxArmor - 1000.0f);

		const FDamageExecTestUnit Blast = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector,
			FRotator::ZeroRotator, 5000.0f);
		ApplyDamage(Attacker, Blast, 1000.0f, 0.0f, false, 1.0f, true, FVector(1000, 0, 0));
		TestEqual(TEXT("盾: 爆風は10%だけ本体へchip"), Blast.Vital->GetArmor(), MaxArmor - 100.0f);
	}

	// --- 手順5: 最低ダメージ1(max(1, round)) ---
	{
		const FDamageExecTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Enemy, 0.4f, 0.0f);
		TestEqual(TEXT("最低保証: round(0.4)=0でも1ダメージ"), Enemy.Vital->GetArmor(), MaxArmor - 1.0f);
	}

	// --- 手順6: よろけ中はDP蓄積0(蓄積無敵猶予。P2-6接続点) ---
	{
		const FDamageExecTestUnit Staggered = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		Staggered.ASC->AddLooseGameplayTag(MSTags::State_Staggered);
		ApplyDamage(Attacker, Staggered, 1000.0f, 30.0f);
		TestEqual(TEXT("よろけ中: ダメージは通る"), Staggered.Vital->GetArmor(), 4000.0f);
		TestEqual(TEXT("よろけ中: DP蓄積は0"), Staggered.Vital->GetDownPoint(), 0.0f);

		const FDamageExecTestUnit KnockedDown = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		KnockedDown.ASC->AddLooseGameplayTag(MSTags::State_KnockedDown);
		ApplyDamage(Attacker, KnockedDown, 1000.0f, 30.0f);
		TestEqual(TEXT("空中ダウン中: DP蓄積は0"), KnockedDown.Vital->GetDownPoint(), 0.0f);
	}

	// --- 死亡遷移: 生存→死亡の1回のみ(死体多重ヒットで再遷移しない。P2-2側責務の回帰確認) ---
	{
		const FDamageExecTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(500, 0, 0), FRotator::ZeroRotator);
		ApplyDamage(Attacker, Enemy, 6000.0f, 0.0f);
		TestEqual(TEXT("死亡: Armorは0でクランプ"), Enemy.Vital->GetArmor(), 0.0f);
		TestEqual(TEXT("死亡: State.Dead付与は1回"), Enemy.ASC->GetTagCount(MSTags::State_Dead), 1);

		ApplyDamage(Attacker, Enemy, 6000.0f, 0.0f); // 死体への多重ヒット
		TestEqual(TEXT("死体多重ヒット: State.Deadが重複付与されない(キル通知も1回)"),
			Enemy.ASC->GetTagCount(MSTags::State_Dead), 1);
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P2-3] UMSDamageExecution 計算手順テスト完了 (FF/爆風/HS/盾/最低1/DP/死亡単一遷移)"));
	return true;
}

bool FMSHostedRoomPreparationDamageTest::RunTest(const FString&)
{
	FDamageExecTestWorld Scope(/*bHostedRoom=*/true);
	AMSGameMode* Mode = Scope.World->GetAuthGameMode<AMSGameMode>();
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("hosted damage test has GameMode"), Mode) ||
		!TestNotNull(TEXT("hosted damage test has GameState"), State)) return false;
	TestTrue(TEXT("hosted room is preparing before any sortie"), Mode->IsWaitingForRoomDeployment());
	const FDamageExecTestUnit Shooter = SpawnUnit(Scope.World, MSTeam::PlayerTeamId(),
		FVector(0, 0, 0), FRotator::ZeroRotator);
	const FDamageExecTestUnit Victim = SpawnUnit(Scope.World, MSTeam::EnemyTeamId(),
		FVector(500, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("shooter has ASC"), Shooter.ASC) ||
		!TestNotNull(TEXT("victim has VitalSet"), Victim.Vital)) return false;
	ApplyDamage(Shooter, Victim, 800.0f, 0.0f);
	TestEqual(TEXT("real GAS damage cannot change Armor during briefing"),
		Victim.Vital->GetArmor(), 5000.0f);
	TestEqual(TEXT("pre-sortie attack cannot change battle gauge"),
		State->EnemyGauge, State->MaxGauge);
	Mode->NotifyRoomDeployment();
	TestFalse(TEXT("first sortie removes the preparation damage gate"),
		Mode->IsWaitingForRoomDeployment());
	ApplyDamage(Shooter, Victim, 800.0f, 0.0f);
	TestTrue(TEXT("real GAS damage resumes after first sortie"),
		Victim.Vital->GetArmor() < 5000.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
