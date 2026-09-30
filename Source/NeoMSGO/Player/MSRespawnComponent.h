// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-7: プレイヤーリスポーン/Deploy。
// 設計元: docs/UE5_MIGRATION_PLAN.md P3-7 / docs/ArchitectureMap.md §26
//         ← Unity: Assets/Scripts/Player/PlayerRespawnController.cs
//
// Unity版仕様の踏襲:
//  - respawnDelay   = 10s (再出撃までの待機秒数。OnDeath購読で起動)
//  - explosionDelay = 5s  (撃破から爆発演出までの遅延。実FXはP5/P7で接続、
//                          本タスクはタイマー+OnExplosionDueデリゲート発火の土台のみ)
//  - dropSpawnHeight = 8m (スポーン地点の上空8mへテレポートし、重力降下で着地させる。
//                          固定Y座標スポーンによる地下埋没バグの再発防止。ガードレールB-11:
//                          「スポーン高さはハードコードせず上空降下方式」— 高さはプロパティで調整可能)
//  - Deploy(preset, spawnOverride): プリセット適用(=InitializeUnit)+位置設定+全快
//
// ガードレールB-11「目標へ移動系は目標を1回だけ確定」の適用:
//  - 死亡時: リスポーン目標TransformをHandleOwnerDeath時点で1回だけ確定(PendingSpawnTransform)。
//    タイマー満了時に再解決しない(待機中にSetDefaultSpawnPointが変わっても目標はブレない)。
//  - Deploy: 呼び出し時点の引数で目標を確定し、同一Deploy処理内で再解決しない。
//    再入(InitializeUnit副作用等からの多重呼び出し)はbDeployInProgressで拒否する。
//
// 責務境界:
//  - Attribute初期化/State.Dead除去は AMSUnitBase::InitializeUnit(単一入口)に委譲。
//    本コンポーネントはタイマー駆動とテレポート(上空降下)のみを担う。
//  - 出撃可否のUIゲート(機体選択画面/円卓)はP8-6/P8-8の責務。CanDeploy()を公開するのみで
//    Deploy自体はゲートしない(初回出撃・デバッグ出撃にも同一APIを使うため)。

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Components/ActorComponent.h"
#include "MSRespawnComponent.generated.h"

class AMSUnitBase;
class AActor;
class UWorld;
class UMSMobileSuitData;
class UMSDeckSubsystem;
struct FMSDeckLoadout;

/**
 * 爆発演出タイミング通知(死亡からExplosionDelay経過時に1回)。
 * @param Unit          撃破されたユニット
 * @param DeathLocation 死亡時点の位置(演出はリスポーン後の現在位置でなくここで再生する)
 * 実際のNiagara爆発FX(Cue.Explosion)はP5/P7でこのデリゲートに接続する。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnExplosionDue, AMSUnitBase*, Unit, FVector, DeathLocation);

/** Deploy/リスポーン完了通知(テレポート直後、降下開始時点)。HUD復帰・カメラ復帰(P8-1)が購読する */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnDeployed, AMSUnitBase*, Unit);

/** Manual-respawn delay elapsed; UI can re-evaluate CanDeploy without relying on widget ticking. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnRespawnReady, AMSUnitBase*, Unit);

USTRUCT()
struct FMSOnlineRespawnSnapshot
{
	GENERATED_BODY()
	UPROPERTY() bool bReady = true;
	UPROPERTY() bool bPending = false;
	UPROPERTY() bool bAlive = false;
	/** True when the owner completed an authoritative emergency recall. */
	UPROPERTY() bool bRecallAwaitingDeploy = false;
	UPROPERTY() double ReadyAt = 0;
	UPROPERTY() TArray<double> SlotReadyAt;
	UPROPERTY() int32 DeployRevision = 0;
	FMSOnlineRespawnSnapshot() { SlotReadyAt.SetNumZeroed(4); }
};

