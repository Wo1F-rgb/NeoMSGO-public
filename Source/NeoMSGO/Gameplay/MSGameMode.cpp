// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSGameMode.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Misc/CommandLine.h"

#include "AI/MSBotSpawnManagerComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameInstance.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSMapDefinitionRow.h"
#include "Data/MSBattleRuleRow.h"
#include "Data/MSMobileSuitData.h"
#include "Core/MSScale.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameSession.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSMultiplayerDebugHUD.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Gameplay/MSSupplyFieldSpawner.h"
#include "Player/MSPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "NeoMSGO.h"
#include "Online/MSRoomSubsystem.h"

AMSGameMode::AMSGameMode()
{
	GameStateClass = AMSGameState::StaticClass();
	PlayerStateClass = AMSPlayerState::StaticClass();
	HUDClass = AMSMultiplayerDebugHUD::StaticClass();

	// AMSPlayerCharacterが実際のゲームプレイ機体(GAS/武器/ロックオン/UI起動を全て内包)。
	// テンプレートのBP_ThirdPersonCharacterを流用すると、これら全ての移行実装が
	// 実プレイで一切呼ばれなくなる(P10でのPIE実地検証で発覚)。
	DefaultPawnClass = AMSPlayerCharacter::StaticClass();

	// 入力マッピングはPawn側で登録する。Controllerはログイン名をPlayerStateへ
	// 渡す最小RPCだけを追加し、テンプレートBPの入力は引き続き使用しない。
	PlayerControllerClass = AMSPlayerController::StaticClass();

	// B2: BOT初期配備マネージャ(docs/BotDeployment_Design.md。StartPlayで起動)
	BotSpawnManager = CreateDefaultSubobject<UMSBotSpawnManagerComponent>(TEXT("BotSpawnManager"));
}

void AMSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Menus belong to one player. A listen host must never pause everybody's
	// movement, replication timers, respawns, or match clock while viewing UI.
	bPauseable = GetNetMode() == NM_Standalone;
	bHasResolvedMapDisplayName = false;
	bRoomBattleStarted = false;
	const UMSGameInstance* Instance = GetGameInstance<UMSGameInstance>();
	bRoomHost = Instance && Instance->bPendingRoomHost;
	if (bRoomHost)
	{
		RoomRuleIndex = Instance->PendingRoomSettings.RuleIndex;
		RoomBotCount = FMath::Clamp(Instance->PendingRoomSettings.BotCount, 0, 11);
		bRoomFillTeamsWithBots = Instance->PendingRoomSettings.bFillTeamsWithBots;
		RoomBotDifficulty = static_cast<uint8>(Instance->PendingRoomSettings.BotDifficulty);
	}
	const FString NormalizedOptions = Options.IsEmpty() || Options.StartsWith(TEXT("?"))
		? Options : FString(TEXT("?")) + Options;
	if (UGameplayStatics::ParseOption(NormalizedOptions, TEXT("RoomHost")) == TEXT("1"))
	{
		bRoomHost = true;
		RoomRuleIndex = FMath::Max(0, FCString::Atoi(*UGameplayStatics::ParseOption(NormalizedOptions, TEXT("RuleIndex"))));
		RoomBotCount = FMath::Clamp(FCString::Atoi(*UGameplayStatics::ParseOption(NormalizedOptions, TEXT("BotCount"))), 0, 11);
		RoomBotDifficulty = static_cast<uint8>(FMath::Clamp(
			FCString::Atoi(*UGameplayStatics::ParseOption(NormalizedOptions, TEXT("BotDifficulty"))), 0, 3));
		if (UGameplayStatics::HasOption(NormalizedOptions, TEXT("FillTeamsWithBots")))
			bRoomFillTeamsWithBots = UGameplayStatics::ParseOption(NormalizedOptions, TEXT("FillTeamsWithBots")) == TEXT("1");
	}
	if (bRoomHost && GameSession)
	{
		// UE's default session rejects the 17th human before our account admission
		// runs. Keep that transport-level limit aligned with the room's human seats.
		GameSession->MaxPlayers = bRoomFillTeamsWithBots ? MSRoomPopulation::MaxCombatants
			: MSRoomPopulation::LegacyMaxCombatants - RoomBotCount;
	}
	if (const FMSMapDefinitionRow* Definition = ResolveMapDefinition(MapName, Options, Instance))
	{
		ResolvedMapDisplayName = Definition->DisplayName;
		ResolvedRoomMapId = Definition->MapId;
		bHasResolvedMapDisplayName = true;
	}
}

