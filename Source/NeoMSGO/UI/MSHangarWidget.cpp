#include "UI/MSHangarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/AssetManager.h"
#include "InputCoreTypes.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSHUDTypes.h"
#include "UI/MSWeaponIconWidget.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSPixelUI.h"
#include "UI/MSPixelPainter.h"
#include "Styling/SlateBrush.h"

namespace
{
	const FLinearColor CardColor(0.055f, 0.085f, 0.12f, 1.0f);
	const FLinearColor AccentColor(0.25f, 0.75f, 1.0f, 1.0f);
	const FLinearColor SelectedColor(0.95f, 0.72f, 0.08f, 1.0f);
	const FLinearColor OverCapacityColor(1.0f, 0.12f, 0.12f, 1.0f);
	const FLinearColor NormalLoadColor(0.8f, 0.9f, 1.0f, 1.0f);
	const FLinearColor SaveSuccessColor(0.48f, 1.0f, 0.72f, 1.0f);
	const FLinearColor SaveCancelledColor(1.0f, 0.78f, 0.35f, 1.0f);

	FSlateBrush MakeSemanticBrush(const FLinearColor& Fill, const FLinearColor& Outline)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = Fill;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(0.0f, FSlateColor(Outline), 1.5f);
		return Brush;
	}

	FLinearColor ScaleSemanticColor(const FLinearColor& Color, const float Scale)
	{
		return FLinearColor(
			FMath::Clamp(Color.R * Scale, 0.0f, 1.0f),
			FMath::Clamp(Color.G * Scale, 0.0f, 1.0f),
			FMath::Clamp(Color.B * Scale, 0.0f, 1.0f),
			Color.A);
	}

	void ApplySemanticBorder(UBorder* Border, const FLinearColor& Fill, const FLinearColor& Outline)
	{
		if (!Border) return;
		Border->SetBrush(MakeSemanticBrush(Fill, Outline));
		Border->SetBrushColor(FLinearColor::White);
	}

	void ApplySemanticButton(UButton* Button, const FLinearColor& Fill, const FLinearColor& Outline)
	{
		if (!Button) return;
		// Keep the normal UMG button and its existing label, but replace the authored
		// blue/purple surface with a semantic vector brush for this action.
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(MakeSemanticBrush(Fill, Outline));
		Style.SetHovered(MakeSemanticBrush(ScaleSemanticColor(Fill, 1.14f), ScaleSemanticColor(Outline, 1.08f)));
		Style.SetPressed(MakeSemanticBrush(ScaleSemanticColor(Fill, 0.78f), ScaleSemanticColor(Outline, 0.88f)));
		Style.SetDisabled(MakeSemanticBrush(FLinearColor(0.22f, 0.24f, 0.24f, 0.92f), FLinearColor(0.45f, 0.48f, 0.48f, 0.92f)));
		Button->SetStyle(Style);
		if (UOverlay* Frame = Cast<UOverlay>(Button->GetContent()))
		{
			// The first overlay child is the existing MSPixelUI button surface. Hide it
			// so its fallback tint cannot cover the semantic brush behind the label.
			if (Frame->GetChildrenCount() > 0 && Frame->GetChildAt(0))
			{
				Frame->GetChildAt(0)->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// Original UI: seconds = (1000 - reload performance) / 100.
	// Runtime assets continue to store seconds. See docs/UIParity/BalanceReferenceAudit.md.
	bool HasReloadPerformance(const UMSWeaponDataBase* Weapon)
	{
		return Weapon && Weapon->MagazineSize > 0 && FMath::IsFinite(Weapon->ReloadTime);
	}

	int32 ReloadPerformance(const UMSWeaponDataBase* Weapon)
	{
		if (!HasReloadPerformance(Weapon)) return 0;
		return FMath::RoundToInt(FMath::Clamp(1000.0f - Weapon->ReloadTime * 100.0f, 0.0f, 1000.0f));
	}

	FString ReloadPerformanceText(const UMSWeaponDataBase* Weapon)
	{
		return HasReloadPerformance(Weapon)
			? FString::FromInt(ReloadPerformance(Weapon)) : FString(TEXT("---"));
	}

	bool IsShieldWeapon(const UMSWeaponDataBase* Weapon)
	{
		return Weapon && (Cast<UMSShieldWeaponData>(Weapon) != nullptr || Weapon->CategoryTag == MSTags::Weapon_Category_Shield);
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, float Size = 18.0f,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		return MSPixelUI::Text(Tree, Text, Size, Color, Name);
	}

	/** Converts a legacy suit preset into the current loadout contract. A shield
	 * remains at its old number while its state moves to bShieldEquipped. */
	FMSDeckLoadout BuildCompatibilityLoadout(const UMSMobileSuitData* Preset)
	{
		FMSDeckLoadout Result;
		Result.EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
		Result.bShieldEquipped = false;
		if (!Preset) return Result;

		Result.EquippedModule = Preset->EquippedModule;
		const bool bHasNativeShield = Preset->GetNativeShield() != nullptr;
		Result.bShieldEquipped = bHasNativeShield;
		const int32 Count = FMath::Min(Preset->EquippedWeapons.Num(), UMSMobileSuitData::MaxWeaponSlots);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef = Preset->EquippedWeapons[Index];
			if (IsShieldWeapon(WeaponRef.LoadSynchronous())) continue;
			Result.EquippedWeapons[Index] = WeaponRef;
		}
		return Result;
	}

	/** Filters a snapshot as well as old standalone presets, preserving its
	 * explicit shield toggle unless the old numbered slot carries that shield. */
	FMSDeckLoadout NormalizeLoadoutForSuit(const UMSMobileSuitData* Preset, const FMSDeckLoadout& Input)
	{
		FMSDeckLoadout Result = Input;
		Result.EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
		bool bLegacyShield = false;
		for (TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : Result.EquippedWeapons)
		{
			if (IsShieldWeapon(WeaponRef.LoadSynchronous()))
			{
				WeaponRef.Reset();
				bLegacyShield = true;
			}
		}
		const bool bHasNativeShield = Preset && Preset->GetNativeShield() != nullptr;
		Result.bShieldEquipped = bHasNativeShield && (Result.bShieldEquipped || bLegacyShield);
		return Result;
	}

	void Place(UCanvasPanel* Canvas, UWidget* Child, const float X, const float Y, const float Width, const float Height)
	{
		if (!Canvas || !Child) return;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(Width, Height));
	}

	void AddComparisonMetric(UWidgetTree* Tree, UCanvasPanel* Canvas, const TCHAR* Label, const int32 Index,
		TArray<TObjectPtr<UTextBlock>>& ValueTexts, TArray<TObjectPtr<UProgressBar>>& Bars)
	{
		if (!Tree || !Canvas) return;
		const int32 Column = Index % 2;
		const int32 Row = Index / 2;
		const float X = 232.0f + Column * 286.0f;
		const float Y = 40.0f + Row * 42.0f;
		UBorder* Cell = Tree->ConstructWidget<UBorder>();
		ApplySemanticBorder(Cell, MSPixel::Hex(0x121c27), MSPixel::Hex(0x5b636f));
		Place(Canvas, Cell, X, Y, 280, 38);
		UTextBlock* ValueText = MakeText(Tree, FText::FromString(FString::Printf(TEXT("%s ---"), Label)), 14.0f, NormalLoadColor);
		ValueText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		ValueText->SetClipping(EWidgetClipping::ClipToBounds);
		Place(Canvas, ValueText, X + 7, Y + 3, 263.0f, 21.0f);
		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>();
		Bar->SetFillColorAndOpacity(AccentColor);
		FProgressBarStyle Style = Bar->GetWidgetStyle();
		Style.BackgroundImage = MakeSemanticBrush(MSPixel::Hex(0x101820), MSPixel::Hex(0x626872));
		Style.FillImage.TintColor = FLinearColor::White;
		Bar->SetWidgetStyle(Style);
		Place(Canvas, Bar, X + 8, Y + 27.0f, 263.0f, 6.0f);
		ValueTexts.Add(ValueText);
		Bars.Add(Bar);
	}

	UOverlay* Panel(UWidgetTree* Tree, UWidget* Content, const EMSVectorSurface Surface = EMSVectorSurface::Panel,
		const FMargin Padding = FMargin(8.0f), const int32 Variant = 0)
	{
		return MSPixelUI::Frame(Tree, Content, Surface, Padding, Variant);
	}

	UButton* Button(UWidgetTree* Tree, const FText& Label, const FName Name, const float PixelHeight = 16.0f,
		const int32 Variant = 0, const FLinearColor TextColor = FLinearColor::White, UTextBlock** OutLabel = nullptr)
	{
		UButton* Result = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Result);
		// ClearButtonBrush resets the authored button style, including its Slate
		// pressed sound. Reapply the shared click route for every generated Hangar
		// action; callers can override the close action with the Back sound below.
		MSUIStyle::ApplyButtonSound(Result);
		UTextBlock* Text = MakeText(Tree, Label, PixelHeight, TextColor);
		if (OutLabel) *OutLabel = Text;
		Text->SetJustification(ETextJustify::Center);
		UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly); Fit->SetContent(Text);
		Result->SetContent(Panel(Tree, Fit, EMSVectorSurface::Button, FMargin(7.0f, 3.0f), Variant));
		MSUIStyle::StretchButtonContent(Result);
		return Result;
	}

	void ClearCommittedSaveStatus(UTextBlock* StatusText, const bool bSavePending)
	{
		if (!StatusText || bSavePending || StatusText->GetText().ToString() != TEXT("オンラインへ保存しました。")) return;
		StatusText->SetText(FText::GetEmpty());
		StatusText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMSWeaponCatalogEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	// The list row already supplies the authored frame. Keeping the default
	// button plate here creates a second, centered button-looking box inside it.
	MSPixelUI::ClearButtonBrush(Button);
	MSUIStyle::ApplyButtonSound(Button);
	UBorder* Border = WidgetTree->ConstructWidget<UBorder>(); RowBorder = Border;
	ApplySemanticBorder(Border, MSPixel::Hex(0x444c57), MSPixel::Hex(0x9caaaE)); Border->SetPadding(FMargin(3.0f));
	UCanvasPanel* Row = WidgetTree->ConstructWidget<UCanvasPanel>(); Border->SetContent(Row);
	Place(Row, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::WeaponImage), 0, 0, 146, 56);
	WeaponIconWidget = WidgetTree->ConstructWidget<UMSWeaponIconWidget>();
	WeaponIconWidget->Initialize();
	Place(Row, WeaponIconWidget, 5, 3, 136, 50);
	WeaponText = MakeText(WidgetTree, FText::GetEmpty(), 17.0f); WeaponText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); WeaponText->SetClipping(EWidgetClipping::ClipToBounds);
	Place(Row, WeaponText, 156, 2, 388, 25);
	WeightText = MakeText(WidgetTree, FText::GetEmpty(), 18.0f); WeightText->SetJustification(ETextJustify::Right);
	Place(Row, WeightText, 352, 28, 187, 23);
	EquippedStateText = MakeText(WidgetTree, FText::GetEmpty(), 12.0f, MSPixel::Hex(0xd7fb5a));
	Place(Row, EquippedStateText, 156, 32, 197, 20);
	// ScrollBox children use their desired size unless the row declares a
	// height. The video rows are one fixed design-pixel band each; wrapping the
	// button keeps that contract while its slot supplies the available width.
	USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(); RowSize->SetHeightOverride(62.0f); RowSize->SetContent(Button);
	Button->SetContent(Border); MSUIStyle::StretchButtonContent(Button); WidgetTree->RootWidget = RowSize;
	Button->OnClicked.AddDynamic(this, &UMSWeaponCatalogEntryWidget::HandleClicked);
}

