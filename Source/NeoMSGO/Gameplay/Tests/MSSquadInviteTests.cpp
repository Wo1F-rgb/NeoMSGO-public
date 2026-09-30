// Copyright NeoMSGO Project. All Rights Reserved.
//
// タスク#144(小隊招待の承認→自動入室)とタスク#145(ダミー名「ガンオンN」統一)の回帰テスト。

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSDummyNameLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

namespace MSSquadInviteTests
{
	struct FScopedFriends
	{
		FString Slot;
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<UMSFriendSubsystem> Friends;

		FScopedFriends()
		{
			Slot = FString::Printf(TEXT("NeoMSGO_SquadInviteAutomation_%s"), *FGuid::NewGuid().ToString());
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
			Instance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
			Friends = TStrongObjectPtr<UMSFriendSubsystem>(NewObject<UMSFriendSubsystem>(Instance.Get()));
			Friends->SetSaveSlotForAutomationTests(Slot);
			Friends->ResetAndReloadForAutomationTests();
			Friends->SeedDefaultFriendsForAutomationTests();
		}

		~FScopedFriends() { UGameplayStatics::DeleteGameInSlot(Slot, 0); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSquadInviteAcceptFlowTest, "NeoMSGO.Gameplay.Squad.InviteAcceptAutoJoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSSquadInviteAcceptFlowTest::RunTest(const FString&)
{
	MSSquadInviteTests::FScopedFriends Scope;
	UMSFriendSubsystem* Friends = Scope.Friends.Get();

	// 送信: 相手は即入室せず承認待ちになる
	const FString Target = MSDummyNames::MakeDummyName(3); // シード: オンライン(InBattle)・非小隊
	TestTrue(TEXT("invite to a non-member succeeds"), Friends->SendSquadInvite(Target));
	TestTrue(TEXT("invite is pending after send"), Friends->HasPendingSquadInvite(Target));
	TestFalse(TEXT("target has not joined yet"), Friends->FindFriend(Target)->bIsSquadMember);
	TestFalse(TEXT("duplicate invite while pending is rejected"), Friends->SendSquadInvite(Target));
	TestEqual(TEXT("online invitee schedules a simulated acceptance"),
		Friends->GetScheduledAutoAcceptCountForAutomationTests(), 1);

	// 承認: 自動入室が両デリゲートで通知される
	int32 ChangedBroadcasts = 0;
	FString AcceptedName;
	FDelegateHandle ChangedHandle = Friends->OnFriendsChanged.AddLambda([&ChangedBroadcasts] { ++ChangedBroadcasts; });
	FDelegateHandle AcceptedHandle = Friends->OnSquadInviteAccepted.AddLambda(
		[&AcceptedName](const FString& Name) { AcceptedName = Name; });
	TestTrue(TEXT("accepting the pending invite succeeds"), Friends->AcceptSquadInvite(Target));
	TestTrue(TEXT("acceptance auto-joins the squad"), Friends->FindFriend(Target)->bIsSquadMember);
	TestFalse(TEXT("pending flag clears on acceptance"), Friends->HasPendingSquadInvite(Target));
	TestTrue(TEXT("friends-changed broadcast fires so squad UIs refresh"), ChangedBroadcasts > 0);
	TestEqual(TEXT("acceptance delegate reports the joined member"), AcceptedName, Target);
	TestTrue(TEXT("simulated acceptance confirms squad membership this session"),
		Friends->IsSquadMembershipConfirmedThisSession(Target));
	TestFalse(TEXT("accepting twice fails"), Friends->AcceptSquadInvite(Target));
	Friends->OnFriendsChanged.Remove(ChangedHandle);
	Friends->OnSquadInviteAccepted.Remove(AcceptedHandle);

	// 辞退: 承認待ちのみ解除され入室しない
	const FString Decliner = MSDummyNames::MakeDummyName(4);
	TestTrue(TEXT("second invite succeeds"), Friends->SendSquadInvite(Decliner));
	TestTrue(TEXT("declining a pending invite succeeds"), Friends->DeclineSquadInvite(Decliner));
	TestFalse(TEXT("declined friend is not in the squad"), Friends->FindFriend(Decliner)->bIsSquadMember);
	TestFalse(TEXT("declined friend is no longer pending"), Friends->HasPendingSquadInvite(Decliner));

	// オフラインの相手には承認シミュレーションを予約しない(承認待ちのまま)
	const FString OfflineTarget = MSDummyNames::MakeDummyName(7); // シード: Offline
	const int32 ScheduledBefore = Friends->GetScheduledAutoAcceptCountForAutomationTests();
	TestTrue(TEXT("offline friend can still be invited"), Friends->SendSquadInvite(OfflineTarget));
	TestEqual(TEXT("offline invitee does not auto-accept"),
		Friends->GetScheduledAutoAcceptCountForAutomationTests(), ScheduledBefore);
	TestTrue(TEXT("offline invite stays pending"), Friends->HasPendingSquadInvite(OfflineTarget));
	Friends->DeclineSquadInvite(OfflineTarget);

	// 容量: 自分1枠+5人で満員。6人目への招待/承認は失敗する
	for (const FMSFriendEntry& Entry : TArray<FMSFriendEntry>(Friends->GetFriends()))
	{
		if (Friends->GetSquadMemberCount() >= UMSFriendSubsystem::MaxInvitedSquadMembers) break;
		Friends->SetFriendSquadMember(Entry.Name, true);
	}
	FString Sixth;
	for (const FMSFriendEntry& Entry : Friends->GetFriends())
		if (!Entry.bIsSquadMember) { Sixth = Entry.Name; break; }
	TestFalse(TEXT("invite is rejected when the squad is full"), Friends->SendSquadInvite(Sixth));

	// 永続化: 承認待ちフラグはセーブ/ロードを跨いで保持される
	const FString PersistTarget = Sixth;
	Friends->SetFriendSquadMember(MSDummyNames::MakeDummyName(1), false); // 1枠空ける
	TestTrue(TEXT("invite succeeds after a slot opens"), Friends->SendSquadInvite(PersistTarget));
	Friends->ResetAndReloadForAutomationTests();
	TestTrue(TEXT("pending invite persists across save reload"), Friends->HasPendingSquadInvite(PersistTarget));
	TestTrue(TEXT("acceptance after reload auto-joins"), Friends->AcceptSquadInvite(PersistTarget));
	TestTrue(TEXT("membership persists in the reloaded data"), Friends->FindFriend(PersistTarget)->bIsSquadMember);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDummyNameUnificationTest, "NeoMSGO.Gameplay.DummyNames.GanOnNumbering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSDummyNameUnificationTest::RunTest(const FString&)
{
	// 生成関数は「ガンオンN」を返す(単一生成点 — タスク#145)
	TestEqual(TEXT("dummy name format is GanOn+number"), MSDummyNames::MakeDummyName(3), FString(TEXT("ガンオン3")));
	TestEqual(TEXT("index clamps to 1"), MSDummyNames::MakeDummyName(0), FString(TEXT("ガンオン1")));
	TestTrue(TEXT("generated names are recognized as dummy names"),
		MSDummyNames::IsDummyName(MSDummyNames::MakeDummyName(42)));
	TestFalse(TEXT("real-looking account names are not dummy names"), MSDummyNames::IsDummyName(TEXT("RealPlayer01")));
	TestFalse(TEXT("prefix alone is not a dummy name"), MSDummyNames::IsDummyName(TEXT("ガンオン")));

	// シードされる架空フレンドは全員「ガンオンN」で統一される
	MSSquadInviteTests::FScopedFriends Scope;
	const TArray<FMSFriendEntry>& Seeded = Scope.Friends->GetFriends();
	TestTrue(TEXT("default friends are seeded"), Seeded.Num() > 0);
	for (const FMSFriendEntry& Entry : Seeded)
	{
		TestTrue(FString::Printf(TEXT("seeded friend '%s' uses the unified dummy name"), *Entry.Name),
			MSDummyNames::IsDummyName(Entry.Name));
	}

	// 実データ(ユーザー追加のフレンド)は改名されない
	Scope.Friends->AddFriend(TEXT("RealPlayer01"), 3, 1450, EMSFriendState::Lobby);
	Scope.Friends->ResetAndReloadForAutomationTests();
	TestNotNull(TEXT("user-added friend keeps its real name after reload"),
		Scope.Friends->FindFriend(TEXT("RealPlayer01")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
