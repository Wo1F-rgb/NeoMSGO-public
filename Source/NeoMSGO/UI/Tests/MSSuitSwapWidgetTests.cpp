#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSSuitSwapWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapCatalogFilterTest, "NeoMSGO.UI.SuitSwap.CatalogCategorySearchAndSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapComparisonTest, "NeoMSGO.UI.SuitSwap.RowOpensComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapUnequippedPreviewTest, "NeoMSGO.UI.SuitSwap.UnequippedNativeShieldPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapConfirmTest, "NeoMSGO.UI.SuitSwap.ConfirmAndCostLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapEmptySlotRegistrationTest, "NeoMSGO.UI.SuitSwap.RemoveAndRegisterEmptySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapIndependentScreensTest, "NeoMSGO.UI.SuitSwap.OwnedAndDevelopmentAreReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSuitSwapTests
{
	UMSMobileSuitData* MakeSuit(UObject* Outer, const TCHAR* Name, const int32 Cost, const EMSMobileSuitUnitType Type)
	{
		UMSMobileSuitData* Result = NewObject<UMSMobileSuitData>(Outer); Result->UnitName = FText::FromString(Name); Result->Cost = Cost; Result->UnitType = Type; return Result;
	}

	TStrongObjectPtr<UMSSuitSwapWidget> MakeSwap(UMSDeckSubsystem* Deck, const TArray<UMSMobileSuitData*>& Catalog)
	{
		TStrongObjectPtr<UMSSuitSwapWidget> Result(NewObject<UMSSuitSwapWidget>(GetTransientPackage())); Result->Initialize(); Result->InitializeForAutomationTests(); Result->SetDeckSubsystemForTesting(Deck); Result->SetCatalogForTesting(Catalog); return Result;
	}

	struct FScopedDeckSaveSlot
	{
		FString Name = FString::Printf(TEXT("NeoMSGO_SuitSwapLoadoutTest_%s"), *FGuid::NewGuid().ToString());
		explicit FScopedDeckSaveSlot(UMSDeckSubsystem* Deck)
		{
			Deck->SetSaveSlotForAutomationTests(Name);
			Deck->EnablePersistenceForAutomationTests();
		}
		~FScopedDeckSaveSlot() { UGameplayStatics::DeleteGameInSlot(Name, 0); }
	};
}

bool FMSSuitSwapCatalogFilterTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TArray<UMSMobileSuitData*> Catalog;
	Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("ALPHA ASSAULT"), 500, EMSMobileSuitUnitType::Assault)); Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("BETA HEAVY"), 420, EMSMobileSuitUnitType::Heavy));
	Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("GAMMA SUPPORT"), 300, EMSMobileSuitUnitType::Support)); Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("DELTA BOMB"), 380, EMSMobileSuitUnitType::Bombardment)); Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("EPSILON SNIPER"), 340, EMSMobileSuitUnitType::Sniper));
	Deck->AssignPreset(0, Catalog[0]); TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), Catalog); Swap->Open(0);
	TestEqual(TEXT("all catalog presets are listed"), Swap->GetFilteredCount(), Catalog.Num()); TestEqual(TEXT("catalog source count matches"), Swap->GetCatalogCount(), Catalog.Num());
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("CategoryHeavyButton")))->OnClicked.Broadcast(); TestEqual(TEXT("category tab changes filtered count"), Swap->GetFilteredCount(), 1);
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("CategoryAllButton")))->OnClicked.Broadcast(); UEditableTextBox* Search = CastChecked<UEditableTextBox>(Swap->GetWidgetFromName(TEXT("SuitSearchInput"))); Search->SetText(FText::FromString(TEXT("support")));
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSearchButton")))->OnClicked.Broadcast(); TestEqual(TEXT("case-insensitive partial-name search leaves one row"), Swap->GetFilteredCount(), 1);
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSearchClearButton")))->OnClicked.Broadcast(); TestEqual(TEXT("clear restores catalog rows"), Swap->GetFilteredCount(), Catalog.Num()); return true;
}

bool FMSSuitSwapComparisonTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* Current = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("CURRENT SUIT"), 400, EMSMobileSuitUnitType::Assault); UMSMobileSuitData* Candidate = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("CANDIDATE SUIT"), 500, EMSMobileSuitUnitType::Heavy);
	Deck->AssignPreset(2, Current); TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), { Candidate }); Swap->Open(2);
	UScrollBox* Rows = CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox"))); TestEqual(TEXT("one candidate row is constructed"), Rows->GetChildrenCount(), 1);
	UMSSuitSwapEntryWidget* Entry = CastChecked<UMSSuitSwapEntryWidget>(Rows->GetChildAt(0)); TestNotNull(TEXT("catalog row owns its clickable button"), Entry->GetEntryButton()); if (!Entry->GetEntryButton()) return false; Entry->GetEntryButton()->OnClicked.Broadcast();
	TestTrue(TEXT("row click enters comparison phase"), Swap->IsComparisonPhase()); TestTrue(TEXT("comparison shows candidate name"), Swap->GetCandidateName().ToString().Contains(TEXT("CANDIDATE SUIT"))); TestTrue(TEXT("comparison shows current name"), Swap->GetCurrentName().ToString().Contains(TEXT("CURRENT SUIT")));
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSwapBackButton")))->OnClicked.Broadcast(); TestFalse(TEXT("first back returns comparison to list"), Swap->IsComparisonPhase()); TestTrue(TEXT("swap screen remains open after comparison back"), Swap->IsOpen()); return true;
}

bool FMSSuitSwapUnequippedPreviewTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSuitSwapShieldPreviewWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UMSMobileSuitData* Current = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("SHIELD OFF CURRENT"), 400, EMSMobileSuitUnitType::Assault);
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Deck.Get());
	Shield->WeaponName = FText::FromString(TEXT("Native Preview Shield"));
	Shield->CategoryTag = MSTags::Weapon_Category_Shield;
	Current->NativeShield = Shield;
	Current->bShieldRemovable = true;
	UMSMobileSuitData* Candidate = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("SHIELD CANDIDATE"), 500, EMSMobileSuitUnitType::Heavy);
	MSSuitSwapTests::FScopedDeckSaveSlot SaveSlot(Deck.Get());
	Deck->AssignPreset(0, Current);
	FMSDeckLoadout Unequipped = Deck->GetLoadoutInSlot(0);
	Unequipped.bShieldEquipped = false;
	TestTrue(TEXT("fixture accepts the removable shield unequip state"), Deck->CommitLoadout(0, Unequipped));

	TStrongObjectPtr<UMSSuitSwapWidget> Swap(NewObject<UMSSuitSwapWidget>(World));
	Swap->Initialize();
	Swap->InitializeForAutomationTests();
	Swap->SetDeckSubsystemForTesting(Deck.Get());
	Swap->SetCatalogForTesting({ Candidate });
	Swap->Open(0);
	Swap->SelectCandidate(Candidate);

	TestTrue(TEXT("comparison readout states that the current native shield is unequipped"),
		Swap->GetCurrentShieldTextForTesting().ToString().Contains(TEXT("未装備")));
	TestFalse(TEXT("current suit preview hides its unequipped native shield"),
		Swap->IsCurrentPreviewShieldVisibleForTesting());

	Swap->Close();
	Swap.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

bool FMSSuitSwapConfirmTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* Current = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("OLD"), 500, EMSMobileSuitUnitType::Assault); UMSMobileSuitData* Other = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("OTHER"), 1000, EMSMobileSuitUnitType::Heavy);
	UMSMobileSuitData* Valid = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("VALID"), 600, EMSMobileSuitUnitType::Support); UMSMobileSuitData* TooExpensive = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("TOO EXPENSIVE"), 800, EMSMobileSuitUnitType::Sniper);
	Deck->AssignPreset(0, Current); Deck->AssignPreset(1, Other); TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), { Valid, TooExpensive }); Swap->Open(0);
	Swap->SelectCandidate(TooExpensive); UButton* Confirm = CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("ConfirmSuitSwapButton"))); TestFalse(TEXT("cost-over-limit swap is disabled"), Confirm->GetIsEnabled());
	TestTrue(TEXT("cost-over-limit deck cost is warning red"), Swap->GetDeckCostTextColorForAutomationTests().Equals(FLinearColor(1.0f, 0.12f, 0.08f, 1.0f)));
	Confirm->OnClicked.Broadcast(); TestEqual(TEXT("disabled over-limit confirm does not change slot"), Deck->GetPresetInSlot(0), Current);
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSwapBackButton")))->OnClicked.Broadcast(); Swap->SelectCandidate(Valid); TestTrue(TEXT("within-limit swap is enabled"), Confirm->GetIsEnabled());
	TestTrue(TEXT("deck cost at or below 1720 returns to white"), Swap->GetDeckCostTextColorForAutomationTests().Equals(FLinearColor::White));
	Confirm->OnClicked.Broadcast(); TestEqual(TEXT("confirm assigns candidate to selected slot"), Deck->GetPresetInSlot(0), Valid); TestFalse(TEXT("confirm closes swap screen"), Swap->IsOpen()); return true;
}

