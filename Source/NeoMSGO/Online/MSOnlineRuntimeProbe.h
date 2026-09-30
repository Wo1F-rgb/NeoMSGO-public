#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "MSOnlineRuntimeProbe.generated.h"

/** Opt-in read-only diagnostics for real game/server processes. No gameplay commands or credentials. */
UCLASS()
class UMSOnlineRuntimeProbe : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
private:
	void WriteSnapshot();
	bool TickSnapshot(float DeltaTime);
	FString OutputPath;
	FTSTicker::FDelegateHandle ProbeTicker;
};
