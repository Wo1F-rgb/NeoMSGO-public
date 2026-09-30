#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TStrongObjectPtr<UMSBattleAssistRulesData> LoadedRules;
#if WITH_DEV_AUTOMATION_TESTS
	TStrongObjectPtr<UMSBattleAssistRulesData> TestRules;
#endif
	bool Fail(FMSBattleAssistValidationResult& Out, const TCHAR* Code, const TCHAR* Message, FPrimaryAssetId Id = FPrimaryAssetId())
	{
		Out = FMSBattleAssistValidationResult();
		Out.Code = FName(Code); Out.Reason = FText::FromString(Message); Out.OffendingModuleId = Id;
		return false;
	}
}

UMSBattleAssistRulesData* MSBattleAssist::GetRulesData()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestRules.IsValid()) return TestRules.Get();
#endif
	if (!LoadedRules.IsValid())
		LoadedRules.Reset(LoadObject<UMSBattleAssistRulesData>(nullptr,
			TEXT("/Game/Data/BattleAssist/DA_BattleAssistRules.DA_BattleAssistRules")));
	return LoadedRules.Get();
}

TArray<UMSBattleAssistModuleData*> MSBattleAssist::GetCatalog()
{
	TArray<UMSBattleAssistModuleData*> Result;
	if (const UMSBattleAssistRulesData* Rules = GetRulesData())
		for (const TSoftObjectPtr<UMSBattleAssistModuleData>& Reference : Rules->Modules)
		{
			UMSBattleAssistModuleData* Module = Reference.LoadSynchronous();
			if (!Module)
			{
				UE_LOG(LogTemp, Error, TEXT("Battle Assist catalog reference is unavailable: %s. Catalog disabled."), *Reference.ToSoftObjectPath().ToString());
				return {};
			}
			Result.Add(Module);
		}
	return Result;
}

UMSBattleAssistModuleData* MSBattleAssist::FindModule(const FPrimaryAssetId& Id)
{
	if (!Id.IsValid() || Id.PrimaryAssetType != UMSBattleAssistModuleData::PrimaryAssetType) return nullptr;
	for (UMSBattleAssistModuleData* Module : GetCatalog()) if (Module->GetPrimaryAssetId() == Id) return Module;
	return nullptr;
}

const FMSBattleAssistSuitProfile* MSBattleAssist::GetSuitProfile(const FPrimaryAssetId& SuitId)
{
	if (const UMSBattleAssistRulesData* Rules = GetRulesData())
		return Rules->SuitProfiles.FindByPredicate([&](const FMSBattleAssistSuitProfile& Profile) { return Profile.SuitId == SuitId; });
	return nullptr;
}

bool MSBattleAssist::ResolveReuseDelaySeconds(const UMSMobileSuitData* Suit, int32 UsedSlots, int32 UsedACSlots, float& OutSeconds)
{
	OutSeconds = 0.f;
	if (!IsValid(Suit) || UsedSlots < 0 || UsedSlots > 10 || UsedACSlots != 0) return false;
	float BaseSeconds = Suit->GetReuseDelaySeconds();
	if (!FMath::IsFinite(BaseSeconds) || BaseSeconds < 0.f) return false;
	if (UsedSlots == 0 && BaseSeconds > 0.f) { OutSeconds = BaseSeconds; return true; }
	const UMSBattleAssistRulesData* Rules = GetRulesData();
	if (!Rules) return false;
	const FMSBattleAssistTimeRow* Row = Rules->TimeRows.FindByPredicate([&](const FMSBattleAssistTimeRow& Item) { return Item.Cost == Suit->Cost; });
	if (!Row || Row->TotalSeconds.Num() != 11 || Row->TotalSeconds[0] <= 0.f || Row->TotalSeconds[UsedSlots] <= 0.f) return false;
	// Neo-only costs (e.g. Guardian's230) need an explicitly authored table row.
	// No generic cost interpolation and no modification of the user's suit asset.
	if (BaseSeconds == 0.f && Suit->ReuseDelaySeconds < 0.f) BaseSeconds = Row->TotalSeconds[0];
	if (BaseSeconds <= 0.f) return false;
	// The table is TOTAL time. Preserve a custom suit's explicit base and apply only the table delta.
	OutSeconds = BaseSeconds + Row->TotalSeconds[UsedSlots] - Row->TotalSeconds[0];
	return FMath::IsFinite(OutSeconds) && OutSeconds >= BaseSeconds;
}