void AMSGameMode::InitGameState()
{
	Super::InitGameState();
	if (HasAuthority() && bHasResolvedMapDisplayName)
	{
		if (AMSGameState* State = GetGameState<AMSGameState>())
		{
			State->MapDisplayName = ResolvedMapDisplayName;
			State->bPlayerHostedRoom = bRoomHost;
			if (bRoomHost)
			{
				State->HostedRoomSettings.MapId = ResolvedRoomMapId;
				State->HostedRoomSettings.RuleIndex = RoomRuleIndex;
				State->HostedRoomSettings.BotCount = RoomBotCount;
				State->HostedRoomSettings.bFillTeamsWithBots = bRoomFillTeamsWithBots;
				State->HostedRoomSettings.BotDifficulty = static_cast<EMSBotDifficulty>(RoomBotDifficulty);
				if (const auto* Instance = GetGameInstance<UMSGameInstance>()) State->HostedRoomSettings.RoomName = Instance->PendingRoomSettings.RoomName;
			}
		}
	}
}

const FMSMapDefinitionRow* AMSGameMode::ResolveMapDefinition(const FString& MapName,
	const FString& Options, const UMSGameInstance* GameInstance)
{
	const FString NormalizedOptions = Options.IsEmpty() || Options.StartsWith(TEXT("?"))
		? Options : FString(TEXT("?")) + Options;
	const FString OptionMapId = UGameplayStatics::ParseOption(NormalizedOptions, TEXT("MapId"));
	if (!OptionMapId.IsEmpty())
	{
		if (const FMSMapDefinitionRow* Definition = FMSMapDefinitionRow::FindMapById(FName(*OptionMapId)))
		{
			return Definition;
		}
	}
	// World名を先に試す: ロビー経由のOpenLevelは常に?MapIdを付与するため、ここに
	// 到達するのは直接PIE/エディタ起動時のみ。その場合は実際にロードされたレベルの
	// 定義を優先しないと、GameInstance既定値(UnrealizerColony)に誤解決してしまう。
	const FString ShortLevelName = UWorld::RemovePIEPrefix(FPackageName::GetShortName(MapName));
	if (const FMSMapDefinitionRow* Definition = FMSMapDefinitionRow::FindMapByLevelName(ShortLevelName))
	{
		return Definition;
	}
	if (GameInstance)
	{
		if (const FMSMapDefinitionRow* Definition = FMSMapDefinitionRow::FindMapById(GameInstance->SelectedMapId))
		{
			return Definition;
		}
	}
	return nullptr;
}

