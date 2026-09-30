// Copyright NeoMSGO Project. All Rights Reserved.

#include "AI/MSBotSpawnManagerComponent.h"

#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnPoint.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSDummyNameLibrary.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Gameplay/MSHomeBase.h"
#include "NavigationSystem.h"
#include "NeoMSGO.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Gameplay/MSPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "TimerManager.h"

namespace
{
	constexpr float TaiganMarkerFloorToleranceCm = 500.0f;
	constexpr float TaiganMarkerScatterLimitCm = 3000.0f;
	constexpr float TaiganNavProjectionXYCm = 1000.0f;
	constexpr float HumanDropColumnMarginCm = 200.0f;

	bool IsTaiganBotSpawnMap(const UWorld* World)
	{
		return World && World->GetMapName().EndsWith(TEXT("Lvl_NewYork_Taigan"));
	}

	bool IsTaiganUndergroundBotSpawn(const UWorld* World, const FVector& Location)
	{
		return IsTaiganBotSpawnMap(World) && Location.Z < -4500.0f;
	}
}

bool UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(const float MarkerCenterZ,
	const float GroundZ, const float CapsuleHalfHeight)
{
	// The authored marker denotes the standing capsule centre, not an arbitrary
	// roof with NavMesh inside the generous legacy projection volume.
	return FMath::Abs(GroundZ - (MarkerCenterZ - CapsuleHalfHeight))
		<= TaiganMarkerFloorToleranceCm;
}

float UMSBotSpawnManagerComponent::GetTaiganSpawnScatterRadius(const float ConfiguredRadius)
{
	return FMath::Min(FMath::Max(0.0f, ConfiguredRadius), TaiganMarkerScatterLimitCm);
}

bool UMSBotSpawnManagerComponent::ConflictsWithHumanDropColumn(const FVector& CandidateGround,
	const float BotCapsuleRadius, const FMSProtectedHumanDropColumn& HumanDrop)
{
	if (FMath::Abs(CandidateGround.Z - HumanDrop.GroundLocation.Z) > TaiganMarkerFloorToleranceCm)
	{
		return false;
	}
	const float Clearance = FMath::Max(0.0f, BotCapsuleRadius)
		+ FMath::Max(0.0f, HumanDrop.CapsuleRadius) + HumanDropColumnMarginCm;
	return FVector::Dist2D(CandidateGround, HumanDrop.GroundLocation) < Clearance;
}

TArray<FMSProtectedHumanDropColumn> UMSBotSpawnManagerComponent::GatherFallingHumanDropColumns() const
{
	TArray<FMSProtectedHumanDropColumn> Drops;
	UWorld* World = GetWorld();
	if (!IsTaiganBotSpawnMap(World)) return Drops;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const AMSPlayerCharacter* Human = Controller ? Cast<AMSPlayerCharacter>(Controller->GetPawn()) : nullptr;
		const UCharacterMovementComponent* Movement = Human ? Human->GetCharacterMovement() : nullptr;
		const UCapsuleComponent* Capsule = Human ? Human->GetCapsuleComponent() : nullptr;
		if (!Human || !Movement || !Movement->IsFalling() || !Capsule
			|| Human->IsHidden() || !Human->GetActorEnableCollision()) continue;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MSBotHumanDropFloor), false);
		Query.AddIgnoredActor(Human);
		FHitResult Floor;
		const FVector Start = Human->GetActorLocation();
		if (!World->LineTraceSingleByChannel(Floor, Start,
			Start - FVector(0.0f, 0.0f, GroundProbeHeight), ECC_WorldStatic, Query)) continue;
		FMSProtectedHumanDropColumn& Drop = Drops.AddDefaulted_GetRef();
		Drop.GroundLocation = Floor.ImpactPoint;
		Drop.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	}
	return Drops;
}

UMSBotSpawnManagerComponent::UMSBotSpawnManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 既定の動的BOT機体。エディタ/レベルごとに差し替え可能(B-9: 数値はDataAsset集約)。
	BotMobileSuitData = TSoftObjectPtr<UMSMobileSuitData>(
		FSoftObjectPath(TEXT("/Game/Data/MobileSuits/DA_MS_StrikerMk1.DA_MS_StrikerMk1")));
}

