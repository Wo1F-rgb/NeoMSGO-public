// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineStartupConfigTest,
	"NeoMSGO.Online.StartupConfig.DefaultAndCommandLineOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSOnlineStartupConfigTests
{
	FString StripAuthArguments(const FString& CommandLine)
	{
		TArray<FString> Tokens;
		CommandLine.ParseIntoArrayWS(Tokens);
		FString Result;
		for (const FString& Token : Tokens)
		{
			const bool bIsMode = Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
				|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase);
			const bool bIsBaseUrl = Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
				|| Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase);
			if (bIsMode || bIsBaseUrl) continue;
			if (!Result.IsEmpty()) Result.AppendChar(TEXT(' '));
			Result += Token;
		}
		return Result;
	}

	struct FScopedCommandLine
	{
		FString Previous = FCommandLine::Get();

		explicit FScopedCommandLine(const FString& AdditionalArguments)
		{
			FString Effective = StripAuthArguments(Previous);
			if (!AdditionalArguments.IsEmpty())
			{
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += AdditionalArguments;
			}
			FCommandLine::Set(*Effective);
		}

		~FScopedCommandLine()
		{
			FCommandLine::Set(*Previous);
		}
	};

	struct FScopedAccountDefaults
	{
		UMSOnlineAccountSubsystem* Defaults = GetMutableDefault<UMSOnlineAccountSubsystem>();
		EMSOnlineAuthMode PreviousMode = Defaults->GetAuthMode();
		FString PreviousBaseUrl = Defaults->GetAuthBaseUrl();

		FScopedAccountDefaults(const EMSOnlineAuthMode Mode, const FString& BaseUrl)
		{
			Defaults->SetAuthModeForAutomationTests(Mode);
			Defaults->SetAuthBaseUrlForAutomationTests(BaseUrl);
		}

		void Set(const EMSOnlineAuthMode Mode, const FString& BaseUrl)
		{
			Defaults->SetAuthModeForAutomationTests(Mode);
			Defaults->SetAuthBaseUrlForAutomationTests(BaseUrl);
		}

		~FScopedAccountDefaults()
		{
			Defaults->SetAuthModeForAutomationTests(PreviousMode);
			Defaults->SetAuthBaseUrlForAutomationTests(PreviousBaseUrl);
		}
	};

	struct FScopedGameInstance
	{
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		UWorld* World = nullptr;

		FScopedGameInstance()
		{
			if (!GameInstance) return;
			GameInstance->AddToRoot();
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
		}

		~FScopedGameInstance()
		{
			if (GameInstance) GameInstance->Shutdown();
			if (World && GEngine) GEngine->DestroyWorldContext(World);
			if (World) World->DestroyWorld(false);
			if (GameInstance) GameInstance->RemoveFromRoot();
		}
	};

	struct FScopedInitializedAccount
	{
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		FSubsystemCollection<UGameInstanceSubsystem> Collection;
		UMSOnlineAccountSubsystem* Account = nullptr;

		FScopedInitializedAccount()
		{
			if (!GameInstance) return;
			GameInstance->AddToRoot();
			Account = NewObject<UMSOnlineAccountSubsystem>(GameInstance);
			if (Account) Account->Initialize(Collection);
		}

		~FScopedInitializedAccount()
		{
			if (Account) Account->Deinitialize();
			if (GameInstance) GameInstance->RemoveFromRoot();
		}
	};
}

