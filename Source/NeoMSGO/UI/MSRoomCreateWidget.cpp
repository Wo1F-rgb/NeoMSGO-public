// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSRoomCreateWidget.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/SpinBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMapDefinitionRow.h"
#include "Engine/Engine.h"
#include "Online/MSRoomRegistrySubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSRoomListEntryWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "Styling/SlateTypes.h"

namespace
{
	const FName RoomCreateScreenId(TEXT("RoomCreate"));
}

namespace MSRoomUI
{
	constexpr float RoomNameColumnWidth = 250.0f;
	constexpr float MapColumnWidth = 180.0f;
	constexpr float PlayerColumnWidth = 112.0f;
	constexpr float BotColumnWidth = 112.0f;
	constexpr float StateColumnWidth = 104.0f;
	constexpr float HostColumnWidth = 220.0f;

	const FLinearColor FieldBackground(0.018f, 0.06f, 0.075f, 1.0f);
	const FLinearColor FieldHovered(0.035f, 0.11f, 0.15f, 1.0f);
	const FLinearColor FieldFocused(0.025f, 0.12f, 0.14f, 1.0f);
	const FLinearColor FieldOutline(0.12f, 0.52f, 0.56f, 1.0f);
	const FLinearColor FieldFocusOutline(0.35f, 0.9f, 0.8f, 1.0f);
	const FLinearColor FieldText(0.94f, 0.98f, 0.9f, 1.0f);
	const FLinearColor FieldHint(0.58f, 0.75f, 0.72f, 1.0f);

	FSlateBrush MakeFieldBrush(const FLinearColor& Fill, const FLinearColor& Outline, const float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(4.0f, FSlateColor(Outline), OutlineWidth);
		return Brush;
	}

	void StyleEditableTextBox(UEditableTextBox* Input, const FText& Hint)
	{
		if (!Input) return;
		Input->SetHintText(Hint);
		FEditableTextBoxStyle Style = Input->GetWidgetStyle();
		Style.TextStyle.Font.Size = FMath::RoundToInt(MSUIStyle::GetFontSize(MSUIStyle::ETextRole::Body));
		Style.TextStyle.Font.TypefaceFontName = FName(TEXT("Regular"));
		Style.TextStyle.ColorAndOpacity = FSlateColor(FieldText);
		Style.ForegroundColor = FSlateColor(FieldText);
		Style.FocusedForegroundColor = FSlateColor(FieldText);
		Style.ReadOnlyForegroundColor = FSlateColor(FieldHint);
		Style.BackgroundColor = FSlateColor(FLinearColor::White);
		Style.Padding = FMargin(12.0f, 6.0f);
		Style.BackgroundImageNormal = MakeFieldBrush(FieldBackground, FieldOutline, 1.0f);
		Style.BackgroundImageHovered = MakeFieldBrush(FieldHovered, FieldFocusOutline, 1.0f);
		Style.BackgroundImageFocused = MakeFieldBrush(FieldFocused, FieldFocusOutline, 2.0f);
		Style.BackgroundImageReadOnly = MakeFieldBrush(FieldBackground, FieldOutline, 1.0f);
		Input->SetWidgetStyle(Style);
	}

	void StyleComboBox(UComboBoxString* Combo)
	{
		if (!Combo) return;
		FSlateFontInfo Font = Combo->GetFont();
		Font.Size = FMath::RoundToInt(MSUIStyle::GetFontSize(MSUIStyle::ETextRole::Body));
		Font.TypefaceFontName = FName(TEXT("Regular"));
		Combo->Font = Font;
		Combo->ForegroundColor = FSlateColor(FieldText);

		FComboBoxStyle Style = Combo->GetWidgetStyle();
		FButtonStyle& ButtonStyle = Style.ComboButtonStyle.ButtonStyle;
		ButtonStyle.SetNormal(MakeFieldBrush(FieldBackground, FieldOutline, 1.0f));
		ButtonStyle.SetHovered(MakeFieldBrush(FieldHovered, FieldFocusOutline, 1.0f));
		ButtonStyle.SetPressed(MakeFieldBrush(FieldFocused, FieldFocusOutline, 2.0f));
		ButtonStyle.SetDisabled(MakeFieldBrush(FieldBackground, FieldOutline, 1.0f));
		Style.ComboButtonStyle.SetContentPadding(FMargin(12.0f, 6.0f));
		Style.SetContentPadding(FMargin(12.0f, 6.0f));
		Style.SetMenuRowPadding(FMargin(8.0f, 4.0f));
		Style.ComboButtonStyle.SetMenuBorderBrush(MakeFieldBrush(
			FLinearColor(0.012f, 0.035f, 0.045f, 1.0f), FieldFocusOutline, 1.0f));
		Style.ComboButtonStyle.SetMenuBorderPadding(FMargin(1.0f));
		Combo->SetWidgetStyle(Style);

		FTableRowStyle ItemStyle = Combo->GetItemStyle();
		const FSlateBrush RowBrush = MakeFieldBrush(FLinearColor(0.018f, 0.055f, 0.07f, 1.0f),
			FieldOutline, 1.0f);
		const FSlateBrush HoverBrush = MakeFieldBrush(FLinearColor(0.06f, 0.2f, 0.22f, 1.0f),
			FieldFocusOutline, 1.0f);
		const FSlateBrush SelectedBrush = MakeFieldBrush(FLinearColor(0.08f, 0.3f, 0.32f, 1.0f),
			FieldFocusOutline, 2.0f);
		ItemStyle.SetEvenRowBackgroundBrush(RowBrush);
		ItemStyle.SetOddRowBackgroundBrush(RowBrush);
		ItemStyle.SetEvenRowBackgroundHoveredBrush(HoverBrush);
		ItemStyle.SetOddRowBackgroundHoveredBrush(HoverBrush);
		ItemStyle.SetActiveBrush(SelectedBrush);
		ItemStyle.SetActiveHoveredBrush(SelectedBrush);
		ItemStyle.SetTextColor(FSlateColor(FieldText));
		ItemStyle.SetSelectedTextColor(FSlateColor(FieldText));
		Combo->SetItemStyle(ItemStyle);
	}

