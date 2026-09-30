#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/GameInstance.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSModuleCustomizeWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSSuitSwapWidget.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCustomizeLobbyAndStructureTest, "NeoMSGO.UI.Customize.LobbyNavigationAndStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCustomizeDeckRefreshTest, "NeoMSGO.UI.Customize.DeckChangedRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCustomizeDeckCostWarningColorTest, "NeoMSGO.UI.Customize.DeckCostWarningColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCustomizeCardRoutesTest, "NeoMSGO.UI.Customize.CardRoutesAndReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSCustomizeWidgetTests
{
	template <typename T>
	TStrongObjectPtr<T> MakeWidget()
	{
		TStrongObjectPtr<T> Widget(NewObject<T>(GetTransientPackage()));
		Widget->Initialize();
		Widget->InitializeForAutomationTests();
		return Widget;
	}

	UMSMobileSuitData* MakeSuit(UObject* Outer, const TCHAR* Name, const int32 Cost)
	{
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Outer);
		Suit->UnitName = FText::FromString(Name);
		Suit->Cost = Cost;
		Suit->MaxArmor = 4800;
		Suit->BoostSpeed = 14.5f;
		Suit->BoostCapacity = 120.0f;
		Suit->BoostRechargeRate = 18.0f;
		Suit->LoadCapacity = 45;
		return Suit;
	}

	UButton* Button(UUserWidget* Owner, const FName Name)
	{
		return Owner ? Cast<UButton>(Owner->GetWidgetFromName(Name)) : nullptr;
	}

	void Click(UUserWidget* Owner, const FName Name)
	{
		if (UButton* Target = Button(Owner, Name)) Target->OnClicked.Broadcast();
	}
}

bool FMSCustomizeLobbyAndStructureTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSCustomizeWidgetTests::MakeWidget<UMSLobbyWidget>();
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSCustomizeWidgetTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSCustomizeWidgetTests::MakeWidget<UMSSuitSwapWidget>();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* Suit = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("TEST SUIT ALPHA"), 345);
	Deck->AssignPreset(0, Suit);
	Customize->SetDeckSubsystemForTesting(Deck.Get());
	Swap->SetDeckSubsystemForTesting(Deck.Get());
	Swap->SetCatalogForTesting({ Suit });
	Customize->SetSuitSwapWidgetForTesting(Swap.Get());
	Lobby->SetCustomizeWidgetForTesting(Customize.Get());

	Lobby->Open();
	MSCustomizeWidgetTests::Click(Lobby.Get(), TEXT("CustomizeButton"));
	TestTrue(TEXT("lobby customize button opens the four-card deck screen"), Customize->IsOpen());
	TestFalse(TEXT("opening customize hides the lobby"), Lobby->IsOpen());
	TestEqual(TEXT("exactly four suit-card slots exist"), Customize->GetSuitCardCount(), UMSCustomizeWidget::CardCount);
	TestTrue(TEXT("slot zero displays its assigned suit name"), Customize->GetDisplayedSlotName(0).ToString().Contains(TEXT("TEST SUIT ALPHA")));

	for (int32 Index = 0; Index < UMSCustomizeWidget::CardCount; ++Index)
	{
		const FName CardName = *FString::Printf(TEXT("SuitCard%d"), Index);
		UButton* Card = MSCustomizeWidgetTests::Button(Customize.Get(), CardName);
		TestNotNull(*FString::Printf(TEXT("suit card %d exists"), Index), Card);
		if (!Card) continue;
		UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot);
		TestNotNull(*FString::Printf(TEXT("suit card %d has a design-canvas slot"), Index), CardSlot);
		if (CardSlot)
		{
			const FVector2D ExpectedPosition(452.0f + 255.0f * Index, 330.0f);
			TestTrue(*FString::Printf(TEXT("suit card %d uses the reference x/y"), Index), CardSlot->GetPosition().Equals(ExpectedPosition, 0.01f));
			TestTrue(*FString::Printf(TEXT("suit card %d uses the reference size"), Index), CardSlot->GetSize().Equals(FVector2D(248.0f, 387.0f), 0.01f));
		}
		for (const TCHAR* Suffix : { TEXT("PreviewImage"), TEXT("NameText"), TEXT("NumberText"), TEXT("StatsText") })
		{
			const FName ChildName = *FString::Printf(TEXT("Slot%d%s"), Index, Suffix);
			UWidget* Child = Customize->GetWidgetFromName(ChildName);
			TestNotNull(*FString::Printf(TEXT("%s is part of the rendered card"), *ChildName.ToString()), Child);
			if (Child) TestNotNull(*FString::Printf(TEXT("%s is attached to card content"), *ChildName.ToString()), Child->GetParent());
		}
	}
	TestTrue(TEXT("ground deck tab remains available"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("GroundDeckTab"))->GetIsEnabled());
	TestFalse(TEXT("space deck tab remains outside the observed flow"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("SpaceDeckTab"))->GetIsEnabled());
	TestTrue(TEXT("develop tab is an enabled route"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("DevelopTab"))->GetIsEnabled());
	TestTrue(TEXT("owned-suit tab is an enabled route"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("OwnedSuitTab"))->GetIsEnabled());
	TestFalse(TEXT("suit-set tab remains unavailable"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("SuitSetTab"))->GetIsEnabled());
	TestFalse(TEXT("owned-item tab remains unavailable"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("OwnedItemTab"))->GetIsEnabled());
	TestFalse(TEXT("energy-recovery tab remains unavailable"), MSCustomizeWidgetTests::Button(Customize.Get(), TEXT("EnergyRecoveryTab"))->GetIsEnabled());

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("OwnedSuitTab"));
	TestTrue(TEXT("owned-suit tab opens the owned-list screen"), Swap->IsOpen() && Swap->IsOwnedListMode());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	TestTrue(TEXT("owned-list back restores the parent deck"), Customize->IsOpen());
	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("DevelopTab"));
	TestTrue(TEXT("develop tab opens the development screen"), Swap->IsOpen() && Swap->IsDevelopmentMode());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	TestTrue(TEXT("development back restores the parent deck"), Customize->IsOpen());

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("CustomizeBackButton"));
	TestFalse(TEXT("back closes customize"), Customize->IsOpen());
	TestTrue(TEXT("back restores the lobby"), Lobby->IsOpen());
	return true;
}

bool FMSCustomizeDeckRefreshTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSCustomizeWidgetTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	Customize->SetDeckSubsystemForTesting(Deck.Get());
	UMSMobileSuitData* First = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("FIRST"), 400);
	UMSMobileSuitData* Shared = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("SHARED"), 525);
	UMSShieldWeaponData* LongNamedShield = NewObject<UMSShieldWeaponData>(Customize.Get());
	LongNamedShield->WeaponName = FText::FromString(TEXT("LONG_NATIVE_SHIELD_NAME_THAT_MUST_NOT_HIDE_STATE"));
	LongNamedShield->CategoryTag = MSTags::Weapon_Category_Shield;
	LongNamedShield->MaxDurability = 1380.0f;
	First->NativeShield = LongNamedShield;
	First->bShieldRemovable = true;
	Deck->AssignPreset(0, First);
	Deck->AssignPreset(1, Shared);
	Deck->AssignPreset(2, Shared);
	Customize->Open();

	TestTrue(TEXT("slot zero shows its live name"), Customize->GetDisplayedSlotName(0).ToString().Contains(TEXT("FIRST")));
	const FString CompactShieldText = Customize->GetDisplayedShieldText(0).ToString();
	TestTrue(TEXT("customize shield card keeps equipped state visible"), CompactShieldText.Contains(TEXT("装着中")));
	TestTrue(TEXT("customize shield card keeps durability visible"), CompactShieldText.Contains(TEXT("1380")));
	TestFalse(TEXT("customize shield card does not let a long name displace state"), CompactShieldText.Contains(TEXT("LONG_NATIVE_SHIELD_NAME")));
	TestTrue(TEXT("two slots can display the same catalog asset independently"),
		Customize->GetDisplayedSlotName(1).ToString().Contains(TEXT("SHARED")) && Customize->GetDisplayedSlotName(2).ToString().Contains(TEXT("SHARED")));
	TestTrue(TEXT("the other occupied card displays its own cost"),
		CastChecked<UTextBlock>(Customize->GetWidgetFromName(TEXT("Slot0StatsText")))->GetText().ToString().Contains(TEXT("COST 400")) &&
		CastChecked<UTextBlock>(Customize->GetWidgetFromName(TEXT("Slot1StatsText")))->GetText().ToString().Contains(TEXT("COST 525")));
	TestTrue(TEXT("deck cost includes each occupied slot"), Customize->GetDisplayedDeckCost().ToString().Contains(TEXT("1450/1720")));
	TestEqual(TEXT("same-asset slot two keeps its preview visible"),
		CastChecked<UImage>(Customize->GetWidgetFromName(TEXT("Slot2PreviewImage")))->GetVisibility(), ESlateVisibility::HitTestInvisible);

	Deck->RemovePreset(1);
	TestTrue(TEXT("removed card shows the empty-slot label"), Customize->GetDisplayedSlotName(1).ToString().Contains(TEXT("機体を入れる")));
	TestEqual(TEXT("removed card hides its preview"),
		CastChecked<UImage>(Customize->GetWidgetFromName(TEXT("Slot1PreviewImage")))->GetVisibility(), ESlateVisibility::Hidden);
	TestTrue(TEXT("removing one shared asset slot leaves the other slot intact"),
		Deck->GetPresetInSlot(1) == nullptr && Deck->GetPresetInSlot(2) == Shared && Customize->GetDisplayedSlotName(2).ToString().Contains(TEXT("SHARED")));
	TestTrue(TEXT("deck cost refreshes after removing one slot"), Customize->GetDisplayedDeckCost().ToString().Contains(TEXT("925/1720")));
	return true;
}

bool FMSCustomizeDeckCostWarningColorTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSCustomizeWidgetTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	Customize->SetDeckSubsystemForTesting(Deck.Get());
	UMSMobileSuitData* First = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("COST FIRST"), 900);
	UMSMobileSuitData* OverLimit = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("COST OVER"), 821);
	UMSMobileSuitData* AtLimit = MSCustomizeWidgetTests::MakeSuit(Customize.Get(), TEXT("COST LIMIT"), 820);
	Deck->AssignPreset(0, First);
	Deck->AssignPreset(1, OverLimit);
	TestEqual(TEXT("test deck exceeds the 1720 cost limit"), Deck->GetCurrentDeckCost(), UMSDeckSubsystem::DeckCostLimit + 1);
	TestTrue(TEXT("deck cost text uses the warning color above 1720"),
		Customize->GetDeckCostTextColorForAutomationTests().Equals(FLinearColor(1.0f, 0.12f, 0.08f, 1.0f)));
	Deck->AssignPreset(1, AtLimit);
	TestEqual(TEXT("test deck returns to the 1720 cost limit"), Deck->GetCurrentDeckCost(), UMSDeckSubsystem::DeckCostLimit);
	TestTrue(TEXT("deck cost text returns to normal at or below 1720"),
		Customize->GetDeckCostTextColorForAutomationTests().Equals(FLinearColor::White));
	return true;
}

bool FMSCustomizeCardRoutesTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSCustomizeWidgetTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSCustomizeWidgetTests::MakeWidget<UMSSuitDetailWidget>();
	TStrongObjectPtr<UMSSuitSwapWidget> Swap = MSCustomizeWidgetTests::MakeWidget<UMSSuitSwapWidget>();
	TStrongObjectPtr<UMSHangarWidget> Hangar(NewObject<UMSHangarWidget>(GetTransientPackage()));
	TStrongObjectPtr<UMSModuleCustomizeWidget> Module(NewObject<UMSModuleCustomizeWidget>(GetTransientPackage()));
	Hangar->Initialize();
	Module->Initialize();

	UMSMobileSuitData* Shared = MSCustomizeWidgetTests::MakeSuit(Deck.Get(), TEXT("SHARED ROUTE SUIT"), 300);
	UMSMobileSuitData* Other = MSCustomizeWidgetTests::MakeSuit(Deck.Get(), TEXT("OTHER ROUTE SUIT"), 440);
	Deck->AssignPreset(0, Shared);
	Deck->AssignPreset(1, Other);
	Deck->AssignPreset(2, Shared);

	Customize->SetDeckSubsystemForTesting(Deck.Get());
	Detail->SetDeckSubsystemForTesting(Deck.Get());
	Swap->SetDeckSubsystemForTesting(Deck.Get());
	Swap->SetCatalogForTesting({ Shared, Other });
	Customize->SetSuitDetailWidgetForTesting(Detail.Get());
	Customize->SetSuitSwapWidgetForTesting(Swap.Get());
	Detail->SetSwapWidgetForTesting(Swap.Get());
	Hangar->InitializeForAutomationTests(Shared);
	Hangar->SetDeckSubsystemForTesting(Deck.Get());
	Hangar->SetReturnSuitDetailWidget(Detail.Get());
	Detail->SetHangarWidgetForTesting(Hangar.Get());
	Module->InitializeForAutomationTests(Shared, {});
	Detail->SetModuleCustomizeWidgetForTesting(Module.Get());

	Customize->Open();
	TestTrue(TEXT("other slot exposes its own name and cost"),
		Customize->GetDisplayedSlotName(1).ToString().Contains(TEXT("OTHER ROUTE SUIT")) &&
		CastChecked<UTextBlock>(Customize->GetWidgetFromName(TEXT("Slot1StatsText")))->GetText().ToString().Contains(TEXT("COST 440")));

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("SuitCard0"));
	TestTrue(TEXT("occupied card opens the individual suit detail screen"), Detail->IsOpen() && !Customize->IsOpen());
	TestEqual(TEXT("detail retains the clicked deck slot"), Detail->GetDisplayedSlotIndex(), 0);
	TestTrue(TEXT("detail displays the selected catalog suit"), Detail->GetDisplayedSuitName().ToString().Contains(TEXT("SHARED ROUTE SUIT")));

	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("WeaponCustomizeButton"));
	TestTrue(TEXT("detail weapon action opens the deck-slot hangar"), Hangar->IsOpen() && !Detail->IsOpen());
	TestEqual(TEXT("weapon action leaves the shared deck selection on slot zero"), Deck->GetSelectedSlotIndex(), 0);
	MSCustomizeWidgetTests::Click(Hangar.Get(), TEXT("HangarBackButton"));
	TestTrue(TEXT("hangar back returns to detail"), Detail->IsOpen() && !Hangar->IsOpen());

	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("ModuleCustomizeButton"));
	TestTrue(TEXT("detail module action opens module customization"), Module->IsOpen() && !Detail->IsOpen());
	MSCustomizeWidgetTests::Click(Module.Get(), TEXT("ModuleCustomizeBackButton"));
	TestTrue(TEXT("module back returns to detail"), Detail->IsOpen() && !Module->IsOpen());

	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("SuitSwapButton"));
	TestTrue(TEXT("detail swap action opens replacement mode"), Swap->IsOpen() && Swap->IsSwapMode() && !Detail->IsOpen());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	TestTrue(TEXT("swap back returns to detail"), Detail->IsOpen() && !Swap->IsOpen());
	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("SuitDetailBackButton"));
	TestTrue(TEXT("detail back returns to the four-card parent"), Customize->IsOpen() && !Detail->IsOpen());

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("SuitCard2"));
	TestTrue(TEXT("a second card reaches detail through the same route"), Detail->IsOpen());
	TestEqual(TEXT("same asset in another card retains its independent selected slot"), Deck->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("same asset in another card retains its displayed name"), Detail->GetDisplayedSuitName().ToString(), Shared->UnitName.ToString());
	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("SuitDetailBackButton"));

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("SuitCard1"));
	TestTrue(TEXT("second occupied card reaches detail before deletion"), Detail->IsOpen());
	MSCustomizeWidgetTests::Click(Detail.Get(), TEXT("RemoveFromDeckButton"));
	TestNull(TEXT("deletion clears the selected parent card"), Deck->GetPresetInSlot(1));
	TestTrue(TEXT("deletion returns to the parent deck"), Customize->IsOpen() && !Detail->IsOpen());
	TestEqual(TEXT("deletion hides the parent card preview"),
		CastChecked<UImage>(Customize->GetWidgetFromName(TEXT("Slot1PreviewImage")))->GetVisibility(), ESlateVisibility::Hidden);
	TestTrue(TEXT("deletion leaves the same asset in its other slot"), Deck->GetPresetInSlot(2) == Shared);

	// The swap widget has one return owner at a time. Rebind it to the parent
	// before exercising the empty-card entry path.
	Swap->SetCustomizeWidget(Customize.Get());
	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("SuitCard1"));
	TestTrue(TEXT("empty card opens the replacement list directly"), Swap->IsOpen() && Swap->IsSwapMode());
	TestFalse(TEXT("empty card bypasses suit detail"), Detail->IsOpen());
	TestFalse(TEXT("replacement list hides the parent deck"), Customize->IsOpen());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	TestTrue(TEXT("empty-card swap back restores the parent deck"), Customize->IsOpen());

	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("OwnedSuitTab"));
	TestTrue(TEXT("parent owned tab opens owned-list mode"), Swap->IsOpen() && Swap->IsOwnedListMode());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	MSCustomizeWidgetTests::Click(Customize.Get(), TEXT("DevelopTab"));
	TestTrue(TEXT("parent develop tab opens development mode"), Swap->IsOpen() && Swap->IsDevelopmentMode());
	MSCustomizeWidgetTests::Click(Swap.Get(), TEXT("SuitSwapBackButton"));
	TestTrue(TEXT("all child routes return to the parent deck"), Customize->IsOpen());
	return true;
}

#endif
