#include "UI/MSBattleAssistCustomizeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSMobileSuitData.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSPixelUI.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSVectorSkinWidget.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSSuitDetailWidget.h"

namespace MSBattleAssistCustomize
{
	const FLinearColor Background(0.004f, 0.012f, 0.022f, 0.96f);
	const FLinearColor TextPrimary(0.88f, 0.96f, 0.95f, 1.0f);
	const FLinearColor TextMuted(0.47f, 0.64f, 0.66f, 1.0f);
	const FLinearColor Accent(0.24f, 0.96f, 0.86f, 1.0f);
	const FLinearColor Selected(1.0f, 0.78f, 0.20f, 1.0f);
	const FLinearColor Warning(1.0f, 0.34f, 0.18f, 1.0f);

	void Place(UCanvasPanel* Canvas, UWidget* Child, const float X, const float Y, const float W, const float H)
	{
		if (!Canvas || !Child) return;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f)); Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(FVector2D(X, Y)); Slot->SetSize(FVector2D(W, H));
	}

	UOverlaySlot* Layer(UOverlay* Overlay, UWidget* Child, FMargin Padding = FMargin(0))
	{
		UOverlaySlot* Slot = Overlay->AddChildToOverlay(Child);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
		Slot->SetPadding(Padding);
		return Slot;
	}

	UTextBlock* Text(UWidgetTree* Tree, const FString& Value, const float Height,
		const FLinearColor& Color = TextPrimary, const FName Name = NAME_None)
	{
		UTextBlock* Result = MSPixelUI::Text(Tree, FText::FromString(Value), Height, Color, Name);
		Result->SetAutoWrapText(true); return Result;
	}

	UButton* Button(UWidgetTree* Tree, const FString& Label, const FName Name, const bool bEnabled = true)
	{
		UButton* Result = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Result);
		UTextBlock* LabelText = Text(Tree, Label, 17.0f, TextPrimary);
		LabelText->SetJustification(ETextJustify::Center);
		Result->SetContent(MSPixelUI::Frame(Tree, LabelText, EMSVectorSurface::Button, FMargin(8.0f, 3.0f), bEnabled ? 0 : 1));
		MSUIStyle::StretchButtonContent(Result); Result->SetIsEnabled(bEnabled); return Result;
	}

	bool InTab(const UMSBattleAssistModuleData* Module, const int32 Tab)
	{
		if (!Module) return false;
		auto Has = [Module](const EMSBattleAssistEffectKind Kind)
		{
			return Module->Effects.ContainsByPredicate([Kind](const FMSBattleAssistEffect& Effect) { return Effect.Kind == Kind; });
		};
		// The observed MSGO categories put movement/defense and shield attack
		// bonuses in 強化.  Lock-on/recovery assists belong in 補助 even when a
		// source row uses the broad Attack category.
		const bool bSupportUtility = Has(EMSBattleAssistEffectKind::MissileLockTimeReduction)
			|| Has(EMSBattleAssistEffectKind::MissileLockConeIncrease)
			|| Has(EMSBattleAssistEffectKind::MeleeHitRecoveryReduction);
		if (Has(EMSBattleAssistEffectKind::ShieldDamageIncrease)) return Tab == 0;
		if (bSupportUtility) return Tab == 2;
		switch (Tab)
		{
		case 0: return Module->Category == EMSModuleCategory::Attack || Module->Category == EMSModuleCategory::Defense || Module->Category == EMSModuleCategory::Movement;
		case 1: return Module->Category == EMSModuleCategory::Special;
		case 2: return Module->Category == EMSModuleCategory::Assist;
		case 3: return Module->Evidence == EMSModuleEvidence::Unknown || Module->Support != EMSModuleSupport::Implemented;
		default: return false;
		}
	}

	FString MaterialText(const TMap<FName, int32>& Materials)
	{
		FString Result;
		for (const TPair<FName, int32>& Pair : Materials)
		{
			if (!Result.IsEmpty()) Result += TEXT("、");
			const FString Key = Pair.Key.ToString();
			FString Label = Key;
			const TCHAR* Prefix = nullptr;
			if (Key.StartsWith(TEXT("chip_"))) { Prefix = TEXT("チップ★"); Label = Key.RightChop(5); }
			else if (Key.StartsWith(TEXT("blueprint_"))) { Prefix = TEXT("設計図★"); Label = Key.RightChop(10); }
			else if (Key.StartsWith(TEXT("capacitor_"))) { Prefix = TEXT("コンデンサ★"); Label = Key.RightChop(10); }
			else if (Key.StartsWith(TEXT("code_"))) { Prefix = TEXT("コード★"); Label = Key.RightChop(5); }
			else if (Key.StartsWith(TEXT("frame_"))) { Prefix = TEXT("フレーム加工材★"); Label = Key.RightChop(6); }
			Result += Prefix ? FString::Printf(TEXT("%s%s %d"), Prefix, *Label, Pair.Value) : FString::Printf(TEXT("%s %d"), *Label, Pair.Value);
		}
		return Result;
	}
}

void UMSBattleAssistCustomizeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); BindSubsystems(); RefreshAll(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSBattleAssistCustomizeWidget::BuildReferenceLayout()
{
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BattleAssistScaleToFit"));
	Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(1920.0f); Design->SetHeightOverride(1080.0f);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BattleAssistDesignCanvas"));
	Design->SetContent(Canvas); Fit->SetContent(Design); WidgetTree->RootWidget = Fit;
	MSBattleAssistCustomize::Place(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::HangarBackdrop, TEXT("BattleAssistHangar")), 0, 0, 1920, 1080);
	PreviewImage = WidgetTree->ConstructWidget<UImage>();
	PreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	MSBattleAssistCustomize::Place(Canvas, PreviewImage, 530, 30, 780, 1030);
	if (GetWorld()) { PreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this); PreviewRenderer->Initialize(GetWorld(), PreviewImage); PreviewRenderer->ConfigureCustomizeView(); }
	MSBattleAssistCustomize::Place(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::BattleModulePanel, TEXT("BattleAssistPanel")), 226, 163, 1500, 692);

	HeaderText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("モジュール  module customize"), 29.0f, MSBattleAssistCustomize::Accent, TEXT("BattleAssistHeaderText"));
	MSBattleAssistCustomize::Place(Canvas, HeaderText, 250, 184, 620, 31);
	SuitText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("機体 ---"), 16.0f, MSBattleAssistCustomize::TextMuted, TEXT("BattleAssistSuitText"));
	MSBattleAssistCustomize::Place(Canvas, SuitText, 1130, 184, 550, 26);

	const TCHAR* TabLabels[] = { TEXT("強化"), TEXT("特殊"), TEXT("補助"), TEXT("その他") };
	for (int32 Index = 0; Index < TabCount; ++Index)
	{
		UButton* Tab = MSBattleAssistCustomize::Button(WidgetTree, TabLabels[Index], *FString::Printf(TEXT("BattleAssistTab%d"), Index));
		TabButtons.Add(Tab); MSBattleAssistCustomize::Place(Canvas, Tab, 250.0f + Index * 100.0f, 249.0f, 96.0f, 30.0f);
	}
	TabButtons[0]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleTab0Clicked);
	TabButtons[1]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleTab1Clicked);
	TabButtons[2]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleTab2Clicked);
	TabButtons[3]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleTab3Clicked);

	MSBattleAssistCustomize::Place(Canvas, MSBattleAssistCustomize::Text(WidgetTree, TEXT("装備したモジュールの総スロット数に応じて再出撃時間が増えます"), 21, MSBattleAssistCustomize::Accent), 875, 250, 820, 30);
	MSBattleAssistCustomize::Place(Canvas, MSBattleAssistCustomize::Text(WidgetTree, TEXT("モジュールスロット"), 27, MSBattleAssistCustomize::Accent), 875, 283, 820, 35);
	for (int32 Row = 0; Row < RowsPerPage; ++Row)
	{
		UButton* RowButton = MSBattleAssistCustomize::Button(WidgetTree, TEXT(""), *FString::Printf(TEXT("BattleAssistModuleRow%d"), Row));
		MSPixelUI::ClearButtonBrush(RowButton);
		UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>();
		UMSVectorSkinWidget* Skin = MSPixelUI::Skin(WidgetTree, EMSVectorSurface::BattleModulePanel, *FString::Printf(TEXT("BattleAssistModuleSkin%d"), Row));
		MSBattleAssistCustomize::Layer(Content, Skin); ModuleSkins.Add(Skin);
		UMSVectorSkinWidget* IconSkin = MSPixelUI::Skin(WidgetTree, EMSVectorSurface::BattleModuleIcon, *FString::Printf(TEXT("BattleAssistModuleIcon%d"), Row));
		MSBattleAssistCustomize::Layer(Content, IconSkin, FMargin(8, 5, 416, 5)); ModuleIconSkins.Add(IconSkin);
		UTextBlock* Name = MSBattleAssistCustomize::Text(WidgetTree, TEXT("---"), 22.0f, MSBattleAssistCustomize::TextPrimary, *FString::Printf(TEXT("BattleAssistModuleName%d"), Row));
		Name->SetAutoWrapText(false); Name->SetClipping(EWidgetClipping::ClipToBounds); MSBattleAssistCustomize::Layer(Content, Name, FMargin(155, 5, 10, 29)); ModuleNameTexts.Add(Name);
		UTextBlock* State = MSBattleAssistCustomize::Text(WidgetTree, TEXT(""), 18.0f, MSBattleAssistCustomize::TextMuted, *FString::Printf(TEXT("BattleAssistModuleState%d"), Row));
		State->SetAutoWrapText(false); MSBattleAssistCustomize::Layer(Content, State, FMargin(155, 37, 10, 5)); ModuleStateTexts.Add(State);
		RowButton->SetContent(Content); MSUIStyle::StretchButtonContent(RowButton); ModuleButtons.Add(RowButton);
		MSBattleAssistCustomize::Place(Canvas, RowButton, 250, 289 + Row * 70, 566, 65);
	}
	ModuleButtons[0]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow0Clicked);
	ModuleButtons[1]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow1Clicked);
	ModuleButtons[2]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow2Clicked);
	ModuleButtons[3]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow3Clicked);
	ModuleButtons[4]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow4Clicked);
	ModuleButtons[5]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRow5Clicked);

	SuitText = CastChecked<UTextBlock>(GetWidgetFromName(TEXT("BattleAssistSuitText")));
	DescriptionText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("モジュールを選択してください。"), 17.0f, MSBattleAssistCustomize::TextPrimary, TEXT("BattleAssistDescriptionText"));
	MSBattleAssistCustomize::Place(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::Panel), 875, 496, 820, 180);
	SelectedNameText = MSBattleAssistCustomize::Text(WidgetTree, TEXT(""), 24, MSBattleAssistCustomize::TextPrimary, TEXT("BattleAssistSelectedName"));
	MSBattleAssistCustomize::Place(Canvas, SelectedNameText, 890, 500, 790, 35);
	DescriptionText->SetAutoWrapText(true); MSBattleAssistCustomize::Place(Canvas, DescriptionText, 898, 543, 775, 65);
	ReasonText = MSBattleAssistCustomize::Text(WidgetTree, TEXT(""), 14.0f, MSBattleAssistCustomize::TextMuted, TEXT("BattleAssistReasonText"));
	ReasonText->SetAutoWrapText(true); MSBattleAssistCustomize::Place(Canvas, ReasonText, 875, 602, 820, 66);
	TimeText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("TIME ---"), 25.0f, FLinearColor(1.0f, .82f, .28f), TEXT("BattleAssistTimeText"));
	MSBattleAssistCustomize::Place(Canvas, TimeText, 1405, 311, 300, 32);
	SlotText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("SLOTS 0/0"), 15.0f, MSBattleAssistCustomize::Accent, TEXT("BattleAssistSlotText"));
	MSBattleAssistCustomize::Place(Canvas, SlotText, 1120, 316, 280, 26);
	SlotModuleIds.SetNum(SlotDisplayCount);
	for (int32 SlotIndexLocal = 0; SlotIndexLocal < SlotDisplayCount; ++SlotIndexLocal)
	{
		UMSVectorSkinWidget* SlotSkin = MSPixelUI::Skin(WidgetTree, EMSVectorSurface::BattleModuleIcon, *FString::Printf(TEXT("BattleAssistSlotIcon%d"), SlotIndexLocal));
		SlotSkins.Add(SlotSkin);
		UButton* SlotButton = WidgetTree->ConstructWidget<UButton>(); MSPixelUI::ClearButtonBrush(SlotButton);
		UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
		UMSVectorSkinWidget* Frame = MSPixelUI::Skin(WidgetTree, EMSVectorSurface::Panel); SlotFrames.Add(Frame);
		MSBattleAssistCustomize::Layer(Layers, Frame); MSBattleAssistCustomize::Layer(Layers, SlotSkin, FMargin(4));
		SlotButton->SetContent(Layers); MSUIStyle::StretchButtonContent(SlotButton); SlotButtons.Add(SlotButton);
		MSBattleAssistCustomize::Place(Canvas, SlotButton, 885 + SlotIndexLocal * 80.5, 343, 78, 80);
	}
	SlotButtons[0]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot0Clicked);
	SlotButtons[1]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot1Clicked);
	SlotButtons[2]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot2Clicked);
	SlotButtons[3]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot3Clicked);
	SlotButtons[4]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot4Clicked);
	SlotButtons[5]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot5Clicked);
	SlotButtons[6]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot6Clicked);
	SlotButtons[7]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot7Clicked);
	SlotButtons[8]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot8Clicked);
	SlotButtons[9]->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleSlot9Clicked);
	PageText = MSBattleAssistCustomize::Text(WidgetTree, TEXT("1 / 1"), 15.0f, MSBattleAssistCustomize::TextPrimary, TEXT("BattleAssistPageText"));
	PageText->SetJustification(ETextJustify::Center); MSBattleAssistCustomize::Place(Canvas, PageText, 478, 726, 100, 25);
	UButton* Previous = MSBattleAssistCustomize::Button(WidgetTree, TEXT("‹"), TEXT("BattleAssistPreviousPageButton"));
	UButton* Next = MSBattleAssistCustomize::Button(WidgetTree, TEXT("›"), TEXT("BattleAssistNextPageButton"));
	Previous->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandlePreviousPageClicked); Next->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleNextPageClicked);
	MSBattleAssistCustomize::Place(Canvas, Previous, 394, 724, 78, 28); MSBattleAssistCustomize::Place(Canvas, Next, 595, 724, 78, 28);

	EquipButton = MSBattleAssistCustomize::Button(WidgetTree, TEXT("装備"), TEXT("BattleAssistEquipButton"));
	RemoveButton = MSBattleAssistCustomize::Button(WidgetTree, TEXT("解除"), TEXT("BattleAssistRemoveButton"));
	ClearButton = MSBattleAssistCustomize::Button(WidgetTree, TEXT("全解除"), TEXT("BattleAssistClearButton"));
	DevelopButton = MSBattleAssistCustomize::Button(WidgetTree, TEXT("開発"), TEXT("BattleAssistDevelopButton"));
	EquipButton->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleEquipClicked);
	RemoveButton->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleRemoveClicked);
	ClearButton->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleClearClicked);
	DevelopButton->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleDevelopClicked);
	MSBattleAssistCustomize::Place(Canvas, EquipButton, 250, 774, 130, 44); MSBattleAssistCustomize::Place(Canvas, RemoveButton, 386, 774, 130, 44);
	MSBattleAssistCustomize::Place(Canvas, ClearButton, 546, 774, 270, 44); MSBattleAssistCustomize::Place(Canvas, DevelopButton, 1460, 773, 235, 44);
	WalletText = MSBattleAssistCustomize::Text(WidgetTree, TEXT(""), 17, MSBattleAssistCustomize::TextPrimary, TEXT("BattleAssistWalletText"));
	MSBattleAssistCustomize::Place(Canvas, WalletText, 895, 694, 785, 70);
	UButton* Back = MSBattleAssistCustomize::Button(WidgetTree, TEXT("戻る"), TEXT("BattleAssistBackButton"));
	Back->OnClicked.AddDynamic(this, &UMSBattleAssistCustomizeWidget::HandleBackClicked); MSBattleAssistCustomize::Place(Canvas, Back, 1553, 124, 246, 43);
}

