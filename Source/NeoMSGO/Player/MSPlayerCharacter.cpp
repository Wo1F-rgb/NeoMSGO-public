// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSPlayerCharacter.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Net/UnrealNetwork.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/MSBotCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSHitReactionComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSShieldComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSScale.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Data/MSWeaponData.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/MSSupplyPack.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSDamageCameraShake.h"
#include "Player/MSDamageFeedbackWidget.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSIncomingLockComponent.h"
#include "Player/MSRespawnComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSHUDWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/MSFullscreenMapWidget.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSSortieWidget.h"
#include "Audio/MSBgmSubsystem.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSTitleWidget.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "Voice/MSNavigatorSubsystem.h"
#include "Weapons/GA_Deploy.h"
#include "Weapons/GA_Fire_Burst.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/GA_Fire_ContinuousBeam.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Fire_Homing.h"
#include "Weapons/GA_Fire_Shotgun.h"
#include "Weapons/GA_Guard.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"
#include "Modules/GA_Module_Transzomm.h"

namespace
{
	// P3-3: MSInputAssetGenCommandletが生成する既定アセットパス(Content/Input/)。
	// ThirdPersonテンプレート由来の Content/Input/Actions/IA_Move 等とは
	// 別サブフォルダ(Actions/MS/)のため衝突しない。BP派生でEditDefaultsOnlyプロパティを
	// 差し替えれば個別マップ/テスト用のIMCにも切替可能。
	constexpr const TCHAR* MappingContextPath = TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer");
	constexpr const TCHAR* MoveActionPath = TEXT("/Game/Input/Actions/MS/IA_Move.IA_Move");
	constexpr const TCHAR* LookActionPath = TEXT("/Game/Input/Actions/MS/IA_Look.IA_Look");
	constexpr const TCHAR* DashActionPath = TEXT("/Game/Input/Actions/MS/IA_Dash.IA_Dash");
	constexpr const TCHAR* JumpActionPath = TEXT("/Game/Input/Actions/MS/IA_Jump.IA_Jump");
	constexpr const TCHAR* CrouchActionPath = TEXT("/Game/Input/Actions/MS/IA_Crouch.IA_Crouch");
	constexpr const TCHAR* FireActionPath = TEXT("/Game/Input/Actions/MS/IA_Fire.IA_Fire");
	constexpr const TCHAR* ReloadActionPath = TEXT("/Game/Input/Actions/MS/IA_Reload.IA_Reload");
	constexpr const TCHAR* LockOnActionPath = TEXT("/Game/Input/Actions/MS/IA_LockOn.IA_LockOn");
	constexpr const TCHAR* InteractActionPath = TEXT("/Game/Input/Actions/MS/IA_Action.IA_Action");
	constexpr const TCHAR* Weapon1ActionPath = TEXT("/Game/Input/Actions/MS/IA_Weapon1.IA_Weapon1");
	constexpr const TCHAR* Weapon2ActionPath = TEXT("/Game/Input/Actions/MS/IA_Weapon2.IA_Weapon2");
	constexpr const TCHAR* Weapon3ActionPath = TEXT("/Game/Input/Actions/MS/IA_Weapon3.IA_Weapon3");
	constexpr const TCHAR* ModuleActionPath = TEXT("/Game/Input/Actions/MS/IA_Module.IA_Module");
	constexpr const TCHAR* ViewToggleActionPath = TEXT("/Game/Input/Actions/MS/IA_ViewToggle.IA_ViewToggle");
	constexpr const TCHAR* RecallActionPath = TEXT("/Game/Input/Actions/MS/IA_Recall.IA_Recall");
	constexpr const TCHAR* EmergencyDodgeActionPath = TEXT("/Game/Input/Actions/MS/IA_EmergencyDodge.IA_EmergencyDodge");
	constexpr const TCHAR* TabActionPath = TEXT("/Game/Input/Actions/MS/IA_Tab.IA_Tab");
}

void AMSPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (WeaponComponent) WeaponComponent->OnWeaponSwitched.AddUniqueDynamic(this, &AMSPlayerCharacter::HandleConfirmedWeaponSwitched);
	if (UMSAttributeSet_Vital* Vital = GetVitalSet())
	{
		Vital->OnDeath.AddUniqueDynamic(this, &AMSPlayerCharacter::HandlePlayerDeath);
		Vital->OnDamagedFrom.AddUniqueDynamic(this, &AMSPlayerCharacter::HandlePlayerDamagedFrom);
		LastDamageFeedbackArmor = Vital->GetArmor();
	}
	if (RespawnComponent)
	{
		RespawnComponent->OnExplosionDue.AddUniqueDynamic(this, &AMSPlayerCharacter::HandleDeathExplosionDue);
		RespawnComponent->OnRespawnReady.AddUniqueDynamic(this, &AMSPlayerCharacter::HandleRespawnReady);
	}
	if (IsLocallyControlled())
	{
		if (UMSUILockSubsystem* Lock = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr)
		{
			Lock->OnGameplayActiveChanged.AddUObject(this, &AMSPlayerCharacter::HandleGameplayActiveChanged);
			HandleGameplayActiveChanged(Lock->IsGameplayActive());
		}
		// 戦闘マップへの初回出撃時にナビゲーターの案内ボイスを再生する。
		// 多重発火の抑制はUMSNavigatorSubsystem::bFirstSortiePlayedThisSessionが担う。
		if (UMSNavigatorSubsystem* Navigator = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSNavigatorSubsystem>() : nullptr)
		{
			Navigator->PlayLine(EMSNavigatorTrigger::FirstSortie);
		}
	}
}

void AMSPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WeaponComponent) WeaponComponent->OnWeaponSwitched.RemoveDynamic(this, &AMSPlayerCharacter::HandleConfirmedWeaponSwitched);
	CancelCurrentWeaponFire();
	CancelModuleCharge();
	if (FullscreenMap && FullscreenMap->IsOpen()) FullscreenMap->Close();
	if (UMSAttributeSet_Vital* Vital = GetVitalSet())
	{
		Vital->OnDeath.RemoveDynamic(this, &AMSPlayerCharacter::HandlePlayerDeath);
		Vital->OnDamagedFrom.RemoveDynamic(this, &AMSPlayerCharacter::HandlePlayerDamagedFrom);
	}
	if (RespawnComponent)
	{
		RespawnComponent->OnExplosionDue.RemoveDynamic(this, &AMSPlayerCharacter::HandleDeathExplosionDue);
		RespawnComponent->OnRespawnReady.RemoveDynamic(this, &AMSPlayerCharacter::HandleRespawnReady);
	}
	if (UMSUILockSubsystem* Lock = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr)
	{
		Lock->OnGameplayActiveChanged.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool AMSPlayerCharacter::IsGameplayInputAllowed() const
{
	if (MSOnline::IsEnabled()) { const auto* PC = Cast<AMSPlayerController>(GetController()); if (!PC || !PC->IsOnlineAuthenticated() || !PC->IsOnlineDeployed()) return false; }
	if (EmergencyRecallComponent && EmergencyRecallComponent->IsAwaitingDeploy()) return false;
	const UAbilitySystemComponent* ASC=GetAbilitySystemComponent();
	if(bDeathPresentationActive || (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead))) return false;
	// A game-instance UI stack belongs to the local player, never to remote players or bots.
	const UMSUILockSubsystem* Lock=IsLocallyControlled() && GetGameInstance()?GetGameInstance()->GetSubsystem<UMSUILockSubsystem>():nullptr;
	return !Lock || Lock->IsGameplayActive();
}

void AMSPlayerCharacter::HandleGameplayActiveChanged(const bool bGameplayActive)
{
	if (!bGameplayActive)
	{
		CancelModuleCharge();
		CancelCurrentWeaponFire();
		if(UMSCharacterMovementComponent* M=GetMSCharacterMovement())
		{
			M->SetBoostInputHeld(false); M->SetHoverInputHeld(false);
			M->SetZeroGravityAscendInputHeld(false); M->SetZeroGravityDescendInputHeld(false); M->StopMovementImmediately();
		}
		LastMovementInput=FVector2D::ZeroVector;
	}
	// The incapacitated scene retains the mech and HUD while gameplay is gated.
	const bool bShowBattle=bGameplayActive || bDeathPresentationActive;
	if (USkeletalMeshComponent* M=GetMesh()) M->SetVisibility(bShowBattle,true);
	if (CombatHUD) CombatHUD->SetVisibility(bShowBattle?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
}

void AMSPlayerCharacter::HandlePlayerDeath(AActor* DeadUnit, AActor*)
{
	if (DeadUnit != this) return;
	ApplyManualCrouchState(false);
	bWasCrouchingForFacing = bStandingFacingCatchUp = false;
	bDeathPresentationActive=true;
	LastDeathScreenRequest=EMSPlayerDeathScreen::None;
	CancelModuleCharge();
	CancelCurrentWeaponFire();
	if(EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Dead);
	if(FullscreenMap && FullscreenMap->IsOpen()) FullscreenMap->Close();
	if(IsLocallyControlled())
	{
		if(UMSUILockSubsystem* Lock=GetGameInstance()?GetGameInstance()->GetSubsystem<UMSUILockSubsystem>():nullptr) Lock->SetPlayerDead(true);
		HandleGameplayActiveChanged(false);
		// Keep game-only keyboard routing during the passive RESPAWN countdown (F/Tab).
		if(APlayerController* PC=Cast<APlayerController>(GetController())) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor=false; }
	}
}

void AMSPlayerCharacter::HandleRespawnReady(AMSUnitBase* Unit)
{
	if(Unit!=this || !bDeathPresentationActive) return;
	bDeathPresentationActive=false;
	LastDeathScreenRequest=GetAutomaticDeathScreen();
	OpenResortieScreen();
	if(IsLocallyControlled()) HandleGameplayActiveChanged(false);
}

