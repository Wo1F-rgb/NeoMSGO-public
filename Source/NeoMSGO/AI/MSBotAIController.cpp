#include "AI/MSBotAIController.h"

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "AI/GA_BotKick.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSHitReactionComponent.h"
#include "Combat/MSUnitBase.h"
#include "Components/StateTreeAIComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSWeaponData.h"
#include "Gameplay/MSCapturePoint.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/GA_Fire_Burst.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/GA_Fire_ContinuousBeam.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Fire_Homing.h"
#include "Player/MSLockOnComponent.h"
#include "Weapons/GA_Fire_Shotgun.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	constexpr float MovementRecoveryStallWindowSeconds = 1.5f;
	constexpr float MovementRecoveryMinGoalDistanceCm = 300.0f;
	constexpr float MovementRecoveryMinDisplacementCm = 25.0f;
	constexpr float MovementRecoveryMinSpeedCmPerSecond = 20.0f;
	constexpr float MovementRecoveryDurationSeconds = 0.85f;
	constexpr float MovementRecoveryCooldownSeconds = 1.0f;
	constexpr float MovementRecoveryRetargetDistanceCm = 1000.0f;
	constexpr float MovementRecoveryMinEscapeOffsetCm = 350.0f;
	constexpr float MovementRecoveryMaxEscapeOffsetCm = 700.0f;
	constexpr float MovementRecoveryMinGroundNormalZ = 0.5f;
	constexpr float TaiganGoalRefreshDistanceCm = 300.0f;
	constexpr float TaiganFailedMoveRetrySeconds = 1.0f;
	constexpr float TaiganEntranceAcceptanceCm = 500.0f;
	constexpr float TaiganEntranceStageDistanceCm = 1000.0f;

	struct FTaiganEntrancePair
	{
		const ATargetPoint* Bottom = nullptr;
		const ATargetPoint* Top = nullptr;
	};
}

AMSBotAIController::AMSBotAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	// アセット未設定でもBeginPlay時にLogStateTreeのErrorを出さないため自動開始を無効化し、
	// OnPossessで明示的にStartLogicする(StartTreeはアセット未設定なら無音で何もしない)
	StateTreeComponent->SetStartLogicAutomatically(false);
	BrainComponent = StateTreeComponent;
	BotStateTreeAsset = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/AI/ST_MSBotCombat.ST_MSBotCombat")));
}

void AMSBotAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	TargetRefreshRemaining = 0.0f;
	StrategicObjective.Reset();
	StrategicObjectiveSlot = 0;
	LastStalledTarget.Reset();
	AvoidedStalledTarget.Reset();
	ConsecutiveTargetStalls = 0;
	AvoidStalledTargetUntil = 0.0f;
	BoostBurstRemaining = 0.0f;
	BoostBurstCooldownRemaining = 0.0f;
	KickCooldownRemaining = 0.0f;
	MovementRecoveryCooldownRemaining = 0.0f;
	MovementStallDuration = 0.0f;
	bHasActiveMovementRequest = false;
	bMovementRecoveryActive = false;
	bIssuingMovementRecoveryRequest = false;
	bHasRecoveryOriginalGoal = false;
	LastMoveRequestResult = -1;
	LastMoveRequestDestination = FVector::ZeroVector;
	LastMoveAcceptanceRadius = 5.0f;
	bLastMoveStopOnOverlap = true;
	bFollowingTaiganEntrance = false;
	TaiganStrategicGoal = FVector::ZeroVector;
	LastFailedTaiganMoveTime = -1000.0f;
	MovementRecoveryOriginalGoal = FVector::ZeroVector;
	MovementRecoveryStrategicGoal = FVector::ZeroVector;
	MovementRecoveryEscapeGoal = FVector::ZeroVector;
	LastMovementRecoveryDestination = FVector::ZeroVector;
	MovementRecoveryOriginalAcceptanceRadius = 5.0f;
	bMovementRecoveryOriginalStopOnOverlap = true;
	bRecoveryWasFollowingTaiganEntrance = false;
	MovementRecoveryAcceptanceRadius = 100.0f;
	MovementRecoveryRemaining = 0.0f;
	MovementRecoveryDirection = 1;
	bPreferBackwardEscape = false;
	MovementRecoveryCount = 0;
	bAILogicSuspendedForDeath = false;
	PossessLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	LastProgressSampleLocation = PossessLocation;
	bLoggedMovement = false;
	bLoggedWeaponFire = false;
	BindDeathFlow(Cast<AMSBotCharacter>(InPawn));
	if (StateTreeComponent)
	{
		AssignedStateTree = BotStateTreeAsset.LoadSynchronous();
		if (AssignedStateTree)
		{
			StateTreeComponent->SetStateTree(AssignedStateTree);
			UE_LOG(LogTemp, Log, TEXT("MSBotAI: assigned %s for %s"), *AssignedStateTree->GetPathName(), *GetNameSafe(InPawn));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSBotAI: required StateTree is missing: %s"), *BotStateTreeAsset.ToSoftObjectPath().ToString());
		}
	}
	SetActorTickEnabled(true);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AMSBotAIController::StartAssignedStateTreeWhenReady);
	}
}

void AMSBotAIController::StartAssignedStateTreeWhenReady()
{
	AMSBotCharacter* Bot = GetBot();
	if (bAILogicSuspendedForDeath || !AssignedStateTree || !StateTreeComponent || StateTreeComponent->IsRunning() || !Bot)
	{
		return;
	}
	if (!StateTreeComponent->IsRegistered() || !Bot->HasActorBegunPlay())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &AMSBotAIController::StartAssignedStateTreeWhenReady);
		}
		return;
	}
	StateTreeComponent->StartLogic();
	UE_LOG(LogTemp, Log, TEXT("MSBotAI: started %s for %s"), *AssignedStateTree->GetPathName(), *GetNameSafe(Bot));
}

void AMSBotAIController::OnUnPossess()
{
	UnbindDeathFlow(GetBot());
	EndBoostBurst(false);
	if (StateTreeComponent) StateTreeComponent->StopLogic(TEXT("Unpossessed"));
	CurrentTarget.Reset();
	StrategicObjective.Reset();
	AssignedStateTree = nullptr;
	ClearDirect3DMovement();
	Super::OnUnPossess();
}

void AMSBotAIController::BindDeathFlow(AMSBotCharacter* Bot)
{
	if (!Bot)
	{
		return;
	}
	if (UMSAttributeSet_Vital* Vital = Bot->GetVitalSet())
	{
		Vital->OnDeath.AddUniqueDynamic(this, &AMSBotAIController::HandleBotDeath);
	}
	if (UMSRespawnComponent* Respawn = Bot->GetRespawnComponent())
	{
		Respawn->OnDeployed.AddUniqueDynamic(this, &AMSBotAIController::HandleBotDeployed);
	}
}

void AMSBotAIController::UnbindDeathFlow(AMSBotCharacter* Bot)
{
	if (!Bot)
	{
		return;
	}
	if (UMSAttributeSet_Vital* Vital = Bot->GetVitalSet())
	{
		Vital->OnDeath.RemoveDynamic(this, &AMSBotAIController::HandleBotDeath);
	}
	if (UMSRespawnComponent* Respawn = Bot->GetRespawnComponent())
	{
		Respawn->OnDeployed.RemoveDynamic(this, &AMSBotAIController::HandleBotDeployed);
	}
}

void AMSBotAIController::HandleBotDeath(AActor* DeadUnit, AActor* Killer)
{
	if (DeadUnit != GetBot())
	{
		return;
	}
	bAILogicSuspendedForDeath = true;
	EndBoostBurst(false);
	StopMovement();
	ClearDirect3DMovement();
	CurrentTarget.Reset();
	if (StateTreeComponent)
	{
		StateTreeComponent->StopLogic(TEXT("BOT dead; awaiting respawn"));
	}
}

void AMSBotAIController::HandleBotDeployed(AMSUnitBase* Unit)
{
	if (Unit != GetBot())
	{
		return;
	}
	bAILogicSuspendedForDeath = false;
	TargetRefreshRemaining = 0.0f;
	CurrentTarget.Reset();
	if (StateTreeComponent && !StateTreeComponent->IsRunning())
	{
		// Restarting re-enters Patrol; FMSBotPatrolTask::EnterState chooses a fresh destination.
		StateTreeComponent->StartLogic();
	}
}

void AMSBotAIController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Auto possession occurs before the dynamically spawned controller/component and pawn finish BeginPlay.
	// Starting in OnPossess runs EnterState once but cannot schedule component ticks, and also observes an
	// uninitialised PatrolCenter. Start on the first controller tick after both actors are fully begun instead.
	StartAssignedStateTreeWhenReady();
	if (bAILogicSuspendedForDeath)
	{
		StopMovement();
		ClearDirect3DMovement();
		return;
	}
	TickDirect3DMovement();
	TickBoostBurst(DeltaSeconds);
	TickMovementRecovery(DeltaSeconds);
	KickCooldownRemaining = FMath::Max(0.0f, KickCooldownRemaining - FMath::Max(0.0f, DeltaSeconds));
	if (IsActionBlocked()) StopMovement(); // Second line of defense independent of StateTree wiring.
	if (!bLoggedMovement && GetBot() && FVector::DistSquared2D(PossessLocation, GetBot()->GetActorLocation()) >= FMath::Square(25.0f))
	{
		bLoggedMovement = true;
		UE_LOG(LogTemp, Display, TEXT("MSBotAI: moved on NavMesh bot=%s distance=%.0fcm"),
			*GetNameSafe(GetBot()), FVector::Dist2D(PossessLocation, GetBot()->GetActorLocation()));
	}
	TickEngagementTelemetry(DeltaSeconds);
	RunNavCoverageSweepOnce();
	TargetRefreshRemaining -= DeltaSeconds;
	const bool bTargetInvalid = !IsValid(CurrentTarget.Get()) ||
		(CurrentTarget->GetAbilitySystemComponent() && CurrentTarget->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	if (bTargetInvalid || TargetRefreshRemaining <= 0.0f) RefreshTarget();
}

