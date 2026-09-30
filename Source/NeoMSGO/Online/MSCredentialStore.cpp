#include "Online/MSCredentialStore.h"

#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <windows.h>
#include <wincrypt.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool MSCredentialStore::Protect(const FString& Secret, const FString& Scope, FString& OutProtected)
{
	OutProtected.Reset();
#if PLATFORM_WINDOWS
	if (Secret.IsEmpty() || Secret.Len() > 4096 || Scope.IsEmpty()) return false;
	FTCHARToUTF8 SecretUtf8(*Secret);
	FTCHARToUTF8 ScopeUtf8(*Scope);
	DATA_BLOB Input{static_cast<DWORD>(SecretUtf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(SecretUtf8.Get()))};
	DATA_BLOB Entropy{static_cast<DWORD>(ScopeUtf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(ScopeUtf8.Get()))};
	DATA_BLOB Output{};
	// No LOCAL_MACHINE flag: only the Windows user who stored it can decrypt it.
	if (!CryptProtectData(&Input, L"NeoMSGO guest resume", &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output)) return false;
	OutProtected = TEXT("dpapi1:") + FBase64::Encode(Output.pbData, Output.cbData);
	FMemory::Memzero(Output.pbData, Output.cbData);
	LocalFree(Output.pbData);
	return true;
#else
	return false;
#endif
}

bool MSCredentialStore::Unprotect(const FString& Protected, const FString& Scope, FString& OutSecret)
{
	OutSecret.Reset();
#if PLATFORM_WINDOWS
	if (!Protected.StartsWith(TEXT("dpapi1:")) || Protected.Len() > 32768 || Scope.IsEmpty()) return false;
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Protected.Mid(7), Bytes) || Bytes.IsEmpty()) return false;
	FTCHARToUTF8 ScopeUtf8(*Scope);
	DATA_BLOB Input{static_cast<DWORD>(Bytes.Num()), Bytes.GetData()};
	DATA_BLOB Entropy{static_cast<DWORD>(ScopeUtf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(ScopeUtf8.Get()))};
	DATA_BLOB Output{};
	if (!CryptUnprotectData(&Input, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output)) return false;
	const bool bValidSize = Output.cbData > 0 && Output.cbData <= 16384;
	if (bValidSize)
	{
		FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Output.pbData), static_cast<int32>(Output.cbData));
		OutSecret = FString(Text.Length(), Text.Get());
	}
	FMemory::Memzero(Output.pbData, Output.cbData);
	LocalFree(Output.pbData);
	return bValidSize && !OutSecret.IsEmpty();
#else
	return false;
#endif
}

namespace
{
	FString GuestEntropy(const FString& ServerScope, const FString& Realm)
	{
		return TEXT("NeoMSGO.Guest.v1|") + ServerScope + TEXT("|") + Realm;
	}

