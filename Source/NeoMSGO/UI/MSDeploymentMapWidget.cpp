#include "UI/MSDeploymentMapWidget.h"

#include "Audio/MSBgmSubsystem.h"
#include "AI/MSBotSpawnPoint.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSTeamStatics.h"
#include "Data/MSMobileSuitData.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSPlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSRespawnComponent.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSMapTypes.h"
#include "UI/MSMapCaptureSubsystem.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"

namespace MSDeployment
{
	const FVector2D MapSize(960.0f, 480.0f);
	constexpr float SpawnMarkerSizePx = 26.0f;
	const FLinearColor Frame(0.91f, 0.93f, 0.91f, 0.98f);
	const FLinearColor Panel(0.015f, 0.11f, 0.13f, 0.98f);
	const FLinearColor Card(0.025f, 0.18f, 0.19f, 1.0f);
	const FLinearColor Selected(0.78f, 0.59f, 0.12f, 1.0f);
	const FLinearColor Accent(0.25f, 0.95f, 0.88f, 1.0f);

	FSlateBrush SolidBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush; Brush.DrawAs = ESlateBrushDrawType::Box; Brush.TintColor = Color; return Brush;
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Value, const float Size,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		const MSUIStyle::ETextRole Role = Size >= 24.0f ? MSUIStyle::ETextRole::H1
			: Size >= 18.0f ? MSUIStyle::ETextRole::H2
			: Size >= 14.0f ? MSUIStyle::ETextRole::Body : MSUIStyle::ETextRole::Caption;
		UTextBlock* Text = MSUIStyle::MakeText(Tree, FText::FromString(Value), Role, false, Color,
			MSUIStyle::ENumericLargeSize::Standard, Name);
		FSlateFontInfo Font = Text->GetFont(); Font.Size = FMath::RoundToInt(Size * 0.75f); Text->SetFont(Font);
		return Text;
	}

	void Place(UCanvasPanel* Parent, UWidget* Child, const FVector2D& Position, const FVector2D& Size,
		const int32 ZOrder = 0, const FVector2D& Alignment = FVector2D::ZeroVector)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child); Slot->SetAnchors(FAnchors(0.0f));
		Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetSize(Size); Slot->SetZOrder(ZOrder);
	}
}

FVector2D UMSDeploymentMapWidget::GetMapSize() const
{
	return MSDeployment::MapSize;
}

void UMSDeploymentMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildReferenceFrame();
	BindDeckSubsystem();
	WarningText = MSDeployment::MakeText(WidgetTree, TEXT("出撃不可: 自軍拠点または出撃地点を選択してください"),
		18.0f, MSMapPalette::Warning, TEXT("DeploymentWarningText"));
	WarningText->SetJustification(ETextJustify::Center);
	MSDeployment::Place(RootCanvas, WarningText, FVector2D(480, 842), FVector2D(960, 32), 5);
	WarningText->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSDeploymentMapWidget::BuildReferenceFrame()
{
	// Video 3 at 134s: a wide modal over the selected suit, not a full-height map.
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(); Fit->SetStretch(EStretch::ScaleToFit);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(1920); Design->SetHeightOverride(1080);
	Design->SetContent(RootCanvas); Fit->SetContent(Design);
	UBorder* Letterbox = WidgetTree->ConstructWidget<UBorder>(); Letterbox->SetPadding(FMargin(0));
	Letterbox->SetBrushColor(FLinearColor::Black); Letterbox->SetContent(Fit); WidgetTree->RootWidget = Letterbox;

	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(); Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	MSDeployment::Place(RootCanvas, Backdrop, FVector2D::ZeroVector, FVector2D(1920, 1080), -5);
	if (GetWorld())
	{
		BackdropRenderer = NewObject<UMSMobileSuitPreviewRenderer>(this);
		BackdropRenderer->Initialize(GetWorld(), Backdrop); BackdropRenderer->ConfigureSortieView(false);
	}

	if (UCanvasPanelSlot* MapSlot = Cast<UCanvasPanelSlot>(MapCanvas->Slot))
	{
		MapSlot->SetAnchors(FAnchors(0)); MapSlot->SetAlignment(FVector2D::ZeroVector);
		MapSlot->SetPosition(FVector2D(340, 327)); MapSlot->SetSize(GetMapSize()); MapSlot->SetZOrder(1);
	}
	UBorder* ModalFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeploymentModalFrame"));
	ModalFrame->SetBrushColor(MSDeployment::Frame); ModalFrame->SetPadding(FMargin(4, 54, 4, 5));
	MSDeployment::Place(RootCanvas, ModalFrame, FVector2D(313, 252), FVector2D(1292, 578), -2);
	UBorder* ModalInner = WidgetTree->ConstructWidget<UBorder>();
	ModalInner->SetBrushColor(FLinearColor(0.002f, 0.018f, 0.018f)); ModalFrame->SetContent(ModalInner);
	UTextBlock* Title = MSDeployment::MakeText(WidgetTree, TEXT("出撃地点選択"), 24, FLinearColor(0.23f, 0.25f, 0.25f), TEXT("DeploymentTitleText"));
	Title->SetJustification(ETextJustify::Center); MSDeployment::Place(RootCanvas, Title, FVector2D(600, 256), FVector2D(720, 30), 4);
	UBorder* MissionBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeploymentMissionBand"));
	MissionBand->SetBrushColor(FLinearColor(0.22f, 0.25f, 0.25f)); MissionBand->SetPadding(FMargin(4, 0));
	MissionRuleText = MSDeployment::MakeText(WidgetTree, TEXT("戦術目標制圧作戦"), 15, FLinearColor::White, TEXT("DeploymentMissionRuleText"));
	MissionRuleText->SetJustification(ETextJustify::Center); MissionBand->SetContent(MissionRuleText);
	MSDeployment::Place(RootCanvas, MissionBand, FVector2D(532, 290), FVector2D(856, 20), 4);

	UBorder* ListFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeploymentMSListFrame"));
	ListFrame->SetBrushColor(MSDeployment::Panel); ListFrame->SetPadding(FMargin(9, 26, 9, 0));
	MSDeployment::Place(RootCanvas, ListFrame, FVector2D(1334, 311), FVector2D(172, 512), 3);
	UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>(); ListFrame->SetContent(List);
	UTextBlock* ListTitle = MSDeployment::MakeText(WidgetTree, TEXT("MS LIST"), 16, MSDeployment::Accent, TEXT("DeploymentMSListTitle"));
	ListTitle->SetJustification(ETextJustify::Center); List->AddChildToVerticalBox(ListTitle)->SetPadding(FMargin(0, 0, 0, 20));
	RenderedPresets.SetNum(4);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(); Card->SetBrushColor(MSDeployment::Accent); Card->SetPadding(FMargin(2));
		UBorder* Inner = WidgetTree->ConstructWidget<UBorder>(); Inner->SetBrushColor(MSDeployment::Card); Inner->SetPadding(FMargin(3));
		Inner->SetContent(Content); Card->SetContent(Inner);
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("DeploymentMSListSlot%dButton"), Index));
		MSUIStyle::ApplyContentButtonStyle(Button); Button->SetContent(Card); MSUIStyle::StretchButtonContent(Button);
		UTextBlock* SlotLabel = MSDeployment::MakeText(WidgetTree, *FString::Printf(TEXT("0%d"), Index + 1), 13, MSDeployment::Accent);
		SlotLabel->SetJustification(ETextJustify::Center); Content->AddChildToVerticalBox(SlotLabel);
		UTextBlock* Name = MSDeployment::MakeText(WidgetTree, TEXT("空き"), 14, FLinearColor::White, *FString::Printf(TEXT("DeploymentMSListSlot%dName"), Index));
		Name->SetJustification(ETextJustify::Center);
		USizeBox* NameBox = WidgetTree->ConstructWidget<USizeBox>(); NameBox->SetHeightOverride(36); NameBox->SetContent(Name);
		Content->AddChildToVerticalBox(NameBox);
		UImage* Portrait = WidgetTree->ConstructWidget<UImage>(); Portrait->SetDesiredSizeOverride(FVector2D(140, 32));
		Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
		UScaleBox* PortraitFit = WidgetTree->ConstructWidget<UScaleBox>(); PortraitFit->SetStretch(EStretch::ScaleToFit); PortraitFit->SetContent(Portrait);
		UOverlay* PortraitOverlay = WidgetTree->ConstructWidget<UOverlay>();
		UOverlaySlot* ImageSlot = PortraitOverlay->AddChildToOverlay(PortraitFit); ImageSlot->SetHorizontalAlignment(HAlign_Fill); ImageSlot->SetVerticalAlignment(VAlign_Fill);
		UTextBlock* Respawn = MSDeployment::MakeText(WidgetTree, TEXT(""), 13, MSDeployment::Accent, *FString::Printf(TEXT("DeploymentMSListSlot%dRespawn"), Index));
		Respawn->SetJustification(ETextJustify::Center);
		UOverlaySlot* CounterSlot = PortraitOverlay->AddChildToOverlay(Respawn); CounterSlot->SetVerticalAlignment(VAlign_Center); CounterSlot->SetHorizontalAlignment(HAlign_Fill);
		// Reserve a complete two-line row for the respawn counter.  The previous
		// 22px slot was shorter than the rendered two-line respawn text, so the
		// second line could be clipped at the bottom of the card.
		USizeBox* PortraitBox = WidgetTree->ConstructWidget<USizeBox>(); PortraitBox->SetHeightOverride(32); PortraitBox->SetContent(PortraitOverlay);
		Content->AddChildToVerticalBox(PortraitBox);
		UMSMobileSuitPreviewRenderer* Renderer = nullptr;
		if (GetWorld())
		{
			Renderer = NewObject<UMSMobileSuitPreviewRenderer>(this); Renderer->Initialize(GetWorld(), Portrait); Renderer->ConfigureDeploymentListView();
		}
		// 13px slot label + 36px two-line name + 32px preview/counter, with the
		// border padding, fits inside this fixed card at both reference sizes.
		USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(); CardSize->SetHeightOverride(96); CardSize->SetContent(Button);
		List->AddChildToVerticalBox(CardSize)->SetPadding(FMargin(0, 0, 0, 10));
		MSListButtons.Add(Button); MSListBorders.Add(Card); MSListNameTexts.Add(Name); MSListRespawnTexts.Add(Respawn);
		MSListPortraits.Add(Portrait); MSListRenderers.Add(Renderer);
	}
	MSListButtons[0]->OnClicked.AddDynamic(this, &UMSDeploymentMapWidget::HandleMSListSlot0Clicked);
	MSListButtons[1]->OnClicked.AddDynamic(this, &UMSDeploymentMapWidget::HandleMSListSlot1Clicked);
	MSListButtons[2]->OnClicked.AddDynamic(this, &UMSDeploymentMapWidget::HandleMSListSlot2Clicked);
	MSListButtons[3]->OnClicked.AddDynamic(this, &UMSDeploymentMapWidget::HandleMSListSlot3Clicked);

	UTextBlock* ReturnLabel = MSDeployment::MakeText(WidgetTree, TEXT("機体を選択する"), 16);
	ReturnLabel->SetAutoWrapText(false); ReturnLabel->SetJustification(ETextJustify::Center);
	UButton* ReturnButton = MSUIStyle::MakeCapsuleButton(WidgetTree, ReturnLabel, MSDeployment::Panel, TEXT("ReturnToSuitSelectionButton"));
	ReturnButton->OnClicked.AddDynamic(this, &UMSDeploymentMapWidget::HandleReturnToSuitSelectionClicked);
	MSDeployment::Place(RootCanvas, ReturnButton, FVector2D(1288, 214), FVector2D(182, 32), 5);

	SelectedDeployMarker = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SelectedDeployMarker"));
	SelectedDeployMarker->SetBrush(MSDeployment::SolidBrush(FLinearColor(1.0f, 0.65f, 0.05f)));
	SelectedDeployMarker->SetRenderTransformAngle(45);
	UCanvasPanelSlot* MarkerSlot = MapCanvas->AddChildToCanvas(SelectedDeployMarker); MarkerSlot->SetAlignment(FVector2D(0.5f)); MarkerSlot->SetSize(FVector2D(34)); MarkerSlot->SetZOrder(8);
	SelectedDeployMarker->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSDeploymentMapWidget::BindDeckSubsystem()
{
	if (!DeckSubsystem && GetGameInstance()) DeckSubsystem = GetGameInstance()->GetSubsystem<UMSDeckSubsystem>();
	if (DeckSubsystem && SelectedDeckSlotIndex == INDEX_NONE) SelectedDeckSlotIndex = DeckSubsystem->GetDeploymentSelectedSlotIndex();
}

