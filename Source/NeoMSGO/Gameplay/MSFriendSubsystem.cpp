// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSFriendSubsystem.h"

#include "Core/MSDummyNameLibrary.h"
#include "Core/MSGameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace MSFriendSubsystemPrivate
{
	FString NormalizeFriendKey(const FString& Name)
	{
		FString Key = Name;
		Key.TrimStartAndEndInline();
		Key.ToLowerInline();
		return Key;
	}
}

void UMSFriendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadFriends();
}

void UMSFriendSubsystem::Deinitialize()
{
	ClearScheduledAutoAccepts();
	Super::Deinitialize();
}

bool UMSFriendSubsystem::ReloadFriends()
{
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		USaveGame* LoadedSave = UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex);
		if (!LoadedSave)
		{
			UE_LOG(LogTemp, Error,
				TEXT("MSFriendSubsystem: Friend save exists but could not be loaded for slot '%s'"), *SaveSlotName);
			return false;
		}

		const UMSFriendSaveGame* Save = Cast<UMSFriendSaveGame>(LoadedSave);
		if (!Save)
		{
			UE_LOG(LogTemp, Error,
				TEXT("MSFriendSubsystem: Friend save exists but could not be loaded for slot '%s'"), *SaveSlotName);
			return false;
		}

		// Confirmation is session-local evidence. Reset it only after a valid save has
		// been read; a failed load must leave both memory and session evidence untouched.
		ConfirmedLocalSquadMembers.Reset();
		Friends = Save->Friends;
		// 旧シードのダミー名を「ガンオンN」へ移行する(実データ=ユーザー追加分は対象外 — タスク#145)
		static const TCHAR* LegacyDummyNames[] = {
			TEXT("Aster213"), TEXT("Kuderia"), TEXT("Mikazuki"), TEXT("Sayla"), TEXT("Bright"),
			TEXT("Kamille"), TEXT("Lalah"), TEXT("Bernie"), TEXT("Christina"), TEXT("Kitano"), TEXT("Satou") };
		bool bMigrated = false;
		for (FMSFriendEntry& Entry : Friends)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(LegacyDummyNames); ++Index)
			{
				if (Entry.Name.Equals(LegacyDummyNames[Index], ESearchCase::IgnoreCase))
				{
					Entry.Name = MSDummyNames::MakeDummyName(Index + 1);
					bMigrated = true;
					break;
				}
			}
		}
		if (bMigrated) SaveFriends();
		OnFriendsChanged.Broadcast();
		return true;
	}

	// New saves intentionally start empty. Test fixtures that need representative
	// dummy entries call SeedDefaultFriendsForAutomationTests() explicitly.
	ConfirmedLocalSquadMembers.Reset();
	Friends.Reset();
	SaveFriends();
	OnFriendsChanged.Broadcast();
	return false;
}

bool UMSFriendSubsystem::SaveFriends() const
{
	UMSFriendSaveGame* Save = Cast<UMSFriendSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSFriendSaveGame::StaticClass()));
	if (!Save) return false;
	Save->Friends = Friends;
	return UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, SaveUserIndex);
}

bool UMSFriendSubsystem::AddFriend(const FString& Name, const int32 Rank, const int32 Rate,
	const EMSFriendState State)
{
	FString CleanName = Name;
	CleanName.TrimStartAndEndInline();
	if (CleanName.IsEmpty() || FindFriend(CleanName)) return false;
	Friends.Emplace(CleanName, FMath::Max(1, Rank), FMath::Max(0, Rate), State);
	SaveFriends();
	OnFriendsChanged.Broadcast();
	return true;
}

bool UMSFriendSubsystem::RemoveFriend(const FString& Name)
{
	const int32 Removed = Friends.RemoveAll([&Name](const FMSFriendEntry& Entry)
	{
		return Entry.Name.Equals(Name, ESearchCase::IgnoreCase);
	});
	if (Removed == 0) return false;
	ConfirmedLocalSquadMembers.Remove(MSFriendSubsystemPrivate::NormalizeFriendKey(Name));
	SaveFriends();
	OnFriendsChanged.Broadcast();
	return true;
}