void UMSWeaponCatalogEntryWidget::SetWeapon(UMSWeaponDataBase* InWeapon, UMSHangarWidget* InOwningHangar)
{
	if (!WidgetTree->RootWidget) NativeOnInitialized();
	Weapon = InWeapon;
	OwningHangar = InOwningHangar;
	const int32 EquippedSlot = OwningHangar && Weapon ? OwningHangar->GetEquippedSlot(Weapon) : INDEX_NONE;
	const bool bSelected = OwningHangar && Weapon && OwningHangar->GetSelectedWeapon() == Weapon;
	const FString StateText = bSelected
		? (EquippedSlot != INDEX_NONE ? FString::Printf(TEXT("選択中 / 装備中 SLOT%d"), EquippedSlot + 1) : FString(TEXT("選択中")))
		: (EquippedSlot != INDEX_NONE ? FString::Printf(TEXT("装備中 SLOT%d"), EquippedSlot + 1) : FString());
	if (WeaponText) { WeaponText->SetText(Weapon ? Weapon->WeaponName : FText::FromString(TEXT("---"))); WeaponText->SetToolTipText(WeaponText->GetText()); }
	if (WeightText) WeightText->SetText(Weapon ? FText::FromString(FString::Printf(TEXT("重量   %d"), Weapon->Weight)) : FText::GetEmpty());
	if (EquippedStateText) EquippedStateText->SetText(FText::FromString(StateText));
	if (WeaponIconWidget)
	{
		WeaponIconWidget->SetWeaponPhoto(Weapon, MSPixel::Hex(0xDAE6E6));
	}
}

void UMSWeaponCatalogEntryWidget::SetSelected(const bool bInSelected)
{
	if (RowBorder)
	{
		if (bInSelected)
		{
			// MSGO5 f14032 uses a dark-amber selected band with a bright yellow rim.
			// Apply it locally because the shared Mode B ListRowSelected plate is cyan.
			ApplySemanticBorder(RowBorder, MSPixel::Hex(0x57550b), MSPixel::Hex(0xE5EF26));
		}
		else
		{
			ApplySemanticBorder(RowBorder, MSPixel::Hex(0x444c57), MSPixel::Hex(0x9caaae));
		}
	}
}

void UMSWeaponCatalogEntryWidget::HandleClicked()
{
	if (OwningHangar && Weapon) OwningHangar->ChooseCatalogWeapon(Weapon);
}

void UMSHangarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true);
	Categories = { MSTags::Weapon_Category_MachineGun, MSTags::Weapon_Category_BeamRifle,
		MSTags::Weapon_Category_Bazooka, MSTags::Weapon_Category_Melee,
		MSTags::Weapon_Category_Supply, MSTags::Weapon_Category_ContinuousBeam,
		MSTags::Weapon_Category_Shotgun, MSTags::Weapon_Category_Homing };
	SelectedCategory = Categories[0];

	// Video 5 is authored at 1920x1080. Keep one design surface and scale it
	// once so the weapon frame retains the observed pixel composition at 16:9.
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("HangarScaleToFit"));
	Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(1920.0f); Design->SetHeightOverride(1080.0f);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HangarDesignCanvas"));
	Design->SetContent(Canvas); Fit->SetContent(Design); WidgetTree->RootWidget = Fit;
	Place(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::HangarBackdrop, TEXT("HangarBackdrop")), 0, 0, 1920, 1080);

	UTextBlock* Title = MakeText(WidgetTree, FText::FromString(TEXT("武器")), 32.0f, MSPixel::Hex(0x79f4ee));
	UScaleBox* TitleFit = WidgetTree->ConstructWidget<UScaleBox>(); TitleFit->SetStretch(EStretch::ScaleToFit); TitleFit->SetStretchDirection(EStretchDirection::DownOnly); TitleFit->SetContent(Title);
	UButton* CloseButton = Button(WidgetTree, FText::FromString(TEXT("戻る")), TEXT("HangarBackButton"), 18.0f, 1, MSPixel::Hex(0xFFF4D8));
	ApplySemanticButton(CloseButton, MSPixel::Hex(0x9A4F0D), MSPixel::Hex(0xF2A13B));
	CloseButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCloseClicked); MSUIStyle::ApplyButtonSound(CloseButton, MSUIStyle::EButtonSound::Back);
	Place(Canvas, CloseButton, 1550, 120, 250, 48);

	UCanvasPanel* FrameCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HangarWeaponFrameCanvas"));
	Place(Canvas, Panel(WidgetTree, FrameCanvas, EMSVectorSurface::WeaponPanel, FMargin(0)), 228, 162, 1492, 693);
	// The heading sits on the frame chrome in the reference. Add it after the
	// frame so the translucent panel never paints over its cyan pixels.
	Place(Canvas, TitleFit, 267, 174, 90, 42);
	Place(Canvas, MakeText(WidgetTree, FText::FromString(TEXT("weapon customize")), 18, MSPixel::Hex(0x79f4ee)), 364, 187, 250, 27);
	Place(FrameCanvas, MakeText(WidgetTree, FText::FromString(TEXT("スロット選択")), 16, MSPixel::Hex(0x79f4ee)), 22, 60, 250, 21);
	UTextBlock* CapacityInfo = MakeText(WidgetTree, FText::FromString(TEXT("ブースト効率   ---\n機体バランス   ---")), 16, MSPixel::Hex(0x83e77e));
	CapacityInfo->SetToolTipText(FText::FromString(TEXT("重量による補正値は現在の機体データに設定されていません。")));
	Place(FrameCanvas, CapacityInfo, 991, 17, 225, 48);
	NativeShieldStatusText = MakeText(WidgetTree, FText::FromString(TEXT("盾なし")), 14.0f, MSPixel::Hex(0x78F3EE), TEXT("NativeShieldStatusText"));
	NativeShieldStatusText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Place(FrameCanvas, NativeShieldStatusText, 1230, 10, 240, 20);
	NativeShieldDurabilityText = MakeText(WidgetTree, FText::FromString(TEXT("DURABILITY ---")), 12.0f, MSPixel::Hex(0xB8E8E5), TEXT("NativeShieldDurabilityText"));
	Place(FrameCanvas, NativeShieldDurabilityText, 1230, 29, 240, 18);
	UTextBlock* ShieldToggleLabel = nullptr;
	NativeShieldToggleButton = Button(WidgetTree, FText::FromString(TEXT("シールド切替")), TEXT("NativeShieldToggleButton"), 12.0f, 0, MSPixel::Hex(0xD9F8F3), &ShieldToggleLabel);
	NativeShieldToggleText = ShieldToggleLabel;
	NativeShieldToggleButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleNativeShieldClicked);
	Place(FrameCanvas, NativeShieldToggleButton, 1230, 51, 240, 26);
	LoadText = MakeText(WidgetTree, FText::GetEmpty(), 18.0f, NormalLoadColor);
	Place(FrameCanvas, LoadText, 410, 18, 568, 25);
	LoadBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HangarLoadBar")); LoadBar->SetFillColorAndOpacity(AccentColor);
	Place(FrameCanvas, LoadBar, 410, 48, 570, 14);

	for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index)
	{
		UButton* CardButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("WeaponSlotButton%d"), Index));
		MSPixelUI::ClearButtonBrush(CardButton);
		MSUIStyle::ApplyButtonSound(CardButton);
		UCanvasPanel* CardCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		UBorder* CardBorder = WidgetTree->ConstructWidget<UBorder>(); MSUIStyle::ApplyBrush(CardBorder, MSUIStyle::EBrush::ListRow, CardColor); CardBorder->SetContent(CardCanvas);
		CardBorder->SetPadding(FMargin(3));
		CardButton->SetContent(CardBorder); MSUIStyle::StretchButtonContent(CardButton);
		Place(CardCanvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::WeaponImage), 0, 0, 174, 54);
		Place(CardCanvas, MakeText(WidgetTree, FText::FromString(FString::Printf(TEXT("SLOT%d"), Index + 1)), 11, MSPixel::Hex(0x8be5e1)), 4, 1, 65, 16);
		UMSWeaponIconWidget* Icon = WidgetTree->ConstructWidget<UMSWeaponIconWidget>(UMSWeaponIconWidget::StaticClass(), *FString::Printf(TEXT("WeaponSlotIcon%d"), Index));
		Icon->Initialize(); Place(CardCanvas, Icon, 8, 13, 157, 39);
		UTextBlock* WeaponName = MakeText(WidgetTree, FText::GetEmpty(), 14.0f, FLinearColor::White);
		WeaponName->SetAutoWrapText(true); WeaponName->SetWrapTextAt(163.0f);
		WeaponName->SetClipping(EWidgetClipping::ClipToBounds);
		Place(CardCanvas, WeaponName, 4, 56, 163, 34);
		Place(FrameCanvas, CardButton, 22 + Index * 189, 82, 180, 97);
		SlotWeaponIcons.Add(Icon); SlotWeaponTexts.Add(WeaponName); SlotCardBorders.Add(CardBorder);
		if (Index == 0) CardButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleSlot0Clicked);
		if (Index == 1) CardButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleSlot1Clicked);
		if (Index == 2) CardButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleSlot2Clicked);
	}

	PickerTitleText = MakeText(WidgetTree, FText::FromString(TEXT("武器 / SLOT1")), 21.0f, AccentColor);
	Place(FrameCanvas, PickerTitleText, 22, 181, 568, 28);
	const TCHAR* Labels[CategoryCount] = { TEXT("MG"), TEXT("BR"), TEXT("BAZ"), TEXT("格闘"), TEXT("補給"), TEXT("照射"), TEXT("SG"), TEXT("誘導") };
	for (int32 Index = 0; Index < CategoryCount; ++Index)
	{
		UButton* CategoryButton = Button(WidgetTree, FText::FromString(Labels[Index]), *FString::Printf(TEXT("WeaponCategoryButton%d"), Index), 11.0f, 0, MSPixel::Hex(0xD9F8F3));
		CategoryButtons.Add(CategoryButton); Place(FrameCanvas, CategoryButton, 22 + Index * 60, 210, 58, 24);
	}
	CategoryButtons[0]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory0Clicked);
	CategoryButtons[1]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory1Clicked);
	CategoryButtons[2]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory2Clicked);
	CategoryButtons[3]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory3Clicked);
	CategoryButtons[4]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory4Clicked);
	CategoryButtons[5]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory5Clicked);
	CategoryButtons[6]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory6Clicked);
	CategoryButtons[7]->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleCategory7Clicked);

	WeaponListView = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("HangarWeaponList")); WeaponListView->SetScrollBarVisibility(ESlateVisibility::Visible);
	Place(FrameCanvas, WeaponListView, 22, 232, 568, 337);
	UHorizontalBox* PageControls = WidgetTree->ConstructWidget<UHorizontalBox>();
	PreviousPageButton = Button(WidgetTree, FText::FromString(TEXT("‹")), TEXT("WeaponPreviousPageButton"), 16.0f, 0, MSPixel::Hex(0xE8FFFF));
	PreviousPageButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandlePreviousPageClicked); PageControls->AddChildToHorizontalBox(PreviousPageButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PageText = MakeText(WidgetTree, FText::FromString(TEXT("1 / 1")), 16.0f, NormalLoadColor); PageText->SetJustification(ETextJustify::Center); PageControls->AddChildToHorizontalBox(PageText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NextPageButton = Button(WidgetTree, FText::FromString(TEXT("›")), TEXT("WeaponNextPageButton"), 16.0f, 0, MSPixel::Hex(0xE8FFFF));
	NextPageButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleNextPageClicked); PageControls->AddChildToHorizontalBox(NextPageButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Place(FrameCanvas, Panel(WidgetTree, PageControls, EMSVectorSurface::Panel, FMargin(4, 1)), 167, 577, 280, 30);

	UCanvasPanel* CompareCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HangarWeaponCompareCanvas"));
	Place(FrameCanvas, CompareCanvas, 616, 82, 948, 590);
	PreviewNameText = MakeText(WidgetTree, FText::FromString(TEXT("武器詳細 / 比較")), 18.0f, AccentColor); PreviewNameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); PreviewNameText->SetVisibility(ESlateVisibility::Collapsed);
	PreviewImage = WidgetTree->ConstructWidget<UImage>(); PreviewImage->SetVisibility(ESlateVisibility::Collapsed);

	// Measured MSGO5 f14098: equipment at (872,244), selection at (964,442).
	auto BuildComparisonPanel = [this, CompareCanvas](const bool bSelected, const float PanelY,
		TObjectPtr<UTextBlock>& HeaderText, TObjectPtr<UMSWeaponIconWidget>& IconWidget,
		TArray<TObjectPtr<UTextBlock>>& StatTexts, TArray<TObjectPtr<UProgressBar>>& StatBars)
	{
		UCanvasPanel* PanelCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Place(CompareCanvas, PanelCanvas, bSelected ? 120 : 28, PanelY, 826, 170);
		UBorder* HeaderPlate = WidgetTree->ConstructWidget<UBorder>();
		ApplySemanticBorder(HeaderPlate, MSPixel::Hex(0x454a52,.98f), MSPixel::Hex(0x929a9e));
		HeaderPlate->SetPadding(FMargin(9,3));
		HeaderText = MakeText(WidgetTree, FText::GetEmpty(), 17.0f, FLinearColor::White);
		HeaderText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); HeaderText->SetClipping(EWidgetClipping::ClipToBounds);
		HeaderPlate->SetContent(HeaderText.Get()); Place(PanelCanvas, HeaderPlate, 28, 0, 790, 34);
		UBorder* StatePlate = WidgetTree->ConstructWidget<UBorder>();
		ApplySemanticBorder(StatePlate, MSPixel::Hex(0x040e13), MSPixel::Hex(0x697983));
		UTextBlock* StateText = MakeText(WidgetTree, FText::FromString(bSelected ? TEXT("選\n択\n中") : TEXT("装\n備\n中")), 14,
			bSelected ? MSPixel::Hex(0xe7e63d) : MSPixel::Hex(0x98ed36));
		StateText->SetJustification(ETextJustify::Center); StatePlate->SetContent(StateText);
		Place(PanelCanvas, StatePlate, 0, 39, 23, 124);
		Place(PanelCanvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::WeaponImage), 28, 39, 198, 124);
		IconWidget = WidgetTree->ConstructWidget<UMSWeaponIconWidget>(UMSWeaponIconWidget::StaticClass(),
			*FString::Printf(TEXT("Hangar%sComparisonIcon"), bSelected ? TEXT("Selected") : TEXT("Equipped")));
		IconWidget->Initialize(); Place(PanelCanvas, IconWidget.Get(), 35, 48, 184, 104);
		const TCHAR* Labels[] = { TEXT("威力"), TEXT("リロード時間"), TEXT("DP"), TEXT("ロック距離 (m)"), TEXT("射程距離 (m)"), TEXT("効果範囲 (m)") };
		for (int32 Index=0;Index<UE_ARRAY_COUNT(Labels);++Index)
			AddComparisonMetric(WidgetTree, PanelCanvas, Labels[Index], Index, StatTexts, StatBars);
	};
	BuildComparisonPanel(false, 0, EquippedComparisonText, EquippedComparisonIcon, EquippedStatTexts, EquippedStatBars);
	BuildComparisonPanel(true, 198, SelectedComparisonText, SelectedComparisonIcon, SelectedStatTexts, SelectedStatBars);
	// Materials and tickets have no persistent inventory yet. Preserve their role
	// and observed positions, while never inventing a balance or a successful upgrade.
	for (int32 Row=0; Row<2; ++Row)
	{
		UBorder* Materials = WidgetTree->ConstructWidget<UBorder>();
		ApplySemanticBorder(Materials, MSPixel::Hex(0x252e39,.85f), MSPixel::Hex(0x64727d)); Materials->SetPadding(FMargin(12,4));
		Materials->SetContent(MakeText(WidgetTree,FText::FromString(Row==0 ? TEXT("必要素材   ---                                      所持数  --- / --- 個") : TEXT("必要素材   ---                                      所持数  --- / --- 個")),16,MSPixel::Hex(0x939aa1)));
		Materials->SetToolTipText(FText::FromString(TEXT("武器強化と素材所持情報はまだ利用できません。")));
		Place(CompareCanvas, Materials, 120, 372+Row*54, 818, 48);
	}
	UBorder* TicketPlate = WidgetTree->ConstructWidget<UBorder>();
	ApplySemanticBorder(TicketPlate, MSPixel::Hex(0x374c5a,.96f), MSPixel::Hex(0x778a94)); TicketPlate->SetPadding(FMargin(14,7));
	TicketPlate->SetContent(MakeText(WidgetTree,FText::FromString(TEXT("チケット所持数     メカニック  ---     マスター  ---")),17,MSPixel::Hex(0xc7d4d9)));
	Place(CompareCanvas,TicketPlate,120,532,550,48);
	UButton* Reset = Button(WidgetTree,FText::FromString(TEXT("強化リセット")),TEXT("WeaponUpgradeResetButton"),20,1);
	ApplySemanticButton(Reset,MSPixel::Hex(0x71211a),MSPixel::Hex(0xdc7353));Reset->SetIsEnabled(false);
	Reset->SetToolTipText(FText::FromString(TEXT("武器強化はまだ利用できません。")));Place(CompareCanvas,Reset,684,532,254,48);
	WeaponParametersText=MakeText(WidgetTree,FText::GetEmpty(),14,NormalLoadColor);
	WeaponParametersText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);WeaponParametersText->SetClipping(EWidgetClipping::ClipToBounds);
	Place(CompareCanvas,WeaponParametersText,128,486,805,25);
	EmptySlotGuideText=MakeText(WidgetTree,FText::FromString(TEXT("左のリストから武器を選択してください。")),16,NormalLoadColor);
	EmptySlotGuideText->SetJustification(ETextJustify::Center);Place(CompareCanvas,EmptySlotGuideText,355,272,560,31);
	SaveStatusText=MakeText(WidgetTree,FText::GetEmpty(),14,NormalLoadColor);SaveStatusText->SetAutoWrapText(true);
	SaveStatusText->SetVisibility(ESlateVisibility::Collapsed);Place(FrameCanvas,SaveStatusText,648,674,820,38);

	EquipButton = Button(WidgetTree, FText::FromString(TEXT("装備")), TEXT("EquipWeaponButton"), 18.0f, 0, MSPixel::Hex(0xF5FFF3));
	if (UOverlay* EquipFrame = Cast<UOverlay>(EquipButton->GetContent()))
		if (EquipFrame->GetChildrenCount() > 1)
			if (UScaleBox* LabelFit = Cast<UScaleBox>(EquipFrame->GetChildAt(1)))
			{
				EquipButtonText = Cast<UTextBlock>(LabelFit->GetContent());
				if (EquipButtonText) EquipButtonText->SetColorAndOpacity(FSlateColor(MSPixel::Hex(0xF5FFF3)));
			}
	ApplySemanticButton(EquipButton, MSPixel::Hex(0x245D1D), MSPixel::Hex(0x98EA45));
	EquipButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleEquipClicked); Place(FrameCanvas, EquipButton, 22, 623, 270, 45);
	RemoveButton = Button(WidgetTree, FText::FromString(TEXT("装備解除")), TEXT("RemoveWeaponButton"), 16.0f, 1, MSPixel::Hex(0xFFF4D8)); RemoveButton->OnClicked.AddDynamic(this, &UMSHangarWidget::HandleRemoveClicked); Place(FrameCanvas, RemoveButton, 302, 623, 180, 45);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHangarWidget::NativeDestruct()
{
	if (DeckSubsystem)
	{
		DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckChanged);
		DeckSubsystem->OnDeckSyncStateChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckSyncStateChanged);
	}
	if (OnlineAccountSubsystem.IsValid())
	{
		OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineAccountChanged);
		OnlineAccountSubsystem->OnRequestFinished.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineRequestFinished);
	}
	OnlineAccountSubsystem.Reset();
	if (PreviewRenderer) PreviewRenderer->Shutdown();
	Super::NativeDestruct();
}

