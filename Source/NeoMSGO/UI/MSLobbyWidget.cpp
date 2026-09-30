// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSLobbyWidget.h"
#include "UI/MSFactionPresentation.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Gameplay/MSPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSMapDefinitionRow.h"
#include "Data/MSMobileSuitData.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "UI/MSAvatarLibrary.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSCommunityWidget.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSFullscreenMapWidget.h"
#include "UI/MSLoginWidget.h"
#include "UI/MSResultWidget.h"
#include "UI/MSRoomCreateWidget.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSShopWidget.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSPixelUI.h"
#include "UI/MSPixelPainter.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUILockSubsystem.h"

namespace MSLobby
{
	constexpr int32 MapsPerPage = 4;
	const FLinearColor BackgroundColor(0.015f, 0.028f, 0.05f, 1.0f);
	const FLinearColor CardColor(0.045f, 0.075f, 0.11f, 1.0f);
	const FLinearColor PanelColor(0.018f, 0.05f, 0.085f, 0.94f);
	constexpr float BottomStripHeight = 150.0f;
	const FLinearColor SelectedColor(0.08f, 0.32f, 0.55f, 1.0f);
	const FLinearColor AccentColor(0.25f, 0.75f, 1.0f, 1.0f);
	const TCHAR* Emblems[] = { TEXT("△ デルタ"), TEXT("◎ リング"), TEXT("＋ クロス"),
		TEXT("◇ ダイヤ"), TEXT("ϟ ボルト"), TEXT("＝ ツインバー") };

	const TCHAR* GetEmblemLabel(const int32 Index)
	{
		static const TCHAR* Labels[] = { TEXT("DELTA"), TEXT("RING"), TEXT("CROSS"),
			TEXT("DIAMOND"), TEXT("BOLT"), TEXT("TWIN-BAR") };
		return Labels[FMath::Clamp(Index, 0, 5)];
	}

	bool IsOnlineMode(const UMSGameInstance* Instance)
	{
		const UMSOnlineAccountSubsystem* Online = Instance
			? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		return Online && Online->IsOnlineMode();
	}

	/** Online UI uses the public identity name; the backend username stays private. */
	FString ResolvePilotDisplayName(const UMSGameInstance* Instance)
	{
		if (!Instance) return FString();
		if (const UMSOnlineAccountSubsystem* Online = Instance->GetSubsystem<UMSOnlineAccountSubsystem>();
			Online && Online->IsOnlineMode())
		{
			if (!Online->GetIdentity().DisplayName.IsEmpty()) return Online->GetIdentity().DisplayName;
			if (!Instance->DisplayName.IsEmpty()) return Instance->DisplayName;
			return FString();
		}
		return !Instance->LoggedInUsername.IsEmpty() ? Instance->LoggedInUsername : Instance->DisplayName;
	}
}

const TArray<FMSBattleRuleRow>& UMSLobbyWidget::GetBattleRules()
{
	return FMSBattleRuleRow::GetBuiltInRules();
}

bool UMSLobbyWidget::ApplyBattleRule(const FMSBattleRuleRow& Rule, AMSGameMode* GameMode, AMSGameState* GameState)
{
	if (!GameMode || !GameState) return false;
	GameMode->ApplyBattleRule(Rule);
	GameState->SetDuration(Rule.DurationSeconds);
	return true;
}

TArray<FString> UMSLobbyWidget::BuildRosterNames(const UMSUnitRegistrySubsystem* Registry)
{
	TArray<FString> Names;
	if (!Registry) return Names;
	Registry->ForEachRegisteredUnit([&Names](AMSUnitBase* Unit)
	{
		if (const UMSMobileSuitData* Data = Unit->GetMobileSuitData(); Data && !Data->UnitName.IsEmpty())
			Names.Add(Data->UnitName.ToString());
		else
			Names.Add(Unit->GetName());
	});
	return Names;
}

TArray<FMSFriendEntry> UMSLobbyWidget::BuildPilotListEntries(const UMSFriendSubsystem* Friends)
{
	TArray<FMSFriendEntry> Entries = Friends ? Friends->GetFriends() : TArray<FMSFriendEntry>();
	Entries.StableSort([](const FMSFriendEntry& A, const FMSFriendEntry& B)
	{
		if (A.bIsSquadMember != B.bIsSquadMember) return A.bIsSquadMember;
		if ((A.State == EMSFriendState::Offline) != (B.State == EMSFriendState::Offline)) return A.State != EMSFriendState::Offline;
		return A.Name < B.Name;
	});
	return Entries;
}

void UMSLobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true);
	BuildReferenceLayout();
	ShowPanel(EMSLobbyPanel::Lobby); SetVisibility(ESlateVisibility::Collapsed); BindBattleEnded();
	BindFriendsChanged();
}

void UMSLobbyWidget::BindFriendsChanged()
{
	UMSFriendSubsystem* Friends = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	if (!Friends || BoundFriendSubsystem.Get() == Friends) return;
	if (UMSFriendSubsystem* Old = BoundFriendSubsystem.Get()) Old->OnFriendsChanged.Remove(FriendsChangedHandle);
	BoundFriendSubsystem = Friends;
	FriendsChangedHandle = Friends->OnFriendsChanged.AddUObject(this, &UMSLobbyWidget::HandleFriendsChangedNative);
}

void UMSLobbyWidget::HandleFriendsChangedNative()
{
	// 相手が小隊招待を承認した際の自動入室を、ロビーの6スロットパネルへ即時反映する(タスク#144)
	if (IsOpen()) RefreshRoster();
}

