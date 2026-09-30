#include "UI/MSBriefingWidget.h"
#include "UI/MSFactionPresentation.h"
#include "Online/MSRoomSubsystem.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSBattleRuleRow.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Gameplay/MSPlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/MSPlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSAvatarLibrary.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUITransitionSubsystem.h"
#include "Voice/MSNavigatorSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"

namespace MSBriefing
{
	UTextBlock* Text(UWidgetTree* Tree, const TCHAR* Value, float Pixels = 16, FName Name = NAME_None)
	{
		UTextBlock* Result = MSUIStyle::MakeText(Tree, FText::FromString(Value), MSUIStyle::ETextRole::Body,
			false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, Name);
		FSlateFontInfo Font = Result->GetFont(); Font.Size = FMath::RoundToInt(Pixels * 0.75f); Result->SetFont(Font);
		return Result;
	}
	const FLinearColor Background(0.008f, 0.018f, 0.028f, 0.97f);
	const FLinearColor Panel(0.025f, 0.075f, 0.095f, 0.98f);
	const FLinearColor Accent(0.12f, 0.86f, 0.95f, 1.0f);
	const FLinearColor TeamA = MSFactionPresentation::RebellionColor;
	const FLinearColor TeamB = MSFactionPresentation::OrderColor;

	FString UnitDisplayName(const AMSUnitBase* Unit)
	{
		if (!Unit) return FString();
		if (const UMSMobileSuitData* Data = Unit->GetMobileSuitData(); Data && !Data->UnitName.IsEmpty())
			return Data->UnitName.ToString();
		return Unit->GetName();
	}

