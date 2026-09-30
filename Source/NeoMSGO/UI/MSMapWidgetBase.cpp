#include "UI/MSMapWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Core/MSTeamStatics.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TargetPoint.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSMapCaptureSubsystem.h"
#include "UI/MSMapTypes.h"

namespace
{
	constexpr float CapturePointShapeScale = 0.72f;
	constexpr float CapturePointLabelHeightScale = 0.70f;
	constexpr float CapturePointLabelFontScale = 0.43f;
	constexpr float CapturePointLabelOutlineSize = 1.0f;

	FSlateBrush MakeSolidBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = Color;
		return Brush;
	}

	/** ワールドの相対座標を「北が +X / 東が +Y」のマップ座標系へ回す(北上表示の基礎)。 */
	FVector WorldToNorthUpFrame(const FVector& RelativeWorld)
	{
		return RelativeWorld.RotateAngleAxis(-MSMapPalette::WorldNorthYawDeg, FVector::UpVector);
	}

	/** WorldToNorthUpFrame の逆変換。 */
	FVector NorthUpFrameToWorld(const FVector& NorthUp)
	{
		return NorthUp.RotateAngleAxis(MSMapPalette::WorldNorthYawDeg, FVector::UpVector);
	}

	FVector2D MeasureCapturePointLabel(const FText& Number, const float FontSize)
	{
		// ConstructCapturePointMarker uses the UMG normal font at the same size.
		// Measuring it through Slate keeps longer authored labels inside the same
		// interaction rectangle.  The deterministic fallback is needed by the
		// NullRHI/editor automation process before Slate has been initialized.
		FVector2D Measured = FVector2D::ZeroVector;
		if (!Number.IsEmpty() && FSlateApplication::IsInitialized())
		{
			// UTextBlock's CDO is the source of the default font used by the
			// instance constructed below; using it avoids measuring with a Slate
			// style that differs from UMG's default bold Roboto face.
			FSlateFontInfo Font = GetDefault<UTextBlock>()->GetFont();
			Font.Size = FMath::RoundToInt(FontSize);
			const auto MeasuredSlate = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Number, Font);
			Measured = FVector2D(static_cast<float>(MeasuredSlate.X), static_cast<float>(MeasuredSlate.Y));
		}
		if (Measured.IsNearlyZero())
		{
			// The slot is the authoritative minimum draw area.  Its width also
			// preserves the single-character marker spacing in the reference UI.
			Measured = FVector2D(FontSize * 0.62f * Number.ToString().Len(), FontSize);
		}
		return Measured + FVector2D(CapturePointLabelOutlineSize * 2.0f);
	}
}

FVector2D UMSMapWidgetBase::WorldToMapLocal(const FVector& World, const float WorldHalfExtentCm,
	const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	return WorldToMapLocalCentered(World, FVector::ZeroVector, WorldHalfExtentCm, MapSizePx, EdgeMarginPx);
}

FBox2d UMSMapWidgetBase::GetLocalAtlasUVRegion(const FVector& PlayerLocation, const FVector& WorldCenter,
	const FVector2D& WorldHalfExtentsCm, const float OrthoWidthCm)
{
	if (WorldHalfExtentsCm.X <= 0.0f || WorldHalfExtentsCm.Y <= 0.0f || OrthoWidthCm <= 0.0f)
		return FBox2d(FVector2d(0, 0), FVector2d(1, 1));
	const FVector Relative = WorldToNorthUpFrame(PlayerLocation - WorldCenter);
	const double CenterU = 0.5 + Relative.Y / (2.0 * WorldHalfExtentsCm.Y);
	const double CenterV = 0.5 - Relative.X / (2.0 * WorldHalfExtentsCm.X);
	const double HalfU = OrthoWidthCm / (4.0 * WorldHalfExtentsCm.Y);
	const double HalfV = OrthoWidthCm / (4.0 * WorldHalfExtentsCm.X);
	return FBox2d(FVector2d(CenterU - HalfU, CenterV - HalfV),
		FVector2d(CenterU + HalfU, CenterV + HalfV));
}

FVector2D UMSMapWidgetBase::WorldToMapLocalCentered(const FVector& World, const FVector& WorldCenter,
	const float WorldHalfExtentCm, const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	return WorldToMapLocalRectCentered(World, WorldCenter, FVector2D(WorldHalfExtentCm), MapSizePx, EdgeMarginPx);
}