void UMSBotSpawnManagerComponent::SetTeamBotCounts(const int32 InAllyBotCount, const int32 InEnemyBotCount)
{
	AllyBotCount = FMath::Max(0, InAllyBotCount);
	EnemyBotCount = FMath::Max(0, InEnemyBotCount);
}

void UMSBotSpawnManagerComponent::SetRoomBotConfiguration(const int32 Count, const EMSEnemyTier Difficulty,
	const bool bFillTeams, const bool bMixedDifficulty)
{
	SetTeamBotCounts(0, FMath::Clamp(Count, 0, 11));
	bFillTeamsWithBots = bFillTeams;
	bUseFixedRoomDifficulty = true;
	bUseMixedRoomDifficulty = bMixedDifficulty;
	FixedRoomDifficulty = Difficulty;
}

void UMSBotSpawnManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TeamPopulationTimer);
	Super::EndPlay(EndPlayReason);
}

int32 UMSBotSpawnManagerComponent::CountHumanSeats(const FGenericTeamId TeamId) const
{
	UWorld* World = GetWorld();
	if (!World) return 0;
	if (MSOnline::IsServerWorld(World))
	{
		const auto* Server = World->GetSubsystem<UMSOnlineServerSubsystem>();
		return Server ? Server->CountActivePlayers(TeamId) : 0;
	}
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const AMSPlayerState* State = PC ? PC->GetPlayerState<AMSPlayerState>() : nullptr;
		if (IsValid(PC) && State && State->GetSelectedFaction() == TeamId) ++Count;
	}
	return Count;
}

void UMSBotSpawnManagerComponent::ReconcileTeam(const FGenericTeamId TeamId, const int32 DesiredBots,
	UMSMobileSuitData* SuitData)
{
	TArray<AMSBotCharacter*> Existing;
	// In a filled battle, placed combat BOTs also consume the team's 25 seats.
	// Neutral actors, human pawns and non-BOT actors are never replaced here.
	for (TActorIterator<AMSBotCharacter> It(GetWorld()); It; ++It)
		if (IsValid(*It) && UMSTeamStatics::IsOnTeam(*It, TeamId)) Existing.Add(*It);
	// Dead BOTs keep their seat during respawn. Removing a seat is not a kill and
	// must not trigger score, gauge loss, or the BOT's auto-respawn timer.
	while (Existing.Num() > DesiredBots)
	{
		AMSBotCharacter* Bot = Existing.Pop();
		if (AController* Controller = Bot->GetController())
		{
			Controller->UnPossess();
			Controller->Destroy();
		}
		Bot->Destroy();
	}
	if (Existing.Num() < DesiredBots && SuitData)
	{
		if (bUseMixedRoomDifficulty)
		{
			TArray<EMSEnemyTier> ExistingTiers;
			ExistingTiers.Reserve(Existing.Num());
			for (const AMSBotCharacter* Bot : Existing)
				ExistingTiers.Add(Bot->GetEnemyTier());
			const TArray<EMSEnemyTier> Missing = BuildMissingMixedTierAssignments(
				DesiredBots, ExistingTiers, 0x4D530000 + TeamId.GetId());
			SpawnTeamBots(TeamId, Missing.Num(), SuitData, &Missing);
		}
		else SpawnTeamBots(TeamId, DesiredBots - Existing.Num(), SuitData);
	}
}

void UMSBotSpawnManagerComponent::RefreshTeamPopulation()
{
	if (!bInitialSpawnDone || !bSpawnBots || !bFillTeamsWithBots || !GetOwner()
		|| !GetOwner()->HasAuthority() || GetOwner()->IsActorBeingDestroyed()
		|| !GetWorld() || GetWorld()->bIsTearingDown) return;
	SpawnedBots.RemoveAll([](const TWeakObjectPtr<AMSBotCharacter>& Bot) { return !Bot.IsValid(); });
	AllyBotCount = FMath::Max(0, MSRoomPopulation::TeamSize - CountHumanSeats(MSTeam::PlayerTeamId()));
	EnemyBotCount = FMath::Max(0, MSRoomPopulation::TeamSize - CountHumanSeats(MSTeam::EnemyTeamId()));
	UMSMobileSuitData* Suit = ResolveBotMobileSuitData();
	if (!Suit && !bWarnedMissingFillSuit)
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("BotSpawnManager: 25vs25 fill cannot load %s; target BOTs ally=%d enemy=%d. Retrying on the next population tick."),
			*BotMobileSuitData.ToString(), AllyBotCount, EnemyBotCount);
		bWarnedMissingFillSuit = true;
	}
	else if (Suit) bWarnedMissingFillSuit = false;
	ReconcileTeam(MSTeam::PlayerTeamId(), AllyBotCount, Suit);
	ReconcileTeam(MSTeam::EnemyTeamId(), EnemyBotCount, Suit);
}

