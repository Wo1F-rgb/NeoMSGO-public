// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MSMultiplayerDebugHUD.generated.h"

/**
 * Minimal presence overlay used until the multiplayer lobby UI is implemented.
 * NOTE (2026-07-21, #208 fix): AMSGameMode sets this as HUDClass unconditionally for every
 * match, including ordinary single-player sorties. DrawHUD() therefore no-ops on
 * NM_Standalone so it only ever draws for an actual room/multiplayer session (listen host,
 * client, or dedicated server), never for a normal solo player.
 */
UCLASS()
class NEOMSGO_API AMSMultiplayerDebugHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
