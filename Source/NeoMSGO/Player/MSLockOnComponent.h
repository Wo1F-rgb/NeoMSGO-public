// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-6: ロックオン/ソフトエイム/ホーミングロック。
// 設計元: docs/UE5_MIGRATION_PLAN.md P3-6 / docs/UE5_Architecture.md §1.7 /
//         docs/ArchitectureMap.md セクション5
//         ← Unity: WeaponController の HandleLockOn / FindLockOnTarget /
//           HandleHomingAutoLock / FindHomingCandidate / HasLineOfSight 一式
//
// Unity版の教訓(ArchitectureMap セクション5)を構造で再発防止する:
//  1. LOS起点はカメラではなく「自機胸元」(losEyeHeight=1.4m。ガードレールB-5)。
//     カメラ基準だとカメラだけ遮蔽されたときに実際は見えている敵のロックが誤って切れる。
//     起点=自機のGetLockOnAimPoint() / 終点=対象のGetLockOnAimPoint()(対象の胸元)。
//  2. ソフトエイムの吸い付き基準は「実カメラ位置」(Camera.main相当=UMSCameraComponentの
//     ワールド座標)。SpringArmのSocketOffset(肩越しオフセット)により実カメラはピボットの
//     右横にずれるため、ピボット(CameraRig)基準だと敵の少し右を向いてしまう。
//  3. LOSトレースは自機Actorを無視リストへ(自機の頭部コライダーを障害物と誤認して
//     ロックが切れるバグの再発防止)。
//  4. 候補フィルタは UMSTeamStatics::CanLockOn のみ(B-2単一API)。味方/中立/占領拠点/
//     死亡済みは構造的に除外され、本拠点コアはロック可能(B-5)。
//  5. LOS/候補収集は WeaponTrace チャンネル(§3.1「弾を止めるものはロックも遮る」B-3/B-6)。
//
// 通常ロック確定は IA_LockOn の手動トグルのみ。dwell は誘導兵器の捕捉予告UIにだけ使用し、
// 画面内へ敵が入っただけで通常ロックを自動発動させない。
// ToggleLockOn() は即時ロック/解除(Unity: 通常ロックオンのトグル。Input_LockOnから接続)。
// ロック後は照準を外しても維持し、遮蔽・死亡でのみ自動解除する(Unity版ホーミングロック仕様)。
// スコープ中は視野角17°へ狭める(P3-5 UMSCameraComponent::IsScoped と連携。計画書P3-6完了条件)。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/MSScale.h"
#include "Engine/EngineTypes.h"
#include "MSLockOnComponent.generated.h"

class UMSCameraComponent;
class AController;

/** Owner reconciliation for predicted manual lock intent, separate from threat presentation. */
USTRUCT()
struct FMSManualLockAuthoritySnapshot
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<AActor> Target = nullptr;
 UPROPERTY() int32 SpawnEpoch = 0;
 UPROPERTY() uint32 RequestSequence = 0;
 UPROPERTY() uint32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSLockOnTargetChangedSignature, AActor*, OldTarget, AActor*, NewTarget);

