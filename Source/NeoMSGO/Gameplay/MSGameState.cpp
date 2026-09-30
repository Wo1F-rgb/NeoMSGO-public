// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSGameState.h"

#include "Audio/MSBgmSubsystem.h"
#include "Core/MSGameInstance.h"
#include "Engine/GameInstance.h"
#include "Gameplay/MSGameMode.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Player/MSCharacterMovementComponent.h"
#include "TimerManager.h"

AMSGameState::AMSGameState()
{
	MapDisplayName = FText::FromString(TEXT("アンリアライザコロニー"));
}

void AMSGameState::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GI = GetGameInstance())
	{
		// The default project map is also used behind the title/login frontend.
		// Do not overwrite Title BGM before authentication. A travelled-in
		// authenticated battle starts here; the no-travel flow also starts Battle
		// from UMSDeploymentMapWidget when deployment is confirmed.
		const UMSGameInstance* MSInstance = Cast<UMSGameInstance>(GI);
		if (MSInstance && MSInstance->IsLoggedIn())
		{
			if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			{
				Bgm->PlayScene(EMSBgmScene::Battle);
			}
		}
	}
	ApplyZeroGravityMapToUnits();
	if (HasAuthority())
	{
		OnBattleEnded.AddUniqueDynamic(this, &AMSGameState::HandleBattleEndedForTimer);
		StartMatchTimer();
	}
}

void AMSGameState::SetZeroGravityMap(const bool bEnabled)
{
	if (!HasAuthority() || bZeroGravityMap == bEnabled) return;
	bZeroGravityMap = bEnabled;
	ApplyZeroGravityMapToUnits();
	ForceNetUpdate();
}

void AMSGameState::OnRep_ZeroGravityMap()
{
	ApplyZeroGravityMapToUnits();
}

void AMSGameState::ApplyZeroGravityMapToUnits()
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (UMSCharacterMovementComponent* Movement =
			Cast<UMSCharacterMovementComponent>(It->GetCharacterMovement()))
		{
			Movement->SetZeroGravityEnabled(bZeroGravityMap);
		}
	}
}

void AMSGameState::SetDuration(const float Seconds)
{
	if (!HasAuthority()) return;

	MatchDuration = FMath::Max(0.0f, Seconds);
	RemainingTime = MatchDuration;
	bMatchTimeExpiredNotified = false;
	OnRemainingTimeChanged.Broadcast(RemainingTime);

	StopMatchTimer();
	if (!bBattleEnded)
	{
		StartMatchTimer();
	}
}

FText AMSGameState::GetFormattedRemainingTime() const
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(RemainingTime));
	return FText::FromString(FString::Printf(TEXT("%02d'%02d"), TotalSeconds / 60, TotalSeconds % 60));
}

void AMSGameState::StartMatchTimer()
{
	if (!HasAuthority() || bBattleEnded || GetWorldTimerManager().IsTimerActive(MatchTimerHandle)) return;
	GetWorldTimerManager().SetTimer(MatchTimerHandle, this, &AMSGameState::HandleMatchTimerTick, 1.0f, true);
}

void AMSGameState::HandleMatchTimerTick()
{
	if (!HasAuthority() || bBattleEnded)
	{
		StopMatchTimer();
		return;
	}

	if (MSOnline::IsServerWorld(GetWorld()))
	{
		const AMSGameMode* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>();
		if (!Mode || !Mode->HasOnlineBattleStarted()) return;
	}
	else
	{
		const AMSGameMode* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>();
		if (Mode && Mode->IsWaitingForRoomDeployment()) return;
	}
	RemainingTime = FMath::Max(0.0f, RemainingTime - 1.0f);
	OnRemainingTimeChanged.Broadcast(RemainingTime);
	if (RemainingTime > 0.0f && RemainingTime <= 120.0f)
	{
		if (UGameInstance* GI = GetGameInstance())
			if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
				Bgm->PlayScene(EMSBgmScene::BattleFinale);
	}
	if (RemainingTime > 0.0f || bMatchTimeExpiredNotified) return;

	bMatchTimeExpiredNotified = true;
	StopMatchTimer();
	if (AMSGameMode* GameMode = GetWorld()->GetAuthGameMode<AMSGameMode>())
	{
		GameMode->NotifyMatchTimeExpired();
	}
}

void AMSGameState::StopMatchTimer()
{
	GetWorldTimerManager().ClearTimer(MatchTimerHandle);
}

void AMSGameState::HandleBattleEndedForTimer(bool)
{
	StopMatchTimer();
}

void AMSGameState::OnRep_Gauges()
{
	OnGaugeChanged.Broadcast(PlayerGauge, EnemyGauge);
}

void AMSGameState::OnRep_BattleEnded()
{
	if (bBattleEnded)
	{
		StopMatchTimer();
		OnBattleEnded.Broadcast(bPlayerWon);
	}
}

void AMSGameState::OnRep_RemainingTime()
{
	OnRemainingTimeChanged.Broadcast(RemainingTime);
	if (!bBattleEnded && RemainingTime > 0.0f && RemainingTime <= 120.0f)
	{
		if (UGameInstance* GI = GetGameInstance())
			if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
				Bgm->PlayScene(EMSBgmScene::BattleFinale);
	}
}

void AMSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSGameState, PlayerGauge);
	DOREPLIFETIME(AMSGameState, EnemyGauge);
	DOREPLIFETIME(AMSGameState, MaxGauge);
	DOREPLIFETIME(AMSGameState, bBattleEnded);
	DOREPLIFETIME(AMSGameState, bPlayerWon);
	DOREPLIFETIME(AMSGameState, RemainingTime);
	DOREPLIFETIME(AMSGameState, MapDisplayName);
	DOREPLIFETIME(AMSGameState, bPlayerHostedRoom);
	DOREPLIFETIME(AMSGameState, HostedRoomSettings);
	DOREPLIFETIME(AMSGameState, bZeroGravityMap);
}
