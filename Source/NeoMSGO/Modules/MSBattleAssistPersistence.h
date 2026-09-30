#pragma once
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace MSBattleAssistPersistence
{
	/** Write a complete sibling first, then replace in one OS operation. */
	inline bool SaveBytes(const FString& Slot, const TArray<uint8>& Bytes)
	{
		// Preserve SaveGameToSlot's empty-name rejection instead of accidentally writing ".sav".
		if (Slot.TrimStartAndEnd().IsEmpty() || Slot == TEXT(".") || Slot == TEXT("..")
			|| FPaths::GetCleanFilename(Slot) != Slot || Slot.Contains(TEXT(":"))) return false;
		const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".sav")));
		const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".pending");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary)) return false;
#if PLATFORM_WINDOWS
		const bool bSaved = !!::MoveFileExW(*Temporary, *Filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
		// Other platforms need their own atomic replacement before enabling persistent edits.
		const bool bSaved = false;
#endif
		if (!bSaved) IFileManager::Get().Delete(*Temporary);
		return bSaved;
	}
}
