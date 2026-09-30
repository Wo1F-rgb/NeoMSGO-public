// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSGameInstance.h"
#include "JsonObjectConverter.h"
#include "Online/MSRoomDescriptor.h"
#include "Online/MSRoomRegistryServer.h"
#include "Online/MSRoomSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomRegistryTtlTest,
	"NeoMSGO.Online.RoomRegistry.TtlExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomRegistryClientLifecycleTest,
	"NeoMSGO.Online.RoomRegistry.HostLifecycleRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSRoomRegistryTtlTest::RunTest(const FString&)
{
	FMSRoomRegistryStore Store;
	FMSRoomDescriptor Descriptor;
	Descriptor.Settings.RoomName = TEXT("TTL Room");
	Descriptor.Address = TEXT("127.0.0.1:7777");

	constexpr double RegisteredAt = 1000.0;
	const FGuid RoomId = Store.RegisterRoom(Descriptor, RegisteredAt);
	TestTrue(TEXT("server generates an ID for an empty RoomId"), RoomId.IsValid());
	TestEqual(TEXT("registered room is listed"), Store.GetRooms().Num(), 1);
	TestEqual(TEXT("nothing expires before 15 seconds"), Store.SweepExpired(RegisteredAt + 14.999), 0);
	TestEqual(TEXT("room remains listed before the TTL"), Store.GetRooms().Num(), 1);
	TestEqual(TEXT("room expires at 15 seconds"), Store.SweepExpired(RegisteredAt + 15.0), 1);
	TestEqual(TEXT("expired room is removed from the list"), Store.GetRooms().Num(), 0);

	Descriptor.RoomId = FGuid::NewGuid();
	Store.RegisterRoom(Descriptor, RegisteredAt);
	TestTrue(TEXT("heartbeat refreshes a registered room"),
		Store.Heartbeat(Descriptor.RoomId, RegisteredAt + 10.0));
	TestEqual(TEXT("refreshed room survives relative to registration"),
		Store.SweepExpired(RegisteredAt + 20.0), 0);
	TestEqual(TEXT("refreshed room expires relative to heartbeat"),
		Store.SweepExpired(RegisteredAt + 25.0), 1);
	return true;
}

bool FMSRoomRegistryClientLifecycleTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> GameInstance(
		NewObject<UMSGameInstance>(GetTransientPackage()));
	TStrongObjectPtr<UMSRoomSubsystem> Subsystem(
		NewObject<UMSRoomSubsystem>(GameInstance.Get()));
	TArray<FString> Paths;
	TArray<FString> Bodies;
	Subsystem->SetRegistryRequestSenderForAutomationTests(
		[&Paths, &Bodies](const FString& Path, const FString& Body)
		{
			Paths.Add(Path);
			Bodies.Add(Body);
			// A failed registry is explicitly non-fatal to the host lifecycle.
			return false;
		});

	FMSRoomDescriptor Descriptor;
	Descriptor.RoomId = FGuid::NewGuid();
	Descriptor.Settings.RoomName = TEXT("Lifecycle Room");
	Descriptor.Settings.MapId = TEXT("NewYark");
	Descriptor.HostDisplayName = TEXT("Guest");
	Descriptor.Address = TEXT("127.0.0.1:7777");
	Subsystem->BeginRegistryHostForAutomationTests(Descriptor);
	TestEqual(TEXT("host success posts one registration request"), Paths.Num(), 1);
	TestEqual(TEXT("registration endpoint"), Paths[0], FString(TEXT("/rooms/register")));
	FMSRoomDescriptor RegisteredDescriptor;
	TestTrue(TEXT("registration body is a room descriptor"),
		FJsonObjectConverter::JsonObjectStringToUStruct(Bodies[0], &RegisteredDescriptor));
	TestEqual(TEXT("registration carries the generated room ID"),
		RegisteredDescriptor.RoomId, Descriptor.RoomId);
	TestFalse(TEXT("registration carries no password"), Bodies[0].Contains(TEXT("Password")));
	TestFalse(TEXT("registration carries no authenticated account record"),
		Bodies[0].Contains(TEXT("LoggedInUsername")));

	Subsystem->SendHeartbeatForAutomationTests();
	TestEqual(TEXT("heartbeat is still attempted after registration transport failure"), Paths.Num(), 2);
	TestEqual(TEXT("heartbeat endpoint"), Paths[1], FString(TEXT("/rooms/heartbeat")));
	TestTrue(TEXT("heartbeat references the hosted room"),
		Bodies[1].Contains(Descriptor.RoomId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestEqual(TEXT("heartbeat timer contract is five seconds"),
		Subsystem->GetHeartbeatIntervalForAutomationTests(), 5.0f);

	Subsystem->EndRegistryHostForAutomationTests();
	TestEqual(TEXT("ending the host posts unregister"), Paths.Num(), 3);
	TestEqual(TEXT("unregister endpoint"), Paths[2], FString(TEXT("/rooms/unregister")));
	TestTrue(TEXT("unregister references the hosted room"),
		Bodies[2].Contains(Descriptor.RoomId.ToString(EGuidFormats::DigitsWithHyphens)));

	Subsystem->SendHeartbeatForAutomationTests();
	TestEqual(TEXT("no heartbeat is sent after unregister"), Paths.Num(), 3);
	return true;
}

#endif
