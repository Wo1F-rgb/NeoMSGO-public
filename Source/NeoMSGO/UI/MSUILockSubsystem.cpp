#include "UI/MSUILockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const FName PlayerDeadScreenId(TEXT("__PlayerDead__"));
}

void UMSUILockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	OpenScreenStack.Reset();
	bLastGameplayActive = true;
	bLastHasInteractiveScreen = false;
	bInputModeNeedsApply = false;
	bIsApplicationActive = true;

	if (FSlateApplication::IsInitialized())
	{
		ActivationStateChangedHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
			this, &UMSUILockSubsystem::HandleAppActivationStateChanged);
	}
	PreLoadMapWithContextHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(
		this, &UMSUILockSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UMSUILockSubsystem::HandlePostLoadMap);
}

void UMSUILockSubsystem::Deinitialize()
{
	if (ActivationStateChangedHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationStateChangedHandle);
	}
	ActivationStateChangedHandle.Reset();
	if (PreLoadMapWithContextHandle.IsValid())
	{
		FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapWithContextHandle);
	}
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	}
	if (WorldBeginPlayHandle.IsValid() && WorldBeginPlayWorld.IsValid())
	{
		WorldBeginPlayWorld->OnWorldBeginPlay.Remove(WorldBeginPlayHandle);
	}
	PreLoadMapWithContextHandle.Reset();
	PostLoadMapHandle.Reset();
	WorldBeginPlayHandle.Reset();
	WorldBeginPlayWorld.Reset();
	OpenScreenStack.Reset();
	OnGameplayActiveChanged.Clear();
	Super::Deinitialize();
}

void UMSUILockSubsystem::PushScreen(const FName ScreenId, FMSUICloseRequestDelegate OnCloseRequested)
{
	if (OpenScreenStack.ContainsByPredicate(
		[ScreenId](const FMSUIScreenEntry& Entry) { return Entry.ScreenId == ScreenId; }))
	{
		RecomputeAndApply();
		return;
	}

	FMSUIScreenEntry& Entry = OpenScreenStack.AddDefaulted_GetRef();
	Entry.ScreenId = ScreenId;
	Entry.OnCloseRequested = MoveTemp(OnCloseRequested);
	RecomputeAndApply();
}

void UMSUILockSubsystem::PopScreen(const FName ScreenId)
{
	OpenScreenStack.RemoveAll(
		[ScreenId](const FMSUIScreenEntry& Entry) { return Entry.ScreenId == ScreenId; });
	RecomputeAndApply();
}

bool UMSUILockSubsystem::IsAnyScreenOpen() const
{
	return !OpenScreenStack.IsEmpty();
}

bool UMSUILockSubsystem::RequestCloseTopScreen()
{
	for (int32 Index = OpenScreenStack.Num() - 1; Index >= 0; --Index)
	{
		if (OpenScreenStack[Index].bIsDeadMarker)
		{
			continue;
		}

		const FName ScreenId = OpenScreenStack[Index].ScreenId;
		FMSUICloseRequestDelegate CloseRequest = OpenScreenStack[Index].OnCloseRequested;
		if (!CloseRequest.IsBound() || CloseRequest.Execute())
		{
			PopScreen(ScreenId);
		}
		return true;
	}
	return false;
}

void UMSUILockSubsystem::SetPlayerDead(const bool bDead)
{
	const int32 ExistingIndex = OpenScreenStack.IndexOfByPredicate(
		[](const FMSUIScreenEntry& Entry) { return Entry.bIsDeadMarker; });
	if (bDead && ExistingIndex == INDEX_NONE)
	{
		FMSUIScreenEntry& Entry = OpenScreenStack.AddDefaulted_GetRef();
		Entry.ScreenId = PlayerDeadScreenId;
		Entry.bIsDeadMarker = true;
	}
	else if (!bDead && ExistingIndex != INDEX_NONE)
	{
		OpenScreenStack.RemoveAt(ExistingIndex);
	}
	RecomputeAndApply();
}

bool UMSUILockSubsystem::IsGameplayActive() const
{
	return OpenScreenStack.IsEmpty();
}

