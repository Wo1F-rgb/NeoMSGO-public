// Copyright NeoMSGO Project. All Rights Reserved.
//
// B2 (docs/BotDeployment_Design.md): BOT初期配備スポーンマネージャ。
// AMSGameMode に搭載され、StartPlay 後にサーバ専用で敵/味方BOTを動的スポーンする。
//
// 設計要点(docs/BotDeployment_Design.md「推奨方針」):
//  - レベル側マーカー AMSBotSpawnPoint をチーム別に列挙。マーカー0のレベルでは
//    AMSHomeBase → APlayerStart の順にフォールバック。
//  - マーカー中心の半径SpawnSearchRadius内ランダム点 → NavMesh射影 →
//    カプセルのブロッキングオーバーラップ検査 → 上空DropSpawnHeightから落下スポーン
//    (「建物の中にbotが埋まっている」への直接対策)。失敗時はリトライ→別マーカーへ
//    フォールバックし、最終未達は Warning ログ(0体でもクラッシュしない)。
//  - Tier配分は Weak60%/Medium30%/Strong10%。丸めは Strong→Medium→Weak の順に
//    最低1体保証(12体なら Strong1 / Medium4 / Weak7)。
//  - 全BOTは InitializeUnit(BotMobileSuitData) を単一入口として初期化する(B-10)。
//  - 再配備は既存 UMSRespawnComponent の自動リスポーン(3s/9s)に委譲する。
//    前線追従リスポーン・人数維持の周期チェックは B3 で拡張する。

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Components/ActorComponent.h"
#include "AI/MSBotAITypes.h"
#include "GenericTeamAgentInterface.h"
#include "MSBotSpawnManagerComponent.generated.h"

class AMSBotCharacter;
class UMSMobileSuitData;

/**
 * #209: Per-team placement telemetry. The Daibao shortfall (味方3/12・敵8/12、直近では0/12) was
 * reported only as "NavMesh射影またはオーバーラップ検査の失敗" — one message for two very
 * different causes. Counting them separately makes the actual cause measurable from one run.
 */
struct FMSBotPlacementDiagnostics
{
	int32 NavProjectionSuccesses = 0;
	int32 NavProjectionFailures = 0;
	int32 GroundTraceFallbacks = 0;
	int32 GroundResolveFailures = 0;
	int32 MarkerLayerRejections = 0;
	int32 OverlapRejections = 0;
	int32 HumanDropRejections = 0;
	int32 UnverifiedPlacements = 0;
	FString FirstGroundHitActor;
	float FirstGroundHitZ = 0.0f;
	float FirstCandidateZ = 0.0f;
};

/** Floor layer and capsule footprint reserved for a human still descending. */
struct FMSProtectedHumanDropColumn
{
	FVector GroundLocation = FVector::ZeroVector;
	float CapsuleRadius = 0.0f;
};

/** Server-only initial BOT deployment manager mounted on AMSGameMode (B2). */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSBotSpawnManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSBotSpawnManagerComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** AMSGameMode::StartPlay から呼ばれる初期配備の単一入口(多重呼び出し安全) */
	void SpawnInitialBots();

	/**
	 * Tier配分(Weak60%/Medium30%/Strong10%)。丸めは Strong→Medium→Weak の順に
	 * 最低1体保証: Strong/Medium は round(比率×総数) を [1, 残数] にクランプし、
	 * 残り全てが Weak。総数12なら Strong1 / Medium4 / Weak7。
	 */
	static void ComputeTierCounts(int32 TotalCount, int32& OutWeak, int32& OutMedium, int32& OutStrong);

	/** ComputeTierCountsの結果を Strong→Medium→Weak の順で並べた配列にする */
	static TArray<EMSEnemyTier> BuildTierAssignments(int32 TotalCount);
	/** Stable, shuffled 60/30/10 room roster. Old fixed-difficulty rooms do not use this. */
	static TArray<EMSEnemyTier> BuildMixedTierAssignments(int32 TotalCount, int32 Seed);
	/** Refill only missing tier seats after a player joins/leaves or placement is retried. */
	static TArray<EMSEnemyTier> BuildMissingMixedTierAssignments(int32 DesiredCount,
		const TArray<EMSEnemyTier>& Existing, int32 Seed);

	UFUNCTION(BlueprintPure, Category = "MS|Spawn")
	bool IsSpawningEnabled() const { return bSpawnBots; }

	UFUNCTION(BlueprintCallable, Category = "MS|Spawn")
	void SetSpawnBotsEnabled(const bool bEnabled) { bSpawnBots = bEnabled; }

	int32 GetAllyBotCount() const { return AllyBotCount; }
	int32 GetEnemyBotCount() const { return EnemyBotCount; }
	float GetSpawnSearchRadius() const { return SpawnSearchRadius; }
	/** Narrows fixture candidate scatter without changing authored level markers. */
	void SetSpawnSearchRadiusForTests(float Radius) { SpawnSearchRadius = FMath::Max(0.0f, Radius); }
	/** Taigan markers author a specific standing floor layer; their Z is a capsule centre. */
	static bool IsTaiganMarkerFloorLayer(float MarkerCenterZ, float GroundZ,
		float CapsuleHalfHeight);
	static float GetTaiganSpawnScatterRadius(float ConfiguredRadius);
	/** Reject same-floor BOT landings whose capsules could push a descending human. */
	static bool ConflictsWithHumanDropColumn(const FVector& CandidateGround, float BotCapsuleRadius,
		const FMSProtectedHumanDropColumn& HumanDrop);
	float GetDropSpawnHeight() const { return DropSpawnHeight; }
	void SetTeamBotCounts(int32 InAllyBotCount, int32 InEnemyBotCount);
	/** Manual rooms keep enemy-only counts; filled rooms reserve 25 seats per team. */
	void SetRoomBotConfiguration(int32 Count, EMSEnemyTier Difficulty, bool bFillTeams = false,
		bool bMixedDifficulty = false);
	/** Counts connected human seats even while their pawn is dead or awaiting sortie. */
	UFUNCTION(BlueprintCallable, Category = "MS|Spawn")
	void RefreshTeamPopulation();
	UFUNCTION(BlueprintPure, Category = "MS|Spawn")
	bool IsTeamFillEnabled() const { return bFillTeamsWithBots; }

	/** テスト/デバッグ用: ソフト参照アセットの代わりに使う機体データを注入する */
	void SetBotMobileSuitDataOverride(UMSMobileSuitData* Data) { BotMobileSuitDataOverride = Data; }

	/** このマネージャがスポーンしたBOT(弱参照。B3の人数維持チェックの土台) */
	const TArray<TWeakObjectPtr<AMSBotCharacter>>& GetSpawnedBots() const { return SpawnedBots; }

	int32 CountSpawnedBots(FGenericTeamId TeamId) const;

