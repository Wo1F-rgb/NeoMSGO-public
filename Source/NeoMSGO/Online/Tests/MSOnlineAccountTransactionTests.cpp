#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSCredentialStore.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <windows.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
FString TransactionDeckJson(int64 Revision)
{
	return FString::Printf(TEXT("{\"schema_version\":2,\"revision\":%lld,\"selected_slot\":0,\"slots\":[")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},")
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false}]}"), static_cast<long long>(Revision));
}
FString TransactionAuthJson(const TCHAR* Id, bool bGuest = false)
{
	return FString::Printf(TEXT("{\"realm_id\":\"transaction-regression\",\"account_id\":\"%s\",")
		TEXT("\"username\":\"%s\",\"display_name\":\"test\",\"is_guest\":%s,\"token\":\"test-token\",\"deck\":%s}"),
		Id, Id, bGuest ? TEXT("true") : TEXT("false"), *TransactionDeckJson(4));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineIdentityTransactionTest,
	"NeoMSGO.Online.Account.ReplacementFailureAndStaleLogout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSOnlineIdentityTransactionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Instance.Get()));
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	Account->SetRequestSenderForAutomationTests([](const FString&, const FString&, const FString&, const FString&) { return true; });
	Account->SetResponseForAutomationTests(200, TransactionAuthJson(TEXT("a")));
	Account->Login(TEXT("a"), TEXT("test-password"), nullptr);
	TestTrue(TEXT("first account is authenticated"), Instance->IsLoggedIn() && Account->HasOnlineSession());
	const int64 GenerationA = Account->GetAuthGeneration();
	Account->SetResponseForAutomationTests(401, TEXT("{\"error\":{\"code\":\"invalid_credentials\"}}"));
	Account->Login(TEXT("b"), TEXT("wrong-password"), nullptr);
	TestFalse(TEXT("failed replacement does not expose old GameInstance identity"), Instance->IsLoggedIn());
	TestFalse(TEXT("failed replacement has no old token or account"), Account->HasOnlineSession() || Account->GetIdentity().IsValid());
	TestTrue(TEXT("replacement invalidates connected old account generation"), Account->GetAuthGeneration() > GenerationA);
	Account->SetDeferredResponsesForAutomationTests(true);
	bool bLateCallbackRan = false;
	Account->Login(TEXT("b"), TEXT("test-password"), [&bLateCallbackRan](bool, const FString&) { bLateCallbackRan = true; });
	TestTrue(TEXT("deferred request is Pending"), Account->GetLoginState() == EMSOnlineLoginState::Pending);
	Account->Logout();
	Account->CompleteNextResponseForAutomationTests(200, TransactionAuthJson(TEXT("b")));
	TestFalse(TEXT("logout discards delayed successful authentication"), Account->HasOnlineSession() || Instance->IsLoggedIn() || bLateCallbackRan);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineDeckTransactionTest,
	"NeoMSGO.Online.Account.SerializedDeckAndAtomicUpgrade", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSOnlineDeckTransactionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>(GEngine));
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Instance.Get()));
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	int32 Requests = 0;
	FString LastBearer;
	Account->SetRequestSenderForAutomationTests([&Requests, &LastBearer](const FString&, const FString&, const FString&, const FString& Bearer)
	{
		++Requests;
		LastBearer = Bearer;
		return true;
	});
	Account->SetResponseForAutomationTests(200, TransactionAuthJson(TEXT("guest"), true));
	Account->LoginAsGuest(FString(), nullptr);
	// A malformed successful upgrade must not replace the still-valid guest token.
	Account->SetResponseForAutomationTests(200, TEXT("{\"realm_id\":\"x\",\"account_id\":\"x\",\"token\":\"wrong-token\",\"deck\":{}}"));
	Account->UpgradeGuest(TEXT("new-account"), TEXT("test-password"), nullptr);
	TestTrue(TEXT("malformed upgrade preserves valid guest identity"), Account->HasOnlineSession() && Account->GetIdentity().AccountId == TEXT("guest"));
	Account->SetDeferredResponsesForAutomationTests(true);
	FMSOnlineDeckSnapshot Candidate = Account->GetCommittedDeck();
	bool bCommitted = false;
	TestTrue(TEXT("first candidate starts"), Account->PutDeck(Candidate, [&bCommitted](EMSOnlineDeckSyncState State, const FString&, const FMSOnlineDeckSnapshot&) { if (State == EMSOnlineDeckSyncState::Committed) bCommitted = true; }));
	TestEqual(TEXT("malformed upgrade did not replace bearer"), LastBearer, FString(TEXT("test-token")));
	const int32 RequestCountBeforeRejected = Requests;
	TestFalse(TEXT("second candidate rejected while first is pending"), Account->PutDeck(Candidate, nullptr));
	TestFalse(TEXT("concurrent refresh cannot overwrite a pending save"), Account->RefreshCommittedDeck(nullptr));
	TestEqual(TEXT("rejected concurrent operations never hit transport"), Requests, RequestCountBeforeRejected);
	TestEqual(TEXT("candidate not applied before acknowledgement"), Account->GetCommittedDeck().Revision, static_cast<int64>(4));
	Account->CompleteNextResponseForAutomationTests(200, TEXT("{\"deck\":") + TransactionDeckJson(5) + TEXT("}"));
	TestTrue(TEXT("first acknowledgement commits"), bCommitted && !Account->IsDeckRequestPending());
	TestEqual(TEXT("server revision retained"), Account->GetCommittedDeck().Revision, static_cast<int64>(5));
	Candidate = Account->GetCommittedDeck();
	Account->PutDeck(Candidate, nullptr);
	Account->CompleteNextResponseForAutomationTests(409, TEXT("{\"current_revision\":6}"));
	TestEqual(TEXT("conflict does not roll back previous confirmed deck"), Account->GetCommittedDeck().Revision, static_cast<int64>(5));
	TestFalse(TEXT("conflict releases serialization gate"), Account->IsDeckRequestPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCredentialProtectionTest,
	"NeoMSGO.Online.Credentials.ScopeBoundProtection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSCredentialProtectionTest::RunTest(const FString& Parameters)
{
	FString Protected;
	FString Decoded;
#if PLATFORM_WINDOWS
	TestTrue(TEXT("current Windows user can protect an opaque credential"), MSCredentialStore::Protect(TEXT("synthetic-resume-credential"), TEXT("server-a|realm-a"), Protected));
	TestFalse(TEXT("stored representation does not contain plaintext"), Protected.Contains(TEXT("synthetic-resume-credential")));
	TestTrue(TEXT("same user and scope restores credential"), MSCredentialStore::Unprotect(Protected, TEXT("server-a|realm-a"), Decoded));
	TestEqual(TEXT("restored credential matches"), Decoded, FString(TEXT("synthetic-resume-credential")));
	TestFalse(TEXT("different realm cannot decrypt"), MSCredentialStore::Unprotect(Protected, TEXT("server-a|realm-b"), Decoded));
	TestTrue(TEXT("failed decryption clears output"), Decoded.IsEmpty());
#else
	TestFalse(TEXT("unsupported platform never stores plaintext"), MSCredentialStore::Protect(TEXT("synthetic"), TEXT("scope"), Protected));
	TestTrue(TEXT("unsupported platform clears output"), Protected.IsEmpty());
#endif
	TestFalse(TEXT("plaintext fallback rejected"), MSCredentialStore::Unprotect(TEXT("synthetic-resume-credential"), TEXT("server-a|realm-a"), Decoded));
	TestFalse(TEXT("malformed ciphertext rejected"), MSCredentialStore::Unprotect(TEXT("dpapi1:AAAA"), TEXT("server-a|realm-a"), Decoded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCredentialDiskRoundTripTest,
	"NeoMSGO.Online.Credentials.AtomicDiskRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSCredentialDiskRoundTripTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/Credentials"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ScopeA = TEXT("http://127.0.0.1:17880|home");
	const FString ScopeB = TEXT("http://127.0.0.2:17880|home");
	const FString FileA = MSCredentialStore::GetGuestFilePath(Directory, ScopeA);
	const FString FileB = MSCredentialStore::GetGuestFilePath(Directory, ScopeB);
	FString Realm, Name, Secret;
	TestTrue(TEXT("fresh UserDir has no remembered guest"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::NotFound);
#if PLATFORM_WINDOWS
	TestTrue(TEXT("guest save creates a real encrypted credential file"), MSCredentialStore::SaveGuest(Directory, ScopeA, TEXT("realm-a"), TEXT("Guest A"), TEXT("synthetic-disk-secret-a")));
	FString StoredBytes;
	TestTrue(TEXT("credential exists on disk immediately after save returns"), FFileHelper::LoadFileToString(StoredBytes, *FileA));
	TestFalse(TEXT("physical file excludes plaintext credential"), StoredBytes.Contains(TEXT("synthetic-disk-secret-a")));
	TestTrue(TEXT("fresh load without GConfig or cached identity decrypts the saved file"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::Success);
	TestEqual(TEXT("disk roundtrip retains realm"), Realm, FString(TEXT("realm-a")));
	TestEqual(TEXT("disk roundtrip retains display name"), Name, FString(TEXT("Guest A")));
	TestTrue(TEXT("disk roundtrip retains opaque secret"), Secret == TEXT("synthetic-disk-secret-a"));
	TestTrue(TEXT("separate server scope starts empty"), MSCredentialStore::LoadGuest(Directory, ScopeB, Realm, Name, Secret) == MSCredentialStore::ELoadResult::NotFound);
	TestTrue(TEXT("test can copy an encrypted file into the wrong scope name"), FFileHelper::SaveStringToFile(StoredBytes, *FileB));
	TestTrue(TEXT("copied credential cannot impersonate another server scope"), MSCredentialStore::LoadGuest(Directory, ScopeB, Realm, Name, Secret) == MSCredentialStore::ELoadResult::Failed);
	TestTrue(TEXT("failed read clears all output identity data"), Realm.IsEmpty() && Name.IsEmpty() && Secret.IsEmpty());

	// Real Windows sharing failure: replacement must fail without deleting or
	// truncating the previously working credential. No injected success flag.
	HANDLE LockedFile = CreateFileW(*FileA, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	TestTrue(TEXT("existing credential can be locked against replacement"), LockedFile != INVALID_HANDLE_VALUE);
	if (LockedFile != INVALID_HANDLE_VALUE)
	{
		TestFalse(TEXT("locked destination reports failed save"), MSCredentialStore::SaveGuest(Directory, ScopeA, TEXT("realm-new"), TEXT("Guest New"), TEXT("synthetic-disk-secret-new")));
		CloseHandle(LockedFile);
	}
	TestTrue(TEXT("failed replacement preserves readable previous credential"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::Success);
	TestTrue(TEXT("failed replacement preserves previous realm and secret"), Realm == TEXT("realm-a") && Secret == TEXT("synthetic-disk-secret-a"));
	TestTrue(TEXT("unlocked atomic replacement succeeds"), MSCredentialStore::SaveGuest(Directory, ScopeA, TEXT("realm-new"), TEXT("Guest New"), TEXT("synthetic-disk-secret-new")));
	TestTrue(TEXT("fresh read selects new realm from the same atomic envelope"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::Success && Realm == TEXT("realm-new") && Secret == TEXT("synthetic-disk-secret-new"));
	TestFalse(TEXT("old realm cannot delete a new realm credential"), MSCredentialStore::RemoveGuest(Directory, ScopeA, TEXT("realm-a")));
	TestTrue(TEXT("matching upgraded realm can remove its credential"), MSCredentialStore::RemoveGuest(Directory, ScopeA, TEXT("realm-new")));
	TestTrue(TEXT("removed credential remains missing on fresh read"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::NotFound);
	TestTrue(TEXT("corrupted JSON fixture can be saved"), FFileHelper::SaveStringToFile(TEXT("{broken"), *FileA));
	TestTrue(TEXT("corruption is failure rather than a different remembered identity"), MSCredentialStore::LoadGuest(Directory, ScopeA, Realm, Name, Secret) == MSCredentialStore::ELoadResult::Failed);
#else
	TestFalse(TEXT("unsupported platform refuses credential persistence"), MSCredentialStore::SaveGuest(Directory, ScopeA, TEXT("realm-a"), TEXT("Guest"), TEXT("synthetic")));
	TestFalse(TEXT("unsupported platform creates no plaintext fallback"), IFileManager::Get().FileExists(*FileA));
#endif
	// Only test-owned exact files and the now-empty unique directory are removed.
	IFileManager::Get().Delete(*FileA, false, false, true);
	IFileManager::Get().Delete(*FileB, false, false, true);
	IFileManager::Get().DeleteDirectory(*Directory, false, false);
	return true;
}

#endif