	/** Online identity display names are safe for the UI; backend usernames stay private. */
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

void UMSBriefingMiniMapWidget::ShowEmbedded()
{
	if (!WidgetTree->RootWidget) Initialize();
	SetVisibility(ESlateVisibility::Visible);
	RefreshOverviewBackground();
	if (GetCachedCapturePoints().IsEmpty()) CacheCapturePointMarkers();
	RefreshCapturePointMarkers();
}

void UMSBriefingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildReferenceLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSBriefingWidget::BuildReferenceLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(); WidgetTree->RootWidget = Root;
	UBorder* Shade = WidgetTree->ConstructWidget<UBorder>(); Shade->SetBrushColor(FLinearColor(0, 0.01f, 0.02f, 0.35f));
	Root->AddChildToOverlay(Shade);
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BriefingDesignScale")); Fit->SetStretch(EStretch::ScaleToFit);
	UOverlaySlot* FitSlot = Root->AddChildToOverlay(Fit); FitSlot->SetHorizontalAlignment(HAlign_Fill); FitSlot->SetVerticalAlignment(VAlign_Fill);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(1920); Design->SetHeightOverride(1080); Fit->SetContent(Design);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BriefingDesignCanvas")); Design->SetContent(Canvas);
	auto Place = [Canvas](UWidget* Child, float X, float Y, float W, float H)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child); Slot->SetPosition(FVector2D(X, Y)); Slot->SetSize(FVector2D(W, H));
	};
	auto Frame = [this](UWidget* Child, FLinearColor Color = MSBriefing::Panel)
	{
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(); MSUIStyle::ApplyBrush(Panel, MSUIStyle::EBrush::LobbyPanel, Color);
		Panel->SetPadding(FMargin(8)); Panel->SetContent(Child); return Panel;
	};
	auto Button = [this](const TCHAR* Label, const TCHAR* Name, float Pixels = 16)
	{
		UTextBlock* Caption = MSBriefing::Text(WidgetTree, Label, Pixels); Caption->SetAutoWrapText(false); Caption->SetJustification(ETextJustify::Center);
		return MSUIStyle::MakeCapsuleButton(WidgetTree, Caption, MSBriefing::Panel, Name);
	};
	// The same peripheral positions as the bridge, with actual local pilot data.
	Place(Frame(MSBriefing::Text(WidgetTree, TEXT("ブリッジ  /  出撃準備"), 18)), 400, 30, 456, 56);
	Place(Frame(MSBriefing::Text(WidgetTree, TEXT("作戦準備　 >　 出撃機体確認　 >　 出撃"), 16)), 920, 34, 570, 52);
	Place(MSBriefing::Text(WidgetTree, TEXT("作戦準備中　出撃デッキを確認してください"), 14), 452, 864, 1016, 22);
	Place(Frame(MSBriefing::Text(WidgetTree, TEXT("チャット\n\n未接続"), 16)), 452, 890, 436, 150);
	UHorizontalBox* PilotShell = WidgetTree->ConstructWidget<UHorizontalBox>();
	ShellPilotAvatar = WidgetTree->ConstructWidget<UImage>(); ShellPilotAvatar->SetDesiredSizeOverride(FVector2D(96));
	PilotShell->AddChildToHorizontalBox(ShellPilotAvatar)->SetVerticalAlignment(VAlign_Center);
	ShellPilotText = MSBriefing::Text(WidgetTree, TEXT(""), 17, TEXT("BriefingShellPilotText"));
	UHorizontalBoxSlot* ShellNameSlot = PilotShell->AddChildToHorizontalBox(ShellPilotText); ShellNameSlot->SetPadding(FMargin(8, 0)); ShellNameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Place(Frame(PilotShell), 896, 890, 360, 150);
	ShellRosterText = MSBriefing::Text(WidgetTree, TEXT(""), 13); Place(Frame(ShellRosterText), 1262, 890, 206, 150);

	Place(Frame(nullptr), 456, 280, 345, 438);
	CountdownText = MSBriefing::Text(WidgetTree, TEXT(""), 20, TEXT("BriefingCountdownText")); CountdownText->SetJustification(ETextJustify::Center);
	Place(Frame(CountdownText), 464, 228, 330, 50);
	RoomInformationText = MSBriefing::Text(WidgetTree, TEXT(""), 17, TEXT("RoomInformationText")); RoomInformationText->SetJustification(ETextJustify::Center);
	Place(RoomInformationText, 468, 292, 321, 84);
	TeamASummaryText = MSBriefing::Text(WidgetTree, TEXT(""), 18, TEXT("BriefingTeamASummaryText")); TeamASummaryText->SetJustification(ETextJustify::Center);
	TeamBSummaryText = MSBriefing::Text(WidgetTree, TEXT(""), 18, TEXT("BriefingTeamBSummaryText")); TeamBSummaryText->SetJustification(ETextJustify::Center);
	Place(Frame(TeamASummaryText, MSBriefing::TeamA), 464, 383, 161, 67);
	Place(Frame(TeamBSummaryText, MSBriefing::TeamB), 632, 383, 161, 67);
	MiniMap = WidgetTree->ConstructWidget<UMSBriefingMiniMapWidget>(UMSBriefingMiniMapWidget::StaticClass(), TEXT("BriefingMiniMap"));
	MiniMap->Initialize(); MiniMap->ShowEmbedded(); Place(MiniMap, 472, 464, 313, 204);
	UTextBlock* MapCheck = MSBriefing::Text(WidgetTree, TEXT("戦場確認"), 15); MapCheck->SetJustification(ETextJustify::Center); Place(MapCheck, 526, 670, 208, 24);

	Place(Frame(nullptr), 812, 260, 654, 460);
	UButton* TeamAButton = Button(TEXT("リベリオン"), TEXT("BriefingTeamAButton"));
	UButton* TeamBButton = Button(TEXT("オーダー"), TEXT("BriefingTeamBButton"));
	TeamAButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleTeamASelected); TeamBButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleTeamBSelected);
	Place(TeamAButton, 816, 232, 128, 32); Place(TeamBButton, 950, 232, 128, 32);
	SuitConfirmationButton = Button(TEXT("出撃機体確認"), TEXT("SuitConfirmationButton"), 14);
	SuitConfirmationButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleSuitConfirmationClicked); Place(SuitConfirmationButton, 1090, 238, 126, 26);
	EquipmentCaption=MSBriefing::Text(WidgetTree,TEXT("装備換装"),14); EquipmentCaption->SetJustification(ETextJustify::Center);
	UButton* Equipment=MSUIStyle::MakeCapsuleButton(WidgetTree,EquipmentCaption,MSBriefing::Panel,TEXT("EquipmentChangeButton"));
	Equipment->OnClicked.AddDynamic(this,&UMSBriefingWidget::HandleEquipmentClicked);Place(Equipment,1220,238,116,26);
	UButton* Back = Button(TEXT("戻る"), TEXT("BriefingBackButton")); Back->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleBackClicked); Place(Back, 1354, 224, 112, 32);
	SelectedTeamText = MSBriefing::Text(WidgetTree, TEXT(""), 16, TEXT("SelectedBriefingTeamText")); Place(SelectedTeamText, 826, 276, 616, 26);
	Place(Frame(MSBriefing::Text(WidgetTree, TEXT("指揮官　---"), 17), FLinearColor(0.08f, 0.14f, 0.07f)), 824, 308, 288, 48);
	PilotGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("PilotCardGrid"));
	for (int32 I = 0; I < 3; ++I) PilotGrid->SetColumnFill(I, 1);
	for (int32 I = 0; I < 4; ++I) PilotGrid->SetRowFill(I, 1);
	Place(PilotGrid, 944, 366, 506, 324);
	PilotPageText = MSBriefing::Text(WidgetTree, TEXT(""), 14, TEXT("BriefingPilotPageText")); PilotPageText->SetJustification(ETextJustify::Center); Place(PilotPageText, 1005, 696, 350, 20);
	PreviousPilotsButton = Button(TEXT("<"), TEXT("BriefingPreviousPilots"), 14); NextPilotsButton = Button(TEXT(">"), TEXT("BriefingNextPilots"), 14);
	PreviousPilotsButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandlePreviousPilots); NextPilotsButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleNextPilots);
	Place(PreviousPilotsButton, 948, 694, 44, 24); Place(NextPilotsButton, 1398, 694, 44, 24);
	StartButton = Button(TEXT("作戦開始"), TEXT("StartOperationButton"), 24); StartButton->OnClicked.AddDynamic(this, &UMSBriefingWidget::HandleStartClicked);
	Place(StartButton, 1106, 742, 360, 54);
	SelectTeam(0);
}

