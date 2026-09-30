#include "UI/MSSpawnSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Data/MSWeaponData.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Gameplay/MSGameState.h"
#include "InputCoreTypes.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSBattleAssistCustomizeWidget.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUILockSubsystem.h"

namespace MSSpawnSelect
{
	const FLinearColor Background(0.003f, 0.006f, 0.008f, 1.0f);
	const FLinearColor Panel(0.004f, 0.015f, 0.017f, 0.94f);
	const FLinearColor Card(0.012f, 0.025f, 0.029f, 1.0f);
	const FLinearColor EmptyCard(0.025f, 0.035f, 0.04f, 1.0f);
	const FLinearColor Selected(0.14f, 0.65f, 0.63f, 1.0f);
	const FLinearColor Accent(0.25f, 0.65f, 0.60f, 1.0f);
	const FLinearColor Muted(0.24f, 0.32f, 0.33f, 1.0f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Value, float Size = 16.0f,
		const FLinearColor& Color = FLinearColor::White, FName Name = NAME_None)
	{
		const MSUIStyle::ETextRole Role = Size >= 24.0f ? MSUIStyle::ETextRole::NumericLarge
			: Size >= 18.0f ? MSUIStyle::ETextRole::H2
			: Size >= 14.0f ? MSUIStyle::ETextRole::Body
			: Size >= 11.0f ? MSUIStyle::ETextRole::Caption : MSUIStyle::ETextRole::Micro;
		UTextBlock* Text = MSUIStyle::MakeText(Tree, Value, Role, false, Color,
			Size >= 27.0f ? MSUIStyle::ENumericLargeSize::Emphasized : MSUIStyle::ENumericLargeSize::Standard, Name);
		// These are reference-canvas pixels, scaled together by the outer ScaleBox.
		FSlateFontInfo Font = Text->GetFont(); Font.Size = FMath::RoundToInt(Size * 0.75f); Text->SetFont(Font);
		return Text;
	}