void UMSLobbyWidget::BuildReferenceLayout()
{
	RuleCards.Reset();
	MapCardButtons.Reset();
	MapCardBorders.Reset();
	// Use one authored 1920x1080 design surface. ScaleToFit keeps every lobby region at
	// the same relative position on the verified 16:9 1920x1080 and 1280x720 viewports.
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	Background = WidgetTree->ConstructWidget<UBorder>();
	// An opaque base and an authored vector bridge keep the battlefield out of this screen.
	MSPixelUI::PlainBorder(Background, FLinearColor::Black);
	Background->SetPadding(FMargin(0.0f));
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Background))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UScaleBox* DesignScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("LobbyDesignScale"));
	DesignScale->SetStretch(EStretch::ScaleToFit);
	DesignScale->SetStretchDirection(EStretchDirection::Both);
	USizeBox* DesignSurface = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyDesignSurface"));
	DesignSurface->SetWidthOverride(1920.0f);
	DesignSurface->SetHeightOverride(1080.0f);
	UCanvasPanel* Screen = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LobbyDesignCanvas"));
	DesignSurface->SetContent(Screen);
	DesignScale->SetContent(DesignSurface);
	UOverlaySlot* ScaleSlot = Root->AddChildToOverlay(DesignScale);
	ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
	ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	auto Place = [Screen](UWidget* Widget, const FVector2D Position, const FVector2D Size)
	{
		UCanvasPanelSlot* Slot = Screen->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(0.0f));
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		Slot->SetAutoSize(false);
		return Slot;
	};
	Place(MSPixelUI::Skin(WidgetTree, EMSVectorSurface::BridgeBackdrop, TEXT("LobbyVectorBridge")),
		FVector2D::ZeroVector, FVector2D(1920, 1080))->SetZOrder(-1);
	// Secondary pages share the same authored surface as the lobby, but their content must
	// remain inside the clear center band.  Keep the page implementation in its existing
	// vertical box and give it a fixed design-space canvas; the outer lobby ScaleBox performs
	// the only viewport scaling (1920x1080 -> 1280x720, etc.).
	auto WrapSecondaryPage = [this, &Place](UVerticalBox* Panel, const FName FrameName)
	{
		constexpr float FrameX = 452.0f;
		constexpr float FrameY = 230.0f;
		constexpr float FrameWidth = 1016.0f;
		constexpr float FrameHeight = 630.0f;
		constexpr float BorderPadding = 2.0f;
		const FMargin ContentInset(44.0f, 30.0f, 44.0f, 26.0f);

		const FString Prefix = FrameName.ToString();
		UScaleBox* PageScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), FrameName);
		PageScale->SetStretch(EStretch::ScaleToFill);
		PageScale->SetStretchDirection(EStretchDirection::Both);
		USizeBox* PageSurface = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sSurface"), *Prefix)));
		PageSurface->SetWidthOverride(FrameWidth);
		PageSurface->SetHeightOverride(FrameHeight);
		UBorder* PageBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("%sBorder"), *Prefix)));
		MSPixelUI::PlainBorder(PageBorder, FLinearColor::Transparent);
		PageBorder->SetPadding(FMargin(BorderPadding));
		PageBorder->SetClipping(EWidgetClipping::ClipToBounds);
		UCanvasPanel* PageCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),
			FName(*FString::Printf(TEXT("%sCanvas"), *Prefix)));
		PageBorder->SetContent(MSPixelUI::Frame(WidgetTree, PageCanvas, EMSVectorSurface::Panel, FMargin(0)));
		PageSurface->SetContent(PageBorder);
		PageScale->SetContent(PageSurface);

		if (UCanvasPanelSlot* FrameSlot = Place(PageScale, FVector2D(FrameX, FrameY),
			FVector2D(FrameWidth, FrameHeight)))
		{
			FrameSlot->SetZOrder(2);
		}
		UCanvasPanelSlot* PanelSlot = PageCanvas->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(0.0f));
		PanelSlot->SetPosition(FVector2D(ContentInset.Left, ContentInset.Top));
		PanelSlot->SetSize(FVector2D(
			FrameWidth - 2.0f * BorderPadding - ContentInset.Left - ContentInset.Right,
			FrameHeight - 2.0f * BorderPadding - ContentInset.Top - ContentInset.Bottom));
		PanelSlot->SetAutoSize(false);
		return PageScale;
	};

	auto MakeFramedPanel = [this](UVerticalBox*& OutContent, const FMargin& PanelPadding = FMargin(10.0f))
	{
		OutContent = WidgetTree->ConstructWidget<UVerticalBox>();
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		MSPixelUI::PlainBorder(Panel, FLinearColor::Transparent);
		Panel->SetPadding(FMargin(0));
		Panel->SetClipping(EWidgetClipping::ClipToBounds);
		Panel->SetContent(MSPixelUI::Frame(WidgetTree, OutContent, EMSVectorSurface::Panel, PanelPadding));
		return Panel;
	};
	auto MakeCard = [this](const FName Name, const TCHAR* Title, const TCHAR* Description, const bool bEnabled)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Button);
		MSUIStyle::ApplyButtonSound(Button);
		Button->SetIsEnabled(bEnabled);
		const bool bMission = Name == TEXT("MissionButton");
		const EMSVectorSurface Surface = bMission ? EMSVectorSurface::MissionCard :
			Name == TEXT("CustomizeButton") ? EMSVectorSurface::CustomizeCard :
			Name == TEXT("TrainingButton") ? EMSVectorSurface::TrainingCard :
			Name == TEXT("GashaconButton") ? EMSVectorSurface::GachaCard : EMSVectorSurface::ShopCard;
		UOverlay* Card = MSPixelUI::Frame(WidgetTree, nullptr, Surface, FMargin(0));
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UOverlaySlot* ContentSlot = Card->AddChildToOverlay(Content);
		ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Bottom);
		ContentSlot->SetPadding(FMargin(bMission ? 25.f : 16.f, 0, 12, bMission ? 14.f : 10.f));
		UTextBlock* TitleText = MSPixelUI::Text(WidgetTree, FText::FromString(Title), bMission ? 40.f : 25.f);
		FSlateFontInfo TitleFont = TitleText->GetFont(); TitleFont.TypefaceFontName = TEXT("Bold"); TitleFont.OutlineSettings.OutlineSize = 1; TitleFont.OutlineSettings.OutlineColor = FLinearColor::White; TitleText->SetFont(TitleFont);
		UScaleBox* TitleFit = WidgetTree->ConstructWidget<UScaleBox>();
		TitleFit->SetStretch(EStretch::ScaleToFit);
		TitleFit->SetStretchDirection(EStretchDirection::DownOnly);
		TitleFit->SetContent(TitleText);
		CastChecked<UScaleBoxSlot>(TitleText->Slot)->SetHorizontalAlignment(HAlign_Left);
		USizeBox* TitleBox = WidgetTree->ConstructWidget<USizeBox>();
		TitleBox->SetHeightOverride(bMission ? 44.f : 29.f); TitleBox->SetContent(TitleFit);
		Content->AddChildToVerticalBox(TitleBox);
		UTextBlock* Caption = MSPixelUI::Text(WidgetTree, FText::FromString(Description), bMission ? 15.f : 12.f,
			MSPixel::Hex(bMission ? 0xffbfd1 : 0xe7e4c0));
		Caption->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Content->AddChildToVerticalBox(Caption)->SetPadding(FMargin(0, bMission ? 3.f : 1.f, 0, 0));
		Button->SetContent(Card);
		MSUIStyle::StretchButtonContent(Button);
		return Button;
	};
	auto MakeVectorButton = [this](const FText& Label, const FName Name, const float TextHeight = 14.f)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Button); MSUIStyle::ApplyButtonSound(Button);
		UTextBlock* Text = MSPixelUI::Text(WidgetTree, Label, TextHeight, MSPixel::Hex(0xdfe8dc));
		Text->SetJustification(ETextJustify::Center);
		Button->SetContent(MSPixelUI::Frame(WidgetTree, Text, EMSVectorSurface::Button, FMargin(9, 3)));
		MSUIStyle::StretchButtonContent(Button);
		return Button;
	};

	// Upper-left breadcrumb and channel, followed by the disabled global navigation rail.
	UHorizontalBox* TopRail = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GlobalNavigationRail"));
	UBorder* Breadcrumb = WidgetTree->ConstructWidget<UBorder>();
	MSPixelUI::PlainBorder(Breadcrumb, FLinearColor::Transparent);
	Breadcrumb->SetPadding(FMargin(0));
	BreadcrumbText = MSPixelUI::Text(WidgetTree, FText::GetEmpty(), 18, MSPixel::Hex(0xf1edca));
	BreadcrumbText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Breadcrumb->SetContent(MSPixelUI::Frame(WidgetTree, BreadcrumbText, EMSVectorSurface::GoldPanel, FMargin(20, 14, 16, 5)));
	if (UCanvasPanelSlot* BreadcrumbSlot = Place(Breadcrumb, FVector2D(400.0f, 30.0f), FVector2D(456.0f, 56.0f)))
		BreadcrumbSlot->SetZOrder(10);
	const TCHAR* GlobalLabels[] = { TEXT("ショップ"), TEXT("チャレンジ"), TEXT("階級"), TEXT("ランキング"),
		TEXT("部隊"), TEXT("コミュニティ"), TEXT("プレゼント"), TEXT("EXIT"), TEXT("設定") };
	const FName GlobalNames[] = { TEXT("GlobalShopButton"), TEXT("GlobalChallengeButton"), TEXT("GlobalRankButton"),
		TEXT("GlobalRankingButton"), TEXT("GlobalSquadButton"), TEXT("GlobalCommunityButton"),
		TEXT("GlobalPresentButton"), TEXT("GlobalExitButton"), TEXT("GlobalSettingsButton") };
	const float GlobalWidths[] = { 58, 58, 58, 58, 58, 64, 58, 58, 62 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GlobalLabels); ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), GlobalNames[Index]);
		MSPixelUI::ClearButtonBrush(Button);
		MSUIStyle::ApplyButtonSound(Button);
		UTextBlock* Label = MSPixelUI::Text(WidgetTree, FText::FromString(GlobalLabels[Index]), 12, MSPixel::Hex(0xc0cfb8));
		Label->SetJustification(ETextJustify::Center);
		UScaleBox* LabelFit = WidgetTree->ConstructWidget<UScaleBox>();
		LabelFit->SetStretch(EStretch::ScaleToFit);
		LabelFit->SetStretchDirection(EStretchDirection::DownOnly);
		LabelFit->SetContent(Label);
		UOverlay* NavContent = WidgetTree->ConstructWidget<UOverlay>();
		UOverlaySlot* IconSlot = NavContent->AddChildToOverlay(MSPixelUI::Skin(WidgetTree,
			EMSVectorSurface::NavigationIcon, NAME_None, Index));
		IconSlot->SetHorizontalAlignment(HAlign_Fill); IconSlot->SetVerticalAlignment(VAlign_Fill);
		IconSlot->SetPadding(FMargin(2, 0, 2, 8));
		UOverlaySlot* LabelSlot = NavContent->AddChildToOverlay(LabelFit);
		LabelSlot->SetHorizontalAlignment(HAlign_Fill); LabelSlot->SetVerticalAlignment(VAlign_Bottom);
		Button->SetContent(NavContent);
		MSUIStyle::StretchButtonContent(Button);
		Button->SetToolTipText(FText::FromString(GlobalLabels[Index]));
		const bool bCommunity = GlobalNames[Index] == TEXT("GlobalCommunityButton");
		const bool bSettings = GlobalNames[Index] == TEXT("GlobalSettingsButton");
		const bool bExit = GlobalNames[Index] == TEXT("GlobalExitButton");
		Button->SetIsEnabled(bCommunity || bSettings || bExit);
		if (bCommunity) Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleCommunityClicked);
		if (bSettings) Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleSettingsClicked);
		if (bExit) { LogoutButton = Button; Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleLogoutClicked); }
		UHorizontalBoxSlot* NavSlot = TopRail->AddChildToHorizontalBox(Button);
		FSlateChildSize NavSize(ESlateSizeRule::Fill); NavSize.Value = GlobalWidths[Index];
		NavSlot->SetSize(NavSize);
		NavSlot->SetPadding(FMargin(2, 0));
	}
	if (UCanvasPanelSlot* TopRailSlot = Place(TopRail, FVector2D(920.0f, 34.0f), FVector2D(570.0f, 52.0f)))
		TopRailSlot->SetZOrder(10);

	LobbyPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LobbyPanel"));
	Place(LobbyPanel, FVector2D::ZeroVector, FVector2D(1920.0f, 1080.0f));

	UHorizontalBox* MainColumns = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* MainColumnsSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyMainColumnsSize"));
	MainColumnsSize->SetWidthOverride(1014.0f);
	MainColumnsSize->SetHeightOverride(457.0f);
	MainColumnsSize->SetContent(MainColumns);
	UVerticalBoxSlot* MainColumnsSlot = LobbyPanel->AddChildToVerticalBox(MainColumnsSize);
	MainColumnsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	MainColumnsSlot->SetHorizontalAlignment(HAlign_Left);
	MainColumnsSlot->SetPadding(FMargin(452.0f, 248.0f, 0.0f, 0.0f));
	UVerticalBox* LeftColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBox* RightColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	USizeBox* LeftColumnSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyLeftColumnSize"));
	LeftColumnSize->SetWidthOverride(423.0f);
	LeftColumnSize->SetHeightOverride(457.0f);
	LeftColumnSize->SetContent(LeftColumn);
	UHorizontalBoxSlot* LeftColumnSlot = MainColumns->AddChildToHorizontalBox(LeftColumnSize);
	LeftColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	LeftColumnSlot->SetHorizontalAlignment(HAlign_Left);
	USizeBox* MainColumnGap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyMainColumnGap"));
	MainColumnGap->SetWidthOverride(12.0f);
	MainColumnGap->SetHeightOverride(1.0f);
	UHorizontalBoxSlot* MainColumnGapSlot = MainColumns->AddChildToHorizontalBox(MainColumnGap);
	MainColumnGapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	MainColumnGapSlot->SetHorizontalAlignment(HAlign_Left);
	USizeBox* RightColumnSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyRightColumnSize"));
	RightColumnSize->SetWidthOverride(579.0f);
	RightColumnSize->SetHeightOverride(457.0f);
	RightColumnSize->SetContent(RightColumn);
	UHorizontalBoxSlot* RightColumnSlot = MainColumns->AddChildToHorizontalBox(RightColumnSize);
	RightColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	RightColumnSlot->SetHorizontalAlignment(HAlign_Left);

	UButton* MissionButton = MakeCard(TEXT("MissionButton"), TEXT("ミッション"), TEXT("ルールを選んでバトル開始！"), true);
	MissionButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMissionClicked);
	USizeBox* MissionSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyMissionSize"));
	MissionSize->SetWidthOverride(423.0f);
	MissionSize->SetHeightOverride(228.0f);
	MissionSize->SetContent(MissionButton);
	LeftColumn->AddChildToVerticalBox(MissionSize)->SetPadding(FMargin(0, 0, 0, 16));

	auto AddMediumRow = [this, LeftColumn](UButton* First, UButton* Second, const float Height, const float BottomGap)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("LobbyMediumRow%d"), LeftColumn->GetChildrenCount())));
		RowSize->SetWidthOverride(423.0f);
		RowSize->SetHeightOverride(Height);
		RowSize->SetContent(Row);
		UVerticalBoxSlot* RowSlot = LeftColumn->AddChildToVerticalBox(RowSize);
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		RowSlot->SetPadding(FMargin(0, 0, 0, BottomGap));
		USizeBox* FirstSize = WidgetTree->ConstructWidget<USizeBox>(); FirstSize->SetWidthOverride(216); FirstSize->SetContent(First);
		USizeBox* SecondSize = WidgetTree->ConstructWidget<USizeBox>(); SecondSize->SetWidthOverride(207); SecondSize->SetContent(Second);
		Row->AddChildToHorizontalBox(FirstSize);
		Row->AddChildToHorizontalBox(SecondSize);
	};
	UButton* CustomizeButton = MakeCard(TEXT("CustomizeButton"), TEXT("カスタマイズ"), TEXT("機体開発・機体強化など"), true);
	CustomizeButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleCustomizeClicked);
	AddMediumRow(
		CustomizeButton,
		MakeCard(TEXT("TrainingButton"), TEXT("大規模戦演習"), TEXT("演習で動きを確認"), false), 99.0f, 17.0f);
	UButton* ShopButton = MakeCard(TEXT("ShopButton"), TEXT("ショップ"), TEXT("購入・売却など"), true);
	ShopButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleShopClicked);
	AddMediumRow(
		MakeCard(TEXT("GashaconButton"), TEXT("ガシャコン"), TEXT("機体やアイテムをゲット！"), false),
		ShopButton, 97.0f, 0.0f);

	// Right column: the pass is an explicit unavailable area, followed by the
	// live-map strip and the notice/party area from the reference frame.
	UOverlay* PassFrame = MSPixelUI::Frame(WidgetTree, nullptr, EMSVectorSurface::PassPanel, FMargin(0));
	UTextBlock* PassTitle = MSPixelUI::Text(WidgetTree, FText::FromString(TEXT("パイロットパス")), 16, MSPixel::Hex(0xfff9da));
	UOverlaySlot* PassTitleSlot = PassFrame->AddChildToOverlay(PassTitle);
	PassTitleSlot->SetPadding(FMargin(18, 3, 0, 0)); PassTitleSlot->SetVerticalAlignment(VAlign_Top);
	UTextBlock* PassState = MSPixelUI::Text(WidgetTree, FText::FromString(TEXT("パス情報\n未対応")), 17, MSPixel::Hex(0xb0c5b9));
	UOverlaySlot* PassStateSlot = PassFrame->AddChildToOverlay(PassState);
	PassStateSlot->SetPadding(FMargin(157, 48, 158, 0)); PassStateSlot->SetVerticalAlignment(VAlign_Top);
	USizeBox* PassSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyPassSize"));
	PassSize->SetWidthOverride(579.0f); PassSize->SetHeightOverride(139.0f); PassSize->SetContent(PassFrame);
	RightColumn->AddChildToVerticalBox(PassSize)->SetPadding(FMargin(0, 12, 0, 0));

	UVerticalBox* MapContent = nullptr;
	UBorder* MapFrame = MakeFramedPanel(MapContent, FMargin(0));
	USizeBox* MapSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyMapSize"));
	MapSize->SetWidthOverride(579.0f); MapSize->SetHeightOverride(85.0f); MapSize->SetContent(MapFrame);
	RightColumn->AddChildToVerticalBox(MapSize)->SetPadding(FMargin(0, 0, 0, 9));
	UHorizontalBox* MapHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* MapHeaderSize = WidgetTree->ConstructWidget<USizeBox>();
	UBorder* MapHeaderPlate = WidgetTree->ConstructWidget<UBorder>();
	MSPixelUI::PlainBorder(MapHeaderPlate, MSPixel::Hex(0x315bb1));
	MapHeaderPlate->SetPadding(FMargin(7, 2)); MapHeaderPlate->SetContent(MapHeader);
	MapHeaderSize->SetHeightOverride(27.0f); MapHeaderSize->SetContent(MapHeaderPlate);
	MapContent->AddChildToVerticalBox(MapHeaderSize);
	MapHeader->AddChildToHorizontalBox(MSPixelUI::Text(WidgetTree, FText::FromString(
		MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance()))
			? TEXT("対戦ルーム") : TEXT("配信中のマップ")),
		13, MSPixel::Hex(0xdce6d8)))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PreviousMapPageButton = MakeVectorButton(FText::FromString(TEXT("◀")), TEXT("PreviousMapPageButton"), 11);
	PreviousMapPageButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandlePreviousMapPageClicked);
	MapHeader->AddChildToHorizontalBox(PreviousMapPageButton)->SetPadding(FMargin(4, 0));
	MapPageLabel = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Micro,
		false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("MapPageLabel"));
	MapPageLabel->SetJustification(ETextJustify::Center);
	MapHeader->AddChildToHorizontalBox(MapPageLabel)->SetPadding(FMargin(7, 2));
	NextMapPageButton = MakeVectorButton(FText::FromString(TEXT("▶")), TEXT("NextMapPageButton"), 11);
	NextMapPageButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleNextMapPageClicked);
	MapHeader->AddChildToHorizontalBox(NextMapPageButton)->SetPadding(FMargin(4, 0));
	UHorizontalBox* Maps = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MapCards"));
	USizeBox* MapCardsSize = WidgetTree->ConstructWidget<USizeBox>();
	MapCardsSize->SetHeightOverride(58.0f); MapCardsSize->SetContent(Maps);
	MapContent->AddChildToVerticalBox(MapCardsSize)->SetPadding(FMargin(28, 0, 34, 0));
	const TArray<FMSMapDefinitionRow>& MapDefinitions = FMSMapDefinitionRow::GetVisibleMapDefinitions();
	const bool bRoomBasedMaps = MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance()));
	if (bRoomBasedMaps)
	{
		PreviousMapPageButton->SetVisibility(ESlateVisibility::Collapsed);
		NextMapPageButton->SetVisibility(ESlateVisibility::Collapsed);
		MapPageLabel->SetVisibility(ESlateVisibility::Collapsed);
		UTextBlock* RoomHint = MSPixelUI::Text(WidgetTree, FText::FromString(
			TEXT("ミッションからルームを作成・検索できます。\nマップと敵Botはルーム作成時に設定します。")), 14, MSPixel::Hex(0xdce6d8));
		RoomHint->SetAutoWrapText(true);
		MapCardsSize->SetContent(RoomHint);
	}
	for (int32 Index = 0; !bRoomBasedMaps && Index < MapDefinitions.Num(); ++Index)
	{
		const FMSMapDefinitionRow& Definition = MapDefinitions[Index];
		UButton* MapButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("MapCard%d"), Index)));
		MSPixelUI::ClearButtonBrush(MapButton);
		MSUIStyle::ApplyButtonSound(MapButton);
		MapButton->SetIsEnabled(Definition.bPlayable);
		UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>();
		UOverlay* Thumbnail = MSPixelUI::Frame(WidgetTree, nullptr, EMSVectorSurface::MapThumbnail, FMargin(0), Index);
		UTextBlock* Terrain = MSPixelUI::Text(WidgetTree,
			FText::FromString(Definition.bPlayable ? (Definition.bSpace ? TEXT("宇宙") : TEXT("地上")) :
				(Definition.bSpace ? TEXT("宇宙 / 配信準備中") : TEXT("地上 / 配信準備中"))), 11);
		UOverlaySlot* TerrainSlot = Thumbnail->AddChildToOverlay(Terrain);
		TerrainSlot->SetHorizontalAlignment(HAlign_Right); TerrainSlot->SetVerticalAlignment(VAlign_Bottom);
		TerrainSlot->SetPadding(FMargin(2));
		USizeBox* ThumbnailSize = WidgetTree->ConstructWidget<USizeBox>();
		ThumbnailSize->SetHeightOverride(40.0f); ThumbnailSize->SetContent(Thumbnail);
		CardContent->AddChildToVerticalBox(ThumbnailSize);
		UTextBlock* MapLabel = MSPixelUI::Text(WidgetTree, Definition.DisplayName, 12, MSPixel::Hex(0xe7e9e6));
		MapLabel->SetJustification(ETextJustify::Center);
		UBorder* MapLabelPlate = WidgetTree->ConstructWidget<UBorder>();
		MSPixelUI::PlainBorder(MapLabelPlate, MSPixel::Hex(0x405153)); MapLabelPlate->SetPadding(FMargin(2, 0));
		MapLabelPlate->SetContent(MapLabel); CardContent->AddChildToVerticalBox(MapLabelPlate);
		MapLabel->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		if (MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance())) && Definition.bPlayable)
		{
			MapButton->SetToolTipText(FText::Format(FText::FromString(TEXT("{0}  ルーム作成で選択")),
				Definition.DisplayName));
		}
		else
		{
			MapButton->SetToolTipText(Definition.bPlayable
				? Definition.DisplayName
				: FText::Format(FText::FromString(TEXT("{0}  配信準備中")), Definition.DisplayName));
		}
		UBorder* CardFrame = WidgetTree->ConstructWidget<UBorder>();
		MSPixelUI::PlainBorder(CardFrame, MSPixel::Hex(0x687778));
		CardFrame->SetPadding(FMargin(1)); CardFrame->SetContent(CardContent);
		MapButton->SetContent(CardFrame);
		MSUIStyle::StretchButtonContent(MapButton);
		UHorizontalBoxSlot* MapSlot = Maps->AddChildToHorizontalBox(MapButton);
		MapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MapSlot->SetPadding(FMargin(2, 0));
		MapButton->SetRenderScale(FVector2D(1.0f, 1.0f));
		MapCardButtons.Add(MapButton);
		MapCardBorders.Add(CardFrame);
		switch (Index)
		{
		case 0: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap0Clicked); break;
		case 1: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap1Clicked); break;
		case 2: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap2Clicked); break;
		case 3: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap3Clicked); break;
		case 4: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap4Clicked); break;
		case 5: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap5Clicked); break;
		case 6: MapButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleMap6Clicked); break;
		default: break;
		}
	}
	RefreshMapSelection();
	UOverlay* NoticePartyStack = WidgetTree->ConstructWidget<UOverlay>();
	UVerticalBox* NoticeContent = nullptr;
	LobbyNoticeFrame = MakeFramedPanel(NoticeContent, FMargin(18, 14));
	LobbyNoticeFrame->SetContent(MSPixelUI::Frame(WidgetTree, NoticeContent, EMSVectorSurface::NoticePanel, FMargin(18, 14)));
	if (UOverlaySlot* NoticeSlot = NoticePartyStack->AddChildToOverlay(LobbyNoticeFrame))
	{
		NoticeSlot->SetHorizontalAlignment(HAlign_Fill); NoticeSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UTextBlock* NoticeTitle = LobbyNoticeTitle = MSPixelUI::Text(WidgetTree, FText::FromString(TEXT("出撃準備")), 40, MSPixel::Hex(0xffedb0));
	NoticeTitle->SetJustification(ETextJustify::Center);
	NoticeContent->AddChildToVerticalBox(NoticeTitle)->SetPadding(FMargin(0, 17, 0, 8));
	UTextBlock* NoticeBody = LobbyNoticeBody = MSPixelUI::Text(WidgetTree,
		FText::FromString(TEXT("機体をカスタマイズして、戦場へ。")), 21, MSPixel::Hex(0xe9efdc));
	NoticeBody->SetJustification(ETextJustify::Center);
	NoticeBody->SetAutoWrapText(true);
	NoticeBody->SetWrapTextAt(500.0f);
	NoticeContent->AddChildToVerticalBox(NoticeBody);
	ResumeSavedPartyButton = MakeVectorButton(FText::FromString(TEXT("保存済み小隊を再開（ローカル）")), TEXT("ResumeSavedPartyButton"));
	ResumeSavedPartyButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleResumeSavedPartyClicked);
	NoticeContent->AddChildToVerticalBox(ResumeSavedPartyButton)->SetPadding(FMargin(0, 8, 0, 0));

	// Current-party panel: 2x3 slot grid (self + up to 5 squad members), frames3-0040 layout.
	UVerticalBox* PartyContent = nullptr;
	LobbyPartyFrame = MakeFramedPanel(PartyContent, FMargin(8, 5));
	if (UOverlaySlot* PartySlot = NoticePartyStack->AddChildToOverlay(LobbyPartyFrame))
	{
		PartySlot->SetHorizontalAlignment(HAlign_Fill); PartySlot->SetVerticalAlignment(VAlign_Fill);
	}
	LobbyPartyFrame->SetVisibility(ESlateVisibility::Collapsed);
	USizeBox* NoticePartySize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyNoticePartySize"));
	NoticePartySize->SetWidthOverride(579.0f); NoticePartySize->SetHeightOverride(212.0f); NoticePartySize->SetContent(NoticePartyStack);
	RightColumn->AddChildToVerticalBox(NoticePartySize);
	PartyContent->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ローカル小隊（通信未接続）")), MSUIStyle::ETextRole::Micro));
	PartyCommentInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PartyCommentInput"));
	PartyCommentInput->SetHintText(FText::FromString(TEXT("小隊コメント")));
	PartyCommentInput->OnTextChanged.AddDynamic(this, &UMSLobbyWidget::HandlePartyCommentChanged);
	USizeBox* PartyCommentSize = WidgetTree->ConstructWidget<USizeBox>();
	PartyCommentSize->SetHeightOverride(22.0f); PartyCommentSize->SetContent(PartyCommentInput);
	PartyContent->AddChildToVerticalBox(PartyCommentSize)->SetPadding(FMargin(0, 0, 0, 3));
	PartyRowTop = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PartyRowTop"));
	USizeBox* PartyRowTopSize = WidgetTree->ConstructWidget<USizeBox>();
	PartyRowTopSize->SetHeightOverride(54.0f); PartyRowTopSize->SetContent(PartyRowTop);
	PartyContent->AddChildToVerticalBox(PartyRowTopSize)->SetPadding(FMargin(0, 0, 0, 2));
	PartyRowBottom = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PartyRowBottom"));
	USizeBox* PartyRowBottomSize = WidgetTree->ConstructWidget<USizeBox>();
	PartyRowBottomSize->SetHeightOverride(54.0f); PartyRowBottomSize->SetContent(PartyRowBottom);
	PartyContent->AddChildToVerticalBox(PartyRowBottomSize)->SetPadding(FMargin(0, 0, 0, 3));
	UHorizontalBox* PartyButtons = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* PartyButtonsSize = WidgetTree->ConstructWidget<USizeBox>();
	PartyButtonsSize->SetHeightOverride(28.0f); PartyButtonsSize->SetContent(PartyButtons);
	PartyContent->AddChildToVerticalBox(PartyButtonsSize);
	auto MakePartyButton = [this, PartyButtons](const TCHAR* Label, const FName Name)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label), MSUIStyle::ETextRole::Caption,
			false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Text, MSLobby::CardColor, Name);
		PartyButtons->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return Button;
	};
	MakePartyButton(TEXT("パイロットを招待"), TEXT("PartyInviteButton"))->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandlePartyInviteClicked);
	MakePartyButton(TEXT("小隊情報参照"), TEXT("PartyInfoButton"))->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandlePartyInfoClicked);
	MakePartyButton(TEXT("小隊から脱退"), TEXT("PartyLeaveButton"))->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandlePartyLeaveClicked);

	// Keep a narrow notification line between the main area and the three permanent
	// information panels. The panels use the reference widths instead of equal fills.
	UTextBlock* LobbyNotification = MSPixelUI::Text(WidgetTree,
		FText::FromString(TEXT("[SYSTEM] 作戦準備を確認してください。")), 13, MSPixel::Hex(0xd0d888));
	USizeBox* LobbyNotificationSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LobbyNotificationSize"));
	LobbyNotificationSize->SetWidthOverride(1016.0f); LobbyNotificationSize->SetHeightOverride(25.0f);
	LobbyNotificationSize->SetContent(LobbyNotification);
	UVerticalBoxSlot* LobbyNotificationSlot = LobbyPanel->AddChildToVerticalBox(LobbyNotificationSize);
	LobbyNotificationSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	LobbyNotificationSlot->SetHorizontalAlignment(HAlign_Left);
	LobbyNotificationSlot->SetPadding(FMargin(452.0f, 158.0f, 0.0f, 0.0f));

	// Bottom strip: static chat, pilot data, and the actual registered-unit roster.
	UHorizontalBox* BottomStrip = WidgetTree->ConstructWidget<UHorizontalBox>();
	BottomStripSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BottomStripSizeBox"));
	BottomStripSizeBox->SetWidthOverride(1016.0f);
	BottomStripSizeBox->SetContent(BottomStrip);
	LobbyPanel->AddChildToVerticalBox(BottomStripSizeBox);
	RestoreBottomStripLayout();
	auto AddInfoPanel = [this, BottomStrip, &MakeFramedPanel](const float Width, const float RightGap)
	{
		UVerticalBox* Content = nullptr;
		UBorder* Frame = MakeFramedPanel(Content, FMargin(10, 6));
		USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>();
		PanelSize->SetWidthOverride(Width); PanelSize->SetHeightOverride(MSLobby::BottomStripHeight);
		PanelSize->SetContent(Frame);
		UHorizontalBoxSlot* PanelSlot = BottomStrip->AddChildToHorizontalBox(PanelSize);
		PanelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		PanelSlot->SetPadding(FMargin(0, 0, RightGap, 0));
		return Content;
	};
	UVerticalBox* Chat = AddInfoPanel(438.0f, 1.0f);
	Chat->AddChildToVerticalBox(MSPixelUI::Text(WidgetTree, FText::FromString(TEXT("チャットログ")),
		13, MSPixel::Hex(0x82bfae)));
	Chat->AddChildToVerticalBox(MSPixelUI::Text(WidgetTree,
		FText::FromString(TEXT("チャット未接続")), 13, MSPixel::Hex(0xd3dfd5)));
	UVerticalBox* Pilot = AddInfoPanel(368.0f, 1.0f);
	Pilot->AddChildToVerticalBox(MSPixelUI::Text(WidgetTree, FText::FromString(TEXT("パイロット情報")),
		13, MSPixel::Hex(0x82bfae)));
	// 立ち絵アバター + ステータス(アバターはMSAvatarLibraryで解決できた場合のみRefreshPersistentDataで表示)
	UHorizontalBox* PilotRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PilotAvatarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PilotAvatarBox"));
	PilotAvatarBox->SetWidthOverride(112); PilotAvatarBox->SetHeightOverride(116);
	PilotAvatarImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PilotAvatarImage"));
	PilotAvatarBox->SetContent(PilotAvatarImage);
	PilotAvatarBox->SetVisibility(ESlateVisibility::Collapsed);
	PilotRow->AddChildToHorizontalBox(PilotAvatarBox)->SetPadding(FMargin(0, 2, 8, 0));
	PilotStatsText = MSPixelUI::Text(WidgetTree, FText::GetEmpty(), 14, MSPixel::Hex(0xdde7d5), TEXT("PilotStatsText"));
	PilotStatsText->SetAutoWrapText(true); PilotStatsText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	PilotRow->AddChildToHorizontalBox(PilotStatsText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Pilot->AddChildToVerticalBox(PilotRow);
	UVerticalBox* Roster = AddInfoPanel(208.0f, 0.0f);
	UTextBlock* RosterTitle = MSPixelUI::Text(WidgetTree,
		FText::FromString(TEXT("PILOT LIST / 接続未確認")), 12, MSPixel::Hex(0x82bfae));
	RosterTitle->SetJustification(ETextJustify::Center);
	Roster->AddChildToVerticalBox(RosterTitle);
	SquadSummaryText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption,
		false, MSLobby::AccentColor);
	Roster->AddChildToVerticalBox(SquadSummaryText);
	SquadSummaryText->SetVisibility(ESlateVisibility::Collapsed);
	LobbyRosterBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LobbyRosterBox"));
	Roster->AddChildToVerticalBox(LobbyRosterBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* MoreButton = MakeVectorButton(FText::FromString(TEXT("MORE")), TEXT("PilotListMoreButton"), 11);
	MoreButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleCommunityClicked);
	Roster->AddChildToVerticalBox(MoreButton)->SetPadding(FMargin(0, 3, 0, 0));

	// Existing squad panel remains reachable from the pilot-list panel.
	SquadPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SquadPanel"));
	SquadPageScale = WrapSecondaryPage(SquadPanel, TEXT("SquadPageScale"));
	UTextBlock* SquadBackLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ブリッジへ戻る")),
		MSUIStyle::ETextRole::H2, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	SquadBackLabel->SetJustification(ETextJustify::Center);
	UButton* SquadBack = MSUIStyle::MakeCapsuleButton(WidgetTree, SquadBackLabel, MSLobby::CardColor,
		NAME_None, MSUIStyle::EButtonSound::Back);
	SquadBack->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleLobbyTabClicked);
	SquadPanel->AddChildToVerticalBox(SquadBack)->SetPadding(FMargin(0, 0, 0, 12));
	SquadPanel->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("部隊名（16文字まで）")),
		MSUIStyle::ETextRole::H2, false, MSLobby::AccentColor));
	SquadNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	SquadNameInput->OnTextChanged.AddDynamic(this, &UMSLobbyWidget::HandleSquadNameChanged);
	SquadPanel->AddChildToVerticalBox(SquadNameInput)->SetPadding(FMargin(0, 8, 0, 14));
	UHorizontalBox* EmblemRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	SquadPanel->AddChildToVerticalBox(EmblemRow);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		UTextBlock* Label = MSUIStyle::MakeText(WidgetTree, FText::FromString(MSLobby::GetEmblemLabel(Index)),
			MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		Label->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Label, MSLobby::CardColor);
		EmblemRow->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		switch (Index)
		{
		case 0: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem0Clicked); break;
		case 1: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem1Clicked); break;
		case 2: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem2Clicked); break;
		case 3: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem3Clicked); break;
		case 4: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem4Clicked); break;
		default: Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleEmblem5Clicked); break;
		}
	}
	EmblemSelectionText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::H2,
		false, MSLobby::AccentColor);
	SquadPanel->AddChildToVerticalBox(EmblemSelectionText)->SetPadding(FMargin(0, 10));
	RosterBox = WidgetTree->ConstructWidget<UVerticalBox>();
	SquadPanel->AddChildToVerticalBox(RosterBox);

	// Mission branches into the legacy offline flow or the online public room browser.
	MissionChoicePanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MissionChoicePanel"));
	MissionChoicePageScale = WrapSecondaryPage(MissionChoicePanel, TEXT("MissionChoicePageScale"));
	MissionChoicePanel->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("ミッション")), MSUIStyle::ETextRole::H1, false, MSLobby::AccentColor));
	const bool bOnlineMode = MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance()));
	auto MakeMissionChoice = [this](const TCHAR* Label, const FName Name)
	{
		// Mission choices are placed in a wide, padded vertical-box slot.  The old direct
		// TextBlock content kept its default wrapping and was measured at the button's
		// desired size, so Japanese labels wrapped one glyph per line at 1280x720.
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label),
			MSUIStyle::ETextRole::H2, false, MSUIStyleColor::TextPrimary,
			MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		Text->SetJustification(ETextJustify::Center);
		UScaleBox* TextFit = WidgetTree->ConstructWidget<UScaleBox>();
		TextFit->SetStretch(EStretch::ScaleToFit);
		TextFit->SetStretchDirection(EStretchDirection::DownOnly);
		TextFit->SetContent(Text);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, TextFit, MSLobby::CardColor, Name);
		// Fill the button's allotted width so TextFit receives the real content area.
		MSUIStyle::StretchButtonContent(Button);
		return Button;
	};
	QuickSortieButton = MakeMissionChoice(bOnlineMode ? TEXT("ルームを検索") : TEXT("クイック出撃"), TEXT("MissionQuickSortieButton"));
	CreateRoomButton = MakeMissionChoice(bOnlineMode ? TEXT("ルームを作成") : TEXT("ルーム作成"), TEXT("MissionCreateRoomButton"));
	JoinRoomButton = MakeMissionChoice(TEXT("ルーム参加"), TEXT("MissionJoinRoomButton"));
	QuickSortieButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleQuickSortieClicked);
	CreateRoomButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleCreateRoomClicked);
	JoinRoomButton->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleJoinRoomClicked);
	MissionChoicePanel->AddChildToVerticalBox(QuickSortieButton)->SetPadding(FMargin(140, 16));
	MissionChoicePanel->AddChildToVerticalBox(CreateRoomButton)->SetPadding(FMargin(140, 16));
	MissionChoicePanel->AddChildToVerticalBox(JoinRoomButton)->SetPadding(FMargin(140, 16));
	if (bOnlineMode)
	{
		// The public browser is the online entry point. Keep the legacy direct-IP
		// row hidden to avoid presenting it as a second online protocol.
		JoinRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	UButton* MissionBack = MakeMissionChoice(TEXT("ブリッジへ戻る"), TEXT("MissionChoiceBackButton"));
	MissionBack->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleLobbyTabClicked);
	MissionChoicePanel->AddChildToVerticalBox(MissionBack)->SetPadding(FMargin(140, 16));

	// Existing operation decision flow is retained behind quick sortie.
	OperationPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OperationPanel"));
	OperationPageScale = WrapSecondaryPage(OperationPanel, TEXT("OperationPageScale"));
	UTextBlock* OperationBackLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ブリッジへ戻る")),
		MSUIStyle::ETextRole::H2, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	OperationBackLabel->SetJustification(ETextJustify::Center);
	UButton* OperationBack = MSUIStyle::MakeCapsuleButton(WidgetTree, OperationBackLabel, MSLobby::CardColor,
		NAME_None, MSUIStyle::EButtonSound::Back);
	OperationBack->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleLobbyTabClicked);
	OperationPanel->AddChildToVerticalBox(OperationBack)->SetPadding(FMargin(0, 0, 0, 10));
	OperationPanel->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("作戦ルール選択")),
		MSUIStyle::ETextRole::H1, false, MSLobby::AccentColor))->SetPadding(FMargin(0, 0, 0, 12));
	UHorizontalBox* Cards = WidgetTree->ConstructWidget<UHorizontalBox>();
	// Keep the rule cards readable inside the secondary frame instead of letting a Fill slot
	// stretch each gray card across the whole remaining viewport height.
	USizeBox* RuleCardsSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OperationRuleCardsSize"));
	RuleCardsSize->SetHeightOverride(248.0f);
	RuleCardsSize->SetContent(Cards);
	OperationPanel->AddChildToVerticalBox(RuleCardsSize)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	const TArray<FMSBattleRuleRow>& Rules = GetBattleRules();
	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		MSUIStyle::ApplyButtonSound(Button);
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		MSUIStyle::ApplyBrush(Card, MSUIStyle::EBrush::ListRow, MSLobby::CardColor);
		Card->SetPadding(FMargin(18));
		const FMSBattleRuleRow& Rule = Rules[Index];
		UTextBlock* RuleText = MSUIStyle::MakeText(WidgetTree,
			FText::Format(FText::FromString(TEXT("{0}\n撃破 {1} / 本拠点 {2}\nゲージ {3} / {4}秒")),
				Rule.Name, FText::AsNumber(Rule.KillDamage), FText::AsNumber(Rule.HomeBaseDamage),
				FText::AsNumber(Rule.MaxGauge), FText::AsNumber(Rule.DurationSeconds)), MSUIStyle::ETextRole::H2,
			false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		// Keep each stat row intact.  The old wrapping TextBlock split the final
		// number/unit pair (for example "1,200" and "秒") inside a card.
		UScaleBox* RuleTextFit = WidgetTree->ConstructWidget<UScaleBox>();
		RuleTextFit->SetStretch(EStretch::ScaleToFit);
		RuleTextFit->SetStretchDirection(EStretchDirection::DownOnly);
		RuleTextFit->SetContent(RuleText);
		Card->SetContent(RuleTextFit);
		Button->SetContent(Card);
		// The card owns the authored list-row frame; remove the UButton's
		// platform gray brush so it cannot mask the state-specific frame.
		MSPixelUI::ClearButtonBrush(Button);
		Cards->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RuleCards.Add(Card);
		if (Index == 0) Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleRule0Clicked);
		else if (Index == 1) Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleRule1Clicked);
		else Button->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleRule2Clicked);
	}
	// SelectedRuleIndex starts at zero; apply its selected frame on first
	// display as well as after subsequent card clicks.
	RefreshRuleSelection();
	UTextBlock* ConfirmLabel = MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("作戦決定 / 出撃デッキ編成へ")), MSUIStyle::ETextRole::H2);
	ConfirmLabel->SetJustification(ETextJustify::Center);
	UButton* Confirm = MSUIStyle::MakeCapsuleButton(WidgetTree, ConfirmLabel, MSLobby::SelectedColor);
	Confirm->OnClicked.AddDynamic(this, &UMSLobbyWidget::HandleConfirmClicked);
	OperationPanel->AddChildToVerticalBox(Confirm)->SetPadding(FMargin(160, 14));
}


