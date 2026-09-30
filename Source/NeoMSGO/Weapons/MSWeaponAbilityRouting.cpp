// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/MSWeaponAbilityRouting.h"

#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"

namespace
{
	bool IsCategory(const UMSWeaponDataBase* Weapon, const FGameplayTag& Category)
	{
		return Weapon && Weapon->CategoryTag.MatchesTagExact(Category);
	}
}

EMSWeaponAbilityRoute MSWeaponAbilityRouting::Resolve(const UMSWeaponDataBase* Weapon)
{
	if (!Weapon)
	{
		return EMSWeaponAbilityRoute::Unsupported;
	}
	const bool bUncategorized = !Weapon->CategoryTag.IsValid();

	// Category is the public data contract. The class check prevents a malformed asset from
	// being routed into an ability whose typed data cast can never succeed.
	if (IsCategory(Weapon, MSTags::Weapon_Category_Melee) || (bUncategorized && Cast<UMSMeleeWeaponData>(Weapon)))
	{
		return Cast<UMSMeleeWeaponData>(Weapon) ? EMSWeaponAbilityRoute::Melee : EMSWeaponAbilityRoute::Unsupported;
	}
	if (IsCategory(Weapon, MSTags::Weapon_Category_Shield) || (bUncategorized && Cast<UMSShieldWeaponData>(Weapon)))
	{
		// Shields are suit-native equipment and are never a selectable weapon
		// route. Keep the legacy class/category recognizable for migration while
		// refusing to expose the old manual GA_Guard entry point.
		return EMSWeaponAbilityRoute::Unsupported;
	}
	if (IsCategory(Weapon, MSTags::Weapon_Category_Supply) || (bUncategorized && Cast<UMSSupplyWeaponData>(Weapon)))
	{
		return Cast<UMSSupplyWeaponData>(Weapon) ? EMSWeaponAbilityRoute::Deploy : EMSWeaponAbilityRoute::Unsupported;
	}
	if (IsCategory(Weapon, MSTags::Weapon_Category_ContinuousBeam) ||
		(bUncategorized && Cast<UMSContinuousBeamWeaponData>(Weapon)))
	{
		return Cast<UMSContinuousBeamWeaponData>(Weapon)
			? EMSWeaponAbilityRoute::ContinuousBeam : EMSWeaponAbilityRoute::Unsupported;
	}
	if (IsCategory(Weapon, MSTags::Weapon_Category_Shotgun) ||
		(bUncategorized && Cast<UMSShotgunWeaponData>(Weapon)))
	{
		return Cast<UMSShotgunWeaponData>(Weapon) ? EMSWeaponAbilityRoute::Shotgun : EMSWeaponAbilityRoute::Unsupported;
	}

	const UMSRangedWeaponData* Ranged = Cast<UMSRangedWeaponData>(Weapon);
	const bool bStandardRangedCategory =
		IsCategory(Weapon, MSTags::Weapon_Category_MachineGun) ||
		IsCategory(Weapon, MSTags::Weapon_Category_BeamRifle) ||
		IsCategory(Weapon, MSTags::Weapon_Category_Bazooka) ||
		(bUncategorized && Ranged && !Cast<UMSHomingWeaponData>(Weapon));
	const bool bHomingCategory = IsCategory(Weapon, MSTags::Weapon_Category_Homing) ||
		(bUncategorized && Cast<UMSHomingWeaponData>(Weapon));
	if (!Ranged || (!bStandardRangedCategory && !bHomingCategory))
	{
		return EMSWeaponAbilityRoute::Unsupported;
	}
	if (bHomingCategory || Cast<UMSHomingWeaponData>(Weapon))
	{
		return Cast<UMSHomingWeaponData>(Weapon)
			? EMSWeaponAbilityRoute::Homing : EMSWeaponAbilityRoute::Unsupported;
	}

	// Charge has priority over burst, matching the mutually-exclusive GA data contracts.
	if (Ranged->bIsChargeable && Ranged->MaxChargeTime > 0.0f)
	{
		return EMSWeaponAbilityRoute::Charge;
	}
	if (Ranged->BurstCount > 1)
	{
		return EMSWeaponAbilityRoute::Burst;
	}
	return EMSWeaponAbilityRoute::Hitscan;
}

FGameplayTag MSWeaponAbilityRouting::GetGameplayAbilityTag(const EMSWeaponAbilityRoute Route)
{
	switch (Route)
	{
	case EMSWeaponAbilityRoute::Melee:
		return MSTags::Ability_Melee;
	case EMSWeaponAbilityRoute::Guard:
		return MSTags::Ability_Guard;
	case EMSWeaponAbilityRoute::Deploy:
		return MSTags::Ability_Deploy;
	case EMSWeaponAbilityRoute::Hitscan:
	case EMSWeaponAbilityRoute::Shotgun:
	case EMSWeaponAbilityRoute::Burst:
	case EMSWeaponAbilityRoute::Homing:
	case EMSWeaponAbilityRoute::Charge:
	case EMSWeaponAbilityRoute::ContinuousBeam:
		return MSTags::Ability_Fire;
	default:
		return FGameplayTag();
	}
}
