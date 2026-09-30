// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-1: カスタム移動コンポーネント。
// 設計元: docs/UE5_Architecture.md §1.1 クラス図 / §2.2 State.Boosting
//         ← Unity: Assets/Scripts/Player/PlayerController.cs (ArchitectureMap.md §21)
//
// Unity版仕様の再現(数値はデータ精度方針により「だいたい近い」で可。仕組みを優先):
//  - 歩行/ブースト速度: MobileSuitData.WalkSpeed / BoostSpeed (m/s) 基準
//  - moveAcceleration = 40 m/s^2 基準。18m機体では体感優先(データ精度方針)で
//    60 m/s^2 相当×MSScale (= 6000 * MSScale cm/s^2) へ引き上げ、左右切り返しを機敏に
//  - gravity = -20 m/s^2 (機体比)。18m世界では距離もMSScale倍のため、体感を1.8m時代と
//    揃えるには重力加速度もMSScale倍が必要 → GravityScale = 2000×MSScale/980 ≈ 19.0
//    同時にUE既定PhysicsVolume TerminalVelocity(4000cm/s)の落下速度クランプが
//    18m機体には低すぎ降下が鈍化するため、NewFallVelocityでMSScale倍の上限に差し替える
//  - nonForwardBoostRatio = 0.88 (ブースト中、前方以外への移動速度に掛ける係数)
//  - ホバー上昇: hoverAscendSpeed=6m/s、maxJumpHeight=4m(離陸地点基準)、
//    jumpCeilingBrakeZone=0.6m(上限手前の減速帯)。上限到達で上昇速度0=その場ホバリング
//  - 滑空高度維持: 空中でのブーストダッシュ継続中は verticalVelocity=0(Unity版そのまま)
//  - しゃがみ中・よろけ/ダウン/死亡中は移動・ダッシュ/ホバー入力を無視(重力は継続適用)
//
// 責務境界:
//  - ブーストゲージの消費/回復/OHはP3-2(UMSAttributeSet_Boost)。本コンポーネントは
//    「残量があるか」を BoostAvailableDelegate で問い合わせ(未バインド時はtrue)、
//    「消費すべきか」を IsConsumingBoost() で公開するのみ。State.Overheated タグは参照する。
//  - 入力バインドはP3-3(Enhanced Input)。本コンポーネントは SetBoostInputHeld /
//    SetHoverInputHeld の意図フラグを受けるだけ。
//  - State.Boosting LooseTag の付与/除去は本コンポーネントの責務(§2.2 付与経路)。

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSBoostMovementPrediction.h"
#include "MSCharacterMovementComponent.generated.h"

class UAbilitySystemComponent;
class FMSCharacterSavedMove;
class UMSBoostComponent;

/** ブースト残量問い合わせ(P3-2がバインド。未バインド時は残量ありとみなす) */
DECLARE_DELEGATE_RetVal(bool, FMSBoostAvailableDelegate);

