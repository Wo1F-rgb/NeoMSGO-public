#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSModuleCustomizeWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleCustomizeEquipTest, "NeoMSGO.UI.ModuleCustomize.CatalogDetailsEquipAndRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleCustomizeHierarchyTest, "NeoMSGO.UI.ModuleCustomize.SuitDetailTransitionAndEscapeHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSModuleCustomizeTests
{
	template <typename T> TStrongObjectPtr<T> MakeWidget()
	{
		TStrongObjectPtr<T> Result(NewObject<T>(GetTransientPackage())); Result->Initialize(); return Result;
	}

	UMSModuleData* MakeModule(UObject* Outer)
	{
		UMSModuleData* Module = NewObject<UMSModuleData>(Outer);
		Module->ModuleName = FText::FromString(TEXT("トランゾム")); Module->Description = FText::FromString(TEXT("30秒間、機動性能を強化する固有モジュール。"));
		return Module;
	}
}

bool FMSModuleCustomizeEquipTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSMobileSuitData> Suit(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	TStrongObjectPtr<UMSModuleData> Existing(MSModuleCustomizeTests::MakeModule(GetTransientPackage()));
	TStrongObjectPtr<UMSModuleData> Module(MSModuleCustomizeTests::MakeModule(GetTransientPackage()));
	Module->EffectDurationSeconds = 2.25f;
	Module->CooldownSeconds = 7.75f;
	Suit->EquippedModule = Existing.Get();
	Deck->AssignPreset(0, Suit.Get());
	Deck->AssignPreset(1, Suit.Get());
	const FString SaveSlot = FString::Printf(TEXT("NeoMSGO_ModuleWidgetTest_%s"), *FGuid::NewGuid().ToString());
	Deck->SetSaveSlotForAutomationTests(SaveSlot);
	Deck->EnablePersistenceForAutomationTests();
	TStrongObjectPtr<UMSModuleCustomizeWidget> Widget = MSModuleCustomizeTests::MakeWidget<UMSModuleCustomizeWidget>();
	Widget->SetDeckSubsystemForTesting(Deck.Get());
	Widget->InitializeForAutomationTests(Suit.Get(), { Module.Get() }); Widget->Open(Suit.Get(), 1);
	Widget->ChooseModule(Module.Get());
	TestEqual(TEXT("catalog contains Transzomm"), Widget->GetCatalogCount(), 1);
	TestEqual(TEXT("uncategorized module is placed in enhancement tab"), Widget->GetFilteredCatalogCount(), 1);
	TestEqual(TEXT("selected module details are shown"), Widget->GetDisplayedModuleName().ToString(), FString(TEXT("トランゾム")));
	TestTrue(TEXT("offline slot equipment is supported"), Widget->IsModuleEquipmentSupported());
	UButton* Equip = CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("EquipModuleButton")));
	UButton* Remove = CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("RemoveModuleButton")));
	TestTrue(TEXT("equip is enabled for a new candidate"), Equip->GetIsEnabled());
	TestEqual(TEXT("unknown slot budget has no fabricated 1/9"),
		CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("ModuleSlotCountText")))->GetText().ToString(), FString(TEXT("スロット数  ---")));
	const FString Parameters = CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("SelectedModuleParametersText")))->GetText().ToString();
	TestTrue(TEXT("duration comes from selected module without integer rounding"), Parameters.Contains(TEXT("2.25")));
	TestTrue(TEXT("cooldown comes from selected module"), Parameters.Contains(TEXT("7.75")));
	Equip->OnClicked.Broadcast();
	TestTrue(TEXT("successful equip marks committed module"), Widget->IsModuleEquipped(Module.Get()));
	TestEqual(TEXT("equipment affects captured slot 1 even while deck selects 0"), Deck->GetLoadoutInSlot(1).EquippedModule.Get(), Module.Get());
	TestEqual(TEXT("same suit other slot remains unchanged"), Deck->GetLoadoutInSlot(0).EquippedModule.Get(), Existing.Get());
	TestEqual(TEXT("shared DataAsset remains unchanged"), Suit->EquippedModule.Get(), Existing.Get());
	Deck->SetSaveSlotForAutomationTests(TEXT(""));
	Remove->OnClicked.Broadcast();
	TestTrue(TEXT("failed removal preserves equipment"), Widget->IsModuleEquipped(Module.Get()));
	TestTrue(TEXT("failed save is shown"), CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("ModuleCustomizeNoticeText")))->GetText().ToString().Contains(TEXT("保存できませんでした")));
	Deck->SetSaveSlotForAutomationTests(SaveSlot);
	Remove->OnClicked.Broadcast();
	TestTrue(TEXT("offline removal commits explicit null"), Deck->GetLoadoutInSlot(1).EquippedModule.IsNull());
	TestTrue(TEXT("real saved slot reloads"), Deck->ReloadDeck());
	TestTrue(TEXT("removal survives reload"), Deck->GetLoadoutInSlot(1).EquippedModule.IsNull());
	Widget->SelectCategory(1); TestEqual(TEXT("special tab is empty without category data"), Widget->GetFilteredCatalogCount(), 0);
	Widget->SelectCategory(0); TestEqual(TEXT("enhancement tab restores uncategorized modules"), Widget->GetFilteredCatalogCount(), 1);
	Widget->Close();
	const FString PreviousCommandLine = FCommandLine::Get();
	FCommandLine::Append(TEXT(" -MSAuthMode=Online"));
	Widget->Open(Suit.Get(), 0);
	Widget->ChooseModule(Module.Get());
	TestFalse(TEXT("online module equipment is unsupported"), Widget->IsModuleEquipmentSupported());
	TestFalse(TEXT("online equip is disabled"), Equip->GetIsEnabled());
	TestFalse(TEXT("online remove is disabled"), Remove->GetIsEnabled());
	Equip->OnClicked.Broadcast(); Remove->OnClicked.Broadcast();
	TestEqual(TEXT("even forced online handlers preserve loadout"), Deck->GetLoadoutInSlot(0).EquippedModule.Get(), Existing.Get());
	TestTrue(TEXT("online limitation is explicit"), CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("ModuleCustomizeNoticeText")))->GetText().ToString().Contains(TEXT("未対応")));
	FCommandLine::Set(*PreviousCommandLine);
	Widget->Close();
	UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
	return true;
}

bool FMSModuleCustomizeHierarchyTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSUILockSubsystem> Lock(NewObject<UMSUILockSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSMobileSuitData> Suit(NewObject<UMSMobileSuitData>(Deck.Get())); Suit->UnitName = FText::FromString(TEXT("MODULE TEST SUIT"));
	TStrongObjectPtr<UMSModuleData> Module(MSModuleCustomizeTests::MakeModule(GetTransientPackage()));
	Deck->AssignPreset(0, Suit.Get());

	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSModuleCustomizeTests::MakeWidget<UMSSuitDetailWidget>(); Detail->InitializeForAutomationTests(); Detail->SetDeckSubsystemForTesting(Deck.Get()); Detail->SetUILockSubsystemForTesting(Lock.Get());
	TStrongObjectPtr<UMSModuleCustomizeWidget> ModuleWidget = MSModuleCustomizeTests::MakeWidget<UMSModuleCustomizeWidget>(); ModuleWidget->InitializeForAutomationTests(Suit.Get(), { Module.Get() }); ModuleWidget->SetUILockSubsystemForTesting(Lock.Get()); ModuleWidget->SetDeckSubsystemForTesting(Deck.Get()); Detail->SetModuleCustomizeWidgetForTesting(ModuleWidget.Get());
	Detail->Open(0); TestTrue(TEXT("detail starts open"), Detail->IsOpen());
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("ModuleCustomizeButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("module customize opens"), ModuleWidget->IsOpen()); TestFalse(TEXT("module customize collapses detail"), Detail->IsOpen());
	CastChecked<UButton>(ModuleWidget->GetWidgetFromName(TEXT("EquipModuleButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("offline module action stays enabled through detail flow"),
		CastChecked<UButton>(ModuleWidget->GetWidgetFromName(TEXT("EquipModuleButton")))->GetIsEnabled());
	TestTrue(TEXT("failed local save leaves shared DataAsset unchanged"), Suit->EquippedModule.IsNull());
	TestTrue(TEXT("Esc request is handled"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc closes only module screen"), ModuleWidget->IsOpen()); TestTrue(TEXT("Esc restores detail"), Detail->IsOpen());
	TestTrue(TEXT("detail loadout does not claim an unsaved module"), CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitDetailLoadoutText")))->GetText().ToString().Contains(TEXT("ABILITY  ---")));
	Detail->Close(); return true;
}

#endif