/**
 * ロックオン/ソフトエイム/ホーミングロック(Unity: WeaponControllerのHandleLockOn系一式)。
 * AMSPlayerCharacter に搭載する。ロック状態は本コンポーネントが保持し、
 * 誘導ミサイルGA(P4-10)へターゲットを供給、ロックオンマーカーUI(P8-3)が参照する。
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSLockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSLockOnComponent();

	// ------------------------------------------------------------------
	// スコア式定数(Unity: score = angleScore*0.35f + distanceScore*0.65f。距離重視)
	// ------------------------------------------------------------------

	/** スコアの角度差重み */
	static constexpr float AngleScoreWeight = 0.35f;

	/** スコアの距離重み */
	static constexpr float DistanceScoreWeight = 0.65f;

	/** ゲームビューポートが無い状況(自動テスト/専用サーバ/BeginPlay前)のフォールバック解像度 px */
	static constexpr float FallbackViewportWidthPx = 1920.0f;
	static constexpr float FallbackViewportHeightPx = 1080.0f;

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent Interface

	/** 実プレイ報告バグ修正: 武器を持ち替えたらロックオンは解除する(Unity版仕様)。 */
	UFUNCTION()
	void HandleWeaponSwitched(int32 NewSlotIndex);

	// ------------------------------------------------------------------
	// 操作API
	// ------------------------------------------------------------------

	/**
	 * トグルロック(Unity: lockOnAction.WasPressedThisFrame()。Input_LockOnから接続)。
	 * ロック中なら解除、未ロックなら現在の最良候補へ即時ロックする(dwell不要)。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|LockOn")
	void ToggleLockOn();

	/** ロック解除(dwell計測もリセット。再ロックには改めて手動トグルが必要) */
	UFUNCTION(BlueprintCallable, Category = "MS|LockOn")
	void ClearLockOn();

	/** Called after deployment initialization. Clears outgoing warnings and stale captured targets. */
	void ResetForDeployment();
	/** Same validated path as the component Server RPC. Only authority may change accepted intent. */
	bool ApplyManualLockIntent(AActor* Candidate, int32 SpawnEpoch, uint32 RequestSequence, int32 TargetEpoch = INDEX_NONE);
	/** Server-only read of actual source state for an incoming-lock receiver. */
	bool GetIncomingThreatState(const AActor* Target, bool& bManual, bool& bMissileTracking, bool& bMissileCaptured) const;
	/** Receiver death/deployment invalidates this source's target without waiting for another tick. */
	void InvalidateIncomingTarget(AActor* Target);
	/** BOTs use controller-selected homing acquisition only; no synthetic manual lock or camera correction. */
	void ConfigureAIHomingOnly() { bAIHomingOnly = true; SoftAimInterpSpeed = 0.f; }
	bool IsAIHomingOnly() const { return bAIHomingOnly; }

	// ------------------------------------------------------------------
	// 状態参照API(誘導ミサイルGA P4-10 / マーカーUI P8-3 用)
	// ------------------------------------------------------------------

	/** 現在ロック中の対象(未ロックはnull)。ロック後は照準を外しても維持される */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	AActor* GetLockedTarget() const { return LockedTarget.Get(); }

	/** ロック中か */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	bool IsLockedOn() const { return LockedTarget.IsValid(); }

	/** Current LOS/range validity for camera assist. Red-lock occlusion grace retains the target, not aim guidance. */
	bool CanApplyManualAimAssist() const;

	/** 装備中武器の赤ロック距離とLOS内にあり、緊急回避で抑止されていないか。毎Tick再評価。 */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	bool IsRedLocked() const { return bIsRedLocked; }

	/** ロック対象が緊急回避した通知。通常ロックを維持したまま赤状態だけ解除する。 */
	UFUNCTION(BlueprintCallable, Category = "MS|LockOn")
	void NotifyTargetEmergencyDodged(AActor* DodgingTarget);

	/** 現在の最良候補(視野角内・LOS通過・スコア最高。dwell計測中の対象) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	AActor* GetCurrentCandidate() const { return CurrentCandidate.Get(); }

	/**
	 * Automatic homing-weapon capture. Separate from the right-click/manual normal lock.
	 * 実プレイ仕様: 捕捉サークル内に「捉え続けて」LockOnDwellTime経過で初めてロック完了する
	 * (円内に入った瞬間の即時ロックは廃止)。円から外れると進捗リセット/捕捉解除。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn|Homing")
	AActor* GetHomingCapturedTarget() const { return HomingCapturedTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "MS|LockOn|Homing")
	bool IsHomingTargetCaptured() const { return HomingCapturedTarget.IsValid(); }

	/** 誘導捕捉dwellの進捗 0..1(リングUIの円弧表示用。ロック完了後は1.0) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn|Homing")
	float GetHomingDwellProgress() const;
 float GetEffectiveHomingDwellSeconds() const;

	/** 現在dwell計測中の誘導候補(未ロック時のみ有効) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn|Homing")
	AActor* GetHomingCandidate() const { return HomingCandidate.Get(); }

	/** dwell進捗 0..1(ホーミングレティクルUIの充填表示用。ロック確定後は1.0) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	float GetDwellProgress() const;

	/** 現在適用中の視野角 度(通常30/スコープ中17。P3-5のスコープ状態と連動) */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	float GetActiveConeDegrees() const;

	/**
	 * 誘導捕捉サークルの円錐全角 度。
	 *
	 * タスク#220: 旧実装は LockOnConeDegrees * 2 = 60°(半角30°)固定で、HUDに描かれる
	 * 誘導カーソル円(MSHUDPalette::HomingCursorSize=480、実描画半径235スレート単位)と
	 * まったく無関係だった。1920x1080/FOV60°では半角30°は画面座標で960px相当=画面半幅ぜんぶ。
	 * つまり「円の4倍以上外側」の敵にもロックが掛かっていた(ユーザー報告の原因)。
	 *
	 * 現実装は「HUDに描かれている円の実描画半径」から毎フレーム逆算する:
	 *   円錐半角 = atan(円の描画半径px / 投影焦点距離px)
	 * これにより FOV スライダー(#18)でも解像度/アスペクト比変更でも、判定円と描画円が
	 * 常に一致する(焦点距離が FOV とビューポートから導かれ、描画半径が DPI スケールで追随するため)。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn|Homing")
	float GetHomingConeDegrees() const;

	/**
	 * 投影焦点距離(px)。UEの投影行列(FMinimalViewInfo::CalculateProjectionMatrixGivenView の
	 * XAxisMultiplier/YAxisMultiplier 分岐)と同一式。ピクセルは正方なので水平/垂直で同値になる。
	 * 画面中心から角度θ方向の点は、中心から FocalLength * tan(θ) px の位置に投影される。
	 * @param CameraAspectRatio UCameraComponent::AspectRatio(MaintainYFOV/MajorAxis縦優先時のみ使用)
	 */
	static float ComputeFocalLengthPixels(float ViewportWidthPx, float ViewportHeightPx,
		float FieldOfViewDegrees, float CameraAspectRatio,
		TEnumAsByte<EAspectRatioAxisConstraint> AxisConstraint);

	/** 画面上の半径 px に対応する円錐の全角 度(= 2 * atan(半径 / 焦点距離))。純関数。 */
	static float ComputeConeDegreesForScreenRadius(float ScreenRadiusPx, float FocalLengthPx);

	/** 円錐全角 度に対応する画面上の半径 px(ComputeConeDegreesForScreenRadius の逆関数)。純関数。 */
	static float ComputeScreenRadiusForConeDegrees(float ConeDegrees, float FocalLengthPx);

	/**
	 * ソフトエイム目標点(=ロック対象のGetLockOnAimPoint。胸元)。
	 * @return ロック中でなければ false
	 */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	bool GetSoftAimTargetLocation(FVector& OutAimPoint) const;

	/**
	 * ソフトエイムの目標視線回転。基準は「実カメラ位置」(UMSCameraComponentワールド座標)。
	 * ピボット(SpringArm根元)基準は禁止(肩越しSocketOffsetぶん照準がずれる。冒頭コメント2)。
	 * ApplySoftAim と UI の照準表示が共用する単一実装。
	 * @return ロック中かつカメラ解決可なら true
	 */
	UFUNCTION(BlueprintPure, Category = "MS|LockOn")
	bool ComputeSoftAimLookRotation(FRotator& OutRotation) const;

	/**
	 * スコア式(純関数。Unity: angleScore*0.35 + distanceScore*0.65)。
	 * angleScore = 1 - (角度差/視野半角), distanceScore = 1 - (距離/最大距離)。各0..1クランプ。
	 * 高いほど良い(距離重視のため、近距離の候補が角度差の小さい遠方候補に勝つ)。
	 */
	static float ComputeLockOnScore(float AngleDeg, float ConeDegrees, float Distance, float MaxDistance);

	/** 有限角速度のエイム補正。MaxCorrectionDegrees は 0.1 秒当たりの補正上限。 */
	static FRotator ComputeLimitedAimRotation(const FRotator& Current, const FRotator& Desired,
		float DeltaTime, float InterpSpeed, float MaxCorrectionDegrees);

	/** Both values are UE world centimeters; authored red-lock distances already include MSGO::MSScale. */
	static bool IsWithinRedLockDistance(float DistanceCm, float RedLockDistanceCm);

	/** Water volumes describe movement state, not opaque cover (#245). */
	static bool ShouldIgnoreLineOfSightOccluder(const AActor* HitActor);

	/** ロック対象が変わったとき(取得/解除/切替)に通知。P8-3マーカーUIが購読する */
	UPROPERTY(BlueprintAssignable, Category = "MS|LockOn")
	FMSLockOnTargetChangedSignature OnLockOnTargetChanged;

