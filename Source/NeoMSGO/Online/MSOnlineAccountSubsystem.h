// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Online/MSOnlineTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSOnlineAccountSubsystem.generated.h"

class IHttpResponse;
class UMSGameInstance;
class IHttpRequest;

UENUM(BlueprintType)
enum class EMSOnlineAuthMode : uint8
{
	OfflineLocal UMETA(DisplayName = "Offline local"),
	Online UMETA(DisplayName = "Online")
};

UENUM(BlueprintType)
enum class EMSOnlineLoginState : uint8
{
	LoggedOut,
	Pending,
	Authenticated,
	Failed
};

UENUM(BlueprintType)
enum class EMSOnlineDeckSyncState : uint8
{
	None,
	Pending,
	Committed,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnlineLoginStateChanged, EMSOnlineLoginState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnlineDeckSyncStateChanged, EMSOnlineDeckSyncState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnlineAccountChanged, FMSOnlineIdentity, Identity, int64, Generation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnlineDeckCommitted, FMSOnlineDeckSnapshot, Deck, int64, Generation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMSOnlineRequestFinished, FName, Operation, bool, bSucceeded,
	FText, Message, int64, Generation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMSOnlineGameTicketReady, FString, ServerId, FString, GameAddress,
	int64, Generation);

/** Completion for an auth operation. The message is safe for display and never contains a token. */
using FMSOnlineAuthCompletion = TFunction<void(bool bSucceeded, const FString& Message)>;
using FMSOnlineDeckCompletion = TFunction<void(EMSOnlineDeckSyncState State, const FString& Message,
	const FMSOnlineDeckSnapshot& Snapshot)>;

UCLASS()
class UMSOnlineDeckCacheSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame) FString RealmId;
	UPROPERTY(SaveGame) FString AccountId;
	UPROPERTY(SaveGame) int32 SchemaVersion = 3;
	UPROPERTY(SaveGame) int64 Revision = 0;
	UPROPERTY(SaveGame) FString DeckJson;
};

/**
 * HTTP-backed identity and account data. Project configuration selects the normal startup
 * mode; -MSAuthMode and -MSAuthBaseUrl explicitly override it for local work or another host.
 *
 * Tokens are process-only. Guest resume tokens are stored in a per-realm/per-server cache
 * slot, while passwords are never persisted or copied into a SaveGame.
 */