	void StyleSpinBox(USpinBox* SpinBox)
	{
		if (!SpinBox) return;
		SpinBox->SetMinFractionalDigits(0);
		SpinBox->SetMaxFractionalDigits(0);
		SpinBox->SetAlwaysUsesDeltaSnap(true);
		SpinBox->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font = SpinBox->GetFont();
		Font.Size = FMath::RoundToInt(MSUIStyle::GetFontSize(MSUIStyle::ETextRole::Body));
		Font.TypefaceFontName = FName(TEXT("Regular"));
		SpinBox->SetFont(Font);
		SpinBox->SetForegroundColor(FSlateColor(FieldText));
		FSpinBoxStyle Style = SpinBox->GetWidgetStyle();
		Style.SetBackgroundBrush(MakeFieldBrush(FieldBackground, FieldOutline, 1.0f));
		Style.SetHoveredBackgroundBrush(MakeFieldBrush(FieldHovered, FieldFocusOutline, 1.0f));
		Style.SetActiveBackgroundBrush(MakeFieldBrush(FieldFocused, FieldFocusOutline, 2.0f));
		Style.SetActiveFillBrush(MakeFieldBrush(
			FLinearColor(0.08f, 0.3f, 0.32f, 1.0f), FieldFocusOutline, 1.0f));
		Style.SetHoveredFillBrush(MakeFieldBrush(
			FLinearColor(0.045f, 0.18f, 0.21f, 1.0f), FieldOutline, 1.0f));
		Style.SetInactiveFillBrush(MakeFieldBrush(
			FLinearColor(0.018f, 0.055f, 0.07f, 1.0f), FieldOutline, 1.0f));
		Style.SetForegroundColor(FSlateColor(FieldText));
		Style.SetTextPadding(FMargin(12.0f, 6.0f));
		SpinBox->SetWidgetStyle(Style);
	}

	UTextBlock* MakeLabel(UWidgetTree* Tree, const FString& Text, const FName Name = NAME_None)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		return Label;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FString& Text, const FName Name)
	{
		UTextBlock* Label = MakeLabel(Tree, Text);
		Label->SetJustification(ETextJustify::Center);
		Label->SetMargin(FMargin(18, 10));
		return MSUIStyle::MakeCapsuleButton(Tree, Label, FLinearColor(0.025f, 0.13f, 0.17f, 1), Name);
	}
}

void UMSRoomCreateWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSRoomCreateWidget::NativeDestruct()
{
	Close();
	Super::NativeDestruct();
}

