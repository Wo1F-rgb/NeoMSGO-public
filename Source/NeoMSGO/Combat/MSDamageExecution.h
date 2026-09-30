// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-3: ダメージExecutionCalculation。
// 設計元: docs/UE5_Architecture.md §1.6 ← Unity: CombatUtility.ApplyPointDamage,
//         WeaponController.MitigateGuardedDamage
//
// 入力(§1.6):
//  - SetByCaller Data.Damage        : 基礎威力(チャージ倍率/D格3倍/meleeCorrection等の乗算は呼び出し側で確定済み)
//  - SetByCaller Data.DownPoint     : DP値(未設定は0)
//  - SetByCaller Data.SplashFalloff : 爆風減衰0〜1.0(P2-4が設定。未設定は1.0)
//  - FMSGameplayEffectContext       : bIsHeadshot(P2-5が設定)/ AttackerLocation / Instigator
//  - Target側コンポーネント           : UMSShieldComponent (native frontal guard)
//  - Target側タグ                    : State.Staggered / State.KnockedDown(P2-6)
//
// 計算手順(§1.6 順序厳守):
//  1. FF判定 UMSTeamStatics::CanDamage == false → Damage=0/DP=0で終了(B-2の最後の防衛線)
//  2. Damage *= SplashFalloff
//  3. HSなら Damage *= 1.5
//  4. Native shield componentが有効な正面ガードなら耐久値へ全量吸収。
//     爆風だけはExplosionChipMultiplierをArmorへ出力し、AttackerLocation未設定は fail-open。
//  5. Armor -= max(1, round(Damage))
//  6. DP: State.Staggered / State.KnockedDown 中は0(蓄積無敵猶予)、それ以外 DownPoint += Data.DownPoint
//
// 死亡判定・よろけ遷移・キル通知は UMSAttributeSet_Vital::PostGameplayEffectExecute 側の責務。
// 本Executionは Armor/DownPoint の2 Modifier出力のみを行い、キル通知の重複実装はしない。

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "MSDamageExecution.generated.h"

UCLASS()
class NEOMSGO_API UMSDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UMSDamageExecution();

	//~ Begin UGameplayEffectExecutionCalculation Interface
	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
	//~ End UGameplayEffectExecutionCalculation Interface

	// ------------------------------------------------------------------
	// 計算定数(Unity版デフォルト値。B-9によりゲームバランス値のDataAsset化対象外の
	// システム定数として本クラスに集約。P4-8/P2-5はこの定数を参照する)
	// ------------------------------------------------------------------

	/** HS倍率(Unity: CombatUtility.HeadshotMultiplier = 1.5f) */
	static constexpr float HeadshotMultiplier = 1.5f;

	/** Legacy default retained for old test/data references; live angle comes from UMSShieldWeaponData. */
	static constexpr float GuardFrontalAngleDegrees = 65.0f;

	/** Legacy default retained for old test/data references; live damage is shield absorption. */
	static constexpr float GuardDamageMultiplier = 0.15f;
};
