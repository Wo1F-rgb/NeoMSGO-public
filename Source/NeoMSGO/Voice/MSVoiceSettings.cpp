// Copyright NeoMSGO Project. All Rights Reserved.

#include "Voice/MSVoiceSettings.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* VoiceSettingsConfigSection = TEXT("/Script/NeoMSGO.MSVoiceSettings");

	TArray<int8>& GetEnabledStates()
	{
		static TArray<int8> States = []
		{
			TArray<int8> Result;
			Result.Init(INDEX_NONE, static_cast<int32>(EMSVoiceCategory::Count));
			return Result;
		}();
		return States;
	}

	int32 CategoryIndex(const EMSVoiceCategory Category)
	{
		const int32 Index = static_cast<int32>(Category);
		return Index >= 0 && Index < static_cast<int32>(EMSVoiceCategory::Count) ? Index : 0;
	}
}

const TCHAR* MSVoiceSettings::GetConfigKey(const EMSVoiceCategory Category)
{
	switch (Category)
	{
	case EMSVoiceCategory::Navigator: return TEXT("NavigatorVoiceEnabled");
	case EMSVoiceCategory::Preset:
	default: return TEXT("PresetVoiceEnabled");
	}
}

bool MSVoiceSettings::IsEnabled(const EMSVoiceCategory Category)
{
	int8& CachedState = GetEnabledStates()[CategoryIndex(Category)];
	if (CachedState == INDEX_NONE)
	{
		bool bEnabled = true;
		GConfig->GetBool(VoiceSettingsConfigSection, GetConfigKey(Category), bEnabled, GGameUserSettingsIni);
		CachedState = bEnabled ? 1 : 0;
	}
	return CachedState != 0;
}

void MSVoiceSettings::SetEnabled(const EMSVoiceCategory Category, const bool bEnabled, const bool bSave)
{
	GetEnabledStates()[CategoryIndex(Category)] = bEnabled ? 1 : 0;
	if (bSave)
	{
		GConfig->SetBool(VoiceSettingsConfigSection, GetConfigKey(Category), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
