#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Data/MSModuleData.h"
#include "Core/MSGameplayTags.h"
#include "UI/MSWeaponIconWidget.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailNavigationTest, "NeoMSGO.UI.SuitDetail.CardNavigationAndStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailRemoveTest, "NeoMSGO.UI.SuitDetail.RemoveReturnsToCustomize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailHangarTest, "NeoMSGO.UI.SuitDetail.WeaponCustomizeReturnsFromHangar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailDirectRouteTest, "NeoMSGO.UI.SuitDetail.DirectWeaponRouteUsesRequestedSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailLayoutTest, "NeoMSGO.UI.SuitDetail.FixedPreviewOverlayAndButtonSounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSuitDetailTests
{
	UTextBlock* FindTextDescendant(UWidget* Root)
	{
		if (!Root) return nullptr;
		if (UTextBlock* Text = Cast<UTextBlock>(Root)) return Text;
		if (UContentWidget* Content = Cast<UContentWidget>(Root))
			if (UWidget* Child = Content->GetContent())
				if (UTextBlock* Text = FindTextDescendant(Child)) return Text;
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
				if (UTextBlock* Text = FindTextDescendant(Panel->GetChildAt(Index))) return Text;
		return nullptr;
	}

	struct FScopedDeckSave
	{
		FString Name = FString::Printf(TEXT("NeoMSGO_SuitDetailTest_%s"),*FGuid::NewGuid().ToString());
		explicit FScopedDeckSave(UMSDeckSubsystem* Deck) { Deck->SetSaveSlotForAutomationTests(Name); Deck->EnablePersistenceForAutomationTests(); }
		~FScopedDeckSave() { UGameplayStatics::DeleteGameInSlot(Name,0); }
	};
	template <typename T> TStrongObjectPtr<T> MakeWidget()
	{
		TStrongObjectPtr<T> Result(NewObject<T>(GetTransientPackage())); Result->Initialize(); Result->InitializeForAutomationTests(); return Result;
	}

	UMSMobileSuitData* MakeSuit(UObject* Outer, const TCHAR* Name, const int32 Cost)
	{
		UMSMobileSuitData* Result = NewObject<UMSMobileSuitData>(Outer); Result->UnitName = FText::FromString(Name); Result->Cost = Cost;
		Result->MaxArmor = 4800; Result->BoostSpeed = 14.5f; Result->BoostCapacity = 120; Result->BoostRechargeRate = 18; Result->LoadCapacity = 45; return Result;
	}

	void Connect(UMSCustomizeWidget* Customize, UMSSuitDetailWidget* Detail, UMSDeckSubsystem* Deck)
	{
		Customize->SetDeckSubsystemForTesting(Deck); Detail->SetDeckSubsystemForTesting(Deck); Customize->SetSuitDetailWidgetForTesting(Detail); Customize->Open();
	}
}

bool FMSSuitDetailNavigationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSSuitDetailTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* First = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("DETAIL FIRST"), 300); UMSMobileSuitData* Third = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("DETAIL THIRD"), 500);
	UMSRangedWeaponData* Rifle = NewObject<UMSRangedWeaponData>(Deck.Get()); Rifle->WeaponName = FText::FromString(TEXT("LIVE BEAM RIFLE")); Rifle->CategoryTag = MSTags::Weapon_Category_BeamRifle;
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Deck.Get()); Shield->WeaponName = FText::FromString(TEXT("LIVE SHIELD")); Shield->CategoryTag = MSTags::Weapon_Category_Shield;
	UMSModuleData* Module = NewObject<UMSModuleData>(Deck.Get()); Module->ModuleName = FText::FromString(TEXT("LIVE MODULE"));
	First->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots); First->EquippedWeapons[0] = Rifle; First->EquippedModule = Module;
	Third->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots); Third->NativeShield = Shield; Third->bShieldRemovable = true;
	Deck->AssignPreset(0, First); Deck->AssignPreset(2, Third); MSSuitDetailTests::Connect(Customize.Get(), Detail.Get(), Deck.Get());
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard0")))->OnClicked.Broadcast();
	TestTrue(TEXT("clicking card N opens detail"), Detail->IsOpen()); TestFalse(TEXT("detail hides customize"), Customize->IsOpen());
	TestTrue(TEXT("clicked slot suit name is displayed"), Detail->GetDisplayedSuitName().ToString().Contains(TEXT("DETAIL FIRST")));
	TestEqual(TEXT("six status rows exist"), Detail->GetStatusRowCount(), UMSSuitDetailWidget::StatusRowCount);
	TestTrue(TEXT("module customize is enabled"), CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("ModuleCustomizeButton")))->GetIsEnabled());
	const FName DisabledButtons[] = { TEXT("EquipmentSetButton"), TEXT("SuitUpgradeButton"), TEXT("TraitUpgradeButton"), TEXT("EnergyRecoveryButton"), TEXT("ColoringEmblemButton"), TEXT("AdvancedCustomButton") };
	TestEqual(TEXT("exactly six disabled action definitions are expected"), static_cast<int32>(UE_ARRAY_COUNT(DisabledButtons)), UMSSuitDetailWidget::DisabledActionCount);
	for (const FName Name : DisabledButtons) TestFalse(*FString::Printf(TEXT("%s is disabled"), *Name.ToString()), CastChecked<UButton>(Detail->GetWidgetFromName(Name))->GetIsEnabled());
	TestEqual(TEXT("unsupported enhancement count is labelled instead of fabricated"), CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("EnhancementCountText")))->GetText().ToString(), FString(TEXT("強化回数  未対応")));
	UTextBlock* BonusSummary = CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitStatusBonusSummaryText")));
	TestNotNull(TEXT("single enhancement summary is attached to the visible instrument"), BonusSummary->GetParent());
	TestEqual(TEXT("unsupported enhancement additions have one truthful summary"), BonusSummary->GetText().ToString(), FString(TEXT("強化補正  未対応")));
	for (int32 Index = 0; Index < UMSSuitDetailWidget::StatusRowCount; ++Index)
		TestNull(TEXT("status rows do not repeat unsupported bonus labels"), Detail->GetWidgetFromName(*FString::Printf(TEXT("SuitStatusBonus%d"), Index)));
	UTextBlock* WeaponName = CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitDetailWeaponName0")));
	UMSWeaponIconWidget* WeaponIcon = CastChecked<UMSWeaponIconWidget>(Detail->GetWidgetFromName(TEXT("SuitDetailWeaponIcon0")));
	TestNotNull(TEXT("weapon name is attached to the actual row"), WeaponName->GetParent()); TestNotNull(TEXT("weapon silhouette is attached to the actual row"), WeaponIcon->GetParent());
	TestEqual(TEXT("detail weapon row displays actual equipped weapon"), WeaponName->GetText().ToString(), Rifle->WeaponName.ToString());
	TestEqual(TEXT("equipped weapon silhouette is visible"), WeaponIcon->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("empty weapon row does not draw a fictitious silhouette"), CastChecked<UMSWeaponIconWidget>(Detail->GetWidgetFromName(TEXT("SuitDetailWeaponIcon1")))->GetVisibility(), ESlateVisibility::Hidden);
	TestTrue(TEXT("deck card retains the clicked suit identity"), Customize->GetDisplayedSlotName(0).EqualTo(First->UnitName));
	UMSWeaponIconWidget* DeckWeaponIcon = CastChecked<UMSWeaponIconWidget>(Customize->GetWidgetFromName(TEXT("Deck0WeaponIcon0")));
	TestTrue(TEXT("deck card weapon tooltip uses the equipped weapon name"), DeckWeaponIcon->GetToolTipText().EqualTo(Rifle->WeaponName));
	TestEqual(TEXT("deck card weapon icon is visible before removal"), DeckWeaponIcon->GetVisibility(),ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("customize keeps native shield readout separate from weapon rows"), Customize->GetDisplayedShieldText(0).ToString().Contains(TEXT("盾なし")));
	UTextBlock* ModuleSummary = CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitDetailLoadoutText")));
	TestNotNull(TEXT("legacy loadout text is a visible module summary"), ModuleSummary->GetParent());
	TestEqual(TEXT("module summary displays the actual equipped module"), ModuleSummary->GetText().ToString(), FString(TEXT("ABILITY  LIVE MODULE")));
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("NextSuitButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("pager skips empty slot and selects slot 3"), Detail->GetDisplayedSlotIndex(), 2); TestTrue(TEXT("pager updates suit name"), Detail->GetDisplayedSuitName().ToString().Contains(TEXT("DETAIL THIRD")));
	TestEqual(TEXT("native shield does not occupy a numbered weapon row"), WeaponName->GetText().ToString(), FString(TEXT("---")));
	TestTrue(TEXT("changing suit shows the native shield separately"), Detail->GetDisplayedShieldText().ToString().Contains(TEXT("LIVE SHIELD")));
	TestEqual(TEXT("new suit without module clears visible module summary"), ModuleSummary->GetText().ToString(), FString(TEXT("ABILITY  ---")));
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("PreviousSuitButton")))->OnClicked.Broadcast(); TestEqual(TEXT("previous pager returns to first occupied slot"), Detail->GetDisplayedSlotIndex(), 0);
	// The deck's loadout notification persists data; the real UI refreshes details when the weapon screen returns.
	TStrongObjectPtr<UMSHangarWidget> Hangar(NewObject<UMSHangarWidget>(GetTransientPackage()));
	MSSuitDetailTests::FScopedDeckSave Save(Deck.Get());
	Hangar->Initialize(); Hangar->InitializeForAutomationTests(First); Hangar->SetDeckSubsystemForTesting(Deck.Get()); Hangar->SetWeaponCatalog({ Rifle }); Detail->SetHangarWidgetForTesting(Hangar.Get());
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("WeaponCustomizeButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("weapon removal starts in the actual weapon screen"), Hangar->IsOpen());
	TestFalse(TEXT("detail is hidden while editing weapons"), Detail->IsOpen());
	CastChecked<UButton>(Hangar->GetWidgetFromName(TEXT("WeaponSlotButton0")))->OnClicked.Broadcast();
	UButton* PrimaryAction = CastChecked<UButton>(Hangar->GetWidgetFromName(TEXT("EquipWeaponButton")));
	UButton* SecondaryRemove = CastChecked<UButton>(Hangar->GetWidgetFromName(TEXT("RemoveWeaponButton")));
	TestEqual(TEXT("equipped slot keeps the primary action visible"), PrimaryAction->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("equipped slot exposes the enabled primary action"), PrimaryAction->GetIsEnabled());
	TestEqual(TEXT("equipped slot uses one visible removal action"), SecondaryRemove->GetVisibility(), ESlateVisibility::Collapsed);
	UTextBlock* PrimaryActionText = MSSuitDetailTests::FindTextDescendant(PrimaryAction->GetContent());
	TestNotNull(TEXT("primary removal action has a visible label"), PrimaryActionText);
	if (PrimaryActionText)
		TestEqual(TEXT("primary removal action is labelled 装備解除"), PrimaryActionText->GetText().ToString(), FString(TEXT("装備解除")));
	PrimaryAction->OnClicked.Broadcast();
	TestTrue(TEXT("real removal action clears only the selected deck loadout"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].IsNull());
	TestEqual(TEXT("removal preserves catalog defaults"), First->EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(Rifle));
	TestNull(TEXT("removal does not change another suit's weapon loadout"), Deck->GetLoadoutInSlot(2).EquippedWeapons[0].Get());
	CastChecked<UButton>(Hangar->GetWidgetFromName(TEXT("HangarBackButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("normal return restores the detail screen"), Detail->IsOpen());
	TestFalse(TEXT("normal return closes the weapon screen"), Hangar->IsOpen());
	TestEqual(TEXT("weapon loadout change clears the actual row name"), WeaponName->GetText().ToString(), FString(TEXT("---")));
	TestEqual(TEXT("weapon removal hides its actual silhouette"), WeaponIcon->GetVisibility(), ESlateVisibility::Hidden);
	TestEqual(TEXT("weapon removal preserves the visible module summary"), ModuleSummary->GetText().ToString(), FString(TEXT("ABILITY  LIVE MODULE")));
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("SuitDetailBackButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("normal detail return restores customize"), Customize->IsOpen());
	TestTrue(TEXT("return keeps the deck card suit identity"), Customize->GetDisplayedSlotName(0).EqualTo(First->UnitName));
	TestEqual(TEXT("deck card hides the removed weapon icon after returning"),DeckWeaponIcon->GetVisibility(),ESlateVisibility::Hidden);
	return true;
}

bool FMSSuitDetailRemoveTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSSuitDetailTests::MakeWidget<UMSCustomizeWidget>(); TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	Deck->AssignPreset(1, MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("REMOVE TARGET"), 360)); MSSuitDetailTests::Connect(Customize.Get(), Detail.Get(), Deck.Get());
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard1")))->OnClicked.Broadcast(); CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("RemoveFromDeckButton")))->OnClicked.Broadcast();
	TestNull(TEXT("remove clears selected deck slot"), Deck->GetPresetInSlot(1)); TestFalse(TEXT("remove closes detail"), Detail->IsOpen()); TestTrue(TEXT("remove restores customize"), Customize->IsOpen()); return true;
}

bool FMSSuitDetailHangarTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSSuitDetailTests::MakeWidget<UMSCustomizeWidget>(); TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	TStrongObjectPtr<UMSHangarWidget> Hangar(NewObject<UMSHangarWidget>(GetTransientPackage())); Hangar->Initialize();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>()); TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* Suit = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("HANGAR TARGET"), 420); Deck->AssignPreset(0, Suit); Hangar->InitializeForAutomationTests(Suit); Hangar->SetDeckSubsystemForTesting(Deck.Get());
	MSSuitDetailTests::Connect(Customize.Get(), Detail.Get(), Deck.Get()); Detail->SetHangarWidgetForTesting(Hangar.Get()); CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard0")))->OnClicked.Broadcast();
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("WeaponCustomizeButton")))->OnClicked.Broadcast(); TestTrue(TEXT("weapon customize opens existing hangar"), Hangar->IsOpen()); TestFalse(TEXT("hangar hides detail"), Detail->IsOpen());
	Hangar->Close(); TestFalse(TEXT("closing hangar closes hangar"), Hangar->IsOpen()); TestTrue(TEXT("closing hangar restores detail"), Detail->IsOpen()); TestTrue(TEXT("restored detail keeps suit name"), Detail->GetDisplayedSuitName().ToString().Contains(TEXT("HANGAR TARGET"))); return true;
}

bool FMSSuitDetailDirectRouteTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCustomizeWidget> Customize = MSSuitDetailTests::MakeWidget<UMSCustomizeWidget>();
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	TStrongObjectPtr<UMSHangarWidget> Hangar(NewObject<UMSHangarWidget>(GetTransientPackage()));
	Hangar->Initialize();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* First = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("DIRECT FIRST"), 300);
	UMSMobileSuitData* Second = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("DIRECT SECOND"), 420);
	Deck->AssignPreset(0, First);
	Deck->AssignPreset(2, Second);
	MSSuitDetailTests::Connect(Customize.Get(), Detail.Get(), Deck.Get());
	Detail->SetHangarWidgetForTesting(Hangar.Get());
	CastChecked<UButton>(Customize->GetWidgetFromName(TEXT("SuitCard0")))->OnClicked.Broadcast();
	TestTrue(TEXT("direct route starts from the currently displayed detail"), Detail->IsOpen());

	// This exercises the public gateway used by other screens. The requested
	// slot must be applied before the hangar opens, rather than using slot zero.
	Detail->OpenWeaponCustomization(2);
	TestEqual(TEXT("direct route commits the requested deck selection"), Deck->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("direct route displays the requested suit"), Detail->GetDisplayedSlotIndex(), 2);
	TestTrue(TEXT("direct route opens the weapon screen once"), Hangar->IsOpen());
	TestFalse(TEXT("direct route hides the detail screen"), Detail->IsOpen());
	return true;
}