/**
 * MS移動コンポーネント(Unity: PlayerController.cs の移動部)。
 * 歩行/ブースト/ホバー上昇/滑空高度維持/重力係数/非前進ブースト減速を実装する。
 * プレイヤー専用ではなく AMSUnitBase 系で共用可能な作り(B-10)だが、P3-1では
 * AMSPlayerCharacter に搭載する。
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMSCharacterMovementComponent();

	/** カプセル中心より水面がこの高さまで達すると胸部浸水として扱う。 */
	static constexpr float UnderwaterDepthThresholdCm = 12.0f * MSGO::MSScale;

	/**
	 * Foot/surface intersection tolerance used only by water-step VFX.
	 * This is intentionally much shallower than UnderwaterDepthThresholdCm so ankle-deep water
	 * can splash before the capsule centre enters the water PhysicsVolume.
	 */
	static constexpr float WaterSurfaceContactToleranceCm = 1.5f * MSGO::MSScale;

	/**
	 * Rendered water and its detection volume are authored independently.  Daibao's sea is
	 * rendered at Z=30 while the volume ends at Z=0, so mesh lookup needs a wider search window
	 * than the actual foot/surface contact tolerance.
	 */
	static constexpr float RenderedWaterSurfaceSearchToleranceCm = 10.0f * MSGO::MSScale;

	/**
	 * 落下(降下)の最大速度 cm/s。UE既定のPhysicsVolume::TerminalVelocity(4000cm/s)は
	 * 1.8m機体基準の値で、18m機体では機体比~4m/sの極端に遅い降下になってしまう。
	 * 1.8m時代と同じ機体比40m/s相当へMSScale倍する(NewFallVelocityで適用)。
	 */
	static constexpr float TerminalFallSpeedCms = 4000.0f * MSGO::MSScale;

	// ---- 機体データ反映(AMSUnitBase::InitializeUnit経由の単一入口) ----

	/**
	 * 移動速度を機体データから設定する(単位: m/s。内部でcm/sへ変換)。
	 * Unity: walkSpeed / boostSpeed(機体プリセット依存)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void SetMovementSpeeds(float InWalkSpeedMps, float InBoostSpeedMps);

	/** Data-driven module multiplier. The active gameplay tag remains the source of truth. */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void SetModuleSpeedMultiplier(float InMultiplier) { ModuleSpeedMultiplier = FMath::Max(0.0f, InMultiplier); }

	float GetModuleSpeedMultiplier() const { return ModuleSpeedMultiplier; }

	/** 機体データ由来の水中適性を設定する。 */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void SetAmphibious(bool bInAmphibious) { bAmphibious = bInAmphibious; }

	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsAmphibious() const { return bAmphibious; }

	/** 水PhysicsVolumeの水面が18m機体の胸元まで達した時、または明示的なSwimming時に水中と扱う。 */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsUnderwater() const;

	/** Surface-contact VFX cadence: 0.5 s at rest, 0.3 s at maximum speed. */
	static float ComputeWaterRippleInterval(float HorizontalSpeedCms, float ReferenceMaxSpeedCms);

	/** Enables MOVE_Flying based zero-gravity movement while preserving walking on a floor. */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement|ZeroGravity")
	void SetZeroGravityEnabled(bool bEnabled);

	/** Re-reads AMSGameState::bZeroGravityMap (called on spawn/respawn). */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement|ZeroGravity")
	void RefreshZeroGravityModeFromWorld();

	UFUNCTION(BlueprintPure, Category = "MS|Movement|ZeroGravity")
	bool IsZeroGravityEnabled() const { return bZeroGravityEnabled; }

	UFUNCTION(BlueprintPure, Category = "MS|Movement|ZeroGravity")
	bool IsZeroGravityFlying() const { return bZeroGravityEnabled && IsFlying(); }

	void SetZeroGravityAscendInputHeld(bool bHeld);
	void SetZeroGravityDescendInputHeld(bool bHeld);
	float GetZeroGravityVerticalInput() const;
	/** Read-only input state for runtime network probes and diagnostics. */
	bool IsBoostInputHeld() const { return bBoostInputHeld; }
	bool IsHoverInputHeld() const { return bHoverInputHeld; }
	bool IsZeroGravityAscendInputHeld() const { return bZeroGravityAscendInputHeld; }
	bool IsZeroGravityDescendInputHeld() const { return bZeroGravityDescendInputHeld; }

	// ---- 入力意図(P3-3のEnhanced Inputがバインド) ----

	/** ダッシュ(ブースト)キーの押下状態。Unity: wantsDash */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void SetBoostInputHeld(bool bHeld) { bBoostInputHeld = bHeld; }

	/** ホバー(ジャンプ)キーの押下状態。Unity: wantsHover */
	UFUNCTION(BlueprintCallable, Category = "MS|Movement")
	void SetHoverInputHeld(bool bHeld) { bHoverInputHeld = bHeld; }

	// ---- 公開状態(Unity: PlayerController 公開プロパティ対応) ----

	/**
	 * ブーストダッシュ中か(Unity: IsDashing = 非しゃがみ・Dash押下・非OH・残量>0)。
	 * ダッシュ格闘(P4-7)の発動条件・アニメ(P5-1)が参照する。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsBoostDashing() const;

	/** ホバー上昇制御中か(空中でホバーキー保持+ブースト使用可) */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsHoverAscending() const;

	/** 滑空(空中ブーストダッシュの高度維持)中か */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsGliding() const;

	/**
	 * ブーストを消費すべき状態か(Unity: HandleBoostState の消費条件 =
	 * (wantsDash || wantsHover) かつ 非OH・非しゃがみ・非行動不能・残量あり)。
	 * P3-2のGE_BoostConsume有効化条件として参照される。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Movement")
	bool IsConsumingBoost() const;

	/** ブースト残量問い合わせフック(P3-2がAttributeSet_Boost残量でバインドする) */
	FMSBoostAvailableDelegate BoostAvailableDelegate;
	/** Reinitialization/ownership boundary, separate from replicated attribute notifications. */
	void ResetBoostPrediction();
	FMSBoostMovementState CaptureBoostMovementState();
	const FMSBoostMovementState& GetPredictedBoostMovementState() const { return BoostMovementState; }
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual bool ForcePositionUpdate(float DeltaTime) override;

	//~ Begin UCharacterMovementComponent Interface
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	/** Uses the stock packed movement RPC while allocating our saved move with MS input flags. */
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	// Water VFX runs after the movement solver, outside prediction/replay hooks.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	/** non-boost 空中制御(AirControl)を半減させる。ブースト中は既存の1.0挙動を維持。 */
	virtual FVector GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration) override;

