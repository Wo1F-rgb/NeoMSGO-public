#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSIncomingLockComponent.generated.h"

class UMSLockOnComponent;
class AController;

/** Owner-only presentation data. Never contains a source actor or world position. */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSIncomingLockEntry
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) int32 ThreatId = 0;
 UPROPERTY(BlueprintReadOnly) float DirectionYawDegrees = 0.f;
 UPROPERTY(BlueprintReadOnly) bool bHasDistance = false;
 UPROPERTY(BlueprintReadOnly) float DistanceMeters = -1.f;
 bool operator==(const FMSIncomingLockEntry& Other) const;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSIncomingLockSnapshot
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) int32 SpawnEpoch = 0;
 UPROPERTY() uint32 Revision = 0;
 UPROPERTY(BlueprintReadOnly) bool bIncomingLock = false;
 UPROPERTY(BlueprintReadOnly) bool bIncomingMissile = false;
 UPROPERTY(BlueprintReadOnly) TArray<FMSIncomingLockEntry> Entries;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSIncomingLockChanged);

/** Server aggregates actual lock sources; only the receiving pawn's owner gets sanitized output. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSIncomingLockComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UMSIncomingLockComponent();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

 UFUNCTION(BlueprintPure, Category="MS|LockOn")
 const FMSIncomingLockSnapshot& GetSnapshot() const;
 UPROPERTY(BlueprintAssignable, Category="MS|LockOn") FMSIncomingLockChanged OnIncomingLockChanged;

 /** Authority-only. The source state is re-read/validated, never accepted from a supplied payload. */
 void RefreshSource(UMSLockOnComponent* Source);
 void RemoveSource(UMSLockOnComponent* Source);
 void ResetForDeployment();

private:
 UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FMSIncomingLockSnapshot Snapshot;
 UFUNCTION() void OnRep_Snapshot();
 UFUNCTION() void HandleOwnerDeath(AActor* DeadUnit, AActor* Killer);
 void RebuildSnapshot();
 void Publish(FMSIncomingLockSnapshot&& Next);
 struct FTrackedSource
 {
  int32 Id = 0;
  bool bManual = false;
  bool bTracking = false;
  bool bCaptured = false;
 };
 TMap<TWeakObjectPtr<UMSLockOnComponent>, FTrackedSource> Sources;
 int32 NextThreatId = 1;
 int32 ObservedEpoch = INDEX_NONE;
 bool bResetting = false;
};
