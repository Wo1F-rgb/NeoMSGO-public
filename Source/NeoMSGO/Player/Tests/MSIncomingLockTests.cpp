#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Player/MSIncomingLockComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSCameraComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSVisualProjectile.h"
#include "AI/MSBotCharacter.h"
#include "AI/MSBotAIController.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/GA_Fire_Homing.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace MSIncomingLockTests
{
 using K=EMSBattleAssistEffectKind;
 struct FFixture
 {
  FString PreviousCommandLine=FCommandLine::Get();
  UWorld* World=nullptr;
  TStrongObjectPtr<UMSBattleAssistRulesData> Rules{NewObject<UMSBattleAssistRulesData>()};
  TStrongObjectPtr<UMSMobileSuitData> Suit{NewObject<UMSMobileSuitData>()};
  TStrongObjectPtr<UMSBattleAssistModuleData> Module{NewObject<UMSBattleAssistModuleData>()};
  TStrongObjectPtr<UMSRangedWeaponData> Gun{NewObject<UMSRangedWeaponData>()};
  TStrongObjectPtr<UMSHomingWeaponData> Missile{NewObject<UMSHomingWeaponData>()};
  FMSBattleAssistLoadout Loadout;
  FFixture()
  {
   TArray<FString> Tokens; PreviousCommandLine.ParseIntoArrayWS(Tokens); FString Effective;
   for(const auto& Token:Tokens)
    if(!Token.StartsWith(TEXT("-MSAuthMode="),ESearchCase::IgnoreCase) && !Token.StartsWith(TEXT("/MSAuthMode="),ESearchCase::IgnoreCase)
     && !Token.StartsWith(TEXT("-MSAuthBaseUrl="),ESearchCase::IgnoreCase) && !Token.StartsWith(TEXT("/MSAuthBaseUrl="),ESearchCase::IgnoreCase)) Effective+=Token+TEXT(" ");
   FCommandLine::Set(*(Effective+TEXT("-MSAuthMode=OfflineLocal")));
   World=UWorld::CreateWorld(EWorldType::Game,false);
   GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
   World->InitializeActorsForPlay(FURL()); World->BeginPlay();
   if(!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
   Suit->Cost=200; Suit->MaxArmor=5000;
   Module->FamilyId=TEXT("IncomingLockFixture"); Module->SlotCost=1;
   Module->Support=EMSModuleSupport::Implemented; Module->Evidence=EMSModuleEvidence::Confirmed;
   Rules->Modules.Add(Module.Get());
   auto& Profile=Rules->SuitProfiles.AddDefaulted_GetRef(); Profile.SuitId=Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity=10;
   auto& Row=Rules->TimeRows.AddDefaulted_GetRef(); Row.Cost=200; Row.TotalSeconds={50,54,58,62,66,77,85,96,110,134,189};
   Loadout.ModuleIds.Add(Module->GetPrimaryAssetId()); MSBattleAssist::SetRulesDataForTests(Rules.Get());
   Gun->Range=100; Gun->MagazineSize=5; Gun->ReserveAmmo=10; Gun->RedLockDistance=0;
   Missile->Range=100; Missile->LockOnRange=100; Missile->CategoryTag=MSTags::Weapon_Category_Homing;
   Missile->MagazineSize=5; Missile->ReserveAmmo=10; Missile->ReloadTime=5; Missile->RedLockDistance=0;
  }
  ~FFixture()
  {
   MSBattleAssist::SetRulesDataForTests(nullptr);
   GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
   FCommandLine::Set(*PreviousCommandLine);
  }
  AMSPlayerCharacter* Spawn(uint8 Team,FVector Location)
  {
   FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   auto* Unit=World->SpawnActor<AMSPlayerCharacter>(Location,FRotator::ZeroRotator,P);
   Unit->SetGenericTeamId(FGenericTeamId(Team)); Unit->InitializeUnit(Suit.Get());
   Unit->GetMSCharacterMovement()->DisableMovement(); Unit->GetWeaponComponent()->InitializeSlots({Gun.Get()});
   return Unit;
  }
  bool Equip(AMSPlayerCharacter* Unit,K Kind,float Value,bool Combined=false)
  {
   Module->Effects.Reset(); auto& Effect=Module->Effects.AddDefaulted_GetRef(); Effect.Kind=Kind; Effect.Value=Value;
   Effect.Unit=Kind==K::IncomingMissilePrelockWarning?TEXT("enabled"):TEXT("max_targets");
   if(Combined) { auto& Extra=Module->Effects.AddDefaulted_GetRef(); Extra.Kind=K::IncomingLockDirectionDistance; Extra.Value=2; Extra.Unit=TEXT("max_targets"); }
   return Unit->InitializeUnitWithBattleAssist(Suit.Get(),Loadout);
  }
  void AimAt(AMSPlayerCharacter* Source,AMSPlayerCharacter* Target)
  {
   const FVector Camera=Source->GetActorLocation()+FVector(0,0,1000);
   Source->GetMSCamera()->SetWorldLocationAndRotation(Camera,(Target->GetLockOnAimPoint()-Camera).Rotation());
  }
  void Tick(AMSPlayerCharacter* Source,AMSPlayerCharacter* Target,float Seconds=.01f)
  {
   Source->GetLockOnComponent()->TickComponent(Seconds,LEVELTICK_All,nullptr);
   Target->GetIncomingLockComponent()->TickComponent(Seconds,LEVELTICK_All,nullptr);
  }
  void Kill(AMSPlayerCharacter* Source,AMSPlayerCharacter* Target)
  {
   auto* Context=new FMSGameplayEffectContext(); Context->AddInstigator(Source,Source); Context->AddSourceObject(Gun.Get());
   Context->SetAttackerLocation(Source->GetActorLocation());
   FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(),FGameplayEffectContextHandle(Context),1);
   Spec.SetSetByCallerMagnitude(MSTags::Data_Damage,100000); Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint,0);
   Source->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(Spec,Target->GetAbilitySystemComponent());
  }
  AActor* Wall(FVector Location)
  {
   auto* Actor=World->SpawnActor<AActor>(); auto* Sphere=NewObject<USphereComponent>(Actor);
   Actor->SetRootComponent(Sphere); Sphere->SetSphereRadius(600); Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   Sphere->SetCollisionResponseToAllChannels(ECR_Ignore); Sphere->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel,ECR_Block);
   Sphere->RegisterComponent(); Actor->SetActorLocation(Location); return Actor;
  }
 };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSIncomingLockManualConsumers,"NeoMSGO.Player.IncomingLock.ManualConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSIncomingLockManualConsumers::RunTest(const FString&)
{
 using namespace MSIncomingLockTests; FFixture F;
 auto* Target=F.Spawn(1,FVector::ZeroVector); auto* Source=F.Spawn(2,FVector(12000,0,0));
 auto* Lock=Source->GetLockOnComponent(); auto* Incoming=Target->GetIncomingLockComponent();
 F.AimAt(Source,Target); Lock->ToggleLockOn();
 TestEqual(TEXT("real normal toggle acquires target"),Lock->GetLockedTarget(),static_cast<AActor*>(Target));
 TestTrue(TEXT("baseline normal lock warning exists without modules"),Incoming->GetSnapshot().bIncomingLock);
 TestEqual(TEXT("baseline never reveals source directions"),Incoming->GetSnapshot().Entries.Num(),0);
 TestFalse(TEXT("normal lock is not a missile"),Incoming->GetSnapshot().bIncomingMissile);
 Lock->ToggleLockOn(); TestFalse(TEXT("real unlock removes warning immediately"),Incoming->GetSnapshot().bIncomingLock);
 TestTrue(TEXT("direction module resolves"),F.Equip(Target,K::IncomingLockDirection,10));
 F.AimAt(Source,Target); Lock->ToggleLockOn();
 TestEqual(TEXT("direction module receives actual lock source"),Incoming->GetSnapshot().Entries.Num(),1);
 if(Incoming->GetSnapshot().Entries.Num()==1)
 {
  const auto Entry=Incoming->GetSnapshot().Entries[0];
  TestEqual(TEXT("world bearing points toward actual source"),Entry.DirectionYawDegrees,0.f);
  TestFalse(TEXT("direction-only source has no distance flag"),Entry.bHasDistance);
  TestEqual(TEXT("direction-only serialized distance is sentinel"),Entry.DistanceMeters,-1.f);
 }
 const uint32 BeforeMoving=Incoming->GetSnapshot().Revision;
 Source->SetActorLocation(FVector(12000,3000,0)); F.AimAt(Source,Target);
 for(int32 Frame=0;Frame<5;++Frame) Lock->TickComponent(.01f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("moving source frames do not force immediate snapshot publications"),Incoming->GetSnapshot().Revision,BeforeMoving);
 Incoming->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("receiver's scheduled update publishes changed bearing"),Incoming->GetSnapshot().Revision>BeforeMoving);
 Source->SetActorLocation(FVector(12000,0,0)); F.AimAt(Source,Target);
 // Movement outside the acquisition cone alone preserves ordinary lock.
 Source->GetMSCamera()->SetWorldRotation(FRotator(0,90,0)); F.Tick(Source,Target);
 TestTrue(TEXT("normal lock survives looking away"),Incoming->GetSnapshot().bIncomingLock);
 F.AimAt(Source,Target);
 auto* Wall=F.Wall((Source->GetLockOnAimPoint()+Target->GetLockOnAimPoint())*.5f); F.Tick(Source,Target,.02f);
 TestNull(TEXT("actual LOS obstruction breaks normal lock"),Lock->GetLockedTarget());
 TestFalse(TEXT("LOS break removes incoming warning"),Incoming->GetSnapshot().bIncomingLock); Wall->Destroy();
 F.AimAt(Source,Target); Lock->ToggleLockOn();
 Source->SetActorLocation(FVector(200000,0,0)); F.AimAt(Source,Target); F.Tick(Source,Target);
 TestFalse(TEXT("out-of-range removes incoming warning"),Incoming->GetSnapshot().bIncomingLock);
 Source->SetActorLocation(FVector(12000,0,0)); F.AimAt(Source,Target); Lock->ToggleLockOn();
 Source->GetWeaponComponent()->InitializeSlots({F.Gun.Get(),F.Gun.Get()}); Source->GetWeaponComponent()->SwitchToSlot(1);
 TestFalse(TEXT("actual weapon switch clears incoming warning"),Incoming->GetSnapshot().bIncomingLock);
 F.AimAt(Source,Target); Lock->ToggleLockOn(); F.Kill(Target,Source);
 TestFalse(TEXT("source death clears warning immediately"),Incoming->GetSnapshot().bIncomingLock);
 Source->InitializeUnit(F.Suit.Get()); Source->GetWeaponComponent()->InitializeSlots({F.Gun.Get()});
 F.AimAt(Source,Target); Lock->ToggleLockOn();
 const int32 OldEpoch=Target->GetBattleAssistEpoch(); Target->InitializeUnit(F.Suit.Get());
 TestTrue(TEXT("receiver deployment epoch advances"),Target->GetBattleAssistEpoch()>OldEpoch);
 TestNull(TEXT("receiver redeploy invalidates old outgoing source lock"),Lock->GetLockedTarget());
 TestFalse(TEXT("receiver redeploy clears old warning"),Incoming->GetSnapshot().bIncomingLock);
 F.AimAt(Source,Target); Lock->ToggleLockOn();
 TestTrue(TEXT("empty module deployment retains baseline warning"),Incoming->GetSnapshot().bIncomingLock);
 TestEqual(TEXT("module removal removes details"),Incoming->GetSnapshot().Entries.Num(),0);
 F.Kill(Source,Target); TestFalse(TEXT("receiver death suppresses warning"),Incoming->GetSnapshot().bIncomingLock);
 TestNull(TEXT("receiver death invalidates source lock immediately"),Lock->GetLockedTarget());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSIncomingLockIntentValidation,"NeoMSGO.Player.IncomingLock.IntentValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSIncomingLockIntentValidation::RunTest(const FString&)
{
 using namespace MSIncomingLockTests; FFixture F;
 auto* Target=F.Spawn(1,FVector::ZeroVector); auto* Source=F.Spawn(2,FVector(12000,0,0));
 auto* Ally=F.Spawn(2,FVector(0,15000,0)); auto* Lock=Source->GetLockOnComponent();
 const int32 Epoch=Source->GetBattleAssistEpoch(); uint32 Sequence=1;
 F.AimAt(Source,Target);
 TestTrue(TEXT("valid RPC-consumer intent acquires target"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++,Target->GetBattleAssistEpoch()));
 TestTrue(TEXT("validated intent reaches actual receiver"),Target->GetIncomingLockComponent()->GetSnapshot().bIncomingLock);
 TestFalse(TEXT("stale intent sequence cannot clear accepted target"),Lock->ApplyManualLockIntent(nullptr,Epoch,1));
 TestEqual(TEXT("stale intent preserves accepted target"),Lock->GetLockedTarget(),static_cast<AActor*>(Target));
 TestFalse(TEXT("old source epoch is rejected"),Lock->ApplyManualLockIntent(nullptr,Epoch-1,Sequence++));
 TestFalse(TEXT("old target epoch cannot lock new deployment"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++,Target->GetBattleAssistEpoch()-1));
 TestNull(TEXT("rejected target epoch leaves no authoritative lock"),Lock->GetLockedTarget());
 TestFalse(TEXT("missing target epoch cannot bypass unit deployment validation"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++,INDEX_NONE));
 TestNull(TEXT("missing epoch creates no authoritative lock"),Lock->GetLockedTarget());
 F.AimAt(Source,Ally); TestFalse(TEXT("friendly intent rejected"),Lock->ApplyManualLockIntent(Ally,Epoch,Sequence++));
 F.AimAt(Source,Target); Source->GetMSCamera()->SetWorldRotation(FRotator(0,90,0));
 TestFalse(TEXT("out-of-cone acquisition rejected"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++));
 F.AimAt(Source,Target); auto* Wall=F.Wall((Source->GetLockOnAimPoint()+Target->GetLockOnAimPoint())*.5f);
 TestFalse(TEXT("occluded acquisition rejected"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++)); Wall->Destroy();
 Source->GetWeaponComponent()->InitializeSlots({});
 TestFalse(TEXT("network intent without weapon rejected"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++));
 Source->GetWeaponComponent()->InitializeSlots({F.Gun.Get()}); F.Kill(Target,Source);
 TestFalse(TEXT("dead source cannot submit lock"),Lock->ApplyManualLockIntent(Target,Epoch,Sequence++));
 // Supplement actual consumers with replication schema validation: threat state must be owner-only.
 // A standalone test world has no actor channel to initialize runtime RepIndex values.
 // Match the engine replication setup before invoking the lifetime declarations directly.
 UMSIncomingLockComponent::StaticClass()->SetUpRuntimeReplicationData();
 TArray<FLifetimeProperty> IncomingProps; Target->GetIncomingLockComponent()->GetLifetimeReplicatedProps(IncomingProps);
 const FProperty* SnapshotProp=FindFProperty<FProperty>(UMSIncomingLockComponent::StaticClass(),TEXT("Snapshot"));
 bool OwnerOnly=false;
 if(SnapshotProp) for(const auto& Prop:IncomingProps) if(Prop.RepIndex==SnapshotProp->RepIndex) OwnerOnly=Prop.Condition==COND_OwnerOnly;
 TestTrue(TEXT("sanitized snapshot replication is owner-only"),OwnerOnly);
 UMSLockOnComponent::StaticClass()->SetUpRuntimeReplicationData();
 TArray<FLifetimeProperty> OutgoingProps; Lock->GetLifetimeReplicatedProps(OutgoingProps);
 const FProperty* ManualProp=FindFProperty<FProperty>(UMSLockOnComponent::StaticClass(),TEXT("ManualLockSnapshot"));
 bool ManualOwnerOnly=false;
 if(ManualProp) for(const auto& Prop:OutgoingProps) if(Prop.RepIndex==ManualProp->RepIndex) ManualOwnerOnly=Prop.Condition==COND_OwnerOnly;
 TestTrue(TEXT("manual lock reconciliation is also owner-only"),ManualOwnerOnly);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSIncomingLockCapacityConsumers,"NeoMSGO.Player.IncomingLock.CapacityConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSIncomingLockCapacityConsumers::RunTest(const FString&)
{
 using namespace MSIncomingLockTests; FFixture F; auto* Target=F.Spawn(1,FVector::ZeroVector);
 TArray<AMSPlayerCharacter*> Sources;
 for(int32 Index=0;Index<11;++Index)
 {
  const float Angle=FMath::DegreesToRadians(Index*32.f);
  Sources.Add(F.Spawn(2,FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(12000.f+Index*1000.f)));
 }
 auto AcquireAll=[&]() { for(auto* Source:Sources) { F.AimAt(Source,Target); Source->GetLockOnComponent()->ToggleLockOn(); } };
 auto* Incoming=Target->GetIncomingLockComponent();
 TestTrue(TEXT("direction module resolves"),F.Equip(Target,K::IncomingLockDirection,10)); AcquireAll();
 TestEqual(TEXT("eleven actual manual sources capped at ten"),Incoming->GetSnapshot().Entries.Num(),10);
 for(const auto& Entry:Incoming->GetSnapshot().Entries) { TestFalse(TEXT("direction-only privacy"),Entry.bHasDistance); TestEqual(TEXT("no distance leaked"),Entry.DistanceMeters,-1.f); }
 Sources[0]->GetLockOnComponent()->ClearLockOn();
 TestTrue(TEXT("clearing one source retains other warnings"),Incoming->GetSnapshot().bIncomingLock);
 TestEqual(TEXT("remaining ten source entries survive"),Incoming->GetSnapshot().Entries.Num(),10);
 TestTrue(TEXT("improved module resolves"),F.Equip(Target,K::IncomingLockDirectionDistance,2)); AcquireAll();
 TestEqual(TEXT("distance module caps actual sources at two"),Incoming->GetSnapshot().Entries.Num(),2);
 if(Incoming->GetSnapshot().Entries.Num()==2)
 {
  TestEqual(TEXT("nearest source uses HUD absolute-cm conversion"),Incoming->GetSnapshot().Entries[0].DistanceMeters,120.f);
  TestEqual(TEXT("second nearest source"),Incoming->GetSnapshot().Entries[1].DistanceMeters,130.f);
 }
 TestTrue(TEXT("combined fixture resolves"),F.Equip(Target,K::IncomingLockDirection,10,true)); AcquireAll();
 TestEqual(TEXT("combined detail list is at most ten"),Incoming->GetSnapshot().Entries.Num(),10);
 int32 Distances=0; for(const auto& Entry:Incoming->GetSnapshot().Entries) if(Entry.bHasDistance) ++Distances;
 TestEqual(TEXT("combined list still reveals only two distances"),Distances,2);
 for(auto* Source:Sources) Source->Destroy();
 TestFalse(TEXT("all source EndPlay removes warnings immediately"),Incoming->GetSnapshot().bIncomingLock);
 TestEqual(TEXT("all source EndPlay removes details"),Incoming->GetSnapshot().Entries.Num(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSIncomingMissileConsumers,"NeoMSGO.Player.IncomingLock.MissileConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSIncomingMissileConsumers::RunTest(const FString&)
{
 using namespace MSIncomingLockTests; FFixture F;
 auto* Target=F.Spawn(1,FVector::ZeroVector); auto* Source=F.Spawn(2,FVector(12000,0,0));
 Source->GetWeaponComponent()->InitializeSlots({F.Missile.Get(),F.Gun.Get()}); F.AimAt(Source,Target);
 auto* Lock=Source->GetLockOnComponent(); auto* Incoming=Target->GetIncomingLockComponent();
 F.Tick(Source,Target,.01f);
 TestEqual(TEXT("actual homing candidate acquired"),Lock->GetHomingCandidate(),static_cast<AActor*>(Target));
 TestFalse(TEXT("baseline does not warn during initial dwell"),Incoming->GetSnapshot().bIncomingMissile);
 F.Tick(Source,Target,.81f);
 TestEqual(TEXT("actual homing capture completes"),Lock->GetHomingCapturedTarget(),static_cast<AActor*>(Target));
 TestTrue(TEXT("baseline warns after capture"),Incoming->GetSnapshot().bIncomingMissile);
 TestTrue(TEXT("captured missile is also an incoming lock"),Incoming->GetSnapshot().bIncomingLock);
 TestTrue(TEXT("prelock module resolves"),F.Equip(Target,K::IncomingMissilePrelockWarning,1));
 F.AimAt(Source,Target); F.Tick(Source,Target,.01f);
 TestNull(TEXT("new target deployment forces fresh missile dwell"),Lock->GetHomingCapturedTarget());
 TestTrue(TEXT("module warning starts at actual candidate acquisition"),Incoming->GetSnapshot().bIncomingMissile);
 TestFalse(TEXT("prelock does not invent a captured lock"),Incoming->GetSnapshot().bIncomingLock);
 TestEqual(TEXT("warning-only module never exposes direction"),Incoming->GetSnapshot().Entries.Num(),0);
 Source->GetWeaponComponent()->ConsumeAmmoAndStartCooldown(0); Source->GetWeaponComponent()->StartReload(0); F.Tick(Source,Target);
 TestFalse(TEXT("actual reload clears candidate warning"),Incoming->GetSnapshot().bIncomingMissile);
 Source->GetWeaponComponent()->InitializeSlots({F.Missile.Get(),F.Gun.Get()}); F.AimAt(Source,Target); F.Tick(Source,Target);
 TestTrue(TEXT("warning returns when actual acquisition resumes"),Incoming->GetSnapshot().bIncomingMissile);
 Source->GetWeaponComponent()->SwitchToSlot(1);
 TestFalse(TEXT("weapon switch clears prelock immediately"),Incoming->GetSnapshot().bIncomingMissile);
 Source->GetWeaponComponent()->SwitchToSlot(0); F.Tick(Source,Target);
 Source->GetMSCamera()->SetWorldRotation(FRotator(0,90,0)); F.Tick(Source,Target);
 TestFalse(TEXT("leaving missile circle clears warning"),Incoming->GetSnapshot().bIncomingMissile);
 F.AimAt(Source,Target); F.Tick(Source,Target);
 auto* Wall=F.Wall((Source->GetLockOnAimPoint()+Target->GetLockOnAimPoint())*.5f); F.Tick(Source,Target);
 TestFalse(TEXT("missile LOS loss clears warning"),Incoming->GetSnapshot().bIncomingMissile); Wall->Destroy();
 F.AimAt(Source,Target); F.Tick(Source,Target); Target->InitializeUnit(F.Suit.Get()); F.Tick(Source,Target,.01f);
 TestFalse(TEXT("module removal restores baseline dwell behavior"),Incoming->GetSnapshot().bIncomingMissile);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSIncomingBotHomingConsumers,"NeoMSGO.Player.IncomingLock.BotHomingConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSIncomingBotHomingConsumers::RunTest(const FString&)
{
 using namespace MSIncomingLockTests; FFixture F;
 auto* Target=F.Spawn(1,FVector::ZeroVector);
 TestTrue(TEXT("bot target has actual prelock warning module"),F.Equip(Target,K::IncomingMissilePrelockWarning,1));
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Bot=F.World->SpawnActor<AMSBotCharacter>(FVector(12000,0,0),FRotator(0,180,0),Params);
 Bot->SetGenericTeamId(FGenericTeamId(2)); Bot->InitializeUnit(F.Suit.Get()); Bot->GetCharacterMovement()->DisableMovement();
 auto* AI=Cast<AMSBotAIController>(Bot->GetController());
 if(!TestNotNull(TEXT("real bot receives its production AI controller"),AI)) return false;
 // Use the real registry target search via controller tick, never inject a target into the lock component.
 AI->Tick(.01f);
 TestEqual(TEXT("production AI chooses hostile unit"),AI->GetCurrentTarget(),static_cast<AMSUnitBase*>(Target));
 F.Missile->MagazineSize=1; F.Missile->ProjectileSpeed=20;
 Bot->GetWeaponComponent()->InitializeSlots({F.Missile.Get(),F.Gun.Get()});
 auto* Lock=Bot->GetLockOnComponent(); auto* Incoming=Target->GetIncomingLockComponent();
 TestTrue(TEXT("bot uses homing-only component mode"),Lock->IsAIHomingOnly());
 Lock->TickComponent(.01f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("bot actual AI intent becomes homing candidate"),Lock->GetHomingCandidate(),static_cast<AActor*>(Target));
 TestTrue(TEXT("bot acquisition emits module prelock warning"),Incoming->GetSnapshot().bIncomingMissile);
 TestNull(TEXT("bot aim never fabricates a manual lock"),Lock->GetLockedTarget());
 const int32 Before=Bot->GetWeaponComponent()->GetMagazineAmmo(0);
 TestFalse(TEXT("actual AI fire task waits for capture"),AI->TryActivateCurrentWeaponAbility());
 TStrongObjectPtr<UGA_Fire_Homing> Direct(NewObject<UGA_Fire_Homing>(Bot));
 TestFalse(TEXT("direct bot GA also rejects before capture"),Direct->FireHoming(Bot,Bot->GetActorLocation(),Bot->GetActorForwardVector()));
 TestEqual(TEXT("no ammunition consumed before capture"),Bot->GetWeaponComponent()->GetMagazineAmmo(0),Before);
 Lock->TickComponent(.81f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("bot completes actual shared dwell"),Lock->GetHomingCapturedTarget(),static_cast<AActor*>(Target));
 TestTrue(TEXT("actual AI task launches captured missile"),AI->TryActivateCurrentWeaponAbility());
 TestEqual(TEXT("launch consumes exactly one round"),Bot->GetWeaponComponent()->GetMagazineAmmo(0),Before-1);
 int32 Guided=0;
 for(TActorIterator<AMSVisualProjectile> It(F.World);It;++It) if(It->GetHomingTarget()==Target) ++Guided;
 TestEqual(TEXT("actual spawned projectile is bound to captured target"),Guided,1);
 Bot->GetWeaponComponent()->SwitchToSlot(1);
 TestFalse(TEXT("bot weapon switch clears incoming warning"),Incoming->GetSnapshot().bIncomingMissile);
 Lock->TickComponent(1.f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("ordinary AI gun aiming creates no incoming lock"),Incoming->GetSnapshot().bIncomingLock);
 Bot->GetWeaponComponent()->InitializeSlots({F.Missile.Get()}); Lock->TickComponent(.01f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("bot starts new missile acquisition"),Incoming->GetSnapshot().bIncomingMissile);
 auto* Wall=F.Wall((Bot->GetLockOnAimPoint()+Target->GetLockOnAimPoint())*.5f);
 Lock->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("bot actual LOS obstruction clears warning"),Incoming->GetSnapshot().bIncomingMissile);
 TestFalse(TEXT("bot cannot spend ammo into a blocked lock"),AI->TryActivateCurrentWeaponAbility()); Wall->Destroy();
 Lock->TickComponent(.01f,LEVELTICK_All,nullptr); Bot->InitializeUnit(F.Suit.Get());
 TestFalse(TEXT("bot redeployment clears old acquisition immediately"),Incoming->GetSnapshot().bIncomingMissile);
 Bot->GetWeaponComponent()->InitializeSlots({F.Missile.Get()}); Lock->TickComponent(.01f,LEVELTICK_All,nullptr);
 Bot->Destroy(); TestFalse(TEXT("bot source EndPlay clears acquisition"),Incoming->GetSnapshot().bIncomingMissile);
 return true;
}
#endif
