// Copyright NeoMSGO Project. All Rights Reserved.
//
// B2 (docs/BotDeployment_Design.md): レベル配置用の軽量BOTスポーンマーカー。
// UMSBotSpawnManagerComponent がチーム別に列挙し、この位置を中心とした
// 半径SpawnSearchRadius内でNavMesh射影+オーバーラップ検査を行って配備する。
// マーカーが1つも無いレベルでは AMSHomeBase 位置がフォールバックとして使われる。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "Gameplay/MSCapturePoint.h"
#include "MSBotSpawnPoint.generated.h"

/** Team-tagged spawn marker for the dynamic BOT deployment system. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSBotSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AMSBotSpawnPoint();

	/** Neutral markers serve both teams. */
	UFUNCTION(BlueprintPure, Category = "MS|Spawn")
	EMSCaptureOwner GetSpawnTeam() const { return SpawnTeam; }

	UFUNCTION(BlueprintCallable, Category = "MS|Spawn")
	void SetSpawnTeam(const EMSCaptureOwner InTeam) { SpawnTeam = InTeam; }

	/** True when bots of TeamId may deploy from this marker (Neutral accepts every team). */
	UFUNCTION(BlueprintPure, Category = "MS|Spawn")
	bool ServesTeam(FGenericTeamId TeamId) const;

protected:
	/** 配備対象チーム。Neutral は敵味方どちらのスポーンにも使用できる共用マーカー */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn")
	EMSCaptureOwner SpawnTeam = EMSCaptureOwner::TeamEnemy;
};