void UMSBotSpawnManagerComponent::ComputeTierCounts(const int32 TotalCount,
	int32& OutWeak, int32& OutMedium, int32& OutStrong)
{
	OutWeak = 0;
	OutMedium = 0;
	OutStrong = 0;
	if (TotalCount <= 0)
	{
		return;
	}

	// Weak60%/Medium30%/Strong10%。丸めは Strong→Medium→Weak の順に最低1体保証
	// (docs/BotDeployment_Design.md)。12体なら Strong1 / Medium4 / Weak7。
	OutStrong = FMath::Clamp(FMath::RoundToInt32(TotalCount * 0.1f), 1, TotalCount);
	const int32 RemainingAfterStrong = TotalCount - OutStrong;
	OutMedium = RemainingAfterStrong > 0
		? FMath::Clamp(FMath::RoundToInt32(TotalCount * 0.3f), 1, RemainingAfterStrong)
		: 0;
	OutWeak = RemainingAfterStrong - OutMedium;
}

TArray<EMSEnemyTier> UMSBotSpawnManagerComponent::BuildTierAssignments(const int32 TotalCount)
{
	int32 WeakCount = 0;
	int32 MediumCount = 0;
	int32 StrongCount = 0;
	ComputeTierCounts(TotalCount, WeakCount, MediumCount, StrongCount);

	TArray<EMSEnemyTier> Assignments;
	Assignments.Reserve(TotalCount);
	for (int32 Index = 0; Index < StrongCount; ++Index) Assignments.Add(EMSEnemyTier::Strong);
	for (int32 Index = 0; Index < MediumCount; ++Index) Assignments.Add(EMSEnemyTier::Medium);
	for (int32 Index = 0; Index < WeakCount; ++Index) Assignments.Add(EMSEnemyTier::Weak);
	return Assignments;
}

TArray<EMSEnemyTier> UMSBotSpawnManagerComponent::BuildMixedTierAssignments(const int32 TotalCount,
	const int32 Seed)
{
	const int32 Count = FMath::Max(0, TotalCount);
	const int32 Percent[3] = {60, 30, 10};
	int32 Counts[3] = {Count * Percent[0] / 100, Count * Percent[1] / 100,
		Count * Percent[2] / 100};
	int32 Remainders[3] = {Count * Percent[0] % 100, Count * Percent[1] % 100,
		Count * Percent[2] % 100};
	for (int32 Remaining = Count - Counts[0] - Counts[1] - Counts[2]; Remaining > 0; --Remaining)
	{
		int32 Best = 0;
		for (int32 Tier = 1; Tier < 3; ++Tier)
			if (Remainders[Tier] > Remainders[Best]) Best = Tier;
		++Counts[Best];
		Remainders[Best] = -1;
	}
	TArray<EMSEnemyTier> Result;
	Result.Reserve(Count);
	for (int32 Tier = 0; Tier < 3; ++Tier)
		for (int32 Index = 0; Index < Counts[Tier]; ++Index)
			Result.Add(static_cast<EMSEnemyTier>(Tier));
	FRandomStream Random(Seed);
	for (int32 Index = Result.Num() - 1; Index > 0; --Index)
		Result.Swap(Index, Random.RandRange(0, Index));
	return Result;
}

TArray<EMSEnemyTier> UMSBotSpawnManagerComponent::BuildMissingMixedTierAssignments(
	const int32 DesiredCount, const TArray<EMSEnemyTier>& Existing, const int32 Seed)
{
	TArray<EMSEnemyTier> Missing;
	int32 ExistingByTier[3] = {0, 0, 0};
	for (const EMSEnemyTier Tier : Existing)
	{
		const int32 Index = static_cast<int32>(Tier);
		if (Index >= 0 && Index < 3) ++ExistingByTier[Index];
	}
	for (const EMSEnemyTier Tier : BuildMixedTierAssignments(DesiredCount, Seed))
	{
		const int32 Index = static_cast<int32>(Tier);
		if (ExistingByTier[Index] > 0) --ExistingByTier[Index];
		else Missing.Add(Tier);
	}
	// An authored BOT with an excess tier still occupies a seat. Never exceed the
	// requested team size just to force the statistical ratio.
	Missing.SetNum(FMath::Min(Missing.Num(), FMath::Max(0, DesiredCount - Existing.Num())));
	return Missing;
}

