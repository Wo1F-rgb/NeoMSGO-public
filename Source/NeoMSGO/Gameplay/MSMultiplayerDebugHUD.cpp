// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSMultiplayerDebugHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "GameFramework/GameStateBase.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AMSMultiplayerDebugHUD::DrawHUD()
{
	Super::DrawHUD();

#if UE_BUILD_SHIPPING
	return;
#else
	// This is an opt-in diagnostic surface.  The HUD class is also used by ordinary
	// gameplay worlds, so a networked net mode alone is not permission to draw it.
	if (!FParse::Param(FCommandLine::Get(), TEXT("MSNetworkDebugOverlay")))
	{
		return;
	}
#endif

	if (!Canvas || !GEngine)
	{
		return;
	}

	// #208/#192 regression fix: this overlay is a room/multiplayer-debug aid only. It must never
	// draw during ordinary single-player play (e.g. a normal Daibao sortie), where it previously
	// always rendered "CONNECTED PLAYERS" because HUDClass is set unconditionally in
	// AMSGameMode's constructor. Standalone/solo games run NM_Standalone with exactly one
	// PlayerState, so gate on NetMode instead of coupling to GameMode's room state.
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Standalone)
	{
		return;
	}

	const AGameStateBase* GameState = World->GetGameState();
	const float X = FMath::Max(20.0f, Canvas->ClipX - 360.0f);
	float Y = 30.0f;
	Canvas->SetDrawColor(FColor(80, 220, 255));
	Canvas->DrawText(GEngine->GetSmallFont(), TEXT("CONNECTED PLAYERS"), X, Y);
	Y += 24.0f;

	if (!GameState)
	{
		Canvas->SetDrawColor(FColor::Silver);
		Canvas->DrawText(GEngine->GetSmallFont(), TEXT("(waiting for GameState)"), X, Y);
		return;
	}

	int32 DisplayIndex = 1;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (!IsValid(PlayerState))
		{
			continue;
		}
		const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState);
		const FString Name = MSPlayerState && !MSPlayerState->GetLoginUsername().IsEmpty()
			? MSPlayerState->GetLoginUsername() : PlayerState->GetPlayerName();
		const AMSUnitBase* Unit = Cast<AMSUnitBase>(PlayerState->GetPawn());
		const UMSAttributeSet_Vital* Vital = Unit ? Unit->GetVitalSet() : nullptr;
		const bool bDestroyed = Vital && Vital->GetArmor() <= 0.0f;
		Canvas->SetDrawColor(bDestroyed ? FColor(255, 80, 80) : FColor::White);
		const FString CombatState = Unit && Vital
			? FString::Printf(TEXT("%s T%d A %.0f/%.0f"), bDestroyed ? TEXT("[DEAD]") : TEXT("[LIVE]"),
				Unit->GetGenericTeamId().GetId(), Vital->GetArmor(), Vital->GetMaxArmor())
			: TEXT(" (pawn pending)");
		Canvas->DrawText(GEngine->GetSmallFont(),
			FString::Printf(TEXT("%d. %s %s"), DisplayIndex++, *CombatState,
				Name.IsEmpty() ? TEXT("(connecting...)") : *Name), X, Y);
		Y += 20.0f;
	}

	if (const AMSGameState* MSState = World->GetGameState<AMSGameState>())
	{
		Canvas->SetDrawColor(FColor(255, 220, 80));
		Canvas->DrawText(GEngine->GetSmallFont(),
			FString::Printf(TEXT("GAUGE %.0f / %.0f (MAX %.0f)"),
				MSState->PlayerGauge, MSState->EnemyGauge, MSState->MaxGauge), X, Y);
		Y += 20.0f;
	}
	int32 PointCount = 0;
	for (TActorIterator<AMSCapturePoint> It(World); It && PointCount < 3; ++It, ++PointCount)
	{
		Canvas->SetDrawColor(FColor(180, 255, 180));
		Canvas->DrawText(GEngine->GetSmallFont(),
			FString::Printf(TEXT("CAP %s %.3f OWNER %d"), *It->GetPointName().ToString(),
				It->GetProgress(), static_cast<int32>(It->GetCurrentOwner())), X, Y);
		Y += 18.0f;
	}
}
