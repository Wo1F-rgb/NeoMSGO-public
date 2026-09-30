// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSResultWidget.h"
#include "Online/MSRoomSubsystem.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSMobileSuitData.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "Voice/MSNavigatorSubsystem.h"

namespace MSResult
{
	const FLinearColor Scrim(0.004f, 0.008f, 0.014f, 0.82f);
	const FLinearColor Panel(0.012f, 0.055f, 0.07f, 0.98f);
	const FLinearColor Accent(0.08f, 0.9f, 0.88f, 1.0f);
	const FLinearColor Purple(0.38f, 0.015f, 0.58f, 0.98f);
	const FLinearColor TableHeader(0.035f, 0.25f, 0.18f, 1.0f);
	const FLinearColor TableRow(0.02f, 0.13f, 0.105f, 0.94f);
	const FLinearColor PlayerRow(0.08f, 0.38f, 0.27f, 1.0f);
	const FLinearColor Win(1.0f, 0.58f, 0.08f, 1.0f);
	const FLinearColor Lose(0.14f, 0.92f, 1.0f, 1.0f);

	FString UnitDisplayName(const AMSUnitBase* Unit)
	{
		if (!Unit) return FString();
		if (const UMSMobileSuitData* Data = Unit->GetMobileSuitData(); Data && !Data->UnitName.IsEmpty())
			return Data->UnitName.ToString();
		return Unit->GetName();
	}

	FString PlayerDisplayName(const APlayerState* PlayerState)
	{
		if (!PlayerState)
		{
			return FText::FromString(TEXT("未設定")).ToString();
		}
		if (const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState);
			MSPlayerState && !MSPlayerState->GetLoginUsername().IsEmpty())
		{
			return MSPlayerState->GetLoginUsername();
		}
		const FString PlayerName = PlayerState->GetPlayerName();
		return PlayerName.IsEmpty() ? FString(TEXT("未設定")) : PlayerName;
	}

	/**
	 * A server PlayerState exists before the online authentication handshake is
	 * complete.  It is presence, not a battle participant, until the verified
	 * login name has been published.  Offline/custom sessions still use the
	 * regular APlayerState name fallback.
	 */
	bool IsBattleParticipant(const APlayerState* PlayerState)
	{
		if (!PlayerState) return false;
		if (const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState))
		{
			return !MSPlayerState->GetLoginUsername().IsEmpty();
		}
		return !PlayerState->GetPlayerName().IsEmpty();
	}

	const APlayerState* GetViewerPlayerState(const UMSResultWidget* Widget)
	{
		const APlayerController* OwnerController = Widget ? Widget->GetOwningPlayer() : nullptr;
		if (!OwnerController)
		{
			return nullptr;
		}

		if (const APlayerState* PlayerState = OwnerController->GetPlayerState<APlayerState>())
		{
			return PlayerState;
		}

		// During seamless travel, the controller can briefly expose the pawn's
		// state before its own PlayerState pointer is refreshed.
		const APawn* OwnedPawn = OwnerController->GetPawn();
		return OwnedPawn ? OwnedPawn->GetPlayerState<APlayerState>() : nullptr;
	}

	void SetFillWeight(UHorizontalBoxSlot* Slot, const float Weight)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Weight;
		Slot->SetSize(Size);
	}

	/** The result state stores the battle outcome from TeamPlayer's perspective. */
	bool IsEnemyPerspective(const UMSResultWidget* Widget)
	{
		const AMSPlayerState* OwnedPlayerState = Cast<AMSPlayerState>(GetViewerPlayerState(Widget));
		// Missing/invalid faction data keeps the historical TeamPlayer perspective.
		return OwnedPlayerState && OwnedPlayerState->GetSelectedFaction() == MSTeam::EnemyTeamId();
	}
}

void UMSResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildReferenceLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSResultWidget::BuildReferenceLayout()
{
	UBorder* Scrim = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Scrim, MSUIStyle::EBrush::Scrim, MSResult::Scrim);
	Scrim->SetPadding(FMargin(48.0f, 30.0f));
	WidgetTree->RootWidget = Scrim;

	UOverlay* Stage = WidgetTree->ConstructWidget<UOverlay>();
	Scrim->SetContent(Stage);
	USizeBox* FrameSize = WidgetTree->ConstructWidget<USizeBox>();
	FrameSize->SetWidthOverride(940.0f);
	FrameSize->SetHeightOverride(560.0f);
	UOverlaySlot* FrameStageSlot = Stage->AddChildToOverlay(FrameSize);
	FrameStageSlot->SetHorizontalAlignment(HAlign_Center);
	FrameStageSlot->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* FrameContent = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* InnerFrame = nullptr;
	FrameSize->SetContent(MSUIStyle::MakeChamferedFrame(WidgetTree, InnerFrame,
		MSUIStyle::EChamferCorner::All, FrameContent, 18.0f));
	if (InnerFrame)
	{
		MSUIStyle::ApplyBrush(InnerFrame, MSUIStyle::EBrush::LobbyPanel, MSResult::Panel);
		InnerFrame->SetPadding(FMargin(12.0f, 8.0f));
	}

	UBorder* EndBand = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(EndBand, MSUIStyle::EBrush::PanelAttack, MSResult::Purple);
	EndBand->SetPadding(FMargin(8.0f, 3.0f));
	UTextBlock* EndLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("戦闘終了")),
		MSUIStyle::ETextRole::H2, true);
	EndLabel->SetJustification(ETextJustify::Center);
	EndBand->SetContent(EndLabel);
	FrameContent->AddChildToVerticalBox(EndBand)->SetPadding(FMargin(190.0f, 0.0f, 190.0f, 5.0f));

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	FrameContent->AddChildToVerticalBox(Header)->SetPadding(FMargin(4.0f, 0.0f, 4.0f, 6.0f));
	MapNameText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption,
		false, MSUIStyleColor::TextSecondary, MSUIStyle::ENumericLargeSize::Standard, TEXT("ResultMapNameText"));
	MSResult::SetFillWeight(Header->AddChildToHorizontalBox(MapNameText), 1.2f);
	GaugeText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Body,
		true, MSResult::Accent, MSUIStyle::ENumericLargeSize::Standard, TEXT("ResultGaugeText"));
	GaugeText->SetJustification(ETextJustify::Center);
	MSResult::SetFillWeight(Header->AddChildToHorizontalBox(GaugeText), 1.35f);
	DurationText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Body,
		true, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("ResultDurationText"));
	DurationText->SetJustification(ETextJustify::Right);
	MSResult::SetFillWeight(Header->AddChildToHorizontalBox(DurationText), 0.75f);
	OutcomeText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Display,
		true, MSResult::Win, MSUIStyle::ENumericLargeSize::Standard, TEXT("ResultOutcomeText"));
	OutcomeText->SetJustification(ETextJustify::Right);
	MSResult::SetFillWeight(Header->AddChildToHorizontalBox(OutcomeText), 0.65f);

	UHorizontalBox* MainColumns = WidgetTree->ConstructWidget<UHorizontalBox>();
	FrameContent->AddChildToVerticalBox(MainColumns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PilotCardsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResultPilotCards"));
	UHorizontalBoxSlot* PilotSlot = MainColumns->AddChildToHorizontalBox(PilotCardsBox);
	MSResult::SetFillWeight(PilotSlot, 0.62f);
	PilotSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));

	UVerticalBox* Ranking = WidgetTree->ConstructWidget<UVerticalBox>();
	MSResult::SetFillWeight(MainColumns->AddChildToHorizontalBox(Ranking), 1.55f);
	auto AddRankingCells = [this](UHorizontalBox* Row, const TCHAR* const Cells[7], const bool bHeader)
	{
		const float Weights[7] = { 0.62f, 1.45f, 1.0f, 0.72f, 0.72f, 0.72f, 0.65f };
		for (int32 Column = 0; Column < 7; ++Column)
		{
			UTextBlock* Cell = MSUIStyle::MakeText(WidgetTree, FText::FromString(Cells[Column]),
				bHeader ? MSUIStyle::ETextRole::Micro : MSUIStyle::ETextRole::Caption,
				bHeader || Column == 2, bHeader ? MSResult::Accent : MSUIStyleColor::TextPrimary);
			Cell->SetAutoWrapText(false);
			Cell->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			Cell->SetJustification(Column == 1 ? ETextJustify::Left : ETextJustify::Center);
			MSResult::SetFillWeight(Row->AddChildToHorizontalBox(Cell), Weights[Column]);
		}
	};
	UBorder* HeaderBorder = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(HeaderBorder, MSUIStyle::EBrush::ListRowSelected, MSResult::TableHeader);
	HeaderBorder->SetPadding(FMargin(4.0f, 3.0f));
	UHorizontalBox* TableHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	HeaderBorder->SetContent(TableHeaderRow);
	const TCHAR* HeaderCells[7] = { TEXT("順位"), TEXT("名前"), TEXT("合計スコア"), TEXT("攻撃"), TEXT("支援"), TEXT("貢献"), TEXT("撃破数") };
	AddRankingCells(TableHeaderRow, HeaderCells, true);
	Ranking->AddChildToVerticalBox(HeaderBorder);
	RankingRowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResultRankingRows"));
	Ranking->AddChildToVerticalBox(RankingRowsBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	FrameContent->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.0f, 6.0f, 4.0f, 0.0f));
	UTextBlock* Hint = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("Escでもロビーへ戻れます")),
		MSUIStyle::ETextRole::Micro, false, MSUIStyleColor::TextSecondary);
	Footer->AddChildToHorizontalBox(Hint)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* NextLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ロビーへ戻る")), MSUIStyle::ETextRole::H2, true,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	NextLabel->SetJustification(ETextJustify::Center);
	NextButton = MSUIStyle::MakeCapsuleButton(WidgetTree, NextLabel, FLinearColor(0.16f, 0.22f, 0.48f, 1.0f), TEXT("ResultNextButton"));
	NextButton->OnClicked.AddDynamic(this, &UMSResultWidget::HandleNextClicked);
	UHorizontalBoxSlot* NextSlot = Footer->AddChildToHorizontalBox(NextButton);
	NextSlot->SetPadding(FMargin(8.0f, 0.0f));
	NextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	NextButton->SetRenderScale(FVector2D(1.35f, 1.0f));
}

