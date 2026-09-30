// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-1: プレイヤー機体Character。
// 設計元: docs/UE5_Architecture.md §1.1 クラス図
//
// AMSPlayerCharacter : AMSUnitBase                     [Player/]
//  ├─ UMSCharacterMovementComponent (P3-1。本タスク。ACharacter標準CMCを差し替え)
//  ├─ USpringArmComponent + UMSCameraComponent         (P3-5)
//  ├─ UMSLockOnComponent                               (P3-6。ロックオン/ソフトエイム/ホーミングロック)
//  └─ UMSRespawnComponent                              (P3-7。本クラスに搭載済み)
//
// GAS(ASC/Vital/DP回復)・チーム実装は基底AMSUnitBase側(P2-1〜P2-6)。
// ダメージ・判定経路はBOTと完全共用(ガードレールB-10)であり、本クラスは
// プレイヤー固有の移動/カメラ/入力受け口のみを持つ。
//
// P3-3: Enhanced Input マッピング。
// 設計元: docs/UE5_MIGRATION_PLAN.md P3-3 / docs/ArchitectureMap.md 入力アクション一覧
//         (Move/Look/Dash/Jump/Crouch/Fire/Reload/LockOn/Action/Weapon1-3/Tab/Esc)
// アセット: Content/Input/Actions/MS/IA_*, Content/Input/IMC_MSPlayer
//           (Source/NeoMSGO/Player/MSInputAssetGenCommandletで生成。
//            ThirdPersonテンプレート由来の Content/Input/Actions/IA_Move 等とは
//            衝突回避のため別サブフォルダ)
// Move/Lookは非リバインド(PlayerMappableKeySettings未設定=保護)。
//
// P3-5: 三人称カメラ/スコープ。
// 設計元: docs/UE5_MIGRATION_PLAN.md P3-5 / docs/UE5_Architecture.md §3.3 /
//         docs/ArchitectureMap.md セクション5・§21カメラ
// SpringArmはbDoCollisionTest=true, ProbeChannel=ECC_Camera(既定)。自機カプセルは
// MSPawnCapsuleプロファイル(P0-5)でCamera=Ignoreのため、カメラが自機を障害物と
// 誤認しないことをチャンネル設計で構造的に保証する(MIGRATION_NOTES 表#2再発防止)。
// スコープFOV/感度はUMSCameraComponent(Player/MSCameraComponent.h)が担当。
// Look入力には基本感度LookSensitivityScale(0.35)×スコープ倍率を乗算する。
// Fire/Reload/Weapon1-3 are connected to the shared weapon/GA routing layer.
// (TODOコメントに対応タスクIDを明記)。しゃがみ(Crouch)のみ本タスクで
// State.Crouching LooseTagのトグルとして実処理する(P3-4本実装の土台)。

#pragma once

#include "CoreMinimal.h"
#include "Combat/MSUnitBase.h"
#include "Player/MSCameraComponent.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "MSPlayerCharacter.generated.h"

class AMSSupplyPack;
class UMSCharacterMovementComponent;
class UMSLockOnComponent;
class UMSIncomingLockComponent;
class UMSRespawnComponent;
class UMSEmergencyRecallComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UMSHUDWidget;
class UMSDamageFeedbackWidget;
class UMSFullscreenMapWidget;
class UMSSpawnSelectWidget;
class UMSLobbyWidget;
class UGA_Deploy;
class UGA_Fire_Burst;
class UGA_Fire_Charge;
class UGA_Fire_ContinuousBeam;
class UGA_Fire_Hitscan;
class UGA_Fire_Homing;
class UGA_Fire_Shotgun;
class UGA_Guard;
class UGA_Melee;
class UGA_Module_Transzomm;
struct FInputActionValue;
struct FInputActionInstance;

UENUM()
enum class EMSPlayerDeathScreen : uint8
{
	None,
	SpawnSelect,
	DeploymentMap
};

/**
 * プレイヤー機体(Unity: Player/PlayerController.cs のCharacter部)。
 * TeamId=TeamPlayer固定。移動仕様はUMSCharacterMovementComponent参照。
 */
