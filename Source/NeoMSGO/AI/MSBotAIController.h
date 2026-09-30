#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "GameplayTagContainer.h"
#include "AI/MSBotAITypes.h"
#include "MSBotAIController.generated.h"

class AMSBotCharacter;
class AMSUnitBase;
class AMSCapturePoint;
class UStateTreeAIComponent;
class UStateTree;
class UWorld;

UCLASS()
class NEOMSGO_API AMSBotAIController : public AAIController
{
	GENERATED_BODY()
public:
	AMSBotAIController();
	virtual void Tick(float DeltaSeconds) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;

	AMSBotCharacter* GetBot() const;
	AMSUnitBase* GetCurrentTarget() const { return CurrentTarget.Get(); }
	UFUNCTION(BlueprintPure, Category="MS|AI|Diagnostics")
	AMSCapturePoint* GetStrategicObjective() const { return StrategicObjective.Get(); }
	UFUNCTION(BlueprintPure, Category="MS|AI|Diagnostics")
	FVector GetRequestedDestinationForTelemetry() const { return LastMoveRequestDestination; }
	UFUNCTION(BlueprintPure, Category="MS|AI|Diagnostics")
	uint8 GetBotTeamIdForTelemetry() const { return GetGenericTeamId().GetId(); }
	uint32 GetStrategicObjectiveSlot() const { return StrategicObjectiveSlot; }
	/** Assigns a capturable point while out of combat, accounting for allied assignments. */
	AMSCapturePoint* ChooseStrategicObjective();
	static float CalculateObjectiveScore(float DistanceCm, int32 OtherAssignments,
		bool bOwnedByTeam, bool bCurrentObjective);
	static FVector CalculateObjectiveApproachPoint(const FVector& ObjectiveLocation,
		float CaptureRadiusCm, float CapsuleRadiusCm, uint32 SlotIndex);
	float GetTargetDistance() const;
	float GetCurrentWeaponRangeCm() const;
	float GetEngageDistance() const;
	float GetBreakDistance() const;
	bool IsActionBlocked() const;
	bool CanIssueMovementRequest() const { return !IsActionBlocked(); }
	/** Uses NavMesh on ground maps and direct 3D steering when the pawn is in zero gravity. */
	bool MoveToLocationOrDirect(const FVector& Destination, float AcceptanceRadius = -1.0f,
		bool bStopOnOverlap = true);
	virtual void StopMovement() override;
	bool IsUsingDirect3DMovement() const;
	bool CanActivateAttackAbility() const;
	/** Weighted steering offset away from nearby living friendly BOTs. */
	FVector GetAllySeparationOffset() const;
	static FVector CalculateSeparationOffset(const FVector& Origin, const TArray<FVector>& FriendlyLocations,
		float RadiusCm, float MaxOffsetCm, const FVector& CoincidentFallback = FVector::RightVector);
	/** Lower score wins; nearby allies already pursuing one hostile encourage target spread. */
	static float CalculateTargetPreferenceScore(float DistanceCm, int32 OtherPursuers,
		bool bCurrentTarget, bool bCrossNavigationLayer, bool bRecentlyStalledTarget = false);
	/** Daibao's 5 km Recast graph makes synchronous full path searches stall the game thread. */
	static bool ShouldUseDirectGroundSteering(const FString& MapName);
	/** Taigan keeps full Nav paths and changes goals only after a meaningful movement. */
	static bool IsTaiganNavigationMap(const FString& MapName);
	static bool IsTaiganSubwayLocation(const FVector& Location);
	static bool ShouldReuseTaiganMove(const FVector& ActiveGoal, const FVector& RequestedGoal,
		float ActiveAcceptanceRadius, float RequestedAcceptanceRadius,
		bool bActiveStopOnOverlap, bool bRequestedStopOnOverlap, bool bPathMoving);
	static bool HasMovementProgress(float DisplacementCm, float SpeedCmPerSecond, bool bTaigan);
	static float CalculateTaiganRVOConsiderationRadius(float CapsuleRadiusCm,
		float MaxWalkSpeedCmPerSecond, float ConfiguredRadiusCm);
	static bool ShouldKeepRecoveryForGoal(const FVector& RecoveryWaypoint,
		const FVector& StrategicGoal, const FVector& RequestedGoal, bool bFollowingEntrance);
	/** Map-authored entrance markers; returns an ordered, layer-aware waypoint list. */
	static TArray<FVector> CollectTaiganEntranceWaypoints(UWorld* World,
		const FVector& BotLocation, const FVector& StrategicGoal);
	/** Pure movement-recovery gate used by runtime logic and deterministic automation tests. */
	static bool ShouldRecoverFromStall(float GoalDistanceCm, float AcceptanceRadiusCm,
		float StallDurationSeconds, float DisplacementCm, bool bHasActiveMovementRequest,
		bool bActionBlocked, bool bGrounded);
	/** Returns a bounded lateral escape point; collision and ground validation happen at runtime. */
	static FVector CalculateLocalEscapeCandidate(const FVector& CurrentLocation,
		const FVector& GoalLocation, float OffsetCm, int32 Direction);
	int32 GetMovementRecoveryCountForTesting() const { return MovementRecoveryCount; }
	bool IsMovementRecoveryActiveForTesting() const { return bMovementRecoveryActive; }
	FVector GetLastMovementRecoveryDestinationForTesting() const { return LastMovementRecoveryDestination; }
	int32 GetLastMoveRequestResultForTesting() const { return LastMoveRequestResult; }
	bool FindLocalEscapePointForTesting(const FVector& GoalLocation, int32 Direction,
		FVector& OutPoint) const { return FindLocalEscapePoint(GoalLocation, Direction, OutPoint); }
	/** True when this controller's map steers directly instead of through NavMesh path following. */
	bool IsUsingDirectGroundSteering() const;
	bool TryActivateCurrentWeaponAbility();
	/** Unity SelectStrongTierWeapon: bazooka at range, gun at close/mid range. */
	bool SelectStrongTierWeaponForDistance();
	/** #69: アクティブ武器が完全弾切れ(マガジン+予備とも0)なら発射可能なスロットへ持ち替える。 */
	bool SelectUsableWeaponSlot();
	static int32 ChooseStrongTierWeaponSlot(float DistanceCm, float BazookaPreferredDistanceCm,
		const TArray<FGameplayTag>& WeaponCategories, int32 CurrentSlot);
	bool TryActivateMeleeAbility();
	bool TryActivateKick();
	/**
	 * #209: One-shot per-controller engagement telemetry. "BOTが攻撃しない" can stall at target
	 * acquisition, Chase or Attack; logging each stage exactly once makes the stall point
	 * measurable from a single headless run without spamming the log every tick.
	 */
	void NotifyChaseStateEntered();
	void NotifyAttackStateEntered();
	bool CanUseKick() const;
	float GetKickCooldownRemaining() const { return FMath::Max(0.0f, KickCooldownRemaining); }
	bool IsAILogicSuspendedForDeath() const { return bAILogicSuspendedForDeath; }
	/** The compiled StateTree currently assigned to the brain component. */
	UStateTree* GetAssignedStateTree() const { return AssignedStateTree.Get(); }

