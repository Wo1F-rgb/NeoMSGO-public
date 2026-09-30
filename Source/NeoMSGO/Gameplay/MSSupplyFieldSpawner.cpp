// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSSupplyFieldSpawner.h"
#include "NeoMSGO.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Core/MSScale.h"

namespace MSSupplyFieldSpawner
{
	constexpr float SpreadAngleDegrees = 40.0f;
	constexpr float GroundTraceHalfHeight = 10000.0f * MSGO::MSScale;
}

AMSSupplyFieldSpawner::AMSSupplyFieldSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void AMSSupplyFieldSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !GetWorld()) return;
	SpawnSupplies();
}

void AMSSupplyFieldSpawner::SpawnSupplies()
{
	if (!HasAuthority() || !GetWorld()) return;
	int32 SpawnedBaseCount = 0;

	for (TActorIterator<AMSHomeBase> It(GetWorld()); It; ++It)
	{
		++SpawnedBaseCount;
		AMSHomeBase* HomeBase = *It;
		const FVector BaseLocation = HomeBase->GetActorLocation();
		FVector CentreDirection = MapCenter - BaseLocation;
		CentreDirection.Z = 0.0f;
		// A placed MapCenter is more flexible than assuming origin; actor forward is deterministic at the centre itself.
		if (!CentreDirection.Normalize()) CentreDirection = GetActorForwardVector().GetSafeNormal2D();

		const FGenericTeamId Faction = HomeBase->GetOwnerTeamId();
		const FVector PodPosition = GroundPosition(BaseLocation + CentreDirection * SpawnDistance, HomeBase);
		const FVector RepairPosition = GroundPosition(BaseLocation
			+ CentreDirection.RotateAngleAxis(MSSupplyFieldSpawner::SpreadAngleDegrees, FVector::UpVector) * SpawnDistance,
			HomeBase);
		const FVector AmmoPosition = GroundPosition(BaseLocation
			+ CentreDirection.RotateAngleAxis(-MSSupplyFieldSpawner::SpreadAngleDegrees, FVector::UpVector) * SpawnDistance,
			HomeBase);

		AMSRepairPod::Spawn(GetWorld(), PodPosition, 0.0f, Faction);
		AMSSupplyPack::Spawn(GetWorld(), RepairPosition, EMSSupplyPackType::Repair, Faction);
		AMSSupplyPack::Spawn(GetWorld(), AmmoPosition, EMSSupplyPackType::Ammo, Faction);
	}
	UE_LOG(LogNeoMSGO, Display, TEXT("SupplyField: bases=%d"), SpawnedBaseCount);
}

FVector AMSSupplyFieldSpawner::GroundPosition(const FVector& Position, const AActor* HomeBase) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSSupplyFieldGroundSnap), false, this);
	QueryParams.AddIgnoredActor(HomeBase);
	const FVector Start = Position + FVector(0.0f, 0.0f, MSSupplyFieldSpawner::GroundTraceHalfHeight);
	const FVector End = Position - FVector(0.0f, 0.0f, MSSupplyFieldSpawner::GroundTraceHalfHeight);
	// Ground snapping is a visibility trace. ECC_WorldStatic is an object channel, and using
	// it here as a trace channel made the result depend on unrelated per-profile responses;
	// on Daibao that left several packs at the home-base Z hundreds of metres above terrain.
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams)) break;
		const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent());
		const FString MeshPath = StaticMeshComponent && StaticMeshComponent->GetStaticMesh()
			? StaticMeshComponent->GetStaticMesh()->GetPathName() : FString();
		// Daibao's building tiles contain broad roof collision. Initial field supplies are ground
		// pickups, so continue through those tiles; bases and ships remain valid authored surfaces.
		if (MeshPath.Contains(TEXT("/Environment/Daibao/Buildings/")) && Hit.GetActor())
		{
			QueryParams.AddIgnoredActor(Hit.GetActor());
			continue;
		}
		FVector Snapped = Position; Snapped.Z = Hit.ImpactPoint.Z; return Snapped;
	}
	return Position;
}
