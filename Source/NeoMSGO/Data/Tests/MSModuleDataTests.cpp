#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Abilities/GameplayAbility.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Engine/AssetManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Modules/GA_Module_Transzomm.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleDataDefaultsTest, "NeoMSGO.Data.Module.DefaultsAndPrimaryAssetId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleDataValidationTest, "NeoMSGO.Data.Module.ValidationRejectsMissingAbilityAndNonPositiveValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleTranszommAssetTest, "NeoMSGO.Data.Module.TranszommAssetRegisteredAndConfigured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSModuleDataDefaultsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSModuleData> Module(NewObject<UMSModuleData>(GetTransientPackage(), TEXT("DA_TestModule")));
	TestEqual(TEXT("primary asset type"), Module->GetPrimaryAssetId().PrimaryAssetType, UMSModuleData::PrimaryAssetType);
	TestEqual(TEXT("primary asset name"), Module->GetPrimaryAssetId().PrimaryAssetName, FName(TEXT("DA_TestModule")));
	TestEqual(TEXT("effect duration"), Module->EffectDurationSeconds, 30.0f);
	TestEqual(TEXT("speed multiplier"), Module->SpeedMultiplier, 1.5f);
	TestEqual(TEXT("boost consumption multiplier"), Module->BoostConsumptionMultiplier, 0.5f);
	TestEqual(TEXT("forced overheat duration"), Module->ForcedOverheatSeconds, 90.0f);
	TestEqual(TEXT("cooldown"), Module->CooldownSeconds, 130.0f);
	TestTrue(TEXT("afterimage enabled"), Module->bEnableAfterimage);
	TestTrue(TEXT("afterimage is red-pink and translucent"), Module->AfterimageColor.R > 0.8f && Module->AfterimageColor.G < 0.3f && Module->AfterimageColor.A >= 0.16f && Module->AfterimageColor.A <= 0.2f);
	TStrongObjectPtr<UMSMobileSuitData> ExistingSuit(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	TestTrue(TEXT("existing mobile suit assets default to no equipped module"), ExistingSuit->EquippedModule.IsNull());
	return true;
}

bool FMSModuleDataValidationTest::RunTest(const FString&)
{
	auto MakeValid = []()
	{
		UMSModuleData* Module = NewObject<UMSModuleData>(GetTransientPackage());
		Module->ModuleAbilityClass = UGameplayAbility::StaticClass();
		return Module;
	};
	auto ExpectInvalid = [this](UMSModuleData* Module, const TCHAR* What)
	{
		FDataValidationContext Context;
		TestEqual(What, Module->IsDataValid(Context), EDataValidationResult::Invalid);
	};

	UMSModuleData* MissingAbility = MakeValid(); MissingAbility->ModuleAbilityClass = nullptr; ExpectInvalid(MissingAbility, TEXT("missing ability is invalid"));
	UMSModuleData* Duration = MakeValid(); Duration->EffectDurationSeconds = 0.0f; ExpectInvalid(Duration, TEXT("non-positive duration is invalid"));
	UMSModuleData* Speed = MakeValid(); Speed->SpeedMultiplier = 0.0f; ExpectInvalid(Speed, TEXT("non-positive speed multiplier is invalid"));
	UMSModuleData* Consumption = MakeValid(); Consumption->BoostConsumptionMultiplier = -1.0f; ExpectInvalid(Consumption, TEXT("non-positive consumption multiplier is invalid"));
	UMSModuleData* Overheat = MakeValid(); Overheat->ForcedOverheatSeconds = 0.0f; ExpectInvalid(Overheat, TEXT("non-positive overheat duration is invalid"));
	UMSModuleData* Cooldown = MakeValid(); Cooldown->CooldownSeconds = 0.0f; ExpectInvalid(Cooldown, TEXT("non-positive cooldown is invalid"));
	return true;
}

bool FMSModuleTranszommAssetTest::RunTest(const FString&)
{
	UMSModuleData* Module = LoadObject<UMSModuleData>(nullptr, TEXT("/Game/Data/Modules/DA_Module_Transzomm.DA_Module_Transzomm"));
	if (!TestNotNull(TEXT("DA_Module_Transzomm exists"), Module)) return false;
	TestEqual(TEXT("display name"), Module->ModuleName.ToString(), FString(TEXT("トランゾム")));
	TestEqual(TEXT("native ability class"), Module->ModuleAbilityClass.Get(), UGA_Module_Transzomm::StaticClass());
	TestEqual(TEXT("effect duration"), Module->EffectDurationSeconds, 30.0f);
	TestEqual(TEXT("speed multiplier"), Module->SpeedMultiplier, 1.5f);
	TestEqual(TEXT("consumption multiplier"), Module->BoostConsumptionMultiplier, 0.5f);
	TestEqual(TEXT("forced overheat"), Module->ForcedOverheatSeconds, 90.0f);
	TestEqual(TEXT("cooldown"), Module->CooldownSeconds, 130.0f);
	TestTrue(TEXT("red-pink afterimage"), Module->bEnableAfterimage && Module->AfterimageColor.R > 0.8f && Module->AfterimageColor.G < 0.3f && Module->AfterimageColor.A >= 0.16f && Module->AfterimageColor.A <= 0.2f);
	TArray<FPrimaryAssetId> Ids; UAssetManager::Get().GetPrimaryAssetIdList(UMSModuleData::PrimaryAssetType, Ids);
	TestTrue(TEXT("AssetManager enumerates DA_Module_Transzomm"), Ids.Contains(Module->GetPrimaryAssetId()));
	return true;
}

#endif