void UMSBriefingWidget::SetRoomInformation(const FText& BattleRuleName, const FText& MapName)
{
	SelectedBattleRuleName = BattleRuleName; SelectedMapName = MapName; RefreshRoomInformation();
}

void UMSBriefingWidget::RefreshRoomInformation()
{
	if (!RoomInformationText) return;
	const AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
	if (State && State->bPlayerHostedRoom)
	{
		const auto& Rules = FMSBattleRuleRow::GetBuiltInRules();
		const int32 RuleIndex = State->HostedRoomSettings.RuleIndex;
		const FText RuleName = Rules.IsValidIndex(RuleIndex) ? Rules[RuleIndex].Name : FText::GetEmpty();
		const FString PopulationText = State->HostedRoomSettings.bFillTeamsWithBots
			? FString(TEXT("25対25（空きをBOT自動補充）"))
			: FString::Printf(TEXT("敵Bot %d機"), State->HostedRoomSettings.BotCount);
		RoomInformationText->SetText(FText::Format(FText::FromString(TEXT("Room Information\n{0}\n{1}\n{2}")),
			RuleName, State->MapDisplayName, FText::FromString(PopulationText)));
		return;
	}
	const FText Rule = SelectedBattleRuleName.IsEmpty() ? FText::FromString(TEXT("戦術目標制圧作戦")) : SelectedBattleRuleName;
	const FText Map = SelectedMapName.IsEmpty()
		? (State ? State->MapDisplayName : FText::FromString(TEXT("アンリアライザコロニー")))
		: SelectedMapName;
	RoomInformationText->SetText(FText::Format(FText::FromString(TEXT("Room Information\n{0}\n{1}\nRoom No. ---")), Rule, Map));
}

void UMSBriefingWidget::RefreshCountdownText()
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
	CountdownText->SetText(RemainingSeconds > 0.0f
		? FText::Format(FText::FromString(TEXT("作戦開始まで {0}秒")), FText::AsNumber(FMath::CeilToInt(RemainingSeconds)))
		: FText::FromString(TEXT("作戦開始待ち")));
}

