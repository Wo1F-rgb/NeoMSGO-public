// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSWeaponAbilityRouting.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaurusChargeBlasterDataTest,
	"NeoMSGO.Weapons.TaurusChargeBlasterData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSTaurusChargeBlasterDataTest::RunTest(const FString& Parameters)
{
	const TCHAR* WeaponPath = TEXT(
		"/Game/Data/Weapons/DA_Weapon_TAUrus_ChargeBlasterLauncher.DA_Weapon_TAUrus_ChargeBlasterLauncher");
	UMSRangedWeaponData* Weapon = LoadObject<UMSRangedWeaponData>(nullptr, WeaponPath);
	if (!TestNotNull(TEXT("TAU-rus charge blaster asset exists"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("weapon category is BeamRifle"),
		Weapon->CategoryTag.MatchesTagExact(MSTags::Weapon_Category_BeamRifle));
	TestEqual(TEXT("chargeable weapon routes to Charge"),
		MSWeaponAbilityRouting::Resolve(Weapon), EMSWeaponAbilityRoute::Charge);
	TestTrue(TEXT("muzzle transform override is enabled"), Weapon->bOverrideMuzzleTransform);
	TestTrue(TEXT("muzzle offset matches the authored WeaponMount-local value"),
		Weapon->MuzzleOffsetOverride.Equals(FVector(95.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("muzzle direction points along local +X"),
		Weapon->MuzzleDirectionOverride.GetSafeNormal().Equals(FVector::ForwardVector, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("burst remains single-shot"), Weapon->BurstCount, 1);
	TestFalse(TEXT("scope mode is disabled"), Weapon->bIsScopeWeapon);
	TestEqual(TEXT("projectile speed selects hitscan"), Weapon->ProjectileSpeed, 0.0f);
	TestEqual(TEXT("weapon owns the TAU-rus charge-blaster skin"),
		Weapon->WeaponMesh.ToSoftObjectPath(),
		FSoftObjectPath(TEXT(
			"/Game/Meshes/Weapons/TAUrus/SM_TAUrus_ChargeBlasterLauncher.SM_TAUrus_ChargeBlasterLauncher")));
	TestEqual(TEXT("splash radius is disabled"), Weapon->SplashRadius, 0.0f);

	UMSMobileSuitData* Taurus = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUrus.DA_MS_TAUrus"));
	if (!TestNotNull(TEXT("TAU-rus mobile-suit asset exists"), Taurus))
	{
		return false;
	}
	// EquippedWeaponsはデッキ永続化(タスク#136)がユーザーのカスタマイズ内容をロード時に
	// 反映するため、実機環境ではスロットの中身が既定装備と異なりうる。ここではアセット構造
	// (スロット数)のみを検証し、既定装備名のアサートはしない。
	TestEqual(TEXT("TAU-rus keeps three weapon slots"), Taurus->EquippedWeapons.Num(), 3);
	return true;
}

#endif