bool UMSFriendSubsystem::SetFriendState(const FString& Name, const EMSFriendState State)
{
	FMSFriendEntry* Entry = FindMutableFriend(Name);
	if (!Entry || Entry->State == State) return Entry != nullptr;
	Entry->State = State;
	SaveFriends();
	OnFriendsChanged.Broadcast();
	return true;
}

bool UMSFriendSubsystem::SetFriendSquadMember(const FString& Name, const bool bIsSquadMember)
{
	FMSFriendEntry* Entry = FindMutableFriend(Name);
	if (!Entry) return false;
	const FString Key = MSFriendSubsystemPrivate::NormalizeFriendKey(Entry->Name);
	if (bIsSquadMember)
	{
		const bool bMembershipChanged = !Entry->bIsSquadMember;
		Entry->bIsSquadMember = true;
		ConfirmedLocalSquadMembers.Add(Key);
		if (bMembershipChanged) SaveFriends();
		// Explicit local confirmation is a refresh event even when the saved flag was already true.
		OnFriendsChanged.Broadcast();
		return true;
	}

	const bool bMembershipChanged = Entry->bIsSquadMember;
	const bool bSessionConfirmationRemoved = ConfirmedLocalSquadMembers.Remove(Key) > 0;
	Entry->bIsSquadMember = false;
	if (bMembershipChanged) SaveFriends();
	if (bMembershipChanged || bSessionConfirmationRemoved) OnFriendsChanged.Broadcast();
	return true;
}

bool UMSFriendSubsystem::SendSquadInvite(const FString& Name)
{
	FMSFriendEntry* Entry = FindMutableFriend(Name);
	if (!Entry || Entry->bIsSquadMember || Entry->bHasPendingSquadInvite) return false;
	if (GetSquadMemberCount() >= MaxInvitedSquadMembers) return false;
	Entry->bHasPendingSquadInvite = true;
	SaveFriends();
	OnFriendsChanged.Broadcast();
	// オンラインの相手は少し遅れて承認する(実際の相手応答のローカルシミュレーション)。
	// オフラインの相手は承認待ちのまま保持される。
	if (Entry->State != EMSFriendState::Offline) ScheduleSimulatedAutoAccept(Entry->Name);
	return true;
}

bool UMSFriendSubsystem::AcceptSquadInvite(const FString& Name)
{
	FMSFriendEntry* Entry = FindMutableFriend(Name);
	if (!Entry || !Entry->bHasPendingSquadInvite || Entry->bIsSquadMember) return false;
	if (GetSquadMemberCount() >= MaxInvitedSquadMembers)
	{
		// 承認時点で満員なら入室させず招待を破棄する
		Entry->bHasPendingSquadInvite = false;
		SaveFriends();
		OnFriendsChanged.Broadcast();
		return false;
	}
	Entry->bHasPendingSquadInvite = false;
	Entry->bIsSquadMember = true;
	const FString JoinedName = Entry->Name;
	ConfirmedLocalSquadMembers.Add(MSFriendSubsystemPrivate::NormalizeFriendKey(JoinedName));
	ScheduledAutoAccepts.Remove(JoinedName);
	SaveFriends();
	OnFriendsChanged.Broadcast();
	OnSquadInviteAccepted.Broadcast(JoinedName);
	return true;
}

bool UMSFriendSubsystem::DeclineSquadInvite(const FString& Name)
{
	FMSFriendEntry* Entry = FindMutableFriend(Name);
	if (!Entry || !Entry->bHasPendingSquadInvite) return false;
	Entry->bHasPendingSquadInvite = false;
	ScheduledAutoAccepts.Remove(Entry->Name);
	SaveFriends();
	OnFriendsChanged.Broadcast();
	return true;
}

bool UMSFriendSubsystem::HasPendingSquadInvite(const FString& Name) const
{
	const FMSFriendEntry* Entry = FindFriend(Name);
	return Entry && Entry->bHasPendingSquadInvite;
}

void UMSFriendSubsystem::ScheduleSimulatedAutoAccept(const FString& Name)
{
	if (ScheduledAutoAccepts.Contains(Name)) return;
	ScheduledAutoAccepts.Add(Name);
	TWeakObjectPtr<UMSFriendSubsystem> WeakThis(this);
	AutoAcceptTickerHandles.Add(FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis, Name](float)
		{
			if (UMSFriendSubsystem* Subsystem = WeakThis.Get())
			{
				if (Subsystem->ScheduledAutoAccepts.Contains(Name)) Subsystem->AcceptSquadInvite(Name);
			}
			return false; // 一度だけ実行
		}), AutoAcceptDelaySeconds));
}