bool AMSBotAIController::IsUsingDirect3DMovement() const
{
	const AMSBotCharacter* Bot = GetBot();
	const UMSCharacterMovementComponent* Movement = Bot
		? Cast<UMSCharacterMovementComponent>(Bot->GetCharacterMovement()) : nullptr;
	return Movement && Movement->IsZeroGravityEnabled();
}

bool AMSBotAIController::MoveToLocationOrDirect(const FVector& Destination,
	const float AcceptanceRadius, const bool bStopOnOverlap)
{
	if (bMovementRecoveryActive && !bIssuingMovementRecoveryRequest)
	{
		// StateTree tasks intentionally reissue their strategic goal every tick. Keep the
		// bounded escape request alive for its short window, unless a materially different
		// target arrives (for example, a newly selected hostile).
		if (bHasRecoveryOriginalGoal && ShouldKeepRecoveryForGoal(
			MovementRecoveryOriginalGoal, MovementRecoveryStrategicGoal,
			Destination, bRecoveryWasFollowingTaiganEntrance))
		{
			return true;
		}
		EndMovementRecovery(false);
	}
	if (!CanIssueMovementRequest()) return false;
	const float RequestAcceptanceRadius = AcceptanceRadius >= 0.0f ? AcceptanceRadius : 5.0f;
	if (!IsUsingDirect3DMovement())
	{
		ClearDirect3DMovement();
		UWorld* World = GetWorld();
		const bool bDirectGroundSteering = World && ShouldUseDirectGroundSteering(World->GetMapName());
		const bool bTaigan = World && IsTaiganNavigationMap(World->GetMapName());
		const FVector BotLocation = GetBot() ? GetBot()->GetActorLocation() : FVector::ZeroVector;
		const bool bCrossLayerGoal = bTaigan
			&& IsTaiganSubwayLocation(BotLocation) != IsTaiganSubwayLocation(Destination);
		const bool bPathMoving = GetMoveStatus() == EPathFollowingStatus::Moving;
		if (bTaigan && bHasActiveMovementRequest && bPathMoving)
		{
			if (bFollowingTaiganEntrance && FVector::Dist(Destination, TaiganStrategicGoal) <= 1000.0f)
			{
				return true; // Finish the authored ramp waypoint before reconsidering the hostile.
			}
			if (!bFollowingTaiganEntrance && ShouldReuseTaiganMove(
				LastMoveRequestDestination, Destination, LastMoveAcceptanceRadius,
				RequestAcceptanceRadius, bLastMoveStopOnOverlap, bStopOnOverlap, bPathMoving))
			{
				return true;
			}
		}
		if (bTaigan && LastMoveRequestResult == static_cast<int32>(EPathFollowingRequestResult::Failed)
			&& FVector::Dist(Destination, TaiganStrategicGoal) <= TaiganGoalRefreshDistanceCm
			&& World->GetTimeSeconds() - LastFailedTaiganMoveTime < TaiganFailedMoveRetrySeconds)
		{
			return false;
		}
		if (bDirectGroundSteering && !bLoggedDirectGroundSteering)
		{
			bLoggedDirectGroundSteering = true;
			UE_LOG(LogTemp, Display,
				TEXT("[P1:AI] Daibao direct ground steering enabled bot=%s; synchronous Recast path search bypassed"),
				*GetNameSafe(GetBot()));
		}
		const EPathFollowingRequestResult::Type RequestResult = MoveToLocation(
			Destination,
			AcceptanceRadius,
			bStopOnOverlap,
			!bDirectGroundSteering,
			!bDirectGroundSteering,
			true,
			{},
			!bCrossLayerGoal);
		EPathFollowingRequestResult::Type EffectiveResult = RequestResult;
		FVector EffectiveDestination = Destination;
		float EffectiveAcceptance = RequestAcceptanceRadius;
		bool bEffectiveStopOnOverlap = bStopOnOverlap;
		bFollowingTaiganEntrance = false;
		TaiganStrategicGoal = Destination;
		if (bCrossLayerGoal && RequestResult == EPathFollowingRequestResult::Failed && !bIssuingMovementRecoveryRequest)
		{
			// The surface goal may be on a different nav layer. Only follow a complete
			// path to a paired entrance; never accept the closest partial path endpoint.
			for (const FVector& Waypoint : CollectTaiganEntranceWaypoints(World, BotLocation, Destination))
			{
				const EPathFollowingRequestResult::Type EntranceResult = MoveToLocation(
					Waypoint, TaiganEntranceAcceptanceCm, false, true, true, true, {}, false);
				if (EntranceResult != EPathFollowingRequestResult::RequestSuccessful)
				{
					continue;
				}
				EffectiveResult = EntranceResult;
				EffectiveDestination = Waypoint;
				EffectiveAcceptance = TaiganEntranceAcceptanceCm;
				bEffectiveStopOnOverlap = false;
				bFollowingTaiganEntrance = true;
				UE_LOG(LogTemp, Display, TEXT("[NYT:AI] %s routing via entrance %s toward %s"),
					*GetNameSafe(GetBot()), *Waypoint.ToCompactString(), *Destination.ToCompactString());
				break;
			}
		}
		// #228: AlreadyAtGoal(=目的地が現在地に潰れている)とFailedを区別して記録する。
		LastMoveRequestResult = static_cast<int32>(EffectiveResult);
		LastMoveRequestDestination = EffectiveDestination;
		LastMoveAcceptanceRadius = EffectiveAcceptance;
		bLastMoveStopOnOverlap = bEffectiveStopOnOverlap;
		const bool bAccepted = EffectiveResult != EPathFollowingRequestResult::Failed;
		if (bTaigan) LastFailedTaiganMoveTime = bAccepted ? -1000.0f : World->GetTimeSeconds();
		bHasActiveMovementRequest = bAccepted;
		if (bAccepted && LastProgressSampleLocation.IsNearlyZero())
		{
			LastProgressSampleLocation = GetBot() ? GetBot()->GetActorLocation() : Destination;
		}
		if (!bAccepted && !bLoggedMoveRequestRejected)
		{
			// #209: distinguishes "the bot never received a destination" from "the bot has a
			// destination but the movement request itself is being rejected every time".
			bLoggedMoveRequestRejected = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[P1:AI] stage=MoveRejected bot=%s destination=%s directGroundSteering=%d"),
				*GetNameSafe(GetBot()), *Destination.ToCompactString(), bDirectGroundSteering ? 1 : 0);
		}
		return bAccepted;
	}

	StopMovement();
	Direct3DDestination = Destination;
	Direct3DAcceptanceRadius = RequestAcceptanceRadius;
	LastMoveRequestDestination = Destination;
	LastMoveAcceptanceRadius = RequestAcceptanceRadius;
	bHasActiveMovementRequest = true;
	if (LastProgressSampleLocation.IsNearlyZero())
	{
		LastProgressSampleLocation = GetBot() ? GetBot()->GetActorLocation() : Destination;
	}
	return true;
}

void AMSBotAIController::StopMovement()
{
	Super::StopMovement();
	ClearDirect3DMovement();
	bHasActiveMovementRequest = false;
	bFollowingTaiganEntrance = false;
	MovementStallDuration = 0.0f;
	bMovementRecoveryActive = false;
	bHasRecoveryOriginalGoal = false;
	bRecoveryWasFollowingTaiganEntrance = false;
	MovementRecoveryRemaining = 0.0f;
	if (const AMSBotCharacter* Bot = GetBot())
	{
		LastProgressSampleLocation = Bot->GetActorLocation();
	}
}

bool AMSBotAIController::ShouldRecoverFromStall(const float GoalDistanceCm,
	const float AcceptanceRadiusCm, const float StallDurationSeconds, const float DisplacementCm,
	const bool bHasActiveMovementRequest, const bool bActionBlocked, const bool bGrounded)
{
	if (!bHasActiveMovementRequest || bActionBlocked || !bGrounded)
	{
		return false;
	}
	const float GoalThreshold = FMath::Max(MovementRecoveryMinGoalDistanceCm,
		FMath::Max(0.0f, AcceptanceRadiusCm) * 2.0f);
	return GoalDistanceCm > GoalThreshold
		&& StallDurationSeconds >= MovementRecoveryStallWindowSeconds
		&& DisplacementCm < MovementRecoveryMinDisplacementCm;
}

FVector AMSBotAIController::CalculateLocalEscapeCandidate(const FVector& CurrentLocation,
	const FVector& GoalLocation, const float OffsetCm, const int32 Direction)
{
	FVector Forward = (GoalLocation - CurrentLocation).GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal2D();
	const float Sign = Direction < 0 ? -1.0f : 1.0f;
	return CurrentLocation + Lateral * (FMath::Max(0.0f, OffsetCm) * Sign);
}

