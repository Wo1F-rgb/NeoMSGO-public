#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MSRoomTestListener.generated.h"

UCLASS()
class UMSRoomTestListener : public UObject
{
	GENERATED_BODY()
public:
	int32 TicketCompletions = 0;
	UFUNCTION() void HandleRequest(FName Operation, bool bSuccess, FText Message, int64 Generation)
	{
		if (Operation == TEXT("game_ticket")) ++TicketCompletions;
	}
};
