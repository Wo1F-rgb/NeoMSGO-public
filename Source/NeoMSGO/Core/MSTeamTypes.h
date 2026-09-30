// Copyright NeoMSGO Project. All Rights Reserved.
//
// P0-4: チーム/陣営システム基盤 — 陣営IDの正規定義。
// 設計元: docs/UE5_Architecture.md §4.1 / docs/UE5_MIGRATION_PLAN.md B-2
//
// 陣営の正規表現は FGenericTeamId に一本化する。
// Faction(Federation/Principality)は機体データ上のフレーバー属性であり、
// 戦闘上のチームとは独立。TeamId と Faction を混同しないこと。

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"

namespace MSTeam
{
	/** プレイヤー + 味方BOT + 自陣営設置物/拠点コア */
	constexpr uint8 TeamPlayer = 1;

	/** 敵BOT + 敵設置物/拠点コア */
	constexpr uint8 TeamEnemy = 2;

	// 中立 = FGenericTeamId::NoTeam (255)。中立設置物(誰でも使える)に使用。

	/** FGenericTeamId としてのプレイヤー陣営 */
	inline FGenericTeamId PlayerTeamId() { return FGenericTeamId(TeamPlayer); }

	/** FGenericTeamId としての敵陣営 */
	inline FGenericTeamId EnemyTeamId() { return FGenericTeamId(TeamEnemy); }

	/** 戦闘上有効なチーム(Player/Enemy)か。NoTeam等は false */
	inline bool IsValidCombatTeam(const FGenericTeamId TeamId)
	{
		return TeamId.GetId() == TeamPlayer || TeamId.GetId() == TeamEnemy;
	}
}
