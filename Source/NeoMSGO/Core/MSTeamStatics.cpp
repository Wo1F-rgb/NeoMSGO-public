// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSLockOnTargetInterface.h"
#include "GameFramework/Pawn.h"
#include "NeoMSGO.h"

FGenericTeamId UMSTeamStatics::GetTeamId(const AActor* Actor)
{
	if (!Actor)
	{
		return FGenericTeamId::NoTeam;
	}

	// 1) Actor自身が IGenericTeamAgentInterface を実装(AMSUnitBase / AMSTeamAgentActor 等)
	if (const IGenericTeamAgentInterface* Agent = Cast<const IGenericTeamAgentInterface>(Actor))
	{
		return Agent->GetGenericTeamId();
	}

	// 2) Pawnで自身が非実装の場合、Controller側(AAIController等)へフォールバック
	if (const APawn* Pawn = Cast<const APawn>(Actor))
	{
		if (const IGenericTeamAgentInterface* ControllerAgent = Cast<const IGenericTeamAgentInterface>(Pawn->GetController()))
		{
			return ControllerAgent->GetGenericTeamId();
		}
	}

	// 3) 発射物等の非エージェントActorは Instigator(発射者Pawn)へフォールバック
	if (const APawn* InstigatorPawn = Actor->GetInstigator())
	{
		if (InstigatorPawn != Actor)
		{
			return GetTeamId(InstigatorPawn);
		}
	}

	return FGenericTeamId::NoTeam;
}

ETeamAttitude::Type UMSTeamStatics::SolveAttitude(const FGenericTeamId TeamA, const FGenericTeamId TeamB)
{
	// NoTeam(中立)が絡む関係は常にNeutral。可否(攻撃/回復/ロック)の意味付けは各判定関数で行う。
	if (TeamA == FGenericTeamId::NoTeam || TeamB == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Neutral;
	}
	return (TeamA == TeamB) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
}

TEnumAsByte<ETeamAttitude::Type> UMSTeamStatics::GetAttitude(const AActor* A, const AActor* B)
{
	return SolveAttitude(GetTeamId(A), GetTeamId(B));
}

bool UMSTeamStatics::IsOnTeam(const AActor* Actor, const FGenericTeamId TeamId)
{
	return Actor && GetTeamId(Actor) == TeamId;
}

bool UMSTeamStatics::CanDamage(const AActor* Shooter, const AActor* Target)
{
	if (!Target || Shooter == Target)
	{
		return false;
	}

	const FGenericTeamId ShooterTeam = GetTeamId(Shooter);
	const FGenericTeamId TargetTeam = GetTeamId(Target);

	// 同一の有効チームのみFF(不可)。中立(NoTeam)は撃つ側/撃たれる側とも制限なし。
	if (ShooterTeam != FGenericTeamId::NoTeam && ShooterTeam == TargetTeam)
	{
		return false;
	}
	return true;
}

bool UMSTeamStatics::CanHeal(const AActor* Healer, const AActor* Target)
{
	if (!Target)
	{
		return false;
	}

	const FGenericTeamId HealerTeam = GetTeamId(Healer);

	// 中立ポッドは全許可
	if (HealerTeam == FGenericTeamId::NoTeam)
	{
		return true;
	}
	// それ以外は同チームのみ
	return HealerTeam == GetTeamId(Target);
}

bool UMSTeamStatics::CanLockOn(const AActor* Viewer, const AActor* Target)
{
	if (!Viewer || !Target || Viewer == Target)
	{
		return false;
	}

	// 敵対関係のみ(味方・中立は不可)
	if (SolveAttitude(GetTeamId(Viewer), GetTeamId(Target)) != ETeamAttitude::Hostile)
	{
		return false;
	}

	// ユニット or 本拠点コア(=IMSLockOnTargetInterface実装)のみ。占領拠点・設置物は非実装のため false。
	const IMSLockOnTargetInterface* LockOnTarget = Cast<const IMSLockOnTargetInterface>(Target);
	if (!LockOnTarget)
	{
		return false;
	}

	// 死亡済み・破壊済みの除外は実装側の責務
	return LockOnTarget->CanBeLockedOn();
}

FGenericTeamId UMSTeamStatics::CaptureOwnerToTeamId(const EMSCaptureOwner Owner)
{
	switch (Owner)
	{
	case EMSCaptureOwner::TeamPlayer: return MSTeam::PlayerTeamId();
	case EMSCaptureOwner::TeamEnemy:  return MSTeam::EnemyTeamId();
	case EMSCaptureOwner::Neutral:
	default:                          return FGenericTeamId::NoTeam;
	}
}

EMSCaptureOwner UMSTeamStatics::TeamIdToCaptureOwner(const FGenericTeamId TeamId)
{
	switch (TeamId.GetId())
	{
	case MSTeam::TeamPlayer: return EMSCaptureOwner::TeamPlayer;
	case MSTeam::TeamEnemy:  return EMSCaptureOwner::TeamEnemy;
	default:                 return EMSCaptureOwner::Neutral;
	}
}

bool UMSTeamStatics::SetTeamId(AActor* Actor, const FGenericTeamId TeamId)
{
	if (IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(Actor))
	{
		Agent->SetGenericTeamId(TeamId);
		return true;
	}

	UE_LOG(LogNeoMSGO, Warning,
		TEXT("UMSTeamStatics::SetTeamId: '%s' は IGenericTeamAgentInterface を実装していないため TeamId を設定できません。"),
		*GetNameSafe(Actor));
	return false;
}

void UMSTeamStatics::RegisterAttitudeSolver()
{
	// エンジンデフォルトSolverは「異チーム=全て敵対(NoTeam含む)」のため、
	// 本プロジェクトの規則(NoTeam=Neutral)へ差し替える。
	// これで AIPerception(Affiliationフィルタ)も UMSTeamStatics と同一規則で動作する(B-2)。
	FGenericTeamId::SetAttitudeSolver(&UMSTeamStatics::SolveAttitude);

	UE_LOG(LogNeoMSGO, Log, TEXT("UMSTeamStatics: チーム態度Solverを登録しました (NoTeam=Neutral / Same=Friendly / Diff=Hostile)"));
}

void UMSTeamStatics::UnregisterAttitudeSolver()
{
	// モジュールアンロード/リロード時にゲームモジュール内関数へのポインタが
	// エンジン側(AIModule)に残らないよう、Solverを解除する。
	FGenericTeamId::ResetAttitudeSolver();

	UE_LOG(LogNeoMSGO, Log, TEXT("UMSTeamStatics: チーム態度Solverを解除しました"));
}