void UMSDeploymentMapWidget::AppendTeamSpawnPointSnapshots(UWorld* World, const FGenericTeamId TeamId,
	TArray<FMSDeployPointSnapshot>& InOutSnapshots, const FVector2D& MapSizePx)
{
	if (!World) return;
	for (TActorIterator<AMSBotSpawnPoint> It(World); It; ++It)
	{
		const AMSBotSpawnPoint* Point = *It;
		if (!IsValid(Point) || !Point->ServesTeam(TeamId)) continue;
		FMSDeployPointSnapshot& Snapshot = InOutSnapshots.AddDefaulted_GetRef();
		const UMSMapCaptureSubsystem* MapSubsystem = World->GetSubsystem<UMSMapCaptureSubsystem>();
		const FVector Center = MapSubsystem ? MapSubsystem->GetMapWorldCenter() : FVector::ZeroVector;
		const FVector2D HalfExtents = MapSubsystem ? MapSubsystem->GetMapWorldHalfExtents() : FVector2D(MSMapPalette::WorldHalfExtentCm);
		Snapshot.MapLocal = WorldToMapLocalRectCentered(Point->GetActorLocation(), Center, HalfExtents, MapSizePx, MSMapPalette::EdgeMarginPx);
		Snapshot.WorldLocation = Point->GetActorLocation(); Snapshot.MarkerSizePx = MSDeployment::SpawnMarkerSizePx;
		Snapshot.bDiamondMarker = true; Snapshot.bCanDeploy = true;
	}
}

