#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MSMapSensingSubsystem.generated.h"

class AMSUnitBase;

/** Authority-owned map intelligence. Only observed positions are sent to each player owner. */
UCLASS()
class NEOMSGO_API UMSMapSensingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	static constexpr float UpdateIntervalSeconds = 0.5f;
	static constexpr float LostContactHoldSeconds = 5.0f;
	static constexpr float VisualRangeCm = 100000.0f;
	static constexpr int32 MaxLineOfSightTracesPerTeamUpdate = 625;
	static bool IsContactWithinHold(double LastSeenAt, double Now);
	static bool HasFriendlyLineOfSight(UWorld* World, const AMSUnitBase* Friendly,
		const AMSUnitBase* Enemy);
private:
	struct FSighting
	{
		FVector LastKnownLocation = FVector::ZeroVector;
		double LastSeenAt = -1.0;
	};
	TMap<uint8, TMap<TWeakObjectPtr<AMSUnitBase>, FSighting>> LastSeenByTeam;
	float UpdateAccumulator = 0.0f;
	void UpdateTeamContacts();
};
