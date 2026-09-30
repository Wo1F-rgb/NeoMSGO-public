// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Deploy.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeployAbilityTest,
	"NeoMSGO.Weapons.DeployAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FDeployTestWorld
	{
		UWorld* World = nullptr;

		FDeployTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSDeployTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FDeployTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AActor* SpawnDeployGround(UWorld* World)
	{
		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* Collision = NewObject<UBoxComponent>(Ground, TEXT("DeployTestGround"));
		Ground->SetRootComponent(Collision);
		Collision->SetBoxExtent(FVector(5000.0f, 5000.0f, 10.0f));
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Collision->SetCollisionObjectType(ECC_WorldStatic);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Collision->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
		return Ground;
	}

	AMSUnitBase* SpawnDeployer(UWorld* World, const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),
			FVector(0.0f, 0.0f, 100.0f), FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		return Unit;
	}

	UMSSupplyWeaponData* EquipSupplyWeapon(AMSUnitBase* Deployer, const EMSDeployedItemType Type)
	{
		UMSSupplyWeaponData* Weapon = NewObject<UMSSupplyWeaponData>(Deployer);
		Weapon->DeployedItemType = Type;
		Weapon->DeployedPodLifetime = 12.0f;
		Weapon->MagazineSize = 0;
		Weapon->FireInterval = 0.0f;
		Deployer->GetWeaponComponent()->InitializeSlots({Weapon});
		return Weapon;
	}
}

bool FMSDeployAbilityTest::RunTest(const FString& Parameters)
{
	FDeployTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	SpawnDeployGround(World);
	const FGenericTeamId DeployerTeam = MSTeam::EnemyTeamId();
	AMSUnitBase* Deployer = SpawnDeployer(World, DeployerTeam);
	UGA_Deploy* Ability = NewObject<UGA_Deploy>(Deployer);

	TestEqual(TEXT("placement distance is three metres"), UGA_Deploy::DeployDistanceCm, 300.0f);

	EquipSupplyWeapon(Deployer, EMSDeployedItemType::RepairPod);
	TestTrue(TEXT("RepairPod deploy succeeds on ground"), Ability->DeployItem(Deployer,
		Deployer->GetActorLocation(), FVector::ForwardVector));
	TArray<AActor*> Pods;
	UGameplayStatics::GetAllActorsOfClass(World, AMSRepairPod::StaticClass(), Pods);
	TestEqual(TEXT("RepairPod type creates exactly one pod"), Pods.Num(), 1);
	if (const AMSRepairPod* Pod = Pods.Num() == 1 ? Cast<AMSRepairPod>(Pods[0]) : nullptr)
	{
		TestEqual(TEXT("RepairPod inherits the deployer's non-neutral faction"), Pod->Faction, DeployerTeam);
		TestEqual(TEXT("RepairPod uses DeployedPodLifetime"), Pod->Lifetime, 12.0f);
		TestTrue(TEXT("RepairPod is placed at the traced three-metre ground point"),
			Pod->GetActorLocation().Equals(FVector(300.0f, 0.0f, 0.0f), 1.0f));
	}

	EquipSupplyWeapon(Deployer, EMSDeployedItemType::SupplyPackRepair);
	TestTrue(TEXT("repair SupplyPack deploy succeeds on ground"), Ability->DeployItem(Deployer,
		Deployer->GetActorLocation(), FVector::ForwardVector));

	EquipSupplyWeapon(Deployer, EMSDeployedItemType::SupplyPackAmmo);
	TestTrue(TEXT("ammo SupplyPack deploy succeeds on ground"), Ability->DeployItem(Deployer,
		Deployer->GetActorLocation(), FVector::ForwardVector));

	TArray<AActor*> Packs;
	UGameplayStatics::GetAllActorsOfClass(World, AMSSupplyPack::StaticClass(), Packs);
	TestEqual(TEXT("both SupplyPack variants are created"), Packs.Num(), 2);
	int32 RepairCount = 0;
	int32 AmmoCount = 0;
	for (const AActor* Actor : Packs)
	{
		const AMSSupplyPack* Pack = CastChecked<AMSSupplyPack>(Actor);
		TestEqual(TEXT("every SupplyPack inherits the deployer's non-neutral faction"), Pack->Faction, DeployerTeam);
		TestTrue(TEXT("every SupplyPack receives DeployedPodLifetime"), Pack->GetLifeSpan() > 0.0f);
		RepairCount += Pack->SupplyPackType == EMSSupplyPackType::Repair;
		AmmoCount += Pack->SupplyPackType == EMSSupplyPackType::Ammo;
	}
	TestEqual(TEXT("repair SupplyPack variant is generated"), RepairCount, 1);
	TestEqual(TEXT("ammo SupplyPack variant is generated"), AmmoCount, 1);

	const int32 PodCountBeforeFailure = Pods.Num();
	const int32 PackCountBeforeFailure = Packs.Num();
	EquipSupplyWeapon(Deployer, EMSDeployedItemType::RepairPod);
	TestFalse(TEXT("deploy safely fails when no ground is found"), Ability->DeployItem(Deployer,
		FVector(20000.0f, 0.0f, 100.0f), FVector::ForwardVector));
	Pods.Reset();
	Packs.Reset();
	UGameplayStatics::GetAllActorsOfClass(World, AMSRepairPod::StaticClass(), Pods);
	UGameplayStatics::GetAllActorsOfClass(World, AMSSupplyPack::StaticClass(), Packs);
	TestEqual(TEXT("failed ground trace creates no RepairPod"), Pods.Num(), PodCountBeforeFailure);
	TestEqual(TEXT("failed ground trace creates no SupplyPack"), Packs.Num(), PackCountBeforeFailure);

	return true;
}

#endif