void AMSPlayerCharacter::HandleDeathExplosionDue(AMSUnitBase* Unit, const FVector DeathLocation)
{
	// Player and bots share the kneel -> timed explosion sequence. Only after the explosion
	// has fired does the local player transition to the sortie UI.
	if (Unit != this) return;

	// BOT(AMSBotCharacter::HandleExplosionDue)と同じ爆発演出を実行する(死亡演出統一)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		FGameplayCueParameters Parameters;
		Parameters.Location = DeathLocation;
		ASC->ExecuteGameplayCue(MSTags::Cue_Explosion, Parameters);
	}

	// The global disabled-suit wait ends on OnRespawnReady; this explosion does not open a menu.
}

void AMSPlayerCharacter::OpenResortieScreen()
{
	if (!IsLocallyControlled()) return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;
	// Both offline and online living pilots use the same recall countdown.
	// Completed recall and death retain the normal suit-selection route.
	if (GetVitalSet() && GetVitalSet()->GetArmor() > 0.0f
		&& !GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead)
		&& EmergencyRecallComponent && !EmergencyRecallComponent->IsAwaitingDeploy())
	{
		EmergencyRecallComponent->TryStartRecall();
		return;
	}
	if (FullscreenMap && FullscreenMap->IsOpen()) FullscreenMap->Close();
	if (!SpawnSelectWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSSpawnSelectWidget::StaticClass(), false);
		SpawnSelectWidget = Existing.IsEmpty() ? CreateWidget<UMSSpawnSelectWidget>(PC) : Cast<UMSSpawnSelectWidget>(Existing[0]);
	}
	if (SpawnSelectWidget)
	{
		SpawnSelectWidget->OpenRespawn(RespawnComponent);
		FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(SpawnSelectWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode); PC->bShowMouseCursor=true;
	}
}

void AMSPlayerCharacter::OpenLobbyScreen()
{
	if (!IsLocallyControlled()) return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;

	if (FullscreenMap && FullscreenMap->IsOpen()) FullscreenMap->Close();
	if (!LobbyWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSLobbyWidget::StaticClass(), false);
		LobbyWidget = Existing.IsEmpty() ? CreateWidget<UMSLobbyWidget>(PC) : Cast<UMSLobbyWidget>(Existing[0]);
	}
	if (LobbyWidget) LobbyWidget->Open();
}

void AMSPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (!IsLocallyControlled()) return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;
	// PossessedBy runs on authority; an owning network client reaches this restart instead.
	// It also retries after PlayerInput becomes available without counting the same context twice.
	if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			InputSubsystem && DefaultMappingContext && !InputSubsystem->HasMappingContext(DefaultMappingContext))
		{
			FModifyContextOptions Options;
			Options.bNotifyUserSettings = true;
			InputSubsystem->AddMappingContext(DefaultMappingContext, 0, Options);
		}
	}
	if (UMSUILockSubsystem* Lock = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr)
	{
		// BeginPlay can run before possession, so establish the local-only binding here as well.
		Lock->OnGameplayActiveChanged.RemoveAll(this);
		Lock->OnGameplayActiveChanged.AddUObject(this, &AMSPlayerCharacter::HandleGameplayActiveChanged);
		HandleGameplayActiveChanged(Lock->IsGameplayActive());
	}
	TArray<UUserWidget*> Existing;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSHUDWidget::StaticClass(), false);
	CombatHUD = Existing.Num() > 0 ? Cast<UMSHUDWidget>(Existing[0]) : CreateWidget<UMSHUDWidget>(PC);
	if (!CombatHUD) return;
	UMSHUDViewModel* VM = NewObject<UMSHUDViewModel>(CombatHUD);
	VM->Initialize(PC);
	CombatHUD->SetViewModel(VM);
	if (!CombatHUD->IsInViewport()) CombatHUD->AddToViewport();
	// The lock callback above can run before the HUD exists (or before an old HUD is reused).
	const UMSUILockSubsystem* UILock = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
	HandleGameplayActiveChanged(!UILock || UILock->IsGameplayActive());

	bOnlineLocalRestartReady = true;
	auto* OnlinePC = Cast<AMSPlayerController>(PC);
	// Online mode is also used by the standalone GrandBattlefield login frontend.
	// Only a network client (or an already admitted local host) waits for admission.
	if (MSOnline::IsEnabled() && (GetNetMode() == NM_Client || (OnlinePC && OnlinePC->IsOnlineAuthenticated())))
	{
		if (OnlinePC) OnlinePC->TryApplyOnlineAdmissionPresentation();
		return;
	}
	OpenInitialFrontend();
}

void AMSPlayerCharacter::OpenInitialFrontend()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled()) return;

#if !UE_BUILD_SHIPPING
	// StartMultiplayerDebug.ps1 supplies a process-local identity and enters the battle map
	// directly. Do not cover the multiplayer presence HUD with the normal title/lobby flow.
	FString DebugUsername;
	if (!MSOnline::IsEnabled() && FParse::Value(FCommandLine::Get(), TEXT("MSDebugUser="), DebugUsername))
	{
		bInitialLobbyOpened = true;
		return;
	}
#endif

	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	if (!bInitialLobbyOpened && Instance && Instance->bPendingSortie)
	{
		if (MSOnline::IsEnabled() && !GetWorld()->GetGameState<AMSGameState>()) return;
		TArray<UUserWidget*> ExistingLobbies;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, ExistingLobbies, UMSLobbyWidget::StaticClass(), false);
		LobbyWidget = ExistingLobbies.IsEmpty() ? CreateWidget<UMSLobbyWidget>(PC) : Cast<UMSLobbyWidget>(ExistingLobbies[0]);

		TArray<UUserWidget*> ExistingBriefings;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, ExistingBriefings, UMSBriefingWidget::StaticClass(), false);
		UMSBriefingWidget* BriefingWidget = ExistingBriefings.IsEmpty()
			? CreateWidget<UMSBriefingWidget>(PC) : Cast<UMSBriefingWidget>(ExistingBriefings[0]);
		FText RuleName;
		AMSGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AMSGameMode>() : nullptr;
		AMSGameState* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
		if (BriefingWidget && UMSLobbyWidget::ConsumePendingBattleRule(Instance, Mode, State, RuleName))
		{
			bInitialLobbyOpened = true;
			if (LobbyWidget) LobbyWidget->SelectBattleRule(Instance->PendingRuleIndex);
			BriefingWidget->SetLobbyWidget(LobbyWidget);
			BriefingWidget->SetRoomInformation(RuleName, State->MapDisplayName);
			BriefingWidget->Open();
			return;
		}
	}

	if (!bInitialLobbyOpened)
	{
		// Logged-out players start at the title screen; UMSLoginWidget opens the lobby
		// through OpenLobbyScreen once UMSAuthSubsystem::Login succeeds.
		if (Instance && !Instance->IsLoggedIn())
		{
			bInitialLobbyOpened = true;
			UMSTitleWidget::OpenTitleScreen(PC);
			return;
		}
		TArray<UUserWidget*> ExistingLobbies;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, ExistingLobbies, UMSLobbyWidget::StaticClass(), false);
		LobbyWidget = ExistingLobbies.IsEmpty() ? CreateWidget<UMSLobbyWidget>(PC) : Cast<UMSLobbyWidget>(ExistingLobbies[0]);
		if (LobbyWidget) { bInitialLobbyOpened = true; LobbyWidget->Open(); }
	}
}