bool AMSBotAIController::FindLocalEscapePoint(const FVector& GoalLocation, const int32 Direction,
	FVector& OutPoint) const
{
	const AMSBotCharacter* Bot = GetBot();
	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = Bot ? Bot->GetCapsuleComponent() : nullptr;
	if (!Bot || !World || !Capsule || IsUsingDirect3DMovement())
	{
		return false;
	}

	const FVector CurrentLocation = Bot->GetActorLocation();
	const float Radius = FMath::Max(1.0f, Capsule->GetScaledCapsuleRadius());
	const float HalfHeight = FMath::Max(Radius, Capsule->GetScaledCapsuleHalfHeight());
	const float EscapeOffset = FMath::Clamp(Radius * 4.0f,
		MovementRecoveryMinEscapeOffsetCm, MovementRecoveryMaxEscapeOffsetCm);
	const float GroundProbeUp = FMath::Max(300.0f, HalfHeight + 100.0f);
	const float GroundProbeDown = HalfHeight + 300.0f;
	constexpr float FloorClearanceCm = 2.0f;
	const FVector Forward = (GoalLocation - CurrentLocation).GetSafeNormal2D().IsNearlyZero()
		? Bot->GetActorForwardVector().GetSafeNormal2D() : (GoalLocation - CurrentLocation).GetSafeNormal2D();
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal2D();
	const float Sign = Direction < 0 ? -1.0f : 1.0f;
	const FVector Backward = -Forward * (EscapeOffset * 0.5f);
	const TArray<FVector> CandidateOffsets = bPreferBackwardEscape
		? TArray<FVector>{
			Backward,
			Lateral * (EscapeOffset * Sign),
			Lateral * (-EscapeOffset * Sign),
			Lateral * (EscapeOffset * Sign) + Forward * (EscapeOffset * 0.35f),
			Lateral * (-EscapeOffset * Sign) + Forward * (EscapeOffset * 0.35f)}
		: TArray<FVector>{
			Lateral * (EscapeOffset * Sign),
			Lateral * (-EscapeOffset * Sign),
			Lateral * (EscapeOffset * Sign) + Forward * (EscapeOffset * 0.35f),
			Lateral * (-EscapeOffset * Sign) + Forward * (EscapeOffset * 0.35f),
			Backward};

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotMovementRecovery), false);
	QueryParams.AddIgnoredActor(Bot);
	for (const FVector& Offset : CandidateOffsets)
	{
		const FVector ProbeCenter = CurrentLocation + Offset;
		const FVector TraceStart = ProbeCenter + FVector(0.0f, 0.0f, GroundProbeUp);
		const FVector TraceEnd = ProbeCenter - FVector(0.0f, 0.0f, GroundProbeDown);
		FHitResult GroundHit;
		if (!World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams)
			|| GroundHit.ImpactNormal.Z < MovementRecoveryMinGroundNormalZ)
		{
			continue;
		}

		const FVector Candidate = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + FloorClearanceCm);
		const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		if (World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn,
			CapsuleShape, QueryParams))
		{
			continue;
		}
		FHitResult SweepHit;
		if (World->SweepSingleByChannel(SweepHit, CurrentLocation, Candidate, FQuat::Identity,
			ECC_Pawn, CapsuleShape, QueryParams))
		{
			if (!SweepHit.bStartPenetrating)
			{
				continue;
			}
			// A bot already embedded in a wall/floor needs to be allowed to leave it, but
			// only within a bounded depenetration. Use the reported normal/depth to validate
			// a temporary sweep start; the runtime request still moves through CharacterMovement
			// and never teleports the actor.
			const FVector PenetrationNormal = SweepHit.ImpactNormal.GetSafeNormal();
			const float PenetrationDepth = FMath::Max(0.0f, SweepHit.PenetrationDepth);
			const float MaxInitialPenetration = FMath::Max(Radius * 2.0f, HalfHeight * 0.75f);
			if (PenetrationNormal.IsNearlyZero() || PenetrationDepth > MaxInitialPenetration)
			{
				continue;
			}
			const float DepenetrationDistance = FMath::Max(
				FMath::Max(PenetrationDepth, FloorClearanceCm) + FloorClearanceCm, 50.0f);
			const FVector DepenetratedStart = CurrentLocation + PenetrationNormal * DepenetrationDistance;
			if (World->OverlapBlockingTestByChannel(DepenetratedStart, FQuat::Identity,
				ECC_Pawn, CapsuleShape, QueryParams))
			{
				continue;
			}
			FHitResult ContinuedSweepHit;
			if (World->SweepSingleByChannel(ContinuedSweepHit, DepenetratedStart, Candidate,
				FQuat::Identity, ECC_Pawn, CapsuleShape, QueryParams))
			{
				continue;
			}
		}
		OutPoint = Candidate;
		return true;
	}
	return false;
}