void UMSBotSpawnManagerComponent::SpawnInitialBots()
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!World || !Owner || !Owner->HasAuthority())
	{
		return;
	}
	if (!bSpawnBots || bInitialSpawnDone)
	{
		return;
	}
	bInitialSpawnDone = true;
	if (bFillTeamsWithBots)
	{
		RefreshTeamPopulation();
		// Also retries placement failures and covers unexpected controller loss.
		World->GetTimerManager().SetTimer(TeamPopulationTimer, this,
			&UMSBotSpawnManagerComponent::RefreshTeamPopulation, 1.0f, true);
		return;
	}

	UMSMobileSuitData* SuitData = ResolveBotMobileSuitData();
	if (!SuitData)
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("BotSpawnManager: BOT用機体データを解決できないため初期配備を中止 (BotMobileSuitData=%s)"),
			*BotMobileSuitData.ToString());
		return;
	}

	const int32 AllySpawned = SpawnTeamBots(MSTeam::PlayerTeamId(), AllyBotCount, SuitData);
	const int32 EnemySpawned = SpawnTeamBots(MSTeam::EnemyTeamId(), EnemyBotCount, SuitData);
	UE_LOG(LogNeoMSGO, Log, TEXT("BotSpawnManager: 初期配備完了 味方 %d/%d 体, 敵 %d/%d 体"),
		AllySpawned, AllyBotCount, EnemySpawned, EnemyBotCount);
}

int32 UMSBotSpawnManagerComponent::CountSpawnedBots(const FGenericTeamId TeamId) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AMSBotCharacter>& Entry : SpawnedBots)
	{
		const AMSBotCharacter* Bot = Entry.Get();
		if (IsValid(Bot) && Bot->GetGenericTeamId() == TeamId)
		{
			++Count;
		}
	}
	return Count;
}

TArray<FVector> UMSBotSpawnManagerComponent::GatherSpawnCenters(const FGenericTeamId TeamId) const
{
	UWorld* World = GetWorld();
	TArray<FVector> Centers;
	if (!World)
	{
		return Centers;
	}

	// 第1候補: チーム(または中立=共用)の AMSBotSpawnPoint マーカー
	for (TActorIterator<AMSBotSpawnPoint> It(World); It; ++It)
	{
		if (It->ServesTeam(TeamId))
		{
			Centers.Add(It->GetActorLocation());
		}
	}
	if (!Centers.IsEmpty())
	{
		return Centers;
	}

	// フォールバック1: 自軍 AMSHomeBase。自軍拠点が無ければ任意の AMSHomeBase
	TArray<FVector> AnyHomeBases;
	for (TActorIterator<AMSHomeBase> It(World); It; ++It)
	{
		AnyHomeBases.Add(It->GetActorLocation());
		if (UMSTeamStatics::CaptureOwnerToTeamId(It->GetCurrentOwner()) == TeamId)
		{
			Centers.Add(It->GetActorLocation());
		}
	}
	if (!Centers.IsEmpty())
	{
		return Centers;
	}
	if (!AnyHomeBases.IsEmpty())
	{
		return AnyHomeBases;
	}

	// フォールバック2: PlayerStart 周辺(docs/BotDeployment_Design.md 推奨方針1)
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centers.Add(It->GetActorLocation());
	}
	return Centers;
}

