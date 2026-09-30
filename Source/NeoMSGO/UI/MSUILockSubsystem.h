#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSUILockSubsystem.generated.h"

class UWorld;
struct FWorldContext;

DECLARE_DELEGATE_RetVal(bool, FMSUICloseRequestDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnGameplayActiveChanged, bool /*bGameplayActive*/);

/** Owns the aggregate UI-open state and applies the corresponding local input mode. */
UCLASS()
class NEOMSGO_API UMSUILockSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void PushScreen(FName ScreenId,
		FMSUICloseRequestDelegate OnCloseRequested = FMSUICloseRequestDelegate());
	void PopScreen(FName ScreenId);
	bool IsAnyScreenOpen() const;
	bool RequestCloseTopScreen();
	void SetPlayerDead(bool bDead);
	bool IsGameplayActive() const;

	FMSOnGameplayActiveChanged OnGameplayActiveChanged;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void RecomputeAndApply();
	void HandleAppActivationStateChanged(bool bIsActive);
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void BindWorldBeginPlay(UWorld* World);
	void HandleWorldBeginPlay();

	struct FMSUIScreenEntry
	{
		FName ScreenId;
		FMSUICloseRequestDelegate OnCloseRequested;
		bool bIsDeadMarker = false;
	};

	TArray<FMSUIScreenEntry> OpenScreenStack;
	bool bLastGameplayActive = true;
	bool bLastHasInteractiveScreen = false;
	bool bIsApplicationActive = true;
	bool bInputModeNeedsApply = false;
	FDelegateHandle ActivationStateChangedHandle;
	FDelegateHandle PreLoadMapWithContextHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle WorldBeginPlayHandle;
	TWeakObjectPtr<UWorld> WorldBeginPlayWorld;
};
