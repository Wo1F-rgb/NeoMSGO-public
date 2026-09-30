// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-10: 誘導ミサイルActor。
// 設計元: docs/UE5_MIGRATION_PLAN.md P4-10 / docs/ArchitectureMap.md §6「誘導ミサイル実体弾システム」
//         ← Unity: Assets/Scripts/Combat/HomingProjectile.cs
//
// 仕様(Unity版準拠):
//  - 旋回率 90°/秒(FQuat::Slerp による RotateTowards 相当)でターゲット方向へ機首を向ける
//  - 寿命 6秒(タイムアウトで自爆=その場で爆風を発生させ消滅)
//  - 進行方向の WeaponTrace LineTrace(B-3: MSCollision::WeaponTraceChannel)で
//    障害物(MSStructure壁・地形・機体)に着弾したら爆発
//  - 追尾対象が消失(死亡・破棄)した場合は誘導を諦めて直進を続ける(再取得はしない)
//  - 追尾維持の判定は UMSTeamStatics::CanLockOn のみ(B-2単一API。味方/中立/死亡済みは
//    構造的に追尾対象にならない。#161「味方誤ロック/追尾」バグの再発防止)
//  - 着弾/自爆時は UMSCombatStatics::ApplySplashDamage(P2-4)で爆風ダメージを与える
//    (FF判定はGE側 UMSDamageExecution 手順1が単一実装。本Actorでは行わない)
//
// 発射側(GA_Fire_Homing等)は P3-6 UMSLockOnComponent::GetLockedTarget() で得た対象を
// InitFromWeaponData / InitHoming に渡すこと(ロック成立条件 CanLockOn は P3-6 側で担保済み。
// 本Actorも念のため受け取り時に CanLockOn で再検証する)。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MSHomingProjectile.generated.h"

class UMSHomingWeaponData;

UCLASS()
class NEOMSGO_API AMSHomingProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMSHomingProjectile();

	// ------------------------------------------------------------------
	// 計算定数(Unity: HomingProjectile.cs 既定値。UE単位=cm)
	// ------------------------------------------------------------------

	/** 既定弾速 cm/s(Unity: speed = projectileSpeed>0 ? ... : 20f [m/s]) */
	static constexpr float DefaultSpeed = 2000.0f;

	/** 既定旋回率 度/秒(Unity: turnRateDegPerSec = ... : 90f) */
	static constexpr float DefaultTurnRateDegPerSec = 90.0f;

	/** 寿命 秒(Unity: MaxLifetime = 6f。タイムアウトで自爆) */
	static constexpr float MaxLifetimeSeconds = 6.0f;

	/** 近接信管半径 cm(Unity: HitRadius = 1.5f [m]。対象へこの距離まで接近したら着弾扱い) */
	static constexpr float ProximityHitRadius = 150.0f;

	// ------------------------------------------------------------------
	// 初期化API(スポーン直後に必ずどちらかを呼ぶこと)
	// ------------------------------------------------------------------

	/**
	 * 誘導パラメータを直接指定して初期化する(テスト/特殊弾用)。
	 * InHomingTarget は UMSTeamStatics::CanLockOn(InInstigatorActor, InHomingTarget) を
	 * 通過した場合のみ追尾対象として受理する(味方/中立/死亡済みは直進弾になる)。
	 *
	 * @param InInstigatorActor 発射者(爆風のInstigator。FF判定・キル帰属)
	 * @param InHomingTarget    追尾対象(P3-6 GetLockedTarget()。null可=無誘導直進)
	 * @param InSpeed           弾速 cm/s(0以下でその場に停止。テスト用)
	 * @param InTurnRateDegPerSec 旋回率 度/秒
	 * @param InSplashRadius    爆風半径 cm(0以下は爆風なしで消滅のみ)
	 * @param InSplashDamage    爆風基礎ダメージ
	 * @param InDownPoint       爆風DP(Unity版の爆風はDP無しのため既定0)
	 */
	void InitHoming(AActor* InInstigatorActor, AActor* InHomingTarget,
		float InSpeed = DefaultSpeed,
		float InTurnRateDegPerSec = DefaultTurnRateDegPerSec,
		float InSplashRadius = 0.0f,
		float InSplashDamage = 0.0f,
		float InDownPoint = 0.0f);

	/**
	 * 武器データから初期化する(m→cm変換込み。発射アビリティ用)。
	 *  - 弾速: ProjectileSpeed>0 ? ProjectileSpeed*100 : DefaultSpeed
	 *  - 旋回率: HomingTurnRateDegPerSec>0 ? 同値 : DefaultTurnRateDegPerSec
	 *  - 爆風: SplashRadius*100 / max(Power,SplashDamage) for positive explosions;
	 *    Power=0 smoke keeps legacy SplashDamage and falloff.
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat")
	void InitFromWeaponData(const UMSHomingWeaponData* Weapon, AActor* InInstigatorActor, AActor* InHomingTarget);

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;

	// ------------------------------------------------------------------
	// 状態参照(テスト/HUD用)
	// ------------------------------------------------------------------

	/** 現在の追尾対象(誘導を諦めた後は null) */
	UFUNCTION(BlueprintPure, Category = "MS|Combat")
	AActor* GetHomingTarget() const { return HomingTarget.Get(); }

	/** 誘導中か(対象消失後の直進弾は false) */
	UFUNCTION(BlueprintPure, Category = "MS|Combat")
	bool IsHomingActive() const { return HomingTarget.IsValid(); }

	/** 着弾/自爆済みか(Explode後、Destroy完了までの間も true) */
	UFUNCTION(BlueprintPure, Category = "MS|Combat")
	bool HasExploded() const { return bExploded; }

	/** 経過寿命 秒 */
	float GetLifetimeElapsed() const { return LifetimeElapsed; }

