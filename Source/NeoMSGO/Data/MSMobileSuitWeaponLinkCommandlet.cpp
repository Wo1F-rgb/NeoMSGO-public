// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSMobileSuitWeaponLinkCommandlet.h"

#if WITH_EDITOR

#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/AssetManager.h"

namespace
{
	/**
	 * 機体(DA_MS_*のサフィックス) → 装備武器(DA_Weapon_*のサフィックス)の対応表。
	 * 各機体のアーキタイプ(P1-2完了報告のコンセプト参照)に沿って2〜3個を割り当てる:
	 *   StrikerMk1        : Assault(連邦)       … 万能アサルト。BR+近接+盾
	 *   VanguardRX         : Assault(連邦)       … 高機動アサルト。長射程ビーム+近接+軽盾
	 *   DreadnoughtHeavy    : Heavy(公国)         … 重装。榴弾砲+バズーカ+重盾
	 *   RaptorRecon         : Sniper(連邦)        … 偵察狙撃。スナイパー+軽量副武装
	 *   BreakerAssault      : Assault(公国)       … 突撃近接寄り。バルカン+斧+軽盾
	 *   FieldKeeperSupport  : Support(連邦)       … 後方支援。リペア+補給+汎用ビーム
	 *   AssaultCommander    : Support(公国, 特務型) … 中距離制圧+誘導兵装
	 *   GuardianDefender    : Assault(連邦, 防衛型) … 防衛特化。ガトリング+盾+格闘
	 */
	struct FMSWeaponLinkDef
	{
		FString MobileSuitAssetName; // DA_MS_* のサフィックス
		TArray<FString> WeaponAssetNames; // DA_Weapon_* のサフィックス(完全名。上限3)
	};

	TArray<FMSWeaponLinkDef> BuildWeaponLinkDefinitions()
	{
		return {
			{ TEXT("StrikerMk1"), { TEXT("DA_Weapon_BR"), TEXT("DA_Weapon_HeatSwordHS1"), TEXT("DA_Weapon_DefenderShieldDS1") } },
			{ TEXT("VanguardRX"), { TEXT("DA_Weapon_LongBeamRifleLB5"), TEXT("DA_Weapon_HeatLancerHL1"), TEXT("DA_Weapon_LightShieldLS2") } },
			{ TEXT("DreadnoughtHeavy"), { TEXT("DA_Weapon_HeavyMortarEX5"), TEXT("DA_Weapon_ShoulderBazookaSB9"), TEXT("DA_Weapon_FortressShieldFS3") } },
			{ TEXT("RaptorRecon"), { TEXT("DA_Weapon_SniperRifle"), TEXT("DA_Weapon_LightMachineGunLM6") } },
			{ TEXT("BreakerAssault"), { TEXT("DA_Weapon_AssaultVulcanAV2"), TEXT("DA_Weapon_HeatAxeHA2"), TEXT("DA_Weapon_LightShieldLS2") } },
			{ TEXT("FieldKeeperSupport"), { TEXT("DA_Weapon_RepairPodDeployer"), TEXT("DA_Weapon_SupplyPackDeployer"), TEXT("DA_Weapon_BeamMachineGun") } },
			{ TEXT("AssaultCommander"), { TEXT("DA_Weapon_HomingMissile"), TEXT("DA_Weapon_MultiHomingMissile"), TEXT("DA_Weapon_BeamLauncherBL4") } },
			{ TEXT("GuardianDefender"), { TEXT("DA_Weapon_DefenderShieldDS1"), TEXT("DA_Weapon_HeavyGatlingHG7"), TEXT("DA_Weapon_GrappleKnuckleGK3") } },
			{ TEXT("TAUrus"), { TEXT("DA_Weapon_TAUrus_ChargeBlasterLauncher"), TEXT("DA_Weapon_MultiHomingMissile"), TEXT("DA_Weapon_ChargeGreatSwordCS1") } },
		};
	}
}

