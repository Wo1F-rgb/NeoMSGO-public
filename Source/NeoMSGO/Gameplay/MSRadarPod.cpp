#include "Gameplay/MSRadarPod.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/MSCombatStatics.h"
#include "Core/MSTeamStatics.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "NeoMSGO.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	AActor* ResolveDeploymentIdentity(AActor* Deployer)
	{
		if (const APawn* Pawn = Cast<APawn>(Deployer))
		{
			if (APlayerState* State = Pawn->GetPlayerState()) return State;
			if (AController* Controller = Pawn->GetController()) return Controller;
		}
		return Deployer;
	}
}

AMSRadarPod::AMSRadarPod()
{
	bReplicates = true;
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	VitalSet = CreateDefaultSubobject<UMSAttributeSet_Vital>(TEXT("VitalSet"));
	SetReplicateMovement(true);
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("RadarHitBox"));
	SetRootComponent(HitBox);
	HitBox->SetBoxExtent(FVector(65.0f, 65.0f, 100.0f));
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RadarVisual"));
	Visual->SetupAttachment(HitBox);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
	Visual->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.5f));
}

void AMSRadarPod::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	if (!HasAuthority()) return;
	VitalSet->OnDeath.AddUniqueDynamic(this, &AMSRadarPod::HandlePodDeath);
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

UAbilitySystemComponent* AMSRadarPod::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool AMSRadarPod::IsActive() const
{
	return !IsActorBeingDestroyed() && VitalSet && VitalSet->GetArmor() > 0.0f;
}

void AMSRadarPod::HandlePodDeath(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || DeadActor != this || IsActorBeingDestroyed()) return;
	HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Authority destruction removes the replicated equipment and its sensing
	// contribution. It never registers as a combat unit or costs team gauge.
	Destroy();
}

void AMSRadarPod::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSRadarPod, ReplicatedTeamId);
	DOREPLIFETIME(AMSRadarPod, RangeStat);
}

FGenericTeamId AMSRadarPod::GetGenericTeamId() const
{
	return FGenericTeamId(ReplicatedTeamId);
}

void AMSRadarPod::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	Super::SetGenericTeamId(NewTeamId);
	ReplicatedTeamId = NewTeamId.GetId();
}

AMSRadarPod* AMSRadarPod::Spawn(UWorld* World, const FVector& Location, AActor* InDeployer,
	const FGenericTeamId Team, const float InRangeStat)
{
	if (!World || World->GetNetMode() == NM_Client || !IsValid(InDeployer) || Team == FGenericTeamId::NoTeam)
		return nullptr;
	AActor* Identity = ResolveDeploymentIdentity(InDeployer);
	AMSRadarPod* Oldest = nullptr;
	int32 ExistingCount = 0;
	for (TActorIterator<AMSRadarPod> It(World); It; ++It)
	{
		AMSRadarPod* Pod = *It;
		if (IsValid(Pod) && Pod->IsActive() && Pod->DeploymentIdentity.Get() == Identity)
		{
			++ExistingCount;
			if (!Oldest || Pod->GetGameTimeSinceCreation() > Oldest->GetGameTimeSinceCreation())
				Oldest = Pod;
		}
	}
	if (ExistingCount >= MaxPerDeployer && Oldest) Oldest->Destroy();
	FActorSpawnParameters Params;
	Params.Owner = InDeployer;
	Params.Instigator = Cast<APawn>(InDeployer);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSRadarPod* Pod = World->SpawnActor<AMSRadarPod>(StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (Pod)
	{
		Pod->Deployer = InDeployer;
		Pod->DeploymentIdentity = Identity;
		Pod->SetGenericTeamId(Team);
		Pod->RangeStat = FMath::Max(0.0f, InRangeStat);
		Pod->SetLifeSpan(LifetimeSeconds);
	}
	return Pod;
}

bool AMSRadarPod::ApplyWeaponDamage(const AActor* Attacker, const float Damage)
{
	// Compatibility helper: all damage, including this entry point, goes through
	// the same GAS execution as bullets, explosion packets and melee overlaps.
	if (!HasAuthority() || !IsActive() || !IsValid(Attacker) || Damage <= 0.0f ||
		UMSTeamStatics::SolveAttitude(UMSTeamStatics::GetTeamId(Attacker), GetGenericTeamId())
			!= ETeamAttitude::Hostile) return false;
	AActor* Source = const_cast<AActor*>(Attacker);
	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source);
	FGameplayEffectContextHandle Context = SourceASC ? SourceASC->MakeEffectContext()
		: AbilitySystemComponent->MakeEffectContext();
	Context.AddInstigator(Source, Source);
	FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
	Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Damage);
	Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
	const float Before = VitalSet->GetArmor();
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(Spec);
	return VitalSet->GetArmor() < Before;
}

bool AMSRadarPod::IsInsideRadarRange(const FVector& RadarPosition, const FVector& TargetPosition,
	const float RadiusCm)
{
	return RadiusCm >= 0.0f && FVector::DistSquared2D(RadarPosition, TargetPosition)
		<= FMath::Square(RadiusCm);
}
