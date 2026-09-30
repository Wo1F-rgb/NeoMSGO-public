#include "Gameplay/MSRepairPod.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponComponent.h"

namespace MSRepairPodVisuals
{
	constexpr int32 RingSegmentCount = 32;
	constexpr float RingHeight = 4.0f * MSGO::MSScale;
	// Preserve the previously approved tower-to-MS ratio at the common 18 m scale.
	constexpr float TowerScale = 0.11f * MSGO::MSScale;
	const FLinearColor FriendlyColor(0.08f, 1.0f, 0.32f, 1.0f);
	const FLinearColor EnemyColor(1.0f, 0.08f, 0.04f, 1.0f);
	const FLinearColor StructureColor(0.12f, 0.16f, 0.18f, 1.0f);
}

AMSRepairPod::AMSRepairPod()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// A dedicated query hitbox is shared by hitscan, projectile splash and both
	// melee overlap paths. Do not use MSCore: that profile marks home-base cores
	// for special explosion occlusion and gauge handling.
	DamageCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageCollision"));
	DamageCollision->SetupAttachment(SceneRoot);
	DamageCollision->SetBoxExtent(FVector(210.0f, 210.0f, 935.0f));
	DamageCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 880.0f));
	DamageCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageCollision->SetCollisionObjectType(ECC_WorldStatic);
	DamageCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	DamageCollision->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
	DamageCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	VitalSet = CreateDefaultSubobject<UMSAttributeSet_Vital>(TEXT("VitalSet"));
	bLockOnTargetable = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterialFinder(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;

	// Visual approximation of the post-2017 fixed home-base repair pod.
	// Reference: 2020-09-24 manufacturer release, ASCII images 3096961/3096962.
	// Dimensions retain the existing gameplay scale; reference images do not
	// establish measured dimensions. The dedicated damage box is unchanged.
	const auto TowerVector = [](const FVector& Value) { return Value * MSRepairPodVisuals::TowerScale; };
	CreateTowerPart(TEXT("Foundation"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 45.0f)), TowerVector(FVector(330.0f, 330.0f, 90.0f)), false);
	CreateTowerPart(TEXT("LowerPlinth"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 145.0f)), TowerVector(FVector(280.0f, 280.0f, 120.0f)), false);
	CreateTowerPart(TEXT("LowerHousing"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 410.0f)), TowerVector(FVector(250.0f, 250.0f, 530.0f)), false);
	CreateTowerPart(TEXT("EnergyChamber"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 895.0f)), TowerVector(FVector(222.0f, 222.0f, 480.0f)), true);
	CreateTowerPart(TEXT("UpperHousing"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 1310.0f)), TowerVector(FVector(245.0f, 245.0f, 370.0f)), false);
	CreateTowerPart(TEXT("ChamberLowerCollar"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 655.0f)), TowerVector(FVector(266.0f, 266.0f, 55.0f)), false);
	CreateTowerPart(TEXT("ChamberUpperCollar"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 1135.0f)), TowerVector(FVector(266.0f, 266.0f, 55.0f)), false);
	CreateTowerPart(TEXT("CrownDeck"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 1500.0f)), TowerVector(FVector(288.0f, 288.0f, 70.0f)), false);
	CreateTowerPart(TEXT("EmitterCap"), Cylinder, TowerVector(FVector(0.0f, 0.0f, 1545.0f)), TowerVector(FVector(228.0f, 228.0f, 30.0f)), false);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Angle = UE_HALF_PI * static_cast<float>(Index);
		const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		const FRotator RadialRotation(0.0f, 90.0f * static_cast<float>(Index), 0.0f);
		// Four separate ribs expose the luminous cylinder instead of a solid
		// square shaft. Stepped, splayed feet stay within the old footprint.
		CreateTowerPart(*FString::Printf(TEXT("FrameRib%d"), Index), Cube,
			TowerVector(Radial * 132.0f + FVector(0.0f, 0.0f, 850.0f)),
			TowerVector(FVector(50.0f, 56.0f, 1280.0f)), false)->SetRelativeRotation(RadialRotation);
		CreateTowerPart(*FString::Printf(TEXT("FrameFoot%d"), Index), Cube,
			TowerVector(Radial * 158.0f + FVector(0.0f, 0.0f, 105.0f)),
			TowerVector(FVector(90.0f, 90.0f, 210.0f)), false)->SetRelativeRotation(RadialRotation);
		CreateTowerPart(*FString::Printf(TEXT("FootShoulder%d"), Index), Cube,
			TowerVector(Radial * 145.0f + FVector(0.0f, 0.0f, 245.0f)),
			TowerVector(FVector(64.0f, 72.0f, 160.0f)), false)->SetRelativeRotation(RadialRotation);
		CreateTowerPart(*FString::Printf(TEXT("StatusRecess%d"), Index), Cube,
			TowerVector(Radial * 159.0f + FVector(0.0f, 0.0f, 1345.0f)),
			TowerVector(FVector(6.0f, 39.0f, 235.0f)), true)->SetRelativeRotation(RadialRotation);
		// Interrupt the upper status strip with dark horizontal grille bars.
		for (int32 Bar = 0; Bar < 7; ++Bar)
		{
			CreateTowerPart(*FString::Printf(TEXT("StatusGrille%d_%d"), Index, Bar), Cube,
				TowerVector(Radial * 164.0f + FVector(0.0f, 0.0f, 1245.0f + 33.0f * Bar)),
				TowerVector(FVector(7.0f, 47.0f, 10.0f)), false)->SetRelativeRotation(RadialRotation);
		}
	}

	BeaconRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BeaconRoot"));
	BeaconRoot->SetupAttachment(SceneRoot);
	BeaconRoot->SetRelativeLocation(TowerVector(FVector(0.0f, 0.0f, 1600.0f)));
	// The source shows a floating open-ended wrench, not a ball or cross.
	// This thickened silhouette remains readable as the existing marker turns.
	const FRotator WrenchTilt(40.0f, 0.0f, 0.0f);
	const auto WrenchPart = [&](const FName& Name, const FVector& Position, const FVector& Size)
	{
		UStaticMeshComponent* Part = CreateTowerPart(Name, Cube,
			TowerVector(WrenchTilt.RotateVector(Position) + FVector(0.0f, 0.0f, 130.0f)),
			TowerVector(Size), true, BeaconRoot);
		Part->SetRelativeRotation(WrenchTilt);
	};
	WrenchPart(TEXT("WrenchHandle"), FVector(0.0f, 0.0f, -45.0f), FVector(34.0f, 26.0f, 165.0f));
	WrenchPart(TEXT("WrenchHeadBridge"), FVector(0.0f, 0.0f, 50.0f), FVector(110.0f, 26.0f, 42.0f));
	WrenchPart(TEXT("WrenchJawLeft"), FVector(-44.0f, 0.0f, 95.0f), FVector(32.0f, 26.0f, 75.0f));
	WrenchPart(TEXT("WrenchJawRight"), FVector(44.0f, 0.0f, 95.0f), FVector(32.0f, 26.0f, 75.0f));

	for (int32 Index = 0; Index < MSRepairPodVisuals::RingSegmentCount; ++Index)
	{
		UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("HealRangeSegment%02d"), Index));
		Segment->SetupAttachment(SceneRoot);
		Segment->SetStaticMesh(Cube);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		RangeRingSegments.Add(Segment);
	}
	if (GlowMaterialFinder.Succeeded())
	{
		for (UStaticMeshComponent* Part : AccentParts) Part->SetMaterial(0, GlowMaterialFinder.Object);
		for (UStaticMeshComponent* Segment : RangeRingSegments) Segment->SetMaterial(0, GlowMaterialFinder.Object);
	}

	BeaconLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(BeaconRoot);
	BeaconLight->SetRelativeLocation(TowerVector(FVector(0.0f, 0.0f, 280.0f)));
	BeaconLight->SetIntensity(1800.0f);
	BeaconLight->SetAttenuationRadius(700.0f * MSGO::MSScale);
	BeaconLight->SetCastShadows(false);
	RangeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RangeLight"));
	RangeLight->SetupAttachment(SceneRoot);
	RangeLight->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f) * MSGO::MSScale);
	RangeLight->SetIntensity(700.0f);
	RangeLight->SetAttenuationRadius(HealRadius + 100.0f * MSGO::MSScale);
	RangeLight->SetCastShadows(false);
}

