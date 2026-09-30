#include "Data/MSBattleAssistModuleData.h"
#include "Misc/DataValidation.h"

const FPrimaryAssetType UMSBattleAssistModuleData::PrimaryAssetType(TEXT("MSBattleAssistModuleData"));

FPrimaryAssetId UMSBattleAssistModuleData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult UMSBattleAssistModuleData::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = true;
	auto Error = [&](const TCHAR* Message) { Context.AddError(FText::FromString(Message)); bValid = false; };
	if (DisplayName.IsEmpty() || FamilyId.IsNone()) Error(TEXT("モジュール名と排他系列が必要です。"));
	if (SlotCost < 0 || SlotCost > 10 || Rank < 0 || Rank > 3) Error(TEXT("モジュールのスロット数または段階が不正です。"));
	if (Support == EMSModuleSupport::Implemented && (Effects.IsEmpty() || Evidence != EMSModuleEvidence::Confirmed))
		Error(TEXT("使用可能なモジュールには確認済み効果が必要です。"));
	for (const FMSBattleAssistEffect& Effect : Effects)
		if (!FMath::IsFinite(Effect.Value) || Effect.Value < 0.f || Effect.Unit.IsNone())
			Error(TEXT("効果値または単位が不正です。"));
	if (bRecipeKnown)
	{
		if (DevelopmentCostGP < 0 || DevelopmentMaterials.IsEmpty()) Error(TEXT("開発レシピが不正です。"));
		for (const auto& Pair : DevelopmentMaterials)
			if (Pair.Key.IsNone() || Pair.Value <= 0) Error(TEXT("開発素材が不正です。"));
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
