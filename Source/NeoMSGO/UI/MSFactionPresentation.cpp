#include "UI/MSFactionPresentation.h"

#include "Core/MSTeamStatics.h"
#include "GameFramework/Pawn.h"
#include "Core/MSTeamTypes.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"

FGenericTeamId MSFactionPresentation::ResolveMatchTeam(const APlayerController* Controller)
{
	if (!Controller) return FGenericTeamId::NoTeam;
	if (const AMSPlayerController* MatchController = Cast<AMSPlayerController>(Controller))
	{
		// Online: server admission, including NoTeam before authentication. Offline: selected faction.
		// Do not fall back to a stale pawn or default PlayerState while admission is pending.
		const FGenericTeamId Team = MatchController->GetOnlineAssignedTeamId();
		return MSTeam::IsValidCombatTeam(Team) ? Team : FGenericTeamId::NoTeam;
	}
	if (const AMSPlayerState* State = Controller->GetPlayerState<AMSPlayerState>())
	{
		const FGenericTeamId Team = State->GetSelectedFaction();
		if (MSTeam::IsValidCombatTeam(Team)) return Team;
	}
	const FGenericTeamId Team = UMSTeamStatics::GetTeamId(Controller->GetPawn());
	return MSTeam::IsValidCombatTeam(Team) ? Team : FGenericTeamId::NoTeam;
}

FGenericTeamId MSFactionPresentation::OpposingTeam(const FGenericTeamId Team)
{
	if (Team == MSTeam::PlayerTeamId()) return MSTeam::EnemyTeamId();
	if (Team == MSTeam::EnemyTeamId()) return MSTeam::PlayerTeamId();
	return FGenericTeamId::NoTeam;
}

const TCHAR* MSFactionPresentation::EnglishName(const FGenericTeamId Team)
{
	if (Team == MSTeam::PlayerTeamId()) return TEXT("REBELLION");
	if (Team == MSTeam::EnemyTeamId()) return TEXT("ORDER");
	return TEXT("--");
}

const TCHAR* MSFactionPresentation::JapaneseName(const FGenericTeamId Team)
{
	if (Team == MSTeam::PlayerTeamId()) return TEXT("リベリオン");
	if (Team == MSTeam::EnemyTeamId()) return TEXT("オーダー");
	return TEXT("陣営未選択");
}

FLinearColor MSFactionPresentation::GaugeColor(const FGenericTeamId Team)
{
	if (Team == MSTeam::PlayerTeamId()) return RebellionColor;
	if (Team == MSTeam::EnemyTeamId()) return OrderColor;
	return UnassignedColor;
}