AMSPlayerCharacter::AMSPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	// 標準UCharacterMovementComponentをUMSCharacterMovementComponentへ差し替え(§1.1)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMSCharacterMovementComponent>(
		  ACharacter::CharacterMovementComponentName))
{
	// プレイヤー機体はTeamPlayer固定(Core/MSTeamTypes.h。BOT側はP6-1で陣営別に設定)
	TeamId = MSTeam::PlayerTeamId();

	// ---- P3-5: 三人称カメラ(SpringArm + UMSCameraComponent) ----
	// Unity: Cinemachine 3rdPersonFollow(肩越しオフセット)相当。
	// カメラ衝突: bDoCollisionTest=true + ProbeChannel=ECC_Camera。
	// 自機カプセルはMSPawnCapsuleプロファイル(P0-5)でCamera=Ignore、
	// 装飾メッシュ/HeadHitboxもカメラ応答なし(§3.3)のため、Blockするのは
	// MSStructureと地形のみ=自機を障害物と誤認しない(MIGRATION_NOTES 表#2再発防止)。
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	SpringArmComponent->SetupAttachment(GetCapsuleComponent());
	// ピボットは胸元相当の高さ。actor空間の身体由来オフセットなので機体と同率で拡大する。
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f) * MSGO::MSScale);
	SpringArmComponent->TargetArmLength = 450.0f * MSGO::MSScale;
	// 肩越しオフセット(Cinemachine ShoulderOffset相当。実カメラがピボット右横にずれる点は
	// P3-6ソフトエイムで「実カメラ基準」を使う根拠になる。ArchitectureMap セクション5)
	SpringArmComponent->SocketOffset = FVector(0.0f, 55.0f, 20.0f) * MSGO::MSScale;
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bDoCollisionTest = true;
	SpringArmComponent->ProbeChannel = ECC_Camera; // 既定値だが§3.3の設計意図として明示
	// エイム即応性優先でカメララグ無効(Unity版もダンピングはロック中のみ)
	SpringArmComponent->bEnableCameraLag = false;
	SpringArmComponent->bEnableCameraRotationLag = false;

	CameraComponent = CreateDefaultSubobject<UMSCameraComponent>(TEXT("FollowCamera"));
	CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
	CameraComponent->bUsePawnControlRotation = false; // 回転はSpringArm側で制御

	// ---- P3-7: リスポーン/Deploy(Unity: PlayerRespawnController) ----
	// respawnDelay=10s / explosionDelay=5s / 上空8m降下方式。詳細はPlayer/MSRespawnComponent.h
	RespawnComponent = CreateDefaultSubobject<UMSRespawnComponent>(TEXT("RespawnComponent"));
	// MSGO8 HP loss -> RESPAWN ~7s. Per-suit reuse TIME is a separate timer.
	RespawnComponent->SetRespawnTimings(5.0f,7.0f);
	EmergencyRecallComponent = CreateDefaultSubobject<UMSEmergencyRecallComponent>(TEXT("EmergencyRecallComponent"));

	// ---- P3-6: ロックオン/ソフトエイム/ホーミングロック ----
	// 視野角30°/17°(スコープ連動)、手動トグル確定、dwellは誘導捕捉UIのみ、LOS起点=自機胸元。
	LockOnComponent = CreateDefaultSubobject<UMSLockOnComponent>(TEXT("LockOnComponent"));
	IncomingLockComponent = CreateDefaultSubobject<UMSIncomingLockComponent>(TEXT("IncomingLockComponent"));

	// P3-3: 既定のEnhanced Inputアセットを解決(未生成環境ではnullのまま=安全にスキップ)
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultMappingContextFinder(MappingContextPath);
	DefaultMappingContext = DefaultMappingContextFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(MoveActionPath);
	MoveAction = MoveActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(LookActionPath);
	LookAction = LookActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> DashActionFinder(DashActionPath);
	DashAction = DashActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> JumpActionFinder(JumpActionPath);
	JumpAction = JumpActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> CrouchActionFinder(CrouchActionPath);
	CrouchAction = CrouchActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> FireActionFinder(FireActionPath);
	FireAction = FireActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> ReloadActionFinder(ReloadActionPath);
	ReloadAction = ReloadActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LockOnActionFinder(LockOnActionPath);
	LockOnAction = LockOnActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> InteractActionFinder(InteractActionPath);
	InteractAction = InteractActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> Weapon1ActionFinder(Weapon1ActionPath);
	Weapon1Action = Weapon1ActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> Weapon2ActionFinder(Weapon2ActionPath);
	Weapon2Action = Weapon2ActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> Weapon3ActionFinder(Weapon3ActionPath);
	Weapon3Action = Weapon3ActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> ModuleActionFinder(ModuleActionPath);
	ModuleAction = ModuleActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> ViewToggleActionFinder(ViewToggleActionPath);
	ViewToggleAction = ViewToggleActionFinder.Object;
	if (DefaultMappingContext && ViewToggleAction &&
		!DefaultMappingContext->GetMappings().ContainsByPredicate([this](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == ViewToggleAction && Mapping.Key == EKeys::P;
		}))
	{
		// Migration fallback for an older IMC_MSPlayer asset. The generator persists the same mapping.
		DefaultMappingContext->MapKey(ViewToggleAction, EKeys::P);
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> RecallActionFinder(RecallActionPath);
	RecallAction = RecallActionFinder.Object;
	static ConstructorHelpers::FObjectFinder<UInputAction> EmergencyDodgeActionFinder(EmergencyDodgeActionPath);
	EmergencyDodgeAction = EmergencyDodgeActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> TabActionFinder(TabActionPath);
	TabAction = TabActionFinder.Object;

	// P8-10: only the ten scalar actions receive runtime player-mappable metadata.
	// Move/Look remain untouched and therefore cannot be rebound.
	UMSSettingsWidget::PreparePlayerMappableInputActions();

	GConfig->GetFloat(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("LookSensitivityScale"),
		LookSensitivityScale, GGameUserSettingsIni);
	LookSensitivityScale = FMath::Max(0.01f, LookSensitivityScale);
	float SavedFieldOfView = 60.0f;
	GConfig->GetFloat(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("DefaultCameraFieldOfView"),
		SavedFieldOfView, GGameUserSettingsIni);
	CameraComponent->SetDefaultFieldOfView(SavedFieldOfView);
	int32 SavedViewPreset = static_cast<int32>(EMSCameraViewPreset::RightShoulder);
	GConfig->GetInt(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("CameraViewPreset"),
		SavedViewPreset, GGameUserSettingsIni);
	CameraComponent->SetViewPreset(SavedViewPreset == static_cast<int32>(EMSCameraViewPreset::HighOverhead)
		? EMSCameraViewPreset::HighOverhead : EMSCameraViewPreset::RightShoulder);
}

void AMSPlayerCharacter::SetLookSensitivityScale(const float NewSensitivity, const bool bSave)
{
	LookSensitivityScale = FMath::Max(0.01f, NewSensitivity);
	if (bSave)
	{
		GConfig->SetFloat(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("LookSensitivityScale"),
			LookSensitivityScale, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

float AMSPlayerCharacter::GetDefaultCameraFieldOfView() const
{
	return CameraComponent ? CameraComponent->GetDefaultFieldOfView() : 60.0f;
}

void AMSPlayerCharacter::SetDefaultCameraFieldOfView(const float NewFieldOfView, const bool bSave)
{
	const float ClampedFieldOfView = FMath::Clamp(NewFieldOfView, 60.0f, 110.0f);
	if (CameraComponent)
	{
		CameraComponent->SetDefaultFieldOfView(ClampedFieldOfView);
	}
	if (bSave)
	{
		GConfig->SetFloat(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("DefaultCameraFieldOfView"),
			ClampedFieldOfView, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void AMSPlayerCharacter::SetCameraViewPreset(const EMSCameraViewPreset NewPreset, const bool bSave)
{
	if (CameraComponent) CameraComponent->SetViewPreset(NewPreset);
	if (bSave)
	{
		GConfig->SetInt(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("CameraViewPreset"),
			static_cast<int32>(NewPreset), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

EMSCameraViewPreset AMSPlayerCharacter::GetCameraViewPreset() const
{
	return CameraComponent ? CameraComponent->GetViewPreset() : EMSCameraViewPreset::RightShoulder;
}

void AMSPlayerCharacter::ToggleCameraViewPreset()
{
	SetCameraViewPreset(GetCameraViewPreset() == EMSCameraViewPreset::RightShoulder
		? EMSCameraViewPreset::HighOverhead
		: EMSCameraViewPreset::RightShoulder);
}

UMSCharacterMovementComponent* AMSPlayerCharacter::GetMSCharacterMovement() const
{
	// コンストラクタで差し替え済みのため、失敗はクラス構成バグ=CastCheckedで即検出
	return CastChecked<UMSCharacterMovementComponent>(GetCharacterMovement());
}

void AMSPlayerCharacter::InitializeUnit(UMSMobileSuitData* Data)
{
	if (MSOnline::IsEnabled() && !HasAuthority()) return;
	ApplyManualCrouchState(false);
	bWasCrouchingForFacing = bStandingFacingCatchUp = false;
	if (HasAuthority() && MSOnline::IsEnabled() && Data) { OnlineSuit = Data; ForceNetUpdate(); }
	CancelModuleCharge();
	ModuleAbilityInstance = nullptr;
	// 基底: MobileSuitData差し替え+GE_InitStatsでAttribute初期値注入(§1.4)
	Super::InitializeUnit(Data);
	if (LockOnComponent) LockOnComponent->ResetForDeployment();
	if (IncomingLockComponent) IncomingLockComponent->ResetForDeployment();
	bDeathPresentationActive=false; LastDeathScreenRequest=EMSPlayerDeathScreen::None;
	if(IsLocallyControlled())
	{
		if(UMSUILockSubsystem* Lock=GetGameInstance()?GetGameInstance()->GetSubsystem<UMSUILockSubsystem>():nullptr) { Lock->SetPlayerDead(false); HandleGameplayActiveChanged(Lock->IsGameplayActive()); }
	}

	// 移動速度と水中適性はプレイヤー/BOT共通で基底の InitializeUnit が反映する。
}

void AMSPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// P3-3: ローカルプレイヤーのEnhancedInputサブシステムへIMC_MSPlayerを登録
	if (const APlayerController* PC = Cast<APlayerController>(NewController))
	{
		// P3-5: 視点ピッチ制限(Unity: pitchMin=-40/pitchMax=70。UEは正=上向きのため符号反転)
		if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			CameraManager->ViewPitchMin = ViewPitchMinDeg;
			CameraManager->ViewPitchMax = ViewPitchMaxDeg;
		}

		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
					LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (DefaultMappingContext)
				{
					Subsystem->AddMappingContext(DefaultMappingContext, 0);
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("AMSPlayerCharacter::PossessedBy: DefaultMappingContext未設定。"
							 "Content/Input/IMC_MSPlayerが未生成の可能性(MSInputAssetGenCommandlet未実行)。"));
				}
			}
		}
	}
}

void AMSPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("AMSPlayerCharacter::SetupPlayerInputComponent: "
			"UEnhancedInputComponentが見つかりません(Project SettingsのDefaultInputComponentClassを確認)。"));
		return;
	}

	// Move/Look(保護。PlayerMappableKeySettings未設定=リバインド不可。IMC_MSPlayer側で担保)
	if (MoveAction)
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_Move);
		EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_MoveCompleted);
		EIC->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AMSPlayerCharacter::Input_MoveCompleted);
	}
	if (LookAction)
	{
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_Look);
	}

	// Dash/Jump(=ホバー): 押下保持フラグをMovementComponentへ伝搬(実処理はUMSCharacterMovementComponent)
	if (DashAction)
	{
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_DashStarted);
		EIC->BindAction(DashAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_DashCompleted);
		EIC->BindAction(DashAction, ETriggerEvent::Canceled, this, &AMSPlayerCharacter::Input_DashCompleted);
	}
	if (JumpAction)
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_HoverStarted);
		EIC->BindAction(JumpAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_ZeroGravityAscend);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_HoverCompleted);
		EIC->BindAction(JumpAction, ETriggerEvent::Canceled, this, &AMSPlayerCharacter::Input_HoverCompleted);
	}

	// Crouch: トグル式(P3-4本実装の土台。State.Crouching LooseTag)
	if (CrouchAction)
	{
		EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_ToggleCrouch);
		EIC->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_ZeroGravityDescend);
		EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_CrouchCompleted);
		EIC->BindAction(CrouchAction, ETriggerEvent::Canceled, this, &AMSPlayerCharacter::Input_CrouchCompleted);
	}

	// Combat, interaction and UI actions.
	if (FireAction)
	{
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_FireStarted);
		EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_FireCompleted);
	}
	if (ReloadAction)
	{
		EIC->BindAction(ReloadAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Reload);
	}
	if (LockOnAction)
	{
		EIC->BindAction(LockOnAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_LockOn);
	}
	if (InteractAction)
	{
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Action);
	}
	if (Weapon1Action)
	{
		EIC->BindAction(Weapon1Action, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Weapon1);
	}
	if (Weapon2Action)
	{
		EIC->BindAction(Weapon2Action, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Weapon2);
	}
	if (Weapon3Action)
	{
		EIC->BindAction(Weapon3Action, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Weapon3);
	}
	if (ModuleAction)
	{
		EIC->BindAction(ModuleAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_ModuleStarted);
		EIC->BindAction(ModuleAction, ETriggerEvent::Ongoing, this, &AMSPlayerCharacter::Input_ModuleOngoing);
		// UInputTriggerHold reaches its one-second threshold as Triggered. Completed is also
		// bound so the requested lifecycle remains explicit; the handler is idempotent and
		// the later key-release Completed event cannot activate the module twice.
		EIC->BindAction(ModuleAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_ModuleCompleted);
		EIC->BindAction(ModuleAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_ModuleCompleted);
		EIC->BindAction(ModuleAction, ETriggerEvent::Canceled, this, &AMSPlayerCharacter::Input_ModuleCanceled);
	}
	if (ViewToggleAction)
	{
		EIC->BindAction(ViewToggleAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::ToggleCameraViewPreset);
	}
	if (RecallAction)
	{
		EIC->BindAction(RecallAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Recall);
	}
	if (EmergencyDodgeAction)
	{
		EIC->BindAction(EmergencyDodgeAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_EmergencyDodge);
	}
	if (TabAction)
	{
		EIC->BindAction(TabAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Tab);
	}
	// These are gameplay-only shortcuts. Bind N/M here so existing input assets and
	// saved player remaps remain untouched.
	PlayerInputComponent->BindKey(EKeys::N, IE_Pressed, this, &AMSPlayerCharacter::Input_ToggleMinimap);
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &AMSPlayerCharacter::Input_MapKey);
}