TArray<FString> UMSResultWidget::BuildRankingNames(const UMSUnitRegistrySubsystem* Registry)
{
	TArray<FString> PlayerNames;
	TArray<FString> OtherNames;
	if (!Registry) return PlayerNames;
	Registry->ForEachRegisteredUnit([&PlayerNames, &OtherNames](AMSUnitBase* Unit)
	{
		if (!Unit) return;
		(Unit->IsA<AMSPlayerCharacter>() ? PlayerNames : OtherNames).Add(MSResult::UnitDisplayName(Unit));
	});
	PlayerNames.Append(OtherNames);
	// Kept for the legacy public name-order helper.  RefreshResultData never uses
	// a fabricated placeholder; its visible rows come from GameState::PlayerArray.
	if (PlayerNames.Num() > 16) PlayerNames.SetNum(16);
	return PlayerNames;
}

void UMSResultWidget::RefreshResultData()
{
	const AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
	const bool bEnemyPerspective = MSResult::IsEnemyPerspective(this);
	const APlayerState* ViewerPlayerState = MSResult::GetViewerPlayerState(this);
	const bool bViewerResultKnown = State && ViewerPlayerState;
	const bool bViewerWon = bViewerResultKnown && (bEnemyPerspective ? !State->bPlayerWon : State->bPlayerWon);
	const FText MapName = State ? State->MapDisplayName : FText::FromString(TEXT("未設定"));
	if (MapNameText)
	{
		MapNameText->SetText(FText::Format(FText::FromString(TEXT("大規模戦　{0}")), MapName));
	}
	if (OutcomeText)
	{
		if (bViewerResultKnown)
		{
			OutcomeText->SetText(FText::FromString(bViewerWon ? TEXT("WIN") : TEXT("LOSE")));
			OutcomeText->SetColorAndOpacity(bViewerWon ? MSResult::Win : MSResult::Lose);
		}
		else
		{
			OutcomeText->SetText(FText::FromString(TEXT("未確定")));
			OutcomeText->SetColorAndOpacity(MSUIStyleColor::TextSecondary);
		}
	}
	const float MaxGauge = State ? State->MaxGauge : 0.0f;
	const float PlayerGauge = State ? (bEnemyPerspective ? State->EnemyGauge : State->PlayerGauge) : 0.0f;
	const float EnemyGauge = State ? (bEnemyPerspective ? State->PlayerGauge : State->EnemyGauge) : 0.0f;
	const float PlayerPercent = MaxGauge > UE_SMALL_NUMBER ? 100.0f * PlayerGauge / MaxGauge : 0.0f;
	const float EnemyPercent = MaxGauge > UE_SMALL_NUMBER ? 100.0f * EnemyGauge / MaxGauge : 0.0f;
	if (GaugeText)
	{
		GaugeText->SetText(bViewerResultKnown
			? FText::FromString(FString::Printf(TEXT("自軍 %.2f%%   /   敵軍 %.2f%%"), PlayerPercent, EnemyPercent))
			: FText::FromString(TEXT("自軍 未集計   /   敵軍 未集計")));
	}
	if (DurationText)
	{
		if (State)
		{
			const int32 ElapsedSeconds = FMath::Max(0, FMath::FloorToInt(State->MatchDuration - State->RemainingTime));
			DurationText->SetText(FText::FromString(FString::Printf(TEXT("戦闘時間  %02d'%02d"), ElapsedSeconds / 60, ElapsedSeconds % 60)));
		}
		else
		{
			DurationText->SetText(FText::FromString(TEXT("戦闘時間 未集計")));
		}
	}
	TArray<const APlayerState*> PlayerStates;
	if (State)
	{
		for (const APlayerState* PlayerState : State->PlayerArray)
		{
			if (IsValid(PlayerState) && MSResult::IsBattleParticipant(PlayerState))
			{
				PlayerStates.Add(PlayerState);
			}
		}
	}
	RebuildRankingRows(PlayerStates, ViewerPlayerState);
}

