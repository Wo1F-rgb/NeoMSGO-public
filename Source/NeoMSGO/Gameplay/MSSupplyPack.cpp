#include "Gameplay/MSSupplyPack.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponComponent.h"

namespace MSSupplyPackVisuals
{
	constexpr float BaseHeight = 70.0f * MSGO::MSScale;
	constexpr float BobAmplitude = 15.0f * MSGO::MSScale;
	constexpr float RotationDegreesPerSecond = 45.0f;
}

AMSSupplyPack::AMSSupplyPack()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(SceneRoot);
	PickupSphere->InitSphereRadius(PickupRadius);
	PickupSphere->SetCollisionProfileName(TEXT("MSDeployable"));
	PickupSphere->SetGenerateOverlapEvents(true);

	FloatingVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FloatingVisualRoot"));
	FloatingVisualRoot->SetupAttachment(SceneRoot);
	FloatingVisualRoot->SetRelativeLocation(FVector(0, 0, MSSupplyPackVisuals::BaseHeight));
	BoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Box")); BoxMesh->SetupAttachment(FloatingVisualRoot);
	AccentA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AccentA")); AccentA->SetupAttachment(FloatingVisualRoot);
	AccentB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AccentB")); AccentB->SetupAttachment(FloatingVisualRoot);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Round = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("AmmoRound%d"), Index));
		Round->SetupAttachment(FloatingVisualRoot); AmmoRounds.Add(Round);
	}
	for (UStaticMeshComponent* Component : { BoxMesh.Get(), AccentA.Get(), AccentB.Get() }) Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (UStaticMeshComponent* Component : AmmoRounds) Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cube.Succeeded()) { BoxMesh->SetStaticMesh(Cube.Object); AccentA->SetStaticMesh(Cube.Object); AccentB->SetStaticMesh(Cube.Object); }
	if (Cylinder.Succeeded()) for (UStaticMeshComponent* Component : AmmoRounds) Component->SetStaticMesh(Cylinder.Object);
}

void AMSSupplyPack::BeginPlay()
{
	Super::BeginPlay();
	PickupSphere->SetSphereRadius(FMath::Max(0.0f, PickupRadius), true);
	PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AMSSupplyPack::OnPickupBeginOverlap);
	PickupSphere->OnComponentEndOverlap.AddDynamic(this, &AMSSupplyPack::OnPickupEndOverlap);
	// Components are already registered when BeginPlay runs in a begun world, so
	// seed players that overlapped during registration before delegates were bound.
	TArray<AActor*> ExistingOverlaps;
	PickupSphere->GetOverlappingActors(ExistingOverlaps, AMSPlayerCharacter::StaticClass());
	for (AActor* Actor : ExistingOverlaps)
	{
		if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(Actor)) Player->AddNearbySupplyPack(this);
	}
	ConfigureVisuals();
}

void AMSSupplyPack::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimationTime += FMath::Max(0.0f, DeltaSeconds);
	const float Height = MSSupplyPackVisuals::BaseHeight + MSSupplyPackVisuals::BobAmplitude * FMath::Sin(AnimationTime);
	FloatingVisualRoot->SetRelativeLocation(FVector(0, 0, Height));
	FloatingVisualRoot->AddLocalRotation(FRotator(0, MSSupplyPackVisuals::RotationDegreesPerSecond * DeltaSeconds, 0));
}

