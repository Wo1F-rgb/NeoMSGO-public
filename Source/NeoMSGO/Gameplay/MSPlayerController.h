// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Online/MSOnlineTypes.h"
#include "GenericTeamAgentInterface.h"
#include "MSPlayerController.generated.h"

class AMSPlayerCharacter;
class IInputProcessor;
class UMSSettingsWidget;
class UMSUILockSubsystem;
class AMSUnitBase;

/** Opaque server-observed enemy contact; no actor NetGUID is sent to the client. */
USTRUCT()
struct FMSMapContact
{
	GENERATED_BODY()
	UPROPERTY() uint32 ContactId = 0;
	UPROPERTY() FVector LastKnownLocation = FVector::ZeroVector;
	UPROPERTY() bool bCurrentlyDetected = false;
};

/** Live friendly position, independent of client-side actor relevancy. Tier is 255 for a player. */
USTRUCT()
struct FMSMapFriendlyContact
{
	GENERATED_BODY()
	UPROPERTY() uint32 UnitId = 0;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() uint8 BotTier = 255;
};

/** Friendly radar coverage shared with the owning team's map; no actor reference. */
USTRUCT()
struct FMSMapRadarRange
{
	GENERATED_BODY()
	UPROPERTY() uint32 RadarId = 0;
	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() float RadiusCm = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnlineDeploymentResult, bool, bSuccess, FText, Message);

/**
 * Local-player controller which owns input that must remain available without a possessed Pawn.
 * In particular, Esc is captured before Slate routes it to a focused widget.
 */
UCLASS()
class NEOMSGO_API AMSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PawnLeavingGame() override;
	void BeginOnlineAuthentication();
	/** Retries admission presentation after either owner state or Pawn restart arrives. */
	void TryApplyOnlineAdmissionPresentation();
	bool IsOnlineAuthenticated() const { return bOnlineAuthenticated; }
	bool IsOnlineDeployed() const { return bOnlineDeployed; }
	UFUNCTION(BlueprintPure, Category="MS|Online") FGenericTeamId GetOnlineAssignedTeamId() const;
	bool IsAuthenticationPending(const FString& Id) const { return !bOnlineAuthenticated && bAuthenticationValidating && ConnectionId == Id; }
	const FMSOnlineIdentity& GetOnlineIdentity() const { return OnlineIdentity; }
	const FMSOnlineDeckSnapshot& GetOnlineDeck() const { return OnlineDeck; }
	const TArray<FMSMapContact>& GetMapContacts() const { return MapContacts; }
	const TArray<FMSMapFriendlyContact>& GetMapFriendlyContacts() const { return MapFriendlyContacts; }
	const TArray<FMSMapRadarRange>& GetMapRadarRanges() const { return MapRadarRanges; }
	bool AreMapContactsFresh() const;
	static bool IsMapContactSnapshotTimeFresh(double ReceivedAt, double Now);
	/** Called only by the authority sensing subsystem; delivered to this controller's owner. */
	void PublishMapContacts(const TArray<FMSMapContact>& Contacts,
		const TArray<FMSMapFriendlyContact>& Friendlies,
		const TArray<FMSMapRadarRange>& Radars, uint8 ViewerTeamId);
	void CompleteOnlineAuthentication(const FMSOnlineIdentity& Identity, const FMSOnlineDeckSnapshot& Deck);
	void RejectOnlineAuthentication(const FString& Reason);
	/** Publishes the authoritative recall transition while retaining the reconnectable pawn. */
	void MarkOnlineRecallAwaitingDeployment();
	UFUNCTION(BlueprintCallable, Category="MS|Online") bool RequestOnlineDeployment(int32 SlotIndex, FName SpawnPointId);
	UPROPERTY(BlueprintAssignable, Category="MS|Online") FMSOnlineDeploymentResult OnOnlineDeploymentResult;
	/** Single authoritative Esc route: close the top screen, or open Settings when none is open. */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void HandleEscapePressed();

	/** Opens Settings without requiring a possessed Pawn or HUD. */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void OpenSettingsScreen();

	/** Development-only two-client combat staging. Commands are inert in Shipping builds. */
	UFUNCTION(Exec) void MSVerifySetup();
	UFUNCTION(Exec) void MSVerifyFire();
	UFUNCTION(Exec) void MSVerifyPrepareKill();
	UFUNCTION(Exec) void MSVerifyRespawn();
	UFUNCTION(Exec) void MSVerifyCapture();

