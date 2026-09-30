// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Online/MSOnlineTypes.h"
#include "Online/MSRoomSubsystem.h"
#include "Voice/MSVoiceTypes.h"
#include "MSGameInstance.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSLoginStateChangedDelegate, bool, bIsLoggedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSAccountChangedDelegate, FString, AccountId);

/** Process-lifetime pilot and squad data which must survive map reloads. */
UCLASS(BlueprintType)
class NEOMSGO_API UMSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	//~ Begin UGameInstance Interface
	/** 保存済みマスター音量を起動直後に復元する(設定画面を開くまで100%で鳴る問題の対策)。 */
	virtual void Init() override;
	virtual void OnStart() override;
	//~ End UGameInstance Interface

	/** Process-only authenticated identity. Never written to SaveGame. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="MS|Authentication")
	FString LoggedInUsername;

	UPROPERTY(Transient, BlueprintReadOnly, Category="MS|Authentication")
	FString AccountId;

	UPROPERTY(Transient, BlueprintReadOnly, Category="MS|Authentication")
	FString RealmId;

	UPROPERTY(Transient, BlueprintReadOnly, Category="MS|Authentication")
	FString DisplayName;

	UPROPERTY(Transient, BlueprintReadOnly, Category="MS|Authentication")
	bool bIsGuest = false;

	UPROPERTY(BlueprintAssignable, Category="MS|Authentication")
	FMSLoginStateChangedDelegate OnLoginStateChanged;

	UPROPERTY(BlueprintAssignable, Category="MS|Authentication")
	FMSAccountChangedDelegate OnAccountChanged;

	UFUNCTION(BlueprintPure, Category="MS|Authentication")
	bool IsLoggedIn() const { return !LoggedInUsername.IsEmpty(); }

	/** Lobby/UI gate. Login UI should call the auth subsystem before entering the lobby. */
	UFUNCTION(BlueprintPure, Category="MS|Authentication")
	bool CanEnterLobby() const { return IsLoggedIn(); }

	UFUNCTION(BlueprintCallable, Category="MS|Authentication")
	void Logout();

	/** Called only after an authority-side credential check succeeds. */
	void SetAuthenticatedUser(const FString& Username);

	/** Updates identity after a successful online response; credentials remain in the account subsystem. */
	void SetAuthenticatedAccount(const FMSOnlineIdentity& Identity, const FString& Username);
	void ClearAuthenticatedAccount();

	/** Map selected in the lobby; persists across map travel. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Map Selection")
	FName SelectedMapId = TEXT("UnrealizerColony");

	/** True while map travel must continue directly into the sortie flow. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Map Selection")
	bool bPendingSortie = false;

	/** Battle rule to restore after pending sortie map travel. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Map Selection", meta=(ClampMin="0"))
	int32 PendingRuleIndex = 0;

	/** Room settings retained across listen-server map travel. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Room")
	FMSRoomSettings PendingRoomSettings;

	UPROPERTY(BlueprintReadWrite, Category="MS|Room")
	bool bPendingRoomHost = false;

	UPROPERTY(BlueprintReadWrite, Category="MS|Room")
	bool bPendingRoomJoin = false;

	/** Preset voice selected for the local pilot; persists across map travel. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Pilot|Voice")
	EMSVoiceType SelectedVoiceType = EMSVoiceType::Hotblood;

	UPROPERTY(BlueprintReadWrite, Category="MS|Pilot")
	int32 KillCount = 0;

	UPROPERTY(BlueprintReadWrite, Category="MS|Pilot")
	int32 DeathCount = 0;

	UPROPERTY(BlueprintReadWrite, Category="MS|Squad")
	FString SquadName = TEXT("第13独立小隊");

	UPROPERTY(BlueprintReadWrite, Category="MS|Squad", meta=(ClampMin="0", ClampMax="5"))
	int32 SquadEmblemIndex = 0;

	/** Comment shown atop the lobby's "current party" 6-slot panel. */
	UPROPERTY(BlueprintReadWrite, Category="MS|Squad")
	FString PartyComment = TEXT("小隊メンバー募集中でち！");

	UFUNCTION(BlueprintPure, Category="MS|Pilot")
	static FText GetRankName(int32 InKillCount);

	UFUNCTION(BlueprintCallable, Category="MS|Pilot")
	void RegisterKill() { ++KillCount; }

	UFUNCTION(BlueprintCallable, Category="MS|Pilot")
	void RegisterDeath() { ++DeathCount; }
};