UCLASS()
class NEOMSGO_API AMSPlayerCharacter : public AMSUnitBase
{
	GENERATED_BODY()

public:
	AMSPlayerCharacter(const FObjectInitializer& ObjectInitializer);
	virtual void PawnClientRestart() override;
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.0f) override;

	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	float GetLookSensitivityScale() const { return LookSensitivityScale; }

	/** Applies and optionally persists mouse sensitivity. Values below 0.01 are never accepted. */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetLookSensitivityScale(float NewSensitivity, bool bSave = true);

	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	float GetDefaultCameraFieldOfView() const;

	/** Applies and optionally persists the normal third-person camera FOV (60-110 degrees). */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetDefaultCameraFieldOfView(float NewFieldOfView, bool bSave = true);

	/**
	 * 実効Look感度 = LookSensitivityScale × スコープ倍率 × (ロックオン中はLockedLookDamping)。
	 * ロックオン(ソフトエイム)中はUMSLockOnComponentがTick毎にSetControlRotation(絶対値)を
	 * 書き込むため、無減衰の加算入力(AddControllerYaw/PitchInput)と同一フレームで競合して
	 * 視点が暴れる。Unity版仕様(lockedLookDamping=0.25)に合わせて入力側を減衰させる。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	float GetEffectiveLookSensitivity() const;

	/** ロックオン中、視線方向をターゲット中心のLockedAimClampDegrees円錐内へ拘束する。 */
	void ClampLookToLockedTarget();

	/** Look入力の適用実体(Input_Lookから呼ばれる。Automationテストからも直接駆動可能) */
	void ApplyLookInput(const FVector2D& LookVector);

	/** カスタム移動コンポーネント(標準CharacterMovementを差し替え済み) */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	UMSCharacterMovementComponent* GetMSCharacterMovement() const;

	/** 追従カメラ(スコープFOV/感度倍率はこのコンポーネントのSetScoped系APIを使用。P3-5) */
	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	UMSCameraComponent* GetMSCamera() const { return CameraComponent; }

	/** カメラブーム(SpringArm。ProbeChannel=ECC_Camera、P0-5プロファイル前提) */
	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	USpringArmComponent* GetCameraBoom() const { return SpringArmComponent; }

	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetCameraViewPreset(EMSCameraViewPreset NewPreset, bool bSave = true);

	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	EMSCameraViewPreset GetCameraViewPreset() const;

	/** Toggles between the two supported camera presets. Bound to IA_ViewToggle (P by default). */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void ToggleCameraViewPreset();

	/** リスポーン/Deploy(P3-7。respawnDelay=10s/explosionDelay=5s/上空8m降下。再出撃UIはP8-8で接続) */
	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	UMSRespawnComponent* GetRespawnComponent() const { return RespawnComponent; }
	UFUNCTION(BlueprintPure, Category = "MS|Emergency Recall")
	UMSEmergencyRecallComponent* GetEmergencyRecallComponent() const { return EmergencyRecallComponent; }
	UFUNCTION(BlueprintPure, Category = "MS|UI")
	UMSLobbyWidget* GetLobbyWidget() const { return LobbyWidget; }
	UMSSpawnSelectWidget* GetSpawnSelectWidget() const { return SpawnSelectWidget; }
	EMSPlayerDeathScreen GetLastDeathScreenRequest() const { return LastDeathScreenRequest; }
	static EMSPlayerDeathScreen GetAutomaticDeathScreen() { return EMSPlayerDeathScreen::SpawnSelect; }

	/**
	 * Opens the key-config/settings panel (spec: Esc = "OpenSettings"). Also exposed so a debug
	 * HUD button can reach it without relying on Esc, which the editor's own PIE "Stop" shortcut
	 * intercepts before gameplay ever sees the key when running inside the embedded viewport.
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void OpenSettingsScreen();

	/** Closes the passive combat map if it is open; used by the controller-owned Esc route. */
	bool CloseFullscreenMapIfOpen();

	/**
	 * Opens the sortie (mobile-suit selection) screen at any time, alive or dead.
	 * Reuses the same widget resolution as the death flow; the actual deployment still
	 * goes through the sortie widget's existing UMSRespawnComponent::Deploy path.
	 * Wired to the Settings screen's Resortie button (Esc opens Settings directly by spec).
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void OpenResortieScreen();

	/** Returns to the lobby UI without ending or otherwise mutating the active battle. */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void OpenLobbyScreen();

	/** Toggles the passive combat-map overlay without changing gameplay input ownership. */
	UFUNCTION(BlueprintCallable, Category = "MS|UI")
	void ToggleFullscreenMap();