UStaticMeshComponent* AMSRepairPod::CreateTowerPart(const FName& Name, UStaticMesh* Mesh,
	const FVector& Location, const FVector& Size, const bool bAccent, USceneComponent* Parent)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Parent ? Parent : SceneRoot.Get());
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Size / 100.0f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCastShadow(!bAccent);
	if (bAccent) AccentParts.Add(Part);
	else StructureParts.Add(Part);
	return Part;
}

void AMSRepairPod::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateRangeRingGeometry();
}

void AMSRepairPod::BeginPlay()
{
	Super::BeginPlay();
	UpdateRangeRingGeometry();
	InitializeVisualMaterials();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	ApplyDestroyedState();
	if (!HasAuthority()) return;
	VitalSet->OnDeath.AddUniqueDynamic(this, &AMSRepairPod::HandlePodDeath);
	const FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(
		UGE_InitStats::StaticClass(), 1.0f, AbilitySystemComponent->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_MaxArmor, FMath::Max(1.0f, MaxArmor));
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_MaxDownPoint, 100.0f);
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_BoostCapacity, 0.0f);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
	if (Lifetime > 0.0f) SetLifeSpan(Lifetime);
}

UAbilitySystemComponent* AMSRepairPod::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMSRepairPod::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	Super::SetGenericTeamId(NewTeamId);
	Faction = NewTeamId;
	if (HasActorBegunPlay()) InitializeVisualMaterials();
}

