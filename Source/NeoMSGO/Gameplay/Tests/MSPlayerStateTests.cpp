// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MSTeamTypes.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSPlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSPlayerStateDefaultsTest,
	"NeoMSGO.Gameplay.Multiplayer.PlayerStateDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSPlayerStateDefaultsTest::RunTest(const FString&)
{
	const AMSPlayerState* PlayerState = GetDefault<AMSPlayerState>();
	const AMSGameMode* GameMode = GetDefault<AMSGameMode>();
	TestEqual(TEXT("selected faction defaults to TeamPlayer"),
		PlayerState->GetSelectedFaction().GetId(), MSTeam::TeamPlayer);
	TestTrue(TEXT("AMSGameMode registers AMSPlayerState"),
		GameMode->PlayerStateClass.Get() == AMSPlayerState::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSPresenceNameArbitrationTest,
	"NeoMSGO.Gameplay.Multiplayer.PresenceNameArbitration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSPresenceNameArbitrationTest::RunTest(const FString&)
{
	const FString Guest1 = FString::Printf(TEXT("%s1"), AMSPlayerState::GetGuestNamePrefix());
	const FString Guest2 = FString::Printf(TEXT("%s2"), AMSPlayerState::GetGuestNamePrefix());
	const FString Guest3 = FString::Printf(TEXT("%s3"), AMSPlayerState::GetGuestNamePrefix());

	TestEqual(TEXT("an unused name is kept as submitted"),
		AMSPlayerState::ResolveUniqueLoginUsername(Guest1, {}), Guest1);
	TestEqual(TEXT("submitted names are trimmed"),
		AMSPlayerState::ResolveUniqueLoginUsername(TEXT("  HostPilot  "), {}), FString(TEXT("HostPilot")));
	TestTrue(TEXT("a blank submission resolves to nothing"),
		AMSPlayerState::ResolveUniqueLoginUsername(TEXT("   "), {}).IsEmpty());

	// #214: the second window's process-local "ゲスト1" must become a different guest number.
	TestEqual(TEXT("a duplicated guest takes the lowest free guest number"),
		AMSPlayerState::ResolveUniqueLoginUsername(Guest1, {Guest1}), Guest2);
	TestEqual(TEXT("guest numbering skips every number already connected"),
		AMSPlayerState::ResolveUniqueLoginUsername(Guest1, {Guest1, Guest2}), Guest3);
	TestEqual(TEXT("a freed guest number is reused"),
		AMSPlayerState::ResolveUniqueLoginUsername(Guest3, {Guest2, Guest3}), Guest1);
	TestEqual(TEXT("duplicate detection ignores case"),
		AMSPlayerState::ResolveUniqueLoginUsername(TEXT("hostpilot"), {TEXT("HostPilot")}),
		FString(TEXT("hostpilot(2)")));
	TestEqual(TEXT("non-guest duplicates keep counting up"),
		AMSPlayerState::ResolveUniqueLoginUsername(TEXT("HostPilot"),
			{TEXT("HostPilot"), TEXT("HostPilot(2)")}), FString(TEXT("HostPilot(3)")));
	TestEqual(TEXT("a guest-like name with no number is treated as an ordinary name"),
		AMSPlayerState::ResolveUniqueLoginUsername(AMSPlayerState::GetGuestNamePrefix(),
			{AMSPlayerState::GetGuestNamePrefix()}),
		FString::Printf(TEXT("%s(2)"), AMSPlayerState::GetGuestNamePrefix()));
	return true;
}

#endif
