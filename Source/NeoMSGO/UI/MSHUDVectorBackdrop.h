#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSHUDVectorBackdrop.generated.h"

class UMSHUDWidget;

/** Lowest canvas child: vector instrument plates paint before the live text. */
UCLASS()
class NEOMSGO_API UMSHUDVectorBackdrop : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetHUD(UMSHUDWidget* InHUD);
protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer,
		const FWidgetStyle& Style, bool bEnabled) const override;
private:
	TWeakObjectPtr<UMSHUDWidget> HUD;
};
