// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Online/MSRoomDescriptor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSRoomRegistrySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnRoomListUpdated, TArray<FMSRoomDescriptor>, Rooms);

/**
 * Client-side view of the always-on room registry.
 * Polling is deliberately opt-in so no room-list traffic is generated while the UI is closed.
 */
UCLASS(Config=Game, DefaultConfig)
class NEOMSGO_API UMSRoomRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MS|Room Registry")
	void StartPolling(FString RegistryAddress);

	UFUNCTION(BlueprintCallable, Category = "MS|Room Registry")
	void StopPolling();

	/** Requests an immediate refresh. Manual refreshes are limited to one per second. */
	UFUNCTION(BlueprintCallable, Category = "MS|Room Registry")
	bool RefreshNow();

	UFUNCTION(BlueprintPure, Category = "MS|Room Registry")
	TArray<FMSRoomDescriptor> GetLastKnownRooms() const { return LastKnownRooms; }

	UPROPERTY(BlueprintAssignable, Category = "MS|Room Registry")
	FOnRoomListUpdated OnRoomListUpdated;
	UPROPERTY(BlueprintAssignable, Category="MS|Room Registry") FMSRoomStatusChanged OnRoomListStatusChanged;
	UFUNCTION(BlueprintPure, Category="MS|Room Registry") FText GetLastErrorText() const { return LastErrorText; }
	UFUNCTION(BlueprintPure, Category="MS|Room Registry") bool IsRequestPending() const { return bRequestPending; }

	bool IsPolling() const { return bPolling; }

#if WITH_DEV_AUTOMATION_TESTS
	void SetRequestSenderForAutomationTests(TFunction<bool(const FString&)> InSender)
	{
		RequestSenderForAutomationTests = MoveTemp(InSender);
	}
	void SetLastKnownRoomsForAutomationTests(const TArray<FMSRoomDescriptor>& Rooms);
	void FirePollingTimerForAutomationTests() { PollRooms(); }
	int32 GetPollInvocationCountForAutomationTests() const { return PollInvocationCountForAutomationTests; }
	bool IsPollingTimerActiveForAutomationTests() const;
#endif

private:
	void PollRooms();
	bool TickPolling(float DeltaTime);
	void ApplyRoomsResponse(bool bSucceeded, int32 Code, const FString& Json);
	void HandleRoomsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	bool ParseRoomList(const FString& Json, TArray<FMSRoomDescriptor>& OutRooms) const;
	FString NormalizeRegistryAddress(FString Address) const;

	UPROPERTY(Transient)
	TArray<FMSRoomDescriptor> LastKnownRooms;

	FString RegistryBaseUrl;
	FTSTicker::FDelegateHandle PollingTicker;
	FText LastErrorText;
	bool bRequestPending = false;
	bool bOnlinePolling = false;
	int64 PollingAuthGeneration = -1;
	FHttpRequestPtr ActiveRequest;
	bool bPolling = false;
	uint32 PollingGeneration = 0;
	uint32 ActiveRequestGeneration = 0;
	double LastManualRefreshSeconds = -DBL_MAX;

	static constexpr float PollingIntervalSeconds = 5.0f;
	static constexpr double ManualRefreshThrottleSeconds = 1.0;
	static constexpr float RequestTimeoutSeconds = 5.0f;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<bool(const FString&)> RequestSenderForAutomationTests;
	int32 PollInvocationCountForAutomationTests = 0;
#endif
};