	/** Called by the Attack StateTree task. Returns true only when a new burst starts. */
	bool TryStartBoostBurstInAttack();
	/** Distance-explicit entry point used by the Attack task and deterministic tests. */
	bool TryStartBoostBurst(float TargetDistanceCm);
	bool IsBoostBursting() const { return BoostBurstRemaining > 0.0f; }
	float GetBoostBurstRemaining() const { return FMath::Max(0.0f, BoostBurstRemaining); }
	float GetBoostBurstCooldownRemaining() const { return FMath::Max(0.0f, BoostBurstCooldownRemaining); }

	static float CalculateEngageDistance(EMSEnemyTier Tier, float WeaponRangeCm, const FMSBotAIConfig& Config);
	static float CalculateBreakDistance(EMSEnemyTier Tier, float WeaponRangeCm, const FMSBotAIConfig& Config);
	static bool TestDistanceRule(EMSBotDistanceRule Rule, float DistanceCm, float EngageCm, float BreakCm, const FMSBotAIConfig& Config);

	/** ブースト頻発条件の距離閾値(P6-2)。Weak/Strong=BoostBurstTriggerDistance(1200cm)、Medium=MeleeEngageDistance*係数(420cm) */
	static float CalculateBoostBurstTriggerDistance(EMSEnemyTier Tier, const FMSBotAIConfig& Config);
	/** ブースト頻発時のクールダウン倍率(P6-2)。Medium=0.4倍、Weak/Strong=通常条件(1.0倍) */
	static float CalculateBoostBurstCooldownMultiplier(EMSEnemyTier Tier, const FMSBotAIConfig& Config);
	/** Strong専用: 距離>=bazookaPreferredDistanceでバズーカ優先(P6-2。武器持ち替え自体はスコープ外) */
	static bool ShouldPreferBazooka(EMSEnemyTier Tier, float DistanceCm, const FMSBotAIConfig& Config);
protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	void RefreshTarget();
	void StartAssignedStateTreeWhenReady();
	void TickBoostBurst(float DeltaSeconds);
	void EndBoostBurst(bool bStartCooldown);
	void TickEngagementTelemetry(float DeltaSeconds);
	void TickMovementRecovery(float DeltaSeconds);
	bool TryStartMovementRecovery();
	bool FindLocalEscapePoint(const FVector& GoalLocation, int32 Direction, FVector& OutPoint) const;
	void EndMovementRecovery(bool bRepathOriginalGoal);
	void TickDirect3DMovement();
	/**
	 * #228: -MSNavSweep 起動時のみ動作する一回限りの実測診断。BOTの現在地から敵陣方向へ
	 * NavMesh射影と同期経路探索を刻んでログ出力し、「どのX座標でナビゲーションが途切れるか」を
	 * 推測ではなく座標で確定させる。
	 */
	void RunNavCoverageSweepOnce();
	void ClearDirect3DMovement();
	void BindDeathFlow(AMSBotCharacter* Bot);
	void UnbindDeathFlow(AMSBotCharacter* Bot);

