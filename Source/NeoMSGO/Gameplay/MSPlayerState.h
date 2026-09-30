// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "Online/MSOnlineTypes.h"
#include "MSPlayerState.generated.h"

/** Replicated player identity and faction shared by lobby and battle worlds. */
UCLASS()
class NEOMSGO_API AMSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AMSPlayerState();

	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-side identity assignment after the owning client submits its authenticated name. */
	void SetLoginUsername(const FString& InUsername);
	void SetVerifiedOnlineIdentity(const FMSOnlineIdentity& Identity);

	/** Trims and length-clamps a submitted name; returns empty when nothing usable remains. */
	static FString SanitizeLoginUsername(const FString& InUsername);

	/**
	 * #214: guest numbering is picked process-locally, so two windows on the same PC both request
	 * "ゲスト1". The server owns the final answer: a desired name already held by another connected
	 * player is re-numbered (guests take the lowest free number, others get a "(N)" suffix).
	 */
	static FString ResolveUniqueLoginUsername(const FString& DesiredUsername,
		const TArray<FString>& TakenUsernames);

	/** Prefix used by MSAuthSubsystem::LoginAsGuest(); shared so re-numbering stays consistent. */
	static const TCHAR* GetGuestNamePrefix();

	UFUNCTION(BlueprintPure, Category="MS|Multiplayer")
	const FString& GetLoginUsername() const { return LoginUsername; }

	UFUNCTION(BlueprintPure, Category="MS|Faction")
	FGenericTeamId GetSelectedFaction() const { return SelectedFaction; }

	/** Server-authoritative faction setter; validation/room capacity is added by the lobby milestone. */
	void SetSelectedFaction(FGenericTeamId InFaction);

protected:
	UPROPERTY(ReplicatedUsing=OnRep_LoginUsername, BlueprintReadOnly, Category="MS|Multiplayer")
	FString LoginUsername;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="MS|Faction")
	FGenericTeamId SelectedFaction;

	UFUNCTION()
	void OnRep_LoginUsername();
};