void UMSBattleAssistCustomizeWidget::BindSubsystems()
{
	UMSDeckSubsystem* NewDeck = ResolveDeckSubsystem();
	if (DeckSubsystem != NewDeck)
	{
		if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleDeckChanged);
		DeckSubsystem = NewDeck;
		if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSBattleAssistCustomizeWidget::HandleDeckChanged);
	}
	UMSBattleAssistProgressSubsystem* NewProgress = ResolveProgressSubsystem();
	if (ProgressSubsystem != NewProgress)
	{
		if (ProgressSubsystem)
		{
			ProgressSubsystem->OnProgressChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleProgressChanged);
			ProgressSubsystem->OnOperationCompleted.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOperationCompleted);
		}
		ProgressSubsystem = NewProgress;
		if (ProgressSubsystem)
		{
			ProgressSubsystem->OnProgressChanged.AddUniqueDynamic(this, &UMSBattleAssistCustomizeWidget::HandleProgressChanged);
			ProgressSubsystem->OnOperationCompleted.AddUniqueDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOperationCompleted);
		}
	}
	UMSOnlineAccountSubsystem* NewOnline = OnlineAccountSubsystem.Get();
	if (UGameInstance* GI = GetGameInstance()) NewOnline = GI->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (OnlineAccountSubsystem.Get() != NewOnline)
	{
		if (OnlineAccountSubsystem.IsValid()) OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged);
		OnlineAccountSubsystem = NewOnline;
	}
	if (OnlineAccountSubsystem.IsValid()) OnlineAccountSubsystem->OnAccountChanged.AddUniqueDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged);
}

