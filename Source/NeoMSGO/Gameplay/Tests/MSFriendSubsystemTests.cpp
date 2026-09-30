// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Gameplay/MSAuthSubsystem.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFriendSubsystemTest, "NeoMSGO.Gameplay.Friends.CRUDPersistenceAndSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFriendSessionMembershipTest, "NeoMSGO.Gameplay.Friends.SessionMembershipConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFriendSaveLoadFailureTest, "NeoMSGO.Gameplay.Friends.SaveLoadFailurePreservesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSFriendSubsystemTest::RunTest(const FString& Parameters)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_FriendAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Friends->SetSaveSlotForAutomationTests(Slot);
	Friends->ResetAndReloadForAutomationTests();

	TestEqual(TEXT("first launch starts with an empty friend list"), Friends->GetFriends().Num(), 0);
	TestTrue(TEXT("a friend can be added to the empty first launch"),
		Friends->AddFriend(TEXT("FirstLaunchPilot"), 2, 1200, EMSFriendState::Lobby));
	Friends->ResetAndReloadForAutomationTests();
	TestEqual(TEXT("explicitly added friend persists from an empty first launch"), Friends->GetFriends().Num(), 1);
	TestNotNull(TEXT("the explicitly added friend is restored"), Friends->FindFriend(TEXT("FirstLaunchPilot")));

	Friends->SeedDefaultFriendsForAutomationTests();
	TestTrue(TEXT("explicit test setup provides representative friends"), Friends->GetFriends().Num() >= 8);
	TSet<int32> Ranks;
	TSet<EMSFriendState> States;
	for (const FMSFriendEntry& Entry : Friends->GetFriends()) { Ranks.Add(Entry.Rank); States.Add(Entry.State); }
	TestTrue(TEXT("seed ranks are non-uniform"), Ranks.Num() > 1);
	TestTrue(TEXT("seed states are non-uniform"), States.Num() > 1);

	TestTrue(TEXT("new friend can be added"), Friends->AddFriend(TEXT("PersistencePilot"), 21, 1777, EMSFriendState::Ready));
	TestFalse(TEXT("duplicate name is rejected case-insensitively"), Friends->AddFriend(TEXT("persistencepilot")));
	TestTrue(TEXT("friend state can be changed"), Friends->SetFriendState(TEXT("PersistencePilot"), EMSFriendState::InBattle));
	TestEqual(TEXT("changed state is retained in memory"), Friends->FindFriend(TEXT("PersistencePilot"))->State, EMSFriendState::InBattle);
	TestTrue(TEXT("friend can be removed"), Friends->RemoveFriend(TEXT("PersistencePilot")));
	TestFalse(TEXT("removing an absent friend is rejected"), Friends->RemoveFriend(TEXT("PersistencePilot")));

	Friends->ResetAndReloadForAutomationTests();
	TestNull(TEXT("removed friend remains removed after reload"), Friends->FindFriend(TEXT("PersistencePilot")));
	TestTrue(TEXT("saved seed list remains available after reload"), Friends->GetFriends().Num() >= 8);
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

bool FMSFriendSessionMembershipTest::RunTest(const FString&)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_FriendSessionAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Friends->SetSaveSlotForAutomationTests(Slot);
	Friends->ResetAndReloadForAutomationTests();

	TestTrue(TEXT("session fixture adds a persisted member"),
		Friends->AddFriend(TEXT("SessionMember"), 3, 1400, EMSFriendState::Lobby));
	int32 ChangedBroadcasts = 0;
	const FDelegateHandle ChangedHandle = Friends->OnFriendsChanged.AddLambda([&ChangedBroadcasts] { ++ChangedBroadcasts; });
	TestTrue(TEXT("explicit squad confirmation succeeds"), Friends->SetFriendSquadMember(TEXT("SessionMember"), true));
	TestTrue(TEXT("explicit squad confirmation is visible this session"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("sessionmember")));
	const int32 BroadcastsAfterFirstConfirmation = ChangedBroadcasts;
	TestTrue(TEXT("reconfirming an already-member friend succeeds"),
		Friends->SetFriendSquadMember(TEXT("sessionmember"), true));
	TestTrue(TEXT("explicit reconfirmation refreshes listeners"), ChangedBroadcasts > BroadcastsAfterFirstConfirmation);
	Friends->OnFriendsChanged.Remove(ChangedHandle);

	Friends->ResetAndReloadForAutomationTests();
	const FMSFriendEntry* PersistedMember = Friends->FindFriend(TEXT("SessionMember"));
	TestNotNull(TEXT("saved membership survives a reload"), PersistedMember);
	TestTrue(TEXT("saved membership flag remains true after reload"), PersistedMember && PersistedMember->bIsSquadMember);
	TestFalse(TEXT("session confirmation is cleared by reload"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("SessionMember")));
	TestTrue(TEXT("the reloaded member can be explicitly confirmed again"),
		Friends->SetFriendSquadMember(TEXT("SessionMember"), true));
	TestTrue(TEXT("reconfirmed member is visible this session"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("SessionMember")));
	TestTrue(TEXT("explicit removal clears session confirmation"),
		Friends->SetFriendSquadMember(TEXT("SessionMember"), false));
	TestFalse(TEXT("removed member is no longer confirmed this session"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("SessionMember")));

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

