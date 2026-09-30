// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSGameInstance.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

#define LOCTEXT_NAMESPACE "MSGameInstance"

void UMSGameInstance::Init()
{
	Super::Init();

#if !UE_BUILD_SHIPPING
	// Deterministic end-to-end sortie smoke path used by local map verification.
	// It exercises the real briefing, suit selection, deployment map and Respawn
	// flow; only the otherwise manual title/login/lobby setup is bypassed.
	FString SortieSmokeMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("MSSortieSmokeMap="), SortieSmokeMap))
	{
		SetAuthenticatedUser(TEXT("SortieSmoke"));
		SelectedMapId = FName(*SortieSmokeMap);
		bPendingSortie = true;
		PendingRuleIndex = 0;
		UE_LOG(LogTemp, Display,
			TEXT("[SORTIE:SMOKE] Prepared authenticated pending sortie for map=%s"),
			*SortieSmokeMap);
	}
#endif
}

void UMSGameInstance::OnStart()
{
	Super::OnStart();

	// verifier指摘対応: 保存済みマスター音量は従来「設定画面を初めて開いた時」に
	// しか適用されず、起動直後〜設定画面初回オープンまで100%で鳴っていた。
	// UMSSettingsWidget::ApplyMasterVolumeと同じ経路(GameUserSettings.ini +
	// SetTransientPrimaryVolume)で起動時に復元する。キー名は設定画面側と共有。
	float MasterVolume = 1.0f;
	GConfig->GetFloat(TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("MasterVolume"),
		MasterVolume, GGameUserSettingsIni);
	MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	if (GEngine)
	{
		FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
		if (AudioDevice.IsValid())
		{
			AudioDevice->SetTransientPrimaryVolume(MasterVolume);
		}
	}
}

void UMSGameInstance::SetAuthenticatedUser(const FString& Username)
{
	FString CleanUsername = Username;
	CleanUsername.TrimStartAndEndInline();
	FMSOnlineIdentity LocalIdentity;
	LocalIdentity.AccountId = CleanUsername;
	LocalIdentity.RealmId = TEXT("offline");
	LocalIdentity.DisplayName = CleanUsername;
	SetAuthenticatedAccount(LocalIdentity, CleanUsername);
}

void UMSGameInstance::Logout()
{
	if (UMSOnlineAccountSubsystem* Online = GetSubsystem<UMSOnlineAccountSubsystem>(); Online && Online->IsOnlineMode())
	{
		Online->Logout();
		return;
	}
	ClearAuthenticatedAccount();
}

void UMSGameInstance::SetAuthenticatedAccount(const FMSOnlineIdentity& Identity, const FString& Username)
{
	FString CleanUsername = Username;
	CleanUsername.TrimStartAndEndInline();
	// Guest responses may omit the optional username field. Keep the lobby gate
	// and existing display-name consumers usable from the server identity alone.
	if (CleanUsername.IsEmpty()) CleanUsername = Identity.DisplayName;
	if (CleanUsername.IsEmpty()) CleanUsername = Identity.AccountId;
	const FString PreviousAccountKey = RealmId + TEXT("/") + AccountId;
	const bool bWasLoggedIn = IsLoggedIn();
	LoggedInUsername = MoveTemp(CleanUsername);
	AccountId = Identity.AccountId;
	RealmId = Identity.RealmId;
	DisplayName = Identity.DisplayName.IsEmpty() ? LoggedInUsername : Identity.DisplayName;
	bIsGuest = Identity.bIsGuest;
	const FString NewAccountKey = RealmId + TEXT("/") + AccountId;
	if (PreviousAccountKey != NewAccountKey)
	{
		OnAccountChanged.Broadcast(AccountId);
	}
	if (bWasLoggedIn != IsLoggedIn()) OnLoginStateChanged.Broadcast(IsLoggedIn());
}

void UMSGameInstance::ClearAuthenticatedAccount()
{
	const bool bWasLoggedIn = IsLoggedIn();
	const bool bHadAccount = !AccountId.IsEmpty() || !RealmId.IsEmpty();
	LoggedInUsername.Reset();
	AccountId.Reset();
	RealmId.Reset();
	DisplayName.Reset();
	bIsGuest = false;
	if (bHadAccount) OnAccountChanged.Broadcast(FString());
	if (bWasLoggedIn) OnLoginStateChanged.Broadcast(false);
}

FText UMSGameInstance::GetRankName(const int32 InKillCount)
{
	if (InKillCount >= 40) return LOCTEXT("SecondLieutenant", "少尉");
	if (InKillCount >= 30) return LOCTEXT("WarrantOfficer", "准尉");
	if (InKillCount >= 20) return LOCTEXT("MasterSergeant", "曹長");
	if (InKillCount >= 15) return LOCTEXT("Sergeant", "軍曹");
	if (InKillCount >= 10) return LOCTEXT("Corporal", "伍長");
	if (InKillCount >= 6) return LOCTEXT("SeniorPrivate", "上等兵");
	if (InKillCount >= 3) return LOCTEXT("PrivateFirstClass", "一等兵");
	return LOCTEXT("PrivateSecondClass", "二等兵");
}

#undef LOCTEXT_NAMESPACE