UMSDeckSubsystem* UMSBattleAssistCustomizeWidget::ResolveDeckSubsystem() const
{
	return DeckSubsystem ? DeckSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr);
}

UMSBattleAssistProgressSubsystem* UMSBattleAssistCustomizeWidget::ResolveProgressSubsystem() const
{
	return ProgressSubsystem ? ProgressSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>() : nullptr);
}

FString MSBattleAssistUI::ResolveEffectiveTime(const UMSMobileSuitData* InSuit,
	const FMSBattleAssistLoadout& Loadout, const FMSBattleAssistProgress* Progress)
{
	if (!InSuit) return TEXT("---");
	if (Loadout.IsEmpty())
	{
		float Seconds = 0.0f;
		return MSBattleAssist::ResolveReuseDelaySeconds(InSuit, 0, 0, Seconds) ? FString::FromInt(FMath::RoundToInt(Seconds)) : TEXT("---");
	}
	FMSBattleAssistValidationResult Validation;
	if (Progress && !MSBattleAssist::ValidateLoadout(InSuit, Loadout, Progress, Validation)) return TEXT("---");
	FMSBattleAssistResolvedLoadout Resolved;
	if (!MSBattleAssist::ResolveLoadout(InSuit, Loadout, Resolved, Validation)) return TEXT("---");
	return FMath::IsFinite(Resolved.EffectiveReuseSeconds) ? FString::FromInt(FMath::RoundToInt(Resolved.EffectiveReuseSeconds)) : TEXT("---");
}

void UMSBattleAssistCustomizeWidget::Open(UMSMobileSuitData* InSuit, const int32 InSlotIndex)
{
	++OperationGeneration; bPendingCommit = false; PendingAccountGeneration = INDEX_NONE; BindSubsystems(); Suit = InSuit; SlotIndex = InSlotIndex; ActiveTab = 0; PageIndex = 0; SelectedRow = 0;
	WorkingLoadout = DeckSubsystem ? DeckSubsystem->GetLoadoutInSlot(InSlotIndex).BattleAssist : FMSBattleAssistLoadout();
	if (PreviewRenderer) PreviewRenderer->SetMobileSuit(InSuit, DeckSubsystem ? DeckSubsystem->GetLoadoutInSlot(InSlotIndex).bShieldEquipped : true);
	RefreshAll();
	if (GetWorld() && !IsInViewport()) AddToViewport(95);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("BattleAssist"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSBattleAssistCustomizeWidget::Close()
{
	if (!IsOpen()) return;
	++OperationGeneration; bPendingCommit = false; PendingAccountGeneration = INDEX_NONE;
	SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("BattleAssist"));
	if (ReturnSuitDetailWidget) ReturnSuitDetailWidget->HandleBattleAssistCustomizeClosed();
}

void UMSBattleAssistCustomizeWidget::RefreshAll()
{
	if (SuitText) SuitText->SetText(FText::FromString(Suit ? FString::Printf(TEXT("機体  %s"), *Suit->UnitName.ToString()) : TEXT("機体 ---")));
	RefreshCatalog(); RefreshRows(); RefreshSelection(); RefreshSlotStrip(); RefreshActions(); RefreshTime(); RefreshProgressState();
}

void UMSBattleAssistCustomizeWidget::RefreshCatalog()
{
	FilteredModules.Reset();
	for (UMSBattleAssistModuleData* Module : MSBattleAssist::GetCatalog()) if (MSBattleAssistCustomize::InTab(Module, ActiveTab)) FilteredModules.Add(Module);
	const int32 MaxPage = FMath::Max(0, (FilteredModules.Num() - 1) / RowsPerPage);
	PageIndex = FMath::Clamp(PageIndex, 0, MaxPage);
	if (!FilteredModules.IsValidIndex(PageIndex * RowsPerPage + FMath::Max(0, SelectedRow))) SelectedRow = FilteredModules.IsValidIndex(PageIndex * RowsPerPage) ? 0 : INDEX_NONE;
}

UMSBattleAssistModuleData* UMSBattleAssistCustomizeWidget::GetSelectedModule() const
{
	return (SelectedRow >= 0 && SelectedRow < RowsPerPage && FilteredModules.IsValidIndex(PageIndex * RowsPerPage + SelectedRow))
		? FilteredModules[PageIndex * RowsPerPage + SelectedRow] : nullptr;
}

bool UMSBattleAssistCustomizeWidget::IsModuleSupported(const UMSBattleAssistModuleData* Module) const
{
	return Module && Module->Support == EMSModuleSupport::Implemented && Module->Evidence == EMSModuleEvidence::Confirmed && !Module->FamilyId.IsNone() && !Module->Effects.IsEmpty();
}

bool UMSBattleAssistCustomizeWidget::IsModuleDeveloped(const UMSBattleAssistModuleData* Module) const
{
	if (!Module) return false;
	const FMSBattleAssistSuitProfile* Profile = Suit ? MSBattleAssist::GetSuitProfile(Suit->GetPrimaryAssetId()) : nullptr;
	if (Profile && Profile->NativeBattleAssistModuleIds.Contains(Module->GetPrimaryAssetId())) return true;
	return ProgressSubsystem && ProgressSubsystem->GetProgress().DevelopedModuleIds.Contains(Module->GetPrimaryAssetId());
}

FString UMSBattleAssistCustomizeWidget::ModuleRequirementsText(const UMSBattleAssistModuleData* Module) const
{
	if (!Module || !Module->bRecipeKnown) return TEXT("開発レシピ未確認");
	const FString Materials = MSBattleAssistCustomize::MaterialText(Module->DevelopmentMaterials);
	return FString::Printf(TEXT("必要 GP %lld%s"), Module->DevelopmentCostGP, Materials.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" / %s"), *Materials));
}

