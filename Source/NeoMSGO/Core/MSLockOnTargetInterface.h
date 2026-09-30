// Copyright NeoMSGO Project. All Rights Reserved.
//
// P0-4: ロックオン対象契約。
// 設計元: docs/UE5_Architecture.md §4.2 強制ルール2
//   - CanLockOn = 「敵対 かつ (ユニット or 本拠点コア)。占領拠点・味方・死亡済みは常にfalse」
//   - 「ユニット or 本拠点コア」は本インターフェースの実装有無で型レベルに表現する。
//     AMSUnitBase / AMSDestructibleTarget(本拠点コア)が実装し、占領拠点(AMSCapturePoint)や
//     設置物(リペアポッド等)は実装しない → UMSTeamStatics::CanLockOn が構造的に false を返す。
//   - 死亡/破壊済みの除外は CanBeLockedOn() の実装側責務(State.Dead 等を参照)。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MSLockOnTargetInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMSLockOnTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class NEOMSGO_API IMSLockOnTargetInterface
{
	GENERATED_BODY()

public:
	/**
	 * 現在ロックオン候補になり得るか。
	 * 死亡済み(State.Dead)・破壊済みは false を返すこと。
	 * 陣営(敵対かどうか)の判定は UMSTeamStatics::CanLockOn 側の責務であり、ここでは行わない。
	 */
	virtual bool CanBeLockedOn() const = 0;

	/**
	 * ロックオン/ソフトエイムの照準基準点(Unity版 Health.AimPoint 相当)。
	 * デフォルトはActor位置。機体は胸元、本拠点コアはコア中心を返すようオーバーライドする。
	 */
	virtual FVector GetLockOnAimPoint() const;
};