void UMSBriefingWidget::RefreshPilotCards()
{
	if (!PilotGrid) return;
	struct FPilotEntry { FString Name; FString SortKey; bool bLocal = false; bool bAI = false; };
	TArray<FPilotEntry> Pilots;
	int32 TeamACount = 0, TeamBCount = 0;
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance
		? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	const bool bOnlineMode = Online && Online->IsOnlineMode();
	if(EquipmentCaption) EquipmentCaption->SetText(FText::FromString(bOnlineMode ? TEXT("参戦装備確認") : TEXT("装備換装")));
	const FString SelfName = MSBriefing::ResolvePilotDisplayName(Instance);
	const APlayerController* OwningController = GetOwningPlayer();
	// A briefing is a local screen.  Resolve the local state through the owning
	// controller only when that controller is actually local; a remote
	// controller's PlayerState must never be marked as "（自分）".
	const APawn* OwningPawn = OwningController ? OwningController->GetPawn<APawn>() : GetOwningPlayerPawn();
	const bool bLocalController = (OwningController && OwningController->IsLocalController()) ||
		(OwningPawn && OwningPawn->IsLocallyControlled());
	const AMSPlayerState* ControllerPlayerState = bLocalController && OwningController
		? OwningController->GetPlayerState<AMSPlayerState>() : nullptr;
	const AMSPlayerState* LocalPlayerState = ControllerPlayerState;
	if (!LocalPlayerState && bLocalController && OwningPawn)
	{
		LocalPlayerState = OwningPawn->GetPlayerState<AMSPlayerState>();
	}
	TSet<const APlayerState*> VerifiedOnlinePlayers;
	if (bOnlineMode)
	{
		const AMSGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
		if (GameState)
		{
			for (const APlayerState* PlayerState : GameState->PlayerArray)
			{
				const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState);
				if (!IsValid(MSPlayerState) || MSPlayerState->GetLoginUsername().IsEmpty()) continue;
				const FGenericTeamId TeamId = MSPlayerState->GetSelectedFaction();
				if (TeamId != MSTeam::PlayerTeamId() && TeamId != MSTeam::EnemyTeamId()) continue;

				VerifiedOnlinePlayers.Add(PlayerState);
				if (TeamId == MSTeam::PlayerTeamId()) ++TeamACount; else ++TeamBCount;
				if (TeamId != (SelectedTeamIndex == 0 ? MSTeam::PlayerTeamId() : MSTeam::EnemyTeamId())) continue;

				const bool bLocal = bLocalController &&
					(PlayerState == LocalPlayerState || PlayerState->GetOwner() == OwningController);
				FString Name = bLocal && !SelfName.IsEmpty() ? SelfName : MSPlayerState->GetPlayerName();
				if (Name.IsEmpty()) Name = MSPlayerState->GetLoginUsername();
				Pilots.Add({Name, FString::Printf(TEXT("player:%s"), *Name), bLocal, false});
			}
		}
	}
	const UMSUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>() : nullptr;
	if (Registry) Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		if (!IsValid(Unit)) return;
		const APlayerState* UnitPlayerState = Unit->GetPlayerState();
		if (bOnlineMode)
		{
			// Authenticated PlayerStates are already represented above. An online
			// player pawn without verified identity is only a connected presence.
			if (UnitPlayerState && VerifiedOnlinePlayers.Contains(UnitPlayerState)) return;
			if (Unit->IsA<AMSPlayerCharacter>())
			{
				const AMSPlayerState* MSUnitPlayerState = Cast<AMSPlayerState>(UnitPlayerState);
				if (!MSUnitPlayerState || MSUnitPlayerState->GetLoginUsername().IsEmpty()) return;
			}
		}
		const FGenericTeamId TeamId = Unit->GetGenericTeamId();
		if (TeamId == MSTeam::PlayerTeamId()) ++TeamACount; else if (TeamId == MSTeam::EnemyTeamId()) ++TeamBCount;
		if (TeamId != (SelectedTeamIndex == 0 ? MSTeam::PlayerTeamId() : MSTeam::EnemyTeamId())) return;
		const bool bLocal = Unit == GetOwningPlayerPawn();
		const bool bAI = !Unit->IsA<AMSPlayerCharacter>();
		FString Name = bLocal && !SelfName.IsEmpty() ? SelfName : (UnitPlayerState ? UnitPlayerState->GetPlayerName() : FString());
		if (Name.IsEmpty()) Name = bAI ? MSBriefing::UnitDisplayName(Unit) : TEXT("パイロット");
		Pilots.Add({Name, Unit->GetName(), bLocal, bAI});
	});
	Pilots.Sort([](const FPilotEntry& A, const FPilotEntry& B) { return A.bLocal != B.bLocal ? A.bLocal : A.SortKey < B.SortKey; });
	FString Signature = FString::Printf(TEXT("%d/%d/%d/%d|"), SelectedTeamIndex, PilotPage, TeamACount, TeamBCount);
	for (const FPilotEntry& Entry : Pilots) Signature += FString::Printf(TEXT("%d%d:%s:%s|"), Entry.bLocal, Entry.bAI, *Entry.SortKey, *Entry.Name);
	if (Signature == LastRosterSignature) return;
	LastRosterSignature = MoveTemp(Signature);
	PilotGrid->ClearChildren(); PilotNameTexts.Reset();
	if (TeamASummaryText) TeamASummaryText->SetText(FText::FromString(FString::Printf(TEXT("REBELLION\n参加者 %d"), TeamACount)));
	if (TeamBSummaryText) TeamBSummaryText->SetText(FText::FromString(FString::Printf(TEXT("ORDER\n参加者 %d"), TeamBCount)));
	PilotCardCount = Pilots.Num();
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(PilotCardCount, 12)); PilotPage = FMath::Clamp(PilotPage, 0, Pages - 1);
	PreviousPilotsButton->SetIsEnabled(PilotPage > 0); NextPilotsButton->SetIsEnabled(PilotPage + 1 < Pages);
	PilotPageText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d　表示 %d–%d / %d名"), PilotPage + 1, Pages,
		PilotCardCount > 0 ? PilotPage * 12 + 1 : 0, FMath::Min((PilotPage + 1) * 12, PilotCardCount), PilotCardCount)));
	FString Summary = TEXT("PILOT LIST\n");
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const int32 PilotIndex = PilotPage * 12 + Index; const bool bOccupied = Pilots.IsValidIndex(PilotIndex);
		const FPilotEntry* Entry = bOccupied ? &Pilots[PilotIndex] : nullptr;
		const FString Name = Entry ? (Entry->bAI ? TEXT("AI · ") + Entry->Name : Entry->Name) + (Entry->bLocal ? TEXT("（自分）") : TEXT("")) : TEXT("空き");
		if (Entry && Index < 5) Summary += Name + TEXT("\n");
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(); MSUIStyle::ApplyBrush(Card, bOccupied ? MSUIStyle::EBrush::ListRowSelected : MSUIStyle::EBrush::ListRow,
			bOccupied ? FLinearColor(0.09f, 0.18f, 0.21f) : FLinearColor(0.04f, 0.06f, 0.07f)); Card->SetPadding(FMargin(4)); Card->SetClipping(EWidgetClipping::ClipToBounds);
		Card->SetToolTipText(FText::FromString(Name));
		UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(); Card->SetContent(Content);
		if (Entry) if (UWidget* Avatar = MSAvatarLibrary::MakeAvatarWidget(WidgetTree, Entry->Name, 36)) Content->AddChildToHorizontalBox(Avatar)->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Label = MSBriefing::Text(WidgetTree, *Name, 13); Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Label->SetJustification(ETextJustify::Center); PilotNameTexts.Add(Label);
		UHorizontalBoxSlot* NameSlot = Content->AddChildToHorizontalBox(Label); NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NameSlot->SetVerticalAlignment(VAlign_Center); NameSlot->SetPadding(FMargin(3, 0));
		UGridSlot* PilotSlot = PilotGrid->AddChildToGrid(Card, Index / 3, Index % 3); PilotSlot->SetPadding(FMargin(3)); PilotSlot->SetHorizontalAlignment(HAlign_Fill); PilotSlot->SetVerticalAlignment(VAlign_Fill);
	}
	ShellPilotAvatar->SetBrushFromTexture(MSAvatarLibrary::ResolveAvatarTexture(SelfName), false);
	ShellPilotText->SetText(FText::FromString(FString::Printf(TEXT("PILOT INFORMATION\n%s\n%s\nGP ---　BC ---"), SelfName.IsEmpty() ? TEXT("未ログイン") : *SelfName,
		Instance ? *UMSGameInstance::GetRankName(Instance->KillCount).ToString() : TEXT("---"))));
	ShellRosterText->SetText(FText::FromString(Summary)); ShellRosterText->SetClipping(EWidgetClipping::ClipToBounds);
}