UCLASS(Config=Game, DefaultConfig)
class NEOMSGO_API UMSOnlineAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Progress APIs share the current authenticated transport; bearer never leaves this subsystem. */
	bool SendBattleAssistRequest(const FString& Verb, const FString& Path, const FString& Body,
		TFunction<void(bool, int32, const FString&)> Completion);
	/** Room APIs use the account session without exposing its bearer credential. */
	bool SendRoomRequest(const FString& Verb, const FString& Path, const FString& Body,
		TFunction<void(bool, int32, const FString&)> Completion);
	bool RequestRoomTicket(const FString& RoomId, const FString& InstanceId);
	void CancelPendingGameTicket();
	const FString& GetCatalogHash() const { return CatalogHash; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	EMSOnlineAuthMode GetAuthMode() const { return AuthMode; }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	bool IsOnlineMode() const { return AuthMode == EMSOnlineAuthMode::Online; }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	EMSOnlineLoginState GetLoginState() const { return LoginState; }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	FString GetAuthBaseUrl() const { return AuthBaseUrl; }

	const FMSOnlineIdentity& GetIdentity() const { return Identity; }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	bool HasAuthenticatedIdentity() const { return LoginState == EMSOnlineLoginState::Authenticated && !Identity.AccountId.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	int64 GetAuthGeneration() const { return AuthGeneration; }

	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	bool HasOnlineSession() const { return HasAuthenticatedIdentity() && !AccessToken.IsEmpty(); }

	const FMSOnlineDeckSnapshot& GetCommittedDeck() const { return CommittedDeck; }
	bool IsDeckRequestPending() const { return bDeckRequestPending; }
	UFUNCTION(BlueprintPure, Category="MS|Online|Auth")
	FString GetCredentialStorageWarning() const { return CredentialStorageWarning; }

	/** POST /v1/auth/login with {username,password}. */
	void Login(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	/** POST /v1/auth/register with {username,password}. */
	void Register(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	/** POST /v1/auth/guest with optional display_name. */
	void LoginAsGuest(const FString& DisplayName, FMSOnlineAuthCompletion Completion);
	/** POST /v1/auth/resume using the locally cached guest resume token. */
	void ResumeGuest(FMSOnlineAuthCompletion Completion);
	/** POST /v1/auth/upgrade with the current guest Bearer token; server keeps AccountId. */
	void UpgradeGuest(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	/** Starts server logout and clears the process-only token immediately. */
	void Logout();

	/** POST /v1/game/tickets. The opaque ticket stays private until TakePendingGameTicket. */
	bool RequestGameTicket(const FString& ServerId);
	/** Takes a matching, unexpired ticket exactly once for the current auth generation. */
	bool TakePendingGameTicket(const FString& ExpectedServerId, FString& OutTicket);

	/** Returns the isolated account+realm deck cache if one has been received this session. */
	bool GetCachedDeck(FMSOnlineDeckSnapshot& OutSnapshot) const;
	/**
	 * PUT /v1/me/deck. The candidate is only committed after a successful response. A 409
	 * leaves the current committed snapshot untouched and returns the server revision.
	 */
	bool PutDeck(const FMSOnlineDeckSnapshot& Candidate, FMSOnlineDeckCompletion Completion);
	/** GET /v1/me/deck and replace the committed account cache only on a valid response. */
	bool RefreshCommittedDeck(FMSOnlineDeckCompletion Completion);
	/** Converts an explicit local deck import into an online candidate; never called implicitly. */
	bool ImportLocalDeck(const FMSOnlineDeckSnapshot& Candidate, FMSOnlineDeckCompletion Completion);

	UPROPERTY(BlueprintAssignable, Category="MS|Online|Auth")
	FMSOnlineLoginStateChanged OnLoginStateChanged;

	UPROPERTY(BlueprintAssignable, Category="MS|Online|Auth")
	FMSOnlineAccountChanged OnAccountChanged;

	UPROPERTY(BlueprintAssignable, Category="MS|Online|Deck")
	FMSOnlineDeckSyncStateChanged OnDeckSyncStateChanged;

	UPROPERTY(BlueprintAssignable, Category="MS|Online|Deck")
	FMSOnlineDeckCommitted OnDeckCommitted;

	UPROPERTY(BlueprintAssignable, Category="MS|Online")
	FMSOnlineRequestFinished OnRequestFinished;

	UPROPERTY(BlueprintAssignable, Category="MS|Online")
	FMSOnlineGameTicketReady OnGameTicketReady;

#if WITH_DEV_AUTOMATION_TESTS
	void SetAuthModeForAutomationTests(EMSOnlineAuthMode InMode) { AuthMode = InMode; }
	void SetAuthBaseUrlForAutomationTests(const FString& InUrl) { AuthBaseUrl = InUrl; }
	void SetIdentityForAutomationTests(const FMSOnlineIdentity& InIdentity);
	void SetDeckForAutomationTests(const FMSOnlineDeckSnapshot& InSnapshot);
	void SetRequestSenderForAutomationTests(TFunction<bool(const FString&, const FString&, const FString&, const FString&)> InSender)
	{
		RequestSenderForAutomationTests = MoveTemp(InSender);
	}
	void SetResponseForAutomationTests(int32 InCode, const FString& InBody)
	{
		AutomationResponseCode = InCode;
		AutomationResponseBody = InBody;
	}
	void SetDeferredResponsesForAutomationTests(bool bDeferred) { bDeferResponsesForAutomationTests = bDeferred; }
	bool CompleteNextResponseForAutomationTests(int32 Code, const FString& Body);
	uint64 GetRequestGenerationForAutomationTests() const { return RequestGeneration; }
	void SetPendingTicketForAutomationTests(const FString& ServerId, const FString& GameAddress, const FString& Ticket);
#endif

private:
	void BeginAuthRequest(const FString& Path, const FString& Body, const FString& Operation,
		FMSOnlineAuthCompletion Completion);
	void HandleAuthResponse(uint64 Generation, const FString& Operation, FMSOnlineAuthCompletion Completion,
		bool bSucceeded, int32 ResponseCode, const FString& Body);
	void HandleLogoutResponse(bool bSucceeded, int32 ResponseCode);
	bool SendHttpRequest(const FString& Verb, const FString& Path, const FString& Body, bool bWithBearer,
		uint64 Generation, TFunction<void(bool, int32, const FString&)> Completion);
	bool ParseAuthEnvelope(const FString& Body, FMSOnlineIdentity& OutIdentity, FMSOnlineDeckSnapshot& OutDeck,
		FString& OutError);
	bool ParseDeck(const TSharedPtr<class FJsonObject>& DeckObject, FMSOnlineDeckSnapshot& OutSnapshot, FString& OutError) const;
	bool SerializeDeck(const FMSOnlineDeckSnapshot& Snapshot, FString& OutJson) const;
	void SetLoginState(EMSOnlineLoginState NewState);
	void ApplyIdentity(const FMSOnlineIdentity& NewIdentity, const FMSOnlineDeckSnapshot& NewDeck);
	void ClearIdentity();
	bool SaveGuestResumeToken();
	void SaveCommittedDeckCache() const;
	bool LoadGuestResumeToken(FString& OutToken, FString& OutDisplayName);
	FString GetGuestResumeServerScope() const;
	FString GetCredentialDirectory() const;
	FString GetGuestResumeSlot() const;
	FString GetAccountCacheKey() const;
	FString BuildUrl(const FString& Path) const;

	UPROPERTY(Config, EditAnywhere, Category="MS|Online|Auth")
	EMSOnlineAuthMode AuthMode = EMSOnlineAuthMode::OfflineLocal;

	UPROPERTY(Config, EditAnywhere, Category="MS|Online|Auth")
	FString AuthBaseUrl = TEXT("http://127.0.0.1:17980");

	UPROPERTY(Transient)
	FMSOnlineIdentity Identity;

	UPROPERTY(Transient)
	EMSOnlineLoginState LoginState = EMSOnlineLoginState::LoggedOut;

	UPROPERTY(Transient)
	TMap<FString, FMSOnlineDeckSnapshot> AccountDeckCache;

	UPROPERTY(Transient)
	FMSOnlineDeckSnapshot CommittedDeck;

	/** Authentication secrets stay transient and are never exposed to Blueprint or SaveGame. */
	FString CurrentUsername;
	FString AccessToken;
	FString GuestResumeToken;
	FString ExpiresAt;
	FString CatalogHash;
	FString CredentialStorageWarning;
	bool bDeckRequestPending = false;

	int64 AuthGeneration = 0;
	uint64 RequestGeneration = 0;
	struct FPendingGameTicket
	{
		FString ServerId;
		FString GameAddress;
		FString Ticket;
		int64 ExpiresAtMs = 0;
		int64 Generation = 0;
		bool bConsumed = false;
	};
	FPendingGameTicket PendingGameTicket;
	uint64 GameTicketRequestGeneration = 0;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<bool(const FString&, const FString&, const FString&, const FString&)> RequestSenderForAutomationTests;
	int32 AutomationResponseCode = 200;
	FString AutomationResponseBody;
	bool bDeferResponsesForAutomationTests = false;
	TArray<TFunction<void(int32, const FString&)>> DeferredResponsesForAutomationTests;
#endif
};
