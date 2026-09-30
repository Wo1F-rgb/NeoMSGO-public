// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSAuthSubsystem.h"

#include "Core/MSGameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Gameplay/MSPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMSAuth, Log, All);

void UMSAuthSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMSOnlineAccountSubsystem>();
	// Online accounts are server-owned. Do not even load the ownerless legacy
	// account list while the explicit online mode is active.
	if (!IsOnlineMode()) ReloadAccounts();

#if !UE_BUILD_SHIPPING
	// Direct-connect multiplayer debug starts on the battle map, so there is no login widget.
	// Each process still uses the normal account SaveGame, isolated by its -UserDir.
	FString DebugUsername;
	if (!IsOnlineMode() && FParse::Value(FCommandLine::Get(), TEXT("MSDebugUser="), DebugUsername))
	{
		if (Login(DebugUsername))
		{
			UE_LOG(LogMSAuth, Display, TEXT("Multiplayer debug login succeeded: %s"), *DebugUsername);
		}
		else
		{
			UE_LOG(LogMSAuth, Error, TEXT("Multiplayer debug login failed: %s"), *DebugUsername);
		}
	}
#endif
}

bool UMSAuthSubsystem::CanVerifyCredentialsLocally() const
{
#if !UE_BUILD_SHIPPING
	// Explicit debug identities are process-local and are intentionally allowed before/while a
	// direct client connection is being established. Production client login remains server-gated.
	FString DebugUsername;
	if (FParse::Value(FCommandLine::Get(), TEXT("MSDebugUser="), DebugUsername))
	{
		return true;
	}
#endif
	const UWorld* World = GetWorld();
	ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
#if WITH_DEV_AUTOMATION_TESTS
	if (NetModeOverrideForAutomationTests.IsSet())
	{
		NetMode = NetModeOverrideForAutomationTests.GetValue();
	}
#endif
	if (NetMode != NM_Client)
	{
		return true;
	}

	// A direct-connect client may reach its login UI without an identity. Permit exactly that
	// initial authentication, then keep the original client-side impersonation guard in force.
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	return Instance && !Instance->IsLoggedIn();
}

bool UMSAuthSubsystem::RegisterAccount(FString Username)
{
	if (IsOnlineMode()) return false;
	if (!CanVerifyCredentialsLocally()) return false;
	Username.TrimStartAndEndInline();
	if (Username.IsEmpty() || FindAccount(Username)) return false;

	FMSAuthAccountRecord Record;
	Record.Username = MoveTemp(Username);
	Accounts.Add(MoveTemp(Record));
	return SaveAccounts();
}

bool UMSAuthSubsystem::Login(FString Username)
{
	if (IsOnlineMode()) return false;
	if (!CanVerifyCredentialsLocally()) return false;
	Username.TrimStartAndEndInline();
	if (Username.IsEmpty()) return false;

	// 身内向けのパスワードレス運用: 未登録名はそのまま新規アカウントとして受け入れる。
	const FMSAuthAccountRecord* Record = FindAccount(Username);
	if (!Record)
	{
		if (!RegisterAccount(Username)) return false;
		Record = FindAccount(Username);
		if (!Record) return false;
	}

	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
	{
		Instance->SetAuthenticatedUser(Record->Username);
	}
	return true;
}

TArray<FString> UMSAuthSubsystem::GatherConnectedUsernames() const
{
	TArray<FString> Connected;
#if WITH_DEV_AUTOMATION_TESTS
	if (ConnectedUsernamesOverrideForAutomationTests.IsSet())
	{
		return ConnectedUsernamesOverrideForAutomationTests.GetValue();
	}
#endif
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState) return Connected;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState);
		if (MSPlayerState && !MSPlayerState->GetLoginUsername().IsEmpty())
		{
			Connected.Add(MSPlayerState->GetLoginUsername());
		}
	}
	return Connected;
}