FText UMSBriefingWidget::GetDisplayedPilotLabel(int32 Index) const { return PilotNameTexts.IsValidIndex(Index) ? PilotNameTexts[Index]->GetText() : FText::GetEmpty(); }
void UMSBriefingWidget::HandlePreviousPilots() { --PilotPage; RefreshPilotCards(); }
void UMSBriefingWidget::HandleNextPilots() { ++PilotPage; RefreshPilotCards(); }

void UMSBriefingWidget::SelectTeam(const int32 TeamIndex)
{
	SelectedTeamIndex = FMath::Clamp(TeamIndex, 0, 1); PilotPage = 0;
	if (SelectedTeamText) SelectedTeamText->SetText(FText::FromString(SelectedTeamIndex == 0 ? TEXT("REBELLION PILOTS") : TEXT("ORDER PILOTS")));
	RefreshPilotCards();
}

void UMSBriefingWidget::Open()
{
	if (UGameInstance* GI = GetGameInstance())
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			Bgm->PlayScene(EMSBgmScene::Waiting);
	if (!IsInViewport() && GetWorld()) AddToViewport(45);
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance
		? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	const bool bOnlineMode = Online && Online->IsOnlineMode();
	// The local 60 second presentation belongs to offline briefing only. Online
	// clients and the home server read the match state instead; the authoritative
	// first-deployment/server timer remains outside this widget.
	RemainingSeconds = bOnlineMode ? 0.0f : BriefingDurationSeconds;
	bTransitionStarted = false;
	if (bOnlineMode)
	{
		if (const AMSPlayerController* OnlineController = Cast<AMSPlayerController>(GetOwningPlayer());
			OnlineController && OnlineController->IsOnlineAuthenticated())
		{
			const FGenericTeamId AssignedTeam = OnlineController->GetOnlineAssignedTeamId();
			if (AssignedTeam == MSTeam::EnemyTeamId()) SelectTeam(1);
			else if (AssignedTeam == MSTeam::PlayerTeamId()) SelectTeam(0);
		}
	}
	if (MiniMap) MiniMap->ShowEmbedded();
	RefreshCountdownText(); RefreshRoomInformation(); RefreshPilotCards();
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
		Lock->PushScreen(TEXT("Briefing"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
	// 出撃前ブリーフィング表示時にナビゲーターの案内ボイスを再生する。
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSNavigatorSubsystem* Navigator = GI->GetSubsystem<UMSNavigatorSubsystem>())
		{
			Navigator->PlayLine(EMSNavigatorTrigger::PreDeployment);
		}
	}
}