#if WITH_DEV_AUTOMATION_TESTS
void UMSLobbyWidget::InitializeForAutomationTests() { if (!Background) NativeOnInitialized(); }
void UMSLobbyWidget::SetShopWidgetForTesting(UMSShopWidget* InWidget) { ShopWidget = InWidget; if (ShopWidget) ShopWidget->SetLobbyWidget(this); }
void UMSLobbyWidget::SetCustomizeWidgetForTesting(UMSCustomizeWidget* InWidget) { CustomizeWidget = InWidget; if (CustomizeWidget) CustomizeWidget->SetLobbyWidget(this); }
void UMSLobbyWidget::SetBriefingWidgetForTesting(UMSBriefingWidget* InWidget) { BriefingWidget = InWidget; if (BriefingWidget) BriefingWidget->SetLobbyWidget(this); }
void UMSLobbyWidget::SetResultWidgetForTesting(UMSResultWidget* InWidget) { ResultWidget = InWidget; if (ResultWidget) ResultWidget->SetLobbyWidget(this); }
void UMSLobbyWidget::SetCommunityWidgetForTesting(UMSCommunityWidget* InWidget) { CommunityWidget = InWidget; if (CommunityWidget) CommunityWidget->SetLobbyWidget(this); }
#endif

void UMSLobbyWidget::NativeDestruct()
{
	if (auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr)
		Rooms->OnRoomStatusChanged.RemoveDynamic(this, &UMSLobbyWidget::RefreshRoomStatus);
	if (AMSGameState* State = BoundGameState.Get()) State->OnBattleEnded.RemoveDynamic(this, &UMSLobbyWidget::HandleBattleEnded);
	if (UMSFriendSubsystem* Friends = BoundFriendSubsystem.Get()) Friends->OnFriendsChanged.Remove(FriendsChangedHandle);
	BoundFriendSubsystem.Reset();
	Super::NativeDestruct();
}