	UButton* MakeButton(UWidgetTree* Tree, FName Name, const TCHAR* Label, bool bEnabled = true)
	{
		UTextBlock* Text = MSUIStyle::MakeText(Tree, FText::FromString(Label), MSUIStyle::ETextRole::Body,
			false, bEnabled ? MSUIStyleColor::TextPrimary : Muted);
		Text->SetAutoWrapText(false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Result = MSUIStyle::MakeCapsuleButton(Tree, Text, Panel, Name);
		Result->SetIsEnabled(bEnabled); return Result;
	}

	FString WeaponName(const TSoftObjectPtr<UMSWeaponDataBase>& Ref)
	{
		const UMSWeaponDataBase* Weapon = Ref.Get();
		if (!Weapon && !Ref.IsNull()) Weapon = Ref.LoadSynchronous();
		return Weapon && !Weapon->WeaponName.IsEmpty() ? Weapon->WeaponName.ToString() : TEXT("---");
	}

	/**
	 * Online countdowns are authoritative GameState presentation. This mirrors
	 * the briefing's state labels so SpawnSelect cannot expose its offline/local
	 * zero-second fallback after online travel.
	 */
	bool ResolveOnlineCountdown(const UUserWidget* Widget, FText& OutText)
	{
		const UMSGameInstance* Instance = Widget ? Cast<UMSGameInstance>(Widget->GetGameInstance()) : nullptr;
		const UMSOnlineAccountSubsystem* Online = Instance
			? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		if (!Online || !Online->IsOnlineMode()) return false;

		const AMSGameState* State = Widget && Widget->GetWorld()
			? Widget->GetWorld()->GetGameState<AMSGameState>() : nullptr;
		if (State && State->bBattleEnded)
		{
			OutText = FText::FromString(TEXT("戦闘終了"));
		}
		else if (State && State->RemainingTime < State->MatchDuration)
		{
			OutText = FText::Format(FText::FromString(TEXT("戦闘中 {0}")), State->GetFormattedRemainingTime());
		}
		else
		{
			// A newly travelled client may render one frame before GameState arrives.
			// Keep the state label stable instead of exposing an offline timer value.
			OutText = FText::FromString(TEXT("出撃準備"));
		}
		return true;
	}
}

void UMSSpawnSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); BindDeckSubsystem();
	RefreshAll(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSSpawnSelectWidget::BuildReferenceLayout()
{
	// Video 3, 130s: a full-screen suit with floating deck, statistics and actions.
	// Author once at 1920x1080; ScaleBox preserves geometry and image aspect at 720p.
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFit);
	UBorder* Letterbox = WidgetTree->ConstructWidget<UBorder>(); Letterbox->SetPadding(FMargin(0));
	Letterbox->SetBrushColor(MSSpawnSelect::Background); Letterbox->SetContent(Fit); WidgetTree->RootWidget = Letterbox;
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>();
	Design->SetWidthOverride(1920); Design->SetHeightOverride(1080); Fit->SetContent(Design);
	UCanvasPanel* Screen = WidgetTree->ConstructWidget<UCanvasPanel>(); Design->SetContent(Screen);
	auto Place = [Screen](UWidget* Widget, float X, float Y, float W, float H)
	{
		UCanvasPanelSlot* Slot = Screen->AddChildToCanvas(Widget);
		Slot->SetPosition(FVector2D(X, Y)); Slot->SetSize(FVector2D(W, H));
	};
	auto Plate = [this](UWidget* Child, FLinearColor Color, float FramePadding = 2.0f)
	{
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(Color);
		Border->SetPadding(FMargin(FramePadding)); Border->SetContent(Child); return Border;
	};
	PreviewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SpawnSuitPreview"));
	PreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Plate(PreviewImage, MSSpawnSelect::Background, 0), 0, 0, 1920, 1080);
	if (GetWorld())
	{
		PreviewRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this);
		PreviewRenderer->Initialize(GetWorld(), PreviewImage);
		PreviewRenderer->ConfigureSortieView(false);
	}

	OperationCountdownText = MSSpawnSelect::MakeText(WidgetTree, FText::GetEmpty(), 22, MSSpawnSelect::Accent, TEXT("SpawnOperationCountdownText"));
	OperationCountdownText->SetJustification(ETextJustify::Center);
	Place(Plate(OperationCountdownText, MSSpawnSelect::Panel, 4), 1220, 5, 225, 38);

	UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("SpawnDeckGrid"));
	Grid->SetColumnFill(0, 1); Grid->SetColumnFill(1, 1); Grid->SetRowFill(0, 1); Grid->SetRowFill(1, 1);
	Place(Grid, 138, 228, 336, 460);
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("SpawnSlot%dButton"), Index));
		FButtonStyle Style;
		Style.SetNormal(FSlateColorBrush(FLinearColor::White));
		Style.SetHovered(FSlateColorBrush(FLinearColor(0.72f, 0.88f, 0.90f)));
		Style.SetPressed(FSlateColorBrush(FLinearColor(0.4f, 0.7f, 0.72f)));
		Style.SetDisabled(FSlateColorBrush(FLinearColor(0.2f, 0.23f, 0.24f)));
		Style.SetNormalPadding(FMargin(0)); Style.SetPressedPadding(FMargin(0)); Button->SetStyle(Style);
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UBorder* Frame = Plate(Plate(Content, MSSpawnSelect::Card, 2), MSSpawnSelect::Muted, 2);
		Button->SetContent(Frame); MSUIStyle::StretchButtonContent(Button);
		UTextBlock* Number = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(FString::Printf(TEXT("0%d"), Index + 1)), 14, FLinearColor(0.76f, 0.81f, 0.84f));
		Number->SetJustification(ETextJustify::Center);
		Content->AddChildToVerticalBox(Number);
		UImage* Portrait = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("SpawnSlot%dPortrait"), Index));
		Portrait->SetDesiredSizeOverride(FVector2D(160, 116));
		Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
		UScaleBox* PortraitFit = WidgetTree->ConstructWidget<UScaleBox>(); PortraitFit->SetStretch(EStretch::ScaleToFit); PortraitFit->SetContent(Portrait);
		UOverlay* PortraitOverlay = WidgetTree->ConstructWidget<UOverlay>();
		UOverlaySlot* ImageSlot = PortraitOverlay->AddChildToOverlay(PortraitFit); ImageSlot->SetHorizontalAlignment(HAlign_Fill); ImageSlot->SetVerticalAlignment(VAlign_Fill);
		USizeBox* PortraitBox = WidgetTree->ConstructWidget<USizeBox>(); PortraitBox->SetHeightOverride(116); PortraitBox->SetContent(PortraitOverlay);
		Content->AddChildToVerticalBox(PortraitBox);
		UMSMobileSuitPreviewRenderer* Renderer = nullptr;
		if (GetWorld())
		{
			Renderer = NewObject<UMSMobileSuitPreviewRenderer>(this); Renderer->Initialize(GetWorld(), Portrait);
			Renderer->ConfigureSortieView(true);
		}
		SlotPreviewRenderers.Add(Renderer); SlotPreviewImages.Add(Portrait);
		UTextBlock* Name = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("- EMPTY -")), 16);
		USizeBox* NameBox = WidgetTree->ConstructWidget<USizeBox>(); NameBox->SetHeightOverride(40); NameBox->SetContent(Name);
		Content->AddChildToVerticalBox(NameBox);
		UTextBlock* Stats = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("COST ---  LV --")), 14, MSSpawnSelect::Accent);
		Stats->SetAutoWrapText(false); Content->AddChildToVerticalBox(Stats);
		UTextBlock* Equipment = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("装備  - / - / -")), 12, MSSpawnSelect::Muted);
		Equipment->SetAutoWrapText(false); Content->AddChildToVerticalBox(Equipment); SlotEquipmentTexts.Add(Equipment);
		UTextBlock* Respawn = MSSpawnSelect::MakeText(WidgetTree, FText::GetEmpty(), 14, MSSpawnSelect::Accent,
			*FString::Printf(TEXT("SpawnSlot%dRespawnText"), Index));
		Respawn->SetJustification(ETextJustify::Center);
		UOverlaySlot* RespawnSlot = PortraitOverlay->AddChildToOverlay(Respawn);
		RespawnSlot->SetHorizontalAlignment(HAlign_Fill); RespawnSlot->SetVerticalAlignment(VAlign_Center);
		UGridSlot* GridSlot = Grid->AddChildToGrid(Button, Index / 2, Index % 2); GridSlot->SetPadding(FMargin(2));
		GridSlot->SetHorizontalAlignment(HAlign_Fill); GridSlot->SetVerticalAlignment(VAlign_Fill);
		SlotButtons.Add(Button); SlotBorders.Add(Frame); SlotNameTexts.Add(Name); SlotStatsTexts.Add(Stats); RespawnTexts.Add(Respawn);
	}
	SlotButtons[0]->OnClicked.AddDynamic(this, &UMSSpawnSelectWidget::HandleSlot0Clicked);
	SlotButtons[1]->OnClicked.AddDynamic(this, &UMSSpawnSelectWidget::HandleSlot1Clicked);
	SlotButtons[2]->OnClicked.AddDynamic(this, &UMSSpawnSelectWidget::HandleSlot2Clicked);
	SlotButtons[3]->OnClicked.AddDynamic(this, &UMSSpawnSelectWidget::HandleSlot3Clicked);

	UButton* Parts = MSSpawnSelect::MakeButton(WidgetTree, TEXT("PartsSelectionButton"), TEXT("パーツ選択"), false);
	Parts->SetToolTipText(FText::FromString(TEXT("パーツは未設定です"))); Place(Parts, 908, 586, 180, 42);
	DeployButton = MSSpawnSelect::MakeButton(WidgetTree, TEXT("SpawnDeployButton"), TEXT("出 撃"));
	DeployButton->OnClicked.AddDynamic(this, &UMSSpawnSelectWidget::HandleDeployClicked); Place(DeployButton, 884, 646, 226, 52);
	UVerticalBox* Mission = WidgetTree->ConstructWidget<UVerticalBox>();
	for (const TCHAR* Label : { TEXT("MUコスト  -- / --"), TEXT("ミッションユニット1"), TEXT("未設定"), TEXT("ミッションユニット2"), TEXT("未設定") })
	{
		UTextBlock* Text = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(Label), 16, MSSpawnSelect::Muted);
		Text->SetJustification(ETextJustify::Center); Mission->AddChildToVerticalBox(Text);
	}
	Place(Plate(Mission, FLinearColor(0.015f, 0.025f, 0.008f, 0.92f), 5), 876, 727, 244, 115);

	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>();
	Place(Plate(Detail, MSSpawnSelect::Panel, 6), 1504, 220, 288, 430);
	SuitNameText = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("---")), 18, FLinearColor::White, TEXT("SpawnSuitDetailNameText"));
	USizeBox* DetailName = WidgetTree->ConstructWidget<USizeBox>(); DetailName->SetHeightOverride(46); DetailName->SetContent(SuitNameText);
	Detail->AddChildToVerticalBox(Plate(DetailName, FLinearColor(0.055f, 0.05f, 0.017f), 2));
	CostTimeText = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("COST ---   TIME ---")), 17, MSSpawnSelect::Accent, TEXT("SpawnSuitCostTimeText"));
	Detail->AddChildToVerticalBox(CostTimeText)->SetPadding(FMargin(0, 2, 0, 6));
	const TCHAR* Labels[] = { TEXT("アーマー"), TEXT("最高速度"), TEXT("ブースト容量"), TEXT("ブーストチャージ"), TEXT("積載量"), TEXT("緊急帰投性能") };
	for (int32 Index = 0; Index < StatusRowCount; ++Index)
	{
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(); Detail->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 0, 0, 3));
		UHorizontalBox* LabelRow = WidgetTree->ConstructWidget<UHorizontalBox>(); Row->AddChildToVerticalBox(LabelRow);
		LabelRow->AddChildToHorizontalBox(MSSpawnSelect::MakeText(WidgetTree, FText::FromString(Labels[Index]), 16))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Value = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("---")), 16, FLinearColor(0.66f, 0.85f, 0.73f),
			*FString::Printf(TEXT("SpawnSuitStatusValue%d"), Index)); LabelRow->AddChildToHorizontalBox(Value); StatusValueTexts.Add(Value);
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *FString::Printf(TEXT("SpawnSuitStatusBar%d"), Index));
		FProgressBarStyle BarStyle; BarStyle.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.02f, 0.04f, 0.045f)));
		BarStyle.SetFillImage(FSlateColorBrush(FLinearColor::White)); Bar->SetWidgetStyle(BarStyle);
		Bar->SetFillColorAndOpacity(FLinearColor(0.07f, 0.29f, 0.16f));
		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(); BarBox->SetHeightOverride(5); BarBox->SetContent(Bar);
		Row->AddChildToVerticalBox(BarBox); StatusBars.Add(Bar);
	}
	LoadoutText = MSSpawnSelect::MakeText(WidgetTree, FText::GetEmpty(), 16, FLinearColor(0.59f, 0.76f, 0.78f), TEXT("SpawnSuitLoadoutText"));
	Detail->AddChildToVerticalBox(LoadoutText)->SetPadding(FMargin(0, 6, 0, 0));
	UVerticalBox* ActiveParts = WidgetTree->ConstructWidget<UVerticalBox>();
	ActiveParts->AddChildToVerticalBox(MSSpawnSelect::MakeText(WidgetTree, FText::FromString(TEXT("発動中のパーツ　未設定")), 16, MSSpawnSelect::Muted));
	UGridPanel* PartsGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("ActivePartsGrid"));
	PartsGrid->SetColumnFill(0, 1); PartsGrid->SetColumnFill(1, 1);
	ActiveParts->AddChildToVerticalBox(PartsGrid)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	const TCHAR* PartLabels[] = { TEXT("EXP"), TEXT("GP"), TEXT("ITEM"), TEXT("ARMOR"), TEXT("BULLETS"), TEXT("RESPAWN"), TEXT("BOOST"), TEXT("BOOST") };
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UTextBlock* Text = MSSpawnSelect::MakeText(WidgetTree, FText::FromString(PartLabels[Index]), 14, MSSpawnSelect::Muted);
		Text->SetJustification(ETextJustify::Center);
		PartsGrid->AddChildToGrid(Plate(Text, MSSpawnSelect::Card, 8), Index / 2, Index % 2)->SetPadding(FMargin(2));
	}
	Place(Plate(ActiveParts, MSSpawnSelect::Panel, 6), 1504, 670, 288, 196);
}
void UMSSpawnSelectWidget::BindDeckSubsystem()
{
	UMSDeckSubsystem* Resolved = ResolveDeckSubsystem(); if (DeckSubsystem == Resolved) return;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleDeckChanged);
	DeckSubsystem = Resolved;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSSpawnSelectWidget::HandleDeckChanged);
}

