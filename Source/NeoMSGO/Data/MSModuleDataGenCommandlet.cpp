#include "Data/MSModuleDataGenCommandlet.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/MSModuleData.h"
#include "Misc/PackageName.h"
#include "Modules/GA_Module_Transzomm.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

int32 UMSModuleDataGenCommandlet::Main(const FString& Params)
{
	const FString AssetName(TEXT("DA_Module_Transzomm"));
	const FString PackageName(TEXT("/Game/Data/Modules/DA_Module_Transzomm"));
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UMSModuleData* Asset = FindObject<UMSModuleData>(Package, *AssetName);
	const bool bCreated = Asset == nullptr;
	if (!Asset)
	{
		Asset = NewObject<UMSModuleData>(Package, UMSModuleData::StaticClass(), FName(*AssetName),
			RF_Public | RF_Standalone);
	}
	if (!Asset)
	{
		return 1;
	}

	Asset->ModuleName = FText::FromString(TEXT("トランゾム"));
	Asset->Description = FText::FromString(TEXT("Fキーを1秒長押しして発動。30秒間、機動性能を強化しブースト消費を半減する。効果終了後は80%回復または最大90秒で強制オーバーヒートが解除され、発動から130秒間再使用できない。"));
	Asset->ModuleAbilityClass = UGA_Module_Transzomm::StaticClass();
	Asset->EffectDurationSeconds = 30.0f;
	Asset->SpeedMultiplier = 1.5f;
	Asset->BoostConsumptionMultiplier = 0.5f;
	Asset->ForcedOverheatSeconds = 90.0f;
	Asset->CooldownSeconds = 130.0f;
	Asset->bEnableAfterimage = true;
	Asset->AfterimageColor = FLinearColor(1.0f, 0.08f, 0.24f, 0.18f);
	Asset->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Asset);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName,
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs) ? 0 : 1;
}

#endif
