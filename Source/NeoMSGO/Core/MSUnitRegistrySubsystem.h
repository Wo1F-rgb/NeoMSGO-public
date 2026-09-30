#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MSUnitRegistrySubsystem.generated.h"

class AMSUnitBase;

DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnUnitRegistered, AMSUnitBase*);

/** Spatially simple, allocation-free team registry. It deliberately performs no actor-world scan. */
UCLASS()
class NEOMSGO_API UMSUnitRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** Fired only when a valid unit is newly added to the registry. */
	FMSOnUnitRegistered OnUnitRegistered;

	void RegisterUnit(AMSUnitBase* Unit);
	void UnregisterUnit(AMSUnitBase* Unit);
	AMSUnitBase* FindNearestHostileUnit(const AActor* Querier, float MaxDistance) const;
	/** Counts registered units on TeamId. Dead units are omitted by default for capture-area population queries. */
	UFUNCTION(BlueprintPure, Category="MS|Team")
	int32 CountUnitsOfTeam(FGenericTeamId TeamId, bool bExcludeDead = true) const;
	/** Counts living team members inside a ground-plane cylinder (vertical position is ignored). */
	UFUNCTION(BlueprintPure, Category="MS|Team")
	int32 CountUnitsOfTeamInRange(FGenericTeamId TeamId, const FVector& Origin, float RadiusCm,
		bool bExcludeDead = true) const;
	bool IsRegistered(const AMSUnitBase* Unit) const;
	int32 GetRegisteredUnitCount() const;
	/** Visits every currently valid registered unit without exposing registry storage. */
	void ForEachRegisteredUnit(TFunctionRef<void(AMSUnitBase*)> Visitor) const;
private:
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AMSUnitBase>> Units;
};
