// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyFieldSpawner.h"
#include "Gameplay/MSSupplyPack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSupplyFieldSpawnerP77Test, "NeoMSGO.Gameplay.SupplyFieldSpawner.P7_7",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSSupplyFieldSpawnerTestP77
{
	struct FScopedFieldWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSupplyFieldSpawnerTestP77World"));
		FScopedFieldWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedFieldWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	AActor* SpawnFieldGround(UWorld* World, const float SurfaceZ)
	{
		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* Collision = NewObject<UBoxComponent>(Ground, TEXT("SupplyFieldTestGround"));
		Ground->SetRootComponent(Collision);
		Collision->SetBoxExtent(FVector(30000.0f, 30000.0f, 50.0f));
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Collision->SetCollisionObjectType(ECC_WorldStatic);
		Collision->SetCollisionResponseToAllChannels(ECR_Block);
		Collision->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0f, 0.0f, SurfaceZ - 50.0f));
		return Ground;
	}

	template<typename T>
	TArray<T*> CollectFieldActors(UWorld* World)
	{
		TArray<T*> Result;
		for (TActorIterator<T> It(World); It; ++It) Result.Add(*It);
		return Result;
	}

	float HorizontalDistance(const FVector& A, const FVector& B)
	{
		return FVector2D::Distance(FVector2D(A), FVector2D(B));
	}
}

bool FMSSupplyFieldSpawnerP77Test::RunTest(const FString&)
{
	using namespace MSSupplyFieldSpawnerTestP77;
	{
		FScopedFieldWorld Scope;
		constexpr float GroundZ = 125.0f;
		SpawnFieldGround(Scope.World, GroundZ);
		AMSHomeBase* Base = Scope.World->SpawnActor<AMSHomeBase>(FVector(5000.0f, 0.0f, 600.0f), FRotator::ZeroRotator);
		Scope.World->SpawnActor<AMSSupplyFieldSpawner>();
		Scope.World->Tick(LEVELTICK_All, 0.016f);

		const TArray<AMSRepairPod*> Pods = CollectFieldActors<AMSRepairPod>(Scope.World);
		const TArray<AMSSupplyPack*> Packs = CollectFieldActors<AMSSupplyPack>(Scope.World);
		TestEqual(TEXT("one home base receives one repair pod"), Pods.Num(), 1);
		TestEqual(TEXT("one home base receives two supply packs"), Packs.Num(), 2);
		int32 RepairCount = 0, AmmoCount = 0;
		for (const AMSSupplyPack* Pack : Packs)
		{
			RepairCount += Pack->SupplyPackType == EMSSupplyPackType::Repair;
			AmmoCount += Pack->SupplyPackType == EMSSupplyPackType::Ammo;
		}
		TestEqual(TEXT("repair pack count"), RepairCount, 1);
		TestEqual(TEXT("ammo pack count"), AmmoCount, 1);
		for (const AActor* Item : TArray<const AActor*>({Pods[0], Packs[0], Packs[1]}))
		{
			TestTrue(TEXT("item distance from home base follows MSScale"),
				FMath::IsNearlyEqual(HorizontalDistance(Item->GetActorLocation(), Base->GetActorLocation()), 1400.0f * MSGO::MSScale, 1.0f));
			TestTrue(TEXT("item is snapped to ground Z"), FMath::IsNearlyEqual(Item->GetActorLocation().Z, GroundZ, 1.0f));
		}
	}

	{
		FScopedFieldWorld Scope;
		// Bases must sit well beyond SpawnDistance (1400cm * MSScale) from MapCenter, otherwise
		// the scaled item-placement radius overshoots the centre and lands closer to the
		// opposing base than to its own.
		AMSHomeBase* PlayerBase = Scope.World->SpawnActor<AMSHomeBase>(FVector(30000.0f, 15000.0f, 300.0f), FRotator::ZeroRotator);
		AMSHomeBase* EnemyBase = Scope.World->SpawnActor<AMSHomeBase>(FVector(-30000.0f, -15000.0f, -200.0f), FRotator::ZeroRotator);
		EnemyBase->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
		Scope.World->SpawnActor<AMSSupplyFieldSpawner>();
		Scope.World->Tick(LEVELTICK_All, 0.016f);

		const TArray<AMSRepairPod*> Pods = CollectFieldActors<AMSRepairPod>(Scope.World);
		const TArray<AMSSupplyPack*> Packs = CollectFieldActors<AMSSupplyPack>(Scope.World);
		TestEqual(TEXT("two bases receive two pods"), Pods.Num(), 2);
		TestEqual(TEXT("two bases receive four packs"), Packs.Num(), 4);
		int32 PlayerItems = 0, EnemyItems = 0;
		for (const AMSRepairPod* Pod : Pods)
		{
			const AMSHomeBase* Nearest = HorizontalDistance(Pod->GetActorLocation(), PlayerBase->GetActorLocation())
				< HorizontalDistance(Pod->GetActorLocation(), EnemyBase->GetActorLocation()) ? PlayerBase : EnemyBase;
			TestEqual(TEXT("pod faction matches its nearest home base"), Pod->Faction, Nearest->GetOwnerTeamId());
			(Pod->Faction == PlayerBase->GetOwnerTeamId() ? PlayerItems : EnemyItems)++;
			TestTrue(TEXT("pod fallback preserves base Z"), FMath::IsNearlyEqual(Pod->GetActorLocation().Z, Nearest->GetActorLocation().Z));
		}
		TestEqual(TEXT("exactly one player repair pod is placed"),
			Pods.FilterByPredicate([PlayerBase](const AMSRepairPod* Pod)
			{
				return Pod && Pod->Faction == PlayerBase->GetOwnerTeamId();
			}).Num(), 1);
		TestEqual(TEXT("exactly one enemy repair pod is placed"),
			Pods.FilterByPredicate([EnemyBase](const AMSRepairPod* Pod)
			{
				return Pod && Pod->Faction == EnemyBase->GetOwnerTeamId();
			}).Num(), 1);
		for (const AMSSupplyPack* Pack : Packs)
		{
			const AMSHomeBase* Nearest = HorizontalDistance(Pack->GetActorLocation(), PlayerBase->GetActorLocation())
				< HorizontalDistance(Pack->GetActorLocation(), EnemyBase->GetActorLocation()) ? PlayerBase : EnemyBase;
			TestEqual(TEXT("pack faction matches its nearest home base"), Pack->Faction, Nearest->GetOwnerTeamId());
			(Pack->Faction == PlayerBase->GetOwnerTeamId() ? PlayerItems : EnemyItems)++;
			TestTrue(TEXT("pack fallback preserves base Z"), FMath::IsNearlyEqual(Pack->GetActorLocation().Z, Nearest->GetActorLocation().Z));
		}
		TestEqual(TEXT("player base independently receives three items"), PlayerItems, 3);
		TestEqual(TEXT("enemy base independently receives three items"), EnemyItems, 3);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