void UMSResultWidget::RebuildRankingRows(const TArray<const APlayerState*>& PlayerStates,
	const APlayerState* ViewerPlayerState)
{
	if (!RankingRowsBox || !PilotCardsBox) return;
	RankingRowsBox->ClearChildren();
	PilotCardsBox->ClearChildren();

	auto AddRankingRow = [this](const FString& RankText, const FString& NameText, const FString& TotalText,
		const bool bPrimary)
	{
		const FString Unavailable = TEXT("未集計");
		const FString Cells[7] = { RankText, NameText, TotalText, Unavailable, Unavailable, Unavailable, Unavailable };
		const FName RowName = bPrimary ? FName(TEXT("ResultPlayerRow")) : NAME_None;
		UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), RowName);
		MSUIStyle::ApplyBrush(RowBorder, bPrimary ? MSUIStyle::EBrush::ListRowSelected : MSUIStyle::EBrush::ListRow, bPrimary ? MSResult::PlayerRow : MSResult::TableRow);
		RowBorder->SetPadding(FMargin(4.0f, 2.0f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		RowBorder->SetContent(Row);
		const float Weights[7] = { 0.62f, 1.45f, 1.0f, 0.72f, 0.72f, 0.72f, 0.65f };
		for (int32 Column = 0; Column < 7; ++Column)
		{
			UTextBlock* Cell = MSUIStyle::MakeText(WidgetTree, FText::FromString(Cells[Column]), MSUIStyle::ETextRole::Caption,
				Column == 2, bPrimary ? FLinearColor(0.94f, 1.0f, 0.7f) : MSUIStyleColor::TextPrimary);
			Cell->SetAutoWrapText(false);
			Cell->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			Cell->SetToolTipText(FText::FromString(Cells[Column]));
			Cell->SetJustification(Column == 1 ? ETextJustify::Left : ETextJustify::Center);
			MSResult::SetFillWeight(Row->AddChildToHorizontalBox(Cell), Weights[Column]);
		}
		RankingRowsBox->AddChildToVerticalBox(RowBorder);
	};

	for (int32 Index = 0; Index < PlayerStates.Num(); ++Index)
	{
		const APlayerState* PlayerState = PlayerStates[Index];
		const FString Name = MSResult::PlayerDisplayName(PlayerState);
		const int32 Total = PlayerState ? FMath::RoundToInt(PlayerState->GetScore()) : 0;
		const FString TotalText = FString::FromInt(Total);
		const bool bViewerRow = PlayerState == ViewerPlayerState;
		AddRankingRow(TEXT("未集計"), Name, TotalText, bViewerRow);

		if (Index < 5)
		{
			UBorder* PilotCard = WidgetTree->ConstructWidget<UBorder>();
			MSUIStyle::ApplyBrush(PilotCard, bViewerRow ? MSUIStyle::EBrush::ListRowSelected : MSUIStyle::EBrush::ListRow, bViewerRow ? MSResult::PlayerRow : FLinearColor(0.045f, 0.18f, 0.15f, 1.0f));
			PilotCard->SetPadding(FMargin(10.0f, 9.0f));
			PilotCard->SetContent(MSUIStyle::MakeText(WidgetTree,
				FText::FromString(FString::Printf(TEXT("%s\nSCORE %s"), *Name, *TotalText)),
				MSUIStyle::ETextRole::Caption, bViewerRow, MSUIStyleColor::TextPrimary));
			PilotCardsBox->AddChildToVerticalBox(PilotCard)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			PilotCardsBox->GetChildAt(Index)->SetRenderScale(FVector2D(1.0f, 0.97f));
		}
	}
	if (PlayerStates.IsEmpty())
	{
		AddRankingRow(TEXT("—"), TEXT("参加者データなし"), TEXT("未集計"), false);
	}
}

