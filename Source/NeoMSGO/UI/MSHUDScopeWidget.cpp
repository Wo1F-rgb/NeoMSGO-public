#include "UI/MSHUDScopeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"
#include "UI/MSHUDTypes.h"

void UMSHUDScopeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
}

int32 UMSHUDScopeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 1.0f || Size.Y <= 1.0f)
	{
		return BaseLayer;
	}

	const FVector2D Center = Size * 0.5f;
	const float Radius = FMath::Max(1.0f, FMath::Min(Size.X, Size.Y) * MSHUDPalette::ScopeRadiusFraction);
	const float BandHeight = Size.Y / static_cast<float>(MaskSegments);
	const FLinearColor MaskColor = MSHUDPalette::ScopeMask;

	// Horizontal bands cover everything outside x^2+y^2=r^2. Using many bands keeps the
	// aperture circular without a texture, material, or imported mask asset.
	for (int32 Segment = 0; Segment < MaskSegments; ++Segment)
	{
		const float Y = (static_cast<float>(Segment) + 0.5f) * BandHeight;
		const float RelativeY = Y - Center.Y;
		const float HalfChord = FMath::Abs(RelativeY) < Radius
			? FMath::Sqrt(FMath::Max(0.0f, Radius * Radius - RelativeY * RelativeY))
			: 0.0f;
		const float LeftEdge = Center.X - HalfChord;
		const float RightEdge = Center.X + HalfChord;
		const float Thickness = BandHeight + 1.0f;
		if (LeftEdge > 0.0f)
		{
			const TArray<FVector2D> LeftBand{{0.0f, Y}, {LeftEdge, Y}};
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(),
				LeftBand, ESlateDrawEffect::None, MaskColor, false, Thickness);
		}
		if (RightEdge < Size.X)
		{
			const TArray<FVector2D> RightBand{{RightEdge, Y}, {Size.X, Y}};
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(),
				RightBand, ESlateDrawEffect::None, MaskColor, false, Thickness);
		}
	}

	auto MakeCircle = [&Center](const float InRadius)
	{
		TArray<FVector2D> Points;
		Points.Reserve(MaskSegments + 1);
		for (int32 Index = 0; Index <= MaskSegments; ++Index)
		{
			const float Angle = 2.0f * UE_PI * static_cast<float>(Index) / static_cast<float>(MaskSegments);
			Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * InRadius);
		}
		return Points;
	};
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2, AllottedGeometry.ToPaintGeometry(),
		MakeCircle(Radius), ESlateDrawEffect::None, MSHUDPalette::ScopeOutline, true,
		MSHUDPalette::ScopeOutlineThickness);

	const float Gap = MSHUDPalette::ScopeCenterGap;
	const float LineExtent = Radius * MSHUDPalette::ScopeReticleExtentFraction;
	const FLinearColor ReticleColor = MSHUDPalette::ScopeReticle;
	auto DrawLine = [&](const FVector2D& Start, const FVector2D& End, const float Thickness)
	{
		const TArray<FVector2D> Points{Start, End};
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 3, AllottedGeometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, ReticleColor, true, Thickness);
	};
	DrawLine(Center + FVector2D(-LineExtent, 0.0f), Center + FVector2D(-Gap, 0.0f), MSHUDPalette::ScopeLineThickness);
	DrawLine(Center + FVector2D(Gap, 0.0f), Center + FVector2D(LineExtent, 0.0f), MSHUDPalette::ScopeLineThickness);
	DrawLine(Center + FVector2D(0.0f, -LineExtent), Center + FVector2D(0.0f, -Gap), MSHUDPalette::ScopeLineThickness);
	DrawLine(Center + FVector2D(0.0f, Gap), Center + FVector2D(0.0f, LineExtent), MSHUDPalette::ScopeLineThickness);

	const float TickSpacing = Radius * MSHUDPalette::ScopeTickSpacingFraction;
	for (int32 Tick = 1; Tick <= ReticleTickCountPerSide; ++Tick)
	{
		const float Offset = TickSpacing * Tick;
		const float TickHalf = Tick % 2 == 0 ? MSHUDPalette::ScopeMajorTickHalfSize : MSHUDPalette::ScopeMinorTickHalfSize;
		for (const float Sign : {-1.0f, 1.0f})
		{
			DrawLine(Center + FVector2D(Sign * Offset, -TickHalf), Center + FVector2D(Sign * Offset, TickHalf), 1.0f);
			DrawLine(Center + FVector2D(-TickHalf, Sign * Offset), Center + FVector2D(TickHalf, Sign * Offset), 1.0f);
		}
	}

	return BaseLayer + 3;
}
