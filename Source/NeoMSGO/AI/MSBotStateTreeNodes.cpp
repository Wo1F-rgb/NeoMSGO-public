#include "AI/MSBotStateTreeNodes.h"

#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSWeaponData.h"
#include "Gameplay/MSCapturePoint.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"
#include "Weapons/MSWeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSCharacterMovementComponent.h"

namespace
{
	AMSBotAIController* MSBotController(FStateTreeExecutionContext& Context) { return Cast<AMSBotAIController>(Context.GetOwner()); }
}

FVector FMSBotPatrolTask::MakeRandomPointInCircle(const FVector& Center, const float Radius, FRandomStream& Random)
{
	const float Angle = Random.FRandRange(0.0f, 2.0f * UE_PI);
	const float Distance = FMath::Sqrt(Random.FRand()) * FMath::Max(0.0f, Radius);
	return Center + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
}

FVector FMSBotPatrolTask::CalculatePatrolAdvanceCandidate(const FVector& CurrentLocation,
	const FVector& HostileLocation, const float PatrolRadius, const float LateralOffset,
	const bool bUseStrategicNavGoal, const bool bZeroGravity)
{
	const FVector Forward = bZeroGravity
		? (HostileLocation - CurrentLocation).GetSafeNormal()
		: (HostileLocation - CurrentLocation).GetSafeNormal2D();
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Forward) * LateralOffset;
	const FVector Base = bUseStrategicNavGoal
		? HostileLocation
		: CurrentLocation + Forward * FMath::Max(0.0f, PatrolRadius);
	return Base + Lateral;
}

bool FMSBotPatrolTask::ProjectPatrolPoint(const UObject* WorldContext, const FVector& Point, FVector& OutPoint)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	FNavLocation Projected;
	if (!Nav || !Nav->ProjectPointToNavigation(Point, Projected)) return false;
	OutPoint = Projected.Location;
	return true;
}

bool FMSBotPatrolTask::ResolvePatrolPoint(const UObject* WorldContext, const FVector& Point,
	const float TraceHeight, FVector& OutPoint)
{
	if (ProjectPatrolPoint(WorldContext, Point, OutPoint))
	{
		return true;
	}
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	const float SafeTraceHeight = FMath::Max(100.0f, TraceHeight);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotPatrolGroundTrace), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Cast<AActor>(const_cast<UObject*>(WorldContext)));
	if (!World->LineTraceSingleByChannel(Hit, Point + FVector(0.0f, 0.0f, SafeTraceHeight),
		Point - FVector(0.0f, 0.0f, SafeTraceHeight), ECC_WorldStatic, QueryParams))
	{
		return false;
	}
	OutPoint = Hit.ImpactPoint;
	return true;
}

float FMSBotPatrolTask::CalculateNavigationRetryDelay(const int32 ConsecutiveFailures)
{
	// A missing/partially generated nav tile must not turn into one synchronous projection and
	// path request per bot per frame. Back off quickly, but recover within two seconds when a
	// streamed nav tile becomes available.
	const int32 Exponent = FMath::Clamp(ConsecutiveFailures - 1, 0, 4);
	return FMath::Min(0.125f * static_cast<float>(1 << Exponent), 2.0f);
}

