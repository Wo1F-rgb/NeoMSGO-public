#include "UI/MSSuitSwapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/AssetManager.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "InputCoreTypes.h"
#include "Data/MSWeaponData.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarChrome.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSPixelUI.h"
#include "UI/MSPixelPainter.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"

namespace MSSuitSwap
{
	const FLinearColor BackgroundColor(0.003f, 0.012f, 0.02f, 0.95f);
	const FLinearColor PanelColor(0.02f, 0.085f, 0.105f, 0.98f);
	const FLinearColor AccentColor(0.18f, 0.9f, 0.84f, 1.0f);
	const FLinearColor WarningColor(1.0f, 0.12f, 0.08f, 1.0f);

	FString TypeLabel(const EMSMobileSuitUnitType Type)
	{
		switch (Type) { case EMSMobileSuitUnitType::Assault: return TEXT("強襲"); case EMSMobileSuitUnitType::Heavy: return TEXT("重撃"); case EMSMobileSuitUnitType::Support: return TEXT("支援"); case EMSMobileSuitUnitType::Bombardment: return TEXT("砲撃"); default: return TEXT("狙撃"); }
	}

	FString Stats(const UMSMobileSuitData* Preset)
	{
		if (!Preset) return TEXT("---");
		const float ReuseDelay = Preset->GetReuseDelaySeconds();
		const FString Time = ReuseDelay > 0.0f ? FString::FromInt(FMath::RoundToInt(ReuseDelay)) : TEXT("---");
		return FString::Printf(TEXT("COST %d  TIME %s\n%s%s\n\nアーマー  %d\n最高速度  %.1f m/s\nブースト容量  %.0f\nブーストチャージ  %.1f\n積載量  %d\n緊急帰投性能  %d"),
			Preset->Cost, *Time, *TypeLabel(Preset->UnitType), Preset->bAmphibious ? TEXT(" / 水中対応") : TEXT(" / 地上"), Preset->MaxArmor,
			Preset->BoostSpeed, Preset->BoostCapacity, Preset->BoostRechargeRate, Preset->LoadCapacity, Preset->EmergencyRecallPerformance);
	}

	FString StatsWithLoadout(const UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout)
	{
		FString Result = Stats(Preset);
		if (!Preset) return Result;
		Result += TEXT("\n\n装備武器");
		for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index)
		{
			const UMSWeaponDataBase* Weapon = Loadout.EquippedWeapons.IsValidIndex(Index) ? Loadout.EquippedWeapons[Index].LoadSynchronous() : nullptr;
			const bool bShieldWeapon = Weapon && Cast<UMSShieldWeaponData>(Weapon);
			Result += FString::Printf(TEXT("\nSLOT%d  %s"), Index + 1, Weapon && !bShieldWeapon ? *Weapon->WeaponName.ToString() : TEXT("---"));
		}
		return Result;
	}

	FString ShieldSummary(const UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout)
	{
		if (!Preset) return TEXT("SHIELD  ---");
		const UMSShieldWeaponData* Shield = Preset->GetNativeShield();
		if (!Shield) return TEXT("SHIELD  盾なし");
		return FString::Printf(TEXT("SHIELD  %s  %s  DUR %d"), *Shield->WeaponName.ToString(),
			!Preset->bShieldRemovable ? TEXT("固定") : (Loadout.bShieldEquipped ? TEXT("装備中") : TEXT("未装備")), FMath::RoundToInt(Shield->MaxDurability));
	}
}

namespace
{
	const FVector2D SwapCanvasSize(1920.0f, 1080.0f);

	void PlaceOnSwapCanvas(UCanvasPanel* Canvas, UWidget* Child, const float X, const float Y,
		const float Width, const float Height)
	{
		if (!Canvas || !Child) return;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f)); Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(FVector2D(X, Y)); Slot->SetSize(FVector2D(Width, Height));
	}

	UTextBlock* SwapText(UWidgetTree* Tree, const FText& Value, const float PixelHeight,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		return MSPixelUI::Text(Tree, Value, PixelHeight, Color, Name);
	}

	UButton* SwapButton(UWidgetTree* Tree, const FText& Label, const FName Name, const bool bEnabled = true,
		const int32 Variant = 1, const float PixelHeight = 14.0f,
		const FLinearColor& TextColor = FLinearColor::FromSRGBColor(FColor(0x25,0x34,0x5A)))
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name); MSPixelUI::ClearButtonBrush(Button);
		UTextBlock* Text = SwapText(Tree, Label, PixelHeight, TextColor); Text->SetJustification(ETextJustify::Center);
		if (Variant == 0) Text->SetColorAndOpacity(MSPixel::Hex(0xE8F0F5));
		UScaleBox* LabelFit = Tree->ConstructWidget<UScaleBox>(); LabelFit->SetStretch(EStretch::ScaleToFit); LabelFit->SetStretchDirection(EStretchDirection::DownOnly); LabelFit->SetContent(Text);
		Button->SetContent(MSPixelUI::Frame(Tree, LabelFit, EMSVectorSurface::Button, FMargin(6, 2), Variant)); MSUIStyle::StretchButtonContent(Button); if (Variant == 1) Text->SetShadowOffset(FVector2D::ZeroVector); Button->SetIsEnabled(bEnabled); return Button;
	}

	UOverlay* SwapPanel(UWidgetTree* Tree, UWidget* Content, const EMSVectorSurface Surface = EMSVectorSurface::Panel,
		const FMargin Padding = FMargin(10))
	{
		return MSPixelUI::Frame(Tree, Content, Surface, Padding);
	}
}

