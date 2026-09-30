// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Deploy.h"

#include "Core/MSTeamStatics.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSRadarPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Weapons/MSWeaponComponent.h"

UGA_Deploy::UGA_Deploy()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Deploy::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Deployer = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bDeployed = Deployer && DeployItem(Deployer,
		Deployer->GetActorLocation(), Deployer->GetActorForwardVector());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bDeployed);
}

bool UGA_Deploy::DeployItem(AActor* Deployer, const FVector& Origin, const FVector& ForwardDirection)
{
	if (!Deployer || !Deployer->HasAuthority() || ForwardDirection.IsNearlyZero())
	{
		return false;
	}

	UWorld* World = Deployer->GetWorld();
	UMSWeaponComponent* Weapons = Deployer->FindComponentByClass<UMSWeaponComponent>();
	if (!World || !Weapons)
	{
		return false;
	}

	const int32 SlotIndex = Weapons->GetActiveSlotIndex();
	const UMSSupplyWeaponData* Weapon = Cast<UMSSupplyWeaponData>(Weapons->GetWeaponData(SlotIndex));
	if (!Weapon)
	{
		return false;
	}

	const FVector TraceStart = Origin + ForwardDirection.GetSafeNormal() * DeployDistanceCm;
	const FVector TraceEnd = TraceStart - FVector::UpVector * GroundTraceDepthCm;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSDeployGroundTrace), false, Deployer);
	FHitResult GroundHit;
	if (!World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return false;
	}

	// Authorize/consume the weapon only after a valid placement point exists.
	if (!Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	// P4-9 regression fix: never use the Spawn default (NoTeam) here. The avatar's resolved
	// team supports both player and BOT team-agent implementations.
	const FGenericTeamId Faction = UMSTeamStatics::GetTeamId(Deployer);
	AActor* DeployedActor = nullptr;
	switch (Weapon->DeployedItemType)
	{
	case EMSDeployedItemType::RepairPod:
		DeployedActor = AMSRepairPod::Spawn(World, GroundHit.ImpactPoint,
			Weapon->DeployedPodLifetime, Faction);
		break;
	case EMSDeployedItemType::SupplyPackRepair:
		DeployedActor = AMSSupplyPack::Spawn(World, GroundHit.ImpactPoint,
			EMSSupplyPackType::Repair, Faction);
		break;
	case EMSDeployedItemType::SupplyPackAmmo:
		DeployedActor = AMSSupplyPack::Spawn(World, GroundHit.ImpactPoint,
			EMSSupplyPackType::Ammo, Faction);
		break;
	case EMSDeployedItemType::RadarPod:
		DeployedActor = AMSRadarPod::Spawn(World, GroundHit.ImpactPoint,
			Deployer, Faction, Weapon->RadarRangeStat);
		break;
	default:
		return false;
	}

	if (DeployedActor && Weapon->DeployedPodLifetime > 0.0f &&
		Weapon->DeployedItemType != EMSDeployedItemType::RepairPod &&
		Weapon->DeployedItemType != EMSDeployedItemType::RadarPod)
	{
		// SupplyPack::Spawn has no lifetime parameter, so apply the same weapon-data lifetime here.
		DeployedActor->SetLifeSpan(Weapon->DeployedPodLifetime);
	}
	return DeployedActor != nullptr;
}
