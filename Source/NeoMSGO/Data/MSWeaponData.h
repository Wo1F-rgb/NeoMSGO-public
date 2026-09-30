// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MSWeaponData.generated.h"

class UStaticMesh;

/** Per-shot charge effects. A projectile snapshots all four values when it launches. */
USTRUCT(BlueprintType)
struct FMSChargeShotMultipliers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0"))
	float Damage = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0"))
	float DownPoint = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.01"))
	float ProjectileSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0"))
	float SplashRadius = 1.0f;
};

/**
 * MSWeaponData (P1-1)
 *
 * Unity版 Assets/Scripts/Data/WeaponData.cs(単一ScriptableObject/9カテゴリ/4バリアント)の
 * UE5データモデル。計画書P1-1の方針に従い、共通パラメータを基底 UPrimaryDataAsset に、
 * カテゴリ固有パラメータを派生クラスに分離する。
 *
 * クラス階層とカテゴリ対応(UE5_Architecture.md §1.3 のGAマッピングと整合):
 *   UMSWeaponDataBase (abstract)
 *   ├─ UMSRangedWeaponData          … MachineGun / BeamRifle / Bazooka(burst/chargeはカテゴリ横断フラグのためこの層)
 *   │   ├─ UMSShotgunWeaponData     … Shotgun
 *   │   ├─ UMSHomingWeaponData      … Homing(Unity: isHoming=true をクラス型で表現)
 *   │   └─ UMSContinuousBeamWeaponData … ContinuousBeam
 *   ├─ UMSMeleeWeaponData           … Melee
 *   ├─ UMSShieldWeaponData          … Shield
 *   └─ UMSSupplyWeaponData          … Supply(Unity: isDeployable=true をクラス型で表現)
 *
 * 数値パラメータはDataAssetに集約し、BPインスタンス上のオーバーライドを作らない(ガードレールB-9)。
 * lockOnRange未設定などのデータ不備は WITH_EDITOR の IsDataValid で検出する(ガードレールB-5)。
 */

class FDataValidationContext;

UENUM(BlueprintType)
enum class EMSWeaponDamageKind : uint8 { Automatic, Ballistic, Beam, Explosive, Melee };
UENUM(BlueprintType)
enum class EMSProjectileKind : uint8 { Automatic, Direct, Rocket, LaunchedBallistic, Thrown };

/**
 * 型/バリエーション分類(Unity: WeaponVariantType)。
 * UI・命名補助用の純粋なメタデータであり、ロジックには影響しない。
 * 実際の数値差は各武器アセットに個別設定される(ArchitectureMap.md §14)。
 */
UENUM(BlueprintType)
enum class EMSWeaponVariantType : uint8
{
	/** 無印/標準型 (N型相当) */
	Standard,
	/** 連射型 (M型相当。装弾数とDP、マガジン1.5〜2倍想定) */
	RapidFire,
	/** 火力型 (F型相当。威力↑、射程/集弾↓想定) */
	Firepower,
	/** 拡散型 (W型相当。爆風範囲↑、威力/弾速↓想定) */
	Wide
};

/** 格闘モーション種別(Unity: MeleeMotionType) */
UENUM(BlueprintType)
enum class EMSMeleeMotionType : uint8
{
	/** 横薙ぎ */
	Slash,
	/** 振りかぶり叩きつけ */
	Overhead,
	/** 突き */
	Thrust,
	/** 正拳突き */
	Punch
};

/**
 * How WeaponMesh is placed on the mech. Presentation only; firing and melee logic ignore it.
 * All modes keep the authored grip origin and local +Z barrel/blade/strike axis.
 */
UENUM(BlueprintType)
enum class EMSWeaponMeshMount : uint8
{
	/** Right-hand WeaponMount, +Z along the mount's forward axis (guns, blades, launchers). */
	Hand,
	/** The same mesh in both hands (right WeaponMount and left WeaponMount_L). */
	DualHands,
	/** Worn over the right fist: +Z strike axis continues the forearm, +X faces the back of the hand. */
	Fist
};

