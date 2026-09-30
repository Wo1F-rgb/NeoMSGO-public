// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MSMobileSuitData.generated.h"

class USkeletalMesh;

/**
 * MSMobileSuitData (P1-2)
 *
 * Unity版 Assets/Scripts/Data/MobileSuitData.cs + MobileSuitCatalog.cs の
 * UE5データモデル。MSWeaponDataBase(P1-1)と同じ UPrimaryDataAsset パターンを踏襲し、
 * PrimaryAssetId・IsDataValidバリデーション・命名規則を揃える。
 *
 * Unity版フィールドとの対応(ArchitectureMap.md §13):
 *   unitName / faction / unitType / cost / maxArmor /
 *   walkSpeed / boostSpeed / boostCapacity / boostConsumptionRate /
 *   boostRechargeRate / boostOverheatPenalty / meleeCorrection /
 *   loadCapacity / equippedWeapons
 *
 * 追加項目(Unity版はHealth.cs側の定数): maxDownPoint。
 * Unity版ではよろけ/ダウン閾値 maxDownPoint=100 は Health.cs 内の固定定数で
 * 全機体共通だが、UE5移行ではP2-1/P2-2の設計方針(GASの属性初期値は
 * 機体データ由来のGameplayEffectで適用)に合わせ、MobileSuitData側に
 * 保持する形にする(全機体共通値100をデフォルトとして踏襲)。
 *
 * Faction/UnitTypeはUnity版同様フレーバー属性であり、戦闘上のチーム判定
 * (FGenericTeamId, Core/MSTeamTypes.h)とは独立する(混同禁止。P0-4の設計注記通り)。
 */

class FDataValidationContext;
class UMSModuleData;
class UMSWeaponDataBase;
class UMSShieldWeaponData;

/** 陣営フレーバー分類(Unity: Faction enum)。戦闘チーム判定には使用しない */
UENUM(BlueprintType)
enum class EMSMobileSuitFaction : uint8
{
	Federation,
	Principality
};

/** 機体タイプ分類(Unity: UnitType enum) */
UENUM(BlueprintType)
enum class EMSMobileSuitUnitType : uint8
{
	Assault,
	Heavy,
	Support,
	Bombardment,
	Sniper
};

/**
 * 機体ステータスデータアセット。
 * Unity版 MobileSuitData.cs の全フィールドに対応する(過不足なく踏襲)。
 */
