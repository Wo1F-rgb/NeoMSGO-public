// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/MSCapturePoint.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Voice/MSVoiceTypes.h"
#include "MSVoiceCutInSubsystem.generated.h"

class AMSCapturePoint;
class AMSPlayerCharacter;
class AMSRepairPod;
class AMSSupplyPack;
class AMSUnitBase;
class UMSEmergencyRecallComponent;

namespace MSVoiceCutInRules
{
	NEOMSGO_API int32 GetPriority(EMSVoiceTrigger Trigger);
	NEOMSGO_API double GetCooldownSeconds(EMSVoiceTrigger Trigger);
	NEOMSGO_API float GetBlipFrequency(EMSVoiceType Type);
	NEOMSGO_API bool ShouldReplace(int32 CurrentPriority, int32 IncomingPriority);
	NEOMSGO_API EMSVoiceType ResolveBotVoiceType(const FString& UnitName);
	NEOMSGO_API bool IsKillStreak(TArrayView<const double> PreviousKillTimes, double Now,
		int32 RequiredKills = 3, double WindowSeconds = 10.0);
}

DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnVoiceCutInRequested, const FMSVoiceCutInRequest&);
DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnVoiceCutInCleared, EMSVoiceCutInChannel);

/** Owns the two preset-voice cut-in channels and translates gameplay notifications into requests. */
UCLASS()
class NEOMSGO_API UMSVoiceCutInSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static constexpr double DisplayDurationSeconds = 3.0;
	static constexpr double FriendlyBotKillRangeCm = 6000.0;
	static constexpr double CaptureRangeCm = 3000.0;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	FMSOnVoiceCutInRequested OnCutInRequested;
	FMSOnVoiceCutInCleared OnCutInCleared;

	UFUNCTION(BlueprintCallable, Category="MS|Voice")
	bool GetActiveRequest(EMSVoiceCutInChannel Channel, FMSVoiceCutInRequest& OutRequest) const;

	/** Public processing entry points keep event translation independently testable. */
	void ProcessKillConfirmed(AActor* Killer, AMSPlayerCharacter* LocalPlayer, double Now);
	void ProcessUnitDeath(AActor* DeadUnit, AActor* Killer, AMSPlayerCharacter* LocalPlayer, double Now);
	void ProcessUnitDamaged(AActor* DamagedUnit, AActor* Attacker, AMSPlayerCharacter* LocalPlayer, double Now);
	bool SubmitRequest(const FMSVoiceCutInRequest& Request, double Now);

	static EMSVoiceType ResolveVoiceType(const AActor* Speaker, const AMSPlayerCharacter* LocalPlayer,
		const UGameInstance* GameInstance);
	static FLinearColor GetPortraitColor(EMSVoiceType Type);

private:
	struct FChannelState
	{
		TOptional<FMSVoiceCutInRequest> Request;
		double DisplayUntil = 0.0;
	};

	double GetNow() const;
	AMSPlayerCharacter* ResolveLocalPlayer() const;
	void RefreshBindings();
	void ClearExpiredRequests(double Now);
	bool TrySubmitRequest(const FMSVoiceCutInRequest& Request, EMSVoiceTrigger CooldownKey, double Now);
	FMSVoiceCutInRequest MakeRequest(AActor* Speaker, AMSPlayerCharacter* LocalPlayer,
		EMSVoiceTrigger Trigger, EMSVoiceCutInChannel Channel, double Now, int32 PriorityOverride = INDEX_NONE) const;

	void HandleKillConfirmed(AActor* Killer);

	UFUNCTION()
	void HandleUnitDeath(AActor* DeadUnit, AActor* Killer);
	UFUNCTION()
	void HandleUnitDamaged(AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation);
	UFUNCTION()
	void HandleDeployed(AMSUnitBase* Unit);
	UFUNCTION()
	void HandleCaptureOwnerChanged(AMSCapturePoint* CapturePoint, EMSCaptureOwner NewOwner);

	void HandlePlayerRepaired(AMSUnitBase* Unit);
	void HandleSupplyPickedUp(AMSUnitBase* Consumer);
	void HandleRecallStarted();

	FChannelState Channels[2];
	/** Cooldowns are isolated per channel so simultaneous Left-death/Right-killer requests are legal. */
	TMap<uint16, double> LastTriggerTimes;
	TArray<double> LocalKillTimes;
	TMap<TWeakObjectPtr<AMSUnitBase>, int32> DeploymentCounts;

	TWeakObjectPtr<class UMSHitFeedbackSubsystem> HitFeedbackSubsystem;
	TSet<TWeakObjectPtr<AMSPlayerCharacter>> BoundPlayers;
	TSet<TWeakObjectPtr<AMSCapturePoint>> BoundCapturePoints;
	TSet<TWeakObjectPtr<AMSRepairPod>> BoundRepairPods;
	TSet<TWeakObjectPtr<AMSSupplyPack>> BoundSupplyPacks;
	TSet<TWeakObjectPtr<UMSEmergencyRecallComponent>> BoundRecallComponents;
};
