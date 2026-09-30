#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GenericTeamAgentInterface.h"
#include "UI/MSMapTypes.h"
#include "MSMapWidgetBase.generated.h"

class AMSCapturePoint;
class UWorld;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UWidgetTree;

/** The common compound capture-point marker shared by the full map and HUD minimap. */
struct NEOMSGO_API FMSMapCapturePointMarker
{
	UCanvasPanel* Root = nullptr;
	UImage* Shape = nullptr;
	UTextBlock* Number = nullptr;
};

/**
 * Layout-space bounds shared by the drawn capture marker and its label.
 *
 * Map input is converted to MapCanvas local space before hit testing.  Keeping
 * this small value type in the common map widget makes that hit target use the
 * same design-space dimensions as ConstructCapturePointMarker, including the
 * rotated home-base diamond and text outline.  It deliberately remains
 * independent of Slate geometry so it is also usable by the deployment map's
 * static click resolver and by headless tests.
 */
struct NEOMSGO_API FMSMapCapturePointInteractionBounds
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D MarkerHalfExtents = FVector2D::ZeroVector;
	FVector2D LabelHalfExtents = FVector2D::ZeroVector;
	FVector2D HalfExtents = FVector2D::ZeroVector;

	bool Contains(const FVector2D& Local) const
	{
		const FVector2D Delta = Local - Center;
		return FMath::Abs(Delta.X) <= HalfExtents.X && FMath::Abs(Delta.Y) <= HalfExtents.Y;
	}
};

/** One of the four authored Taigan ramp endpoints, identified by saved actor tags. */
struct NEOMSGO_API FMSMapEntranceLocation
{
	int32 Number = 0;
	FVector Location = FVector::ZeroVector;
};

struct NEOMSGO_API FMSMapEntranceMarker
{
	UCanvasPanel* Root = nullptr;
	UImage* Shape = nullptr;
	UTextBlock* Number = nullptr;
};