void UMSSuitSwapEntryWidget::NativeOnInitialized()
{
	if (WidgetTree->RootWidget) return;
	Super::NativeOnInitialized();
	EntryButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SuitSwapEntryButton")); USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(); RowSize->SetHeightOverride(41); RowSize->SetContent(EntryButton); WidgetTree->RootWidget = RowSize;
	MSPixelUI::ClearButtonBrush(EntryButton);
	LabelText = SwapText(WidgetTree, FText::FromString(TEXT("---")), 16.0f, FLinearColor(0.72f, 0.86f, 0.88f)); LabelText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	EntryButton->SetContent(MSPixelUI::Frame(WidgetTree, LabelText, EMSVectorSurface::Panel, FMargin(10, 2)));
	MSUIStyle::StretchButtonContent(EntryButton);
	EntryButton->OnClicked.AddDynamic(this, &UMSSuitSwapEntryWidget::HandleClicked);
}

void UMSSuitSwapEntryWidget::SetEntry(UMSMobileSuitData* InPreset, UMSSuitSwapWidget* InOwner)
{
	Preset = InPreset; Owner = InOwner;
	// WidgetTree::ConstructWidget creates the row UObject but does not initialize its nested tree in headless/runtime-built lists.
	if (!WidgetTree->RootWidget) NativeOnInitialized();
	if (LabelText && Preset) LabelText->SetText(FText::FromString(FString::Printf(TEXT("%s\nCOST %d     %s%s"), *Preset->UnitName.ToString(), Preset->Cost,
		*MSSuitSwap::TypeLabel(Preset->UnitType), Preset->bAmphibious ? TEXT("     水中") : TEXT(""))));
}
void UMSSuitSwapEntryWidget::HandleClicked() { if (Owner && Preset) Owner->SelectCandidate(Preset); }

void UMSSuitSwapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSSuitSwapWidget::BuildReferenceLayout()
{
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("SuitSwapScaleToFit")); Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both);
	USizeBox* DesignSize = WidgetTree->ConstructWidget<USizeBox>(); DesignSize->SetWidthOverride(SwapCanvasSize.X); DesignSize->SetHeightOverride(SwapCanvasSize.Y);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SuitSwapDesignCanvas")); DesignSize->SetContent(Canvas); Fit->SetContent(DesignSize); WidgetTree->RootWidget = Fit;
	PlaceOnSwapCanvas(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::HangarBackdrop, TEXT("SuitSwapHangarBackdrop")), 0, 0, 1920, 1080);

	HeaderIdentityText = MSHangarChrome::Build(this, Canvas, FText::FromString(TEXT("格納庫")));
	UButton* Back = SwapButton(WidgetTree, FText::FromString(TEXT("戻る")), TEXT("SuitSwapBackButton"), true, 0, 18); Back->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleBackClicked); PlaceOnSwapCanvas(Canvas, Back, 1352, 225, 114, 30);
	// Full-screen list/comparison canvases are added below; keep this shared exit above them for pointer input.
	CastChecked<UCanvasPanelSlot>(Back->Slot)->SetZOrder(10);

	ListPhase = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SuitSwapListPhase"));
	ListCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(); ListPhase->AddChildToVerticalBox(ListCanvas)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PlaceOnSwapCanvas(Canvas, ListPhase, 0, 0, 1920, 1080);
	UCanvasPanel* ModalCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	CatalogFrame = SwapPanel(WidgetTree, ModalCanvas, EMSVectorSurface::Panel, FMargin(0));
	PlaceOnSwapCanvas(ListCanvas, CatalogFrame, 177, 230, 438, 507);
	ListTitleText = SwapText(WidgetTree, FText::FromString(TEXT("機体入れ替え")), 18, FLinearColor::White, TEXT("SuitSwapTitleText")); ListTitleText->SetJustification(ETextJustify::Center); PlaceOnSwapCanvas(ModalCanvas, ListTitleText, 131, 0, 179, 30);
	SearchInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SuitSearchInput")); SearchInput->SetHintText(FText::FromString(TEXT("機体名を入力して検索")));
	SearchFrame = SwapPanel(WidgetTree, SearchInput, EMSVectorSurface::Panel, FMargin(5, 1));
	PlaceOnSwapCanvas(ModalCanvas, SearchFrame, 17, 33, 284, 29);
	FEditableTextBoxStyle SearchStyle = SearchInput->GetWidgetStyle();
	SearchStyle.TextStyle.Font.Size = 12;
	SearchStyle.ForegroundColor = MSPixel::Hex(0xE8F0F5);
	SearchStyle.FocusedForegroundColor = MSPixel::Hex(0xFFFFFF);
	SearchStyle.BackgroundImageNormal.DrawAs = ESlateBrushDrawType::NoDrawType;
	SearchStyle.BackgroundImageHovered.DrawAs = ESlateBrushDrawType::NoDrawType;
	SearchStyle.BackgroundImageFocused.DrawAs = ESlateBrushDrawType::NoDrawType;
	SearchStyle.BackgroundImageReadOnly.DrawAs = ESlateBrushDrawType::NoDrawType;
	SearchStyle.Padding = FMargin(3, 1);
	SearchInput->SetWidgetStyle(SearchStyle);
	UButton* Search = SwapButton(WidgetTree, FText::FromString(TEXT("検索")), TEXT("SuitSearchButton"), true, 1); Search->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleSearchClicked); PlaceOnSwapCanvas(ModalCanvas, Search, 304, 39, 50, 22);
	UButton* Clear = SwapButton(WidgetTree, FText::FromString(TEXT("クリア")), TEXT("SuitSearchClearButton"), true, 1); Clear->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleClearClicked); PlaceOnSwapCanvas(ModalCanvas, Clear, 358, 39, 51, 22);
	UButton* Sort = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CostSortButton")); MSPixelUI::ClearButtonBrush(Sort);
	SortText = SwapText(WidgetTree, FText::FromString(TEXT("COST：高い順")), 12, FLinearColor::White, TEXT("CostSortText")); SortText->SetJustification(ETextJustify::Center);
	Sort->SetContent(MSPixelUI::Frame(WidgetTree, SortText, EMSVectorSurface::Button, FMargin(5, 2), 0)); MSUIStyle::StretchButtonContent(Sort); Sort->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleSortClicked); PlaceOnSwapCanvas(ModalCanvas, Sort, 23, 64, 167, 23);
	const TCHAR* TabLabels[] = { TEXT("オール"), TEXT("強襲"), TEXT("重撃"), TEXT("支援"), TEXT("砲撃"), TEXT("狙撃") };
	const FName TabNames[] = { TEXT("CategoryAllButton"), TEXT("CategoryAssaultButton"), TEXT("CategoryHeavyButton"), TEXT("CategorySupportButton"), TEXT("CategoryBombardmentButton"), TEXT("CategorySniperButton") };
	const float TabX = 29.0f; const float TabWidth = 381.0f / UE_ARRAY_COUNT(TabLabels);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabLabels); ++Index)
	{
		UButton* Tab = SwapButton(WidgetTree, FText::FromString(TabLabels[Index]), TabNames[Index], true, 0, 15);
		PlaceOnSwapCanvas(ModalCanvas, Tab, TabX + Index * TabWidth, 92, TabWidth, 23);
	}
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategoryAllButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategoryAllClicked);
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategoryAssaultButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategoryAssaultClicked);
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategoryHeavyButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategoryHeavyClicked);
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategorySupportButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategorySupportClicked);
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategoryBombardmentButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategoryBombardmentClicked);
	CastChecked<UButton>(WidgetTree->FindWidget(TEXT("CategorySniperButton")))->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleCategorySniperClicked);
	CatalogScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SuitCatalogScrollBox")); CatalogScrollBox->SetScrollBarVisibility(ESlateVisibility::Collapsed); PlaceOnSwapCanvas(ModalCanvas, CatalogScrollBox, 29, 116, 381, 333);
	UHorizontalBox* Pager = WidgetTree->ConstructWidget<UHorizontalBox>();
	PreviousPageButton = SwapButton(WidgetTree, FText::FromString(TEXT("‹")), TEXT("SuitCatalogPreviousButton"), false, 0, 12); PreviousPageButton->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandlePreviousPageClicked); Pager->AddChildToHorizontalBox(PreviousPageButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PageNumberText = SwapText(WidgetTree, FText::GetEmpty(), 16, FLinearColor::White, TEXT("SuitCatalogPageText")); PageNumberText->SetJustification(ETextJustify::Center); Pager->AddChildToHorizontalBox(PageNumberText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NextPageButton = SwapButton(WidgetTree, FText::FromString(TEXT("›")), TEXT("SuitCatalogNextButton"), false, 0, 12); NextPageButton->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleNextPageClicked); Pager->AddChildToHorizontalBox(NextPageButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PlaceOnSwapCanvas(ModalCanvas, SwapPanel(WidgetTree, Pager, EMSVectorSurface::Panel, FMargin(5, 2)), 29, 449, 381, 34);

	ListPreviewImage = WidgetTree->ConstructWidget<UImage>(); ListPreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	UScaleBox* ListImageFit = WidgetTree->ConstructWidget<UScaleBox>(); ListImageFit->SetStretch(EStretch::ScaleToFit); ListImageFit->SetContent(ListPreviewImage); ListImageFit->SetVisibility(ESlateVisibility::HitTestInvisible); PlaceOnSwapCanvas(ListCanvas, ListImageFit, 700, 105, 510, 765);
	ListCurrentSummaryText = SwapText(WidgetTree, FText::FromString(TEXT("現行機体\n---")), 13, FLinearColor(0.72f, 0.94f, 0.96f), TEXT("ListCurrentSummaryText")); ListCurrentSummaryText->SetAutoWrapText(true);
	ListCurrentSummaryFrame = SwapPanel(WidgetTree, ListCurrentSummaryText, EMSVectorSurface::Panel, FMargin(10, 9));
	PlaceOnSwapCanvas(ListCanvas, ListCurrentSummaryFrame, 1227, 282, 228, 426);
	DevelopmentInstructionText = SwapText(WidgetTree, FText::FromString(TEXT("左のリストから設計図を選択してください。")), 15, FLinearColor(0.86f, 0.96f, 0.94f), TEXT("DevelopmentInstructionText"));
	DevelopmentInstructionText->SetJustification(ETextJustify::Center); DevelopmentInstructionText->SetAutoWrapText(true);
	PlaceOnSwapCanvas(ListCanvas, DevelopmentInstructionText, 1030, 309, 380, 48);
	DevelopmentEmptyStateText = SwapText(WidgetTree, FText::FromString(TEXT("機体開発は準備中です")), 14, FLinearColor(0.52f, 0.72f, 0.72f), TEXT("DevelopmentEmptyStateText"));
	DevelopmentEmptyStateText->SetJustification(ETextJustify::Center); DevelopmentEmptyStateText->SetAutoWrapText(true);
	PlaceOnSwapCanvas(ListCanvas, DevelopmentEmptyStateText, 475, 410, 377, 70);

	ComparisonPhase = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SuitSwapComparisonPhase"));
	UCanvasPanel* CompareCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(); ComparisonPhase->AddChildToVerticalBox(CompareCanvas)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PlaceOnSwapCanvas(Canvas, ComparisonPhase, 0, 0, 1920, 1080);
	auto MakeSuitPanel = [this, CompareCanvas](const bool bCandidate, const float X, const float Y, const float Width, const float Height)
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		UOverlay* Frame = SwapPanel(WidgetTree, Column, EMSVectorSurface::Panel, FMargin(12)); PlaceOnSwapCanvas(CompareCanvas, Frame, X, Y, Width, Height);
		UTextBlock* Name = SwapText(WidgetTree, FText::FromString(TEXT("---")), 17, FLinearColor::White, bCandidate ? TEXT("CandidateSuitNameText") : TEXT("CurrentSuitNameText")); Name->SetAutoWrapText(true); Column->AddChildToVerticalBox(Name);
		UImage* Preview = WidgetTree->ConstructWidget<UImage>();  USizeBox* ImageSize = WidgetTree->ConstructWidget<USizeBox>(); ImageSize->SetHeightOverride(bCandidate ? 220 : 140); UScaleBox* ImageFit = WidgetTree->ConstructWidget<UScaleBox>(); ImageFit->SetStretch(EStretch::ScaleToFit); ImageFit->SetContent(Preview); ImageSize->SetContent(ImageFit); Column->AddChildToVerticalBox(ImageSize);
		UTextBlock* StatsText = SwapText(WidgetTree, FText::FromString(TEXT("---")), 11, FLinearColor(0.75f, 0.95f, 0.96f), bCandidate ? TEXT("CandidateSuitStatsText") : TEXT("CurrentSuitStatsText")); StatsText->SetAutoWrapText(true); Column->AddChildToVerticalBox(StatsText);
		UTextBlock* ShieldText = SwapText(WidgetTree, FText::FromString(TEXT("SHIELD  ---")), 12, MSSuitSwap::AccentColor, bCandidate ? TEXT("CandidateSuitShieldText") : TEXT("CurrentSuitShieldText")); ShieldText->SetAutoWrapText(false); Column->AddChildToVerticalBox(ShieldText);
		if (bCandidate) { CandidateNameText = Name; CandidateStatsText = StatsText; CandidateShieldText = ShieldText; CandidatePreviewImage = Preview; }
		else { CurrentNameText = Name; CurrentStatsText = StatsText; CurrentShieldText = ShieldText; CurrentPreviewImage = Preview; }
	};
	MakeSuitPanel(true, 466, 230, 438, 500);
	MakeSuitPanel(false, 1227, 282, 228, 426);
	CostAfterText = SwapText(WidgetTree, FText::FromString(TEXT("デッキコスト\n---/---")), 16, FLinearColor::White, TEXT("DeckCostAfterSwapText")); CostAfterText->SetJustification(ETextJustify::Center); PlaceOnSwapCanvas(CompareCanvas, SwapPanel(WidgetTree, CostAfterText, EMSVectorSurface::GoldPanel, FMargin(8, 7)), 895, 651, 178, 61);
	ConfirmButton = SwapButton(WidgetTree, FText::FromString(TEXT("入れ替え")), TEXT("ConfirmSuitSwapButton"), false, 1, 15); ConfirmButton->OnClicked.AddDynamic(this, &UMSSuitSwapWidget::HandleConfirmClicked); PlaceOnSwapCanvas(CompareCanvas, ConfirmButton, 895, 720, 178, 42);
	if (GetWorld())
	{
		ListPreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this); ListPreviewRenderer->Initialize(GetWorld(), ListPreviewImage); ListPreviewRenderer->ConfigureCustomizeView();
		CandidatePreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this); CandidatePreviewRenderer->Initialize(GetWorld(), CandidatePreviewImage); CandidatePreviewRenderer->ConfigureCustomizeView();
		CurrentPreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this); CurrentPreviewRenderer->Initialize(GetWorld(), CurrentPreviewImage); CurrentPreviewRenderer->ConfigureCustomizeView();
	}
	ShowListPhase();
}

