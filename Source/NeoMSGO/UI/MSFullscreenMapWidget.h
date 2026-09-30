#pragma once

#include "CoreMinimal.h"
#include "UI/MSMapWidgetBase.h"
#include "MSFullscreenMapWidget.generated.h"

class UCanvasPanel;
class UImage;
class UProgressBar;
class UTextBlock;
class UMSMapRadarRangeWidget;

UCLASS()
class NEOMSGO_API UMSFullscreenMapWidget : public UMSMapWidgetBase
{
	GENERATED_BODY()

public:
	static FVector2D GetLayoutMapSize(const FVector2D& ViewportSize);
	virtual void Open() override;
	virtual void Close() override;

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FVector2D GetMapSize() const override;
	virtual float GetMapEdgeMarginPx() const override { return 0.0f; }
	virtual bool ShowEntranceMarkers() const override { return true; }

private:
	void BuildHeader();
	void RefreshHeader();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> PlayerMarker;
	UPROPERTY(Transient) TObjectPtr<UMSMapRadarRangeWidget> RadarRangeLayer;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> UnitMarkers;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RemainingTimeText;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> PlayerGaugeBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> EnemyGaugeBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NorthText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EntranceLegendText;
	float HeaderRefreshAccumulator = 0.0f;
};