float AMSPlayerCharacter::ComputeReceivedDamageFeedbackSeverity(
	const float DamageAmount, const float MaximumArmor)
{
	const float DamageFraction = FMath::Max(0.0f, DamageAmount) / FMath::Max(MaximumArmor, 1.0f);
	return FMath::Clamp(0.2f + FMath::Sqrt(DamageFraction) * 1.6f, 0.22f, 1.0f);
}

FVector2D AMSPlayerCharacter::ComputeReceivedDamageScreenDirection(
	const FVector& VictimLocation, const FVector& AttackerLocation, const FRotator& ViewRotation)
{
	const FVector ToAttacker = (AttackerLocation - VictimLocation).GetSafeNormal2D();
	if (ToAttacker.IsNearlyZero())
	{
		return FVector2D(0.0f, -1.0f);
	}
	const FVector ViewForward = ViewRotation.Vector().GetSafeNormal2D();
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y).GetSafeNormal2D();
	return FVector2D(
		FVector::DotProduct(ToAttacker, ViewRight),
		-FVector::DotProduct(ToAttacker, ViewForward)).GetSafeNormal();
}

void AMSPlayerCharacter::DebugReceiveDamageFeedback()
{
#if !UE_BUILD_SHIPPING
	if (!GetWorld() || !IsLocallyControlled())
	{
		return;
	}
	AActor* DamageInstigator = nullptr;
	for (TActorIterator<AMSBotCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetGenericTeamId() != GetGenericTeamId())
		{
			DamageInstigator = *It;
			break;
		}
	}
	UAbilitySystemComponent* SourceASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(DamageInstigator);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponent();
	if (DamageInstigator && SourceASC && TargetASC)
	{
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(DamageInstigator, DamageInstigator);
		if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
		{
			MSContext->SetAttackerLocation(DamageInstigator->GetActorLocation());
		}
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
		const float PreviewDamage = FMath::Max(
			100.0f, GetVitalSet() ? GetVitalSet()->GetMaxArmor() * 0.15f : 100.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, PreviewDamage);
		TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
	}
	else
	{
		// Keep the acceptance hook useful on an empty test level without weakening production flow.
		HandlePlayerDamagedFrom(this, DamageInstigator,
			DamageInstigator ? DamageInstigator->GetActorLocation()
				: GetActorLocation() + GetActorForwardVector() * 1000.0f);
	}
	AMSTransientVfxActor::SpawnImpactSparks(
		GetWorld(), GetActorLocation() + FVector::UpVector * 4.5f * MSGO::MSScale,
		FLinearColor(1.0f, 0.22f, 0.035f), true, 0.34f, EMSImpactVisualStyle::Explosive);
	// The debug hook also runs on uninitialized visual-audit PIE pawns whose Armor starts at zero.
	// Invoke the exact production handler so overlay acceptance remains testable in that state.
	const float MaxArmor = GetVitalSet() ? FMath::Max(GetVitalSet()->GetMaxArmor(), 1000.0f) : 1000.0f;
	LastDamageFeedbackArmor =
		(GetVitalSet() ? GetVitalSet()->GetArmor() : 0.0f) + MaxArmor * 0.15f;
	HandlePlayerDamagedFrom(this, DamageInstigator,
		DamageInstigator ? DamageInstigator->GetActorLocation()
			: GetActorLocation() + GetActorForwardVector() * 1000.0f);
#endif
}

bool AMSPlayerCharacter::DebugTriggerHUDDensityEvents()
{
#if !UE_BUILD_SHIPPING
	if (!GetWorld() || !IsLocallyControlled() || !GetVitalSet())
	{
		return false;
	}
	AMSBotCharacter* Enemy = nullptr;
	for (TActorIterator<AMSBotCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetGenericTeamId() != GetGenericTeamId() && It->GetVitalSet())
		{
			Enemy = *It;
			break;
		}
	}
	UAbilitySystemComponent* PlayerASC = GetAbilitySystemComponent();
	UAbilitySystemComponent* EnemyASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	if (!PlayerASC || !EnemyASC)
	{
		return false;
	}

	// The warning is sourced from the production DownPoint threshold/state machine.
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	FGameplayEffectContextHandle DownContext = EnemyASC->MakeEffectContext();
	DownContext.AddInstigator(Enemy, Enemy);
	FGameplayEffectSpec DownSpec(GetDefault<UGE_Damage>(), DownContext, 1.0f);
	DownSpec.SetSetByCallerMagnitude(MSTags::Data_Damage, 0.0f);
	DownSpec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, GetVitalSet()->GetMaxDownPoint());
	EnemyASC->ApplyGameplayEffectSpecToTarget(DownSpec, PlayerASC);

	// The battle log is sourced from the same real OnDeath delegate used in combat.
	FGameplayEffectContextHandle KillContext = PlayerASC->MakeEffectContext();
	KillContext.AddInstigator(this, this);
	FGameplayEffectSpec KillSpec(GetDefault<UGE_Damage>(), KillContext, 1.0f);
	KillSpec.SetSetByCallerMagnitude(MSTags::Data_Damage,
		FMath::Max(1.0f, Enemy->GetVitalSet()->GetMaxArmor() * 10.0f));
	KillSpec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
	PlayerASC->ApplyGameplayEffectSpecToTarget(KillSpec, EnemyASC);
	return true;
#else
	return false;
#endif
}

void AMSPlayerCharacter::HandlePlayerDamagedFrom(
	AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation)
{
	if (DamagedUnit != this)
	{
		return;
	}

	UMSAttributeSet_Vital* Vital = GetVitalSet();
	const float CurrentArmor = Vital ? Vital->GetArmor() : 0.0f;
	const float MaximumArmor = Vital ? Vital->GetMaxArmor() : 1.0f;
	const float PreviousArmor = LastDamageFeedbackArmor > 0.0f
		? LastDamageFeedbackArmor : MaximumArmor;
	const float DamageAmount = FMath::Max(1.0f, PreviousArmor - CurrentArmor);
	LastDamageFeedbackArmor = CurrentArmor;

	if (!IsLocallyControlled())
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (AttackerLocation.IsNearlyZero() && Attacker)
	{
		AttackerLocation = Attacker->GetActorLocation();
	}
	const float Severity = ComputeReceivedDamageFeedbackSeverity(DamageAmount, MaximumArmor);
	const FRotator ViewRotation = PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraRotation() : GetActorRotation();
	const FVector2D Direction = ComputeReceivedDamageScreenDirection(
		GetActorLocation(), AttackerLocation, ViewRotation);

	if (!DamageFeedbackWidget)
	{
		DamageFeedbackWidget = CreateWidget<UMSDamageFeedbackWidget>(PC);
		if (DamageFeedbackWidget)
		{
			DamageFeedbackWidget->AddToViewport(95);
			// A native-only UserWidget has no designer-authored desired size. Pin its
			// viewport slot explicitly so NativePaint receives the full screen geometry.
			DamageFeedbackWidget->SetPositionInViewport(FVector2D::ZeroVector, false);
			DamageFeedbackWidget->SetDesiredSizeInViewport(FVector2D::ZeroVector);
			// UE 5.7's position/size setters reset anchors to (0,0). Stretch last
			// with zero margins so resizing and DPI changes retain the viewport edges.
			DamageFeedbackWidget->SetAnchorsInViewport(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			DamageFeedbackWidget->SetAlignmentInViewport(FVector2D::ZeroVector);
		}
	}
	if (DamageFeedbackWidget)
	{
		DamageFeedbackWidget->ShowDamage(Direction, Severity);
	}
	// Single-instance shake restarts instead of stacking during automatic-fire hits.
	PC->ClientStartCameraShake(UMSDamageCameraShake::StaticClass(),
		FMath::Lerp(0.45f, 1.0f, Severity));
	UE_LOG(LogTemp, Display,
		TEXT("MSReceivedDamageFeedback: damage=%.1f max=%.1f severity=%.2f dir=(%.2f,%.2f) overlay=%s"),
		DamageAmount, MaximumArmor, Severity, Direction.X, Direction.Y,
		DamageFeedbackWidget ? TEXT("true") : TEXT("false"));
}

void AMSPlayerCharacter::Input_Move(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	// IA_Move: X=左右(A=-1/D=+1), Y=前後(W=+1/S=-1。IMC_MSPlayer側Swizzle+Negateで整形済み)
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DetectMovementDoubleTap(MovementVector);
	LastMovementInput = MovementVector;
	ApplyMovementInput(MovementVector);
}

void AMSPlayerCharacter::Input_MoveCompleted(const FInputActionValue& Value)
{
	(void)Value;
	LastMovementInput = FVector2D::ZeroVector;
}

FVector AMSPlayerCharacter::MovementInputToWorldDirection(const FVector2D& MovementVector) const
{
	const float Yaw = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	const FRotator YawRotation(0.0f, Yaw, 0.0f);
	return (FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * MovementVector.Y
		+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * MovementVector.X).GetSafeNormal();
}

void AMSPlayerCharacter::DetectMovementDoubleTap(const FVector2D& MovementVector)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const float Components[4] = { MovementVector.Y, -MovementVector.Y, -MovementVector.X, MovementVector.X };
	const float Previous[4] = { LastMovementInput.Y, -LastMovementInput.Y, -LastMovementInput.X, LastMovementInput.X };
	const FVector2D Directions[4] = { {0, 1}, {0, -1}, {-1, 0}, {1, 0} };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Components[Index] > 0.5f && Previous[Index] <= 0.5f)
		{
			if (Now - LastMovementTapTimes[Index] <= MovementDoubleTapWindowSeconds)
			{
				TryEmergencyDodge(MovementInputToWorldDirection(Directions[Index]));
				LastMovementTapTimes[Index] = -1.0e9;
			}
			else
			{
				LastMovementTapTimes[Index] = Now;
			}
		}
	}
}

