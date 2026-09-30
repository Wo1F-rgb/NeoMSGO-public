// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-4: 爆風ダメージユーティリティ。
// 設計元: docs/UE5_Architecture.md §1.6「爆風ユーティリティ」/ §1.7(トレース単一入口 B-3)
//         ← Unity: CombatUtility.ApplySplashDamage / IsHomeBaseCoreOccluded
//
// 責務境界(B-2/B-3):
//  - FF(フレンドリーファイア)判定はここでは行わない。GE_SplashDamage → UMSDamageExecution
//    手順1の UMSTeamStatics::CanDamage が単一実装(重複実装禁止)。
//    本ユーティリティは範囲内の全ダメージ対象へGEを適用し、SetByCaller
//    Data.Damage / Data.SplashFalloff を書き込むだけ。
//  - 遮蔽判定・二重適用防止・falloff計算(空間クエリに属するもの)は本クラスの責務。
//
// 数値仕様(爆風チューニング):
//  - 半径内側15%は1.0、その外側はMinFalloffまで直線減衰、半径外は0
//  - 同一アクターへは1回だけ適用(複数コライダー(カプセル+HeadHitbox等)対策。Unity: HashSet<Health>)
//  - 本拠点コア(MSCoreプロファイル)は「爆心→コア中心(+150cm上方)」のWeaponTraceで
//    遮蔽物があれば除外(壁越し爆風防止。Unity: IsHomeBaseCoreOccluded)
//  - 直撃対象も範囲内なら爆風を追加で受ける(直撃+爆風加算がデフォルト。Unity版仕様)。
//    直撃側経路と重ねたくない呼び出し元のために DirectHitActor 除外オプションを用意。

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "MSCombatStatics.generated.h"

class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSHitscanResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MS|Combat")
	bool bBlockingHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "MS|Combat")
	bool bCanDamage = false;

	UPROPERTY(BlueprintReadOnly, Category = "MS|Combat")
	bool bIsHeadshot = false;

	UPROPERTY(BlueprintReadOnly, Category = "MS|Combat")
	FVector EndPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MS|Combat")
	FHitResult Hit;
};

/**
 * コリジョン定数(P0-5 Config/DefaultEngine.ini との対応を1箇所に固定)。
 * 全武器トレース(即着/照射/格闘Sweep/爆風Overlap/HS判定/ロックオンLOS)はこの定数を使うこと(B-3)。
 */
namespace MSCollision
{
	/** WeaponTrace カスタムトレースチャンネル(DefaultEngine.ini: ECC_GameTraceChannel1) */
	constexpr ECollisionChannel WeaponTraceChannel = ECC_GameTraceChannel1;

	/** 本拠点コアのコリジョンプロファイル名(P0-5: WeaponTrace=Block, Camera=Ignore) */
	inline const FName CoreProfileName(TEXT("MSCore"));

	/** 本拠点建屋/ビル群のコリジョンプロファイル名(P0-5: WeaponTrace+Camera=Block) */
	inline const FName StructureProfileName(TEXT("MSStructure"));
}

