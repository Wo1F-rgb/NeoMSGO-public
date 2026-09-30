// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSMobileSuitDataGenCommandlet.h"

#if WITH_EDITOR

#include "Data/MSMobileSuitData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
	/** MobileSuitCatalog.Definitions相当(ArchitectureMap.md §13)のコード直書き8機体定義 */
	struct FMSMobileSuitDef
	{
		FString AssetName;      // DA_MS_* のサフィックス
		FString DisplayName;    // UnitName(Unity版の日本語オリジナル名を踏襲)
		EMSMobileSuitFaction Faction;
		EMSMobileSuitUnitType UnitType;
		int32 Cost;
		int32 MaxArmor;
		float WalkSpeed;
		float BoostSpeed;
		float BoostCapacity;
		float BoostConsumptionRate;
		float BoostRechargeRate;
		float BoostOverheatPenalty;
		float MeleeCorrection;
		int32 LoadCapacity;
	};

	TArray<FMSMobileSuitDef> BuildMobileSuitDefinitions()
	{
		return {
			// AssetName,             DisplayName,                Faction,                                UnitType,                                  Cost, Armor, Walk, Boost, BCap,  Cons, Rech, OHPen, Melee, Load
			{ TEXT("StrikerMk1"),       TEXT("ストライカーMk-I"),       EMSMobileSuitFaction::Federation,      EMSMobileSuitUnitType::Assault,      200, 3425, 6.0f, 14.0f, 100.0f, 20.0f, 15.0f, 3.0f, 1.00f, 40 },
			{ TEXT("VanguardRX"),       TEXT("ヴァンガードRX"),         EMSMobileSuitFaction::Federation,      EMSMobileSuitUnitType::Assault,      320, 4200, 6.2f, 14.5f, 105.0f, 19.0f, 15.0f, 2.8f, 1.05f, 42 },
			{ TEXT("DreadnoughtHeavy"), TEXT("ドレッドノート重装型"),     EMSMobileSuitFaction::Principality,    EMSMobileSuitUnitType::Heavy,        280, 5400, 5.0f, 11.5f, 125.0f, 17.0f, 11.0f, 4.0f, 1.20f, 58 },
			{ TEXT("RaptorRecon"),      TEXT("ラプター偵察型"),         EMSMobileSuitFaction::Federation,      EMSMobileSuitUnitType::Sniper,       160, 2800, 7.2f, 16.5f, 90.0f,  23.0f, 19.0f, 2.3f, 0.80f, 28 },
			{ TEXT("BreakerAssault"),   TEXT("ブレイカー突撃型"),       EMSMobileSuitFaction::Principality,    EMSMobileSuitUnitType::Assault,      240, 4000, 6.5f, 15.0f, 100.0f, 21.0f, 14.0f, 3.2f, 1.10f, 38 },
			{ TEXT("FieldKeeperSupport"), TEXT("フィールドキーパー支援型"), EMSMobileSuitFaction::Federation,    EMSMobileSuitUnitType::Support,      220, 3800, 5.8f, 13.0f, 110.0f, 18.0f, 16.0f, 3.0f, 0.90f, 46 },
			{ TEXT("AssaultCommander"), TEXT("アサルトコマンダー特務型"), EMSMobileSuitFaction::Principality,    EMSMobileSuitUnitType::Support,      260, 4100, 5.9f, 13.5f, 112.0f, 18.5f, 15.5f, 3.1f, 1.00f, 48 },
			{ TEXT("GuardianDefender"), TEXT("ガーディアン防衛型"),     EMSMobileSuitFaction::Federation,      EMSMobileSuitUnitType::Assault,      230, 4600, 5.6f, 13.2f, 108.0f, 18.0f, 14.5f, 3.3f, 1.00f, 44 },
		};
	}
}

int32 UMSMobileSuitDataGenCommandlet::Main(const FString& Params)
{
	const TArray<FMSMobileSuitDef> Definitions = BuildMobileSuitDefinitions();

	int32 NumSaved = 0;
	int32 NumFailed = 0;

	for (const FMSMobileSuitDef& Def : Definitions)
	{
		const FString AssetName = FString::Printf(TEXT("DA_MS_%s"), *Def.AssetName);
		const FString PackageName = FString::Printf(TEXT("/Game/Data/MobileSuits/%s"), *AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UMSMobileSuitData* Asset = NewObject<UMSMobileSuitData>(
			Package, UMSMobileSuitData::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);

		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitDataGen: failed to create %s"), *AssetName);
			++NumFailed;
			continue;
		}

		Asset->UnitName = FText::FromString(Def.DisplayName);
		Asset->Faction = Def.Faction;
		Asset->UnitType = Def.UnitType;
		Asset->Cost = Def.Cost;
		Asset->MaxArmor = Def.MaxArmor;
		Asset->MaxDownPoint = 100;
		Asset->WalkSpeed = Def.WalkSpeed;
		Asset->BoostSpeed = Def.BoostSpeed;
		Asset->BoostCapacity = Def.BoostCapacity;
		Asset->BoostConsumptionRate = Def.BoostConsumptionRate;
		Asset->BoostRechargeRate = Def.BoostRechargeRate;
		Asset->BoostOverheatPenalty = Def.BoostOverheatPenalty;
		Asset->MeleeCorrection = Def.MeleeCorrection;
		Asset->LoadCapacity = Def.LoadCapacity;
		// EquippedWeapons: P1-3(71武器アセット変換)完了後にP1-4のカタログ配線で設定する。
		// 現時点では武器アセットが存在しないため未設定(IsDataValidの警告は既知・意図的)。

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
			UE_LOG(LogTemp, Display, TEXT("MSMobileSuitDataGen: saved %s"), *PackageFileName);
			++NumSaved;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSMobileSuitDataGen: failed to save %s"), *PackageFileName);
			++NumFailed;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("MSMobileSuitDataGen: done. Saved=%d Failed=%d"), NumSaved, NumFailed);

	return NumFailed > 0 ? 1 : 0;
}

#endif // WITH_EDITOR
