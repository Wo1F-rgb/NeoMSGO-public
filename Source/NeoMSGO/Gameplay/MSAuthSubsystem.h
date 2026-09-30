// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSAuthSubsystem.generated.h"

USTRUCT()
struct FMSAuthAccountRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString Username;
};

UCLASS()
class UMSAuthSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame) TArray<FMSAuthAccountRecord> Accounts;
};

/** OfflineLocal account compatibility layer. Explicit Online mode delegates to HTTP auth. */
UCLASS()
class NEOMSGO_API UMSAuthSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Authority/standalone only. 既存名はfalse(重複)。 */
	bool RegisterAccount(FString Username);
	/** Authority/standalone only. 未登録名は自動登録してログインする。空名のみ失敗。 */
	bool Login(FString Username);
	/** 「ゲストN」の空き番号で自動登録+ログインし、確定したゲスト名を返す(失敗時は空文字)。 */
	FString LoginAsGuest();
	bool ReloadAccounts();

	/** In NM_Client worlds, permits initial login only; an authenticated client cannot switch identity locally. */
	bool CanVerifyCredentialsLocally() const;

	/** Online mode is explicit; these calls never fall back to the local name database. */
	bool IsOnlineMode() const;
	FString GetOnlineAuthBaseUrl() const;
	void LoginOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	void RegisterOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	void LoginAsGuestOnline(const FString& DisplayName, FMSOnlineAuthCompletion Completion);
	void ResumeGuestOnline(FMSOnlineAuthCompletion Completion);
	void UpgradeGuestOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion);
	void LogoutOnline();

#if WITH_DEV_AUTOMATION_TESTS
	void SetSaveSlotForAutomationTests(const FString& InSlotName) { SaveSlotName = InSlotName; }
	void ResetAndReloadForAutomationTests() { Accounts.Reset(); ReloadAccounts(); }
	int32 GetAccountCountForAutomationTests() const { return Accounts.Num(); }
	void SetNetModeForAutomationTests(ENetMode InNetMode) { NetModeOverrideForAutomationTests = InNetMode; }
	void SetConnectedUsernamesForAutomationTests(const TArray<FString>& InUsernames)
	{
		ConnectedUsernamesOverrideForAutomationTests = InUsernames;
	}
#endif

private:
	/** Names already held by connected players, read from replicated AMSPlayerState presence. */
	TArray<FString> GatherConnectedUsernames() const;
	const FMSAuthAccountRecord* FindAccount(const FString& Username) const;
	bool SaveAccounts() const;

	UPROPERTY(Transient) TArray<FMSAuthAccountRecord> Accounts;
	FString SaveSlotName = TEXT("NeoMSGO_Accounts_v1");
	static constexpr int32 SaveUserIndex = 0;
#if WITH_DEV_AUTOMATION_TESTS
	TOptional<ENetMode> NetModeOverrideForAutomationTests;
	TOptional<TArray<FString>> ConnectedUsernamesOverrideForAutomationTests;
#endif
};
