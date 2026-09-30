#pragma once

#include "CoreMinimal.h"
#include "UI/MSFactionPresentation.h"
#include "Core/MSScale.h"
#include "GameplayTagContainer.h"
#include "MSHUDTypes.generated.h"

USTRUCT()
struct NEOMSGO_API FMSHUDIconStyle
{
	GENERATED_BODY()
	FLinearColor Color = FLinearColor::White;
	/** 旧略号(MG/BR等)。ツールチップ/テスト用に保持。表示はシルエット(UMSWeaponIconWidget)へ移行済み */
	FString Abbreviation;
	/** シルエット選択用の武器カテゴリ */
	FGameplayTag Category;
};

/** Reference HUD trigger-mode badge derived from existing weapon category data. */
enum class EMSHUDFireMode : uint8
{
	SemiAutomatic,
	FullAutomatic,
	AllPurpose
};

struct NEOMSGO_API FMSHUDKillLogEntry
{
	FText Text;
	FLinearColor Color = FLinearColor::White;
	float RemainingLifetime = 0.0f;
};

namespace MSHUDPalette
{
	inline constexpr float DPWarningThreshold = 0.70f;
	inline constexpr float WeaponSlotHeight = 48.0f;
	inline constexpr float WeaponSlotSpacing = 4.0f;
	// 武器カード内テキストのフォントサイズ。UMG既定(24pt)のままだと17-20px枠を食み出して
	// 武器名/アイコン/残弾が互いに重なるため、枠高に収まる値を明示する(実プレイ報告バグ対策)。
	inline constexpr float WeaponNameFontSize = 12.0f;
	inline constexpr float WeaponIconFontSize = 11.0f;
	inline constexpr float WeaponStatusFontSize = 11.0f;
	inline constexpr float LockOnMarkerSize = 64.0f;
	inline constexpr float LockOnMarkerOuterThickness = 2.5f;
	inline constexpr float LockOnMarkerInnerThickness = 1.5f;
	inline constexpr float AmmoArcDiameter = 190.0f;
	inline constexpr float AmmoArcDegrees = 110.0f;
	inline constexpr float AmmoArcThickness = 5.0f;
	inline constexpr int32 AmmoArcSegments = 32;
	inline constexpr float HomingCursorSize = 480.0f;
	inline constexpr float HomingCursorThickness = 10.0f;
	/**
	 * 誘導カーソル円の「実際に描かれる」半径(Slate単位=DPIスケール前)。
	 * UMSHUDRingWidget::ComputeDrawRadius(HomingCursorSize, HomingCursorThickness) と同一値であることを
	 * NeoMSGO.UI.HUD.HomingCircleLockParity が保証する。
	 * UMSLockOnComponent::GetHomingConeDegrees() が「この円=ロック判定円」の唯一の根拠として参照する。
	 */
	inline constexpr float HomingCursorDrawRadius = HomingCursorSize * 0.5f - HomingCursorThickness * 0.5f;
	inline constexpr float HomingTargetRingSize = 112.0f;
	inline constexpr float HomingTargetArcThickness = 6.0f;
	inline constexpr float ScopeRadiusFraction = 0.47f;
	inline constexpr float ScopeReticleExtentFraction = 0.84f;
	inline constexpr float ScopeTickSpacingFraction = 0.075f;
	inline constexpr float ScopeCenterGap = 5.0f;
	inline constexpr float ScopeLineThickness = 1.25f;
	inline constexpr float ScopeOutlineThickness = 3.0f;
	inline constexpr float ScopeMinorTickHalfSize = 4.0f;
	inline constexpr float ScopeMajorTickHalfSize = 7.0f;
	inline constexpr float UnitBarWidth = 72.0f;
	inline constexpr float UnitBarHeight = 9.0f;
	inline constexpr float UnitBarWorldOffsetZ = 230.0f * MSGO::MSScale;
	inline constexpr int32 MaxUnitBars = 16;
	inline constexpr int32 MaxKillLogLines = 5;
	inline constexpr float KillLogLifetime = 4.0f;
	inline constexpr float MinimapSize = 256.0f;
	inline constexpr float MinimapPlayerIconSize = 22.0f;
	// The circular local map spans 1,000 actual world metres from its centre.
	inline constexpr float MinimapOrthoWidth = 200000.0f;
	inline constexpr float MinimapCaptureHeight = 10000.0f * MSGO::MSScale;
	inline constexpr float MinimapPickupPollInterval = 0.5f;
	inline constexpr float HitMarkerDuration = 0.2f;
	inline constexpr float KillPopupDuration = 1.3f;
	inline constexpr float KillPopupPopInDuration = 0.12f;
	inline constexpr float KillPopupHoldDuration = 0.7f;
	inline constexpr float DamageDirectionDuration = 1.0f;
	inline constexpr float DamageDirectionRadius = 120.0f;
	inline constexpr int32 MaxMinimapEnemyMarkers = 128;
	inline constexpr int32 MaxMinimapPickupMarkers = 32;
	inline constexpr int32 KillScorePoints = 100;
	inline constexpr float ArmorValueFontSize = 44.0f; // HUD内最大フォント(本家ARMOR大型数字)
	inline constexpr float ArmorMaxFontSize = 22.0f;
	inline constexpr float ArmorLowThreshold = 0.30f;
	inline constexpr float CaptureBarWidth = 520.0f;
	inline constexpr float CaptureBarHeight = 50.0f;
	inline constexpr float CaptureLabelFontSize = 18.0f;
	inline constexpr float BoostBarWidth = 230.0f;
	inline constexpr float BoostBarHeight = 14.0f;
	inline constexpr float BoostStatusFontSize = 10.0f;
	inline constexpr float MissionIconSize = 64.0f;
	inline const FLinearColor TeamAGauge = MSFactionPresentation::RebellionColor;
	inline const FLinearColor TeamBGauge = MSFactionPresentation::OrderColor;
	// 通常=オレンジ、OH中=赤。BoostRecoveringはOHタグ解除後〜満タンのHUD内部状態識別に使い、
	// 実バーは通常色のまま「RECOVERY」文字を重ねる。
	inline const FLinearColor BoostFill(1.0f, .62f, .05f, 1.0f);
	inline const FLinearColor BoostRecovering(1.0f, 1.0f, .10f, 1.0f);
	inline const FLinearColor BoostOverheated(1.0f, .12f, .06f, 1.0f);
	inline const FLinearColor ArmorFill(.25f, .95f, .75f, 1.0f);
	inline const FLinearColor ArmorLow(1.0f, .12f, .08f, 1.0f);
	inline const FLinearColor AmmoArc(1.0f, .82f, .08f, .96f);
	inline const FLinearColor AmmoArcReloading(1.0f, .08f, .04f, .96f);
	inline const FLinearColor CaptureFill = FLinearColor::FromSRGBColor(FColor(228, 62, 4));
	inline const FLinearColor CaptureHostileFill = FLinearColor::FromSRGBColor(FColor(5, 165, 214));
	inline const FLinearColor CaptureBackground(.08f, .1f, .12f, .8f);
	inline const FLinearColor PanelBackground(.02f, .05f, .08f, .72f);
	inline const FLinearColor HUDAccent(.35f, .95f, .9f, 1.0f);
	inline const FLinearColor MissionDefense(.08f, .75f, .7f, .92f);
	inline const FLinearColor MissionAttack(.9f, .35f, .15f, .92f);
	inline const FLinearColor DPWarning(1.0f, .25f, .15f, 1.0f);
	inline const FLinearColor DPNormal(.75f, .45f, 1.0f, .95f);
	inline const FLinearColor Selected(.4f, .9f, 1.0f, 1.0f);
	inline const FLinearColor Unselected(.75f, .75f, .75f, .85f);
	inline const FLinearColor Reload(1.0f, .75f, .15f, .95f);
	inline const FLinearColor Contested(1.0f, .6f, .15f, .95f);
	inline const FLinearColor LockOnMarkerOuter(0.0f, 0.0f, 0.0f, 0.25f);
	// 実プレイ報告対応: 通常ロックは黄色、接近して赤ロックに切り替わると赤色になる
	// (Gundam Online本家準拠の2段階配色。旧シアンから変更)。
	inline const FLinearColor LockOnMarkerNormal(1.0f, .82f, .08f, 1.0f);
	inline const FLinearColor LockOnMarker(1.0f, .04f, .02f, 1.0f);
	inline const FLinearColor HomingCursorIdle(1.0f, 1.0f, 1.0f, .46f);
	inline const FLinearColor HomingCursorTracking(1.0f, .08f, .04f, .88f);
	inline const FLinearColor HomingTargetProgress(1.0f, .82f, .05f, .98f);
	inline const FLinearColor ScopeMask(0.0f, 0.0f, 0.0f, 1.0f);
	inline const FLinearColor ScopeOutline(0.02f, 0.025f, 0.03f, 1.0f);
	inline const FLinearColor ScopeReticle(0.82f, 0.92f, 0.9f, 0.92f);
	inline const FLinearColor EnemyUnitBar(.9f, .15f, .15f, 1.0f);
	inline const FLinearColor AllyUnitBar(.2f, .85f, .35f, 1.0f);
	inline const FLinearColor KillLogPlayerDeath(.9f, .15f, .15f, 1.0f);
	inline const FLinearColor KillLogEnemyDeath(.2f, .85f, .35f, 1.0f);
	inline const FLinearColor MinimapPlayer(1.0f, .45f, .08f, .9f);
	inline const FLinearColor MinimapFOV(1.0f, .4f, .06f, .16f);
	inline const FLinearColor MinimapEnemy(.95f, .08f, .06f, 1.0f);
	inline const FLinearColor MinimapFriendly(.12f, .72f, 1.0f, 1.0f);
	inline const FLinearColor MinimapPickup(.15f, 1.0f, .45f, .95f);
	inline const FLinearColor MinimapAmmoPickup(1.0f, .82f, .12f, .98f);
	inline const FLinearColor HitMarkerNormal(1.0f, 1.0f, 1.0f, .95f);
	inline const FLinearColor HitMarkerHeadshot(1.0f, .25f, .15f, 1.0f);
	inline const FLinearColor DamageDirection(1.0f, .25f, .15f, .95f);