bool MSBattleAssist::ValidateLoadout(const UMSMobileSuitData* Suit, const FMSBattleAssistLoadout& Loadout,
	const FMSBattleAssistProgress* Progress, FMSBattleAssistValidationResult& Out)
{
	Out = FMSBattleAssistValidationResult();
	if (!IsValid(Suit)) return Fail(Out, TEXT("InvalidSuit"), TEXT("機体が選択されていません。"));
	if (Loadout.ModuleIds.Num() > 10 || Loadout.ACModuleIds.Num() > 6)
		return Fail(Out, TEXT("SlotOverflow"), TEXT("装備できるモジュール数を超えています。"));
	// AC remains a separate, preserved field; uncertain rules do not grant free additional slots.
	if (!Loadout.ACModuleIds.IsEmpty()) return Fail(Out, TEXT("InvalidAC"), TEXT("この機体のACモジュール枠は使用できません。"));
	const FMSBattleAssistSuitProfile* Profile = GetSuitProfile(Suit->GetPrimaryAssetId());
	if (!Profile && !Loadout.IsEmpty()) return Fail(Out, TEXT("InvalidSuit"), TEXT("この機体のモジュール枠が設定されていません。"));
	TArray<UMSBattleAssistModuleData*> Selected;
	TSet<FPrimaryAssetId> Seen;
	TSet<FName> Families;
	int32 UsedSlots = 0;
	auto Check = [&](const FPrimaryAssetId& Id, bool bNative) -> bool
	{
		UMSBattleAssistModuleData* Module = FindModule(Id);
		if (!Module) return Fail(Out, TEXT("UnknownId"), TEXT("登録されていないモジュールです。"), Id);
		if (Seen.Contains(Id)) return Fail(Out, TEXT("Duplicate"), TEXT("同じモジュールは重複して装備できません。"), Id);
		if (Families.Contains(Module->FamilyId)) return Fail(Out, TEXT("FamilyConflict"), TEXT("同じ系列のモジュールは同時に装備できません。"), Id);
		if (Module->FamilyId.IsNone() || Module->Support != EMSModuleSupport::Implemented || Module->Effects.IsEmpty()
			|| Module->Evidence != EMSModuleEvidence::Confirmed)
			return Fail(Out, TEXT("UnsupportedRule"), TEXT("このモジュールは現在装備できません。"), Id);
		if ((bNative && Module->SlotCost != 0) || (!bNative && Module->SlotCost <= 0) || Module->SlotCost < 0 || Module->SlotCost > 10)
			return Fail(Out, TEXT("InvalidSlotCost"), TEXT("機体固有のモジュールは付加枠へ装備できません。"), Id);
		if (!Module->AllowedUnitTypes.IsEmpty() && !Module->AllowedUnitTypes.Contains(Suit->UnitType))
			return Fail(Out, TEXT("ClassRestricted"), TEXT("この兵科には装備できません。"), Id);
		if (!bNative && Progress && !Progress->DevelopedModuleIds.Contains(Id))
			return Fail(Out, TEXT("NotDeveloped"), TEXT("先にこのモジュールを開発してください。"), Id);
		for (const UMSBattleAssistModuleData* Other : Selected)
		{
			if (Module->ConflictingFamilies.Contains(Other->FamilyId) || Other->ConflictingFamilies.Contains(Module->FamilyId)
				|| Module->ExplicitConflictIds.Contains(Other->GetPrimaryAssetId()) || Other->ExplicitConflictIds.Contains(Id))
				return Fail(Out, TEXT("ExplicitConflict"), TEXT("同時に装備できないモジュールの組み合わせです。"), Id);
		}
		for (const FMSBattleAssistEffect& Effect : Module->Effects)
			if (!FMath::IsFinite(Effect.Value) || Effect.Value < 0.f || Effect.Unit.IsNone())
				return Fail(Out, TEXT("UnsupportedRule"), TEXT("モジュールの効果設定が不正です。"), Id);
		Seen.Add(Id); Families.Add(Module->FamilyId); Selected.Add(Module);
		if (!bNative) UsedSlots += Module->SlotCost;
		return true;
	};
	if (Profile)
	{
		for (const FPrimaryAssetId& Id : Profile->NativeBattleAssistModuleIds) if (!Check(Id, true)) return false;
		if (Profile->BattleAssistSlotCapacity < 0 || Profile->BattleAssistSlotCapacity > 10)
			return Fail(Out, TEXT("InvalidSuit"), TEXT("機体のモジュール枠設定が不正です。"));
	}
	for (const FPrimaryAssetId& Id : Loadout.ModuleIds) if (!Check(Id, false)) return false;
	if (Profile && UsedSlots > Profile->BattleAssistSlotCapacity)
		return Fail(Out, TEXT("SlotOverflow"), TEXT("モジュールの最大スロット数を超えるため装備できません。"));
	float Seconds = 0.f;
	if (!ResolveReuseDelaySeconds(Suit, UsedSlots, 0, Seconds))
		return Fail(Out, TEXT("InvalidTime"), TEXT("この機体と装備の再出撃時間を計算できません。"));
	Out.bValid = true; return true;
}