void AMSGameMode::StartPlay()
{
	NormalizeDaibaoHomeBaseFactions();
	Super::StartPlay();
	if (!HasAuthority()) return;

	if (UMSUnitRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>())
	{
		UnitRegistry = Registry;
		Registry->OnUnitRegistered.AddUObject(this, &AMSGameMode::SubscribeToUnit);
		Registry->ForEachRegisteredUnit([this](AMSUnitBase* Unit) { SubscribeToUnit(Unit); });
	}

	for (TActorIterator<AMSHomeBase> It(GetWorld()); It; ++It)
	{
		AMSHomeBase* HomeBase = *It;
		HomeBases.Add(HomeBase);
		HomeBase->OnHomeBaseDestroyed.AddUniqueDynamic(this, &AMSGameMode::HandleHomeBaseDestroyed);
		HomeBase->OnHomeBaseDamaged.AddUniqueDynamic(this, &AMSGameMode::HandleHomeBaseDamaged);
	}
	UE_LOG(LogNeoMSGO, Display, TEXT("Battle: %d home bases registered for gauge damage"), HomeBases.Num());

	// Some maps predate AMSSupplyFieldSpawner. Keep the level asset untouched and guarantee one
	// runtime spawner; its BeginPlay places one permanent team-owned pod at every home base.
	bool bHasSupplyFieldSpawner = false;
	for (TActorIterator<AMSSupplyFieldSpawner> It(GetWorld()); It; ++It)
	{
		bHasSupplyFieldSpawner = true;
		break;
	}
	if (!bHasSupplyFieldSpawner)
	{
		GetWorld()->SpawnActor<AMSSupplyFieldSpawner>();
	}

	ApplyRules(KillDamage, HomeBaseDestroyDamage, MaxGauge);
	if (bRoomHost)
	{
		const TArray<FMSBattleRuleRow>& Rules = FMSBattleRuleRow::GetBuiltInRules();
		if (Rules.IsValidIndex(RoomRuleIndex))
		{
			const FMSBattleRuleRow& Rule = Rules[RoomRuleIndex];
			ApplyBattleRule(Rule);
			if (AMSGameState* State = GetGameState<AMSGameState>())
			{
				State->SetDuration(Rule.DurationSeconds);
			}
		}
		const EMSEnemyTier Tier = RoomBotDifficulty == static_cast<uint8>(EMSBotDifficulty::Easy)
			? EMSEnemyTier::Weak
			: RoomBotDifficulty == static_cast<uint8>(EMSBotDifficulty::Hard)
				? EMSEnemyTier::Strong : EMSEnemyTier::Medium;
		if (BotSpawnManager)
		{
			BotSpawnManager->SetRoomBotConfiguration(RoomBotCount, Tier, bRoomFillTeamsWithBots,
				RoomBotDifficulty == static_cast<uint8>(EMSBotDifficulty::Mixed));
		}
		UE_LOG(LogNeoMSGO, Display, TEXT("Room: team fill=%s, manual enemy BOTs=%d (difficulty=%d, rule=%d)"),
			bRoomFillTeamsWithBots ? TEXT("25vs25") : TEXT("off"), RoomBotCount, RoomBotDifficulty, RoomRuleIndex);
	}

	// B2: 戦力ゲージ購読(SubscribeToUnit)を確立した後にBOTを初期配備する。
	// スポーンされたBOTはUnitRegistry経由で自動的にゲージルールへ乗る。
	if (BotSpawnManager && !MSOnline::IsServerWorld(GetWorld()) && !IsWaitingForRoomDeployment())
	{
		BotSpawnManager->SpawnInitialBots();
	}
}

bool AMSGameMode::IsWaitingForRoomDeployment() const
{
	return bRoomHost && !bRoomBattleStarted && !MSOnline::IsServerWorld(GetWorld());
}

void AMSGameMode::NotifyRoomDeployment()
{
	if (!HasAuthority() || !IsWaitingForRoomDeployment() || !GetWorld()) return;
	bRoomBattleStarted = true;
	if (BotSpawnManager) BotSpawnManager->SpawnInitialBots();
	UE_LOG(LogNeoMSGO, Display, TEXT("Hosted room battle started after first successful human deployment"));
}

void AMSGameMode::NotifyOnlineDeployment()
{
	if (!HasAuthority() || !MSOnline::IsServerWorld(GetWorld()) || bOnlineBattleStarted) return;
	bOnlineBattleStarted = true;
	if (BotSpawnManager) BotSpawnManager->SpawnInitialBots();
	UE_LOG(LogNeoMSGO, Display, TEXT("Online battle started after first verified deployment"));
}

