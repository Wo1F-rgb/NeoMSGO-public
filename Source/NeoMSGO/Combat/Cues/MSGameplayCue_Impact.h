// Copyright NeoMSGO Project. All Rights Reserved.
// P9-2: 実SE接続済み。着弾VFXは実行時生成StaticMeshComponent(発光Cube)による
// 恒久実装(AMSTransientVfxActor::SpawnImpactSparks)を使用する。
#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MSGameplayCue_Impact.generated.h"

class USoundBase;

/** GameplayCue.MS.Impact.Normal: 通常着弾Burst Cue。 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_Impact_Normal : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	UMSGameplayCue_Impact_Normal();
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	USoundBase* ResolveImpactSound(const UObject* SourceObject) const;
	USoundBase* GetMeleeImpactSound() const { return MeleeImpactSound; }
	USoundBase* GetOtherImpactSound() const { return OtherImpactSound; }

private:
	UPROPERTY()
	TObjectPtr<USoundBase> MeleeImpactSound;
	UPROPERTY()
	TObjectPtr<USoundBase> OtherImpactSound;
};

/** GameplayCue.MS.Impact.Headshot: ヘッドショット着弾Burst Cue。 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_Impact_Headshot : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	UMSGameplayCue_Impact_Headshot();
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	USoundBase* GetHeadshotSound() const { return OtherImpactSound; }

private:
	UPROPERTY()
	TObjectPtr<USoundBase> OtherImpactSound;
};
