// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCapturePointP71Test, "NeoMSGO.Gameplay.CapturePoint.P7_1",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCapturePointMapRelevancyTest,
	"NeoMSGO.Gameplay.CapturePoint.MapReplicationRelevancy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSCapturePointTestP71
{
	struct FScopedCaptureWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSCapturePointTestP71World"));
		FScopedCaptureWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedCaptureWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	int32 AdvanceUntilOwner(AMSCapturePoint* Point, const int32 Allies, const int32 Enemies,
		const EMSCaptureOwner WantedOwner, const float StepSeconds, const int32 Limit,
		int32* OwnerChangeCount = nullptr)
	{
		int32 Steps = 0;
		EMSCaptureOwner PreviousOwner = Point->GetCurrentOwner();
		while (Point->GetCurrentOwner() != WantedOwner && Steps < Limit)
		{
			Point->TickCaptureLogic(Allies, Enemies, StepSeconds);
			++Steps;
			if (Point->GetCurrentOwner() != PreviousOwner)
			{
				if (OwnerChangeCount) ++*OwnerChangeCount;
				PreviousOwner = Point->GetCurrentOwner();
			}
		}
		return Steps;
	}
}

bool FMSCapturePointMapRelevancyTest::RunTest(const FString&)
{
	AMSCapturePoint* CapturePoint = NewObject<AMSCapturePoint>(GetTransientPackage());
	TestNotNull(TEXT("Capture point test object exists"), CapturePoint);
	if (!CapturePoint) return false;

	TestTrue(TEXT("Capture points replicate"), CapturePoint->GetIsReplicated());
	TestTrue(TEXT("Capture points are always relevant for map metadata"), CapturePoint->bAlwaysRelevant);
	TestTrue(TEXT("A distant viewer still receives capture-point state"),
		CapturePoint->IsNetRelevantFor(nullptr, nullptr, FVector(1000000.0f, 1000000.0f, 0.0f)));

	const AMSHomeBase* HomeBaseDefaults = AMSHomeBase::StaticClass()->GetDefaultObject<AMSHomeBase>();
	TestNotNull(TEXT("Home-base class defaults exist"), HomeBaseDefaults);
	if (HomeBaseDefaults)
	{
		TestTrue(TEXT("Home bases inherit map-wide capture-point relevancy"), HomeBaseDefaults->bAlwaysRelevant);
		TestTrue(TEXT("A distant viewer still receives home-base metadata"),
			HomeBaseDefaults->IsNetRelevantFor(nullptr, nullptr, FVector(-1000000.0f, -1000000.0f, 0.0f)));
	}
	return true;
}

