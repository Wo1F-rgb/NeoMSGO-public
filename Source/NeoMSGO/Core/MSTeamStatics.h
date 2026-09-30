// Copyright NeoMSGO Project. All Rights Reserved.
//
// P0-4: 陣営判定の単一API。
// 設計元: docs/UE5_Architecture.md §4.2 / docs/UE5_MIGRATION_PLAN.md ガードレールB-2
//
// 【強制ルール】陣営に関する全判定(ダメージExecutionのFF判定、トレースのFF弾停止、
// ロックオン候補選定、誘導ミサイル追尾対象、リペア/補給の利用可否、BOT索敵)は
// 本クラスの判定関数のみを使用する。TeamIdの生比較・独自陣営判定コードはレビューで却下する。
//
// Unity版対応: CombatUtility.GetFaction / IsFriendlyFire、BotFaction、
// WeaponController.IsLockOnEligible の陣営部分を本APIに集約。

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenericTeamAgentInterface.h"
#include "Gameplay/MSCapturePoint.h"
#include "MSTeamStatics.generated.h"

UCLASS()
class NEOMSGO_API UMSTeamStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------
	// 判定API(§4.2の5関数)
	// ------------------------------------------------------------------

	/**
	 * ActorのTeamIdを解決する。非実装Actorは NoTeam。
	 * 解決順: Actor自身 → (Pawnなら)Controller → Instigator(発射物等のフォールバック)。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static FGenericTeamId GetTeamId(const AActor* Actor);

	/** AからBへの態度。NoTeamが絡めばNeutral、同チームFriendly、異チームHostile */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static TEnumAsByte<ETeamAttitude::Type> GetAttitude(const AActor* A, const AActor* B);

	/** Team identity query for presentation/counting code; keeps raw TeamId comparisons inside this API. */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static bool IsOnTeam(const AActor* Actor, FGenericTeamId TeamId);

	/**
	 * FF判定(ダメージ可否)。同チームのみ false(中立は全員に可、中立への攻撃も可)。
	 * 自分自身・Target無効も false。
	 * 使用箇所: UMSDamageExecution 手順1(§1.6)、UMSCombatStatics::ResolveHitscan のFF弾停止、
	 * 爆風適用(ApplySplashDamage)、誘導ミサイル着弾。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static bool CanDamage(const AActor* Shooter, const AActor* Target);

	/**
	 * 回復可否。同チーム、または Healer が中立(中立ポッドは全許可)のみ true。
	 * 使用箇所: GE_Repair の適用前フィルタ、補給パック回収判定。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static bool CanHeal(const AActor* Healer, const AActor* Target);

	/**
	 * ロックオン可否。敵対(Hostile) かつ Target が IMSLockOnTargetInterface 実装
	 * (=ユニット or 本拠点コア)かつ CanBeLockedOn()(死亡/破壊済み除外)のとき true。
	 * 占領拠点・味方・中立・死亡済みは常に false。本拠点コアはロック可能(B-5)。
	 * 使用箇所: UMSLockOnComponent の候補選定、誘導ミサイルの追尾維持。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static bool CanLockOn(const AActor* Viewer, const AActor* Target);

	// ------------------------------------------------------------------
	// EMSCaptureOwner との相互変換(§4.3 — 接続はこの2関数のみ)
	// ------------------------------------------------------------------

	/** 拠点所有状態→TeamId。Neutral は NoTeam(本拠点コアのTeamId決定、補給スポナーの陣営引き継ぎ用) */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static FGenericTeamId CaptureOwnerToTeamId(EMSCaptureOwner Owner);

	/** TeamId→拠点所有状態。Player/Enemy以外は Neutral */
	UFUNCTION(BlueprintPure, Category = "MS|Team")
	static EMSCaptureOwner TeamIdToCaptureOwner(FGenericTeamId TeamId);

	// ------------------------------------------------------------------
	// ヘルパー
	// ------------------------------------------------------------------

	/**
	 * ActorへTeamIdを設定(IGenericTeamAgentInterface::SetGenericTeamId 呼び出し)。
	 * 非実装Actorには false を返す。設置武器の陣営引き継ぎ(B-8)で使用。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Team")
	static bool SetTeamId(AActor* Actor, FGenericTeamId TeamId);

	/** TeamId同士の態度解決(規則の単一実装。AIPerception用グローバルSolverにも登録される) */
	static ETeamAttitude::Type SolveAttitude(FGenericTeamId TeamA, FGenericTeamId TeamB);

	/**
	 * FGenericTeamId::SetAttitudeSolver に SolveAttitude を登録する。
	 * モジュール起動時(FNeoMSGOModule::StartupModule)に1回呼ぶ。
	 * これにより AIPerception の Affiliation 判定も本APIと同一規則になる(§4.2 強制ルール4)。
	 */
	static void RegisterAttitudeSolver();

	/**
	 * RegisterAttitudeSolver で登録したSolverを解除する(FGenericTeamId::ResetAttitudeSolver)。
	 * モジュール終了時(FNeoMSGOModule::ShutdownModule)に1回呼ぶ。
	 * モジュールアンロード/リロード時にダングリングポインタが残るのを防ぐ。
	 */
	static void UnregisterAttitudeSolver();
};