void UMSHangarWidget::Open(UMSMobileSuitData* Preset)
{
	if (!Preset) return;
	BindDeckSubsystem();
	int32 DeckSlotIndex = INDEX_NONE;
	if (DeckSubsystem)
	{
		const int32 SharedSlot = DeckSubsystem->GetSelectedSlotIndex();
		if (DeckSubsystem->GetPresetInSlot(SharedSlot) == Preset)
		{
			DeckSlotIndex = SharedSlot;
		}
		else
		{
			for (int32 Index = 0; Index < UMSDeckSubsystem::SlotCount; ++Index)
			{
				if (DeckSubsystem->GetPresetInSlot(Index) == Preset)
				{
					DeckSlotIndex = Index;
					break;
				}
			}
		}
	}
	OpenInternal(Preset, DeckSlotIndex);
}

void UMSHangarWidget::OpenForDeckSlot(const int32 DeckSlotIndex)
{
	BindDeckSubsystem();
	if (!DeckSubsystem || DeckSlotIndex < 0 || DeckSlotIndex >= UMSDeckSubsystem::SlotCount) return;
	UMSMobileSuitData* Preset = DeckSubsystem->GetPresetInSlot(DeckSlotIndex);
	if (!Preset) return;
	OpenInternal(Preset, DeckSlotIndex);
}

void UMSHangarWidget::OpenReadOnlyLoadout(UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout)
{
	if (!Preset || bOnlineSavePending) return;
	OpenInternal(Preset, INDEX_NONE);
	bReadOnlyLoadout = true;
	WorkingLoadout = NormalizeLoadoutForSuit(Preset, Loadout);
	SelectedWeapon = GetWorkingWeapon(0);
	if (SelectedWeapon) SelectedCategory = SelectedWeapon->CategoryTag;
	RefreshLoadout(); RefreshFilteredWeapons(true); RefreshSelectionPreview();
	if (SaveStatusText)
	{
		SaveStatusText->SetText(FText::FromString(TEXT("参戦中の装備を表示しています。変更は試合を退出して格納庫で行ってください。")));
		SaveStatusText->SetVisibility(ESlateVisibility::Visible);
	}
}

void UMSHangarWidget::BindDeckSubsystem()
{
	UMSDeckSubsystem* Resolved = ResolveDeckSubsystem();
	if (DeckSubsystem != Resolved)
	{
		if (DeckSubsystem)
		{
			DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckChanged);
			DeckSubsystem->OnDeckSyncStateChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckSyncStateChanged);
		}
		DeckSubsystem = Resolved;
	}
	if (DeckSubsystem)
	{
		DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleDeckChanged);
		DeckSubsystem->OnDeckSyncStateChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleDeckSyncStateChanged);
	}

	UMSOnlineAccountSubsystem* ResolvedOnline = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		ResolvedOnline = GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	}
	if (OnlineAccountSubsystem.Get() != ResolvedOnline)
	{
		if (OnlineAccountSubsystem.IsValid())
		{
			OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineAccountChanged);
			OnlineAccountSubsystem->OnRequestFinished.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineRequestFinished);
		}
		OnlineAccountSubsystem = ResolvedOnline;
	}
	if (OnlineAccountSubsystem.IsValid())
	{
		OnlineAccountSubsystem->OnAccountChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleOnlineAccountChanged);
		OnlineAccountSubsystem->OnRequestFinished.AddUniqueDynamic(this, &UMSHangarWidget::HandleOnlineRequestFinished);
	}
}

UMSDeckSubsystem* UMSHangarWidget::ResolveDeckSubsystem() const
{
	if (DeckSubsystem) return DeckSubsystem.Get();
	if (UGameInstance* GameInstance = GetGameInstance())
		return GameInstance->GetSubsystem<UMSDeckSubsystem>();
	return nullptr;
}

void UMSHangarWidget::HandleDeckChanged(const int32 ChangedSlotIndex)
{
	if (bOnlineSavePending) return;
	if (!bUsingDeckLoadout || !DeckSubsystem || (ChangedSlotIndex != INDEX_NONE && ChangedSlotIndex != ActiveDeckSlotIndex)) return;
	UMSMobileSuitData* Preset = DeckSubsystem->GetPresetInSlot(ActiveDeckSlotIndex);
	if (!Preset)
	{
		Close();
		return;
	}
	ActivePreset = Preset;
	WorkingLoadout = DeckSubsystem->GetLoadoutInSlot(ActiveDeckSlotIndex);
	SelectedWeapon = GetWorkingWeapon(PickerSlotIndex);
	if (SelectedWeapon && Categories.Contains(SelectedWeapon->CategoryTag)) SelectedCategory = SelectedWeapon->CategoryTag;
	RefreshLoadout(); RefreshFilteredWeapons(true); RefreshSelectionPreview();
}

void UMSHangarWidget::SetSavePendingVisual(const bool bPending)
{
	bOnlineSavePending = bPending;
	if (SaveStatusText && bPending)
		SaveStatusText->SetColorAndOpacity(FSlateColor(NormalLoadColor));
	if (EquipButton)
	{
		EquipButton->SetIsEnabled(!bReadOnlyLoadout && !bPending && SelectedWeapon != nullptr);
	}
	if (RemoveButton)
	{
		RemoveButton->SetIsEnabled(!bReadOnlyLoadout && !bPending && GetWorkingWeapon(PickerSlotIndex) != nullptr);
	}
}

