// Copyright NeoMSGO Project. All Rights Reserved.
// P4-13 PLACEHOLDER: 撃破確認の実SEはP9-2で接続する。
#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MSGameplayCue_KillConfirm.generated.h"

class USoundBase;

/** GameplayCue.MS.Kill.Confirm: Killer側ASCから死亡遷移時に一度だけ発火するCue。 */
UCLASS()
class NEOMSGO_API UMSGameplayCue_KillConfirm : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	UMSGameplayCue_KillConfirm();
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	USoundBase* GetDescendingToneSound() const { return KillConfirmSound; }
	USoundBase* GetMetallicShineSound() const { return KillConfirmSound; }
	USoundBase* GetKillConfirmSound() const { return KillConfirmSound; }

private:
	UPROPERTY()
	TObjectPtr<USoundBase> KillConfirmSound;
};
