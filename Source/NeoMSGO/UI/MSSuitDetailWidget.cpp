#include "UI/MSSuitDetailWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Data/MSWeaponData.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "InputCoreTypes.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSBattleAssistCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSHangarChrome.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSModuleCustomizeWidget.h"
#include "UI/MSPixelUI.h"
#include "UI/MSWeaponIconWidget.h"
#include "UI/MSSuitSwapWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"

namespace MSSuitDetail
{
	const FLinearColor BackgroundColor(0.003f, 0.012f, 0.02f, 0.94f);
	const FLinearColor PanelColor(0.025f, 0.09f, 0.115f, 0.98f);
	const FLinearColor AccentColor(0.18f, 0.9f, 0.84f, 1.0f);
	const FLinearColor DisabledColor(0.34f, 0.4f, 0.41f, 1.0f);
	const FLinearColor SellColor(0.92f, 0.38f, 0.08f, 1.0f);
	const FLinearColor BonusColor(0.25f, 1.0f, 0.42f, 1.0f);

	FString WeaponName(const TSoftObjectPtr<UMSWeaponDataBase>& Ref)
	{
		const UMSWeaponDataBase* Weapon = Ref.Get();
		if (!Weapon && !Ref.IsNull()) Weapon = Ref.LoadSynchronous();
		return Weapon && !Weapon->WeaponName.IsEmpty() ? Weapon->WeaponName.ToString() : TEXT("---");
	}

	FString ModuleName(const TSoftObjectPtr<UMSModuleData>& Ref)
	{
		const UMSModuleData* Module = Ref.Get();
		if (!Module && !Ref.IsNull()) Module = Ref.LoadSynchronous();
		return Module && !Module->ModuleName.IsEmpty() ? Module->ModuleName.ToString() : TEXT("---");
	}
}

namespace
{
	const FVector2D DetailCanvasSize(1920.0f, 1080.0f);

	void PlaceOnDetailCanvas(UCanvasPanel* Canvas, UWidget* Child, const float X, const float Y,
		const float Width, const float Height)
	{
		if (!Canvas || !Child) return;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(Width, Height));
	}

	UTextBlock* DetailText(UWidgetTree* Tree, const FText& Value, const float PixelHeight,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		return MSPixelUI::Text(Tree, Value, PixelHeight, Color, Name);
	}

	UButton* DetailButton(UWidgetTree* Tree, const FText& Label, const FName Name, const bool bEnabled,
		const int32 Variant = 1, const FLinearColor& TextColor = FLinearColor::FromSRGBColor(FColor(0x25,0x34,0x5A)),
		const MSUIStyle::EButtonSound Sound = MSUIStyle::EButtonSound::Click)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Button);
		UTextBlock* Text = DetailText(Tree, Label, 20.0f, TextColor);
		if (Variant == 0) Text->SetColorAndOpacity(FLinearColor::White);
		Text->SetJustification(ETextJustify::Center);
		UScaleBox* LabelFit = Tree->ConstructWidget<UScaleBox>(); LabelFit->SetStretch(EStretch::ScaleToFit); LabelFit->SetStretchDirection(EStretchDirection::DownOnly); LabelFit->SetContent(Text);
		Button->SetContent(MSPixelUI::Frame(Tree, LabelFit, EMSVectorSurface::Button, FMargin(7.0f, 3.0f), Variant));
		MSUIStyle::StretchButtonContent(Button);
		// ClearButtonBrush intentionally removes the stock brushes. Re-apply the semantic
		// pressed sound afterwards so the vector skin does not make UI clicks silent.
		MSUIStyle::ApplyButtonSound(Button, Sound);
		if (Variant == 1) Text->SetShadowOffset(FVector2D::ZeroVector);
		Button->SetIsEnabled(bEnabled);
		return Button;
	}

	UOverlay* DetailPanel(UWidgetTree* Tree, UWidget* Content, const EMSVectorSurface Surface = EMSVectorSurface::Panel,
		const FMargin Padding = FMargin(10.0f))
	{
		return MSPixelUI::Frame(Tree, Content, Surface, Padding);
	}
}

void UMSSuitDetailWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); BindDeckSubsystem();
	OnVisibilityChanged.AddUniqueDynamic(this, &UMSSuitDetailWidget::HandleDetailVisibilityChanged);
	Refresh(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSSuitDetailWidget::BuildReferenceLayout()
{
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("SuitDetailScaleToFit"));
	Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both);
	USizeBox* DesignSize = WidgetTree->ConstructWidget<USizeBox>(); DesignSize->SetWidthOverride(DetailCanvasSize.X); DesignSize->SetHeightOverride(DetailCanvasSize.Y);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SuitDetailDesignCanvas"));
	DesignSize->SetContent(Canvas); Fit->SetContent(DesignSize); WidgetTree->RootWidget = Fit;
	PlaceOnDetailCanvas(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::HangarBackdrop, TEXT("SuitDetailHangarBackdrop")), 0, 0, 1920, 1080);
	PreviewImage = WidgetTree->ConstructWidget<UImage>();
	PreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnDetailCanvas(Canvas, PreviewImage, 0, 0, 1920, 1080);

	HeaderIdentityText=MSHangarChrome::Build(this,Canvas,FText::FromString(TEXT("格納庫 > カスタマイズ")));
	UButton* Back = DetailButton(WidgetTree, FText::FromString(TEXT("戻る")), TEXT("SuitDetailBackButton"), true, 1, FLinearColor(.55f,.05f,.15f), MSUIStyle::EButtonSound::Back);
	Back->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleBackClicked); PlaceOnDetailCanvas(Canvas, Back, 1352, 225, 114, 30);

	const TCHAR* ActionLabels[] = { TEXT("武器カスタマイズ"), TEXT("装備／特性セット"), TEXT("機体強化"), TEXT("固有能力"), TEXT("特性強化"), TEXT("アドバンストカスタム"), TEXT("エネルギー回復"), TEXT("カラーリング／エンブレム"), TEXT("機体を入れ替え"), TEXT("出撃機体から外す") };
	const float ActionY[] = { 266, 307, 348, 389, 430, 471, 530, 571, 629, 670 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ActionLabels); ++Index)
	{
		const FName Name = Index == 0 ? TEXT("WeaponCustomizeButton") : Index == 1 ? TEXT("EquipmentSetButton") : Index == 2 ? TEXT("SuitUpgradeButton") : Index == 3 ? TEXT("ModuleCustomizeButton") : Index == 4 ? TEXT("TraitUpgradeButton") : Index == 5 ? TEXT("AdvancedCustomButton") : Index == 6 ? TEXT("EnergyRecoveryButton") : Index == 7 ? TEXT("ColoringEmblemButton") : Index == 8 ? TEXT("SuitSwapButton") : TEXT("RemoveFromDeckButton");
		const bool bEnabled = Index == 0 || Index == 3 || Index == 8 || Index == 9;
		UButton* Action = DetailButton(WidgetTree, FText::FromString(ActionLabels[Index]), Name, bEnabled);
		PlaceOnDetailCanvas(Canvas, Action, 460, ActionY[Index], 199, 30);
		if (Index == 0) Action->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleWeaponCustomizeClicked);
		else if (Index == 3) Action->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleModuleCustomizeClicked);
		else if (Index == 8) Action->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleSwapClicked);
		else if (Index == 9) Action->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleRemoveClicked);
	}
	UButton* BattleAssist = DetailButton(WidgetTree, FText::FromString(TEXT("バトルアシスト")), TEXT("BattleAssistCustomizeButton"), true);
	BattleAssist->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleBattleAssistCustomizeClicked);
	PlaceOnDetailCanvas(Canvas, BattleAssist, 460, 711, 199, 30);
	EnhancementCountText = DetailText(WidgetTree, FText::FromString(TEXT("強化回数  未対応")), 13.0f, FLinearColor(1.0f, 0.83f, 0.29f, 1.0f), TEXT("EnhancementCountText"));
	UOverlay* EnhanceFrame = DetailPanel(WidgetTree, EnhancementCountText, EMSVectorSurface::GoldPanel, FMargin(5, 3)); EnhanceFrame->SetVisibility(ESlateVisibility::HitTestInvisible); PlaceOnDetailCanvas(Canvas, EnhanceFrame, 659, 344, 138, 33);

	UVerticalBox* PreviewColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* PreviewCaption = DetailText(WidgetTree, FText::FromString(TEXT("MOBILE SUIT PREVIEW")), 14.0f, MSSuitDetail::AccentColor); PreviewCaption->SetJustification(ETextJustify::Center); PreviewColumn->AddChildToVerticalBox(PreviewCaption);
	// Keep the preview's desired height owned by this fixed region. Rotation controls
	// live in the overlay and therefore cannot add a row to PreviewColumn or push the
	// deck pager into the persistent bottom chat/pilot strip.
	UOverlay* PreviewRegion = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SuitDetailPreviewRegion"));
	USizeBox* PreviewSize = WidgetTree->ConstructWidget<USizeBox>(); PreviewSize->SetWidthOverride(510); PreviewSize->SetHeightOverride(650);
	PreviewSize->SetContent(PreviewRegion); PreviewColumn->AddChildToVerticalBox(PreviewSize);
	SuitNameText = DetailText(WidgetTree, FText::FromString(TEXT("---")), 18.0f, FLinearColor::White, TEXT("SuitDetailNameText")); SuitNameText->SetAutoWrapText(false); SuitNameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); SuitNameText->SetJustification(ETextJustify::Center); PreviewColumn->AddChildToVerticalBox(SuitNameText);

	PlaceOnDetailCanvas(Canvas, PreviewColumn, 700, 105, 510, 765);

	RotateLeftButton = DetailButton(WidgetTree, FText::FromString(TEXT("↶")), TEXT("RotateSuitLeftButton"), false, 0);
	RotateRightButton = DetailButton(WidgetTree, FText::FromString(TEXT("↷")), TEXT("RotateSuitRightButton"), false, 0);
	RotateLeftButton->SetToolTipText(FText::FromString(TEXT("長押しで機体を左回転")));
	RotateRightButton->SetToolTipText(FText::FromString(TEXT("長押しで機体を右回転")));
	RotateLeftButton->OnPressed.AddDynamic(this, &UMSSuitDetailWidget::HandleRotateLeftPressed);
	RotateRightButton->OnPressed.AddDynamic(this, &UMSSuitDetailWidget::HandleRotateRightPressed);
	for (UButton* Button : {RotateLeftButton.Get(), RotateRightButton.Get()})
	{
		Button->OnReleased.AddDynamic(this, &UMSSuitDetailWidget::StopPreviewRotation);
		Button->OnUnhovered.AddDynamic(this, &UMSSuitDetailWidget::StopPreviewRotation);
	}
	UTextBlock* RotationHint = DetailText(WidgetTree, FText::FromString(TEXT("長押しで回転")), 16, MSSuitDetail::AccentColor);
	RotationHint->SetAutoWrapText(false); RotationHint->SetJustification(ETextJustify::Center);
	UHorizontalBox* RotationBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SuitDetailRotationBar"));
	USizeBox* LeftRotationSize = WidgetTree->ConstructWidget<USizeBox>(); LeftRotationSize->SetWidthOverride(58); LeftRotationSize->SetHeightOverride(34); LeftRotationSize->SetContent(RotateLeftButton); RotationBar->AddChildToHorizontalBox(LeftRotationSize);
	USizeBox* RotationHintSize = WidgetTree->ConstructWidget<USizeBox>(); RotationHintSize->SetWidthOverride(142); RotationHintSize->SetHeightOverride(34); RotationHintSize->SetContent(RotationHint); RotationBar->AddChildToHorizontalBox(RotationHintSize);
	USizeBox* RightRotationSize = WidgetTree->ConstructWidget<USizeBox>(); RightRotationSize->SetWidthOverride(58); RightRotationSize->SetHeightOverride(34); RightRotationSize->SetContent(RotateRightButton); RotationBar->AddChildToHorizontalBox(RightRotationSize);
	UOverlaySlot* RotationSlot = PreviewRegion->AddChildToOverlay(RotationBar); RotationSlot->SetHorizontalAlignment(HAlign_Center); RotationSlot->SetVerticalAlignment(VAlign_Bottom); RotationSlot->SetPadding(FMargin(0, 0, 0, 8));

	UVerticalBox* Pager = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* PagerControls = WidgetTree->ConstructWidget<UHorizontalBox>(); Pager->AddChildToVerticalBox(PagerControls);
	UButton* Previous = DetailButton(WidgetTree, FText::FromString(TEXT("‹")), TEXT("PreviousSuitButton"), true, 0); Previous->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandlePreviousClicked); PagerControls->AddChildToHorizontalBox(Previous)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	DeckNumberText = DetailText(WidgetTree, FText::FromString(TEXT("01")), 18.0f, FLinearColor::White, TEXT("DeckNumberText")); DeckNumberText->SetJustification(ETextJustify::Center); PagerControls->AddChildToHorizontalBox(DeckNumberText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* Next = DetailButton(WidgetTree, FText::FromString(TEXT("›")), TEXT("NextSuitButton"), true, 0); Next->OnClicked.AddDynamic(this, &UMSSuitDetailWidget::HandleNextClicked); PagerControls->AddChildToHorizontalBox(Next)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	// The pager remains an independent canvas child below the fixed preview column;
	// its position does not depend on the rotation controls' desired size.
	PlaceOnDetailCanvas(Canvas, DetailPanel(WidgetTree, Pager, EMSVectorSurface::GoldPanel, FMargin(7, 9)), 866, 800, 178, 61);

	UVerticalBox* Details = WidgetTree->ConstructWidget<UVerticalBox>();
	auto AddInstrumentRow = [this, Details](UWidget* Content, const float Height)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetHeightOverride(Height); Size->SetContent(Content); Details->AddChildToVerticalBox(Size);
	};
	AddInstrumentRow(DetailText(WidgetTree, FText::FromString(TEXT("機体詳細")), 16, MSSuitDetail::AccentColor), 18);
	RightNameText = DetailText(WidgetTree, FText::FromString(TEXT("---")), 17, FLinearColor::White, TEXT("SuitDetailRightNameText")); RightNameText->SetAutoWrapText(false); RightNameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); RightNameText->SetClipping(EWidgetClipping::ClipToBounds);
	UScaleBox* SuitNameFit = WidgetTree->ConstructWidget<UScaleBox>(); SuitNameFit->SetStretch(EStretch::ScaleToFit); SuitNameFit->SetStretchDirection(EStretchDirection::DownOnly); SuitNameFit->SetContent(RightNameText); CastChecked<UScaleBoxSlot>(RightNameText->Slot)->SetHorizontalAlignment(HAlign_Left); AddInstrumentRow(SuitNameFit, 24);
	CostTimeText = DetailText(WidgetTree, FText::FromString(TEXT("COST ---")), 14, FLinearColor(1,.9f,.35f), TEXT("SuitDetailCostTimeText")); AddInstrumentRow(CostTimeText, 20);
	AddInstrumentRow(DetailText(WidgetTree, FText::FromString(TEXT("TIME ---")), 14, FLinearColor(1,.9f,.35f), TEXT("SuitDetailInstrumentTimeText")), 20);
	SuitTypeBadgeText = DetailText(WidgetTree, FText::FromString(TEXT("タイプ ---")), 12, FLinearColor(1,.72f,.15f), TEXT("SuitTypeBadgeText")); AddInstrumentRow(SuitTypeBadgeText, 16);
	StatusBonusSummaryText = DetailText(WidgetTree, FText::FromString(TEXT("強化補正  未対応")), 11, MSSuitDetail::DisabledColor, TEXT("SuitStatusBonusSummaryText")); AddInstrumentRow(StatusBonusSummaryText, 16);
	const TCHAR* StatusLabels[] = { TEXT("アーマー"), TEXT("最高速度 (m/s)"), TEXT("ブースト容量"), TEXT("ブーストチャージ"), TEXT("積載量"), TEXT("緊急帰投性能") };
	StatusBars.Reset(); StatusValueTexts.Reset(); WeaponIcons.Reset(); WeaponNameTexts.Reset();
	for (int32 Index = 0; Index < 6; ++Index)
	{
		UVerticalBox* Status = WidgetTree->ConstructWidget<UVerticalBox>(); AddInstrumentRow(Status, 25);
		UHorizontalBox* LabelRow = WidgetTree->ConstructWidget<UHorizontalBox>(); Status->AddChildToVerticalBox(LabelRow);
		LabelRow->AddChildToHorizontalBox(DetailText(WidgetTree, FText::FromString(StatusLabels[Index]), 12, MSSuitDetail::AccentColor))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Value = DetailText(WidgetTree, FText::FromString(TEXT("---")), 12, FLinearColor::White, *FString::Printf(TEXT("SuitStatusValue%d"), Index)); LabelRow->AddChildToHorizontalBox(Value); StatusValueTexts.Add(Value);
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *FString::Printf(TEXT("SuitStatusBar%d"), Index)); Bar->SetFillColorAndOpacity(Index == 5 ? FLinearColor(.35f,.72f,1) : FLinearColor(.18f,.95f,.48f));
		USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(); BarSize->SetHeightOverride(5); BarSize->SetContent(Bar); Status->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0, 2, 0, 0)); StatusBars.Add(Bar);
	}
	for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index)
	{
		UBorder* RowPlate = WidgetTree->ConstructWidget<UBorder>(); MSPixelUI::PlainBorder(RowPlate, FLinearColor(.016f,.07f,.11f,.88f)); RowPlate->SetPadding(FMargin(2, 1)); AddInstrumentRow(RowPlate, 36);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(); RowPlate->SetContent(Row);
		UMSWeaponIconWidget* Icon = WidgetTree->ConstructWidget<UMSWeaponIconWidget>(UMSWeaponIconWidget::StaticClass(), *FString::Printf(TEXT("SuitDetailWeaponIcon%d"), Index)); Icon->Initialize(); Icon->SetVisibility(ESlateVisibility::HitTestInvisible); WeaponIcons.Add(Icon);
		UVerticalBox* IconColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		USizeBox* ColumnSize = WidgetTree->ConstructWidget<USizeBox>(); ColumnSize->SetWidthOverride(70); ColumnSize->SetContent(IconColumn); Row->AddChildToHorizontalBox(ColumnSize)->SetVerticalAlignment(VAlign_Center);
		IconColumn->AddChildToVerticalBox(DetailText(WidgetTree, FText::FromString(FString::Printf(TEXT("SLOT%d"), Index + 1)), 10, MSSuitDetail::AccentColor));
		USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(); IconSize->SetWidthOverride(70); IconSize->SetHeightOverride(17); IconSize->SetContent(Icon); IconColumn->AddChildToVerticalBox(IconSize);
		UTextBlock* Name = DetailText(WidgetTree, FText::FromString(TEXT("---")), 13, FLinearColor(.8f,.94f,.98f), *FString::Printf(TEXT("SuitDetailWeaponName%d"), Index));
		// Weapon rows have a fixed height. Keep each name on one readable line and
		// clip long variants instead of creating a short third line in the next row.
		Name->SetAutoWrapText(false); Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); Name->SetClipping(EWidgetClipping::ClipToBounds);
		UScaleBox* WeaponNameFit = WidgetTree->ConstructWidget<UScaleBox>(); WeaponNameFit->SetStretch(EStretch::ScaleToFit); WeaponNameFit->SetStretchDirection(EStretchDirection::DownOnly); WeaponNameFit->SetContent(Name); CastChecked<UScaleBoxSlot>(Name->Slot)->SetHorizontalAlignment(HAlign_Left);
		Row->AddChildToHorizontalBox(WeaponNameFit)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); WeaponNameTexts.Add(Name);
	}
	NativeShieldText = DetailText(WidgetTree, FText::FromString(TEXT("SHIELD  盾なし")), 12, MSSuitDetail::AccentColor, TEXT("SuitDetailShieldText"));
	NativeShieldText->SetAutoWrapText(false); NativeShieldText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	UScaleBox* ShieldFit = WidgetTree->ConstructWidget<UScaleBox>(); ShieldFit->SetStretch(EStretch::ScaleToFit); ShieldFit->SetStretchDirection(EStretchDirection::DownOnly); ShieldFit->SetContent(NativeShieldText); CastChecked<UScaleBoxSlot>(NativeShieldText->Slot)->SetHorizontalAlignment(HAlign_Left); AddInstrumentRow(ShieldFit, 28);
	LoadoutText = DetailText(WidgetTree, FText::FromString(TEXT("ABILITY  ---")), 12, FLinearColor(.72f,.94f,.96f), TEXT("SuitDetailLoadoutText")); LoadoutText->SetAutoWrapText(false); LoadoutText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); LoadoutText->SetClipping(EWidgetClipping::ClipToBounds);
	UScaleBox* ModuleFit = WidgetTree->ConstructWidget<UScaleBox>(); ModuleFit->SetStretch(EStretch::ScaleToFit); ModuleFit->SetStretchDirection(EStretchDirection::DownOnly); ModuleFit->SetContent(LoadoutText); CastChecked<UScaleBoxSlot>(LoadoutText->Slot)->SetHorizontalAlignment(HAlign_Left); AddInstrumentRow(ModuleFit, 28);
	PlaceOnDetailCanvas(Canvas, DetailPanel(WidgetTree, Details, EMSVectorSurface::Panel, FMargin(10, 9)), 1227, 282, 228, 456);
}

