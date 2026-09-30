// Copyright NeoMSGO Project. All Rights Reserved.
//
// P0-4 検証: UMSTeamStatics の真理値表テスト(ログ出力ベースの簡易確認を兼ねる)。
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Core.TeamStatics"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Core.TeamStatics"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSTeamAgentActor.h"
#include "NeoMSGO.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTeamStaticsTest,
	"NeoMSGO.Core.TeamStatics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	AMSTeamAgentActor* MakeAgent(const FGenericTeamId TeamId, const bool bLockable)
	{
		// BeginPlay不要の純粋ロジック検証のため、ワールドを介さず生成する
		AMSTeamAgentActor* Actor = NewObject<AMSTeamAgentActor>(GetTransientPackage());
		Actor->SetGenericTeamId(TeamId);
		if (bLockable)
		{
			// テスト用にロック可能扱いへ(本来は本拠点コア/ユニット派生のみ true)
			FProperty* Prop = AMSTeamAgentActor::StaticClass()->FindPropertyByName(TEXT("bLockOnTargetable"));
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue_InContainer(Actor, true);
			}
		}
		return Actor;
	}
}

bool FMSTeamStaticsTest::RunTest(const FString& Parameters)
{
	AMSTeamAgentActor* PlayerUnit  = MakeAgent(MSTeam::PlayerTeamId(), /*bLockable=*/true);
	AMSTeamAgentActor* AllyUnit    = MakeAgent(MSTeam::PlayerTeamId(), /*bLockable=*/true);
	AMSTeamAgentActor* EnemyUnit   = MakeAgent(MSTeam::EnemyTeamId(),  /*bLockable=*/true);
	AMSTeamAgentActor* NeutralPod  = MakeAgent(FGenericTeamId::NoTeam, /*bLockable=*/false);
	AMSTeamAgentActor* EnemyDeploy = MakeAgent(MSTeam::EnemyTeamId(),  /*bLockable=*/false); // 敵設置物(ロック不可)
	AActor*            PlainActor  = NewObject<AActor>(GetTransientPackage());               // 非実装Actor

	// --- GetTeamId ---
	TestEqual(TEXT("GetTeamId: プレイヤー陣営"), UMSTeamStatics::GetTeamId(PlayerUnit).GetId(), MSTeam::TeamPlayer);
	TestEqual(TEXT("GetTeamId: 敵陣営"), UMSTeamStatics::GetTeamId(EnemyUnit).GetId(), MSTeam::TeamEnemy);
	TestTrue(TEXT("GetTeamId: 非実装ActorはNoTeam"), UMSTeamStatics::GetTeamId(PlainActor) == FGenericTeamId::NoTeam);
	TestTrue(TEXT("GetTeamId: nullはNoTeam"), UMSTeamStatics::GetTeamId(nullptr) == FGenericTeamId::NoTeam);

	// --- GetAttitude / グローバルSolver登録 ---
	TestEqual(TEXT("Attitude: 同チーム=Friendly"), UMSTeamStatics::GetAttitude(PlayerUnit, AllyUnit).GetValue(), ETeamAttitude::Friendly);
	TestEqual(TEXT("Attitude: 異チーム=Hostile"), UMSTeamStatics::GetAttitude(PlayerUnit, EnemyUnit).GetValue(), ETeamAttitude::Hostile);
	TestEqual(TEXT("Attitude: 中立絡み=Neutral"), UMSTeamStatics::GetAttitude(PlayerUnit, NeutralPod).GetValue(), ETeamAttitude::Neutral);
	TestEqual(TEXT("Solver: FGenericTeamId::GetAttitude も同一規則(AIPerception経路)"),
		FGenericTeamId::GetAttitude(MSTeam::PlayerTeamId(), FGenericTeamId::NoTeam), ETeamAttitude::Neutral);
	TestEqual(TEXT("Solver: 異チーム=Hostile(AIPerception経路)"),
		FGenericTeamId::GetAttitude(MSTeam::PlayerTeamId(), MSTeam::EnemyTeamId()), ETeamAttitude::Hostile);

	// --- CanDamage (FF判定) ---
	TestTrue(TEXT("CanDamage: 敵対は可"), UMSTeamStatics::CanDamage(PlayerUnit, EnemyUnit));
	TestFalse(TEXT("CanDamage: 味方誤爆は不可(FF)"), UMSTeamStatics::CanDamage(PlayerUnit, AllyUnit));
	TestFalse(TEXT("CanDamage: 敵BOT同士は不可"), UMSTeamStatics::CanDamage(EnemyUnit, EnemyDeploy));
	TestFalse(TEXT("CanDamage: 自分自身は不可"), UMSTeamStatics::CanDamage(PlayerUnit, PlayerUnit));
	TestTrue(TEXT("CanDamage: 中立の攻撃は全員可"), UMSTeamStatics::CanDamage(NeutralPod, PlayerUnit));
	TestTrue(TEXT("CanDamage: 中立対象への攻撃は可"), UMSTeamStatics::CanDamage(PlayerUnit, NeutralPod));
	TestFalse(TEXT("CanDamage: Target無効は不可"), UMSTeamStatics::CanDamage(PlayerUnit, nullptr));

	// --- CanHeal (回復可否) ---
	TestTrue(TEXT("CanHeal: 同チームは可"), UMSTeamStatics::CanHeal(AllyUnit, PlayerUnit));
	TestFalse(TEXT("CanHeal: 敵対は不可"), UMSTeamStatics::CanHeal(EnemyDeploy, PlayerUnit));
	TestTrue(TEXT("CanHeal: 中立ポッドは全許可(プレイヤー)"), UMSTeamStatics::CanHeal(NeutralPod, PlayerUnit));
	TestTrue(TEXT("CanHeal: 中立ポッドは全許可(敵)"), UMSTeamStatics::CanHeal(NeutralPod, EnemyUnit));
	TestFalse(TEXT("CanHeal: Target無効は不可"), UMSTeamStatics::CanHeal(NeutralPod, nullptr));

	// --- CanLockOn (ロック可否) ---
	TestTrue(TEXT("CanLockOn: 敵対ユニットは可"), UMSTeamStatics::CanLockOn(PlayerUnit, EnemyUnit));
	TestFalse(TEXT("CanLockOn: 味方は不可"), UMSTeamStatics::CanLockOn(PlayerUnit, AllyUnit));
	TestFalse(TEXT("CanLockOn: 中立は不可"), UMSTeamStatics::CanLockOn(PlayerUnit, NeutralPod));
	TestFalse(TEXT("CanLockOn: 敵設置物(非ロック対象)は不可"), UMSTeamStatics::CanLockOn(PlayerUnit, EnemyDeploy));
	TestFalse(TEXT("CanLockOn: 非実装Actorは不可"), UMSTeamStatics::CanLockOn(PlayerUnit, PlainActor));
	TestFalse(TEXT("CanLockOn: 自分自身は不可"), UMSTeamStatics::CanLockOn(PlayerUnit, PlayerUnit));

	// --- EMSCaptureOwner 変換 ---
	TestTrue(TEXT("Convert: TeamPlayer→TeamId 1"), UMSTeamStatics::CaptureOwnerToTeamId(EMSCaptureOwner::TeamPlayer) == MSTeam::PlayerTeamId());
	TestTrue(TEXT("Convert: TeamEnemy→TeamId 2"), UMSTeamStatics::CaptureOwnerToTeamId(EMSCaptureOwner::TeamEnemy) == MSTeam::EnemyTeamId());
	TestTrue(TEXT("Convert: Neutral→NoTeam"), UMSTeamStatics::CaptureOwnerToTeamId(EMSCaptureOwner::Neutral) == FGenericTeamId::NoTeam);
	TestEqual(TEXT("Convert: TeamId 1→TeamPlayer"), UMSTeamStatics::TeamIdToCaptureOwner(MSTeam::PlayerTeamId()), EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Convert: NoTeam→Neutral"), UMSTeamStatics::TeamIdToCaptureOwner(FGenericTeamId::NoTeam), EMSCaptureOwner::Neutral);

	// --- SetTeamId ヘルパー(設置物の陣営引き継ぎ B-8) ---
	AMSTeamAgentActor* SpawnedPod = MakeAgent(FGenericTeamId::NoTeam, false);
	TestTrue(TEXT("SetTeamId: 実装Actorへ設定成功"), UMSTeamStatics::SetTeamId(SpawnedPod, MSTeam::PlayerTeamId()));
	TestFalse(TEXT("SetTeamId後: 同陣営ポッドから敵は回復不可"), UMSTeamStatics::CanHeal(SpawnedPod, EnemyUnit));
	TestTrue(TEXT("SetTeamId後: 同陣営ポッドから味方は回復可"), UMSTeamStatics::CanHeal(SpawnedPod, PlayerUnit));
	AddExpectedMessagePlain(TEXT("IGenericTeamAgentInterface"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("SetTeamId: 非実装Actorは失敗"), UMSTeamStatics::SetTeamId(PlainActor, MSTeam::PlayerTeamId()));

	UE_LOG(LogNeoMSGO, Log, TEXT("[P0-4] UMSTeamStatics 真理値表テスト完了 (FF/回復/ロックの単一API検証)"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
