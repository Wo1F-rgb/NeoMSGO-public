// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSHomeBase.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Gameplay/MSDestructibleTarget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace MSHomeBaseGeometry
{
	constexpr float Half = 800.0f * MSGO::MSScale;
	constexpr float Height = 800.0f * MSGO::MSScale;
	constexpr float Thickness = 60.0f * MSGO::MSScale;
	constexpr float EntranceWidth = 600.0f * MSGO::MSScale;
	constexpr float EntranceHeight = 550.0f * MSGO::MSScale;
}

AMSHomeBase::AMSHomeBase()
{
	using namespace MSHomeBaseGeometry;
	CreateBuildingPart(TEXT("Wall_Back"), FVector(-Half, 0.0f, Height * 0.5f), FVector(Thickness, Half * 2.0f, Height));
	CreateBuildingPart(TEXT("Wall_Left"), FVector(0.0f, -Half, Height * 0.5f), FVector(Half * 2.0f, Thickness, Height));
	CreateBuildingPart(TEXT("Wall_Right"), FVector(0.0f, Half, Height * 0.5f), FVector(Half * 2.0f, Thickness, Height));
	CreateBuildingPart(TEXT("Wall_FrontLeft"), FVector(Half, -550.0f * MSGO::MSScale, Height * 0.5f), FVector(Thickness, 500.0f * MSGO::MSScale, Height));
	CreateBuildingPart(TEXT("Wall_FrontRight"), FVector(Half, 550.0f * MSGO::MSScale, Height * 0.5f), FVector(Thickness, 500.0f * MSGO::MSScale, Height));
	CreateBuildingPart(TEXT("Lintel"), FVector(Half, 0.0f, EntranceHeight + 125.0f * MSGO::MSScale), FVector(Thickness, EntranceWidth, 250.0f * MSGO::MSScale));
	CreateBuildingPart(TEXT("Roof"), FVector(0.0f, 0.0f, Height + Thickness * 0.5f), FVector(Half * 2.0f, Half * 2.0f, Thickness));
	CreateBuildingPart(TEXT("Wall_Screen"), FVector(400.0f, 0.0f, 175.0f) * MSGO::MSScale, FVector(Thickness, 700.0f * MSGO::MSScale, 350.0f * MSGO::MSScale));
}

void AMSHomeBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// An authored level must be collision-correct in the editor and in cooked maps,
	// before BeginPlay spawns the separate GAS core.
	ApplyHomeBaseTypeVisuals();
}

UStaticMeshComponent* AMSHomeBase::CreateBuildingPart(const FName Name, const FVector& Location, const FVector& Size)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(SceneRoot);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Size / 100.0f);
	Part->SetCollisionProfileName(TEXT("MSStructure"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded()) Part->SetStaticMesh(CubeMesh.Object);
	BuildingParts.Add(Part);
	return Part;
}

void AMSHomeBase::SetHomeBaseType(const EMSHomeBaseType NewType)
{
	HomeBaseType = NewType;
	ApplyHomeBaseTypeVisuals();
	if (HasActorBegunPlay()) UpdateVisuals();
}

float AMSHomeBase::GetDefaultMaxArmor() const
{
	return HomeBaseType == EMSHomeBaseType::Fortress ? 100000.0f : 75000.0f;
}

