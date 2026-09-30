// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "MSDamageFeedbackWidget.generated.h"

/**
 * Local received-damage overlay: translucent red screen edges and a directional chevron.
 * It never captures input and its capped opacity keeps the combat view readable.
 */
UCLASS()
class NEOMSGO_API UMSDamageFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowDamage(const FVector2D& ScreenDirection, float Severity);

	float GetFeedbackAlpha() const;
	float GetPeakIntensity() const { return PeakIntensity; }
	FVector2D GetDamageDirection() const { return DamageDirection; }
	static constexpr float FeedbackDurationSeconds = 0.58f;
	static constexpr float MaximumEdgeOpacity = 0.34f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float ElapsedSeconds = FeedbackDurationSeconds;
	float PeakIntensity = 0.0f;
	FVector2D DamageDirection = FVector2D(0.0f, -1.0f);
};