void UMSBriefingWidget::Close()
{
	if (!IsOpen()) return;
	if (LobbyWidget)
	{
		UMSUITransitionSubsystem::Transition(this, LobbyWidget,
			FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseInternal(true); }));
	}
	else CloseInternal(true);
}

FReply UMSBriefingWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSBriefingWidget::CloseForBattleEnd()
{
	CloseInternal(false);
}

void UMSBriefingWidget::CloseInternal(const bool bReturnToLobby)
{
	if (!IsOpen() && !bEquipmentOpen) return;
	if(EquipmentWidget && bEquipmentOpen)
	{
		EquipmentWidget->SetReturnBriefingWidget(nullptr);
		EquipmentWidget->Close();
		if(EquipmentWidget->IsOpen()) return;
	}
	bEquipmentOpen=false;
	SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance()) if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("Briefing"));
	if (bReturnToLobby && GetWorld() && GetWorld()->GetNetMode() != NM_Standalone && GetGameInstance())
	{
		if (auto* Rooms = GetGameInstance()->GetSubsystem<UMSRoomSubsystem>()) Rooms->LeaveRoom();
	}
	else if (bReturnToLobby && LobbyWidget) LobbyWidget->Open();
	else if (GetWorld()) UGameplayStatics::SetGamePaused(this, false);
}

void UMSBriefingWidget::AdvanceCountdown(const float DeltaTime)
{
	if ((!IsOpen() && !bEquipmentOpen) || bTransitionStarted || DeltaTime <= 0.0f) return;
	RemainingSeconds = FMath::Max(0.0f, RemainingSeconds - DeltaTime); RefreshCountdownText();
	if (SortieWidget && SortieWidget->IsOpen()) SortieWidget->RefreshBriefingCountdown();
}

void UMSBriefingWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// UUserWidget is ticked by Slate; this delta continues while UWorld is paused.
	AdvanceCountdown(InDeltaTime);
	if (IsOpen())
	{
		RosterRefreshElapsed += InDeltaTime;
		if (RosterRefreshElapsed >= 1.0f) { RosterRefreshElapsed = 0; RefreshPilotCards(); RefreshRoomInformation(); }
	}
}

bool UMSBriefingWidget::EnsureSortieWidget()
{
	if (SortieWidget) return true;
	APlayerController* PC = GetOwningPlayer(); if (!PC) return false;
	TArray<UUserWidget*> Existing; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSSortieWidget::StaticClass(), false);
	SortieWidget = Existing.IsEmpty() ? CreateWidget<UMSSortieWidget>(PC) : Cast<UMSSortieWidget>(Existing[0]);
	return SortieWidget != nullptr;
}

