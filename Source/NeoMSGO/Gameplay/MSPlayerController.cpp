// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSPlayerController.h"
#include "Online/MSRoomSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSDeckSubsystem.h"
#include "AI/MSBotSpawnPoint.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Core/MSGameInstance.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSPlayerState.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Player/MSPlayerCharacter.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSTeamStatics.h"
#include "Data/MSWeaponData.h"
#include "Data/MSMobileSuitData.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/MSWeaponComponent.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSUILockSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMSMultiplayer, Log, All);

bool AMSPlayerController::AreMapContactsFresh() const
{
	const UWorld* World = GetWorld();
	return World && MapContactsWorld.Get() == World &&
		MapContactsViewerTeamId != 255 &&
		MapContactsViewerTeamId == ResolveMapContactViewerTeamId() &&
		IsMapContactSnapshotTimeFresh(MapContactsReceivedAt, World->GetTimeSeconds());
}

bool AMSPlayerController::IsMapContactSnapshotTimeFresh(const double ReceivedAt, const double Now)
{
	return ReceivedAt >= 0.0 && Now >= ReceivedAt && Now - ReceivedAt <= 2.0;
}

uint8 AMSPlayerController::ResolveMapContactViewerTeamId() const
{
	FGenericTeamId Team = UMSTeamStatics::GetTeamId(GetPawn());
	if (Team == FGenericTeamId::NoTeam) Team = GetOnlineAssignedTeamId();
	return Team.GetId();
}

void AMSPlayerController::PublishMapContacts(const TArray<FMSMapContact>& Contacts,
	const TArray<FMSMapFriendlyContact>& Friendlies,
	const TArray<FMSMapRadarRange>& Radars, const uint8 ViewerTeamId)
{
	if (HasAuthority()) ClientReceiveMapContacts(Contacts, Friendlies, Radars, ViewerTeamId);
}

void AMSPlayerController::ClientReceiveMapContacts_Implementation(const TArray<FMSMapContact>& Contacts,
	const TArray<FMSMapFriendlyContact>& Friendlies,
	const TArray<FMSMapRadarRange>& Radars, const uint8 ViewerTeamId)
{
	MapContacts = Contacts;
	MapFriendlyContacts = Friendlies;
	MapRadarRanges = Radars;
	MapContactsReceivedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	MapContactsWorld = GetWorld();
	MapContactsViewerTeamId = ViewerTeamId;
}

/** Slate-level Esc capture. Returning handled also prevents the old Pawn input path from firing. */
class FMSEscapeInputProcessor final : public IInputProcessor
{
public:
	explicit FMSEscapeInputProcessor(AMSPlayerController* InController)
		: Controller(InController)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		(void)DeltaTime;
		(void)SlateApp;
		(void)Cursor;
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		(void)SlateApp;
		if (InKeyEvent.GetKey() != EKeys::Escape)
		{
			return false;
		}

		// Settings can also be created by the lobby, so do not rely on the controller's cache.
		// The focused selector must receive Escape key-up to cancel its current selection.
		if (AMSPlayerController* PlayerController = Controller.Get())
		{
			TArray<UUserWidget*> SettingsScreens;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayerController, SettingsScreens,
				UMSSettingsWidget::StaticClass(), false);
			for (UUserWidget* Screen : SettingsScreens)
			{
				const UMSSettingsWidget* Settings = Cast<UMSSettingsWidget>(Screen);
				if (Settings && Settings->GetOwningPlayer() == PlayerController &&
					Settings->IsOpen() && Settings->IsSelectingKey())
				{
					return false;
				}
			}
		}

		// Consume repeats so holding Esc cannot rapidly open and close the same screen.
		if (!InKeyEvent.IsRepeat())
		{
			if (AMSPlayerController* PlayerController = Controller.Get())
			{
				PlayerController->HandleEscapePressed();
			}
		}
		return true;
	}

private:
	TWeakObjectPtr<AMSPlayerController> Controller;
};

void AMSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	RegisterEscapeInputProcessor();
	// #213: a direct-connect client reaches the login UI *after* BeginPlay, so the one-shot publish
	// below would miss a guest login. Also publish whenever the local identity is established.
	if (IsLocalController())
	{
		if (UMSGameInstance* Instance = GetGameInstance<UMSGameInstance>())
		{
			Instance->OnLoginStateChanged.AddDynamic(this, &AMSPlayerController::HandleLoginStateChanged);
		}
	}
	SubmitAuthenticatedUsername();
}

void AMSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(AuthenticationReadyTimer);
	if (GetGameInstance()) if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>()) Account->OnGameTicketReady.RemoveDynamic(this, &AMSPlayerController::HandleAuthenticationTicketReady);
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(AuthenticationTimer); if (HasAuthority()) if (auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>()) Server->Release(this); }
	if (GetGameInstance()) if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>()) Account->OnAccountChanged.RemoveDynamic(this, &AMSPlayerController::HandleOnlineAccountChanged);
	ConnectionId.Reset(); bAuthenticationValidating = false;
	if (UMSGameInstance* Instance = GetGameInstance<UMSGameInstance>())
	{
		Instance->OnLoginStateChanged.RemoveDynamic(this, &AMSPlayerController::HandleLoginStateChanged);
	}
	UnregisterEscapeInputProcessor();
	Super::EndPlay(EndPlayReason);
}

void AMSPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	SubmitAuthenticatedUsername();
	TryApplyOnlineAdmissionPresentation();
}

void AMSPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	// Covers controllers that become local after BeginPlay (travel and delayed net setup).
	RegisterEscapeInputProcessor();
}

void AMSPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (MSOnline::IsEnabled()) { TryApplyOnlineAdmissionPresentation(); return; }
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || !IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("MSAutoVerify")))
	{
		return;
	}
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!State || State->PlayerArray.Num() < 2)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (MultiplayerVerifyStartTime < 0.0)
	{
		MultiplayerVerifyStartTime = Now;
		ServerRunMultiplayerVerification_Implementation(TEXT("Setup"));
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=setup elapsed=0"));
	}
	const double Elapsed = Now - MultiplayerVerifyStartTime;
	if (Elapsed >= 4.0 && Elapsed < 14.0 && Now - MultiplayerVerifyLastLoopFire >= 0.65)
	{
		// Keep the target alive while a stream of real replicated projectiles is available to capture.
		ServerRunMultiplayerVerification_Implementation(TEXT("Setup"));
		ServerRunMultiplayerVerification_Implementation(TEXT("Fire"));
		MultiplayerVerifyLastLoopFire = Now;
		if (MultiplayerVerifyStage < 1)
		{
			MultiplayerVerifyStage = 1;
			UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=projectiles elapsed=%.1f"), Elapsed);
		}
	}
	else if (Elapsed >= 15.0 && MultiplayerVerifyStage < 2)
	{
		ServerRunMultiplayerVerification_Implementation(TEXT("Setup"));
		ServerRunMultiplayerVerification_Implementation(TEXT("Fire"));
		MultiplayerVerifyStage = 2;
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=damage elapsed=%.1f"), Elapsed);
	}
	else if (Elapsed >= 23.0 && MultiplayerVerifyStage < 3)
	{
		ServerRunMultiplayerVerification_Implementation(TEXT("PrepareKill"));
		ServerRunMultiplayerVerification_Implementation(TEXT("Fire"));
		MultiplayerVerifyStage = 3;
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=kill elapsed=%.1f"), Elapsed);
	}
	else if (Elapsed >= 32.0 && MultiplayerVerifyStage < 4)
	{
		ServerRunMultiplayerVerification_Implementation(TEXT("Respawn"));
		MultiplayerVerifyStage = 4;
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=respawn elapsed=%.1f"), Elapsed);
	}
	else if (Elapsed >= 40.0 && MultiplayerVerifyStage < 5)
	{
		ServerRunMultiplayerVerification_Implementation(TEXT("Capture"));
		MultiplayerVerifyStage = 5;
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSAutoVerify stage=capture elapsed=%.1f"), Elapsed);
	}
#endif
}

