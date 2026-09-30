#include "UI/MSHUDVectorBackdrop.h"
#include "UI/MSHUDWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"

void UMSHUDVectorBackdrop::SetHUD(UMSHUDWidget* InHUD) { HUD = InHUD; }

void UMSHUDVectorBackdrop::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ForceVolatile(true);
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
}

int32 UMSHUDVectorBackdrop::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer,
	const FWidgetStyle& Style, bool bEnabled) const
{
	const int32 Base = Super::NativePaint(Args, Geometry, Culling, Elements, Layer, Style, bEnabled);
	return HUD.IsValid() ? HUD->PaintVideoBackdrop(Args, Geometry, Culling, Elements, Base, Style, bEnabled) : Base;
}
