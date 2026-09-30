// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Semantic voice channels. Add future channels (for example Commander) without changing callers. */
enum class EMSVoiceCategory : uint8
{
	Preset,
	Navigator,
	Count,
};

namespace MSVoiceSettings
{
	/** Missing settings default to enabled. */
	NEOMSGO_API bool IsEnabled(EMSVoiceCategory Category);

	/** Applies immediately and optionally persists to GameUserSettings.ini. */
	NEOMSGO_API void SetEnabled(EMSVoiceCategory Category, bool bEnabled, bool bSave = true);

	/** Stable GConfig key used by settings UI and automation checks. */
	NEOMSGO_API const TCHAR* GetConfigKey(EMSVoiceCategory Category);
}