protected:
	/** 通常時の視野角 度(円錐の全角。Unity: lockOnConeDegrees=30f) */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float LockOnConeDegrees = 30.0f;

	/** スコープ中の視野角 度(Unity: homingReticleConeDegrees=17f。P3-5スコープ状態と連携) */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float ScopedLockOnConeDegrees = 17.0f;

	/** 誘導捕捉UIのdwell完了時間 秒。通常ロックの自動確定には使わない。 */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn", meta = (ClampMin = "0.0", Units = "s"))
	float LockOnDwellTime = 0.8f;

	/** ロックオン最大距離 cm(Unity: range=100f m。データ精度方針により近似で可) */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn", meta = (ClampMin = "100.0", Units = "cm"))
	float LockOnRange = 10000.0f * MSGO::MSScale;

	/** ソフトエイムの吸い付き補間速度(RInterpTo速度。0で吸い付き無効) */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn", meta = (ClampMin = "0.0"))
	float SoftAimInterpSpeed = 14.0f;

	/** Yellow tracking limit in degrees per 0.1 s. Provisional tuning, not a measured source-game value. */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn|RedLock", meta = (ClampMin = "0.0", Units = "deg"))
	float NormalAimCorrectionMaxDegrees = 4.0f;

	/** Red tracking limit in degrees per 0.1 s; stronger than yellow but still finite. */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn|RedLock", meta = (ClampMin = "0.0", Units = "deg"))
	float RedAimCorrectionMaxDegrees = 8.0f;

	UPROPERTY(EditAnywhere, Category = "MS|LockOn|RedLock", meta = (ClampMin = "1.0"))
	float RedAimInterpMultiplier = 1.6f;

	/** 赤ロック中だけ許す一時遮蔽の維持猶予。 */
	UPROPERTY(EditAnywhere, Category = "MS|LockOn|RedLock", meta = (ClampMin = "0.0", Units = "s"))
	float RedLockBreakGraceSeconds = 0.35f;

	/**
	 * 視点(実カメラ)の位置と前方向を解決する。
	 * 候補選定の円錐判定・スコア距離・ソフトエイム基準の全てがこの実カメラ基準(冒頭コメント2)。
	 * @return カメラ未搭載の機体(BOT等に誤搭載した場合)は false
	 */
	bool GetViewPoint(FVector& OutLocation, FVector& OutForward) const;

	/**
	 * 最良候補の選定(Unity: FindLockOnTarget / FindHomingCandidate)。
	 * WeaponTrace球Overlap収集 → CanLockOn(B-2) → 視野角(全角/2) → LOS → スコア最大。
	 */
	AActor* SelectBestCandidate(const FVector& ViewLocation, const FVector& ViewForward,
		float MaxRangeCm, float ConeDegrees) const;

	/**
	 * LOS判定(Unity: HasLineOfSight)。起点=自機胸元(自機のGetLockOnAimPoint。カメラ禁止、B-5)、
	 * 終点=対象の胸元(対象のGetLockOnAimPoint)。WeaponTraceチャンネル。自機Actorは無視リストへ。
	 * 対象自身へのヒットは遮蔽と見なさない。
	 */
	bool HasLineOfSightTo(const AActor* Target) const;

	/** LOS起点=自機胸元(IMSLockOnTargetInterface::GetLockOnAimPoint。未実装ならActor位置) */
	FVector GetOwnAimOrigin() const;

	/** ロック対象の差し替え(OnLockOnTargetChanged通知込み) */
	void SetLockedTarget(AActor* NewTarget);

	/** ソフトエイム適用(ロック中のみ。コントローラの視線を実カメラ基準の目標回転へ補間) */
	void ApplySoftAim(float DeltaTime);

	/** 装備中武器と対象距離から赤ロック状態を更新する。 */
	void UpdateRedLockState();

	/**
	 * 現在装備中の武器が誘導武器(Weapon.Category.Homing)であり、かつリロード中(ReloadRemaining > 0)か。
	 * true の間は誘導捕捉(候補選定/dwell進行/HomingCapturedTargetへの確定)および手動ロック(ToggleLockOn)
	 * を一切進行させない(「リロード完了までロック不可」という仕様。TickComponent/ToggleLockOn共用)。
	 */
	bool IsActiveWeaponHomingAndReloading() const;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetManualLockIntent(AActor* Candidate, int32 SpawnEpoch, uint32 RequestSequence, int32 TargetEpoch);
	UFUNCTION() void OnRep_ManualLockSnapshot();
	UFUNCTION() void HandleLockOwnerDeath(AActor* DeadUnit, AActor* Killer);
	UPROPERTY(ReplicatedUsing=OnRep_ManualLockSnapshot) FMSManualLockAuthoritySnapshot ManualLockSnapshot;
	void PublishManualLockSnapshot();
	void ReconcileManualLockSnapshot();
	void SubmitManualLockIntent(AActor* Candidate);
	void SyncIncomingTargets();
	void ClearPublishedIncomingTargets();
	bool IsLockOwnerAlive() const;
	bool IsManualTargetInRange(const AActor* Target) const;
	int32 GetDeploymentEpoch() const;
	static int32 GetTargetDeploymentEpoch(const AActor* Target);
	TArray<TWeakObjectPtr<AActor>> PublishedIncomingTargets;
	TWeakObjectPtr<AController> ObservedController;
	int32 ObservedDeploymentEpoch = INDEX_NONE;
	int32 ManualTargetEpoch = 0;
	int32 HomingTargetEpoch = 0;
	uint32 LocalRequestSequence = 0;
	uint32 AcceptedRequestSequence = 0;
	uint32 AppliedSnapshotRevision = 0;
	bool bEndingPlay = false;
	bool bAIHomingOnly = false;
	AActor* GetAIHomingIntentTarget() const;
	/** 自機カメラ(P3-5。スコープ状態参照+実カメラ位置。BeginPlayで解決) */
	UPROPERTY(Transient)
	TObjectPtr<UMSCameraComponent> CameraComponent;

	/** ロック中の対象(Weak: 破棄検出。ロック後は照準を外しても維持、遮蔽/死亡で解除) */
	TWeakObjectPtr<AActor> LockedTarget;

	/** dwell計測中の最良候補 */
	TWeakObjectPtr<AActor> CurrentCandidate;

	/** Updated every frame only while a homing weapon is equipped; never drives soft aim/camera. */
	TWeakObjectPtr<AActor> HomingCapturedTarget;

	/** dwell計測中の誘導候補(捕捉前)。円から外れる/候補が変わるとリセット */
	TWeakObjectPtr<AActor> HomingCandidate;

	/** 現候補への注視継続時間 秒(候補が切り替わると0へリセット) */
	float CandidateDwellTime = 0.0f;

	/** 誘導候補への注視継続時間 秒(LockOnDwellTime到達でHomingCapturedTargetへ昇格) */
	float HomingDwellTime = 0.0f;

	bool bIsRedLocked = false;
	float LockBreakElapsed = 0.0f;
	bool bRedLockOcclusionGraceActive = false;
	TWeakObjectPtr<AActor> RedLockSuppressedTarget;

	/** Only true during ToggleLockOn's synchronous selection, avoiding per-frame log spam. */
	mutable bool bLogNextSelectionDiagnostics = false;

	/** 対象が誘導捕捉サークル内(視野角・距離・LOS・CanLockOn)に留まっているか */
	bool IsWithinHomingCircle(const AActor* Target, const FVector& ViewLocation, const FVector& ViewForward,
		float MaxRangeCm, float ConeDegrees) const;
};
