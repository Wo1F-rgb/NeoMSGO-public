// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "MSEmergencyRecallComponent.generated.h"

class AMSUnitBase;
class AMSTransientVfxActor;
class UAbilitySystemComponent;

DECLARE_MULTICAST_DELEGATE(FMSOnRecallCompleted);
DECLARE_MULTICAST_DELEGATE(FMSOnRecallStarted);

/** Authority-validated reasons; shared by keyboard and menu entry points. */
UENUM(BlueprintType)
enum class EMSRecallReason : uint8
{
	None, Unavailable, NotDeployed, Airborne, Incapacitated, Dead, Cooldown,
	AlreadyActive, AwaitingDeployment, ManualCancel, Movement, Action, Damage
};

UENUM(BlueprintType)
enum class EMSRecallNotice : uint8 { None, Rejected, Cancelled, Completed };

/** Player-only emergency recall countdown and resortie transition. */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSEmergencyRecallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSEmergencyRecallComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "MS|Emergency Recall")
	bool TryStartRecall();

	UFUNCTION(BlueprintCallable, Category = "MS|Emergency Recall")
	void CancelRecall(EMSRecallReason Reason = EMSRecallReason::ManualCancel);

	/** Does not mutate state or claim an online request has succeeded. */
	EMSRecallReason GetStartBlockReason() const;
	bool IsRequestPending() const { return bRequestPending; }
	float GetRecallDuration() const { return RecallDurationSeconds; }
	float GetRecallProgress() const;
	EMSRecallNotice GetNotice() const;
	EMSRecallReason GetNoticeReason() const { return NoticeReason; }
	static FText GetReasonText(EMSRecallReason Reason);

	UFUNCTION(BlueprintPure, Category = "MS|Emergency Recall")
	bool IsRecallActive() const { return bRecallActive && RecallGeneration > LastTerminatedRecallGeneration; }

	UFUNCTION(BlueprintPure, Category = "MS|Emergency Recall")
	bool IsAwaitingDeploy() const;

	UFUNCTION(BlueprintPure, Category = "MS|Emergency Recall")
	float GetRecallTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "MS|Emergency Recall")
	float GetRecallCooldownRemaining() const;

	FMSOnRecallCompleted OnRecallCompleted;
	/** Once per authoritative start, including owning-client replicated transitions. */
	FMSOnRecallStarted OnRecallStarted;

#if WITH_DEV_AUTOMATION_TESTS
	/** Feed the production RepNotify path, including duplicate property notifications. */
	void ApplyReplicatedStateForTesting(bool bActive, bool bAwaiting, int32 Generation);
	void ApplyAuthorityNoticeForTesting(EMSRecallNotice Notice, EMSRecallReason Reason);
	void SetRequestPendingForTesting() { bRequestPending = true; }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool StartRecallInternal();
	void ApplyRecallPresentation();
	void RemoveRecallPresentation();
	void OpenResortieAfterRecall();
	double GetSynchronizedTime() const;
	void NotifyRecallStartedOnce();
	void PublishNotice(EMSRecallNotice Notice, EMSRecallReason Reason);
	void ApplyNotice(EMSRecallNotice Notice, EMSRecallReason Reason);

	UFUNCTION(Server, Reliable) void ServerTryStartRecall();
	UFUNCTION(Server, Reliable) void ServerCancelRecall(EMSRecallReason Reason);
	UFUNCTION(Client, Reliable) void ClientRecallNotice(EMSRecallNotice Notice, EMSRecallReason Reason, double CooldownUntil, int32 Generation);
	UFUNCTION() void OnRep_RecallState();

	void FinishRecall();
	void CleanupRecall(bool bStartCooldown);

	UFUNCTION()
	void HandleOwnerDamaged(AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation);
 UFUNCTION() void HandleOwnerStaggered(AActor* Unit, bool bKnockedDown);

	UFUNCTION()
	void HandleOwnerDeployed(AMSUnitBase* Unit);

	void HandleDeadTagChanged(const FGameplayTag Tag, int32 NewCount);

	UPROPERTY(Transient)
	TObjectPtr<AMSTransientVfxActor> RecallRing;

	FTimerHandle RecallTimerHandle;
	FDelegateHandle DeadTagEventHandle;
	UPROPERTY(ReplicatedUsing=OnRep_RecallState)
	bool bRecallActive = false;
	bool bRecallTagsApplied = false;
	UPROPERTY(ReplicatedUsing=OnRep_RecallState)
	bool bAwaitingDeploy = false;
	/** Server world time at which the authoritative countdown ends. */
	UPROPERTY(ReplicatedUsing=OnRep_RecallState)
	double RecallFinishAt = 0.0;
	UPROPERTY(ReplicatedUsing=OnRep_RecallState)
	float RecallDurationSeconds = 0.0f;
	UPROPERTY(ReplicatedUsing=OnRep_RecallState)
	int32 RecallGeneration = 0;
	UPROPERTY(Replicated)
	double CooldownEndTime = 0.0;
	bool bAwaitingDeployPresentationApplied = false;
	bool bRequestPending = false;
	int32 LastNotifiedRecallGeneration = 0;
	int32 LastTerminatedRecallGeneration = 0;
	EMSRecallNotice NoticeKind = EMSRecallNotice::None;
	EMSRecallReason NoticeReason = EMSRecallReason::None;
	double NoticeExpiresAt = 0.0;

	static constexpr float RecallCooldownSeconds = 5.0f;
};