/** 設置物種別(Unity: DeployedItemType) */
UENUM(BlueprintType)
enum class EMSDeployedItemType : uint8
{
	RepairPod,
	SupplyPackRepair,
	SupplyPackAmmo,
	RadarPod
};

/**
 * 全武器共通の基底データアセット。
 * Unity版 WeaponData.cs の「基本/攻撃性能/弾薬/射程/装備コスト/内蔵武器/持ち替え」フィールドに対応。
 */
UCLASS(Abstract, BlueprintType)
class NEOMSGO_API UMSWeaponDataBase : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** PrimaryAssetType: "MSWeaponData"(AssetManagerでの一括列挙用。P1-3で71武器を登録) */
	static const FPrimaryAssetType PrimaryAssetType;

	// ---- 基本 ----

	/** 武器表示名(Unity: weaponName) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic")
	FText WeaponName;

	/** 武器カテゴリ(Unity: WeaponCategory enum → Weapon.Category.* タグ。MSGameplayTags.h のネイティブタグのみ使用) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic", meta = (Categories = "Weapon.Category"))
	FGameplayTag CategoryTag;

	/** 両手武器など、選択中は常に機体シールドを使えない武装。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield")
	bool bDisablesNativeShield = false;

	/** 盾内蔵火器など、射撃中も防御できる例外。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield")
	bool bCanFireWithNativeShield = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield")
	bool bCanChargeWithNativeShield = false;

	/** 型/バリエーション分類(Unity: variantType)。ロジック非依存のメタデータ */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic")
	EMSWeaponVariantType VariantType = EMSWeaponVariantType::Standard;

 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
 EMSWeaponDamageKind DamageKind = EMSWeaponDamageKind::Automatic;
 EMSWeaponDamageKind GetEffectiveDamageKind() const;

	// ---- 攻撃性能 ----

	/** 一発あたりの実HPダメージ(Unity: power=100)。表示値とは尺度が異なる。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	int32 Power = 100;

	/** 武器性能UIの威力。実ダメージはPowerをそのまま使用する。 */
	UFUNCTION(BlueprintPure, Category = "MS|Weapon|Display")
	int32 GetDisplayPower() const { return Power * 10; }

	/** 発射間隔 秒(Unity: fireInterval=0.5) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0", Units = "s"))
	float FireInterval = 0.5f;

	/** 一発あたりのダウンポイント蓄積量(Unity: downPoint=15) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	int32 DownPoint = 15;

	// ---- 弾薬 ----

	/** 装弾数(Unity: magazineSize=10)。0以下は無限弾扱い(UE5_Architecture.md §1.5) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 MagazineSize = 10;

	/** 予備弾数(Unity: reserveAmmo=0) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 ReserveAmmo = 0;

	/** リロード時間 秒(Unity: reloadTime=3) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.0", Units = "s"))
	float ReloadTime = 3.0f;

	// ---- 射程 ----

	/** 有効射程 m(Unity: range=100) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0"))
	float Range = 100.0f;

	/** Legacy assets use scaled gameplay meters. Set to 1 for ranges authored in actual world meters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0"))
	float RangeWorldScale = MSGO::MSScale;

	UFUNCTION(BlueprintPure, Category = "MS|Weapon|Range")
	float GetRangeWorldCm() const { return Range * 100.0f * RangeWorldScale; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon|Range")
	float GetDisplayRangeMeters() const { return GetRangeWorldCm() / 100.0f; }

	/**
	 * ロックオン可能距離 m(Unity: lockOnRange=0)。
	 * Unity版の既知バグ「新武器のlockOnRange未設定(=0のまま出荷)」の再発防止のため、
	 * ロックオン対象カテゴリでは IsDataValid が 0 以下をエラーにする(ガードレールB-5)。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0"))
	float LockOnRange = 0.0f;

	UFUNCTION(BlueprintPure, Category = "MS|Weapon|Range")
	float GetLockOnRangeWorldCm() const { return LockOnRange * 100.0f * RangeWorldScale; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon|Range")
	float GetDisplayLockOnRangeMeters() const { return GetLockOnRangeWorldCm() / 100.0f; }

	/**
	 * 赤ロック距離。メートル値ではなく、MSGO::MSScale適用済みのUE世界距離(cm)。
	 * 0は赤ロックなし。生成データの既定は通常射撃300m、バズーカ250m、スコープ狙撃450m。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", Units = "cm"))
	float RedLockDistance = 30000.0f * MSGO::MSScale;

	// ---- 装備コスト ----

	/** 装備重量(Unity: weight=10。MobileSuitData.loadCapacityに対するコスト) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0"))
	int32 Weight = 10;

	// ---- 内蔵武器 / 持ち替え ----

	/** 内蔵武器か(Unity: isBuiltIn=false。頭部バルカン/肩部ロケットポッド等。切替0秒) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Switching")
	bool bIsBuiltIn = false;

	/**
	 * 持ち替え時間 秒(Unity: weaponSwitchDuration=0.6)。
	 * 実効値は必ず GetEffectiveWeaponSwitchDuration() を経由すること(内蔵武器は常に0)。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Switching", meta = (ClampMin = "0.0", Units = "s", EditCondition = "!bIsBuiltIn"))
	float WeaponSwitchDuration = 0.6f;

	// ---- Visual ----

	/** Weapon-owned skin. An unset mesh keeps the procedural category-based fallback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UStaticMesh> WeaponMesh;

	/**
	 * Per-weapon mesh correction relative to the normalized right-hand WeaponMount.
	 * Translation is in authored centimeters; rotation and scale correct the mesh's grip/pivot axes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	FTransform WeaponMeshTransform = FTransform::Identity;

	/** Placement of WeaponMesh (one hand, both hands, or over the fist). Ignored by built-in modules and native shields. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	EMSWeaponMeshMount WeaponMeshMount = EMSWeaponMeshMount::Hand;

	// ---- Muzzle ----

	/** Uses the following WeaponMount-local muzzle position and direction instead of the legacy weapon-category defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Muzzle")
	bool bOverrideMuzzleTransform = false;

	/** WeaponMount-local muzzle position in centimeters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Muzzle",
		meta = (EditCondition = "bOverrideMuzzleTransform", Units = "cm"))
	FVector MuzzleOffsetOverride = FVector::ZeroVector;

	/** WeaponMount-local firing direction. It is normalized when resolved at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Muzzle",
		meta = (EditCondition = "bOverrideMuzzleTransform"))
	FVector MuzzleDirectionOverride = FVector::ForwardVector;

public:
	/** 実効持ち替え時間。内蔵武器は常に0(Unity: isBuiltInなら常に0、ガードレールB-4) */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData")
	float GetEffectiveWeaponSwitchDuration() const
	{
		return bIsBuiltIn ? 0.0f : WeaponSwitchDuration;
	}

	/** 無限弾か(MagazineSize<=0) */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData")
	bool HasInfiniteAmmo() const
	{
		return MagazineSize <= 0;
	}

	//~ Begin UObject/UPrimaryDataAsset Interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	//~ End UObject/UPrimaryDataAsset Interface