#if WITH_DEV_AUTOMATION_TESTS
	/** Headless tests cannot CreateWidget without a LocalPlayer; they inject a prebuilt widget. */
	void SetSpawnSelectWidgetForTesting(UMSSpawnSelectWidget* InWidget) { SpawnSelectWidget = InWidget; }
	void SetLobbyWidgetForTesting(UMSLobbyWidget* InWidget) { LobbyWidget = InWidget; }
	void SetFullscreenMapForTesting(UMSFullscreenMapWidget* InWidget) { FullscreenMap = InWidget; }
	/** Drives the bound recall key handler without a local Enhanced Input subsystem. */
	void PressRecallInputForTesting();
	/** Enhanced InputのMoveイベントと同じダブルタップ検出経路を駆動する。 */
	void ProcessMovementInputForTesting(const FVector2D& MovementVector)
	{
		DetectMovementDoubleTap(MovementVector);
		LastMovementInput = MovementVector;
		ApplyMovementInput(MovementVector);
	}
#endif

	/** ロックオン/ソフトエイム/ホーミングロック(P3-6。誘導ミサイルGA P4-10/マーカーUI P8-3が参照) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	UMSLockOnComponent* GetLockOnComponent() const { return LockOnComponent; }
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	UMSIncomingLockComponent* GetIncomingLockComponent() const { return IncomingLockComponent; }

	/** Nearest currently overlapping supply pack, used by the P8 pickup prompt. */
	UFUNCTION(BlueprintPure, Category = "MS|Interaction")
	AMSSupplyPack* GetNearbySupplyPack() const;
	void AddNearbySupplyPack(AMSSupplyPack* Pack);
	void RemoveNearbySupplyPack(AMSSupplyPack* Pack);

	/** Dispatches the active weapon to its data-selected direct GA entry point. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool TryFireCurrentWeapon();

	/** Fire-button press/release entry points. The press keeps eligible weapons firing. */
	bool StartCurrentWeaponFire();
	void StopCurrentWeaponFire();
	/** Cancel without releasing a charged shot (death, disconnect, UI suspension, switch). */
	void CancelCurrentWeaponFire();
	bool IsAutomaticFireActive() const;

	/** Releases a held charge or continuous beam. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	void CompleteCurrentWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool TryReloadCurrentWeapon();
	bool IsGameplayInputAllowed() const;
	bool IsDeathPresentationActive() const { return bDeathPresentationActive; }

	/** Cancels held-fire abilities before changing the active weapon. */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	bool TrySwitchWeaponSlot(int32 SlotIndex);

	/**
	 * ユーザーレビュー#71: 右クリック(IA_LockOn)の武器カテゴリ分岐。
	 * スコープ武器(bIsScopeWeapon: スナイパー系)装備中はスコープズームのトグル、
	 * それ以外は従来どおりロックオンのトグル。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Weapon")
	void ToggleLockOnOrScope();

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	EMSWeaponAbilityRoute GetLastWeaponAbilityRoute() const { return LastWeaponAbilityRoute; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsBurstFireActive() const;

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsChargeFireActive() const;
	float GetChargeFireProgress01() const;

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	bool IsContinuousBeamFireActive() const;

	/** Input semantics shared with automation tests. */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void ToggleCrouch();
	void ApplyMovementInput(const FVector2D& MovementVector);

	/** 固定ブーストを消費し、指定方向へ無敵なしの緊急スラストを行う。 */
	UFUNCTION(BlueprintCallable, Category = "MS|EmergencyDodge")
	bool TryEmergencyDodge(const FVector& WorldDirection);

	UFUNCTION(BlueprintPure, Category = "MS|EmergencyDodge")
	float GetEmergencyDodgeCooldownRemaining() const;

	/** Converts damage into the restrained 0..1 feedback intensity used by shake and overlay. */
	static float ComputeReceivedDamageFeedbackSeverity(float DamageAmount, float MaximumArmor);
	/** Projects attacker direction into screen axes: X=right, Y=up. */
	static FVector2D ComputeReceivedDamageScreenDirection(
		const FVector& VictimLocation, const FVector& AttackerLocation, const FRotator& ViewRotation);

	/** Non-shipping visual acceptance hook; applies real GE_Damage from an enemy bot when available. */
	UFUNCTION(BlueprintCallable, Exec, Category = "MS|Debug")
	void DebugReceiveDamageFeedback();

	/** Non-shipping HUD acceptance hook; drives real GAS knockdown and enemy-death events. */
	UFUNCTION(BlueprintCallable, Exec, Category = "MS|Debug")
	bool DebugTriggerHUDDensityEvents();

	static constexpr float EmergencyDodgeBoostCost = 25.0f;
	static constexpr float EmergencyDodgeCooldownSeconds = 3.0f;
	void BeginHoverInput();
	/** Input-independent module dispatch used by IA_Module and automation tests. */
	bool TryActivateEquippedModule();
	static constexpr float ModuleChargeDurationSeconds = 1.0f;
	bool IsModuleChargeActive() const { return bModuleChargeActive; }
	float GetModuleChargeProgress01() const { return ModuleChargeProgress01; }

	//~ Begin AMSUnitBase Interface
	/** 基底の初期化(GE_InitStats)に加え、機体データの移動速度をMovementComponentへ反映する */
	virtual void InitializeUnit(UMSMobileSuitData* Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	void ApplyOnlineDeploymentPresentation();
	/** Presentation only: never deploys, heals or resets the retained Pawn's resources. */
	bool ApplyOnlineAdmissionPresentation(bool bRestoredDeployment);
	void ApplyOnlineDeathPresentation();
	void SuspendOnlineInputs();
	//~ End AMSUnitBase Interface

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ Begin APawn/ACharacter Interface
	/** ローカルプレイヤーのEnhancedInputサブシステムへIMC_MSPlayerを登録する(P3-3) */
	virtual void PossessedBy(AController* NewController) override;
	/** IA_*一式をUEnhancedInputComponentへバインドする(P3-3) */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn/ACharacter Interface

	// ---- 入力ハンドラ ----

	void Input_Move(const FInputActionValue& Value);
	void Input_MoveCompleted(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);

	/** Dash(Shift)押下保持をMovementComponentへ伝搬(Unity: wantsDash) */
	void Input_DashStarted(const FInputActionValue& Value);
	void Input_DashCompleted(const FInputActionValue& Value);

	/** Jump(Space)=ホバー上昇トリガー押下保持(Unity: wantsHover。§21。通常ジャンプは無し) */
	void Input_HoverStarted(const FInputActionValue& Value);
	void Input_ZeroGravityAscend(const FInputActionValue& Value);
	void Input_HoverCompleted(const FInputActionValue& Value);

	/** しゃがみトグル(State.Crouching LooseTag。P3-4本実装の土台) */
	void Input_ToggleCrouch(const FInputActionValue& Value);
	void Input_ZeroGravityDescend(const FInputActionValue& Value);
	void Input_CrouchCompleted(const FInputActionValue& Value);

	/** IA_Fire press/release routed by active weapon data. */
	void Input_FireStarted(const FInputActionValue& Value);
	void Input_FireCompleted(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerStartCurrentWeaponFire();

	UFUNCTION(Server, Reliable)
	void ServerStopCurrentWeaponFire();
	UFUNCTION(Server, Reliable)
	void ServerCancelCurrentWeaponFire();
	UFUNCTION(Server, Reliable) void ServerSwitchWeaponSlot(int32 SlotIndex);
	UFUNCTION(Server, Reliable) void ServerReloadCurrentWeapon();
	UFUNCTION(Server, Reliable) void ServerUseNearbySupplyPack();
	UFUNCTION() void HandleConfirmedWeaponSwitched(int32 SlotIndex);
	UFUNCTION() void OnRep_OnlineSuit();
	UPROPERTY(ReplicatedUsing=OnRep_OnlineSuit) TObjectPtr<UMSMobileSuitData> OnlineSuit;

	/** Starts reload for the active weapon slot. */
	void Input_Reload(const FInputActionValue& Value);

	/** トグルロック(P3-6。UMSLockOnComponent::ToggleLockOn。dwell 0.8sの自動ロックはTick側) */
	void Input_LockOn(const FInputActionValue& Value);

	/** TODO(P7-6): 補給パック回収等の汎用インタラクトに接続 */
	void Input_Action(const FInputActionValue& Value);

	/** Changes weapon slot after cancelling held-fire state. */
	void Input_Weapon1(const FInputActionValue& Value);
	void Input_Weapon2(const FInputActionValue& Value);
	void Input_Weapon3(const FInputActionValue& Value);
	void Input_ModuleStarted(const FInputActionInstance& Instance);
	void Input_ModuleOngoing(const FInputActionInstance& Instance);
	void Input_ModuleCompleted(const FInputActionInstance& Instance);
	void Input_ModuleCanceled(const FInputActionInstance& Instance);
	void Input_Recall(const FInputActionValue& Value);
	void Input_EmergencyDodge(const FInputActionValue& Value);

	/** Toggles the fullscreen combat-map overlay. */
	void Input_Tab(const FInputActionValue& Value);
	void Input_MapKey();
	void Input_ToggleMinimap();

	UFUNCTION()
	void HandlePlayerDeath(AActor* DeadUnit, AActor* Killer);
	UFUNCTION() void HandleRespawnReady(AMSUnitBase* Unit);
	UFUNCTION()
	void HandlePlayerDamagedFrom(AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation);
	UFUNCTION() void HandleDeathExplosionDue(AMSUnitBase* Unit, FVector DeathLocation);
	void HandleGameplayActiveChanged(bool bGameplayActive);

protected:
	// ---- Enhanced Input アセット(Content/Input/。MSInputAssetGenCommandletが生成) ----
	// BP派生や別マップ用の差し替えを許すためEditDefaultsOnly。コンストラクタで既定アセットを解決する。

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> DashAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> LockOnAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> Weapon1Action;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> Weapon2Action;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> Weapon3Action;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> ModuleAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> ViewToggleAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> RecallAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> EmergencyDodgeAction;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputAction> TabAction;

	UPROPERTY(Transient)
	TObjectPtr<UGA_Module_Transzomm> ModuleAbilityInstance;

	// ---- カメラ(P3-5) ----

	/** カメラブーム。bDoCollisionTest=true/ProbeChannel=ECC_Camera(§3.3の構造保証前提) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> SpringArmComponent;

	/** 追従カメラ(スコープFOV遷移+感度倍率。Player/MSCameraComponent.h) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMSCameraComponent> CameraComponent;

	/**
	 * Look入力の基本感度係数(Unity: lookSensitivity=0.08だが入力値スケールが異なるため
	 * UE側は0.35を既定とする。計画書P3-5完了条件。データ精度方針により近似で可)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Camera", meta = (ClampMin = "0.01"))
	float LookSensitivityScale = 0.35f;

	/**
	 * ロックオン(ソフトエイム)中のLook入力減衰率(Unity: lockedLookDamping=0.25)。
	 * ソフトエイムのSetControlRotation(絶対値上書き)と加算入力の同一フレーム競合による
	 * 視点暴れを抑止する(GetEffectiveLookSensitivityで乗算)。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LockedLookDamping = 0.25f;

	/**
	 * ロックオン中にカーソル(視線)がターゲットから離れられる最大角度 度。
	 * この円錐の外へLook入力で出ようとしても円錐面へクランプされる
	 * (実プレイ要望: ロックオンした周辺からカーソルがはみ出ない)。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Camera", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float LockedAimClampDegrees = 9.0f;

	/** 視点ピッチ下限 度(下向き。Unity: pitchMax=70の下向き70°に対応。UEは正=上向き) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Camera", meta = (ClampMin = "-89.9", ClampMax = "0.0"))
	float ViewPitchMinDeg = -70.0f;

	/** 視点ピッチ上限 度(上向き。Unity: pitchMin=-40の上向き40°に対応) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Camera", meta = (ClampMin = "0.0", ClampMax = "89.9"))
	float ViewPitchMaxDeg = 40.0f;

	// ---- リスポーン(P3-7) ----

	/** リスポーン/Deploy(Unity: PlayerRespawnController。Player/MSRespawnComponent.h) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Respawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMSRespawnComponent> RespawnComponent;

	/** Player-only emergency recall state; BOT classes never receive this component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Emergency Recall",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMSEmergencyRecallComponent> EmergencyRecallComponent;

	// ---- ロックオン(P3-6) ----

	/** ロックオン/ソフトエイム/ホーミングロック(Player/MSLockOnComponent.h) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|LockOn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMSLockOnComponent> LockOnComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|LockOn")
	TObjectPtr<UMSIncomingLockComponent> IncomingLockComponent;

private:
	// Lazily-created direct-entry GA instances. Stateful held-fire abilities must be reused.
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_Hitscan> HitscanAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_Homing> HomingAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_Shotgun> ShotgunAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_Burst> BurstAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_Charge> ChargeAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Fire_ContinuousBeam> ContinuousBeamAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Melee> MeleeAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Guard> GuardAbility;
	UPROPERTY(Transient)
	TObjectPtr<UGA_Deploy> DeployAbility;
	EMSWeaponAbilityRoute LastWeaponAbilityRoute = EMSWeaponAbilityRoute::Unsupported;

	UPROPERTY(Transient)
	TObjectPtr<UMSHUDWidget> CombatHUD;
	UPROPERTY(Transient)
	TObjectPtr<UMSDamageFeedbackWidget> DamageFeedbackWidget;
	UPROPERTY(Transient)
	TObjectPtr<UMSFullscreenMapWidget> FullscreenMap;
	UPROPERTY(Transient)
	TObjectPtr<UMSSpawnSelectWidget> SpawnSelectWidget;
	UPROPERTY(Transient)
	TObjectPtr<UMSLobbyWidget> LobbyWidget;
	UPROPERTY(Transient)
	TSet<TObjectPtr<AMSSupplyPack>> NearbySupplyPacks;
	/** Manual crouch owns one tag count; emergency recall owns its separate count. */
	UPROPERTY(ReplicatedUsing=OnRep_CrouchToggled)
	bool bCrouchToggled = false;
	bool bManualCrouchTagApplied = false;
	bool bWasCrouchingForFacing = false;
	bool bStandingFacingCatchUp = false;
	void ApplyManualCrouchState(bool bCrouching);
	UFUNCTION() void OnRep_CrouchToggled();
	UFUNCTION(Server, Reliable) void ServerSetManualCrouch(bool bCrouching);
	UFUNCTION(Client, Reliable) void ClientRejectManualCrouch();
	void ClearCrouchForMovement();
	void HandleAutomaticFire();
	void StopAutomaticFire();
	bool IsAutomaticRoute(EMSWeaponAbilityRoute Route) const;
	FTimerHandle AutomaticFireTimer;
	int32 AutomaticFireSlotIndex = INDEX_NONE;
	bool bAutomaticFireHeld = false;
	bool CanChargeEquippedModule() const;
	void CancelModuleCharge();
	bool bModuleChargeActive = false;
	float ModuleChargeProgress01 = 0.0f;
	EMSPlayerDeathScreen LastDeathScreenRequest = EMSPlayerDeathScreen::None;
	bool bDeathPresentationActive = false;
	bool bInitialLobbyOpened = false;
	bool bOnlineLocalRestartReady = false;
	void OpenInitialFrontend();
	void CloseOnlineAdmissionFrontend();
	FVector2D LastMovementInput = FVector2D::ZeroVector;
	double LastMovementTapTimes[4] = {-1.0e9, -1.0e9, -1.0e9, -1.0e9};
	double LastEmergencyDodgeTime = -1.0e9;
	float LastDamageFeedbackArmor = 0.0f;
	static constexpr float MovementDoubleTapWindowSeconds = 0.28f;
	void DetectMovementDoubleTap(const FVector2D& MovementVector);
	FVector MovementInputToWorldDirection(const FVector2D& MovementVector) const;
};