void AMSHomeBase::ApplyHomeBaseTypeVisuals()
{
	for (UStaticMeshComponent* Part : BuildingParts)
	{
		if (!Part) continue;
		Part->SetVisibility(!bUseAuthoredExterior, true);
		Part->SetHiddenInGame(bUseAuthoredExterior, true);
		if (bUseAuthoredExterior) Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		else Part->SetCollisionProfileName(TEXT("MSStructure"));
		Part->SetCanEverAffectNavigation(!bUseAuthoredExterior);
	}
	if (bUseAuthoredExterior) return;

	FVector Proportion(1.0f);
	switch (HomeBaseType)
	{
	case EMSHomeBaseType::Battleship:
		Proportion = FVector(1.65f, 0.72f, 0.72f);
		break;
	case EMSHomeBaseType::CommandPost:
		Proportion = FVector(0.78f, 0.78f, 1.45f);
		break;
	case EMSHomeBaseType::Reactor:
		Proportion = FVector(1.18f, 1.18f, 0.82f);
		break;
	default:
		break;
	}

	using namespace MSHomeBaseGeometry;
	const FVector DefaultLocations[] = {
		FVector(-Half, 0.0f, Height * 0.5f),
		FVector(0.0f, -Half, Height * 0.5f),
		FVector(0.0f, Half, Height * 0.5f),
		FVector(Half, -550.0f * MSGO::MSScale, Height * 0.5f),
		FVector(Half, 550.0f * MSGO::MSScale, Height * 0.5f),
		FVector(Half, 0.0f, EntranceHeight + 125.0f * MSGO::MSScale),
		FVector(0.0f, 0.0f, Height + Thickness * 0.5f),
		FVector(400.0f, 0.0f, 175.0f) * MSGO::MSScale
	};
	const FVector DefaultScales[] = {
		FVector(Thickness, Half * 2.0f, Height) / 100.0f,
		FVector(Half * 2.0f, Thickness, Height) / 100.0f,
		FVector(Half * 2.0f, Thickness, Height) / 100.0f,
		FVector(Thickness, 500.0f * MSGO::MSScale, Height) / 100.0f,
		FVector(Thickness, 500.0f * MSGO::MSScale, Height) / 100.0f,
		FVector(Thickness, EntranceWidth, 250.0f * MSGO::MSScale) / 100.0f,
		FVector(Half * 2.0f, Half * 2.0f, Thickness) / 100.0f,
		FVector(Thickness, 700.0f * MSGO::MSScale, 350.0f * MSGO::MSScale) / 100.0f
	};
	const int32 PartCount = FMath::Min(BuildingParts.Num(), static_cast<int32>(UE_ARRAY_COUNT(DefaultLocations)));
	for (int32 Index = 0; Index < PartCount; ++Index)
	{
		if (UStaticMeshComponent* Part = BuildingParts[Index])
		{
			Part->SetRelativeLocation(DefaultLocations[Index] * Proportion);
			Part->SetRelativeScale3D(DefaultScales[Index] * Proportion);
		}
	}
}

void AMSHomeBase::BeginPlay()
{
	Super::BeginPlay();
	ApplyHomeBaseTypeVisuals();
	if (!HasAuthority()) return;

	SetFixedOwner(HomeBaseFaction);
	const FTransform CoreTransform(GetActorRotation(), GetActorTransform().TransformPosition(CoreLocalOffset));
	CoreTarget = GetWorld()->SpawnActorDeferred<AMSDestructibleTarget>(
		AMSDestructibleTarget::StaticClass(), CoreTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (CoreTarget)
	{
		CoreTarget->SetMaxArmorSetting(GetDefaultMaxArmor());
		CoreTarget->FinishSpawning(CoreTransform);
		CoreTarget->SetGenericTeamId(UMSTeamStatics::CaptureOwnerToTeamId(HomeBaseFaction));
		CoreTarget->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		CoreTarget->GetVitalSet()->OnDeath.AddDynamic(this, &AMSHomeBase::HandleCoreDeath);
		// Partial damage must be observable too: the battle gauge tracks accumulated core
		// damage, not only the final destruction event (task #226).
		CoreTarget->GetVitalSet()->OnDamagedFrom.AddDynamic(this, &AMSHomeBase::HandleCoreDamaged);
	}
}

float AMSHomeBase::GetCoreArmorLostRatio() const
{
	if (bDestroyed) return 1.0f;
	const UMSAttributeSet_Vital* Vital = CoreTarget ? CoreTarget->GetVitalSet() : nullptr;
	if (!Vital) return 0.0f;
	const float MaxArmor = Vital->GetMaxArmor();
	if (MaxArmor <= 0.0f) return 0.0f;
	return FMath::Clamp(1.0f - Vital->GetArmor() / MaxArmor, 0.0f, 1.0f);
}

void AMSHomeBase::HandleCoreDamaged(AActor*, AActor*, FVector)
{
	if (!HasAuthority() || bDestroyed) return;
	OnHomeBaseDamaged.Broadcast(this, HomeBaseFaction);
}

void AMSHomeBase::HandleCoreDeath(AActor*, AActor*)
{
	if (!HasAuthority() || bDestroyed) return;
	bDestroyed = true;
	// A destroyed base must stop intercepting attacks through its former damage receiver.
	// The building remains as world geometry while the core no longer produces hit confirms.
	if (CoreTarget) CoreTarget->SetActorEnableCollision(false);
	SunkenTargetLocation = GetActorLocation() + FVector(0.0f, 0.0f, -200.0f * MSGO::MSScale);
	SetReplicateMovement(true);
	++DestructionBroadcastCount;
	OnHomeBaseDestroyed.Broadcast(this, HomeBaseFaction);
}

UAbilitySystemComponent* AMSHomeBase::GetAbilitySystemComponent() const
{
	// Building parts are components of this actor, whereas the GAS owner is the separately
	// spawned core. Forwarding here makes every exterior WeaponTrace hit use the same Armor,
	// death and hit-feedback path as a direct core hit.
	return !bDestroyed && CoreTarget ? CoreTarget->GetAbilitySystemComponent() : nullptr;
}

void AMSHomeBase::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	const EMSCaptureOwner NewFaction = UMSTeamStatics::TeamIdToCaptureOwner(NewTeamId);
	if (NewFaction == EMSCaptureOwner::Neutral) return;
	HomeBaseFaction = NewFaction;
	if (HasAuthority()) SetFixedOwner(HomeBaseFaction);
	if (CoreTarget) CoreTarget->SetGenericTeamId(NewTeamId);
}

