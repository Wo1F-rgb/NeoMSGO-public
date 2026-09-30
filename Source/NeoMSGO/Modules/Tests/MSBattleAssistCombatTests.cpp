#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "AbilitySystemComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Components/CapsuleComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSBoostGameplayEffects.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSCameraComponent.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/GA_Melee.h"
#include "Gameplay/MSSupplyPack.h"
#include "UObject/StrongObjectPtr.h"

namespace MSBattleAssistCombatTests
{
 using K = EMSBattleAssistEffectKind;
 struct FScopedOfflineAuth
 {
  FString Previous = FCommandLine::Get();
  FScopedOfflineAuth()
  {
   TArray<FString> Tokens; Previous.ParseIntoArrayWS(Tokens);
   FString Effective;
   for (const FString& Token : Tokens)
   {
    if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
     || Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)
     || Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
     || Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase)) continue;
    if (!Effective.IsEmpty()) Effective += TEXT(" ");
    Effective += Token;
   }
   Effective += TEXT(" -MSAuthMode=OfflineLocal"); FCommandLine::Set(*Effective);
  }
  ~FScopedOfflineAuth() { FCommandLine::Set(*Previous); }
 };
 struct FFixture
 {
  FScopedOfflineAuth OfflineAuth;
  UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
  TStrongObjectPtr<UMSBattleAssistRulesData> Rules{NewObject<UMSBattleAssistRulesData>()};
  TStrongObjectPtr<UMSMobileSuitData> Suit{NewObject<UMSMobileSuitData>()};
  TStrongObjectPtr<UMSBattleAssistModuleData> Module{NewObject<UMSBattleAssistModuleData>()};
  FMSBattleAssistLoadout Loadout;
  FFixture()
  {
   GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
   World->InitializeActorsForPlay(FURL()); World->BeginPlay();
   if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
   Suit->Cost = 200; Suit->MaxArmor = 5000.f;
   Module->FamilyId = TEXT("CombatFixture"); Module->SlotCost = 1;
   Module->Support = EMSModuleSupport::Implemented; Module->Evidence = EMSModuleEvidence::Confirmed;
   Rules->Modules.Add(Module.Get());
   auto& Profile = Rules->SuitProfiles.AddDefaulted_GetRef();
   Profile.SuitId = Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity = 10;
   auto& Row = Rules->TimeRows.AddDefaulted_GetRef(); Row.Cost = 200; Row.TotalSeconds = {50.f,54.f,58.f,62.f,66.f,77.f,85.f,96.f,110.f,134.f,189.f};
   Loadout.ModuleIds.Add(Module->GetPrimaryAssetId());
   MSBattleAssist::SetRulesDataForTests(Rules.Get());
  }
  ~FFixture()
  {
   MSBattleAssist::SetRulesDataForTests(nullptr);
   GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
  }
  AMSPlayerCharacter* Spawn(uint8 Team = 1, FVector Position = FVector::ZeroVector)
  {
   FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   auto* U = World->SpawnActor<AMSPlayerCharacter>(Position, FRotator::ZeroRotator, P);
   U->SetGenericTeamId(FGenericTeamId(Team)); U->InitializeUnit(Suit.Get()); return U;
  }
  void Effect(K Kind, float Value)
  {
   Module->Effects.Reset(); auto& E = Module->Effects.AddDefaulted_GetRef();
   E.Kind = Kind; E.Value = Value; E.Unit = TEXT("fixture_source_units");
  }
  bool Equip(AMSPlayerCharacter* Unit, K Kind, float Value)
  {
   Effect(Kind, Value); return Unit->InitializeUnitWithBattleAssist(Suit.Get(), Loadout);
  }
  void Tick(float Seconds)
  {
   for (float T = 0.f; T < Seconds; T += .01f) { World->Tick(LEVELTICK_All,FMath::Min(.01f,Seconds-T)); ++GFrameCounter; }
  }
 };
 void Damage(AMSPlayerCharacter* Source, AMSPlayerCharacter* Target, UMSWeaponDataBase* Weapon,
  bool bExplosion = false, float DP = 0.f, FVector Location = FVector(1000,0,0))
 {
  auto* C = new FMSGameplayEffectContext(); C->AddInstigator(Source,Source); C->AddSourceObject(Weapon); C->SetAttackerLocation(Location);
  FGameplayEffectSpec S(bExplosion ? static_cast<const UGameplayEffect*>(GetDefault<UGE_SplashDamage>()) : static_cast<const UGameplayEffect*>(GetDefault<UGE_Damage>()),FGameplayEffectContextHandle(C),1.f);
  S.SetSetByCallerMagnitude(MSTags::Data_Damage,1000.f); S.SetSetByCallerMagnitude(MSTags::Data_DownPoint,DP);
  Source->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(S,Target->GetAbilitySystemComponent());
 }
 FVector StageBladeMidpoint(AMSPlayerCharacter* Attacker, int32 Stage, float SampleTime)
 {
  FVector Start, End;
  // Stage 11 is the left-hand-only strike; all other stages include the right hand.
  FMSStandardSaberMotion::SampleBlade(Stage,SampleTime,100.f,Start,End,Stage==11);
  FTransform Transform=Attacker->GetActorTransform();
  Transform.SetScale3D(Transform.GetScale3D()*MSGO::MSScale);
  if (auto* Rig=Attacker->GetMechRigComponent())
   if (auto* Root=Rig->GetRigRoot()) Transform=Root->GetComponentTransform();
  const auto Timing=FMSStandardSaberMotion::GetTiming(Stage);
  const float Alpha=FMath::Clamp((SampleTime-Timing.LungeStart)/FMath::Max(Timing.LungeEnd-Timing.LungeStart,KINDA_SMALL_NUMBER),0.f,1.f);
  return Transform.TransformPosition((Start+End)*.5f)+Attacker->GetActorForwardVector()*(Alpha*Timing.LungeDistanceCm*MSGO::MSScale);
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistDamageConsumers,"NeoMSGO.BattleAssist.Combat.DamageConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistDamageConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F;
 auto* Source=F.Spawn(1,FVector(10000,0,0)); auto* Target=F.Spawn(2);
 auto* Ballistic=NewObject<UMSRangedWeaponData>(); Ballistic->CategoryTag=MSTags::Weapon_Category_MachineGun;
 auto* Beam=NewObject<UMSRangedWeaponData>(); Beam->CategoryTag=MSTags::Weapon_Category_BeamRifle;
 auto* Melee=NewObject<UMSMeleeWeaponData>();
 for (K Kind : {K::BallisticDamageReduction,K::BeamDamageReduction,K::MeleeDamageReduction,K::ExplosiveDamageReduction,K::UnderwaterDamageReduction})
 for (float Percent : {6.f,8.f,10.f})
 {
  TestTrue(TEXT("authority resolves defensive rank"),F.Equip(Target,Kind,Percent));
  auto* Weapon=Kind==K::BeamDamageReduction?static_cast<UMSWeaponDataBase*>(Beam):Kind==K::MeleeDamageReduction?static_cast<UMSWeaponDataBase*>(Melee):Ballistic;
  if(Kind==K::UnderwaterDamageReduction) Target->GetMSCharacterMovement()->SetMovementMode(MOVE_Swimming);
  Damage(Source,Target,Weapon,Kind==K::ExplosiveDamageReduction,1.f);
  TestEqual(TEXT("matching real GAS packet reduced"),Target->GetVitalSet()->GetArmor(),4000.f+Percent*10.f);
  TestEqual(TEXT("armor module does not reduce DP"),Target->GetVitalSet()->GetDownPoint(),1.f);
  Target->GetVitalSet()->SetArmor(5000.f); Target->GetMSCharacterMovement()->SetMovementMode(MOVE_Walking);
  Damage(Source,Target,Kind==K::BallisticDamageReduction?Beam:Ballistic,false);
  TestEqual(TEXT("nonmatching packet unchanged"),Target->GetVitalSet()->GetArmor(),4000.f);
  Target->InitializeUnit(F.Suit.Get()); Damage(Source,Target,Weapon,Kind==K::ExplosiveDamageReduction);
  TestEqual(TEXT("unequipped packet returns to baseline"),Target->GetVitalSet()->GetArmor(),4000.f);
 }
 for(float P:{10.f,15.f,25.f})
 {
  F.Equip(Target,K::BackshotDamageReduction,P); Damage(Source,Target,Ballistic,false,0.f,FVector(-1000,0,0));
  TestEqual(TEXT("rear packet"),Target->GetVitalSet()->GetArmor(),4000.f+P*10.f);
  Target->GetVitalSet()->SetArmor(5000); Damage(Source,Target,Ballistic);
  TestEqual(TEXT("front packet unaffected"),Target->GetVitalSet()->GetArmor(),4000.f);
  Target->GetVitalSet()->SetArmor(5000); Damage(Source,Target,Ballistic,true,0.f,FVector(-1000,0,0));
  TestEqual(TEXT("splash has no rear classification"),Target->GetVitalSet()->GetArmor(),4000.f);
 }
 auto* Shield=NewObject<UMSShieldWeaponData>(); Shield->MaxDurability=5000.f; F.Suit->NativeShield=Shield;
 for(float P:{25.f,35.f,50.f})
 {
  F.Equip(Source,K::ShieldDamageIncrease,P); Target->InitializeUnit(F.Suit.Get());
  // The previous rank explicitly removed the deck shield; respawn preserves that user choice.
  Target->GetShieldComponent()->SetDeckShieldEnabled(true);
  Damage(Source,Target,Ballistic); TestEqual(TEXT("shield alone receives attack bonus"),Target->GetShieldComponent()->GetCurrentDurability(),4000.f-P*10.f);
  TestEqual(TEXT("shield direct hit leaves armor intact"),Target->GetVitalSet()->GetArmor(),5000.f);
  Target->InitializeUnit(F.Suit.Get()); Damage(Source,Target,Ballistic,true);
  TestEqual(TEXT("explosion chip excludes shield bonus"),Target->GetVitalSet()->GetArmor(),4900.f);
  Target->GetShieldComponent()->SetDeckShieldEnabled(false); Target->GetVitalSet()->SetArmor(5000.f);
  Damage(Source,Target,Ballistic); TestEqual(TEXT("unshielded damage excludes shield bonus"),Target->GetVitalSet()->GetArmor(),4000.f);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistWeaponConsumers,"NeoMSGO.BattleAssist.Combat.WeaponConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistWeaponConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn(); auto* W=U->GetWeaponComponent();
 auto* Gun=NewObject<UMSRangedWeaponData>(); Gun->MagazineSize=10; Gun->ReserveAmmo=11; Gun->ReloadTime=5.f; Gun->WeaponSwitchDuration=2.f;
 auto* BuiltIn=NewObject<UMSRangedWeaponData>(); BuiltIn->bIsBuiltIn=true;
 for(float P:{4.f,6.f,10.f})
 {
  TestTrue(TEXT("reload module resolves"),F.Equip(U,K::ReloadTimeReduction,P)); W->InitializeSlots({Gun});
  TestTrue(TEXT("reload test consumes an actual round"),W->ConsumeAmmoAndStartCooldown(0));
  TestTrue(TEXT("reload actually starts"),W->StartReload(0));
  TestEqual(TEXT("resolved reload source percent"),U->GetBattleAssistEffectValue(K::ReloadTimeReduction),P);
  // Default IsNearlyEqual tolerance is below one float ULP at these second values.
  const float ExpectedSeconds=P==4.f?4.8f:P==6.f?4.7f:4.5f;
  TestEqual(FString::Printf(TEXT("reload duration source rank %.0f"),P),W->GetReloadDuration(0),ExpectedSeconds,.000001f);
  U->InitializeUnit(F.Suit.Get()); W->InitializeSlots({Gun}); W->ConsumeAmmoAndStartCooldown(0); W->StartReload(0);
  TestEqual(TEXT("reload reset"),W->GetReloadDuration(0),5.f);
 }
 for(float P:{10.f,15.f,20.f})
 {
  F.Equip(U,K::WeaponSwitchTimeReduction,P); W->InitializeSlots({Gun,Gun,BuiltIn}); W->SwitchToSlot(1);
  TestTrue(TEXT("switch duration source rank"),FMath::IsNearlyEqual(W->GetSwitchRemaining(),2.f*(1.f-P*.01f)));
  W->SwitchToSlot(2); TestFalse(TEXT("builtin switch stays instant"),W->IsSwitching());
 }
 F.Equip(U,K::ReserveAmmoIncrease,10.f); W->InitializeSlots({Gun,BuiltIn});
 TestEqual(TEXT("reserve rounds ceil 11*1.1"),W->GetReserveAmmo(0),13); TestEqual(TEXT("magazine unchanged"),W->GetMagazineAmmo(0),10);
 TestEqual(TEXT("zero reserve unchanged"),W->GetReserveAmmo(1),0);
 W->ConsumeAmmoAndStartCooldown(0); W->StartReload(0); W->TickComponent(6.f,LEVELTICK_All,nullptr); W->RefillAllAmmo();
 TestEqual(TEXT("supply uses enlarged cap without multiplying existing ammo"),W->GetReserveAmmo(0),13);
 W->RefillAllAmmo(); TestEqual(TEXT("repeat refill cannot grow cap"),W->GetReserveAmmo(0),13);
 U->InitializeUnit(F.Suit.Get()); W->InitializeSlots({Gun}); TestEqual(TEXT("new empty spawn removes reserve bonus"),W->GetReserveAmmo(0),11);
 for(float P:{15.f,20.f,30.f})
 {
  F.Equip(U,K::WeaponChargeTimeReduction,P); Gun->bIsChargeable=true; Gun->MaxChargeTime=2.f; W->InitializeSlots({Gun});
  auto* Charge=NewObject<UGA_Fire_Charge>(U); TestTrue(TEXT("real charge starts"),Charge->StartCharge(U,U->GetActorLocation(),FVector::ForwardVector));
  F.Tick(.2f); TestTrue(TEXT("charge and HUD share shortened duration"),FMath::IsNearlyEqual(Charge->GetChargeProgress01(),.2f/(2.f*(1.f-P*.01f)),.015f));
  TestTrue(TEXT("HUD uses replicated charge clock"),U->IsChargeFireActive());
  TestTrue(TEXT("HUD and damage charge progress agree"),FMath::IsNearlyEqual(U->GetChargeFireProgress01(),Charge->GetChargeProgress01(),.001f));
  TestEqual(TEXT("charge snapshot captures deployment epoch"),W->GetChargeSnapshot().SpawnEpoch,U->GetBattleAssistEpoch());
  Charge->CancelCharge(); TestFalse(TEXT("cancel clears replicated HUD state"),U->IsChargeFireActive());
 }
 F.Equip(U,K::WeaponChargeTimeReduction,30.f); W->InitializeSlots({Gun});
 auto* OldCharge=NewObject<UGA_Fire_Charge>(U); OldCharge->StartCharge(U,U->GetActorLocation(),FVector::ForwardVector);
 U->InitializeUnit(F.Suit.Get()); TestFalse(TEXT("new spawn clears charge HUD"),U->IsChargeFireActive());
 W->InitializeSlots({Gun}); auto* NewCharge=NewObject<UGA_Fire_Charge>(U); NewCharge->StartCharge(U,U->GetActorLocation(),FVector::ForwardVector);
 OldCharge->CancelCharge(); TestTrue(TEXT("stale prior spawn cancellation cannot clear new charge"),U->IsChargeFireActive());
 TestEqual(TEXT("new unequipped charge duration returns to base"),W->GetChargeSnapshot().Duration,2.f);
 NewCharge->CancelCharge();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistMovementConsumers,"NeoMSGO.BattleAssist.Combat.MovementConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistMovementConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn(); auto* M=U->GetMSCharacterMovement();
 M->SetMovementMode(MOVE_Walking); const float Walk=M->GetMaxSpeed();
 F.Equip(U,K::WalkSpeedIncrease,20.f); M->SetMovementMode(MOVE_Walking);
 TestTrue(TEXT("normal movement +20 percent"),FMath::IsNearlyEqual(M->GetMaxSpeed(),Walk*1.2f,.01f));
 M->SetBoostInputHeld(true); const float Boost=M->GetMaxSpeed();
 U->InitializeUnit(F.Suit.Get()); M->SetMovementMode(MOVE_Walking); M->SetBoostInputHeld(true);
 TestTrue(TEXT("walk module does not alter boost"),FMath::IsNearlyEqual(M->GetMaxSpeed(),Boost,.01f)); M->SetBoostInputHeld(false);
 for(float Ratio:{85.f,90.f,95.f})
 {
  F.Equip(U,K::WaterInaptitudeSpeedRatio,Ratio); M->MaxSwimSpeed=1000.f; M->SetMovementMode(MOVE_Swimming);
  TestEqual(TEXT("underwater speed override"),M->GetMaxSpeed(),Ratio*10.f);
  M->SetAmphibious(true); TestEqual(TEXT("amphibious does not get water penalty"),M->GetMaxSpeed(),1000.f);
  M->SetAmphibious(false); M->SetMovementMode(MOVE_Walking); TestTrue(TEXT("dry walk unmodified"),FMath::IsNearlyEqual(M->GetMaxSpeed(),Walk,.01f));
 }
 auto* B=U->FindComponentByClass<UMSBoostComponent>(); auto* ASC=U->GetAbilitySystemComponent();
 for(float P:{10.f,15.f,25.f})
 {
  F.Equip(U,K::OverheatRecoveryReduction,P); B->SetRates(20.f,10.f); U->GetBoostSet()->SetBoost(0.f);
  ASC->SetLooseGameplayTagCount(MSTags::State_Overheated,1);
  const float Start=U->GetBoostSet()->GetBoost(); F.Tick(.11f);
  TestTrue(TEXT("real OH recovery is accelerated"),U->GetBoostSet()->GetBoost()>Start+1.0f);
  TestTrue(TEXT("ordinary OH rate corresponds to duration reduction"),FMath::IsNearlyEqual(B->GetEffectiveRechargePerSecond(),10.f/(1.f-P*.01f)));
  ASC->SetLooseGameplayTagCount(MSTags::State_Module_ForcedOverheat,1);
  TestEqual(TEXT("Transzomm forced OH unchanged"),B->GetEffectiveRechargePerSecond(),10.f);
  ASC->SetLooseGameplayTagCount(MSTags::State_Module_ForcedOverheat,0); ASC->SetLooseGameplayTagCount(MSTags::State_Overheated,0);
  TestEqual(TEXT("ordinary non-OH regen unchanged"),B->GetEffectiveRechargePerSecond(),10.f);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistLifecycleConsumers,"NeoMSGO.BattleAssist.Combat.LifecycleConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistLifecycleConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn(1); auto* Enemy=F.Spawn(2,FVector(10000,0,0));
 auto* Gun=NewObject<UMSRangedWeaponData>(); auto* Recall=U->GetEmergencyRecallComponent(); auto* Move=U->GetMSCharacterMovement();
 F.Equip(U,K::RecallInterruptRequiresStagger,1.f); Move->SetMovementMode(MOVE_Walking);
 auto* Weapon=U->GetWeaponComponent(); Gun->ReserveAmmo=11; Weapon->InitializeSlots({Gun}); Weapon->ConsumeAmmoAndStartCooldown(0);
 const int32 Epoch=U->GetBattleAssistEpoch(); const int32 Magazine=Weapon->GetMagazineAmmo(0);
 TestTrue(TEXT("module recall starts on ground"),Recall->TryStartRecall());
 Damage(Enemy,U,Gun); TestTrue(TEXT("nonstagger damage does not cancel module recall"),Recall->IsRecallActive());
 U->GetVitalSet()->SetMaxDownPoint(100.f); Damage(Enemy,U,Gun,false,100.f);
 TestFalse(TEXT("same packet DP threshold cancels module recall"),Recall->IsRecallActive());
 TestEqual(TEXT("cancel does not apply a new module epoch"),U->GetBattleAssistEpoch(),Epoch);
 TestEqual(TEXT("cancel does not replenish magazine"),Weapon->GetMagazineAmmo(0),Magazine);
 U->InitializeUnit(F.Suit.Get()); Move->SetMovementMode(MOVE_Walking);
 // Reset only the component's cooldown through elapsed real world time.
 F.Tick(1.1f); Move->SetMovementMode(MOVE_Walking);
 if(Recall->GetRecallCooldownRemaining()>0.f) F.Tick(Recall->GetRecallCooldownRemaining()+.02f);
 Move->SetMovementMode(MOVE_Walking); TestTrue(TEXT("baseline recall starts"),Recall->TryStartRecall());
 Damage(Enemy,U,Gun); TestFalse(TEXT("baseline nonstagger hit cancels"),Recall->IsRecallActive());
 F.Equip(U,K::RecallInterruptRequiresStagger,1.f);
 if(Recall->GetRecallCooldownRemaining()>0.f) F.Tick(Recall->GetRecallCooldownRemaining()+.02f);
 Move->SetMovementMode(MOVE_Walking); TestTrue(TEXT("recall before lethal hit starts"),Recall->TryStartRecall());
 U->GetVitalSet()->SetArmor(100.f); Damage(Enemy,U,Gun); TestFalse(TEXT("lethal hit always cancels"),Recall->IsRecallActive());
 // Freeze the readyAt from deployed source values, even after the module catalog is changed.
 F.Equip(U,K::WalkSpeedIncrease,20.f); auto* Respawn=U->GetRespawnComponent();
 Respawn->ActiveDeckSlotIndex=2; Respawn->ClearDeathFlow();
 const double Before=F.World->GetTimeSeconds(); U->GetVitalSet()->SetArmor(100.f); Damage(Enemy,U,Gun);
 TestTrue(TEXT("death uses resolved module TIME"),FMath::IsNearlyEqual(Respawn->GetDeckSlotTimeRemaining(2),54.f,.01f));
 const double Frozen=Respawn->DeckSlotReadyAt[2]; F.Module->Effects.Reset(); F.Rules->TimeRows[0].TotalSeconds[1]=1.f;
 TestEqual(TEXT("catalog or hangar edit cannot rewrite dead spawn readyAt"),Respawn->DeckSlotReadyAt[2],Frozen);
 TestTrue(TEXT("death readyAt is absolute"),FMath::IsNearlyEqual(Frozen,Before+54.0,.01));
 const int32 DeathEpoch=U->GetBattleAssistEpoch(); U->InitializeUnit(F.Suit.Get());
 TestTrue(TEXT("new spawn advances module epoch"),U->GetBattleAssistEpoch()>DeathEpoch);
 TestEqual(TEXT("empty spawn preserves legacy base TIME"),U->GetBattleAssistResolved().EffectiveReuseSeconds,50.f);
 TestTrue(TEXT("old slot cooldown stays frozen after new spawn"),FMath::IsNearlyEqual(Respawn->DeckSlotReadyAt[2],Frozen));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistMeleeAndRepairConsumers,"NeoMSGO.BattleAssist.Combat.MeleeAndRepairConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistMeleeAndRepairConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn(1); auto* Target=F.Spawn(2,FVector(3000,0,0));
 auto* Saber=NewObject<UMSMeleeWeaponData>(); Saber->MagazineSize=0; Saber->Range=5.f; Saber->Power=1; Saber->DownPoint=0;
 Saber->CategoryTag=MSTags::Weapon_Category_Melee;
 F.Equip(U,K::DashMeleeBoostReduction,10.f); U->GetWeaponComponent()->InitializeSlots({Saber});
 auto* Melee=NewObject<UGA_Melee>(U); U->GetBoostSet()->SetBoost(100.f);
 TestTrue(TEXT("actual D melee starts"),Melee->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,true,1.f));
 TestEqual(TEXT("D melee cost 30 reduced to 27"),U->GetBoostSet()->GetBoost(),73.f); Melee->CancelMelee();
 for(float P:{10.f,20.f,30.f})
 {
  U->SetActorLocation(FVector::ZeroVector); Target->SetActorLocation(FVector(3000,0,0));
  F.Equip(U,K::MeleeHitRecoveryReduction,P); U->GetWeaponComponent()->InitializeSlots({Saber});
  auto* Hit=NewObject<UGA_Melee>(U); TestTrue(TEXT("hit melee starts"),Hit->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,false,1.f));
  TestTrue(TEXT("real hostile collision hit"),Hit->GetLastHitCount()>0);
  F.Tick(.45f*(1.f-P*.01f)+.02f);
  TestFalse(TEXT("hit recovery ends early"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  F.Equip(U,K::MeleeHitRecoveryReduction,P); U->SetActorLocation(FVector::ZeroVector); Target->SetActorLocation(FVector(1000000,0,0));
  U->GetWeaponComponent()->InitializeSlots({Saber}); auto* Miss=NewObject<UGA_Melee>(U);
  Miss->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,false,1.f); TestEqual(TEXT("actual miss"),Miss->GetLastHitCount(),0);
  F.Tick(.45f*(1.f-P*.01f)+.005f); TestTrue(TEXT("whiff recovery unchanged"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  F.Tick(.46f); TestFalse(TEXT("whiff clears normally"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  F.Equip(U,K::MeleeHitRecoveryReduction,P); U->SetActorLocation(FVector::ZeroVector);
  Target->SetGenericTeamId(FGenericTeamId(1)); Target->SetActorLocation(FVector(3000,0,0));
  U->GetMSCharacterMovement()->DisableMovement(); Target->GetMSCharacterMovement()->DisableMovement();
  U->GetWeaponComponent()->InitializeSlots({Saber}); TStrongObjectPtr<UGA_Melee> Friendly(NewObject<UGA_Melee>(U));
  const float AllyArmor=Target->GetVitalSet()->GetArmor();
  TestTrue(TEXT("friendly-only swing starts"),Friendly->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,false,1.f));
  TestEqual(TEXT("legacy applied-count contract includes FF-rejected GE"),Friendly->GetLastHitCount(),1);
  TestEqual(TEXT("friendly hit is not successful for recovery"),Friendly->GetLastSuccessfulHitCount(),0);
  TestEqual(TEXT("GE execution preserves friendly armor"),Target->GetVitalSet()->GetArmor(),AllyArmor);
  F.Tick(.45f*(1.f-P*.01f)+.005f);
  TestTrue(TEXT("friendly overlap cannot shorten recovery"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  // Legacy normal CancelMelee deliberately retains recovery. Keep the ability alive until
  // its real 0.45-second timer expires before beginning the next rank's independent swing.
  F.Tick(.45f*P*.01f+.015f);
  TestFalse(TEXT("friendly-only recovery releases at its original deadline"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  Friendly->CancelMelee(); Target->SetGenericTeamId(FGenericTeamId(2));
 }
 for(float P:{15.f,30.f,40.f})
 {
  F.Equip(U,K::ReceivedRepairIncrease,P); U->GetVitalSet()->SetArmor(1000.f);
  auto* Pack=AMSSupplyPack::Spawn(F.World,U->GetActorLocation(),EMSSupplyPackType::Repair,FGenericTeamId(1));
  const float Base=Pack->HealAmount;
  TestTrue(TEXT("real repair pickup consumed"),Pack->TryConsume(U));
  TestTrue(TEXT("repair pickup includes recipient module"),FMath::IsNearlyEqual(U->GetVitalSet()->GetArmor(),1000.f+Base*(1.f+P*.01f),.01f));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistSaberStageConsumers,"NeoMSGO.BattleAssist.Combat.SaberStageConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistSaberStageConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F;
 auto* U=F.Spawn(1); auto* Target=F.Spawn(2); auto* Second=F.Spawn(2); auto* Late=F.Spawn(2);
 auto* Saber=NewObject<UMSMeleeWeaponData>(); Saber->MagazineSize=0; Saber->Power=100; Saber->DownPoint=0;
 Saber->FireInterval=.1f; Saber->bUseStandardBeamSaberMotion=true;
 // Preserve WeaponTrace overlap while removing body blocking from the attacker's timed lunge.
 for (auto* Unit : {U,Target,Second,Late}) Unit->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
 for (int32 Stage : {0,1,2,10,11,12,13,14})
 for (float Percent : {10.f,20.f,30.f})
 // -1 is an ally-only overlap: GE application remains observable, but cannot shorten recovery.
 for (int32 ExpectedHits : {-1,0,1,2})
 {
  const FString Label=FString::Printf(TEXT("stage %d rank %.0f hits %d: "),Stage,Percent,ExpectedHits);
  Target->SetGenericTeamId(FGenericTeamId(ExpectedHits<0?1:2));
  TestTrue(Label+TEXT("module resolves"),F.Equip(U,K::MeleeHitRecoveryReduction,Percent));
  U->SetActorLocation(FVector::ZeroVector); U->SetActorRotation(FRotator::ZeroRotator);
  for (auto* Unit : {U,Target,Second,Late}) Unit->GetMSCharacterMovement()->DisableMovement();
  for (auto* Enemy : {Target,Second,Late})
  {
   Enemy->SetActorLocation(FVector(1000000,0,0)); Enemy->GetVitalSet()->SetArmor(5000.f);
  }
  Saber->bUseGundamDualSaberMotion=Stage>=10; U->GetWeaponComponent()->InitializeSlots({Saber});
  TStrongObjectPtr<UGA_Melee> Ability(NewObject<UGA_Melee>(U));
  const bool bDash=Stage==2 || Stage==14;
  TestTrue(Label+TEXT("real attack starts"),Ability->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,bDash));
  if (!bDash)
  {
   // Reach later stages exclusively through the public combo input, with preceding stages whiffing.
   for (int32 Prior=Stage>=10?10:0; Prior<Stage; ++Prior)
   {
    F.Tick(FMSStandardSaberMotion::GetTiming(Prior).Duration+.03f);
    TestTrue(Label+TEXT("real follow-up accepted"),Ability->PerformMelee(U,U->GetActorLocation(),FVector::ForwardVector,false));
   }
  }
  TestEqual(Label+TEXT("requested stage reached"),Ability->GetLastStandardSaberAttackIndex(),Stage);
  const auto Timing=FMSStandardSaberMotion::GetTiming(Stage);
  const FVector HitPoint=StageBladeMidpoint(U,Stage,(Timing.ActiveStart+Timing.ActiveEnd)*.5f);
  if (ExpectedHits!=0) Target->SetActorLocation(HitPoint);
  if (ExpectedHits>1) Second->SetActorLocation(HitPoint);
  float Elapsed=0.f;
  auto AdvanceTo=[&](float Time) { if(Time>Elapsed) { F.Tick(Time-Elapsed); Elapsed=Time; } };
  AdvanceTo(Timing.ActiveStart-.02f);
  TestEqual(Label+TEXT("startup damage window is unchanged"),Target->GetVitalSet()->GetArmor(),5000.f);
  TestEqual(Label+TEXT("no startup hit count"),Ability->GetLastHitCount(),0);
  AdvanceTo(Timing.ActiveEnd+.02f);
  TestEqual(Label+TEXT("actual blade applies GE to each intended actor once"),Ability->GetLastHitCount(),FMath::Abs(ExpectedHits));
  TestEqual(Label+TEXT("only hostile hit qualifies for recovery"),Ability->GetLastSuccessfulHitCount(),FMath::Max(0,ExpectedHits));
  TestTrue(Label+TEXT("first target damage matches hit or whiff"),ExpectedHits>0?Target->GetVitalSet()->GetArmor()<5000.f:Target->GetVitalSet()->GetArmor()==5000.f);
  const float ArmorAfterWindow=Target->GetVitalSet()->GetArmor();
  // A fresh actor at the blade after ActiveEnd must never receive recovery-period damage.
  FVector BladeStart,BladeEnd;
  FMSStandardSaberMotion::SampleBlade(Stage,Timing.ActiveEnd,100.f,BladeStart,BladeEnd,Stage==11);
  FTransform CurrentTransform=U->GetMechRigComponent()->GetRigRoot()->GetComponentTransform();
  Late->SetActorLocation(CurrentTransform.TransformPosition((BladeStart+BladeEnd)*.5f));
  const float RecoveryStart=FMath::Max(Timing.ActiveEnd,Timing.LungeEnd);
  const float ReducedEnd=RecoveryStart+(Timing.Duration-RecoveryStart)*(1.f-Percent*.01f);
  AdvanceTo(ReducedEnd-.015f);
  TestTrue(Label+TEXT("tail is not shortened more than once for multiple hits"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  AdvanceTo(ReducedEnd+.015f);
  TestEqual(Label+TEXT("only a successful hit ends recovery early"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery),ExpectedHits<=0);
  TestEqual(Label+TEXT("repeated blade samples do not damage same actor again"),Target->GetVitalSet()->GetArmor(),ArmorAfterWindow);
  TestEqual(Label+TEXT("damage cannot extend into recovery"),Late->GetVitalSet()->GetArmor(),5000.f);
  if (ExpectedHits<=0)
  {
   AdvanceTo(Timing.Duration-.015f);
   TestTrue(Label+TEXT("whiff retains full original duration"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
   AdvanceTo(Timing.Duration+.015f);
   TestFalse(Label+TEXT("whiff releases at original deadline"),U->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
  }
  Ability->CancelMelee();
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistProjectileConsumers,"NeoMSGO.BattleAssist.Combat.ProjectileConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistProjectileConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn();
 auto* Rocket=NewObject<UMSRangedWeaponData>(); Rocket->CategoryTag=MSTags::Weapon_Category_Bazooka;
 Rocket->ProjectileSpeed=30.f; Rocket->SplashRadius=10.f; Rocket->SplashDamage=1000; Rocket->Power=1000;
 for(float P:{2.f,3.f,4.f})
 {
  F.Equip(U,K::BlastRadiusIncrease,P);
  for(EMSProjectileKind Kind:{EMSProjectileKind::Rocket,EMSProjectileKind::LaunchedBallistic,EMSProjectileKind::Thrown})
  {
   Rocket->ProjectileKind=Kind;
   auto* Projectile=AMSVisualProjectile::LaunchVisualProjectile(U,U->GetActorLocation(),FVector::ForwardVector,Rocket);
   TestNotNull(TEXT("real projectile launch"),Projectile);
   if(Projectile) { TestTrue(TEXT("blast control target filter"),FMath::IsNearlyEqual(Projectile->GetSplashRadiusCm(),10.f*100.f*MSGO::MSScale*(Kind==EMSProjectileKind::Thrown?1.f:1.f+P*.01f),.01f)); Projectile->Destroy(); }
  }
 }
 U->InitializeUnit(F.Suit.Get()); Rocket->ProjectileKind=EMSProjectileKind::Rocket;
 auto* Plain=AMSVisualProjectile::LaunchVisualProjectile(U,U->GetActorLocation(),FVector::ForwardVector,Rocket);
 if(Plain) TestTrue(TEXT("new empty spawn splash reset"),FMath::IsNearlyEqual(Plain->GetSplashRadiusCm(),10.f*100.f*MSGO::MSScale,.01f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistMissileConsumers,"NeoMSGO.BattleAssist.Combat.MissileConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistMissileConsumers::RunTest(const FString&)
{
 using namespace MSBattleAssistCombatTests; FFixture F; auto* U=F.Spawn(1); auto* Target=F.Spawn(2);
 auto* Homing=NewObject<UMSHomingWeaponData>(); Homing->CategoryTag=MSTags::Weapon_Category_Homing; Homing->Range=100.f; Homing->LockOnRange=100.f;
 auto* Lock=U->GetLockOnComponent(); auto* Camera=U->GetMSCamera();
 for(float Seconds:{.1f,.2f,.4f})
 {
  F.Equip(U,K::MissileLockTimeReduction,Seconds); U->GetWeaponComponent()->InitializeSlots({Homing}); Lock->ClearLockOn();
  const FVector AimOffset=Target->GetLockOnAimPoint()-Target->GetActorLocation();
  Target->SetActorLocation(Camera->GetComponentLocation()+Camera->GetForwardVector()*10000.f-AimOffset);
  Lock->TickComponent(.001f,LEVELTICK_All,nullptr);
  Lock->TickComponent(.8f-Seconds-.02f,LEVELTICK_All,nullptr);
  TestNull(TEXT("missile capture not ready before reduced dwell"),Lock->GetHomingCapturedTarget());
  Lock->TickComponent(.03f,LEVELTICK_All,nullptr);
  TestEqual(TEXT("actual missile candidate captures after reduced dwell"),Lock->GetHomingCapturedTarget(),static_cast<AActor*>(Target));
  TestEqual(TEXT("captured HUD progress"),Lock->GetHomingDwellProgress(),1.f);
  Target->SetActorLocation(FVector(-1000000,0,0)); Lock->TickComponent(.01f,LEVELTICK_All,nullptr);
  TestNull(TEXT("out of cone clears capture"),Lock->GetHomingCapturedTarget());
 }
 U->InitializeUnit(F.Suit.Get()); U->GetWeaponComponent()->InitializeSlots({Homing}); Lock->ClearLockOn();
 const FVector AimOffset=Target->GetLockOnAimPoint()-Target->GetActorLocation();
 Target->SetActorLocation(Camera->GetComponentLocation()+Camera->GetForwardVector()*10000.f-AimOffset);
 Lock->TickComponent(.001f,LEVELTICK_All,nullptr); Lock->TickComponent(.65f,LEVELTICK_All,nullptr);
 TestNull(TEXT("unequipped baseline remains 0.8 second"),Lock->GetHomingCapturedTarget());
 Lock->TickComponent(.2f,LEVELTICK_All,nullptr); TestEqual(TEXT("baseline captures normally"),Lock->GetHomingCapturedTarget(),static_cast<AActor*>(Target));
 return true;
}
#endif