UCLASS(BlueprintType)
class NEOMSGO_API UMSMobileSuitData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** PrimaryAssetType: "MSMobileSuitData"(AssetManagerでの8機体一括列挙用) */
	static const FPrimaryAssetType PrimaryAssetType;

	/** 装備武器スロット数の上限(Unity: weaponAssetNames最大3。P4-1の3スロット切替設計と一致) */
	static constexpr int32 MaxWeaponSlots = 3;

	// ---- 基本 ----

	/** 機体表示名(Unity: unitName)。実在ガンダム名は使用不可(ガードレールB-1) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic")
	FText UnitName;

	/** 陣営フレーバー(Unity: faction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic")
	EMSMobileSuitFaction Faction = EMSMobileSuitFaction::Federation;

	/** 機体タイプ(Unity: unitType) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic")
	EMSMobileSuitUnitType UnitType = EMSMobileSuitUnitType::Assault;

	/** 出撃コスト(Unity: cost=200) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic", meta = (ClampMin = "0"))
	int32 Cost = 200;

	/** Explicit seconds for nonstandard costs. -1 uses the documented base COST/TIME table. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Basic", meta = (ClampMin = "-1", Units = "s"))
	float ReuseDelaySeconds = -1.0f;

	/** Basic reuse time, before item/module/merit adjustments. Unknown costs return zero. */
	UFUNCTION(BlueprintPure, Category = "Basic")
	float GetReuseDelaySeconds() const;

	// ---- 耐久 ----

	/** 最大アーマー(Unity: maxArmor=5000) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vital", meta = (ClampMin = "1"))
	int32 MaxArmor = 5000;

	/**
	 * ダウンポイント上限(Unity: Health.cs の定数 maxDownPoint=100。全機体共通)。
	 * UE5では機体データ経由でAttribute初期値GEに適用するため、ここに保持する(上部コメント参照)。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vital", meta = (ClampMin = "1"))
	int32 MaxDownPoint = 100;
	/** Post-2019-08-21 DG: common 800 flinch, totals 1600/1700/1800. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vital", meta=(ClampMin="1",ClampMax="3"))
	int32 DownGaugeLevel = 1;
	int32 GetDownGaugeCapacity() const
	{
		// Migrate serialized prototype 100 without overwriting intentionally authored test/special capacities.
		return MaxDownPoint==100 ? 1500+100*FMath::Clamp(DownGaugeLevel,1,3) : MaxDownPoint;
	}

	/** 緊急帰投性能。0で15秒、1000で5秒へ線形マッピングする。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Emergency Recall",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 EmergencyRecallPerformance = 427;

	// ---- 移動 ----

	/** 歩行速度 m/s(Unity: walkSpeed=6) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 6.0f;

	/** ブースト時速度 m/s(Unity: boostSpeed=14) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BoostSpeed = 14.0f;

	/** 水中適性。true の機体は水中での移動速度ペナルティを受けない。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	bool bAmphibious = false;

	// ---- ブースト ----

	/** ブースト容量(Unity: boostCapacity=100) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.0"))
	float BoostCapacity = 100.0f;

	/** ブースト消費速度 /s(Unity: boostConsumptionRate=20) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.0"))
	float BoostConsumptionRate = 20.0f;

	/** ブースト回復速度 /s(Unity: boostRechargeRate=15) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.0"))
	float BoostRechargeRate = 15.0f;

	/** オーバーヒートペナルティ 秒(Unity: boostOverheatPenalty=3) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.0", Units = "s"))
	float BoostOverheatPenalty = 3.0f;

	// ---- 格闘 ----

	/** 格闘威力補正(Unity: meleeCorrection=1) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0.0"))
	float MeleeCorrection = 1.0f;

	// ---- 装備 ----

	/** 積載量上限(Unity: loadCapacity=40。装備武器のWeight合計上限) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (ClampMin = "0"))
	int32 LoadCapacity = 40;

	/**
	 * 初期装備武器(Unity: equippedWeapons: List<WeaponData>)。
	 * PrimaryAssetの循環ハードロードを避けるためソフト参照とし、
	 * AssetManagerのPrimaryAssetId解決(P1-4)経由でロードする。
	 * 上限は MaxWeaponSlots(=3, P4-1の3スロット切替設計と一致)。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (ClampMin = "0"))
	TArray<TSoftObjectPtr<UMSWeaponDataBase>> EquippedWeapons;

	/** 機体固有シールド。武器3枠とは独立し、後期ガンオン同様に積載重量は0。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UMSShieldWeaponData> NativeShield;

	/** false の機体では格納庫から取り外せない。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	bool bShieldRemovable = true;

	/** 旧アセットの移行中だけ EquippedWeapons 内の盾も解決する。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMSShieldWeaponData* GetNativeShield() const;

	/** 固有モジュール装備(1スロット)。未装備は null を許容する。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UMSModuleData> EquippedModule;

	// ---- 実メッシュ表示(TAURUS試験導入。docs/TAURUS_MeshTrial_Plan.md) ----

	/**
	 * 実メッシュ(SkeletalMesh)。未設定(null)なら従来どおりCubeリグ表示。
	 * 設定時はUMSMeshVisualComponentが表示Cubeを非表示化しゴーストリグ転写で駆動する。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> VisualMesh;

	/** 実メッシュの目標全高cm(18m Cubeリグに合わせる。Bounds高からの自動フィットに使用) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "10.0", Units = "cm"))
	float VisualMeshTargetHeightCm = 1800.0f;

	/** インポート後のメッシュ前方とActor +X のズレ補正 Yaw(度) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	float VisualMeshYawOffsetDeg = 0.0f;

public:
	/** 装備武器スロット数 */
	UFUNCTION(BlueprintPure, Category = "MSMobileSuitData")
	int32 GetWeaponSlotCount() const
	{
		return EquippedWeapons.Num();
	}

	//~ Begin UObject/UPrimaryDataAsset Interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	//~ End UObject/UPrimaryDataAsset Interface
};
