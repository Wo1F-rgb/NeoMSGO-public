#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "Online/MSOnlineTypes.h"
#include "GenericTeamAgentInterface.h"
#include "MSOnlineServerSubsystem.generated.h"

class AMSPlayerController;
class APawn;
class FJsonObject;
class UMSMobileSuitData;
struct FMSDeckLoadout;

/** Project auth defaults with explicit command-line overrides, also for worlds without a GameInstance. */
namespace MSOnline
{
	NEOMSGO_API bool IsEnabled();
	NEOMSGO_API FString GetAuthBaseUrl();
	NEOMSGO_API bool IsServerWorld(const UWorld* World);
	NEOMSGO_API bool ParseDeck(const TSharedPtr<FJsonObject>& Object, FMSOnlineDeckSnapshot& Out);
	NEOMSGO_API bool ResolveSlot(const FMSOnlineDeckSlot& Slot, UMSMobileSuitData*& Suit, FMSDeckLoadout& Loadout,
		bool bLegacySchema = false);
	NEOMSGO_API bool ValidateDeck(const FMSOnlineDeckSnapshot& Deck);
}

struct FMSOnlineMatchAccount
{
	FMSOnlineDeckSnapshot Deck;
	TWeakObjectPtr<AMSPlayerController> ActiveController;
	TWeakObjectPtr<APawn> RetainedPawn;
	uint8 TeamId = 0;
	bool bHasDeployed = false;
	bool bWithdrawnForBotFill = false;
	bool bRetainedWasHidden = true;
	bool bRetainedHadCollision = false;
	uint8 RetainedMovementMode = 0;
	double SlotReadyAt[4] = {};
	double RespawnReadyAt = 0;
};

UCLASS()
class NEOMSGO_API UMSOnlineServerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	bool IsReady() const { return bConfigured && bHeartbeatReady; }
	const FString& GetServerId() const { return ServerId; }
	const FText& GetStartupError() const { return StartupError; }
	void ConsumeTicket(AMSPlayerController* Controller, const FString& Ticket, const FString& ConnectionId);
	bool Admit(AMSPlayerController* Controller, const FMSOnlineIdentity& Identity, FMSOnlineDeckSnapshot& Deck);
	void Release(AMSPlayerController* Controller);
	int32 CountActivePlayers(FGenericTeamId TeamId, const AMSPlayerController* Excluding = nullptr) const;
	FMSOnlineMatchAccount* FindAccount(const FMSOnlineIdentity& Identity);
	void RefreshRoomHeartbeat() { Heartbeat(); }
private:
	void RefreshTeamBots();
	void Heartbeat();
	bool TickHeartbeat(float DeltaTime);
	FString RoomInstanceId;
	FString RoomLease;
	FText StartupError;
	FTSTicker::FDelegateHandle RoomHeartbeatTicker;
	FString ServerId;
	FString BaseUrl;
	FString ServerKey;
	FString CatalogHash;
	FString RealmId;
	bool bConfigured = false;
	bool bHeartbeatReady = false;
	bool bHeartbeatPending = false;
	FTimerHandle HeartbeatTimer;
	TMap<FString, FMSOnlineMatchAccount> Accounts;
};