FString UMSBattleAssistCustomizeWidget::ModuleStateText(const UMSBattleAssistModuleData* Module) const
{
	if (!Module) return TEXT("");
	const FString State = !IsModuleSupported(Module) ? TEXT("利用不可") : WorkingLoadout.ModuleIds.Contains(Module->GetPrimaryAssetId()) ? TEXT("装備中  E") : !IsModuleDeveloped(Module) ? TEXT("未開発  LOCK") : TEXT("開発済み");
	return FString::Printf(TEXT("スロット %d     %s"), Module->SlotCost, *State);
}

void UMSBattleAssistCustomizeWidget::RefreshRows()
{
	for (int32 Row = 0; Row < RowsPerPage; ++Row)
	{
		const int32 ModuleIndex = PageIndex * RowsPerPage + Row;
		UMSBattleAssistModuleData* Module = FilteredModules.IsValidIndex(ModuleIndex) ? FilteredModules[ModuleIndex] : nullptr;
		if (ModuleNameTexts.IsValidIndex(Row)) ModuleNameTexts[Row]->SetText(Module ? Module->DisplayName : FText::FromString(TEXT("---")));
		if (ModuleStateTexts.IsValidIndex(Row)) ModuleStateTexts[Row]->SetText(Module ? FText::FromString(ModuleStateText(Module)) : FText::GetEmpty());
		if (ModuleIconSkins.IsValidIndex(Row)) ModuleIconSkins[Row]->SetVisibility(Module ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (Module && ModuleIconSkins.IsValidIndex(Row)) ModuleIconSkins[Row]->SetSurface(EMSVectorSurface::BattleModuleIcon, Module->Rank);
		if (ModuleButtons.IsValidIndex(Row)) ModuleButtons[Row]->SetIsEnabled(Module != nullptr);
	}
	if (PageText) PageText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), PageIndex + 1, FMath::Max(1, (FilteredModules.Num() + RowsPerPage - 1) / RowsPerPage))));
}

void UMSBattleAssistCustomizeWidget::RefreshSelection()
{
	for (int32 Row = 0; Row < ModuleSkins.Num(); ++Row)
	{
		const bool bSelected = Row == SelectedRow;
		ModuleSkins[Row]->SetAccent(bSelected ? MSBattleAssistCustomize::Selected : MSBattleAssistCustomize::Accent);
		if (ModuleIconSkins.IsValidIndex(Row)) ModuleIconSkins[Row]->SetAccent(bSelected ? MSBattleAssistCustomize::Selected : MSBattleAssistCustomize::Accent);
	}
	UMSBattleAssistModuleData* Module = GetSelectedModule();
	if (SelectedNameText) SelectedNameText->SetText(Module ? FText::FromString(FString::Printf(TEXT("%s     スロット %d"), *Module->DisplayName.ToString(), Module->SlotCost)) : FText::GetEmpty());
	if (!Module)
	{
		if (DescriptionText) DescriptionText->SetText(FText::FromString(TEXT("モジュールを選択してください。")));
		if (ReasonText) ReasonText->SetText(FText::GetEmpty()); return;
	}
	if (DescriptionText) DescriptionText->SetText(Module->Description.IsEmpty() ? FText::FromString(TEXT("説明未確認")) : Module->Description);
	if (ReasonText)
	{
		if (!IsModuleSupported(Module)) ReasonText->SetText(FText::FromString(TEXT("閲覧のみ: このモジュールの効果または根拠は未実装です。装備・開発はできません。")));
		else if (!IsModuleDeveloped(Module)) ReasonText->SetText(FText::FromString(ModuleRequirementsText(Module)));
		else ReasonText->SetText(FText::FromString(TEXT("装備すると右側の再出撃時間プレビューに反映されます。")));
	}
}

void UMSBattleAssistCustomizeWidget::RefreshSlotStrip()
{
	const auto* Profile = Suit ? MSBattleAssist::GetSuitProfile(Suit->GetPrimaryAssetId()) : nullptr;
	const int32 Capacity = Profile ? Profile->BattleAssistSlotCapacity : 0;
	int32 Used = 0;
	for (const FPrimaryAssetId& Id : WorkingLoadout.ModuleIds)
		if (const UMSBattleAssistModuleData* Module = MSBattleAssist::FindModule(Id)) Used += FMath::Max(0, Module->SlotCost);
	for (int32 Index = 0; Index < SlotSkins.Num(); ++Index)
	{
		SlotSkins[Index]->SetVisibility(ESlateVisibility::Hidden); SlotModuleIds[Index] = FPrimaryAssetId();
		SlotButtons[Index]->SetIsEnabled(false);
		SlotFrames[Index]->SetRenderOpacity(Index < Capacity ? 1.f : .3f);
		SlotButtons[Index]->SetToolTipText(FText::FromString(Index < Capacity ? TEXT("空きスロット") : TEXT("使用できないスロット")));
	}
	int32 SlotCursor = 0;
	for (const FPrimaryAssetId& Id : WorkingLoadout.ModuleIds)
	{
		const UMSBattleAssistModuleData* Module = MSBattleAssist::FindModule(Id); if (!Module) continue;
		for (int32 Count = 0; Count < Module->SlotCost && SlotSkins.IsValidIndex(SlotCursor); ++Count, ++SlotCursor)
		{
			SlotSkins[SlotCursor]->SetVisibility(ESlateVisibility::HitTestInvisible);
			SlotSkins[SlotCursor]->SetAccent(MSBattleAssistCustomize::Accent);
			SlotSkins[SlotCursor]->SetSurface(EMSVectorSurface::BattleModuleIcon, Module->Rank);
			SlotButtons[SlotCursor]->SetIsEnabled(true); SlotButtons[SlotCursor]->SetToolTipText(Module->DisplayName);
			SlotModuleIds[SlotCursor] = Id;
		}
	}
	if (SlotText) SlotText->SetText(FText::FromString(FString::Printf(TEXT("スロット数  %d / %d"), Used, Capacity)));
}

