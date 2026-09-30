#include "UI/MSFullscreenMapWidget.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/MSUnitBase.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSMapTypes.h"
#include "UI/MSMapRadarRangeWidget.h"

namespace
{
	UImage* AddFullscreenMapImage(UWidgetTree* Tree, UCanvasPanel* Parent, const FVector2D& Position,
		const FVector2D& Size, const FLinearColor& Color)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		FSlateBrush Brush; Brush.DrawAs = ESlateBrushDrawType::Box; Brush.TintColor = Color; Image->SetBrush(Brush);
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Image);
		Slot->SetAlignment(FVector2D(0.5f)); Slot->SetPosition(Position); Slot->SetSize(Size);
		return Image;
	}
}

FVector2D UMSFullscreenMapWidget::GetLayoutMapSize(const FVector2D& ViewportSize)
{
	const float Scale = FMath::Max(0.1f, FMath::Min(ViewportSize.X / 1920.0f, ViewportSize.Y / 1080.0f));
	return FVector2D(1260.0f, 810.0f) * Scale;
}

FVector2D UMSFullscreenMapWidget::GetMapSize() const
{
	int32 Width = 0, Height = 0;
	if (const APlayerController* PC = GetOwningPlayer()) PC->GetViewportSize(Width, Height);
	return GetLayoutMapSize(Width > 0 && Height > 0 ? FVector2D(Width, Height) : FVector2D(1920.0f, 1080.0f));
}

void UMSFullscreenMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// The combat map is a passive overlay. It must never receive keyboard or pointer focus,
	// because gameplay (including the second Tab press) continues underneath it.
	SetIsFocusable(false);
	RadarRangeLayer = WidgetTree->ConstructWidget<UMSMapRadarRangeWidget>();
	UCanvasPanelSlot* RadarSlot = GetMapCanvas()->AddChildToCanvas(RadarRangeLayer);
	RadarSlot->SetPosition(FVector2D::ZeroVector); RadarSlot->SetSize(GetMapSize());
	RadarRangeLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	for (int32 Index = 0; Index < MSMapPalette::MaxUnitMarkers; ++Index)
	{
		// Brush tint multiplies ColorAndOpacity; leave it white so ally blue does not turn purple.
		UImage* Marker = AddFullscreenMapImage(WidgetTree, GetMapCanvas(), FVector2D::ZeroVector, FVector2D(12.0f), FLinearColor::White);
		Marker->SetRenderTransformAngle(45.0f); Marker->SetVisibility(ESlateVisibility::Collapsed); UnitMarkers.Add(Marker);
	}
	PlayerMarker = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* PlayerSlot = GetMapCanvas()->AddChildToCanvas(PlayerMarker);
	PlayerSlot->SetAlignment(FVector2D(0.5f)); PlayerSlot->SetSize(FVector2D(26.0f));
	AddFullscreenMapImage(WidgetTree, PlayerMarker, FVector2D(13.0f, 9.0f), FVector2D(5.0f, 18.0f), MSMapPalette::Player);
	UImage* Left = AddFullscreenMapImage(WidgetTree, PlayerMarker, FVector2D(8.0f, 15.0f), FVector2D(4.0f, 13.0f), MSMapPalette::Player); Left->SetRenderTransformAngle(-45.0f);
	UImage* Right = AddFullscreenMapImage(WidgetTree, PlayerMarker, FVector2D(18.0f, 15.0f), FVector2D(4.0f, 13.0f), MSMapPalette::Player); Right->SetRenderTransformAngle(45.0f);
	BuildHeader();
}