void UMSHangarWidget::HandleDeckSyncStateChanged(const EMSOnlineDeckSyncState State)
{
	// Only consume sync transitions for the candidate initiated by this Hangar.
	// Other deck UI can share the same subsystem and must not lock this screen.
	if (!bOnlineSavePending) return;
	if (OnlineAccountSubsystem.IsValid()
		&& PendingAccountGeneration >= 0
		&& OnlineAccountSubsystem->GetAuthGeneration() != PendingAccountGeneration)
	{
		bOnlineSavePending = false;
		bOnlineSaveStateObserved = true;
		PendingLoadout = FMSDeckLoadout();
		PendingDeckSlotIndex = INDEX_NONE;
		SetSavePendingVisual(false);
		if (SaveStatusText)
		{
			SaveStatusText->SetColorAndOpacity(FSlateColor(SaveCancelledColor));
			SaveStatusText->SetText(FText::FromString(TEXT("アカウントが切り替わったため、保存結果を破棄しました。")));
			SaveStatusText->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	bOnlineSaveStateObserved = true;
	if (State == EMSOnlineDeckSyncState::Pending)
	{
		SetSavePendingVisual(true);
		if (SaveStatusText)
		{
			SaveStatusText->SetText(FText::FromString(TEXT("オンラインへ保存中…")));
			SaveStatusText->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	const int32 CommittedSlot = PendingDeckSlotIndex;
	const FMSDeckLoadout CommittedLoadout = PendingLoadout;
	const FString FailureMessage = PendingSaveFailureMessage;
	bOnlineSavePending = false;
	PendingDeckSlotIndex = INDEX_NONE;
	PendingLoadout = FMSDeckLoadout();
	PendingSaveFailureMessage.Reset();
	SetSavePendingVisual(false);
	if (State == EMSOnlineDeckSyncState::Committed)
	{
		if (bUsingDeckLoadout && CommittedSlot == ActiveDeckSlotIndex && CommittedLoadout.EquippedWeapons.Num() == UMSMobileSuitData::MaxWeaponSlots)
		{
			WorkingLoadout = CommittedLoadout;
		}
		SelectedWeapon = GetWorkingWeapon(PickerSlotIndex);
		if (SelectedWeapon && Categories.Contains(SelectedWeapon->CategoryTag)) SelectedCategory = SelectedWeapon->CategoryTag;
		RefreshLoadout(); RefreshFilteredWeapons(true); RefreshSelectionPreview();
		if (SaveStatusText)
		{
			SaveStatusText->SetColorAndOpacity(FSlateColor(SaveSuccessColor));
			SaveStatusText->SetText(FText::FromString(TEXT("オンラインへ保存しました。")));
			SaveStatusText->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	ShowSaveFailureMessage(FailureMessage);
}

void UMSHangarWidget::HandleOnlineAccountChanged(const FMSOnlineIdentity, const int64 Generation)
{
	if (!bOnlineSavePending || Generation == PendingAccountGeneration) return;
	bOnlineSavePending = false;
	bOnlineSaveStateObserved = true;
	PendingDeckSlotIndex = INDEX_NONE;
	PendingLoadout = FMSDeckLoadout();
	PendingSaveFailureMessage.Reset();
	SetSavePendingVisual(false);
	if (SaveStatusText)
	{
		SaveStatusText->SetColorAndOpacity(FSlateColor(SaveCancelledColor));
		SaveStatusText->SetText(FText::FromString(TEXT("アカウントが切り替わったため、保存結果を破棄しました。")));
		SaveStatusText->SetVisibility(ESlateVisibility::Visible);
	}
}

void UMSHangarWidget::HandleOnlineRequestFinished(const FName Operation, const bool bSuccess,
	const FText Message, const int64 Generation)
{
	if (Operation != TEXT("deck_put") || !bOnlineSavePending || Generation != PendingAccountGeneration) return;
	if (!bSuccess && !Message.IsEmpty()) PendingSaveFailureMessage = Message.ToString();
}

void UMSHangarWidget::OpenInternal(UMSMobileSuitData* Preset, const int32 DeckSlotIndex)
{
	bReadOnlyLoadout = false;
	if (!Preset) return;
	ActivePreset = Preset;
	ActiveDeckSlotIndex = DeckSlotIndex;
	bUsingDeckLoadout = DeckSubsystem && ActiveDeckSlotIndex >= 0 && ActiveDeckSlotIndex < UMSDeckSubsystem::SlotCount
		&& DeckSubsystem->GetPresetInSlot(ActiveDeckSlotIndex) == Preset;
	if (bUsingDeckLoadout)
	{
		WorkingLoadout = DeckSubsystem->GetLoadoutInSlot(ActiveDeckSlotIndex);
	}
	else
	{
		// Compatibility path for native tests that supply a standalone transient preset.
		WorkingLoadout = BuildCompatibilityLoadout(ActivePreset);
	}
	if (PreviewNameText) PreviewNameText->SetText(ActivePreset->UnitName);
	if (PreviewRenderer) PreviewRenderer->SetMobileSuit(ActivePreset,
		ActivePreset->GetNativeShield() && WorkingLoadout.bShieldEquipped);
	ActiveSuitCategories.Reset();
	for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : WorkingLoadout.EquippedWeapons)
		if (UMSWeaponDataBase* Weapon = WeaponRef.LoadSynchronous(); Weapon && !IsShieldWeapon(Weapon)) ActiveSuitCategories.Add(Weapon->CategoryTag);
	if (WeaponCatalog.IsEmpty()) RefreshCatalogFromAssetManager();
	PickerSlotIndex = 0; CurrentPageIndex = 0;
	SelectedWeapon = GetWorkingWeapon(PickerSlotIndex);
	if (SelectedWeapon && Categories.Contains(SelectedWeapon->CategoryTag)) SelectedCategory = SelectedWeapon->CategoryTag;
	if (SaveStatusText) { SaveStatusText->SetText(FText::GetEmpty()); SaveStatusText->SetVisibility(ESlateVisibility::Collapsed); }
	if (!ActiveSuitCategories.IsEmpty() && !ActiveSuitCategories.Contains(SelectedCategory)) SelectedCategory = ActiveSuitCategories.Array()[0];
	if (!IsInViewport()) AddToViewport(ReturnBriefingWidget ? 60 : 40); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus(); RefreshLoadout(); RefreshFilteredWeapons(true); RefreshSelectionPreview();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("Hangar"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return !IsOpen(); }));
}

void UMSHangarWidget::Close()
{
	if (bOnlineSavePending)
	{
		if (SaveStatusText) SaveStatusText->SetText(FText::FromString(TEXT("オンライン保存中です。完了するまで閉じられません。")));
		return;
	}
	SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("Hangar"));
	// Deck-backed edits are deliberately only in WorkingLoadout until Equip/Remove.
	// Dropping this candidate makes Back/RMB/Escape a true cancel path.
	SelectedWeapon = nullptr;
	WorkingLoadout = FMSDeckLoadout();
	ActivePreset = nullptr;
	ActiveDeckSlotIndex = INDEX_NONE;
	bUsingDeckLoadout = false;
	if (ReturnSortieWidget) ReturnSortieWidget->HandleHangarClosed();
	else if (ReturnSuitDetailWidget) ReturnSuitDetailWidget->HandleHangarClosed();
	else if (ReturnBriefingWidget) ReturnBriefingWidget->HandleEquipmentClosed();
}

FReply UMSHangarWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSHangarWidget::SetWeaponCatalog(const TArray<UMSWeaponDataBase*>& Weapons)
{
	WeaponCatalog.Reset();
	for (UMSWeaponDataBase* Weapon : Weapons)
		if (IsValid(Weapon) && !IsShieldWeapon(Weapon)) WeaponCatalog.Add(Weapon);
	RefreshFilteredWeapons();
}

void UMSHangarWidget::RefreshCatalogFromAssetManager()
{
	UAssetManager& Manager = UAssetManager::Get(); TArray<FPrimaryAssetId> Ids; Manager.GetPrimaryAssetIdList(UMSWeaponDataBase::PrimaryAssetType, Ids);
	Ids.Sort([](const FPrimaryAssetId& A, const FPrimaryAssetId& B) { return A.ToString() < B.ToString(); });
	TArray<UMSWeaponDataBase*> Weapons;
	for (const FPrimaryAssetId& Id : Ids)
	{
		UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(Manager.GetPrimaryAssetObject(Id));
		if (!Weapon) Weapon = Cast<UMSWeaponDataBase>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		if (Weapon) Weapons.Add(Weapon);
	}
	SetWeaponCatalog(Weapons);
}

void UMSHangarWidget::SelectCategory(FGameplayTag Category)
{
	if (Categories.Contains(Category))
	{
		ClearCommittedSaveStatus(SaveStatusText, bOnlineSavePending);
		SelectedCategory = Category;
		CurrentPageIndex = 0;
		if (!SelectedWeapon || SelectedWeapon->CategoryTag != SelectedCategory) SelectedWeapon = nullptr;
		RefreshFilteredWeapons(); RefreshSelectionPreview();
	}
}

void UMSHangarWidget::RefreshFilteredWeapons(const bool bSyncSelectedWeaponPage)
{
	FilteredItems.Reset();
	if (WeaponListView) WeaponListView->ClearChildren();
	for (UMSWeaponDataBase* Weapon : WeaponCatalog) if (Weapon && !IsShieldWeapon(Weapon) && Weapon->CategoryTag == SelectedCategory)
	{
		FilteredItems.Add(Weapon);
	}
	PageCount = FMath::Max(1, FMath::DivideAndRoundUp(FilteredItems.Num(), WeaponPageSize));
	if (bSyncSelectedWeaponPage && SelectedWeapon)
	{
		const int32 SelectedIndex = FilteredItems.IndexOfByPredicate([this](const TObjectPtr<UMSWeaponDataBase>& Weapon)
		{
			return Weapon.Get() == SelectedWeapon;
		});
		if (SelectedIndex != INDEX_NONE) CurrentPageIndex = SelectedIndex / WeaponPageSize;
	}
	CurrentPageIndex = FMath::Clamp(CurrentPageIndex, 0, PageCount - 1);
	if (WeaponListView)
	{
		const int32 FirstIndex = CurrentPageIndex * WeaponPageSize;
		const int32 LastIndex = FMath::Min(FirstIndex + WeaponPageSize, FilteredItems.Num());
		for (int32 Index = FirstIndex; Index < LastIndex; ++Index)
		{
			UMSWeaponCatalogEntryWidget* Entry = WidgetTree->ConstructWidget<UMSWeaponCatalogEntryWidget>();
			Entry->SetWeapon(FilteredItems[Index], this);
			Entry->SetSelected(FilteredItems[Index].Get() == SelectedWeapon);
			if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(WeaponListView->AddChild(Entry)))
			{
				EntrySlot->SetHorizontalAlignment(HAlign_Fill);
				EntrySlot->SetVerticalAlignment(VAlign_Top);
				EntrySlot->SetPadding(FMargin(0, 1, 0, 2));
			}
		}
	}
	if (PageText) PageText->SetText(FText::Format(FText::FromString(TEXT("{0} / {1}")), CurrentPageIndex + 1, PageCount));
	if (PreviousPageButton) PreviousPageButton->SetIsEnabled(CurrentPageIndex > 0);
	if (NextPageButton) NextPageButton->SetIsEnabled(CurrentPageIndex + 1 < PageCount);
	if (WeaponListView) WeaponListView->ScrollToStart();
}

FMSDeckLoadout UMSHangarWidget::GetWorkingLoadout() const
{
	if (bUsingDeckLoadout || bReadOnlyLoadout)
	{
		return WorkingLoadout;
	}
	FMSDeckLoadout Result;
	if (ActivePreset)
	{
		Result = BuildCompatibilityLoadout(ActivePreset);
	}
	return Result;
}

UMSWeaponDataBase* UMSHangarWidget::GetWorkingWeapon(const int32 SlotIndex) const
{
	const FMSDeckLoadout Loadout = GetWorkingLoadout();
	return Loadout.EquippedWeapons.IsValidIndex(SlotIndex) ? Loadout.EquippedWeapons[SlotIndex].LoadSynchronous() : nullptr;
}

bool UMSHangarWidget::IsNativeShieldSupported() const
{
	return ActivePreset && ActivePreset->GetNativeShield() != nullptr;
}

bool UMSHangarWidget::IsNativeShieldEquipped() const
{
	return IsNativeShieldSupported() && GetWorkingLoadout().bShieldEquipped;
}

void UMSHangarWidget::RefreshNativeShield()
{
	const UMSShieldWeaponData* Shield = ActivePreset ? ActivePreset->GetNativeShield() : nullptr;
	const bool bSupported = Shield != nullptr;
	const bool bEquipped = bSupported && GetWorkingLoadout().bShieldEquipped;
	const bool bRemovable = bSupported && ActivePreset->bShieldRemovable;
	if (NativeShieldStatusText)
	{
		FString Status = TEXT("盾なし");
		if (bSupported)
		{
			const FString State = !bRemovable ? TEXT("固定装備") : (bEquipped ? TEXT("装着中") : TEXT("未装備"));
			Status = FString::Printf(TEXT("%s | %s"), *State, *Shield->WeaponName.ToString());
		}
		NativeShieldStatusText->SetText(FText::FromString(Status));
		NativeShieldStatusText->SetColorAndOpacity(FSlateColor(bSupported ? MSPixel::Hex(0x78F3EE) : MSPixel::Hex(0x839095)));
	}
	if (NativeShieldDurabilityText)
	{
		NativeShieldDurabilityText->SetText(bSupported
			? FText::FromString(FString::Printf(TEXT("DURABILITY %d"), FMath::RoundToInt(Shield->MaxDurability)))
			: FText::FromString(TEXT("DURABILITY ---")));
	}
	if (NativeShieldToggleButton)
	{
		const bool bEnabled = bSupported && bRemovable && !bReadOnlyLoadout && !bOnlineSavePending;
		NativeShieldToggleButton->SetIsEnabled(bEnabled);
		if (NativeShieldToggleText)
		{
			NativeShieldToggleText->SetText(
				bSupported && bEquipped ? FText::FromString(TEXT("シールドを外す"))
				: bSupported ? FText::FromString(TEXT("シールドを装着する"))
				: FText::FromString(TEXT("シールド切替")));
		}
		NativeShieldToggleButton->SetToolTipText(
			!bSupported ? FText::FromString(TEXT("この機体は固有シールドに対応していません。"))
			: !bRemovable ? FText::FromString(TEXT("この機体のシールドは固定装備です。"))
			: bReadOnlyLoadout ? FText::FromString(TEXT("出撃中の編成は変更できません。"))
			: FText::GetEmpty());
	}
}

bool UMSHangarWidget::CommitWorkingLoadout(const FMSDeckLoadout& Candidate)
{
	if (bReadOnlyLoadout) return false;
	if (!ActivePreset || Candidate.EquippedWeapons.Num() != UMSMobileSuitData::MaxWeaponSlots) return false;
	if (bOnlineSavePending)
	{
		if (SaveStatusText) SaveStatusText->SetText(FText::FromString(TEXT("保存処理が完了するまでお待ちください。")));
		return false;
	}
	if (bUsingDeckLoadout)
	{
		if (!DeckSubsystem || ActiveDeckSlotIndex < 0 || ActiveDeckSlotIndex >= UMSDeckSubsystem::SlotCount)
		{
			ShowSaveFailureMessage();
			return false;
		}

		UMSOnlineAccountSubsystem* Online = OnlineAccountSubsystem.Get();
		const bool bOnline = Online && Online->IsOnlineMode() && Online->HasOnlineSession();
		if (bOnline)
		{
			PendingLoadout = Candidate;
			PendingDeckSlotIndex = ActiveDeckSlotIndex;
			PendingAccountGeneration = Online->GetAuthGeneration();
			PendingSaveFailureMessage.Reset();
			bOnlineSaveStateObserved = false;
			SetSavePendingVisual(true);
			const bool bCommitted = DeckSubsystem->CommitLoadout(ActiveDeckSlotIndex, Candidate);
			if (!bCommitted && !bOnlineSaveStateObserved)
			{
				bOnlineSavePending = false;
				PendingDeckSlotIndex = INDEX_NONE;
				PendingLoadout = FMSDeckLoadout();
				SetSavePendingVisual(false);
				ShowSaveFailureMessage();
				return false;
			}
			// A pending HTTP operation keeps the current committed preview visible.
			// The sync handler applies Candidate only after the server commits it.
			return bCommitted;
		}

		if (!DeckSubsystem->CommitLoadout(ActiveDeckSlotIndex, Candidate))
		{
			ShowSaveFailureMessage();
			return false;
		}
		WorkingLoadout = Candidate;
		return true;
	}

	// Equipment is owned by the deck slot. A standalone preset is read-only.
	return false;
}

FMSDeckLoadout UMSHangarWidget::BuildSelectedWeaponLoadout() const
{
	FMSDeckLoadout Candidate = GetWorkingLoadout();
	if (!SelectedWeapon || !Candidate.EquippedWeapons.IsValidIndex(PickerSlotIndex)) return Candidate;
	if (GetWorkingWeapon(PickerSlotIndex) == SelectedWeapon)
	{
		Candidate.EquippedWeapons[PickerSlotIndex].Reset();
		return Candidate;
	}
	for (int32 Index = 0; Index < Candidate.EquippedWeapons.Num(); ++Index)
		if (Index != PickerSlotIndex && Candidate.EquippedWeapons[Index].LoadSynchronous() == SelectedWeapon) Candidate.EquippedWeapons[Index].Reset();
	Candidate.EquippedWeapons[PickerSlotIndex] = SelectedWeapon;
	return Candidate;
}

void UMSHangarWidget::ShowSaveFailureMessage(const FString& Detail)
{
	if (!SaveStatusText) return;
	SaveStatusText->SetColorAndOpacity(FSlateColor(OverCapacityColor));
	const FString Prefix = Detail.IsEmpty() ? FString(TEXT("保存できませんでした。")) : Detail + TEXT("\n");
	SaveStatusText->SetText(FText::FromString(Prefix + TEXT("装備は保持されています。再試行または再読込してください。")));
	SaveStatusText->SetVisibility(ESlateVisibility::Visible);
}

void UMSHangarWidget::ChooseCatalogWeapon(UMSWeaponDataBase* Weapon)
{
	if (!Weapon || IsShieldWeapon(Weapon) || (!WeaponCatalog.Contains(Weapon) && !IsWeaponAvailableForActiveSuit(Weapon))) return;
	ClearCommittedSaveStatus(SaveStatusText, bOnlineSavePending);
	SelectedWeapon = Weapon; RefreshFilteredWeapons(true); RefreshSelectionPreview();
}

void UMSHangarWidget::BeginPickSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= UMSMobileSuitData::MaxWeaponSlots) return;
	ClearCommittedSaveStatus(SaveStatusText, bOnlineSavePending);
	PickerSlotIndex = SlotIndex;
	CurrentPageIndex = 0;
	SelectedWeapon = GetWorkingWeapon(SlotIndex);
	if (SelectedWeapon && Categories.Contains(SelectedWeapon->CategoryTag)) SelectedCategory = SelectedWeapon->CategoryTag;
	if (PickerTitleText) PickerTitleText->SetText(FText::Format(FText::FromString(TEXT("武器リスト / SLOT{0}")), SlotIndex + 1));
	RefreshLoadout(); RefreshFilteredWeapons(true); RefreshSelectionPreview();
}