EStateTreeRunStatus FMSBotPatrolTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	AMSBotAIController* Controller = MSBotController(Context);
	AMSBotCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	if (!Bot || !Controller->CanIssueMovementRequest()) return EStateTreeRunStatus::Running;
	auto& Data = Context.GetInstanceData<FInstanceDataType>(*this);
	++Data.DestinationAttempts;
	FRandomStream Random(FMath::Rand());
	FVector PatrolCenter = Bot->GetPatrolCenter();
	// PatrolCenter is captured while air-deployed. Preserve its XY region, but project from the
	// bot's current height so the saved 800 cm deployment altitude cannot keep projection failing.
	PatrolCenter.Z = Bot->GetActorLocation().Z;
	FVector Candidate;
	const UMSCharacterMovementComponent* Movement = Cast<UMSCharacterMovementComponent>(Bot->GetCharacterMovement());
	const bool bZeroGravity = Movement && Movement->IsZeroGravityEnabled();
	const UMSUnitRegistrySubsystem* Registry = Bot->GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>();
	const AMSUnitBase* NearestHostile = Controller->GetCurrentTarget();
	if (!NearestHostile && Registry)
		NearestHostile = Registry->FindNearestHostileUnit(Bot, TNumericLimits<float>::Max());
	AMSCapturePoint* Objective = Controller->ChooseStrategicObjective();
	if (Objective)
	{
		const float CapsuleRadius = Bot->GetCapsuleComponent()
			? Bot->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
		const uint32 ProbeSlot = Controller->GetStrategicObjectiveSlot()
			+ static_cast<uint32>(FMath::Max(0, Data.DestinationAttempts - 1)) * 13u;
		Candidate = AMSBotAIController::CalculateObjectiveApproachPoint(
			Objective->GetActorLocation(), Objective->GetCaptureRadius(), CapsuleRadius,
			ProbeSlot);
	}
	else if (NearestHostile)
	{
		// #228: A 10 m local goal in City lands inside the first building block. Nav projection
		// snaps it back to the facade edge and MoveTo reports AlreadyAtGoal once capsule overlap is
		// included, so every bot stops at X=+/-246380. Give A* the hostile's distant location on
		// normal ground NavMesh maps; the valid full path then selects the surrounding road network.
		// Daibao direct steering and zero gravity still require bounded local waypoints.
		const bool bUseStrategicNavGoal =
			!bZeroGravity && !Controller->IsUsingDirectGroundSteering();
		const float LateralOffset =
			Random.FRandRange(-1.0f, 1.0f) * FMath::Max(Bot->GetAIConfig().PatrolRadius,
				Bot->GetCapsuleComponent() ? Bot->GetCapsuleComponent()->GetScaledCapsuleRadius() * 4.0f : 0.0f);
		Candidate = CalculatePatrolAdvanceCandidate(
			Bot->GetActorLocation(), NearestHostile->GetActorLocation(),
			Bot->GetAIConfig().PatrolRadius, LateralOffset, bUseStrategicNavGoal, bZeroGravity);
	}
	else
	{
		Candidate = MakeRandomPointInCircle(PatrolCenter, Bot->GetAIConfig().PatrolRadius, Random);
		if (bZeroGravity)
		{
			Candidate.Z += Random.FRandRange(-0.35f, 0.35f) * Bot->GetAIConfig().PatrolRadius;
		}
	}
	// #209: 地表トレースのフォールバックは「NavMeshを経路探索に使わないマップ」限定にする。
	// NavMesh経路探索が有効なCity/GrandBattlefieldでフォールバックを使うと、NavMesh外の点でも
	// 目的地が「決まってしまい」、MoveToLocationの射影で現在地に潰れて AlreadyAtGoal になり、
	// 「候補を引き直して再挑戦する」従来のリトライが働かなくなってBOTが棒立ちで固まる。
	Data.bHasDestination = bZeroGravity ||
		(Controller->IsUsingDirectGroundSteering()
			? ResolvePatrolPoint(Bot, Candidate, Bot->GetAIConfig().PatrolRadius * 4.0f, Data.Destination)
			: ProjectPatrolPoint(Bot, Candidate, Data.Destination));
	if (bZeroGravity) Data.Destination = Candidate;
	if (Objective && Data.bHasDestination &&
		(FVector::Dist2D(Data.Destination, Objective->GetActorLocation()) > Objective->GetCaptureRadius() * 0.85f
			|| FMath::Abs(Data.Destination.Z - Objective->GetActorLocation().Z) > 2200.0f))
	{
		// A projected roof or distant Nav tile is not a valid capture destination.
		Data.bHasDestination = false;
	}
	if (Data.bHasDestination)
	{
		const float Acceptance = Objective ? FMath::Min(200.0f, Bot->GetAIConfig().PatrolAcceptanceRadius)
			: Bot->GetAIConfig().PatrolAcceptanceRadius;
		const bool bMoveAccepted = Controller->MoveToLocationOrDirect(Data.Destination, Acceptance, !Objective);
		// Bots deploy from the air. The first request is normally rejected while MOVE_Falling;
		// keeping bHasDestination set would prevent the StateTree from ever retrying after landing.
		if (!bMoveAccepted)
		{
			Data.bHasDestination = false;
		}
		else
		{
			if (Data.DestinationAttempts > 1)
			{
				UE_LOG(LogTemp, Display, TEXT("[P1:AI] Patrol navigation recovered bot=%s attempts=%d"),
					*Bot->GetName(), Data.DestinationAttempts);
			}
			Data.DestinationAttempts = 0;
			Data.RetryDelayRemaining = 0.0f;
		}
	}
	if (!Data.bHasDestination)
	{
		Data.RetryDelayRemaining = CalculateNavigationRetryDelay(Data.DestinationAttempts);
		// #228: 「==5」の一度きりでは、恒久的に射影が失敗し続けている状態がログに残らなかった。
		// 5回目以降は40回ごと(=十数秒に1回)に繰り返し出して停止の継続を観測できるようにする。
		if (Data.DestinationAttempts >= 5 && (Data.DestinationAttempts - 5) % 40 == 0)
		{
			const UWorld* World = Bot->GetWorld();
			const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
			UE_LOG(LogTemp, Warning,
				TEXT("[P1:AI] Patrol projection/path still unavailable after %d throttled attempts bot=%s location=%s candidate=%s nav=%s next_retry=%.3fs"),
				Data.DestinationAttempts, *Bot->GetName(), *Bot->GetActorLocation().ToCompactString(),
				*Candidate.ToCompactString(), Nav ? TEXT("yes") : TEXT("no"), Data.RetryDelayRemaining);
		}
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMSBotPatrolTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMSBotAIController* Controller = MSBotController(Context); AMSBotCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	if (!Bot || !Controller->CanIssueMovementRequest()) return EStateTreeRunStatus::Running;
	auto& Data = Context.GetInstanceData<FInstanceDataType>(*this);
	Data.RetryDelayRemaining = FMath::Max(0.0f, Data.RetryDelayRemaining - DeltaTime);
	if (AMSCapturePoint* Objective = Controller->GetStrategicObjective())
	{
		if (Objective->CanDeployFor(Bot->GetGenericTeamId()))
		{
			if (Data.RetryDelayRemaining <= 0.0f) return EnterState(Context, FStateTreeTransitionResult());
			return EStateTreeRunStatus::Running;
		}
		const float HoldDistance = FMath::Max(350.0f, Bot->GetCapsuleComponent()
			? Bot->GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.5f : 350.0f);
		if (Data.bHasDestination
			&& FVector::Dist2D(Bot->GetActorLocation(), Data.Destination) <= HoldDistance
			&& FMath::Abs(Bot->GetActorLocation().Z - Data.Destination.Z) <= 1500.0f
			&& FVector::Dist2D(Bot->GetActorLocation(), Objective->GetActorLocation())
				<= Objective->GetCaptureRadius() * 0.8f)
		{
			Controller->StopMovement();
			return EStateTreeRunStatus::Running; // Hold the distributed position while capturing.
		}
	}
	if (Controller->IsUsingDirect3DMovement())
	{
		if (!Data.bHasDestination ||
			FVector::Dist(Bot->GetActorLocation(), Data.Destination) <= Bot->GetAIConfig().PatrolAcceptanceRadius)
		{
			if (Data.RetryDelayRemaining <= 0.0f)
			{
				return EnterState(Context, FStateTreeTransitionResult());
			}
			return EStateTreeRunStatus::Running;
		}
		Controller->MoveToLocationOrDirect(Data.Destination, Bot->GetAIConfig().PatrolAcceptanceRadius);
		return EStateTreeRunStatus::Running;
	}
	if ((!Data.bHasDestination || Controller->GetMoveStatus() != EPathFollowingStatus::Moving ||
		FVector::Dist2D(Bot->GetActorLocation(), Data.Destination) <= Bot->GetAIConfig().PatrolAcceptanceRadius)
		&& Data.RetryDelayRemaining <= 0.0f)
		return EnterState(Context, FStateTreeTransitionResult());
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMSBotChaseTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	if (AMSBotAIController* Controller = MSBotController(Context)) Controller->NotifyChaseStateEntered();
	return EStateTreeRunStatus::Running;
}

