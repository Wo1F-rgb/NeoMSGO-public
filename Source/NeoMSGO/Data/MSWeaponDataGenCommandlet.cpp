// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSWeaponDataGenCommandlet.h"

#if WITH_EDITOR

#include "Data/MSWeaponData.h"
#include "Data/MSWeaponLibraryOpusVisuals.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
	/**
	 * weapon_import_plan.json(Unity版71武器の実データ解析結果)相当のコード直書き定義。
	 * 各フィールドはUMSWeaponDataBase/派生クラスの既定値(MSWeaponData.h)と同一のデフォルト値を持ち、
	 * JSON側で大多数の武器が共有する既定値(BurstCount=1, PelletCount=1, BeamDamagePerSecond=300 等)を
	 * 明示指定省略できるようにしている(C++20 designated initializer)。
	 */
	struct FMSWeaponDef
	{
		// ---- 識別 ----
		FString AssetName;
		FString WeaponName;
		FString Category;       // Weapon.Category.* のサフィックス(MachineGun/BeamRifle/...)
		FString UEClassName;    // UMSRangedWeaponData 等のクラス名
		FString VariantType = TEXT("Standard");

		// ---- 攻撃性能 ----
		int32 Power = 100;
		float FireInterval = 0.5f;
		int32 DownPoint = 15;

		// ---- 弾薬 ----
		int32 MagazineSize = 10;
		int32 ReserveAmmo = 0;
		float ReloadTime = 3.0f;

		// ---- 射程 ----
		float Range = 100.0f;
		float RangeWorldScale = MSGO::MSScale;
		float LockOnRange = 90.0f;
		/** MSScale適用前の設計距離(m)。0=赤ロックなし。 */
		float RedLockDistanceMeters = -1.0f;
		/** 校正済み武器だけが使うUE世界距離(cm)。-1なら従来の設計距離を使う。 */
		float RedLockDistanceWorldCm = -1.0f;

		// ---- コスト / 持ち替え ----
		int32 Weight = 10;
		bool bIsBuiltIn = false;
		float WeaponSwitchDuration = 0.6f;
		FString WeaponMeshPath;
		FTransform WeaponMeshTransform = FTransform::Identity;
		bool bOverrideMuzzleTransform = false;
		FVector MuzzleOffsetOverride = FVector::ZeroVector;
		FVector MuzzleDirectionOverride = FVector::ForwardVector;

		// ---- チャージ(Ranged) ----
		bool bIsChargeable = false;
		float MaxChargeTime = 1.2f;
		bool bUseChargeProfile = false;
		FMSChargeShotMultipliers ChargeHalf;
		FMSChargeShotMultipliers ChargeFull;

		// ---- 誘導(Homing) ----
		float HomingLockOnDwellTime = 0.8f;
		float HomingTurnRateDegPerSec = 90.0f;

		// ---- バースト(Ranged) ----
		int32 BurstCount = 1;
		float BurstInterval = 0.08f;

		// ---- スコープ(Ranged) ----
		bool bIsScopeWeapon = false;
		float ScopedFieldOfView = 12.0f;
		float ScopedSensitivityMultiplier = 0.35f;

		// ---- 爆風(Ranged) ----
		float SplashRadius = 0.0f;
		int32 SplashDamage = 0;

		// ---- 設置(Supply) ----
		bool bIsDeployable = false;
		float DeployedPodLifetime = 30.0f;
		FString DeployedItemType = TEXT("RepairPod");
		float RadarRangeStat = 4800.0f;

		// ---- ガード(Shield) ----
		float GuardDamageMultiplier = 0.15f;
		float GuardFrontalAngleDegrees = 65.0f;

		// ---- 散弾(Shotgun) ----
		int32 PelletCount = 1;
		float PelletSpreadDegrees = 8.0f;

		// ---- 照射ビーム(ContinuousBeam) ----
		int32 BeamDamagePerSecond = 300;
		float BeamTickInterval = 0.1f;
		float BeamRadius = 0.15f;
		int32 BeamDownPointPerSecond = 20;

		// ---- 格闘(Melee) ----
		FString MeleeMotionType = TEXT("Slash");

		// ---- 弾道エフェクト(Ranged) ----
		FLinearColor TracerColor = FLinearColor::White;
		float TracerWidth = 0.04f;
		float TracerLifetime = 0.12f;
		float ProjectileSpeed = 0.0f;
	};

	FMSChargeShotMultipliers ChargeMultipliers(float Damage, float DownPoint,
		float ProjectileSpeed, float SplashRadius)
	{
		FMSChargeShotMultipliers Result;
		Result.Damage = Damage;
		Result.DownPoint = DownPoint;
		Result.ProjectileSpeed = ProjectileSpeed;
		Result.SplashRadius = SplashRadius;
		return Result;
	}

	/**
	 * IsDataValid(MSWeaponData.cpp)を満たすための、weapon_import_plan.json原値からの補正一覧
	 * (このコマンドレット固有の対応。#P1-3タスク指示「IsDataValidでエラーが出た場合はJSON値や
	 * コマンドレットのフィールド設定を修正して再実行」に基づく):
	 *
	 *  - Shield 3種(DefenderShieldDS1/FortressShieldFS3/LightShieldLS2): 原値 Range=0.0 → 5.0 に補正。
	 *    Range<=0 は基底IsDataValidで無条件エラーのため(Shieldは近接ガードのリーチとして意味を持たせる)。
	 *  - ContinuousBeam 2種(ConvergenceBeamCB9/FocusBeamFB2): 原値 FireInterval=0.0 → BeamTickIntervalと
	 *    同値(0.1)に補正。基底IsDataValidが FireInterval<=0 を無条件エラーにするため
	 *    (照射ビームの実際のダメージ間隔はBeamTickIntervalが担う。ガードレールB-6踏襲)。
	 *  - スコープ狙撃武器4種(LongRifleLR8/LongRifleLR8F/QuickSniperQS2/SniperRifle): 原値 LockOnRange=0.0 は
	 *    Unity版の既知バグ(新武器のlockOnRange未設定)を再現してしまうため、他武器と同じ Range*0.9 の
	 *    パターンで補正(ガードレールB-5が意図的に検出する不備のため、意図的にゼロのまま出荷しない)。
	 *  - SmokeGrenade: 原値 SplashDamage=0 かつ SplashRadius=5.0(視界遮蔽目的の非ダメージ範囲効果)は
	 *    「SplashRadius>0ならSplashDamage>0必須」の派生クラスバリデーションに抵触するため、
	 *    ダメージ実質ゼロの名目値 SplashDamage=1 に補正(視界遮蔽ロジック自体は本コマンドレットの範囲外)。
	 */
	TArray<FMSWeaponDef> BuildWeaponDefinitions()
	{
		using T = FMSWeaponDef;
		return {
			T{ .AssetName = TEXT("DA_Weapon_BR"), .WeaponName = TEXT("ビームライフルSA"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 2200, .FireInterval = 1.5f, .DownPoint = 15, .MagazineSize = 50, .ReserveAmmo = 36, .ReloadTime = 4.5f,
			   .Range = 300.0f, .LockOnRange = 250.0f, .Weight = 14, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.3207547f, 0.14676042f, 0.28036723f, 0.95f), .TracerWidth = 0.12f, .TracerLifetime = 0.2f },

			T{ .AssetName = TEXT("DA_Weapon_AssaultVulcanAV2"), .WeaponName = TEXT("アサルトバルカンAV-2"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 140, .FireInterval = 0.09f, .DownPoint = 12, .MagazineSize = 55, .ReserveAmmo = 275, .ReloadTime = 2.8f,
			   .Range = 160.0f, .LockOnRange = 144.0f, .Weight = 9, .WeaponSwitchDuration = 0.5f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_AssaultVulcanAV2F"), .WeaponName = TEXT("アサルトバルカンAV-2F"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 196, .FireInterval = 0.1f, .DownPoint = 13, .MagazineSize = 35, .ReserveAmmo = 175, .ReloadTime = 2.8f,
			   .Range = 110.0f, .LockOnRange = 99.0f, .Weight = 11, .WeaponSwitchDuration = 0.5f,
			   .TracerColor = FLinearColor(1.0f, 0.8f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_AssaultVulcanAV2M"), .WeaponName = TEXT("アサルトバルカンAV-2M"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 130, .FireInterval = 0.09f, .DownPoint = 16, .MagazineSize = 100, .ReserveAmmo = 300, .ReloadTime = 3.2f,
			   .Range = 155.0f, .LockOnRange = 139.5f, .Weight = 10, .WeaponSwitchDuration = 0.5f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BeamCannon"), .WeaponName = TEXT("ビームキャノン"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 4500, .FireInterval = 2.8f, .DownPoint = 15, .MagazineSize = 3, .ReserveAmmo = 9, .ReloadTime = 6.0f,
			   .Range = 350.0f, .LockOnRange = 300.0f, .Weight = 22, .WeaponSwitchDuration = 1.9f,
			   .TracerColor = FLinearColor(0.6f, 0.3f, 1.0f, 0.95f), .TracerWidth = 0.35f, .TracerLifetime = 0.28f },

			T{ .AssetName = TEXT("DA_Weapon_BeamCarbineBK2"), .WeaponName = TEXT("ビームカービンBK-2"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 290, .FireInterval = 0.12f, .DownPoint = 9, .MagazineSize = 42, .ReserveAmmo = 168, .ReloadTime = 2.8f,
			   .Range = 170.0f, .LockOnRange = 153.0f, .Weight = 10, .WeaponSwitchDuration = 0.5f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BeamLauncherBL4"), .WeaponName = TEXT("ビームランチャーBL-4"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 500, .FireInterval = 0.18f, .DownPoint = 18, .MagazineSize = 35, .ReserveAmmo = 140, .ReloadTime = 3.0f,
			   .Range = 220.0f, .LockOnRange = 198.0f, .Weight = 13, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BeamLauncherBL4F"), .WeaponName = TEXT("ビームランチャーBL-4F"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 700, .FireInterval = 0.2f, .DownPoint = 20, .MagazineSize = 22, .ReserveAmmo = 88, .ReloadTime = 3.0f,
			   .Range = 150.0f, .LockOnRange = 135.0f, .Weight = 15, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.2f, 1.0f, 0.35f, 0.95f) },

			T{ .AssetName = TEXT("DA_Weapon_BeamLauncherBL4M"), .WeaponName = TEXT("ビームランチャーBL-4M"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 430, .FireInterval = 0.16f, .DownPoint = 22, .MagazineSize = 63, .ReserveAmmo = 189, .ReloadTime = 3.4f,
			   .Range = 210.0f, .LockOnRange = 189.0f, .Weight = 14, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BeamMachineGun"), .WeaponName = TEXT("ビームマシンガン"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 450, .FireInterval = 0.15f, .DownPoint = 15, .MagazineSize = 40, .ReserveAmmo = 160, .ReloadTime = 3.0f,
			   .Range = 200.0f, .LockOnRange = 180.0f, .Weight = 12, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 1.0f, 0.95f), .TracerWidth = 0.07f, .TracerLifetime = 0.15f },

			T{ .AssetName = TEXT("DA_Weapon_BurstBeamRifleBB3"), .WeaponName = TEXT("バーストビームライフルBB-3"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 380, .FireInterval = 0.85f, .DownPoint = 14, .MagazineSize = 24, .ReserveAmmo = 96, .ReloadTime = 3.0f,
			   .Range = 200.0f, .LockOnRange = 180.0f, .Weight = 12, .WeaponSwitchDuration = 0.6f, .BurstCount = 3, .BurstInterval = 0.09f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BurstBeamRifleBB3M"), .WeaponName = TEXT("バーストビームライフルBB-3M"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 350, .FireInterval = 0.85f, .DownPoint = 16, .MagazineSize = 48, .ReserveAmmo = 144, .ReloadTime = 3.4f,
			   .Range = 190.0f, .LockOnRange = 171.0f, .Weight = 13, .WeaponSwitchDuration = 0.6f, .BurstCount = 3, .BurstInterval = 0.09f,
			   .TracerColor = FLinearColor(0.3f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_BurstCarbineBC4"), .WeaponName = TEXT("バーストカービンBC-4"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 170, .FireInterval = 0.55f, .DownPoint = 14, .MagazineSize = 30, .ReserveAmmo = 150, .ReloadTime = 2.6f,
			   .Range = 170.0f, .LockOnRange = 153.0f, .Weight = 9, .WeaponSwitchDuration = 0.5f, .BurstCount = 3, .BurstInterval = 0.07f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_ChargeBazookaCB3"), .WeaponName = TEXT("チャージバズーカCB-3"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 1800, .FireInterval = 2.2f, .DownPoint = 15, .MagazineSize = 3, .ReserveAmmo = 18, .ReloadTime = 5.5f,
			   .Range = 190.0f, .LockOnRange = 170.0f, .Weight = 18, .WeaponSwitchDuration = 2.0f,
			   // Approximation: standard Japanese CBZ F charge profile/time, not the same named weapon.
			   // Evidence: docs/Combat/ChargeRateReference20260928F.md; base Power remains NeoMSGO-authored.
			   .bIsChargeable = true, .MaxChargeTime = 2.2f, .bUseChargeProfile = true,
			   .ChargeHalf = ChargeMultipliers(1.2f, 1.2f, 1.1f, 1.2f),
			   .ChargeFull = ChargeMultipliers(1.5f, 1.5f, 1.2f, 1.5f),
			   .SplashRadius = 4.5f, .SplashDamage = 900,
			   .TracerColor = FLinearColor(1.0f, 0.55f, 0.1f, 1.0f), .TracerWidth = 0.15f, .TracerLifetime = 0.25f, .ProjectileSpeed = 35.0f },

			T{ .AssetName = TEXT("DA_Weapon_ChargeBeamRifleCB2"), .WeaponName = TEXT("チャージビームライフルCB-2"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 2200, .FireInterval = 1.5f, .DownPoint = 15, .MagazineSize = 6, .ReserveAmmo = 36, .ReloadTime = 4.5f,
			   .Range = 300.0f, .LockOnRange = 250.0f, .Weight = 14, .WeaponSwitchDuration = 0.7f,
			   // Approximation: Initial GM CBR N charge profile/time; charged DP follows damage.
			   .bIsChargeable = true, .MaxChargeTime = 1.4f, .bUseChargeProfile = true,
			   .ChargeHalf = ChargeMultipliers(1.5f, 1.5f, 1.2f, 1.0f),
			   .ChargeFull = ChargeMultipliers(2.0f, 2.0f, 1.5f, 1.0f),
			   .TracerColor = FLinearColor(0.15f, 1.0f, 0.3f, 0.95f), .TracerWidth = 0.12f, .TracerLifetime = 0.2f },

			T{ .AssetName = TEXT("DA_Weapon_ChargeGreatSwordCS1"), .WeaponName = TEXT("チャージグレートソードCS-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 1700, .FireInterval = 0.9f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.5f, .LockOnRange = 15.0f, .Weight = 9, .WeaponSwitchDuration = 0.25f,
			   .MeleeMotionType = TEXT("Overhead") },

			T{ .AssetName = TEXT("DA_Weapon_ChargeLongRifleCL6"), .WeaponName = TEXT("チャージロングライフルCL-6"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 900, .FireInterval = 1.2f, .DownPoint = 26, .MagazineSize = 10, .ReserveAmmo = 40, .ReloadTime = 3.6f,
			   .Range = 300.0f, .LockOnRange = 220.0f, .Weight = 15, .WeaponSwitchDuration = 1.2f,
			   // Custom long rifle: use CBR-type anchors, not CSR's 2x/4x; retain authored 1.6 s.
			   .bIsChargeable = true, .MaxChargeTime = 1.6f, .bUseChargeProfile = true,
			   .ChargeHalf = ChargeMultipliers(1.5f, 1.5f, 1.2f, 1.0f),
			   .ChargeFull = ChargeMultipliers(2.0f, 2.0f, 1.5f, 1.0f),
			   .TracerColor = FLinearColor(0.3f, 0.8f, 1.0f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_TAUrus_ChargeBlasterLauncher"), .WeaponName = TEXT("チャージブラスターランチャー"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 2000, .FireInterval = 1.8f, .DownPoint = 30, .MagazineSize = 4, .ReserveAmmo = 24, .ReloadTime = 5.0f,
			   .Range = 280.0f, .LockOnRange = 230.0f, .Weight = 16, .WeaponSwitchDuration = 1.0f,
			   .WeaponMeshPath = TEXT("/Game/Meshes/Weapons/TAUrus/SM_TAUrus_ChargeBlasterLauncher.SM_TAUrus_ChargeBlasterLauncher"),
			   .WeaponMeshTransform = FTransform(FRotator(0.0f, 90.0f, 0.0f)),
			   .bOverrideMuzzleTransform = true, .MuzzleOffsetOverride = FVector(95.0f, 0.0f, 0.0f), .MuzzleDirectionOverride = FVector::ForwardVector,
			   // TAU-rus custom blaster: CBR-type anchors, retaining its authored 1.5 s duration.
			   .bIsChargeable = true, .MaxChargeTime = 1.5f, .bUseChargeProfile = true,
			   .ChargeHalf = ChargeMultipliers(1.5f, 1.5f, 1.2f, 1.0f),
			   .ChargeFull = ChargeMultipliers(2.0f, 2.0f, 1.5f, 1.0f),
			   .BurstCount = 1, .bIsScopeWeapon = false, .SplashRadius = 0.0f, .SplashDamage = 0,
			   .TracerColor = FLinearColor(0.55f, 0.25f, 1.0f, 0.95f), .TracerWidth = 0.18f, .TracerLifetime = 0.22f, .ProjectileSpeed = 0.0f },

			// ConvergenceBeamCB9: FireInterval 0.0(原値)→BeamTickIntervalと同値0.1に補正(基底IsDataValid対応)
			T{ .AssetName = TEXT("DA_Weapon_ConvergenceBeamCB9"), .WeaponName = TEXT("収束ビーム砲CB-9"), .Category = TEXT("ContinuousBeam"), .UEClassName = TEXT("UMSContinuousBeamWeaponData"),
			   .Power = 0, .FireInterval = 0.1f, .DownPoint = 0, .MagazineSize = 120, .ReserveAmmo = 240, .ReloadTime = 5.0f,
			   .Range = 60.0f, .LockOnRange = 60.0f, .Weight = 18, .WeaponSwitchDuration = 1.8f,
			   .BeamDamagePerSecond = 320, .BeamTickInterval = 0.1f, .BeamRadius = 0.18f, .BeamDownPointPerSecond = 25,
			   .TracerColor = FLinearColor(0.25f, 0.85f, 1.0f, 0.95f), .TracerWidth = 0.2f, .TracerLifetime = 0.12f },

			// DefenderShieldDS1: Range 0.0(原値)→5.0に補正(基底IsDataValid「Range>0」対応。ガード有効間合いとして意味付け)
			T{ .AssetName = TEXT("DA_Weapon_DefenderShieldDS1"), .WeaponName = TEXT("ディフェンダーシールドDS-1"), .Category = TEXT("Shield"), .UEClassName = TEXT("UMSShieldWeaponData"),
			   .Power = 0, .FireInterval = 0.25f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 3.0f,
			   .Range = 5.0f, .LockOnRange = 0.0f, .Weight = 12, .WeaponSwitchDuration = 0.5f,
			   .GuardDamageMultiplier = 0.15f, .GuardFrontalAngleDegrees = 65.0f,
			   .TracerColor = FLinearColor(1.0f, 0.95f, 0.6f, 0.9f) },

			// FocusBeamFB2: FireInterval 0.0(原値)→0.1に補正(ConvergenceBeamCB9と同様の理由)
			T{ .AssetName = TEXT("DA_Weapon_FocusBeamFB2"), .WeaponName = TEXT("フォーカスビームFB-2"), .Category = TEXT("ContinuousBeam"), .UEClassName = TEXT("UMSContinuousBeamWeaponData"),
			   .Power = 0, .FireInterval = 0.1f, .DownPoint = 0, .MagazineSize = 100, .ReserveAmmo = 200, .ReloadTime = 5.0f,
			   .Range = 55.0f, .LockOnRange = 55.0f, .Weight = 16, .WeaponSwitchDuration = 1.8f,
			   .BeamDamagePerSecond = 380, .BeamTickInterval = 0.1f, .BeamRadius = 0.16f, .BeamDownPointPerSecond = 28,
			   .TracerColor = FLinearColor(0.4f, 0.6f, 1.0f, 0.95f), .TracerWidth = 0.18f, .TracerLifetime = 0.12f },

			// FortressShieldFS3: Range 0.0(原値)→5.0に補正(DefenderShieldDS1と同様の理由)
			T{ .AssetName = TEXT("DA_Weapon_FortressShieldFS3"), .WeaponName = TEXT("フォートレスシールドFS-3"), .Category = TEXT("Shield"), .UEClassName = TEXT("UMSShieldWeaponData"),
			   .Power = 0, .FireInterval = 0.25f, .DownPoint = 0, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 3.0f,
			   .Range = 5.0f, .LockOnRange = 0.0f, .Weight = 18, .WeaponSwitchDuration = 0.9f,
			   .GuardDamageMultiplier = 0.07f, .GuardFrontalAngleDegrees = 78.0f,
			   .TracerColor = FLinearColor(1.0f, 0.95f, 0.6f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_GatlingGun"), .WeaponName = TEXT("ガトリング砲GT-6"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 35, .FireInterval = 1.4f, .DownPoint = 15, .MagazineSize = 80, .ReserveAmmo = 160, .ReloadTime = 4.5f,
			   .Range = 70.0f, .LockOnRange = 130.0f, .Weight = 22, .WeaponSwitchDuration = 1.3f, .BurstCount = 20, .BurstInterval = 0.045f,
			   .TracerColor = FLinearColor(1.0f, 0.55f, 0.15f, 0.95f), .TracerWidth = 0.035f, .TracerLifetime = 0.08f, .ProjectileSpeed = 110.0f },

			T{ .AssetName = TEXT("DA_Weapon_GrappleKnuckleGK3"), .WeaponName = TEXT("グラップルナックルGK-3"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 2100, .FireInterval = 0.6f, .DownPoint = 35, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 5.0f, .LockOnRange = 13.0f, .Weight = 5, .WeaponSwitchDuration = 0.15f,
			   .MeleeMotionType = TEXT("Punch") },

			T{ .AssetName = TEXT("DA_Weapon_Grenade"), .WeaponName = TEXT("グレネード"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 400, .FireInterval = 1.8f, .DownPoint = 15, .MagazineSize = 3, .ReserveAmmo = 9, .ReloadTime = 3.5f,
			   .Range = 60.0f, .LockOnRange = 54.0f, .Weight = 6, .WeaponSwitchDuration = 0.6f,
			   .SplashRadius = 3.5f, .SplashDamage = 500,
			   .TracerColor = FLinearColor(0.4f, 0.45f, 0.35f, 1.0f), .TracerWidth = 0.08f, .TracerLifetime = 0.2f, .ProjectileSpeed = 12.0f },

			T{ .AssetName = TEXT("DA_Weapon_GrenadeLauncherGL2"), .WeaponName = TEXT("グレネードランチャーGL-2"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 850, .FireInterval = 0.6f, .DownPoint = 18, .MagazineSize = 6, .ReserveAmmo = 24, .ReloadTime = 3.5f,
			   .Range = 140.0f, .LockOnRange = 126.0f, .Weight = 12, .WeaponSwitchDuration = 1.0f,
			   .SplashRadius = 3.0f, .SplashDamage = 350,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 30.0f },

			T{ .AssetName = TEXT("DA_Weapon_GrenadeLauncherGL2W"), .WeaponName = TEXT("グレネードランチャーGL-2W"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Wide"),
			   .Power = 700, .FireInterval = 0.6f, .DownPoint = 16, .MagazineSize = 6, .ReserveAmmo = 24, .ReloadTime = 3.5f,
			   .Range = 130.0f, .LockOnRange = 117.0f, .Weight = 13, .WeaponSwitchDuration = 1.0f,
			   .SplashRadius = 5.0f, .SplashDamage = 280,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 27.0f },

			T{ .AssetName = TEXT("DA_Weapon_HeadVulcan"), .WeaponName = TEXT("頭部バルカン"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 80, .FireInterval = 0.12f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 80.0f, .LockOnRange = 150.0f, .Weight = 4, .bIsBuiltIn = true, .WeaponSwitchDuration = 0.0f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f), .TracerWidth = 0.03f, .TracerLifetime = 0.1f },

			T{ .AssetName = TEXT("DA_Weapon_HeatAxeHA2"), .WeaponName = TEXT("ヒートアクスHA-2"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 2900, .FireInterval = 1.0f, .DownPoint = 40, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.0f, .LockOnRange = 14.0f, .Weight = 8, .WeaponSwitchDuration = 0.22f,
			   .MeleeMotionType = TEXT("Overhead") },

			T{ .AssetName = TEXT("DA_Weapon_HeatAxeHA2F"), .WeaponName = TEXT("ヒートアクスHA-2F"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 3600, .FireInterval = 1.3f, .DownPoint = 45, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 5.5f, .LockOnRange = 13.0f, .Weight = 9, .WeaponSwitchDuration = 0.25f,
			   .MeleeMotionType = TEXT("Overhead") },

			T{ .AssetName = TEXT("DA_Weapon_HeatLancerHL1"), .WeaponName = TEXT("ヒートランサーHL-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 1800, .FireInterval = 0.85f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.0f, .LockOnRange = 15.0f, .Weight = 6, .WeaponSwitchDuration = 0.18f,
			   .MeleeMotionType = TEXT("Thrust") },

			T{ .AssetName = TEXT("DA_Weapon_HeatSwordHS1"), .WeaponName = TEXT("ヒートソードHS-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 2300, .FireInterval = 0.7f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.0f, .LockOnRange = 14.0f, .Weight = 6, .WeaponSwitchDuration = 0.18f,
			   .MeleeMotionType = TEXT("Slash") },

			T{ .AssetName = TEXT("DA_Weapon_HeavyGatlingHG7"), .WeaponName = TEXT("重ガトリングHG-7"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 225, .FireInterval = 0.133333333f, .DownPoint = 8, .MagazineSize = 150, .ReserveAmmo = 450, .ReloadTime = 3.8f,
			   .Range = 140.0f, .LockOnRange = 126.0f, .Weight = 15, .WeaponSwitchDuration = 1.4f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_HeavyGatlingHG7M"), .WeaponName = TEXT("重ガトリングHG-7M"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 190, .FireInterval = 0.133333333f, .DownPoint = 10, .MagazineSize = 260, .ReserveAmmo = 520, .ReloadTime = 4.2f,
			   .Range = 135.0f, .LockOnRange = 121.5f, .Weight = 17, .WeaponSwitchDuration = 1.4f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_HeavyMortarEX5"), .WeaponName = TEXT("重榴弾砲EX-5"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 3000, .FireInterval = 2.0f, .DownPoint = 35, .MagazineSize = 5, .ReserveAmmo = 20, .ReloadTime = 4.5f,
			   .Range = 180.0f, .LockOnRange = 162.0f, .Weight = 16, .WeaponSwitchDuration = 2.0f,
			   .SplashRadius = 4.0f, .SplashDamage = 1000,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 38.0f },

			T{ .AssetName = TEXT("DA_Weapon_HeavyMortarEX5F"), .WeaponName = TEXT("貫通榴弾砲EX-5F"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 4150, .FireInterval = 2.2f, .DownPoint = 38, .MagazineSize = 4, .ReserveAmmo = 16, .ReloadTime = 5.0f,
			   .Range = 120.0f, .LockOnRange = 108.0f, .Weight = 19, .WeaponSwitchDuration = 2.0f,
			   .SplashRadius = 3.0f, .SplashDamage = 875,
			   .TracerColor = FLinearColor(1.0f, 0.5f, 0.15f, 0.95f), .ProjectileSpeed = 40.0f },

			T{ .AssetName = TEXT("DA_Weapon_HeavyMortarEX5W"), .WeaponName = TEXT("拡散榴弾砲EX-5W"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Wide"),
			   .Power = 2200, .FireInterval = 2.0f, .DownPoint = 40, .MagazineSize = 5, .ReserveAmmo = 20, .ReloadTime = 4.5f,
			   .Range = 170.0f, .LockOnRange = 153.0f, .Weight = 17, .WeaponSwitchDuration = 2.0f,
			   .SplashRadius = 7.0f, .SplashDamage = 810,
			   .TracerColor = FLinearColor(1.0f, 0.6f, 0.2f, 0.9f), .ProjectileSpeed = 26.0f },

			T{ .AssetName = TEXT("DA_Weapon_HomingMissile"), .WeaponName = TEXT("追尾式ミサイル"), .Category = TEXT("Homing"), .UEClassName = TEXT("UMSHomingWeaponData"),
			   .Power = 1800, .FireInterval = 2.0f, .DownPoint = 15, .MagazineSize = 4, .ReserveAmmo = 12, .ReloadTime = 4.5f,
			   .Range = 250.0f, .LockOnRange = 150.0f, .Weight = 14, .WeaponSwitchDuration = 0.8f,
			   .HomingLockOnDwellTime = 0.8f, .HomingTurnRateDegPerSec = 110.0f,
			   .SplashRadius = 2.5f, .SplashDamage = 400,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.2f, 1.0f), .TracerWidth = 0.1f, .TracerLifetime = 0.2f, .ProjectileSpeed = 25.0f },

			T{ .AssetName = TEXT("DA_Weapon_LightMachineGunLM6"), .WeaponName = TEXT("軽量マシンガンLM-6"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 110, .FireInterval = 0.08f, .DownPoint = 10, .MagazineSize = 60, .ReserveAmmo = 300, .ReloadTime = 2.5f,
			   .Range = 150.0f, .LockOnRange = 135.0f, .Weight = 7, .WeaponSwitchDuration = 0.4f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_LightMachineGunLM6F"), .WeaponName = TEXT("軽量マシンガンLM-6F"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 155, .FireInterval = 0.08f, .DownPoint = 11, .MagazineSize = 50, .ReserveAmmo = 250, .ReloadTime = 2.7f,
			   .Range = 120.0f, .LockOnRange = 108.0f, .Weight = 8, .WeaponSwitchDuration = 0.4f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_LightMachineGunLM6M"), .WeaponName = TEXT("軽量マシンガンLM-6M"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 105, .FireInterval = 0.08f, .DownPoint = 13, .MagazineSize = 120, .ReserveAmmo = 360, .ReloadTime = 2.9f,
			   .Range = 145.0f, .LockOnRange = 130.5f, .Weight = 8, .WeaponSwitchDuration = 0.4f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.5f, 0.9f) },

			// LightShieldLS2: Range 0.0(原値)→5.0に補正(DefenderShieldDS1と同様の理由)
			T{ .AssetName = TEXT("DA_Weapon_LightShieldLS2"), .WeaponName = TEXT("ライトシールドLS-2"), .Category = TEXT("Shield"), .UEClassName = TEXT("UMSShieldWeaponData"),
			   .Power = 0, .FireInterval = 0.25f, .DownPoint = 0, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 3.0f,
			   .Range = 5.0f, .LockOnRange = 0.0f, .Weight = 7, .WeaponSwitchDuration = 0.5f,
			   .GuardDamageMultiplier = 0.35f, .GuardFrontalAngleDegrees = 55.0f,
			   .TracerColor = FLinearColor(1.0f, 0.95f, 0.6f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_LongBeamRifleLB5"), .WeaponName = TEXT("ロングビームライフルLB-5"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 850, .FireInterval = 0.55f, .DownPoint = 20, .MagazineSize = 18, .ReserveAmmo = 72, .ReloadTime = 3.2f,
			   .Range = 280.0f, .LockOnRange = 220.0f, .Weight = 13, .WeaponSwitchDuration = 0.8f,
			   .TracerColor = FLinearColor(0.3f, 0.8f, 1.0f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_LongBeamRifleLB5F"), .WeaponName = TEXT("ロングビームライフルLB-5F"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 1150, .FireInterval = 0.7f, .DownPoint = 24, .MagazineSize = 14, .ReserveAmmo = 56, .ReloadTime = 3.4f,
			   .Range = 230.0f, .LockOnRange = 207.0f, .Weight = 14, .WeaponSwitchDuration = 0.9f,
			   .TracerColor = FLinearColor(0.3f, 0.8f, 1.0f, 0.9f) },

			// LongRifleLR8: LockOnRange 0.0(原値。スコープ武器の既知バグパターン)→Range*0.9の他武器パターンで補正
			T{ .AssetName = TEXT("DA_Weapon_LongRifleLR8"), .WeaponName = TEXT("ロングライフルLR-8"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 2600, .FireInterval = 2.0f, .DownPoint = 30, .MagazineSize = 3, .ReserveAmmo = 12, .ReloadTime = 3.5f,
			   .Range = 550.0f, .LockOnRange = 495.0f, .Weight = 17, .WeaponSwitchDuration = 1.6f,
			   .bIsScopeWeapon = true, .ScopedFieldOfView = 10.0f, .ScopedSensitivityMultiplier = 0.3f,
			   .TracerColor = FLinearColor(1.0f, 1.0f, 0.7f, 0.9f), .TracerWidth = 0.05f, .TracerLifetime = 0.15f },

			// LongRifleLR8F: LongRifleLR8と同様の理由でLockOnRange補正
			T{ .AssetName = TEXT("DA_Weapon_LongRifleLR8F"), .WeaponName = TEXT("ロングライフルLR-8F"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 3600, .FireInterval = 2.6f, .DownPoint = 40, .MagazineSize = 1, .ReserveAmmo = 8, .ReloadTime = 3.2f,
			   .Range = 450.0f, .LockOnRange = 405.0f, .Weight = 18, .WeaponSwitchDuration = 1.7f,
			   .bIsScopeWeapon = true, .ScopedFieldOfView = 9.0f, .ScopedSensitivityMultiplier = 0.28f,
			   .TracerColor = FLinearColor(1.0f, 1.0f, 0.7f, 0.9f), .TracerWidth = 0.05f, .TracerLifetime = 0.15f },

			T{ .AssetName = TEXT("DA_Weapon_MidBazookaMB6"), .WeaponName = TEXT("ミッドバズーカMB-6"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 2200, .FireInterval = 1.8f, .DownPoint = 32, .MagazineSize = 4, .ReserveAmmo = 16, .ReloadTime = 4.0f,
			   .Range = 190.0f, .LockOnRange = 171.0f, .Weight = 14, .WeaponSwitchDuration = 1.8f,
			   .SplashRadius = 3.5f, .SplashDamage = 700,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 42.0f },

			T{ .AssetName = TEXT("DA_Weapon_MidBazookaMB6F"), .WeaponName = TEXT("ミッドバズーカMB-6F"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 3000, .FireInterval = 2.2f, .DownPoint = 36, .MagazineSize = 4, .ReserveAmmo = 12, .ReloadTime = 4.6f,
			   .Range = 150.0f, .LockOnRange = 135.0f, .Weight = 16, .WeaponSwitchDuration = 2.0f,
			   .SplashRadius = 3.0f, .SplashDamage = 900,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 40.0f },

			T{ .AssetName = TEXT("DA_Weapon_MidBazookaMB6M"), .WeaponName = TEXT("ミッドバズーカMB-6M"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("RapidFire"),
			   .Power = 1900, .FireInterval = 1.6f, .DownPoint = 34, .MagazineSize = 8, .ReserveAmmo = 24, .ReloadTime = 4.4f,
			   .Range = 185.0f, .LockOnRange = 166.5f, .Weight = 15, .WeaponSwitchDuration = 1.8f,
			   .SplashRadius = 3.2f, .SplashDamage = 600,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 42.0f },

			T{ .AssetName = TEXT("DA_Weapon_MidBazookaMB6W"), .WeaponName = TEXT("ミッドバズーカMB-6W"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"), .VariantType = TEXT("Wide"),
			   .Power = 1600, .FireInterval = 1.8f, .DownPoint = 28, .MagazineSize = 4, .ReserveAmmo = 16, .ReloadTime = 4.0f,
			   .Range = 180.0f, .LockOnRange = 162.0f, .Weight = 15, .WeaponSwitchDuration = 1.8f,
			   .SplashRadius = 6.0f, .SplashDamage = 500,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.3f, 0.9f), .ProjectileSpeed = 36.0f },

			T{ .AssetName = TEXT("DA_Weapon_MissilePod"), .WeaponName = TEXT("連装ミサイルポッド"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 250, .FireInterval = 2.5f, .DownPoint = 15, .MagazineSize = 12, .ReserveAmmo = 24, .ReloadTime = 4.0f,
			   .Range = 120.0f, .LockOnRange = 100.0f, .Weight = 16, .WeaponSwitchDuration = 1.2f, .BurstCount = 6, .BurstInterval = 0.08f,
			   .SplashRadius = 1.5f, .SplashDamage = 150,
			   .TracerColor = FLinearColor(1.0f, 0.6f, 0.15f, 1.0f), .TracerWidth = 0.07f, .TracerLifetime = 0.18f, .ProjectileSpeed = 22.0f },

			T{ .AssetName = TEXT("DA_Weapon_MultiHomingMissile"), .WeaponName = TEXT("多連装追尾ミサイル"), .Category = TEXT("Homing"), .UEClassName = TEXT("UMSHomingWeaponData"),
			   .Power = 700, .FireInterval = 3.5f, .DownPoint = 15, .MagazineSize = 8, .ReserveAmmo = 16, .ReloadTime = 6.0f,
			   .Range = 220.0f, .LockOnRange = 132.0f, .Weight = 20, .WeaponSwitchDuration = 1.2f,
			   .HomingLockOnDwellTime = 0.8f, .HomingTurnRateDegPerSec = 100.0f, .BurstCount = 4, .BurstInterval = 0.15f,
			   .SplashRadius = 2.0f, .SplashDamage = 250,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.2f, 1.0f), .TracerWidth = 0.1f, .TracerLifetime = 0.2f, .ProjectileSpeed = 22.0f },

			T{ .AssetName = TEXT("DA_Weapon_PowerKnucklePN1"), .WeaponName = TEXT("パワーナックルPN-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 2000, .FireInterval = 0.55f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.0f, .LockOnRange = 14.0f, .Weight = 6, .WeaponSwitchDuration = 0.1f,
			   .MeleeMotionType = TEXT("Punch") },

			// QuickSniperQS2: LockOnRange 0.0(原値)→Range*0.9で補正(LongRifleLR8系と同様の理由)
			T{ .AssetName = TEXT("DA_Weapon_QuickSniperQS2"), .WeaponName = TEXT("クイックスナイパーQS-2"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 1600, .FireInterval = 1.1f, .DownPoint = 20, .MagazineSize = 5, .ReserveAmmo = 20, .ReloadTime = 3.0f,
			   .Range = 420.0f, .LockOnRange = 378.0f, .Weight = 14, .WeaponSwitchDuration = 1.3f,
			   .bIsScopeWeapon = true, .ScopedFieldOfView = 14.0f, .ScopedSensitivityMultiplier = 0.4f,
			   .TracerColor = FLinearColor(1.0f, 1.0f, 0.7f, 0.9f), .TracerWidth = 0.05f, .TracerLifetime = 0.15f },

			T{ .AssetName = TEXT("DA_Weapon_RapidBeamRifle"), .WeaponName = TEXT("連射ビームライフル"), .Category = TEXT("BeamRifle"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 900, .FireInterval = 0.35f, .DownPoint = 15, .MagazineSize = 12, .ReserveAmmo = 48, .ReloadTime = 3.5f,
			   .Range = 280.0f, .LockOnRange = 220.0f, .Weight = 15, .WeaponSwitchDuration = 0.6f,
			   .TracerColor = FLinearColor(0.2f, 1.0f, 0.5f, 0.95f), .TracerWidth = 0.13f, .TracerLifetime = 0.18f },

			T{ .AssetName = TEXT("DA_Weapon_RepairPodDeployer"), .WeaponName = TEXT("リペアポッド設置装置"), .Category = TEXT("Supply"), .UEClassName = TEXT("UMSSupplyWeaponData"),
			   .Power = 0, .FireInterval = 1.5f, .DownPoint = 15, .MagazineSize = 1, .ReserveAmmo = 2, .ReloadTime = 8.0f,
			   .Range = 5.0f, .LockOnRange = 0.0f, .Weight = 12, .WeaponSwitchDuration = 0.4f,
			   .bIsDeployable = true, .DeployedPodLifetime = 45.0f, .DeployedItemType = TEXT("RepairPod"),
			   .TracerColor = FLinearColor(0.25f, 1.0f, 0.55f, 0.9f) },

			// Additional support catalog item. A dedicated recipe creates only this asset in live projects;
			// this row preserves it if the full generator is ever intentionally rerun.
			T{ .AssetName = TEXT("DA_Weapon_RadarPodDeployer"), .WeaponName = TEXT("レーダーポッド設置装置"), .Category = TEXT("Supply"), .UEClassName = TEXT("UMSSupplyWeaponData"),
			   .Power = 0, .FireInterval = 1.5f, .DownPoint = 15, .MagazineSize = 1, .ReserveAmmo = 2, .ReloadTime = 8.0f,
			   .Range = 3.0f, .RangeWorldScale = 1.0f, .LockOnRange = 0.0f, .Weight = 12, .WeaponSwitchDuration = 0.4f,
			   .WeaponMeshPath = TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/SM_RPD_RepairPodDeployer.SM_RPD_RepairPodDeployer"),
			   .WeaponMeshTransform = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(193.0f / 1800.0f)),
			   .bIsDeployable = true, .DeployedPodLifetime = 150.0f, .DeployedItemType = TEXT("RadarPod"), .RadarRangeStat = 4800.0f,
			   .TracerColor = FLinearColor(0.25f, 1.0f, 0.55f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_ScatterShotSS3"), .WeaponName = TEXT("スキャッターショットSS-3"), .Category = TEXT("Shotgun"), .UEClassName = TEXT("UMSShotgunWeaponData"),
			   .Power = 100, .FireInterval = 1.0f / 1.12f, .DownPoint = 12, .MagazineSize = 5, .ReserveAmmo = 20, .ReloadTime = 3.2f,
			   .Range = 300.0f, .RangeWorldScale = 1.0f, .LockOnRange = 300.0f, .RedLockDistanceWorldCm = 20000.0f, .Weight = 13, .WeaponSwitchDuration = 0.7f,
			   .PelletCount = 5, .PelletSpreadDegrees = 9.0f,
			   .TracerColor = FLinearColor(1.0f, 0.8f, 0.3f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_ScatterShotSS3F"), .WeaponName = TEXT("スキャッターショットSS-3F"), .Category = TEXT("Shotgun"), .UEClassName = TEXT("UMSShotgunWeaponData"), .VariantType = TEXT("Firepower"),
			   .Power = 110, .FireInterval = 1.0f / 1.12f, .DownPoint = 14, .MagazineSize = 3, .ReserveAmmo = 15, .ReloadTime = 3.2f,
			   .Range = 240.0f, .RangeWorldScale = 1.0f, .LockOnRange = 240.0f, .RedLockDistanceWorldCm = 19000.0f, .Weight = 14, .WeaponSwitchDuration = 0.7f,
			   .PelletCount = 5, .PelletSpreadDegrees = 12.0f,
			   .TracerColor = FLinearColor(1.0f, 0.8f, 0.3f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_SeekerMissileSM4"), .WeaponName = TEXT("シーカーミサイルSM-4"), .Category = TEXT("Homing"), .UEClassName = TEXT("UMSHomingWeaponData"),
			   .Power = 1500, .FireInterval = 1.8f, .DownPoint = 25, .MagazineSize = 4, .ReserveAmmo = 16, .ReloadTime = 4.2f,
			   .Range = 240.0f, .LockOnRange = 144.0f, .Weight = 13, .WeaponSwitchDuration = 0.9f,
			   .HomingLockOnDwellTime = 0.8f, .HomingTurnRateDegPerSec = 100.0f,
			   .SplashRadius = 2.5f, .SplashDamage = 350,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.2f, 1.0f), .TracerWidth = 0.1f, .TracerLifetime = 0.2f, .ProjectileSpeed = 28.0f },

			T{ .AssetName = TEXT("DA_Weapon_SeekerMissileSM4W"), .WeaponName = TEXT("シーカーミサイルSM-4W"), .Category = TEXT("Homing"), .UEClassName = TEXT("UMSHomingWeaponData"), .VariantType = TEXT("Wide"),
			   .Power = 1200, .FireInterval = 1.8f, .DownPoint = 22, .MagazineSize = 4, .ReserveAmmo = 16, .ReloadTime = 4.2f,
			   .Range = 230.0f, .LockOnRange = 138.0f, .Weight = 14, .WeaponSwitchDuration = 0.9f,
			   .HomingLockOnDwellTime = 0.9f, .HomingTurnRateDegPerSec = 85.0f,
			   .SplashRadius = 4.5f, .SplashDamage = 300,
			   .TracerColor = FLinearColor(1.0f, 0.7f, 0.2f, 1.0f), .TracerWidth = 0.1f, .TracerLifetime = 0.2f, .ProjectileSpeed = 26.0f },

			T{ .AssetName = TEXT("DA_Weapon_Shotgun"), .WeaponName = TEXT("散弾砲SG-1"), .Category = TEXT("Shotgun"), .UEClassName = TEXT("UMSShotgunWeaponData"),
			   .Power = 220, .FireInterval = 1.1f, .DownPoint = 15, .MagazineSize = 6, .ReserveAmmo = 18, .ReloadTime = 3.0f,
			   .Range = 45.0f, .LockOnRange = 40.0f, .Weight = 14, .WeaponSwitchDuration = 0.7f,
			   .PelletCount = 8, .PelletSpreadDegrees = 9.0f,
			   .TracerColor = FLinearColor(1.0f, 0.8f, 0.3f, 0.9f), .TracerWidth = 0.05f, .TracerLifetime = 0.1f },

			T{ .AssetName = TEXT("DA_Weapon_ShoulderBazookaSB9"), .WeaponName = TEXT("ショルダーバズーカSB-9"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 3000, .FireInterval = 2.0f, .DownPoint = 15, .MagazineSize = 4, .ReserveAmmo = 24, .ReloadTime = 5.0f,
			   .Range = 200.0f, .LockOnRange = 180.0f, .Weight = 16, .WeaponSwitchDuration = 2.0f,
			   .SplashRadius = 4.5f, .SplashDamage = 1000,
			   .TracerColor = FLinearColor(1.0f, 0.35f, 0.1f, 1.0f), .TracerWidth = 0.15f, .TracerLifetime = 0.25f, .ProjectileSpeed = 40.0f },

			T{ .AssetName = TEXT("DA_Weapon_ShoulderRocketPod"), .WeaponName = TEXT("肩部ロケットポッド"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 800, .FireInterval = 1.2f, .DownPoint = 15, .MagazineSize = 6, .ReserveAmmo = 0, .ReloadTime = 3.5f,
			   .Range = 150.0f, .LockOnRange = 130.0f, .Weight = 8, .bIsBuiltIn = true, .WeaponSwitchDuration = 0.0f,
			   .SplashRadius = 3.0f, .SplashDamage = 300,
			   .TracerColor = FLinearColor(1.0f, 0.5f, 0.2f, 1.0f), .TracerWidth = 0.1f, .TracerLifetime = 0.2f, .ProjectileSpeed = 30.0f },

			// SmokeGrenade: SplashDamage 0(原値。視界遮蔽目的の非ダメージ範囲効果)→
			// 「SplashRadius>0ならSplashDamage>0必須」バリデーション対応のため名目値1に補正
			T{ .AssetName = TEXT("DA_Weapon_SmokeGrenade"), .WeaponName = TEXT("スモークグレネード"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 0, .FireInterval = 2.5f, .DownPoint = 15, .MagazineSize = 2, .ReserveAmmo = 4, .ReloadTime = 4.0f,
			   .Range = 50.0f, .LockOnRange = 45.0f, .Weight = 4, .WeaponSwitchDuration = 0.5f,
			   .SplashRadius = 5.0f, .SplashDamage = 1,
			   .TracerColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f), .TracerWidth = 0.08f, .TracerLifetime = 0.2f, .ProjectileSpeed = 10.0f },

			// SniperRifle: LockOnRange 0.0(原値)→Range*0.9で補正(LongRifleLR8系と同様の理由)
			T{ .AssetName = TEXT("DA_Weapon_SniperRifle"), .WeaponName = TEXT("スナイパーライフル"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 3200, .FireInterval = 2.2f, .DownPoint = 15, .MagazineSize = 1, .ReserveAmmo = 8, .ReloadTime = 3.0f,
			   .Range = 500.0f, .LockOnRange = 450.0f, .Weight = 16, .WeaponSwitchDuration = 1.6f,
			   .bIsScopeWeapon = true, .ScopedFieldOfView = 10.0f, .ScopedSensitivityMultiplier = 0.3f,
			   .TracerColor = FLinearColor(1.0f, 1.0f, 0.7f, 0.9f), .TracerWidth = 0.05f, .TracerLifetime = 0.15f },

			T{ .AssetName = TEXT("DA_Weapon_SupplyPackDeployer"), .WeaponName = TEXT("補給パック投擲装置"), .Category = TEXT("Supply"), .UEClassName = TEXT("UMSSupplyWeaponData"),
			   .Power = 0, .FireInterval = 1.2f, .DownPoint = 15, .MagazineSize = 1, .ReserveAmmo = 2, .ReloadTime = 6.0f,
			   .Range = 5.0f, .LockOnRange = 0.0f, .Weight = 8, .WeaponSwitchDuration = 0.4f,
			   .bIsDeployable = true, .DeployedPodLifetime = 30.0f, .DeployedItemType = TEXT("SupplyPackRepair"),
			   .TracerColor = FLinearColor(0.2f, 0.9f, 0.4f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_TripleRocketTR3"), .WeaponName = TEXT("3連装ロケットランチャーTR-3"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 900, .FireInterval = 2.6f, .DownPoint = 20, .MagazineSize = 9, .ReserveAmmo = 27, .ReloadTime = 4.5f,
			   .Range = 150.0f, .LockOnRange = 135.0f, .Weight = 17, .WeaponSwitchDuration = 1.6f, .BurstCount = 3, .BurstInterval = 0.12f,
			   .SplashRadius = 2.0f, .SplashDamage = 200,
			   .TracerColor = FLinearColor(1.0f, 0.85f, 0.4f, 0.9f), .ProjectileSpeed = 30.0f },

			// TAU-sw only: the dual data type drives both the two-handed visual and the Slash_Dual motion.
			// Traded against ヒートソードHS-1 (2300 / 0.7s): less per-swing, faster, so DPS lands slightly ahead.
			T{ .AssetName = TEXT("DA_Weapon_TAUsw_TwinHeatSword"), .WeaponName = TEXT("ツインヒートソードTHS-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSDualHeatSwordWeaponData"),
			   .Power = 1500, .FireInterval = 0.42f, .DownPoint = 12, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 6.0f, .LockOnRange = 14.0f, .Weight = 8, .WeaponSwitchDuration = 0.22f,
			   .WeaponMeshPath = TEXT("/Game/Meshes/Weapons/TAUsw/SM_TAUsw_HeatSword.SM_TAUsw_HeatSword"),
			   .MeleeMotionType = TEXT("Slash") },

			T{ .AssetName = TEXT("DA_Weapon_TwinDaggerTD1"), .WeaponName = TEXT("ツインダガーTD-1"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 1300, .FireInterval = 0.38f, .DownPoint = 18, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 5.5f, .LockOnRange = 14.0f, .Weight = 4, .WeaponSwitchDuration = 0.1f,
			   .MeleeMotionType = TEXT("Thrust") },

			T{ .AssetName = TEXT("DA_Weapon_TwinGrenadeLauncher"), .WeaponName = TEXT("連装擲弾筒GL-2"), .Category = TEXT("Bazooka"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 550, .FireInterval = 2.4f, .DownPoint = 15, .MagazineSize = 4, .ReserveAmmo = 8, .ReloadTime = 4.0f,
			   .Range = 90.0f, .LockOnRange = 81.0f, .Weight = 16, .WeaponSwitchDuration = 1.1f, .BurstCount = 2, .BurstInterval = 0.12f,
			   .SplashRadius = 3.5f, .SplashDamage = 380,
			   .TracerColor = FLinearColor(0.65f, 0.85f, 0.3f, 1.0f), .TracerWidth = 0.08f, .TracerLifetime = 0.2f, .ProjectileSpeed = 16.0f },

			T{ .AssetName = TEXT("DA_Weapon_VulcanMG40"), .WeaponName = TEXT("バルカンMG-40"), .Category = TEXT("MachineGun"), .UEClassName = TEXT("UMSRangedWeaponData"),
			   .Power = 130, .FireInterval = 0.08f, .DownPoint = 15, .MagazineSize = 60, .ReserveAmmo = 300, .ReloadTime = 3.0f,
			   .Range = 150.0f, .LockOnRange = 150.0f, .Weight = 8, .WeaponSwitchDuration = 0.5f,
			   .TracerColor = FLinearColor(1.0f, 0.9f, 0.4f, 0.95f), .TracerWidth = 0.03f, .TracerLifetime = 0.15f, .ProjectileSpeed = 110.0f },

			T{ .AssetName = TEXT("DA_Weapon_WideShotgunSG2W"), .WeaponName = TEXT("拡散ショットガンSG-2W"), .Category = TEXT("Shotgun"), .UEClassName = TEXT("UMSShotgunWeaponData"), .VariantType = TEXT("Wide"),
			   .Power = 100, .FireInterval = 1.0f / 1.12f, .DownPoint = 10, .MagazineSize = 5, .ReserveAmmo = 20, .ReloadTime = 3.2f,
			   .Range = 300.0f, .RangeWorldScale = 1.0f, .LockOnRange = 300.0f, .RedLockDistanceWorldCm = 20000.0f, .Weight = 15, .WeaponSwitchDuration = 0.7f,
			   .PelletCount = 5, .PelletSpreadDegrees = 14.0f,
			   .TracerColor = FLinearColor(1.0f, 0.95f, 0.6f, 0.9f) },

			T{ .AssetName = TEXT("DA_Weapon_heat_kunai"), .WeaponName = TEXT("コールドクナイD型"), .Category = TEXT("Melee"), .UEClassName = TEXT("UMSMeleeWeaponData"),
			   .Power = 3800, .FireInterval = 0.7f, .DownPoint = 15, .MagazineSize = 0, .ReserveAmmo = 0, .ReloadTime = 0.0f,
			   .Range = 2.0f, .LockOnRange = 12.0f, .Weight = 6, .WeaponSwitchDuration = 0.1f,
			   .MeleeMotionType = TEXT("Slash") },
		};
	}

	UClass* ResolveWeaponClass(const FString& ClassName)
	{
		if (ClassName == TEXT("UMSRangedWeaponData")) return UMSRangedWeaponData::StaticClass();
		if (ClassName == TEXT("UMSShotgunWeaponData")) return UMSShotgunWeaponData::StaticClass();
		if (ClassName == TEXT("UMSHomingWeaponData")) return UMSHomingWeaponData::StaticClass();
		if (ClassName == TEXT("UMSContinuousBeamWeaponData")) return UMSContinuousBeamWeaponData::StaticClass();
		if (ClassName == TEXT("UMSMeleeWeaponData")) return UMSMeleeWeaponData::StaticClass();
		if (ClassName == TEXT("UMSDualHeatSwordWeaponData")) return UMSDualHeatSwordWeaponData::StaticClass();
		if (ClassName == TEXT("UMSShieldWeaponData")) return UMSShieldWeaponData::StaticClass();
		if (ClassName == TEXT("UMSSupplyWeaponData")) return UMSSupplyWeaponData::StaticClass();
		return nullptr;
	}

	FGameplayTag ResolveCategoryTag(const FString& Category)
	{
		if (Category == TEXT("MachineGun")) return MSTags::Weapon_Category_MachineGun;
		if (Category == TEXT("BeamRifle")) return MSTags::Weapon_Category_BeamRifle;
		if (Category == TEXT("Bazooka")) return MSTags::Weapon_Category_Bazooka;
		if (Category == TEXT("Melee")) return MSTags::Weapon_Category_Melee;
		if (Category == TEXT("Shield")) return MSTags::Weapon_Category_Shield;
		if (Category == TEXT("Supply")) return MSTags::Weapon_Category_Supply;
		if (Category == TEXT("ContinuousBeam")) return MSTags::Weapon_Category_ContinuousBeam;
		if (Category == TEXT("Shotgun")) return MSTags::Weapon_Category_Shotgun;
		if (Category == TEXT("Homing")) return MSTags::Weapon_Category_Homing;
		return FGameplayTag();
	}

	EMSWeaponVariantType ResolveVariantType(const FString& S)
	{
		if (S == TEXT("RapidFire")) return EMSWeaponVariantType::RapidFire;
		if (S == TEXT("Firepower")) return EMSWeaponVariantType::Firepower;
		if (S == TEXT("Wide")) return EMSWeaponVariantType::Wide;
		return EMSWeaponVariantType::Standard;
	}

	EMSMeleeMotionType ResolveMeleeMotionType(const FString& S)
	{
		if (S == TEXT("Overhead")) return EMSMeleeMotionType::Overhead;
		if (S == TEXT("Thrust")) return EMSMeleeMotionType::Thrust;
		if (S == TEXT("Punch")) return EMSMeleeMotionType::Punch;
		return EMSMeleeMotionType::Slash;
	}

	EMSDeployedItemType ResolveDeployedItemType(const FString& S)
	{
		if (S == TEXT("SupplyPackRepair")) return EMSDeployedItemType::SupplyPackRepair;
		if (S == TEXT("SupplyPackAmmo")) return EMSDeployedItemType::SupplyPackAmmo;
		if (S == TEXT("RadarPod")) return EMSDeployedItemType::RadarPod;
		return EMSDeployedItemType::RepairPod;
	}

	// Mirrors the named rows in Tools/NewYorkTaigan/apply_weapon_reference_balance.py.
	// Keep this narrow: all other legacy definitions retain their original balance and scale.
	void ApplyNewYorkReferenceBalance(FMSWeaponDef& Def)
	{
		const FString& N = Def.AssetName;
		auto SetRange = [&Def](int32 Power, float Range, float LockRange)
		{
			Def.Power = Power;
			Def.Range = Range;
			Def.LockOnRange = LockRange;
			Def.RangeWorldScale = 1.0f;
		};
		if (N == TEXT("DA_Weapon_BR") || N == TEXT("DA_Weapon_RapidBeamRifle"))
		{
			SetRange(300, 810.0f, 810.0f);
			Def.FireInterval = 0.35f; Def.MagazineSize = 14; Def.ReserveAmmo = 70;
			Def.ReloadTime = 3.4f; Def.RedLockDistanceWorldCm = 33000.0f;
		}
		else if (N == TEXT("DA_Weapon_Shotgun"))
		{
			SetRange(100, 300.0f, 300.0f);
			Def.FireInterval = 1.0f / 1.12f; Def.MagazineSize = 5; Def.ReserveAmmo = 20;
			Def.ReloadTime = 3.2f; Def.PelletCount = 5; Def.RedLockDistanceWorldCm = 20000.0f;
		}
		else if (N == TEXT("DA_Weapon_ScatterShotSS3") || N == TEXT("DA_Weapon_WideShotgunSG2W"))
		{
			// Custom N and provisional wide archetypes keep their authored pellet spread.
			SetRange(100, 300.0f, 300.0f);
			Def.FireInterval = 1.0f / 1.12f; Def.MagazineSize = 5; Def.ReserveAmmo = 20;
			Def.ReloadTime = 3.2f; Def.PelletCount = 5; Def.RedLockDistanceWorldCm = 20000.0f;
		}
		else if (N == TEXT("DA_Weapon_ScatterShotSS3F"))
		{
			// Zaku II F-type shotgun F archetype; custom visual/spread remain intact.
			SetRange(110, 240.0f, 240.0f);
			Def.FireInterval = 1.0f / 1.12f; Def.MagazineSize = 3; Def.ReserveAmmo = 15;
			Def.ReloadTime = 3.2f; Def.PelletCount = 5; Def.RedLockDistanceWorldCm = 19000.0f;
		}
		else if (N == TEXT("DA_Weapon_HeadVulcan"))
		{
			SetRange(55, 480.0f, 480.0f);
			Def.FireInterval = 0.15f; Def.MagazineSize = 45; Def.ReserveAmmo = 315;
			Def.ReloadTime = 1.4f; Def.RedLockDistanceWorldCm = 18000.0f;
		}
		else if (N == TEXT("DA_Weapon_HeatSwordHS1"))
		{
			Def.Power = 350; // Melee reach and animation remain on their legacy contract.
		}
		else if (N == TEXT("DA_Weapon_MidBazookaMB6") || N == TEXT("DA_Weapon_ShoulderBazookaSB9"))
		{
			SetRange(850, 600.0f, 600.0f);
			Def.SplashDamage = 850; Def.MagazineSize = 1; Def.ReserveAmmo = 8;
			Def.ReloadTime = 3.0f; Def.RedLockDistanceWorldCm = 18000.0f;
		}
		else if (N == TEXT("DA_Weapon_MidBazookaMB6F"))
		{
			SetRange(1450, 480.0f, 480.0f);
			Def.SplashDamage = 1450; Def.MagazineSize = 1; Def.ReserveAmmo = 3;
			Def.ReloadTime = 4.0f; Def.RedLockDistanceWorldCm = 17000.0f;
		}
		else if (N == TEXT("DA_Weapon_MidBazookaMB6M"))
		{
			SetRange(575, 600.0f, 600.0f);
			Def.SplashDamage = 575; Def.MagazineSize = 3; Def.ReserveAmmo = 21;
			Def.ReloadTime = 4.4f; Def.RedLockDistanceWorldCm = 16000.0f;
		}
		else if (N == TEXT("DA_Weapon_MidBazookaMB6W"))
		{
			SetRange(850, 600.0f, 600.0f);
			Def.SplashDamage = 850; Def.MagazineSize = 1; Def.ReserveAmmo = 8;
			Def.ReloadTime = 3.0f; Def.RedLockDistanceWorldCm = 18000.0f;
		}
		else if (N == TEXT("DA_Weapon_HomingMissile") || N == TEXT("DA_Weapon_MissilePod")
			|| N == TEXT("DA_Weapon_SeekerMissileSM4") || N == TEXT("DA_Weapon_SeekerMissileSM4W"))
		{
			SetRange(300, 690.0f, 690.0f);
			Def.SplashDamage = 300; Def.ReloadTime = 2.6f;
		}
		else if (N == TEXT("DA_Weapon_LightMachineGunLM6") || N == TEXT("DA_Weapon_VulcanMG40"))
		{
			SetRange(290, 570.0f, 570.0f);
			Def.FireInterval = 1.0f / 6.0f; Def.MagazineSize = 35; Def.ReserveAmmo = 245;
			Def.ReloadTime = 2.0f; Def.RedLockDistanceWorldCm = 28000.0f;
		}
		else if (N == TEXT("DA_Weapon_LightMachineGunLM6F"))
		{
			SetRange(330, 510.0f, 510.0f);
			Def.FireInterval = 1.0f / 6.0f; Def.MagazineSize = 25; Def.ReserveAmmo = 175;
			Def.ReloadTime = 2.0f; Def.RedLockDistanceWorldCm = 27000.0f;
		}
		else if (N == TEXT("DA_Weapon_LightMachineGunLM6M"))
		{
			SetRange(240, 540.0f, 540.0f);
			Def.FireInterval = 0.15f; Def.MagazineSize = 70; Def.ReserveAmmo = 420;
			Def.ReloadTime = 3.4f; Def.RedLockDistanceWorldCm = 26000.0f;
		}
	}

	float ResolveRedLockDistance(const FMSWeaponDef& Def)
	{
		if (Def.RedLockDistanceWorldCm >= 0.0f)
		{
			return Def.RedLockDistanceWorldCm;
		}
		if (Def.RedLockDistanceMeters >= 0.0f)
		{
			return Def.RedLockDistanceMeters * 100.0f * MSGO::MSScale;
		}
		if (Def.Category == TEXT("Melee") || Def.Category == TEXT("Shield") || Def.Category == TEXT("Supply"))
		{
			return 0.0f;
		}
		if (Def.bIsScopeWeapon)
		{
			return 450.0f * 100.0f * MSGO::MSScale;
		}
		if (Def.Category == TEXT("Bazooka"))
		{
			return 250.0f * 100.0f * MSGO::MSScale;
		}
		return 300.0f * 100.0f * MSGO::MSScale;
	}
}

int32 UMSWeaponDataGenCommandlet::Main(const FString& Params)
{
	TArray<FMSWeaponDef> Definitions = BuildWeaponDefinitions();
	for (FMSWeaponDef& Def : Definitions) ApplyNewYorkReferenceBalance(Def);

	int32 NumSaved = 0;
	int32 NumFailed = 0;

	for (const FMSWeaponDef& Def : Definitions)
	{
		UClass* WeaponClass = ResolveWeaponClass(Def.UEClassName);
		if (!WeaponClass)
		{
			UE_LOG(LogTemp, Error, TEXT("MSWeaponDataGen: unknown UEClass '%s' for %s"), *Def.UEClassName, *Def.AssetName);
			++NumFailed;
			continue;
		}

		const FString PackageName = FString::Printf(TEXT("/Game/Data/Weapons/%s"), *Def.AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UMSWeaponDataBase* Asset = NewObject<UMSWeaponDataBase>(Package, WeaponClass, FName(*Def.AssetName), RF_Public | RF_Standalone);
		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("MSWeaponDataGen: failed to create %s"), *Def.AssetName);
			++NumFailed;
			continue;
		}

		// ---- 基底(UMSWeaponDataBase)共通フィールド ----
		Asset->WeaponName = FText::FromString(Def.WeaponName);
		Asset->CategoryTag = ResolveCategoryTag(Def.Category);
		Asset->VariantType = ResolveVariantType(Def.VariantType);
		Asset->Power = Def.Power;
		Asset->FireInterval = Def.FireInterval;
		Asset->DownPoint = Def.DownPoint;
		Asset->MagazineSize = Def.MagazineSize;
		Asset->ReserveAmmo = Def.ReserveAmmo;
		Asset->ReloadTime = Def.ReloadTime;
		Asset->Range = Def.Range;
		Asset->RangeWorldScale = Def.RangeWorldScale;
		Asset->LockOnRange = Def.LockOnRange;
		Asset->RedLockDistance = ResolveRedLockDistance(Def);
		Asset->Weight = Def.Weight;
		Asset->bIsBuiltIn = Def.bIsBuiltIn;
		Asset->WeaponSwitchDuration = Def.WeaponSwitchDuration;
		Asset->WeaponMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Def.WeaponMeshPath));
		Asset->WeaponMeshTransform = Def.WeaponMeshTransform;
		Asset->bOverrideMuzzleTransform = Def.bOverrideMuzzleTransform;
		Asset->MuzzleOffsetOverride = Def.MuzzleOffsetOverride;
		Asset->MuzzleDirectionOverride = Def.MuzzleDirectionOverride;

		// ---- 派生クラス別フィールド ----
		if (UMSRangedWeaponData* Ranged = Cast<UMSRangedWeaponData>(Asset))
		{
			Ranged->TracerColor = Def.TracerColor;
			Ranged->TracerWidth = Def.TracerWidth;
			Ranged->TracerLifetime = Def.TracerLifetime;
			Ranged->ProjectileSpeed = Def.ProjectileSpeed;
			Ranged->SplashRadius = Def.SplashRadius;
			Ranged->SplashDamage = Def.SplashDamage;
			Ranged->BurstCount = Def.BurstCount;
			Ranged->BurstInterval = Def.BurstInterval;
			Ranged->bIsChargeable = Def.bIsChargeable;
			Ranged->MaxChargeTime = Def.MaxChargeTime;
			Ranged->bUseChargeProfile = Def.bUseChargeProfile;
			Ranged->HalfChargeMultipliers = Def.ChargeHalf;
			Ranged->FullChargeMultipliers = Def.ChargeFull;
			Ranged->bIsScopeWeapon = Def.bIsScopeWeapon;
			Ranged->ScopedFieldOfView = Def.ScopedFieldOfView;
			Ranged->ScopedSensitivityMultiplier = Def.ScopedSensitivityMultiplier;

			// Keep authored heavy-weapon visuals when the original data is regenerated.
			// ImportHeavyWeapons.py updates existing assets without regenerating balance.
			static const TMap<FString, FString> HeavyVisuals = {
				{TEXT("DA_Weapon_ChargeBazookaCB3"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_MidBazookaMB6"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_MidBazookaMB6F"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_MidBazookaMB6M"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_MidBazookaMB6W"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_ShoulderBazookaSB9"), TEXT("MB6_Bazooka")},
				{TEXT("DA_Weapon_TripleRocketTR3"), TEXT("ML3_MissileLauncher")},
				{TEXT("DA_Weapon_HomingMissile"), TEXT("ML3_MissileLauncher")},
				{TEXT("DA_Weapon_SeekerMissileSM4"), TEXT("ML3_MissileLauncher")},
				{TEXT("DA_Weapon_SeekerMissileSM4W"), TEXT("ML3_MissileLauncher")},
				{TEXT("DA_Weapon_MissilePod"), TEXT("ML6_MissilePod")},
				{TEXT("DA_Weapon_MultiHomingMissile"), TEXT("ML6_MissilePod")},
				{TEXT("DA_Weapon_ShoulderRocketPod"), TEXT("SP6_ShoulderPod")}
			};
			if (const FString* MeshName = HeavyVisuals.Find(Def.AssetName))
			{
				const FString Folder = TEXT("/Game/Meshes/Weapons/HeavyWeapons/");
				Asset->WeaponMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
					Folder + TEXT("SM_") + *MeshName + TEXT(".SM_") + *MeshName));
				Asset->WeaponMeshTransform = FTransform(FQuat::Identity,
					FVector::ZeroVector, FVector(1.0f / MSGO::MSScale));
				Asset->bOverrideMuzzleTransform = false;
				const FString Projectile = Def.AssetName.Contains(TEXT("Missile"))
					? TEXT("Missile_Projectile") : TEXT("Rocket_Projectile");
				Ranged->ProjectileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
					Folder + TEXT("SM_") + Projectile + TEXT(".SM_") + Projectile));
				Ranged->ProjectileMeshTransform = FTransform::Identity;
			}
		}

		if (UMSShotgunWeaponData* Shotgun = Cast<UMSShotgunWeaponData>(Asset))
		{
			Shotgun->PelletCount = Def.PelletCount;
			Shotgun->PelletSpreadDegrees = Def.PelletSpreadDegrees;
		}

		if (UMSHomingWeaponData* Homing = Cast<UMSHomingWeaponData>(Asset))
		{
			Homing->HomingLockOnDwellTime = Def.HomingLockOnDwellTime;
			Homing->HomingTurnRateDegPerSec = Def.HomingTurnRateDegPerSec;
		}

		if (UMSContinuousBeamWeaponData* Beam = Cast<UMSContinuousBeamWeaponData>(Asset))
		{
			Beam->BeamDamagePerSecond = Def.BeamDamagePerSecond;
			Beam->BeamTickInterval = Def.BeamTickInterval;
			Beam->BeamRadius = Def.BeamRadius;
			Beam->BeamDownPointPerSecond = Def.BeamDownPointPerSecond;
		}

		if (UMSMeleeWeaponData* Melee = Cast<UMSMeleeWeaponData>(Asset))
		{
			Melee->MeleeMotionType = ResolveMeleeMotionType(Def.MeleeMotionType);
		}

		if (UMSShieldWeaponData* Shield = Cast<UMSShieldWeaponData>(Asset))
		{
			Shield->GuardDamageMultiplier = Def.GuardDamageMultiplier;
			Shield->GuardFrontalAngleDegrees = Def.GuardFrontalAngleDegrees;
			// 後期仕様: 盾は機体固有装備、積載重量なし。独自機体の耐久の基準値。
			Shield->Weight = 0;
			Shield->MaxDurability = Def.AssetName.Contains(TEXT("LightShield")) ? 990.0f
				: Def.AssetName.Contains(TEXT("FortressShield")) ? 1820.0f : 1380.0f;
		}

		if (UMSSupplyWeaponData* Supply = Cast<UMSSupplyWeaponData>(Asset))
		{
			Supply->DeployedItemType = ResolveDeployedItemType(Def.DeployedItemType);
			Supply->DeployedPodLifetime = Def.DeployedPodLifetime;
			Supply->RadarRangeStat = Def.RadarRangeStat;
		}

		// Keep the WeaponLibraryOpus visuals (58 definitions) when balance data is regenerated.
		// ImportWeaponLibraryOpus.py writes the same presentation fields to the existing assets.
		if (const MSWeaponLibraryOpus::FVisualRow* Opus = MSWeaponLibraryOpus::Find(Def.AssetName))
		{
			MSWeaponLibraryOpus::ApplyVisual(*Asset, *Opus);
		}

		Asset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
		if (bSaved)
		{
			UE_LOG(LogTemp, Display, TEXT("MSWeaponDataGen: saved %s"), *PackageFileName);
			++NumSaved;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSWeaponDataGen: failed to save %s"), *PackageFileName);
			++NumFailed;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("MSWeaponDataGen: done. Saved=%d Failed=%d"), NumSaved, NumFailed);

	return NumFailed > 0 ? 1 : 0;
}

#endif // WITH_EDITOR
