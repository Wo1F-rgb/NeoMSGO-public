// Copyright NeoMSGO Project. All Rights Reserved.
// P4-13: GameplayCueからHUDへ命中確認を橋渡しするGameInstance単位の通知経路。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSHitFeedbackSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FMSOnHitConfirmed, AActor* /*Shooter*/, bool /*bIsHeadshot*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnKillConfirmed, AActor* /*Killer*/);

UCLASS()
class NEOMSGO_API UMSHitFeedbackSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** P8-2のヒットマーカーUIはこのデリゲートへ購読する。 */
	FMSOnHitConfirmed OnHitConfirmed;
	FMSOnKillConfirmed OnKillConfirmed;

	void BroadcastHitConfirmed(AActor* Shooter, bool bIsHeadshot);
	void BroadcastKillConfirmed(AActor* Killer);
};