FVector FMSBotChaseTask::CalculateChaseDestination(const FVector& BotLocation,
	const FVector& TargetLocation, const float EngageDistanceCm, const float WeaponRangeCm,
	const uint32 Lane)
{
	FVector Radial = (BotLocation - TargetLocation).GetSafeNormal2D();
	if (Radial.IsNearlyZero()) Radial = FVector::ForwardVector;
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial);
	const float Standoff = FMath::Max(0.0f, FMath::Min(EngageDistanceCm, WeaponRangeCm) * 0.65f);
	const float Side = (static_cast<float>(Lane % 7u) - 3.0f) / 6.0f;
	return TargetLocation + (Radial + Tangent * Side).GetSafeNormal2D() * Standoff;
}

EStateTreeRunStatus FMSBotChaseTask::Tick(FStateTreeExecutionContext& Context, float) const
{
	AMSBotAIController* Controller = MSBotController(Context);
	if (Controller && Controller->CanIssueMovementRequest() && Controller->GetCurrentTarget())
	{
		const FVector Destination = CalculateChaseDestination(
			Controller->GetBot()->GetActorLocation(), Controller->GetCurrentTarget()->GetActorLocation(),
			Controller->GetEngageDistance(), Controller->GetCurrentWeaponRangeCm(),
			Controller->GetBot()->GetUniqueID()) + Controller->GetAllySeparationOffset();
		Controller->MoveToLocationOrDirect(Destination, 150.0f, false);
	}
	return EStateTreeRunStatus::Running;
}

