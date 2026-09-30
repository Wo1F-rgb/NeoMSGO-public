// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSShieldComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShieldComponentTest,
	"NeoMSGO.Combat.NativeShield.Component",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShieldAutomaticFireRecoveryTest,
	"NeoMSGO.Combat.NativeShield.AutomaticFireRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FShieldTestWorld
	{
		UWorld* World = nullptr;
		FShieldTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSNativeShieldComponentTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FShieldTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSShieldComponentTest::RunTest(const FString& Parameters)
{
	FShieldTestWorld Scope;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = Scope.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("native shield test unit spawns"), Unit)) return false;

	Unit->SetGenericTeamId(MSTeam::EnemyTeamId());
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Unit, TEXT("NativeShieldSuit"));
	UMSShieldWeaponData* ShieldData = NewObject<UMSShieldWeaponData>(Suit, TEXT("NativeShieldData"));
	ShieldData->MaxDurability = 1000.0f;
	ShieldData->GuardFrontalAngleDegrees = 65.0f;
	Suit->NativeShield = ShieldData;
	Unit->InitializeUnit(Suit);

	UMSShieldComponent* Shield = Unit->GetShieldComponent();
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("native shield component exists"), Shield)
		|| !TestNotNull(TEXT("unit ASC exists"), ASC)) return false;

	TestTrue(TEXT("native shield starts equipped"), Shield->IsNativeShieldEquipped());
	TestEqual(TEXT("native shield starts at the data max"), Shield->GetCurrentDurability(), 1000.0f);
	TestTrue(TEXT("front guard is automatic"), Shield->CanProvideGuard());
	TestTrue(TEXT("legacy State.Guarding is not required for damage guard"),
		!ASC->HasMatchingGameplayTag(MSTags::State_Guarding) || Shield->CanProvideGuard());

	float ArmorDamage = -1.0f;
	float ShieldDamage = -1.0f;
	bool bGuarded = false;
	TestTrue(TEXT("frontal packet is absorbed"), Shield->TryAbsorbDamage(
		600.0f, false, FVector(1000.0f, 0.0f, 0.0f), ArmorDamage, ShieldDamage, bGuarded));
	TestTrue(TEXT("frontal packet reports guarded"), bGuarded);
	TestEqual(TEXT("shield loses exactly the packet"), Shield->GetCurrentDurability(), 400.0f);
	TestEqual(TEXT("direct frontal packet has no armor spill"), ArmorDamage, 0.0f);

	TestFalse(TEXT("rear packet cannot be blocked"), Shield->TryAbsorbDamage(
		100.0f, false, FVector(-1000.0f, 0.0f, 0.0f), ArmorDamage, ShieldDamage, bGuarded));
	TestEqual(TEXT("rear packet preserves the incoming armor damage"), ArmorDamage, 100.0f);
	TestEqual(TEXT("rear packet leaves shield unchanged"), Shield->GetCurrentDurability(), 400.0f);

	// A broken shield remains unavailable until a repair reaches its full max.
	TestTrue(TEXT("second frontal packet still reaches the shield"), Shield->TryAbsorbDamage(
		400.0f, false, FVector(1000.0f, 0.0f, 0.0f), ArmorDamage, ShieldDamage, bGuarded));
	TestTrue(TEXT("shield marks zero durability as broken"), Shield->IsShieldBroken());
	Shield->Repair(999.0f);
	TestTrue(TEXT("partial repair keeps broken shield disabled"), Shield->IsShieldBroken());
	Shield->Repair(1.0f);
	TestFalse(TEXT("full repair reactivates shield"), Shield->IsShieldBroken());
	TestEqual(TEXT("full repair restores the max"), Shield->GetCurrentDurability(), 1000.0f);
	TestTrue(TEXT("shield can be damaged before respawn reset"), Shield->TryAbsorbDamage(
		400.0f, false, FVector(1000.0f, 0.0f, 0.0f), ArmorDamage, ShieldDamage, bGuarded));
	Shield->ResetForRespawn();
	TestFalse(TEXT("respawn clears the broken state"), Shield->IsShieldBroken());
	TestEqual(TEXT("respawn restores native shield to max"), Shield->GetCurrentDurability(), 1000.0f);

	ASC->AddLooseGameplayTag(MSTags::State_Reloading);
	TestTrue(TEXT("reload alone does not disable native guard"), Shield->CanProvideGuard());
	ASC->RemoveLooseGameplayTag(MSTags::State_Reloading);
	UMSRangedWeaponData* TwoHanded = NewObject<UMSRangedWeaponData>(Unit, TEXT("TwoHandedNativeShieldBlocker"));
	TwoHanded->bDisablesNativeShield = true;
	Unit->GetWeaponComponent()->InitializeSlots({TwoHanded});
	TestFalse(TEXT("data-marked two-handed weapon disables native guard"), Shield->CanProvideGuard());
	TwoHanded->bDisablesNativeShield = false;
	ASC->AddLooseGameplayTag(MSTags::State_Switching);
	TestFalse(TEXT("weapon switching disables native guard"), Shield->CanProvideGuard());
	ASC->RemoveLooseGameplayTag(MSTags::State_Switching);
	ASC->AddLooseGameplayTag(MSTags::State_Staggered);
	TestFalse(TEXT("flinch disables native guard"), Shield->CanProvideGuard());
	ASC->RemoveLooseGameplayTag(MSTags::State_Staggered);

	Shield->SetDeckShieldEnabled(false);
	TestFalse(TEXT("deck unequip removes native shield"), Shield->IsNativeShieldEquipped());
	Shield->SetDeckShieldEnabled(true);
	TestTrue(TEXT("deck equip restores native shield"), Shield->IsNativeShieldEquipped());
	TestEqual(TEXT("deck re-equip starts a fresh shield"), Shield->GetCurrentDurability(), 1000.0f);
	return true;
}

