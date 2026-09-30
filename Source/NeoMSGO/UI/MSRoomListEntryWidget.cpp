// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSRoomListEntryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/MSMapDefinitionRow.h"
#include "UI/MSPixelUI.h"
#include "UI/MSUIStyle.h"

namespace MSRoomListEntry
{
	constexpr float RoomNameColumnWidth = 250.0f;
	constexpr float MapColumnWidth = 180.0f;
	constexpr float PlayerColumnWidth = 112.0f;
	constexpr float BotColumnWidth = 112.0f;
	constexpr float StateColumnWidth = 104.0f;
	constexpr float HostColumnWidth = 220.0f;

	UTextBlock* MakeColumn(UWidgetTree* Tree, const FName Name)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.93f, 0.88f, 1.0f)));
		Text->SetMargin(FMargin(8.0f, 0.0f));
		Text->SetAutoWrapText(false);
		Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		return Text;
	}

	FText StateLabel(const EMSRoomState State)
	{
		switch (State)
		{
		case EMSRoomState::Waiting: return FText::FromString(TEXT("待機中"));
		case EMSRoomState::InProgress: return FText::FromString(TEXT("対戦中"));
		case EMSRoomState::Full: return FText::FromString(TEXT("満員"));
		default: return FText::FromString(TEXT("応答なし"));
		}
	}
}

void UMSRoomListEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildLayout();
}

void UMSRoomListEntryWidget::BuildLayout()
{
	if (WidgetTree->RootWidget)
	{
		return;
	}
	SelectButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("RoomListEntryButton"));
	MSPixelUI::ClearButtonBrush(SelectButton);
	MSUIStyle::ApplyButtonSound(SelectButton);
	SelectButton->OnClicked.AddDynamic(this, &UMSRoomListEntryWidget::HandleClicked);
	WidgetTree->RootWidget = SelectButton;

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.035f, 0.075f, 0.12f, 0.98f));
	Card->SetPadding(FMargin(14.0f, 8.0f));
	SelectButton->SetContent(Card);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Card->SetContent(Row);

	RoomNameText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryName"));
	MapText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryMap"));
	PlayerCountText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryPlayers"));
	BotCountText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryBots"));
	StateText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryState"));
	HostText = MSRoomListEntry::MakeColumn(WidgetTree, TEXT("RoomEntryHost"));
	auto AddFixedColumn = [this, Row](UTextBlock* Text, const float Width)
	{
		USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>();
		Column->SetWidthOverride(Width);
		Column->SetContent(Text);
		Row->AddChildToHorizontalBox(Column);
	};
	// Fixed desired widths make the row measurable inside a ScrollBox. Fill slots alone
	// have no desired width, which caused the map and host fields to collapse together.
	AddFixedColumn(RoomNameText, MSRoomListEntry::RoomNameColumnWidth);
	AddFixedColumn(MapText, MSRoomListEntry::MapColumnWidth);
	AddFixedColumn(PlayerCountText, MSRoomListEntry::PlayerColumnWidth);
	AddFixedColumn(BotCountText, MSRoomListEntry::BotColumnWidth);
	AddFixedColumn(StateText, MSRoomListEntry::StateColumnWidth);
	AddFixedColumn(HostText, MSRoomListEntry::HostColumnWidth);
}

void UMSRoomListEntryWidget::SetRoomDescriptor(const FMSRoomDescriptor& InDescriptor)
{
	BuildLayout();
	RoomDescriptor = InDescriptor;
	RefreshText();
}

void UMSRoomListEntryWidget::RefreshText()
{
	const FMSMapDefinitionRow* Map = FMSMapDefinitionRow::FindMapById(RoomDescriptor.Settings.MapId);
	if (RoomNameText) RoomNameText->SetText(FText::FromString(RoomDescriptor.Settings.RoomName));
	if (MapText) MapText->SetText(Map ? Map->DisplayName
		: FText::FromName(RoomDescriptor.Settings.MapId));
	const int32 EnemyBots = FMath::Clamp(RoomDescriptor.Settings.BotCount, 0, 11);
	const int32 MaxHumans = RoomDescriptor.Settings.bFillTeamsWithBots
		? MSRoomPopulation::MaxCombatants : FMath::Clamp(MSRoomPopulation::LegacyMaxCombatants - EnemyBots, 1, MSRoomPopulation::LegacyMaxCombatants);
	const int32 CurrentHumans = FMath::Clamp(RoomDescriptor.CurrentPlayerCount, 0, MaxHumans);
	if (PlayerCountText) PlayerCountText->SetText(FText::FromString(FString::Printf(
		TEXT("%d/%d"), CurrentHumans, MaxHumans)));
	if (BotCountText)
	{
		BotCountText->SetAutoWrapText(RoomDescriptor.Settings.bFillTeamsWithBots);
		BotCountText->SetText(RoomDescriptor.Settings.bFillTeamsWithBots
			? FText::FromString(TEXT("25対25\nBOT自動補充"))
			: FText::FromString(FString::Printf(TEXT("%d"), EnemyBots)));
	}
	if (StateText) StateText->SetText(MSRoomListEntry::StateLabel(RoomDescriptor.State));
	const FString HostName = RoomDescriptor.HostDisplayName.IsEmpty()
		? TEXT("ホスト未確認") : RoomDescriptor.HostDisplayName;
	if (HostText) HostText->SetText(FText::FromString(HostName));
	SelectButton->SetToolTipText(FText::Format(
		FText::FromString(RoomDescriptor.Settings.bFillTeamsWithBots
			? TEXT("{0} / {1} / {2} / 25対25（人 {3}/{4}） / BOT自動補充")
			: TEXT("{0} / {1} / {2} / 人 {3}/{4} / 敵Bot {5}")),
		FText::FromString(RoomDescriptor.Settings.RoomName), MapText ? MapText->GetText() : FText::GetEmpty(),
		FText::FromString(HostName),
		FText::AsNumber(CurrentHumans), FText::AsNumber(MaxHumans), FText::AsNumber(EnemyBots)));
}

void UMSRoomListEntryWidget::HandleClicked()
{
	OnRoomSelected.Broadcast(RoomDescriptor);
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSRoomListEntryWidget::InitializeForAutomationTests()
{
	if (!WidgetTree->RootWidget) NativeOnInitialized();
}
#endif
