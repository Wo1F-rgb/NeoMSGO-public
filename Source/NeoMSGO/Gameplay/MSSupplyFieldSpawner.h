// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GameFramework/Actor.h"
#include "MSSupplyFieldSpawner.generated.h"

/** Places the initial team-restricted repair and ammunition supplies around every home base. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSSupplyFieldSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMSSupplyFieldSpawner();

protected:
	virtual void BeginPlay() override;

	/** Unity's 14 m default, scaled to the 18m MS baseline via MSGO::MSScale (≈130.6m effective). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|SupplyField", meta=(ClampMin="0.0", Units="cm"))
	float SpawnDistance = 1400.0f * MSGO::MSScale;

	/** Explicit map centre supports levels whose playable area is not centred on the world origin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|SupplyField")
	FVector MapCenter = FVector::ZeroVector;

private:
	void SpawnSupplies();
	FVector GroundPosition(const FVector& Position, const AActor* HomeBase) const;
};