bool AMSSupplyPack::TryConsume(AMSUnitBase* Consumer)
{
	if (!HasAuthority() || !IsValid(Consumer) || !Consumer->HasAuthority() || IsActorBeingDestroyed()) return false;
	if (Faction != FGenericTeamId::NoTeam && !UMSTeamStatics::IsOnTeam(Consumer, Faction)) return false;

	if (SupplyPackType == EMSSupplyPackType::Repair)
	{
		UMSAttributeSet_Vital* Vital = Consumer->GetVitalSet();
		UAbilitySystemComponent* ASC = Consumer->GetAbilitySystemComponent();
		if (!Vital || !ASC || Vital->GetArmor() >= Vital->GetMaxArmor()) return false;
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGE_RepairPod::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_RepairPod, FMath::Max(0.0f, HealAmount) * (1.f + Consumer->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ReceivedRepairIncrease) * .01f));
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
	else
	{
		UMSWeaponComponent* Weapons = Consumer->GetWeaponComponent();
		if (!Weapons) return false;
		Weapons->RefillAllAmmo();
	}

	OnSupplyPickedUp.Broadcast(Consumer);
	Destroy();
	return true;
}

AMSSupplyPack* AMSSupplyPack::Spawn(UWorld* World, const FVector& Position, const EMSSupplyPackType InType, const FGenericTeamId InFaction)
{
	if (!World) return nullptr;
	AMSSupplyPack* Pack = World->SpawnActorDeferred<AMSSupplyPack>(StaticClass(), FTransform(Position), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Pack)
	{
		Pack->SupplyPackType = InType; Pack->Faction = InFaction;
		UGameplayStatics::FinishSpawningActor(Pack, FTransform(Position));
	}
	return Pack;
}

AMSSupplyPack* AMSSupplyPack::SpawnSupplyPack(const UObject* Context, const FVector Position,
	const EMSSupplyPackType InType, const FGenericTeamId InFaction)
{
	return Spawn(GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr,
		Position, InType, InFaction);
}

void AMSSupplyPack::OnPickupBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(OtherActor)) Player->AddNearbySupplyPack(this);
}

void AMSSupplyPack::OnPickupEndOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32)
{
	if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(OtherActor)) Player->RemoveNearbySupplyPack(this);
}

void AMSSupplyPack::ConfigureVisuals()
{
	const bool bRepair = SupplyPackType == EMSSupplyPackType::Repair;
	BoxMesh->SetRelativeScale3D(FVector(0.8f, 0.55f, 0.4f) * MSGO::MSScale);
	AccentA->SetVisibility(true); AccentB->SetVisibility(true);
	AccentA->SetRelativeScale3D((bRepair ? FVector(0.12f, 0.58f, 0.08f) : FVector(0.82f, 0.58f, 0.06f)) * MSGO::MSScale);
	AccentB->SetRelativeScale3D((bRepair ? FVector(0.58f, 0.12f, 0.08f) : FVector(0.82f, 0.06f, 0.42f)) * MSGO::MSScale);
	AccentA->SetRelativeLocation(FVector(0, 0, 43) * MSGO::MSScale); AccentB->SetRelativeLocation(FVector(0, 0, 43) * MSGO::MSScale);
	const FLinearColor BoxColor = bRepair ? FLinearColor(0.9f, 0.9f, 0.9f) : FLinearColor(0.35f, 0.16f, 0.05f);
	const FLinearColor AccentColor = bRepair ? FLinearColor(0.1f, 0.9f, 0.25f) : FLinearColor(1.0f, 0.75f, 0.05f);
	ApplyVisualColor(BoxMesh, BoxColor); ApplyVisualColor(AccentA, AccentColor); ApplyVisualColor(AccentB, AccentColor);
	for (int32 Index = 0; Index < AmmoRounds.Num(); ++Index)
	{
		UStaticMeshComponent* Round = AmmoRounds[Index]; Round->SetVisibility(!bRepair);
		Round->SetRelativeLocation(FVector(-25.0f + Index * 25.0f, 0, 55) * MSGO::MSScale);
		Round->SetRelativeScale3D(FVector(0.09f, 0.09f, 0.35f) * MSGO::MSScale); ApplyVisualColor(Round, AccentColor);
	}
}

void AMSSupplyPack::ApplyVisualColor(UStaticMeshComponent* Component, const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* MID = Component ? Component->CreateAndSetMaterialInstanceDynamic(0) : nullptr)
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color); MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		VisualMaterials.Add(MID);
	}
}
