// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Gameplay/MSPlayerController.h"
#include "InputActionValue.h"
#include "MSDiscreteInputTests.generated.h"

/** Exposes engine evaluation for automation; does not override input routing or switch weapons. */
UCLASS()
class AMSDiscreteInputTestController : public AMSPlayerController
{
 GENERATED_BODY()
public:
 void EvaluateDiscreteInputFrame(float DeltaSeconds) { ProcessPlayerInput(DeltaSeconds, false); }
 bool HasPawnInputInEngineStack(UInputComponent* Component)
 {
  TArray<UInputComponent*> Stack; BuildInputStack(Stack); return Stack.Contains(Component);
 }
 void ObserveWeapon2Started(const FInputActionValue&) { ++Weapon2StartedCount; }
 int32 Weapon2StartedCount = 0;
};