void UMSFriendSubsystem::ClearScheduledAutoAccepts()
{
	for (const FTSTicker::FDelegateHandle& Handle : AutoAcceptTickerHandles)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}
	AutoAcceptTickerHandles.Reset();
	ScheduledAutoAccepts.Reset();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSFriendSubsystem::FlushPendingAutoAcceptsForAutomationTests()
{
	const TArray<FString> Pending = ScheduledAutoAccepts;
	for (const FString& Name : Pending) AcceptSquadInvite(Name);
}
#endif

const FMSFriendEntry* UMSFriendSubsystem::FindFriend(const FString& Name) const
{
	return Friends.FindByPredicate([&Name](const FMSFriendEntry& Entry)
	{
		return Entry.Name.Equals(Name, ESearchCase::IgnoreCase);
	});
}

FMSFriendEntry* UMSFriendSubsystem::FindMutableFriend(const FString& Name)
{
	return Friends.FindByPredicate([&Name](const FMSFriendEntry& Entry)
	{
		return Entry.Name.Equals(Name, ESearchCase::IgnoreCase);
	});
}

int32 UMSFriendSubsystem::GetSquadMemberCount() const
{
	int32 Count = 0;
	for (const FMSFriendEntry& Entry : Friends) if (Entry.bIsSquadMember) ++Count;
	return Count;
}

FString UMSFriendSubsystem::GetSelfDisplayName() const
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	return Instance ? Instance->LoggedInUsername : FString();
}

bool UMSFriendSubsystem::IsSquadMembershipConfirmedThisSession(const FString& Name) const
{
	return ConfirmedLocalSquadMembers.Contains(MSFriendSubsystemPrivate::NormalizeFriendKey(Name));
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSFriendSubsystem::SeedDefaultFriendsForAutomationTests()
{
	SeedDefaultFriends();
	ConfirmedLocalSquadMembers.Reset();
	for (const FMSFriendEntry& Entry : Friends)
		if (Entry.bIsSquadMember)
			ConfirmedLocalSquadMembers.Add(MSFriendSubsystemPrivate::NormalizeFriendKey(Entry.Name));
	SaveFriends();
	OnFriendsChanged.Broadcast();
}
#endif

void UMSFriendSubsystem::SeedDefaultFriends()
{
	// 架空キャラ名は MSDummyNames::MakeDummyName に統一(「ガンオンN」— タスク#145)
	Friends = {
		FMSFriendEntry(MSDummyNames::MakeDummyName(1), 14, 1562, EMSFriendState::Reserved, true),
		FMSFriendEntry(MSDummyNames::MakeDummyName(2), 11, 1518, EMSFriendState::Lobby, true),
		FMSFriendEntry(MSDummyNames::MakeDummyName(3), 18, 1680, EMSFriendState::InBattle),
		FMSFriendEntry(MSDummyNames::MakeDummyName(4), 9, 1472, EMSFriendState::Ready),
		FMSFriendEntry(MSDummyNames::MakeDummyName(5), 22, 1735, EMSFriendState::Reserved),
		FMSFriendEntry(MSDummyNames::MakeDummyName(6), 16, 1604, EMSFriendState::Lobby),
		FMSFriendEntry(MSDummyNames::MakeDummyName(7), 13, 1540, EMSFriendState::Offline),
		FMSFriendEntry(MSDummyNames::MakeDummyName(8), 7, 1398, EMSFriendState::Offline),
		FMSFriendEntry(MSDummyNames::MakeDummyName(9), 12, 1525, EMSFriendState::InBattle),
		// 立ち絵アバター持ちのダミーパイロット(MSAvatarLibraryが名前で解決する)
		FMSFriendEntry(MSDummyNames::MakeDummyName(10), 15, 1590, EMSFriendState::Lobby, true),
		FMSFriendEntry(MSDummyNames::MakeDummyName(11), 10, 1505, EMSFriendState::Ready)
	};
}