UMSDeckSubsystem* UMSSpawnSelectWidget::ResolveDeckSubsystem() const
{
	return DeckSubsystem ? DeckSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr);
}

void UMSSpawnSelectWidget::OpenPreOperation(const float RemainingOperationSeconds)
{
	if (RespawnComponent) RespawnComponent->OnRespawnReady.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleRespawnReady);
	BindDeckSubsystem(); Mode = EMSSpawnSelectMode::PreOperation; RespawnComponent = nullptr;
	OperationRemainingSeconds = FMath::Max(0.0f, RemainingOperationSeconds); RefreshAll();
	if (GetWorld() && !IsInViewport()) AddToViewport(55); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PushScreen(TEXT("SpawnSelect"), FMSUICloseRequestDelegate::CreateWeakLambda(this, []() { return false; }));
}

void UMSSpawnSelectWidget::OpenRespawn(UMSRespawnComponent* InRespawnComponent)
{
	if (RespawnComponent && RespawnComponent != InRespawnComponent)
		RespawnComponent->OnRespawnReady.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleRespawnReady);
	BindDeckSubsystem(); Mode = EMSSpawnSelectMode::Respawn; RespawnComponent = InRespawnComponent;
	if (RespawnComponent) RespawnComponent->OnRespawnReady.AddUniqueDynamic(this, &UMSSpawnSelectWidget::HandleRespawnReady);
	RefreshAll();
	if (GetWorld() && !IsInViewport()) AddToViewport(55); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PushScreen(TEXT("SpawnSelect"), FMSUICloseRequestDelegate::CreateWeakLambda(this, []() { return false; }));
}