	bool ReadGuestEnvelope(const FString& Path, const FString& ServerScope,
		FString& OutRealm, FString& OutName, FString& OutProtected)
	{
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size <= 0 || Size > 65536) return false;
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path)) return false;
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Object) || !Object.IsValid()) return false;
		double Version = 0;
		FString StoredScope;
		return Object->TryGetNumberField(TEXT("version"), Version) && Version == 1
			&& Object->TryGetStringField(TEXT("server_scope"), StoredScope) && StoredScope == ServerScope
			&& Object->TryGetStringField(TEXT("realm_id"), OutRealm) && !OutRealm.IsEmpty() && OutRealm.Len() <= 256
			&& Object->TryGetStringField(TEXT("display_name"), OutName) && OutName.Len() <= 1024
			&& Object->TryGetStringField(TEXT("protected_resume_token"), OutProtected)
			&& OutProtected.StartsWith(TEXT("dpapi1:")) && OutProtected.Len() <= 32768;
	}

	bool WriteGuestAtomically(const FString& Path, const FString& Contents)
	{
#if PLATFORM_WINDOWS
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
		const FString TempPath = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
		FTCHARToUTF8 Bytes(*Contents);
		HANDLE File = CreateFileW(*TempPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (File == INVALID_HANDLE_VALUE) return false;
		DWORD Written = 0;
		const bool bWritten = WriteFile(File, Bytes.Get(), static_cast<DWORD>(Bytes.Length()), &Written, nullptr)
			&& Written == static_cast<DWORD>(Bytes.Length()) && FlushFileBuffers(File);
		CloseHandle(File);
		// Same-directory replacement is atomic; never delete the previous credential first.
		const bool bReplaced = bWritten && MoveFileExW(*TempPath, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		if (!bReplaced) DeleteFileW(*TempPath);
		return bReplaced;
#else
		return false;
#endif
	}
}

FString MSCredentialStore::GetGuestFilePath(const FString& Directory, const FString& ServerScope)
{
	FTCHARToUTF8 ScopeUtf8(*ServerScope);
	uint8 Hash[20];
	FSHA1::HashBuffer(ScopeUtf8.Get(), ScopeUtf8.Length(), Hash);
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("guest-") + BytesToHex(Hash, 20) + TEXT(".json")));
}

bool MSCredentialStore::SaveGuest(const FString& Directory, const FString& ServerScope, const FString& Realm,
	const FString& DisplayName, const FString& Secret)
{
	if (Directory.IsEmpty() || ServerScope.IsEmpty() || ServerScope.Len() > 2048 || Realm.IsEmpty() || Realm.Len() > 256 || DisplayName.Len() > 1024) return false;
	FString Protected;
	if (!Protect(Secret, GuestEntropy(ServerScope, Realm), Protected)) return false;
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("version"), 1);
	Object->SetStringField(TEXT("server_scope"), ServerScope);
	Object->SetStringField(TEXT("realm_id"), Realm);
	Object->SetStringField(TEXT("display_name"), DisplayName);
	Object->SetStringField(TEXT("protected_resume_token"), Protected);
	FString Contents;
	if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Contents))) return false;
	const FString Path = GetGuestFilePath(Directory, ServerScope);
	if (!WriteGuestAtomically(Path, Contents)) return false;
	// A fresh physical read/decrypt verifies persistence, independent of GConfig.
	FString ReadRealm, ReadName, ReadSecret;
	return LoadGuest(Directory, ServerScope, ReadRealm, ReadName, ReadSecret) == ELoadResult::Success
		&& ReadRealm == Realm && ReadName == DisplayName && ReadSecret == Secret;
}

MSCredentialStore::ELoadResult MSCredentialStore::LoadGuest(const FString& Directory, const FString& ServerScope,
	FString& OutRealm, FString& OutDisplayName, FString& OutSecret)
{
	OutRealm.Reset(); OutDisplayName.Reset(); OutSecret.Reset();
	const FString Path = GetGuestFilePath(Directory, ServerScope);
	if (!IFileManager::Get().FileExists(*Path)) return ELoadResult::NotFound;
	FString Realm, Name, Protected, Secret;
	if (!ReadGuestEnvelope(Path, ServerScope, Realm, Name, Protected)
		|| !Unprotect(Protected, GuestEntropy(ServerScope, Realm), Secret)) return ELoadResult::Failed;
	OutRealm = MoveTemp(Realm);
	OutDisplayName = MoveTemp(Name);
	OutSecret = MoveTemp(Secret);
	return ELoadResult::Success;
}

bool MSCredentialStore::RemoveGuest(const FString& Directory, const FString& ServerScope, const FString& Realm)
{
	const FString Path = GetGuestFilePath(Directory, ServerScope);
	if (!IFileManager::Get().FileExists(*Path)) return true;
	FString StoredRealm, Name, Protected;
	if (!ReadGuestEnvelope(Path, ServerScope, StoredRealm, Name, Protected) || StoredRealm != Realm) return false;
	return IFileManager::Get().Delete(*Path, true, false, true);
}