protected:
	/** falseにするとBOTスポーンを一切行わない(B2受け入れ条件) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn")
	bool bSpawnBots = true;

	/** 味方チーム初期配備数(既定12体。docs/BotDeployment_Design.md パラメータ表) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "0"))
	int32 AllyBotCount = 12;

	/** 敵チーム初期配備数(既定12体) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "0"))
	int32 EnemyBotCount = 12;

	/** スポーン探索半径(マーカー中心のランダム散布範囲。既定1500cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnSearchRadius = 1500.0f * MSGO::MSScale;

	/** 1スポーン地点あたりのNavMesh射影+オーバーラップ検査リトライ回数(既定5回) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "1"))
	int32 MaxPlacementAttemptsPerPoint = 5;

	/** 上空落下スポーン高さ(UMSRespawnComponentのドロップスポーン方式と同値。既定800cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float DropSpawnHeight = 800.0f * MSGO::MSScale;

	/**
	 * 地表探索トレースの上下探索距離。マーカーZが地表から大きく外れていても地形を捉えるため
	 * DropSpawnHeightとは独立に長く取る(#209: Daibaoの拠点Zは地形から数百m離れていた)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundProbeHeight = 500000.0f;

	/** 動的スポーンBOTへ InitializeUnit で適用する機体データ(Armor/武器の供給元) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Spawn")
	TSoftObjectPtr<UMSMobileSuitData> BotMobileSuitData;

private:
	/** チーム別のスポーン中心候補。マーカー → AMSHomeBase → APlayerStart の順でフォールバック */
	TArray<FVector> GatherSpawnCenters(FGenericTeamId TeamId) const;

	/**
	 * 1中心に対する配置解決: 半径内ランダム点 → NavMesh射影(NavDataがある場合のみ)→
	 * カプセルのブロッキングオーバーラップ検査(ECC_Pawn)。失敗はリトライ。
	 */
	bool ResolveSpawnLocation(const FVector& Center, FVector& OutGroundLocation,
		FMSBotPlacementDiagnostics& Diagnostics,
		const TArray<FMSProtectedHumanDropColumn>& HumanDrops,
		bool bAllowUnverifiedPlacement = false) const;
	TArray<FMSProtectedHumanDropColumn> GatherFallingHumanDropColumns() const;

	/**
	 * #209: NavMesh射影に依存しない地表解決。Daibaoの5kmRecastグラフは試合開始フレームでは
	 * まだ生成中で ProjectPointToNavigation が全候補で失敗するため、BOTが1体も配備されない。
	 * NavMeshがあればそれを優先し、無い/未生成なら下向きトレースで地表を求める。
	 */
	bool ResolveGroundLocation(const FVector& Candidate, FVector& OutGroundLocation,
		FMSBotPlacementDiagnostics& Diagnostics) const;

	/** Tier割当済みのBOTを1体スポーンし、チーム設定+InitializeUnitまで行う */
	AMSBotCharacter* SpawnSingleBot(FGenericTeamId TeamId, EMSEnemyTier Tier,
		const FVector& GroundLocation, UMSMobileSuitData* SuitData);

	/** 要求数ぶんスポーンし、実際にスポーンできた数を返す */
	int32 SpawnTeamBots(FGenericTeamId TeamId, int32 Count, UMSMobileSuitData* SuitData,
		const TArray<EMSEnemyTier>* TierOverride = nullptr);

	UMSMobileSuitData* ResolveBotMobileSuitData() const;

	/** テスト注入用の機体データ(nullならBotMobileSuitDataを同期ロード) */
	UPROPERTY(Transient)
	TObjectPtr<UMSMobileSuitData> BotMobileSuitDataOverride;

	TArray<TWeakObjectPtr<AMSBotCharacter>> SpawnedBots;
	bool bInitialSpawnDone = false;
	bool bUseFixedRoomDifficulty = false;
	bool bUseMixedRoomDifficulty = false;
	bool bFillTeamsWithBots = false;
	bool bWarnedMissingFillSuit = false;
	FTimerHandle TeamPopulationTimer;
	int32 CountHumanSeats(FGenericTeamId TeamId) const;
	void ReconcileTeam(FGenericTeamId TeamId, int32 DesiredBots, UMSMobileSuitData* SuitData);
	EMSEnemyTier FixedRoomDifficulty = EMSEnemyTier::Medium;

	/** 次にスポーンするBOTへ割り当てるダミーパイロット名の連番(「ガンオンN」— タスク#145) */
	int32 NextDummyNameIndex = 1;
};