void UMSSpawnSelectWidget::Close()
{
	if (!IsOpen()) return; SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("SpawnSelect"));
}

FReply UMSSpawnSelectWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMSSpawnSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Escape is a consumed close request while this screen is active. The shared
	// UI lock deliberately keeps SpawnSelect open until a deployment flow closes it.
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMSSpawnSelectWidget::ResumeFromDeployment()
{
	RefreshAll();
	// DeploymentMap may have changed the shared deck selection. Restore the
	// visible card explicitly so every detail panel follows the selected slot.
	if (DeckSubsystem)
	{
		const int32 DeckSelection = DeckSubsystem->GetDeploymentSelectedSlotIndex();
		if (DeckSubsystem->GetDeploymentPresetInSlot(DeckSelection))
		{
			SelectedSlotIndex = DeckSelection;
			RefreshSelection();
			RefreshDetails();
		}
	}
	if (GetWorld() && !IsInViewport()) AddToViewport(55);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("SpawnSelect"), FMSUICloseRequestDelegate::CreateWeakLambda(this, []() { return false; }));
}

void UMSSpawnSelectWidget::RefreshAll()
{
	if (DeckSubsystem && (!DeckSubsystem->GetDeploymentPresetInSlot(SelectedSlotIndex)))
	{
		SelectedSlotIndex = DeckSubsystem->GetDeploymentSelectedSlotIndex();
		if (!DeckSubsystem->GetDeploymentPresetInSlot(SelectedSlotIndex)) SelectedSlotIndex = INDEX_NONE;
		if (SelectedSlotIndex == INDEX_NONE)
			for (int32 Index = 0; Index < SlotCount; ++Index) if (DeckSubsystem->GetDeploymentPresetInSlot(Index)) { SelectedSlotIndex = Index; break; }
	}
	for (int32 Index = 0; Index < SlotCount; ++Index) RefreshCard(Index);
	RefreshSelection(); RefreshDetails(); RefreshCountdownAndDeployState(0.0f);
}