void AMSPlayerController::RegisterEscapeInputProcessor()
{
	if (!IsLocalController() || EscapeInputProcessor.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	EscapeInputProcessor = MakeShared<FMSEscapeInputProcessor>(this);
	// Index zero puts the diagnostic escape route ahead of focused UMG widgets and PIE shortcuts.
	FSlateApplication::Get().RegisterInputPreProcessor(EscapeInputProcessor, 0);
}

void AMSPlayerController::UnregisterEscapeInputProcessor()
{
	if (EscapeInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(EscapeInputProcessor);
	}
	EscapeInputProcessor.Reset();
}

UMSUILockSubsystem* AMSPlayerController::GetUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem)
	{
		return TestUILockSubsystem;
	}
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void AMSPlayerController::HandleEscapePressed()
{
	if (!IsLocalController())
	{
		return;
	}

	// Preserve the passive combat-map special case from the former Pawn handler.
	if (AMSPlayerCharacter* PlayerCharacter = GetPawn<AMSPlayerCharacter>();
		PlayerCharacter && PlayerCharacter->CloseFullscreenMapIfOpen())
	{
		return;
	}

	if (UMSUILockSubsystem* UILock = GetUILockSubsystem();
		UILock && UILock->RequestCloseTopScreen())
	{
		return;
	}

	// This fallback also makes the toggle deterministic in minimal diagnostic/test worlds.
	if (SettingsWidget && SettingsWidget->IsOpen())
	{
		SettingsWidget->Close();
		return;
	}
	OpenSettingsScreen();
}

void AMSPlayerController::OpenSettingsScreen()
{
	if (!IsLocalController())
	{
		return;
	}
	if (!SettingsWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSSettingsWidget::StaticClass(), false);
		SettingsWidget = Existing.IsEmpty()
			? CreateWidget<UMSSettingsWidget>(this)
			: Cast<UMSSettingsWidget>(Existing[0]);
	}
	if (SettingsWidget)
	{
		SettingsWidget->OpenSettings();
	}
}

void AMSPlayerController::SubmitAuthenticatedUsername()
{
	if (MSOnline::IsEnabled()) return;
	if (!IsLocalController())
	{
		return;
	}
	const UMSGameInstance* Instance = GetGameInstance<UMSGameInstance>();
	if (!Instance || Instance->LoggedInUsername.IsEmpty())
	{
		UE_LOG(LogMSMultiplayer, Warning, TEXT("Local player has no authenticated username to publish"));
		return;
	}
	ServerSetLoginUsername(Instance->LoggedInUsername);
}

void AMSPlayerController::ClientReportAssignedUsername_Implementation(const FString& Username)
{
	if (Username.IsEmpty())
	{
		return;
	}
	UMSGameInstance* Instance = GetGameInstance<UMSGameInstance>();
	if (!Instance || Instance->LoggedInUsername.Equals(Username, ESearchCase::CaseSensitive))
	{
		return;
	}
	UE_LOG(LogMSMultiplayer, Display, TEXT("Server reassigned local identity: %s -> %s"),
		*Instance->LoggedInUsername, *Username);
	Instance->SetAuthenticatedUser(Username);
}

void AMSPlayerController::HandleLoginStateChanged(const bool bIsLoggedIn)
{
	if (bIsLoggedIn)
	{
		SubmitAuthenticatedUsername();
	}
}