int32 UMSMobileSuitWeaponLinkCommandlet::Main(const FString& Params)
{
	const TArray<FMSWeaponLinkDef> Definitions = BuildWeaponLinkDefinitions();

	int32 NumSaved = 0;
	int32 NumFailed = 0;

	for (const FMSWeaponLinkDef& Def : Definitions)
	{
		const FString MSAssetName = FString::Printf(TEXT("DA_MS_%s"), *Def.MobileSuitAssetName);
		const FString MSPackageName = FString::Printf(TEXT("/Game/Data/MobileSuits/%s"), *MSAssetName);

		UPackage* Package = LoadPackage(nullptr, *MSPackageName, LOAD_None);
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: failed to load package %s"), *MSPackageName);
			++NumFailed;
			continue;
		}

		UMSMobileSuitData* Asset = FindObject<UMSMobileSuitData>(Package, *MSAssetName);
		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: failed to find %s in %s"), *MSAssetName, *MSPackageName);
			++NumFailed;
			continue;
		}

		Asset->EquippedWeapons.Reset();
		Asset->NativeShield.Reset();

		bool bAllWeaponsResolved = true;
		for (const FString& WeaponAssetName : Def.WeaponAssetNames)
		{
			const FString WeaponPackageName = FString::Printf(TEXT("/Game/Data/Weapons/%s"), *WeaponAssetName);

			UPackage* WeaponPackage = LoadPackage(nullptr, *WeaponPackageName, LOAD_None);
			if (!WeaponPackage)
			{
				UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: failed to load weapon package %s (for %s)"), *WeaponPackageName, *MSAssetName);
				bAllWeaponsResolved = false;
				continue;
			}

			UMSWeaponDataBase* WeaponAsset = FindObject<UMSWeaponDataBase>(WeaponPackage, *WeaponAssetName);
			if (!WeaponAsset)
			{
				UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: failed to find weapon %s in %s"), *WeaponAssetName, *WeaponPackageName);
				bAllWeaponsResolved = false;
				continue;
			}

			if (UMSShieldWeaponData* Shield = Cast<UMSShieldWeaponData>(WeaponAsset))
			{
				Asset->NativeShield = Shield;
				Asset->EquippedWeapons.Add(nullptr); // 元の武器番号は詰めない。
			}
			else
			{
				Asset->EquippedWeapons.Add(TSoftObjectPtr<UMSWeaponDataBase>(WeaponAsset));
			}

			UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: %s <- %s"), *MSAssetName, *WeaponAssetName);
		}

		if (!bAllWeaponsResolved)
		{
			++NumFailed;
			continue;
		}

		Asset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			MSPackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
		if (bSaved)
		{
			UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: saved %s (%d weapons)"), *PackageFileName, Asset->EquippedWeapons.Num());
			++NumSaved;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: failed to save %s"), *PackageFileName);
			++NumFailed;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: done. Saved=%d Failed=%d"), NumSaved, NumFailed);

	// --- 検証パス(P1-4完了条件): 保存済みDataAssetを読み直し、
	//     機体→装備武器のソフト参照解決(TSoftObjectPtr::LoadSynchronous)が動作することを確認する。
	UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: verification pass start"));
	for (const FMSWeaponLinkDef& Def : Definitions)
	{
		const FString MSAssetName = FString::Printf(TEXT("DA_MS_%s"), *Def.MobileSuitAssetName);
		const FString MSPackageName = FString::Printf(TEXT("/Game/Data/MobileSuits/%s"), *MSAssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *MSPackageName, *MSAssetName);

		UMSMobileSuitData* Asset = LoadObject<UMSMobileSuitData>(nullptr, *ObjectPath);
		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitWeaponLink: [verify] failed to reload %s"), *MSAssetName);
			continue;
		}

		TArray<FString> ResolvedWeaponNames;
		for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : Asset->EquippedWeapons)
		{
			UMSWeaponDataBase* ResolvedWeapon = WeaponRef.LoadSynchronous();
			ResolvedWeaponNames.Add(ResolvedWeapon ? ResolvedWeapon->GetName() : TEXT("<unresolved>"));
		}

		UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: [verify] 機体%sの装備武器は %s"),
			*Asset->UnitName.ToString(), *FString::Join(ResolvedWeaponNames, TEXT(", ")));
	}
	UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: verification pass end"));

	// --- AssetManagerスキャン確認(P1-4完了条件): DefaultGame.iniのPrimaryAssetTypesToScan設定により
	//     機体・武器がPrimaryAssetとして一括列挙できることを確認する。
	if (UAssetManager::IsInitialized())
	{
		UAssetManager& Manager = UAssetManager::Get();
		Manager.ScanPathsSynchronous({ TEXT("/Game/Data/MobileSuits"), TEXT("/Game/Data/Weapons") });

		TArray<FPrimaryAssetId> MSIds;
		Manager.GetPrimaryAssetIdList(UMSMobileSuitData::PrimaryAssetType, MSIds);
		TArray<FPrimaryAssetId> WeaponIds;
		Manager.GetPrimaryAssetIdList(UMSWeaponDataBase::PrimaryAssetType, WeaponIds);

		UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: [AssetManager] MSMobileSuitData scanned = %d (expect 9)"), MSIds.Num());
		UE_LOG(LogTemp, Display, TEXT("MSMobileSuitWeaponLink: [AssetManager] MSWeaponData scanned = %d (expect 72)"), WeaponIds.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MSMobileSuitWeaponLink: [AssetManager] UAssetManager not initialized; skipping scan verification."));
	}

	return NumFailed > 0 ? 1 : 0;
}

#endif // WITH_EDITOR