	UFUNCTION()
	void HandleBotDeath(AActor* DeadUnit, AActor* Killer);

	UFUNCTION()
	void HandleBotDeployed(AMSUnitBase* Unit);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|AI") TObjectPtr<UStateTreeAIComponent> StateTreeComponent;
	/** Soft reference keeps the generated asset cookable without loading it for every controller CDO. */
	UPROPERTY(EditDefaultsOnly, Category="MS|AI") TSoftObjectPtr<UStateTree> BotStateTreeAsset;
	UPROPERTY(Transient) TObjectPtr<UStateTree> AssignedStateTree;
	UPROPERTY(Transient) TWeakObjectPtr<AMSUnitBase> CurrentTarget;
	TWeakObjectPtr<AMSCapturePoint> StrategicObjective;
	uint32 StrategicObjectiveSlot = 0;
	TWeakObjectPtr<AMSUnitBase> LastStalledTarget;
	TWeakObjectPtr<AMSUnitBase> AvoidedStalledTarget;
	int32 ConsecutiveTargetStalls = 0;
	float AvoidStalledTargetUntil = 0.0f;
	float TargetRefreshRemaining = 0.0f;
	float BoostBurstRemaining = 0.0f;
	float BoostBurstCooldownRemaining = 0.0f;
	float PreBoostMaxWalkSpeed = 0.0f;
	float PreBoostMaxAcceleration = 0.0f;
	float KickCooldownRemaining = 0.0f;
	bool bAILogicSuspendedForDeath = false;
	FVector PossessLocation = FVector::ZeroVector;
	bool bLoggedMovement = false;
	bool bLoggedWeaponFire = false;
	bool bLoggedDirectGroundSteering = false;
	bool bLoggedTargetAcquired = false;
	bool bLoggedChaseEntered = false;
	bool bLoggedAttackEntered = false;
	bool bLoggedMoveRequestRejected = false;
	/** #228: 直近の MoveToLocation 戻り値(EPathFollowingRequestResult)と要求先。停止原因の切り分け用。 */
	int32 LastMoveRequestResult = -1;
	FVector LastMoveRequestDestination = FVector::ZeroVector;
	float LastMoveAcceptanceRadius = 5.0f;
	bool bLastMoveStopOnOverlap = true;
	bool bFollowingTaiganEntrance = false;
	FVector TaiganStrategicGoal = FVector::ZeroVector;
	float LastFailedTaiganMoveTime = -1000.0f;
	bool bHasActiveMovementRequest = false;
	FVector LastProgressSampleLocation = FVector::ZeroVector;
	float MovementStallDuration = 0.0f;
	float MovementRecoveryCooldownRemaining = 0.0f;
	bool bMovementRecoveryActive = false;
	bool bIssuingMovementRecoveryRequest = false;
	bool bHasRecoveryOriginalGoal = false;
	FVector MovementRecoveryOriginalGoal = FVector::ZeroVector;
	FVector MovementRecoveryStrategicGoal = FVector::ZeroVector;
	FVector MovementRecoveryEscapeGoal = FVector::ZeroVector;
	FVector LastMovementRecoveryDestination = FVector::ZeroVector;
	float MovementRecoveryOriginalAcceptanceRadius = 5.0f;
	bool bMovementRecoveryOriginalStopOnOverlap = true;
	bool bRecoveryWasFollowingTaiganEntrance = false;
	float MovementRecoveryAcceptanceRadius = 100.0f;
	float MovementRecoveryRemaining = 0.0f;
	int32 MovementRecoveryDirection = 1;
	bool bPreferBackwardEscape = false;
	int32 MovementRecoveryCount = 0;
	static constexpr float EngagementTelemetryInterval = 15.0f;
	float EngagementTelemetryRemaining = EngagementTelemetryInterval;
	TOptional<FVector> Direct3DDestination;
	float Direct3DAcceptanceRadius = 5.0f;
};
