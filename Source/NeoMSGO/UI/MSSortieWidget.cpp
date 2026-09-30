#include "UI/MSSortieWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Gameplay/MSGameState.h"
#include "InputCoreTypes.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUILockSubsystem.h"

namespace MSSortie
{
	const FLinearColor BackgroundColor(0.008f, 0.02f, 0.032f, 0.86f);
	const FLinearColor PanelColor(0.018f, 0.07f, 0.09f, 0.98f);
	const FLinearColor CardColor(0.035f, 0.105f, 0.14f, 1.0f);
	const FLinearColor SelectedColor(0.04f, 0.34f, 0.43f, 1.0f);
	const FLinearColor AccentColor(0.2f, 0.9f, 0.86f, 1.0f);
	const FLinearColor WarningColor(1.0f, 0.18f, 0.12f, 1.0f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, const float Size = 18.0f,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		const MSUIStyle::ETextRole Role = Size >= 24.0f ? MSUIStyle::ETextRole::NumericLarge
			: Size >= 18.0f ? MSUIStyle::ETextRole::H2
			: Size >= 14.0f ? MSUIStyle::ETextRole::Body : MSUIStyle::ETextRole::Caption;
		return MSUIStyle::MakeText(Tree, Text, Role, false, Color,
			Size >= 27.0f ? MSUIStyle::ENumericLargeSize::Emphasized : MSUIStyle::ENumericLargeSize::Standard, Name);
	}

	UButton* MakeButton(UWidgetTree* Tree, const FText& Label, const float Size = 17.0f, const FName Name = NAME_None,
		UTextBlock** OutText = nullptr)
	{
		UTextBlock* Text = MSUIStyle::MakeText(Tree, Label, MSUIStyle::ETextRole::Body);
		Text->SetAutoWrapText(false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(Tree, Text, PanelColor, Name);
		if (OutText) *OutText = Text;
		return Button;
	}

	FText UnitTypeText(const EMSMobileSuitUnitType Type)
	{
		switch (Type)
		{
		case EMSMobileSuitUnitType::Assault: return FText::FromString(TEXT("強襲"));
		case EMSMobileSuitUnitType::Heavy: return FText::FromString(TEXT("重撃"));
		case EMSMobileSuitUnitType::Support: return FText::FromString(TEXT("支援"));
		case EMSMobileSuitUnitType::Bombardment: return FText::FromString(TEXT("砲撃"));
		case EMSMobileSuitUnitType::Sniper: return FText::FromString(TEXT("狙撃"));
		default: return FText::FromString(TEXT("オール"));
		}
	}

	bool MatchesCategory(const UMSMobileSuitData* Preset, const EMSSortieCatalogCategory Category)
	{
		if (!Preset || Category == EMSSortieCatalogCategory::All) return Preset != nullptr;
		return static_cast<uint8>(Preset->UnitType) == static_cast<uint8>(Category) - 1;
	}
}

void UMSMobileSuitCatalogEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	MSUIStyle::ApplyContentButtonStyle(Button);
	UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
	Border->SetBrushColor(MSSortie::CardColor); Border->SetPadding(FMargin(12.0f, 7.0f));
	NameText = MSSortie::MakeText(WidgetTree, FText::GetEmpty(), 16.0f);
	Border->SetContent(NameText); Button->SetContent(Border); WidgetTree->RootWidget = Button;
	Button->OnClicked.AddDynamic(this, &UMSMobileSuitCatalogEntryWidget::HandleClicked);
}

void UMSMobileSuitCatalogEntryWidget::SetPreset(UMSMobileSuitData* InPreset, UMSSortieWidget* InOwningSortieWidget)
{
	Preset = InPreset; OwningSortieWidget = InOwningSortieWidget;
	if (NameText && Preset)
	{
		NameText->SetText(FText::Format(FText::FromString(TEXT("{0}  /  {1}   COST {2}")),
			Preset->UnitName, MSSortie::UnitTypeText(Preset->UnitType), FText::AsNumber(Preset->Cost)));
	}
}

void UMSMobileSuitCatalogEntryWidget::HandleClicked()
{
	if (OwningSortieWidget && Preset) OwningSortieWidget->SelectCatalogPreset(Preset);
}

void UMSSortieWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(); Background->SetBrushColor(MSSortie::BackgroundColor);
	UCanvasPanelSlot* BackgroundSlot = Root->AddChildToCanvas(Background);
	BackgroundSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); BackgroundSlot->SetOffsets(FMargin(0.0f));
	Background->SetPadding(FMargin(24.0f, 16.0f, 24.0f, 20.0f));

	UVerticalBox* Screen = WidgetTree->ConstructWidget<UVerticalBox>(); Background->SetContent(Screen);
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Screen->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 12));
	CountdownText = MSSortie::MakeText(WidgetTree, FText::GetEmpty(), 29.0f, MSSortie::AccentColor, TEXT("SortieCountdownText"));
	CountdownText->SetJustification(ETextJustify::Center);
	Header->AddChildToHorizontalBox(CountdownText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* CloseButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("×")), 28.0f, TEXT("SortieModalCloseButton"));
	MSUIStyle::ApplyButtonSound(CloseButton, MSUIStyle::EButtonSound::Back);
	CloseButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCloseClicked);
	UHorizontalBoxSlot* CloseSlot = Header->AddChildToHorizontalBox(CloseButton); CloseSlot->SetPadding(FMargin(12, 0, 0, 0));

	UHorizontalBox* Main = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* MainSlot = Screen->AddChildToVerticalBox(Main); MainSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Left: the reference frame uses a compact 2x2 sortie deck.
	UVerticalBox* DeckColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* DeckPanel = nullptr;
	UBorder* DeckFrame = MSUIStyle::MakeFramedPanel(WidgetTree, DeckPanel, DeckColumn, false,
		MSSortie::AccentColor, MSSortie::PanelColor, FMargin(10));
	UHorizontalBoxSlot* DeckPanelSlot = Main->AddChildToHorizontalBox(DeckFrame);
	DeckPanelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DeckPanelSlot->SetPadding(FMargin(0, 0, 10, 0));
	DeckColumn->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("SORTIE DECK")), 20, MSSortie::AccentColor))
		->SetPadding(FMargin(4, 0, 0, 8));

	TArray<UButton*> SlotButtons;
	for (int32 Row = 0; Row < 2; ++Row)
	{
		UHorizontalBox* SlotRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBoxSlot* RowSlot = DeckColumn->AddChildToVerticalBox(SlotRow); RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Index = Row * 2 + Column;
			UButton* SlotButton = WidgetTree->ConstructWidget<UButton>();
			MSUIStyle::ApplyContentButtonStyle(SlotButton);
			UBorder* Card = WidgetTree->ConstructWidget<UBorder>(); Card->SetPadding(FMargin(11)); Card->SetBrushColor(MSSortie::CardColor);
			UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>(); Card->SetContent(CardContent); SlotButton->SetContent(Card);
			UTextBlock* SlotLabel = MSSortie::MakeText(WidgetTree,
				FText::Format(FText::FromString(TEXT("0{0}")), Index + 1), 14.0f, MSSortie::AccentColor);
			UTextBlock* Name = MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("-EMPTY-")), 22.0f);
			Name->SetJustification(ETextJustify::Center);
			UTextBlock* Stats = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("COST  ---")), MSUIStyle::ETextRole::NumericLarge,
				false, FLinearColor(0.72f, 0.84f, 0.88f));
			Stats->SetJustification(ETextJustify::Center);
			UVerticalBox* Weapons = WidgetTree->ConstructWidget<UVerticalBox>();
			CardContent->AddChildToVerticalBox(SlotLabel);
			CardContent->AddChildToVerticalBox(Name)->SetPadding(FMargin(0, 12, 0, 8));
			CardContent->AddChildToVerticalBox(Stats)->SetPadding(FMargin(0, 0, 0, 8));
			UTextBlock* Shield = MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("SHIELD  盾なし")), 12.0f, MSSortie::AccentColor, *FString::Printf(TEXT("SortieSlotShieldText%d"), Index));
			Shield->SetJustification(ETextJustify::Center); Shield->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			CardContent->AddChildToVerticalBox(Shield)->SetPadding(FMargin(0, 0, 0, 5));
			CardContent->AddChildToVerticalBox(Weapons)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UHorizontalBoxSlot* CardSlot = SlotRow->AddChildToHorizontalBox(SlotButton);
			CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CardSlot->SetPadding(FMargin(4));
			SlotButtons.Add(SlotButton); SlotBorders.Add(Card); SlotNameTexts.Add(Name); SlotStatsTexts.Add(Stats); SlotWeaponBoxes.Add(Weapons); SlotShieldTexts.Add(Shield);
		}
	}
	SlotButtons[0]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSlot0Clicked);
	SlotButtons[1]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSlot1Clicked);
	SlotButtons[2]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSlot2Clicked);
	SlotButtons[3]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSlot3Clicked);
	UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	if (Deck)
	{
		Deck->OnDeckChanged.AddUniqueDynamic(this, &UMSSortieWidget::HandleDeckChanged);
	}

	// Center: the reference's sortie-deck summary and explicit insert/remove/replace controls.
	USizeBox* ControlSize = WidgetTree->ConstructWidget<USizeBox>(); ControlSize->SetWidthOverride(285.0f);
	UHorizontalBoxSlot* ControlSlot = Main->AddChildToHorizontalBox(ControlSize); ControlSlot->SetPadding(FMargin(0, 0, 10, 0));
	UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* ControlPanel = nullptr;
	ControlSize->SetContent(MSUIStyle::MakeFramedPanel(WidgetTree, ControlPanel, Controls, false,
		MSSortie::AccentColor, MSSortie::PanelColor, FMargin(14)));
	UTextBlock* DeckLabel = MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("出撃デッキ")), 22, MSSortie::AccentColor);
	DeckLabel->SetJustification(ETextJustify::Center); Controls->AddChildToVerticalBox(DeckLabel)->SetPadding(FMargin(0, 4, 0, 18));
	UTextBlock* MuCost = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("MUコスト\n0200 / 0220")), MSUIStyle::ETextRole::NumericLarge,
		false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("DummyMuCostText"));
	MuCost->SetJustification(ETextJustify::Center); Controls->AddChildToVerticalBox(MuCost)->SetPadding(FMargin(0, 0, 0, 12));
	Controls->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("ミッションユニット1\nブーストユニット Lv1")), 15, FLinearColor(0.75f, 0.9f, 0.9f), TEXT("DummyMissionUnit1Text")))->SetPadding(FMargin(0, 3));
	Controls->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("ミッションユニット2\nアーマーユニット Lv1")), 15, FLinearColor(0.75f, 0.9f, 0.9f), TEXT("DummyMissionUnit2Text")))->SetPadding(FMargin(0, 3, 0, 18));
	Controls->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("デッキコスト")), 18, MSSortie::AccentColor));
	DeckCostText = MSSortie::MakeText(WidgetTree, FText::GetEmpty(), 30);
	DeckCostText->SetJustification(ETextJustify::Center); Controls->AddChildToVerticalBox(DeckCostText)->SetPadding(FMargin(0, 4, 0, 4));
	CostWarningText = MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("COST OVER\n出撃できません")), 17, MSSortie::WarningColor);
	CostWarningText->SetJustification(ETextJustify::Center); Controls->AddChildToVerticalBox(CostWarningText)->SetPadding(FMargin(0, 0, 0, 18));
	CatalogSelectionText = MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("所持機体を選択")), 16, FLinearColor(0.72f, 0.84f, 0.88f));
	CatalogSelectionText->SetJustification(ETextJustify::Center); Controls->AddChildToVerticalBox(CatalogSelectionText)->SetPadding(FMargin(0, 0, 0, 10));
	UButton* ReplaceButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("入れ替え")), 17.0f, TEXT("SortieReplaceButton"));
	UButton* RemoveButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("機体を外す")), 17.0f, TEXT("SortieRemoveButton"));
	UButton* InsertButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("機体を入れる")), 17.0f, TEXT("SortieInsertButton"));
	Controls->AddChildToVerticalBox(ReplaceButton)->SetPadding(FMargin(0, 3));
	Controls->AddChildToVerticalBox(RemoveButton)->SetPadding(FMargin(0, 3));
	Controls->AddChildToVerticalBox(InsertButton)->SetPadding(FMargin(0, 3));
	ReplaceButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleReplaceClicked);
	RemoveButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleRemoveClicked);
	InsertButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleInsertClicked);

	// Right: owned mobile suits with six working category tabs and a scrollable list.
	UVerticalBox* CatalogColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* CatalogPanel = nullptr;
	UBorder* CatalogFrame = MSUIStyle::MakeFramedPanel(WidgetTree, CatalogPanel, CatalogColumn, false,
		MSSortie::AccentColor, MSSortie::PanelColor, FMargin(10));
	UHorizontalBoxSlot* CatalogPanelSlot = Main->AddChildToHorizontalBox(CatalogFrame); CatalogPanelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CatalogColumn->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, FText::FromString(TEXT("MOBILE SUIT LIST")), 20, MSSortie::AccentColor))
		->SetPadding(FMargin(4, 0, 0, 7));
	UHorizontalBox* SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>(); CatalogColumn->AddChildToVerticalBox(SearchRow)->SetPadding(FMargin(0, 0, 0, 6));
	SearchInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SortieSearchInput"));
	SearchInput->SetHintText(FText::FromString(TEXT("機体名を入力して検索")));
	SearchRow->AddChildToHorizontalBox(SearchInput)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* SearchButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("検索")), 14.0f, TEXT("SortieSearchButton"));
	UButton* ClearButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("クリア")), 14.0f, TEXT("SortieSearchClearButton"));
	SearchButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSearchClicked); ClearButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleSearchClearClicked);
	SearchRow->AddChildToHorizontalBox(SearchButton)->SetPadding(FMargin(5, 0)); SearchRow->AddChildToHorizontalBox(ClearButton);
	UTextBlock* CostSortLabel = nullptr;
	UButton* CostSortButton = MSSortie::MakeButton(WidgetTree, FText::FromString(TEXT("COST：高い順")), 14.0f, TEXT("SortieCostSortButton"), &CostSortLabel);
	CostSortText = CostSortLabel;
	CostSortButton->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCostSortClicked); CatalogColumn->AddChildToVerticalBox(CostSortButton)->SetPadding(FMargin(0, 0, 0, 6));
	UHorizontalBox* CategoryTabs = WidgetTree->ConstructWidget<UHorizontalBox>(); CatalogColumn->AddChildToVerticalBox(CategoryTabs)->SetPadding(FMargin(0, 0, 0, 8));
	const TCHAR* CategoryLabels[] = { TEXT("オール"), TEXT("強襲"), TEXT("重撃"), TEXT("支援"), TEXT("砲撃"), TEXT("狙撃") };
	const FName CategoryNames[] = { TEXT("SortieCategoryAllButton"), TEXT("SortieCategoryAssaultButton"), TEXT("SortieCategoryHeavyButton"), TEXT("SortieCategorySupportButton"), TEXT("SortieCategoryBombardmentButton"), TEXT("SortieCategorySniperButton") };
	TArray<UButton*> CategoryButtons;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CategoryLabels); ++Index)
	{
		UButton* Button = MSSortie::MakeButton(WidgetTree, FText::FromString(CategoryLabels[Index]), 14.0f, CategoryNames[Index]);
		CategoryTabs->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CategoryButtons.Add(Button);
	}
	CategoryButtons[0]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategoryAllClicked);
	CategoryButtons[1]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategoryAssaultClicked);
	CategoryButtons[2]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategoryHeavyClicked);
	CategoryButtons[3]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategorySupportClicked);
	CategoryButtons[4]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategoryBombardmentClicked);
	CategoryButtons[5]->OnClicked.AddDynamic(this, &UMSSortieWidget::HandleCategorySniperClicked);
	CatalogListView = WidgetTree->ConstructWidget<UScrollBox>(); CatalogListView->SetScrollBarVisibility(ESlateVisibility::Visible);
	CatalogColumn->AddChildToVerticalBox(CatalogListView)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	RefreshCatalogFromAssetManager(); RefreshSelectionStyle(); RefreshDeckCost(); RefreshBriefingCountdown();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSSortieWidget::NativeDestruct()
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSortieWidget::HandleDeckChanged);
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSSortieWidget::InitializeForAutomationTests()
{
	if (!CatalogListView) NativeOnInitialized();
}