void UMSSuitDetailWidget::BindDeckSubsystem()
{
	UMSDeckSubsystem* Resolved = ResolveDeckSubsystem(); if (DeckSubsystem == Resolved) return;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSuitDetailWidget::HandleDeckChanged);
	DeckSubsystem = Resolved;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSSuitDetailWidget::HandleDeckChanged);
}

namespace
{
	int64 OnlineAuthGeneration(const UUserWidget* Widget)
	{
		const UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		const UMSOnlineAccountSubsystem* Account = GameInstance ? GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		return Account && Account->IsOnlineMode() ? Account->GetAuthGeneration() : INDEX_NONE;
	}

	bool IsOnlineAuthGenerationCurrent(const UUserWidget* Widget, const int64 ExpectedGeneration)
	{
		return ExpectedGeneration == INDEX_NONE || OnlineAuthGeneration(Widget) == ExpectedGeneration;
	}
}

void UMSSuitDetailWidget::Open(const int32 SlotIndex)
{
	OpenInternal(SlotIndex, nullptr);
}

void UMSSuitDetailWidget::CancelPendingSelection()
{
	StopPreviewRotation();
	++SelectionRequestGeneration;
}

void UMSSuitDetailWidget::OpenInternal(const int32 SlotIndex, TFunction<void(bool bSucceeded)> Completion)
{
	StopPreviewRotation();
	BindDeckSubsystem();
	// Invalidate a previous pending open even when the new target is no longer
	// occupied. This prevents a late acknowledgement from reopening an old slot.
	const uint64 RequestGeneration = ++SelectionRequestGeneration;
	if (!DeckSubsystem || !DeckSubsystem->GetPresetInSlot(SlotIndex))
	{
		if (Completion) Completion(false);
		return;
	}

	const bool bWasOpen = IsOpen();
	const int32 PreviousDisplayedSlot = DisplayedSlotIndex;
	const int64 AuthGeneration = OnlineAuthGeneration(this);
	const TWeakObjectPtr<UMSSuitDetailWidget> WeakThis(this);
	const TSharedRef<TFunction<void(bool)>> CompletionHolder =
		MakeShared<TFunction<void(bool)>>(MoveTemp(Completion));
	const bool bStarted = DeckSubsystem->RequestSelectedSlotIndex(SlotIndex,
		[WeakThis, SlotIndex, RequestGeneration, AuthGeneration, bWasOpen, PreviousDisplayedSlot, CompletionHolder](const bool bSucceeded) mutable
	{
		if (!WeakThis.IsValid() || WeakThis->SelectionRequestGeneration != RequestGeneration) return;
		if (!IsOnlineAuthGenerationCurrent(WeakThis.Get(), AuthGeneration)) return;
		bool bOpened = false;
		if (bSucceeded && WeakThis->DeckSubsystem && WeakThis->DeckSubsystem->GetPresetInSlot(SlotIndex))
		{
			WeakThis->DisplayedSlotIndex = SlotIndex;
			WeakThis->Refresh();
			if (WeakThis->GetWorld() && !WeakThis->IsInViewport()) WeakThis->AddToViewport(90);
			WeakThis->SetVisibility(ESlateVisibility::Visible);
			WeakThis->SetKeyboardFocus();
			if (!bWasOpen)
			{
				if (UMSUILockSubsystem* Lock = WeakThis->ResolveUILockSubsystem())
					Lock->PushScreen(TEXT("SuitDetail"), FMSUICloseRequestDelegate::CreateWeakLambda(WeakThis.Get(), [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Close(); return true; } return false; }));
			}
			if (WeakThis->CustomizeWidget) WeakThis->CustomizeWidget->HandleSuitDetailOpened(SlotIndex);
			bOpened = true;
		}
		else if (bWasOpen)
		{
			// Keep the previous screen intact when a requested online selection fails.
			WeakThis->DisplayedSlotIndex = PreviousDisplayedSlot;
			WeakThis->Refresh();
		}
		else
		{
			WeakThis->SetVisibility(ESlateVisibility::Collapsed);
			if (WeakThis->CustomizeWidget) WeakThis->CustomizeWidget->HandleSuitDetailClosed();
		}
		if (*CompletionHolder)
		{
			TFunction<void(bool)> FinalCompletion = MoveTemp(*CompletionHolder);
			FinalCompletion(bOpened);
		}
	});
	if (!bStarted && *CompletionHolder)
	{
		TFunction<void(bool)> FinalCompletion = MoveTemp(*CompletionHolder);
		FinalCompletion(false);
	}
}

