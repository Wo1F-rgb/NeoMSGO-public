#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"

class APlayerController;

/** Match affiliation only. Suit provenance and viewer-relative HP/map colors are separate. */
namespace MSFactionPresentation
{
	inline const FLinearColor RebellionColor(0.8f, 0.06f, 0.08f, 1.0f);
	inline const FLinearColor OrderColor(0.05f, 0.32f, 0.82f, 1.0f);
	inline const FLinearColor UnassignedColor(0.5f, 0.5f, 0.5f, 1.0f);

	FGenericTeamId ResolveMatchTeam(const APlayerController* Controller);
	FGenericTeamId OpposingTeam(FGenericTeamId Team);
	const TCHAR* EnglishName(FGenericTeamId Team);
	const TCHAR* JapaneseName(FGenericTeamId Team);
	FLinearColor GaugeColor(FGenericTeamId Team);
}
