// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/MSWeaponData.h"

#include "Core/MSGameplayTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType UMSWeaponDataBase::PrimaryAssetType(TEXT("MSWeaponData"));

FPrimaryAssetId UMSWeaponDataBase::GetPrimaryAssetId() const
{
	// クラスごとに型が分かれると AssetManager での71武器一括列挙(P1-3)が煩雑になるため、
	// 全派生クラスで単一の PrimaryAssetType "MSWeaponData" に統一する。
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR

namespace
{
	/** ロックオン対象カテゴリで LockOnRange 未設定(<=0)をエラーにする際の共通メッセージ */
	FText MakeWeaponAssetError(const UObject* Asset, const FString& Message)
	{
		return FText::FromString(FString::Printf(TEXT("[%s] %s"), *GetNameSafe(Asset), *Message));
	}
}

EDataValidationResult UMSWeaponDataBase::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// --- 基本 ---
	if (WeaponName.IsEmpty())
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("WeaponName が未設定です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (!CategoryTag.IsValid())
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("CategoryTag が未設定です。Weapon.Category.* を設定してください。")));
		Result = EDataValidationResult::Invalid;
	}
	else if (!CategoryTag.ToString().StartsWith(TEXT("Weapon.Category.")))
	{
		Context.AddError(MakeWeaponAssetError(this, FString::Printf(
			TEXT("CategoryTag '%s' は Weapon.Category.* 配下ではありません。"), *CategoryTag.ToString())));
		Result = EDataValidationResult::Invalid;
	}
	else if (!GetAllowedCategoryTags().HasTagExact(CategoryTag))
	{
		// クラス型とカテゴリタグの不整合(例: UMSShotgunWeaponData に Weapon.Category.Bazooka)
		Context.AddError(MakeWeaponAssetError(this, FString::Printf(
			TEXT("CategoryTag '%s' はデータクラス %s では使用できません(許容: %s)。"),
			*CategoryTag.ToString(), *GetClass()->GetName(), *GetAllowedCategoryTags().ToStringSimple())));
		Result = EDataValidationResult::Invalid;
	}

	// --- 攻撃性能 ---
	if (FireInterval <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("FireInterval は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	// --- 射程 / ロックオン(Unity版既知バグ「新武器のlockOnRange未設定」の再発防止。ガードレールB-5) ---
	if (Range <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("Range は 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (RequiresLockOnRange() && LockOnRange <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, FString::Printf(
			TEXT("LockOnRange が未設定(%.1f)です。このカテゴリ(%s)ではロックオン距離の設定が必須です。"),
			LockOnRange, *CategoryTag.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	// --- 弾薬(MagazineSize<=0 は無限弾のため対象外) ---
	if (MagazineSize > 0 && ReloadTime <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("有限弾(MagazineSize>0)の武器は ReloadTime に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (ReserveAmmo < 0)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("ReserveAmmo は 0 以上が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (bOverrideMuzzleTransform && MuzzleDirectionOverride.IsNearlyZero())
	{
		Context.AddError(MakeWeaponAssetError(this,
			TEXT("MuzzleDirectionOverride must be non-zero when bOverrideMuzzleTransform is enabled.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSWeaponDataBase::GetAllowedCategoryTags() const
{
	// 抽象基底: 直接使用は想定しない(空 = 全カテゴリ不許可)
	return FGameplayTagContainer();
}

// ====================================================================
// UMSRangedWeaponData
// ====================================================================

#endif // WITH_EDITOR

FMSChargeShotMultipliers UMSRangedWeaponData::EvaluateChargeMultipliers(float Progress01) const
{
	const float Progress = FMath::Clamp(Progress01, 0.0f, 1.0f);
	FMSChargeShotMultipliers Result;
	if (!bUseChargeProfile || !bIsChargeable)
	{
		// Saved weapons without a profile keep the existing continuous 1x..2x behavior.
		Result.Damage = Result.DownPoint = FMath::Lerp(1.0f, 2.0f, Progress);
		return Result;
	}

	// The reference gives half/full values, not a continuous curve. Interpolation
	// between those authored anchors is an explicit NeoMSGO implementation estimate.
	const float HalfAt = FMath::Clamp(HalfChargeThreshold01, 0.01f, 0.99f);
	const FMSChargeShotMultipliers Start;
	const FMSChargeShotMultipliers& From = Progress <= HalfAt ? Start : HalfChargeMultipliers;
	const FMSChargeShotMultipliers& To = Progress <= HalfAt ? HalfChargeMultipliers : FullChargeMultipliers;
	const float Alpha = Progress <= HalfAt ? Progress / HalfAt : (Progress - HalfAt) / (1.0f - HalfAt);
	Result.Damage = FMath::Lerp(From.Damage, To.Damage, Alpha);
	Result.DownPoint = FMath::Lerp(From.DownPoint, To.DownPoint, Alpha);
	Result.ProjectileSpeed = FMath::Lerp(From.ProjectileSpeed, To.ProjectileSpeed, Alpha);
	Result.SplashRadius = FMath::Lerp(From.SplashRadius, To.SplashRadius, Alpha);
	return Result;
}

float UMSRangedWeaponData::GetExplosionBaseDamage() const
{
	// A positive radius and positive legacy splash value identify an actual explosion. The
	// larger of Power and SplashDamage is one packet; Power=0 smoke remains handled by its
	// legacy SplashDamage path in projectile callers.
	if (SplashRadius <= 0.0f || SplashDamage <= 0)
	{
		return 0.0f;
	}
	return FMath::Max(static_cast<float>(Power), static_cast<float>(SplashDamage));
}

float UMSRangedWeaponData::GetExplosionMinFalloff() const
{
	// Power-zero smoke keeps the old outer falloff. Other explosives use the explicit
	// NeoMSGO profile: chargeable weapons retain a less severe minimum.
	if (Power <= 0)
	{
		return 0.3f;
	}
	return bIsChargeable ? 0.2f : 0.1f;
}

float UMSRangedWeaponData::GetExplosionFullDamageRadiusRatio() const
{
	return Power > 0 ? 0.15f : 0.0f;
}

#if WITH_EDITOR

EDataValidationResult UMSRangedWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (BurstCount > 1 && BurstInterval <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("バースト武器(BurstCount>1)は BurstInterval に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (bIsChargeable && MaxChargeTime <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("チャージ武器(bIsChargeable)は MaxChargeTime に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}
	if (bUseChargeProfile)
	{
		if (!bIsChargeable || !FMath::IsFinite(HalfChargeThreshold01) ||
			HalfChargeThreshold01 <= 0.0f || HalfChargeThreshold01 >= 1.0f)
		{
			Context.AddError(MakeWeaponAssetError(this,
				TEXT("Charge profile requires a chargeable weapon and a half-charge threshold strictly between 0 and 1.")));
			Result = EDataValidationResult::Invalid;
		}
		const auto IsValid = [](const FMSChargeShotMultipliers& M)
		{
			return FMath::IsFinite(M.Damage) && M.Damage >= 0.0f &&
				FMath::IsFinite(M.DownPoint) && M.DownPoint >= 0.0f &&
				FMath::IsFinite(M.ProjectileSpeed) && M.ProjectileSpeed > 0.0f &&
				FMath::IsFinite(M.SplashRadius) && M.SplashRadius >= 0.0f;
		};
		if (!IsValid(HalfChargeMultipliers) || !IsValid(FullChargeMultipliers))
		{
			Context.AddError(MakeWeaponAssetError(this,
				TEXT("Charge profile multipliers must be finite and non-negative, with positive projectile speed.")));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (SplashRadius > 0.0f && SplashDamage <= 0)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("爆風武器(SplashRadius>0)は SplashDamage に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSRangedWeaponData::GetAllowedCategoryTags() const
{
	FGameplayTagContainer Allowed;
	Allowed.AddTag(MSTags::Weapon_Category_MachineGun);
	Allowed.AddTag(MSTags::Weapon_Category_BeamRifle);
	Allowed.AddTag(MSTags::Weapon_Category_Bazooka);
	return Allowed;
}

// ====================================================================
// UMSShotgunWeaponData
// ====================================================================

EDataValidationResult UMSShotgunWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (PelletCount <= 1)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("散弾武器は PelletCount > 1 が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (PelletSpreadDegrees <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("散弾武器は PelletSpreadDegrees に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSShotgunWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_Shotgun);
}

// ====================================================================
// UMSHomingWeaponData
// ====================================================================

EDataValidationResult UMSHomingWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (HomingTurnRateDegPerSec <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("誘導武器は HomingTurnRateDegPerSec に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (ProjectileSpeed <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("誘導武器は可視弾(ProjectileSpeed>0)である必要があります。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSHomingWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_Homing);
}

// ====================================================================
// UMSContinuousBeamWeaponData
// ====================================================================

EDataValidationResult UMSContinuousBeamWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (BeamTickInterval <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("照射ビームは BeamTickInterval に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BeamDamagePerSecond <= 0)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("照射ビームは BeamDamagePerSecond に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (BeamRadius <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("照射ビームは BeamRadius に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSContinuousBeamWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_ContinuousBeam);
}

// ====================================================================
// UMSMeleeWeaponData
// ====================================================================

FGameplayTagContainer UMSMeleeWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_Melee);
}

// ====================================================================
// UMSShieldWeaponData
// ====================================================================

EDataValidationResult UMSShieldWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!FMath::IsFinite(MaxDurability) || MaxDurability <= 0.0f ||
		!FMath::IsFinite(GuardDownPointMultiplier) || GuardDownPointMultiplier < 0.0f || GuardDownPointMultiplier > 1.0f ||
		!FMath::IsFinite(ExplosionChipMultiplier) || ExplosionChipMultiplier < 0.0f || ExplosionChipMultiplier > 1.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("シールド耐久・DP軽減・爆風貫通が不正です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (GuardDamageMultiplier < 0.0f || GuardDamageMultiplier > 1.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("GuardDamageMultiplier は 0〜1 の範囲が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	if (GuardFrontalAngleDegrees <= 0.0f || GuardFrontalAngleDegrees > 180.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("GuardFrontalAngleDegrees は 0 より大きく 180 以下が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSShieldWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_Shield);
}

// ====================================================================
// UMSSupplyWeaponData
// ====================================================================

EDataValidationResult UMSSupplyWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (DeployedPodLifetime <= 0.0f)
	{
		Context.AddError(MakeWeaponAssetError(this, TEXT("設置武器は DeployedPodLifetime に 0 より大きい値が必要です。")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

FGameplayTagContainer UMSSupplyWeaponData::GetAllowedCategoryTags() const
{
	return FGameplayTagContainer(MSTags::Weapon_Category_Supply);
}

#endif // WITH_EDITOR

EMSWeaponDamageKind UMSWeaponDataBase::GetEffectiveDamageKind() const
{
 if (DamageKind != EMSWeaponDamageKind::Automatic) return DamageKind;
 if (IsA<UMSMeleeWeaponData>()) return EMSWeaponDamageKind::Melee;
 // Stable legacy IDs preserve shipped DA bytes; display names never classify gameplay.
 if (GetPrimaryAssetId() == FPrimaryAssetId(PrimaryAssetType, TEXT("DA_Weapon_MPU01_BeamSprayGun"))) return EMSWeaponDamageKind::Beam;
 if (CategoryTag == MSTags::Weapon_Category_BeamRifle || CategoryTag == MSTags::Weapon_Category_ContinuousBeam) return EMSWeaponDamageKind::Beam;
 if (CategoryTag == MSTags::Weapon_Category_Bazooka || CategoryTag == MSTags::Weapon_Category_Homing) return EMSWeaponDamageKind::Explosive;
 return EMSWeaponDamageKind::Ballistic;
}
EMSProjectileKind UMSRangedWeaponData::GetEffectiveProjectileKind() const
{
 if (ProjectileKind != EMSProjectileKind::Automatic) return ProjectileKind;
 const FPrimaryAssetId Id = GetPrimaryAssetId();
 if (Id == FPrimaryAssetId(PrimaryAssetType,TEXT("DA_Weapon_Grenade")) || Id == FPrimaryAssetId(PrimaryAssetType,TEXT("DA_Weapon_SmokeGrenade"))) return EMSProjectileKind::Thrown;
 if (Id == FPrimaryAssetId(PrimaryAssetType,TEXT("DA_Weapon_GrenadeLauncherGL2")) || Id == FPrimaryAssetId(PrimaryAssetType,TEXT("DA_Weapon_GrenadeLauncherGL2W")) || Id == FPrimaryAssetId(PrimaryAssetType,TEXT("DA_Weapon_TwinGrenadeLauncher"))) return EMSProjectileKind::LaunchedBallistic;
 return CategoryTag == MSTags::Weapon_Category_Bazooka ? EMSProjectileKind::Rocket : EMSProjectileKind::Direct;
}
bool UMSRangedWeaponData::SupportsBlastRadiusControl() const
{
 const EMSProjectileKind Kind = GetEffectiveProjectileKind();
 return CategoryTag == MSTags::Weapon_Category_Bazooka && (Kind == EMSProjectileKind::Rocket || Kind == EMSProjectileKind::LaunchedBallistic);
}
