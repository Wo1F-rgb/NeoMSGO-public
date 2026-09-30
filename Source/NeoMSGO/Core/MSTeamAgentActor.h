// Copyright NeoMSGO Project. All Rights Reserved.
//
// P0-4: IGenericTeamAgentInterface 実装の共通基底(非Character用)。
// 設計元: docs/UE5_Architecture.md §4.1 / §4.2 強制ルール3
//
// AMSDestructibleTarget(本拠点コア)・AMSRepairPod 等、ACharacterを継承しない
// 陣営所属Actorはこのクラスを基底にする(TeamId保持+Getter/Setterの重複実装を排除)。
// AMSUnitBase は ACharacter 派生のため IGenericTeamAgentInterface を直接実装する。
//
// 設置物の陣営引き継ぎ(B-8)のため、スポーン直後に必ず SetGenericTeamId を呼ぶこと。
// デフォルトは NoTeam(中立)であり「未設定=中立」が観測可能になっている。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "Core/MSLockOnTargetInterface.h"
#include "MSTeamAgentActor.generated.h"

UCLASS()
class NEOMSGO_API AMSTeamAgentActor : public AActor, public IGenericTeamAgentInterface, public IMSLockOnTargetInterface
{
	GENERATED_BODY()

public:
	AMSTeamAgentActor();

	//~ IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	//~ IMSLockOnTargetInterface
	// デフォルト false(設置物はロック不可)。本拠点コア等のロック可能派生のみ true にする。
	virtual bool CanBeLockedOn() const override;

protected:
	/** 所属チーム(1=Player, 2=Enemy, 255=NoTeam)。エディタ配置物はここで指定可能 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Team")
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	/** ロックオン候補になり得るか(死亡/破壊状態の判定は派生クラスで CanBeLockedOn を上書き) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Team")
	bool bLockOnTargetable = false;
};
