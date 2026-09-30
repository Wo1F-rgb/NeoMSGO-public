// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSGameInstance.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameInstanceRankTest, "NeoMSGO.Core.GameInstance.RankBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameInstanceCountersTest, "NeoMSGO.Core.GameInstance.PersistentCounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameInstanceMapSelectionTest, "NeoMSGO.Core.GameInstance.PersistentMapSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGameInstanceLoginGateTest, "NeoMSGO.Core.GameInstance.LoginGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSGameInstanceRankTest::RunTest(const FString&)
{
	struct FCase { int32 Kills; const TCHAR* Rank; };
	const FCase Cases[] = {
		{0, TEXT("二等兵")}, {2, TEXT("二等兵")}, {3, TEXT("一等兵")}, {5, TEXT("一等兵")},
		{6, TEXT("上等兵")}, {9, TEXT("上等兵")}, {10, TEXT("伍長")}, {14, TEXT("伍長")},
		{15, TEXT("軍曹")}, {19, TEXT("軍曹")}, {20, TEXT("曹長")}, {29, TEXT("曹長")},
		{30, TEXT("准尉")}, {39, TEXT("准尉")}, {40, TEXT("少尉")}, {41, TEXT("少尉")}
	};
	for (const FCase& TestCase : Cases)
	{
		TestEqual(FString::Printf(TEXT("rank at %d kills"), TestCase.Kills),
			UMSGameInstance::GetRankName(TestCase.Kills).ToString(), FString(TestCase.Rank));
	}
	return true;
}

bool FMSGameInstanceCountersTest::RunTest(const FString&)
{
	UMSGameInstance* Instance = NewObject<UMSGameInstance>();
	Instance->RegisterKill(); Instance->RegisterKill(); Instance->RegisterDeath();
	TestEqual(TEXT("kills remain on the GameInstance object"), Instance->KillCount, 2);
	TestEqual(TEXT("deaths remain on the GameInstance object"), Instance->DeathCount, 1);
	Instance->SquadName = TEXT("Persistent Squad"); Instance->SquadEmblemIndex = 5;
	TestEqual(TEXT("squad name is process-lifetime state"), Instance->SquadName, FString(TEXT("Persistent Squad")));
	TestEqual(TEXT("emblem is process-lifetime state"), Instance->SquadEmblemIndex, 5);
	return true;
}

bool FMSGameInstanceMapSelectionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>());
	TestEqual(TEXT("default selected map"), Instance->SelectedMapId, FName(TEXT("UnrealizerColony")));
	TestFalse(TEXT("sortie is not pending by default"), Instance->bPendingSortie);
	TestEqual(TEXT("default pending rule"), Instance->PendingRuleIndex, 0);
	TestFalse(TEXT("room host is not pending by default"), Instance->bPendingRoomHost);
	TestFalse(TEXT("room join is not pending by default"), Instance->bPendingRoomJoin);

	Instance->SelectedMapId = TEXT("NewYark");
	Instance->bPendingSortie = true;
	Instance->PendingRuleIndex = 2;
	TestEqual(TEXT("selected map remains on the live GameInstance"), Instance->SelectedMapId, FName(TEXT("NewYark")));
	TestTrue(TEXT("pending sortie remains on the live GameInstance"), Instance->bPendingSortie);
	TestEqual(TEXT("pending rule remains on the live GameInstance"), Instance->PendingRuleIndex, 2);
	return true;
}

bool FMSGameInstanceLoginGateTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>());
	TestFalse(TEXT("login state defaults false"), Instance->IsLoggedIn());
	TestFalse(TEXT("lobby gate defaults closed"), Instance->CanEnterLobby());
	Instance->SetAuthenticatedUser(TEXT("  AuthenticatedPilot  "));
	TestTrue(TEXT("successful auth opens lobby gate"), Instance->CanEnterLobby());
	TestEqual(TEXT("username is trimmed"), Instance->LoggedInUsername, FString(TEXT("AuthenticatedPilot")));
	Instance->Logout();
	TestFalse(TEXT("logout closes lobby gate"), Instance->CanEnterLobby());
	return true;
}

#endif