bool FMSSuitDetailLayoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	const TSharedRef<SWidget> SlateWidget = Detail->TakeWidget();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* Suit = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("LAYOUT PREVIEW"), 300);
	Deck->AssignPreset(0, Suit);
	Detail->SetDeckSubsystemForTesting(Deck.Get());
	Detail->Open(0);

	UCanvasPanel* DesignCanvas = CastChecked<UCanvasPanel>(Detail->GetWidgetFromName(TEXT("SuitDetailDesignCanvas")));
	UImage* FullscreenPreview = nullptr;
	UCanvasPanelSlot* PreviewSlot = nullptr;
	int32 PreviewIndex = INDEX_NONE;
	for (int32 Index = 0; Index < DesignCanvas->GetChildrenCount(); ++Index)
	{
		UImage* Image = Cast<UImage>(DesignCanvas->GetChildAt(Index));
		UCanvasPanelSlot* Slot = Image ? Cast<UCanvasPanelSlot>(Image->Slot) : nullptr;
		if (Slot && Slot->GetPosition() == FVector2D::ZeroVector &&
			Slot->GetSize() == FVector2D(1920.0f, 1080.0f))
		{
			FullscreenPreview = Image;
			PreviewSlot = Slot;
			PreviewIndex = Index;
			break;
		}
	}
	TestNotNull(TEXT("preview image is a root design-canvas child"), FullscreenPreview);
	TestNotNull(TEXT("preview image has its own full-screen canvas slot"), PreviewSlot);
	if (FullscreenPreview && PreviewSlot)
	{
		TestEqual(TEXT("preview image fills the 1920 by 1080 design canvas"), PreviewSlot->GetSize(), FVector2D(1920.0f, 1080.0f));
		TestEqual(TEXT("preview image begins at the design-canvas origin"), PreviewSlot->GetPosition(), FVector2D::ZeroVector);
		TestEqual(TEXT("preview image does not intercept controls"), FullscreenPreview->GetVisibility(), ESlateVisibility::HitTestInvisible);
	}
	UButton* BackButton = CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("SuitDetailBackButton")));
	UCanvasPanelSlot* BackSlot = Cast<UCanvasPanelSlot>(BackButton->Slot);
	TestNotNull(TEXT("back control is a direct design-canvas child"), BackSlot);
	if (BackSlot && PreviewSlot)
	{
		const int32 BackIndex = DesignCanvas->GetChildIndex(BackButton);
		TestTrue(TEXT("full-screen preview paints before the controls above it"),
			PreviewSlot->GetZOrder() < BackSlot->GetZOrder() ||
			(PreviewSlot->GetZOrder() == BackSlot->GetZOrder() && PreviewIndex < BackIndex));
	}

	UOverlay* PreviewRegion = CastChecked<UOverlay>(Detail->GetWidgetFromName(TEXT("SuitDetailPreviewRegion")));
	UHorizontalBox* RotationBar = CastChecked<UHorizontalBox>(Detail->GetWidgetFromName(TEXT("SuitDetailRotationBar")));
	USizeBox* PreviewSize = Cast<USizeBox>(PreviewRegion->GetParent());
	UVerticalBox* PreviewColumn = PreviewSize ? Cast<UVerticalBox>(PreviewSize->GetParent()) : nullptr;
	TestEqual(TEXT("preview region contains the rotation overlay layer"), PreviewRegion->GetChildrenCount(), 1);
	TestTrue(TEXT("rotation bar overlays inside the fixed preview region"),
		PreviewRegion->GetChildAt(0) == RotationBar && RotationBar->GetParent() == PreviewRegion);
	TestNotNull(TEXT("preview region remains inside its fixed-size box"), PreviewSize);
	TestNotNull(TEXT("fixed preview box stays in the preview column"), PreviewColumn);
	if (PreviewSize && PreviewColumn)
	{
		TestEqual(TEXT("preview column keeps its original three rows"), PreviewColumn->GetChildrenCount(), 3);
		TestEqual(TEXT("preview box remains the middle column row"), PreviewColumn->GetChildAt(1), static_cast<UWidget*>(PreviewSize));
		TestEqual(TEXT("rotation controls do not add a row to the preview column"), RotationBar->GetParent(), static_cast<UPanelWidget*>(PreviewRegion));
		TestEqual(TEXT("preview region keeps its fixed width"), PreviewSize->GetWidthOverride(), 510.0f);
		TestEqual(TEXT("preview region keeps its fixed height"), PreviewSize->GetHeightOverride(), 650.0f);
	}

	UButton* Left = CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("RotateSuitLeftButton")));
	UWidget* LeftParent = Left->GetParent();
	TestNotNull(TEXT("left rotation button has a fixed-size wrapper"), LeftParent);
	if (LeftParent)
	{
		TestTrue(TEXT("left rotation button stays inside the preview overlay"),
			LeftParent->GetParent() == RotationBar && RotationBar->GetParent() == PreviewRegion);
	}

	UButton* Previous = CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("PreviousSuitButton")));
	UWidget* PagerFrame = Previous;
	for (int32 Depth = 0; Depth < 3 && PagerFrame; ++Depth) PagerFrame = PagerFrame->GetParent();
	UCanvasPanelSlot* PagerSlot = PagerFrame ? Cast<UCanvasPanelSlot>(PagerFrame->Slot) : nullptr;
	TestNotNull(TEXT("deck pager is an independent canvas child"), PagerSlot);
	if (PagerSlot)
	{
		TestEqual(TEXT("deck pager keeps its fixed vertical position"), PagerSlot->GetPosition(), FVector2D(866.0f, 800.0f));
		TestEqual(TEXT("deck pager keeps its fixed size"), PagerSlot->GetSize(), FVector2D(178.0f, 61.0f));
		TestTrue(TEXT("deck pager clears the persistent footer chrome"), PagerSlot->GetPosition().Y + PagerSlot->GetSize().Y <= 864.0f);
	}
	TestTrue(TEXT("deck pager is separate from preview overlay"), PagerFrame != PreviewRegion);
	const FVector2D PagerPositionBeforeRotation = PagerSlot ? PagerSlot->GetPosition() : FVector2D::ZeroVector;
	const FVector2D PagerSizeBeforeRotation = PagerSlot ? PagerSlot->GetSize() : FVector2D::ZeroVector;
	Left->OnPressed.Broadcast();
	TestTrue(TEXT("rotation press advances without changing the layout row"),
		!FMath::IsNearlyZero(Detail->TickPreviewRotationForTesting(1.0f / 30.0f, true)));
	TestEqual(TEXT("rotation leaves the pager canvas position unchanged"),
		PagerSlot ? PagerSlot->GetPosition() : FVector2D::ZeroVector, PagerPositionBeforeRotation);
	TestEqual(TEXT("rotation leaves the pager canvas size unchanged"),
		PagerSlot ? PagerSlot->GetSize() : FVector2D::ZeroVector, PagerSizeBeforeRotation);
	Detail->Close();

	UTextBlock* WeaponName = CastChecked<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitDetailWeaponName0")));
	TestFalse(TEXT("weapon names cannot create extra wrapped rows"), WeaponName->GetAutoWrapText());
	TestEqual(TEXT("weapon names use ellipsis within their fixed row"), WeaponName->GetTextOverflowPolicy(), ETextOverflowPolicy::Ellipsis);
	TestNotNull(TEXT("detail action keeps the UI click sound"),
		CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("WeaponCustomizeButton")))->GetStyle().PressedSlateSound.GetResourceObject());
	TestNotNull(TEXT("detail back keeps the UI back sound"),
		CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("SuitDetailBackButton")))->GetStyle().PressedSlateSound.GetResourceObject());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailHoldRotationTest, "NeoMSGO.UI.SuitDetail.HoldRotationAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSSuitDetailHoldRotationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSSuitDetailWidget> Detail = MSSuitDetailTests::MakeWidget<UMSSuitDetailWidget>();
	// UWidget::IsVisible reads a live Slate widget, not only serialized Visibility.
	// Keep the actual Slate tree alive as a displayed runtime widget would.
	const TSharedRef<SWidget> SlateWidget = Detail->TakeWidget();
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	UMSMobileSuitData* First = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("ROTATE FIRST"), 300);
	UMSMobileSuitData* Other = MSSuitDetailTests::MakeSuit(Deck.Get(), TEXT("ROTATE OTHER"), 400);
	Deck->AssignPreset(0, First); Deck->AssignPreset(2, Other);
	Detail->SetDeckSubsystemForTesting(Deck.Get()); Detail->Open(0);
	TestTrue(TEXT("hold fixture has a visible live Slate tree"), Detail->IsVisible());
	UButton* Left = CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("RotateSuitLeftButton")));
	UButton* Right = CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("RotateSuitRightButton")));
	const FMSDeckLoadout Before = Deck->GetLoadoutInSlot(0);
	auto TickOneSecond = [&Detail](bool bPressed)
	{
		float Angle = 0; for (int32 Frame = 0; Frame < 60; ++Frame) Angle += Detail->TickPreviewRotationForTesting(1.0f / 60.0f, bPressed);
		return Angle;
	};
	Left->OnPressed.Broadcast();
	TestTrue(TEXT("left held for one second rotates 75 degrees"), FMath::IsNearlyEqual(TickOneSecond(true), -75.0f, 0.01f));
	Left->OnReleased.Broadcast();
	TestEqual(TEXT("released arrow stays idle even if stale pressed state is supplied"), TickOneSecond(true), 0.0f);
	Right->OnPressed.Broadcast();
	TestTrue(TEXT("right hold rotates in the opposite direction"), FMath::IsNearlyEqual(TickOneSecond(true), 75.0f, 0.01f));
	TestEqual(TEXT("lost Slate press immediately cancels rotation"), TickOneSecond(false), 0.0f);
	TestEqual(TEXT("press does not restart after capture loss"), TickOneSecond(true), 0.0f);
	Right->OnPressed.Broadcast(); Right->OnUnhovered.Broadcast();
	TestEqual(TEXT("leaving button cancels rotation"), TickOneSecond(true), 0.0f);
	Left->OnPressed.Broadcast(); Detail->Close();
	TestEqual(TEXT("closing screen stops rotation"), TickOneSecond(true), 0.0f);
	Detail->Open(0);
	TestEqual(TEXT("reopened screen does not retain held input"), TickOneSecond(true), 0.0f);
	Right->OnPressed.Broadcast(); Detail->SetVisibility(ESlateVisibility::Collapsed); Detail->SetVisibility(ESlateVisibility::Visible);
	TestEqual(TEXT("child screen hide and return clears rotation"), TickOneSecond(true), 0.0f);
	TestEqual(TEXT("rotation never changes deck selection"), Deck->GetSelectedSlotIndex(), 0);
	TestEqual(TEXT("rotation never changes displayed slot"), Detail->GetDisplayedSlotIndex(), 0);
	TestEqual(TEXT("rotation preserves first preset"), Deck->GetPresetInSlot(0), First);
	TestEqual(TEXT("rotation preserves other preset"), Deck->GetPresetInSlot(2), Other);
	TestTrue(TEXT("rotation preserves weapon loadout"), Deck->GetLoadoutInSlot(0).EquippedWeapons == Before.EquippedWeapons);
	Right->OnPressed.Broadcast();
	CastChecked<UButton>(Detail->GetWidgetFromName(TEXT("NextSuitButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("existing next arrow still changes deck slot"), Detail->GetDisplayedSlotIndex(), 2);
	TestEqual(TEXT("changing machine cancels previous hold"), TickOneSecond(true), 0.0f);
	Detail->Close();
	return true;
}

#endif
