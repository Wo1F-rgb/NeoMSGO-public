// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/DataAsset.h"
#include "MSModuleData.generated.h"

class FDataValidationContext;

/** Data-driven definition of a mobile suit's single equipped module. */
UCLASS(BlueprintType)
class NEOMSGO_API UMSModuleData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** PrimaryAssetType used for AssetManager catalog enumeration. */
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Module")
	FText ModuleName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Module", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Module")
	TSubclassOf<UGameplayAbility> ModuleAbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0", Units = "s"))
	float EffectDurationSeconds = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0"))
	float SpeedMultiplier = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0"))
	float BoostConsumptionMultiplier = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0", Units = "s"))
	float ForcedOverheatSeconds = 90.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownSeconds = 130.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Afterimage")
	bool bEnableAfterimage = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Afterimage")
	FLinearColor AfterimageColor = FLinearColor(1.0f, 0.08f, 0.24f, 0.18f);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