void AMSGameMode::NormalizeDaibaoHomeBaseFactions()
{
	if (!HasAuthority() || !GetWorld() ||
		!UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).Contains(TEXT("Lvl_Biome_Daibao")))
	{
		return;
	}

	// PointName is authoritative: EFSF owns A/B and ZEON owns X/Y. Do not infer
	// factions from position; map landmarks may be renamed or moved.
	for (TActorIterator<AMSHomeBase> It(GetWorld()); It; ++It)
	{
		AMSHomeBase* Base = *It;
		if (!IsValid(Base)) continue;
		const FString Point = Base->GetPointName().ToString().TrimStartAndEnd();
		const bool bPlayerSide = Point.Equals(TEXT("A"), ESearchCase::IgnoreCase)
			|| Point.Equals(TEXT("B"), ESearchCase::IgnoreCase);
		const bool bEnemySide = Point.Equals(TEXT("X"), ESearchCase::IgnoreCase)
			|| Point.Equals(TEXT("Y"), ESearchCase::IgnoreCase);
		if (!bPlayerSide && !bEnemySide) continue;

		const FGenericTeamId DesiredTeam = bPlayerSide
			? MSTeam::PlayerTeamId() : MSTeam::EnemyTeamId();
		if (Base->GetGenericTeamId() != DesiredTeam)
		{
			UE_LOG(LogNeoMSGO, Display,
				TEXT("Daibao faction normalization: point=%s base=%s %d -> %d"),
				*Point, *GetNameSafe(Base), Base->GetGenericTeamId().GetId(), DesiredTeam.GetId());
			Base->SetGenericTeamId(DesiredTeam);
		}
	}
}

void AMSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (MSOnline::IsServerWorld(GetWorld()))
	{
		if (AMSPlayerController* PC = Cast<AMSPlayerController>(NewPlayer)) PC->BeginOnlineAuthentication();
	}
	const AMSGameState* State = GetGameState<AMSGameState>();
	UE_LOG(LogNeoMSGO, Display, TEXT("Room: player joined; GameState->PlayerArray.Num()=%d"),
		State ? State->PlayerArray.Num() : 0);
}

void AMSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSRoomSubsystem* RoomSubsystem = GameInstance->GetSubsystem<UMSRoomSubsystem>())
		{
			RoomSubsystem->NotifyHostSessionEnded(GetWorld());
		}
	}
	if (UMSUnitRegistrySubsystem* Registry = UnitRegistry.Get())
	{
		Registry->OnUnitRegistered.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AMSGameMode::ApplyRules(const float KillDamageValue, const float HomeBaseDamageValue,
	const float MaxGaugeValue)
{
	if (!HasAuthority()) return;
	KillDamage = FMath::Max(0.0f, KillDamageValue);
	bScaleKillDamageByCost = false;
	HomeBaseDestroyDamage = FMath::Max(0.0f, HomeBaseDamageValue);
	MaxGauge = FMath::Max(0.0f, MaxGaugeValue);
	// Both gauges are refilled below, so the per-base damage ledger has to start over as well.
	HomeBaseGaugeApplied.Reset();
	if (AMSGameState* State = GetGameState<AMSGameState>())
	{
		State->MaxGauge = MaxGauge;
		State->PlayerGauge = MaxGauge;
		State->EnemyGauge = MaxGauge;
		State->bBattleEnded = false;
		State->bPlayerWon = false;
		State->OnGaugeChanged.Broadcast(State->PlayerGauge, State->EnemyGauge);
	}
}

void AMSGameMode::ApplyBattleRule(const FMSBattleRuleRow& Rule)
{
	if (!HasAuthority()) return;
	ApplyRules(Rule.KillDamage, Rule.HomeBaseDamage, Rule.MaxGauge);
	bScaleKillDamageByCost = Rule.bScaleKillDamageByCost;
}

void AMSGameMode::ApplyGaugeDamage(const FGenericTeamId DamagedTeam, const float Amount)
{
	AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded || IsWaitingForRoomDeployment() || Amount <= 0.0f) return;

	if (DamagedTeam == MSTeam::PlayerTeamId())
	{
		State->PlayerGauge = FMath::Clamp(State->PlayerGauge - Amount, 0.0f, State->MaxGauge);
	}
	else if (DamagedTeam == MSTeam::EnemyTeamId())
	{
		State->EnemyGauge = FMath::Clamp(State->EnemyGauge - Amount, 0.0f, State->MaxGauge);
	}
	else
	{
		return;
	}

	State->OnGaugeChanged.Broadcast(State->PlayerGauge, State->EnemyGauge);
	if (State->PlayerGauge <= 0.0f) EndBattle(false);
	else if (State->EnemyGauge <= 0.0f) EndBattle(true);
}