void UMSSuitSwapWidget::Open(const int32 InSlotIndex)
{
	OpenScreen(EMSSuitSwapScreenMode::Swap, InSlotIndex);
}

void UMSSuitSwapWidget::OpenOwnedList()
{
	OpenScreen(EMSSuitSwapScreenMode::OwnedList, SlotIndex);
}

void UMSSuitSwapWidget::OpenDevelopment()
{
	OpenScreen(EMSSuitSwapScreenMode::Development, SlotIndex);
}

void UMSSuitSwapWidget::OpenScreen(const EMSSuitSwapScreenMode InMode, const int32 InSlotIndex)
{
	DeckSubsystem = ResolveDeckSubsystem();
	if (InMode == EMSSuitSwapScreenMode::Swap)
	{
		if (!DeckSubsystem || InSlotIndex < 0 || InSlotIndex >= UMSDeckSubsystem::SlotCount) return;
		SlotIndex = InSlotIndex;
	}

	ScreenMode = InMode;
	CandidatePreset = nullptr;
	bComparisonPhase = false;
	PageIndex = 0;
	SelectedCategoryIndex = 0;
	bCostDescending = true;
	if (SearchInput) SearchInput->SetText(FText::GetEmpty());
	if (SortText) SortText->SetText(FText::FromString(TEXT("COST：高い順")));
	if (!bCatalogInjectedForTests && InMode != EMSSuitSwapScreenMode::Development) LoadCatalog();
	if (InMode == EMSSuitSwapScreenMode::Development)
	{
		// Blueprint and currency inventories are not part of the current project data model.
		// Keep this screen empty instead of treating the suit catalog as development data.
		FilteredCatalog.Reset();
	}
	MSHangarChrome::Refresh(this);
	RefreshModeLayout();
	ApplyFilters();
	RefreshCurrentDetails();
	ShowListPhase();
	if (GetWorld() && !IsInViewport()) AddToViewport(100);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PushScreen(TEXT("SuitSwap"), FMSUICloseRequestDelegate::CreateUObject(this, &UMSSuitSwapWidget::Close));
}