void UMSRoomCreateWidget::BuildLayout()
{
	if (WidgetTree->RootWidget)
	{
		return;
	}
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RoomScreenBackground"));
	Background->SetBrushColor(FLinearColor(0.01f, 0.025f, 0.045f, 0.98f));
	Background->SetPadding(MSOnline::IsEnabled() ? FMargin(24.0f) : FMargin(120.0f, 50.0f));
	WidgetTree->RootWidget = Background;
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	if (MSOnline::IsEnabled())
	{
		Background->SetHorizontalAlignment(HAlign_Center);
		Background->SetVerticalAlignment(VAlign_Center);
		UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("RoomScreenFit"));
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::Both);
		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(1120);
		Width->SetHeightOverride(760);
		UBorder* Inner = nullptr;
		Width->SetContent(MSUIStyle::MakeFramedPanel(WidgetTree, Inner, Root, true,
			MSUIStyleColor::FrameAccent, FLinearColor(0.01f, 0.035f, 0.05f, 1), FMargin(36)));
		Fit->SetContent(Width);
		Background->SetContent(Fit);
	}
	else Background->SetContent(Root);

	CreatePanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomCreatePanel"));
	Root->AddChildToVerticalBox(CreatePanel);
	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("ルーム作成"), TEXT("RoomCreateTitle")));
	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("ルーム名")));
	RoomNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomNameInput"));
	MSRoomUI::StyleEditableTextBox(RoomNameInput, FText::FromString(TEXT("ルーム名を入力")));
	CreatePanel->AddChildToVerticalBox(RoomNameInput)->SetPadding(FMargin(0, 4, 0, 12));

	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("マップ")));
	MapComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RoomMapComboBox"));
	for (const FMSMapDefinitionRow& Map : FMSMapDefinitionRow::GetVisibleMapDefinitions())
	{
		if (!Map.bPlayable || Map.LevelPackagePath.IsEmpty()) continue;
		PlayableMapIds.Add(Map.MapId);
		MapComboBox->AddOption(Map.DisplayName.ToString());
	}
	MSRoomUI::StyleComboBox(MapComboBox);
	CreatePanel->AddChildToVerticalBox(MapComboBox)->SetPadding(FMargin(0, 4, 0, 12));

	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("ルール")));
	RuleComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RoomRuleComboBox"));
	for (const FMSBattleRuleRow& Rule : UMSLobbyWidget::GetBattleRules()) RuleComboBox->AddOption(Rule.Name.ToString());
	MSRoomUI::StyleComboBox(RuleComboBox);
	CreatePanel->AddChildToVerticalBox(RuleComboBox)->SetPadding(FMargin(0, 4, 0, 12));

	FillTeamsWithBotsCheckBox = WidgetTree->ConstructWidget<UCheckBox>(
		UCheckBox::StaticClass(), TEXT("FillTeamsWithBotsCheckBox"));
	FillTeamsWithBotsCheckBox->SetContent(
		MSRoomUI::MakeLabel(WidgetTree, TEXT("25対25（空きをBOTで補充）")));
	FillTeamsWithBotsCheckBox->SetIsChecked(true);
	FillTeamsWithBotsCheckBox->OnCheckStateChanged.AddDynamic(
		this, &UMSRoomCreateWidget::HandleFillTeamsWithBotsChanged);
	CreatePanel->AddChildToVerticalBox(FillTeamsWithBotsCheckBox)->SetPadding(FMargin(0, 4, 0, 8));

	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("敵Bot数 (0-11) / 人間と合計12（手動設定時）")));
	BotCountSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("RoomBotCountSpinBox"));
	BotCountSpinBox->SetMinValue(0.0f);
	BotCountSpinBox->SetMaxValue(11.0f);
	BotCountSpinBox->SetMinSliderValue(0.0f);
	BotCountSpinBox->SetMaxSliderValue(11.0f);
	BotCountSpinBox->SetDelta(1.0f);
	BotCountSpinBox->SetValue(0.0f);
	MSRoomUI::StyleSpinBox(BotCountSpinBox);
	BotCountSpinBox->SetIsEnabled(false);
	CreatePanel->AddChildToVerticalBox(BotCountSpinBox)->SetPadding(FMargin(0, 4, 0, 12));

	CreatePanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("BOT難易度")));
	DifficultyComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RoomDifficultyComboBox"));
	DifficultyComboBox->AddOption(TEXT("Easy"));
	DifficultyComboBox->AddOption(TEXT("Normal"));
	DifficultyComboBox->AddOption(TEXT("Hard"));
	DifficultyComboBox->AddOption(TEXT("Mixed (Easy 60% / Normal 30% / Hard 10%)"));
	DifficultyComboBox->SetSelectedIndex(1);
	MSRoomUI::StyleComboBox(DifficultyComboBox);
	CreatePanel->AddChildToVerticalBox(DifficultyComboBox)->SetPadding(FMargin(0, 4, 0, 18));
	RoomStartButton = MSRoomUI::MakeButton(WidgetTree, TEXT("ルーム開始"), TEXT("RoomStartButton"));
	RoomStartButton->OnClicked.AddDynamic(this, &UMSRoomCreateWidget::HandleStartClicked);
	CreatePanel->AddChildToVerticalBox(RoomStartButton)->SetPadding(FMargin(0, 6));

	JoinPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomJoinPanel"));
	Root->AddChildToVerticalBox(JoinPanel);
	JoinPanel->AddChildToVerticalBox(MSRoomUI::MakeLabel(WidgetTree, TEXT("ルーム参加"), TEXT("RoomJoinTitle")));

	UHorizontalBox* SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("RoomSearchRow"));
	JoinPanel->AddChildToVerticalBox(SearchRow)->SetPadding(FMargin(0, 10, 0, 8));
	SearchTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(
		UEditableTextBox::StaticClass(), TEXT("RoomSearchTextBox"));
	MSRoomUI::StyleEditableTextBox(SearchTextBox, FText::FromString(TEXT("ルーム名 / ホスト名で検索")));
	SearchTextBox->OnTextChanged.AddDynamic(this, &UMSRoomCreateWidget::HandleSearchTextChanged);
	SearchRow->AddChildToHorizontalBox(SearchTextBox)->SetSize(
		FSlateChildSize(ESlateSizeRule::Fill));
	RefreshButton = MSRoomUI::MakeButton(WidgetTree, TEXT("更新"), TEXT("RoomRefreshButton"));
	RefreshButton->OnClicked.AddDynamic(this, &UMSRoomCreateWidget::HandleRefreshClicked);
	SearchRow->AddChildToHorizontalBox(RefreshButton)->SetPadding(FMargin(10, 0, 0, 0));

	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("RoomListHeader"));
	JoinPanel->AddChildToVerticalBox(HeaderRow)->SetPadding(FMargin(12, 0, 12, 4));
	auto AddHeader = [this, HeaderRow](const FString& Text, const float Width)
	{
		UTextBlock* Header = MSRoomUI::MakeLabel(WidgetTree, Text);
		// Keep the heading baseline and width identical to the row below. Fixed
		// desired widths also prevent the ScrollBox from collapsing empty Fill slots.
		Header->SetMargin(FMargin(8.0f, 0.0f));
		USizeBox* HeaderSize = WidgetTree->ConstructWidget<USizeBox>();
		HeaderSize->SetWidthOverride(Width);
		HeaderSize->SetContent(Header);
		HeaderRow->AddChildToHorizontalBox(HeaderSize);
	};
	AddHeader(TEXT("ルーム名"), MSRoomUI::RoomNameColumnWidth);
	AddHeader(TEXT("マップ"), MSRoomUI::MapColumnWidth);
	AddHeader(TEXT("人間"), MSRoomUI::PlayerColumnWidth);
	AddHeader(TEXT("BOT"), MSRoomUI::BotColumnWidth);
	AddHeader(TEXT("状態"), MSRoomUI::StateColumnWidth);
	AddHeader(TEXT("ホスト"), MSRoomUI::HostColumnWidth);

	RoomListScrollBox = WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), TEXT("RoomListScrollBox"));
	USizeBox* RoomListSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("RoomListSize"));
	RoomListSize->SetHeightOverride(MSOnline::IsEnabled() ? 390.0f : 260.0f);
	RoomListSize->SetContent(RoomListScrollBox);
	JoinPanel->AddChildToVerticalBox(RoomListSize);

	AdvancedJoinToggle = MSRoomUI::MakeButton(
		WidgetTree, TEXT("詳細設定 ▶"), TEXT("AdvancedJoinToggle"));
	AdvancedJoinToggle->OnClicked.AddDynamic(
		this, &UMSRoomCreateWidget::HandleAdvancedJoinClicked);
	JoinPanel->AddChildToVerticalBox(AdvancedJoinToggle)->SetPadding(FMargin(0, 12, 0, 4));

	AdvancedJoinPanel = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("AdvancedJoinPanel"));
	JoinPanel->AddChildToVerticalBox(AdvancedJoinPanel);
	AdvancedJoinPanel->AddChildToVerticalBox(
		MSRoomUI::MakeLabel(WidgetTree, TEXT("ホストIPアドレス"), TEXT("RoomJoinAddressLabel")));
	JoinAddressInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomJoinAddressInput"));
	JoinAddressInput->SetText(FText::FromString(TEXT("127.0.0.1")));
	MSRoomUI::StyleEditableTextBox(JoinAddressInput, FText::FromString(TEXT("接続先アドレス")));
	AdvancedJoinPanel->AddChildToVerticalBox(JoinAddressInput)->SetPadding(FMargin(0, 4, 0, 10));
	JoinButton = MSRoomUI::MakeButton(WidgetTree, TEXT("参加"), TEXT("RoomJoinButton"));
	JoinButton->OnClicked.AddDynamic(this, &UMSRoomCreateWidget::HandleJoinClicked);
	AdvancedJoinPanel->AddChildToVerticalBox(JoinButton)->SetPadding(FMargin(0, 6));
	AdvancedJoinPanel->SetVisibility(ESlateVisibility::Collapsed);

	UButton* BackButton = MSRoomUI::MakeButton(WidgetTree, TEXT("戻る"), TEXT("RoomBackButton"));
	BackButton->OnClicked.AddDynamic(this, &UMSRoomCreateWidget::HandleBackClicked);
	Root->AddChildToVerticalBox(BackButton)->SetPadding(FMargin(0, 16, 0, 0));
	OnlineStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OnlineRoomStatus"));
	Root->AddChildToVerticalBox(OnlineStatusText);
	ShowCreate(true);
}