bool FMSCapturePointP71Test::RunTest(const FString&)
{
	TestEqual(TEXT("Multiplier 1"), AMSCapturePoint::CalculateCaptureSpeedMultiplier(1), 1.0f);
	TestEqual(TEXT("Multiplier 2"), AMSCapturePoint::CalculateCaptureSpeedMultiplier(2), 1.5f);
	TestEqual(TEXT("Multiplier 3"), AMSCapturePoint::CalculateCaptureSpeedMultiplier(3), 2.0f);
	TestEqual(TEXT("Multiplier 5 capped"), AMSCapturePoint::CalculateCaptureSpeedMultiplier(5), 2.0f);

	AMSCapturePoint* Point = NewObject<AMSCapturePoint>(GetTransientPackage());
	TestTrue(TEXT("CaptureRadius scales from 800 UU through MSScale"),
		FMath::IsNearlyEqual(Point->GetCaptureRadius(), 800.0f * MSGO::MSScale));
	const float Step = 0.01f;
	const int32 FirstCaptureSteps = MSCapturePointTestP71::AdvanceUntilOwner(Point, 1, 0,
		EMSCaptureOwner::TeamPlayer, Step, 1000);
	TestTrue(TEXT("One capturer reaches +1 in about 6.67 seconds"),
		FMath::Abs(FirstCaptureSteps * Step - (1.0f / 0.15f)) <= Step * 2.0f);
	TestEqual(TEXT("First capture progress clamps to +1"), Point->GetProgress(), 1.0f);

	Point->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
	int32 OwnerChanges = 0;
	const int32 RetakeSteps = MSCapturePointTestP71::AdvanceUntilOwner(Point, 1, 0,
		EMSCaptureOwner::TeamPlayer, Step, 2000, &OwnerChanges);
	TestTrue(TEXT("Enemy-owned retake takes twice first capture"),
		FMath::Abs(RetakeSteps - FirstCaptureSteps * 2) <= 2);
	TestEqual(TEXT("Retake neutralizes then captures (two owner transitions)"), OwnerChanges, 2);

	Point->SetFixedOwner(EMSCaptureOwner::TeamPlayer);
	while (Point->GetProgress() > 0.4f + KINDA_SMALL_NUMBER) Point->TickCaptureLogic(0, 1, Step);
	const EMSCaptureOwner OwnerBeforeRecovery = Point->GetCurrentOwner();
	for (int32 Index = 0; Index < 2500; ++Index) Point->TickCaptureLogic(0, 0, Step);
	TestEqual(TEXT("Owned point was Player before recovery"), OwnerBeforeRecovery, EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Owned point remains Player while unattended"), Point->GetCurrentOwner(), EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Owned partial progress recovers to +1"), Point->GetProgress(), 1.0f);

	Point->SetFixedOwner(EMSCaptureOwner::Neutral);
	for (int32 Index = 0; Index < 267; ++Index) Point->TickCaptureLogic(1, 0, Step);
	TestTrue(TEXT("Neutral partial capture prepared near +0.4"), FMath::IsNearlyEqual(Point->GetProgress(), 0.4f, 0.01f));
	for (int32 Index = 0; Index < 1500; ++Index) Point->TickCaptureLogic(0, 0, Step);
	TestTrue(TEXT("Neutral partial progress decays to zero"), FMath::IsNearlyZero(Point->GetProgress()));
	TestEqual(TEXT("Neutral unattended point stays neutral"), Point->GetCurrentOwner(), EMSCaptureOwner::Neutral);

	Point->TickCaptureLogic(1, 0, 1.0f);
	const float ContestedStart = Point->GetProgress();
	Point->TickCaptureLogic(3, 2, 1.0f);
	TestEqual(TEXT("Contested point does not move"), Point->GetProgress(), ContestedStart);

	MSCapturePointTestP71::FScopedCaptureWorld Scope;
	UMSUnitRegistrySubsystem* Registry = Scope.World->GetSubsystem<UMSUnitRegistrySubsystem>();
	AMSBotCharacter* InRange = Scope.World->SpawnActor<AMSBotCharacter>(FVector(700.0f * MSGO::MSScale, 0.0f, 5000.0f), FRotator::ZeroRotator);
	AMSBotCharacter* OutOfRange = Scope.World->SpawnActor<AMSBotCharacter>(FVector(801.0f * MSGO::MSScale, 0.0f, 0.0f), FRotator::ZeroRotator);
	AMSBotCharacter* DeadInRange = Scope.World->SpawnActor<AMSBotCharacter>(FVector(100.0f * MSGO::MSScale, 0.0f, 0.0f), FRotator::ZeroRotator);
	InRange->SetGenericTeamId(MSTeam::PlayerTeamId());
	OutOfRange->SetGenericTeamId(MSTeam::PlayerTeamId());
	DeadInRange->SetGenericTeamId(MSTeam::PlayerTeamId());
	DeadInRange->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TestEqual(TEXT("Range count uses ground plane and excludes outside/dead units"),
		Registry->CountUnitsOfTeamInRange(MSTeam::PlayerTeamId(), FVector::ZeroVector, 800.0f * MSGO::MSScale), 1);
	TestEqual(TEXT("Range count can include dead units"),
		Registry->CountUnitsOfTeamInRange(MSTeam::PlayerTeamId(), FVector::ZeroVector, 800.0f * MSGO::MSScale, false), 2);

	AMSCapturePoint* WorldPoint = Scope.World->SpawnActor<AMSCapturePoint>();
	TestNotNull(TEXT("Capture point owns a range-ring visual"), WorldPoint->GetRangeRing());
	if (WorldPoint->GetRangeRing())
	{
		const float DisplayedRadius = WorldPoint->GetRangeRing()->GetRelativeScale3D().X * 50.0f;
		TestTrue(TEXT("RangeRing radius exactly follows gameplay CaptureRadius"),
			FMath::IsNearlyEqual(DisplayedRadius, WorldPoint->GetCaptureRadius(), 0.1f));
		TestFalse(TEXT("Legacy solid disk must not obscure map terrain"),WorldPoint->GetRangeRing()->IsVisible());
	}
	const UInstancedStaticMeshComponent* Outline=WorldPoint->FindComponentByClass<UInstancedStaticMeshComponent>();
	TestNotNull(TEXT("Point has a perimeter outline"),Outline);
	if(Outline)
	{
		TestTrue(TEXT("Perimeter renders without a central disk"),Outline->GetInstanceCount()>32);
		TestEqual(TEXT("Perimeter does not block players or navigation"),Outline->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
		FTransform First;Outline->GetInstanceTransform(0,First,false);
		TestTrue(TEXT("Outline lies on gameplay boundary, not at its center"),First.GetLocation().Size2D()>WorldPoint->GetCaptureRadius()*.98f);
	}
	AMSHomeBase* Home=Scope.World->SpawnActor<AMSHomeBase>();
	if(const UInstancedStaticMeshComponent* HomeOutline=Home->FindComponentByClass<UInstancedStaticMeshComponent>())
		TestFalse(TEXT("Home base does not show a capturable point outline"),HomeOutline->IsVisible());
	WorldPoint->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
	TestFalse(TEXT("Capture point never implements lock-on target policy"), UMSTeamStatics::CanLockOn(InRange, WorldPoint));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
