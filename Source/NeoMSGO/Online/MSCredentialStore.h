#pragma once

#include "CoreMinimal.h"

/** Current-user Windows DPAPI only. No plaintext fallback on unsupported platforms or failure. */
namespace MSCredentialStore
{
	NEOMSGO_API bool Protect(const FString& Secret, const FString& Scope, FString& OutProtected);
	NEOMSGO_API bool Unprotect(const FString& Protected, const FString& Scope, FString& OutSecret);

	enum class ELoadResult : uint8 { Success, NotFound, Failed };
	/** One atomically replaced envelope per server scope; its realm is the latest guest realm. */
	NEOMSGO_API bool SaveGuest(const FString& Directory, const FString& ServerScope, const FString& Realm,
		const FString& DisplayName, const FString& Secret);
	NEOMSGO_API ELoadResult LoadGuest(const FString& Directory, const FString& ServerScope,
		FString& OutRealm, FString& OutDisplayName, FString& OutSecret);
	/** Removes only the current matching realm; cannot remove another server/realm's credential. */
	NEOMSGO_API bool RemoveGuest(const FString& Directory, const FString& ServerScope, const FString& Realm);
	NEOMSGO_API FString GetGuestFilePath(const FString& Directory, const FString& ServerScope);
}