void UMSFullscreenMapWidget::BuildHeader()
{
	const FVector2D MapSize = GetMapSize();
	UCanvasPanel* Header = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FullscreenMapHeader"));
	UCanvasPanelSlot* HeaderSlot = RootCanvas->AddChildToCanvas(Header);
	HeaderSlot->SetAnchors(FAnchors(0.5f)); HeaderSlot->SetAlignment(FVector2D(0.5f));
	HeaderSlot->SetPosition(FVector2D(0.0f, -MapSize.Y * 0.5f - 72.0f));
	HeaderSlot->SetSize(FVector2D(MapSize.X, 72.0f));

	auto AddText = [this, Header](const FName Name, const FVector2D& Position, const FVector2D& Size, const int32 FontSize)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White)); Text->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font = Text->GetFont(); Font.Size = FontSize; Font.OutlineSettings.OutlineSize = 1; Text->SetFont(Font);
		UCanvasPanelSlot* Slot = Header->AddChildToCanvas(Text); Slot->SetPosition(Position); Slot->SetSize(Size);
		return Text;
	};
	MapNameText = AddText(TEXT("FullscreenMapName"), FVector2D(MapSize.X * 0.5f - 180.0f, 0.0f), FVector2D(360.0f, 26.0f), 18);
	RemainingTimeText = AddText(TEXT("FullscreenMapRemainingTime"), FVector2D(MapSize.X - 150.0f, 0.0f), FVector2D(130.0f, 26.0f), 18);

	auto AddGauge = [this, Header](const FName Name, const float Y, const FLinearColor& Color)
	{
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
		Bar->SetFillColorAndOpacity(Color); Bar->SetPercent(1.0f);
		UCanvasPanelSlot* Slot = Header->AddChildToCanvas(Bar); Slot->SetPosition(FVector2D(GetMapSize().X * 0.5f - 260.0f, Y)); Slot->SetSize(FVector2D(520.0f, 12.0f));
		return Bar;
	};
	PlayerGaugeBar = AddGauge(TEXT("FullscreenMapPlayerGauge"), 34.0f, MSMapPalette::Friendly);
	EnemyGaugeBar = AddGauge(TEXT("FullscreenMapEnemyGauge"), 51.0f, MSMapPalette::Enemy);

	NorthText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FullscreenMapNorth"));
	NorthText->SetText(FText::FromString(TEXT("N"))); NorthText->SetColorAndOpacity(FSlateColor(FLinearColor::White)); NorthText->SetJustification(ETextJustify::Center);
	FSlateFontInfo NorthFont = NorthText->GetFont(); NorthFont.Size = 22; NorthFont.OutlineSettings.OutlineSize = 2; NorthText->SetFont(NorthFont);
	UCanvasPanelSlot* NorthSlot = GetMapCanvas()->AddChildToCanvas(NorthText);
	// 「上が北」であることが一目で伝わるよう、方位記号は右上ではなく上辺中央に置く。
	NorthSlot->SetAlignment(FVector2D(0.5f)); NorthSlot->SetPosition(FVector2D(MapSize.X * 0.5f, 30.0f)); NorthSlot->SetSize(FVector2D(32.0f));
	EntranceLegendText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FullscreenEntranceLegend"));
	EntranceLegendText->SetJustification(ETextJustify::Center);
	EntranceLegendText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo LegendFont = EntranceLegendText->GetFont(); LegendFont.Size = 14; EntranceLegendText->SetFont(LegendFont);
	UCanvasPanelSlot* LegendSlot = RootCanvas->AddChildToCanvas(EntranceLegendText);
	LegendSlot->SetAnchors(FAnchors(0.5f)); LegendSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	LegendSlot->SetPosition(FVector2D(0.0f, MapSize.Y * 0.5f + 8.0f)); LegendSlot->SetSize(FVector2D(MapSize.X, 24.0f));
	EntranceLegendText->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSFullscreenMapWidget::RefreshHeader()
{
	const AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
	if (MapNameText) MapNameText->SetText(State ? State->MapDisplayName : FText::FromString(TEXT("BATTLE MAP")));
	if (RemainingTimeText) RemainingTimeText->SetText(State ? State->GetFormattedRemainingTime() : FText::FromString(TEXT("00'00")));
	const float Maximum = State ? FMath::Max(State->MaxGauge, KINDA_SMALL_NUMBER) : 1.0f;
	if (PlayerGaugeBar) PlayerGaugeBar->SetPercent(State ? FMath::Clamp(State->PlayerGauge / Maximum, 0.0f, 1.0f) : 0.0f);
	if (EnemyGaugeBar) EnemyGaugeBar->SetPercent(State ? FMath::Clamp(State->EnemyGauge / Maximum, 0.0f, 1.0f) : 0.0f);
	if (EntranceLegendText)
	{
		EntranceLegendText->SetText(FText::FromString(IsUndergroundMapLayer()
			? TEXT("◆ 1–4  地上出口") : TEXT("◆ 1–4  地下入口")));
		EntranceLegendText->SetVisibility(HasEntranceMarkers() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMSFullscreenMapWidget::Open()
{
	Super::Open();
	RefreshHeader(); HeaderRefreshAccumulator = 0.0f;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMSFullscreenMapWidget::Close()
{
	Super::Close();
}

void UMSFullscreenMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsOpen()) return;
	HeaderRefreshAccumulator += FMath::Max(0.0f, InDeltaTime);
	if (HeaderRefreshAccumulator >= MSMapPalette::PollIntervalSeconds)
	{
		HeaderRefreshAccumulator = FMath::Fmod(HeaderRefreshAccumulator, MSMapPalette::PollIntervalSeconds); RefreshHeader();
	}
	AMSUnitBase* Player = Cast<AMSUnitBase>(GetOwningPlayerPawn()); UWorld* World = GetWorld();
	if (!IsValid(Player) || !World) return;
	if (RadarRangeLayer)
	{
		static const TArray<FMSMapRadarRange> NoRadars;
		const AMSPlayerController* RadarController = Cast<AMSPlayerController>(GetOwningPlayer());
		const TArray<FMSMapRadarRange>& Radars = RadarController && RadarController->AreMapContactsFresh()
			? RadarController->GetMapRadarRanges() : NoRadars;
		RadarRangeLayer->SetOverviewView(GetMapWorldCenter(), GetMapWorldHalfExtents(),
			GetMapSize(), UMSTeamStatics::GetTeamId(Player), Radars);
	}
	PositionMarker(PlayerMarker, ProjectWorldToMap(Player->GetActorLocation()));
	// 地図は北上(ワールドYaw=WorldNorthYawDeg が画面上)。矢印もその基準に合わせて回す。
	PlayerMarker->SetRenderTransformAngle(Player->GetActorRotation().Yaw - MSMapPalette::WorldNorthYawDeg);

	int32 Used = 0;
	const AMSPlayerController* Controller = Cast<AMSPlayerController>(GetOwningPlayer());
	const bool bFreshSnapshot = Controller && Controller->AreMapContactsFresh();
	if (bFreshSnapshot)
	{
		for (const FMSMapFriendlyContact& Friendly : Controller->GetMapFriendlyContacts())
		{
			if (Used >= UnitMarkers.Num()) break;
			if (Friendly.UnitId == 0 || Friendly.UnitId == Player->GetUniqueID() ||
				Friendly.Location.ContainsNaN()) continue;
			UImage* Marker = UnitMarkers[Used++];
			Marker->SetColorAndOpacity(FriendlyDotColor(Friendly.BotTier));
			PositionMarker(Marker, ProjectWorldToMap(Friendly.Location));
			Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else if (UMSUnitRegistrySubsystem* Registry = World->GetSubsystem<UMSUnitRegistrySubsystem>())
	{
		const FGenericTeamId ViewerTeam = UMSTeamStatics::GetTeamId(Player);
		Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
		{
			if (Used >= UnitMarkers.Num() || Unit == Player) return;
			const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
			if (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead)) return;
			const FMSHUDMinimapUnitMarker Style = UMSHUDViewModel::MakeMinimapUnitMarker(ViewerTeam, UMSTeamStatics::GetTeamId(Unit));
			if (!Style.bFriendly) return;
			UImage* Marker = UnitMarkers[Used++]; Marker->SetColorAndOpacity(FriendlyDotColor(255));
			PositionMarker(Marker, ProjectWorldToMap(Unit->GetActorLocation())); Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		});
	}
	if (bFreshSnapshot) for (const FMSMapContact& Contact : Controller->GetMapContacts())
	{
		if (Used >= UnitMarkers.Num()) break;
		if (Contact.ContactId == 0 || Contact.LastKnownLocation.ContainsNaN()) continue;
		UImage* Marker = UnitMarkers[Used++];
		Marker->SetColorAndOpacity(MSMapPalette::Enemy);
		PositionMarker(Marker, ProjectWorldToMap(Contact.LastKnownLocation));
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	for (int32 Index = Used; Index < UnitMarkers.Num(); ++Index) UnitMarkers[Index]->SetVisibility(ESlateVisibility::Collapsed);
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSFullscreenMapWidget::InitializeForAutomationTests() { if (!PlayerMarker) NativeOnInitialized(); }
#endif