bool FMSFriendSaveLoadFailureTest::RunTest(const FString&)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_FriendLoadFailureAutomation_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Friends->SetSaveSlotForAutomationTests(Slot);
	Friends->ResetAndReloadForAutomationTests();
	TestTrue(TEXT("failure fixture adds a persisted friend"),
		Friends->AddFriend(TEXT("LoadFailureMember"), 6, 1666, EMSFriendState::Lobby));
	TestTrue(TEXT("failure fixture confirms squad membership"),
		Friends->SetFriendSquadMember(TEXT("LoadFailureMember"), true));
	const TArray<FMSFriendEntry> FriendsBeforeFailedReload = Friends->GetFriends();
	TestTrue(TEXT("session confirmation exists before failed reload"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("LoadFailureMember")));

	UMSAuthSaveGame* WrongTypeSave = Cast<UMSAuthSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSAuthSaveGame::StaticClass()));
	TestNotNull(TEXT("wrong-class save fixture is created"), WrongTypeSave);
	if (!WrongTypeSave)
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	FMSAuthAccountRecord WrongTypeRecord;
	WrongTypeRecord.Username = TEXT("WrongTypePayload");
	WrongTypeSave->Accounts.Add(WrongTypeRecord);
	TestTrue(TEXT("wrong-class save overwrites the isolated slot"),
		UGameplayStatics::SaveGameToSlot(WrongTypeSave, Slot, 0));
	TArray<uint8> WrongTypeBytes;
	TestTrue(TEXT("wrong-class save bytes can be captured"), UGameplayStatics::LoadDataFromSlot(WrongTypeBytes, Slot, 0));

	// Cast failure must be observable while preserving both in-memory state and
	// session-local confirmation; the error expectation matches only this known log.
	AddExpectedError(TEXT("MSFriendSubsystem: Friend save exists but could not be loaded"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("wrong-class save reports a failed reload"), Friends->ReloadFriends());
	TestTrue(TEXT("failed reload preserves session confirmation"),
		Friends->IsSquadMembershipConfirmedThisSession(TEXT("LoadFailureMember")));
	const TArray<FMSFriendEntry>& FriendsAfterFailedReload = Friends->GetFriends();
	bool bFriendListUnchanged = FriendsAfterFailedReload.Num() == FriendsBeforeFailedReload.Num();
	for (int32 Index = 0; bFriendListUnchanged && Index < FriendsBeforeFailedReload.Num(); ++Index)
	{
		const FMSFriendEntry& Before = FriendsBeforeFailedReload[Index];
		const FMSFriendEntry& After = FriendsAfterFailedReload[Index];
		bFriendListUnchanged = Before.Name == After.Name && Before.Rank == After.Rank && Before.Rate == After.Rate
			&& Before.State == After.State && Before.bIsSquadMember == After.bIsSquadMember
			&& Before.bHasPendingSquadInvite == After.bHasPendingSquadInvite;
	}
	TestTrue(TEXT("failed reload preserves the in-memory friend list"), bFriendListUnchanged);

	TArray<uint8> BytesAfterFailedReload;
	TestTrue(TEXT("save bytes remain readable after failed reload"),
		UGameplayStatics::LoadDataFromSlot(BytesAfterFailedReload, Slot, 0));
	TestTrue(TEXT("failed reload does not overwrite the existing save bytes"), WrongTypeBytes == BytesAfterFailedReload);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#endif