void UMSHangarWidget::SelectWeaponForSlot(int32 SlotIndex, UMSWeaponDataBase* Weapon)
{
	if (!ActivePreset || !Weapon || IsShieldWeapon(Weapon) || SlotIndex < 0 || SlotIndex >= UMSMobileSuitData::MaxWeaponSlots) return;
	FMSDeckLoadout Candidate = GetWorkingLoadout();
	for (int32 Index = 0; Index < Candidate.EquippedWeapons.Num(); ++Index)
	{
		if (Index != SlotIndex && Candidate.EquippedWeapons[Index].LoadSynchronous() == Weapon) Candidate.EquippedWeapons[Index].Reset();
	}
	Candidate.EquippedWeapons[SlotIndex] = Weapon;
	if (!CommitWorkingLoadout(Candidate)) return;
	RefreshLoadout(); RefreshFilteredWeapons(); RefreshSelectionPreview();
}

void UMSHangarWidget::ConfirmSelectedWeapon()
{
	if (!ActivePreset || !SelectedWeapon) return;
	const FMSDeckLoadout Candidate = BuildSelectedWeaponLoadout();
	if (CommitWorkingLoadout(Candidate))
	{
		RefreshLoadout(); RefreshFilteredWeapons(); RefreshSelectionPreview();
	}
}

void UMSHangarWidget::ClearSelectedSlot()
{
	if (!ActivePreset) return;
	FMSDeckLoadout Candidate = GetWorkingLoadout();
	if (!Candidate.EquippedWeapons.IsValidIndex(PickerSlotIndex)) return;
	Candidate.EquippedWeapons[PickerSlotIndex].Reset();
	if (!CommitWorkingLoadout(Candidate)) return;
	SelectedWeapon = nullptr;
	RefreshLoadout(); RefreshFilteredWeapons(); RefreshSelectionPreview();
}