FVector2D UMSMapWidgetBase::WorldToMapLocalRectCentered(const FVector& World, const FVector& WorldCenter,
	const FVector2D& WorldHalfExtentsCm, const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	if (WorldHalfExtentsCm.X <= 0.0f || WorldHalfExtentsCm.Y <= 0.0f) return MapSizePx * 0.5f;
	// 北上表示: 回転後の +X(=地理的な北)が画面上、+Y(=地理的な東)が画面右になる。
	const FVector Relative = WorldToNorthUpFrame(World - WorldCenter);
	const FVector2D HalfUsable = GetRectContentSize(WorldHalfExtentsCm, MapSizePx, EdgeMarginPx) * 0.5f;
	const float NormalizedX = FMath::Clamp(Relative.X / WorldHalfExtentsCm.X, -1.0f, 1.0f);
	const float NormalizedY = FMath::Clamp(Relative.Y / WorldHalfExtentsCm.Y, -1.0f, 1.0f);
	return FVector2D(MapSizePx.X * 0.5f + NormalizedY * HalfUsable.X,
		MapSizePx.Y * 0.5f - NormalizedX * HalfUsable.Y);
}

FVector UMSMapWidgetBase::MapLocalToWorld(const FVector2D& Local, const float WorldHalfExtentCm,
	const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	return MapLocalToWorldCentered(Local, FVector::ZeroVector, WorldHalfExtentCm, MapSizePx, EdgeMarginPx);
}

FVector UMSMapWidgetBase::MapLocalToWorldCentered(const FVector2D& Local, const FVector& WorldCenter,
	const float WorldHalfExtentCm, const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	return MapLocalToWorldRectCentered(Local, WorldCenter, FVector2D(WorldHalfExtentCm), MapSizePx, EdgeMarginPx);
}

FVector UMSMapWidgetBase::MapLocalToWorldRectCentered(const FVector2D& Local, const FVector& WorldCenter,
	const FVector2D& WorldHalfExtentsCm, const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	const FVector2D HalfUsable = GetRectContentSize(WorldHalfExtentsCm, MapSizePx, EdgeMarginPx) * 0.5f;
	const float NormalizedX = FMath::Clamp((MapSizePx.Y * 0.5f - Local.Y) / FMath::Max(HalfUsable.Y, KINDA_SMALL_NUMBER), -1.0f, 1.0f);
	const float NormalizedY = FMath::Clamp((Local.X - MapSizePx.X * 0.5f) / FMath::Max(HalfUsable.X, KINDA_SMALL_NUMBER), -1.0f, 1.0f);
	return WorldCenter + NorthUpFrameToWorld(
		FVector(NormalizedX * WorldHalfExtentsCm.X, NormalizedY * WorldHalfExtentsCm.Y, 0.0f));
}

FVector2D UMSMapWidgetBase::GetRectContentSize(const FVector2D& WorldHalfExtentsCm,
	const FVector2D& MapSizePx, const float EdgeMarginPx)
{
	if (WorldHalfExtentsCm.X <= 0.0f || WorldHalfExtentsCm.Y <= 0.0f) return FVector2D::ZeroVector;
	const FVector2D Available(
		FMath::Max(0.0f, MapSizePx.X - EdgeMarginPx * 2.0f),
		FMath::Max(0.0f, MapSizePx.Y - EdgeMarginPx * 2.0f));
	const float PixelsPerCm = FMath::Min(Available.X / (WorldHalfExtentsCm.Y * 2.0f),
		Available.Y / (WorldHalfExtentsCm.X * 2.0f));
	return FVector2D(WorldHalfExtentsCm.Y * 2.0f * PixelsPerCm,
		WorldHalfExtentsCm.X * 2.0f * PixelsPerCm);
}

float UMSMapWidgetBase::GetCaptureMarkerSizeForCanvas(const float StandardSizePx, const FVector2D& ContentSizePx)
{
	// A 208px briefing map must not inherit the 48/38px full-map symbols.
	return StandardSizePx * FMath::Clamp(FMath::Min(ContentSizePx.X / 500.0f,
		ContentSizePx.Y / 320.0f), 0.40f, 1.0f);
}

FLinearColor UMSMapWidgetBase::FriendlyDotColor(const uint8 BotTier)
{
	// Preserve a small tier distinction without letting tier 2 read as a green enemy/utility dot.
	switch (BotTier)
	{
	case 1: return FLinearColor(0.20f, 0.73f, 1.00f, 1.0f);
	case 2: return FLinearColor(0.44f, 0.81f, 1.00f, 1.0f);
	default: return MSMapPalette::Friendly;
	}
}