	// Keep lock-on graphics prominent without letting them obscure the target.
	static_assert(LockOnMarkerSize >= 56.0f && LockOnMarkerSize <= 72.0f);
	static_assert(LockOnMarkerOuterThickness > 0.0f && LockOnMarkerOuterThickness <= 3.0f);
	static_assert(LockOnMarkerInnerThickness > 0.0f && LockOnMarkerInnerThickness < LockOnMarkerOuterThickness);
	static_assert(AmmoArcThickness >= 4.0f && AmmoArcThickness <= 6.0f);
}

struct NEOMSGO_API FMSHUDKillLog
{
	static FMSHUDKillLogEntry MakeEntry(const FString& UnitName, bool bPlayerDeath);
	static FMSHUDKillLogEntry MakeTimedEntry(const FText& EventTime, const FString& UnitName,
		bool bPlayerDeath);
	static void AddEntry(TArray<FMSHUDKillLogEntry>& Entries, const FMSHUDKillLogEntry& Entry,
		int32 MaxLines = MSHUDPalette::MaxKillLogLines);
	static void TickEntries(TArray<FMSHUDKillLogEntry>& Entries, float DeltaTime);
};

struct NEOMSGO_API FMSHUDTypes
{
	static FMSHUDIconStyle GetIconStyle(const FGameplayTag& Category);
	static EMSHUDFireMode GetFireMode(const FGameplayTag& Category);
	static FText GetFireModeLabel(const FGameplayTag& Category);
};
