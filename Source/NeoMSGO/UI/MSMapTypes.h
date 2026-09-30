#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"

namespace MSMapPalette
{
	// WorldScale 10x (docs/WorldScale_10x_Plan.md W6): 全世界距離を MSGO::MSScale(=1800/193 ≈9.3264)
	// で統一するルールに従い、旧18m計画時点のフォールバック半幅20500cmへ適用。
	// 実マップ(AMSMapBoundsActor)の実測値設定はW3/W3以降の別ウェーブが担当し、このフォールバックは
	// マップ境界アクタが存在しない/未設定な場合にのみ使われる保守的デフォルト値。
	inline constexpr float WorldHalfExtentCm = 20500.0f * MSGO::MSScale; // ≈191191.7cm
	// 出撃時のフリーデプロイ許容半径(旧30m=3000cm)も同ルールでスケール。
	inline constexpr float FreeDeployRangeCm = 3000.0f * MSGO::MSScale; // ≈27979.3cm
	inline constexpr float DeploymentMapSizePx = 760.0f;
	inline constexpr float FullscreenMapSizePx = 700.0f;
	inline constexpr float EdgeMarginPx = 24.0f;
	inline constexpr float HomeBaseMarkerSizePx = 48.0f;
	inline constexpr float CapturePointMarkerSizePx = 38.0f;
	inline constexpr float RepairPodOuterSizePx = 22.0f;
	inline constexpr float RepairPodHoleSizePx = 10.0f;
	inline constexpr float RepairPackSizePx = 18.0f;
	inline constexpr float AmmoPackSizePx = 16.0f;
	inline constexpr float PollIntervalSeconds = 0.5f;
	inline constexpr int32 MaxRepairPodMarkers = 16;
	inline constexpr int32 MaxRepairPackMarkers = 16;
	inline constexpr int32 MaxAmmoPackMarkers = 16;
	inline constexpr int32 MaxUnitMarkers = 128;

	// ダイバオ地形は Tools/RealWorld/generate_daibao_terrain.py の geo_to_local_m()
	//   local = (geo_south, -geo_east)
	// によって生成されており、ワールド +X = 地理的な南 / ワールド +Y = 地理的な西 になる。
	// つまり地理的な北はワールド Yaw 180°(= -X)、東は Yaw 270°(= -Y)方向。
	// 素直に +X を画面上に描くと地図が180°回転(南が上)してしまうため、地図系はすべて
	// この Yaw を「画面上方向」として扱う。SceneCapture2D のヨー、マーカー投影、
	// プレイヤー矢印の向きはすべてこの定数を基準に揃えること。
	inline constexpr float WorldNorthYawDeg = 180.0f;

	inline const FLinearColor Background(0.015f, 0.025f, 0.04f, 0.96f);
	inline const FLinearColor Grid(0.12f, 0.2f, 0.28f, 0.55f);
	inline const FLinearColor Destroyed(0.2f, 0.2f, 0.2f, 1.0f);
	inline const FLinearColor Friendly(0.3f, 0.6f, 1.0f, 1.0f);
	/** Keep all ally tiers in the blue/cyan team family; 255 denotes a human player. */
	inline FLinearColor FriendlyContactColor(const uint8 BotTier)
	{
		switch (BotTier)
		{
		case 0: return FLinearColor(0.30f, 0.60f, 1.00f, 1.0f);
		case 1: return FLinearColor(0.20f, 0.82f, 0.96f, 1.0f);
		case 2: return FLinearColor(0.34f, 1.00f, 0.72f, 1.0f);
		default: return Friendly;
		}
	}
	inline const FLinearColor Enemy(0.85f, 0.25f, 0.2f, 1.0f);
	inline const FLinearColor Neutral(0.6f, 0.6f, 0.6f, 1.0f);
	inline const FLinearColor Player(0.2f, 0.9f, 0.35f, 1.0f);
	inline const FLinearColor Repair(0.2f, 0.9f, 0.65f, 1.0f);
	inline const FLinearColor Ammo(1.0f, 0.75f, 0.15f, 1.0f);
	inline const FLinearColor Warning(1.0f, 0.28f, 0.18f, 1.0f);
}