FMSMapClickResolution UMSDeploymentMapWidget::ResolveMapClick(const FVector2D& Local,
	const TArray<FMSDeployPointSnapshot>& PointSnapshots, const bool bHasNearestDeployablePoint,
	const float NearestDeployableDistanceCm, const float FreeDeployRangeCm, const float WorldHalfExtentCm,
	const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	FMSMapClickResolution Result;
	const FMSDeployPointSnapshot* Hit = nullptr; float BestDistanceSq = TNumericLimits<float>::Max();
	for (const FMSDeployPointSnapshot& Point : PointSnapshots)
	{
		const float DistanceSq = FVector2D::DistSquared(Local, Point.MapLocal);
		const FMSMapCapturePointInteractionBounds DrawnBounds = UMSMapWidgetBase::GetCapturePointInteractionBounds(
			Point.MapLocal, Point.MarkerSizePx, Point.Label, Point.bDiamondMarker);
		// MapCanvas local coordinates and ConstructCapturePointMarker use the
		// same fixed design space.  A rectangle, rather than a guessed radius,
		// includes the actual label slot and keeps the closest overlapping marker.
		if (DrawnBounds.Contains(Local) && DistanceSq < BestDistanceSq)
		{
			Hit = &Point; BestDistanceSq = DistanceSq;
		}
	}
	if (Hit)
	{
		Result.bDirectPoint = true; Result.bResolved = Hit->bCanDeploy; Result.WorldLocation = Hit->WorldLocation; return Result;
	}
	Result.WorldLocation = MapLocalToWorld(Local, WorldHalfExtentCm, MapSizePx, EdgeMarginPx);
	Result.bResolved = bHasNearestDeployablePoint && NearestDeployableDistanceCm <= FreeDeployRangeCm;
	return Result;
}

void UMSDeploymentMapWidget::Open()
{
	bOnlineDeploymentPending = false;
	if (AMSPlayerController* PC = Cast<AMSPlayerController>(GetOwningPlayer()))
		PC->OnOnlineDeploymentResult.AddUniqueDynamic(this, &ThisClass::HandleOnlineDeploymentResult);
	RenderedPresets.Init(nullptr, 4); BackdropPreset = nullptr;
	const double StartSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map Open begin"));
	ClearSelection();
	BindDeckSubsystem();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map Open before base elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	Super::Open();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map Open after base elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	CacheFriendlySpawnPointMarkers();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map Open after spawn markers elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	RefreshMSList();
	if (MissionRuleName.IsEmpty()) SetMissionRuleName(FText::FromString(TEXT("戦術目標制圧作戦")));
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSUILockSubsystem* UILock = GameInstance->GetSubsystem<UMSUILockSubsystem>())
			UILock->PushScreen(TEXT("DeploymentMap"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { if (bOnlineDeploymentPending) return false; HandleCloseRequested(); return true; }));
	}
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map Open complete elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void UMSDeploymentMapWidget::Close()
{
	bOnlineDeploymentPending = false;
	if (AMSPlayerController* PC = Cast<AMSPlayerController>(GetOwningPlayer()))
		PC->OnOnlineDeploymentResult.RemoveDynamic(this, &ThisClass::HandleOnlineDeploymentResult);
	Super::Close();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSUILockSubsystem* UILock = GameInstance->GetSubsystem<UMSUILockSubsystem>()) UILock->PopScreen(TEXT("DeploymentMap"));
	}
}