void UMSRoomCreateWidget::OpenCreate()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	if (!WidgetTree->RootWidget) BuildLayout();
	StopRoomPolling();
	BindRoomStatus();
	ShowCreate(true);
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Account = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	FString Username = TEXT("Player");
	if (Account && Account->IsOnlineMode())
	{
		Username = Account->GetIdentity().DisplayName.IsEmpty()
			? TEXT("プレイヤー") : Account->GetIdentity().DisplayName;
	}
	else if (Instance && !Instance->LoggedInUsername.IsEmpty())
	{
		Username = Instance->LoggedInUsername;
	}
	RoomNameInput->SetText(FText::FromString(FString::Printf(TEXT("%sの部屋"), *Username)));
	int32 MapIndex = Instance ? PlayableMapIds.IndexOfByKey(Instance->SelectedMapId) : INDEX_NONE;
	MapComboBox->SetSelectedIndex(MapIndex == INDEX_NONE ? 0 : MapIndex);
	RuleComboBox->SetSelectedIndex(0);
	if (FillTeamsWithBotsCheckBox)
	{
		FillTeamsWithBotsCheckBox->SetIsChecked(true);
	}
	if (BotCountSpinBox) BotCountSpinBox->SetIsEnabled(false);
	if (RoomStartButton) RoomStartButton->SetIsEnabled(true);
	if (OnlineStatusText) OnlineStatusText->SetText(FText::GetEmpty());
	if (!IsInViewport() && GEngine && GEngine->GameViewport) AddToViewport(60);
	SetVisibility(ESlateVisibility::Visible);
	if (!bScreenRegistered)
	{
		if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		{
			Lock->PushScreen(RoomCreateScreenId, FMSUICloseRequestDelegate::CreateWeakLambda(
				this, [this]() { HandleBackClicked(); return true; }));
			bScreenRegistered = true;
#if WITH_DEV_AUTOMATION_TESTS
			++PushScreenCountForAutomationTests;
#endif
		}
	}
	RefreshStatusPresentation();
}