#if WITH_DEV_AUTOMATION_TESTS
	friend class FMSOnlineRetainedPresentationTest;
	friend class FMSOnlineRecallRedeployTest;
	void SetSettingsWidgetForTesting(UMSSettingsWidget* InWidget) { SettingsWidget = InWidget; }
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
	bool HasEscapeInputProcessorForTesting() const { return EscapeInputProcessor.IsValid(); }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;
	virtual void ReceivedPlayer() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	UFUNCTION(Client, Unreliable) void ClientReceiveMapContacts(const TArray<FMSMapContact>& Contacts,
		const TArray<FMSMapFriendlyContact>& Friendlies,
		const TArray<FMSMapRadarRange>& Radars, uint8 ViewerTeamId);
	UPROPERTY(Transient) TArray<FMSMapContact> MapContacts;
	UPROPERTY(Transient) TArray<FMSMapFriendlyContact> MapFriendlyContacts;
	UPROPERTY(Transient) TArray<FMSMapRadarRange> MapRadarRanges;
	double MapContactsReceivedAt = -1.0;
	TWeakObjectPtr<UWorld> MapContactsWorld;
	uint8 MapContactsViewerTeamId = 255;
	uint8 ResolveMapContactViewerTeamId() const;
	UFUNCTION(Client, Reliable) void ClientRequestAuthentication(const FString& ServerId);
	UFUNCTION(Server, Reliable) void ServerAuthenticate(const FString& Ticket);
	UFUNCTION(Server, Reliable) void ServerRequestOnlineDeployment(int32 SlotIndex, FName SpawnPointId);
	UFUNCTION(Client, Reliable) void ClientOnlineDeploymentResult(bool bSuccess, const FText& Message);
	UFUNCTION() void HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation);
	UFUNCTION() void HandleAuthenticationTicketReady(FString ServerId, FString GameAddress, int64 Generation);
	void RequestAuthenticationWhenServerReady();
	FString AwaitingAuthenticationServer;
	FTimerHandle AuthenticationReadyTimer;
	void ExpireOnlineAuthentication();
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) bool bOnlineAuthenticated = false;
	UPROPERTY(Replicated) bool bOnlineDeployed = false;
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) FMSOnlineIdentity OnlineIdentity;
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) FMSOnlineDeckSnapshot OnlineDeck;
	UFUNCTION() void OnRep_OnlineAdmission();
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) TObjectPtr<AMSPlayerCharacter> OnlineAdmissionPawn;
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) bool bOnlineRestoredDeployment = false;
	UPROPERTY(ReplicatedUsing=OnRep_OnlineAdmission) uint8 OnlineAssignedTeamId = 255;
	bool bOnlineAdmissionPresented = false;
	bool bOnlineDeckNotified = false;
	FString ConnectionId;
	bool bAuthenticationValidating = false;
	bool bTicketSubmitted = false;
	bool bDeploymentPending = false;
	int64 ClientAuthGeneration = -1;
	FTimerHandle AuthenticationTimer;
	void RegisterEscapeInputProcessor();
	void UnregisterEscapeInputProcessor();
	UMSUILockSubsystem* GetUILockSubsystem() const;
	void SubmitAuthenticatedUsername();

	UFUNCTION(Server, Reliable)
	void ServerSetLoginUsername(const FString& Username);

	UFUNCTION(Server, Reliable)
	void ServerRunMultiplayerVerification(FName Action);

	/** #214: the server may re-number a duplicated guest name; mirror its answer back locally. */
	UFUNCTION(Client, Reliable)
	void ClientReportAssignedUsername(const FString& Username);

	/** Publishes the identity as soon as a late (e.g. guest UI) login completes. */
	UFUNCTION()
	void HandleLoginStateChanged(bool bIsLoggedIn);

	TSharedPtr<IInputProcessor> EscapeInputProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMSSettingsWidget> SettingsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;

	double MultiplayerVerifyStartTime = -1.0;
	double MultiplayerVerifyLastLoopFire = -1.0;
	int32 MultiplayerVerifyStage = 0;
};