TArray<FMSMapEntranceLocation> UMSMapWidgetBase::GatherEntranceLocations(UWorld* World, const bool bUnderground)
{
	TArray<FMSMapEntranceLocation> Locations;
	if (!World) return Locations;
	TArray<int32> Counts;
	Counts.Init(0, 4);
	TArray<FVector> Positions;
	Positions.Init(FVector::ZeroVector, 4);
	const FName SideTag = bUnderground ? FName(TEXT("NYT_NavBottom")) : FName(TEXT("NYT_NavTop"));
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		ATargetPoint* Point = *It;
		if (!IsValid(Point) || !Point->ActorHasTag(TEXT("NYT_NavEntrance")) || !Point->ActorHasTag(SideTag)) continue;
		for (int32 Number = 1; Number <= 4; ++Number)
		{
			if (!Point->ActorHasTag(FName(*FString::Printf(TEXT("NYT_NavEntry_%02d"), Number)))) continue;
			// Duplicate markers make the authored entrance ambiguous; omit that number rather than guessing.
			++Counts[Number - 1];
			Positions[Number - 1] = Point->GetActorLocation();
			break;
		}
	}
	for (int32 Index = 0; Index < Counts.Num(); ++Index)
	{
		if (Counts[Index] != 1) continue;
		FMSMapEntranceLocation& Location = Locations.AddDefaulted_GetRef();
		Location.Number = Index + 1;
		Location.Location = Positions[Index];
	}
	return Locations;
}

FMSMapEntranceMarker UMSMapWidgetBase::ConstructEntranceMarker(UWidgetTree* Tree, UCanvasPanel* Parent,
	const int32 Number, const float SizePx)
{
	FMSMapEntranceMarker Marker;
	if (!Tree || !Parent) return Marker;
	Marker.Root = Tree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* RootSlot = Parent->AddChildToCanvas(Marker.Root);
	RootSlot->SetAlignment(FVector2D(0.5f)); RootSlot->SetSize(FVector2D(SizePx)); RootSlot->SetZOrder(7);
	Marker.Shape = Tree->ConstructWidget<UImage>();
	Marker.Shape->SetBrush(MakeSolidBrush(FLinearColor(0.12f, 0.86f, 1.0f)));
	UCanvasPanelSlot* ShapeSlot = Marker.Root->AddChildToCanvas(Marker.Shape);
	ShapeSlot->SetAlignment(FVector2D(0.5f)); ShapeSlot->SetPosition(FVector2D(SizePx * 0.5f));
	ShapeSlot->SetSize(FVector2D(SizePx * 0.78f));
	Marker.Shape->SetRenderTransformAngle(45.0f);
	Marker.Number = Tree->ConstructWidget<UTextBlock>();
	Marker.Number->SetText(FText::AsNumber(Number)); Marker.Number->SetJustification(ETextJustify::Center);
	Marker.Number->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FSlateFontInfo Font = Marker.Number->GetFont(); Font.Size = FMath::RoundToInt(FMath::Max(9.0f, SizePx * 0.55f));
	Font.OutlineSettings.OutlineSize = 0; Marker.Number->SetFont(Font);
	UCanvasPanelSlot* NumberSlot = Marker.Root->AddChildToCanvas(Marker.Number);
	NumberSlot->SetAlignment(FVector2D(0.5f)); NumberSlot->SetPosition(FVector2D(SizePx * 0.5f));
	NumberSlot->SetSize(FVector2D(SizePx));
	Marker.Root->SetVisibility(ESlateVisibility::Collapsed);
	return Marker;
}

void UMSMapWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;
	MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* MapSlot = RootCanvas->AddChildToCanvas(MapCanvas);
	MapSlot->SetAnchors(FAnchors(0.5f));
	MapSlot->SetAlignment(FVector2D(0.5f));
	MapSlot->SetPosition(FVector2D::ZeroVector);
	MapSlot->SetSize(GetMapSize());
	BackgroundImage = ConstructSolidImage(MapCanvas, GetMapSize() * 0.5f, GetMapSize(), MSMapPalette::Background);

	// Keep the translucent grid above either the overview image or the solid fallback.
	const FVector2D InnerSize = GetMapSize() - FVector2D(GetMapEdgeMarginPx() * 2.0f);
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const float Fraction = static_cast<float>(Index) / 4.0f;
		MapGridLines.Add(ConstructSolidImage(MapCanvas, FVector2D(GetMapEdgeMarginPx() + InnerSize.X * Fraction, GetMapSize().Y * 0.5f),
			FVector2D(1.0f, InnerSize.Y), MSMapPalette::Grid));
		MapGridLines.Add(ConstructSolidImage(MapCanvas, FVector2D(GetMapSize().X * 0.5f, GetMapEdgeMarginPx() + InnerSize.Y * Fraction),
			FVector2D(InnerSize.X, 1.0f), MSMapPalette::Grid));
	}

	for (int32 Index = 0; Index < MSMapPalette::MaxRepairPodMarkers; ++Index)
	{
		UCanvasPanel* Marker = ConstructBullseyeMarker(); Marker->SetVisibility(ESlateVisibility::Collapsed); RepairPodMarkers.Add(Marker);
	}
	for (int32 Index = 0; Index < MSMapPalette::MaxRepairPackMarkers; ++Index)
	{
		UCanvasPanel* Marker = ConstructCrossMarker(); Marker->SetVisibility(ESlateVisibility::Collapsed); RepairPackMarkers.Add(Marker);
	}
	for (int32 Index = 0; Index < MSMapPalette::MaxAmmoPackMarkers; ++Index)
	{
		UImage* Marker = ConstructDiamondMarker(); Marker->SetVisibility(ESlateVisibility::Collapsed); AmmoPackMarkers.Add(Marker);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSMapWidgetBase::Open()
{
	const double StartSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map base Open begin"));
	if (!IsInViewport()) AddToViewport(20);
	SetVisibility(ESlateVisibility::Visible);
	RefreshOverviewBackground();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map base before capture points elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	if (CapturePoints.IsEmpty()) CacheCapturePointMarkers();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map base after capture points elapsed=%.3fms count=%d"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0, CapturePoints.Num());
	RefreshCapturePointMarkers();
	RefreshEntranceMarkers();
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map base before pickups elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	RefreshPickupActors();
	UpdatePickupMarkers();
	PollAccumulator = 0.0f;
	UE_LOG(LogTemp, Display, TEXT("[P1:DEPLOY] Map base Open complete elapsed=%.3fms"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

FVector UMSMapWidgetBase::GetMapWorldCenter() const
{
	const UWorld* World = GetWorld();
	const UMSMapCaptureSubsystem* Subsystem = World ? World->GetSubsystem<UMSMapCaptureSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetMapWorldCenter() : FVector::ZeroVector;
}

float UMSMapWidgetBase::GetMapWorldHalfExtent() const
{
	const UWorld* World = GetWorld();
	const UMSMapCaptureSubsystem* Subsystem = World ? World->GetSubsystem<UMSMapCaptureSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetMapWorldHalfExtent() : MSMapPalette::WorldHalfExtentCm;
}

FVector2D UMSMapWidgetBase::GetMapWorldHalfExtents() const
{
	const UWorld* World = GetWorld();
	const UMSMapCaptureSubsystem* Subsystem = World ? World->GetSubsystem<UMSMapCaptureSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetMapWorldHalfExtents() : FVector2D(MSMapPalette::WorldHalfExtentCm);
}

FVector2D UMSMapWidgetBase::GetMapContentSize() const
{
	return GetRectContentSize(GetMapWorldHalfExtents(), GetMapSize(), GetMapEdgeMarginPx());
}

FVector2D UMSMapWidgetBase::ProjectWorldToMap(const FVector& World) const
{
	return WorldToMapLocalRectCentered(World, GetMapWorldCenter(), GetMapWorldHalfExtents(), GetMapSize(), GetMapEdgeMarginPx());
}

FVector UMSMapWidgetBase::UnprojectMapToWorld(const FVector2D& Local) const
{
	return MapLocalToWorldRectCentered(Local, GetMapWorldCenter(), GetMapWorldHalfExtents(), GetMapSize(), GetMapEdgeMarginPx());
}

void UMSMapWidgetBase::RefreshOverviewBackground()
{
	if (!BackgroundImage) return;
	UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(BackgroundImage->Slot);
	UWorld* World = GetWorld();
	UMSMapCaptureSubsystem* Subsystem = World ? World->GetSubsystem<UMSMapCaptureSubsystem>() : nullptr;
	if (ShowEntranceMarkers() && Subsystem && IsValid(GetOwningPlayerPawn()))
		bUndergroundMapLayer = Subsystem->ShouldUseUndergroundLayer(
			GetOwningPlayerPawn()->GetActorLocation(), bUndergroundMapLayer)
			&& Subsystem->GetUndergroundOverviewTexture() != nullptr;
	else bUndergroundMapLayer = false;
	const FVector2D ContentSize = GetMapContentSize();
	for (int32 Index = 0; Index < MapGridLines.Num() / 2; ++Index)
	{
		const float Fraction = static_cast<float>(Index + 1) / 4.0f;
		if (UCanvasPanelSlot* Vertical = Cast<UCanvasPanelSlot>(MapGridLines[Index * 2]->Slot))
		{
			Vertical->SetPosition(FVector2D(GetMapSize().X * 0.5f + (Fraction - 0.5f) * ContentSize.X, GetMapSize().Y * 0.5f));
			Vertical->SetSize(FVector2D(1.0f, ContentSize.Y));
		}
		if (UCanvasPanelSlot* Horizontal = Cast<UCanvasPanelSlot>(MapGridLines[Index * 2 + 1]->Slot))
		{
			Horizontal->SetPosition(FVector2D(GetMapSize().X * 0.5f, GetMapSize().Y * 0.5f + (Fraction - 0.5f) * ContentSize.Y));
			Horizontal->SetSize(FVector2D(ContentSize.X, 1.0f));
		}
	}
	UTexture* Overview = bUndergroundMapLayer ? Subsystem->GetUndergroundOverviewTexture()
		: (Subsystem ? static_cast<UTexture*>(Subsystem->GetOverviewTexture()) : nullptr);
	if (Overview)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.SetResourceObject(Overview);
		Brush.TintColor = FLinearColor::White;
		BackgroundImage->SetBrush(Brush);
		if (BackgroundSlot) BackgroundSlot->SetSize(ContentSize);
	}
	else
	{
		BackgroundImage->SetBrush(MakeSolidBrush(MSMapPalette::Background));
		if (BackgroundSlot) BackgroundSlot->SetSize(ContentSize);
	}
}

void UMSMapWidgetBase::RefreshEntranceMarkers()
{
	if (!ShowEntranceMarkers() || !MapCanvas) return;
	if (EntranceMarkerRoots.IsEmpty())
	{
		for (int32 Number = 1; Number <= 4; ++Number)
		{
			const FMSMapEntranceMarker Marker = ConstructEntranceMarker(WidgetTree, MapCanvas, Number, 20.0f);
			EntranceMarkerRoots.Add(Marker.Root);
			EntranceMarkerShapes.Add(Marker.Shape);
		}
	}
	const TArray<FMSMapEntranceLocation> Locations = GatherEntranceLocations(GetWorld(), bUndergroundMapLayer);
	bHasEntranceMarkers = !Locations.IsEmpty();
	for (int32 Index = 0; Index < EntranceMarkerRoots.Num(); ++Index)
	{
		UCanvasPanel* Marker = EntranceMarkerRoots[Index];
		const FMSMapEntranceLocation* Location = Locations.FindByPredicate(
			[Index](const FMSMapEntranceLocation& Entry) { return Entry.Number == Index + 1; });
		if (!Marker || !Location) { if (Marker) Marker->SetVisibility(ESlateVisibility::Collapsed); continue; }
		if (UImage* Shape = EntranceMarkerShapes.IsValidIndex(Index) ? EntranceMarkerShapes[Index] : nullptr)
			Shape->SetColorAndOpacity(bUndergroundMapLayer ? FLinearColor(1.0f, 0.73f, 0.18f)
				: FLinearColor(0.12f, 0.86f, 1.0f));
		PositionMarker(Marker, ProjectWorldToMap(Location->Location));
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UMSMapWidgetBase::Close()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSMapWidgetBase::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsOpen()) return;
	PollAccumulator += FMath::Max(0.0f, InDeltaTime);
	if (PollAccumulator >= MSMapPalette::PollIntervalSeconds)
	{
		PollAccumulator = FMath::Fmod(PollAccumulator, MSMapPalette::PollIntervalSeconds);
		RefreshCapturePointMarkers();
		if (ShowEntranceMarkers())
		{
			RefreshOverviewBackground();
			RefreshEntranceMarkers();
		}
		RefreshPickupActors();
		UpdatePickupMarkers();
	}
}

UImage* UMSMapWidgetBase::ConstructSolidImage(UCanvasPanel* Parent, const FVector2D& Position,
	const FVector2D& Size, const FLinearColor& Color)
{
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrush(MakeSolidBrush(Color));
	UCanvasPanelSlot* ImageSlot = Parent->AddChildToCanvas(Image);
	ImageSlot->SetAlignment(FVector2D(0.5f)); ImageSlot->SetPosition(Position); ImageSlot->SetSize(Size);
	return Image;
}

void UMSMapWidgetBase::CacheCapturePointMarkers()
{
	UWorld* World = GetWorld(); if (!World) return;
	CapturePoints.Reset(); CapturePointMarkers.Reset(); CapturePointMarkerShapes.Reset();
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
		CapturePoints.Add(*It);
	CapturePoints.Sort([](const AMSCapturePoint& A, const AMSCapturePoint& B)
	{
		const int32 NameOrder = A.GetPointName().ToString().Compare(B.GetPointName().ToString(), ESearchCase::IgnoreCase);
		return NameOrder != 0 ? NameOrder < 0 : A.GetName() < B.GetName();
	});
	for (int32 Index = 0; Index < CapturePoints.Num(); ++Index)
	{
		AMSCapturePoint* Point = CapturePoints[Index];
		const float FullSize = Point->IsA<AMSHomeBase>() ? MSMapPalette::HomeBaseMarkerSizePx : MSMapPalette::CapturePointMarkerSizePx;
		const float Size = GetCaptureMarkerSizeForCanvas(FullSize, GetMapContentSize());
		const FMSMapCapturePointMarker Marker = ConstructCapturePointMarker(WidgetTree, MapCanvas,
			ProjectWorldToMap(Point->GetActorLocation()), Size, ResolveCapturePointNumber(Point, Index), Point->IsA<AMSHomeBase>());
		CapturePointMarkers.Add(Marker.Root); CapturePointMarkerShapes.Add(Marker.Shape);
	}
}

void UMSMapWidgetBase::RefreshCapturePointMarkers()
{
	FGenericTeamId ViewerTeam = UMSTeamStatics::GetTeamId(GetOwningPlayerPawn());
	if (const auto* PC = Cast<AMSPlayerController>(GetOwningPlayer()); PC && PC->IsOnlineAuthenticated())
		ViewerTeam = PC->GetOnlineAssignedTeamId();
	for (int32 Index = 0; Index < CapturePoints.Num(); ++Index)
	{
		AMSCapturePoint* Point = CapturePoints[Index]; UCanvasPanel* Marker = CapturePointMarkers.IsValidIndex(Index) ? CapturePointMarkers[Index] : nullptr;
		UImage* Shape = CapturePointMarkerShapes.IsValidIndex(Index) ? CapturePointMarkerShapes[Index] : nullptr;
		if (!IsValid(Point) || !Marker) { if (Marker) Marker->SetVisibility(ESlateVisibility::Collapsed); continue; }
		if (Shape) Shape->SetColorAndOpacity(ResolveCapturePointColor(Point, ViewerTeam));
		PositionMarker(Marker, ProjectWorldToMap(Point->GetActorLocation()));
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

FMSMapCapturePointMarker UMSMapWidgetBase::ConstructCapturePointMarker(UWidgetTree* Tree, UCanvasPanel* Parent,
	const FVector2D& Position, const float Size, const FText& Number, const bool bHomeBase)
{
	FMSMapCapturePointMarker Result;
	if (!Tree || !Parent) return Result;
	const FMSMapCapturePointInteractionBounds Interaction =
		GetCapturePointInteractionBounds(Position, Size, Number, bHomeBase);
	const FVector2D InteractionSize = Interaction.HalfExtents * 2.0f;
	const FVector2D InteractionCenter = InteractionSize * 0.5f;
	Result.Root = Tree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* RootSlot = Parent->AddChildToCanvas(Result.Root);
	RootSlot->SetAlignment(FVector2D(0.5f)); RootSlot->SetPosition(Position); RootSlot->SetSize(InteractionSize);

	Result.Shape = Tree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.DrawAs = bHomeBase ? ESlateBrushDrawType::Box : ESlateBrushDrawType::RoundedBox;
	if (!bHomeBase)
	{
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	}
	Brush.TintColor = MSMapPalette::Neutral;
	Result.Shape->SetBrush(Brush);
	UCanvasPanelSlot* ShapeSlot = Result.Root->AddChildToCanvas(Result.Shape);
	ShapeSlot->SetAlignment(FVector2D(0.5f)); ShapeSlot->SetPosition(InteractionCenter); ShapeSlot->SetSize(FVector2D(Size * CapturePointShapeScale));
	if (bHomeBase) Result.Shape->SetRenderTransformAngle(45.0f);

	Result.Number = Tree->ConstructWidget<UTextBlock>();
	Result.Number->SetText(Number); Result.Number->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Result.Number->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = Result.Number->GetFont(); Font.Size = FMath::RoundToInt(FMath::Max(8.0f, Size * CapturePointLabelFontScale)); Font.OutlineSettings.OutlineSize = 1; Font.OutlineSettings.OutlineColor = FLinearColor::Black;
	Result.Number->SetFont(Font);
	UCanvasPanelSlot* NumberSlot = Result.Root->AddChildToCanvas(Result.Number);
	NumberSlot->SetAlignment(FVector2D(0.5f)); NumberSlot->SetPosition(InteractionCenter); NumberSlot->SetSize(FVector2D(Size, Size * CapturePointLabelHeightScale));
	return Result;
}

FMSMapCapturePointInteractionBounds UMSMapWidgetBase::GetCapturePointInteractionBounds(
	const FVector2D& Position, const float Size, const FText& Number, const bool bHomeBase)
{
	FMSMapCapturePointInteractionBounds Result;
	Result.Center = Position;
	const float SafeSize = FMath::Max(0.0f, Size);
	const float LabelFontSize = FMath::Max(8.0f, SafeSize * CapturePointLabelFontScale);
	const FVector2D LabelSlotSize(SafeSize, SafeSize * CapturePointLabelHeightScale);
	const FVector2D LabelMeasuredSize = MeasureCapturePointLabel(Number, LabelFontSize);
	// UTextBlock is laid out in the slot used above.  Preserve that slot even
	// when font measurement is unavailable, then include the measured glyph and
	// its one-pixel outline when it is wider than the authored slot.
	Result.LabelHalfExtents = FVector2D(
		FMath::Max(LabelSlotSize.X, LabelMeasuredSize.X) * 0.5f,
		FMath::Max(LabelSlotSize.Y, LabelMeasuredSize.Y) * 0.5f);
	const float RotatedShapeHalfExtent = SafeSize * CapturePointShapeScale * 0.5f * FMath::Sqrt(2.0f);
	const float UnrotatedShapeHalfExtent = SafeSize * CapturePointShapeScale * 0.5f;
	Result.MarkerHalfExtents = FVector2D(
		bHomeBase ? RotatedShapeHalfExtent : UnrotatedShapeHalfExtent,
		bHomeBase ? RotatedShapeHalfExtent : UnrotatedShapeHalfExtent);
	// The root panel has a Size x Size layout rectangle and is the common
	// anchor for the shape and label.  Include it in the union so rounding and
	// CanvasPanel slot clipping cannot make click and paint disagree at edges.
	Result.HalfExtents = FVector2D(
		FMath::Max(SafeSize * 0.5f, FMath::Max(Result.MarkerHalfExtents.X, Result.LabelHalfExtents.X)),
		FMath::Max(SafeSize * 0.5f, FMath::Max(Result.MarkerHalfExtents.Y, Result.LabelHalfExtents.Y)));
	return Result;
}

FText UMSMapWidgetBase::ResolveCapturePointNumber(const AMSCapturePoint* Point, const int32 FallbackIndex)
{
	const FString Identifier = Point ? Point->GetPointName().ToString() : FString();
	FString Digits;
	for (const TCHAR Character : Identifier) if (FChar::IsDigit(Character)) Digits.AppendChar(Character);
	if (!Digits.IsEmpty())
	{
		return FText::AsNumber(FCString::Atoi(*Digits));
	}
	// 本拠点はA/B/C(自軍)・X/Y/Z(敵軍)の英字1文字がPointNameに入る。数字に潰すと
	// 数字拠点と区別できない(実プレイ報告)ため、英字はそのまま表示する。
	FString Letters;
	for (const TCHAR Character : Identifier)
		if (Character < 128 && FChar::IsAlpha(Character)) Letters.AppendChar(FChar::ToUpper(Character));
	if (!Letters.IsEmpty())
	{
		return FText::FromString(Letters.Left(2));
	}
	return FText::AsNumber(FallbackIndex + 1);
}

FLinearColor UMSMapWidgetBase::ResolveCapturePointColor(const AMSCapturePoint* Point, const FGenericTeamId ViewerTeam)
{
	if (!Point) return MSMapPalette::Neutral;
	if (const AMSHomeBase* Home = Cast<AMSHomeBase>(Point); Home && Home->IsHomeBaseDestroyed()) return MSMapPalette::Destroyed;
	const ETeamAttitude::Type Attitude = UMSTeamStatics::SolveAttitude(ViewerTeam, Point->GetOwnerTeamId());
	if (Attitude == ETeamAttitude::Friendly) return MSMapPalette::Friendly;
	if (Attitude == ETeamAttitude::Hostile) return MSMapPalette::Enemy;
	return MSMapPalette::Neutral;
}

void UMSMapWidgetBase::RefreshPickupActors()
{
	RepairPods.Reset(); RepairPacks.Reset(); AmmoPacks.Reset(); UWorld* World = GetWorld(); if (!World) return;
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, AMSRepairPod::StaticClass(), Found);
	for (AActor* Actor : Found) if (IsValid(Actor)) RepairPods.Add(Actor);
	Found.Reset(); UGameplayStatics::GetAllActorsOfClass(World, AMSSupplyPack::StaticClass(), Found);
	for (AActor* Actor : Found) if (const AMSSupplyPack* Pack = Cast<AMSSupplyPack>(Actor))
	{
		(Pack->SupplyPackType == EMSSupplyPackType::Repair ? RepairPacks : AmmoPacks).Add(Actor);
	}
}

void UMSMapWidgetBase::UpdatePickupMarkers()
{
	if (!ShowPickupMarkers())
	{
		for (UWidget* Marker : RepairPodMarkers) Marker->SetVisibility(ESlateVisibility::Collapsed);
		for (UWidget* Marker : RepairPackMarkers) Marker->SetVisibility(ESlateVisibility::Collapsed);
		for (UWidget* Marker : AmmoPackMarkers) Marker->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	auto UpdatePool = [this](const TArray<TObjectPtr<AActor>>& Actors, const auto& Markers)
	{
		int32 Used = 0;
		for (AActor* Actor : Actors)
		{
			if (!IsValid(Actor) || Used >= Markers.Num()) continue;
			UWidget* Marker = Markers[Used++]; PositionMarker(Marker, ProjectWorldToMap(Actor->GetActorLocation()));
			Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		for (int32 Index = Used; Index < Markers.Num(); ++Index) Markers[Index]->SetVisibility(ESlateVisibility::Collapsed);
	};
	UpdatePool(RepairPods, RepairPodMarkers); UpdatePool(RepairPacks, RepairPackMarkers); UpdatePool(AmmoPacks, AmmoPackMarkers);
}

UCanvasPanel* UMSMapWidgetBase::ConstructBullseyeMarker()
{
	UCanvasPanel* Marker = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* MarkerSlot = MapCanvas->AddChildToCanvas(Marker); MarkerSlot->SetAlignment(FVector2D(0.5f)); MarkerSlot->SetSize(FVector2D(MSMapPalette::RepairPodOuterSizePx));
	ConstructSolidImage(Marker, FVector2D(MSMapPalette::RepairPodOuterSizePx * 0.5f), FVector2D(MSMapPalette::RepairPodOuterSizePx), MSMapPalette::Repair);
	ConstructSolidImage(Marker, FVector2D(MSMapPalette::RepairPodOuterSizePx * 0.5f), FVector2D(MSMapPalette::RepairPodHoleSizePx), MSMapPalette::Background);
	return Marker;
}

UCanvasPanel* UMSMapWidgetBase::ConstructCrossMarker()
{
	UCanvasPanel* Marker = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* MarkerSlot = MapCanvas->AddChildToCanvas(Marker); MarkerSlot->SetAlignment(FVector2D(0.5f)); MarkerSlot->SetSize(FVector2D(MSMapPalette::RepairPackSizePx));
	const FVector2D Center(MSMapPalette::RepairPackSizePx * 0.5f);
	ConstructSolidImage(Marker, Center, FVector2D(6.0f, MSMapPalette::RepairPackSizePx), MSMapPalette::Repair);
	ConstructSolidImage(Marker, Center, FVector2D(MSMapPalette::RepairPackSizePx, 6.0f), MSMapPalette::Repair);
	return Marker;
}

UImage* UMSMapWidgetBase::ConstructDiamondMarker()
{
	UImage* Marker = ConstructSolidImage(MapCanvas, FVector2D::ZeroVector, FVector2D(MSMapPalette::AmmoPackSizePx), MSMapPalette::Ammo);
	Marker->SetRenderTransformAngle(45.0f); return Marker;
}

void UMSMapWidgetBase::PositionMarker(UWidget* Marker, const FVector2D& Position)
{
	if (Marker) if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Marker->Slot)) Slot->SetPosition(Position);
}

void UMSMapWidgetBase::SetMarkerColor(UWidget* Marker, const FLinearColor& Color)
{
	if (UImage* Image = Cast<UImage>(Marker)) Image->SetColorAndOpacity(Color);
}