void UMSRoomCreateWidget::OpenCreateForMap(const FName MapId)
{
	OpenCreate();
	const int32 MapIndex = PlayableMapIds.IndexOfByKey(MapId);
	if (MapComboBox && MapIndex != INDEX_NONE)
	{
		MapComboBox->SetSelectedIndex(MapIndex);
	}
}

void UMSRoomCreateWidget::OpenJoin()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	if (!WidgetTree->RootWidget) BuildLayout();
	ShowCreate(false);
	BindRoomStatus();
	if (!IsInViewport() && GEngine && GEngine->GameViewport) AddToViewport(60);
	SetVisibility(ESlateVisibility::Visible);
	if (!bScreenRegistered)
	{
		if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		{
			Lock->PushScreen(RoomCreateScreenId, FMSUICloseRequestDelegate::CreateWeakLambda(
				this, [this]() { HandleBackClicked(); return true; }));
			bScreenRegistered = true;
#if WITH_DEV_AUTOMATION_TESTS
			++PushScreenCountForAutomationTests;
#endif
		}
	}
	if (auto* Title = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("RoomJoinTitle"))))
	{
		Title->SetText(FText::FromString(TEXT("ルームを検索")));
		Title->SetColorAndOpacity(MSUIStyleColor::FrameAccent);
		Title->SetMargin(FMargin(0, 0, 0, 16));
	}
	bAdvancedJoinExpanded = false;
	if (AdvancedJoinToggle)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(AdvancedJoinToggle->GetContent()))
			Label->SetText(FText::FromString(TEXT("詳細設定 ▶")));
	}
	if (MSOnline::IsEnabled())
	{
		// Online rooms are selected from the public registry. Direct-IP is an offline
		// fallback only; keeping it collapsed prevents an address from being mistaken
		// for an online room ID.
		if (AdvancedJoinPanel) AdvancedJoinPanel->SetVisibility(ESlateVisibility::Collapsed);
		if (AdvancedJoinToggle) AdvancedJoinToggle->SetVisibility(ESlateVisibility::Collapsed);
		if (JoinAddressInput) JoinAddressInput->SetVisibility(ESlateVisibility::Collapsed);
		if (JoinButton) JoinButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		if (AdvancedJoinToggle) AdvancedJoinToggle->SetVisibility(ESlateVisibility::Visible);
		if (AdvancedJoinPanel) AdvancedJoinPanel->SetVisibility(ESlateVisibility::Collapsed);
		if (JoinAddressInput) JoinAddressInput->SetVisibility(ESlateVisibility::Visible);
		if (JoinButton) JoinButton->SetVisibility(ESlateVisibility::Visible);
	}
	if (RoomListScrollBox) RoomListScrollBox->SetVisibility(ESlateVisibility::Visible);
	if (SearchTextBox) SearchTextBox->SetVisibility(ESlateVisibility::Visible);
	if (RefreshButton) RefreshButton->SetVisibility(ESlateVisibility::Visible);
	if (JoinButton) JoinButton->SetIsEnabled(true);
	StartRoomPolling();
	RefreshStatusPresentation();
}