void UMSHangarWidget::ToggleNativeShield()
{
	if (!IsNativeShieldSupported() || !ActivePreset->bShieldRemovable || bReadOnlyLoadout || bOnlineSavePending) return;
	FMSDeckLoadout Candidate = GetWorkingLoadout();
	Candidate.bShieldEquipped = !Candidate.bShieldEquipped;
	if (!CommitWorkingLoadout(Candidate)) return;
	RefreshLoadout(); RefreshFilteredWeapons(); RefreshSelectionPreview();
}

void UMSHangarWidget::RefreshLoadout()
{
	CurrentLoad = 0;
	const FMSDeckLoadout Loadout = GetWorkingLoadout();
	if (PreviewRenderer) PreviewRenderer->SetMobileSuit(ActivePreset,
		ActivePreset && ActivePreset->GetNativeShield() && Loadout.bShieldEquipped);
	for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index)
	{
		UMSWeaponDataBase* Weapon = Loadout.EquippedWeapons.IsValidIndex(Index) ? Loadout.EquippedWeapons[Index].LoadSynchronous() : nullptr;
		// Keep old shield assets out of numbered weapon cards even if a deck
		// snapshot reaches this refresh before migration has run.
		if (IsShieldWeapon(Weapon)) Weapon = nullptr;
		if (Weapon) CurrentLoad += Weapon->Weight;
		if (SlotWeaponTexts.IsValidIndex(Index)) SlotWeaponTexts[Index]->SetText(Weapon
			? Weapon->WeaponName
			: FText::FromString(TEXT("-EMPTY-")));
		if (SlotWeaponIcons.IsValidIndex(Index) && SlotWeaponIcons[Index])
		{
			SlotWeaponIcons[Index]->SetVisibility(Weapon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
			if (Weapon) SlotWeaponIcons[Index]->SetWeaponPhoto(Weapon, MSPixel::Hex(0xDAE6E6));
		}
	}
	bOverCapacity = ActivePreset && CurrentLoad > ActivePreset->LoadCapacity;
	for (int32 Index=0; Index<SlotCardBorders.Num(); ++Index)
		ApplySemanticBorder(SlotCardBorders[Index],MSPixel::Hex(Index==PickerSlotIndex ? 0x59560b : 0x353f47),MSPixel::Hex(Index==PickerSlotIndex ? 0xe6ec36 : 0x8f9c9c));
	RefreshNativeShield();
}

bool UMSHangarWidget::IsWeaponAvailableForActiveSuit(const UMSWeaponDataBase* Weapon) const
{
	if (!Weapon || IsShieldWeapon(Weapon)) return false;
	// When no initial weapon categories exist (test/empty preset), the catalog remains usable as a fallback.
	if (ActiveSuitCategories.IsEmpty()) return true;
	if (ActiveSuitCategories.Contains(Weapon->CategoryTag)) return true;
	return GetEquippedSlot(Weapon) != INDEX_NONE;
}

int32 UMSHangarWidget::GetEquippedSlot(const UMSWeaponDataBase* Weapon) const
{
	if (!ActivePreset || !Weapon) return INDEX_NONE;
	const FMSDeckLoadout Loadout = GetWorkingLoadout();
	for (int32 Index = 0; Index < Loadout.EquippedWeapons.Num(); ++Index)
		if (Loadout.EquippedWeapons[Index].Get() == Weapon || Loadout.EquippedWeapons[Index].LoadSynchronous() == Weapon) return Index;
	return INDEX_NONE;
}