void UMSLobbyWidget::BindBattleEnded()
{
	AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
	if (!State || State == BoundGameState.Get()) return;
	if (AMSGameState* Old = BoundGameState.Get()) Old->OnBattleEnded.RemoveDynamic(this, &UMSLobbyWidget::HandleBattleEnded);
	BoundGameState = State; State->OnBattleEnded.AddUniqueDynamic(this, &UMSLobbyWidget::HandleBattleEnded);
}

void UMSLobbyWidget::Open()
{
	if (UGameInstance* GI = GetGameInstance())
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			Bgm->PlayScene(EMSBgmScene::Lobby);
	if (GetWorld() && !IsInViewport()) AddToViewport(40);
	BindBattleEnded(); RefreshPersistentData(); RefreshRoster(); RefreshMapSelection(); ShowPanel(EMSLobbyPanel::Lobby);
	if (auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr)
		Rooms->OnRoomStatusChanged.AddUniqueDynamic(this, &UMSLobbyWidget::RefreshRoomStatus);
	RefreshRoomStatus();
	RestoreBottomStripLayout();
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("Lobby"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSLobbyWidget::RestoreBottomStripLayout()
{
	if (!BottomStripSizeBox) return;
	BottomStripSizeBox->SetWidthOverride(1016.0f);
	BottomStripSizeBox->SetHeightOverride(MSLobby::BottomStripHeight);
	if (UVerticalBoxSlot* BottomStripSlot = Cast<UVerticalBoxSlot>(BottomStripSizeBox->Slot))
	{
		BottomStripSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		BottomStripSlot->SetHorizontalAlignment(HAlign_Left);
		BottomStripSlot->SetPadding(FMargin(452.0f, 2.0f, 0.0f, 0.0f));
	}
}

void UMSLobbyWidget::Close()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (GetWorld()) UGameplayStatics::SetGamePaused(this, false);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("Lobby"));
}