bool AMSBotAIController::TryStartMovementRecovery()
{
	AMSBotCharacter* Bot = GetBot();
	UCharacterMovementComponent* Movement = Bot ? Bot->GetCharacterMovement() : nullptr;
	if (!Bot || !Movement || bMovementRecoveryActive || MovementRecoveryCooldownRemaining > 0.0f
		|| !bHasActiveMovementRequest || IsUsingDirect3DMovement())
	{
		return false;
	}
	const float GoalDistance = FVector::Dist2D(Bot->GetActorLocation(), LastMoveRequestDestination);
	const float Displacement = FVector::Dist2D(Bot->GetActorLocation(), LastProgressSampleLocation);
	if (!ShouldRecoverFromStall(GoalDistance, LastMoveAcceptanceRadius, MovementStallDuration,
		Displacement, bHasActiveMovementRequest, IsActionBlocked(), Movement->IsMovingOnGround()))
	{
		return false;
	}

	FVector EscapePoint;
	if (!FindLocalEscapePoint(LastMoveRequestDestination, MovementRecoveryDirection, EscapePoint))
	{
		// Do not run five capsule sweeps every frame when the surrounding geometry has no
		// usable ground candidate. A later attempt can succeed after another unit moves away.
		MovementRecoveryCooldownRemaining = MovementRecoveryCooldownSeconds;
		return false;
	}
	const FVector OriginalGoal = LastMoveRequestDestination;
	const float OriginalAcceptance = LastMoveAcceptanceRadius;
	const bool bOriginalStopOnOverlap = bLastMoveStopOnOverlap;
	const bool bWasFollowingEntrance = bFollowingTaiganEntrance;
	const FVector StrategicGoal = bWasFollowingEntrance ? TaiganStrategicGoal : OriginalGoal;
	StopMovement();
	bIssuingMovementRecoveryRequest = true;
	const bool bAccepted = MoveToLocationOrDirect(
		EscapePoint, FMath::Min(OriginalAcceptance, 100.0f), /*bStopOnOverlap=*/false);
	bIssuingMovementRecoveryRequest = false;
	if (!bAccepted)
	{
		// Keep the accepted strategic goal alive if the short escape request is rejected
		// by path following or a transient collision state.
		bIssuingMovementRecoveryRequest = true;
		const bool bRestored = MoveToLocationOrDirect(OriginalGoal, OriginalAcceptance,
			bOriginalStopOnOverlap);
		bIssuingMovementRecoveryRequest = false;
		if (bRestored && bWasFollowingEntrance && GetMoveStatus() == EPathFollowingStatus::Moving)
		{
			bFollowingTaiganEntrance = true;
			TaiganStrategicGoal = StrategicGoal;
		}
		MovementRecoveryCooldownRemaining = MovementRecoveryCooldownSeconds;
		return false;
	}

	bMovementRecoveryActive = true;
	bHasRecoveryOriginalGoal = true;
	MovementRecoveryOriginalGoal = OriginalGoal;
	MovementRecoveryStrategicGoal = StrategicGoal;
	MovementRecoveryEscapeGoal = EscapePoint;
	LastMovementRecoveryDestination = EscapePoint;
	MovementRecoveryOriginalAcceptanceRadius = OriginalAcceptance;
	bMovementRecoveryOriginalStopOnOverlap = bOriginalStopOnOverlap;
	bRecoveryWasFollowingTaiganEntrance = bWasFollowingEntrance;
	MovementRecoveryAcceptanceRadius = FMath::Min(OriginalAcceptance, 100.0f);
	MovementRecoveryRemaining = MovementRecoveryDurationSeconds;
	MovementRecoveryDirection *= -1;
	bPreferBackwardEscape = !bPreferBackwardEscape;
	MovementRecoveryCooldownRemaining = MovementRecoveryCooldownSeconds;
	MovementStallDuration = 0.0f;
	LastProgressSampleLocation = Bot->GetActorLocation();
	++MovementRecoveryCount;
	if (CurrentTarget.IsValid())
	{
		ConsecutiveTargetStalls = LastStalledTarget.Get() == CurrentTarget.Get()
			? ConsecutiveTargetStalls + 1 : 1;
		LastStalledTarget = CurrentTarget;
		if (ConsecutiveTargetStalls >= 3)
		{
			// Three real, validated escape attempts against the same hostile indicate
			// a persistently blocked approach. Try another nearby enemy for five seconds.
			// If there is no alternative, RefreshTarget still keeps this one.
			AvoidedStalledTarget = CurrentTarget;
			AvoidStalledTargetUntil = GetWorld()->GetTimeSeconds() + 5.0f;
			ConsecutiveTargetStalls = 0;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[P1:AI] movement recovery bot=%s reason=stalled goal=%s escape=%s count=%d"),
		*GetNameSafe(Bot), *OriginalGoal.ToCompactString(), *EscapePoint.ToCompactString(),
		MovementRecoveryCount);
	return true;
}

void AMSBotAIController::EndMovementRecovery(const bool bRepathOriginalGoal)
{
	const bool bCanRepath = bRepathOriginalGoal && bHasRecoveryOriginalGoal;
	const FVector OriginalGoal = MovementRecoveryOriginalGoal;
	const FVector StrategicGoal = MovementRecoveryStrategicGoal;
	const float OriginalAcceptance = MovementRecoveryOriginalAcceptanceRadius;
	const bool bOriginalStopOnOverlap = bMovementRecoveryOriginalStopOnOverlap;
	const bool bWasFollowingEntrance = bRecoveryWasFollowingTaiganEntrance;
	bMovementRecoveryActive = false;
	bHasRecoveryOriginalGoal = false;
	bRecoveryWasFollowingTaiganEntrance = false;
	MovementRecoveryRemaining = 0.0f;
	bHasActiveMovementRequest = false;
	AAIController::StopMovement();
	ClearDirect3DMovement();
	if (bCanRepath && CanIssueMovementRequest())
	{
		bIssuingMovementRecoveryRequest = true;
		const bool bRestored = MoveToLocationOrDirect(OriginalGoal, OriginalAcceptance,
			bOriginalStopOnOverlap);
		bIssuingMovementRecoveryRequest = false;
		if (bRestored && bWasFollowingEntrance && GetMoveStatus() == EPathFollowingStatus::Moving)
		{
			bFollowingTaiganEntrance = true;
			TaiganStrategicGoal = StrategicGoal;
		}
	}
}

void AMSBotAIController::TickMovementRecovery(const float DeltaSeconds)
{
	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	MovementRecoveryCooldownRemaining = FMath::Max(0.0f,
		MovementRecoveryCooldownRemaining - SafeDeltaSeconds);
	AMSBotCharacter* Bot = GetBot();
	UCharacterMovementComponent* Movement = Bot ? Bot->GetCharacterMovement() : nullptr;
	if (!Bot || !Movement)
	{
		return;
	}
	if (StateTreeComponent && !StateTreeComponent->IsRunning())
	{
		if (bMovementRecoveryActive)
		{
			EndMovementRecovery(false);
		}
		else
		{
			bHasActiveMovementRequest = false;
			MovementStallDuration = 0.0f;
			LastProgressSampleLocation = Bot->GetActorLocation();
		}
		return;
	}
	if (bMovementRecoveryActive)
	{
		if (IsActionBlocked())
		{
			EndMovementRecovery(false);
			return;
		}
		MovementRecoveryRemaining -= SafeDeltaSeconds;
		const float EscapeDistance = FVector::Dist2D(Bot->GetActorLocation(), MovementRecoveryEscapeGoal);
		const float EscapeAcceptance = FMath::Max(100.0f, MovementRecoveryAcceptanceRadius);
		if (EscapeDistance <= EscapeAcceptance || MovementRecoveryRemaining <= 0.0f)
		{
			EndMovementRecovery(true);
		}
		return;
	}
	if (!bHasActiveMovementRequest || IsUsingDirect3DMovement()
		|| (!IsUsingDirectGroundSteering() && GetMoveStatus() != EPathFollowingStatus::Moving))
	{
		MovementStallDuration = 0.0f;
		LastProgressSampleLocation = Bot->GetActorLocation();
		return;
	}
	const float GoalDistance = FVector::Dist2D(Bot->GetActorLocation(), LastMoveRequestDestination);
	const float GoalThreshold = FMath::Max(MovementRecoveryMinGoalDistanceCm,
		LastMoveAcceptanceRadius * 2.0f);
	if (GoalDistance <= GoalThreshold || !Movement->IsMovingOnGround())
	{
		MovementStallDuration = 0.0f;
		LastProgressSampleLocation = Bot->GetActorLocation();
		return;
	}
	const float Displacement = FVector::Dist2D(Bot->GetActorLocation(), LastProgressSampleLocation);
	if (HasMovementProgress(Displacement, Movement->Velocity.Size2D(),
		IsTaiganNavigationMap(GetWorld()->GetMapName())))
	{
		MovementStallDuration = 0.0f;
		LastProgressSampleLocation = Bot->GetActorLocation();
		ConsecutiveTargetStalls = 0;
		return;
	}
	MovementStallDuration += SafeDeltaSeconds;
	TryStartMovementRecovery();
}

bool AMSBotAIController::ShouldUseDirectGroundSteering(const FString& MapName)
{
	return MapName.Contains(TEXT("Lvl_Biome_Daibao"), ESearchCase::IgnoreCase);
}

bool AMSBotAIController::IsTaiganNavigationMap(const FString& MapName)
{
	return MapName.Contains(TEXT("Lvl_NewYork_Taigan"), ESearchCase::IgnoreCase);
}

bool AMSBotAIController::IsTaiganSubwayLocation(const FVector& Location)
{
	// Subway roof is at -3400cm; a capsule centre below -2500cm belongs to
	// the underground layer, unlike above-ground rooftops at +3000..+7000cm.
	return Location.Z < -2500.0f;
}

bool AMSBotAIController::ShouldReuseTaiganMove(const FVector& ActiveGoal, const FVector& RequestedGoal,
	const float ActiveAcceptanceRadius, const float RequestedAcceptanceRadius,
	const bool bActiveStopOnOverlap, const bool bRequestedStopOnOverlap, const bool bPathMoving)
{
	return bPathMoving && bActiveStopOnOverlap == bRequestedStopOnOverlap
		&& FMath::IsNearlyEqual(ActiveAcceptanceRadius, RequestedAcceptanceRadius, 1.0f)
		&& FVector::DistSquared(ActiveGoal, RequestedGoal) <= FMath::Square(TaiganGoalRefreshDistanceCm);
}

bool AMSBotAIController::HasMovementProgress(const float DisplacementCm,
	const float SpeedCmPerSecond, const bool bTaigan)
{
	// A bot can report high instantaneous velocity while colliding back and forth within
	// a few centimetres. On Taigan, only net progress should cancel stall recovery.
	return DisplacementCm >= MovementRecoveryMinDisplacementCm
		|| (!bTaigan && SpeedCmPerSecond >= MovementRecoveryMinSpeedCmPerSecond);
}

float AMSBotAIController::CalculateTaiganRVOConsiderationRadius(const float CapsuleRadiusCm,
	const float MaxWalkSpeedCmPerSecond, const float ConfiguredRadiusCm)
{
	return FMath::Max(ConfiguredRadiusCm,
		FMath::Max(FMath::Max(0.0f, CapsuleRadiusCm) * 6.0f,
			FMath::Max(0.0f, MaxWalkSpeedCmPerSecond) * 0.45f));
}

bool AMSBotAIController::ShouldKeepRecoveryForGoal(const FVector& RecoveryWaypoint,
	const FVector& StrategicGoal, const FVector& RequestedGoal, const bool bFollowingEntrance)
{
	// An entrance detour is an implementation step, not the StateTree's objective.
	// Compare incoming Chase/Attack requests with the objective so the short
	// collision-escape move can finish before restoring the entrance path.
	return bFollowingEntrance
		? FVector::Dist(StrategicGoal, RequestedGoal) <= MovementRecoveryRetargetDistanceCm
		: FVector::Dist2D(RecoveryWaypoint, RequestedGoal) <= MovementRecoveryRetargetDistanceCm;
}

TArray<FVector> AMSBotAIController::CollectTaiganEntranceWaypoints(UWorld* World,
	const FVector& BotLocation, const FVector& StrategicGoal)
{
	TArray<FVector> Waypoints;
	if (!World || !IsTaiganNavigationMap(World->GetMapName())
		|| IsTaiganSubwayLocation(BotLocation) == IsTaiganSubwayLocation(StrategicGoal))
	{
		return Waypoints;
	}
	TMap<FName, FTaiganEntrancePair> Pairs;
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		const ATargetPoint* Marker = *It;
		if (!IsValid(Marker) || !Marker->ActorHasTag(FName(TEXT("NYT_NavEntrance")))) continue;
		const bool bBottom = Marker->ActorHasTag(FName(TEXT("NYT_NavBottom")));
		const bool bTop = Marker->ActorHasTag(FName(TEXT("NYT_NavTop")));
		if (bBottom == bTop) continue;
		for (const FName Tag : Marker->Tags)
		{
			if (!Tag.ToString().StartsWith(TEXT("NYT_NavEntry_"))) continue;
			FTaiganEntrancePair& Pair = Pairs.FindOrAdd(Tag);
			if (bBottom) Pair.Bottom = Marker;
			else Pair.Top = Marker;
			break;
		}
	}
	struct FCandidate { FVector Waypoint; double Score; };
	TArray<FCandidate> Candidates;
	for (const TPair<FName, FTaiganEntrancePair>& Item : Pairs)
	{
		const FTaiganEntrancePair& Pair = Item.Value;
		if (!IsValid(Pair.Bottom) || !IsValid(Pair.Top)) continue;
		const FVector Bottom = Pair.Bottom->GetActorLocation();
		const FVector Top = Pair.Top->GetActorLocation();
		if (Bottom.Z >= Top.Z - 2500.0f) continue;
		const bool bGoingUp = StrategicGoal.Z > BotLocation.Z;
		const FVector NearEnd = bGoingUp ? Bottom : Top;
		const FVector FarEnd = bGoingUp ? Top : Bottom;
		const bool bAtNearEnd = FVector::Dist2D(BotLocation, NearEnd) <= TaiganEntranceStageDistanceCm
			&& FMath::Abs(BotLocation.Z - NearEnd.Z) <= 1500.0f;
		Candidates.Add({bAtNearEnd ? FarEnd : NearEnd,
			FVector::Dist2D(BotLocation, NearEnd) + FVector::Dist2D(FarEnd, StrategicGoal)});
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
	for (const FCandidate& Candidate : Candidates) Waypoints.Add(Candidate.Waypoint);
	return Waypoints;
}

bool AMSBotAIController::IsUsingDirectGroundSteering() const
{
	const UWorld* World = GetWorld();
	return World && ShouldUseDirectGroundSteering(World->GetMapName());
}

void AMSBotAIController::TickDirect3DMovement()
{
	AMSBotCharacter* Bot = GetBot();
	if (!Direct3DDestination.IsSet() || !Bot || !IsUsingDirect3DMovement()) return;
	if (IsActionBlocked())
	{
		ClearDirect3DMovement();
		return;
	}
	const FVector Delta = Direct3DDestination.GetValue() - Bot->GetActorLocation();
	if (Delta.SizeSquared() <= FMath::Square(Direct3DAcceptanceRadius))
	{
		ClearDirect3DMovement();
		return;
	}
	Bot->AddMovementInput(Delta.GetSafeNormal());
}

void AMSBotAIController::ClearDirect3DMovement()
{
	Direct3DDestination.Reset();
}

void AMSBotAIController::TickBoostBurst(const float DeltaSeconds)
{
	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	if (IsBoostBursting())
	{
		if (IsActionBlocked())
		{
			EndBoostBurst(true);
		}
		else
		{
			if (SafeDeltaSeconds >= BoostBurstRemaining) EndBoostBurst(true);
			else BoostBurstRemaining -= SafeDeltaSeconds;
		}
	}
	else
	{
		BoostBurstCooldownRemaining = FMath::Max(0.0f, BoostBurstCooldownRemaining - SafeDeltaSeconds);
	}
}

bool AMSBotAIController::TryStartBoostBurstInAttack()
{
	return TryStartBoostBurst(GetTargetDistance());
}

bool AMSBotAIController::TryStartBoostBurst(const float TargetDistanceCm)
{
	AMSBotCharacter* Bot = GetBot();
	UCharacterMovementComponent* Movement = Bot ? Bot->GetCharacterMovement() : nullptr;
	UAbilitySystemComponent* ASC = Bot ? Bot->GetAbilitySystemComponent() : nullptr;
	if (!Bot || !Movement || !ASC || IsActionBlocked() || IsBoostBursting() || BoostBurstCooldownRemaining > 0.0f)
		return false;

	const FMSBotAIConfig& Config = Bot->GetAIConfig();
	if (TargetDistanceCm <= CalculateBoostBurstTriggerDistance(Bot->GetEnemyTier(), Config)) return false;

	PreBoostMaxWalkSpeed = Movement->MaxWalkSpeed;
	PreBoostMaxAcceleration = Movement->MaxAcceleration;
	Movement->MaxWalkSpeed = PreBoostMaxWalkSpeed * Config.BoostBurstSpeedMultiplier;
	Movement->MaxAcceleration = FMath::Max(
		PreBoostMaxAcceleration * Config.BoostBurstSpeedMultiplier,
		PreBoostMaxAcceleration + Config.BoostBurstAccelerationMinBonus);
	BoostBurstRemaining = Config.BoostBurstDuration;
	ASC->AddLooseGameplayTag(MSTags::State_Boosting);
	return true;
}

void AMSBotAIController::EndBoostBurst(const bool bStartCooldown)
{
	AMSBotCharacter* Bot = GetBot();
	if (IsBoostBursting())
	{
		if (UCharacterMovementComponent* Movement = Bot ? Bot->GetCharacterMovement() : nullptr)
		{
			Movement->MaxWalkSpeed = PreBoostMaxWalkSpeed;
			Movement->MaxAcceleration = PreBoostMaxAcceleration;
		}
		if (UAbilitySystemComponent* ASC = Bot ? Bot->GetAbilitySystemComponent() : nullptr)
			ASC->RemoveLooseGameplayTag(MSTags::State_Boosting);
	}
	BoostBurstRemaining = 0.0f;
	if (bStartCooldown && Bot)
	{
		const FMSBotAIConfig& Config = Bot->GetAIConfig();
		BoostBurstCooldownRemaining = Config.BoostBurstCooldown
			* CalculateBoostBurstCooldownMultiplier(Bot->GetEnemyTier(), Config);
	}
	else if (!bStartCooldown)
	{
		BoostBurstCooldownRemaining = 0.0f;
	}
}

void AMSBotAIController::RefreshTarget()
{
	AMSBotCharacter* Bot = GetBot();
	if (!Bot) { CurrentTarget.Reset(); return; }
	TargetRefreshRemaining = Bot->GetAIConfig().TargetRefreshInterval;
	if (bMovementRecoveryActive) return; // Finish the short escape before changing the strategic goal.
	if (UMSUnitRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>())
	{
		const float SearchRange = Bot->GetAIConfig().LoseSightRange;
		AMSUnitBase* Nearest = Registry->FindNearestHostileUnit(Bot, SearchRange);
		AMSUnitBase* Best = Nearest;
		if (Nearest)
		{
			// Count only local teammates. A distant squad's target choice should not pull
			// this bot toward an unrelated fight across the map.
			TMap<const AMSUnitBase*, int32> PursuerCounts;
			Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
			{
				const AMSBotCharacter* Ally = Cast<AMSBotCharacter>(Unit);
				const AMSBotAIController* AllyController = Ally
					? Cast<AMSBotAIController>(Ally->GetController()) : nullptr;
				const UAbilitySystemComponent* AllyASC = Ally ? Ally->GetAbilitySystemComponent() : nullptr;
				if (Ally && Ally != Bot && AllyController && Ally->GetGenericTeamId() == Bot->GetGenericTeamId()
					&& (!AllyASC || !AllyASC->HasMatchingGameplayTag(MSTags::State_Dead))
					&& FVector::Dist2D(Ally->GetActorLocation(), Bot->GetActorLocation()) <= 8000.0f
					&& FMath::Abs(Ally->GetActorLocation().Z - Bot->GetActorLocation().Z) <= 2000.0f
					&& AllyController->GetCurrentTarget())
					++PursuerCounts.FindOrAdd(AllyController->GetCurrentTarget());
			});
			const float NearestDistance = FVector::Dist(Bot->GetActorLocation(), Nearest->GetActorLocation());
			const bool bTaigan = IsTaiganNavigationMap(GetWorld()->GetMapName());
			float BestScore = TNumericLimits<float>::Max();
			Registry->ForEachRegisteredUnit([&](AMSUnitBase* Candidate)
			{
				if (!UMSTeamStatics::CanDamage(Bot, Candidate)) return;
				const UAbilitySystemComponent* ASC = Candidate->GetAbilitySystemComponent();
				if (!ASC || ASC->HasMatchingGameplayTag(MSTags::State_Dead)) return;
				const float Distance = FVector::Dist(Bot->GetActorLocation(), Candidate->GetActorLocation());
				if (Distance > SearchRange || Distance > NearestDistance + 4000.0f) return;
				const bool bCrossLayer = bTaigan &&
					IsTaiganSubwayLocation(Bot->GetActorLocation()) != IsTaiganSubwayLocation(Candidate->GetActorLocation());
				const float Score = CalculateTargetPreferenceScore(Distance,
					PursuerCounts.FindRef(Candidate), Candidate == CurrentTarget.Get(), bCrossLayer,
					Candidate == AvoidedStalledTarget.Get() && GetWorld()->GetTimeSeconds() < AvoidStalledTargetUntil);
				if (Score < BestScore) { BestScore = Score; Best = Candidate; }
			});
		}
		CurrentTarget = Best;
	}
	if (!bLoggedTargetAcquired && CurrentTarget.IsValid())
	{
		bLoggedTargetAcquired = true;
		UE_LOG(LogTemp, Display, TEXT("[P1:AI] stage=TargetAcquired bot=%s target=%s distance=%.0fcm detection=%.0fcm"),
			*GetNameSafe(Bot), *GetNameSafe(CurrentTarget.Get()), GetTargetDistance(),
			Bot->GetAIConfig().DetectionRange);
	}
}

