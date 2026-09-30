// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float UMSMobileSuitData::GetReuseDelaySeconds() const
{
	if(ReuseDelaySeconds>=0.0f) return ReuseDelaySeconds;
	// wiki_system_data.txt, battle-assist module table (2017-12-13 base column),
	// also visible as 340/400, 380/590 and 400/690 in MSGO9. Never interpolate an unknown cost.
	switch(Cost)
	{
	case 160:return 30; case 180:return 40; case 200:return 50; case 220:return 70;
	case 240:return 100; case 260:return 140; case 280:return 190; case 300:return 250;
	case 320:return 320; case 340:return 400; case 360:return 490; case 380:return 590; case 400:return 690;
	default:return 0;
	}
}

const FPrimaryAssetType UMSMobileSuitData::PrimaryAssetType(TEXT("MSMobileSuitData"));

UMSShieldWeaponData* UMSMobileSuitData::GetNativeShield() const
{
	if (!NativeShield.IsNull()) return NativeShield.LoadSynchronous();
	for (const auto& Weapon : EquippedWeapons)
	{
		if (auto* Shield = Cast<UMSShieldWeaponData>(Weapon.LoadSynchronous())) return Shield;
	}
	return nullptr;
}

FPrimaryAssetId UMSMobileSuitData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR

namespace
{
	FText MakeMobileSuitAssetError(const UObject* Asset, const FString& Message)
	{
		return FText::FromString(FString::Printf(TEXT("[%s] %s"), *GetNameSafe(Asset), *Message));
	}
}

EDataValidationResult UMSMobileSuitData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// --- 基本 ---
	if (UnitName.IsEmpty())
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("UnitName が未設定です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (Cost < 0)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("Cost は 0 以上が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	// --- 耐久 ---
	if (MaxArmor <= 0)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("MaxArmor は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (MaxDownPoint <= 0)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("MaxDownPoint は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	// --- 移動 ---
	if (WalkSpeed <= 0.0f)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("WalkSpeed は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BoostSpeed <= 0.0f)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("BoostSpeed は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BoostSpeed < WalkSpeed)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("BoostSpeed は WalkSpeed 以上である必要があります。")));
		Result = EDataValidationResult::Invalid;
	}

	// --- ブースト ---
	if (BoostCapacity <= 0.0f)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("BoostCapacity は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BoostConsumptionRate <= 0.0f)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("BoostConsumptionRate は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BoostRechargeRate <= 0.0f)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("BoostRechargeRate は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	// --- 装備 ---
	if (LoadCapacity <= 0)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("LoadCapacity は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (EquippedWeapons.Num() <= 0)
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("EquippedWeapons が空です。少なくとも1つの武器を設定してください。")));
		Result = EDataValidationResult::Invalid;
	}
	else if (EquippedWeapons.Num() > MaxWeaponSlots)
	{
		Context.AddError(MakeMobileSuitAssetError(this, FString::Printf(
			TEXT("EquippedWeapons が上限(%d)を超えています(%d件)。"), MaxWeaponSlots, EquippedWeapons.Num())));
		Result = EDataValidationResult::Invalid;
	}

	for (const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef : EquippedWeapons)
	{
		// 空欄は武器番号を維持するための正規の状態。盾は独立欄へ移す。
		if (Cast<UMSShieldWeaponData>(WeaponRef.LoadSynchronous()))
		{
			Context.AddError(MakeMobileSuitAssetError(this, TEXT("シールドは EquippedWeapons ではなく NativeShield に設定してください。")));
			Result = EDataValidationResult::Invalid;
			break;
		}
	}
	if (!NativeShield.IsNull() && !NativeShield.LoadSynchronous())
	{
		Context.AddError(MakeMobileSuitAssetError(this, TEXT("NativeShield を解決できません。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#endif // WITH_EDITOR