void AMSPlayerController::ServerSetLoginUsername_Implementation(const FString& Username)
{
	if (MSOnline::IsEnabled()) return;
	if (AMSPlayerState* MSPlayerState = GetPlayerState<AMSPlayerState>())
	{
		// #214: two windows on one PC both number their guest locally and both ask for "ゲスト1".
		// The server is the only place that sees every connected identity, so it arbitrates here.
		TArray<FString> TakenUsernames;
		if (const AGameStateBase* PresenceState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			for (const APlayerState* Other : PresenceState->PlayerArray)
			{
				if (!IsValid(Other) || Other == MSPlayerState)
				{
					continue;
				}
				const AMSPlayerState* OtherMSPlayerState = Cast<AMSPlayerState>(Other);
				if (OtherMSPlayerState && !OtherMSPlayerState->GetLoginUsername().IsEmpty())
				{
					TakenUsernames.Add(OtherMSPlayerState->GetLoginUsername());
				}
			}
		}

		const FString Resolved = AMSPlayerState::ResolveUniqueLoginUsername(Username, TakenUsernames);
		if (Resolved.IsEmpty())
		{
			return;
		}
		if (!Resolved.Equals(AMSPlayerState::SanitizeLoginUsername(Username), ESearchCase::CaseSensitive))
		{
			UE_LOG(LogMSMultiplayer, Display, TEXT("Duplicate presence name '%s' re-numbered to '%s'"),
				*Username, *Resolved);
		}
		MSPlayerState->SetLoginUsername(Resolved);
		ClientReportAssignedUsername(Resolved);
		UE_LOG(LogMSMultiplayer, Display, TEXT("Presence registered: %s"), *MSPlayerState->GetLoginUsername());

		int32 PlayerPawnCount = 0;
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			if (It->IsPlayerControlled())
			{
				++PlayerPawnCount;
			}
		}
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		UE_LOG(LogMSMultiplayer, Display, TEXT("Presence snapshot: Players=%d PlayerPawns=%d"),
			GameState ? GameState->PlayerArray.Num() : 0, PlayerPawnCount);
	}
}

void AMSPlayerController::MSVerifySetup()
{
#if !UE_BUILD_SHIPPING
	ServerRunMultiplayerVerification(TEXT("Setup"));
#endif
}

void AMSPlayerController::MSVerifyFire()
{
#if !UE_BUILD_SHIPPING
	ServerRunMultiplayerVerification(TEXT("Fire"));
#endif
}

void AMSPlayerController::MSVerifyPrepareKill()
{
#if !UE_BUILD_SHIPPING
	ServerRunMultiplayerVerification(TEXT("PrepareKill"));
#endif
}

void AMSPlayerController::MSVerifyRespawn()
{
#if !UE_BUILD_SHIPPING
	ServerRunMultiplayerVerification(TEXT("Respawn"));
#endif
}

void AMSPlayerController::MSVerifyCapture()
{
#if !UE_BUILD_SHIPPING
	ServerRunMultiplayerVerification(TEXT("Capture"));
#endif
}