void UMSLobbyWidget::ShowPanel(const EMSLobbyPanel Panel)
{
	if (LobbyPanel) LobbyPanel->SetVisibility(Panel == EMSLobbyPanel::Lobby ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (SquadPanel) SquadPanel->SetVisibility(Panel == EMSLobbyPanel::Squad ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (MissionChoicePanel) MissionChoicePanel->SetVisibility(Panel == EMSLobbyPanel::MissionChoice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (OperationPanel) OperationPanel->SetVisibility(Panel == EMSLobbyPanel::Operation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// The page frame owns the background and clipping area, so mirror panel visibility on the
	// wrapper as well. This keeps a collapsed secondary page from leaving an empty center plate
	// over the lobby and preserves the existing panel state/callbacks for automation callers.
	if (SquadPageScale) SquadPageScale->SetVisibility(Panel == EMSLobbyPanel::Squad ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (MissionChoicePageScale) MissionChoicePageScale->SetVisibility(Panel == EMSLobbyPanel::MissionChoice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (OperationPageScale) OperationPageScale->SetVisibility(Panel == EMSLobbyPanel::Operation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (Panel == EMSLobbyPanel::Squad) RefreshRoster();
}

void UMSLobbyWidget::RefreshRoomStatus()
{
	const auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr;
	const bool bFailure = MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance())) && Rooms &&
		!Rooms->IsRoomOperationPending() && !Rooms->GetRoomStatusText().IsEmpty();
	if (LobbyNoticeTitle) LobbyNoticeTitle->SetText(FText::FromString(bFailure ? TEXT("ルーム接続エラー") : TEXT("出撃準備")));
	if (LobbyNoticeBody) LobbyNoticeBody->SetText(bFailure ? Rooms->GetRoomStatusText() :
		FText::FromString(TEXT("機体をカスタマイズして、戦場へ。")));
	RefreshParty();
}

void UMSLobbyWidget::RefreshPersistentData()
{
	const FGenericTeamId Faction = MSFactionPresentation::ResolveMatchTeam(GetOwningPlayer());
	const TCHAR* FactionName = MSFactionPresentation::JapaneseName(Faction);
	if (BreadcrumbText) BreadcrumbText->SetText(FText::FromString(FString::Printf(TEXT("ブリッジ  /  %s  /  ローカル"), FactionName)));
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()); if (!Instance) return;
	if (SquadSummaryText) SquadSummaryText->SetText(FText::Format(FText::FromString(TEXT("{0}  {1}")),
		FText::FromString(MSLobby::GetEmblemLabel(Instance->SquadEmblemIndex)), FText::FromString(Instance->SquadName)));
	const FString SelfName = MSLobby::ResolvePilotDisplayName(Instance);
	const FString DisplayName = SelfName.IsEmpty() ? TEXT("未ログイン") : SelfName;
	if (PilotStatsText) PilotStatsText->SetText(FText::Format(FText::FromString(TEXT("{0}\nGP          ---\nBC          ---\n\nRANK   {1}")),
		FText::FromString(DisplayName), UMSGameInstance::GetRankName(Instance->KillCount)));
	if (PilotAvatarBox && PilotAvatarImage)
	{
		UTexture2D* AvatarTexture = MSAvatarLibrary::ResolveAvatarTexture(DisplayName);
		if (AvatarTexture) PilotAvatarImage->SetBrushFromTexture(AvatarTexture, false);
		PilotAvatarBox->SetVisibility(AvatarTexture ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	bUpdatingSquadName = true; if (SquadNameInput) SquadNameInput->SetText(FText::FromString(Instance->SquadName)); bUpdatingSquadName = false;
	if (EmblemSelectionText) EmblemSelectionText->SetText(FText::Format(FText::FromString(TEXT("選択中: {0}")),
		FText::FromString(MSLobby::GetEmblemLabel(Instance->SquadEmblemIndex))));
}

void UMSLobbyWidget::RefreshRoster()
{
	if (RosterBox) RosterBox->ClearChildren();
	if (LobbyRosterBox) LobbyRosterBox->ClearChildren();
	const UMSUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>() : nullptr;
	for (const FString& Name : BuildRosterNames(Registry))
	{
		if (RosterBox) RosterBox->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(Name),
			MSUIStyle::ETextRole::Body));
	}

	if (!LobbyRosterBox) return;
	const UMSFriendSubsystem* Friends = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	for (const FMSFriendEntry& Entry : BuildPilotListEntries(Friends))
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Row->SetToolTipText(FText::FromString(TEXT("保存済みパイロット / 接続状態未確認")));
		UBorder* RankBlock = WidgetTree->ConstructWidget<UBorder>();
		MSPixelUI::PlainBorder(RankBlock, MSPixel::Hex(0x254A49));
		RankBlock->SetPadding(FMargin(3, 1));
		UTextBlock* RankBars = MSPixelUI::Text(WidgetTree,
			FText::FromString(FString::Printf(TEXT("%02d"), Entry.Rank)), 12, MSPixel::Hex(0xe9e5b2));
		RankBars->SetJustification(ETextJustify::Center);
		RankBlock->SetContent(RankBars);
		USizeBox* RankSize = WidgetTree->ConstructWidget<USizeBox>();
		RankSize->SetWidthOverride(30); RankSize->SetContent(RankBlock);
		Row->AddChildToHorizontalBox(RankSize)->SetPadding(FMargin(0, 1, 6, 1));

		const bool bOnline = Entry.State != EMSFriendState::Offline;
		UTextBlock* Name = MSPixelUI::Text(WidgetTree, FText::FromString(Entry.Name), 13,
			bOnline ? MSUIStyleColor::TextPrimary : FLinearColor(0.36f, 0.43f, 0.47f, 1));
		Name->SetAutoWrapText(false); Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); Name->SetToolTipText(FText::FromString(Entry.Name));
		Row->AddChildToHorizontalBox(Name)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(); RowSize->SetHeightOverride(22); RowSize->SetContent(Row);
		LobbyRosterBox->AddChild(RowSize);
	}

	RefreshParty();
}

void UMSLobbyWidget::RefreshParty()
{
	if (!PartyRowTop || !PartyRowBottom) return;

	PartyRowTop->ClearChildren();
	PartyRowBottom->ClearChildren();

	const UMSFriendSubsystem* Friends = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	TArray<FMSFriendEntry> SquadMembers;
	if (Friends)
	{
		for (const FMSFriendEntry& Entry : Friends->GetFriends())
			if (Entry.bIsSquadMember && Friends->IsSquadMembershipConfirmedThisSession(Entry.Name)) SquadMembers.Add(Entry);
		SquadMembers.StableSort([](const FMSFriendEntry& A, const FMSFriendEntry& B) { return A.Name < B.Name; });
	}
	const auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr;
	const bool bRoomFailure = MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance())) && Rooms &&
		!Rooms->IsRoomOperationPending() && !Rooms->GetRoomStatusText().IsEmpty();
	if (ResumeSavedPartyButton) ResumeSavedPartyButton->SetVisibility(!bRoomFailure && Friends && Friends->GetSquadMemberCount() > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// The lower-right 210px region is a state slot: show the real party only when
	// there is an actual member, otherwise keep the authored notice area visible.
	if (LobbyNoticeFrame && LobbyPartyFrame)
	{
		const bool bHasParty = !bRoomFailure && !SquadMembers.IsEmpty();
		LobbyNoticeFrame->SetVisibility(bHasParty ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		LobbyPartyFrame->SetVisibility(bHasParty ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	for (int32 SlotIndex = 0; SlotIndex < 6; ++SlotIndex)
	{
		UWidget* SlotWidget = nullptr;
		if (SlotIndex == 0) SlotWidget = BuildPartySelfSlot();
		else if (SquadMembers.IsValidIndex(SlotIndex - 1)) SlotWidget = BuildPartyMemberSlot(SquadMembers[SlotIndex - 1]);
		else SlotWidget = BuildPartyEmptySlot();

		UHorizontalBox* TargetRow = SlotIndex < 3 ? PartyRowTop : PartyRowBottom;
		TargetRow->AddChildToHorizontalBox(SlotWidget)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (PartyCommentInput)
	{
		const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
		const FString Comment = Instance ? Instance->PartyComment : FString();
		bUpdatingPartyComment = true;
		PartyCommentInput->SetText(FText::FromString(Comment));
		bUpdatingPartyComment = false;
	}
}

UWidget* UMSLobbyWidget::BuildPartySelfSlot()
{
	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Card, MSUIStyle::EBrush::ListRow, MSLobby::CardColor);
	Card->SetPadding(FMargin(5, 3));
	Card->SetContent(Content);

	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const FString SelfName = MSLobby::ResolvePilotDisplayName(Instance);
	const FString DisplayName = SelfName.IsEmpty() ? TEXT("未ログイン") : SelfName;
	if (UWidget* Avatar = MSAvatarLibrary::MakeAvatarWidget(WidgetTree, DisplayName, 30.0f))
	{
		USizeBox* AvatarSize = WidgetTree->ConstructWidget<USizeBox>();
		AvatarSize->SetWidthOverride(30.0f); AvatarSize->SetHeightOverride(30.0f); AvatarSize->SetContent(Avatar);
		Content->AddChildToHorizontalBox(AvatarSize)->SetPadding(FMargin(0, 0, 5, 0));
	}
	UVerticalBox* Details = WidgetTree->ConstructWidget<UVerticalBox>();
	Content->AddChildToHorizontalBox(Details)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Details->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(FString::Printf(TEXT("★ %s"), *DisplayName)), MSUIStyle::ETextRole::Caption, true));

	UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
	Badge->SetBrushColor(UMSCommunityWidget::GetStateColor(EMSFriendState::Lobby));
	Badge->SetPadding(FMargin(3, 0));
	UTextBlock* StateText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ローカル")),
		MSUIStyle::ETextRole::Micro, true, FLinearColor::White);
	StateText->SetClipping(EWidgetClipping::ClipToBounds);
	Badge->SetContent(StateText);

	UHorizontalBox* Status = WidgetTree->ConstructWidget<UHorizontalBox>();
	Details->AddChildToVerticalBox(Status);
	Status->AddChildToHorizontalBox(Badge)->SetPadding(FMargin(0, 1, 4, 0));
	Status->AddChildToHorizontalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("RATE ---")), MSUIStyle::ETextRole::Micro));

	return Card;
}

