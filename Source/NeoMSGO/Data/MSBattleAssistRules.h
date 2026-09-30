#pragma once
#include "CoreMinimal.h"
#include "Data/MSBattleAssistTypes.h"
#include "Data/MSBattleAssistSuitProfile.h"

class UMSMobileSuitData;
class UMSBattleAssistModuleData;

namespace MSBattleAssist
{
	NEOMSGO_API UMSBattleAssistRulesData* GetRulesData();
	NEOMSGO_API TArray<UMSBattleAssistModuleData*> GetCatalog();
	NEOMSGO_API UMSBattleAssistModuleData* FindModule(const FPrimaryAssetId& Id);
	NEOMSGO_API const FMSBattleAssistSuitProfile* GetSuitProfile(const FPrimaryAssetId& SuitId);
	/** Null progress is only for an already admitted, server-validated loadout. */
	NEOMSGO_API bool ValidateLoadout(const UMSMobileSuitData* Suit, const FMSBattleAssistLoadout& Loadout,
		const FMSBattleAssistProgress* Progress, FMSBattleAssistValidationResult& Out);
	NEOMSGO_API bool ResolveLoadout(const UMSMobileSuitData* Suit, const FMSBattleAssistLoadout& Loadout,
		FMSBattleAssistResolvedLoadout& Out, FMSBattleAssistValidationResult& Error);
	NEOMSGO_API bool ResolveReuseDelaySeconds(const UMSMobileSuitData* Suit, int32 UsedSlots, int32 UsedACSlots, float& OutSeconds);
	NEOMSGO_API float GetEffectValue(const FMSBattleAssistResolvedLoadout& Loadout, EMSBattleAssistEffectKind Kind, float DefaultValue = 0.f);
#if WITH_DEV_AUTOMATION_TESTS
	NEOMSGO_API void SetRulesDataForTests(UMSBattleAssistRulesData* Data);
#endif
}