float AMSPlayerCharacter::GetEmergencyDodgeCooldownRemaining() const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : LastEmergencyDodgeTime;
	return FMath::Max(0.0, LastEmergencyDodgeTime + EmergencyDodgeCooldownSeconds - Now);
}

bool AMSPlayerCharacter::TryEmergencyDodge(const FVector& WorldDirection)
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if(!IsGameplayInputAllowed()) return false;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const UMSAttributeSet_Boost* Boost = GetBoostSet();
	if (!ASC || !Boost || ASC->HasMatchingGameplayTag(MSTags::State_Overheated)
		|| Boost->GetBoost() < EmergencyDodgeBoostCost || GetEmergencyDodgeCooldownRemaining() > 0.0f)
	{
		return false;
	}

	FVector Direction = WorldDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector().GetSafeNormal2D();
	}
	UMSCombatStatics::ConsumeBoostOrOverheat(ASC, EmergencyDodgeBoostCost);
	LaunchCharacter(Direction * (2200.0f * MSGO::MSScale), true, false);
	LastEmergencyDodgeTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	for (TObjectIterator<UMSLockOnComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->NotifyTargetEmergencyDodged(this);
		}
	}
	return true;
}

void AMSPlayerCharacter::ApplyMovementInput(const FVector2D& MovementVector)
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return;
	if(!IsGameplayInputAllowed()) return;
	if (!MovementVector.IsNearlyZero())
	{
		if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Movement);
		ClearCrouchForMovement();
	}
	if (!Controller)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void AMSPlayerCharacter::Input_Look(const FInputActionValue& Value)
{
	ApplyLookInput(Value.Get<FVector2D>());
}

void AMSPlayerCharacter::ApplyLookInput(const FVector2D& LookVector)
{
	if(!IsGameplayInputAllowed()) return;
	if (!Controller)
	{
		return;
	}

	const float Sensitivity = GetEffectiveLookSensitivity();
	AddControllerYawInput(LookVector.X * Sensitivity);
	AddControllerPitchInput(LookVector.Y * Sensitivity);
	ClampLookToLockedTarget();
}

void AMSPlayerCharacter::ClampLookToLockedTarget()
{
	// A crouching pilot is orbiting the stationary suit, not steering its aim.
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
		ASC && ASC->HasMatchingGameplayTag(MSTags::State_Crouching)) return;
	// 実プレイ要望: ロックオン中はカーソルがターゲット周辺(円錐)からはみ出ない。
	// Look入力でターゲットから離れようとした分だけ円錐面へ引き戻す。
	if (!Controller || !LockOnComponent || !LockOnComponent->IsLockedOn())
	{
		return;
	}
	FVector TargetAimPoint;
	if (!LockOnComponent->GetSoftAimTargetLocation(TargetAimPoint))
	{
		return;
	}
	const FVector Eye = CameraComponent ? CameraComponent->GetComponentLocation() : GetActorLocation();
	const FVector ToTarget = (TargetAimPoint - Eye).GetSafeNormal();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FVector Aim = ControlRotation.Vector();
	const float AngleRad = FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim, ToTarget), -1.0f, 1.0f));
	const float MaxRad = FMath::DegreesToRadians(LockedAimClampDegrees);
	if (AngleRad <= MaxRad)
	{
		return;
	}
	// ターゲット方向を軸に、許容角ぶんだけ現在のずれ方向へ倒した向き=円錐面上の最近傍
	const FVector RotationAxis = FVector::CrossProduct(ToTarget, Aim).GetSafeNormal();
	if (RotationAxis.IsNearlyZero())
	{
		return;
	}
	const FVector Clamped = ToTarget.RotateAngleAxis(LockedAimClampDegrees, RotationAxis);
	FRotator NewRotation = Clamped.Rotation();
	NewRotation.Roll = ControlRotation.Roll;
	Controller->SetControlRotation(NewRotation);
}

float AMSPlayerCharacter::GetEffectiveLookSensitivity() const
{
	// P3-5: 基本感度0.35(計画書完了条件)×スコープ中は武器データの感度倍率
	// (Unity: lookSensitivity × scopedSensitivityMultiplier 相当)
	float Sensitivity =
		LookSensitivityScale * (CameraComponent ? CameraComponent->GetLookSensitivityMultiplier() : 1.0f);

	// ロックオン中はソフトエイム(UMSLockOnComponent::ApplySoftAimのSetControlRotation絶対値
	// 上書き)と加算入力が同一フレームで競合して視点が暴れるため、Unity版仕様
	// (lockedLookDamping=0.25)どおり入力側を減衰させて競合を解消する(実プレイ報告バグ対策)。
	if (LockOnComponent && LockOnComponent->CanApplyManualAimAssist())
	{
		Sensitivity *= LockedLookDamping;
	}
	return Sensitivity;
}

void AMSPlayerCharacter::Input_DashStarted(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Movement);
	GetMSCharacterMovement()->SetBoostInputHeld(true);
}

void AMSPlayerCharacter::Input_DashCompleted(const FInputActionValue& Value)
{
	GetMSCharacterMovement()->SetBoostInputHeld(false);
}

void AMSPlayerCharacter::Input_HoverStarted(const FInputActionValue& Value)
{
	BeginHoverInput();
}

void AMSPlayerCharacter::BeginHoverInput()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return;
	if(!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Movement);
	ClearCrouchForMovement();
	UMSCharacterMovementComponent* Movement = GetMSCharacterMovement();
	if (Movement->IsZeroGravityEnabled())
	{
		Movement->SetZeroGravityAscendInputHeld(true);
		return;
	}
	// Unity版に通常ジャンプは無し。Jumpキーはホバー上昇トリガー(§21)
	GetMSCharacterMovement()->SetHoverInputHeld(true);
}

void AMSPlayerCharacter::Input_HoverCompleted(const FInputActionValue& Value)
{
	UMSCharacterMovementComponent* Movement = GetMSCharacterMovement();
	Movement->SetZeroGravityAscendInputHeld(false);
	Movement->SetHoverInputHeld(false);
}

void AMSPlayerCharacter::Input_ZeroGravityAscend(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	if (GetMSCharacterMovement()->IsZeroGravityEnabled())
	{
		AddMovementInput(FVector::UpVector, 1.0f);
	}
}

void AMSPlayerCharacter::Input_ToggleCrouch(const FInputActionValue& Value)
{
	if (!IsGameplayInputAllowed()) return;
	if (UMSCharacterMovementComponent* Movement = GetMSCharacterMovement(); Movement->IsZeroGravityEnabled())
	{
		Movement->SetZeroGravityDescendInputHeld(true);
		return;
	}
	ToggleCrouch();
}

void AMSPlayerCharacter::Input_ZeroGravityDescend(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	if (GetMSCharacterMovement()->IsZeroGravityEnabled())
	{
		AddMovementInput(FVector::DownVector, 1.0f);
	}
}

void AMSPlayerCharacter::Input_CrouchCompleted(const FInputActionValue& Value)
{
	GetMSCharacterMovement()->SetZeroGravityDescendInputHeld(false);
}

void AMSPlayerCharacter::ToggleCrouch()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return;
	if(!IsGameplayInputAllowed()) return;
	if (!GetAbilitySystemComponent()) return;
	ApplyManualCrouchState(!bCrouchToggled);
	if (!HasAuthority()) ServerSetManualCrouch(bCrouchToggled);
}

void AMSPlayerCharacter::ApplyManualCrouchState(const bool bCrouching)
{
	bCrouchToggled = bCrouching;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
		ASC && bManualCrouchTagApplied != bCrouching)
	{
		if (bCrouching) ASC->AddLooseGameplayTag(MSTags::State_Crouching);
		else ASC->RemoveLooseGameplayTag(MSTags::State_Crouching);
		bManualCrouchTagApplied = bCrouching;
	}
	if (HasAuthority()) ForceNetUpdate();
}

void AMSPlayerCharacter::OnRep_CrouchToggled()
{
	ApplyManualCrouchState(bCrouchToggled);
}

void AMSPlayerCharacter::ServerSetManualCrouch_Implementation(const bool bCrouching)
{
	// Releasing a posture remains valid during death/UI transitions. Entering it
	// requires the same authenticated, deployed, living pawn as the normal input.
	if (bCrouching && (!IsGameplayInputAllowed() || UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())))
	{
		ApplyManualCrouchState(false);
		ClientRejectManualCrouch();
		return;
	}
	ApplyManualCrouchState(bCrouching);
}

void AMSPlayerCharacter::ClientRejectManualCrouch_Implementation()
{
	ApplyManualCrouchState(false);
}

void AMSPlayerCharacter::ClearCrouchForMovement()
{
	if (!bCrouchToggled) return;
	ApplyManualCrouchState(false);
	if (!HasAuthority()) ServerSetManualCrouch(false);
}

