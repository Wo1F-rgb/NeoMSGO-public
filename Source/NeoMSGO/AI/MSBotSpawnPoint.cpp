// Copyright NeoMSGO Project. All Rights Reserved.

#include "AI/MSBotSpawnPoint.h"

#include "Components/SceneComponent.h"
#include "Core/MSTeamStatics.h"

AMSBotSpawnPoint::AMSBotSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool AMSBotSpawnPoint::ServesTeam(const FGenericTeamId TeamId) const
{
	if (SpawnTeam == EMSCaptureOwner::Neutral)
	{
		return true;
	}
	return UMSTeamStatics::CaptureOwnerToTeamId(SpawnTeam) == TeamId;
}