void UMSSpawnSelectWidget::RefreshCard(const int32 SlotIndex)
{
	if (!SlotButtons.IsValidIndex(SlotIndex)) return;
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetDeploymentPresetInSlot(SlotIndex) : nullptr;
	SlotButtons[SlotIndex]->SetIsEnabled(Preset != nullptr);
	SlotNameTexts[SlotIndex]->SetText(Preset ? Preset->UnitName : FText::FromString(TEXT("- EMPTY -")));
	const FMSDeckLoadout SlotLoadout = DeckSubsystem ? DeckSubsystem->GetDeploymentLoadoutInSlot(SlotIndex) : FMSDeckLoadout();
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (DeckSubsystem && DeckSubsystem->GetGameInstance())
	{
		if (UMSBattleAssistProgressSubsystem* ProgressSubsystem = DeckSubsystem->GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>())
		{
			ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue;
		}
	}
	const FString ReuseTime = MSBattleAssistUI::ResolveEffectiveTime(Preset, SlotLoadout.BattleAssist, Progress);
	SlotStatsTexts[SlotIndex]->SetText(Preset ? FText::FromString(FString::Printf(TEXT("COST %d  TIME %s"), Preset->Cost, *ReuseTime)) : FText::FromString(TEXT("COST ---")));
	if (SlotPreviewRenderers.IsValidIndex(SlotIndex) && SlotPreviewRenderers[SlotIndex]) SlotPreviewRenderers[SlotIndex]->SetMobileSuit(Preset,
		DeckSubsystem && DeckSubsystem->GetDeploymentLoadoutInSlot(SlotIndex).bShieldEquipped);
	if (SlotPreviewImages.IsValidIndex(SlotIndex)) SlotPreviewImages[SlotIndex]->SetVisibility(Preset ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	FString Equipment = TEXT("装備 "); FString Tooltip;
	for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index)
	{
		const bool bEquipped = Preset && SlotLoadout.EquippedWeapons.IsValidIndex(Index) && !SlotLoadout.EquippedWeapons[Index].IsNull();
		Equipment += bEquipped ? FString::FromInt(Index + 1) : TEXT("-");
		if (Index < 2) Equipment += TEXT(" / ");
		Tooltip += FString::Printf(TEXT("%d  %s\n"), Index + 1, bEquipped ? *MSSpawnSelect::WeaponName(SlotLoadout.EquippedWeapons[Index]) : TEXT("未装備"));
	}
	SlotEquipmentTexts[SlotIndex]->SetText(FText::FromString(Equipment)); SlotButtons[SlotIndex]->SetToolTipText(FText::FromString(Tooltip));
}