UCLASS()
class NEOMSGO_API UMSCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Shared WeaponTrace hitscan used by every instant-hit weapon category.
	 * Shooter is ignored at actor level, which excludes its capsule, hit boxes and every child component.
	 * A friendly actor is still returned as the blocking hit but bCanDamage is false.
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat", meta = (WorldContext = "WorldContextObject"))
	static FMSHitscanResult ResolveHitscan(
		const UObject* WorldContextObject,
		const FVector& Origin,
		const FVector& Direction,
		float Range,
		AActor* Shooter);

	/**
	 * WeaponTraceの遮蔽物としてだけ無視するvolume判定。
	 * 水以外のAPhysicsVolume、構造物、機体、味方は遮蔽物として扱う。
	 */
	static bool ShouldIgnoreWeaponTraceOccluder(const AActor* HitActor);

	// ------------------------------------------------------------------
	// 計算定数(Unity版デフォルト値。B-9のDataAsset化対象外のシステム定数)
	// ------------------------------------------------------------------

	/** 通常爆風の下限倍率。武器固有の旧プロファイルは呼び出し側から明示する。 */
	static constexpr float SplashMinFalloff = 0.1f;

	/** 爆心近傍で全量を与える半径比率。武器固有値は呼び出し側から明示する。 */
	static constexpr float SplashFullDamageRadiusRatio = 0.15f;

	/** コア遮蔽判定のトレース終点高さオフセット(Unity: target + Vector3.up * 1.5f = 150cm) */
	static constexpr float CoreOcclusionAimHeightOffset = 150.0f * MSGO::MSScale;

	// ------------------------------------------------------------------
	// P2-4 API
	// ------------------------------------------------------------------

	/**
	 * 爆風falloff倍率。半径内側のFullDamageRadiusRatioは1.0、その外側は
	 * MinFalloffまで直線減衰し、半径外は0。通常値は下限0.1、内側15%。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Combat")
	static float CalcSplashFalloff(
		float Distance,
		float Radius,
		float MinFalloff = 0.1f,
		float FullDamageRadiusRatio = 0.15f);

	/**
	 * 爆風ダメージ適用(Unity: CombatUtility.ApplySplashDamage)。
	 * Origin中心・半径RadiusのSphereOverlap(WeaponTraceチャンネル)で対象を収集し、
	 * ASC保持アクターごとに1回だけ UGE_SplashDamage を適用する。
	 *
	 *  - SetByCaller Data.Damage = BaseDamage(乗算済み基礎威力。チャージ倍率等は呼び出し側で確定)
	 *  - SetByCaller Data.SplashFalloff = CalcSplashFalloff(対象コリジョン表面までの距離, Radius)
	 *  - SetByCaller Data.DownPoint = DownPoint * DP用falloff(外周0、0以下なら未設定。距離減衰はここで一度だけ適用)
	 *  - Context: Instigator=InstigatorActor, AttackerLocation=Origin(盾正面判定/被弾方向通知は爆心基準)
	 *
	 * FF除外は行わない(GE側 UMSDamageExecution 手順1が単一実装。B-2)。
	 * MSCoreプロファイルのコンポーネントを持つ対象は IsHomeBaseCoreOccluded で遮蔽時に除外。
	 *
	 * @param IgnoreActors           収集から除外(発射体Actor自身など。Instigatorは常に除外)
	 * @param InstigatorActor        攻撃者(ContextのInstigator。FF判定・キル帰属に使用)
	 * @param DownPoint              爆風で加算するDP(Unity版の爆風はDP無しのため既定0)
	 * @param DirectHitActor         直撃対象(bExcludeDirectHitActor=true のとき除外)
	 * @param bExcludeDirectHitActor true=直撃対象を爆風から除外。false=確認済み直撃対象も爆風候補へ補完
	 * @param MinFalloff             半径到達時の倍率。通常0.1、旧煙プロファイル等は呼び出し側で明示
	 * @param FullDamageRadiusRatio  爆心からこの半径比率までは全量(通常0.15)
	 * @return GEを適用した対象数(FFでダメージ0になった適用も数に含む)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "IgnoreActors"))
	static int32 ApplySplashDamage(
		const UObject* WorldContextObject,
		const FVector& Origin,
		float Radius,
		float BaseDamage,
		const TArray<AActor*>& IgnoreActors,
		AActor* InstigatorActor,
		float DownPoint = 0.0f,
		AActor* DirectHitActor = nullptr,
		bool bExcludeDirectHitActor = false,
		float MinFalloff = 0.1f,
		float FullDamageRadiusRatio = 0.15f, float ShieldDamageScale = -1.f,
		float DirectHitDownPointMultiplier = 1.0f);

	/**
	 * 本拠点コアへの視線遮蔽判定(Unity: CombatUtility.IsHomeBaseCoreOccluded)。
	 * TraceStart(爆心/射点)→ CoreLocation + 150cm上方 への WeaponTrace LineTrace で、
	 * コア自身(CoreActor)以外のブロッカー(MSStructure壁/建屋・地形・機体)があれば true。
	 * OverlapSphere/爆風は壁を貫通するため、壁越しのコア攻撃防止に必ず本判定を通すこと。
	 *
	 * @param TraceStart   爆心または射点
	 * @param CoreLocation コアActorの基準位置(内部で+150cm上方を終点にする)
	 * @param CoreActor    コア自身(トレース無視リストへ。null可)
	 * @param IgnoreActors 追加の無視対象(攻撃者自身など)
	 * @return 遮蔽されていれば true(=ダメージを与えてはならない)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "IgnoreActors"))
	static bool IsHomeBaseCoreOccluded(
		const UObject* WorldContextObject,
		const FVector& TraceStart,
		const FVector& CoreLocation,
		const AActor* CoreActor,
		const TArray<AActor*>& IgnoreActors);

	// ------------------------------------------------------------------
	// P2-5 API — ヘッドショット判定
	// 設計元: docs/ArchitectureMap.md L84-89 (CombatUtility.ResolveHitscan特殊ロジック)
	//         ← Unity: HeadHitbox.cs(空マーカー)+ ResolveHitscan側のHS判定
	//
	// Unity版仕様: Physics.RaycastAll は同一トレース線上の全コライダーを距離昇順で返すため、
	// 「最も近いヒット(=命中対象として採用したHealth)が胴体コライダーでも、同じHealth配下の
	// ヒット候補群の中にHeadHitboxが含まれていればHS扱い」というやや特殊な判定になる
	// (胴体の大コライダーが頭部を覆っていて最近傍ヒットが常に頭部とは限らないため)。
	// UE5側は単発トレース(LineTraceSingle)を用いる想定(B-3: 全武器共通トレース)のため、
	// 命中コンポーネントそのものがHeadHitboxかだけでなく、命中アクター配下のHeadHitbox球が
	// トレース線分(TraceStart→TraceEnd)と交差しているか(=RaycastAllなら候補に挙がっていたか)
	// を追加チェックすることで同じ挙動を再現する。P4-2(共通ヒットスキャン)から呼ばれる想定。
	// ------------------------------------------------------------------

	/**
	 * トレース命中結果からヘッドショット判定を行う(Unity: ResolveHitscanのHS特殊ロジック)。
	 *
	 *  1. 命中コンポーネント自体が AMSUnitBase::HeadHitboxComponentName ならHS確定。
	 *  2. そうでなくても、命中アクター配下に HeadHitbox コンポーネント(USphereComponent)が
	 *     存在し、そのワールド球がトレース線分(TraceStart→TraceEnd)と交差していればHS扱い
	 *     (胴体コライダー越しでも頭部候補があればHS、のUnity版仕様を再現)。
	 *  3. 命中アクターにHeadHitboxが存在しない(機体以外に命中した等)場合は false。
	 *
	 * @param Hit        トレースの命中結果(対象アクター/命中コンポーネントの特定に使用)
	 * @param TraceStart トレース始点(HeadHitbox球との交差判定に使用)
	 * @param TraceEnd   トレース終点(HeadHitbox球との交差判定に使用)
	 * @return HS扱いなら true
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat")
	static bool IsHeadshotHit(const FHitResult& Hit, const FVector& TraceStart, const FVector& TraceEnd);

	// ------------------------------------------------------------------
	// P3-2 API — ブースト即時消費+強制オーバーヒート
	// 設計元: docs/UE5_Architecture.md §1.2「強制OH API」← Unity: PlayerController.ConsumeBoost
	// (#150: 残量不足なら残り全部消費+強制オーバーヒート。0クランプのみだった旧仕様からの変更)
	// ------------------------------------------------------------------

	/**
	 * ブーストの即時消費(Unity: PlayerController.ConsumeBoost。ダッシュ格闘P4-7用の一元API)。
	 * GE_BoostConsumeで全額(-Cost)を請求する。残量不足時はAttributeSetの0クランプにより
	 * 「残り全消費」となり、0到達でUMSAttributeSet_Boostが強制OH(GE_Overheat+State.Overheated)
	 * を発動する。残量ちょうど=Costでも0到達のためOHする(Unity: currentBoost<=0 規則)。
	 * 消費自体は常に実行される(攻撃は成立する。Unity版と同じ)。
	 * 注意: しゃがみ/よろけ中の発動可否は呼び出し側(GA_MeleeのActivationBlockedTags/
	 * IsBoostDashing条件)の責務。本APIは無条件に消費する(Unity版ConsumeBoostと同じ)。
	 *
	 * @param ASC   対象ユニットのASC(UMSAttributeSet_Boost保持を想定)
	 * @param Cost  消費量(D格=30。0以下は何もしない)
	 * @return true=OHせず消費完了 / false=残量不足で全消費+強制OH発動
	 *         (BoostSet未保持のASCは消費対象なしとしてtrue)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Combat")
	static bool ConsumeBoostOrOverheat(UAbilitySystemComponent* ASC, float Cost);

	// ------------------------------------------------------------------
	// 継続射撃(バースト/照射/チャージ)向け: 発射時点の起点・方向の再取得
	// 実プレイ報告バグ対策: 開始時に保存したOrigin/Directionを使い回すと、連射中に機体が
	// 移動・旋回しても弾が「開始地点」から出続ける。毎発の発射時に本APIで再取得すること。
	// ------------------------------------------------------------------

	/**
	 * 現在の発射起点・方向・可視起点(銃口)を解決する。
	 *  - UMSCameraComponent搭載機(プレイヤー): トレース起点=現在のカメラ位置/方向=現在のカメラ前方
	 *  - 非搭載機(BOT等): 開始時の機体Transform基準の相対オフセット・相対方向を現在Transformへ再適用
	 *    (BOTが開始時に指定した「目標方向」の相対関係を保ったまま機体へ追従する)
	 *  - 可視起点: UMSWeaponVisualComponent搭載なら現在の銃口ワールド位置、なければ相対再適用
	 */
	static void ResolveCurrentFireOrigins(const AActor* Shooter, const FTransform& StartTransform,
		const FVector& StartTraceOrigin, const FVector& StartDirection, const FVector& StartVisualOrigin,
		FVector& OutTraceOrigin, FVector& OutDirection, FVector& OutVisualOrigin);
};