void AMSPlayerController::ServerRunMultiplayerVerification_Implementation(const FName Action)
{
	if (MSOnline::IsEnabled()) return;
#if UE_BUILD_SHIPPING
	(void)Action;
	return;
#else
	AMSPlayerCharacter* Shooter = GetPawn<AMSPlayerCharacter>();
	AMSPlayerCharacter* Target = nullptr;
	for (TActorIterator<AMSPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Shooter && It->IsPlayerControlled())
		{
			Target = *It;
			break;
		}
	}
	if (!Shooter || !Target)
	{
		UE_LOG(LogMSMultiplayer, Warning, TEXT("MSVerify %s needs two possessed player characters"), *Action.ToString());
		return;
	}

	auto FaceEachOther = [Shooter, Target]()
	{
		const FRotator ShooterRotation = (Target->GetActorLocation() - Shooter->GetActorLocation()).Rotation();
		const FRotator TargetRotation = (Shooter->GetActorLocation() - Target->GetActorLocation()).Rotation();
		Shooter->SetActorRotation(FRotator(0, ShooterRotation.Yaw, 0));
		Target->SetActorRotation(FRotator(0, TargetRotation.Yaw, 0));
		if (AController* C = Shooter->GetController()) C->SetControlRotation(ShooterRotation);
		if (AController* C = Target->GetController()) C->SetControlRotation(TargetRotation);
	};

	if (Action == TEXT("Setup"))
	{
		const FVector Anchor = Shooter->GetActorLocation();
		Shooter->SetGenericTeamId(MSTeam::PlayerTeamId());
		Target->SetGenericTeamId(MSTeam::EnemyTeamId());
		UMSMobileSuitData* VerificationSuit = LoadObject<UMSMobileSuitData>(nullptr,
			TEXT("/Game/Data/MobileSuits/DA_MS_StrikerMk1.DA_MS_StrikerMk1"));
		Shooter->InitializeUnit(VerificationSuit ? VerificationSuit : Shooter->GetMobileSuitData());
		Target->InitializeUnit(VerificationSuit ? VerificationSuit : Target->GetMobileSuitData());
		UMSWeaponDataBase* VerificationWeapon = LoadObject<UMSWeaponDataBase>(nullptr,
			TEXT("/Game/Data/Weapons/DA_Weapon_Grenade.DA_Weapon_Grenade"));
		if (VerificationWeapon && Shooter->GetWeaponComponent())
		{
			Shooter->GetWeaponComponent()->InitializeSlots({VerificationWeapon});
		}
		Shooter->SetActorLocation(Anchor, false, nullptr, ETeleportType::TeleportPhysics);
		Target->SetActorLocation(Anchor + FVector(6000.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		FaceEachOther();
	}
	else if (Action == TEXT("Fire"))
	{
		FaceEachOther();
		const FVector Origin = Shooter->GetLockOnAimPoint();
		const FVector Direction = (Target->GetLockOnAimPoint() - Origin).GetSafeNormal();
		UGA_Fire_Hitscan* VerificationFire = NewObject<UGA_Fire_Hitscan>(Shooter);
		const bool bFired = VerificationFire &&
			VerificationFire->FireFromOrigins(Shooter, Origin, Direction, Origin);
		UE_LOG(LogMSMultiplayer, Display, TEXT("MSVerify exact fire fired=%s targetArmor=%.0f"),
			bFired ? TEXT("true") : TEXT("false"),
			Target->GetVitalSet() ? Target->GetVitalSet()->GetArmor() : -1.0f);
	}
	else if (Action == TEXT("PrepareKill"))
	{
		if (UMSAttributeSet_Vital* Vital = Target->GetVitalSet()) Vital->SetArmor(1.0f);
		Target->ForceNetUpdate();
		FaceEachOther();
	}
	else if (Action == TEXT("Respawn"))
	{
		Target->InitializeUnit(Target->GetMobileSuitData());
		Target->SetActorHiddenInGame(false);
		Target->SetActorEnableCollision(true);
		Target->SetActorLocation(Shooter->GetActorLocation() + FVector(6000.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Target->ForceNetUpdate();
		FaceEachOther();
	}
	else if (Action == TEXT("Capture"))
	{
		for (TActorIterator<AMSCapturePoint> It(GetWorld()); It; ++It)
		{
			if (!It->IsCaptureLocked())
			{
				It->SetFixedOwner(EMSCaptureOwner::Neutral);
				Shooter->SetActorLocation(It->GetActorLocation() + FVector(0, 0, 1200.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Target->SetActorLocation(It->GetActorLocation() + FVector(It->GetCaptureRadius() * 2.0f, 0, 1200.0f), false, nullptr, ETeleportType::TeleportPhysics);
				break;
			}
		}
	}
	Shooter->ForceNetUpdate();
	Target->ForceNetUpdate();
	UE_LOG(LogMSMultiplayer, Display, TEXT("MSVerify %s shooter=%s target=%s"),
		*Action.ToString(), *GetNameSafe(Shooter), *GetNameSafe(Target));
#endif
}

void AMSPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(AMSPlayerController, bOnlineAuthenticated, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, bOnlineDeployed, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, OnlineIdentity, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, OnlineDeck, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, OnlineAdmissionPawn, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, bOnlineRestoredDeployment, COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(AMSPlayerController, OnlineAssignedTeamId, COND_OwnerOnly);
}
void AMSPlayerController::BeginOnlineAuthentication()
{
 if (!HasAuthority() || !MSOnline::IsEnabled() || !ConnectionId.IsEmpty()) return;
 ConnectionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
 GetWorld()->GetTimerManager().SetTimer(AuthenticationTimer, this, &AMSPlayerController::ExpireOnlineAuthentication, 20.0f, false);
 GetWorld()->GetTimerManager().SetTimer(AuthenticationReadyTimer, this, &AMSPlayerController::RequestAuthenticationWhenServerReady, 0.25f, true);
 RequestAuthenticationWhenServerReady();
}
void AMSPlayerController::RequestAuthenticationWhenServerReady()
{
 if (!HasAuthority() || !GetWorld() || bOnlineAuthenticated || bAuthenticationValidating || ConnectionId.IsEmpty()) return;
 auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>();
 if (GetWorld()->GetNumPlayerControllers() > 12)
 { RejectOnlineAuthentication(TEXT("server_unavailable_or_full")); return; }
 if (!Server || !Server->IsReady()) return;
 GetWorld()->GetTimerManager().ClearTimer(AuthenticationReadyTimer);
 ClientRequestAuthentication(Server->GetServerId());
}
void AMSPlayerController::ClientRequestAuthentication_Implementation(const FString& ServerId)
{
 if (bTicketSubmitted || !IsLocalController() || !GetGameInstance()) return;
 auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
 if (!Account || !Account->HasOnlineSession()) { ServerAuthenticate(FString()); return; }
 AwaitingAuthenticationServer = ServerId;
 ClientAuthGeneration = Account->GetAuthGeneration();
 Account->OnAccountChanged.AddUniqueDynamic(this, &AMSPlayerController::HandleOnlineAccountChanged);
 FString Ticket;
 if (!Account->TakePendingGameTicket(ServerId, Ticket))
 {
  Account->OnGameTicketReady.AddUniqueDynamic(this, &AMSPlayerController::HandleAuthenticationTicketReady);
  return;
 }
 Account->OnGameTicketReady.RemoveDynamic(this, &AMSPlayerController::HandleAuthenticationTicketReady);
 ClientAuthGeneration = Account->GetAuthGeneration(); bTicketSubmitted = true;
 Account->OnAccountChanged.AddUniqueDynamic(this, &AMSPlayerController::HandleOnlineAccountChanged);
 ServerAuthenticate(Ticket); Ticket.Reset();
}
void AMSPlayerController::HandleAuthenticationTicketReady(FString ServerId, FString, int64 Generation)
{
 if (AwaitingAuthenticationServer == ServerId && ClientAuthGeneration == Generation) ClientRequestAuthentication_Implementation(ServerId);
}
void AMSPlayerController::ServerAuthenticate_Implementation(const FString& Ticket)
{
 if (!MSOnline::IsEnabled() || bOnlineAuthenticated || bAuthenticationValidating || ConnectionId.IsEmpty()) return;
 if (Ticket.Len() < 24 || Ticket.Len() > 512) { RejectOnlineAuthentication(TEXT("ticket_required")); return; }
 bAuthenticationValidating = true;
 if (auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>()) Server->ConsumeTicket(this, Ticket, ConnectionId);
 else RejectOnlineAuthentication(TEXT("server_unavailable"));
}
void AMSPlayerController::ExpireOnlineAuthentication() { RejectOnlineAuthentication(TEXT("authentication_expired")); }
void AMSPlayerController::RejectOnlineAuthentication(const FString& Reason)
{
 if (!HasAuthority()) return;
 bOnlineAuthenticated = false; bAuthenticationValidating = false; ConnectionId.Reset();
 GetWorld()->GetTimerManager().ClearTimer(AuthenticationTimer);
 GetWorld()->GetTimerManager().ClearTimer(AuthenticationReadyTimer);
 UE_LOG(LogMSMultiplayer, Warning, TEXT("Online admission rejected: %s"), *Reason);
 if (auto* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>())
  if (Mode->GameSession && Mode->GameSession->KickPlayer(this, FText::FromString(Reason))) return;
 ClientReturnToMainMenuWithTextReason(FText::FromString(Reason));
 Destroy();
}
void AMSPlayerController::CompleteOnlineAuthentication(const FMSOnlineIdentity& Identity, const FMSOnlineDeckSnapshot& Deck)
{
 if (!HasAuthority() || !bAuthenticationValidating) return;
 GetWorld()->GetTimerManager().ClearTimer(AuthenticationTimer);
 bAuthenticationValidating = false; bOnlineAuthenticated = true; OnlineIdentity = Identity; OnlineDeck = Deck;
 if (auto* PS = GetPlayerState<AMSPlayerState>()) PS->SetVerifiedOnlineIdentity(Identity);
 auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>();
 FMSOnlineMatchAccount* Account = Server ? Server->FindAccount(Identity) : nullptr;
 if (Account) { OnlineAssignedTeamId = Account->TeamId; if (auto* PS = GetPlayerState<AMSPlayerState>()) PS->SetSelectedFaction(FGenericTeamId(Account->TeamId)); }
 if (Account && Account->bHasDeployed && !Account->RetainedPawn.IsValid())
 { RejectOnlineAuthentication(TEXT("match_pawn_unavailable")); return; }
 if (Account && Account->RetainedPawn.IsValid())
 {
  bOnlineDeployed = Account->bHasDeployed; bOnlineRestoredDeployment = Account->bHasDeployed;
  Possess(Account->RetainedPawn.Get());
 }
 else if (auto* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>())
 {
  Mode->RestartPlayer(this);
  if (auto* OnlinePawn = GetPawn<AMSPlayerCharacter>())
  {
   OnlinePawn->SetGenericTeamId(FGenericTeamId::NoTeam);
   OnlinePawn->SetActorHiddenInGame(true); OnlinePawn->SetActorEnableCollision(false);
   OnlinePawn->GetCharacterMovement()->DisableMovement();
  }
 }
 OnlineAdmissionPawn = GetPawn<AMSPlayerCharacter>();
 ForceNetUpdate();
 TryApplyOnlineAdmissionPresentation();
 if (IsLocalController() && GetGameInstance()) if (auto* Rooms = GetGameInstance()->GetSubsystem<UMSRoomSubsystem>())
 { Rooms->NotifyLocalAdmission(this); if (Server) Server->RefreshRoomHeartbeat(); }
 UE_LOG(LogMSMultiplayer, Display, TEXT("Online admission accepted connection=%s account=%s revision=%lld"), *ConnectionId, *Identity.AccountId, Deck.Revision);
}
void AMSPlayerController::HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation)
{
 if (ClientAuthGeneration >= 0 && Generation != ClientAuthGeneration)
 {
  bTicketSubmitted = false; bDeploymentPending = false;
  if (auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr) Rooms->LeaveRoom();
  else ClientTravel(TEXT("/Game/Maps/Lvl_GrandBattlefield"), TRAVEL_Absolute);
 }
}
void AMSPlayerController::PawnLeavingGame()
{
 if (MSOnline::IsEnabled() && bOnlineAuthenticated && GetPawn())
 {
  if (auto* OnlinePawn = GetPawn<AMSPlayerCharacter>()) OnlinePawn->SuspendOnlineInputs();
  if (auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>()) Server->Release(this);
  UnPossess(); return;
 }
 Super::PawnLeavingGame();
}
void AMSPlayerController::MarkOnlineRecallAwaitingDeployment()
{
 if (!HasAuthority() || !MSOnline::IsEnabled() || !bOnlineAuthenticated) return;
 // Keep bOnlineDeployed/account.bHasDeployed true while the retained pawn is
 // hidden.  RespawnComponent's replicated recall flag is the additional
 // server-authorized state that permits the next sortie and lets reconnects
 // restore the awaiting-deploy screen instead of the first-sortie briefing.
 ForceNetUpdate();
}
bool AMSPlayerController::RequestOnlineDeployment(int32 SlotIndex, FName SpawnPointId)
{
 if (!MSOnline::IsEnabled() || !bOnlineAuthenticated || bDeploymentPending || SlotIndex < 0 || SlotIndex >= 4 || SpawnPointId.IsNone()) return false;
 bDeploymentPending = true; ServerRequestOnlineDeployment(SlotIndex, SpawnPointId); return true;
}
void AMSPlayerController::ServerRequestOnlineDeployment_Implementation(int32 SlotIndex, FName SpawnPointId)
{
 const FText Rejected = NSLOCTEXT("NeoMSGO", "OnlineDeployRejected", "出撃条件を満たしていません。編成・地点・待機時間を確認してください。");
 auto Reject = [this, &Rejected]() { ClientOnlineDeploymentResult(false, Rejected); };
 if (!MSOnline::IsEnabled() || !bOnlineAuthenticated || !OnlineDeck.Slots.IsValidIndex(SlotIndex)) { Reject(); return; }
 auto* OnlinePawn = GetPawn<AMSPlayerCharacter>();
 auto* PS = GetPlayerState<AMSPlayerState>();
 auto* State = GetWorld()->GetGameState<AMSGameState>();
 auto* Respawn = OnlinePawn ? OnlinePawn->GetRespawnComponent() : nullptr;
 if (!OnlinePawn || !PS || !Respawn || (State && State->bBattleEnded) || !Respawn->CanDeployDeckSlot(SlotIndex)) { Reject(); return; }
 // Initial sortie, an actually dead suit, or a server-completed recall. A living
 // pawn that only opened the UI cannot replenish HP/ammo.
 if (bOnlineDeployed && OnlinePawn->GetVitalSet() && OnlinePawn->GetVitalSet()->GetArmor() > 0 &&
  !Respawn->IsOnlineRecallAwaitingDeploy()) { Reject(); return; }
 AActor* Point = nullptr;
 for (TActorIterator<AMSCapturePoint> It(GetWorld()); It; ++It)
  if (It->GetFName() == SpawnPointId && It->CanDeployFor(PS->GetSelectedFaction())) { Point = *It; break; }
 if (!Point) for (TActorIterator<AMSBotSpawnPoint> It(GetWorld()); It; ++It)
  if (It->GetFName() == SpawnPointId && It->ServesTeam(PS->GetSelectedFaction())) { Point = *It; break; }
 UMSMobileSuitData* Suit = nullptr; FMSDeckLoadout Loadout;
 if (!Point || !MSOnline::ResolveSlot(OnlineDeck.Slots[SlotIndex], Suit, Loadout, OnlineDeck.SchemaVersion == 1) || !Suit) { Reject(); return; }
 if (!Respawn->DeployVerifiedOnlineSlot(SlotIndex, Suit, Loadout, Point->GetActorTransform())) { Reject(); return; }
 bOnlineDeployed = true; OnlinePawn->SetGenericTeamId(PS->GetSelectedFaction());
 if (auto* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>()) Mode->NotifyOnlineDeployment();
 if (auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>()) if (auto* Account = Server->FindAccount(OnlineIdentity)) Account->bHasDeployed = true;
 OnlinePawn->SetActorHiddenInGame(false); OnlinePawn->SetActorEnableCollision(true);
 OnlinePawn->ForceNetUpdate(); ForceNetUpdate();
 ClientOnlineDeploymentResult(true, FText::GetEmpty());
}
void AMSPlayerController::ClientOnlineDeploymentResult_Implementation(bool bSuccess, const FText& Message)
{
 bDeploymentPending = false;
 OnOnlineDeploymentResult.Broadcast(bSuccess, Message);
}

FGenericTeamId AMSPlayerController::GetOnlineAssignedTeamId() const
{
 if (MSOnline::IsEnabled()) return bOnlineAuthenticated ? FGenericTeamId(OnlineAssignedTeamId) : FGenericTeamId::NoTeam;
 const auto* State = GetPlayerState<AMSPlayerState>();
 return State ? State->GetSelectedFaction() : FGenericTeamId::NoTeam;
}

void AMSPlayerController::OnRep_OnlineAdmission()
{
 TryApplyOnlineAdmissionPresentation();
}

void AMSPlayerController::TryApplyOnlineAdmissionPresentation()
{
 if (!MSOnline::IsEnabled() || !IsLocalController() || bOnlineAdmissionPresented ||
     !bOnlineAuthenticated || !OnlineIdentity.IsValid() || !OnlineAdmissionPawn ||
     GetPawn() != OnlineAdmissionPawn || OnlineAdmissionPawn->GetController() != this ||
     OnlineAssignedTeamId == FGenericTeamId::NoTeam.GetId() || !GetPlayerState<AMSPlayerState>()) return;
 if (auto* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr) Rooms->NotifyLocalAdmission(this);
 // Deck and its admission marker share this owning actor. Never copy the match snapshot
 // into the account's persistent hangar deck; only invalidate the deployment UI view.
 if (!bOnlineDeckNotified)
 {
  bOnlineDeckNotified = true;
  if (auto* Deck = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr)
   Deck->OnDeckChanged.Broadcast(INDEX_NONE);
 }
 bOnlineAdmissionPresented = OnlineAdmissionPawn->ApplyOnlineAdmissionPresentation(bOnlineRestoredDeployment);
}
