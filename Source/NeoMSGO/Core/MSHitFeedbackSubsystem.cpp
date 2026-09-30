// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSHitFeedbackSubsystem.h"

void UMSHitFeedbackSubsystem::BroadcastHitConfirmed(AActor* Shooter, const bool bIsHeadshot)
{
	OnHitConfirmed.Broadcast(Shooter, bIsHeadshot);
}

void UMSHitFeedbackSubsystem::BroadcastKillConfirmed(AActor* Killer)
{
	OnKillConfirmed.Broadcast(Killer);
}