protected:
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;
	virtual void PhysFlying(float deltaTime, int32 Iterations) override;
	/** UE既定のPhysicsVolume::TerminalVelocityクランプをMSScale倍の上限に差し替える。 */
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	//~ End UCharacterMovementComponent Interface

protected:
	// ---- 移動パラメータ(Unity: PlayerController.cs。数値はデータ精度方針で近似可) ----

	/**
	 * ブースト中、前方以外へ移動する際の速度係数(Unity: nonForwardBoostRatio=0.88f)。
	 * 入力方向と機体前方の水平内積(0〜1)で 1.0 とこの値をLerpして適用する
	 * (Unity版の趣旨「前進以外は88%」の連続近似。斜め前進は部分的に減速)。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Boost", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NonForwardBoostRatio = 0.88f;

	/** ホバー上昇速度 cm/s(Unity: hoverAscendSpeed=6f m/s) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Hover", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HoverAscendSpeed = 600.0f * MSGO::MSScale;

	/** 離陸地点からの最大上昇高度 cm(Unity: maxJumpHeight=4f m。ユーザーテストプレイ指示によりUnity比×3) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Hover", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxHoverHeight = 1200.0f * MSGO::MSScale;

	/** 上限手前で上昇を減速し始める距離 cm(Unity: jumpCeilingBrakeZone=0.6f m) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Hover", meta = (ClampMin = "1.0", Units = "cm"))
	float HoverCeilingBrakeZone = 60.0f * MSGO::MSScale;

	/** 歩行速度 cm/s(SetMovementSpeedsで機体データから設定。Unity既定 walkSpeed=6m/s) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Movement", meta = (Units = "cm/s"))
	float WalkSpeedCms = 600.0f;

	/** ブースト速度 cm/s(SetMovementSpeedsで機体データから設定。Unity既定 boostSpeed=14m/s) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Movement", meta = (Units = "cm/s"))
	float BoostSpeedCms = 1400.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Movement")
	float ModuleSpeedMultiplier = 1.0f;

	/** 水中適性なしの場合、水中で GetMaxSpeed に 0.8 倍を適用する。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Movement")
	bool bAmphibious = false;

	/** Low braking keeps momentum after input is released in space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Movement|ZeroGravity", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float ZeroGravityBrakingDeceleration = 250.0f;

	/** Distance below the capsule at which Flying settles into Walking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MS|Movement|ZeroGravity", meta = (ClampMin = "0.0", Units = "cm"))
	float ZeroGravityFloorSnapDistance = 12.0f * MSGO::MSScale;

#if WITH_EDITOR
	/** Deterministic editor-only entry point for real-map PIE acceptance capture. */
	UFUNCTION(BlueprintCallable, Category = "MS|Testing")
	void UpdateWaterFootRippleForEditorAcceptance(float DeltaSeconds);
#endif