bool UMSBotSpawnManagerComponent::ResolveGroundLocation(const FVector& Candidate,
	FVector& OutGroundLocation, FMSBotPlacementDiagnostics& Diagnostics) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (IsTaiganUndergroundBotSpawn(World, Candidate))
	{
		// The marker is below the subway roof. A long trace or generous Nav Z
		// projection would choose the upper layer instead of the authored floor.
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MSTaiganBotFloor), false);
		Query.AddIgnoredActor(GetOwner());
		if (!World->LineTraceSingleByChannel(Hit,
			Candidate + FVector(0.0f, 0.0f, 900.0f),
			Candidate - FVector(0.0f, 0.0f, 15000.0f), ECC_WorldStatic, Query))
		{
			++Diagnostics.GroundResolveFailures;
			return false;
		}
		++Diagnostics.GroundTraceFallbacks;
		if (Diagnostics.FirstGroundHitActor.IsEmpty())
		{
			Diagnostics.FirstGroundHitActor = GetNameSafe(Hit.GetActor());
			Diagnostics.FirstGroundHitZ = Hit.ImpactPoint.Z;
			Diagnostics.FirstCandidateZ = Candidate.Z;
		}
		OutGroundLocation = Hit.ImpactPoint;
		return true;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bHasNavData = NavSys && NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
	if (bHasNavData)
	{
		FNavLocation Projected;
		const bool bTaigan = IsTaiganBotSpawnMap(World);
		const UCapsuleComponent* DefaultCapsule = GetDefault<AMSBotCharacter>()->GetCapsuleComponent();
		const float CapsuleHalfHeight = DefaultCapsule ? DefaultCapsule->GetScaledCapsuleHalfHeight() : 0.0f;
		const FVector ProjectionExtent = bTaigan
			? FVector(TaiganNavProjectionXYCm, TaiganNavProjectionXYCm,
				CapsuleHalfHeight + TaiganMarkerFloorToleranceCm)
			: FVector(SpawnSearchRadius, SpawnSearchRadius, DropSpawnHeight);
		if (NavSys->ProjectPointToNavigation(Candidate, Projected,
			ProjectionExtent))
		{
			++Diagnostics.NavProjectionSuccesses;
			if (bTaigan)
			{
				// Taigan's marker is a standing capsule centre. Only the authored
				// floor band is valid: tracing from high above a lower Nav layer can
				// hit a roof and silently turn an underground spawn into a surface one.
				const float ExpectedFloorZ = Candidate.Z - CapsuleHalfHeight;
				FHitResult NavSnap;
				FCollisionQueryParams SnapParams(SCENE_QUERY_STAT(MSBotSpawnNavSnap), false);
				SnapParams.AddIgnoredActor(GetOwner());
				if (!World->LineTraceSingleByChannel(NavSnap,
					FVector(Projected.Location.X, Projected.Location.Y,
						ExpectedFloorZ + TaiganMarkerFloorToleranceCm),
					FVector(Projected.Location.X, Projected.Location.Y,
						ExpectedFloorZ - TaiganMarkerFloorToleranceCm),
					ECC_WorldStatic, SnapParams))
				{
					++Diagnostics.GroundResolveFailures;
					return false;
				}
				++Diagnostics.GroundTraceFallbacks;
				if (Diagnostics.FirstGroundHitActor.IsEmpty())
				{
					Diagnostics.FirstGroundHitActor = GetNameSafe(NavSnap.GetActor());
					Diagnostics.FirstGroundHitZ = NavSnap.ImpactPoint.Z;
					Diagnostics.FirstCandidateZ = Candidate.Z;
				}
				OutGroundLocation = FVector(Projected.Location.X, Projected.Location.Y,
					NavSnap.ImpactPoint.Z);
				return true;
			}
			// Legacy maps retain the existing long collision snap after Nav
			// projection. Only Taigan restricts the trace to the marker floor band.
			FHitResult NavSnap;
			FCollisionQueryParams SnapParams(SCENE_QUERY_STAT(MSBotSpawnNavSnap), false);
			SnapParams.AddIgnoredActor(GetOwner());
			if (World->LineTraceSingleByChannel(NavSnap,
				Projected.Location + FVector(0.0f, 0.0f, GroundProbeHeight),
				Projected.Location - FVector(0.0f, 0.0f, GroundProbeHeight),
				ECC_WorldStatic, SnapParams))
			{
				++Diagnostics.GroundTraceFallbacks;
				if (Diagnostics.FirstGroundHitActor.IsEmpty())
				{
					Diagnostics.FirstGroundHitActor = GetNameSafe(NavSnap.GetActor());
					Diagnostics.FirstGroundHitZ = NavSnap.ImpactPoint.Z;
					Diagnostics.FirstCandidateZ = Projected.Location.Z;
				}
				OutGroundLocation = FVector(Projected.Location.X, Projected.Location.Y,
					NavSnap.ImpactPoint.Z);
				return true;
			}
			OutGroundLocation = Projected.Location;
			return true;
		}
		++Diagnostics.NavProjectionFailures;
	}

	// NavMeshが無い/まだ生成されていない場合の地表解決。マーカーのZは必ずしも地表高度では
	// ないため(Daibaoの拠点は実測で地形から大きく離れていた)、上空から十分長い距離を掘る。
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotSpawnGroundTrace), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(GetOwner());
	if (World->LineTraceSingleByChannel(Hit, Candidate + FVector(0.0f, 0.0f, GroundProbeHeight),
		Candidate - FVector(0.0f, 0.0f, GroundProbeHeight), ECC_WorldStatic, QueryParams))
	{
		++Diagnostics.GroundTraceFallbacks;
		if (Diagnostics.FirstGroundHitActor.IsEmpty())
		{
			Diagnostics.FirstGroundHitActor = GetNameSafe(Hit.GetActor());
			Diagnostics.FirstGroundHitZ = Hit.ImpactPoint.Z;
			Diagnostics.FirstCandidateZ = Candidate.Z;
		}
		OutGroundLocation = Hit.ImpactPoint;
		return true;
	}

	// 地形すら見つからないテストワールド等では候補点をそのまま使う(従来のNavData無し挙動)。
	if (!bHasNavData)
	{
		++Diagnostics.GroundTraceFallbacks;
		OutGroundLocation = Candidate;
		return true;
	}
	++Diagnostics.GroundResolveFailures;
	return false;
}