void UMSBattleAssistCustomizeWidget::SelectEquippedSlot(int32 Index)
{
	if (!SlotModuleIds.IsValidIndex(Index) || !SlotModuleIds[Index].IsValid()) return;
	const auto Id = SlotModuleIds[Index];
	const auto* Module = MSBattleAssist::FindModule(Id);
	for (int32 Tab = 0; Tab < TabCount; ++Tab)
		if (MSBattleAssistCustomize::InTab(Module,Tab)) { ActiveTab = Tab; break; }
	RefreshCatalog();
	const int32 Item = FilteredModules.IndexOfByPredicate([&](const auto& M){return M && M->GetPrimaryAssetId()==Id;});
	if (Item == INDEX_NONE) return;
	PageIndex = Item / RowsPerPage; SelectedRow = Item % RowsPerPage; RefreshAll();
}

void UMSBattleAssistCustomizeWidget::RefreshTime()
{
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (ProgressSubsystem) { ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue; }
	const FString Time = MSBattleAssistUI::ResolveEffectiveTime(Suit, WorkingLoadout, Progress);
	FString Preview = Time;
	if (const auto* Module = GetSelectedModule(); Module && IsModuleSupported(Module))
	{
		FMSBattleAssistLoadout Candidate = WorkingLoadout;
		if (Candidate.ModuleIds.Contains(Module->GetPrimaryAssetId())) Candidate.ModuleIds.Remove(Module->GetPrimaryAssetId());
		else Candidate.ModuleIds.Add(Module->GetPrimaryAssetId());
		const FString Next = MSBattleAssistUI::ResolveEffectiveTime(Suit,Candidate,nullptr);
		if (Time != TEXT("---") && Next != TEXT("---") && Time != Next)
			Preview = FString::Printf(TEXT("%s → %s"), *Time, *Next);
	}
	if (TimeText) TimeText->SetText(FText::FromString(FString::Printf(TEXT("TIME %s"), *Preview)));
}

void UMSBattleAssistCustomizeWidget::RefreshProgressState()
{
	const bool bPending = bPendingCommit || (ProgressSubsystem && ProgressSubsystem->IsPending());
	if (HeaderText) HeaderText->SetColorAndOpacity(bPending ? MSBattleAssistCustomize::Warning : MSBattleAssistCustomize::Accent);
	if (WalletText && ProgressSubsystem)
	{
		const auto P = ProgressSubsystem->GetProgress(); FString Line = FString::Printf(TEXT("所持 GP  %lld"),P.GP);
		if (const auto* M=GetSelectedModule(); M && M->bRecipeKnown && !IsModuleDeveloped(M))
		{
			TMap<FName,int32> Owned; for (const auto& Pair:M->DevelopmentMaterials) Owned.Add(Pair.Key,P.Materials.FindRef(Pair.Key));
			Line += TEXT("\n所持素材  ") + MSBattleAssistCustomize::MaterialText(Owned);
		}
		WalletText->SetText(FText::FromString(Line));
	}
}

void UMSBattleAssistCustomizeWidget::RefreshActions()
{
	const bool bPending = bPendingCommit || (ProgressSubsystem && ProgressSubsystem->IsPending());
	UMSBattleAssistModuleData* Module = GetSelectedModule();
	const bool bSupported = IsModuleSupported(Module); const bool bDeveloped = IsModuleDeveloped(Module);
	const bool bEquipped = Module && WorkingLoadout.ModuleIds.Contains(Module->GetPrimaryAssetId());
	bool bCanEquip = !bPending && bSupported && bDeveloped && !bEquipped;
	if (bCanEquip)
	{
		FMSBattleAssistLoadout Candidate=WorkingLoadout; Candidate.ModuleIds.Add(Module->GetPrimaryAssetId());
		FMSBattleAssistValidationResult Error; bCanEquip=MSBattleAssist::ValidateLoadout(Suit,Candidate,nullptr,Error);
		if (!bCanEquip && ReasonText) ReasonText->SetText(Error.Reason);
	}
	if (EquipButton) EquipButton->SetIsEnabled(bCanEquip);
	if (RemoveButton) RemoveButton->SetIsEnabled(!bPending && bEquipped);
	if (ClearButton) ClearButton->SetIsEnabled(!bPending && !WorkingLoadout.IsEmpty());
	bool bCanDevelop = !bPending && bSupported && Module && Module->bRecipeKnown && !bDeveloped;
	if (bCanDevelop && ProgressSubsystem)
	{
		const FMSBattleAssistProgress& Progress = ProgressSubsystem->GetProgress();
		bCanDevelop = Progress.GP >= Module->DevelopmentCostGP;
		for (const TPair<FName, int32>& Pair : Module->DevelopmentMaterials) if (Progress.Materials.FindRef(Pair.Key) < Pair.Value) bCanDevelop = false;
	}
	if (DevelopButton) DevelopButton->SetIsEnabled(bCanDevelop);
}

void UMSBattleAssistCustomizeWidget::SetTab(const int32 TabIndex)
{
	ActiveTab = FMath::Clamp(TabIndex, 0, TabCount - 1); PageIndex = 0; SelectedRow = 0; RefreshAll();
}

void UMSBattleAssistCustomizeWidget::SelectRow(const int32 RowIndex)
{
	const int32 ModuleIndex = PageIndex * RowsPerPage + RowIndex;
	if (!FilteredModules.IsValidIndex(ModuleIndex)) return;
	SelectedRow = RowIndex; RefreshSelection(); RefreshActions(); RefreshTime(); RefreshProgressState();
}

void UMSBattleAssistCustomizeWidget::CyclePage(const int32 Direction)
{
	const int32 PageCount = FMath::Max(1, (FilteredModules.Num() + RowsPerPage - 1) / RowsPerPage);
	PageIndex = (PageIndex + Direction + PageCount) % PageCount; SelectedRow = INDEX_NONE; RefreshRows(); RefreshSelection(); RefreshActions(); RefreshTime(); RefreshProgressState();
}

