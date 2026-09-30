// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "Core/MSTeamAgentActor.h"
#include "MSDestructibleTarget.generated.h"

class UAbilitySystemComponent;
class UBoxComponent;
class UMSAttributeSet_Vital;
class USceneComponent;
class UStaticMeshComponent;

/** GAS-backed, non-character destruction target used by home-base cores. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSDestructibleTarget : public AMSTeamAgentActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMSDestructibleTarget();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual bool CanBeLockedOn() const override;
	virtual FVector GetLockOnAimPoint() const override;

	UFUNCTION(BlueprintPure, Category="MS|Vital")
	UMSAttributeSet_Vital* GetVitalSet() const { return VitalSet; }

	UFUNCTION(BlueprintPure, Category="MS|Vital")
	UBoxComponent* GetCoreCollision() const { return CoreCollision; }

	UFUNCTION(BlueprintPure, Category="MS|Vital")
	float GetMaxArmorSetting() const { return MaxArmor; }

	/** Sets the initialization value consumed by BeginPlay. Call before the actor finishes spawning. */
	void SetMaxArmorSetting(const float InMaxArmor) { MaxArmor = FMath::Max(1.0f, InMaxArmor); }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Vital", meta=(ClampMin="1.0"))
	float MaxArmor = 90000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|GAS")
	TObjectPtr<UMSAttributeSet_Vital> VitalSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Collision")
	TObjectPtr<UBoxComponent> CoreCollision;

private:
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<UStaticMeshComponent> CoreMesh;
};
