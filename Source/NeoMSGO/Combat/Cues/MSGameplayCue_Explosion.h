// Copyright NeoMSGO Project. All Rights Reserved.
// P9-2: 実SE接続済み。爆発VFXは実行時生成StaticMeshComponent(発光Cube)による
// 恒久実装(AMSTransientVfxActor::SpawnExplosion)を使用する。
#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MSGameplayCue_Explosion.generated.h"

class USoundBase;

/** GameplayCue.MS.Explosion: 誘導弾の着弾/自爆共通Burst Cue。 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_Explosion : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	UMSGameplayCue_Explosion();
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	USoundBase* ResolveExplosionSound(const UObject* SourceObject) const;
	USoundBase* GetUnitExplosionSound() const { return UnitExplosionSound; }
	USoundBase* GetOrdnanceExplosionSound() const { return OrdnanceExplosionSound; }

	// B-7準拠。爆発VFX(AMSTransientVfxActor::SpawnExplosion)の表示時間は必ずこの範囲に収める。
	static constexpr float ExplosionMinDurationSeconds = 0.04f;
	static constexpr float ExplosionMaxDurationSeconds = 0.06f;
	/** SplashRadius=0 の爆発で維持する既定の視覚半径(スケール前cm)。 */
	static constexpr float ExplosionVisualRadiusCm = 90.0f;

private:
	UPROPERTY()
	TObjectPtr<USoundBase> UnitExplosionSound;
	UPROPERTY()
	TObjectPtr<USoundBase> OrdnanceExplosionSound;
};