void UMSSpawnSelectWidget::RefreshSelection()
{
	for (int32 Index = 0; Index < SlotBorders.Num(); ++Index)
		SlotBorders[Index]->SetBrushColor(Index == SelectedSlotIndex ? MSSpawnSelect::Selected : (IsSlotEnabled(Index) ? MSSpawnSelect::Muted : MSSpawnSelect::EmptyCard));
}

void UMSSpawnSelectWidget::RefreshDetails()
{
	UMSMobileSuitData* Preset = GetSelectedPreset(); if (PreviewRenderer) PreviewRenderer->SetMobileSuit(Preset,
		DeckSubsystem && DeckSubsystem->GetDeploymentLoadoutInSlot(SelectedSlotIndex).bShieldEquipped);
	if (!Preset)
	{
		SuitNameText->SetText(FText::FromString(TEXT("---"))); CostTimeText->SetText(FText::FromString(TEXT("COST ---  TIME ---")));
		for (int32 Index = 0; Index < StatusBars.Num(); ++Index) { StatusBars[Index]->SetPercent(0); StatusValueTexts[Index]->SetText(FText::FromString(TEXT("---"))); }
		LoadoutText->SetText(FText::FromString(TEXT("SLOT1 ---\nSLOT2 ---\nSLOT3 ---\nABILITY ---"))); return;
	}
	const FMSDeckLoadout SlotLoadout=DeckSubsystem?DeckSubsystem->GetDeploymentLoadoutInSlot(SelectedSlotIndex):FMSDeckLoadout();
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (DeckSubsystem && DeckSubsystem->GetGameInstance())
	{
		if (UMSBattleAssistProgressSubsystem* ProgressSubsystem = DeckSubsystem->GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>())
		{
			ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue;
		}
	}
	const FString ReuseTime = MSBattleAssistUI::ResolveEffectiveTime(Preset, SlotLoadout.BattleAssist, Progress);
	SuitNameText->SetText(Preset->UnitName); CostTimeText->SetText(FText::FromString(FString::Printf(TEXT("COST %d   TIME %s"), Preset->Cost,*ReuseTime)));
	const float Values[] = { static_cast<float>(Preset->MaxArmor), Preset->BoostSpeed * 100.0f, Preset->BoostCapacity, Preset->BoostRechargeRate, static_cast<float>(Preset->LoadCapacity), static_cast<float>(Preset->EmergencyRecallPerformance) };
	const float Maximums[] = { 10000.0f, 2500.0f, 200.0f, 50.0f, 100.0f, 1000.0f };
	for (int32 Index = 0; Index < StatusRowCount; ++Index) { StatusBars[Index]->SetPercent(FMath::Clamp(Values[Index] / Maximums[Index], 0.0f, 1.0f)); StatusValueTexts[Index]->SetText(FText::AsNumber(FMath::RoundToInt(Values[Index]))); }
	FString Loadout; for (int32 Index = 0; Index < UMSMobileSuitData::MaxWeaponSlots; ++Index) Loadout += FString::Printf(TEXT("SLOT%d  %s\n"), Index + 1, SlotLoadout.EquippedWeapons.IsValidIndex(Index) ? *MSSpawnSelect::WeaponName(SlotLoadout.EquippedWeapons[Index]) : TEXT("---"));
	const UMSModuleData* Module = SlotLoadout.EquippedModule.LoadSynchronous();
	Loadout += FString::Printf(TEXT("モジュール  %s\n特性  未設定"), Module ? *Module->ModuleName.ToString() : TEXT("未装備")); LoadoutText->SetText(FText::FromString(Loadout));
}