TArray<FText> UMSSortieWidget::GetSlotWeaponNameTextsForTests(const int32 SlotIndex) const
{
	TArray<FText> Names;
	if (!SlotWeaponBoxes.IsValidIndex(SlotIndex) || !SlotWeaponBoxes[SlotIndex])
	{
		return Names;
	}
	for (int32 Index = 0; Index < SlotWeaponBoxes[SlotIndex]->GetChildrenCount(); ++Index)
	{
		if (const UTextBlock* Text = Cast<UTextBlock>(SlotWeaponBoxes[SlotIndex]->GetChildAt(Index)))
		{
			Names.Add(Text->GetText());
		}
	}
	return Names;
}

FText UMSSortieWidget::GetSlotShieldTextForTests(const int32 SlotIndex) const
{
	return SlotShieldTexts.IsValidIndex(SlotIndex) && SlotShieldTexts[SlotIndex] ? SlotShieldTexts[SlotIndex]->GetText() : FText::GetEmpty();
}

FLinearColor UMSSortieWidget::GetDeckCostTextColorForAutomationTests() const
{
	return DeckCostText ? DeckCostText->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
}

void UMSSortieWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InDeckSubsystem)
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSSortieWidget::HandleDeckChanged);
	DeckSubsystem = InDeckSubsystem;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSSortieWidget::HandleDeckChanged);
	for (int32 SlotIndex = 0; SlotIndex < SortieSlotCount; ++SlotIndex) RefreshSlotCard(SlotIndex);
	RefreshSelectionStyle(); RefreshDeckCost();
}
#endif

void UMSSortieWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshBriefingCountdown();
}

bool UMSSortieWidget::IsAdmittedLoadoutView() const
{
	const UGameInstance* Instance = GetGameInstance();
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	return BriefingWidget && Online && Online->IsOnlineMode();
}

void UMSSortieWidget::Open()
{
	const bool bWasOpen = IsOpen();
	// Headless automation widgets have no world, so viewport registration is meaningless there.
	if (!IsInViewport() && GetWorld()) AddToViewport(60);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	SelectedSlotIndex = GetSelectedSlotIndex();
	if (CatalogItems.IsEmpty()) RefreshCatalogFromAssetManager();
	for (int32 SlotIndex = 0; SlotIndex < SortieSlotCount; ++SlotIndex) RefreshSlotCard(SlotIndex);
	RefreshSelectionStyle(); RefreshDeckCost(); RefreshBriefingCountdown();
	if (!bWasOpen) if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("Sortie"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSSortieWidget::Close()
{
	if (!IsOpen()) return;
	SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("Sortie"));
	if (BriefingWidget && BriefingWidget->IsOpen()) BriefingWidget->SetKeyboardFocus();
}

FReply UMSSortieWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSSortieWidget::SetSelectedSlotIndex(const int32 SlotIndex)
{
	UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	if (!Deck || SlotIndex < 0 || SlotIndex >= Deck->GetSlotCount()) return;
	if (IsAdmittedLoadoutView()) Deck->SetDeploymentSelectedSlotIndex(SlotIndex);
	else Deck->SetSelectedSlotIndex(SlotIndex);
	SelectedSlotIndex = GetSelectedSlotIndex(); RefreshSelectionStyle(); RefreshDeckCost();
}

void UMSSortieWidget::AssignPresetToSelectedSlot(UMSMobileSuitData* Preset)
{
	if (IsAdmittedLoadoutView()) return;
	if (!WouldExceedCostLimit(Preset)) if (UMSDeckSubsystem* Deck = ResolveDeckSubsystem())
	{
		Deck->AssignPreset(SelectedSlotIndex, Preset);
	}
}

void UMSSortieWidget::SelectCatalogPreset(UMSMobileSuitData* Preset)
{
	SelectedCatalogPreset = Preset;
	if (CatalogSelectionText)
	{
		CatalogSelectionText->SetText(Preset
			? FText::Format(FText::FromString(TEXT("選択中\n{0}\nCOST {1}")), Preset->UnitName, FText::AsNumber(Preset->Cost))
			: FText::FromString(TEXT("所持機体を選択")));
	}
	RefreshDeckCost();
}

void UMSSortieWidget::SetBriefingWidget(UMSBriefingWidget* InBriefingWidget)
{
	BriefingWidget = InBriefingWidget;
	RefreshBriefingCountdown();
}

void UMSSortieWidget::SelectCatalogCategory(const EMSSortieCatalogCategory Category)
{
	SelectedCatalogCategory = Category; RefreshFilteredCatalog();
}

bool UMSSortieWidget::InsertSelectedCatalogPreset()
{
	if (IsAdmittedLoadoutView()) return false;
	if (GetPresetInSlot(SelectedSlotIndex) || !SelectedCatalogPreset || WouldExceedCostLimit(SelectedCatalogPreset)) return false;
	return ResolveDeckSubsystem() && ResolveDeckSubsystem()->AssignPreset(SelectedSlotIndex, SelectedCatalogPreset);
}

bool UMSSortieWidget::ReplaceWithSelectedCatalogPreset()
{
	if (IsAdmittedLoadoutView()) return false;
	if (!GetPresetInSlot(SelectedSlotIndex) || !SelectedCatalogPreset || WouldExceedCostLimit(SelectedCatalogPreset)) return false;
	return ResolveDeckSubsystem() && ResolveDeckSubsystem()->AssignPreset(SelectedSlotIndex, SelectedCatalogPreset);
}

bool UMSSortieWidget::RemovePresetFromSelectedSlot()
{
	if (IsAdmittedLoadoutView()) return false;
	if (UMSDeckSubsystem* Deck = ResolveDeckSubsystem()) return Deck->RemovePreset(SelectedSlotIndex);
	return false;
}

UMSMobileSuitData* UMSSortieWidget::GetPresetInSlot(const int32 SlotIndex) const
{
	if (UMSDeckSubsystem* Deck = ResolveDeckSubsystem()) return IsAdmittedLoadoutView() ? Deck->GetDeploymentPresetInSlot(SlotIndex) : Deck->GetPresetInSlot(SlotIndex);
	return nullptr;
}

int32 UMSSortieWidget::GetCatalogItemCount() const { return CatalogItems.Num(); }

UMSMobileSuitData* UMSSortieWidget::GetFilteredCatalogPreset(const int32 Index) const
{
	return FilteredCatalogItems.IsValidIndex(Index) ? FilteredCatalogItems[Index].Get() : nullptr;
}

int32 UMSSortieWidget::GetSortieSlotCount() const { return UMSDeckSubsystem::SlotCount; }

int32 UMSSortieWidget::GetSelectedSlotIndex() const
{
	if (const UMSDeckSubsystem* Deck = ResolveDeckSubsystem()) return IsAdmittedLoadoutView() ? Deck->GetDeploymentSelectedSlotIndex() : Deck->GetSelectedSlotIndex();
	return SelectedSlotIndex;
}

UMSDeckSubsystem* UMSSortieWidget::GetDeckSubsystem() const { return ResolveDeckSubsystem(); }

int32 UMSSortieWidget::GetCurrentDeckCost() const
{
	if (IsAdmittedLoadoutView())
	{
		int32 Cost = 0;
		for (int32 Index = 0; Index < SortieSlotCount; ++Index) if (const auto* Preset = GetPresetInSlot(Index)) Cost += Preset->Cost;
		return Cost;
	}
	if (const UMSDeckSubsystem* Deck = ResolveDeckSubsystem()) return Deck->GetCurrentDeckCost();
	return 0;
}

int32 UMSSortieWidget::GetDeckCostLimit() const { return UMSDeckSubsystem::DeckCostLimit; }

bool UMSSortieWidget::IsDeckOverCostLimit() const
{
	return GetCurrentDeckCost() > GetDeckCostLimit();
}

void UMSSortieWidget::SetCatalogPresets(const TArray<UMSMobileSuitData*>& Presets)
{
	CatalogItems.Reset();
	for (UMSMobileSuitData* Preset : Presets) if (IsValid(Preset)) CatalogItems.Add(Preset);
	if (!CatalogItems.Contains(SelectedCatalogPreset)) SelectCatalogPreset(CatalogItems.IsEmpty() ? nullptr : CatalogItems[0]);
	RefreshFilteredCatalog();
}

void UMSSortieWidget::RefreshFilteredCatalog()
{
	FilteredCatalogItems.Reset();
	if (CatalogListView) CatalogListView->ClearChildren();
	const FString Query = SearchInput ? SearchInput->GetText().ToString().TrimStartAndEnd() : FString();
	for (UMSMobileSuitData* Preset : CatalogItems)
	{
		if (!MSSortie::MatchesCategory(Preset, SelectedCatalogCategory)) continue;
		if (!Query.IsEmpty() && !Preset->UnitName.ToString().Contains(Query, ESearchCase::IgnoreCase)) continue;
		FilteredCatalogItems.Add(Preset);
	}
	FilteredCatalogItems.Sort([this](const UMSMobileSuitData& A, const UMSMobileSuitData& B)
	{
		if (A.Cost == B.Cost) return A.UnitName.ToString() < B.UnitName.ToString();
		return bCostDescending ? A.Cost > B.Cost : A.Cost < B.Cost;
	});
	if (CatalogListView)
	{
		for (UMSMobileSuitData* Preset : FilteredCatalogItems)
		{
			UMSMobileSuitCatalogEntryWidget* Entry = WidgetTree->ConstructWidget<UMSMobileSuitCatalogEntryWidget>();
			Entry->SetPreset(Preset, this);
			if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(CatalogListView->AddChild(Entry))) EntrySlot->SetPadding(FMargin(0, 0, 0, 4));
		}
	}
}