void UMSSuitDetailWidget::Close()
{
	StopPreviewRotation();
	++SelectionRequestGeneration;
	if (!IsOpen()) return; SetVisibility(ESlateVisibility::Collapsed);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TEXT("SuitDetail"));
	if (CustomizeWidget) CustomizeWidget->HandleSuitDetailClosed();
}

FReply UMSSuitDetailWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSSuitDetailWidget::EnsurePreviewRenderer()
{
	if (!PreviewImage || !GetWorld()) return;
	if (!PreviewRenderer) PreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this);
	if (!PreviewRenderer->IsInitialized() && PreviewRenderer->Initialize(GetWorld(), PreviewImage))
		PreviewRenderer->ConfigureHangarView();
}

void UMSSuitDetailWidget::HandleRotateLeftPressed()
{
	PreviewRotationDirection = -1; PreviewRotationPendingSeconds = 0.0f; bPreviewRotationTickObserved = false;
	UE_LOG(LogTemp, Display, TEXT("[SuitPreviewRotation] Press Left visible=%d initialized=%d"),IsVisible(),PreviewRenderer && PreviewRenderer->IsInitialized());
}
void UMSSuitDetailWidget::HandleRotateRightPressed()
{
	PreviewRotationDirection = 1; PreviewRotationPendingSeconds = 0.0f; bPreviewRotationTickObserved = false;
	UE_LOG(LogTemp, Display, TEXT("[SuitPreviewRotation] Press Right visible=%d initialized=%d"),IsVisible(),PreviewRenderer && PreviewRenderer->IsInitialized());
}
void UMSSuitDetailWidget::StopPreviewRotation()
{
	if(PreviewRotationDirection != 0)
		UE_LOG(LogTemp, Display, TEXT("[SuitPreviewRotation] Stop tickObserved=%d yaw=%.3f pending=%.4f"),bPreviewRotationTickObserved,PreviewRenderer?PreviewRenderer->GetPreviewYaw():0.0f,PreviewRotationPendingSeconds);
	PreviewRotationDirection = 0; PreviewRotationPendingSeconds = 0.0f;
}
void UMSSuitDetailWidget::HandleDetailVisibilityChanged(const ESlateVisibility NewVisibility)
{
	if (NewVisibility != ESlateVisibility::Visible) StopPreviewRotation();
}

