// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponComponentTest,
	"NeoMSGO.Weapons.SlotsAmmoSwitching",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FWeaponTestWorld
	{
		UWorld* World = nullptr;
		FWeaponTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSWeaponTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FWeaponTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickSlotsWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.05f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	UMSRangedWeaponData* MakeWeapon(UObject* Outer, int32 Magazine, int32 Reserve, float Reload, float Switch, bool bBuiltIn = false)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->MagazineSize = Magazine;
		Weapon->ReserveAmmo = Reserve;
		Weapon->ReloadTime = Reload;
		Weapon->WeaponSwitchDuration = Switch;
		Weapon->bIsBuiltIn = bBuiltIn;
		Weapon->FireInterval = 1.0f;
		return Weapon;
	}
}

bool FMSWeaponComponentTest::RunTest(const FString& Parameters)
{
	FWeaponTestWorld TestWorld;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UMSWeaponComponent* Component = Unit->GetWeaponComponent();
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	TestTrue(TEXT("weapon component participates in actor replication"), Component->GetIsReplicated());

	UMSRangedWeaponData* W0 = MakeWeapon(Unit, 2, 4, 1.0f, 0.6f);
	UMSRangedWeaponData* W1 = MakeWeapon(Unit, 1, 1, 0.5f, 0.6f);
	UMSRangedWeaponData* W2 = MakeWeapon(Unit, 1, 0, 0.5f, 9.0f, true);
	Component->InitializeSlots({ W0, W1, W2 });
	TestEqual(TEXT("three slots are installed"), Component->GetSlotCount(), 3);

	TestTrue(TEXT("first shot starts fireInterval"), Component->ConsumeAmmoAndStartCooldown(0));
	TestTrue(TEXT("fire cooldown is active"), Component->GetFireCooldownRemaining() > 0.0f);
	TestTrue(TEXT("switch to built-in slot succeeds"), Component->SwitchToSlot(2));
	TestFalse(TEXT("built-in slot has zero switch wait"), Component->IsSwitching());
	TestEqual(TEXT("switch always resets fire cooldown"), Component->GetFireCooldownRemaining(), 0.0f);
	const float BuiltInSwitchEnd = Component->GetSwitchEndServerTime();
	TestFalse(TEXT("same-slot built-in selection does not restart switching"), Component->SwitchToSlot(2));
	TestEqual(TEXT("same-slot selection leaves built-in switch timestamp unchanged"), Component->GetSwitchEndServerTime(), BuiltInSwitchEnd);
	TestFalse(TEXT("switch does not auto-reload previous slot"), Component->IsReloading(0));
	TestEqual(TEXT("zero-duration switch has no Switching tag"), ASC->GetTagCount(MSTags::State_Switching), 0);

	TestTrue(TEXT("normal switch starts wait"), Component->SwitchToSlot(0));
	TestEqual(TEXT("Switching tag is granted during wait"), ASC->GetTagCount(MSTags::State_Switching), 1);
	TickSlotsWorld(TestWorld.World, 0.7f);
	TestEqual(TEXT("Switching tag is removed after wait"), ASC->GetTagCount(MSTags::State_Switching), 0);

	TestTrue(TEXT("reload slot 0 starts"), Component->StartReload(0));
	TestEqual(TEXT("active reload grants Reloading tag"), ASC->GetTagCount(MSTags::State_Reloading), 1);
	TestEqual(TEXT("reload duration is captured at start"), Component->GetReloadDuration(0), 1.0f);
	TestEqual(TEXT("reload starts with full remaining duration"), Component->GetReloadRemaining(0), 1.0f);
	TestEqual(TEXT("reload progress starts at zero"), Component->GetReloadProgress01(0), 0.0f);
	const float ReloadEndBeforeSwitch = Component->GetReloadEndServerTime(0);
	TestTrue(TEXT("switch away while reload continues"), Component->SwitchToSlot(1));
	const float SwitchEndWhileOnSlot1 = Component->GetSwitchEndServerTime();
	TestFalse(TEXT("same-slot selection during switch does not extend the wait"), Component->SwitchToSlot(1));
	TestEqual(TEXT("same-slot selection keeps the current switch timestamp"), Component->GetSwitchEndServerTime(), SwitchEndWhileOnSlot1);
	TestTrue(TEXT("inactive slot remains reloading"), Component->IsReloading(0));
	TestEqual(TEXT("Reloading tag describes active slot only"), ASC->GetTagCount(MSTags::State_Reloading), 0);
	TestTrue(TEXT("switching back to the reloading slot succeeds"), Component->SwitchToSlot(0));
	TestEqual(TEXT("slot A is selected immediately when switching back"), Component->GetActiveSlotIndex(), 0);
	TestTrue(TEXT("switching away and back does not cancel slot A reload"), Component->IsReloading(0));
	TestEqual(TEXT("switching away and back preserves slot A reload end time"), Component->GetReloadEndServerTime(0), ReloadEndBeforeSwitch);
	TestTrue(TEXT("switching to slot B again leaves slot A reload running"), Component->SwitchToSlot(1));
	TestEqual(TEXT("slot B is selected after the return path"), Component->GetActiveSlotIndex(), 1);
	TickSlotsWorld(TestWorld.World, 1.1f);
	TestFalse(TEXT("inactive reload completes in background"), Component->IsReloading(0));
	TestEqual(TEXT("reload transfers reserve into magazine"), Component->GetMagazineAmmo(0), 2);
	TestEqual(TEXT("completed reload clears its server end timestamp"), Component->GetReloadEndServerTime(0), 0.0f);

	TickSlotsWorld(TestWorld.World, 0.7f);
	TestTrue(TEXT("slot 1 shot consumes its only round"), Component->ConsumeAmmoAndStartCooldown(1));
	TestTrue(TEXT("magazine depletion automatically starts reload"), Component->IsReloading(1));
	TickSlotsWorld(TestWorld.World, 0.6f);
	TestFalse(TEXT("automatic reload completes after configured duration"), Component->IsReloading(1));
	TestEqual(TEXT("automatic reload transfers reserve"), Component->GetMagazineAmmo(1), 1);
	TickSlotsWorld(TestWorld.World, 0.5f);
	TestTrue(TEXT("reloaded round can fire"), Component->ConsumeAmmoAndStartCooldown(1));
	TickSlotsWorld(TestWorld.World, 1.1f);
	TestTrue(TEXT("EMPTY API is true when magazine and reserve are zero"), Component->IsEmpty(1));
	TestEqual(TEXT("EMPTY display contract for P8-1"), Component->GetAmmoDisplay(1).ToString(), FString(TEXT("EMPTY")));
	TestFalse(TEXT("reload is rejected with zero reserve"), Component->StartReload(1));

	// Slot identity is positional: an empty catalog entry does not compress the
	// three visible rows or become a selectable weapon.
	Component->InitializeSlots({ nullptr, W0, nullptr });
	TestEqual(TEXT("null entries keep all three slot positions"), Component->GetSlotCount(), 3);
	TestNull(TEXT("slot 0 remains empty"), Component->GetWeaponData(0));
	TestTrue(TEXT("slot 1 is the first selectable weapon"), Component->GetActiveSlotIndex() == 1);
	TestEqual(TEXT("empty slot keeps an explicit EMPTY display"), Component->GetAmmoDisplay(0).ToString(), FString(TEXT("EMPTY")));
	TestFalse(TEXT("empty slot 0 cannot be selected"), Component->SwitchToSlot(0));
	TestFalse(TEXT("empty slot 2 cannot be selected"), Component->SwitchToSlot(2));
	TestFalse(TEXT("empty slot 0 cannot fire"), Component->ConsumeAmmoAndStartCooldown(0));

	// A simulated autonomous proxy represents the client-side object. All state
	// writes are rejected until the server's replicated snapshot arrives.
	Component->InitializeSlots({ W0, W1, nullptr });
	TestTrue(TEXT("authority fixture can consume before client guard"), Component->ConsumeAmmoAndStartCooldown(0));
	TickSlotsWorld(TestWorld.World, 1.1f);
	const int32 ClientGuardMagazine = Component->GetMagazineAmmo(0);
	const int32 ClientGuardActive = Component->GetActiveSlotIndex();
	const int32 ClientGuardSlotCount = Component->GetSlotCount();
	const float ClientGuardCooldown = Component->GetFireCooldownRemaining();
	Unit->SetRole(ROLE_AutonomousProxy);
	TestFalse(TEXT("client cannot replace the authoritative loadout"), Component->SetLoadout({ W2, W1, nullptr }));
	Component->InitializeSlots({ W2, W1, nullptr });
	TestEqual(TEXT("client loadout initialization leaves slot count unchanged"), Component->GetSlotCount(), ClientGuardSlotCount);
	TestEqual(TEXT("client loadout initialization leaves active slot unchanged"), Component->GetActiveSlotIndex(), ClientGuardActive);
	TestFalse(TEXT("client cannot switch the replicated active slot"), Component->SwitchToSlot(1));
	TestFalse(TEXT("client cannot start a replicated reload"), Component->StartReload(0));
	TestFalse(TEXT("client cannot consume replicated ammo"), Component->ConsumeAmmoAndStartCooldown(0));
	Component->EnsureFireCooldown(9.0f);
	TestEqual(TEXT("client cannot write the fire cooldown"), Component->GetFireCooldownRemaining(), ClientGuardCooldown);
	TestEqual(TEXT("client mutation attempts leave magazine unchanged"), Component->GetMagazineAmmo(0), ClientGuardMagazine);
	Unit->SetRole(ROLE_Authority);

	return true;
}

#endif