bool AMSRepairPod::CanBeLockedOn() const
{
	return !bDestroyed && Faction != FGenericTeamId::NoTeam;
}

FVector AMSRepairPod::GetLockOnAimPoint() const
{
	return DamageCollision ? DamageCollision->GetComponentLocation() : GetActorLocation();
}

void AMSRepairPod::HandlePodDeath(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || DeadActor != this || bDestroyed) return;
	bDestroyed = true;
	ApplyDestroyedState();
	ForceNetUpdate();
}

void AMSRepairPod::ApplyDestroyedState()
{
	if (DamageCollision) DamageCollision->SetCollisionEnabled(
		bDestroyed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	for (UStaticMeshComponent* Part : AccentParts) if (Part) Part->SetVisibility(!bDestroyed);
	for (UStaticMeshComponent* Segment : RangeRingSegments) if (Segment) Segment->SetVisibility(!bDestroyed);
	if (BeaconLight) BeaconLight->SetVisibility(!bDestroyed);
	if (RangeLight) RangeLight->SetVisibility(!bDestroyed);
}

void AMSRepairPod::OnRep_Destroyed()
{
	ApplyDestroyedState();
}

void AMSRepairPod::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDestroyed) return;
	if (BeaconRoot) BeaconRoot->AddLocalRotation(FRotator(0.0f, 45.0f * DeltaSeconds, 0.0f));
	if (HasAuthority())
	{
		const float EffectiveInterval = FMath::Max(TickInterval, 0.01f);
		RepairTimeAccumulator += FMath::Max(0.0f, DeltaSeconds);
		while (RepairTimeAccumulator >= EffectiveInterval)
		{
			RepairTimeAccumulator -= EffectiveInterval;
			RepairUnitsInRange();
		}
	}
}

void AMSRepairPod::RepairUnitsInRange()
{
	if (bDestroyed || !HasAuthority() || !GetWorld()) return;
	const UMSUnitRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>();
	if (!Registry) return;
	const float RadiusSq = FMath::Square(FMath::Max(0.0f, HealRadius));
	Registry->ForEachRegisteredUnit([this, RadiusSq](AMSUnitBase* Unit)
	{
		if (!IsValid(Unit) || FVector::DistSquared(GetActorLocation(), Unit->GetActorLocation()) > RadiusSq) return;
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
		if (!ASC || ASC->HasMatchingGameplayTag(MSTags::State_Dead)) return;
		if (Faction != FGenericTeamId::NoTeam && !UMSTeamStatics::IsOnTeam(Unit, Faction)) return;
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGE_RepairPod::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(MSTags::Data_RepairPod, FMath::Max(0.0f, HealPerTick) * (1.f + Unit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ReceivedRepairIncrease) * .01f));
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
		// Shield durability is independent from Armor. A broken native shield
		// remains unavailable until this repair stream reaches full durability.
		if (UMSShieldComponent* Shield = Unit->GetShieldComponent())
		{
			Shield->Repair(FMath::Max(0.0f, HealPerTick) * (1.f + Unit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ReceivedRepairIncrease) * .01f));
		}
		if (UMSWeaponComponent* Weapons = Unit->GetWeaponComponent()) Weapons->RestoreAmmoTick(AmmoFractionPerTick);
		if (Cast<AMSPlayerCharacter>(Unit))
		{
			OnPlayerRepairTick(Unit);
			OnPlayerRepaired.Broadcast(Unit);
		}
	});
}

