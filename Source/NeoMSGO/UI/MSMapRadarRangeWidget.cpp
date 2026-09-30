#include "UI/MSMapRadarRangeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"
#include "UI/MSMapWidgetBase.h"
#include "UI/MSMapTypes.h"

namespace
{
	constexpr int32 RingSegments = 96;
	const FLinearColor FriendlyRadarColor(0.16f, 0.95f, 0.35f, 0.72f);
}

void UMSMapRadarRangeWidget::SetOverviewView(const FVector& InWorldCenter,
	const FVector2D& InHalfExtentsCm, const FVector2D& InCanvasSize,
	const FGenericTeamId InViewerTeam, const TArray<FMSMapRadarRange>& InRadars)
{
	WorldCenter = InWorldCenter;
	WorldHalfExtentsCm = InHalfExtentsCm;
	CanvasSize = InCanvasSize;
	ViewerTeam = InViewerTeam;
	RadarRanges = InRadars;
	bLocalCircle = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UMSMapRadarRangeWidget::SetLocalView(const FVector& InWorldCenter,
	const float InDiameterCm, const FVector2D& InCanvasSize,
	const FGenericTeamId InViewerTeam, const TArray<FMSMapRadarRange>& InRadars)
{
	WorldCenter = InWorldCenter;
	WorldHalfExtentsCm = FVector2D(InDiameterCm * 0.5f);
	CanvasSize = InCanvasSize;
	ViewerTeam = InViewerTeam;
	RadarRanges = InRadars;
	bLocalCircle = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

float UMSMapRadarRangeWidget::OverviewPixelsPerCm(const FVector2D& HalfExtentsCm,
	const FVector2D& InCanvasSize)
{
	if (HalfExtentsCm.X <= 0.0f || HalfExtentsCm.Y <= 0.0f) return 0.0f;
	const FVector2D Content = UMSMapWidgetBase::GetRectContentSize(HalfExtentsCm, InCanvasSize, 0.0f);
	return Content.X / (HalfExtentsCm.Y * 2.0f);
}

FVector2D UMSMapRadarRangeWidget::ProjectCenterUnclamped(const FVector& WorldPosition,
	const FVector& InWorldCenter, const FVector2D& HalfExtentsCm,
	const FVector2D& InCanvasSize, const bool bLocalCircle)
{
	const FVector Delta = WorldPosition - InWorldCenter;
	const FVector NorthUp = Delta.RotateAngleAxis(-MSMapPalette::WorldNorthYawDeg, FVector::UpVector);
	const float PixelsPerCm = bLocalCircle
		? InCanvasSize.X / FMath::Max(2.0f * HalfExtentsCm.Y, KINDA_SMALL_NUMBER)
		: OverviewPixelsPerCm(HalfExtentsCm, InCanvasSize);
	return InCanvasSize * 0.5f + FVector2D(NorthUp.Y, -NorthUp.X) * PixelsPerCm;
}

TArray<TArray<FVector2D>> UMSMapRadarRangeWidget::BuildClippedRingPaths(
	const FVector2D& CenterPx, const float RadiusPx, const FVector2D& InCanvasSize,
	const FVector2D& ContentSize, const bool bCircleClip)
{
	TArray<TArray<FVector2D>> Paths;
	if (RadiusPx <= 0.0f || InCanvasSize.X <= 0.0f || InCanvasSize.Y <= 0.0f ||
		ContentSize.X <= 0.0f || ContentSize.Y <= 0.0f) return Paths;
	const FVector2D CanvasCenter = InCanvasSize * 0.5f;
	const FVector2D HalfContent = ContentSize * 0.5f;
	const float CircleRadius = FMath::Max(0.0f, FMath::Min(InCanvasSize.X, InCanvasSize.Y) * 0.5f - 3.0f);
	auto IsInside = [&](const FVector2D& Point)
	{
		const FVector2D Delta = Point - CanvasCenter;
		return FMath::Abs(Delta.X) <= HalfContent.X && FMath::Abs(Delta.Y) <= HalfContent.Y &&
			(!bCircleClip || Delta.SizeSquared() <= FMath::Square(CircleRadius));
	};
	TArray<FVector2D> Current;
	for (int32 Index = 0; Index < RingSegments; ++Index)
	{
		const float A = 2.0f * UE_PI * static_cast<float>(Index) / RingSegments;
		const float B = 2.0f * UE_PI * static_cast<float>(Index + 1) / RingSegments;
		const FVector2D Start = CenterPx + FVector2D(FMath::Cos(A), FMath::Sin(A)) * RadiusPx;
		const FVector2D End = CenterPx + FVector2D(FMath::Cos(B), FMath::Sin(B)) * RadiusPx;
		if (IsInside(Start) && IsInside(End))
		{
			if (Current.IsEmpty()) Current.Add(Start);
			Current.Add(End);
		}
		else if (Current.Num() >= 2)
		{
			Paths.Add(MoveTemp(Current));
			Current.Reset();
		}
	}
	if (Current.Num() >= 2) Paths.Add(MoveTemp(Current));
	return Paths;
}

void UMSMapRadarRangeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	SetIsFocusable(false);
	SetClipping(EWidgetClipping::ClipToBounds);
}

int32 UMSMapRadarRangeWidget::NativePaint(const FPaintArgs& Args,
	const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (ViewerTeam == FGenericTeamId::NoTeam ||
		WorldHalfExtentsCm.X <= 0.0f || WorldHalfExtentsCm.Y <= 0.0f) return BaseLayer;
	const FVector2D ActualSize = AllottedGeometry.GetLocalSize();
	const FVector2D ContentSize = bLocalCircle ? ActualSize :
		UMSMapWidgetBase::GetRectContentSize(WorldHalfExtentsCm, ActualSize, 0.0f);
	const float PixelsPerCm = bLocalCircle
		? ActualSize.X / (WorldHalfExtentsCm.Y * 2.0f)
		: OverviewPixelsPerCm(WorldHalfExtentsCm, ActualSize);
	for (const FMSMapRadarRange& Radar : RadarRanges)
	{
		if (Radar.RadarId == 0 || !FMath::IsFinite(Radar.RadiusCm) || Radar.RadiusCm <= 0.0f ||
			Radar.Center.ContainsNaN()) continue;
		const FVector2D Center = ProjectCenterUnclamped(Radar.Center, WorldCenter,
			WorldHalfExtentsCm, ActualSize, bLocalCircle);
		const float RadiusPx = Radar.RadiusCm * PixelsPerCm;
		for (const TArray<FVector2D>& Path : BuildClippedRingPaths(Center, RadiusPx,
			ActualSize, ContentSize, bLocalCircle))
		{
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1,
				AllottedGeometry.ToPaintGeometry(), Path, ESlateDrawEffect::None,
				FriendlyRadarColor, true, 1.6f);
		}
	}
	return BaseLayer + 1;
}
