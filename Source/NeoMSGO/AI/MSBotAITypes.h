#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "MSBotAITypes.generated.h"

UENUM(BlueprintType)
enum class EMSEnemyTier : uint8
{
	Weak,
	Medium,
	Strong
};

UENUM(BlueprintType)
enum class EMSBotDistanceRule : uint8
{
	PatrolToChase,
	ChaseToPatrol,
	ChaseToAttack,
	AttackToChase
};

/**
 * データ駆動の階級別チューニングテーブル(P6-2)。P6-1で導入した基本値に加えて、
 * 階級式(AMSBotAIController::Calculate*)が参照する残りの実データ(ブースト頻発条件・
 * 格闘間合い停止距離・キック間合い)を保持する。単一インスタンスを3階級で共有し、
 * Tier別の値の違いは Calculate*(Tier, Config) 側の式で表現する(既存P6-1設計を踏襲。
 * AC1で許容されている「既存FMSBotAIConfigを拡張したデータ駆動テーブル」方式)。
 */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBotAIConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float DetectionRange = 2500.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float LoseSightRange = 3500.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float PatrolRadius = 1000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float PatrolAcceptanceRadius = 50.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float MeleeEngageDistance = 350.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float BazookaPreferredDistance = 1500.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float StrongEngageDistanceBonus = 500.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float StrafeDistanceFactor = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float StrafeReverseMinInterval = 1.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float StrafeReverseMaxInterval = 3.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackRotationInterpSpeed = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float TargetRefreshInterval = 0.5f;
	/** Radius in which living friendly BOTs contribute a separation steering offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm", ClampMin="0.0")) float SeparationRadius = 320.0f;
	/** Maximum separation offset added to chase/attack destinations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm", ClampMin="0.0")) float SeparationMaxOffset = 220.0f;
	/** CharacterMovement RVO neighborhood used as the final local collision-avoidance layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm", ClampMin="0.0")) float RVOAvoidanceRadius = 260.0f;

	/** Weak/Strongのブースト頻発条件(Unity: boostBurstTriggerDistance=12m) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float BoostBurstTriggerDistance = 1200.0f * MSGO::MSScale;
	/** Mediumのブースト頻発条件係数(MeleeEngageDistance*係数=420cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MediumBoostTriggerFactor = 1.2f;
	/** Mediumのブーストクールダウン倍率(Unity: 0.4倍) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MediumBoostCooldownMultiplier = 0.4f;
	/** Unity BOT burst tuning. AccelerationMinBonus is scaled for UE's cm/s^2 CharacterMovement defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BoostBurstSpeedMultiplier = 2.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float BoostBurstDuration = 1.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float BoostBurstCooldown = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm/s^2")) float BoostBurstAccelerationMinBonus = 2000.0f;
	/** Mediumが格闘間合いへ直進した際の静止距離(Unity: 2.4m以内で静止) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float MediumMeleeStopDistance = 240.0f * MSGO::MSScale;
	/** Strong専用キックの間合い(Unity: kickRange=3f)。実処理はP6-5でパラメータのみ保持 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="cm")) float KickRange = 300.0f;

	/**
	 * ユーザーレビュー#69(BOT同士の見つめ合いフリーズ対策):
	 * Attack状態でこの秒数発砲(格闘/キック含む)できなければ、ストレイフ方向を反転し
	 * 目標へ距離を詰める「位置取り直し」へ切り替える。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float AttackRepositionTimeout = 2.5f;
	/** 位置取り直しの継続時間 秒(この間はRepositionDestinationへ移動し続ける) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s")) float AttackRepositionDuration = 1.2f;
};

/**
 * 全階級共通の弱体化パラメータ(P6-2。プレイヤー体験調整)。
 * damageMultiplier/aimSpreadDegreesはAMSBotCharacter::GetCombatModifiers()経由で
 * GA_Fire_Hitscan::ResolveAndApplyDamage・Fire、GA_Melee::PerformMeleeへ供給される。
 * fireIntervalMultiplierはUMSWeaponComponent::ConsumeAmmoAndStartCooldownへ供給される
 * (格闘武器には*0.4のみ適用=実質2倍。5倍そのまま適用すると攻撃間隔3.5秒になり
 * 格闘が事実上機能しなかった経緯があるため、GetMeleeFireIntervalMultiplier()で分離する)。
 */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBotWeakeningParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageMultiplier = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="deg")) float AimSpreadDegrees = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FireIntervalMultiplier = 5.0f;
	/** 格闘のFireIntervalMultiplierに掛かる追加係数(5倍*0.4=実質2倍) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MeleeFireIntervalFactor = 0.4f;

	float GetMeleeFireIntervalMultiplier() const { return FireIntervalMultiplier * MeleeFireIntervalFactor; }
};
