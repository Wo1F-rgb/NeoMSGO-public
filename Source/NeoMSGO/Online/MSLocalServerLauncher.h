#pragma once

#include "CoreMinimal.h"

/** Starts only this installation's local account/list service; never a remote process. */
namespace MSLocalServerLauncher
{
    bool IsAvailable();
    void Start(const FString& AuthBaseUrl, TFunction<void(bool, const FString&)> Completion);
}