void AMSPlayerCharacter::FaceRotation(FRotator NewControlRotation, const float DeltaTime)
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	// The camera remains free, but looking around must not spin a fallen body.
	if (UMSHitReactionComponent::BlocksCombat(ASC))
	{
		bStandingFacingCatchUp=true;
		return;
	}
	if (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Crouching))
	{
		// Control rotation still drives CameraBoom: looking around must not turn
		// the crouching capsule, mesh, or weapon (also used by emergency recall).
		bWasCrouchingForFacing = true;
		bStandingFacingCatchUp = false;
		return;
	}
	if (bWasCrouchingForFacing)
	{
		bWasCrouchingForFacing = false;
		bStandingFacingCatchUp = true;
	}
	if (bStandingFacingCatchUp && bUseControllerRotationYaw)
	{
		const float TargetYaw = NewControlRotation.Yaw;
		NewControlRotation.Yaw = FMath::FixedTurn(GetActorRotation().Yaw, TargetYaw,
			360.0f * FMath::Max(DeltaTime, 0.0f));
		bStandingFacingCatchUp = FMath::Abs(FMath::FindDeltaAngleDegrees(
			NewControlRotation.Yaw, TargetYaw)) > 0.1f;
	}
	Super::FaceRotation(NewControlRotation, DeltaTime);
}

void AMSPlayerCharacter::Input_FireStarted(const FInputActionValue& Value)
{
	(void)Value;
	// Input_FireStarted dispatches directly to the server RPC, so the local
	// UI gate must run here as well as inside StartCurrentWeaponFire().
	// State.MeleeRecovery is intentionally not part of IsGameplayInputAllowed;
	// the standard saber follow-up buffer must remain reachable.
	if (!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	if (HasAuthority()) StartCurrentWeaponFire();
	else ServerStartCurrentWeaponFire();
}

void AMSPlayerCharacter::Input_FireCompleted(const FInputActionValue& Value)
{
	(void)Value;
	if (HasAuthority()) StopCurrentWeaponFire();
	else ServerStopCurrentWeaponFire();
}

void AMSPlayerCharacter::ServerStartCurrentWeaponFire_Implementation()
{
	StartCurrentWeaponFire();
}

void AMSPlayerCharacter::ServerStopCurrentWeaponFire_Implementation()
{
	StopCurrentWeaponFire();
}

void AMSPlayerCharacter::ServerCancelCurrentWeaponFire_Implementation()
{
	CancelCurrentWeaponFire();
}

void AMSPlayerCharacter::Input_Reload(const FInputActionValue& Value)
{
	(void)Value;
	if (!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	TryReloadCurrentWeapon();
}

bool AMSPlayerCharacter::StartCurrentWeaponFire()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if (!HasAuthority()) { if (!IsGameplayInputAllowed()) return false; ServerStartCurrentWeaponFire(); return true; }
	if(!IsGameplayInputAllowed()) return false;
	StopAutomaticFire();
	if (!WeaponComponent)
	{
		return false;
	}

	const int32 SlotIndex = WeaponComponent->GetActiveSlotIndex();
	const UMSWeaponDataBase* Weapon = WeaponComponent->GetWeaponData(SlotIndex);
	const EMSWeaponAbilityRoute Route = MSWeaponAbilityRouting::Resolve(Weapon);
	const bool bFired = TryFireCurrentWeapon();
	if (!Weapon || !IsAutomaticRoute(Route) ||
		(!Weapon->HasInfiniteAmmo() && WeaponComponent->GetMagazineAmmo(SlotIndex) <= 0))
	{
		return bFired;
	}

	bAutomaticFireHeld = true;
	AutomaticFireSlotIndex = SlotIndex;
	const float EffectiveInterval = Weapon->FireInterval *
		FMath::Max(0.0f, GetCombatModifiers().FireIntervalMultiplier);
	GetWorldTimerManager().SetTimer(AutomaticFireTimer, this,
		&AMSPlayerCharacter::HandleAutomaticFire, FMath::Max(0.01f, EffectiveInterval), false);
	return bFired;
}

void AMSPlayerCharacter::StopCurrentWeaponFire()
{
	StopAutomaticFire();
	CompleteCurrentWeaponFire();
}

void AMSPlayerCharacter::CancelCurrentWeaponFire()
{
	// Local UI suspension must also stop the authoritative timers. This RPC is
	// cancellation-only, so it remains valid while the account/death gate closes.
	if (!HasAuthority() && IsLocallyControlled()) ServerCancelCurrentWeaponFire();
	StopAutomaticFire();
	if (MeleeAbility) MeleeAbility->CancelMelee();
	if (BurstAbility) BurstAbility->CancelBurst();
	if (HomingAbility) HomingAbility->CancelSalvo();
	if (ChargeAbility) ChargeAbility->CancelCharge();
	if (ContinuousBeamAbility) ContinuousBeamAbility->StopBeam();
}

bool AMSPlayerCharacter::IsAutomaticFireActive() const
{
	// A one-shot timer is inactive inside its own callback while the trigger is
	// still held. Guard state follows the trigger, including that callback.
	return bAutomaticFireHeld;
}

bool AMSPlayerCharacter::IsAutomaticRoute(const EMSWeaponAbilityRoute Route) const
{
	return Route == EMSWeaponAbilityRoute::Hitscan || Route == EMSWeaponAbilityRoute::Shotgun ||
		Route == EMSWeaponAbilityRoute::Burst || Route == EMSWeaponAbilityRoute::Homing;
}

void AMSPlayerCharacter::HandleAutomaticFire()
{
	if (!bAutomaticFireHeld || !WeaponComponent ||
		WeaponComponent->GetActiveSlotIndex() != AutomaticFireSlotIndex)
	{
		StopAutomaticFire();
		return;
	}

	const UMSWeaponDataBase* Weapon = WeaponComponent->GetWeaponData(AutomaticFireSlotIndex);
	if (!Weapon || !IsAutomaticRoute(MSWeaponAbilityRouting::Resolve(Weapon)) ||
		(!Weapon->HasInfiniteAmmo() && WeaponComponent->GetMagazineAmmo(AutomaticFireSlotIndex) <= 0))
	{
		StopAutomaticFire();
		return;
	}

	TryFireCurrentWeapon();
	if (!bAutomaticFireHeld || WeaponComponent->GetActiveSlotIndex() != AutomaticFireSlotIndex)
	{
		return;
	}
	if (!Weapon->HasInfiniteAmmo() && WeaponComponent->GetMagazineAmmo(AutomaticFireSlotIndex) <= 0)
	{
		StopAutomaticFire();
		return;
	}

	// Anchor the next round to this attempt rather than the original press. A
	// periodic timer can fire before this round's cooldown reaches zero after a
	// frame-delayed callback, then skip an entire interval.
	const float EffectiveInterval = Weapon->FireInterval *
		FMath::Max(0.0f, GetCombatModifiers().FireIntervalMultiplier);
	GetWorldTimerManager().SetTimer(AutomaticFireTimer, this,
		&AMSPlayerCharacter::HandleAutomaticFire, FMath::Max(0.01f, EffectiveInterval), false);
}

void AMSPlayerCharacter::StopAutomaticFire()
{
	bAutomaticFireHeld = false;
	AutomaticFireSlotIndex = INDEX_NONE;
	GetWorldTimerManager().ClearTimer(AutomaticFireTimer);
}

bool AMSPlayerCharacter::TryFireCurrentWeapon()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if (MSOnline::IsEnabled() && !HasAuthority()) return false;
	if(!IsGameplayInputAllowed()) return false;
	if (!WeaponComponent)
	{
		LastWeaponAbilityRoute = EMSWeaponAbilityRoute::Unsupported;
		return false;
	}

	const int32 SlotIndex = WeaponComponent->GetActiveSlotIndex();
	const UMSWeaponDataBase* Weapon = WeaponComponent->GetWeaponData(SlotIndex);
	LastWeaponAbilityRoute = MSWeaponAbilityRouting::Resolve(Weapon);

	const FVector CameraOrigin = CameraComponent ? CameraComponent->GetComponentLocation() : GetActorLocation();
	const FVector DefaultFireDirection = MSOnline::IsEnabled() && HasAuthority() ? GetBaseAimRotation().Vector() : CameraComponent
		? CameraComponent->GetForwardVector().GetSafeNormal()
		: GetActorForwardVector();
	const UMSWeaponVisualComponent* WeaponVisual = GetWeaponVisualComponent();
	const FVector MuzzleOrigin = WeaponVisual
		? WeaponVisual->GetMuzzleWorldLocation()
		: GetActorLocation();
	// 実プレイ報告バグ修正: トレース方向は常にカメラ照準方向を使う。GetMuzzleWorldDirection
	// (武器メッシュと平行な向き)を使うと、銃口向きが照準とずれる武器(TAU-rusチャージブラスター等)で
	// 弾がクロスヘアに収束しない。弾道の見た目はVisualOrigin(銃口)→トレース着弾点で別途収束する。
	const FVector FireDirection = DefaultFireDirection;

	switch (LastWeaponAbilityRoute)
	{
	case EMSWeaponAbilityRoute::Hitscan:
		if (!HitscanAbility) HitscanAbility = NewObject<UGA_Fire_Hitscan>(this);
		return HitscanAbility->FireFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::Shotgun:
		if (!ShotgunAbility) ShotgunAbility = NewObject<UGA_Fire_Shotgun>(this);
		return ShotgunAbility->FireShotgunFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::Burst:
		if (!BurstAbility) BurstAbility = NewObject<UGA_Fire_Burst>(this);
		return BurstAbility->StartBurstFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::Homing:
		if (!HomingAbility) HomingAbility = NewObject<UGA_Fire_Homing>(this);
		return HomingAbility->FireHomingFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::Charge:
		if (!ChargeAbility) ChargeAbility = NewObject<UGA_Fire_Charge>(this);
		return ChargeAbility->StartChargeFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::ContinuousBeam:
		if (!ContinuousBeamAbility) ContinuousBeamAbility = NewObject<UGA_Fire_ContinuousBeam>(this);
		return ContinuousBeamAbility->StartBeamFromOrigins(this, CameraOrigin, FireDirection, MuzzleOrigin);
	case EMSWeaponAbilityRoute::Melee:
	{
		if (!MeleeAbility) MeleeAbility = NewObject<UGA_Melee>(this);
		const UMSCharacterMovementComponent* Movement = GetMSCharacterMovement();
		const bool bWantsDashMelee = Movement && Movement->IsBoostDashing();
		return MeleeAbility->PerformMelee(this, GetActorLocation(), GetActorForwardVector(), bWantsDashMelee);
	}
	case EMSWeaponAbilityRoute::Guard:
		// Native shields guard automatically from their suit component. A legacy
		// shield data asset in a selectable slot must not provide a manual toggle.
		return false;
	case EMSWeaponAbilityRoute::Deploy:
		if (!DeployAbility) DeployAbility = NewObject<UGA_Deploy>(this);
		return DeployAbility->DeployItem(this, GetActorLocation(), GetActorForwardVector());
	default:
		UE_LOG(LogTemp, Warning, TEXT("AMSPlayerCharacter: no implemented fire route for weapon '%s' (category '%s')."),
			*GetNameSafe(Weapon), Weapon ? *Weapon->CategoryTag.ToString() : TEXT("None"));
		return false;
	}
}

