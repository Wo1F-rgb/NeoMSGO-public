// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSTeamAgentActor.h"

AMSTeamAgentActor::AMSTeamAgentActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMSTeamAgentActor::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId AMSTeamAgentActor::GetGenericTeamId() const
{
	return TeamId;
}

bool AMSTeamAgentActor::CanBeLockedOn() const
{
	return bLockOnTargetable;
}
