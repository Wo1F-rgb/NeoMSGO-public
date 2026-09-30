// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSDestructibleTarget.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "UObject/ConstructorHelpers.h"

AMSDestructibleTarget::AMSDestructibleTarget()
{
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CoreCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("CoreCollision"));
	CoreCollision->SetupAttachment(SceneRoot);
	CoreCollision->SetBoxExtent(FVector(150.0f * MSGO::MSScale));
	CoreCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f * MSGO::MSScale));
	CoreCollision->SetCollisionProfileName(TEXT("MSCore"));

	CoreMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CoreMesh"));
	CoreMesh->SetupAttachment(SceneRoot);
	CoreMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f * MSGO::MSScale));
	CoreMesh->SetRelativeScale3D(FVector(3.0f * MSGO::MSScale));
	CoreMesh->SetCollisionProfileName(TEXT("MSNoCollision"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded()) CoreMesh->SetStaticMesh(CubeMesh.Object);

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	VitalSet = CreateDefaultSubobject<UMSAttributeSet_Vital>(TEXT("VitalSet"));
	bLockOnTargetable = true;
}

void AMSDestructibleTarget::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	if (!HasAuthority()) return;

	const FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(
		UGE_InitStats::StaticClass(), 1.0f, AbilitySystemComponent->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_MaxArmor, MaxArmor);
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_MaxDownPoint, 100.0f);
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Init_BoostCapacity, 0.0f);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

UAbilitySystemComponent* AMSDestructibleTarget::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool AMSDestructibleTarget::CanBeLockedOn() const
{
	return AbilitySystemComponent && !AbilitySystemComponent->HasMatchingGameplayTag(MSTags::State_Dead);
}

FVector AMSDestructibleTarget::GetLockOnAimPoint() const
{
	return GetActorLocation() + FVector(0.0f, 0.0f, 150.0f * MSGO::MSScale);
}
