#include "AI/MSBotStateTreeGenCommandlet.h"

#if WITH_EDITOR

#include "AI/MSBotStateTreeNodes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "Misc/PackageName.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	constexpr TCHAR BotStateTreePackageName[] = TEXT("/Game/AI/ST_MSBotCombat");
	constexpr TCHAR StateTreeAssetName[] = TEXT("ST_MSBotCombat");

	void SetRule(FStateTreeTransition& Transition, const EMSBotDistanceRule Rule)
	{
		auto& Condition = Transition.AddCondition<FMSBotDistanceCondition>();
		Condition.Node.GetMutable<FMSBotDistanceCondition>().Rule = Rule;
	}
}

int32 UMSBotStateTreeGenCommandlet::Main(const FString& Params)
{
	UPackage* Package = CreatePackage(BotStateTreePackageName);
	Package->FullyLoad();
	UStateTree* StateTree = FindObject<UStateTree>(Package, StateTreeAssetName);
	if (!StateTree)
	{
		StateTree = NewObject<UStateTree>(Package, StateTreeAssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree, NAME_None, RF_Transactional);
	StateTree->EditorData = EditorData;
	EditorData->Schema = NewObject<UStateTreeAIComponentSchema>(EditorData, NAME_None, RF_Transactional);
	EditorData->AddEvaluator<FMSBotTargetEvaluator>();

	UStateTreeState& Root = EditorData->AddRootState();
	Root.Name = TEXT("BotCombat");
	Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;

	UStateTreeState& Patrol = Root.AddChildState(TEXT("Patrol"));
	Patrol.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
	Patrol.AddTask<FMSBotPatrolTask>();

	UStateTreeState& Chase = Root.AddChildState(TEXT("Chase"));
	Chase.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
	Chase.AddTask<FMSBotChaseTask>();

	UStateTreeState& Attack = Root.AddChildState(TEXT("Attack"));
	Attack.SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
	Attack.AddTask<FMSBotAttackTask>();

	FStateTreeTransition& PatrolToChase = Patrol.AddTransition(
		EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Chase);
	SetRule(PatrolToChase, EMSBotDistanceRule::PatrolToChase);

	// Loss is checked before attack range so Chase always returns to Patrol cleanly.
	FStateTreeTransition& ChaseToPatrol = Chase.AddTransition(
		EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Patrol);
	SetRule(ChaseToPatrol, EMSBotDistanceRule::ChaseToPatrol);
	FStateTreeTransition& ChaseToAttack = Chase.AddTransition(
		EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Attack);
	SetRule(ChaseToAttack, EMSBotDistanceRule::ChaseToAttack);

	FStateTreeTransition& AttackToChase = Attack.AddTransition(
		EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Chase);
	SetRule(AttackToChase, EMSBotDistanceRule::AttackToChase);

	FStateTreeCompilerLog CompilerLog;
	FStateTreeCompiler Compiler(CompilerLog);
	if (!Compiler.Compile(*StateTree))
	{
		CompilerLog.DumpToLog(LogTemp);
		UE_LOG(LogTemp, Error, TEXT("MSBotStateTreeGen: compilation failed"));
		return 1;
	}

	StateTree->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(BotStateTreePackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, StateTree, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("MSBotStateTreeGen: failed to save %s"), BotStateTreePackageName);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("MSBotStateTreeGen: saved compiled bot StateTree %s"), BotStateTreePackageName);
	return 0;
}

#endif
