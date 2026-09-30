// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MSWeaponAbilityRouting.generated.h"

class UMSWeaponDataBase;

/** The gameplay entry point selected for a weapon trigger. */
UENUM(BlueprintType)
enum class EMSWeaponAbilityRoute : uint8
{
	Unsupported,
	Hitscan,
	Shotgun,
	Burst,
	Homing,
	Charge,
	ContinuousBeam,
	Melee,
	Guard,
	Deploy
};

/** Shared weapon-data routing used by both player input and BOT ability selection. */
namespace MSWeaponAbilityRouting
{
	NEOMSGO_API EMSWeaponAbilityRoute Resolve(const UMSWeaponDataBase* Weapon);
	NEOMSGO_API FGameplayTag GetGameplayAbilityTag(EMSWeaponAbilityRoute Route);
}