bool FMSSuitSwapEmptySlotRegistrationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSUILockSubsystem> UILock(NewObject<UMSUILockSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSCustomizeWidget> Customize(NewObject<UMSCustomizeWidget>(GetTransientPackage())); Customize->Initialize(); Customize->InitializeForAutomationTests();
	TStrongObjectPtr<UMSSuitDetailWidget> Detail(NewObject<UMSSuitDetailWidget>(GetTransientPackage())); Detail->Initialize(); Detail->InitializeForAutomationTests();
	UMSMobileSuitData* Removed = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("REMOVED SUIT"), 500, EMSMobileSuitUnitType::Assault);
	UMSMobileSuitData* Replacement = MSSuitSwapTests::MakeSuit(Deck.Get(), TEXT("REGISTERED SUIT"), 600, EMSMobileSuitUnitType::Support);
	Deck->AssignPreset(2, Removed);
	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), { Replacement });
	Customize->SetDeckSubsystemForTesting(Deck.Get()); Detail->SetDeckSubsystemForTesting(Deck.Get());
	Customize->SetSuitDetailWidgetForTesting(Detail.Get()); Customize->SetSuitSwapWidgetForTesting(Swap.Get());
	Customize->SetUILockSubsystemForTesting(UILock.Get()); Swap->SetUILockSubsystemForTesting(UILock.Get());

	Customize->Open();
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard2")))->OnClicked.Broadcast();
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("RemoveFromDeckButton")))->OnClicked.Broadcast();
	TestNull(TEXT("removing the suit clears the selected slot"), Deck->GetPresetInSlot(2));
	TestTrue(TEXT("removing the suit returns to customize"), Customize->IsOpen());

	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard2")))->OnClicked.Broadcast();
	TestTrue(TEXT("clicking the empty card opens the suit selection list"), Swap->IsOpen());
	TestFalse(TEXT("the empty-card flow bypasses suit detail"), Detail->IsOpen());
	TestFalse(TEXT("the selection list hides customize while open"), Customize->IsOpen());
	TestEqual(TEXT("the empty slot still has a selectable catalog row"), Swap->GetFilteredCount(), 1);
	TestTrue(TEXT("Esc is consumed by the directly opened selection list"), UILock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc closes the selection list"), Swap->IsOpen());
	TestTrue(TEXT("Esc restores customize without popping it"), Customize->IsOpen());
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard2")))->OnClicked.Broadcast();
	TestTrue(TEXT("the empty slot can reopen the selection list after Esc"), Swap->IsOpen());

	UScrollBox* Rows = CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox")));
	UMSSuitSwapEntryWidget* Entry = CastChecked<UMSSuitSwapEntryWidget>(Rows->GetChildAt(0));
	Entry->GetEntryButton()->OnClicked.Broadcast();
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("ConfirmSuitSwapButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("confirm registers the selected suit in the empty slot"), Deck->GetPresetInSlot(2), Replacement);
	TestFalse(TEXT("confirm closes the selection list"), Swap->IsOpen());
	TestTrue(TEXT("confirm returns to customize"), Customize->IsOpen());
	TestTrue(TEXT("customize redraws the registered suit"), Customize->GetDisplayedSlotName(2).ToString().Contains(TEXT("REGISTERED SUIT")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitSwapPaginationTest, "NeoMSGO.UI.SuitSwap.SevenRowsPagingAndFilterReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSSuitSwapPaginationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GI.Get()));
	TArray<UMSMobileSuitData*> Catalog;
	for (int32 Index = 0; Index < 15; ++Index) Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), *FString::Printf(TEXT("SUIT %02d"), Index), 500 - Index, EMSMobileSuitUnitType::Assault));
	Deck->AssignPreset(0, Catalog[0]); auto Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), Catalog); Swap->Open(0);
	UScrollBox* Rows = CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox")));
	UButton* Previous = CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitCatalogPreviousButton")));
	UButton* Next = CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitCatalogNextButton")));
	UTextBlock* Page = CastChecked<UTextBlock>(Swap->GetWidgetFromName(TEXT("SuitCatalogPageText")));
	TestEqual(TEXT("first page has exactly seven live rows"), Rows->GetChildrenCount(), 7); TestEqual(TEXT("first page actual count"), Page->GetText().ToString(), FString(TEXT("1 / 3"))); TestFalse(TEXT("first page cannot go back"), Previous->GetIsEnabled());
	Next->OnClicked.Broadcast(); TestEqual(TEXT("next page number changes"), Page->GetText().ToString(), FString(TEXT("2 / 3"))); TestEqual(TEXT("second page has seven rows"), Rows->GetChildrenCount(), 7);
	CastChecked<UMSSuitSwapEntryWidget>(Rows->GetChildAt(0))->GetEntryButton()->OnClicked.Broadcast(); TestEqual(TEXT("page two row selects actual eighth candidate"), Swap->GetCandidateName().ToString(), Catalog[7]->UnitName.ToString()); Swap->Close();
	Next->OnClicked.Broadcast(); TestEqual(TEXT("last page only contains remaining row"), Rows->GetChildrenCount(), 1); TestFalse(TEXT("last page cannot go forward"), Next->GetIsEnabled());
	Previous->OnClicked.Broadcast(); TestEqual(TEXT("previous returns to second page"), Page->GetText().ToString(), FString(TEXT("2 / 3")));
	CastChecked<UEditableTextBox>(Swap->GetWidgetFromName(TEXT("SuitSearchInput")))->SetText(FText::FromString(TEXT("SUIT 14"))); CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSearchButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("search resets to page one of filtered result"), Page->GetText().ToString(), FString(TEXT("1 / 1"))); TestEqual(TEXT("search row count"), Rows->GetChildrenCount(), 1);
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSearchClearButton")))->OnClicked.Broadcast(); Next->OnClicked.Broadcast(); CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("CategoryHeavyButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("category reset has no stale rows"), Rows->GetChildrenCount(), 0); TestEqual(TEXT("empty category stays on valid first page"), Page->GetText().ToString(), FString(TEXT("1 / 1"))); TestFalse(TEXT("empty category cannot go back"), Previous->GetIsEnabled()); TestFalse(TEXT("empty category cannot go next"), Next->GetIsEnabled());
	return true;
}

bool FMSSuitSwapIndependentScreensTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GI.Get()));
	TStrongObjectPtr<UMSUILockSubsystem> UILock(NewObject<UMSUILockSubsystem>(GI.Get()));
	TStrongObjectPtr<UMSCustomizeWidget> Customize(NewObject<UMSCustomizeWidget>(GetTransientPackage()));
	Customize->Initialize(); Customize->InitializeForAutomationTests(); Customize->SetDeckSubsystemForTesting(Deck.Get()); Customize->SetUILockSubsystemForTesting(UILock.Get());

	TArray<UMSMobileSuitData*> Catalog;
	for (int32 Index = 0; Index < 15; ++Index)
		Catalog.Add(MSSuitSwapTests::MakeSuit(Deck.Get(), *FString::Printf(TEXT("OWNED SUIT %02d"), Index), 500 - Index, EMSMobileSuitUnitType::Assault));
	UMSMobileSuitData* Current = Catalog[0];
	Deck->AssignPreset(0, Current); Deck->SetSelectedSlotIndex(2);
	const FMSDeckLoadout BeforeLoadout = Deck->GetLoadoutInSlot(0);

	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSSuitSwapTests::MakeSwap(Deck.Get(), Catalog);
	Customize->SetSuitSwapWidgetForTesting(Swap.Get()); Swap->SetUILockSubsystemForTesting(UILock.Get()); Customize->Open(); Customize->SetVisibility(ESlateVisibility::Collapsed);
	Swap->OpenOwnedList();
	TestTrue(TEXT("owned tab opens the independent owned-list mode"), Swap->IsOwnedListMode());
	TestFalse(TEXT("owned list does not enter comparison mode"), Swap->IsComparisonPhase());
	TestEqual(TEXT("owned list first page contains seven rows"), CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox")))->GetChildrenCount(), 7);
	UButton* Next = CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitCatalogNextButton")));
	Next->OnClicked.Broadcast();
	TestEqual(TEXT("owned list page control reaches the second page"), CastChecked<UTextBlock>(Swap->GetWidgetFromName(TEXT("SuitCatalogPageText")))->GetText().ToString(), FString(TEXT("2 / 3")));
	CastChecked<UMSSuitSwapEntryWidget>(CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox")))->GetChildAt(0))->GetEntryButton()->OnClicked.Broadcast();
	TestTrue(TEXT("owned row selection remains in the owned-list screen"), Swap->IsOwnedListMode() && Swap->IsOpen() && !Swap->IsComparisonPhase());
	TestEqual(TEXT("owned row selection leaves the selected slot unchanged"), Deck->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("owned row selection leaves the equipped suit unchanged"), Deck->GetPresetInSlot(0), Current);
	const FMSDeckLoadout AfterOwnedLoadout = Deck->GetLoadoutInSlot(0);
	TestEqual(TEXT("owned row selection preserves loadout slot count"), AfterOwnedLoadout.EquippedWeapons.Num(), BeforeLoadout.EquippedWeapons.Num());
	for (int32 Index = 0; Index < BeforeLoadout.EquippedWeapons.Num() && Index < AfterOwnedLoadout.EquippedWeapons.Num(); ++Index)
		TestEqual(TEXT("owned row selection preserves each weapon slot"), AfterOwnedLoadout.EquippedWeapons[Index].ToSoftObjectPath().ToString(), BeforeLoadout.EquippedWeapons[Index].ToSoftObjectPath().ToString());
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSwapBackButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("owned list back closes the screen"), Swap->IsOpen());
	TestTrue(TEXT("owned list back returns to customize"), Customize->IsOpen());

	Customize->SetVisibility(ESlateVisibility::Collapsed); Swap->OpenDevelopment();
	TestTrue(TEXT("develop tab opens the independent development mode"), Swap->IsDevelopmentMode());
	TestFalse(TEXT("development screen has no confirmable deck swap"), Swap->CanConfirmSwap());
	TestEqual(TEXT("development screen exposes an empty blueprint list"), CastChecked<UScrollBox>(Swap->GetWidgetFromName(TEXT("SuitCatalogScrollBox")))->GetChildrenCount(), 0);
	TestTrue(TEXT("development screen explains its empty data boundary"), CastChecked<UTextBlock>(Swap->GetWidgetFromName(TEXT("DevelopmentEmptyStateText")))->GetVisibility() != ESlateVisibility::Collapsed);
	const FMSDeckLoadout BeforeDevelopmentLoadout = Deck->GetLoadoutInSlot(0);
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("ConfirmSuitSwapButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("development confirm input cannot change the selected suit"), Deck->GetPresetInSlot(0), Current);
	TestEqual(TEXT("development confirm input preserves selected slot"), Deck->GetSelectedSlotIndex(), 2);
	const FMSDeckLoadout AfterDevelopmentLoadout = Deck->GetLoadoutInSlot(0);
	TestEqual(TEXT("development confirm input preserves loadout slot count"), AfterDevelopmentLoadout.EquippedWeapons.Num(), BeforeDevelopmentLoadout.EquippedWeapons.Num());
	CastChecked<UButton>(Swap->GetWidgetFromName(TEXT("SuitSwapBackButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("development back closes the screen"), Swap->IsOpen());
	TestTrue(TEXT("development back returns to customize"), Customize->IsOpen());
	return true;
}
#endif