void UMSRoomCreateWidget::Close()
{
	StopRoomPolling();
	UnbindRoomStatus();
	SetVisibility(ESlateVisibility::Collapsed);
	if (bScreenRegistered)
	{
		if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		{
			Lock->PopScreen(RoomCreateScreenId);
#if WITH_DEV_AUTOMATION_TESTS
			++PopScreenCountForAutomationTests;
#endif
		}
		bScreenRegistered = false;
	}
}

void UMSRoomCreateWidget::ShowCreate(const bool bCreate)
{
	if (CreatePanel) CreatePanel->SetVisibility(bCreate ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (JoinPanel) JoinPanel->SetVisibility(bCreate ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UMSRoomCreateWidget::StartRoomPolling()
{
	UGameInstance* GI = GetGameInstance();
	UMSRoomRegistrySubsystem* Registry = GI
		? GI->GetSubsystem<UMSRoomRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	Registry->OnRoomListUpdated.RemoveDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListUpdated);
	Registry->OnRoomListUpdated.AddDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListUpdated);
	Registry->OnRoomListStatusChanged.RemoveDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListStatusChanged);
	Registry->OnRoomListStatusChanged.AddDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListStatusChanged);
	Registry->StartPolling(RegistryAddress);
	HandleRoomListUpdated(Registry->GetLastKnownRooms());
	RefreshStatusPresentation();
}

void UMSRoomCreateWidget::StopRoomPolling()
{
	UGameInstance* GI = GetGameInstance();
	UMSRoomRegistrySubsystem* Registry = GI
		? GI->GetSubsystem<UMSRoomRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	Registry->StopPolling();
	Registry->OnRoomListUpdated.RemoveDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListUpdated);
	Registry->OnRoomListStatusChanged.RemoveDynamic(
		this, &UMSRoomCreateWidget::HandleRoomListStatusChanged);
}

void UMSRoomCreateWidget::BindRoomStatus()
{
	UGameInstance* GI = GetGameInstance();
	UMSRoomSubsystem* Rooms = GI ? GI->GetSubsystem<UMSRoomSubsystem>() : nullptr;
	if (!Rooms) return;
	Rooms->OnRoomStatusChanged.RemoveDynamic(this, &UMSRoomCreateWidget::HandleRoomStatusChanged);
	Rooms->OnRoomStatusChanged.AddDynamic(this, &UMSRoomCreateWidget::HandleRoomStatusChanged);
}

void UMSRoomCreateWidget::UnbindRoomStatus()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSRoomSubsystem* Rooms = GI->GetSubsystem<UMSRoomSubsystem>())
			Rooms->OnRoomStatusChanged.RemoveDynamic(this, &UMSRoomCreateWidget::HandleRoomStatusChanged);
	}
}

void UMSRoomCreateWidget::HandleRoomListUpdated(TArray<FMSRoomDescriptor> Rooms)
{
	LatestRooms = MoveTemp(Rooms);
	RebuildRoomList();
	RefreshStatusPresentation();
}

void UMSRoomCreateWidget::RebuildRoomList()
{
	if (!RoomListScrollBox)
	{
		return;
	}
	RoomListScrollBox->ClearChildren();
	DisplayedRoomEntries.Reset();
	const FString Search = SearchTextBox
		? SearchTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	for (const FMSRoomDescriptor& Room : LatestRooms)
	{
		if (!Search.IsEmpty()
			&& !Room.Settings.RoomName.Contains(Search, ESearchCase::IgnoreCase)
			&& !Room.HostDisplayName.Contains(Search, ESearchCase::IgnoreCase))
		{
			continue;
		}
		UMSRoomListEntryWidget* Entry = WidgetTree->ConstructWidget<UMSRoomListEntryWidget>(
			UMSRoomListEntryWidget::StaticClass());
		Entry->SetRoomDescriptor(Room);
		Entry->OnRoomSelected.AddDynamic(this, &UMSRoomCreateWidget::HandleRoomSelected);
		RoomListScrollBox->AddChild(Entry);
		DisplayedRoomEntries.Add(Entry);
	}
}

