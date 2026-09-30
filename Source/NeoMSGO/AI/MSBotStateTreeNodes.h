#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/MSBotAITypes.h"
#include "MSBotStateTreeNodes.generated.h"

class AMSUnitBase;

USTRUCT()
struct FMSBotEmptyTaskInstanceData
{
	GENERATED_BODY()
};

USTRUCT()
struct FMSBotPatrolTaskInstanceData
{
	GENERATED_BODY()
	FVector Destination = FVector::ZeroVector;
	bool bHasDestination = false;
	int32 DestinationAttempts = 0;
	float RetryDelayRemaining = 0.0f;
};

USTRUCT(meta=(DisplayName="MS Bot Patrol"))
struct NEOMSGO_API FMSBotPatrolTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FMSBotPatrolTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	static FVector MakeRandomPointInCircle(const FVector& Center, float Radius, FRandomStream& Random);
	/**
	 * Ground NavMesh maps use the hostile itself as the strategic path goal, so A* can route
	 * around city blocks. Direct-steering and zero-gravity maps retain short local waypoints.
	 */
	static FVector CalculatePatrolAdvanceCandidate(const FVector& CurrentLocation,
		const FVector& HostileLocation, float PatrolRadius, float LateralOffset,
		bool bUseStrategicNavGoal, bool bZeroGravity);
	static bool ProjectPatrolPoint(const UObject* WorldContext, const FVector& Point, FVector& OutPoint);
	/**
	 * #209: Daibao's 5 km Recast graph is still building (or absent) while the match starts, so
	 * ProjectPointToNavigation returns false for every patrol candidate and the bot never receives
	 * a destination at all — it stands at its deployment point forever and therefore never engages.
	 * Fall back to a downward world trace so patrol works without a navmesh; the navmesh result is
	 * still preferred whenever it is available (City / GrandBattlefield behaviour is unchanged).
	 */
	static bool ResolvePatrolPoint(const UObject* WorldContext, const FVector& Point, float TraceHeight, FVector& OutPoint);
	static float CalculateNavigationRetryDelay(int32 ConsecutiveFailures);
};

USTRUCT()
struct FMSBotAttackTaskInstanceData
{
	GENERATED_BODY()
	float ReverseRemaining = 0.0f;
	int32 StrafeDirection = 1;
	/** #69: 最後に攻撃(射撃/格闘/キック)が成立してからの経過秒。タイムアウトで位置取り直し */
	float NoFireDuration = 0.0f;
	/** #69: 位置取り直しモードの残り秒(>0の間はRepositionDestinationへ移動) */
	float RepositionRemaining = 0.0f;
};

USTRUCT(meta=(DisplayName="MS Bot Chase"))
struct NEOMSGO_API FMSBotChaseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FMSBotEmptyTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	static FVector CalculateChaseDestination(const FVector& BotLocation, const FVector& TargetLocation,
		float EngageDistanceCm, float WeaponRangeCm, uint32 Lane);
};

USTRUCT(meta=(DisplayName="MS Bot Attack"))
struct NEOMSGO_API FMSBotAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FMSBotAttackTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	static FVector CalculateStrafeDestination(const FVector& Bot, const FVector& Target, float DesiredDistance, int32 Direction);
	/** Maintain ranged standoff without moving beyond the weapon's preferred orbit. */
	static float CalculateRangedOrbitRadius(float CurrentDistance, float PreferredDistance,
		float CapsuleRadius);
	/** Desired planar velocity used by ranged attack steering; kept pure for deterministic tests. */
	static FVector CalculateAttackMoveVelocity(const FVector& Bot, const FVector& Destination, float MaxSpeedCmPerSecond);

	/**
	 * #69: 格闘武器時の停止距離。従来はWeak/StrongがEngageDistance(武器射程換算で240cm超)で
	 * 静止し、TryActivateMeleeAbilityの発動条件(MediumMeleeStopDistance=240cm以内)を永遠に
	 * 満たせず「見つめ合いフリーズ」になっていた。全階級で発動条件と同じ距離まで詰める。
	 */
	static float CalculateMeleeStopDistance(EMSEnemyTier Tier, const struct FMSBotAIConfig& Config, float EngageDistance);

	/**
	 * #69: 発砲タイムアウト時の位置取り直し先。通常ストレイフより内側(0.6倍)へ強い接線成分で
	 * 回り込み、互いに射線・条件を満たせないまま膠着する状況を崩す。
	 */
	static FVector CalculateRepositionDestination(const FVector& Bot, const FVector& Target, float CurrentDistance, int32 Direction);
};

USTRUCT(meta=(DisplayName="MS Bot Distance Rule"))
struct NEOMSGO_API FMSBotDistanceCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FMSBotEmptyTaskInstanceData;
	UPROPERTY(EditAnywhere, Category="Condition") EMSBotDistanceRule Rule = EMSBotDistanceRule::PatrolToChase;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT()
struct NEOMSGO_API FMSBotEvaluatorInstanceData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Output") TObjectPtr<AMSUnitBase> Target;
	UPROPERTY(VisibleAnywhere, Category="Output") float Distance = TNumericLimits<float>::Max();
};

USTRUCT(meta=(DisplayName="MS Bot Target"))
struct NEOMSGO_API FMSBotTargetEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FMSBotEvaluatorInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