void UMSDeploymentMapWidget::SetDeployPreset(UMSMobileSuitData* Preset)
{
	DeployPreset = Preset; BindDeckSubsystem();
	if (DeckSubsystem)
	{
		// The shared selected index is authoritative when the same preset is
		// present in more than one slot; pointer matching alone loses that index.
		const int32 SharedSelectedIndex = DeckSubsystem->GetDeploymentSelectedSlotIndex();
		if (DeckSubsystem->GetDeploymentPresetInSlot(SharedSelectedIndex) == Preset)
		{
			SelectedDeckSlotIndex = SharedSelectedIndex;
		}
		else
		{
			SelectedDeckSlotIndex = INDEX_NONE;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (DeckSubsystem->GetDeploymentPresetInSlot(Index) == Preset)
				{
					SelectedDeckSlotIndex = Index;
					break;
				}
			}
		}
	}
	RefreshMSList();
}

void UMSDeploymentMapWidget::SetMissionRuleName(const FText& InRuleName)
{
	MissionRuleName = InRuleName.IsEmpty() ? FText::FromString(TEXT("戦術目標制圧作戦")) : InRuleName;
	if (MissionRuleText) MissionRuleText->SetText(MissionRuleName);
}

FText UMSDeploymentMapWidget::GetMissionRuleName() const
{
	return MissionRuleText ? MissionRuleText->GetText() : MissionRuleName;
}

bool UMSDeploymentMapWidget::SelectDeckSlot(const int32 SlotIndex)
{
	BindDeckSubsystem();
	UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetDeploymentPresetInSlot(SlotIndex) : nullptr;
	if (!Preset) return false;
	DeckSubsystem->SetDeploymentSelectedSlotIndex(SlotIndex); SelectedDeckSlotIndex=DeckSubsystem->GetDeploymentSelectedSlotIndex();
	if(SelectedDeckSlotIndex!=SlotIndex) return false;
	DeployPreset = Preset; RefreshMSList(); return true;
}

void UMSDeploymentMapWidget::ClearSelection()
{
	SelectedSpawnTransform.Reset();
	if (WarningText) WarningText->SetVisibility(ESlateVisibility::Collapsed);
	RefreshSelectedMarker();
}

void UMSDeploymentMapWidget::CacheFriendlySpawnPointMarkers()
{
	for (UImage* Marker : FriendlySpawnPointMarkers) if (Marker) Marker->RemoveFromParent();
	FriendlySpawnPoints.Reset(); FriendlySpawnPointMarkers.Reset();
	const FGenericTeamId TeamId = ResolveDeploymentTeam();
	if (!GetWorld()) return;
	for (TActorIterator<AMSBotSpawnPoint> It(GetWorld()); It; ++It)
	{
		AMSBotSpawnPoint* Point = *It; if (!IsValid(Point) || !Point->ServesTeam(TeamId)) continue;
		FriendlySpawnPoints.Add(Point);
		UImage* Marker = WidgetTree->ConstructWidget<UImage>(); Marker->SetBrush(MSDeployment::SolidBrush(MSMapPalette::Friendly)); Marker->SetRenderTransformAngle(45.0f);
		UCanvasPanelSlot* MarkerCanvasSlot = MapCanvas->AddChildToCanvas(Marker); MarkerCanvasSlot->SetAlignment(FVector2D(0.5f)); MarkerCanvasSlot->SetSize(FVector2D(MSDeployment::SpawnMarkerSizePx)); MarkerCanvasSlot->SetZOrder(6);
		FriendlySpawnPointMarkers.Add(Marker);
	}
	RefreshFriendlySpawnPointMarkers();
}

