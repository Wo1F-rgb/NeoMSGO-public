// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MSBattleRuleRow.generated.h"

/** DataTable-compatible battle-rule definition used by the lobby operation cards. */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleRuleRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule") FText Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule", meta=(ClampMin="0.0")) float KillDamage = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule", meta=(ClampMin="0.0")) float HomeBaseDamage = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule", meta=(ClampMin="0.0")) float MaxGauge = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule", meta=(ClampMin="0.0")) float DurationSeconds = 0.0f;
	/** KillDamage is the loss for COST 200; explicit opt-in preserves custom rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Rule") bool bScaleKillDamageByCost = false;

	/** Authoritative code-defined rules shared by quick sortie and direct-IP rooms. */
	static const TArray<FMSBattleRuleRow>& GetBuiltInRules();
};
