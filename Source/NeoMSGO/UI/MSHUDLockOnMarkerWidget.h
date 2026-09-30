#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MSHUDTypes.h"
#include "MSHUDLockOnMarkerWidget.generated.h"

/** Thin yellow/red diamond, diagonal ticks, and a right-hand range leader. */
UCLASS()
class NEOMSGO_API UMSHUDLockOnMarkerWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	float GetOuterThickness() const { return MSHUDPalette::LockOnMarkerOuterThickness; }
	float GetInnerThickness() const { return MSHUDPalette::LockOnMarkerInnerThickness; }
	FLinearColor GetOuterColor() const { return MSHUDPalette::LockOnMarkerOuter; }
	FLinearColor GetInnerColor() const { return bRedLocked ? MSHUDPalette::LockOnMarker : MSHUDPalette::LockOnMarkerNormal; }
	void SetRedLocked(bool bInRedLocked) { if (bRedLocked != bInRedLocked) { bRedLocked = bInRedLocked; Invalidate(EInvalidateWidgetReason::Paint); } }
	bool IsRedLocked() const { return bRedLocked; }

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bRedLocked = false;
};