float AMSBotAIController::CalculateObjectiveScore(const float DistanceCm,
	const int32 OtherAssignments, const bool bOwnedByTeam, const bool bCurrentObjective)
{
	// A point's capture rate stops improving after three capturers. This penalty sends
	// subsequent bots to another sector while distance still matters on large maps.
	return FMath::Max(0.0f, DistanceCm) + FMath::Max(0, OtherAssignments) * 50000.0f
		+ (bOwnedByTeam ? 1000000.0f : 0.0f) - (bCurrentObjective ? 18000.0f : 0.0f);
}

FVector AMSBotAIController::CalculateObjectiveApproachPoint(const FVector& ObjectiveLocation,
	const float CaptureRadiusCm, const float CapsuleRadiusCm, const uint32 SlotIndex)
{
	// Stable, non-collinear destinations inside the actual capture zone. The spiral
	// avoids a marching line and gives large capsules room to settle around the point.
	const float Angle = static_cast<float>(SlotIndex) * 2.39996323f;
	const float Radius = FMath::Min(FMath::Max(0.0f, CaptureRadiusCm) * 0.72f,
		FMath::Max(900.0f, CapsuleRadiusCm * 3.0f) * FMath::Sqrt(static_cast<float>(SlotIndex) + 0.5f));
	return ObjectiveLocation + FVector(FMath::Cos(Angle) * Radius,
		FMath::Sin(Angle) * Radius, 0.0f);
}