protected:
#if WITH_EDITOR
	/** このデータクラスが許容する Weapon.Category.* タグ集合(カテゴリタグとクラス型の不整合検出用) */
	virtual FGameplayTagContainer GetAllowedCategoryTags() const;

	/** LockOnRange > 0 を必須とするか(Shield/Supplyのみ不要) */
	virtual bool RequiresLockOnRange() const { return true; }
#endif
};

/**
 * 射撃系武器の共通データ(MachineGun / BeamRifle / Bazooka はこのクラスを直接使用)。
 * burst/charge はUnity版でカテゴリ横断のフラグであるため、個別派生ではなくこの層に置く
 * (UE5_Architecture.md §1.3: burstCount>1 → UGA_Fire_Burst、isChargeable → UGA_Fire_Charge の派生選択)。
 */
UCLASS(BlueprintType)
class NEOMSGO_API UMSRangedWeaponData : public UMSWeaponDataBase
{
	GENERATED_BODY()

public:
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
 EMSProjectileKind ProjectileKind = EMSProjectileKind::Automatic;
 EMSProjectileKind GetEffectiveProjectileKind() const;
 bool SupportsBlastRadiusControl() const;

	// ---- 弾道エフェクト ----

	/** トレーサー色(Unity: tracerColor) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracer")
	FLinearColor TracerColor = FLinearColor::White;

	/** トレーサー幅 m(Unity: tracerWidth=0.04) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracer", meta = (ClampMin = "0.0"))
	float TracerWidth = 0.04f;

	/** トレーサー表示時間 秒(Unity: tracerLifetime=0.12) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracer", meta = (ClampMin = "0.0", Units = "s"))
	float TracerLifetime = 0.12f;

	/** 可視弾の弾速 m/s(Unity: projectileSpeed=0)。0以下は即着ヒットスキャン。>0 は発射時命中確定+見た目着弾時にダメージ適用(ガードレールB-6) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracer", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 0.0f;

	/**
	 * Optional authored projectile body. The mesh's local +X is the flight axis and its
	 * dimensions are authored in UE world centimetres. This transform is applied directly
	 * to the projectile actor; MSScale is deliberately not applied a second time here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile Visual")
	TSoftObjectPtr<UStaticMesh> ProjectileMesh;

	/** Actor-relative centimetre correction for ProjectileMesh (rotation/translation/scale). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile Visual")
	FTransform ProjectileMeshTransform = FTransform::Identity;

	/** Rocket-motor flame behind ProjectileMesh. Unpowered shells and thrown grenades disable it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile Visual")
	bool bProjectileMeshExhaust = true;

	// ---- 爆風 ----

	/** 爆風半径 m(Unity: splashRadius=0)。0以下は爆風なし */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Splash", meta = (ClampMin = "0.0"))
	float SplashRadius = 0.0f;

