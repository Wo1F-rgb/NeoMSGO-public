#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSHUDScopeWidget.generated.h"

/** Code-only sniper scope overlay. The scene remains visible inside the circular aperture. */
UCLASS()
class NEOMSGO_API UMSHUDScopeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 MaskSegments = 128;
	static constexpr int32 ReticleTickCountPerSide = 8;

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
