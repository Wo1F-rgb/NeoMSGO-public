#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/MSMapTypes.h"
#include "MSMapBoundsActor.generated.h"

class UBoxComponent;
class UTexture2D;

/** Level-authored playable bounds used by the overview capture and projections. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSMapBoundsActor : public AActor
{
	GENERATED_BODY()

public:
	AMSMapBoundsActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	FVector GetMapWorldCenter() const { return GetActorLocation(); }
	float GetMapWorldHalfExtent() const { return FMath::Max(1.0f, WorldHalfExtentCm); }
	FVector2D GetMapWorldHalfExtents() const
	{
		return FVector2D(GetMapWorldHalfExtent(), WorldHalfExtentYCm > 0.0f ? WorldHalfExtentYCm : GetMapWorldHalfExtent());
	}
	float GetCaptureHeight() const { return FMath::Max(1.0f, CaptureHeightCm); }
	bool UsesUnlitOverview() const { return bUseUnlitOverview; }

	/** World X half extent in centimetres (also Y when WorldHalfExtentYCm is zero).
	 * WorldScale 10x (docs/WorldScale_10x_Plan.md W6): デフォルト値はMSMapPalette::WorldHalfExtentCm
	 * (旧20500cm×MSGO::MSScale)に追従。実マップごとの実測値はレベル側のAMSMapBoundsActorインスタンスで上書きされる
	 * (W3以降の別ウェーブが担当)。この既定値は境界アクタを新規配置した直後の保守的な初期値。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map", meta=(ClampMin="1.0", UIMin="1000.0"))
	float WorldHalfExtentCm = MSMapPalette::WorldHalfExtentCm;

	/** Optional world Y half extent. Zero keeps existing square maps unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map", meta=(ClampMin="0.0", UIMin="0.0"))
	float WorldHalfExtentYCm = 0.0f;

	/** Height above the actor centre from which the top-down image is captured. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map", meta=(ClampMin="1.0", UIMin="1000.0"))
	float CaptureHeightCm = 50000.0f;

	/** Opt-in tactical capture for maps whose orthographic lighting obscures terrain.
	 * Affects only overview imagery, never the playable scene's lighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map")
	bool bUseUnlitOverview = false;

	/** Explicit per-level opt-in. Only the Taigan layer importer enables this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground")
	bool bEnableUndergroundLayer = false;

	/** Hard reference saved on the map so the generated layer is included in cook. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground")
	TObjectPtr<UTexture2D> UndergroundOverviewTexture;

	/** Source-derived world-coordinate corridor, entrance and hall boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground")
	TArray<FBox> UndergroundRegions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground")
	float UndergroundEnterZCm = -3500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground")
	float UndergroundExitZCm = -3000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Map|Underground", meta=(ClampMin="0.0"))
	float UndergroundXYHysteresisCm = 300.0f;

	bool IsUndergroundLocation(const FVector& WorldLocation, bool bWasUnderground) const;

private:
	UPROPERTY(VisibleAnywhere, Category="MS|Map")
	TObjectPtr<UBoxComponent> BoundsPreview;
};