void AMSHomeBase::OnRep_HomeBaseFaction()
{
	// CurrentOwner has its own authoritative replication stream. A faction packet
	// may arrive before or after it; only refresh faction-dependent presentation.
	UpdateVisuals();
}

FGenericTeamId AMSHomeBase::GetGenericTeamId() const
{
	return UMSTeamStatics::CaptureOwnerToTeamId(HomeBaseFaction);
}

bool AMSHomeBase::CanDeployFor(const FGenericTeamId TeamId) const
{
	return Super::CanDeployFor(TeamId) && !IsHomeBaseDestroyed();
}

void AMSHomeBase::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bDestroyed || DeltaSeconds <= 0.0f) return;
	const FVector Current = GetActorLocation();
	const FVector Next = FMath::VInterpConstantTo(Current, SunkenTargetLocation, DeltaSeconds, SinkSpeed);
	if (!Next.Equals(Current)) SetActorLocation(Next);
}

void AMSHomeBase::ApplyBuildingColor(UStaticMeshComponent* Component, UMaterialInstanceDynamic*& Material,
	const FLinearColor& Color)
{
	if (!Material) Material = Component->CreateAndSetMaterialInstanceDynamic(0);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}
}

void AMSHomeBase::UpdateVisuals_Implementation()
{
	ApplyHomeBaseTypeVisuals();
	if (FlagPole) FlagPole->SetVisibility(false, true);
	if (Flag) Flag->SetVisibility(false, true);
	if (RangeRing) RangeRing->SetVisibility(false, true);

	FLinearColor TeamColor(0.75f, 0.75f, 0.75f, 1.0f);
	if (HomeBaseFaction == EMSCaptureOwner::TeamPlayer) TeamColor = FLinearColor(0.25f, 0.55f, 1.0f, 1.0f);
	else if (HomeBaseFaction == EMSCaptureOwner::TeamEnemy) TeamColor = FLinearColor(0.85f, 0.2f, 0.15f, 1.0f);
	FLinearColor TypeColor(0.52f, 0.52f, 0.52f, 1.0f);
	switch (HomeBaseType)
	{
	case EMSHomeBaseType::Battleship:
		TypeColor = FLinearColor(0.16f, 0.25f, 0.38f, 1.0f);
		break;
	case EMSHomeBaseType::CommandPost:
		TypeColor = FLinearColor(0.62f, 0.48f, 0.18f, 1.0f);
		break;
	case EMSHomeBaseType::Reactor:
		TypeColor = FLinearColor(0.12f, 0.52f, 0.34f, 1.0f);
		break;
	default:
		break;
	}
	const FLinearColor WallColor = FMath::Lerp(TypeColor, TeamColor, 0.35f);
	if (bUseAuthoredExterior) return;
	BuildingMaterials.SetNum(BuildingParts.Num());
	for (int32 Index = 0; Index < BuildingParts.Num(); ++Index)
	{
		UMaterialInstanceDynamic* MID = BuildingMaterials[Index];
		ApplyBuildingColor(BuildingParts[Index], MID, WallColor);
		BuildingMaterials[Index] = MID;
	}
}

void AMSHomeBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSHomeBase, bDestroyed);
	DOREPLIFETIME(AMSHomeBase, HomeBaseFaction);
}
