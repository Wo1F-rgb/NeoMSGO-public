// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSDamageGameplayEffects.h"

#include "Combat/MSDamageExecution.h"

UGE_Damage::UGE_Damage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Armor減算+DownPoint加算はExecution(§1.6の6ステップ)が出力する。Modifierは持たない。
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UMSDamageExecution::StaticClass();
	Executions.Add(Execution);
}