void AMSPlayerCharacter::CompleteCurrentWeaponFire()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) { CancelCurrentWeaponFire(); return; }
	if (ChargeAbility && ChargeAbility->IsCharging())
	{
		// 実プレイ報告バグ修正: チャージ開始時の位置ではなく、リリース時点のカメラ方向・銃口位置で発射する
		const FVector CameraOrigin = CameraComponent ? CameraComponent->GetComponentLocation() : GetActorLocation();
		const FVector DefaultFireDirection = MSOnline::IsEnabled() && HasAuthority() ? GetBaseAimRotation().Vector() : CameraComponent
			? CameraComponent->GetForwardVector().GetSafeNormal()
			: GetActorForwardVector();
		const UMSWeaponVisualComponent* WeaponVisual = GetWeaponVisualComponent();
		const FVector MuzzleOrigin = WeaponVisual ? WeaponVisual->GetMuzzleWorldLocation() : GetActorLocation();
		// 発射方向はTryFireCurrentWeaponと同じくカメラ照準方向(銃口向きだと照準に収束しない)
		ChargeAbility->ReleaseChargeFromOrigins(CameraOrigin, DefaultFireDirection, MuzzleOrigin);
	}
	if (ContinuousBeamAbility && ContinuousBeamAbility->IsBeamActive())
	{
		ContinuousBeamAbility->StopBeam();
	}
}

bool AMSPlayerCharacter::TryReloadCurrentWeapon()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if (!HasAuthority()) { if (!IsGameplayInputAllowed()) return false; ServerReloadCurrentWeapon(); return true; }
	if(!IsGameplayInputAllowed()) return false;
	StopAutomaticFire();
	return WeaponComponent && WeaponComponent->StartReload(WeaponComponent->GetActiveSlotIndex());
}

bool AMSPlayerCharacter::TrySwitchWeaponSlot(const int32 SlotIndex)
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if (!HasAuthority()) { if (!IsGameplayInputAllowed() || SlotIndex < 0 || SlotIndex >= 3) return false; ServerSwitchWeaponSlot(SlotIndex); return true; }
	if(!IsGameplayInputAllowed()) return false;
	if (!WeaponComponent || SlotIndex == WeaponComponent->GetActiveSlotIndex() ||
		!WeaponComponent->GetWeaponData(SlotIndex))
	{
		return false;
	}

	CancelCurrentWeaponFire();
	return WeaponComponent->SwitchToSlot(SlotIndex);
}

bool AMSPlayerCharacter::IsBurstFireActive() const
{
	return BurstAbility && BurstAbility->IsBurstActive();
}

bool AMSPlayerCharacter::IsChargeFireActive() const
{
	return WeaponComponent && WeaponComponent->IsChargeSnapshotActive();
}

float AMSPlayerCharacter::GetChargeFireProgress01() const
{
	return WeaponComponent ? WeaponComponent->GetChargeProgress01() : 0.0f;
}

bool AMSPlayerCharacter::IsContinuousBeamFireActive() const
{
	return ContinuousBeamAbility && ContinuousBeamAbility->IsBeamActive();
}

void AMSPlayerCharacter::Input_LockOn(const FInputActionValue& Value)
{
	(void)Value;
	ToggleLockOnOrScope();
}

void AMSPlayerCharacter::ToggleLockOnOrScope()
{
	if(!IsGameplayInputAllowed()) return;
	// ユーザーレビュー#71: スコープ武器(スナイパー系)装備中の右クリックはロックオンではなく
	// スコープズームのトグル(FOVはUMSCameraComponent::GetCameraViewが武器データ値へ補間、
	// スコープ中はScopedSensitivityMultiplierで感度低下)。
	const UMSRangedWeaponData* Ranged = WeaponComponent
		? Cast<UMSRangedWeaponData>(WeaponComponent->GetWeaponData(WeaponComponent->GetActiveSlotIndex()))
		: nullptr;
	if (Ranged && Ranged->bIsScopeWeapon && CameraComponent)
	{
		CameraComponent->SetScopedFromWeapon(Ranged, !CameraComponent->IsScoped());
		return;
	}

	// P3-6: IA_LockOn押下による手動トグル。画面内の敵をdwellだけで自動ロックしない。
	if (LockOnComponent)
	{
		LockOnComponent->ToggleLockOn();
	}
}

void AMSPlayerCharacter::Input_Action(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	if (MSOnline::IsEnabled()) { ServerUseNearbySupplyPack(); return; }
	if (AMSSupplyPack* Pack = GetNearbySupplyPack()) Pack->TryConsume(this);
}

AMSSupplyPack* AMSPlayerCharacter::GetNearbySupplyPack() const
{
	AMSSupplyPack* Nearest = nullptr;
	float NearestDistanceSq = TNumericLimits<float>::Max();
	for (AMSSupplyPack* Pack : NearbySupplyPacks)
	{
		if (!IsValid(Pack) || Pack->IsActorBeingDestroyed()) continue;
		const float DistanceSq = FVector::DistSquared(GetActorLocation(), Pack->GetActorLocation());
		if (DistanceSq < NearestDistanceSq) { Nearest = Pack; NearestDistanceSq = DistanceSq; }
	}
	return Nearest;
}

void AMSPlayerCharacter::AddNearbySupplyPack(AMSSupplyPack* Pack)
{
	if (IsValid(Pack)) NearbySupplyPacks.Add(Pack);
}

void AMSPlayerCharacter::RemoveNearbySupplyPack(AMSSupplyPack* Pack)
{
	NearbySupplyPacks.Remove(Pack);
}

void AMSPlayerCharacter::Input_Weapon1(const FInputActionValue& Value)
{
	(void)Value;
	if (!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	TrySwitchWeaponSlot(0);
}

void AMSPlayerCharacter::Input_Weapon2(const FInputActionValue& Value)
{
	(void)Value;
	if (!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	TrySwitchWeaponSlot(1);
}

void AMSPlayerCharacter::Input_Weapon3(const FInputActionValue& Value)
{
	(void)Value;
	if (!IsGameplayInputAllowed()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	TrySwitchWeaponSlot(2);
}

bool AMSPlayerCharacter::CanChargeEquippedModule() const
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if(!IsGameplayInputAllowed()) return false;
	if (MSOnline::IsEnabled()) return false;
	const UMSModuleData* ModuleData = GetEquippedModule();
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ModuleData || !ModuleData->ModuleAbilityClass || !ASC)
	{
		return false;
	}

	FGameplayTagContainer BlockedTags;
	BlockedTags.AddTag(MSTags::State_Module_EffectActive);
	BlockedTags.AddTag(MSTags::State_Module_Cooldown);
	BlockedTags.AddTag(MSTags::State_Overheated);
	BlockedTags.AddTag(MSTags::State_Dead);
	return !ASC->HasAnyMatchingGameplayTags(BlockedTags);
}

void AMSPlayerCharacter::Input_ModuleStarted(const FInputActionInstance& Instance)
{
	if(bDeathPresentationActive && RespawnComponent) { RespawnComponent->RequestEmergencyEject(); return; }
	(void)Instance;
	CancelModuleCharge();
	if (!CanChargeEquippedModule()) return;
	if (EmergencyRecallComponent) EmergencyRecallComponent->CancelRecall(EMSRecallReason::Action);
	bModuleChargeActive = true;
}

void AMSPlayerCharacter::Input_ModuleOngoing(const FInputActionInstance& Instance)
{
	if (!bModuleChargeActive) return;
	if (!CanChargeEquippedModule())
	{
		CancelModuleCharge();
		return;
	}
	ModuleChargeProgress01 = FMath::Clamp(
		Instance.GetElapsedTime() / ModuleChargeDurationSeconds, 0.0f, 1.0f);
}

void AMSPlayerCharacter::Input_ModuleCompleted(const FInputActionInstance& Instance)
{
	if (!bModuleChargeActive) return;
	ModuleChargeProgress01 = FMath::Clamp(
		Instance.GetElapsedTime() / ModuleChargeDurationSeconds, 0.0f, 1.0f);
	if (ModuleChargeProgress01 < 1.0f || !CanChargeEquippedModule())
	{
		CancelModuleCharge();
		return;
	}

	bModuleChargeActive = false;
	ModuleChargeProgress01 = 0.0f;
	TryActivateEquippedModule();
}

void AMSPlayerCharacter::Input_ModuleCanceled(const FInputActionInstance& Instance)
{
	(void)Instance;
	CancelModuleCharge();
}

void AMSPlayerCharacter::CancelModuleCharge()
{
	bModuleChargeActive = false;
	ModuleChargeProgress01 = 0.0f;
}

void AMSPlayerCharacter::Input_Recall(const FInputActionValue& Value)
{
	if(!IsGameplayInputAllowed()) return;
	(void)Value;
	if (!EmergencyRecallComponent)
	{
		return;
	}
	// Z starts recall once. Further presses must not cancel an active countdown or
	// its still-pending online request; movement, actions, damage and death retain
	// their separate interruption paths.
	if (EmergencyRecallComponent->IsRecallActive() || EmergencyRecallComponent->IsRequestPending())
	{
		return;
	}
	EmergencyRecallComponent->TryStartRecall();
}

#if WITH_DEV_AUTOMATION_TESTS
void AMSPlayerCharacter::PressRecallInputForTesting()
{
	Input_Recall(FInputActionValue(true));
}
#endif

void AMSPlayerCharacter::Input_EmergencyDodge(const FInputActionValue& Value)
{
	(void)Value;
	TryEmergencyDodge(MovementInputToWorldDirection(LastMovementInput));
}

bool AMSPlayerCharacter::TryActivateEquippedModule()
{
	if (UMSHitReactionComponent::BlocksCombat(GetAbilitySystemComponent())) return false;
	if (MSOnline::IsEnabled()) return false; // Modules await a dedicated authoritative implementation.
	if(!IsGameplayInputAllowed()) return false;
	UMSModuleData* ModuleData = GetEquippedModule();
	if (!ModuleData || !ModuleData->ModuleAbilityClass ||
		!ModuleData->ModuleAbilityClass->IsChildOf(UGA_Module_Transzomm::StaticClass()))
	{
		return false;
	}
	if (!ModuleAbilityInstance || ModuleAbilityInstance->GetClass() != ModuleData->ModuleAbilityClass)
	{
		ModuleAbilityInstance = NewObject<UGA_Module_Transzomm>(this, ModuleData->ModuleAbilityClass);
	}
	return ModuleAbilityInstance->TryActivateModule(this, ModuleData);
}

void AMSPlayerCharacter::Input_Tab(const FInputActionValue& Value)
{
	(void)Value;
	ToggleFullscreenMap();
}

namespace
{
	bool IsTextEditingFocused()
	{
		if (!FSlateApplication::IsInitialized()) return false;
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return Focused.IsValid() && Focused->GetTypeAsString().Contains(TEXT("EditableText"));
	}
}

void AMSPlayerCharacter::Input_MapKey()
{
	if (IsGameplayInputAllowed() && !IsTextEditingFocused()) ToggleFullscreenMap();
}

void AMSPlayerCharacter::Input_ToggleMinimap()
{
	if (!IsLocallyControlled() || !IsGameplayInputAllowed() || IsTextEditingFocused()) return;
	if (CombatHUD) CombatHUD->ToggleMinimapMode();
}

void AMSPlayerCharacter::ToggleFullscreenMap()
{
	if (!IsLocallyControlled()) return;
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const bool bDead = ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead);
	if ((bDead && !bDeathPresentationActive) || (!bDeathPresentationActive && !IsGameplayInputAllowed()))
	{
		if (FullscreenMap && FullscreenMap->IsOpen()) FullscreenMap->Close();
		return;
	}
	if (!FullscreenMap)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC || !PC->IsLocalController()) return;
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSFullscreenMapWidget::StaticClass(), false);
		FullscreenMap = Existing.Num() > 0 ? Cast<UMSFullscreenMapWidget>(Existing[0]) : CreateWidget<UMSFullscreenMapWidget>(PC);
	}
	if (!FullscreenMap) return;
	if (FullscreenMap->IsOpen()) FullscreenMap->Close(); else FullscreenMap->Open();
}

