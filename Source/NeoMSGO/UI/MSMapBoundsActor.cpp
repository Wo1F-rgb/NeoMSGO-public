#include "UI/MSMapBoundsActor.h"

#include "Components/BoxComponent.h"

AMSMapBoundsActor::AMSMapBoundsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	BoundsPreview = CreateDefaultSubobject<UBoxComponent>(TEXT("MapBoundsPreview"));
	SetRootComponent(BoundsPreview);
	BoundsPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsPreview->SetGenerateOverlapEvents(false);
	BoundsPreview->SetHiddenInGame(true);
	#if WITH_EDITOR
	BoundsPreview->SetIsVisualizationComponent(true);
	#endif
}

void AMSMapBoundsActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// This preview box has no collision, so skip the overlap update: with the default
	// bUpdateOverlaps=true, SetBoxExtent dereferences the owning world's collision data,
	// which is still null while the level is loading and crashed the editor on startup.
	if (IsValid(BoundsPreview))
	{
		const FVector2D HalfExtents = GetMapWorldHalfExtents();
		BoundsPreview->SetBoxExtent(FVector(HalfExtents.X, HalfExtents.Y, 100.0f),
			/*bUpdateOverlaps=*/false);
	}
}


bool AMSMapBoundsActor::IsUndergroundLocation(const FVector& WorldLocation, const bool bWasUnderground) const
{
	if (!bEnableUndergroundLayer || WorldLocation.ContainsNaN()) return false;
	const double ThresholdZ = bWasUnderground
		? FMath::Max(UndergroundEnterZCm, UndergroundExitZCm) : UndergroundEnterZCm;
	// Both thresholds are below Taigan's -2700cm river bed. Low altitude alone
	// never chooses this layer: the point must also occupy authored subway XY.
	if (WorldLocation.Z > ThresholdZ) return false;
	const double Margin = bWasUnderground ? FMath::Max(0.0f, UndergroundXYHysteresisCm) : 0.0;
	for (const FBox& Region : UndergroundRegions)
	{
		if (Region.IsValid && WorldLocation.X >= Region.Min.X - Margin && WorldLocation.X <= Region.Max.X + Margin
			&& WorldLocation.Y >= Region.Min.Y - Margin && WorldLocation.Y <= Region.Max.Y + Margin
			&& WorldLocation.Z >= Region.Min.Z && WorldLocation.Z <= Region.Max.Z) return true;
	}
	return false;
}