FVector FMSBotAttackTask::CalculateStrafeDestination(const FVector& Bot, const FVector& Target, const float DesiredDistance, const int32 Direction)
{
	FVector Radial = (Bot - Target).GetSafeNormal2D(); if (Radial.IsNearlyZero()) Radial = FVector::ForwardVector;
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial) * (Direction < 0 ? -1.0f : 1.0f);
	// 円周維持: 半径方向+接線方向の合成を正規化し、常に半径DesiredDistanceの円周上を目的地にする
	return Target + (Radial + Tangent * 0.35f).GetSafeNormal2D() * DesiredDistance;
}

float FMSBotAttackTask::CalculateRangedOrbitRadius(const float CurrentDistance,
	const float PreferredDistance, const float CapsuleRadius)
{
	const float Preferred = FMath::Max(0.0f, PreferredDistance);
	const float Clearance = FMath::Min(Preferred, FMath::Max(0.0f, CapsuleRadius) * 2.5f);
	return FMath::Clamp(FMath::Max(0.0f, CurrentDistance), Clearance, Preferred);
}

FVector FMSBotAttackTask::CalculateAttackMoveVelocity(const FVector& Bot, const FVector& Destination,
	const float MaxSpeedCmPerSecond)
{
	return (Destination - Bot).GetSafeNormal2D() * FMath::Max(0.0f, MaxSpeedCmPerSecond);
}

float FMSBotAttackTask::CalculateMeleeStopDistance(const EMSEnemyTier Tier, const FMSBotAIConfig& Config, const float EngageDistance)
{
	// #69: TryActivateMeleeAbilityはMediumMeleeStopDistance(240cm)以内でしか発動しない。
	// 停止距離がそれより遠い(Weak/Strong: EngageDistance=武器射程)と「攻撃も移動もしない」
	// 見つめ合いフリーズになるため、全階級で発動条件以内まで詰めることを保証する。
	return FMath::Min(Tier == EMSEnemyTier::Medium ? Config.MediumMeleeStopDistance : EngageDistance,
		Config.MediumMeleeStopDistance);
}

