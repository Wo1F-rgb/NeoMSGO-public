#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "AI/MSBotStateTreeNodes.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSCapturePoint.h"
#include "Components/CapsuleComponent.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotStrategicSpacingTest,
	"NeoMSGO.AI.BotAI.StrategicSpacing.TwentyFiveBots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBotStrategicSpacingTest::RunTest(const FString&)
{
	static int32 WorldSerial = 0;
	const FString PackageName = FString::Printf(TEXT("/Temp/MSBotStrategicSpacingWorld_%d"),
		++WorldSerial);
	UPackage* Package = CreatePackage(*PackageName);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		FName(TEXT("MSBotStrategicSpacingWorld")), Package);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const TArray<FVector> ObjectiveLocations = {
		FVector(120000.0f, -30000.0f, 0.0f),
		FVector(120000.0f, 0.0f, 0.0f),
		FVector(120000.0f, 30000.0f, 0.0f)
	};
	TArray<AMSCapturePoint*> Points;
	for (const FVector& Location : ObjectiveLocations)
	{
		Points.Add(World->SpawnActor<AMSCapturePoint>(AMSCapturePoint::StaticClass(),
			Location, FRotator::ZeroRotator, SpawnParams));
	}
	TMap<AMSCapturePoint*, TArray<FVector>> Destinations;
	TArray<AMSBotAIController*> Controllers;
	bool bAllValid = true;
	for (int32 Index = 0; Index < 25; ++Index)
	{
		AMSBotCharacter* Bot = World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
			FVector(0.0f, static_cast<float>(Index) * 10.0f, 0.0f), FRotator::ZeroRotator,
			SpawnParams);
		if (!Bot) { bAllValid = false; break; }
		Bot->SetGenericTeamId(MSTeam::PlayerTeamId());
		AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
		if (!Controller)
		{
			Controller = World->SpawnActor<AMSBotAIController>();
			if (Controller) Controller->Possess(Bot);
		}
		if (!Controller) { bAllValid = false; break; }
		Controllers.Add(Controller);
		AMSCapturePoint* Chosen = Controller->ChooseStrategicObjective();
		if (!Chosen) { bAllValid = false; break; }
		const float CapsuleRadius = Bot->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Destination = AMSBotAIController::CalculateObjectiveApproachPoint(
			Chosen->GetActorLocation(), Chosen->GetCaptureRadius(), CapsuleRadius,
			Controller->GetStrategicObjectiveSlot());
		TestTrue(TEXT("assigned destination is in the actual capture zone"),
			FVector::Dist2D(Destination, Chosen->GetActorLocation())
				< Chosen->GetCaptureRadius() * 0.8f);
		for (const FVector& Existing : Destinations.FindOrAdd(Chosen))
		{
			TestTrue(TEXT("same-objective destinations clear two full capsule radii"),
				FVector::Dist2D(Destination, Existing) > CapsuleRadius * 2.0f);
		}
		Destinations.FindOrAdd(Chosen).Add(Destination);
	}
	TestTrue(TEXT("all 25 bots receive independent assignments"), bAllValid);
	int32 Smallest = 25;
	int32 Largest = 0;
	for (AMSCapturePoint* Point : Points)
	{
		const int32 Assigned = Destinations.FindOrAdd(Point).Num();
		Smallest = FMath::Min(Smallest, Assigned);
		Largest = FMath::Max(Largest, Assigned);
	}
	TestTrue(TEXT("all three sectors receive a squad"), Smallest >= 6);
	TestTrue(TEXT("no sector absorbs the whole team"), Largest <= 11);
	// Casualties leave holes in the occupied slots. The next arrival must not use
	// the live count as a slot index and collide with an existing higher slot.
	if(Controllers.Num()==25)
	{
		Controllers[3]->UnPossess();
		AMSBotCharacter* Replacement=World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,SpawnParams);
		Replacement->SetGenericTeamId(MSTeam::PlayerTeamId());
		AMSBotAIController* ReplacementController=Cast<AMSBotAIController>(Replacement->GetController());
		if(!ReplacementController){ReplacementController=World->SpawnActor<AMSBotAIController>();ReplacementController->Possess(Replacement);}
		AMSCapturePoint* ReplacementObjective=ReplacementController->ChooseStrategicObjective();
		for(const AMSBotAIController* Existing:Controllers)
			if(Existing->GetPawn() && Existing->GetStrategicObjective()==ReplacementObjective)
				TestNotEqual(TEXT("replacement claims a genuinely vacant slot"),ReplacementController->GetStrategicObjectiveSlot(),Existing->GetStrategicObjectiveSlot());
	}
	for(uint32 A=0;A<25;++A)
		for(uint32 B=A+1;B<25;++B)
			TestTrue(TEXT("one remaining objective still has room for a full team"),FVector::Dist2D(
				AMSBotAIController::CalculateObjectiveApproachPoint(FVector::ZeroVector,7461.f,317.f,A),
				AMSBotAIController::CalculateObjectiveApproachPoint(FVector::ZeroVector,7461.f,317.f,B))>634.f);

	const FVector Target(10000.0f, 0.0f, 0.0f);
	const FVector ChaseA = FMSBotChaseTask::CalculateChaseDestination(
		FVector::ZeroVector, Target, 3500.0f, 2500.0f, 0);
	const FVector ChaseB = FMSBotChaseTask::CalculateChaseDestination(
		FVector::ZeroVector, Target, 3500.0f, 2500.0f, 6);
	TestTrue(TEXT("chase lanes do not have an identical endpoint"),
		FVector::Dist2D(ChaseA, ChaseB) > 500.0f);
	TestTrue(TEXT("chase endpoint stays inside actual weapon range"),
		FVector::Dist2D(ChaseA, Target) < 2500.0f && FVector::Dist2D(ChaseB, Target) < 2500.0f);
	TestTrue(TEXT("claim cost outweighs a modest proximity advantage"),
		AMSBotAIController::CalculateObjectiveScore(110000.0f, 2, false, false)
			> AMSBotAIController::CalculateObjectiveScore(122000.0f, 0, false, false));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
