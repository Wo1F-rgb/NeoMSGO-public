#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GenericTeamAgentInterface.h"
#include "Gameplay/MSPlayerController.h"
#include "MSMapRadarRangeWidget.generated.h"

/** Draws friendly radar range in map coordinates without adding thousands of UMG children. */
UCLASS()
class NEOMSGO_API UMSMapRadarRangeWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetOverviewView(const FVector& InWorldCenter, const FVector2D& InHalfExtentsCm,
		const FVector2D& InCanvasSize, FGenericTeamId InViewerTeam,
		const TArray<FMSMapRadarRange>& InRadars);
	void SetLocalView(const FVector& InWorldCenter, float InDiameterCm,
		const FVector2D& InCanvasSize, FGenericTeamId InViewerTeam,
		const TArray<FMSMapRadarRange>& InRadars);

	/** Pure geometry used by the renderer and headless clipping regression tests. */
	static TArray<TArray<FVector2D>> BuildClippedRingPaths(const FVector2D& CenterPx,
		float RadiusPx, const FVector2D& CanvasSize, const FVector2D& ContentSize,
		bool bCircleClip);
	static float OverviewPixelsPerCm(const FVector2D& HalfExtentsCm,
		const FVector2D& CanvasSize);
	static FVector2D ProjectCenterUnclamped(const FVector& WorldPosition,
		const FVector& WorldCenter, const FVector2D& HalfExtentsCm,
		const FVector2D& CanvasSize, bool bLocalCircle);

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FVector WorldCenter = FVector::ZeroVector;
	FVector2D WorldHalfExtentsCm = FVector2D::ZeroVector;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FGenericTeamId ViewerTeam = FGenericTeamId::NoTeam;
	TArray<FMSMapRadarRange> RadarRanges;
	bool bLocalCircle = false;
};