void UMSUILockSubsystem::RecomputeAndApply()
{
	const bool bGameplayActive = IsGameplayActive();
	const bool bStateChanged = bGameplayActive != bLastGameplayActive;
	// Death blocks gameplay actions but still routes F/Tab through the game.
	// A visible menu above death (for example SpawnSelect) needs UI input even
	// though the aggregate gameplay-active state remains false.
	const bool bHasInteractiveScreen = OpenScreenStack.ContainsByPredicate(
		[](const FMSUIScreenEntry& Entry) { return !Entry.bIsDeadMarker; });
	const bool bInputPolicyChanged = bHasInteractiveScreen != bLastHasInteractiveScreen;
	bLastGameplayActive = bGameplayActive;
	bLastHasInteractiveScreen = bHasInteractiveScreen;

	if (!bIsApplicationActive)
	{
		bInputModeNeedsApply = bInputModeNeedsApply || bStateChanged || bInputPolicyChanged;
	}
	else if (bStateChanged || bInputPolicyChanged || bInputModeNeedsApply)
	{
		const bool bWasInputModeRequested = bInputModeNeedsApply;
		bool bAppliedInputMode = false;
		if (UGameInstance* GameInstance = GetGameInstance(); GameInstance && GameInstance->GetWorld())
		{
			if (APlayerController* PlayerController = GameInstance->GetPrimaryPlayerController(false))
			{
				if (!bHasInteractiveScreen)
				{
					PlayerController->SetInputMode(FInputModeGameOnly());
					PlayerController->bShowMouseCursor = false;
				}
				else
				{
					FInputModeGameAndUI InputMode;
					InputMode.SetHideCursorDuringCapture(false);
					PlayerController->SetInputMode(InputMode);
					PlayerController->bShowMouseCursor = true;
				}
				bAppliedInputMode = true;
			}
		}
		// A post-travel callback can run before the new world's local controller
		// is registered. Keep the one-shot refresh pending for BeginPlay instead
		// of losing it and leaving the old cursor policy behind.
		if (bWasInputModeRequested)
		{
			bInputModeNeedsApply = !bAppliedInputMode;
		}
	}

	if (bStateChanged)
	{
		OnGameplayActiveChanged.Broadcast(bGameplayActive);
	}
}

void UMSUILockSubsystem::HandleAppActivationStateChanged(const bool bIsActive)
{
	bIsApplicationActive = bIsActive;
	if (!bIsActive)
	{
		bInputModeNeedsApply = true;
		return;
	}
	RecomputeAndApply();
}

void UMSUILockSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString&)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}

	// GameInstanceSubsystems survive travel while viewport widgets can lose their
	// old world's GameInstance before NativeDestruct runs.  Clear those entries
	// here so a stale RoomCreate/Lobby delegate cannot keep the next world locked.
	OpenScreenStack.Reset();
	bInputModeNeedsApply = true;
	RecomputeAndApply();
}

void UMSUILockSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!LoadedWorld || !GameInstance)
	{
		return;
	}
	bool bOwnedByThisGameInstance = LoadedWorld->GetGameInstance() == GameInstance;
	if (!bOwnedByThisGameInstance && GEngine)
	{
		if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(LoadedWorld))
		{
			bOwnedByThisGameInstance = WorldContext->OwningGameInstance == GameInstance;
		}
	}
	if (!bOwnedByThisGameInstance)
	{
		return;
	}

	BindWorldBeginPlay(LoadedWorld);

	// Re-apply the policy to the new world's PlayerController.  The stack is
	// normally empty after PreLoadMap; a newly created modal screen will push its
	// own entry after this point and receive the usual UI input mode.
	bInputModeNeedsApply = true;
	RecomputeAndApply();
}

void UMSUILockSubsystem::BindWorldBeginPlay(UWorld* World)
{
	if (!World)
	{
		return;
	}

	if (WorldBeginPlayWorld.Get() == World && WorldBeginPlayHandle.IsValid())
	{
		return;
	}
	if (WorldBeginPlayHandle.IsValid() && WorldBeginPlayWorld.IsValid())
	{
		WorldBeginPlayWorld->OnWorldBeginPlay.Remove(WorldBeginPlayHandle);
	}
	WorldBeginPlayWorld = World;
	WorldBeginPlayHandle = World->OnWorldBeginPlay.AddUObject(
		this, &UMSUILockSubsystem::HandleWorldBeginPlay);
}

void UMSUILockSubsystem::HandleWorldBeginPlay()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* BegunWorld = WorldBeginPlayWorld.Get();
	if (!GameInstance || !BegunWorld || (BegunWorld != GameInstance->GetWorld()
		&& BegunWorld->GetGameInstance() != GameInstance))
	{
		return;
	}

	if (bInputModeNeedsApply)
	{
		RecomputeAndApply();
	}
}