void AMSGameMode::SubscribeToUnit(AMSUnitBase* Unit)
{
	if (!HasAuthority() || !IsValid(Unit)) return;
	if (UMSAttributeSet_Vital* Vital = Unit->GetVitalSet())
	{
		Vital->OnDeath.AddUniqueDynamic(this, &AMSGameMode::HandleUnitDeath);
	}
}

float AMSGameMode::GetDeathGaugeDamage(const int32 UnitCost) const
{
	return KillDamage * (bScaleKillDamageByCost ? FMath::Max(0, UnitCost) / 200.0f : 1.0f);
}

void AMSGameMode::HandleUnitDeath(AActor* DeadUnit, AActor* Killer)
{
	const AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded || IsWaitingForRoomDeployment()) return;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(DeadUnit))
	{
		if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
		{
			const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
			if (DeadUnit == PlayerPawn) Instance->RegisterDeath();
			if (Killer == PlayerPawn && DeadUnit != PlayerPawn) Instance->RegisterKill();
		}
		const UMSMobileSuitData* Suit = Unit->GetMobileSuitData();
		ApplyGaugeDamage(Unit->GetGenericTeamId(), GetDeathGaugeDamage(Suit ? Suit->Cost : 200));
	}
}

void AMSGameMode::HandleHomeBaseDamaged(AMSHomeBase* HomeBase, const EMSCaptureOwner HomeBaseFaction)
{
	const AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded || IsWaitingForRoomDeployment() || !IsValid(HomeBase)) return;
	SyncHomeBaseGaugeDamage(HomeBase, HomeBaseFaction, HomeBase->GetCoreArmorLostRatio());
}

void AMSGameMode::SyncHomeBaseGaugeDamage(AMSHomeBase* HomeBase, const EMSCaptureOwner HomeBaseFaction,
	const float LostRatio)
{
	float& Applied = HomeBaseGaugeApplied.FindOrAdd(HomeBase, 0.0f);
	const float Target = HomeBaseDestroyDamage * FMath::Clamp(LostRatio, 0.0f, 1.0f);
	const float Delta = Target - Applied;
	if (Delta <= 0.0f) return;
	Applied = Target;
	ApplyGaugeDamage(UMSTeamStatics::CaptureOwnerToTeamId(HomeBaseFaction), Delta);
	const AMSGameState* State = GetGameState<AMSGameState>();
	UE_LOG(LogNeoMSGO, Display,
		TEXT("HomeBase gauge: %s lost %.1f%% armor -> -%.1f gauge (Player=%.1f Enemy=%.1f)"),
		*GetNameSafe(HomeBase), FMath::Clamp(LostRatio, 0.0f, 1.0f) * 100.0f, Delta,
		State ? State->PlayerGauge : -1.0f, State ? State->EnemyGauge : -1.0f);
}

void AMSGameMode::HandleHomeBaseDestroyed(AMSHomeBase* HomeBase, const EMSCaptureOwner HomeBaseFaction)
{
	const AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded || IsWaitingForRoomDeployment()) return;

	const FGenericTeamId DamagedTeam = UMSTeamStatics::CaptureOwnerToTeamId(HomeBaseFaction);
	// Charge whatever part of this base's gauge share was not already taken by partial damage.
	SyncHomeBaseGaugeDamage(HomeBase, HomeBaseFaction, 1.0f);
	State = GetGameState<AMSGameState>();
	if (!State || State->bBattleEnded || !AreAllHomeBasesDestroyed(DamagedTeam)) return;
	if (DamagedTeam == MSTeam::PlayerTeamId()) EndBattle(false);
	else if (DamagedTeam == MSTeam::EnemyTeamId()) EndBattle(true);
}

bool AMSGameMode::AreAllHomeBasesDestroyed(const FGenericTeamId Team) const
{
	int32 TeamBaseCount = 0;
	for (const TWeakObjectPtr<AMSHomeBase>& Entry : HomeBases)
	{
		const AMSHomeBase* Base = Entry.Get();
		if (!IsValid(Base) || UMSTeamStatics::CaptureOwnerToTeamId(Base->GetCurrentOwner()) != Team) continue;
		++TeamBaseCount;
		if (!Base->IsHomeBaseDestroyed()) return false;
	}
	return TeamBaseCount > 0;
}