bool AMSPlayerCharacter::CloseFullscreenMapIfOpen()
{
	if (FullscreenMap && FullscreenMap->IsOpen())
	{
		FullscreenMap->Close();
		return true;
	}
	return false;
}

void AMSPlayerCharacter::OpenSettingsScreen()
{
	if (AMSPlayerController* PC = Cast<AMSPlayerController>(GetController()))
	{
		PC->OpenSettingsScreen();
	}
}

void AMSPlayerCharacter::ServerSwitchWeaponSlot_Implementation(int32 SlotIndex) { TrySwitchWeaponSlot(SlotIndex); }
void AMSPlayerCharacter::ServerReloadCurrentWeapon_Implementation() { TryReloadCurrentWeapon(); }
void AMSPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMSPlayerCharacter, OnlineSuit);
 // Owner predicts its discrete toggle. Other players receive the authoritative pose.
 DOREPLIFETIME_CONDITION(AMSPlayerCharacter, bCrouchToggled, COND_SkipOwner);
}
void AMSPlayerCharacter::OnRep_OnlineSuit()
{
 if (!OnlineSuit) return;
 MobileSuitData = OnlineSuit;
 if (auto* Shield = GetShieldComponent()) Shield->InitializeFromMobileSuit(OnlineSuit);
 if (auto* Visual = GetMeshVisualComponent()) Visual->InitializeFromMobileSuit(OnlineSuit);
 // Suit and component asset references may become available in either replication order.
 if (auto* WeaponVisual = GetWeaponVisualComponent()) WeaponVisual->RefreshFromWeaponComponent();
 if (auto* Movement = GetMSCharacterMovement())
 {
  Movement->SetMovementSpeeds(OnlineSuit->WalkSpeed, OnlineSuit->BoostSpeed);
  Movement->SetAmphibious(OnlineSuit->bAmphibious);
 }
}
void AMSPlayerCharacter::ApplyOnlineDeploymentPresentation()
{
 ApplyManualCrouchState(false);
 bWasCrouchingForFacing = bStandingFacingCatchUp = false;
 // A same-suit redeploy may not produce OnRep_OnlineSuit; invalidate old movement responses here.
 if (auto* Movement = GetMSCharacterMovement()) Movement->ResetBoostPrediction();
 bDeathPresentationActive = false; LastDeathScreenRequest = EMSPlayerDeathScreen::None;
 if (IsLocallyControlled()) if (auto* Lock = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr)
 {
  Lock->SetPlayerDead(false); HandleGameplayActiveChanged(Lock->IsGameplayActive());
 }
}

void AMSPlayerCharacter::ApplyOnlineDeathPresentation()
{
 if (!bDeathPresentationActive) HandlePlayerDeath(this, nullptr);
}
void AMSPlayerCharacter::SuspendOnlineInputs()
{
 ApplyManualCrouchState(false);
 bWasCrouchingForFacing = bStandingFacingCatchUp = false;
 LastMovementInput = FVector2D::ZeroVector;
 HandleGameplayActiveChanged(false); // Cancel charge before any release path; disconnect must not fire a final shot.
}

void AMSPlayerCharacter::ServerUseNearbySupplyPack_Implementation()
{
 if (!MSOnline::IsEnabled() || !IsGameplayInputAllowed()) return;
 AMSSupplyPack* Pack = GetNearbySupplyPack();
 if (Pack && Pack->IsOverlappingActor(this)) Pack->TryConsume(this);
}

void AMSPlayerCharacter::HandleConfirmedWeaponSwitched(int32 SlotIndex)
{
 // Runs from server confirmation/replication, not from the client's requested slot.
 if (IsLocallyControlled() && CameraComponent) CameraComponent->SetScopedFromWeapon(nullptr, false);
}

bool AMSPlayerCharacter::ApplyOnlineAdmissionPresentation(const bool bRestoredDeployment)
{
 if (!IsLocallyControlled() || !bOnlineLocalRestartReady) return false;
 if (!bRestoredDeployment)
 {
  OpenInitialFrontend();
  return bInitialLobbyOpened;
 }
 // A retained actor may be mapped before its owner-only component state. Its default
 // readiness is for first sortie, so never interpret that default as a reconnect result.
 if (!RespawnComponent || RespawnComponent->GetOnlineRespawnSnapshot().DeployRevision <= 0) return false;
 bInitialLobbyOpened = true;
 if (auto* Instance = Cast<UMSGameInstance>(GetGameInstance()))
 {
  Instance->bPendingSortie = false;
  Instance->bPendingRoomJoin = false;
  Instance->bPendingRoomHost = false;
 }
 CloseOnlineAdmissionFrontend();
 if (RespawnComponent->GetOnlineRespawnSnapshot().bAlive)
 {
  ApplyOnlineDeploymentPresentation();
  if (auto* PC = Cast<APlayerController>(GetController()))
  { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; }
 }
 else if (RespawnComponent->GetOnlineRespawnSnapshot().bRecallAwaitingDeploy)
 {
  // A retained recalled suit is alive in the hangar, not an incapacitated suit.
  bDeathPresentationActive = false;
  SetActorHiddenInGame(true);
  SetActorEnableCollision(false);
  OpenResortieScreen();
 }
 else
 {
  // OnRep may already have run before possession. Reapply local death input/HUD state,
  // using the existing deadlines; no death event or timer is restarted here.
  HandlePlayerDeath(this, nullptr);
  if (RespawnComponent->CanDeploy()) HandleRespawnReady(this);
 }
 return true;
}

void AMSPlayerCharacter::CloseOnlineAdmissionFrontend()
{
 APlayerController* PC = Cast<APlayerController>(GetController());
 if (!PC || !IsLocallyControlled()) return;
 auto CloseOwned = [this, PC](UClass* Class, auto Close)
 {
  TArray<UUserWidget*> Found;
  UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, Class, false);
  for (UUserWidget* Widget : Found) if (Widget && Widget->GetOwningPlayer() == PC) Close(Widget);
 };
 // Briefing's ordinary Close schedules a transition back to Lobby. The terminal close
 // deliberately avoids that delayed callback reopening a screen over a retained live suit.
 CloseOwned(UMSBriefingWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSBriefingWidget>(W)->CloseForBattleEnd(); });
 CloseOwned(UMSSpawnSelectWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSSpawnSelectWidget>(W)->Close(); });
 CloseOwned(UMSSortieWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSSortieWidget>(W)->Close(); });
 CloseOwned(UMSLobbyWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSLobbyWidget>(W)->Close(); });
 CloseOwned(UMSDeploymentMapWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSDeploymentMapWidget>(W)->Close(); });
 CloseOwned(UMSFullscreenMapWidget::StaticClass(), [](UUserWidget* W) { CastChecked<UMSFullscreenMapWidget>(W)->Close(); });
 if (auto* Bgm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSBgmSubsystem>() : nullptr) Bgm->PlayScene(EMSBgmScene::Battle);
}