void UMSSpawnSelectWidget::RefreshCountdownAndDeployState(const float DeltaTime)
{
	if (Mode == EMSSpawnSelectMode::PreOperation)
	{
		OperationCountdownText->SetVisibility(ESlateVisibility::Visible);
		if (UWidget* CountdownPlate = OperationCountdownText->GetParent()) CountdownPlate->SetVisibility(ESlateVisibility::Visible);
		FText OnlineCountdown;
		if (MSSpawnSelect::ResolveOnlineCountdown(this, OnlineCountdown))
		{
			OperationCountdownText->SetText(OnlineCountdown);
		}
		else
		{
			OperationRemainingSeconds = FMath::Max(0.0f, OperationRemainingSeconds - FMath::Max(0.0f, DeltaTime));
			OperationCountdownText->SetText(FText::Format(FText::FromString(TEXT("作戦開始まで {0}秒")), FText::AsNumber(FMath::CeilToInt(OperationRemainingSeconds))));
		}
		for (UTextBlock* Text : RespawnTexts) Text->SetText(FText::GetEmpty());
	}
	else
	{
		OperationCountdownText->SetVisibility(ESlateVisibility::Collapsed);
		if (UWidget* CountdownPlate = OperationCountdownText->GetParent()) CountdownPlate->SetVisibility(ESlateVisibility::Collapsed);
		const float Remaining = RespawnComponent ? RespawnComponent->GetRespawnTimeRemaining() : 0.0f;
		for (int32 Index=0;Index<RespawnTexts.Num();++Index)
		{
			const float SlotRemaining=RespawnComponent?FMath::Max(Remaining,RespawnComponent->GetDeckSlotTimeRemaining(Index)):Remaining;
			const bool bOccupied=DeckSubsystem && DeckSubsystem->GetDeploymentPresetInSlot(Index);
			RespawnTexts[Index]->SetText(bOccupied && SlotRemaining>0.0f?FText::Format(FText::FromString(TEXT("再出撃まで\nあと {0} 秒")),FText::AsNumber(FMath::CeilToInt(SlotRemaining))):FText::GetEmpty());
		}
	}
	if (DeployButton) DeployButton->SetIsEnabled(GetSelectedPreset() != nullptr && (Mode != EMSSpawnSelectMode::Respawn || (RespawnComponent && RespawnComponent->CanDeployDeckSlot(SelectedSlotIndex))));
}

bool UMSSpawnSelectWidget::SelectSlot(const int32 SlotIndex)
{
	if (!DeckSubsystem || !DeckSubsystem->GetDeploymentPresetInSlot(SlotIndex)) return false;
	DeckSubsystem->SetDeploymentSelectedSlotIndex(SlotIndex); SelectedSlotIndex = DeckSubsystem->GetDeploymentSelectedSlotIndex(); if(SelectedSlotIndex!=SlotIndex) return false; RefreshSelection(); RefreshDetails(); RefreshCountdownAndDeployState(0.0f); return true;
}