bool FMSShieldAutomaticFireRecoveryTest::RunTest(const FString& Parameters)
{
	struct FOfflineAuth
	{
		FString Previous = FCommandLine::Get();
		FOfflineAuth()
		{
			TArray<FString> Tokens; Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)) continue;
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			Effective += TEXT("-MSAuthMode=OfflineLocal");
			FCommandLine::Set(*Effective);
		}
		~FOfflineAuth(){FCommandLine::Set(*Previous);}
	} OfflineAuth;

	// The two calibrated automatic intervals exercise both the head Vulcan and
	// the 120mm machine-gun/FA path without mutating saved weapon assets.
	for (const float Interval : {0.15f, 1.0f / 6.0f})
	{
		FShieldTestWorld Scope;
		FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Unit=Scope.World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(),FVector(0,0,900),FRotator::ZeroRotator,Params);
		if (!TestNotNull(TEXT("automatic-fire shield unit exists"),Unit)) return false;
		UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(Unit);
		UMSShieldWeaponData* ShieldData=NewObject<UMSShieldWeaponData>(Suit);
		ShieldData->MaxDurability=1000.0f;ShieldData->GuardFrontalAngleDegrees=65.0f;
		Suit->NativeShield=ShieldData;Unit->InitializeUnit(Suit);
		UMSRangedWeaponData* Auto=NewObject<UMSRangedWeaponData>(Unit);
		Auto->CategoryTag=MSTags::Weapon_Category_MachineGun;
		Auto->FireInterval=Interval;Auto->MagazineSize=30;Auto->ReserveAmmo=0;
		Auto->bCanFireWithNativeShield=false;
		UMSRangedWeaponData* ShieldIntegrated=NewObject<UMSRangedWeaponData>(Unit);
		ShieldIntegrated->CategoryTag=MSTags::Weapon_Category_MachineGun;
		ShieldIntegrated->FireInterval=Interval;ShieldIntegrated->MagazineSize=30;
		ShieldIntegrated->WeaponSwitchDuration=0.05f;
		ShieldIntegrated->bCanFireWithNativeShield=true;
		UMSWeaponComponent* Weapons=Unit->GetWeaponComponent();
		Weapons->InitializeSlots({Auto,ShieldIntegrated});
		UMSShieldComponent* Shield=Unit->GetShieldComponent();
		TestTrue(TEXT("native shield guards before trigger hold"),Shield->CanProvideGuard());
		TestTrue(TEXT("first automatic round fires"),Unit->StartCurrentWeaponFire());
		TestTrue(TEXT("authoritative trigger remains held"),Unit->IsAutomaticFireActive());
		TestFalse(TEXT("shot immediately lowers shield"),Shield->CanProvideGuard());
		float HoldArmorDamage=0.0f,HoldShieldDamage=0.0f;bool bHoldGuarded=false;
		TestFalse(TEXT("frontal damage during fire passes the lowered shield"),Shield->TryAbsorbDamage(100.0f,false,
			Unit->GetActorLocation()+Unit->GetActorForwardVector()*1000.0f,HoldArmorDamage,HoldShieldDamage,bHoldGuarded));
		TestEqual(TEXT("lowered shield does not consume durability"),Shield->GetCurrentDurability(),1000.0f);
		TestEqual(TEXT("unblocked packet retains armor damage"),HoldArmorDamage,100.0f);

		int32 LastAmmo=Weapons->GetMagazineAmmo(0);
		TArray<float> ShotTimes;ShotTimes.Add(Scope.World->GetTimeSeconds());
		bool bGuardRaisedDuringHold=false;
		for(int32 Step=0;Step<85;++Step)
		{
			Scope.World->Tick(LEVELTICK_All,0.01f);++GFrameCounter;
			const int32 Ammo=Weapons->GetMagazineAmmo(0);
			if(Ammo<LastAmmo) ShotTimes.Add(Scope.World->GetTimeSeconds());
			LastAmmo=Ammo;
			bGuardRaisedDuringHold |= Shield->CanProvideGuard();
		}
		TestFalse(TEXT("shield never rises between held automatic rounds"),bGuardRaisedDuringHold);
		TestTrue(TEXT("automatic fire continues at the original rate"),ShotTimes.Num()>=4);
		for(int32 Shot=1;Shot<ShotTimes.Num();++Shot)
		{
			AddInfo(FString::Printf(TEXT("automatic fire %.3fs: round %d actual %.3fs -> %.3fs (delta %.3fs)"),
				Interval, Shot, ShotTimes[Shot-1], ShotTimes[Shot], ShotTimes[Shot]-ShotTimes[Shot-1]));
			TestTrue(FString::Printf(TEXT("round %d interval remains %.3fs within frame tolerance"),Shot,Interval),
				FMath::Abs((ShotTimes[Shot]-ShotTimes[Shot-1])-Interval)<=0.04f);
		}
		Unit->StopCurrentWeaponFire();
		TestFalse(TEXT("release stops the automatic trigger timer"),Unit->IsAutomaticFireActive());
		for(int32 Step=0;Step<10;++Step){Scope.World->Tick(LEVELTICK_All,0.01f);++GFrameCounter;}
		TestFalse(TEXT("shield stays down during post-fire recovery"),Shield->CanProvideGuard());
		for(int32 Step=0;Step<60;++Step){Scope.World->Tick(LEVELTICK_All,0.01f);++GFrameCounter;}
		TestTrue(TEXT("shield returns after cooldown and recovery"),Shield->CanProvideGuard());

		TestTrue(TEXT("fresh shot can still fire at the original rate"),Unit->StartCurrentWeaponFire());
		Unit->StopCurrentWeaponFire();
		TestTrue(TEXT("switch to shield-integrated weapon succeeds"),Weapons->SwitchToSlot(1));
		TestFalse(TEXT("switch still lowers native shield"),Shield->CanProvideGuard());
		for(int32 Step=0;Step<10;++Step){Scope.World->Tick(LEVELTICK_All,0.01f);++GFrameCounter;}
		TestTrue(TEXT("new weapon ignores old slot's recovery after switch"),Shield->CanProvideGuard());
		TestTrue(TEXT("integrated weapon fires without lowering guard"),Unit->StartCurrentWeaponFire());
		TestTrue(TEXT("bCanFireWithNativeShield preserves guard during fire"),Shield->CanProvideGuard());
		Unit->StopCurrentWeaponFire();

		float ArmorDamage=0.0f,ShieldDamage=0.0f;bool bGuarded=false;
		TestTrue(TEXT("front hit can still break the shield"),Shield->TryAbsorbDamage(1000.0f,false,
			Unit->GetActorLocation()+Unit->GetActorForwardVector()*1000.0f,ArmorDamage,ShieldDamage,bGuarded));
		TestFalse(TEXT("broken shield never guards"),Shield->CanProvideGuard());
		Shield->SetDeckShieldEnabled(false);
		TestFalse(TEXT("deck-off shield stays unavailable"),Shield->CanProvideGuard());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
