#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSSupplyPack.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSupplyPackP76Test, "NeoMSGO.Gameplay.SupplyPack.P7_6",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSSupplyPackTestP76
{
	struct FScopedSupplyWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSupplyPackTestP76World"));
		FScopedSupplyWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedSupplyWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	void AdvanceSupplyWorld(UWorld* World, float Seconds = 0.05f)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.02f); World->Tick(LEVELTICK_All, Step);
			Seconds -= Step; ++GFrameCounter;
		}
	}

	AMSPlayerCharacter* SpawnSupplyConsumer(UWorld* World, const FVector& Location, const FGenericTeamId Team, float Armor)
	{
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(Location, FRotator::ZeroRotator, Params);
		Unit->SetGenericTeamId(Team); Unit->GetVitalSet()->SetMaxArmor(2000.0f); Unit->GetVitalSet()->SetArmor(Armor);
		return Unit;
	}

	UMSRangedWeaponData* MakeSupplyWeapon(UObject* Outer, int32 Magazine, int32 Reserve)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->MagazineSize = Magazine; Weapon->ReserveAmmo = Reserve; Weapon->FireInterval = 0.0f;
		return Weapon;
	}
}

bool FMSSupplyPackP76Test::RunTest(const FString&)
{
	using namespace MSSupplyPackTestP76;
	FScopedSupplyWorld Scope;
	AMSPlayerCharacter* Player = SpawnSupplyConsumer(Scope.World, FVector::ZeroVector, MSTeam::PlayerTeamId(), 100.0f);
	AMSPlayerCharacter* Enemy = SpawnSupplyConsumer(Scope.World, FVector(2000, 0, 0), MSTeam::EnemyTeamId(), 100.0f);

	AMSSupplyPack* Repair = AMSSupplyPack::Spawn(Scope.World, FVector::ZeroVector, EMSSupplyPackType::Repair);
	TestTrue(TEXT("neutral repair pack can be consumed"), Repair->TryConsume(Player));
	TestEqual(TEXT("repair restores 1500 Armor"), Player->GetVitalSet()->GetArmor(), 1600.0f);
	TestTrue(TEXT("successful repair destroys actor"), Repair->IsActorBeingDestroyed());

	AMSSupplyPack* ClampRepair = AMSSupplyPack::Spawn(Scope.World, FVector::ZeroVector, EMSSupplyPackType::Repair);
	TestTrue(TEXT("second repair succeeds below maximum"), ClampRepair->TryConsume(Player));
	TestEqual(TEXT("repair clamps at MaxArmor"), Player->GetVitalSet()->GetArmor(), 2000.0f);
	AMSSupplyPack* FullRepair = AMSSupplyPack::Spawn(Scope.World, FVector::ZeroVector, EMSSupplyPackType::Repair);
	TestFalse(TEXT("repair pack is not consumed at full Armor"), FullRepair->TryConsume(Player));
	TestFalse(TEXT("failed full-Armor pickup remains"), FullRepair->IsActorBeingDestroyed()); FullRepair->Destroy();

	AMSSupplyPack* NeutralEnemyPack = AMSSupplyPack::Spawn(Scope.World, Enemy->GetActorLocation(), EMSSupplyPackType::Repair);
	TestTrue(TEXT("neutral pack permits enemy consumer"), NeutralEnemyPack->TryConsume(Enemy));
	AMSSupplyPack* FactionPack = AMSSupplyPack::Spawn(Scope.World, Enemy->GetActorLocation(), EMSSupplyPackType::Repair, MSTeam::PlayerTeamId());
	TestFalse(TEXT("faction pack rejects another team"), FactionPack->TryConsume(Enemy));
	TestFalse(TEXT("rejected faction pack remains"), FactionPack->IsActorBeingDestroyed()); FactionPack->Destroy();

	UMSWeaponComponent* Weapons = Player->GetWeaponComponent();
	UMSRangedWeaponData* W0 = MakeSupplyWeapon(Player, 3, 7); UMSRangedWeaponData* W1 = MakeSupplyWeapon(Player, 2, 5);
	W0->bIsBuiltIn = true; W1->bIsBuiltIn = true;
	Weapons->InitializeSlots({W0, W1});
	TestTrue(TEXT("deplete weapon 0"), Weapons->ConsumeAmmoDuringBurst(0));
	TestTrue(TEXT("switch to weapon 1"), Weapons->SwitchToSlot(1));
	TestTrue(TEXT("deplete weapon 1"), Weapons->ConsumeAmmoDuringBurst(1));
	AMSSupplyPack* Ammo = AMSSupplyPack::Spawn(Scope.World, FVector::ZeroVector, EMSSupplyPackType::Ammo);
	TestTrue(TEXT("ammo pack consumes"), Ammo->TryConsume(Player));
	TestEqual(TEXT("weapon 0 magazine is full"), Weapons->GetMagazineAmmo(0), 3);
	TestEqual(TEXT("weapon 0 reserve is full"), Weapons->GetReserveAmmo(0), 7);
	TestEqual(TEXT("weapon 1 magazine is full"), Weapons->GetMagazineAmmo(1), 2);
	TestEqual(TEXT("weapon 1 reserve is full"), Weapons->GetReserveAmmo(1), 5);

	Player->SetActorLocation(FVector::ZeroVector);
	AMSSupplyPack* Near = AMSSupplyPack::Spawn(Scope.World, FVector(300, 0, 0) * MSGO::MSScale, EMSSupplyPackType::Ammo);
	AdvanceSupplyWorld(Scope.World);
	TestEqual(TEXT("overlap detects pack inside scaled pickup radius"), Player->GetNearbySupplyPack(), Near);
	Player->SetActorLocation(FVector(1200, 0, 0) * MSGO::MSScale, false, nullptr, ETeleportType::TeleportPhysics);
	AdvanceSupplyWorld(Scope.World);
	TestNull(TEXT("overlap removes pack outside scaled pickup radius"), Player->GetNearbySupplyPack());
	Near->Destroy();
	return true;
}

#endif