	/**
	 * 旧保存互換の爆風ダメージ(Unity: splashDamage=0)。正の爆発ではPowerと比較して
	 * 大きい方を基礎値に使い、Powerを超える明示的な爆風値を指定するfallbackになる。
	 * Power=0の煙や非爆発武器では従来値をそのまま使う。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Splash", meta = (ClampMin = "0", EditCondition = "SplashRadius > 0"))
	int32 SplashDamage = 0;

	/** Confirmed direct impact only; nearby splash targets retain ordinary DP. Opt-in per weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Splash", meta = (ClampMin = "1.0"))
	float DirectHitDownPointMultiplier = 1.0f;

	/** 正の半径とSplashDamageを持つ爆発の単一packet基礎ダメージ。該当しない場合は0。 */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData|Explosion")
	float GetExplosionBaseDamage() const;

	/** 爆発の外周falloff下限。チャージ可能武器は0.2、通常は0.1、Power=0の煙は旧値0.3。 */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData|Explosion")
	float GetExplosionMinFalloff() const;

	/** 爆発中心でfull damageを維持する半径割合。通常は0.15、Power=0の煙は0。 */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData|Explosion")
	float GetExplosionFullDamageRadiusRatio() const;

	// ---- 連射(バースト) ----

	/** 1トリガーの発射数(Unity: burstCount=1)。>1 でバースト射撃 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "1"))
	int32 BurstCount = 1;

	/** バースト内の発射間隔 秒(Unity: burstInterval=0.08) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s", EditCondition = "BurstCount > 1"))
	float BurstInterval = 0.08f;

	// ---- チャージ ----

	/** チャージ可能か(Unity: isChargeable=false)。Weapon.Trait.Chargeable 相当 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
	bool bIsChargeable = false;

	/** 最大チャージ時間 秒(Unity: maxChargeTime=1.2) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0", Units = "s", EditCondition = "bIsChargeable"))
	float MaxChargeTime = 1.2f;

	/** Opt-in authored charge milestones. Older assets retain their linear 1x..2x damage/DP behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (EditCondition = "bIsChargeable"))
	bool bUseChargeProfile = false;

	/** Fraction of MaxChargeTime at the authored half-charge milestone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.01", ClampMax = "0.99", EditCondition = "bIsChargeable && bUseChargeProfile"))
	float HalfChargeThreshold01 = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (EditCondition = "bIsChargeable && bUseChargeProfile"))
	FMSChargeShotMultipliers HalfChargeMultipliers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (EditCondition = "bIsChargeable && bUseChargeProfile"))
	FMSChargeShotMultipliers FullChargeMultipliers;

	/** Piecewise linear estimate between 0%, half and full; the source only establishes the milestones. */
	FMSChargeShotMultipliers EvaluateChargeMultipliers(float Progress01) const;