bool MSBattleAssist::ResolveLoadout(const UMSMobileSuitData* Suit, const FMSBattleAssistLoadout& Loadout,
	FMSBattleAssistResolvedLoadout& Out, FMSBattleAssistValidationResult& Error)
{
	Out = FMSBattleAssistResolvedLoadout();
	if (!ValidateLoadout(Suit, Loadout, nullptr, Error)) return false;
	FMSBattleAssistResolvedLoadout Candidate;
	const FMSBattleAssistSuitProfile* Profile = GetSuitProfile(Suit->GetPrimaryAssetId());
	if (Profile) Candidate.EffectiveModuleIds = Profile->NativeBattleAssistModuleIds;
	Candidate.EffectiveModuleIds.Append(Loadout.ModuleIds);
	for (const FPrimaryAssetId& Id : Loadout.ModuleIds) Candidate.UsedSlots += FindModule(Id)->SlotCost;
	for (const FPrimaryAssetId& Id : Candidate.EffectiveModuleIds) Candidate.Effects.Append(FindModule(Id)->Effects);
	if (!ResolveReuseDelaySeconds(Suit, Candidate.UsedSlots, 0, Candidate.EffectiveReuseSeconds)) return false;
	if (const UMSBattleAssistRulesData* Rules = GetRulesData()) Candidate.RulesRevision = Rules->RulesRevision;
	Out = MoveTemp(Candidate); return true;
}

float MSBattleAssist::GetEffectValue(const FMSBattleAssistResolvedLoadout& Loadout, EMSBattleAssistEffectKind Kind, float DefaultValue)
{
	bool bFound = false; float Value = DefaultValue;
	for (const FMSBattleAssistEffect& Effect : Loadout.Effects)
		if (Effect.Kind == Kind) { Value = bFound ? FMath::Max(Value, Effect.Value) : Effect.Value; bFound = true; }
	return Value;
}

#if WITH_DEV_AUTOMATION_TESTS
void MSBattleAssist::SetRulesDataForTests(UMSBattleAssistRulesData* Data) { TestRules.Reset(Data); }
#endif
