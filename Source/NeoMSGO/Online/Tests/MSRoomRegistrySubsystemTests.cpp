// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Online/MSRoomRegistrySubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomRegistryStopPollingTest,
	"NeoMSGO.Online.RoomRegistry.StopPollingStopsTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSRoomRegistrySubsystemTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, false, TEXT("MSRoomRegistrySubsystemTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);

		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSRoomRegistryStopPollingTest::RunTest(const FString&)
{
	MSRoomRegistrySubsystemTests::FScopedWorld Scope;
	UMSRoomRegistrySubsystem* Registry =
		Scope.GameInstance->GetSubsystem<UMSRoomRegistrySubsystem>();
	TestNotNull(TEXT("room registry subsystem exists"), Registry);
	if (!Registry) return false;

	int32 Requests = 0;
	Registry->SetRequestSenderForAutomationTests(
		[&Requests](const FString& Url)
		{
			++Requests;
			return Url == TEXT("http://127.0.0.1:7778/rooms");
		});
	Registry->StartPolling(TEXT("127.0.0.1:7778/"));
	TestEqual(TEXT("opening polling performs the initial GET"), Requests, 1);
	TestTrue(TEXT("three-second polling timer is active"),
		Registry->IsPollingTimerActiveForAutomationTests());

	Registry->FirePollingTimerForAutomationTests();
	TestEqual(TEXT("the timer fires one additional GET after three seconds"), Requests, 2);

	TArray<FMSRoomDescriptor> CachedRooms;
	CachedRooms.AddDefaulted_GetRef().Settings.RoomName = TEXT("Cached room");
	Registry->SetLastKnownRoomsForAutomationTests(CachedRooms);
	Registry->SetRequestSenderForAutomationTests(
		[&Requests](const FString&)
		{
			++Requests;
			return false;
		});
	TestTrue(TEXT("manual refresh is accepted while polling"), Registry->RefreshNow());
	TestEqual(TEXT("failed request preserves the last-known list"),
		Registry->GetLastKnownRooms().Num(), 1);

	Registry->StopPolling();
	const int32 RequestsAtStop = Requests;
	TestFalse(TEXT("StopPolling disables the polling state"), Registry->IsPolling());
	TestFalse(TEXT("StopPolling clears the world timer"),
		Registry->IsPollingTimerActiveForAutomationTests());
	Registry->FirePollingTimerForAutomationTests();
	TestEqual(TEXT("no timer-fired HTTP request occurs after StopPolling"),
		Requests, RequestsAtStop);
	return true;
}

#endif