	// ---- スコープ ----

	/** スコープ武器か(Unity: isScopeWeapon=false)。Weapon.Trait.Scope 相当 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scope")
	bool bIsScopeWeapon = false;

	/** スコープ時FOV 度(Unity: scopedFieldOfView=12) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scope", meta = (ClampMin = "1.0", ClampMax = "170.0", EditCondition = "bIsScopeWeapon"))
	float ScopedFieldOfView = 12.0f;

	/** スコープ時感度倍率(Unity: scopedSensitivityMultiplier=0.35) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scope", meta = (ClampMin = "0.01", ClampMax = "1.0", EditCondition = "bIsScopeWeapon"))
	float ScopedSensitivityMultiplier = 0.35f;

public:
	/** 即着ヒットスキャンか(ProjectileSpeed<=0) */
	UFUNCTION(BlueprintPure, Category = "MSWeaponData")
	bool IsHitscan() const
	{
		return ProjectileSpeed <= 0.0f;
	}

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
#endif
};

/** 散弾武器データ(Shotgun。Unity: pelletCount>1 の武器) */
UCLASS(BlueprintType)
class NEOMSGO_API UMSShotgunWeaponData : public UMSRangedWeaponData
{
	GENERATED_BODY()

public:
	/** 1発あたりのペレット数(Unity: pelletCount=1。散弾武器では>1必須) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shotgun", meta = (ClampMin = "1"))
	int32 PelletCount = 8;

	/** ペレット拡散角 度(Unity: pelletSpreadDegrees=8) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shotgun", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float PelletSpreadDegrees = 8.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
#endif
};

/** 誘導ミサイル武器データ(Homing。Unity: isHoming=true をクラス型で表現) */
UCLASS(BlueprintType)
class NEOMSGO_API UMSHomingWeaponData : public UMSRangedWeaponData
{
	GENERATED_BODY()

public:
	/** 誘導ロック成立に必要な照準維持時間 秒(Unity: homingLockOnDwellTime=0.8)。ロック維持はUMSLockOnComponentの責務 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Homing", meta = (ClampMin = "0.0", Units = "s"))
	float HomingLockOnDwellTime = 0.8f;

	/** 誘導旋回速度 度/秒(Unity: homingTurnRateDegPerSec=90) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Homing", meta = (ClampMin = "0.0"))
	float HomingTurnRateDegPerSec = 90.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
#endif
};

/** 照射型ビーム武器データ(ContinuousBeam「ゲロビ」。押している間、射線上に継続ダメージ/DP) */
UCLASS(BlueprintType)
class NEOMSGO_API UMSContinuousBeamWeaponData : public UMSRangedWeaponData
{
	GENERATED_BODY()

public:
	/** 秒間ダメージ(Unity: beamDamagePerSecond=300) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ContinuousBeam", meta = (ClampMin = "0"))
	int32 BeamDamagePerSecond = 300;

	/** ダメージtick間隔 秒(Unity: beamTickInterval=0.1)。トレース関数はヒットスキャンと同一関数を使用(ガードレールB-6) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ContinuousBeam", meta = (ClampMin = "0.01", Units = "s"))
	float BeamTickInterval = 0.1f;

	/** ビーム半径 m(Unity: beamRadius=0.15) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ContinuousBeam", meta = (ClampMin = "0.0"))
	float BeamRadius = 0.15f;

	/** 秒間ダウンポイント蓄積量(Unity: beamDownPointPerSecond=20) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ContinuousBeam", meta = (ClampMin = "0"))
	int32 BeamDownPointPerSecond = 20;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
#endif
};

/** 格闘武器データ(Melee。リーチはRange、通常/D格の分岐はGA_Melee側の責務) */
UCLASS(BlueprintType)
class NEOMSGO_API UMSMeleeWeaponData : public UMSWeaponDataBase
{
	GENERATED_BODY()

public:
	/** 格闘モーション種別(Unity: meleeMotionType=Slash) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	EMSMeleeMotionType MeleeMotionType = EMSMeleeMotionType::Slash;

	/** ジム標準N型: 通常2段／D横薙ぎ。未指定武器の従来挙動は保持する。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	bool bUseStandardBeamSaberMotion = false;

	/** ガンダム二刀流N/D共通: 通常4段／D格時計回り回転斬り。GM Nとは独立して指定する。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	bool bUseGundamDualSaberMotion = false;

	/** D格1回のブースト消費。既存格闘武器の30を既定値として維持する。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0.0"))
	float DashMeleeBoostCost = 30.0f;

	bool UsesTimedSaberMotion() const { return bUseStandardBeamSaberMotion || bUseGundamDualSaberMotion; }

#if WITH_EDITOR
protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
#endif
};

/** 機体固有シールドのデータ。既存アセットIDを維持するためクラス名を保持する。武器枠には装備しない。 */
UCLASS(BlueprintType)
class NEOMSGO_API UMSShieldWeaponData : public UMSWeaponDataBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "1.0"))
	float MaxDurability = 1380.0f;

