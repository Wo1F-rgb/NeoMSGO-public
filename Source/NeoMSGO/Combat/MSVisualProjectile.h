// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-11: 視覚弾+遅延ダメージ適用。
// 設計元: docs/UE5_MIGRATION_PLAN.md P4-11 / ガードレールB-6
//         ← Unity: Assets/Scripts/Combat/VisualProjectile.cs + WeaponFx.ResolveImpact
//
// 設計(B-6: 可視弾の遅延ダメージ):
//  - 命中判定は発射時に確定する。LaunchVisualProjectile() が UMSCombatStatics::ResolveHitscan
//    (全武器共通トレース。B-3)を即時実行し、「当たるか/誰に当たるか/HSか/FFで無効か」を
//    FMSHitscanResult として固定する(Unity: WeaponFx.PlayFireEffects → CombatUtility.ResolveHitscan)。
//  - Actor自体は「見た目の弾」であり、発射点→着弾予定点を
//    FlightDuration = Distance / (ProjectileSpeed[m/s] * 100) 秒かけてLerp移動するだけ
//    (Unity: VisualProjectile.Launch の duration = Distance / speed、到達判定 t >= 1)。
//  - 飛翔中はダメージGE適用ゼロ。実ダメージ(UGE_Damage)・爆風(ApplySplashDamage/UGE_SplashDamage)・
//    ヒット演出通知(OnImpactResolved)はすべて飛翔完了時の ResolveImpact() で行う
//    (Unity: WeaponFx.ResolveImpact = applyDamageOnImpact → PlayImpact → 爆風)。
//  - ダメージ量・DP・爆発威力は発射時の DamageMultiplier(GA_Fire_Chargeのチャージ倍率等)で
//    確定済みの値を保持する。正の爆発は max(Power,SplashDamage) * multiplier を
//    ApplySplashDamage の1 packetへ渡し、Power=0の煙は旧SplashDamage経路を維持する。
//
// 責務境界:
//  - FF判定はGE側 UMSDamageExecution 手順1が単一実装(B-2)。本Actorは bCanDamage=false の
//    直撃をスキップするだけで、爆風のFFは ApplySplashDamage → Execution に委ねる。
//  - 弾体メッシュ/トレイル等の見た目は P4-13(GameplayCue体系)の責務。本ActorはRoot移動のみ。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/MSCombatStatics.h"
#include "UObject/SoftObjectPath.h"
#include "MSVisualProjectile.generated.h"

class UMSHomingWeaponData;
class UAudioComponent;
class UMaterialInterface;
class UMSRangedWeaponData;
struct FMSChargeShotMultipliers;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EMSProjectileVisualStyle : uint8
{
	Tracer,
	BeamBolt,
	BazookaShell
};

/**
 * Replicated visual-only identity for a projectile. Damage, hit results, flight time, and
 * homing state remain server-authoritative; this payload only lets a client construct the
 * same body/trail presentation after the replicated actor arrives.
 */
USTRUCT()
struct FMSProjectileVisualReplication
{
	GENERATED_BODY()

	UPROPERTY()
	EMSProjectileVisualStyle Style = EMSProjectileVisualStyle::Tracer;

	UPROPERTY()
	FLinearColor BodyColor = FLinearColor::White;

	UPROPERTY()
	FLinearColor TrailColor = FLinearColor::White;

	UPROPERTY()
	FVector BodyScale = FVector::OneVector;

	UPROPERTY()
	FVector TrailLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector TrailScale = FVector::OneVector;

	UPROPERTY()
	float GlowAttenuationRadius = 180.0f;

	UPROPERTY()
	float GlowIntensity = 1800.0f;

	/** Exhaust material scalar. Fallback Cube materials ignore this parameter. */
	UPROPERTY()
	float TrailIntensity = 1.0f;

	UPROPERTY()
	bool bHasAuthoredMesh = false;

	UPROPERTY()
	FSoftObjectPath ProjectileMeshPath;

	UPROPERTY()
	FTransform ProjectileMeshTransform = FTransform::Identity;

	/** Rocket-motor flame behind an authored body. Shells and thrown grenades fly without it. */
	UPROPERTY()
	bool bAuthoredExhaust = true;
};

/** 着弾解決通知(ヒット演出のフック。P4-13でGameplayCue発火に接続)。飛翔完了時に一度だけ発火する */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMSVisualProjectileImpactEvent,
	class AMSVisualProjectile* /*Projectile*/, const FMSHitscanResult& /*ConfirmedHit*/);