/**
 * リスポーン/Deployコンポーネント(Unity: PlayerRespawnController)。
 * AMSPlayerCharacter に搭載する(AMSUnitBase系なら搭載可能な作り)。
 * BOTの自動リスポーン(P6-1/P6-7)は別経路(AIController側)だが、
 * どちらも InitializeUnit を単一入口として通る(ガードレールB-10)。
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSRespawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSRespawnComponent();
	const FMSOnlineRespawnSnapshot& GetOnlineRespawnSnapshot() const { return OnlineRespawn; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	/** Only AMSPlayerController's validated authority path may invoke this entry. */
	bool DeployVerifiedOnlineSlot(int32 SlotIndex, UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout, const FTransform& Spawn);
	/** Marks a completed server-validated recall as awaiting a new sortie. */
	void BeginOnlineRecall();
	bool IsOnlineRecallAwaitingDeploy() const { return bOnlineRecallAwaitingDeploy; }

	/**
	 * 出撃/再出撃(Unity: PlayerRespawnController.Deploy)。
	 *  1. 目標Transformを呼び出し時点で1回だけ確定(B-11。以降の処理で再解決しない)
	 *  2. 進行中の死亡フロー(爆発/リスポーンタイマー)をキャンセル
	 *  3. InitializeUnit(Preset)で機体切替+State.Dead除去+Attribute全快
	 *  4. 目標の上空 DropSpawnHeight へテレポート → 重力降下(地下埋没防止)
	 * @param Preset        出撃機体データ。null なら現在の機体データのまま再初期化
	 * @param SpawnOverride 出撃地点(円卓UI等が指定。位置+Yaw回転を使用)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Respawn")
	void Deploy(UMSMobileSuitData* Preset, FTransform SpawnOverride);

	/** Player sortie path: applies the selected slot's committed weapons, preserving catalog identity. */
	UFUNCTION(BlueprintCallable, Category = "MS|Respawn")
	bool DeployDeckSlot(UMSDeckSubsystem* Deck, int32 SlotIndex, FTransform SpawnOverride);

	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	float GetDeckSlotTimeRemaining(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	bool CanDeployDeckSlot(int32 SlotIndex) const;

	/** Leaves the disabled suit now; per-suit cooldown still applies. */
	UFUNCTION(BlueprintCallable, Category = "MS|Respawn")
	bool RequestEmergencyEject();

	/** 既定スポーン地点(BeginPlay時の初期配置、またはSetDefaultSpawnPointの設定値)へのDeploy */
	UFUNCTION(BlueprintCallable, Category = "MS|Respawn")
	void DeployAtDefaultSpawn(UMSMobileSuitData* Preset);

	/**
	 * 既定スポーン地点を差し替える(拠点占領による出撃地点変更等。P8-6)。
	 * 死亡待機中に呼ばれても、確定済みのリスポーン目標(PendingSpawnTransform)は変更しない(B-11)。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Respawn")
	void SetDefaultSpawnPoint(const FTransform& InTransform);
	/** The stored ground-layer sortie target, before drop height is applied. */
	const FTransform& GetDefaultSpawnPoint() const { return DefaultSpawnTransform; }

	/** 死亡→リスポーン待機中か(Unity: IsDead 相当。リスポーンタイマー稼働中) */
	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	bool IsRespawnPending() const;

	/** リスポーンまでの残り秒数(Unity: RespawnCountdown。待機中でなければ0) */
	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	float GetRespawnTimeRemaining() const;

	/** 再出撃可能か(死亡待機満了、またはサーバー検証済み帰投完了)。UIゲート用(P8-8) */
	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	bool CanDeploy() const { return bRespawnReady; }

	/** Legacy/AI policy switch. Player defaults to the P8-8 manual sortie flow. */
	void SetAutoRespawnEnabled(bool bEnabled) { bAutoRespawn = bEnabled; }

	/** Ownerごとの死亡演出/復帰タイミングを設定する。BOTはUnity版の3s/9sを使用する。 */
	void SetRespawnTimings(float InExplosionDelay, float InRespawnDelay)
	{
		ExplosionDelay = FMath::Max(0.0f, InExplosionDelay);
		RespawnDelay = FMath::Max(0.0f, InRespawnDelay);
	}
	float GetExplosionDelay() const { return ExplosionDelay; }
	float GetRespawnDelay() const { return RespawnDelay; }
	bool IsAutoRespawnEnabled() const { return bAutoRespawn; }

	/** 今回の死亡フローで爆発通知(OnExplosionDue)を発火済みか(演出の多重再生防止・テスト観測用) */
	UFUNCTION(BlueprintPure, Category = "MS|Respawn")
	bool HasExplosionFired() const { return bExplosionFired; }

	/** 爆発演出タイミング通知(死亡からExplosionDelay=5s) */
	UPROPERTY(BlueprintAssignable, Category = "MS|Respawn")
	FMSOnExplosionDue OnExplosionDue;

	/** Deploy/リスポーン完了通知(上空テレポート直後) */
	UPROPERTY(BlueprintAssignable, Category = "MS|Respawn")
	FMSOnDeployed OnDeployed;

	/** Fired after CanDeploy changes to true for a manual respawn. */
	UPROPERTY(BlueprintAssignable, Category = "MS|Respawn")
	FMSOnRespawnReady OnRespawnReady;

protected:
	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/** 再出撃までの待機秒数(Unity: respawnDelay=10f) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 10.0f;

	/** 撃破から爆発演出までの遅延秒数(Unity: explosionDelay=5f) */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float ExplosionDelay = 5.0f;

	/**
	 * スポーン地点の上空何cmから重力降下を開始するか。
	 * 固定Z座標テレポートによる地下埋没バグの再発防止(B-11)。
	 *
	 * 旧値は 800cm * MSScale = 約74.6m だったが、ダイバオの拠点ランドマーク
	 * (ビッグサイト型/立像広場、高さ約81m)より低く、構造物の内部に湧いて
	 * 埋まる不具合が発生した。拠点構造物の数倍の高度から降下させるため引き上げる。
	 * 3200cm * MSScale = 約298m(構造物比 約3.7倍)。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Respawn", meta = (ClampMin = "0.0", Units = "cm"))
	float DropSpawnHeight = 3200.0f * MSGO::MSScale;

	/** Daibaoの約164,000cmの高度ずれを覆う、地表トレースの上下各探索距離。 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Respawn", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundProbeHeight = 500000.0f;

public:
	/** 降下開始高度(cm)。テストが実値を参照できるよう公開する。 */
	float GetDropSpawnHeight() const { return DropSpawnHeight; }

	/** Shared Taigan underground placement: require cover and a clear capsule below it. */
	static bool TryResolveCoveredDropLocation(UWorld* World, const FVector& GroundLocation,
		float CapsuleRadius, float CapsuleHalfHeight, float DesiredDropHeight,
		const AActor* IgnoredActor, FVector& OutDropLocation);

private:
 friend class FMSBattleAssistLifecycleConsumers;
	UFUNCTION(Server, Reliable) void ServerEmergencyEject();
	UFUNCTION() void OnRep_OnlineRespawn();
	void PublishOnlineRespawn();
	double GetSynchronizedTime() const;
	UPROPERTY(ReplicatedUsing=OnRep_OnlineRespawn) FMSOnlineRespawnSnapshot OnlineRespawn;
	int32 ObservedDeployRevision = 0;
	/** Server-only source state for OnlineRespawn.bAlive during a completed recall. */
	bool bOnlineRecallAwaitingDeploy = false;

	/**
	 * RespawnDelay満了時に自動でリスポーンするか。
	 * 再出撃UI(P8-8: 機体選択→円卓)実装後はfalseにし、UIがCanDeploy()を見てDeployを呼ぶ。
	 * それまでは満了時に現在機体のまま確定済み目標へ自動リスポーンする。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Respawn")
	bool bAutoRespawn = false;

private:
	/** OnDeath購読ハンドラ。リスポーン目標をこの時点で1回だけ確定し、両タイマーを起動する */
	UFUNCTION()
	void HandleOwnerDeath(AActor* DeadUnit, AActor* Killer);

	/** ExplosionDelay満了: OnExplosionDue発火(実FXはP5/P7で接続) */
	void HandleExplosionTimer();

	/** RespawnDelay満了: bRespawnReady=true。bAutoRespawnなら確定済み目標へExecuteDeploy */
	void HandleRespawnTimer();

	/**
	 * Deploy実処理。既定マップでは確定済み目標を保持(B-11)。Taiganの地下本拠点のみ
	 * 明示したDeploymentSpawnAnchorへ写し、床/天井/カプセルを検証してから出撃する。
	 */
	bool ExecuteDeploy(UMSMobileSuitData* Preset, const FTransform& ConfirmedTarget,
		bool bConsumeDeathRespawnReadiness, const FMSDeckLoadout* Loadout = nullptr);

	/** 既定はNavMesh優先/長距離trace。Taigan地下は候補直下の床だけを採用。 */
	FVector ResolveGroundLocation(const FVector& Candidate, FString& OutResolveMethod,
		FString& OutHitActor) const;

	/** 死亡フローのタイマーを全消去する(Deploy成立時・EndPlay)。出撃可否は呼び出し元が管理する。 */
	void ClearDeathFlow();

	/** Owner(AMSUnitBase)取得。搭載先が不正ならnullptr+エラーログ */
	AMSUnitBase* GetOwnerUnit() const;

	/** 既定スポーン地点(BeginPlayで初期配置をキャプチャ。SetDefaultSpawnPointで差し替え可) */
	FTransform DefaultSpawnTransform = FTransform::Identity;

	/** 死亡時に1回だけ確定したリスポーン目標(B-11。タイマー満了時はこの値のみを使う) */
	FTransform PendingSpawnTransform = FTransform::Identity;

	/** 死亡時点の位置(爆発演出用。リスポーン後に発火しても死亡地点で再生できるよう保持) */
	FVector DeathLocation = FVector::ZeroVector;

	FTimerHandle ExplosionTimerHandle;
	FTimerHandle RespawnTimerHandle;

	/**
	 * RespawnDelay経過済み(CanDeploy)。Deploy成立でリセット。
	 * 既定でtrue: 一度も死亡していない初回出撃はrespawnDelay待ちなしで即座に可能でなければならない
	 * (P8-8機体選択画面からの初回出撃が永久にブロックされる回帰を防ぐ)。
	 */
	bool bRespawnReady = true;

	/** 今回の死亡フローで爆発通知を発火済みか */
	bool bExplosionFired = false;

	/** Deploy再入ガード(InitializeUnit副作用からの多重呼び出しで目標がブレるのを防ぐ — B-11) */
	bool bDeployInProgress = false;
	int32 ActiveDeckSlotIndex = INDEX_NONE;
	double DeckSlotReadyAt[4] = {};
};