AMSRepairPod* AMSRepairPod::Spawn(UWorld* World, const FVector& Position, const float InLifetime,
	const FGenericTeamId InFaction)
{
	if (!World) return nullptr;
	AMSRepairPod* Pod = World->SpawnActorDeferred<AMSRepairPod>(StaticClass(), FTransform(Position), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Pod)
	{
		Pod->Lifetime = FMath::Max(0.0f, InLifetime);
		Pod->SetGenericTeamId(InFaction);
		UGameplayStatics::FinishSpawningActor(Pod, FTransform(Position));
	}
	return Pod;
}

AMSRepairPod* AMSRepairPod::SpawnRepairPod(const UObject* Context, const FVector Position,
	const float InLifetime, const FGenericTeamId InFaction)
{
	return Spawn(GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr,
		Position, InLifetime, InFaction);
}

void AMSRepairPod::UpdateRangeRingGeometry()
{
	const float Radius = FMath::Max(0.0f, HealRadius);
	const float SegmentLength = UE_TWO_PI * Radius / static_cast<float>(MSRepairPodVisuals::RingSegmentCount) * 0.72f;
	for (int32 Index = 0; Index < RangeRingSegments.Num(); ++Index)
	{
		const float Angle = UE_TWO_PI * static_cast<float>(Index) / static_cast<float>(RangeRingSegments.Num());
		if (UStaticMeshComponent* Segment = RangeRingSegments[Index])
		{
			Segment->SetRelativeLocation(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle),
				MSRepairPodVisuals::RingHeight));
			Segment->SetRelativeRotation(FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f));
			Segment->SetRelativeScale3D(FVector(0.08f * MSGO::MSScale, SegmentLength / 100.0f, 0.04f * MSGO::MSScale));
		}
	}
	if (RangeLight) RangeLight->SetAttenuationRadius(Radius + 100.0f * MSGO::MSScale);
}

FLinearColor AMSRepairPod::GetTeamVisualColor() const
{
	return Faction == MSTeam::EnemyTeamId() ? MSRepairPodVisuals::EnemyColor : MSRepairPodVisuals::FriendlyColor;
}

void AMSRepairPod::InitializeVisualMaterials()
{
	const FLinearColor TeamColor = GetTeamVisualColor();
	VisualMaterials.Reset();
	auto ApplyColor = [this](UStaticMeshComponent* Component, const FLinearColor& Color, const bool bGlow)
	{
		if (!Component) return;
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
		if (!MID) MID = Component->CreateAndSetMaterialInstanceDynamic(0);
		if (!MID) return;
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), Color * 6.0f);
		if (bGlow)
		{
			// M_SimpleGlow reads its switch at CPD 0 and RGBA at CPD 1..4.
			// MID parameters alone leave its particle-colour branch white.
			Component->SetCustomPrimitiveDataFloat(0, 1.0f);
			Component->SetCustomPrimitiveDataVector4(1, FVector4(Color.R, Color.G, Color.B, 1.0f));
		}
		VisualMaterials.AddUnique(MID);
	};
	for (UStaticMeshComponent* Part : StructureParts) ApplyColor(Part, MSRepairPodVisuals::StructureColor, false);
	for (UStaticMeshComponent* Part : AccentParts) ApplyColor(Part, TeamColor, true);
	for (UStaticMeshComponent* Segment : RangeRingSegments) ApplyColor(Segment, TeamColor, true);
	if (BeaconLight) BeaconLight->SetLightColor(TeamColor);
	if (RangeLight) RangeLight->SetLightColor(TeamColor);
}

void AMSRepairPod::OnRep_Faction()
{
	Super::SetGenericTeamId(Faction);
	InitializeVisualMaterials();
}

void AMSRepairPod::OnRep_HealRadius()
{
	UpdateRangeRingGeometry();
}

void AMSRepairPod::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSRepairPod, HealRadius);
	DOREPLIFETIME(AMSRepairPod, Faction);
	DOREPLIFETIME(AMSRepairPod, bDestroyed);
}