AMSCapturePoint* AMSBotAIController::ChooseStrategicObjective()
{
	AMSBotCharacter* Bot = GetBot();
	UWorld* World = GetWorld();
	if (!Bot || !World) { StrategicObjective.Reset(); return nullptr; }
	TArray<AMSCapturePoint*> Points;
	bool bHasUnownedPoint = false;
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
	{
		AMSCapturePoint* Point = *It;
		if (!IsValid(Point) || Point->IsCaptureLocked()) continue;
		Points.Add(Point);
		bHasUnownedPoint |= !Point->CanDeployFor(Bot->GetGenericTeamId());
	}
	if (!bHasUnownedPoint) { StrategicObjective.Reset(); return nullptr; }
	AMSCapturePoint* Best = nullptr;
	uint32 BestAvailableSlot = 0;
	float BestScore = TNumericLimits<float>::Max();
	for (AMSCapturePoint* Point : Points)
	{
		const bool bOwned = Point->CanDeployFor(Bot->GetGenericTeamId());
		if (bOwned) continue;
		int32 Claims = 0;
		TSet<uint32> OccupiedSlots;
		for (TActorIterator<AMSBotAIController> It(World); It; ++It)
		{
			const AMSBotAIController* AllyController = *It;
			const AMSBotCharacter* Ally = AllyController ? AllyController->GetBot() : nullptr;
			const UAbilitySystemComponent* AllyASC = Ally ? Ally->GetAbilitySystemComponent() : nullptr;
			if (AllyController != this && Ally && Ally->GetGenericTeamId() == Bot->GetGenericTeamId()
				&& AllyController->GetStrategicObjective() == Point
				&& (!AllyASC || !AllyASC->HasMatchingGameplayTag(MSTags::State_Dead)))
			{
				++Claims;
				OccupiedSlots.Add(AllyController->GetStrategicObjectiveSlot());
			}
		}
		const float Distance = FVector::Dist2D(Bot->GetActorLocation(), Point->GetActorLocation());
		const float LayerPenalty = IsTaiganNavigationMap(World->GetMapName())
			&& IsTaiganSubwayLocation(Bot->GetActorLocation()) != IsTaiganSubwayLocation(Point->GetActorLocation())
			? 40000.0f : 0.0f;
		const float Score = CalculateObjectiveScore(Distance, Claims, bOwned,
			Point == StrategicObjective.Get()) + LayerPenalty;
		if (Score < BestScore)
		{
			BestScore = Score; Best = Point;
			BestAvailableSlot = Point == StrategicObjective.Get() ? StrategicObjectiveSlot : 0;
			while (OccupiedSlots.Contains(BestAvailableSlot)) ++BestAvailableSlot;
		}
	}
	StrategicObjectiveSlot = BestAvailableSlot;
	StrategicObjective = Best;
	return Best;
}

float AMSBotAIController::CalculateTargetPreferenceScore(const float DistanceCm,
	const int32 OtherPursuers, const bool bCurrentTarget, const bool bCrossNavigationLayer,
	const bool bRecentlyStalledTarget)
{
	return FMath::Max(0.0f, DistanceCm) + FMath::Max(0, OtherPursuers) * 900.0f
		+ (bCrossNavigationLayer ? 2000.0f : 0.0f) + (bRecentlyStalledTarget ? 5000.0f : 0.0f)
		- (bCurrentTarget ? 450.0f : 0.0f);
}

void AMSBotAIController::TickEngagementTelemetry(const float DeltaSeconds)
{
	// #209: 「攻撃しない」がどの段階で止まっているかを実測するための定期テレメトリ。
	// 1体あたり15秒に1行。索敵範囲外の最寄り敵距離も出すことで、BOTが交戦距離へ
	// 収束しているのか、それとも全く近づけていないのかを区別できる。
	EngagementTelemetryRemaining -= FMath::Max(0.0f, DeltaSeconds);
	if (EngagementTelemetryRemaining > 0.0f) return;
	EngagementTelemetryRemaining = EngagementTelemetryInterval;
	const AMSBotCharacter* Bot = GetBot();
	const UCharacterMovementComponent* Movement = Bot ? Bot->GetCharacterMovement() : nullptr;
	if (!Bot || !Movement) return;
	const UMSUnitRegistrySubsystem* Registry = GetWorld()
		? GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>() : nullptr;
	const AMSUnitBase* NearestHostile = Registry
		? Registry->FindNearestHostileUnit(Bot, TNumericLimits<float>::Max()) : nullptr;
	const float NearestHostileDistance = NearestHostile
		? FVector::Dist(Bot->GetActorLocation(), NearestHostile->GetActorLocation()) : -1.0f;
	// 「落下モードのまま速度0で固まる」= ジオメトリ内部にめり込んで床が見つからない状態。
	// 直下の床までの距離と貫通の有無まで出して、配置の失敗を座標レベルで特定できるようにする。
	const FVector Location = Bot->GetActorLocation();
	float FloorDistance = -1.0f;
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(MSBotTelemetryFloor), /*bTraceComplex=*/false);
	FloorParams.AddIgnoredActor(Bot);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, Location,
		Location - FVector(0.0f, 0.0f, 500000.0f), ECC_WorldStatic, FloorParams))
	{
		FloorDistance = Location.Z - FloorHit.ImpactPoint.Z;
	}
	const bool bPenetrating = GetWorld() && GetWorld()->OverlapBlockingTestByChannel(
		Location, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(50.0f), FloorParams);
	// #228: 「移動要求は出ているのに速度0」なのか「そもそも移動要求が無い」のかを切り分けるため、
	// PathFollowing の状態と現在のゴール、StateTree稼働有無まで同じ1行に載せる。
	const UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
	const FVector MoveGoal = PathFollowing ? PathFollowing->GetCurrentTargetLocation() : FVector::ZeroVector;
	const UNavigationSystemV1* NavSys = GetWorld()
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	FNavLocation SelfProjection;
	const bool bOnNavMesh = NavSys && NavSys->ProjectPointToNavigation(Location, SelfProjection);
	UE_LOG(LogTemp, Display,
		TEXT("[P1:AI] telemetry bot=%s loc=%s displaced=%.0fcm mode=%d speed=%.0f maxWalk=%.0f floorDist=%.0f penetrating=%d nearestHostile=%.0fcm target=%s blocked=%d moveStatus=%d goal=%s onNav=%d stateTree=%d recoveryCount=%d recoveryActive=%d"),
		*Bot->GetName(), *Location.ToCompactString(), FVector::Dist2D(PossessLocation, Location),
		static_cast<int32>(Movement->MovementMode), Movement->Velocity.Size2D(), Movement->MaxWalkSpeed,
		FloorDistance, bPenetrating ? 1 : 0,
		NearestHostileDistance, *GetNameSafe(CurrentTarget.Get()), IsActionBlocked() ? 1 : 0,
		static_cast<int32>(GetMoveStatus()), *MoveGoal.ToCompactString(), bOnNavMesh ? 1 : 0,
		StateTreeComponent && StateTreeComponent->IsRunning() ? 1 : 0, MovementRecoveryCount,
		bMovementRecoveryActive ? 1 : 0);
	UE_LOG(LogTemp, Display,
		TEXT("[P1:AI] telemetry2 bot=%s lastMoveResult=%d lastDest=%s projectedDest=%d"),
		*Bot->GetName(), LastMoveRequestResult, *LastMoveRequestDestination.ToCompactString(),
		NavSys && NavSys->ProjectPointToNavigation(LastMoveRequestDestination, SelfProjection) ? 1 : 0);
}

