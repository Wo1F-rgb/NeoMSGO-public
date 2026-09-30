// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-12: 弾道トレーサー用 GameplayCue(Burst/Instant)。
// 設計元: docs/UE5_MIGRATION_PLAN.md P4-12、docs/ArchitectureMap.md §8(Unity: TracerEffect.cs)
//
// 恒久実装:
//   本クラスは実行時生成StaticMeshComponent(発光Cube、AMSVisualProjectile::ConfigureVisuals
//   と同じ /Engine/BasicShapes/Cube + UMaterialInstanceDynamic + UPointLightComponent
//   パターン)による弾道トレーサー(AMSTransientVfxActor::SpawnTracer)を描画する。
//   DrawDebug系と異なりShipping相当でも描画される。外部から見えるインターフェース
//   (GameplayCueTag = MSTags::Cue_Weapon_Tracer, データ駆動のColor/Width/Lifetime)は
//   変更していない。
//
// データ駆動性(P4-12 AC1):
//   Weapon->TracerColor / TracerWidth / TracerLifetime を Parameters.SourceObject
//   (UMSRangedWeaponData) から読み取って描画パラメータに反映する。SourceObjectが
//   武器データでない/未設定の場合はUMSRangedWeaponDataのデフォルト値にフォールバックする。
//
// 始点/終点:
//   始点 = FMSGameplayEffectContext::GetAttackerLocation()(GA_Fire_Hitscanが
//   Context->SetAttackerLocation(Origin) で設定済み。P2-3)。未設定時はInstigatorの現在位置。
//   終点 = Parameters.Location(命中点。GA_Fire_HitscanのApplyGameplayEffectSpecToTarget時に
//   Context->AddHitResult経由で伝播する想定。P4-13でCue発火経路を配線する際に設定する)。

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MSGameplayCue_Tracer.generated.h"

class UMSRangedWeaponData;

/**
 * 即着武器の弾道トレーサーをBurstイベントとして描画するCue。
 * GameplayCueTag: GameplayCue.MS.Weapon.Tracer (MSTags::Cue_Weapon_Tracer)
 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_Tracer : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UMSGameplayCue_Tracer();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
	/** Parameters.SourceObjectから武器データを解決する。取得できなければnullptr。 */
	static const UMSRangedWeaponData* ResolveWeaponData(const FGameplayCueParameters& Parameters);
};
