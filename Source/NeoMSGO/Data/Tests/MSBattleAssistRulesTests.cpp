#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistConstraintsTest, "NeoMSGO.BattleAssist.Data.ConstraintsAndTime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistConstraintsTest::RunTest(const FString&)
{
 UMSBattleAssistRulesData* Rules = NewObject<UMSBattleAssistRulesData>();
 MSBattleAssist::SetRulesDataForTests(Rules);
 ON_SCOPE_EXIT { MSBattleAssist::SetRulesDataForTests(nullptr); };
 UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>();
 Suit->Cost = 200; Suit->ReuseDelaySeconds = -1.f;
 FMSBattleAssistSuitProfile Profile; Profile.SuitId = Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity = 4;
 Rules->SuitProfiles.Add(Profile);
 FMSBattleAssistTimeRow Time; Time.Cost = 200; Time.TotalSeconds = {50,54,58,62,66,77,85,96,110,134,189}; Rules->TimeRows.Add(Time);
 auto AddModule = [&](const TCHAR* Family, int32 Slots)
 {
  UMSBattleAssistModuleData* M = NewObject<UMSBattleAssistModuleData>(Rules);
  M->FamilyId = Family; M->SlotCost = Slots; M->Evidence = EMSModuleEvidence::Confirmed; M->Support = EMSModuleSupport::Implemented;
  FMSBattleAssistEffect Effect; Effect.Kind = EMSBattleAssistEffectKind::ReloadTimeReduction; Effect.Value = 4; Effect.Unit = "percent";
  M->Effects.Add(Effect); Rules->Modules.Add(M); return M;
 };
 UMSBattleAssistModuleData* A = AddModule(TEXT("reload"), 2);
 UMSBattleAssistModuleData* A2 = AddModule(TEXT("reload"), 3);
 UMSBattleAssistModuleData* B = AddModule(TEXT("switch"), 2);
 FMSBattleAssistProgress Progress;
 Progress.DevelopedModuleIds = {A->GetPrimaryAssetId(), A2->GetPrimaryAssetId(), B->GetPrimaryAssetId()};
 FMSBattleAssistLoadout Loadout; FMSBattleAssistValidationResult Error; FMSBattleAssistResolvedLoadout Resolved;
 Loadout.ModuleIds = {A->GetPrimaryAssetId(),B->GetPrimaryAssetId()};
 TestTrue(TEXT("two different families exactly fill four slots"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestTrue(TEXT("resolve accepted set"), MSBattleAssist::ResolveLoadout(Suit, Loadout, Resolved, Error));
 TestEqual(TEXT("occupied slots sum costs rather than item count"), Resolved.UsedSlots, 4);
 TestEqual(TEXT("cost200 four slots TIME66, not base+66"), Resolved.EffectiveReuseSeconds, 66.f);
 Suit->ReuseDelaySeconds = 25;
 MSBattleAssist::ResolveLoadout(Suit, Loadout, Resolved, Error);
 TestEqual(TEXT("custom base retained with original additional16 seconds"), Resolved.EffectiveReuseSeconds, 41.f);
 Suit->ReuseDelaySeconds = -1;
 Loadout.ModuleIds.Add(A->GetPrimaryAssetId());
 TestFalse(TEXT("duplicate rejected"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestEqual(TEXT("duplicate error stable"), Error.Code, FName("Duplicate"));
 Loadout.ModuleIds = {A->GetPrimaryAssetId(), A2->GetPrimaryAssetId()};
 TestFalse(TEXT("levels of same family exclusive"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestEqual(TEXT("family reason"), Error.Code, FName("FamilyConflict"));
 Loadout.ModuleIds = {A2->GetPrimaryAssetId(), B->GetPrimaryAssetId()};
 TestFalse(TEXT("five slots exceeds four"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestEqual(TEXT("slot reason"), Error.Code, FName("SlotOverflow"));
 Loadout.ModuleIds = {A->GetPrimaryAssetId(), B->GetPrimaryAssetId()};
 B->ConflictingFamilies.Add(A->FamilyId);
 TestFalse(TEXT("one-way authored explicit conflict enforced symmetrically"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestEqual(TEXT("conflict reason"), Error.Code, FName("ExplicitConflict"));
 B->ConflictingFamilies.Reset(); Loadout.ModuleIds = {A->GetPrimaryAssetId()};
 Progress.DevelopedModuleIds.Reset();
 TestFalse(TEXT("not developed is rejected"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 TestEqual(TEXT("development reason"), Error.Code, FName("NotDeveloped"));
 A->Support = EMSModuleSupport::UnresolvedValue;
 TestFalse(TEXT("unknown coefficients cannot equip even without progress guard"), MSBattleAssist::ValidateLoadout(Suit, Loadout, nullptr, Error));
 TestEqual(TEXT("unsupported reason"), Error.Code, FName("UnsupportedRule"));
 Loadout.ModuleIds.Reset(); Loadout.ACModuleIds = {A->GetPrimaryAssetId()};
 TestFalse(TEXT("unconfirmed AC cannot grant free capacity"), MSBattleAssist::ValidateLoadout(Suit, Loadout, nullptr, Error));
 TestEqual(TEXT("AC reason"), Error.Code, FName("InvalidAC"));
 Loadout.ACModuleIds.Reset();
 TestTrue(TEXT("clear all requires no progress"), MSBattleAssist::ValidateLoadout(Suit, Loadout, &Progress, Error));
 A->Support = EMSModuleSupport::Implemented;
 A->SlotCost = 0;
 Rules->SuitProfiles[0].NativeBattleAssistModuleIds = {A->GetPrimaryAssetId()};
 TestTrue(TEXT("native ability survives empty user equipment"), MSBattleAssist::ResolveLoadout(Suit,Loadout,Resolved,Error));
 TestTrue(TEXT("native id included in resolved set"), Resolved.EffectiveModuleIds.Contains(A->GetPrimaryAssetId()));
 TestEqual(TEXT("native ability consumes no normal slots"), Resolved.UsedSlots, 0);
 TestEqual(TEXT("native ability adds no TIME"), Resolved.EffectiveReuseSeconds, 50.f);
 A->SlotCost = 1;
 TestFalse(TEXT("native profile cannot hide a paid-slot module as free ability"), MSBattleAssist::ValidateLoadout(Suit,Loadout,nullptr,Error));
 TestEqual(TEXT("invalid native slot cost reason"), Error.Code,FName("InvalidSlotCost"));
 A->SlotCost = 0;
 Loadout.ModuleIds = {A2->GetPrimaryAssetId()};
 TestFalse(TEXT("native family excludes its standard variant"), MSBattleAssist::ValidateLoadout(Suit,Loadout,nullptr,Error));
 TestEqual(TEXT("native exclusion reason"), Error.Code,FName("FamilyConflict"));
 Rules->SuitProfiles[0].NativeBattleAssistModuleIds.Reset();
 Loadout.ModuleIds = {FPrimaryAssetId(TEXT("MSWeaponData:DA_InvalidModule"))};
 TestFalse(TEXT("weapon ids cannot masquerade as modules"), MSBattleAssist::ValidateLoadout(Suit,Loadout,nullptr,Error));
 TestEqual(TEXT("unknown id reason"), Error.Code,FName("UnknownId"));
 Loadout.ModuleIds.Reset();
 float Seconds = 0;
 TestFalse(TEXT("negative slots not accepted"), MSBattleAssist::ResolveReuseDelaySeconds(Suit,-1,0,Seconds));
 TestFalse(TEXT("over maximum table not accepted"), MSBattleAssist::ResolveReuseDelaySeconds(Suit,11,0,Seconds));
 Rules->TimeRows[0].TotalSeconds[1] = -1;
 TestFalse(TEXT("unsupported TIME never instant reuse"), MSBattleAssist::ResolveReuseDelaySeconds(Suit,1,0,Seconds));
 Rules->Modules.Add(TSoftObjectPtr<UMSBattleAssistModuleData>());
 AddExpectedError(TEXT("Battle Assist catalog reference is unavailable"), EAutomationExpectedErrorFlags::Contains, 1);
 TestTrue(TEXT("one missing reference disables whole catalog, not a partial list"), MSBattleAssist::GetCatalog().IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistCatalogTest, "NeoMSGO.BattleAssist.Data.PersistedCatalog",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistCatalogTest::RunTest(const FString&)
{
 const auto Catalog = MSBattleAssist::GetCatalog();
 if (!TestEqual(TEXT("125 researched variants persisted"), Catalog.Num(), 125)) return false;
 TSet<FPrimaryAssetId> IDs;
 int32 Implemented = 0, Unknown = 0, Foundation = 0;
 for (const auto* M : Catalog)
 {
  TestFalse(TEXT("unique primary id"), IDs.Contains(M->GetPrimaryAssetId())); IDs.Add(M->GetPrimaryAssetId());
  TestTrue(TEXT("source retained"), M->SourceUrl.StartsWith(TEXT("https://gow.swiki.jp/")));
  TestFalse(TEXT("family present"), M->FamilyId.IsNone());
  TestTrue(TEXT("positive slot cost within original range"), M->SlotCost>0 && M->SlotCost<=10);
  if (M->Support == EMSModuleSupport::Implemented)
  {
   ++Implemented; TestEqual(TEXT("implemented value is evidenced"), M->Evidence, EMSModuleEvidence::Confirmed);
   TestFalse(TEXT("implemented effect is not empty"), M->Effects.IsEmpty());
  }
  if (M->Support == EMSModuleSupport::UnresolvedValue) ++Unknown;
  if (M->Support == EMSModuleSupport::RequiresFoundation) ++Foundation;
 }
 TestEqual(TEXT("55 variants have core consumers to verify"), Implemented, 55);
 TestEqual(TEXT("26 evidenced variants still require foundation"), Foundation, 26);
 TestEqual(TEXT("44 unknown original coefficients remain honestly disabled"), Unknown, 44);
 const auto* Rules = MSBattleAssist::GetRulesData();
 if (!TestNotNull(TEXT("rules asset"), Rules)) return false;
 TestEqual(TEXT("14 original Neo suits explicitly profiled"), Rules->SuitProfiles.Num(), 14);
 for (const auto& Profile : Rules->SuitProfiles)
 {
  const FString Name = Profile.SuitId.PrimaryAssetName.ToString();
  UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr, *FString::Printf(TEXT("/Game/Data/MobileSuits/%s.%s"), *Name, *Name));
  if (!TestNotNull(Name, Suit)) continue;
  FMSBattleAssistLoadout L; FMSBattleAssistValidationResult E;
  TestTrue(TEXT("existing empty equipment remains compatible"), MSBattleAssist::ValidateLoadout(Suit,L,nullptr,E));
  float Previous = Suit->GetReuseDelaySeconds();
  for (int32 Slot=1; Slot<=Profile.BattleAssistSlotCapacity; ++Slot)
  {
   float Seconds = 0;
   TestTrue(TEXT("every available slot has TIME"), MSBattleAssist::ResolveReuseDelaySeconds(Suit,Slot,0,Seconds));
   TestTrue(TEXT("TIME increases with occupied slots"), Seconds>Previous); Previous=Seconds;
  }
 }
 return true;
}
#endif