bool UMSBotSpawnManagerComponent::ResolveSpawnLocation(const FVector& Center, FVector& OutGroundLocation,
	FMSBotPlacementDiagnostics& Diagnostics,
	const TArray<FMSProtectedHumanDropColumn>& HumanDrops,
	const bool bAllowUnverifiedPlacement) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	float CapsuleRadius = 42.0f * MSGO::MSScale;
	float CapsuleHalfHeight = 96.0f * MSGO::MSScale;
	if (const UCapsuleComponent* Capsule = GetDefault<AMSBotCharacter>()->GetCapsuleComponent())
	{
		Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	}
	const bool bTaigan = IsTaiganBotSpawnMap(World);
	const float ScatterRadius = bTaigan
		? GetTaiganSpawnScatterRadius(SpawnSearchRadius) : SpawnSearchRadius;

	FVector FirstResolvedCandidate = FVector::ZeroVector;
	bool bHasResolvedCandidate = false;

	for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerPoint; ++Attempt)
	{
		const FVector2D Scatter = FMath::RandPointInCircle(ScatterRadius);
		FVector Candidate = Center + FVector(Scatter.X, Scatter.Y, 0.0f);
		if (!ResolveGroundLocation(Candidate, Candidate, Diagnostics))
		{
			continue;
		}
		if (bTaigan && (!IsTaiganMarkerFloorLayer(Center.Z, Candidate.Z, CapsuleHalfHeight)
			|| FVector::Dist2D(Center, Candidate) > ScatterRadius + KINDA_SMALL_NUMBER))
		{
			++Diagnostics.MarkerLayerRejections;
			continue;
		}
		if (bTaigan && HumanDrops.ContainsByPredicate([&Candidate, CapsuleRadius](const FMSProtectedHumanDropColumn& Drop)
			{ return UMSBotSpawnManagerComponent::ConflictsWithHumanDropColumn(Candidate, CapsuleRadius, Drop); }))
		{
			++Diagnostics.HumanDropRejections;
			continue;
		}
		if (!bHasResolvedCandidate)
		{
			FirstResolvedCandidate = Candidate;
			bHasResolvedCandidate = true;
		}

		// 静的ジオメトリとのブロッキングオーバーラップ検査(「建物の中に埋まる」対策)。
		// 地面との誤接触を避けるためカプセル底を少し浮かせて検査する。
		const FVector CapsuleCenter = Candidate + FVector(0.0f, 0.0f,
			CapsuleHalfHeight + 10.0f * MSGO::MSScale);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotSpawnOverlap), /*bTraceComplex=*/false);
		if (World->OverlapBlockingTestByChannel(CapsuleCenter, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), QueryParams))
		{
			++Diagnostics.OverlapRejections;
			continue;
		}
		if (IsTaiganUndergroundBotSpawn(World, Center))
		{
			FVector CoveredDrop;
			if (!UMSRespawnComponent::TryResolveCoveredDropLocation(World, Candidate,
				CapsuleRadius, CapsuleHalfHeight, DropSpawnHeight, GetOwner(), CoveredDrop))
			{
				++Diagnostics.OverlapRejections;
				continue;
			}
		}

		OutGroundLocation = Candidate;
		return true;
	}

	// #209: 旧mapは全リトライがオーバーラップで弾かれても地表候補へ配備する。
	// Taiganはmarkerが指定した床層の厳密性を優先し、この救済を使わない。
	// BOTは上空DropSpawnHeightから重力降下し、SpawnCollisionHandlingがカプセルを押し出すため、
	// 「1体も配備されず試合が成立しない」よりは押し出し着地のほうが遥かに軽微な副作用である。
	if (bAllowUnverifiedPlacement && bHasResolvedCandidate &&
		!bTaigan)
	{
		++Diagnostics.UnverifiedPlacements;
		OutGroundLocation = FirstResolvedCandidate;
		return true;
	}
	return false;
}