UWidget* UMSLobbyWidget::BuildPartyMemberSlot(const FMSFriendEntry& Entry)
{
	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Card, MSUIStyle::EBrush::ListRow, MSLobby::CardColor);
	Card->SetPadding(FMargin(5, 3));
	Card->SetContent(Content);

	const bool bOnline = Entry.State != EMSFriendState::Offline;
	if (UWidget* Avatar = MSAvatarLibrary::MakeAvatarWidget(WidgetTree, Entry.Name, 30.0f))
	{
		USizeBox* AvatarSize = WidgetTree->ConstructWidget<USizeBox>();
		AvatarSize->SetWidthOverride(30.0f); AvatarSize->SetHeightOverride(30.0f); AvatarSize->SetContent(Avatar);
		Content->AddChildToHorizontalBox(AvatarSize)->SetPadding(FMargin(0, 0, 5, 0));
	}
	UVerticalBox* Details = WidgetTree->ConstructWidget<UVerticalBox>();
	Content->AddChildToHorizontalBox(Details)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Details->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(Entry.Name),
		MSUIStyle::ETextRole::Caption, true,
		bOnline ? MSUIStyleColor::TextPrimary : FLinearColor(0.36f, 0.43f, 0.47f, 1)));
	Details->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(FString::Printf(TEXT("RANK %d"), Entry.Rank)), MSUIStyle::ETextRole::Micro));

	UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
	Badge->SetBrushColor(UMSCommunityWidget::GetStateColor(Entry.State));
	Badge->SetPadding(FMargin(3, 0));
	UTextBlock* StateText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ローカル")),
		MSUIStyle::ETextRole::Micro, true,
		bOnline ? FLinearColor::White : FLinearColor(0.55f, 0.58f, 0.6f, 1));
	StateText->SetClipping(EWidgetClipping::ClipToBounds);
	Badge->SetContent(StateText);
	UHorizontalBox* Status = WidgetTree->ConstructWidget<UHorizontalBox>();
	Details->AddChildToVerticalBox(Status);
	Status->AddChildToHorizontalBox(Badge)->SetPadding(FMargin(0, 1, 4, 0));
	Status->AddChildToHorizontalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(FString::Printf(TEXT("保存RATE %d"), Entry.Rate)), MSUIStyle::ETextRole::Micro));

	return Card;
}