bool FMSOnlineStartupConfigTest::RunTest(const FString&)
{
	using namespace MSOnlineStartupConfigTests;

	UMSOnlineAccountSubsystem* Defaults = GetMutableDefault<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online account config default exists"), Defaults)) return false;

	bool bPassed = true;

	// Read the shipped DefaultGame.ini independently of the test runner's merged
	// Game.ini. An unquoted URL is parsed by UE as "http:", so checking for a
	// scheme delimiter catches that regression without naming the configured host.
	FConfigFile ProjectDefaultConfig;
	ProjectDefaultConfig.Read(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGame.ini")));
	const FConfigSection* OnlineConfig = ProjectDefaultConfig.FindSection(
		TEXT("/Script/NeoMSGO.MSOnlineAccountSubsystem"));
	bPassed &= TestNotNull(TEXT("DefaultGame.ini contains the online account section"), OnlineConfig);
	FString ConfigModeText;
	FString ConfigBaseUrl;
	if (OnlineConfig)
	{
		bPassed &= TestTrue(TEXT("DefaultGame.ini contains AuthMode"),
			OnlineConfig->GetString(TEXT("AuthMode"), ConfigModeText));
		bPassed &= TestTrue(TEXT("DefaultGame.ini contains AuthBaseUrl"),
			OnlineConfig->GetString(TEXT("AuthBaseUrl"), ConfigBaseUrl));
		ConfigModeText.TrimStartAndEndInline();
		ConfigBaseUrl.TrimStartAndEndInline();
		bPassed &= TestTrue(TEXT("DefaultGame.ini AuthMode is Online"),
			ConfigModeText.Equals(TEXT("Online"), ESearchCase::IgnoreCase));
		bPassed &= TestTrue(TEXT("DefaultGame.ini AuthBaseUrl is a complete URL"),
			ConfigBaseUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| ConfigBaseUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase));
	}

	// Feed the values parsed from the real file into a normal no-argument
	// GameInstance startup. The scope restores the CDO even when the merged test
	// config supplied a different value.
	if (OnlineConfig && !ConfigBaseUrl.IsEmpty())
	{
		FScopedAccountDefaults ProjectDefaultScope(
			EMSOnlineAuthMode::Online, ConfigBaseUrl);
		FScopedCommandLine CommandLine(FString{});
		FScopedGameInstance Scope;
		UMSOnlineAccountSubsystem* Account = Scope.GameInstance
			? Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		bPassed &= TestNotNull(TEXT("DefaultGame.ini reaches the real account initialize"), Account);
		if (Account)
		{
			bPassed &= TestTrue(TEXT("DefaultGame.ini reaches Account as Online"),
				Account->GetAuthMode() == EMSOnlineAuthMode::Online);
			bPassed &= TestTrue(TEXT("DefaultGame.ini reaches the common Online mode"),
				MSOnline::IsEnabled());
			bPassed &= TestEqual(TEXT("DefaultGame.ini reaches Account with its complete URL"),
				Account->GetAuthBaseUrl(), ConfigBaseUrl);
			bPassed &= TestEqual(TEXT("DefaultGame.ini reaches the common complete URL"),
				MSOnline::GetAuthBaseUrl(), ConfigBaseUrl);
		}
	}

	FScopedAccountDefaults DefaultScope(
		EMSOnlineAuthMode::Online, TEXT("  http://127.0.0.1:17880  "));

	// A bare project launch has no auth switches. The account is initialized from
	// the Config CDO, and the command-line-independent helpers must agree with it.
	{
		FScopedCommandLine CommandLine(FString{});
		FScopedGameInstance Scope;
		UMSOnlineAccountSubsystem* Account = Scope.GameInstance
			? Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		bPassed &= TestNotNull(TEXT("Config Online creates the real account subsystem"), Account);
		if (Account)
		{
			bPassed &= TestTrue(TEXT("no-argument initialize keeps Config Online"),
				Account->GetAuthMode() == EMSOnlineAuthMode::Online);
			bPassed &= TestTrue(TEXT("no-argument common mode is Online"), MSOnline::IsEnabled());
			bPassed &= TestEqual(TEXT("no-argument account URL uses the Config CDO"),
				Account->GetAuthBaseUrl(), FString(TEXT("http://127.0.0.1:17880")));
			bPassed &= TestEqual(TEXT("no-argument common URL matches the account"),
				MSOnline::GetAuthBaseUrl(), Account->GetAuthBaseUrl());
		}
	}

	// An explicit non-Online mode wins over an Online Config default.
	{
		FScopedCommandLine CommandLine(TEXT("-MSAuthMode=OfflineLocal"));
		// A full GameInstance initializes UMSAuthSubsystem as well; its OfflineLocal
		// startup path reads the user's account SaveGame. Initialize only the real
		// account subsystem through an isolated collection for this no-I/O case.
		FScopedInitializedAccount Scope;
		UMSOnlineAccountSubsystem* Account = Scope.Account;
		bPassed &= TestNotNull(TEXT("explicit OfflineLocal creates the account subsystem"), Account);
		if (Account)
		{
			bPassed &= TestTrue(TEXT("explicit OfflineLocal overrides Config Online"),
				Account->GetAuthMode() == EMSOnlineAuthMode::OfflineLocal);
			bPassed &= TestFalse(TEXT("explicit OfflineLocal leaves the account offline"),
				Account->IsOnlineMode());
			bPassed &= TestFalse(TEXT("explicit OfflineLocal disables the common mode"),
				MSOnline::IsEnabled());
			bPassed &= TestEqual(TEXT("explicit OfflineLocal keeps the trimmed configured URL"),
				Account->GetAuthBaseUrl(), FString(TEXT("http://127.0.0.1:17880")));
		}
	}

	// A Config Offline default can still be promoted by explicit Online switches;
	// the quoted URL deliberately exercises surrounding-whitespace trimming.
	DefaultScope.Set(EMSOnlineAuthMode::OfflineLocal, TEXT("http://127.0.0.1:17880"));
	const FString OverrideUrl = TEXT("http://override.example:17880");
	{
		const FString Arguments = FString::Printf(
			TEXT("-MSAuthMode=Online -MSAuthBaseUrl=\"  %s  \""), *OverrideUrl);
		FScopedCommandLine CommandLine(Arguments);
		FScopedGameInstance Scope;
		UMSOnlineAccountSubsystem* Account = Scope.GameInstance
			? Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		bPassed &= TestNotNull(TEXT("explicit Online creates the account subsystem"), Account);
		if (Account)
		{
			bPassed &= TestTrue(TEXT("explicit Online overrides Config OfflineLocal"),
				Account->GetAuthMode() == EMSOnlineAuthMode::Online);
			bPassed &= TestTrue(TEXT("explicit Online enables the common mode"), MSOnline::IsEnabled());
			bPassed &= TestEqual(TEXT("explicit URL is trimmed and applied to the account"),
				Account->GetAuthBaseUrl(), OverrideUrl);
			bPassed &= TestEqual(TEXT("explicit URL is shared by the common helper"),
				MSOnline::GetAuthBaseUrl(), OverrideUrl);
			bPassed &= TestEqual(TEXT("account and common URL remain identical after override"),
				MSOnline::GetAuthBaseUrl(), Account->GetAuthBaseUrl());
		}
	}

	return bPassed;
}

#endif