void UMSBattleAssistCustomizeWidget::CommitCandidate(const FMSBattleAssistLoadout& Candidate)
{
	if (bPendingCommit || !DeckSubsystem || !Suit) return;
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (ProgressSubsystem) { ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue; }
	FMSBattleAssistValidationResult Validation;
	if (!MSBattleAssist::ValidateLoadout(Suit, Candidate, Progress, Validation))
	{
		if (ReasonText) ReasonText->SetText(Validation.Reason);
		return;
	}
	bPendingCommit = true; RefreshProgressState(); RefreshActions();
	const uint64 RequestGeneration = OperationGeneration;
	const int32 RequestSlotIndex = SlotIndex;
	const TWeakObjectPtr<UMSMobileSuitData> RequestSuit = Suit;
	PendingAccountGeneration = GetCurrentAccountGeneration();
	const int64 RequestAccountGeneration = PendingAccountGeneration;
	const TWeakObjectPtr<UMSBattleAssistCustomizeWidget> WeakThis(this);
	DeckSubsystem->RequestBattleAssistLoadout(SlotIndex, Candidate, [WeakThis, Candidate, RequestGeneration, RequestSlotIndex, RequestSuit, RequestAccountGeneration](const bool bSuccess)
	{
		if (!WeakThis.IsValid()) return;
		WeakThis->HandleCommitCompletion(Candidate, RequestGeneration, RequestSlotIndex, RequestSuit, RequestAccountGeneration, bSuccess);
	});
}

int64 UMSBattleAssistCustomizeWidget::GetCurrentAccountGeneration() const
{
	return OnlineAccountSubsystem.IsValid() && OnlineAccountSubsystem->IsOnlineMode()
		? OnlineAccountSubsystem->GetAuthGeneration() : INDEX_NONE;
}

void UMSBattleAssistCustomizeWidget::HandleCommitCompletion(const FMSBattleAssistLoadout& Candidate,
	const uint64 RequestGeneration, const int32 RequestSlotIndex,
	const TWeakObjectPtr<UMSMobileSuitData> RequestSuit, const int64 RequestAccountGeneration,
	const bool bSuccess)
{
	if (OperationGeneration != RequestGeneration || SlotIndex != RequestSlotIndex || Suit != RequestSuit.Get()) return;
	if (RequestAccountGeneration != INDEX_NONE && GetCurrentAccountGeneration() != RequestAccountGeneration)
	{
		CancelPendingAccountChange();
		return;
	}
	bPendingCommit = false; PendingAccountGeneration = INDEX_NONE;
	if (bSuccess)
	{
		WorkingLoadout = Candidate;
		RefreshAll();
	}
	else if (ReasonText) ReasonText->SetText(FText::FromString(TEXT("装備変更を保存できませんでした。現在の装備を維持します。")));
	RefreshProgressState(); RefreshActions();
}

void UMSBattleAssistCustomizeWidget::CancelPendingAccountChange()
{
	if (!bPendingCommit) return;
	++OperationGeneration;
	bPendingCommit = false; PendingAccountGeneration = INDEX_NONE;
	if (DeckSubsystem && DeckSubsystem->GetLoadoutInSlot(SlotIndex).BattleAssist.IsEmpty()) WorkingLoadout = FMSBattleAssistLoadout();
	else if (DeckSubsystem) WorkingLoadout = DeckSubsystem->GetLoadoutInSlot(SlotIndex).BattleAssist;
	RefreshAll();
	if (ReasonText) ReasonText->SetText(FText::FromString(TEXT("アカウントが切り替わったため、保存結果を破棄しました。")));
	RefreshProgressState(); RefreshActions();
}

void UMSBattleAssistCustomizeWidget::TryEquipSelected()
{
	UMSBattleAssistModuleData* Module = GetSelectedModule();
	if (!Module || WorkingLoadout.ModuleIds.Contains(Module->GetPrimaryAssetId())) return;
	FMSBattleAssistLoadout Candidate = WorkingLoadout; Candidate.ModuleIds.Add(Module->GetPrimaryAssetId()); CommitCandidate(Candidate);
}

void UMSBattleAssistCustomizeWidget::RemoveSelected()
{
	UMSBattleAssistModuleData* Module = GetSelectedModule();
	if (!Module) return;
	FMSBattleAssistLoadout Candidate = WorkingLoadout; Candidate.ModuleIds.Remove(Module->GetPrimaryAssetId()); CommitCandidate(Candidate);
}

void UMSBattleAssistCustomizeWidget::ClearLoadout()
{
	if (!WorkingLoadout.IsEmpty()) CommitCandidate(FMSBattleAssistLoadout());
}

void UMSBattleAssistCustomizeWidget::DevelopSelected()
{
	UMSBattleAssistModuleData* Module = GetSelectedModule();
	if (!Module || !ProgressSubsystem || !Module->bRecipeKnown || IsModuleDeveloped(Module)) return;
	if (ProgressSubsystem->TryDevelopModule(Module->GetPrimaryAssetId())) { RefreshProgressState(); RefreshActions(); }
}