UMSMobileSuitData* UMSSpawnSelectWidget::GetSelectedPreset() const { return DeckSubsystem ? DeckSubsystem->GetDeploymentPresetInSlot(SelectedSlotIndex) : nullptr; }
FText UMSSpawnSelectWidget::GetDisplayedSuitName() const { return SuitNameText ? SuitNameText->GetText() : FText::GetEmpty(); }
FText UMSSpawnSelectWidget::GetDisplayedCountdownText() const { return OperationCountdownText ? OperationCountdownText->GetText() : FText::GetEmpty(); }
FText UMSSpawnSelectWidget::GetCardRespawnText(const int32 SlotIndex) const { return RespawnTexts.IsValidIndex(SlotIndex) ? RespawnTexts[SlotIndex]->GetText() : FText::GetEmpty(); }
bool UMSSpawnSelectWidget::IsSlotEnabled(const int32 SlotIndex) const { return SlotButtons.IsValidIndex(SlotIndex) && SlotButtons[SlotIndex]->GetIsEnabled(); }
bool UMSSpawnSelectWidget::IsDeployEnabled() const { return DeployButton && DeployButton->GetIsEnabled(); }

void UMSSpawnSelectWidget::HandleDeckChanged(int32) { RefreshAll(); }
void UMSSpawnSelectWidget::HandleSlot0Clicked() { SelectSlot(0); }
void UMSSpawnSelectWidget::HandleSlot1Clicked() { SelectSlot(1); }
void UMSSpawnSelectWidget::HandleSlot2Clicked() { SelectSlot(2); }
void UMSSpawnSelectWidget::HandleSlot3Clicked() { SelectSlot(3); }

void UMSSpawnSelectWidget::HandleRespawnReady(AMSUnitBase*)
{
	// NativeTick can be suspended by the containing viewport/widget hierarchy. Readiness is a
	// gameplay state transition, so update the deploy gate directly when the timer completes.
	if (Mode == EMSSpawnSelectMode::Respawn) RefreshCountdownAndDeployState(0.0f);
}

bool UMSSpawnSelectWidget::EnsureDeploymentMapWidget()
{
	if (DeploymentMapWidget) return true;
	APlayerController* PC = GetOwningPlayer(); if (!PC) return false;
	TArray<UUserWidget*> Existing;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSDeploymentMapWidget::StaticClass(), false);
	DeploymentMapWidget = Existing.IsEmpty() ? CreateWidget<UMSDeploymentMapWidget>(PC) : Cast<UMSDeploymentMapWidget>(Existing[0]);
	return DeploymentMapWidget != nullptr;
}

void UMSSpawnSelectWidget::HandleDeployClicked()
{
	if (!IsDeployEnabled()) return;
	const double StartSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Preview button begin t=%.6f"), StartSeconds);
	++DeploymentRequestCount;
	if (!EnsureDeploymentMapWidget())
	{
		UE_LOG(LogTemp, Error, TEXT("[P1:DEPLOY] EnsureDeploymentMapWidget failed"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Deployment widget ready elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	DeploymentMapWidget->SetSourceSpawnSelectWidget(this);
	DeploymentMapWidget->SetMissionRuleName(MissionRuleName);
	DeploymentMapWidget->SetDeployPreset(GetSelectedPreset());
	Close();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Opening deployment map elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	DeploymentMapWidget->Open();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Deployment map open complete elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void UMSSpawnSelectWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime); if (IsOpen()) RefreshCountdownAndDeployState(InDeltaTime);
}

void UMSSpawnSelectWidget::NativeDestruct()
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleDeckChanged);
	if (RespawnComponent) RespawnComponent->OnRespawnReady.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleRespawnReady);
	if (PreviewRenderer) PreviewRenderer->Shutdown();
	for (UMSMobileSuitPreviewRenderer* Renderer : SlotPreviewRenderers) if (Renderer) Renderer->Shutdown();
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSSpawnSelectWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSSpawnSelectWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem)
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSpawnSelectWidget::HandleDeckChanged);
	DeckSubsystem = InSubsystem; if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSSpawnSelectWidget::HandleDeckChanged); RefreshAll();
}
void UMSSpawnSelectWidget::SetRespawnComponentForTesting(UMSRespawnComponent* InComponent) { RespawnComponent = InComponent; RefreshCountdownAndDeployState(0.0f); }
void UMSSpawnSelectWidget::AdvanceForAutomationTests(const float DeltaTime) { RefreshCountdownAndDeployState(DeltaTime); }
#endif
