#include "Combat/MSHitReactionComponent.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Player/MSPlayerCharacter.h"
#include "Net/UnrealNetwork.h"

UMSHitReactionComponent::UMSHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick=true;
	SetIsReplicatedByDefault(true);
}
void UMSHitReactionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Animator=GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>()) AddTickPrerequisiteComponent(Animator);
	if (auto* Visual=GetOwner()->FindComponentByClass<UMSMeshVisualComponent>()) AddTickPrerequisiteComponent(Visual);
}
void UMSHitReactionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMSHitReactionComponent,State);
}
double UMSHitReactionComponent::ServerTime() const
{
	const UWorld* W=GetWorld();
	const AGameStateBase* GS=W?W->GetGameState():nullptr;
	return GS?GS->GetServerWorldTimeSeconds():(W?W->GetTimeSeconds():0.);
}
float UMSHitReactionComponent::GetPhaseElapsed() const { return FMath::Max(0.,ServerTime()-State.StartServerTime); }
bool UMSHitReactionComponent::BlocksCombat(const UAbilitySystemComponent* ASC)
{
	return ASC && (ASC->HasMatchingGameplayTag(MSTags::State_Dead)
		|| ASC->HasMatchingGameplayTag(MSTags::State_Flinched)
		|| ASC->HasMatchingGameplayTag(MSTags::State_Staggered)
		|| ASC->HasMatchingGameplayTag(MSTags::State_KnockedDown)
		|| ASC->HasMatchingGameplayTag(MSTags::State_GettingUp));
}
bool UMSHitReactionComponent::ProtectsDownGauge(const UAbilitySystemComponent* ASC)
{
	return ASC && (ASC->HasMatchingGameplayTag(MSTags::State_Dead)
		|| ASC->HasMatchingGameplayTag(MSTags::State_ReactionProtected)
		|| ASC->HasMatchingGameplayTag(MSTags::State_Staggered)
		|| ASC->HasMatchingGameplayTag(MSTags::State_KnockedDown)
		|| ASC->HasMatchingGameplayTag(MSTags::State_GettingUp));
}
void UMSHitReactionComponent::ApplyTags()
{
	auto* Unit=Cast<AMSUnitBase>(GetOwner());
	auto* ASC=Unit?Unit->GetAbilitySystemComponent():nullptr;
	if (!ASC) return;
	const auto P=State.Phase;
	ASC->SetLooseGameplayTagCount(MSTags::State_Flinched,P==EMSHitReactionPhase::Flinch?1:0);
	ASC->SetLooseGameplayTagCount(MSTags::State_Staggered,P==EMSHitReactionPhase::Stagger?1:0);
	ASC->SetLooseGameplayTagCount(MSTags::State_KnockedDown,
		P==EMSHitReactionPhase::FallingDown||P==EMSHitReactionPhase::Downed?1:0);
	ASC->SetLooseGameplayTagCount(MSTags::State_GettingUp,P==EMSHitReactionPhase::GettingUp?1:0);
	ASC->SetLooseGameplayTagCount(MSTags::State_ReactionProtected,
		P!=EMSHitReactionPhase::None&&P!=EMSHitReactionPhase::Flinch?1:0);
}
void UMSHitReactionComponent::OnRep_State() { ApplyTags(); }
void UMSHitReactionComponent::SetPhase(EMSHitReactionPhase P)
{
	State.Phase=P; State.StartServerTime=ServerTime(); ++State.Sequence;
	if (P==EMSHitReactionPhase::None) State.bProne=false;
	ApplyTags(); GetOwner()->ForceNetUpdate();
	if (P!=EMSHitReactionPhase::None)
		if (auto* Player=Cast<AMSPlayerCharacter>(GetOwner())) Player->CancelCurrentWeaponFire();
}
void UMSHitReactionComponent::StartFlinch()
{
	if (GetOwner()->HasAuthority() && State.Phase==EMSHitReactionPhase::None) SetPhase(EMSHitReactionPhase::Flinch);
}
void UMSHitReactionComponent::StartHeavyReaction(bool bForceKnockdown)
{
	auto* Unit=Cast<AMSUnitBase>(GetOwner());
	if (!Unit || !Unit->HasAuthority() || ProtectsDownGauge(Unit->GetAbilitySystemComponent())) return;
	const bool bFall=bForceKnockdown || !Unit->GetCharacterMovement()->IsMovingOnGround();
	State.bProne=bFall;
	Unit->GetVitalSet()->SetDownPoint(0);
	SetPhase(bFall?EMSHitReactionPhase::FallingDown:EMSHitReactionPhase::Stagger);
	Unit->GetVitalSet()->OnStagger.Broadcast(Unit,bFall);
	FGameplayEventData Event;
	Event.EventTag=MSTags::Event_Stagger;Event.Target=Unit;Event.EventMagnitude=bFall?1.f:0.f;
	Unit->GetAbilitySystemComponent()->HandleGameplayEvent(MSTags::Event_Stagger,&Event);
}
void UMSHitReactionComponent::ResetReaction()
{
	if (GetOwner()->HasAuthority()) SetPhase(EMSHitReactionPhase::None);
	if (ReactionRoot) ReactionRoot->SetRelativeTransform(FTransform::Identity);
	if (ReactionBodyHitbox) ReactionBodyHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (auto* Unit=Cast<AMSUnitBase>(GetOwner())) Unit->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
}
void UMSHitReactionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Dt,Type,Tick);
	auto* Unit=Cast<AMSUnitBase>(GetOwner());
	if (!Unit) return;
	if (Unit->HasAuthority())
	{
		const float T=GetPhaseElapsed();
		if (Unit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead))
		{
			if (State.Phase!=EMSHitReactionPhase::None) ResetReaction();
		}
		else switch (State.Phase)
		{
		case EMSHitReactionPhase::Flinch: if(T>=FlinchSeconds) SetPhase(EMSHitReactionPhase::None); break;
		case EMSHitReactionPhase::Stagger: if(T>=StaggerSeconds) SetPhase(EMSHitReactionPhase::GettingUp); break;
		case EMSHitReactionPhase::FallingDown:
			if(T>=FallBlendSeconds && (Unit->GetCharacterMovement()->IsMovingOnGround()
				|| Unit->GetCharacterMovement()->IsSwimming() || Unit->GetCharacterMovement()->IsFlying()))
				SetPhase(EMSHitReactionPhase::Downed);
			break;
		case EMSHitReactionPhase::Downed: if(T>=DownedSeconds) SetPhase(EMSHitReactionPhase::GettingUp); break;
		case EMSHitReactionPhase::GettingUp: if(T>=GetUpSeconds) SetPhase(EMSHitReactionPhase::None); break;
		default: break;
		}
	}
	ApplyPresentation();
}
void UMSHitReactionComponent::ApplyPresentation()
{
	auto* Unit=Cast<AMSUnitBase>(GetOwner());
	auto* Rig=Unit?Unit->GetMechRigComponent():nullptr;
	if (!Rig || !Rig->GetRigRoot()) return;
	if (!ReactionRoot)
	{
		ReactionRoot=NewObject<USceneComponent>(Unit,TEXT("HitReactionRoot"));
		Unit->AddInstanceComponent(ReactionRoot);
		ReactionRoot->SetupAttachment(Unit->GetRootComponent());
		ReactionRoot->RegisterComponent();
		Rig->GetRigRoot()->AttachToComponent(ReactionRoot,FAttachmentTransformRules::KeepRelativeTransform);
		ReactionBodyHitbox=NewObject<UCapsuleComponent>(Unit,TEXT("ReactionBodyHitbox"));
		Unit->AddInstanceComponent(ReactionBodyHitbox);
		ReactionBodyHitbox->SetupAttachment(ReactionRoot);
		ReactionBodyHitbox->InitCapsuleSize(30.f*MSGO::MSScale,80.f*MSGO::MSScale);
		ReactionBodyHitbox->SetRelativeLocation(FVector(0,0,Rig->GetRigRootRestRelativeZ()));
		ReactionBodyHitbox->SetCollisionProfileName(TEXT("MSHitbox"));
		ReactionBodyHitbox->SetCanEverAffectNavigation(false);
		ReactionBodyHitbox->RegisterComponent();
	}
	float DownAlpha=0;
	if(State.Phase==EMSHitReactionPhase::FallingDown) DownAlpha=FMath::Clamp(GetPhaseElapsed()/FallBlendSeconds,0.f,1.f);
	if(State.Phase==EMSHitReactionPhase::Downed) DownAlpha=1;
	if(State.Phase==EMSHitReactionPhase::GettingUp && State.bProne) DownAlpha=1-FMath::Clamp(GetPhaseElapsed()/GetUpSeconds,0.f,1.f);
	DownAlpha=FMath::SmoothStep(0.f,1.f,DownAlpha);
	const FQuat Rotation=FRotator(85.f*DownAlpha,0,0).Quaternion();
	// Rotate the whole rigid hierarchy. Keep the lowered torso above the floor;
	// the world/camera capsule remains upright under CharacterMovement ownership.
	const float Half=Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Rest=Rig->GetRigRootRestRelativeZ();
	const FVector Pivot(0,0,Rest);
	const FVector Lowering(0,0,-FMath::Max(0.f,Half+Rest-300.f)*DownAlpha);
	ReactionRoot->SetRelativeTransform(FTransform(Rotation,Pivot-Rotation.RotateVector(Pivot)+Lowering));
	if (DownAlpha>0 && Unit->GetCharacterMovement()->IsMovingOnGround())
	{
		double Bottom=TNumericLimits<double>::Max();
		const auto* Visual=Unit->GetMeshVisualComponent();
		const bool bMeshContacts=Visual && Visual->HasCrouchContactProfile();
		if (bMeshContacts) Bottom=Visual->GetCrouchContactMinWorldZ();
		for (const FName Name:Rig->GetPartNames())
		{
			// Imported shin/sole hulls differ from the hidden fallback cubes. Using
			// those cubes lifted both visible feet during the last half of get-up.
			if (bMeshContacts && (Name==TEXT("Shin_L") || Name==TEXT("Shin_R")
				|| Name==TEXT("Foot_L") || Name==TEXT("Foot_R"))) continue;
			if (const auto* Part=Rig->GetPart(Name))
			{
				const auto Bounds=Part->CalcBounds(Part->GetComponentTransform());
				Bottom=FMath::Min(Bottom,Bounds.Origin.Z-Bounds.BoxExtent.Z);
			}
		}
		if (Bottom<TNumericLimits<double>::Max())
			ReactionRoot->AddWorldOffset(FVector(0,0,Unit->GetActorLocation().Z-Half+2.-Bottom));
	}
	const bool bProneQuery=DownAlpha>.1f;
	Unit->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,bProneQuery?ECR_Ignore:ECR_Block);
	ReactionBodyHitbox->SetCollisionEnabled(bProneQuery?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
	// The head query must not remain at the standing capsule apex during a reaction.
	TInlineComponentArray<USphereComponent*> Spheres(Unit);
	for (USphereComponent* Head:Spheres)
	{
		if (Head->GetFName()==AMSUnitBase::HeadHitboxComponentName)
		{
			if (auto* Visual=Unit->GetMeshVisualComponent();Visual && Visual->GetPoseableMesh()
				&& Visual->GetPoseableMesh()->GetBoneIndex(TEXT("head"))!=INDEX_NONE)
				Head->SetWorldLocation(Visual->GetPoseableMesh()->GetBoneLocation(TEXT("head"),EBoneSpaces::WorldSpace));
			else if (auto* Joint=Rig->GetJoint(TEXT("Head"))) Head->SetWorldLocation(Joint->GetComponentLocation());
		}
	}
}
