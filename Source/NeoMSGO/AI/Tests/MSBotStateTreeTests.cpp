#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "AbilitySystemComponent.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "StateTree.h"
#include "Weapons/GA_Fire_Hitscan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotStateTreeRuntimeTest,
	"NeoMSGO.AI.BotAI.Review.StateTreeAssignedAndTargetAcquired",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotStateTreeRuntimeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBotStateTreeRuntimeWorld"));
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->GetBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* Bot = World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AMSBotCharacter* Enemy = World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
	Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
	Enemy->SetGenericTeamId(MSTeam::EnemyTeamId());

	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	Controller->Tick(0.6f);

	UStateTree* AssignedTree = Controller->GetAssignedStateTree();
	TestNotNull(TEXT("StateTreeAIComponent receives the generated StateTree asset"), AssignedTree);
	if (AssignedTree)
	{
		TestEqual(TEXT("Assigned StateTree asset path"), AssignedTree->GetPathName(),
			FString(TEXT("/Game/AI/ST_MSBotCombat.ST_MSBotCombat")));
		TestTrue(TEXT("Generated StateTree is compiled and ready"), AssignedTree->IsReadyToRun());
	}
	TestSamePtr(TEXT("Nearby hostile is acquired by the bot controller"),
		static_cast<AMSUnitBase*>(Controller->GetCurrentTarget()), static_cast<AMSUnitBase*>(Enemy));
	TestNotNull(TEXT("BOT ASC is granted a ranged fire ability used by the Attack state"),
		Bot->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UGA_Fire_Hitscan::StaticClass()));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
