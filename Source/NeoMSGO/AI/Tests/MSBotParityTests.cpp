#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotAIController.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AI/MSBotStateTreeNodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotMixedRoomDistributionTest,
	"NeoMSGO.AI.BotParity.MixedRoomDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotTacticalSpacingTest,
	"NeoMSGO.AI.BotParity.TacticalSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBotMixedRoomDistributionTest::RunTest(const FString&)
{
	const int32 Seed = 0x4D530001;
	const TArray<EMSEnemyTier> Roster = UMSBotSpawnManagerComponent::BuildMixedTierAssignments(25, Seed);
	TestEqual(TEXT("25-seat room has 25 assignments"), Roster.Num(), 25);
	TestEqual(TEXT("Easy is the majority (60%)"), Roster.FilterByPredicate(
		[](EMSEnemyTier Tier) { return Tier == EMSEnemyTier::Weak; }).Num(), 15);
	TestEqual(TEXT("Normal gets the 0.5 tie in largest-remainder rounding"), Roster.FilterByPredicate(
		[](EMSEnemyTier Tier) { return Tier == EMSEnemyTier::Medium; }).Num(), 8);
	TestEqual(TEXT("Hard remains a small minority"), Roster.FilterByPredicate(
		[](EMSEnemyTier Tier) { return Tier == EMSEnemyTier::Strong; }).Num(), 2);
	TestTrue(TEXT("same seed reproduces the room roster"),
		Roster == UMSBotSpawnManagerComponent::BuildMixedTierAssignments(25, Seed));
	TestTrue(TEXT("the other team receives an independently shuffled roster"),
		Roster != UMSBotSpawnManagerComponent::BuildMixedTierAssignments(25, Seed + 1));
	TestEqual(TEXT("one-seat Mixed room stays Easy"),
		UMSBotSpawnManagerComponent::BuildMixedTierAssignments(1, Seed)[0], EMSEnemyTier::Weak);
	TestTrue(TEXT("empty room is safe"),
		UMSBotSpawnManagerComponent::BuildMixedTierAssignments(0, Seed).IsEmpty());

	TArray<EMSEnemyTier> Existing = Roster;
	const EMSEnemyTier VacatedTier = Existing[7];
	Existing.RemoveAt(7);
	const TArray<EMSEnemyTier> Refill = UMSBotSpawnManagerComponent::BuildMissingMixedTierAssignments(
		25, Existing, Seed);
	TestEqual(TEXT("one vacated seat gets one refill"), Refill.Num(), 1);
	if (Refill.Num() == 1) TestEqual(TEXT("refill restores the missing tier"), Refill[0], VacatedTier);
	TestTrue(TEXT("full room does not spawn an extra BOT"),
		UMSBotSpawnManagerComponent::BuildMissingMixedTierAssignments(25, Roster, Seed).IsEmpty());
	TestEqual(TEXT("placement retry requests the entire 25-seat distribution"),
		UMSBotSpawnManagerComponent::BuildMissingMixedTierAssignments(25, TArray<EMSEnemyTier>(), Seed).Num(), 25);
	TestTrue(TEXT("a human taking a seat never causes an extra refill"),
		UMSBotSpawnManagerComponent::BuildMissingMixedTierAssignments(24, Roster, Seed).IsEmpty());
	return true;
}

bool FMSBotTacticalSpacingTest::RunTest(const FString&)
{
	const float Alone = AMSBotAIController::CalculateTargetPreferenceScore(3000.0f, 0, false, false);
	const float Crowded = AMSBotAIController::CalculateTargetPreferenceScore(2800.0f, 2, false, false);
	TestTrue(TEXT("nearby BOTs avoid dogpiling when another viable hostile is close"), Alone < Crowded);
	TestTrue(TEXT("current target gets a hysteresis bonus instead of oscillating each refresh"),
		AMSBotAIController::CalculateTargetPreferenceScore(3000.0f, 0, true, false) < Alone);
	TestTrue(TEXT("Taigan same-layer hostile wins over an otherwise equal cross-layer hostile"),
		Alone < AMSBotAIController::CalculateTargetPreferenceScore(3000.0f, 0, false, true));
	TestTrue(TEXT("three validated stalls favor a nearby alternate target"),
		AMSBotAIController::CalculateTargetPreferenceScore(3500.0f, 0, false, false)
			< AMSBotAIController::CalculateTargetPreferenceScore(3000.0f, 0, true, false, true));
	TestEqual(TEXT("ranged BOT with space keeps its current orbit"),
		FMSBotAttackTask::CalculateRangedOrbitRadius(1500.0f, 6000.0f, 317.0f), 1500.0f);
	TestTrue(TEXT("near-overlap ranged BOT backs out beyond two capsule radii"),
		FMSBotAttackTask::CalculateRangedOrbitRadius(400.0f, 6000.0f, 317.0f) > 634.0f);
	TestEqual(TEXT("orbit never pushes beyond preferred firing range"),
		FMSBotAttackTask::CalculateRangedOrbitRadius(9000.0f, 6000.0f, 317.0f), 6000.0f);
	return true;
}

#endif