void UMSHangarWidget::RefreshSelectionPreview()
{
	UMSWeaponDataBase* Equipped = GetWorkingWeapon(PickerSlotIndex);
	PreviewLoad = CurrentLoad;
	if (SelectedWeapon && !bReadOnlyLoadout)
	{
		// Use the same candidate as ConfirmSelectedWeapon. In particular, selecting
		// the weapon already in this slot previews an equipment removal.
		const FMSDeckLoadout PreviewLoadout = BuildSelectedWeaponLoadout();
		PreviewLoad = 0;
		for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : PreviewLoadout.EquippedWeapons)
			if (UMSWeaponDataBase* Weapon = WeaponRef.LoadSynchronous()) PreviewLoad += Weapon->Weight;
	}
	// Keep the warning while the committed load is already over the suit's
	// capacity.  Selecting the currently equipped weapon previews its removal,
	// so using PreviewLoad alone would hide an existing over-capacity condition
	// immediately after the equip confirmation.
	const bool bCurrentOverCapacity = ActivePreset && CurrentLoad > ActivePreset->LoadCapacity;
	const bool bPreviewOverCapacity = ActivePreset && PreviewLoad > ActivePreset->LoadCapacity;
	bOverCapacity = bCurrentOverCapacity || bPreviewOverCapacity;
	if (LoadText)
	{
		LoadText->SetText(FText::FromString(FString::Printf(TEXT("積載量 %d → %d / %d"), CurrentLoad, PreviewLoad, ActivePreset ? ActivePreset->LoadCapacity : 0)));
		LoadText->SetColorAndOpacity(FSlateColor(bOverCapacity ? OverCapacityColor : NormalLoadColor));
	}
	if (LoadBar)
	{
		LoadBar->SetPercent(ActivePreset && ActivePreset->LoadCapacity > 0 ? FMath::Clamp(static_cast<float>(PreviewLoad) / ActivePreset->LoadCapacity, 0.0f, 1.0f) : 0.0f);
		LoadBar->SetFillColorAndOpacity(bOverCapacity ? OverCapacityColor : AccentColor);
	}
	auto ValuesFor = [](const UMSWeaponDataBase* Weapon)
	{
		const UMSRangedWeaponData* Ranged=Cast<UMSRangedWeaponData>(Weapon);
		return TArray<float>{Weapon ? float(Weapon->GetDisplayPower()) : 0.f, float(ReloadPerformance(Weapon)),
			Weapon ? float(Weapon->DownPoint) : 0.f, Weapon ? Weapon->GetDisplayLockOnRangeMeters() : 0.f,
			Weapon ? Weapon->GetDisplayRangeMeters() : 0.f, Ranged ? Ranged->SplashRadius : 0.f};
	};
	const TArray<float> OldValues=ValuesFor(Equipped);
	const TCHAR* MetricLabels[]={TEXT("威力"),TEXT("リロード"),TEXT("DP"),TEXT("ロック距離 (m)"),TEXT("射程距離 (m)"),TEXT("効果範囲 (m)")};
	auto SetMetrics = [&](const UMSWeaponDataBase* Weapon, const TArray<TObjectPtr<UTextBlock>>& Texts,
		const TArray<TObjectPtr<UProgressBar>>& Bars, bool bCandidate)
	{
		const TArray<float> Values=ValuesFor(Weapon);
			const float Maxima[]={50000.f,1000.f,100.f,1000.f*MSGO::MSScale,1000.f*MSGO::MSScale,100.f};
		for(int32 Index=0;Index<Texts.Num() && Index<Values.Num();++Index)
		{
			const bool Available=Weapon && (Index!=1 || HasReloadPerformance(Weapon)) && (Index!=5 || Cast<UMSRangedWeaponData>(Weapon));
			FString Value=Available ? FString::Printf(TEXT("%.0f"),Values[Index]) : FString(TEXT("---"));
			const bool Changed=Available && bCandidate && Equipped && !FMath::IsNearlyEqual(Values[Index],OldValues[Index]);
			if(Changed) Value+=FString::Printf(TEXT("  %+.0f"),Values[Index]-OldValues[Index]);
			if(Texts[Index]) { Texts[Index]->SetText(FText::FromString(FString::Printf(TEXT("%s   %s"),MetricLabels[Index],*Value)));
				Texts[Index]->SetColorAndOpacity(Changed ? (Values[Index]>OldValues[Index] ? MSPixel::Hex(0x8ded51) : MSPixel::Hex(0xffa17e)) : NormalLoadColor); }
			if(Bars.IsValidIndex(Index) && Bars[Index]) Bars[Index]->SetPercent(Available ? FMath::Clamp(Values[Index]/Maxima[Index],0.f,1.f) : 0.f);
		}
	};
	SetMetrics(Equipped,EquippedStatTexts,EquippedStatBars,false);SetMetrics(SelectedWeapon,SelectedStatTexts,SelectedStatBars,true);
	const ESlateVisibility SelectedComparisonVisibility = SelectedWeapon ? ESlateVisibility::Visible : ESlateVisibility::Hidden;
	for (const TObjectPtr<UTextBlock>& Text : SelectedStatTexts) if (Text) Text->SetVisibility(SelectedComparisonVisibility);
	for (const TObjectPtr<UProgressBar>& Bar : SelectedStatBars) if (Bar) Bar->SetVisibility(SelectedComparisonVisibility);
	auto SetComparisonIcon = [](UMSWeaponIconWidget* IconWidget, const UMSWeaponDataBase* Weapon, const FLinearColor& IconColor)
	{
		if (!IconWidget) return;
		IconWidget->SetVisibility(Weapon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (Weapon) IconWidget->SetWeaponPhoto(Weapon, IconColor);
	};
	SetComparisonIcon(EquippedComparisonIcon, Equipped, MSPixel::Hex(0xDAE6E6));
	SetComparisonIcon(SelectedComparisonIcon, SelectedWeapon, MSPixel::Hex(0xDAE6E6));
	if (EquippedComparisonText) EquippedComparisonText->SetText(Equipped ? FText::FromString(FString::Printf(TEXT("装備中  %s"), *Equipped->WeaponName.ToString())) : FText::FromString(TEXT("装備中  -EMPTY-")));
	if (SelectedComparisonText) SelectedComparisonText->SetText(SelectedWeapon ? FText::FromString(FString::Printf(TEXT("選択中  %s"), *SelectedWeapon->WeaponName.ToString())) : FText::FromString(TEXT("選択中  ---")));
	if (WeaponParametersText)
	{
		if (!SelectedWeapon) WeaponParametersText->SetText(FText::GetEmpty());
		else
		{
			auto Diff = [Equipped](float Value, float Old, bool bLowerIsBetter = false) { if (!Equipped || FMath::IsNearlyEqual(Value, Old)) return FString(TEXT(" ")); const bool Better = bLowerIsBetter ? Value < Old : Value > Old; return Better ? FString(TEXT("▲")) : FString(TEXT("▼")); };
			const FString ReloadDiff = HasReloadPerformance(Equipped) && HasReloadPerformance(SelectedWeapon)
				? Diff(ReloadPerformance(SelectedWeapon), ReloadPerformance(Equipped)) : FString(TEXT(" "));
			// The cards above already expose each metric with its performance bar.
			// Keep this supplementary summary to one compact line so the fixed 22px
			// slot cannot clip the second and third lines at 1280p.
			WeaponParametersText->SetText(FText::FromString(FString::Printf(TEXT("威力 %d %s  射程 %.0f %s  リロード %s %s  DP %d %s  弾数 %d / 予備 %d"), SelectedWeapon->GetDisplayPower(), *Diff(SelectedWeapon->GetDisplayPower(), Equipped ? Equipped->GetDisplayPower() : 0), SelectedWeapon->GetDisplayRangeMeters(), *Diff(SelectedWeapon->GetDisplayRangeMeters(), Equipped ? Equipped->GetDisplayRangeMeters() : 0), *ReloadPerformanceText(SelectedWeapon), *ReloadDiff, SelectedWeapon->DownPoint, *Diff(SelectedWeapon->DownPoint, Equipped ? Equipped->DownPoint : 0), SelectedWeapon->MagazineSize, SelectedWeapon->ReserveAmmo)));
		}
	}
	const bool bSelectedEquippedHere = SelectedWeapon && SelectedWeapon == Equipped;
	if (EquipButtonText) EquipButtonText->SetText(FText::FromString(bSelectedEquippedHere ? TEXT("装備解除") : TEXT("装備")));
	if (EquipButton) EquipButton->SetIsEnabled(!bReadOnlyLoadout && !bOnlineSavePending && SelectedWeapon != nullptr);
	if (RemoveButton)
	{
		// Selecting the equipped weapon already turns the primary green action into
		// 装備解除; hide the secondary remove button to avoid two identical actions.
		const bool bShowSecondaryRemove = Equipped != nullptr && !bSelectedEquippedHere;
		RemoveButton->SetVisibility(bShowSecondaryRemove ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		RemoveButton->SetIsEnabled(!bReadOnlyLoadout && !bOnlineSavePending && bShowSecondaryRemove);
	}
	if (EmptySlotGuideText) EmptySlotGuideText->SetVisibility(SelectedWeapon ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

UMSWeaponDataBase* UMSHangarWidget::GetFilteredWeapon(int32 Index) const { return FilteredItems.IsValidIndex(Index) ? FilteredItems[Index].Get() : nullptr; }
FSlateColor UMSHangarWidget::GetLoadTextColor() const { return LoadText ? LoadText->GetColorAndOpacity() : FSlateColor(FLinearColor::Transparent); }

#if WITH_DEV_AUTOMATION_TESTS
void UMSHangarWidget::InitializeForAutomationTests(UMSMobileSuitData* Preset)
{
	if (!WeaponListView) NativeOnInitialized(); ActivePreset = Preset;
	ActiveSuitCategories.Reset();
	if (ActivePreset) for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : ActivePreset->EquippedWeapons)
		if (UMSWeaponDataBase* Weapon = WeaponRef.LoadSynchronous(); Weapon && !IsShieldWeapon(Weapon)) ActiveSuitCategories.Add(Weapon->CategoryTag);
	RefreshLoadout(); RefreshFilteredWeapons(); RefreshSelectionPreview();
}
void UMSHangarWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem)
{
	if (DeckSubsystem)
	{
		DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckChanged);
		DeckSubsystem->OnDeckSyncStateChanged.RemoveDynamic(this, &UMSHangarWidget::HandleDeckSyncStateChanged);
	}
	DeckSubsystem = InSubsystem;
	ActiveDeckSlotIndex = INDEX_NONE;
	bUsingDeckLoadout = false;
	if (DeckSubsystem)
	{
		DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleDeckChanged);
		DeckSubsystem->OnDeckSyncStateChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleDeckSyncStateChanged);
	}
}
void UMSHangarWidget::SetOnlineAccountForTesting(UMSOnlineAccountSubsystem* InSubsystem)
{
	if (OnlineAccountSubsystem.IsValid())
	{
		OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineAccountChanged);
		OnlineAccountSubsystem->OnRequestFinished.RemoveDynamic(this, &UMSHangarWidget::HandleOnlineRequestFinished);
	}
	OnlineAccountSubsystem = InSubsystem;
	if (OnlineAccountSubsystem.IsValid())
	{
		OnlineAccountSubsystem->OnAccountChanged.AddUniqueDynamic(this, &UMSHangarWidget::HandleOnlineAccountChanged);
		OnlineAccountSubsystem->OnRequestFinished.AddUniqueDynamic(this, &UMSHangarWidget::HandleOnlineRequestFinished);
	}
}
void UMSHangarWidget::BeginOnlineSaveForAutomationTests(const FMSDeckLoadout& Candidate, const int32 DeckSlotIndex, const int64 AccountGeneration)
{
	PendingLoadout = Candidate;
	PendingDeckSlotIndex = DeckSlotIndex;
	PendingAccountGeneration = AccountGeneration;
	PendingSaveFailureMessage.Reset();
	bOnlineSaveStateObserved = false;
	SetSavePendingVisual(true);
}
FText UMSHangarWidget::GetWeaponParametersTextForTesting() const{return WeaponParametersText?WeaponParametersText->GetText():FText::GetEmpty();}
float UMSHangarWidget::GetReloadBarPercentForTesting(bool bSelected) const
{
	const TArray<TObjectPtr<UProgressBar>>& Bars = bSelected ? SelectedStatBars : EquippedStatBars;
	return Bars.IsValidIndex(1) && Bars[1] ? Bars[1]->GetPercent() : 0.0f;
}
float UMSHangarWidget::GetPowerBarPercentForTesting(bool bSelected) const
{
	const TArray<TObjectPtr<UProgressBar>>& Bars = bSelected ? SelectedStatBars : EquippedStatBars;
	return Bars.IsValidIndex(0) && Bars[0] ? Bars[0]->GetPercent() : 0.0f;
}
FText UMSHangarWidget::GetEquippedComparisonTextForTesting() const
{
	if (!EquippedComparisonText) return FText::GetEmpty();
	FString Result = EquippedComparisonText->GetText().ToString();
	for (const TObjectPtr<UTextBlock>& StatText : EquippedStatTexts)
		if (StatText)
		{
			Result += TEXT("\n");
			Result += StatText->GetText().ToString();
		}
	return FText::FromString(Result);
}
FText UMSHangarWidget::GetSaveStatusTextForTesting() const
{
	return SaveStatusText ? SaveStatusText->GetText() : FText::GetEmpty();
}
FSlateColor UMSHangarWidget::GetSaveStatusColorForTesting() const
{
	return SaveStatusText ? SaveStatusText->GetColorAndOpacity() : FSlateColor(FLinearColor::Transparent);
}
FText UMSHangarWidget::GetNativeShieldStatusTextForTesting() const
{
	return NativeShieldStatusText ? NativeShieldStatusText->GetText() : FText::GetEmpty();
}
FText UMSHangarWidget::GetNativeShieldDurabilityTextForTesting() const
{
	return NativeShieldDurabilityText ? NativeShieldDurabilityText->GetText() : FText::GetEmpty();
}
FText UMSHangarWidget::GetNativeShieldToggleTextForTesting() const
{
	return NativeShieldToggleText ? NativeShieldToggleText->GetText() : FText::GetEmpty();
}
bool UMSHangarWidget::IsNativeShieldToggleEnabledForTesting() const
{
	return NativeShieldToggleButton && NativeShieldToggleButton->GetIsEnabled();
}
#endif

void UMSHangarWidget::HandleCategory0Clicked() { SelectCategory(Categories[0]); }
void UMSHangarWidget::HandleCategory1Clicked() { SelectCategory(Categories[1]); }
void UMSHangarWidget::HandleCategory2Clicked() { SelectCategory(Categories[2]); }
void UMSHangarWidget::HandleCategory3Clicked() { SelectCategory(Categories[3]); }
void UMSHangarWidget::HandleCategory4Clicked() { SelectCategory(Categories[4]); }
void UMSHangarWidget::HandleCategory5Clicked() { SelectCategory(Categories[5]); }
void UMSHangarWidget::HandleCategory6Clicked() { SelectCategory(Categories[6]); }
void UMSHangarWidget::HandleCategory7Clicked() { SelectCategory(Categories[7]); }
void UMSHangarWidget::HandleSlot0Clicked() { BeginPickSlot(0); }
void UMSHangarWidget::HandleSlot1Clicked() { BeginPickSlot(1); }
void UMSHangarWidget::HandleSlot2Clicked() { BeginPickSlot(2); }
void UMSHangarWidget::HandlePreviousPageClicked()
{
	if (CurrentPageIndex <= 0) return;
	ClearCommittedSaveStatus(SaveStatusText, bOnlineSavePending);
	--CurrentPageIndex;
	RefreshFilteredWeapons();
}
void UMSHangarWidget::HandleNextPageClicked()
{
	if (CurrentPageIndex + 1 >= PageCount) return;
	ClearCommittedSaveStatus(SaveStatusText, bOnlineSavePending);
	++CurrentPageIndex;
	RefreshFilteredWeapons();
}
void UMSHangarWidget::HandleEquipClicked() { ConfirmSelectedWeapon(); }
void UMSHangarWidget::HandleRemoveClicked() { ClearSelectedSlot(); }
void UMSHangarWidget::HandleNativeShieldClicked() { ToggleNativeShield(); }
void UMSHangarWidget::HandleCloseClicked() { Close(); }