bool UMSBriefingWidget::EnsureSpawnSelectWidget()
{
	if (SpawnSelectWidget) return true;
	APlayerController* PC = GetOwningPlayer(); if (!PC) return false;
	TArray<UUserWidget*> Existing; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSSpawnSelectWidget::StaticClass(), false);
	SpawnSelectWidget = Existing.IsEmpty() ? CreateWidget<UMSSpawnSelectWidget>(PC) : Cast<UMSSpawnSelectWidget>(Existing[0]);
	return SpawnSelectWidget != nullptr;
}

bool UMSBriefingWidget::StartOperation()
{
	if (bEquipmentOpen || bTransitionStarted || !EnsureSpawnSelectWidget()) return false;
	const float ContinuedRemainingSeconds = RemainingSeconds;
	bTransitionStarted = true;
	const bool bAccepted = UMSUITransitionSubsystem::Transition(this, SpawnSelectWidget,
		FSimpleDelegate::CreateWeakLambda(this, [this, ContinuedRemainingSeconds]()
		{
			CloseInternal(false);
			SpawnSelectWidget->SetMissionRuleName(SelectedBattleRuleName);
			SpawnSelectWidget->OpenPreOperation(ContinuedRemainingSeconds);
		}));
	if (!bAccepted) bTransitionStarted = false;
	return bAccepted;
}

void UMSBriefingWidget::HandleStartClicked() { StartOperation(); }
void UMSBriefingWidget::HandleSuitConfirmationClicked()
{
	if (!EnsureSortieWidget()) return;
	SortieWidget->SetBriefingWidget(this);
	SortieWidget->Open();
}

void UMSBriefingWidget::HandleEquipmentClicked()
{
	if(bTransitionStarted || bEquipmentOpen || !GetOwningPlayer() || !GetGameInstance()) return;
	UMSDeckSubsystem* Deck=GetGameInstance()->GetSubsystem<UMSDeckSubsystem>();
	if(!Deck) return;
	const UMSOnlineAccountSubsystem* Online=GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
	const bool bOnline=Online && Online->IsOnlineMode();
	const int32 EquipmentSlotIndex=bOnline ? Deck->GetDeploymentSelectedSlotIndex() : Deck->GetSelectedSlotIndex();
	if(!EquipmentWidget) EquipmentWidget=CreateWidget<UMSHangarWidget>(GetOwningPlayer());
	if(!EquipmentWidget) return;
	EquipmentWidget->SetReturnBriefingWidget(this);
	if(bOnline)
	{
		const AMSPlayerController* PC=Cast<AMSPlayerController>(GetOwningPlayer());
		UMSMobileSuitData* Suit=nullptr;FMSDeckLoadout Loadout;
		if(!PC || !PC->IsOnlineAuthenticated() || !PC->GetOnlineDeck().Slots.IsValidIndex(EquipmentSlotIndex) ||
			!MSOnline::ResolveSlot(PC->GetOnlineDeck().Slots[EquipmentSlotIndex],Suit,Loadout,PC->GetOnlineDeck().SchemaVersion == 1) || !Suit) return;
		EquipmentWidget->OpenReadOnlyLoadout(Suit,Loadout);
	}
	else EquipmentWidget->OpenForDeckSlot(EquipmentSlotIndex);
	if(!EquipmentWidget->IsOpen()) return;
	bEquipmentOpen=true;SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMSBriefingWidget::HandleEquipmentClosed()
{
	if(!bEquipmentOpen) return;
	bEquipmentOpen=false;
	// Reopening must not restart the countdown, reset the side/page, or re-admit.
	RefreshCountdownText();RefreshRoomInformation();RefreshPilotCards();
	SetVisibility(ESlateVisibility::Visible);SetKeyboardFocus();
}
void UMSBriefingWidget::HandleTeamASelected() { SelectTeam(0); }
void UMSBriefingWidget::HandleTeamBSelected() { SelectTeam(1); }
void UMSBriefingWidget::HandleBackClicked() { Close(); }

void UMSBriefingWidget::NativeDestruct()
{
	if (IsOpen()) CloseForBattleEnd();
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSBriefingWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSBriefingWidget::AdvanceCountdownForAutomationTests(const float DeltaTime) { AdvanceCountdown(DeltaTime); }
#endif
