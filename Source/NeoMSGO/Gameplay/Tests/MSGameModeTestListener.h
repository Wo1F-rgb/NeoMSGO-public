// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MSGameModeTestListener.generated.h"

/** Dynamic-delegate receiver used by the P7-3 automation test. */
UCLASS()
class UMSGameModeTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleBattleEnded(bool bWon)
	{
		++BroadcastCount;
		bLastPlayerWon = bWon;
	}

	int32 BroadcastCount = 0;
	bool bLastPlayerWon = false;
};