void UMSBattleAssistCustomizeWidget::HandleBackClicked() { Close(); }
void UMSBattleAssistCustomizeWidget::HandleSlot0Clicked() { SelectEquippedSlot(0); }
void UMSBattleAssistCustomizeWidget::HandleSlot1Clicked() { SelectEquippedSlot(1); }
void UMSBattleAssistCustomizeWidget::HandleSlot2Clicked() { SelectEquippedSlot(2); }
void UMSBattleAssistCustomizeWidget::HandleSlot3Clicked() { SelectEquippedSlot(3); }
void UMSBattleAssistCustomizeWidget::HandleSlot4Clicked() { SelectEquippedSlot(4); }
void UMSBattleAssistCustomizeWidget::HandleSlot5Clicked() { SelectEquippedSlot(5); }
void UMSBattleAssistCustomizeWidget::HandleSlot6Clicked() { SelectEquippedSlot(6); }
void UMSBattleAssistCustomizeWidget::HandleSlot7Clicked() { SelectEquippedSlot(7); }
void UMSBattleAssistCustomizeWidget::HandleSlot8Clicked() { SelectEquippedSlot(8); }
void UMSBattleAssistCustomizeWidget::HandleSlot9Clicked() { SelectEquippedSlot(9); }
void UMSBattleAssistCustomizeWidget::HandleTab0Clicked() { SetTab(0); }
void UMSBattleAssistCustomizeWidget::HandleTab1Clicked() { SetTab(1); }
void UMSBattleAssistCustomizeWidget::HandleTab2Clicked() { SetTab(2); }
void UMSBattleAssistCustomizeWidget::HandleTab3Clicked() { SetTab(3); }
void UMSBattleAssistCustomizeWidget::HandleRow0Clicked() { SelectRow(0); }
void UMSBattleAssistCustomizeWidget::HandleRow1Clicked() { SelectRow(1); }
void UMSBattleAssistCustomizeWidget::HandleRow2Clicked() { SelectRow(2); }
void UMSBattleAssistCustomizeWidget::HandleRow3Clicked() { SelectRow(3); }
void UMSBattleAssistCustomizeWidget::HandleRow4Clicked() { SelectRow(4); }
void UMSBattleAssistCustomizeWidget::HandleRow5Clicked() { SelectRow(5); }
void UMSBattleAssistCustomizeWidget::HandlePreviousPageClicked() { CyclePage(-1); }
void UMSBattleAssistCustomizeWidget::HandleNextPageClicked() { CyclePage(1); }
void UMSBattleAssistCustomizeWidget::HandleEquipClicked() { TryEquipSelected(); }
void UMSBattleAssistCustomizeWidget::HandleRemoveClicked() { RemoveSelected(); }
void UMSBattleAssistCustomizeWidget::HandleClearClicked() { ClearLoadout(); }
void UMSBattleAssistCustomizeWidget::HandleDevelopClicked() { DevelopSelected(); }
void UMSBattleAssistCustomizeWidget::HandleProgressChanged() { RefreshAll(); }
void UMSBattleAssistCustomizeWidget::HandleOperationCompleted(bool bSuccess, FText Message)
{
	bPendingCommit = false; RefreshAll();
	if (!bSuccess && ReasonText) ReasonText->SetText(Message.IsEmpty() ? FText::FromString(TEXT("開発に失敗しました。残高と材料を確認してください。")) : Message);
}
void UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged(FMSOnlineIdentity, const int64 Generation)
{
	if (bPendingCommit && PendingAccountGeneration != INDEX_NONE && Generation != PendingAccountGeneration) CancelPendingAccountChange();
}
void UMSBattleAssistCustomizeWidget::HandleDeckChanged(int32 ChangedSlotIndex)
{
	if (bPendingCommit && PendingAccountGeneration != INDEX_NONE && GetCurrentAccountGeneration() != PendingAccountGeneration)
	{
		CancelPendingAccountChange();
		return;
	}
	if (ChangedSlotIndex == INDEX_NONE || ChangedSlotIndex == SlotIndex)
	{
		if (DeckSubsystem) WorkingLoadout = DeckSubsystem->GetLoadoutInSlot(SlotIndex).BattleAssist;
		RefreshAll();
	}
}

FText UMSBattleAssistCustomizeWidget::GetDisplayedTimeText() const { return TimeText ? TimeText->GetText() : FText::GetEmpty(); }
FText UMSBattleAssistCustomizeWidget::GetDisplayedReasonText() const { return ReasonText ? ReasonText->GetText() : FText::GetEmpty(); }

void UMSBattleAssistCustomizeWidget::NativeDestruct()
{
	if (PreviewRenderer) PreviewRenderer->Shutdown();
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleDeckChanged);
	if (OnlineAccountSubsystem.IsValid()) OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged);
	if (ProgressSubsystem)
	{
		ProgressSubsystem->OnProgressChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleProgressChanged);
		ProgressSubsystem->OnOperationCompleted.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOperationCompleted);
	}
	Super::NativeDestruct();
}

FReply UMSBattleAssistCustomizeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton) { Close(); return FReply::Handled(); }
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSBattleAssistCustomizeWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSBattleAssistCustomizeWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem) { DeckSubsystem = InSubsystem; BindSubsystems(); RefreshAll(); }
void UMSBattleAssistCustomizeWidget::SetProgressSubsystemForTesting(UMSBattleAssistProgressSubsystem* InSubsystem) { ProgressSubsystem = InSubsystem; BindSubsystems(); RefreshAll(); }
void UMSBattleAssistCustomizeWidget::SetOnlineAccountForTesting(UMSOnlineAccountSubsystem* InSubsystem)
{
	if (OnlineAccountSubsystem.IsValid()) OnlineAccountSubsystem->OnAccountChanged.RemoveDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged);
	OnlineAccountSubsystem = InSubsystem;
	if (OnlineAccountSubsystem.IsValid()) OnlineAccountSubsystem->OnAccountChanged.AddUniqueDynamic(this, &UMSBattleAssistCustomizeWidget::HandleOnlineAccountChanged);
}
void UMSBattleAssistCustomizeWidget::SetSuitForTesting(UMSMobileSuitData* InSuit, const int32 InSlotIndex) { Suit = InSuit; SlotIndex = InSlotIndex; RefreshAll(); }
void UMSBattleAssistCustomizeWidget::BeginPendingCommitForAutomationTests(const FMSBattleAssistLoadout& Candidate, const int64 AccountGeneration)
{
	bPendingCommit = true; PendingAccountGeneration = AccountGeneration; TestPendingCandidate = Candidate; TestPendingOperationGeneration = OperationGeneration;
}
void UMSBattleAssistCustomizeWidget::CompletePendingCommitForAutomationTests(const bool bSuccess)
{
	HandleCommitCompletion(TestPendingCandidate, TestPendingOperationGeneration, SlotIndex, TWeakObjectPtr<UMSMobileSuitData>(Suit.Get()), PendingAccountGeneration, bSuccess);
}
#endif
