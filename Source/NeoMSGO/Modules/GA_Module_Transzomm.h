// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Module_Transzomm.generated.h"

class UAbilitySystemComponent;
class UMSModuleData;
class USoundBase;

/** Data-driven direct-entry ability for the Transzomm module. */
UCLASS()
class NEOMSGO_API UGA_Module_Transzomm : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Module_Transzomm();

	virtual void BeginDestroy() override;

	/** Activates the equipped module and owns its tag-event cleanup subscription. */
	UFUNCTION(BlueprintCallable, Category="MS|Module")
	bool TryActivateModule(AActor* ModuleOwner, const UMSModuleData* Data);

	/** Gameplay-side hook for the briefing UI's transition to sortie. */
	UFUNCTION(BlueprintCallable, Category="MS|Audio", meta=(WorldContext="WorldContextObject"))
	static void PlaySortieTransition(UObject* WorldContextObject);

	USoundBase* GetModuleActivateSound() const { return ModuleActivateSound; }
	USoundBase* GetSortieTransitionSound() const { return SortieTransitionSound; }

private:
	void HandleEffectTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleDeadTagChanged(const FGameplayTag Tag, int32 NewCount);
	void CleanupActiveModule(bool bApplyForcedOverheat);
	bool ApplyDurationEffect(TSubclassOf<UGameplayEffect> EffectClass,
		const FGameplayTag& DurationTag, float DurationSeconds) const;

	TWeakObjectPtr<AActor> ActiveOwner;
	TWeakObjectPtr<UAbilitySystemComponent> ActiveASC;
	TWeakObjectPtr<const UMSModuleData> ActiveData;
	FDelegateHandle EffectTagEventHandle;
	FDelegateHandle DeadTagEventHandle;

	UPROPERTY()
	TObjectPtr<USoundBase> ModuleActivateSound;
	UPROPERTY()
	TObjectPtr<USoundBase> SortieTransitionSound;
};