void UMSDeploymentMapWidget::RefreshFriendlySpawnPointMarkers()
{
	for (int32 Index = 0; Index < FriendlySpawnPointMarkers.Num(); ++Index)
	{
		UImage* Marker = FriendlySpawnPointMarkers[Index]; AMSBotSpawnPoint* Point = FriendlySpawnPoints.IsValidIndex(Index) ? FriendlySpawnPoints[Index] : nullptr;
		if (!Marker || !IsValid(Point)) { if (Marker) Marker->SetVisibility(ESlateVisibility::Collapsed); continue; }
		PositionMarker(Marker, ProjectWorldToMap(Point->GetActorLocation()));
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UMSDeploymentMapWidget::RefreshMSList()
{
	BindDeckSubsystem();
	UMSRespawnComponent* Respawn = ResolveRespawnComponent();
	const float Remaining = Respawn ? Respawn->GetRespawnTimeRemaining() : 0.0f;
	for (int32 Index = 0; Index < MSListButtons.Num(); ++Index)
	{
		UMSMobileSuitData* Preset = DeckSubsystem ? DeckSubsystem->GetDeploymentPresetInSlot(Index) : nullptr;
		const float SlotRemaining=Respawn?FMath::Max(Remaining,Respawn->GetDeckSlotTimeRemaining(Index)):Remaining;
		MSListButtons[Index]->SetIsEnabled(Preset != nullptr);
		MSListNameTexts[Index]->SetText(Preset ? Preset->UnitName : FText::FromString(TEXT("- EMPTY -")));
		MSListRespawnTexts[Index]->SetText(Preset && SlotRemaining > 0.0f ? FText::Format(FText::FromString(TEXT("再出撃まで\n{0}秒")), FText::AsNumber(FMath::CeilToInt(SlotRemaining))) : FText::GetEmpty());
		MSListBorders[Index]->SetBrushColor(Index == SelectedDeckSlotIndex ? MSDeployment::Selected : MSDeployment::Card);
		MSListButtons[Index]->SetToolTipText(Preset ? Preset->UnitName : FText::GetEmpty());
		MSListPortraits[Index]->SetVisibility(Preset ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (RenderedPresets[Index] != Preset)
		{
			if (MSListRenderers[Index]) MSListRenderers[Index]->SetMobileSuit(Preset,
				DeckSubsystem && DeckSubsystem->GetDeploymentLoadoutInSlot(Index).bShieldEquipped);
			RenderedPresets[Index] = Preset;
		}
	}
	if (BackdropRenderer && BackdropPreset != DeployPreset)
	{
		BackdropRenderer->SetMobileSuit(DeployPreset,
			DeckSubsystem && DeckSubsystem->GetDeploymentLoadoutInSlot(SelectedDeckSlotIndex).bShieldEquipped); BackdropPreset = DeployPreset;
	}
}

void UMSDeploymentMapWidget::NativeDestruct()
{
	if (AMSPlayerController* PC = Cast<AMSPlayerController>(GetOwningPlayer()))
		PC->OnOnlineDeploymentResult.RemoveDynamic(this, &ThisClass::HandleOnlineDeploymentResult);
	if (BackdropRenderer) BackdropRenderer->Shutdown();
	for (UMSMobileSuitPreviewRenderer* Renderer : MSListRenderers) if (Renderer) Renderer->Shutdown();
	Super::NativeDestruct();
}

UMSRespawnComponent* UMSDeploymentMapWidget::ResolveRespawnComponent() const
{
	if (RespawnComponentOverride) return RespawnComponentOverride;
	APawn* Pawn = GetOwningPlayerPawn();
	return Pawn ? Pawn->FindComponentByClass<UMSRespawnComponent>() : nullptr;
}

FGenericTeamId UMSDeploymentMapWidget::ResolveDeploymentTeam() const
{
	if (MSOnline::IsEnabled())
	{
		if (const AMSPlayerController* PC = Cast<AMSPlayerController>(GetOwningPlayer()))
			return PC->GetOnlineAssignedTeamId();
	}
	return UMSTeamStatics::GetTeamId(GetOwningPlayerPawn());
}
void UMSDeploymentMapWidget::RefreshSelectedMarker()
{
	if (!SelectedDeployMarker) return;
	if (!SelectedSpawnTransform.IsSet()) { SelectedDeployMarker->SetVisibility(ESlateVisibility::Collapsed); return; }
	PositionMarker(SelectedDeployMarker, ProjectWorldToMap(SelectedSpawnTransform->GetLocation()));
	SelectedDeployMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FReply UMSDeploymentMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		HandleCloseRequested();
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !GetMapCanvas()) return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	const FVector2D Local = GetMapCanvas()->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (Local.X < 0.0f || Local.Y < 0.0f || Local.X > GetMapSize().X || Local.Y > GetMapSize().Y) return FReply::Unhandled();
	const FVector2D ContentSize = GetMapContentSize();
	const FVector2D ContentMin = (GetMapSize() - ContentSize) * 0.5f;
	if (Local.X < ContentMin.X || Local.Y < ContentMin.Y ||
		Local.X > ContentMin.X + ContentSize.X || Local.Y > ContentMin.Y + ContentSize.Y)
		return FReply::Unhandled();

	const FGenericTeamId TeamId = ResolveDeploymentTeam();
	TArray<FMSDeployPointSnapshot> Snapshots; Snapshots.Reserve(GetCachedCapturePoints().Num() + FriendlySpawnPoints.Num());
	for (int32 PointIndex = 0; PointIndex < GetCachedCapturePoints().Num(); ++PointIndex)
	{
		AMSCapturePoint* Point = GetCachedCapturePoints()[PointIndex];
		if (!IsValid(Point)) continue;
		FMSDeployPointSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
		Snapshot.MapLocal = ProjectWorldToMap(Point->GetActorLocation());
		Snapshot.WorldLocation = Point->GetActorLocation(); Snapshot.bCanDeploy = Point->CanDeployFor(TeamId);
		Snapshot.MarkerSizePx = Point->IsA<AMSHomeBase>() ? MSMapPalette::HomeBaseMarkerSizePx : MSMapPalette::CapturePointMarkerSizePx;
		Snapshot.Label = ResolveCapturePointNumber(Point, PointIndex);
		Snapshot.bDiamondMarker = Point->IsA<AMSHomeBase>();
	}
	AppendTeamSpawnPointSnapshots(GetWorld(), TeamId, Snapshots, GetMapSize());
	const FVector CandidateWorld = UnprojectMapToWorld(Local);
	float NearestDistance = TNumericLimits<float>::Max();
	AMSCapturePoint* Nearest = AMSCapturePoint::FindNearestDeployablePoint(GetWorld(), TeamId, CandidateWorld, NearestDistance);
	FMSMapClickResolution Resolution = ResolveMapClick(Local, Snapshots, Nearest != nullptr, NearestDistance,
		MSMapPalette::FreeDeployRangeCm, GetMapWorldHalfExtent(), GetMapSize(), MSMapPalette::EdgeMarginPx);
	if (Resolution.bResolved && !Resolution.bDirectPoint)
	{
		Resolution.WorldLocation = CandidateWorld;
	}
	if (Resolution.bResolved) ConfirmSelection(Resolution.WorldLocation);
	else WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
	return FReply::Handled();
}

void UMSDeploymentMapWidget::ConfirmSelection(const FVector& WorldLocation)
{
	if (bOnlineDeploymentPending) return;
	if (bDeployOnSelect && MSOnline::IsEnabled())
	{
		RequestOnlineSelection(WorldLocation);
		return;
	}
	UMSRespawnComponent* Respawn = nullptr;
	if (bDeployOnSelect)
	{
		Respawn = ResolveRespawnComponent();
		if (!Respawn || !Respawn->CanDeploy() || (SelectedDeckSlotIndex!=INDEX_NONE && !Respawn->CanDeployDeckSlot(SelectedDeckSlotIndex)))
		{
			if (WarningText) WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
			return;
		}
	}

	// A deck can change while the map remains open. Do not deploy the former
	// suit or silently fall back to catalog defaults after removal/replacement.
	if(bDeployOnSelect && DeckSubsystem && (!DeployPreset || DeckSubsystem->GetDeploymentPresetInSlot(SelectedDeckSlotIndex)!=DeployPreset))
	{
		if(WarningText) WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, WorldLocation);
	if (!bDeployOnSelect)
	{
		SelectedSpawnTransform = SpawnTransform; RefreshSelectedMarker(); OnSpawnPointSelected.Broadcast(SpawnTransform);
		return;
	}
	if (!Respawn) return;
	if (DeckSubsystem && DeckSubsystem->GetDeploymentPresetInSlot(SelectedDeckSlotIndex)==DeployPreset)
	{
		if (!Respawn->DeployDeckSlot(DeckSubsystem,SelectedDeckSlotIndex,SpawnTransform)) return;
	}
	else Respawn->Deploy(DeployPreset, SpawnTransform);
	SelectedSpawnTransform = SpawnTransform; RefreshSelectedMarker(); OnSpawnPointSelected.Broadcast(SpawnTransform);
	DeployPreset = nullptr; CloseFrontendAfterDeploy(); ClearSelection();
}

bool UMSDeploymentMapWidget::RequestOnlineSelection(const FVector& WorldLocation)
{
	AMSPlayerController* PC = Cast<AMSPlayerController>(GetOwningPlayer());
	if (!PC || !PC->IsOnlineAuthenticated() || SelectedDeckSlotIndex < 0 || SelectedDeckSlotIndex >= 4)
	{
		if (WarningText) { WarningText->SetText(FText::FromString(TEXT("サーバーへのログインを確認してください。"))); WarningText->SetVisibility(ESlateVisibility::HitTestInvisible); }
		return false;
	}
	const FGenericTeamId TeamId = ResolveDeploymentTeam();
	AActor* Closest = nullptr;
	double DistanceSquared = TNumericLimits<double>::Max();
	auto Consider = [&](AActor* Point)
	{
		const double CandidateDistance = FVector::DistSquared2D(Point->GetActorLocation(), WorldLocation);
		if (CandidateDistance < DistanceSquared) { Closest = Point; DistanceSquared = CandidateDistance; }
	};
	for (AMSCapturePoint* Point : GetCachedCapturePoints())
		if (IsValid(Point) && Point->CanDeployFor(TeamId)) Consider(Point);
	for (AMSBotSpawnPoint* Point : FriendlySpawnPoints)
		if (IsValid(Point) && Point->ServesTeam(TeamId)) Consider(Point);
	if (!Closest || DistanceSquared > FMath::Square(static_cast<double>(MSMapPalette::FreeDeployRangeCm)))
	{
		if (WarningText) { WarningText->SetText(FText::FromString(TEXT("自軍の出撃地点を選択してください。"))); WarningText->SetVisibility(ESlateVisibility::HitTestInvisible); }
		return false;
	}
	// Only a named, server-resolved point crosses the network. No client-authored teleport transform.
	SelectedSpawnTransform = FTransform(FRotator::ZeroRotator, Closest->GetActorLocation());
	RefreshSelectedMarker();
	bOnlineDeploymentPending = true;
	if (WarningText) { WarningText->SetText(FText::FromString(TEXT("サーバーに出撃を確認しています…"))); WarningText->SetVisibility(ESlateVisibility::HitTestInvisible); }
	if (!PC->RequestOnlineDeployment(SelectedDeckSlotIndex, Closest->GetFName()))
	{
		bOnlineDeploymentPending = false;
		if (WarningText) WarningText->SetText(FText::FromString(TEXT("出撃を要求できませんでした。接続と待ち時間を確認してください。")));
		return false;
	}
	return true;
}

void UMSDeploymentMapWidget::HandleOnlineDeploymentResult(const bool bSuccess, FText Message)
{
	if (!bOnlineDeploymentPending || !IsOpen()) return;
	bOnlineDeploymentPending = false;
	if (!bSuccess)
	{
		if (WarningText) { WarningText->SetText(Message); WarningText->SetVisibility(ESlateVisibility::HitTestInvisible); }
		return;
	}
	if (SelectedSpawnTransform.IsSet()) OnSpawnPointSelected.Broadcast(SelectedSpawnTransform.GetValue());
	DeployPreset = nullptr;
	CloseFrontendAfterDeploy();
	ClearSelection();
}

void UMSDeploymentMapWidget::CloseFrontendAfterDeploy()
{
	if (UGameInstance* GI = GetGameInstance())
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			Bgm->PlayScene(EMSBgmScene::Battle);
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSSpawnSelectWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSSpawnSelectWidget* Typed = Cast<UMSSpawnSelectWidget>(Widget); Typed && Typed->GetOwningPlayer() == GetOwningPlayer()) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSBriefingWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSBriefingWidget* Typed = Cast<UMSBriefingWidget>(Widget); Typed && Typed->GetOwningPlayer() == GetOwningPlayer()) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSSortieWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSSortieWidget* Typed = Cast<UMSSortieWidget>(Widget); Typed && Typed->GetOwningPlayer() == GetOwningPlayer()) Typed->Close();
	Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UMSLobbyWidget::StaticClass(), false);
	for (UUserWidget* Widget : Found) if (UMSLobbyWidget* Typed = Cast<UMSLobbyWidget>(Widget); Typed && Typed->GetOwningPlayer() == GetOwningPlayer()) Typed->Close();
	Close();
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(this, false);
		// Also clear a stale pauser directly. This keeps deployment authoritative even
		// when the briefing owner changed during a death/respawn transition.
		World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	}
}

void UMSDeploymentMapWidget::HandleMSListSlot0Clicked() { SelectDeckSlot(0); }
void UMSDeploymentMapWidget::HandleMSListSlot1Clicked() { SelectDeckSlot(1); }
void UMSDeploymentMapWidget::HandleMSListSlot2Clicked() { SelectDeckSlot(2); }
void UMSDeploymentMapWidget::HandleMSListSlot3Clicked() { SelectDeckSlot(3); }

void UMSDeploymentMapWidget::HandleReturnToSuitSelectionClicked()
{
	if (bOnlineDeploymentPending) return;
	Close();
	if (SourceSpawnSelectWidget) SourceSpawnSelectWidget->ResumeFromDeployment();
}

void UMSDeploymentMapWidget::HandleCloseRequested()
{
	HandleReturnToSuitSelectionClicked();
}

void UMSDeploymentMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsOpen()) { RefreshFriendlySpawnPointMarkers(); RefreshMSList(); }
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSDeploymentMapWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSDeploymentMapWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem)
{
	DeckSubsystem = InSubsystem; SelectedDeckSlotIndex = DeckSubsystem ? DeckSubsystem->GetDeploymentSelectedSlotIndex() : INDEX_NONE; RefreshMSList();
}
#endif