void AMSGameMode::NotifyMatchTimeExpired()
{
	const AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded || IsWaitingForRoomDeployment()) return;
	EndBattle(State->PlayerGauge >= State->EnemyGauge);
}

void AMSGameMode::EndBattle(const bool bInPlayerWon)
{
	AMSGameState* State = GetGameState<AMSGameState>();
	if (!HasAuthority() || !State || State->bBattleEnded) return;
	State->bBattleEnded = true;
	State->bPlayerWon = bInPlayerWon;
	State->OnBattleEnded.Broadcast(bInPlayerWon);
}

AActor* AMSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (AActor* Authored = Super::ChoosePlayerStart_Implementation(Player))
	{
		return Authored;
	}

	// Daibao is generated from real terrain and ships no APlayerStart, so the engine falls back to
	// the world origin, the capsule spawns inside terrain, and RestartPlayer produces no pawn at
	// all.  That is fatal rather than cosmetic: the briefing and spawn-select widgets are created
	// by AMSPlayerCharacter, so a missing pawn silently removes the entire sortie UI.
	NormalizeDaibaoHomeBaseFactions();
	const FGenericTeamId PlayerTeam = UMSTeamStatics::GetTeamId(Player);
	const AMSHomeBase* Fallback = nullptr;
	for (TActorIterator<AMSHomeBase> It(GetWorld()); It; ++It)
	{
		AMSHomeBase* Base = *It;
		if (!IsValid(Base) || Base->IsHomeBaseDestroyed()) continue;
		if (!Fallback) Fallback = Base;
		if (Base->GetGenericTeamId() == PlayerTeam) { Fallback = Base; break; }
	}
	if (!Fallback)
	{
		UE_LOG(LogNeoMSGO, Error,
			TEXT("ChoosePlayerStart: no APlayerStart and no usable home base — the player pawn "
				"cannot spawn and the sortie UI will not appear."));
		return nullptr;
	}

	// Drop in from far above the base rather than at its feet: spawning at base height buries the
	// mech inside the structure on this map (#224).
	constexpr float DropHeightCm = 3200.0f * MSGO::MSScale; // ≈298m
	FVector Origin, Extent;
	Fallback->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
	const FVector SpawnLocation(Origin.X, Origin.Y, Origin.Z + Extent.Z + DropHeightCm);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	FRotator SpawnRotation = (FVector::ZeroVector - SpawnLocation).Rotation();
	SpawnRotation.Pitch = 0.0f;
	SpawnRotation.Roll = 0.0f;
	if (FVector2D(SpawnLocation.X, SpawnLocation.Y).IsNearlyZero())
	{
		SpawnRotation = Fallback->GetActorRotation();
		SpawnRotation.Pitch = 0.0f;
		SpawnRotation.Roll = 0.0f;
	}
	APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(
		APlayerStart::StaticClass(), SpawnLocation, SpawnRotation, Params);

	UE_LOG(LogNeoMSGO, Display,
		TEXT("ChoosePlayerStart: no APlayerStart in %s; falling back to home base %s at %s, "
			"facing battlefield center yaw=%.1f"),
		*GetWorld()->GetMapName(), *GetNameSafe(Fallback), *SpawnLocation.ToCompactString(),
		SpawnRotation.Yaw);
	return Start;
}

void AMSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
 if (MSOnline::IsServerWorld(GetWorld())) return;
 Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}
void AMSGameMode::RestartPlayer(AController* NewPlayer)
{
 if (MSOnline::IsServerWorld(GetWorld()))
 {
  const auto* PC = Cast<AMSPlayerController>(NewPlayer);
  if (!PC || !PC->IsOnlineAuthenticated() || PC->GetPawn() || PC->IsOnlineDeployed()) return;
 }
 Super::RestartPlayer(NewPlayer);
}
APawn* AMSGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& Transform)
{
 if (MSOnline::IsServerWorld(GetWorld()))
 {
  const auto* PC = Cast<AMSPlayerController>(NewPlayer);
  if (!PC || !PC->IsOnlineAuthenticated()) return nullptr;
 }
 return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Transform);
}
