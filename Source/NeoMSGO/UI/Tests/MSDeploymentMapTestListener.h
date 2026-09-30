// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MSDeploymentMapTestListener.generated.h"

/** Dynamic-delegate receiver used by the deployment-map automation tests. */
UCLASS()
class UMSDeploymentMapTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleSpawnPointSelected(FTransform InSpawnTransform)
	{
		++BroadcastCount;
		LastSpawnTransform = InSpawnTransform;
	}

	int32 BroadcastCount = 0;
	FTransform LastSpawnTransform;
};
