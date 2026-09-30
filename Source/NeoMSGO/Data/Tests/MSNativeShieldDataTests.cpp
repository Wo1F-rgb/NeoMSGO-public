#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSNativeShieldAssetsTest, "NeoMSGO.Data.NativeShield.PersistedEquipment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSNativeShieldAssetsTest::RunTest(const FString&)
{
	const TCHAR* Names[] = { TEXT("StrikerMk1"), TEXT("VanguardRX"), TEXT("DreadnoughtHeavy"),
		TEXT("BreakerAssault"), TEXT("GuardianDefender") };
	for (const TCHAR* Name : Names)
	{
		const FString Path = FString::Printf(TEXT("/Game/Data/MobileSuits/DA_MS_%s.DA_MS_%s"), Name, Name);
		UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr, *Path);
		if (!TestNotNull(Path, Suit)) continue;
		UMSShieldWeaponData* Shield = Suit->GetNativeShield();
		if (!TestNotNull(TEXT("native shield resolves"), Shield)) continue;
		TestFalse(TEXT("saved explicit native shield reference"), Suit->NativeShield.IsNull());
		TestEqual(TEXT("three weapon positions retained"), Suit->EquippedWeapons.Num(), 3);
		int32 GunCount = 0;
		for (const auto& Ref : Suit->EquippedWeapons)
		{
			UMSWeaponDataBase* Weapon = Ref.LoadSynchronous();
			TestNull(TEXT("no shield consumes a weapon slot"), Cast<UMSShieldWeaponData>(Weapon));
			GunCount += Weapon ? 1 : 0;
		}
		TestEqual(TEXT("two existing weapons remain"), GunCount, 2);
		TestEqual(TEXT("post-2019 shield weight"), Shield->Weight, 0);
		TestTrue(TEXT("shield has real durability"), Shield->MaxDurability > 0.0f);
		FDataValidationContext Context;
		TestNotEqual(TEXT("null slot is valid after shield migration"), Suit->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSNativeShieldSlotValidationTest, "NeoMSGO.Data.NativeShield.RejectsShieldInWeaponSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSNativeShieldSlotValidationTest::RunTest(const FString&)
{
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>();
	Suit->UnitName = FText::FromString(TEXT("shield validation"));
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>();
	Suit->EquippedWeapons.Add(Shield);
	FDataValidationContext Context;
	TestEqual(TEXT("shield in a weapon slot rejected"), Suit->IsDataValid(Context), EDataValidationResult::Invalid);
	Suit->EquippedWeapons[0] = NewObject<UMSRangedWeaponData>();
	Suit->NativeShield = Shield;
	FDataValidationContext FixedContext;
	TestNotEqual(TEXT("independent shield accepted"), Suit->IsDataValid(FixedContext), EDataValidationResult::Invalid);
	return true;
}

#endif