FString UMSAuthSubsystem::LoginAsGuest()
{
	if (IsOnlineMode()) return FString();
	if (!CanVerifyCredentialsLocally()) return FString();

	// #214: each process numbers guests from its own -UserDir account file, so two windows on one PC
	// would both pick "ゲスト1". Replicated presence already tells a connected client which names are
	// in use, so skip those here and the *displayed* name is right from the first frame. The server
	// still arbitrates in AMSPlayerController::ServerSetLoginUsername, which settles genuine races
	// (two clients logging in before either presence has replicated).
	const TArray<FString> ConnectedUsernames = GatherConnectedUsernames();
	FString GuestName;
	for (int32 Index = 1; Index < 1000; ++Index)
	{
		FString Candidate = FString::Printf(TEXT("ゲスト%d"), Index);
		const bool bTakenByConnectedPlayer = ConnectedUsernames.ContainsByPredicate(
			[&Candidate](const FString& Other) { return Other.Equals(Candidate, ESearchCase::IgnoreCase); });
		if (!FindAccount(Candidate) && !bTakenByConnectedPlayer)
		{
			GuestName = MoveTemp(Candidate);
			break;
		}
	}
	if (GuestName.IsEmpty() || !Login(GuestName)) return FString();
	return GuestName;
}

bool UMSAuthSubsystem::IsOnlineMode() const
{
	const UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (Online && Online->IsOnlineMode()) return true;
	// A missing subsystem must not downgrade an explicitly online process, or
	// overwrite an identity that has already been verified by an account server.
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	if (Instance && !Instance->RealmId.IsEmpty() && Instance->RealmId != TEXT("offline")) return true;
	FString ExplicitMode;
	return FParse::Value(FCommandLine::Get(), TEXT("MSAuthMode="), ExplicitMode)
		&& ExplicitMode.Equals(TEXT("Online"), ESearchCase::IgnoreCase);
}

FString UMSAuthSubsystem::GetOnlineAuthBaseUrl() const
{
	const UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	return Online ? Online->GetAuthBaseUrl() : FString();
}

void UMSAuthSubsystem::LoginOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion)
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->Login(Username, Password, MoveTemp(Completion));
		return;
	}
	if (Completion) Completion(false, TEXT("オンライン認証を初期化できませんでした。"));
}

void UMSAuthSubsystem::RegisterOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion)
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->Register(Username, Password, MoveTemp(Completion));
		return;
	}
	if (Completion) Completion(false, TEXT("オンライン認証を初期化できませんでした。"));
}

void UMSAuthSubsystem::LoginAsGuestOnline(const FString& DisplayName, FMSOnlineAuthCompletion Completion)
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->LoginAsGuest(DisplayName, MoveTemp(Completion));
		return;
	}
	if (Completion) Completion(false, TEXT("オンライン認証を初期化できませんでした。"));
}

void UMSAuthSubsystem::ResumeGuestOnline(FMSOnlineAuthCompletion Completion)
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->ResumeGuest(MoveTemp(Completion));
		return;
	}
	if (Completion) Completion(false, TEXT("オンライン認証を初期化できませんでした。"));
}

void UMSAuthSubsystem::UpgradeGuestOnline(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion)
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->UpgradeGuest(Username, Password, MoveTemp(Completion));
		return;
	}
	if (Completion) Completion(false, TEXT("オンライン認証を初期化できませんでした。"));
}

void UMSAuthSubsystem::LogoutOnline()
{
	if (UMSOnlineAccountSubsystem* Online = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr)
	{
		Online->Logout();
	}
}

bool UMSAuthSubsystem::ReloadAccounts()
{
	Accounts.Reset();
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex)) return false;
	const UMSAuthSaveGame* Save = Cast<UMSAuthSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
	if (!Save) return false;
	Accounts = Save->Accounts;
	return true;
}

bool UMSAuthSubsystem::SaveAccounts() const
{
	UMSAuthSaveGame* Save = Cast<UMSAuthSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSAuthSaveGame::StaticClass()));
	if (!Save) return false;
	Save->Accounts = Accounts;
	return UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, SaveUserIndex);
}

const FMSAuthAccountRecord* UMSAuthSubsystem::FindAccount(const FString& Username) const
{
	return Accounts.FindByPredicate([&Username](const FMSAuthAccountRecord& Record)
	{
		return Record.Username.Equals(Username, ESearchCase::IgnoreCase);
	});
}
