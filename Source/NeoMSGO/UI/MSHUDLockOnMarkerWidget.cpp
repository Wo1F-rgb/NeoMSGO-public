#include "UI/MSHUDLockOnMarkerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"

void UMSHUDLockOnMarkerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
}

int32 UMSHUDLockOnMarkerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 PaintLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D C = Size * 0.5f;
	const float Radius = FMath::Min(Size.X, Size.Y) * 0.40f;
	const TArray<FVector2D> Diamond{
		C + FVector2D(0, -Radius), C + FVector2D(Radius, 0),
		C + FVector2D(0, Radius), C + FVector2D(-Radius, 0), C + FVector2D(0, -Radius)};
	auto Draw = [&](const TArray<FVector2D>& Points, FLinearColor Color, float Thickness, int32 Layer)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, Thickness);
	};
	Draw(Diamond, GetOuterColor(), GetOuterThickness(), PaintLayer + 1);
	Draw(Diamond, GetInnerColor(), GetInnerThickness(), PaintLayer + 2);
	for (const FVector2D Direction : {FVector2D(-1,-1), FVector2D(1,-1), FVector2D(1,1), FVector2D(-1,1)})
		Draw({C + Direction * (Radius * 0.74f), C + Direction * (Radius * 1.08f)},
			GetInnerColor(), GetInnerThickness(), PaintLayer + 2);
	// The original distance stays yellow in both lock phases.
	Draw({C + FVector2D(Radius, 6), C + FVector2D(Radius + 83, 6),
		C + FVector2D(Radius + 89, 0)}, MSHUDPalette::LockOnMarkerNormal, 1.5f, PaintLayer + 2);
	return PaintLayer + 2;
}
