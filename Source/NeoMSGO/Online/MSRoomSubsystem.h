// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/MSOnlineTypes.h"
#include "MSRoomSubsystem.generated.h"

class UWorld;
struct FMSRoomDescriptor;
class UNetDriver;
class AMSPlayerController;

namespace MSRoomProtocol
{
	// Bump the room build ID alongside Backend when replicated gameplay state changes.
	// The network version below is reserved for packed movement wire format changes.
	inline constexpr const TCHAR* BuildId = TEXT("neomsgo-rooms-5");
	inline constexpr uint32 NetworkVersion = 3;
}

/** Population limits shared by room settings, discovery, admission, and UI. */
namespace MSRoomPopulation
{
	inline constexpr int32 TeamSize = 25;
	inline constexpr int32 MaxCombatants = TeamSize * 2;
	inline constexpr int32 LegacyMaxCombatants = 12;
}

/** In-memory capability for one room instance. Never serialized or exposed to Blueprint. */
struct FMSHostedRoomSession
{
	FString RoomId, InstanceId, Lease, BaseUrl, CatalogHash, RealmId;
	int32 GamePort = 7787;
	bool IsValid() const { return !RoomId.IsEmpty() && !InstanceId.IsEmpty() && !Lease.IsEmpty(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSRoomStatusChanged);

UENUM(BlueprintType)
enum class EMSBotDifficulty : uint8
{
	Easy UMETA(DisplayName = "Easy"),
	Normal UMETA(DisplayName = "Normal"),
	Hard UMETA(DisplayName = "Hard"),
	Mixed UMETA(DisplayName = "Mixed")
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSRoomSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString RoomName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FName MapId = TEXT("UnrealizerColony");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room", meta = (ClampMin = "0"))
	int32 RuleIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room", meta = (ClampMin = "0", ClampMax = "11"))
	int32 BotCount = 0;

	/** When enabled, each team is kept at 25 combatants by spawning BOTs into empty seats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	bool bFillTeamsWithBots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	EMSBotDifficulty BotDifficulty = EMSBotDifficulty::Normal;

	int32 GetMaxCombatants() const
	{
		return bFillTeamsWithBots ? MSRoomPopulation::MaxCombatants : MSRoomPopulation::LegacyMaxCombatants;
	}
};

/**
 * Listen-room lifecycle and direct-IP travel entry points.
 * Hosting is advertised to the optional HTTP registry; direct-IP joining remains available.
 */
UCLASS(Config=Game, DefaultConfig)
class NEOMSGO_API UMSRoomSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MS|Room")
	bool HostRoom(const FMSRoomSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "MS|Room")
	bool JoinRoom(const FString& Address);
	UFUNCTION(BlueprintCallable, Category="MS|Room") bool JoinOnlineServer(const FString& ServerId);
	UFUNCTION(BlueprintCallable, Category="MS|Room") bool JoinOnlineRoom(const FString& RoomId, const FString& InstanceId);
	UFUNCTION(BlueprintPure, Category="MS|Room") bool IsRoomOperationPending() const { return bRoomOperationPending; }
	UFUNCTION(BlueprintPure, Category="MS|Room") FText GetRoomStatusText() const { return RoomStatusText; }
	UPROPERTY(BlueprintAssignable, Category="MS|Room") FMSRoomStatusChanged OnRoomStatusChanged;
	const FMSHostedRoomSession& GetHostedSession() const { return HostedSession; }
	void NotifyLocalAdmission(AMSPlayerController* Controller);
	bool IsHostAdmitted() const { return bHostAdmitted; }

	UFUNCTION(BlueprintCallable, Category = "MS|Room")
	void LeaveRoom();

	UFUNCTION(BlueprintPure, Category = "MS|Room")
	const FMSRoomSettings& GetRoomSettings() const { return RoomSettings; }

	/** Called by the authoritative game mode when its listen-server world ends. */
	void NotifyHostSessionEnded(const UWorld* EndedWorld);

	static bool NormalizeAddress(const FString& Address, FString& OutAddress);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetJoinDebounceForAutomationTests() { LastJoinAttemptSeconds = 0.0; }
	void SetRegistryRequestSenderForAutomationTests(
		TFunction<bool(const FString&, const FString&)> InSender)
	{
		RegistryRequestSenderForAutomationTests = MoveTemp(InSender);
	}
	void BeginRegistryHostForAutomationTests(const FMSRoomDescriptor& Descriptor);
	void SendHeartbeatForAutomationTests();
	void EndRegistryHostForAutomationTests();
	static constexpr float GetHeartbeatIntervalForAutomationTests() { return HeartbeatIntervalSeconds; }
#endif

private:
	bool CreateOnlineRoom(const FMSRoomSettings& Settings);
	bool CanStartOnlineOperation();
	void TravelToHostedMap();
	void SetRoomStatus(bool bPending, const FText& Message);
	void CloseOnlineRoom();
	bool TickOnlineRoom(float DeltaTime);
	void FailOnlineRoom(const FText& Message, bool bReturnToLobby);
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error);
	FMSHostedRoomSession HostedSession;
	FString PendingRoomInstance;
	FTSTicker::FDelegateHandle OnlineRoomTicker;
	FText RoomStatusText;
	bool bRoomOperationPending = false;
	bool bHostAdmitted = false;
	bool bHostTicketRequested = false;
	bool bReturningToLobby = false;
	uint32 RoomOperationGeneration = 0;
	double OnlineOperationStartedAt = 0;
	UFUNCTION() void HandleOnlineTicketReady(FString ServerId, FString GameAddress, int64 Generation);
	UFUNCTION() void HandleOnlineRequestFinished(FName Operation, bool bSuccess, FText Message, int64 Generation);
	UFUNCTION() void HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation);
	FString PendingOnlineServer;
	int64 PendingOnlineGeneration = -1;
	void BeginRegistryHosting(const FMSRoomDescriptor& Descriptor);
	void EndRegistryHosting();
	void SendRegistryHeartbeat();
	bool TickRegistryHeartbeat(float DeltaTime);
	bool SendRegistryPost(const FString& Path, const FString& JsonBody);
	void StartHeartbeatTicker();
	void StopHeartbeatTicker();
	void HandlePostLoadMap(UWorld* LoadedWorld);

	UPROPERTY(Transient)
	FMSRoomSettings RoomSettings;

	UPROPERTY(Transient)
	FGuid HostedRoomId;

	UPROPERTY(Config, EditAnywhere, Category = "MS|Room Registry")
	FString RegistryBaseUrl = TEXT("http://127.0.0.1:17788");

	UPROPERTY(Config, EditAnywhere, Category = "MS|Room Registry")
	FString AdvertisedRoomAddress = TEXT("127.0.0.1:7787");

	FTSTicker::FDelegateHandle RegistryHeartbeatTickerHandle;
	TWeakObjectPtr<UWorld> RegisteredHostWorld;
	bool bRegistryHosting = false;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<bool(const FString&, const FString&)> RegistryRequestSenderForAutomationTests;
#endif

	/** Guards against a burst of join activations each spawning its own PendingNetDriver. */
	double LastJoinAttemptSeconds = 0.0;
	static constexpr double JoinDebounceSeconds = 1.0;
	static constexpr float HeartbeatIntervalSeconds = 5.0f;
};