UWidget* UMSLobbyWidget::BuildPartyEmptySlot()
{
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Card, MSUIStyle::EBrush::ListRow, FLinearColor(0.018f, 0.05f, 0.085f, 0.9f));
	Card->SetPadding(FMargin(6, 5));

	UTextBlock* EmptyLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("NO PILOT")),
		MSUIStyle::ETextRole::Caption, false, MSLobby::AccentColor);
	EmptyLabel->SetJustification(ETextJustify::Center);
	Card->SetHorizontalAlignment(HAlign_Fill);
	Card->SetVerticalAlignment(VAlign_Fill);
	Card->SetContent(EmptyLabel);

	return Card;
}

int32 UMSLobbyWidget::GetPartySlotCount() const
{
	return (PartyRowTop ? PartyRowTop->GetChildrenCount() : 0) + (PartyRowBottom ? PartyRowBottom->GetChildrenCount() : 0);
}

int32 UMSLobbyWidget::GetDisplayedRosterCount() const { return LobbyRosterBox ? LobbyRosterBox->GetChildrenCount() : 0; }
FLinearColor UMSLobbyWidget::GetBackgroundColor() const { return Background ? Background->GetBrushColor() : FLinearColor::Transparent; }

void UMSLobbyWidget::SelectBattleRule(const int32 RuleIndex)
{
	if (!GetBattleRules().IsValidIndex(RuleIndex)) return; SelectedRuleIndex = RuleIndex; RefreshRuleSelection();
}
void UMSLobbyWidget::RefreshRuleSelection()
{
	for (int32 Index = 0; Index < RuleCards.Num(); ++Index) if (RuleCards[Index])
		MSUIStyle::ApplyBrush(RuleCards[Index], Index == SelectedRuleIndex ? MSUIStyle::EBrush::ListRowSelected : MSUIStyle::EBrush::ListRow, Index == SelectedRuleIndex ? MSLobby::SelectedColor : MSLobby::CardColor);
}

int32 UMSLobbyWidget::GetMapPageCount() const
{
	return FMath::Max(1, FMath::DivideAndRoundUp(MapCardButtons.Num(), MSLobby::MapsPerPage));
}

bool UMSLobbyWidget::IsMapCardSelected(const int32 MapIndex) const
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const TArray<FMSMapDefinitionRow>& Definitions = FMSMapDefinitionRow::GetVisibleMapDefinitions();
	return Instance && Definitions.IsValidIndex(MapIndex) && Instance->SelectedMapId == Definitions[MapIndex].MapId;
}

void UMSLobbyWidget::RefreshMapSelection()
{
	const int32 PageCount = GetMapPageCount();
	CurrentMapPage = FMath::Clamp(CurrentMapPage, 0, PageCount - 1);
	if (MapPageLabel)
		MapPageLabel->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), CurrentMapPage + 1, PageCount)));
	if (PreviousMapPageButton) PreviousMapPageButton->SetIsEnabled(CurrentMapPage > 0);
	if (NextMapPageButton) NextMapPageButton->SetIsEnabled(CurrentMapPage + 1 < PageCount);
	for (int32 Index = 0; Index < MapCardButtons.Num(); ++Index)
	{
		if (MapCardButtons[Index])
			MapCardButtons[Index]->SetVisibility(Index / MSLobby::MapsPerPage == CurrentMapPage
				? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (MapCardBorders.IsValidIndex(Index) && MapCardBorders[Index])
			MSPixelUI::PlainBorder(MapCardBorders[Index], MSPixel::Hex(IsMapCardSelected(Index) ? 0xe3c25c : 0x586967));
	}
}

void UMSLobbyWidget::SelectMap(const int32 MapIndex)
{
	const TArray<FMSMapDefinitionRow>& Definitions = FMSMapDefinitionRow::GetVisibleMapDefinitions();
	if (!Definitions.IsValidIndex(MapIndex) || !Definitions[MapIndex].bPlayable) return;
	if (MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance())))
	{
		// Online map choice belongs to room creation. Preserve the map strip as a
		// discovery surface and route its click to the form with the chosen map.
		if (EnsureRoomCreateWidget())
		{
			Close();
			RoomCreateWidget->OpenCreateForMap(Definitions[MapIndex].MapId);
		}
		return;
	}
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
	{
		Instance->SelectedMapId = Definitions[MapIndex].MapId;
		RefreshMapSelection();
	}
}

bool UMSLobbyWidget::ApplySelectedBattleRule()
{
	if (!GetBattleRules().IsValidIndex(SelectedRuleIndex) || !GetWorld()) return false;
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	FString DestinationLevelName;
	if (PrepareMapTravel(Instance, SelectedRuleIndex,
		UGameplayStatics::GetCurrentLevelName(this, true), DestinationLevelName))
	{
		const FName SelectedMapId = Instance->SelectedMapId;
		// The lobby owns both the pause and the GameInstance-subsystem screen-stack entry.
		// Close it before destroying this world so neither state survives travel.
		Close();
		UGameplayStatics::SetGamePaused(this, false);
		UGameplayStatics::OpenLevel(this, FName(*DestinationLevelName), true,
			FString::Printf(TEXT("MapId=%s"), *SelectedMapId.ToString()));
		return true;
	}
	AMSGameMode* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>(); AMSGameState* State = GetWorld()->GetGameState<AMSGameState>();
	if (!ApplyBattleRule(GetBattleRules()[SelectedRuleIndex], Mode, State)) return false;
	if (!BriefingWidget)
	{
		APlayerController* PC = GetOwningPlayer(); if (!PC) return false;
		TArray<UUserWidget*> Existing; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSBriefingWidget::StaticClass(), false);
		BriefingWidget = Existing.IsEmpty() ? CreateWidget<UMSBriefingWidget>(PC) : Cast<UMSBriefingWidget>(Existing[0]);
	}
	if (!BriefingWidget) return false;
	BriefingWidget->SetLobbyWidget(this);
	BriefingWidget->SetRoomInformation(GetBattleRules()[SelectedRuleIndex].Name, State->MapDisplayName);
	return UMSUITransitionSubsystem::Transition(this, BriefingWidget,
		FSimpleDelegate::CreateWeakLambda(this, [this]() { Close(); BriefingWidget->Open(); }));
}

bool UMSLobbyWidget::PrepareMapTravel(UMSGameInstance* GameInstance, const int32 RuleIndex,
	const FString& CurrentLevelName, FString& OutDestinationLevelName)
{
	OutDestinationLevelName.Reset();
	if (!GameInstance || !GetBattleRules().IsValidIndex(RuleIndex)) return false;

	const FMSMapDefinitionRow* SelectedMap = FMSMapDefinitionRow::FindMapById(GameInstance->SelectedMapId);
	if (!SelectedMap || !SelectedMap->bPlayable || SelectedMap->LevelPackagePath.IsEmpty()) return false;

	const FString NormalizedCurrentLevelName = UWorld::RemovePIEPrefix(FPackageName::GetShortName(CurrentLevelName));
	const FMSMapDefinitionRow* CurrentMap = FMSMapDefinitionRow::FindMapByLevelName(NormalizedCurrentLevelName);
	const FString SelectedLevelName = FPackageName::GetShortName(SelectedMap->LevelPackagePath);
	// Unknown transient/automation worlds retain the legacy in-world route. Runtime map-to-map
	// travel is only issued when both endpoints are entries in the authoritative definition table.
	if (!CurrentMap || SelectedLevelName.Equals(NormalizedCurrentLevelName, ESearchCase::IgnoreCase)) return false;

	GameInstance->bPendingSortie = true;
	GameInstance->PendingRuleIndex = RuleIndex;
	OutDestinationLevelName = SelectedLevelName;
	return true;
}

bool UMSLobbyWidget::ConsumePendingBattleRule(UMSGameInstance* GameInstance, AMSGameMode* GameMode,
	AMSGameState* GameState, FText& OutRuleName)
{
	OutRuleName = FText::GetEmpty();
	if (!GameInstance || !GameInstance->bPendingSortie) return false;

	const int32 RuleIndex = GameInstance->PendingRuleIndex;
	GameInstance->bPendingSortie = false;
	GameInstance->bPendingRoomHost = false;
	GameInstance->bPendingRoomJoin = false;
	if (!GetBattleRules().IsValidIndex(RuleIndex)) return false;

	const FMSBattleRuleRow& Rule = GetBattleRules()[RuleIndex];
	// The listen server owns rule mutation. A joined client only needs to continue into the
	// same briefing/deployment flow; replicated GameState already carries authoritative values.
	if (GameMode)
	{
		if (!ApplyBattleRule(Rule, GameMode, GameState)) return false;
	}
	else if (!GameState)
	{
		return false;
	}
	OutRuleName = Rule.Name;
	return true;
}