void AMSBotAIController::RunNavCoverageSweepOnce()
{
	static bool bSweepDone = false;
	if (bSweepDone)
	{
		return;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("MSNavSweep")))
	{
		bSweepDone = true;
		return;
	}
	UWorld* World = GetWorld();
	AMSBotCharacter* Bot = GetBot();
	if (!World || !Bot || World->GetTimeSeconds() < 45.0f)
	{
		return;
	}
	bSweepDone = true;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	const FVector Origin = Bot->GetActorLocation();
	UE_LOG(LogTemp, Display, TEXT("[T228:NAVSWEEP] begin bot=%s origin=%s nav=%d navdata=%s bounds=%s"),
		*Bot->GetName(), *Origin.ToCompactString(), Nav ? 1 : 0, *GetNameSafe(NavData),
		NavData ? *NavData->GetBounds().ToString() : TEXT("none"));
	if (!Nav || !NavData)
	{
		return;
	}
	const float Sign = Origin.X > 0.0f ? -1.0f : 1.0f;
	for (float Offset = 0.0f; Offset <= 600000.0f; Offset += 2000.0f)
	{
		const FVector Probe = Origin + FVector(Sign * Offset, 0.0f, 0.0f);
		FNavLocation Narrow;
		const bool bNarrow = Nav->ProjectPointToNavigation(Probe, Narrow);
		FNavLocation Wide;
		const bool bWide = Nav->ProjectPointToNavigation(Probe, Wide, FVector(1000.0f, 1000.0f, 30000.0f));
		FHitResult GroundHit;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(MSBotNavSweepGround), /*bTraceComplex=*/false);
		GroundParams.AddIgnoredActor(Bot);
		const bool bGround = World->LineTraceSingleByChannel(GroundHit,
			Probe + FVector(0.0f, 0.0f, 200000.0f), Probe - FVector(0.0f, 0.0f, 50000.0f),
			ECC_WorldStatic, GroundParams);
		UE_LOG(LogTemp, Display,
			TEXT("[T228:NAVSWEEP] x=%.0f narrow=%d narrowZ=%.0f wide=%d wideZ=%.0f wideDX=%.0f groundZ=%.0f groundActor=%s"),
			Probe.X, bNarrow ? 1 : 0, bNarrow ? Narrow.Location.Z : -99999.0f,
			bWide ? 1 : 0, bWide ? Wide.Location.Z : -99999.0f,
			bWide ? Wide.Location.X - Probe.X : -99999.0f,
			bGround ? GroundHit.ImpactPoint.Z : -99999.0f,
			bGround ? *GetNameSafe(GroundHit.GetActor()) : TEXT("none"));
	}
	const FVector EnemySide(Sign * 279793.0f, Origin.Y, Origin.Z);
	FPathFindingQuery Query(Bot, *NavData, Origin, EnemySide);
	Query.SetAllowPartialPaths(true);
	const FPathFindingResult Result = Nav->FindPathSync(Query);
	const FNavPathSharedPtr Path = Result.Path;
	UE_LOG(LogTemp, Display,
		TEXT("[T228:NAVSWEEP] pathTo=%s success=%d partial=%d points=%d end=%s length=%.0f"),
		*EnemySide.ToCompactString(), Result.IsSuccessful() ? 1 : 0,
		Path.IsValid() && Path->IsPartial() ? 1 : 0,
		Path.IsValid() ? Path->GetPathPoints().Num() : -1,
		Path.IsValid() && Path->GetPathPoints().Num() > 0
			? *Path->GetPathPoints().Last().Location.ToCompactString() : TEXT("none"),
		Path.IsValid() ? Path->GetLength() : -1.0f);
}

void AMSBotAIController::NotifyChaseStateEntered()
{
	if (bLoggedChaseEntered) return;
	bLoggedChaseEntered = true;
	UE_LOG(LogTemp, Display, TEXT("[P1:AI] stage=Chase bot=%s target=%s distance=%.0fcm engage=%.0fcm"),
		*GetNameSafe(GetBot()), *GetNameSafe(CurrentTarget.Get()), GetTargetDistance(), GetEngageDistance());
}

void AMSBotAIController::NotifyAttackStateEntered()
{
	if (bLoggedAttackEntered) return;
	bLoggedAttackEntered = true;
	UE_LOG(LogTemp, Display, TEXT("[P1:AI] stage=Attack bot=%s target=%s distance=%.0fcm weaponRange=%.0fcm"),
		*GetNameSafe(GetBot()), *GetNameSafe(CurrentTarget.Get()), GetTargetDistance(), GetCurrentWeaponRangeCm());
}

FGenericTeamId AMSBotAIController::GetGenericTeamId() const
{
	const IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(GetPawn());
	return Agent ? Agent->GetGenericTeamId() : FGenericTeamId::NoTeam;
}

void AMSBotAIController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(GetPawn())) Agent->SetGenericTeamId(NewTeamID);
}

AMSBotCharacter* AMSBotAIController::GetBot() const { return Cast<AMSBotCharacter>(GetPawn()); }
float AMSBotAIController::GetTargetDistance() const { return GetBot() && CurrentTarget.IsValid() ? FVector::Dist(GetBot()->GetActorLocation(), CurrentTarget->GetActorLocation()) : TNumericLimits<float>::Max(); }

float AMSBotAIController::GetCurrentWeaponRangeCm() const
{
	const UMSWeaponComponent* Weapons = GetBot() ? GetBot()->GetWeaponComponent() : nullptr;
	const UMSWeaponDataBase* Weapon = Weapons ? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	if (const UMSMeleeWeaponData* MeleeWeapon = Cast<UMSMeleeWeaponData>(Weapon))
	{
		return UGA_Melee::GetMeleeReachCmForWeapon(MeleeWeapon, /*bDashMelee=*/false);
	}
	return Weapon ? Weapon->GetRangeWorldCm() : 0.0f;
}

float AMSBotAIController::CalculateEngageDistance(const EMSEnemyTier Tier, const float WeaponRangeCm, const FMSBotAIConfig& C)
{
	switch (Tier) { case EMSEnemyTier::Medium: return C.MeleeEngageDistance; case EMSEnemyTier::Strong: return FMath::Min(WeaponRangeCm, C.BazookaPreferredDistance + C.StrongEngageDistanceBonus); default: return WeaponRangeCm; }
}
float AMSBotAIController::CalculateBreakDistance(const EMSEnemyTier Tier, const float WeaponRangeCm, const FMSBotAIConfig& C) { return Tier == EMSEnemyTier::Medium ? C.MeleeEngageDistance * 1.5f : WeaponRangeCm; }
float AMSBotAIController::GetEngageDistance() const { const AMSBotCharacter* B=GetBot(); return B ? CalculateEngageDistance(B->GetEnemyTier(), GetCurrentWeaponRangeCm(), B->GetAIConfig()) : 0.0f; }
float AMSBotAIController::GetBreakDistance() const { const AMSBotCharacter* B=GetBot(); return B ? CalculateBreakDistance(B->GetEnemyTier(), GetCurrentWeaponRangeCm(), B->GetAIConfig()) : 0.0f; }

float AMSBotAIController::CalculateBoostBurstTriggerDistance(const EMSEnemyTier Tier, const FMSBotAIConfig& C)
{
	switch (Tier)
	{
	case EMSEnemyTier::Medium: return C.MeleeEngageDistance * C.MediumBoostTriggerFactor;
	default: return C.BoostBurstTriggerDistance; // Weak/Strong: 通常条件
	}
}

float AMSBotAIController::CalculateBoostBurstCooldownMultiplier(const EMSEnemyTier Tier, const FMSBotAIConfig& C)
{
	return Tier == EMSEnemyTier::Medium ? C.MediumBoostCooldownMultiplier : 1.0f;
}

bool AMSBotAIController::ShouldPreferBazooka(const EMSEnemyTier Tier, const float DistanceCm, const FMSBotAIConfig& C)
{
	return Tier == EMSEnemyTier::Strong && DistanceCm >= C.BazookaPreferredDistance;
}

int32 AMSBotAIController::ChooseStrongTierWeaponSlot(const float DistanceCm,
	const float BazookaPreferredDistanceCm, const TArray<FGameplayTag>& Categories, const int32 CurrentSlot)
{
	int32 BazookaSlot = INDEX_NONE;
	int32 GunSlot = INDEX_NONE;
	for (int32 Index = 0; Index < Categories.Num(); ++Index)
	{
		if (BazookaSlot == INDEX_NONE && Categories[Index] == MSTags::Weapon_Category_Bazooka)
		{
			BazookaSlot = Index;
		}
		else if (GunSlot == INDEX_NONE &&
			(Categories[Index] == MSTags::Weapon_Category_MachineGun ||
			 Categories[Index] == MSTags::Weapon_Category_BeamRifle))
		{
			GunSlot = Index;
		}
	}
	if (DistanceCm >= BazookaPreferredDistanceCm && BazookaSlot != INDEX_NONE)
	{
		return BazookaSlot;
	}
	return GunSlot != INDEX_NONE ? GunSlot : CurrentSlot;
}

bool AMSBotAIController::SelectStrongTierWeaponForDistance()
{
	AMSBotCharacter* Bot = GetBot();
	UMSWeaponComponent* Weapons = Bot ? Bot->GetWeaponComponent() : nullptr;
	if (!Bot || Bot->GetEnemyTier() != EMSEnemyTier::Strong || !Weapons)
	{
		return false;
	}
	TArray<FGameplayTag> Categories;
	for (int32 Index = 0; Index < Weapons->GetSlotCount(); ++Index)
	{
		const UMSWeaponDataBase* Weapon = Weapons->GetWeaponData(Index);
		Categories.Add(Weapon ? Weapon->CategoryTag : FGameplayTag());
	}
	const int32 CurrentSlot = Weapons->GetActiveSlotIndex();
	const int32 DesiredSlot = ChooseStrongTierWeaponSlot(
		GetTargetDistance(), Bot->GetAIConfig().BazookaPreferredDistance, Categories, CurrentSlot);
	return DesiredSlot != CurrentSlot && Weapons->SwitchToSlot(DesiredSlot);
}

bool AMSBotAIController::SelectUsableWeaponSlot()
{
	// #69: 弾切れ武器を構えたまま膠着するBOTの回復手段。IsEmptyは有限弾でマガジン+予備が
	// 共に0のときのみtrue(リロード中や無限弾はfalse)なので、通常の射撃サイクルには介入しない。
	AMSBotCharacter* Bot = GetBot();
	UMSWeaponComponent* Weapons = Bot ? Bot->GetWeaponComponent() : nullptr;
	if (!Weapons || !Weapons->IsEmpty(Weapons->GetActiveSlotIndex()))
	{
		return false;
	}
	for (int32 Index = 0; Index < Weapons->GetSlotCount(); ++Index)
	{
		if (Index != Weapons->GetActiveSlotIndex() && Weapons->GetWeaponData(Index) &&
			!Weapons->IsEmpty(Index) && Weapons->SwitchToSlot(Index))
		{
			return true;
		}
	}
	return false;
}