FVector FMSBotAttackTask::CalculateRepositionDestination(const FVector& Bot, const FVector& Target, const float CurrentDistance, const int32 Direction)
{
	FVector Radial = (Bot - Target).GetSafeNormal2D(); if (Radial.IsNearlyZero()) Radial = FVector::ForwardVector;
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial) * (Direction < 0 ? -1.0f : 1.0f);
	// 膠着打破: 距離を0.6倍へ詰めつつ接線成分を強めて(1.2)大きく回り込む
	const float RepositionDistance = FMath::Max(CurrentDistance * 0.6f, 150.0f);
	return Target + (Radial + Tangent * 1.2f).GetSafeNormal2D() * RepositionDistance;
}

EStateTreeRunStatus FMSBotAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	auto& Data = Context.GetInstanceData<FInstanceDataType>(*this);
	AMSBotAIController* C = MSBotController(Context); const AMSBotCharacter* B = C ? C->GetBot() : nullptr;
	if (C) C->NotifyAttackStateEntered();
	Data.StrafeDirection = FMath::RandBool() ? 1 : -1;
	Data.ReverseRemaining = B ? FMath::FRandRange(B->GetAIConfig().StrafeReverseMinInterval, B->GetAIConfig().StrafeReverseMaxInterval) : 1.6f;
	Data.NoFireDuration = 0.0f;
	Data.RepositionRemaining = 0.0f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMSBotAttackTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMSBotAIController* C = MSBotController(Context); AMSBotCharacter* B = C ? C->GetBot() : nullptr; AMSUnitBase* T = C ? C->GetCurrentTarget() : nullptr;
	if (!B || !T || !C->CanIssueMovementRequest()) return EStateTreeRunStatus::Running;
	auto& Data = Context.GetInstanceData<FInstanceDataType>(*this);
	const FMSBotAIConfig& Config = B->GetAIConfig();
	C->TryStartBoostBurstInAttack();
	C->SelectStrongTierWeaponForDistance();
	// #69: 現在の武器が完全に弾切れ(マガジン+予備とも0)なら撃てるスロットへ持ち替える
	C->SelectUsableWeaponSlot();
	const bool bKicked = C->TryActivateKick();
	B->GetCharacterMovement()->bOrientRotationToMovement = false;
	const FRotator Desired = (T->GetActorLocation() - B->GetActorLocation()).Rotation();
	B->SetActorRotation(FMath::RInterpTo(B->GetActorRotation(), Desired, DeltaTime, Config.AttackRotationInterpSpeed));
	const UMSWeaponComponent* Weapons = B->GetWeaponComponent();
	const bool bMelee = Weapons && Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()));

	// #69: 発砲タイムアウト経過中は位置取り直し(反転+内側へ回り込み)で膠着を崩す
	Data.RepositionRemaining = FMath::Max(0.0f, Data.RepositionRemaining - DeltaTime);
	if (Data.RepositionRemaining > 0.0f)
	{
		const FVector Destination = CalculateRepositionDestination(
			B->GetActorLocation(), T->GetActorLocation(), C->GetTargetDistance(), Data.StrafeDirection)
			+ C->GetAllySeparationOffset();
		C->MoveToLocationOrDirect(Destination, Config.PatrolAcceptanceRadius);
	}
	else if (bMelee)
	{
		// P6-2 AC4: 格闘間合いまで直進しMediumMeleeStopDistance(2.4m)以内で静止する。
		// #69: 従来Weak/StrongはEngageDistance(武器射程)で静止していたが、それでは
		// TryActivateMeleeAbilityの発動距離(240cm)に届かず膠着するため全階級で詰める。
		const float StopDistance = CalculateMeleeStopDistance(B->GetEnemyTier(), Config,
			FMath::Min(C->GetEngageDistance(), C->GetCurrentWeaponRangeCm()));
		if (C->GetTargetDistance() > StopDistance)
		{
			C->MoveToLocationOrDirect(T->GetActorLocation() + C->GetAllySeparationOffset(), StopDistance);
		}
		else
		{
			const FVector Separation = C->GetAllySeparationOffset();
			if (!Separation.IsNearlyZero())
			{
				// Taigan's old 220cm separation goal was inside the 317cm capsule-overlap
				// acceptance radius, so these requests completed without moving.
				const bool bTaigan = AMSBotAIController::IsTaiganNavigationMap(B->GetWorld()->GetMapName());
				C->MoveToLocationOrDirect(B->GetActorLocation() + Separation,
					bTaigan ? 100.0f : 10.0f, !bTaigan);
			}
			else C->StopMovement();
		}
	}
	else
	{
		Data.ReverseRemaining -= DeltaTime;
		if (Data.ReverseRemaining <= 0.0f) { Data.StrafeDirection *= -1; Data.ReverseRemaining = FMath::FRandRange(Config.StrafeReverseMinInterval, Config.StrafeReverseMaxInterval); }
		// Orbit at the current radius when possible. Only retreat when the two large
		// capsules are nearly touching; this avoids all ranged BOTs piling onto one point.
		const float PreferredDistance = C->GetEngageDistance() * Config.StrafeDistanceFactor;
		const float CapsuleRadius = B->GetCapsuleComponent()
			? B->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
		const float StrafeDistance = CalculateRangedOrbitRadius(
			C->GetTargetDistance(), PreferredDistance, CapsuleRadius);
		const FVector Destination = CalculateStrafeDestination(B->GetActorLocation(), T->GetActorLocation(), StrafeDistance, Data.StrafeDirection)
			+ C->GetAllySeparationOffset();
		const FVector DesiredVelocity = CalculateAttackMoveVelocity(
			B->GetActorLocation(), Destination, B->GetCharacterMovement()->MaxWalkSpeed);
		if (!DesiredVelocity.IsNearlyZero()) C->MoveToLocationOrDirect(Destination,
			FMath::Min(100.0f, Config.PatrolAcceptanceRadius), false);
	}
	if (!bKicked) C->TryActivateCurrentWeaponAbility();
	// 攻撃成立の判定: GA活性化は内部で発射失敗しても成功を返すため、実際に弾を消費した証跡
	// (発射クールダウン進行中)またはリロード進行中(=膠着ではない)を「前進」とみなす。
	const int32 ActiveSlot = Weapons ? Weapons->GetActiveSlotIndex() : INDEX_NONE;
	const bool bFired = bKicked || (Weapons &&
		(Weapons->GetFireCooldownRemaining() > 0.0f || Weapons->IsReloading(ActiveSlot)));
	if (bFired)
	{
		Data.NoFireDuration = 0.0f;
	}
	else
	{
		Data.NoFireDuration += DeltaTime;
		if (Data.RepositionRemaining <= 0.0f && Data.NoFireDuration >= Config.AttackRepositionTimeout)
		{
			// タイムアウト: ストレイフ方向を反転し、一定時間の位置取り直しモードへ入る
			Data.StrafeDirection *= -1;
			Data.NoFireDuration = 0.0f;
			Data.RepositionRemaining = Config.AttackRepositionDuration;
		}
	}
	return EStateTreeRunStatus::Running;
}

bool FMSBotDistanceCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMSBotAIController* C = MSBotController(Context); const AMSBotCharacter* B = C ? C->GetBot() : nullptr;
	return C && B && !C->IsActionBlocked() && AMSBotAIController::TestDistanceRule(Rule, C->GetTargetDistance(), C->GetEngageDistance(), C->GetBreakDistance(), B->GetAIConfig());
}

void FMSBotTargetEvaluator::Tick(FStateTreeExecutionContext& Context, float) const
{
	auto& Data = Context.GetInstanceData<FInstanceDataType>(*this); AMSBotAIController* C = MSBotController(Context);
	Data.Target = C ? C->GetCurrentTarget() : nullptr; Data.Distance = C ? C->GetTargetDistance() : TNumericLimits<float>::Max();
}