/**
 * 視覚専用飛翔弾(Unity: VisualProjectile + WeaponFx.ResolveImpact の遅延適用部)。
 * 発射時に命中を確定し、見た目の着弾時にのみダメージを適用する。
 */
UCLASS(NotBlueprintable)
class NEOMSGO_API AMSVisualProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMSVisualProjectile();

	/**
	 * 発射時命中確定+視覚弾スポーン(唯一の生成入口)。
	 *
	 *  1. ResolveHitscan(Origin, Direction, Weapon->Range)を即時実行し命中を確定する。
	 *  2. 着弾予定点(命中: ImpactPoint / 外れ: 最大射程点)までの飛翔時間を
	 *     Distance / (Weapon->ProjectileSpeed[m/s] * 100) で算出する。
	 *  3. 見た目の弾Actorをスポーンして飛翔を開始する。ダメージはこの時点では一切適用しない。
	 *
	 * @param Shooter          発射者(トレース除外・GE ContextのInstigator)
	 * @param Origin           発射点(銃口)
	 * @param Direction        発射方向
	 * @param Weapon           武器データ(Power/DownPoint/Range/ProjectileSpeed/Splash*を参照)
	 * @param DamageMultiplier 既存の共通倍率。チャージ固有の独立倍率はFromOriginsの追加引数で渡す。
	 * @return スポーンした視覚弾(失敗時 nullptr)。命中結果は GetConfirmedHit() で参照可能
	 */
	static AMSVisualProjectile* LaunchVisualProjectile(
		AActor* Shooter,
		const FVector& Origin,
		const FVector& Direction,
		const UMSRangedWeaponData* Weapon,
		float DamageMultiplier = 1.0f);

	/** Keeps camera-based hit resolution separate from the visible muzzle spawn. */
	static AMSVisualProjectile* LaunchVisualProjectileFromOrigins(
		AActor* Shooter, const FVector& TraceOrigin, const FVector& VisualOrigin,
		const FVector& Direction, const UMSRangedWeaponData* Weapon, float DamageMultiplier = 1.0f,
		const FMSChargeShotMultipliers* ChargeMultipliers = nullptr);

	/** Launches the same visible projectile with turn-rate-limited target tracking. */
	static AMSVisualProjectile* LaunchHomingVisualProjectile(
		AActor* Shooter,
		const FVector& Origin,
		const FVector& Direction,
		const UMSHomingWeaponData* Weapon,
		AActor* HomingTarget,
		float DamageMultiplier = 1.0f);

	static AMSVisualProjectile* LaunchHomingVisualProjectileFromOrigins(
		AActor* Shooter, const FVector& TraceOrigin, const FVector& VisualOrigin,
		const FVector& Direction, const UMSHomingWeaponData* Weapon, AActor* HomingTarget,
		float DamageMultiplier = 1.0f);

	virtual void Tick(float DeltaSeconds) override;

	/** 発射時に確定した命中結果(bBlockingHit / bCanDamage / bIsHeadshot / Hit) */
	const FMSHitscanResult& GetConfirmedHit() const { return ConfirmedHit; }

	/** 飛翔中か(true の間はダメージGE適用ゼロ) */
	bool IsInFlight() const { return bInFlight; }

	/** 着弾解決済みか(ダメージ適用・演出通知が完了したか) */
	bool HasResolvedImpact() const { return bImpactResolved; }
	bool IsHomingFlight() const { return bHomingFlight; }
	AActor* GetHomingTarget() const { return HomingTarget.Get(); }

	/** 発射→着弾予定点の飛翔時間 秒 */
	float GetFlightDuration() const { return FlightDuration; }
 float GetSplashRadiusCm() const { return SplashRadiusCm; }

	UFUNCTION(BlueprintPure, Category="MS|VFX")
	EMSProjectileVisualStyle GetVisualStyle() const { return VisualStyle; }

	UFUNCTION(BlueprintPure, Category="MS|VFX")
	UStaticMeshComponent* GetVisualBody() const { return VisualBody; }

	UFUNCTION(BlueprintPure, Category="MS|VFX")
	UStaticMeshComponent* GetVisualTrail() const { return VisualTrail; }

	/** True when the projectile body is using WeaponData.ProjectileMesh rather than the Cube fallback. */
	UFUNCTION(BlueprintPure, Category="MS|VFX")
	bool IsUsingAuthoredProjectileMesh() const { return bUsingAuthoredProjectileMesh; }

	/** 着弾解決通知(ヒット演出フック。P4-13 GameplayCue接続用) */
	FMSVisualProjectileImpactEvent OnImpactResolved;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 着弾解決(Unity: WeaponFx.ResolveImpact)。飛翔完了時に一度だけ実行される。
	 * 爆発は直撃対象を中心指定した単一packet、非爆発/煙は従来の直撃→爆風、最後に演出通知。
	 */
	void ResolveImpact();

	/** 直撃 UGE_Damage 適用(GA_Fire_Hitscan::ResolveAndApplyDamage のGE適用部と同一契約) */
	bool ApplyConfirmedDirectDamage();

	/** 爆発packet/旧煙爆風を P2-4 ApplySplashDamage に渡す */
	int32 ApplyConfirmedSplashDamage();