void UMSSuitSwapWidget::RefreshModeLayout()
{
	const bool bSwap = ScreenMode == EMSSuitSwapScreenMode::Swap;
	const bool bOwned = ScreenMode == EMSSuitSwapScreenMode::OwnedList;
	const bool bDevelopment = ScreenMode == EMSSuitSwapScreenMode::Development;
	if(SearchInput) SearchInput->SetIsEnabled(!bDevelopment);
	for(const FName Name : {FName(TEXT("SuitSearchButton")),FName(TEXT("SuitSearchClearButton")),FName(TEXT("CostSortButton")),FName(TEXT("CategoryAllButton")),FName(TEXT("CategoryAssaultButton")),FName(TEXT("CategoryHeavyButton")),FName(TEXT("CategorySupportButton")),FName(TEXT("CategoryBombardmentButton")),FName(TEXT("CategorySniperButton"))})
		if(UWidget* Control=GetWidgetFromName(Name)) Control->SetIsEnabled(!bDevelopment);
	if (UTextBlock* Breadcrumb = Cast<UTextBlock>(GetWidgetFromName(TEXT("HangarBreadcrumbText"))))
		Breadcrumb->SetText(FText::FromString(bSwap ? TEXT("格納庫 > カスタマイズ") : bOwned ? TEXT("格納庫 > 機体リスト") : TEXT("格納庫 > 開発室")));
	if (ListTitleText)
	{
		ListTitleText->SetText(FText::FromString(bSwap ? TEXT("機体入れ替え") : bOwned ? TEXT("所持機体リスト") : TEXT("機体開発")));
	}
	if (DevelopmentInstructionText) DevelopmentInstructionText->SetVisibility(bDevelopment ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (DevelopmentEmptyStateText) DevelopmentEmptyStateText->SetVisibility(bDevelopment ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (ListPreviewImage) ListPreviewImage->SetVisibility(bSwap ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (ListCurrentSummaryText) ListCurrentSummaryText->SetVisibility(bSwap ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (ListCurrentSummaryFrame) ListCurrentSummaryFrame->SetVisibility(bSwap ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (CatalogFrame)
	{
		if (UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(CatalogFrame->Slot))
		{
			if (bSwap) { FrameSlot->SetPosition(FVector2D(177.0f, 230.0f)); FrameSlot->SetSize(FVector2D(438.0f, 507.0f)); }
			else if (bOwned) { FrameSlot->SetPosition(FVector2D(460.0f, 244.0f)); FrameSlot->SetSize(FVector2D(435.0f, 510.0f)); }
			else { FrameSlot->SetPosition(FVector2D(452.0f, 230.0f)); FrameSlot->SetSize(FVector2D(1016.0f, 506.0f)); }
		}
	}
	if (ListTitleText)
	{
		if (UCanvasPanelSlot* TitleSlot = Cast<UCanvasPanelSlot>(ListTitleText->Slot))
		{
			const float TitleWidth = bDevelopment ? 260.0f : 190.0f;
			const float FrameWidth = bDevelopment ? 1016.0f : bOwned ? 435.0f : 438.0f;
			TitleSlot->SetPosition(FVector2D((FrameWidth - TitleWidth) * 0.5f, 0.0f)); TitleSlot->SetSize(FVector2D(TitleWidth, 30.0f));
		}
	}
	if (CatalogScrollBox)
	{
		if (UCanvasPanelSlot* RowsSlot = Cast<UCanvasPanelSlot>(CatalogScrollBox->Slot))
		{
			if (bDevelopment) { RowsSlot->SetPosition(FVector2D(23.0f, 120.0f)); RowsSlot->SetSize(FVector2D(377.0f, 333.0f)); }
			else { RowsSlot->SetPosition(FVector2D(29.0f, 116.0f)); RowsSlot->SetSize(FVector2D(bOwned ? 377.0f : 381.0f, 333.0f)); }
		}
	}
	if (SearchFrame)
	{
		if (UCanvasPanelSlot* SearchSlot = Cast<UCanvasPanelSlot>(SearchFrame->Slot))
			SearchSlot->SetPosition(FVector2D(bDevelopment ? 23.0f : 17.0f, bDevelopment ? 40.0f : 33.0f));
	}
	if (DevelopmentInstructionText)
	{
		if (UCanvasPanelSlot* InstructionSlot = Cast<UCanvasPanelSlot>(DevelopmentInstructionText->Slot))
			InstructionSlot->SetPosition(FVector2D(1030.0f, 309.0f));
	}
	if (DevelopmentEmptyStateText)
	{
		if (UCanvasPanelSlot* EmptySlot = Cast<UCanvasPanelSlot>(DevelopmentEmptyStateText->Slot))
			EmptySlot->SetPosition(FVector2D(475.0f, 410.0f));
	}
}

bool UMSSuitSwapWidget::Close()
{
	if (bComparisonPhase) { ShowListPhase(); return false; }
	if (!IsOpen()) return true; SetVisibility(ESlateVisibility::Collapsed);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TEXT("SuitSwap"));
	if (SuitDetailWidget) SuitDetailWidget->HandleSwapClosed();
	else if (CustomizeWidget) CustomizeWidget->HandleSuitSwapClosed();
	return true;
}

FReply UMSSuitSwapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSSuitSwapWidget::LoadCatalog()
{
	Catalog.Reset(); if (DeckSubsystem) for (UMSMobileSuitData* Preset : DeckSubsystem->GetCatalogPresets()) if (Preset) Catalog.Add(Preset);
}

void UMSSuitSwapWidget::ApplyFilters()
{
	PageIndex = 0;
	FilteredCatalog.Reset();
	if (ScreenMode == EMSSuitSwapScreenMode::Development)
	{
		RebuildRows();
		return;
	}
	const FString Query = SearchInput ? SearchInput->GetText().ToString().TrimStartAndEnd() : FString();
	for (UMSMobileSuitData* Preset : Catalog)
	{
		if (!Preset) continue;
		if (SelectedCategoryIndex > 0 && static_cast<int32>(Preset->UnitType) != SelectedCategoryIndex - 1) continue;
		if (!Query.IsEmpty() && !Preset->UnitName.ToString().Contains(Query, ESearchCase::IgnoreCase)) continue;
		FilteredCatalog.Add(Preset);
	}
	FilteredCatalog.Sort([this](const UMSMobileSuitData& A, const UMSMobileSuitData& B)
	{
		if (A.Cost == B.Cost) return A.UnitName.ToString() < B.UnitName.ToString();
		return bCostDescending ? A.Cost > B.Cost : A.Cost < B.Cost;
	});
	RebuildRows();
}

void UMSSuitSwapWidget::RebuildRows()
{
	if (!CatalogScrollBox) return;
	CatalogScrollBox->ClearChildren();
	const int32 PageCount = FMath::Max(1, FMath::DivideAndRoundUp(FilteredCatalog.Num(), PageSize));
	PageIndex = FMath::Clamp(PageIndex, 0, PageCount - 1);
	if (PageNumberText) PageNumberText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), PageIndex + 1, PageCount)));
	if (PreviousPageButton) PreviousPageButton->SetIsEnabled(PageIndex > 0);
	if (NextPageButton) NextPageButton->SetIsEnabled(PageIndex + 1 < PageCount);
	const int32 End = FMath::Min((PageIndex + 1) * PageSize, FilteredCatalog.Num());
	for (int32 Index = PageIndex * PageSize; Index < End; ++Index)
	{
		UMSSuitSwapEntryWidget* Entry = WidgetTree->ConstructWidget<UMSSuitSwapEntryWidget>(); Entry->SetEntry(FilteredCatalog[Index], this);
		UScrollBoxSlot* RowSlot = CastChecked<UScrollBoxSlot>(CatalogScrollBox->AddChild(Entry)); RowSlot->SetPadding(FMargin(0, 0, 0, Index + 1 < End ? 7 : 0));
	}
	CatalogScrollBox->ScrollToStart();
}