float UMSSuitDetailWidget::TickPreviewRotation(const float DeltaSeconds, const bool bActuallyPressed)
{
	if(PreviewRotationDirection != 0 && !bPreviewRotationTickObserved)
	{
		bPreviewRotationTickObserved = true;
		UE_LOG(LogTemp, Display, TEXT("[SuitPreviewRotation] FirstTick dt=%.4f visible=%d actualPressed=%d deck=%d preset=%d initialized=%d"),DeltaSeconds,IsVisible(),bActuallyPressed,DeckSubsystem!=nullptr,DeckSubsystem && DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex)!=nullptr,PreviewRenderer && PreviewRenderer->IsInitialized());
	}
	if (!IsVisible() || !bActuallyPressed || !DeckSubsystem || !DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex))
	{
		StopPreviewRotation();
		return 0.0f;
	}
	if (PreviewRotationDirection == 0 || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return 0.0f;
	// One capture at most per widget frame; a hitch must not cause a large spin or a catch-up capture burst.
	PreviewRotationPendingSeconds += FMath::Min(DeltaSeconds, 0.1f);
	if (PreviewRotationPendingSeconds < 1.0f / 30.0f) return 0.0f;
	const float DeltaYaw = PreviewRotationDirection * 75.0f * PreviewRotationPendingSeconds;
	PreviewRotationPendingSeconds = 0.0f;
	if (PreviewRenderer && PreviewRenderer->IsInitialized()) PreviewRenderer->RotatePreview(DeltaYaw);
	return DeltaYaw;
}

void UMSSuitDetailWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UButton* HeldButton = PreviewRotationDirection < 0 ? RotateLeftButton.Get() : RotateRightButton.Get();
	TickPreviewRotation(InDeltaTime, HeldButton && HeldButton->GetIsEnabled() && HeldButton->IsPressed());
}

void UMSSuitDetailWidget::Refresh()
{
	EnsurePreviewRenderer();
	MSHangarChrome::Refresh(this);
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex) : nullptr;
	if (RotateLeftButton) RotateLeftButton->SetIsEnabled(Preset != nullptr);
	if (RotateRightButton) RotateRightButton->SetIsEnabled(Preset != nullptr);
	const FMSDeckLoadout EquipmentForTime = DeckSubsystem ? DeckSubsystem->GetLoadoutInSlot(DisplayedSlotIndex) : FMSDeckLoadout();
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (DeckSubsystem && DeckSubsystem->GetGameInstance())
	{
		if (UMSBattleAssistProgressSubsystem* ProgressSubsystem = DeckSubsystem->GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>())
		{
			ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue;
		}
	}
	const FString ReuseTime = MSBattleAssistUI::ResolveEffectiveTime(Preset, EquipmentForTime.BattleAssist, Progress);
	if (UTextBlock* TimeText = Cast<UTextBlock>(GetWidgetFromName(TEXT("SuitDetailInstrumentTimeText"))))
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("TIME %s"),*ReuseTime)));
	for (const FName Name : { FName(TEXT("WeaponCustomizeButton")), FName(TEXT("ModuleCustomizeButton")), FName(TEXT("BattleAssistCustomizeButton")), FName(TEXT("RemoveFromDeckButton")) })
		if (UButton* Button = Cast<UButton>(GetWidgetFromName(Name))) Button->SetIsEnabled(Preset != nullptr);
	if (UButton* Button = Cast<UButton>(GetWidgetFromName(TEXT("SuitSwapButton")))) Button->SetIsEnabled(DeckSubsystem != nullptr);
	if (UButton* Button = Cast<UButton>(GetWidgetFromName(TEXT("PreviousSuitButton")))) Button->SetIsEnabled(FindOccupiedSlot(-1) != DisplayedSlotIndex);
	if (UButton* Button = Cast<UButton>(GetWidgetFromName(TEXT("NextSuitButton")))) Button->SetIsEnabled(FindOccupiedSlot(1) != DisplayedSlotIndex);
	if (DeckNumberText) DeckNumberText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), DisplayedSlotIndex + 1)));
	const FMSDeckLoadout Equipment=DeckSubsystem?DeckSubsystem->GetLoadoutInSlot(DisplayedSlotIndex):FMSDeckLoadout();
	if (PreviewRenderer) PreviewRenderer->SetMobileSuit(Preset, Equipment.bShieldEquipped);
	if (PreviewImage) PreviewImage->SetVisibility(Preset ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (!Preset)
	{
		if (SuitNameText) SuitNameText->SetText(FText::FromString(TEXT("---")));
		if (RightNameText) RightNameText->SetText(FText::FromString(TEXT("---")));
		if (CostTimeText) CostTimeText->SetText(FText::FromString(TEXT("COST ---")));
		if (SuitTypeBadgeText) SuitTypeBadgeText->SetText(FText::FromString(TEXT("タイプ ---")));
		if (EnhancementCountText) EnhancementCountText->SetText(FText::FromString(TEXT("強化回数  未対応")));
		for (int32 Index = 0; Index < StatusBars.Num(); ++Index) { StatusBars[Index]->SetPercent(0); StatusValueTexts[Index]->SetText(FText::FromString(TEXT("---"))); }
		for (int32 Index = 0; Index < WeaponIcons.Num(); ++Index) { WeaponIcons[Index]->SetVisibility(ESlateVisibility::Hidden); WeaponNameTexts[Index]->SetText(FText::FromString(TEXT("---"))); }
		if (NativeShieldText) NativeShieldText->SetText(FText::FromString(TEXT("SHIELD  盾なし")));
		if (LoadoutText) LoadoutText->SetText(FText::FromString(TEXT("ABILITY  ---"))); return;
	}
	if (SuitNameText) SuitNameText->SetText(Preset->UnitName);
	if (RightNameText) RightNameText->SetText(Preset->UnitName);
	if (CostTimeText) CostTimeText->SetText(FText::FromString(FString::Printf(TEXT("COST %d"), Preset->Cost)));
	if (SuitTypeBadgeText) SuitTypeBadgeText->SetText(FText::FromString(Preset->bAmphibious ? TEXT("タイプ 水中対応") : TEXT("タイプ 地上")));
	if (EnhancementCountText) EnhancementCountText->SetText(FText::FromString(TEXT("強化回数  未対応")));
	const float Values[] = { static_cast<float>(Preset->MaxArmor), Preset->BoostSpeed, Preset->BoostCapacity, Preset->BoostRechargeRate, static_cast<float>(Preset->LoadCapacity), static_cast<float>(Preset->EmergencyRecallPerformance) };
	const float Maximums[] = { 10000.0f, 25.0f, 200.0f, 50.0f, 100.0f, 1000.0f };
	for (int32 Index = 0; Index < StatusRowCount; ++Index)
	{
		StatusBars[Index]->SetPercent(FMath::Clamp(Values[Index] / Maximums[Index], 0.0f, 1.0f));
		StatusValueTexts[Index]->SetText(Index == 1 || Index == 3 ? FText::FromString(FString::Printf(TEXT("%.1f"), Values[Index])) : FText::AsNumber(FMath::RoundToInt(Values[Index])));
	}
	for (int32 Index = 0; Index < WeaponIcons.Num(); ++Index)
	{
		const UMSWeaponDataBase* Weapon = Equipment.EquippedWeapons.IsValidIndex(Index) ? Equipment.EquippedWeapons[Index].LoadSynchronous() : nullptr;
		const bool bShieldWeapon = Weapon && Cast<UMSShieldWeaponData>(Weapon);
		WeaponNameTexts[Index]->SetText(Weapon && !bShieldWeapon ? Weapon->WeaponName : FText::FromString(TEXT("---")));
		WeaponIcons[Index]->SetVisibility(Weapon && !bShieldWeapon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (Weapon && !bShieldWeapon) WeaponIcons[Index]->SetWeaponPhoto(Weapon, FLinearColor(.72f,.85f,.91f));
	}
	if (NativeShieldText)
	{
		const UMSShieldWeaponData* Shield = Preset->GetNativeShield();
		if (!Shield) NativeShieldText->SetText(FText::FromString(TEXT("SHIELD  盾なし")));
		else
		{
			const FString State = !Preset->bShieldRemovable ? TEXT("固定") : (Equipment.bShieldEquipped ? TEXT("装備中") : TEXT("未装備"));
			NativeShieldText->SetText(FText::FromString(FString::Printf(TEXT("SHIELD  %s  %s  DUR %d"), *Shield->WeaponName.ToString(), *State, FMath::RoundToInt(Shield->MaxDurability))));
		}
	}
	if (LoadoutText) LoadoutText->SetText(FText::FromString(FString::Printf(TEXT("ABILITY  %s"), *MSSuitDetail::ModuleName(Equipment.EquippedModule))));
}

int32 UMSSuitDetailWidget::FindOccupiedSlot(const int32 Direction) const
{
	if (!DeckSubsystem) return DisplayedSlotIndex;
	for (int32 Offset = 1; Offset < UMSDeckSubsystem::SlotCount; ++Offset)
	{
		const int32 Candidate = (DisplayedSlotIndex + Direction * Offset + UMSDeckSubsystem::SlotCount * 2) % UMSDeckSubsystem::SlotCount;
		if (DeckSubsystem->GetPresetInSlot(Candidate)) return Candidate;
	}
	return DisplayedSlotIndex;
}

void UMSSuitDetailWidget::SetDisplayedSlot(const int32 SlotIndex)
{
	StopPreviewRotation();
	const uint64 RequestGeneration = ++SelectionRequestGeneration;
	if (SlotIndex == DisplayedSlotIndex || !DeckSubsystem || !DeckSubsystem->GetPresetInSlot(SlotIndex)) return;
	const int64 AuthGeneration = OnlineAuthGeneration(this);
	const TWeakObjectPtr<UMSSuitDetailWidget> WeakThis(this);
	DeckSubsystem->RequestSelectedSlotIndex(SlotIndex,
		[WeakThis, SlotIndex, RequestGeneration, AuthGeneration](const bool bSucceeded)
	{
		if (!WeakThis.IsValid() || WeakThis->SelectionRequestGeneration != RequestGeneration) return;
		if (!IsOnlineAuthGenerationCurrent(WeakThis.Get(), AuthGeneration)) return;
		if (bSucceeded && WeakThis->DeckSubsystem && WeakThis->DeckSubsystem->GetPresetInSlot(SlotIndex))
		{
			WeakThis->DisplayedSlotIndex = SlotIndex;
			WeakThis->Refresh();
		}
		else if (WeakThis->IsOpen())
		{
			WeakThis->Refresh();
		}
	});
}

bool UMSSuitDetailWidget::EnsureHangarWidget()
{
	if (!HangarWidget) if (APlayerController* PC = GetOwningPlayer()) HangarWidget = CreateWidget<UMSHangarWidget>(PC);
	if (HangarWidget) HangarWidget->SetReturnSuitDetailWidget(this); return HangarWidget != nullptr;
}

bool UMSSuitDetailWidget::EnsureModuleCustomizeWidget()
{
	if (!ModuleCustomizeWidget) if (APlayerController* PC = GetOwningPlayer()) ModuleCustomizeWidget = CreateWidget<UMSModuleCustomizeWidget>(PC);
	if (ModuleCustomizeWidget) ModuleCustomizeWidget->SetReturnSuitDetailWidget(this); return ModuleCustomizeWidget != nullptr;
}

bool UMSSuitDetailWidget::EnsureBattleAssistCustomizeWidget()
{
	if (!BattleAssistCustomizeWidget) if (APlayerController* PC = GetOwningPlayer()) BattleAssistCustomizeWidget = CreateWidget<UMSBattleAssistCustomizeWidget>(PC);
	if (BattleAssistCustomizeWidget) BattleAssistCustomizeWidget->SetReturnSuitDetailWidget(this);
	return BattleAssistCustomizeWidget != nullptr;
}

bool UMSSuitDetailWidget::EnsureSwapWidget()
{
	if (!SwapWidget) if (APlayerController* PC = GetOwningPlayer()) SwapWidget = CreateWidget<UMSSuitSwapWidget>(PC);
	if (SwapWidget) SwapWidget->SetSuitDetailWidget(this); return SwapWidget != nullptr;
}

UMSDeckSubsystem* UMSSuitDetailWidget::ResolveDeckSubsystem() const { return DeckSubsystem ? DeckSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr); }
UMSUILockSubsystem* UMSSuitDetailWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void UMSSuitDetailWidget::HandleDeckChanged(const int32 ChangedSlotIndex) { if (ChangedSlotIndex == INDEX_NONE || ChangedSlotIndex == DisplayedSlotIndex) Refresh(); }
void UMSSuitDetailWidget::HandleBackClicked() { Close(); }
void UMSSuitDetailWidget::HandlePreviousClicked() { SetDisplayedSlot(FindOccupiedSlot(-1)); }
void UMSSuitDetailWidget::HandleNextClicked() { SetDisplayedSlot(FindOccupiedSlot(1)); }
void UMSSuitDetailWidget::OpenWeaponCustomization(const int32 SlotIndex)
{
	const TWeakObjectPtr<UMSSuitDetailWidget> WeakThis(this);
	OpenInternal(SlotIndex, [WeakThis](const bool bSucceeded)
	{
		if (bSucceeded && WeakThis.IsValid()) WeakThis->HandleWeaponCustomizeClicked();
	});
}
void UMSSuitDetailWidget::OpenModuleCustomization(const int32 SlotIndex)
{
	const TWeakObjectPtr<UMSSuitDetailWidget> WeakThis(this);
	OpenInternal(SlotIndex, [WeakThis](const bool bSucceeded)
	{
		if (bSucceeded && WeakThis.IsValid()) WeakThis->HandleBattleAssistCustomizeClicked();
	});
}
void UMSSuitDetailWidget::HandleWeaponCustomizeClicked()
{
	++SelectionRequestGeneration;
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex) : nullptr;
	if (Preset && EnsureHangarWidget()) { HangarWidget->OpenForDeckSlot(DisplayedSlotIndex); if(HangarWidget->IsOpen()) SetVisibility(ESlateVisibility::Collapsed); }
}
void UMSSuitDetailWidget::HandleModuleCustomizeClicked()
{
	++SelectionRequestGeneration;
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex) : nullptr;
	if (Preset && EnsureModuleCustomizeWidget()) { SetVisibility(ESlateVisibility::Collapsed); ModuleCustomizeWidget->Open(Preset, DisplayedSlotIndex); }
}
void UMSSuitDetailWidget::OpenBattleAssistCustomization(const int32 SlotIndex)
{
	OpenModuleCustomization(SlotIndex);
}
void UMSSuitDetailWidget::HandleBattleAssistCustomizeClicked()
{
	++SelectionRequestGeneration;
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetPresetInSlot(DisplayedSlotIndex) : nullptr;
	if (Preset && EnsureBattleAssistCustomizeWidget()) { SetVisibility(ESlateVisibility::Collapsed); BattleAssistCustomizeWidget->Open(Preset, DisplayedSlotIndex); }
}
void UMSSuitDetailWidget::HandleSwapClicked() { ++SelectionRequestGeneration; if (EnsureSwapWidget()) { SetVisibility(ESlateVisibility::Collapsed); SwapWidget->Open(DisplayedSlotIndex); } }
void UMSSuitDetailWidget::HandleRemoveClicked()
{
	++SelectionRequestGeneration;
	if (DeckSubsystem && DeckSubsystem->RemovePreset(DisplayedSlotIndex)) Close();
}
void UMSSuitDetailWidget::HandleHangarClosed() { Refresh(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus(); }
void UMSSuitDetailWidget::HandleModuleCustomizeChanged() { Refresh(); }
void UMSSuitDetailWidget::HandleModuleCustomizeClosed() { Refresh(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus(); }
void UMSSuitDetailWidget::HandleBattleAssistCustomizeClosed() { Refresh(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus(); }
void UMSSuitDetailWidget::HandleSwapClosed() { Refresh(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus(); }
FText UMSSuitDetailWidget::GetDisplayedSuitName() const { return SuitNameText ? SuitNameText->GetText() : FText::GetEmpty(); }
FText UMSSuitDetailWidget::GetDisplayedShieldText() const { return NativeShieldText ? NativeShieldText->GetText() : FText::GetEmpty(); }

void UMSSuitDetailWidget::NativeDestruct()
{
	StopPreviewRotation();
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSuitDetailWidget::HandleDeckChanged);
	if (PreviewRenderer) PreviewRenderer->Shutdown(); Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSSuitDetailWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSSuitDetailWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem)
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSuitDetailWidget::HandleDeckChanged);
	DeckSubsystem = InSubsystem; if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSSuitDetailWidget::HandleDeckChanged); Refresh();
}
void UMSSuitDetailWidget::SetSwapWidgetForTesting(UMSSuitSwapWidget* InWidget) { SwapWidget = InWidget; if (SwapWidget) SwapWidget->SetSuitDetailWidget(this); }
void UMSSuitDetailWidget::SetModuleCustomizeWidgetForTesting(UMSModuleCustomizeWidget* InWidget) { ModuleCustomizeWidget = InWidget; if (ModuleCustomizeWidget) ModuleCustomizeWidget->SetReturnSuitDetailWidget(this); }
void UMSSuitDetailWidget::SetBattleAssistCustomizeWidgetForTesting(UMSBattleAssistCustomizeWidget* InWidget) { BattleAssistCustomizeWidget = InWidget; if (BattleAssistCustomizeWidget) BattleAssistCustomizeWidget->SetReturnSuitDetailWidget(this); }
#endif
