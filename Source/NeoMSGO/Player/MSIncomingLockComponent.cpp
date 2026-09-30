#include "Player/MSIncomingLockComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

bool FMSIncomingLockEntry::operator==(const FMSIncomingLockEntry& Other) const
{
 return ThreatId==Other.ThreatId && DirectionYawDegrees==Other.DirectionYawDegrees
  && bHasDistance==Other.bHasDistance && DistanceMeters==Other.DistanceMeters;
}

UMSIncomingLockComponent::UMSIncomingLockComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
 PrimaryComponentTick.TickInterval=.1f;
 SetIsReplicatedByDefault(true);
}

void UMSIncomingLockComponent::BeginPlay()
{
 Super::BeginPlay();
 if(auto* Unit=Cast<AMSUnitBase>(GetOwner()))
 {
  ObservedEpoch=Unit->GetBattleAssistEpoch();
  if(auto* Vital=Unit->GetVitalSet()) Vital->OnDeath.AddUniqueDynamic(this,&ThisClass::HandleOwnerDeath);
 }
}

void UMSIncomingLockComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 ResetForDeployment();
 if(auto* Unit=Cast<AMSUnitBase>(GetOwner()))
  if(auto* Vital=Unit->GetVitalSet()) Vital->OnDeath.RemoveDynamic(this,&ThisClass::HandleOwnerDeath);
 Super::EndPlay(Reason);
}

void UMSIncomingLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(UMSIncomingLockComponent,Snapshot,COND_OwnerOnly);
}

const FMSIncomingLockSnapshot& UMSIncomingLockComponent::GetSnapshot() const
{
 static const FMSIncomingLockSnapshot Empty;
 const auto* Unit=Cast<AMSUnitBase>(GetOwner());
 if(!Unit || Snapshot.SpawnEpoch!=Unit->GetBattleAssistEpoch()) return Empty;
 // Previously possessed proxies can retain old replicated memory; never expose it to a new HUD.
 if(!Unit->HasAuthority() && !Unit->IsLocallyControlled()) return Empty;
 if(Unit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead)) return Empty;
 return Snapshot;
}

void UMSIncomingLockComponent::OnRep_Snapshot() { OnIncomingLockChanged.Broadcast(); }
void UMSIncomingLockComponent::HandleOwnerDeath(AActor*, AActor*) { ResetForDeployment(); }

void UMSIncomingLockComponent::ResetForDeployment()
{
 if(!GetOwner() || !GetOwner()->HasAuthority() || bResetting) return;
 bResetting=true;
 TArray<TWeakObjectPtr<UMSLockOnComponent>> Previous; Sources.GetKeys(Previous);
 Sources.Reset();
 for(const auto& Source:Previous) if(Source.IsValid()) Source->InvalidateIncomingTarget(GetOwner());
 const auto* Unit=Cast<AMSUnitBase>(GetOwner());
 ObservedEpoch=Unit?Unit->GetBattleAssistEpoch():0;
 FMSIncomingLockSnapshot Next; Next.SpawnEpoch=ObservedEpoch;
 Publish(MoveTemp(Next));
 bResetting=false;
}

void UMSIncomingLockComponent::RefreshSource(UMSLockOnComponent* Source)
{
 if(!GetOwner() || !GetOwner()->HasAuthority() || !Source || bResetting) return;
 bool Manual=false,Tracking=false,Captured=false;
 if(Source->GetIncomingThreatState(GetOwner(),Manual,Tracking,Captured))
 {
  if(auto* Existing=Sources.Find(Source))
  {
   // Source ticks every frame. Only membership/phase transitions publish immediately;
   // moving bearing/distance updates are coalesced by the receiver's 10 Hz tick.
   if(Existing->bManual==Manual && Existing->bTracking==Tracking && Existing->bCaptured==Captured) return;
   Existing->bManual=Manual; Existing->bTracking=Tracking; Existing->bCaptured=Captured;
  }
  else Sources.Add(Source,{NextThreatId++,Manual,Tracking,Captured});
 }
 else if(Sources.Remove(Source)==0) return;
 RebuildSnapshot();
}