AMSBotCharacter* UMSBotSpawnManagerComponent::SpawnSingleBot(const FGenericTeamId TeamId,
	const EMSEnemyTier Tier, const FVector& GroundLocation, UMSMobileSuitData* SuitData)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = GetOwner();

	// 上空DropSpawnHeightから重力降下でスポーンする(UMSRespawnComponentと同方式。
	// 固定Z座標スポーンによる地下/建物埋没バグの再発防止 — B-11)。
	const FRotator SpawnRotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);
	FVector AirLocation = GroundLocation + FVector(0.0f, 0.0f, DropSpawnHeight);
	const bool bTaiganUnderground = IsTaiganUndergroundBotSpawn(World, GroundLocation);
	float CapsuleHalfHeight = 0.0f;
	if (bTaiganUnderground)
	{
		float CapsuleRadius = 0.0f;
		if (const UCapsuleComponent* Capsule = GetDefault<AMSBotCharacter>()->GetCapsuleComponent())
		{
			Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
		}
		if (!UMSRespawnComponent::TryResolveCoveredDropLocation(World, GroundLocation,
			CapsuleRadius, CapsuleHalfHeight, DropSpawnHeight, GetOwner(), AirLocation)) return nullptr;
	}
	// The initial covered drop is close to the roof (about -4271 cm in Taigan),
	// above the underground-target cutoff. Record a capsule centre near the
	// floor for the *next* sortie, so ground resolution stays below the roof.
	const FVector RespawnTarget = bTaiganUnderground
		? GroundLocation + FVector(0.0f, 0.0f, CapsuleHalfHeight + 50.0f)
		: GroundLocation;
	if (bTaiganUnderground && !IsTaiganUndergroundBotSpawn(World, RespawnTarget)) return nullptr;
	AMSBotCharacter* Bot = World->SpawnActor<AMSBotCharacter>(
		AMSBotCharacter::StaticClass(), AirLocation, SpawnRotation, SpawnParams);
	if (!Bot)
	{
		return nullptr;
	}

	Bot->SetEnemyTier(Tier);
	Bot->SetGenericTeamId(TeamId);
	// 架空パイロット名は「ガンオンN」に統一(キルログ等のUI表示用 — タスク#145)
	Bot->SetUnitDisplayName(MSDummyNames::MakeDummyName(NextDummyNameIndex++));

	// 初期化は InitializeUnit を単一入口として通す(ガードレールB-10)。
	// Armor全快+武器スロット構築(InitializeFromMobileSuit)までここで完了する。
	Bot->InitializeUnit(SuitData);
	Bot->ApplyTierWeaponLoadout();

	// 地上は従来の床、地下は床近くの安全なカプセル中心を既定目標にする。
	if (UMSRespawnComponent* Respawn = Bot->GetRespawnComponent())
	{
		Respawn->SetDefaultSpawnPoint(FTransform(SpawnRotation, RespawnTarget));
	}
	if (UCharacterMovementComponent* Movement = Bot->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}

	SpawnedBots.Add(Bot);
	return Bot;
}