UCLASS(Abstract)
class NEOMSGO_API UMSMapWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="MS|Map")
	static FVector2D WorldToMapLocal(const FVector& World, float WorldHalfExtentCm,
		const FVector2D& MapSizePx, float EdgeMarginPx);

	UFUNCTION(BlueprintPure, Category="MS|Map")
	static FVector MapLocalToWorld(const FVector2D& Local, float WorldHalfExtentCm,
		const FVector2D& MapSizePx, float EdgeMarginPx);

	static FVector2D WorldToMapLocalCentered(const FVector& World, const FVector& WorldCenter,
		float WorldHalfExtentCm, const FVector2D& MapSizePx, float EdgeMarginPx);
	static FVector MapLocalToWorldCentered(const FVector2D& Local, const FVector& WorldCenter,
		float WorldHalfExtentCm, const FVector2D& MapSizePx, float EdgeMarginPx);
	static FVector2D WorldToMapLocalRectCentered(const FVector& World, const FVector& WorldCenter,
		const FVector2D& WorldHalfExtentsCm, const FVector2D& MapSizePx, float EdgeMarginPx);
	static FVector MapLocalToWorldRectCentered(const FVector2D& Local, const FVector& WorldCenter,
		const FVector2D& WorldHalfExtentsCm, const FVector2D& MapSizePx, float EdgeMarginPx);
	/** Largest undistorted overview rectangle inside a map canvas. */
	static FVector2D GetRectContentSize(const FVector2D& WorldHalfExtentsCm,
		const FVector2D& MapSizePx, float EdgeMarginPx);
	/** Briefing uses a smaller icon while preserving a readable digit. */
	static float GetCaptureMarkerSizeForCanvas(float StandardSizePx, const FVector2D& ContentSizePx);
	/** Only the four existing tagged ramp pairs are eligible. No actor labels are needed in cooked builds. */
	static TArray<FMSMapEntranceLocation> GatherEntranceLocations(UWorld* World, bool bUnderground);
	static FLinearColor FriendlyDotColor(uint8 BotTier);
	static FMSMapEntranceMarker ConstructEntranceMarker(UWidgetTree* Tree, UCanvasPanel* Parent,
		int32 Number, float SizePx);
	/** North-up atlas crop for a square player-centred local view, before the UI's 180° rotation. */
	static FBox2d GetLocalAtlasUVRegion(const FVector& PlayerLocation, const FVector& WorldCenter,
		const FVector2D& WorldHalfExtentsCm, float OrthoWidthCm);

	UFUNCTION(BlueprintCallable, Category="MS|Map")
	virtual void Open();

	UFUNCTION(BlueprintCallable, Category="MS|Map")
	virtual void Close();

	UFUNCTION(BlueprintPure, Category="MS|Map")
	bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	/** Creates an upright white number over a team-coloured circle (point) or diamond (home base). */
	static FMSMapCapturePointMarker ConstructCapturePointMarker(UWidgetTree* Tree, UCanvasPanel* Parent,
		const FVector2D& Position, float Size, const FText& Number, bool bHomeBase);
	/**
	 * Returns the axis-aligned MapCanvas rectangle occupied by the marker and
	 * its visible label.  Callers that resolve pointer input must use this
	 * rectangle rather than a separate marker radius.
	 */
	static FMSMapCapturePointInteractionBounds GetCapturePointInteractionBounds(
		const FVector2D& Position, float Size, const FText& Number, bool bHomeBase);
	/** Uses authored digits when available, otherwise the deterministic one-based marker index. */
	static FText ResolveCapturePointNumber(const AMSCapturePoint* Point, int32 FallbackIndex);
	/** Shared ownership/destruction colour policy for all map surfaces. */
	static FLinearColor ResolveCapturePointColor(const AMSCapturePoint* Point, FGenericTeamId ViewerTeam);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FVector2D GetMapSize() const PURE_VIRTUAL(UMSMapWidgetBase::GetMapSize, return FVector2D::ZeroVector;);
	virtual float GetMapEdgeMarginPx() const { return MSMapPalette::EdgeMarginPx; }
	virtual bool ShowEntranceMarkers() const { return false; }
	virtual bool ShowPickupMarkers() const { return true; }
	virtual FVector GetMapWorldCenter() const;
	virtual float GetMapWorldHalfExtent() const;
	virtual FVector2D GetMapWorldHalfExtents() const;
	FVector2D GetMapContentSize() const;
	FVector2D ProjectWorldToMap(const FVector& World) const;
	FVector UnprojectMapToWorld(const FVector2D& Local) const;

	UCanvasPanel* GetMapCanvas() const { return MapCanvas; }
	const TArray<TObjectPtr<AMSCapturePoint>>& GetCachedCapturePoints() const { return CapturePoints; }
	void CacheCapturePointMarkers();
	void RefreshCapturePointMarkers();
	/** Embedded map surfaces share the overview without adding another viewport widget. */
	void RefreshOverviewBackground();
	void RefreshEntranceMarkers();
	bool IsUndergroundMapLayer() const { return bUndergroundMapLayer; }
	bool HasEntranceMarkers() const { return bHasEntranceMarkers; }
	void RefreshPickupActors();
	void UpdatePickupMarkers();
	UCanvasPanel* ConstructBullseyeMarker();
	UCanvasPanel* ConstructCrossMarker();
	UImage* ConstructDiamondMarker();
	static void PositionMarker(UWidget* Marker, const FVector2D& Position);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MapCanvas;
	UPROPERTY(Transient) TObjectPtr<UImage> BackgroundImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MapGridLines;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> EntranceMarkerRoots;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> EntranceMarkerShapes;

private:
	UImage* ConstructSolidImage(UCanvasPanel* Parent, const FVector2D& Position,
		const FVector2D& Size, const FLinearColor& Color);
	void SetMarkerColor(UWidget* Marker, const FLinearColor& Color);
	UPROPERTY(Transient) TArray<TObjectPtr<AMSCapturePoint>> CapturePoints;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> CapturePointMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CapturePointMarkerShapes;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> RepairPods;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> RepairPacks;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> AmmoPacks;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> RepairPodMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> RepairPackMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> AmmoPackMarkers;
	float PollAccumulator = 0.0f;
	bool bUndergroundMapLayer = false;
	bool bHasEntranceMarkers = false;
};
