// Copyright NeoMSGO Project. All Rights Reserved.
// P9-2: 実SE接続済み。マズルフラッシュは実行時生成StaticMeshComponent(発光Cube)による
// 恒久実装(AMSTransientVfxActor::SpawnMuzzleFlash)を使用する。
#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MSGameplayCue_Fire.generated.h"

class USoundBase;

/** GameplayCue.MS.Weapon.Fire: 全射撃種・Player/BOT共通の発射Burst Cue。 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_Fire : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	UMSGameplayCue_Fire();
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	USoundBase* ResolveFireSound(const UObject* SourceObject) const;
	static FVector ResolveMuzzleLocation(AActor* MyTarget, const FGameplayCueParameters& Parameters);
	USoundBase* GetDefaultFireSound() const { return RifleSound; }

private:
	UPROPERTY()
	TObjectPtr<USoundBase> BeamRifleSound;
	UPROPERTY()
	TObjectPtr<USoundBase> RifleSound;
	UPROPERTY()
	TObjectPtr<USoundBase> SMGSound;
	UPROPERTY()
	TObjectPtr<USoundBase> BazookaSound;
	UPROPERTY()
	TObjectPtr<USoundBase> LMGSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ShotgunSound;
	UPROPERTY()
	TObjectPtr<USoundBase> MeleeSound;
};