void UMSIncomingLockComponent::RemoveSource(UMSLockOnComponent* Source)
{
 if(!GetOwner() || !GetOwner()->HasAuthority()) return;
 Sources.Remove(Source);
 if(!bResetting) RebuildSnapshot();
}

void UMSIncomingLockComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 if(!GetOwner() || !GetOwner()->HasAuthority()) return;
 if(const auto* Unit=Cast<AMSUnitBase>(GetOwner()); Unit && Unit->GetBattleAssistEpoch()!=ObservedEpoch) ResetForDeployment();
 RebuildSnapshot();
}

void UMSIncomingLockComponent::RebuildSnapshot()
{
 const auto* Unit=Cast<AMSUnitBase>(GetOwner());
 if(!Unit || !Unit->HasAuthority() || bResetting) return;
 FMSIncomingLockSnapshot Next; Next.SpawnEpoch=Unit->GetBattleAssistEpoch();
 if(Unit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead))
 {
  Sources.Reset(); Publish(MoveTemp(Next)); return;
 }
 using K=EMSBattleAssistEffectKind;
 const int32 DirectionCap=FMath::Clamp(FMath::FloorToInt(Unit->GetBattleAssistEffectValue(K::IncomingLockDirection)),0,10);
 const int32 DistanceCap=FMath::Clamp(FMath::FloorToInt(Unit->GetBattleAssistEffectValue(K::IncomingLockDirectionDistance)),0,2);
 const bool PrelockWarning=Unit->GetBattleAssistEffectValue(K::IncomingMissilePrelockWarning)>0.f;
 struct FThreat { int32 Id; FVector Delta; float DistanceSquared; };
 TArray<FThreat> Threats;
 for(auto It=Sources.CreateIterator();It;++It)
 {
  auto* Source=It.Key().Get(); bool Manual=false,Tracking=false,Captured=false;
  if(!Source || !Source->GetIncomingThreatState(Unit,Manual,Tracking,Captured)) { It.RemoveCurrent(); continue; }
  Next.bIncomingLock|=Manual||Captured;
  Next.bIncomingMissile|=Captured||(PrelockWarning&&Tracking);
  if((Manual||Captured) && FMath::Max(DirectionCap,DistanceCap)>0)
  {
   const FVector Delta=Source->GetOwner()->GetActorLocation()-Unit->GetActorLocation();
   Threats.Add({It.Value().Id,Delta,static_cast<float>(Delta.SizeSquared())});
  }
 }
 // NeoMSGO policy: nearest sources, stable receiver-local ID breaks ties. Original ordering unverified.
 Threats.Sort([](const FThreat& A,const FThreat& B){return A.DistanceSquared==B.DistanceSquared?A.Id<B.Id:A.DistanceSquared<B.DistanceSquared;});
 for(int32 Index=0;Index<FMath::Min(Threats.Num(),FMath::Max(DirectionCap,DistanceCap));++Index)
 {
  const auto& Threat=Threats[Index]; auto& Entry=Next.Entries.AddDefaulted_GetRef();
  Entry.ThreatId=Threat.Id;
  Entry.DirectionYawDegrees=FMath::RoundToFloat(Threat.Delta.Rotation().Yaw);
  Entry.bHasDistance=Index<DistanceCap;
  // Match MSHUDViewModel::WorldDistanceToMeters: migrated absolute world centimeters / 100.
  if(Entry.bHasDistance) Entry.DistanceMeters=FMath::RoundToFloat(FMath::Sqrt(Threat.DistanceSquared)/100.f);
 }
 Publish(MoveTemp(Next));
}

void UMSIncomingLockComponent::Publish(FMSIncomingLockSnapshot&& Next)
{
 if(Snapshot.SpawnEpoch==Next.SpawnEpoch && Snapshot.bIncomingLock==Next.bIncomingLock
  && Snapshot.bIncomingMissile==Next.bIncomingMissile && Snapshot.Entries==Next.Entries) return;
 Next.Revision=Snapshot.Revision+1;
 Snapshot=MoveTemp(Next);
 OnIncomingLockChanged.Broadcast();
 if(GetOwner()) GetOwner()->ForceNetUpdate();
}