void UMSSuitSwapWidget::SelectCandidate(UMSMobileSuitData* Preset)
{
	if (!Preset || ScreenMode == EMSSuitSwapScreenMode::Development) return;
	CandidatePreset = Preset;
	// The owned-suit screen is a read-only inventory view. A row can be selected
	// for UI feedback, but it never enters the deck replacement confirmation flow.
	if (ScreenMode == EMSSuitSwapScreenMode::OwnedList) return;
	ShowComparisonPhase();
}
void UMSSuitSwapWidget::ShowListPhase() { bComparisonPhase = false; if (ListPhase) ListPhase->SetVisibility(ESlateVisibility::Visible); if (ComparisonPhase) ComparisonPhase->SetVisibility(ESlateVisibility::Collapsed); }
void UMSSuitSwapWidget::ShowComparisonPhase() { bComparisonPhase = true; if (ListPhase) ListPhase->SetVisibility(ESlateVisibility::Collapsed); if (ComparisonPhase) ComparisonPhase->SetVisibility(ESlateVisibility::Visible); RefreshComparison(); }

void UMSSuitSwapWidget::RefreshCurrentDetails()
{
	UMSMobileSuitData* Current = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(SlotIndex) : nullptr;
	const FMSDeckLoadout Loadout = DeckSubsystem ? DeckSubsystem->GetLoadoutInSlot(SlotIndex) : FMSDeckLoadout();
	if (ListPreviewRenderer) ListPreviewRenderer->SetMobileSuit(Current, Loadout.bShieldEquipped);
	if (ListPreviewImage) ListPreviewImage->SetVisibility(ScreenMode == EMSSuitSwapScreenMode::Swap && Current ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (ListCurrentSummaryText)
	{
		const FString Details = DeckSubsystem ? MSSuitSwap::StatsWithLoadout(Current, Loadout) : MSSuitSwap::Stats(Current);
		ListCurrentSummaryText->SetText(FText::FromString(FString::Printf(TEXT("現行機体\n%s\n\n%s\n\n%s"), Current ? *Current->UnitName.ToString() : TEXT("---"), *Details, *MSSuitSwap::ShieldSummary(Current, Loadout))));
	}
}

void UMSSuitSwapWidget::RefreshComparison()
{
	UMSMobileSuitData* Current = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(SlotIndex) : nullptr;
	const FMSDeckLoadout CurrentLoadout = DeckSubsystem ? DeckSubsystem->GetLoadoutInSlot(SlotIndex) : FMSDeckLoadout();
	if (CandidateNameText) CandidateNameText->SetText(CandidatePreset ? CandidatePreset->UnitName : FText::FromString(TEXT("---")));
	if (CurrentNameText) CurrentNameText->SetText(Current ? Current->UnitName : FText::FromString(TEXT("---")));
	if (CandidateStatsText) CandidateStatsText->SetText(FText::FromString(MSSuitSwap::Stats(CandidatePreset)));
	if (CurrentStatsText) CurrentStatsText->SetText(FText::FromString(MSSuitSwap::StatsWithLoadout(Current, CurrentLoadout)));
	if (CandidateShieldText)
	{
		const FMSDeckLoadout CandidateLoadout;
		CandidateShieldText->SetText(FText::FromString(MSSuitSwap::ShieldSummary(CandidatePreset, CandidateLoadout)));
	}
	if (CurrentShieldText) CurrentShieldText->SetText(FText::FromString(MSSuitSwap::ShieldSummary(Current, CurrentLoadout)));
	if (CandidatePreviewRenderer) CandidatePreviewRenderer->SetMobileSuit(CandidatePreset);
	if (CurrentPreviewRenderer) CurrentPreviewRenderer->SetMobileSuit(Current, CurrentLoadout.bShieldEquipped);
	if (CandidatePreviewImage) CandidatePreviewImage->SetVisibility(CandidatePreset ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (CurrentPreviewImage) CurrentPreviewImage->SetVisibility(Current ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	const int32 CostAfter = GetDeckCostAfterSwap();
	if (CostAfterText) { CostAfterText->SetText(FText::FromString(FString::Printf(TEXT("デッキコスト\n%d/%d"), CostAfter, UMSDeckSubsystem::DeckCostLimit))); CostAfterText->SetColorAndOpacity(FSlateColor(CostAfter > UMSDeckSubsystem::DeckCostLimit ? MSSuitSwap::WarningColor : FLinearColor::White)); }
	if (ConfirmButton) ConfirmButton->SetIsEnabled(CanConfirmSwap());
}

int32 UMSSuitSwapWidget::GetDeckCostAfterSwap() const
{
	if (!DeckSubsystem || !CandidatePreset) return DeckSubsystem ? DeckSubsystem->GetCurrentDeckCost() : 0;
	const UMSMobileSuitData* Current = DeckSubsystem->GetPresetInSlot(SlotIndex);
	return DeckSubsystem->GetCurrentDeckCost() - (Current ? Current->Cost : 0) + CandidatePreset->Cost;
}
bool UMSSuitSwapWidget::CanConfirmSwap() const { return ScreenMode == EMSSuitSwapScreenMode::Swap && DeckSubsystem && CandidatePreset && SlotIndex >= 0 && SlotIndex < UMSDeckSubsystem::SlotCount && GetDeckCostAfterSwap() <= UMSDeckSubsystem::DeckCostLimit; }
FText UMSSuitSwapWidget::GetCandidateName() const { return CandidateNameText ? CandidateNameText->GetText() : FText::GetEmpty(); }
FText UMSSuitSwapWidget::GetCurrentName() const { return CurrentNameText ? CurrentNameText->GetText() : FText::GetEmpty(); }
FText UMSSuitSwapWidget::GetCurrentShieldTextForTesting() const { return CurrentShieldText ? CurrentShieldText->GetText() : FText::GetEmpty(); }
FText UMSSuitSwapWidget::GetCandidateShieldTextForTesting() const { return CandidateShieldText ? CandidateShieldText->GetText() : FText::GetEmpty(); }

UMSDeckSubsystem* UMSSuitSwapWidget::ResolveDeckSubsystem() const { return DeckSubsystem ? DeckSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr); }
UMSUILockSubsystem* UMSSuitSwapWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void UMSSuitSwapWidget::HandlePreviousPageClicked() { if (PageIndex > 0) { --PageIndex; RebuildRows(); } }
void UMSSuitSwapWidget::HandleNextPageClicked() { if ((PageIndex + 1) * PageSize < FilteredCatalog.Num()) { ++PageIndex; RebuildRows(); } }
void UMSSuitSwapWidget::SelectCategory(const int32 CategoryIndex) { SelectedCategoryIndex = CategoryIndex; ApplyFilters(); }
void UMSSuitSwapWidget::HandleSearchClicked() { ApplyFilters(); }
void UMSSuitSwapWidget::HandleClearClicked() { if (SearchInput) SearchInput->SetText(FText::GetEmpty()); ApplyFilters(); }
void UMSSuitSwapWidget::HandleSortClicked() { bCostDescending = !bCostDescending; if (SortText) SortText->SetText(FText::FromString(bCostDescending ? TEXT("COST：高い順") : TEXT("COST：低い順"))); ApplyFilters(); }
void UMSSuitSwapWidget::HandleCategoryAllClicked() { SelectCategory(0); }
void UMSSuitSwapWidget::HandleCategoryAssaultClicked() { SelectCategory(1); }
void UMSSuitSwapWidget::HandleCategoryHeavyClicked() { SelectCategory(2); }
void UMSSuitSwapWidget::HandleCategorySupportClicked() { SelectCategory(3); }
void UMSSuitSwapWidget::HandleCategoryBombardmentClicked() { SelectCategory(4); }
void UMSSuitSwapWidget::HandleCategorySniperClicked() { SelectCategory(5); }
void UMSSuitSwapWidget::HandleConfirmClicked()
{
	if (ScreenMode != EMSSuitSwapScreenMode::Swap || !CanConfirmSwap() || !DeckSubsystem->AssignPreset(SlotIndex, CandidatePreset)) return;
	bComparisonPhase = false; Close();
}
void UMSSuitSwapWidget::HandleBackClicked() { Close(); }

void UMSSuitSwapWidget::NativeDestruct()
{
	if (ListPreviewRenderer) ListPreviewRenderer->Shutdown();
	if (CandidatePreviewRenderer) CandidatePreviewRenderer->Shutdown(); if (CurrentPreviewRenderer) CurrentPreviewRenderer->Shutdown(); Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
bool UMSSuitSwapWidget::IsCurrentPreviewShieldVisibleForTesting() const
{
	return CurrentPreviewRenderer && CurrentPreviewRenderer->IsNativeShieldVisible();
}

void UMSSuitSwapWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
FLinearColor UMSSuitSwapWidget::GetDeckCostTextColorForAutomationTests() const
{
	return CostAfterText ? CostAfterText->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
}
void UMSSuitSwapWidget::SetCatalogForTesting(const TArray<UMSMobileSuitData*>& InCatalog)
{
	Catalog.Reset(); for (UMSMobileSuitData* Preset : InCatalog) if (Preset) Catalog.Add(Preset); bCatalogInjectedForTests = true; ApplyFilters();
}
#endif