void UMSSortieWidget::RefreshCatalogFromAssetManager()
{
	if (UMSDeckSubsystem* Deck = ResolveDeckSubsystem()) SetCatalogPresets(Deck->GetCatalogPresets());
}

void UMSSortieWidget::RefreshSlotCard(const int32 SlotIndex)
{
	if (!SlotNameTexts.IsValidIndex(SlotIndex)) return;
	UMSMobileSuitData* Preset = GetPresetInSlot(SlotIndex);
	SlotNameTexts[SlotIndex]->SetText(Preset ? Preset->UnitName : FText::FromString(TEXT("-EMPTY-")));
	SlotStatsTexts[SlotIndex]->SetText(Preset
		? FText::Format(FText::FromString(TEXT("{0}  /  COST {1}\nARMOR {2}")),
			MSSortie::UnitTypeText(Preset->UnitType), FText::AsNumber(Preset->Cost), FText::AsNumber(Preset->MaxArmor))
		: FText::FromString(TEXT("COST  ---")));
	if (SlotShieldTexts.IsValidIndex(SlotIndex) && SlotShieldTexts[SlotIndex])
	{
		const UMSShieldWeaponData* Shield = Preset ? Preset->GetNativeShield() : nullptr;
		if (!Shield)
		{
			SlotShieldTexts[SlotIndex]->SetText(FText::FromString(TEXT("SHIELD  盾なし")));
			SlotShieldTexts[SlotIndex]->SetColorAndOpacity(FSlateColor(MSSortie::AccentColor));
		}
		else
		{
			const UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
			const FMSDeckLoadout ShieldLoadout = Deck ? (IsAdmittedLoadoutView() ? Deck->GetDeploymentLoadoutInSlot(SlotIndex) : Deck->GetLoadoutInSlot(SlotIndex)) : FMSDeckLoadout();
			// Keep the actionable state and durability ahead of the authored name;
			// the card's narrow one-line slot otherwise hides both behind ellipsis.
			const FString State = !Preset->bShieldRemovable ? TEXT("固定装備") : (ShieldLoadout.bShieldEquipped ? TEXT("装着中") : TEXT("未装備"));
			SlotShieldTexts[SlotIndex]->SetText(FText::FromString(FString::Printf(TEXT("SHIELD  %s  %d"), *State, FMath::RoundToInt(Shield->MaxDurability))));
			SlotShieldTexts[SlotIndex]->SetColorAndOpacity(FSlateColor(MSSortie::AccentColor));
		}
	}
	UVerticalBox* Weapons = SlotWeaponBoxes[SlotIndex]; Weapons->ClearChildren();
	if (!Preset) return;
	const UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	const FMSDeckLoadout Loadout = Deck ? (IsAdmittedLoadoutView() ? Deck->GetDeploymentLoadoutInSlot(SlotIndex) : Deck->GetLoadoutInSlot(SlotIndex)) : FMSDeckLoadout();
	for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : Loadout.EquippedWeapons)
	{
		if (WeaponRef.IsNull())
		{
			continue;
		}
		// 未ロードのソフト参照は表示名(WeaponName)を得るため同期ロードする。
		// 以前は .Get() が未ロード時に nullptr を返し、生アセット名(DL_WEAPON_...)へ
		// フォールバックして表示されていた(実プレイ報告バグ)。UI表示用途の小型データアセット
		// のため同期ロードのコストは許容範囲(MSHangarWidget/MSWeaponComponent と同方針)。
		const UMSWeaponDataBase* Weapon = WeaponRef.Get() ? WeaponRef.Get() : WeaponRef.LoadSynchronous();
		if (Weapon && Cast<UMSShieldWeaponData>(Weapon)) continue;
		const FText WeaponName = (Weapon && !Weapon->WeaponName.IsEmpty())
			? Weapon->WeaponName
			: FText::FromString(WeaponRef.GetAssetName());
		Weapons->AddChildToVerticalBox(MSSortie::MakeText(WidgetTree, WeaponName, 12.0f, FLinearColor(0.72f, 0.8f, 0.84f)));
	}
}