bool AMSBotAIController::TestDistanceRule(const EMSBotDistanceRule Rule, const float D, const float Engage, const float Break, const FMSBotAIConfig& C)
{
	switch (Rule) { case EMSBotDistanceRule::PatrolToChase: return D <= C.DetectionRange; case EMSBotDistanceRule::ChaseToPatrol: return D > C.LoseSightRange; case EMSBotDistanceRule::ChaseToAttack: return D <= Engage; case EMSBotDistanceRule::AttackToChase: return D > Break; default: return false; }
}

bool AMSBotAIController::IsActionBlocked() const
{
	const UAbilitySystemComponent* ASC = GetBot() ? GetBot()->GetAbilitySystemComponent() : nullptr;
	return !ASC || UMSHitReactionComponent::BlocksCombat(ASC);
}

bool AMSBotAIController::CanActivateAttackAbility() const
{
	const AMSBotCharacter* Bot = GetBot();
	return !IsActionBlocked() && CurrentTarget.IsValid() && UMSTeamStatics::CanDamage(Bot, CurrentTarget.Get());
}

FVector AMSBotAIController::CalculateSeparationOffset(const FVector& Origin,
	const TArray<FVector>& FriendlyLocations, const float RadiusCm, const float MaxOffsetCm,
	const FVector& CoincidentFallback)
{
	if (RadiusCm <= 0.0f || MaxOffsetCm <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	FVector Steering = FVector::ZeroVector;
	for (const FVector& FriendlyLocation : FriendlyLocations)
	{
		FVector Away = Origin - FriendlyLocation;
		Away.Z = 0.0f;
		const float Distance = Away.Size2D();
		if (Distance > RadiusCm)
		{
			continue;
		}
		const FVector Direction = Distance > KINDA_SMALL_NUMBER
			? Away / Distance
			: CoincidentFallback.GetSafeNormal2D(SMALL_NUMBER, FVector::RightVector);
		Steering += Direction * (1.0f - Distance / RadiusCm);
	}
	return Steering.GetClampedToMaxSize2D(1.0f) * MaxOffsetCm;
}

FVector AMSBotAIController::GetAllySeparationOffset() const
{
	const AMSBotCharacter* Bot = GetBot();
	const UWorld* World = GetWorld();
	const UMSUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UMSUnitRegistrySubsystem>() : nullptr;
	if (!Bot || !Registry)
	{
		return FVector::ZeroVector;
	}

	TArray<FVector> FriendlyLocations;
	const bool bTaigan = IsTaiganNavigationMap(World->GetMapName());
	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 0.0f;
	const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		const AMSBotCharacter* FriendlyBot = Cast<AMSBotCharacter>(Unit);
		const UAbilitySystemComponent* FriendlyASC = FriendlyBot ? FriendlyBot->GetAbilitySystemComponent() : nullptr;
		if (FriendlyBot && FriendlyBot != Bot && FriendlyBot->GetGenericTeamId() == Bot->GetGenericTeamId() &&
			(!FriendlyASC || !FriendlyASC->HasMatchingGameplayTag(MSTags::State_Dead)) &&
			(!bTaigan || FMath::Abs(FriendlyBot->GetActorLocation().Z - Bot->GetActorLocation().Z)
				<= CapsuleHalfHeight * 2.0f))
		{
			FriendlyLocations.Add(FriendlyBot->GetActorLocation());
		}
	});

	// Exact overlaps have no geometric away vector. Give each BOT a stable fallback direction so
	// stacked spawns fan out instead of remaining a zero-force pile.
	const float Angle = FMath::DegreesToRadians(static_cast<float>(GetUniqueID() % 360));
	const FVector Fallback(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
	const FMSBotAIConfig& Config = Bot->GetAIConfig();
	const float SeparationRadius = bTaigan
		? FMath::Max(Config.SeparationRadius, CapsuleRadius * 4.0f) : Config.SeparationRadius;
	const float MaxOffset = bTaigan
		? FMath::Max(Config.SeparationMaxOffset, CapsuleRadius * 2.5f) : Config.SeparationMaxOffset;
	const FVector Offset = CalculateSeparationOffset(Bot->GetActorLocation(), FriendlyLocations,
		SeparationRadius, MaxOffset, Fallback);
	if (bTaigan && Offset.IsNearlyZero())
	{
		// Two equally close allies can cancel their avoidance vectors exactly.
		// Give this bot a stable way to leave the pile instead of standing in place.
		for (const FVector& Friendly : FriendlyLocations)
		{
			if (FVector::Dist2D(Bot->GetActorLocation(), Friendly) < SeparationRadius)
				return Fallback * MaxOffset;
		}
	}
	return Offset;
}

bool AMSBotAIController::TryActivateCurrentWeaponAbility()
{
	if (!CanActivateAttackAbility()) return false;
	UAbilitySystemComponent* ASC = GetBot()->GetAbilitySystemComponent();
	const UMSWeaponComponent* Weapons = GetBot()->GetWeaponComponent();
	const UMSWeaponDataBase* Weapon = Weapons ? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	const EMSWeaponAbilityRoute Route = MSWeaponAbilityRouting::Resolve(Weapon);
	if (Route == EMSWeaponAbilityRoute::Homing)
	{
		const UMSLockOnComponent* Lock = GetBot()->GetLockOnComponent();
		bool Manual=false, Tracking=false, Captured=false;
		if (!Lock || Lock->GetHomingCapturedTarget()!=CurrentTarget.Get()
			|| !Lock->GetIncomingThreatState(CurrentTarget.Get(),Manual,Tracking,Captured) || !Captured) return false;
	}
	if (Route == EMSWeaponAbilityRoute::Melee)
	{
		const bool bActivated = TryActivateMeleeAbility();
		if (bActivated && !bLoggedWeaponFire)
		{
			bLoggedWeaponFire = true;
			UE_LOG(LogTemp, Display, TEXT("MSBotAI: fired weapon bot=%s route=Melee target=%s"), *GetNameSafe(GetBot()), *GetNameSafe(CurrentTarget.Get()));
		}
		return bActivated;
	}
	TSubclassOf<UGameplayAbility> AbilityClass;
	switch (Route)
	{
	case EMSWeaponAbilityRoute::Hitscan: AbilityClass = UGA_Fire_Hitscan::StaticClass(); break;
	case EMSWeaponAbilityRoute::Shotgun: AbilityClass = UGA_Fire_Shotgun::StaticClass(); break;
	case EMSWeaponAbilityRoute::Burst: AbilityClass = UGA_Fire_Burst::StaticClass(); break;
	case EMSWeaponAbilityRoute::Homing: AbilityClass = UGA_Fire_Homing::StaticClass(); break;
	case EMSWeaponAbilityRoute::Charge: AbilityClass = UGA_Fire_Charge::StaticClass(); break;
	case EMSWeaponAbilityRoute::ContinuousBeam: AbilityClass = UGA_Fire_ContinuousBeam::StaticClass(); break;
	default: return false;
	}
	const bool bActivated = ASC->TryActivateAbilityByClass(AbilityClass);
	if (bActivated && !bLoggedWeaponFire)
	{
		bLoggedWeaponFire = true;
		UE_LOG(LogTemp, Display, TEXT("MSBotAI: fired weapon bot=%s route=%d target=%s"),
			*GetNameSafe(GetBot()), static_cast<int32>(Route), *GetNameSafe(CurrentTarget.Get()));
	}
	return bActivated;
}

bool AMSBotAIController::TryActivateMeleeAbility()
{
	if (!CanActivateAttackAbility()) return false;
	const UMSWeaponComponent* Weapons = GetBot()->GetWeaponComponent();
	const UMSWeaponDataBase* Weapon = Weapons ? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	const float MeleeReachCm = GetCurrentWeaponRangeCm();
	const float ActivationDistanceCm = FMath::Min(GetBot()->GetAIConfig().MediumMeleeStopDistance, MeleeReachCm);
	if (MSWeaponAbilityRouting::Resolve(Weapon) != EMSWeaponAbilityRoute::Melee ||
		GetTargetDistance() > ActivationDistanceCm) return false;
	UAbilitySystemComponent* ASC = GetBot()->GetAbilitySystemComponent();
	if (!ASC) return false;
	if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UGA_Melee::StaticClass()))
	{
		if (UGA_Melee* Melee = Cast<UGA_Melee>(Spec->GetPrimaryInstance()); Melee && Melee->IsActive())
		{
			// A live GAS activation owns the stage/recovery timers. GAS will reject a
			// second TryActivateAbilityByClass while it is active, so forward combo
			// input to that same instance (legacy recovery still rejects the swing).
			AMSBotCharacter* Bot = GetBot();
			const UMSCharacterMovementComponent* Movement =
				Cast<UMSCharacterMovementComponent>(Bot->GetCharacterMovement());
			return Melee->PerformMelee(Bot, Bot->GetActorLocation(),
				Bot->GetActorForwardVector(), Movement && Movement->IsBoostDashing(), 1.0f);
		}
	}
	return ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass());
}

bool AMSBotAIController::CanUseKick() const
{
	const AMSBotCharacter* Bot = GetBot();
	return Bot && Bot->GetEnemyTier() == EMSEnemyTier::Strong && CanActivateAttackAbility()
		&& KickCooldownRemaining <= 0.0f && GetTargetDistance() <= Bot->GetAIConfig().KickRange;
}

bool AMSBotAIController::TryActivateKick()
{
	if (!CanUseKick()) return false;
	if (!GetBot()->GetAbilitySystemComponent()->TryActivateAbilityByClass(UGA_BotKick::StaticClass())) return false;
	KickCooldownRemaining = UGA_BotKick::KickCooldownSeconds;
	return true;
}