int32 UMSBotSpawnManagerComponent::SpawnTeamBots(const FGenericTeamId TeamId, const int32 Count,
	UMSMobileSuitData* SuitData, const TArray<EMSEnemyTier>* TierOverride)
{
	if (Count <= 0)
	{
		return 0;
	}
	UWorld* World = GetWorld();
	if (!World) return 0;
	const bool bTaigan = IsTaiganBotSpawnMap(World);
	const TArray<FMSProtectedHumanDropColumn> HumanDrops = bTaigan
		? GatherFallingHumanDropColumns() : TArray<FMSProtectedHumanDropColumn>();

	const TArray<FVector> Centers = GatherSpawnCenters(TeamId);
	if (Centers.IsEmpty())
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("BotSpawnManager: チーム%d のスポーン地点が存在しないため %d 体の配備を中止 (AMSBotSpawnPoint/AMSHomeBase/PlayerStart いずれも無し)"),
			TeamId.GetId(), Count);
		return 0;
	}

	TArray<EMSEnemyTier> Tiers;
	if (TierOverride)
	{
		Tiers = *TierOverride;
	}
	else if (bUseMixedRoomDifficulty)
	{
		Tiers = BuildMixedTierAssignments(Count, 0x4D530000 + TeamId.GetId());
	}
	else if (bUseFixedRoomDifficulty)
	{
		Tiers.Init(FixedRoomDifficulty, Count);
	}
	else
	{
		Tiers = BuildTierAssignments(Count);
	}
	FMSBotPlacementDiagnostics Diagnostics;
	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < Tiers.Num(); ++Index)
	{
		bool bPlaced = false;
		// 配置失敗時は別スポーンポイントへフォールバック(リトライは ResolveSpawnLocation 内)。
		// 1周目は厳格判定。旧mapだけ2周目で従来の未検証地表救済を試す。
		for (int32 Pass = 0; Pass < (bTaigan ? 1 : 2) && !bPlaced; ++Pass)
		{
			for (int32 CenterOffset = 0; CenterOffset < Centers.Num() && !bPlaced; ++CenterOffset)
			{
				const FVector& Center = Centers[(Index + CenterOffset) % Centers.Num()];
				FVector GroundLocation = FVector::ZeroVector;
				if (!ResolveSpawnLocation(Center, GroundLocation, Diagnostics,
					HumanDrops, /*bAllowUnverifiedPlacement=*/Pass == 1))
				{
					continue;
				}
				if (SpawnSingleBot(TeamId, Tiers[Index], GroundLocation, SuitData))
				{
					++SpawnedCount;
					bPlaced = true;
				}
			}
		}
	}

	// #209: 「NavMesh射影 or オーバーラップ」の一括Warningでは原因が切り分けられなかったため、
	// 両者を分離して常時記録する(未達でなくても射影失敗数が見えるようにする)。
	UE_LOG(LogNeoMSGO, Log,
		TEXT("BotSpawnManager: チーム%d 配置内訳 nav_ok=%d nav_fail=%d ground_trace=%d ground_fail=%d layer_reject=%d human_drop_reject=%d overlap_reject=%d unverified=%d first_ground_hit=%s hit_z=%.0f marker_z=%.0f"),
		TeamId.GetId(), Diagnostics.NavProjectionSuccesses, Diagnostics.NavProjectionFailures,
		Diagnostics.GroundTraceFallbacks, Diagnostics.GroundResolveFailures, Diagnostics.MarkerLayerRejections,
		Diagnostics.HumanDropRejections, Diagnostics.OverlapRejections, Diagnostics.UnverifiedPlacements,
		Diagnostics.FirstGroundHitActor.IsEmpty() ? TEXT("none") : *Diagnostics.FirstGroundHitActor,
		Diagnostics.FirstGroundHitZ, Diagnostics.FirstCandidateZ);

	if (SpawnedCount < Count)
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("BotSpawnManager: チーム%d の配備が要求数未達 (%d/%d)。NavMesh射影またはオーバーラップ検査の失敗が続いた可能性"),
			TeamId.GetId(), SpawnedCount, Count);
	}
	return SpawnedCount;
}

UMSMobileSuitData* UMSBotSpawnManagerComponent::ResolveBotMobileSuitData() const
{
	if (BotMobileSuitDataOverride)
	{
		return BotMobileSuitDataOverride;
	}
	return BotMobileSuitData.LoadSynchronous();
}