void UMSResultWidget::Open()
{
	if (IsOpen()) return;
	if (UGameInstance* GI = GetGameInstance())
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			Bgm->PlayScene(EMSBgmScene::Result);
	if (GetWorld() && !IsInViewport()) AddToViewport(60);
	RefreshResultData();
	SetVisibility(ESlateVisibility::Visible);
	SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (UGameInstance* GI = GetGameInstance())
		if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
			Lock->PushScreen(TEXT("Result"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
	// 勝敗に応じてナビゲーターの帰還ボイス(勝利/敗北)を再生する。
		if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSNavigatorSubsystem* Navigator = GI->GetSubsystem<UMSNavigatorSubsystem>())
		{
			const AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
			const APlayerState* ViewerPlayerState = MSResult::GetViewerPlayerState(this);
			if (State && ViewerPlayerState)
			{
				const bool bWon = MSResult::IsEnemyPerspective(this) ? !State->bPlayerWon : State->bPlayerWon;
				Navigator->PlayLine(bWon ? EMSNavigatorTrigger::VictoryReturn : EMSNavigatorTrigger::DefeatReturn);
			}
		}
	}
}

void UMSResultWidget::Close()
{
	if (!IsOpen()) return;
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone && GetGameInstance())
	{
		if (auto* Rooms = GetGameInstance()->GetSubsystem<UMSRoomSubsystem>()) { CloseImmediate(); Rooms->LeaveRoom(); return; }
	}
	if (LobbyWidget)
	{
		UMSUITransitionSubsystem::Transition(this, LobbyWidget,
			FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseImmediate(); LobbyWidget->Open(); }));
		return;
	}
	CloseImmediate();
}

FReply UMSResultWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSResultWidget::CloseImmediate()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (UGameInstance* GI = GetGameInstance())
		if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(TEXT("Result"));
	if (GetWorld()) UGameplayStatics::SetGamePaused(this, false);
}

void UMSResultWidget::SetLobbyWidget(UMSLobbyWidget* InLobbyWidget)
{
	LobbyWidget = InLobbyWidget;
}

void UMSResultWidget::HandleNextClicked()
{
	Close();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSResultWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
#endif