private:
	friend class FMSCharacterSavedMove;
	friend struct FMSCharacterMoveResponseDataContainer;
	friend class FMSBoostMovementReplayTest;
	friend class FMSBoostMovementResponseTest;
	friend class FMSBoostMovementClockTest;
	friend class FMSBoostRecoveryTest;
	friend class FMSZeroGravityBoostParityTest;
	UMSBoostComponent* ResolveBoostDriver();
	void RestoreBoostMovementState(const FMSBoostMovementState& State);
	void TraceBoostMove(const TCHAR* Phase, float TimeStamp, float DeltaSeconds,
		const FMSBoostMovementState& Start, const FMSBoostMovementState& End, int32 ConsumingAtStart = -1) const;
	FMSCharacterNetworkMoveDataContainer MSNetworkMoveData;
	FMSCharacterMoveResponseDataContainer MSResponseData;
	FMSBoostMovementState BoostMovementState;
	FMSBoostMovementState PendingResponseBoostState;
	TWeakObjectPtr<UMSBoostComponent> CachedBoostDriver;
	uint32 ClientSimulationEpoch = 1;
	uint32 PendingResponseClientEpoch = 0;
	uint32 ResponseSequence = 0;
	uint32 LastAcceptedResponseSequence = 0;
	float PendingResponseTimeStamp = -1.0f;
	float ActiveServerMoveTimeStamp = -1.0f;
	bool bRequestBoostCorrection = false;
	bool bApplyingBoostCorrection = false;
	bool bCombinedBoostMove = false;
	bool bForcedBoostUpdate = false;
	bool bHasTakeoffBase = false;
	const FMSCharacterNetworkMoveData* ActiveBoostNetworkMove = nullptr;
	friend class FMSWaterFootRippleBranchesTest;
	// ---- 内部判定 ----

	/** 行動不能(State.Dead / State.Staggered / State.KnockedDown / State.MeleeRecovery=P4-7格闘硬直)か。移動・ブースト入力無視(重力は継続) */
	bool IsIncapacitated() const;

	/** しゃがみ中(State.Crouching)か。移動速度0+ダッシュ/ホバー入力無視(P3-4) */
	bool IsCrouchingByTag() const;

	/** ブースト使用可能か(非行動不能・非しゃがみ・非OH・残量あり) */
	bool CanUseBoost() const;

	/** ホバー上昇の目標垂直速度(上限4m手前の減速帯でHoverAscendSpeed×0〜1) */
	float ComputeHoverAscendSpeed() const;

	/** ブースト中の非前進減速係数を速度へ適用 */
	float ApplyNonForwardBoostRatio(float Speed) const;

	/** OwnerのASCを遅延解決してタグ照会(未解決ならfalse=タグなし扱い) */
	bool OwnerHasTag(const struct FGameplayTag& Tag) const;

	/** State.Boosting LooseTag をブースト使用状態と同期(§2.2 付与経路=MovementComponent) */
	void UpdateBoostingLooseTag();

	void UpdateWaterFootRipple(float DeltaSeconds);
	void SpawnWaterFootRipple(const FVector& SurfaceLocation, float HorizontalSpeedCms, bool bEntrySplash);
	bool FindIntersectingWaterSurface(FVector& OutSurfaceLocation) const;

	/** OwnerのASC(遅延解決キャッシュ) */
	mutable TWeakObjectPtr<UAbilitySystemComponent> CachedASC;

	/** ダッシュキー押下中(Unity: wantsDash) */
	bool bBoostInputHeld = false;

	/** ホバーキー押下中(Unity: wantsHover) */
	bool bHoverInputHeld = false;

	bool bZeroGravityEnabled = false;
	bool bZeroGravityAscendInputHeld = false;
	bool bZeroGravityDescendInputHeld = false;
	float PreZeroGravityBrakingDecelerationFlying = 0.0f;

	/** 離陸地点Z(cm)。接地→非接地遷移時に記録。ホバー上限 = TakeoffBaseZ + MaxHoverHeight */
	float TakeoffBaseZ = 0.0f;

	/** State.Boosting LooseTagの現在付与状態(差分更新用) */
	bool bBoostingTagApplied = false;

	bool bWasTouchingWater = false;
	float WaterRippleElapsed = 0.0f;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only observation/injection; keeps branch tests on UpdateWaterFootRipple itself. */
	int32 WaterRippleSpawnCountForTest = 0;
	TOptional<ENetMode> WaterRippleNetModeOverrideForTest;
#endif
};
