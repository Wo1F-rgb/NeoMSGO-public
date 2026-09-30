// Copyright NeoMSGO Project. All Rights Reserved.

#include "Data/MSBattleRuleRow.h"

const TArray<FMSBattleRuleRow>& FMSBattleRuleRow::GetBuiltInRules()
{
	static const TArray<FMSBattleRuleRow> Rules = []
	{
		TArray<FMSBattleRuleRow> Result;
		auto Add = [&Result](const TCHAR* Name, const float Kill, const float Base,
			const float Gauge, const float Duration)
		{
			FMSBattleRuleRow& Row = Result.AddDefaulted_GetRef();
			Row.Name = FText::FromString(Name);
			Row.KillDamage = Kill;
			Row.HomeBaseDamage = Base;
			Row.MaxGauge = Gauge;
			Row.DurationSeconds = Duration;
		};
		// Wiki confirms COST dependence, but its 60,000 total is a historical
		// community estimate. This normalized linear baseline is project tuning,
		// not a claimed exact late-service original formula.
		Add(TEXT("拠点制圧戦"), 10000.0f * 200.0f / 60000.0f, 2500, 10000, 1200);
		Result.Last().bScaleKillDamageByCost = true;
		Add(TEXT("本拠点強襲戦"), 0, 2500, 10000, 900);
		Add(TEXT("殲滅戦"), 500, 0, 10000, 600);
		return Result;
	}();
	return Rules;
}
