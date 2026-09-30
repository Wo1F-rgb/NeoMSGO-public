// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSModuleData.h"

#include "Abilities/GameplayAbility.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType UMSModuleData::PrimaryAssetType(TEXT("MSModuleData"));

FPrimaryAssetId UMSModuleData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR

namespace
{
	FText MakeModuleAssetError(const UObject* Asset, const TCHAR* Message)
	{
		return FText::FromString(FString::Printf(TEXT("[%s] %s"), *GetNameSafe(Asset), Message));
	}
}

EDataValidationResult UMSModuleData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto RequirePositive = [this, &Context, &Result](const float Value, const TCHAR* FieldName)
	{
		if (Value <= 0.0f)
		{
			Context.AddError(MakeModuleAssetError(this, *FString::Printf(TEXT("%s は 0 より大きい値が必要です。"), FieldName)));
			Result = EDataValidationResult::Invalid;
		}
	};

	if (!ModuleAbilityClass)
	{
		Context.AddError(MakeModuleAssetError(this, TEXT("ModuleAbilityClass が未設定です。")));
		Result = EDataValidationResult::Invalid;
	}
	RequirePositive(EffectDurationSeconds, TEXT("EffectDurationSeconds"));
	RequirePositive(SpeedMultiplier, TEXT("SpeedMultiplier"));
	RequirePositive(BoostConsumptionMultiplier, TEXT("BoostConsumptionMultiplier"));
	RequirePositive(ForcedOverheatSeconds, TEXT("ForcedOverheatSeconds"));
	RequirePositive(CooldownSeconds, TEXT("CooldownSeconds"));
	return Result;
}

#endif // WITH_EDITOR