protected:
	/** 追尾維持判定(B-2: UMSTeamStatics::CanLockOnのみ)。消失したら誘導を諦めて直進(再取得なし) */
	void UpdateHomingTargetValidity();

	/** 旋回率制限つきでターゲット方向へ機首を向ける(Unity: Quaternion.RotateTowards) */
	void RotateTowardsTarget(float DeltaSeconds);

	/** 前進+WeaponTraceで着弾検出(障害物/機体)+近接信管。着弾したら true */
	bool MoveAndDetectImpact(float DeltaSeconds);

	/** 爆発処理: ApplySplashDamage(P2-4)を1回だけ実行して自身を破棄する */
	void Explode(const FVector& Origin, AActor* DirectHitActor);

	/** 発射者(爆風Instigator。トレース無視対象) */
	TWeakObjectPtr<AActor> InstigatorActor;
	TWeakObjectPtr<const UMSHomingWeaponData> WeaponData;

	/** 追尾対象(消失で無効化。以後は直進) */
	TWeakObjectPtr<AActor> HomingTarget;

	/** 弾速 cm/s */
	float Speed = DefaultSpeed;

	/** 旋回率 度/秒 */
	float TurnRateDegPerSec = DefaultTurnRateDegPerSec;

	/** 爆風半径 cm(0以下は爆風なし) */
	float SplashRadius = 0.0f;

	/** 爆風基礎ダメージ */
	float SplashDamage = 0.0f;
 float LaunchShieldDamageScale = 1.f;

	/** 爆風DP */
	float DownPoint = 0.0f;

	/** 爆発falloff profile。数値InitHomingは新profile(.1/.15)を既定にする。 */
	float MinFalloff = 0.1f;
	float FullDamageRadiusRatio = 0.15f;
	bool bUsesSingleExplosionPacket = false;

	/** 経過寿命 秒 */
	float LifetimeElapsed = 0.0f;

	/** 爆発済みフラグ(二重爆発防止) */
	bool bExploded = false;
};