void UMSRoomCreateWidget::RefreshStatusPresentation()
{
	if (!OnlineStatusText) return;
	UGameInstance* GI = GetGameInstance();
	UMSRoomSubsystem* Rooms = GI ? GI->GetSubsystem<UMSRoomSubsystem>() : nullptr;
	UMSRoomRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UMSRoomRegistrySubsystem>() : nullptr;
	const bool bShowingCreateForm = CreatePanel && CreatePanel->GetVisibility() == ESlateVisibility::Visible;
	if (Rooms && Rooms->IsRoomOperationPending())
	{
		OnlineStatusText->SetText(Rooms->GetRoomStatusText());
	}
	else if (bShowingCreateForm)
	{
		// The registry status belongs to the browser. Keep the create form clear
		// unless the room subsystem has a meaningful failure/status to report.
		const FText Status = Rooms ? Rooms->GetRoomStatusText() : FText::GetEmpty();
		OnlineStatusText->SetText(Status);
	}
	else if (MSOnline::IsEnabled() && Registry)
	{
		const FText RegistryError = Registry->GetLastErrorText();
		if (Registry->IsRequestPending())
		{
			OnlineStatusText->SetText(FText::FromString(TEXT("ルーム一覧を取得中…")));
		}
		else if (!RegistryError.IsEmpty())
		{
			OnlineStatusText->SetText(FText::Format(
				FText::FromString(TEXT("取得失敗: {0}　「更新」で再検索")), RegistryError));
		}
		else if (LatestRooms.IsEmpty())
		{
			OnlineStatusText->SetText(FText::FromString(
				TEXT("公開中のルームなし。作成または更新で再検索できます。")));
		}
		else
		{
			OnlineStatusText->SetText(FText::Format(
				FText::FromString(TEXT("公開ルーム {0}件")), FText::AsNumber(LatestRooms.Num())));
		}
	}
	else if (Rooms)
	{
		const FText Status = Rooms->GetRoomStatusText();
		if (!Status.IsEmpty()) OnlineStatusText->SetText(Status);
	}
	OnlineStatusText->SetAutoWrapText(true);
	// Keep the two-line status/footer inside the fixed framed panel at 1280x720.
	OnlineStatusText->SetMargin(FMargin(0, 0, 0, 0));
	if (Rooms)
	{
		const bool bPending = Rooms->IsRoomOperationPending();
		if (RoomStartButton) RoomStartButton->SetIsEnabled(!bPending);
		if (JoinButton && JoinButton->GetVisibility() != ESlateVisibility::Collapsed)
			JoinButton->SetIsEnabled(!bPending);
		for (UMSRoomListEntryWidget* Entry : DisplayedRoomEntries)
			if (Entry && Entry->GetSelectButton()) Entry->GetSelectButton()->SetIsEnabled(!bPending);
	}
}

void UMSRoomCreateWidget::HandleStartClicked()
{
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	UMSRoomSubsystem* Rooms = Instance ? Instance->GetSubsystem<UMSRoomSubsystem>() : nullptr;
	const int32 MapIndex = MapComboBox ? MapComboBox->GetSelectedIndex() : INDEX_NONE;
	if (!Rooms || !PlayableMapIds.IsValidIndex(MapIndex)) return;
	if (Rooms->IsRoomOperationPending()) return;
	FMSRoomSettings Settings;
	Settings.RoomName = RoomNameInput ? RoomNameInput->GetText().ToString() : FString();
	Settings.RoomName.TrimStartAndEndInline();
	if (Settings.RoomName.IsEmpty())
	{
		if (OnlineStatusText) OnlineStatusText->SetText(FText::FromString(TEXT("部屋名を入力してください。")));
		return;
	}
	Settings.MapId = PlayableMapIds[MapIndex];
	Settings.RuleIndex = RuleComboBox ? FMath::Max(0, RuleComboBox->GetSelectedIndex()) : 0;
	Settings.BotCount = FMath::Clamp(
		BotCountSpinBox ? FMath::RoundToInt(BotCountSpinBox->GetValue()) : 0, 0, 11);
	Settings.bFillTeamsWithBots = FillTeamsWithBotsCheckBox
		? FillTeamsWithBotsCheckBox->IsChecked() : true;
	Settings.BotDifficulty = static_cast<EMSBotDifficulty>(FMath::Clamp(
		DifficultyComboBox ? DifficultyComboBox->GetSelectedIndex() : 1, 0, 3));
	if (RoomStartButton) RoomStartButton->SetIsEnabled(false);
	const bool bStarted = Rooms->HostRoom(Settings);
	if (!bStarted)
	{
		if (RoomStartButton) RoomStartButton->SetIsEnabled(true);
		RefreshStatusPresentation();
	}
	else
	{
		RefreshStatusPresentation();
	}
}