	/** 盾防御時に本体へ届くDP倍率。現行100DPスケールは変更しない。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuardDownPointMultiplier = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplosionChipMultiplier = 0.1f;

	/** 旧保存の互換用。現在の防御計算は盾耐久を消費し、この本体ダメージ倍率は使わない。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuardDamageMultiplier = 0.15f;

	/** ガード有効前方角 度(Unity: guardFrontalAngleDegrees=65) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float GuardFrontalAngleDegrees = 65.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
	virtual bool RequiresLockOnRange() const override { return false; }
#endif
};

/** 補給/設置武器データ(Supply。リペアポッド/補給パック等。Unity: isDeployable=true をクラス型で表現) */
UCLASS(BlueprintType)
class NEOMSGO_API UMSSupplyWeaponData : public UMSWeaponDataBase
{
	GENERATED_BODY()

public:
	/** 設置物種別(Unity: deployedItemType=RepairPod) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply")
	EMSDeployedItemType DeployedItemType = EMSDeployedItemType::RepairPod;

	/** 設置物の存続時間 秒(Unity: deployedPodLifetime=30) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0", Units = "s"))
	float DeployedPodLifetime = 30.0f;

	/** Radar effect range is a listed value; radius in metres is this value / 10. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply|Radar", meta = (ClampMin = "0.0", EditCondition = "DeployedItemType == EMSDeployedItemType::RadarPod"))
	float RadarRangeStat = 4800.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

protected:
	virtual FGameplayTagContainer GetAllowedCategoryTags() const override;
	virtual bool RequiresLockOnRange() const override { return false; }
#endif
};