private:
	void InitializeFlight(AActor* Shooter, const FVector& Origin,
		const FMSHitscanResult& Trace, const UMSRangedWeaponData* Weapon, float DamageMultiplier,
		const FMSChargeShotMultipliers* ChargeMultipliers = nullptr);
	void InitializeHomingFlight(AActor* Shooter, const FVector& Origin, const FVector& InitialDirection,
		const FMSHitscanResult& Trace, const UMSHomingWeaponData* Weapon, AActor* Target,
		float DamageMultiplier);
	void ConfigureVisuals(const UMSRangedWeaponData* Weapon);
	void ApplyVisualReplication(const FMSProjectileVisualReplication& State);
	void StartFlightAudio();

	UFUNCTION()
	void OnRep_VisualReplication();

	/** Default assets are retained so an authored projectile can clear constructor overrides. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultCubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ExhaustConeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DefaultVisualMaterial;

	/** Optional additive material used only by the authored projectile exhaust cone. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ExhaustMaterial;

	UPROPERTY(VisibleAnywhere, Category="MS|VFX")
	TObjectPtr<UStaticMeshComponent> VisualBody;

	UPROPERTY(VisibleAnywhere, Category="MS|VFX")
	TObjectPtr<UStaticMeshComponent> VisualTrail;

	UPROPERTY(VisibleAnywhere, Category="MS|VFX")
	TObjectPtr<UPointLightComponent> GlowLight;

	/** Bazooka/missile flight loop, stopped as part of impact resolution. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> FlightAudio;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FlightSound;

	UPROPERTY(ReplicatedUsing=OnRep_VisualReplication)
	FMSProjectileVisualReplication ReplicatedVisuals;

	EMSProjectileVisualStyle VisualStyle = EMSProjectileVisualStyle::Tracer;

	/** 発射者(GE ContextのInstigator。着弾前に消滅していてもGE適用自体は行う) */
	TWeakObjectPtr<AActor> ShooterActor;

	/** Weapon source retained so impact/explosion Cues can select authored audio. */
	TWeakObjectPtr<const UMSRangedWeaponData> WeaponData;

	/** 発射時に確定した命中結果 */
	FMSHitscanResult ConfirmedHit;

	/** 発射点 */
	FVector StartLocation = FVector::ZeroVector;

	/** 着弾予定点(命中: ImpactPoint / 外れ: 最大射程点) */
	FVector ImpactLocation = FVector::ZeroVector;

	/** 発射時に確定した直撃威力(Power * DamageMultiplier) */
	float ConfirmedDamage = 0.0f;
 float LaunchShieldDamageScale = 1.f;

	/** 発射時に確定したDP(DownPoint * DamageMultiplier) */
	float ConfirmedDownPoint = 0.0f;

	/** 爆風半径 cm(SplashRadius[m] * 100。0以下は爆風なし) */
	float SplashRadiusCm = 0.0f;

	/** 発射時に確定した爆発packet基礎威力(チャージ倍率乗算・丸め済み) */
	float ScaledSplashDamage = 0.0f;

	/** 発射時に確定した爆発profile */
	float ExplosionMinFalloff = 0.1f;
	float ExplosionFullDamageRadiusRatio = 0.15f;
	float ExplosionDownPoint = 0.0f;
	float ExplosionDirectHitDownPointMultiplier = 1.0f;
	bool bUsesSingleExplosionPacket = false;

	/** 飛翔時間 秒(= Distance / (ProjectileSpeed * 100)) */
	float FlightDuration = 0.0f;

	/** 飛翔経過 秒 */
	float ElapsedFlightTime = 0.0f;

	TWeakObjectPtr<AActor> HomingTarget;
	FVector HomingDirection = FVector::ForwardVector;
	float HomingSpeedCmPerSecond = 0.0f;
	float HomingTurnRateDegPerSecond = 0.0f;
	bool bHomingFlight = false;
	bool bUsingAuthoredProjectileMesh = false;

	bool bInFlight = false;
	bool bImpactResolved = false;
};