void UMSSortieWidget::RefreshSelectionStyle()
{
	for (int32 Index = 0; Index < SlotBorders.Num(); ++Index)
		SlotBorders[Index]->SetBrushColor(Index == SelectedSlotIndex ? MSSortie::SelectedColor : MSSortie::CardColor);
}

void UMSSortieWidget::RefreshDeckCost()
{
	const bool bAdmitted = IsAdmittedLoadoutView();
	for (const FName Name : { FName(TEXT("SortieReplaceButton")), FName(TEXT("SortieRemoveButton")), FName(TEXT("SortieInsertButton")) })
		if (UButton* Button = Cast<UButton>(GetWidgetFromName(Name)))
		{
			Button->SetIsEnabled(!bAdmitted);
			Button->SetToolTipText(bAdmitted ? FText::FromString(TEXT("参戦中の編成です。入れ替えは試合を退出して格納庫で行ってください。")) : FText::GetEmpty());
		}
	const int32 DisplayedCost = GetCurrentDeckCost();
	const bool bOverCost = DisplayedCost > GetDeckCostLimit() || (!bAdmitted && WouldExceedCostLimit(SelectedCatalogPreset));
	if (DeckCostText)
	{
		DeckCostText->SetText(FText::Format(FText::FromString(TEXT("{0} / {1}")), FText::AsNumber(DisplayedCost), FText::AsNumber(GetDeckCostLimit())));
		DeckCostText->SetColorAndOpacity(FSlateColor(bOverCost ? MSSortie::WarningColor : MSSortie::AccentColor));
	}
	if (CostWarningText) CostWarningText->SetVisibility(bOverCost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

int32 UMSSortieWidget::GetProspectiveDeckCost(UMSMobileSuitData* Preset) const
{
	const UMSMobileSuitData* Current = GetPresetInSlot(SelectedSlotIndex);
	return GetCurrentDeckCost() - (Current ? Current->Cost : 0) + (Preset ? Preset->Cost : 0);
}

bool UMSSortieWidget::WouldExceedCostLimit(UMSMobileSuitData* Preset) const
{
	return Preset && GetProspectiveDeckCost(Preset) > GetDeckCostLimit();
}

void UMSSortieWidget::RefreshBriefingCountdown()
{
	if (!CountdownText) return;
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance
		? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (Online && Online->IsOnlineMode())
	{
		const AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
		if (State && State->bBattleEnded)
		{
			CountdownText->SetText(FText::FromString(TEXT("戦闘終了")));
		}
		else if (State && State->RemainingTime < State->MatchDuration)
		{
			CountdownText->SetText(FText::Format(FText::FromString(TEXT("戦闘中 {0}")), State->GetFormattedRemainingTime()));
		}
		else
		{
			CountdownText->SetText(FText::FromString(TEXT("出撃準備")));
		}
		return;
	}
	const float Remaining = BriefingWidget ? BriefingWidget->GetRemainingSeconds() : 0.0f;
	CountdownText->SetText(FText::Format(FText::FromString(TEXT("作戦開始まで {0}秒")), FText::AsNumber(FMath::CeilToInt(Remaining))));
}

FText UMSSortieWidget::GetDisplayedCountdownText() const
{
	return CountdownText ? CountdownText->GetText() : FText::GetEmpty();
}

UMSDeckSubsystem* UMSSortieWidget::ResolveDeckSubsystem() const
{
	if (DeckSubsystem) return DeckSubsystem;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		DeckSubsystem = GameInstance->GetSubsystem<UMSDeckSubsystem>();
	}
	// Native automation widgets have no game instance. Keep their test-only deck isolated.
	// UMSDeckSubsystemはClassWithin=GameInstanceのため、Outerも専用GameInstanceにする(ensure回避)
	if (!DeckSubsystem)
	{
		UGameInstance* TestOnlyGameInstance = NewObject<UGameInstance>(GEngine);
		DeckSubsystem = NewObject<UMSDeckSubsystem>(TestOnlyGameInstance);
	}
	return DeckSubsystem;
}

void UMSSortieWidget::HandleDeckChanged(const int32 ChangedSlotIndex)
{
	SelectedSlotIndex = GetSelectedSlotIndex();
	if (ChangedSlotIndex == INDEX_NONE)
	{
		RefreshSelectionStyle();
	}
	else
	{
		RefreshSlotCard(ChangedSlotIndex);
	}
	RefreshDeckCost();
}

void UMSSortieWidget::HandleSlot0Clicked() { SetSelectedSlotIndex(0); }
void UMSSortieWidget::HandleSlot1Clicked() { SetSelectedSlotIndex(1); }
void UMSSortieWidget::HandleSlot2Clicked() { SetSelectedSlotIndex(2); }
void UMSSortieWidget::HandleSlot3Clicked() { SetSelectedSlotIndex(3); }
void UMSSortieWidget::HandleCategoryAllClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::All); }
void UMSSortieWidget::HandleCategoryAssaultClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::Assault); }
void UMSSortieWidget::HandleCategoryHeavyClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::Heavy); }
void UMSSortieWidget::HandleCategorySupportClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::Support); }
void UMSSortieWidget::HandleCategoryBombardmentClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::Bombardment); }
void UMSSortieWidget::HandleCategorySniperClicked() { SelectCatalogCategory(EMSSortieCatalogCategory::Sniper); }
void UMSSortieWidget::HandleInsertClicked() { InsertSelectedCatalogPreset(); }
void UMSSortieWidget::HandleRemoveClicked() { RemovePresetFromSelectedSlot(); }
void UMSSortieWidget::HandleReplaceClicked() { ReplaceWithSelectedCatalogPreset(); }

void UMSSortieWidget::HandleSearchClicked() { RefreshFilteredCatalog(); }
void UMSSortieWidget::HandleSearchClearClicked() { if (SearchInput) SearchInput->SetText(FText::GetEmpty()); RefreshFilteredCatalog(); }
void UMSSortieWidget::HandleCostSortClicked()
{
	bCostDescending = !bCostDescending;
	if (CostSortText) CostSortText->SetText(FText::FromString(bCostDescending ? TEXT("COST：高い順") : TEXT("COST：低い順")));
	RefreshFilteredCatalog();
}
void UMSSortieWidget::HandleCloseClicked() { Close(); }

void UMSSortieWidget::HandleHangarClosed()
{
	RefreshSlotCard(SelectedSlotIndex); RefreshDeckCost(); Open();
}