void UMSLobbyWidget::HandleSquadNameChanged(const FText& Text)
{
	if (bUpdatingSquadName) return; FString Value = Text.ToString().Left(16);
	if (Value != Text.ToString()) { bUpdatingSquadName = true; SquadNameInput->SetText(FText::FromString(Value)); bUpdatingSquadName = false; }
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance())) Instance->SquadName = Value;
	RefreshPersistentData();
}
void UMSLobbyWidget::HandleResumeSavedPartyClicked()
{
	UMSFriendSubsystem* Friends = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	if (!Friends) return;
	TArray<FString> SavedNames;
	for (const FMSFriendEntry& Entry : Friends->GetFriends()) if (Entry.bIsSquadMember) SavedNames.Add(Entry.Name);
	for (const FString& Name : SavedNames) Friends->SetFriendSquadMember(Name, true);
	RefreshParty();
}

void UMSLobbyWidget::HandlePartyCommentChanged(const FText& Text)
{
	if (bUpdatingPartyComment) return;
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance())) Instance->PartyComment = Text.ToString();
}
void UMSLobbyWidget::HandlePartyInviteClicked()
{
	if (EnsureCommunityWidget()) { CommunityWidget->ShowTab(EMSCommunityTab::Friends); CommunityWidget->Open(); }
}
void UMSLobbyWidget::HandlePartyInfoClicked()
{
	if (EnsureCommunityWidget()) { CommunityWidget->ShowTab(EMSCommunityTab::Squad); CommunityWidget->Open(); }
}
void UMSLobbyWidget::HandlePartyLeaveClicked()
{
	UMSFriendSubsystem* Friends = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	if (Friends)
	{
		for (const FMSFriendEntry& Entry : TArray<FMSFriendEntry>(Friends->GetFriends()))
			if (Entry.bIsSquadMember) Friends->SetFriendSquadMember(Entry.Name, false);
	}
	RefreshParty();
}
void UMSLobbyWidget::SelectEmblem(const int32 Index) { if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance())) { Instance->SquadEmblemIndex = FMath::Clamp(Index, 0, 5); RefreshPersistentData(); } }
void UMSLobbyWidget::HandleLobbyTabClicked() { ShowPanel(EMSLobbyPanel::Lobby); }
void UMSLobbyWidget::HandleSquadTabClicked() { ShowPanel(EMSLobbyPanel::Squad); }
void UMSLobbyWidget::HandleOperationTabClicked() { ShowPanel(EMSLobbyPanel::Operation); }
void UMSLobbyWidget::HandleMissionClicked() { ShowPanel(EMSLobbyPanel::MissionChoice); }
void UMSLobbyWidget::HandleQuickSortieClicked()
{
	if (MSLobby::IsOnlineMode(Cast<UMSGameInstance>(GetGameInstance())))
	{
		HandleJoinRoomClicked();
		return;
	}
	ShowPanel(EMSLobbyPanel::Operation);
}
void UMSLobbyWidget::HandleCreateRoomClicked()
{
	if (EnsureRoomCreateWidget()) { Close(); RoomCreateWidget->OpenCreate(); }
}
void UMSLobbyWidget::HandleJoinRoomClicked()
{
	if (EnsureRoomCreateWidget()) { Close(); RoomCreateWidget->OpenJoin(); }
}
bool UMSLobbyWidget::EnsureShopWidget()
{
	if (!ShopWidget)
	{
		APlayerController* PC = GetOwningPlayer();
		if (!PC) return false;
		TArray<UUserWidget*> Existing; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSShopWidget::StaticClass(), false);
		ShopWidget = Existing.IsEmpty() ? CreateWidget<UMSShopWidget>(PC) : Cast<UMSShopWidget>(Existing[0]);
	}
	if (ShopWidget) ShopWidget->SetLobbyWidget(this);
	return ShopWidget != nullptr;
}
bool UMSLobbyWidget::EnsureCustomizeWidget()
{
	if (!CustomizeWidget)
	{
		APlayerController* PC = GetOwningPlayer();
		if (!PC) return false;
		TArray<UUserWidget*> Existing; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSCustomizeWidget::StaticClass(), false);
		CustomizeWidget = Existing.IsEmpty() ? CreateWidget<UMSCustomizeWidget>(PC) : Cast<UMSCustomizeWidget>(Existing[0]);
	}
	if (CustomizeWidget) CustomizeWidget->SetLobbyWidget(this);
	return CustomizeWidget != nullptr;
}
bool UMSLobbyWidget::EnsureResultWidget()
{
	if (!ResultWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSResultWidget::StaticClass(), false);
		ResultWidget = Existing.IsEmpty() ? nullptr : Cast<UMSResultWidget>(Existing[0]);
		if (!ResultWidget)
		{
			APlayerController* PC = GetOwningPlayer();
			if (!PC) return false;
			ResultWidget = CreateWidget<UMSResultWidget>(PC);
		}
	}
	if (ResultWidget) ResultWidget->SetLobbyWidget(this);
	return ResultWidget != nullptr;
}
bool UMSLobbyWidget::EnsureCommunityWidget()
{
	if (!CommunityWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSCommunityWidget::StaticClass(), false);
		CommunityWidget = Existing.IsEmpty() ? nullptr : Cast<UMSCommunityWidget>(Existing[0]);
		if (!CommunityWidget)
		{
			APlayerController* PC = GetOwningPlayer();
			if (!PC) return false;
			CommunityWidget = CreateWidget<UMSCommunityWidget>(PC);
		}
	}
	if (CommunityWidget) CommunityWidget->SetLobbyWidget(this);
	return CommunityWidget != nullptr;
}
bool UMSLobbyWidget::EnsureRoomCreateWidget()
{
	if (!RoomCreateWidget)
	{
		APlayerController* PC = GetOwningPlayer();
		if (!PC) return false;
		RoomCreateWidget = CreateWidget<UMSRoomCreateWidget>(PC);
	}
	RoomCreateWidget->SetLobbyWidget(this);
	return true;
}
void UMSLobbyWidget::HandleShopClicked()
{
	if (EnsureShopWidget()) UMSUITransitionSubsystem::Transition(this, ShopWidget,
		FSimpleDelegate::CreateWeakLambda(this, [this]() { Close(); ShopWidget->Open(); }));
}
void UMSLobbyWidget::HandleCustomizeClicked()
{
	if (EnsureCustomizeWidget()) UMSUITransitionSubsystem::Transition(this, CustomizeWidget,
		FSimpleDelegate::CreateWeakLambda(this, [this]() { Close(); CustomizeWidget->Open(); }));
}

void UMSLobbyWidget::OpenCustomizeScreen()
{
	HandleCustomizeClicked();
}

void UMSLobbyWidget::OpenResultScreen()
{
	if (EnsureResultWidget())
	{
		Close();
		ResultWidget->Open();
	}
}
void UMSLobbyWidget::HandleCommunityClicked()
{
	if (EnsureCommunityWidget()) CommunityWidget->Open();
}
void UMSLobbyWidget::HandleSettingsClicked()
{
	// Settings needs no possessed AMSPlayerCharacter (it falls back to GConfig for FOV/sensitivity),
	// so it opens directly here rather than routing through AMSPlayerCharacter::OpenSettingsScreen.
	if (!SettingsWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSSettingsWidget::StaticClass(), false);
		SettingsWidget = Existing.IsEmpty()
			? CreateWidget<UMSSettingsWidget>(GetOwningPlayer())
			: Cast<UMSSettingsWidget>(Existing[0]);
	}
	if (SettingsWidget) SettingsWidget->OpenSettings();
}
void UMSLobbyWidget::HandleLogoutClicked()
{
	// EXIT = logout: close the lobby, clear the transient login state, reopen the login screen.
	Close();
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance())) Instance->Logout();
	if (LoginWidget) { LoginWidget->Open(); return; }
	LoginWidget = UMSLoginWidget::OpenLoginScreen(GetOwningPlayer());
}
void UMSLobbyWidget::HandleRule0Clicked() { SelectBattleRule(0); } void UMSLobbyWidget::HandleRule1Clicked() { SelectBattleRule(1); } void UMSLobbyWidget::HandleRule2Clicked() { SelectBattleRule(2); }
void UMSLobbyWidget::HandlePreviousMapPageClicked() { --CurrentMapPage; RefreshMapSelection(); }
void UMSLobbyWidget::HandleNextMapPageClicked() { ++CurrentMapPage; RefreshMapSelection(); }
void UMSLobbyWidget::HandleMap0Clicked() { SelectMap(0); }
void UMSLobbyWidget::HandleMap1Clicked() { SelectMap(1); }
void UMSLobbyWidget::HandleMap2Clicked() { SelectMap(2); }
void UMSLobbyWidget::HandleMap3Clicked() { SelectMap(3); }
void UMSLobbyWidget::HandleMap4Clicked() { SelectMap(4); }
void UMSLobbyWidget::HandleMap5Clicked() { SelectMap(5); }
void UMSLobbyWidget::HandleMap6Clicked() { SelectMap(6); }
void UMSLobbyWidget::HandleConfirmClicked() { ApplySelectedBattleRule(); }
void UMSLobbyWidget::HandleEmblem0Clicked() { SelectEmblem(0); } void UMSLobbyWidget::HandleEmblem1Clicked() { SelectEmblem(1); }
void UMSLobbyWidget::HandleEmblem2Clicked() { SelectEmblem(2); } void UMSLobbyWidget::HandleEmblem3Clicked() { SelectEmblem(3); }
void UMSLobbyWidget::HandleEmblem4Clicked() { SelectEmblem(4); } void UMSLobbyWidget::HandleEmblem5Clicked() { SelectEmblem(5); }

void UMSLobbyWidget::HandleBattleEnded(bool)
{
	// Drain topmost screens without allowing normal parent restoration during battle-end cleanup.
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSFullscreenMapWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSFullscreenMapWidget* Typed = Cast<UMSFullscreenMapWidget>(Widget)) Typed->Close();
	Found.Reset();
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSDeploymentMapWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSDeploymentMapWidget* Typed = Cast<UMSDeploymentMapWidget>(Widget)) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSSpawnSelectWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSSpawnSelectWidget* Typed = Cast<UMSSpawnSelectWidget>(Widget)) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSSortieWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSSortieWidget* Typed = Cast<UMSSortieWidget>(Widget)) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSBriefingWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSBriefingWidget* Typed = Cast<UMSBriefingWidget>(Widget)) Typed->CloseForBattleEnd();
	if (EnsureResultWidget()) ResultWidget->Open();
	else Open();
}