void UMSRoomCreateWidget::HandleJoinClicked()
{
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const FString Address = JoinAddressInput ? JoinAddressInput->GetText().ToString() : FString();
#if WITH_DEV_AUTOMATION_TESTS
	if (JoinHandlerForAutomationTests)
	{
		JoinHandlerForAutomationTests(Address);
		return;
	}
#endif
	if (UMSRoomSubsystem* Rooms = Instance ? Instance->GetSubsystem<UMSRoomSubsystem>() : nullptr)
	{
		if (Rooms->IsRoomOperationPending()) return;
		const bool bStarted = Rooms->JoinRoom(Address);
		if (JoinButton) JoinButton->SetIsEnabled(!bStarted);
		RefreshStatusPresentation();
	}
}

void UMSRoomCreateWidget::HandleRoomSelected(FMSRoomDescriptor Room)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (JoinHandlerForAutomationTests)
	{
		JoinHandlerForAutomationTests(Room.Address);
		return;
	}
#endif
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	if (UMSRoomSubsystem* Rooms = Instance ? Instance->GetSubsystem<UMSRoomSubsystem>() : nullptr)
	{
		if (Rooms->IsRoomOperationPending()) return;
		// Online descriptors carry the registry's room identity and instance token;
		// the public address is only used by the offline direct-IP flow.
		const bool bStarted = MSOnline::IsEnabled()
			? Rooms->JoinOnlineRoom(
				Room.RoomId.ToString(EGuidFormats::DigitsWithHyphensLower), Room.InstanceId)
			: Rooms->JoinRoom(Room.Address);
		if (bStarted && JoinButton) JoinButton->SetIsEnabled(false);
		RefreshStatusPresentation();
	}
}

void UMSRoomCreateWidget::HandleSearchTextChanged(const FText&)
{
	// Search is intentionally local; never call RefreshNow or PollRooms from this path.
	RebuildRoomList();
}

void UMSRoomCreateWidget::HandleRefreshClicked()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSRoomRegistrySubsystem* Registry = GI->GetSubsystem<UMSRoomRegistrySubsystem>())
		{
			Registry->RefreshNow();
			RefreshStatusPresentation();
		}
	}
}

void UMSRoomCreateWidget::HandleAdvancedJoinClicked()
{
	if (MSOnline::IsEnabled()) return;
	bAdvancedJoinExpanded = !bAdvancedJoinExpanded;
	if (AdvancedJoinPanel)
	{
		AdvancedJoinPanel->SetVisibility(bAdvancedJoinExpanded
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (AdvancedJoinToggle)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(AdvancedJoinToggle->GetContent()))
		{
			Label->SetText(FText::FromString(
				bAdvancedJoinExpanded ? TEXT("詳細設定 ▼") : TEXT("詳細設定 ▶")));
		}
	}
}

void UMSRoomCreateWidget::HandleBackClicked()
{
	Close();
	if (LobbyWidget) LobbyWidget->Open();
}

int32 UMSRoomCreateWidget::GetDisplayedRoomCount() const
{
	return DisplayedRoomEntries.Num();
}

UMSRoomListEntryWidget* UMSRoomCreateWidget::GetDisplayedRoomEntry(const int32 Index) const
{
	return DisplayedRoomEntries.IsValidIndex(Index) ? DisplayedRoomEntries[Index] : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSRoomCreateWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSRoomCreateWidget::SetSearchTextForAutomationTests(const FString& Search)
{
	if (SearchTextBox) SearchTextBox->SetText(FText::FromString(Search));
	HandleSearchTextChanged(FText::FromString(Search));
}
#endif

void UMSRoomCreateWidget::HandleOnlineRequestFinished(FName Operation, bool bSuccess, FText Message, int64 Generation)
{
 auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
 if (Operation != TEXT("game_ticket") || !Account || Account->GetAuthGeneration() != Generation) return;
 if (OnlineStatusText) OnlineStatusText->SetText(bSuccess ? FText::FromString(TEXT("サーバーへ接続中…")) : Message);
 if (!bSuccess && JoinButton) JoinButton->SetIsEnabled(true);
}

void UMSRoomCreateWidget::HandleFillTeamsWithBotsChanged(const bool bIsChecked)
{
	if (BotCountSpinBox)
	{
		// Keep the manually selected value intact so turning the default mode off
		// returns to the existing 0..11 enemy-BOT setting without data loss.
		BotCountSpinBox->SetIsEnabled(!bIsChecked);
	}
}

void UMSRoomCreateWidget::HandleRoomListStatusChanged()
{
	RefreshStatusPresentation();
}

void UMSRoomCreateWidget::HandleRoomStatusChanged()
{
	RefreshStatusPresentation();
}
