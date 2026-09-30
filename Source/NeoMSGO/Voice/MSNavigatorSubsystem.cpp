// Copyright NeoMSGO Project. All Rights Reserved.
#include "Voice/MSNavigatorSubsystem.h"

#include "Audio/MSAudioPlayback.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"
#include "Voice/MSVoiceSettings.h"

namespace
{
	const TCHAR* NavigatorConfigSection = TEXT("/Script/NeoMSGO.MSNavigatorSubsystem");
	const TCHAR* NavigatorConfigKey = TEXT("SelectedNavigator");
}

void UMSNavigatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// GetSelectedNavigatorの初回呼び出しでGConfigから読み込むため、ここでは何もしない。
}

EMSNavigator UMSNavigatorSubsystem::GetSelectedNavigator() const
{
	if (!bSelectedNavigatorLoaded)
	{
		int32 Value = static_cast<int32>(EMSNavigator::NoelAnderson);
		GConfig->GetInt(NavigatorConfigSection, NavigatorConfigKey, Value, GGameUserSettingsIni);
		UMSNavigatorSubsystem* MutableThis = const_cast<UMSNavigatorSubsystem*>(this);
		MutableThis->SelectedNavigator = Value == static_cast<int32>(EMSNavigator::YuukiNakasato)
			? EMSNavigator::YuukiNakasato : EMSNavigator::NoelAnderson;
		MutableThis->bSelectedNavigatorLoaded = true;
	}
	return SelectedNavigator;
}

void UMSNavigatorSubsystem::SetSelectedNavigator(const EMSNavigator Navigator, const bool bSave)
{
	SelectedNavigator = Navigator;
	bSelectedNavigatorLoaded = true;
	if (bSave)
	{
		GConfig->SetInt(NavigatorConfigSection, NavigatorConfigKey, static_cast<int32>(Navigator), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSNavigatorSubsystem::PlayLine(const EMSNavigatorTrigger Trigger)
{
	if (!MSVoiceSettings::IsEnabled(EMSVoiceCategory::Navigator))
	{
		return;
	}

	// FirstSortieは1戦闘につき1回のみ。既に鳴っていればここで抑制する。
	if (Trigger == EMSNavigatorTrigger::FirstSortie)
	{
		if (bFirstSortiePlayedThisSession)
		{
			return;
		}
		bFirstSortiePlayedThisSession = true;
	}

	const TCHAR* NavigatorId = GetNavigatorId(GetSelectedNavigator());
	const TCHAR* TriggerId = GetTriggerId(Trigger);
	const FString AssetName = FString::Printf(TEXT("%s_%s"), NavigatorId, TriggerId);
	const FString ObjectPath = FString::Printf(
		TEXT("/Game/Audio/Imported/Navigator/%s.%s"), *AssetName, *AssetName);

	// 自動テスト環境等でアセット未インポートの場合、LoadObjectはnullptrを返す。
	// PlayMetaSound2Dはnullサウンドを渡してもクラッシュしないため、そのままno-opとして扱う。
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *ObjectPath);
	if (!Sound)
	{
		return;
	}
	MSAudio::PlayMetaSound2D(this, Sound, 1.0f, 0.6f, 3.0f);
}

FText UMSNavigatorSubsystem::GetNavigatorDisplayName(const EMSNavigator Navigator)
{
	switch (Navigator)
	{
	case EMSNavigator::YuukiNakasato: return FText::FromString(TEXT("ユウキ・ナカサト"));
	case EMSNavigator::NoelAnderson:
	default: return FText::FromString(TEXT("ノエル・アンダーソン"));
	}
}

const TCHAR* UMSNavigatorSubsystem::GetNavigatorId(const EMSNavigator Navigator)
{
	switch (Navigator)
	{
	case EMSNavigator::YuukiNakasato: return TEXT("YuukiNakasato");
	case EMSNavigator::NoelAnderson:
	default: return TEXT("NoelAnderson");
	}
}

const TCHAR* UMSNavigatorSubsystem::GetTriggerId(const EMSNavigatorTrigger Trigger)
{
	switch (Trigger)
	{
	case EMSNavigatorTrigger::Login: return TEXT("Login");
	case EMSNavigatorTrigger::PreDeployment: return TEXT("PreDeployment");
	case EMSNavigatorTrigger::FirstSortie: return TEXT("FirstSortie");
	case EMSNavigatorTrigger::VictoryReturn: return TEXT("VictoryReturn");
	case EMSNavigatorTrigger::DefeatReturn: return TEXT("DefeatReturn");
	default: return TEXT("");
	}
}
