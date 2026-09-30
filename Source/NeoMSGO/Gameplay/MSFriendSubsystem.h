// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSFriendSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMSFriendState : uint8
{
	Offline,
	Lobby,
	Reserved,
	InBattle,
	Ready
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSFriendEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") int32 Rank = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") int32 Rate = 1500;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") EMSFriendState State = EMSFriendState::Offline;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") bool bIsSquadMember = false;
	/** 小隊招待を送信済みで相手の承認待ち(承認されると自動で小隊入室する — タスク#144) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="MS|Friends") bool bHasPendingSquadInvite = false;

	FMSFriendEntry() = default;
	FMSFriendEntry(FString InName, int32 InRank, int32 InRate, EMSFriendState InState, bool bInSquad = false)
		: Name(MoveTemp(InName)), Rank(InRank), Rate(InRate), State(InState), bIsSquadMember(bInSquad) {}
};

UCLASS()
class NEOMSGO_API UMSFriendSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame) TArray<FMSFriendEntry> Friends;
};

DECLARE_MULTICAST_DELEGATE(FMSFriendsChangedDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FMSSquadInviteAcceptedDelegate, const FString& /*FriendName*/);

/**
 * Local friend/presence model used by the lobby UI.
 * オンライン同期は将来のバックエンド実装が必要。
 */
UCLASS()
class NEOMSGO_API UMSFriendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool AddFriend(const FString& Name, int32 Rank = 1, int32 Rate = 1500,
		EMSFriendState State = EMSFriendState::Lobby);
	bool RemoveFriend(const FString& Name);
	const TArray<FMSFriendEntry>& GetFriends() const { return Friends; }
	bool SetFriendState(const FString& Name, EMSFriendState State);
	bool SetFriendSquadMember(const FString& Name, bool bIsSquadMember);
	const FMSFriendEntry* FindFriend(const FString& Name) const;
	int32 GetSquadMemberCount() const;

	/** 小隊の最大メンバー数(自分1枠+招待5枠=6スロットパネルのうち招待可能数) */
	static constexpr int32 MaxInvitedSquadMembers = 5;

	/**
	 * 小隊招待を送信する(タスク#144)。相手は即入室せず承認待ち(bHasPendingSquadInvite)になる。
	 * オンラインの相手はAutoAcceptDelaySeconds後に承認をシミュレートし、AcceptSquadInviteを
	 * 経由して自動で小隊へ入室する(オンライン同期は将来のバックエンド実装が必要)。
	 * @return 送信できたらtrue(未登録/既に小隊/既に招待済み/満員はfalse)
	 */
	bool SendSquadInvite(const FString& Name);
	/** 相手が招待を承認した時の入口。承認待ちを解除して小隊メンバーへ昇格し、両UIへ通知する */
	bool AcceptSquadInvite(const FString& Name);
	/** 相手が招待を辞退した時の入口。承認待ちのみ解除する */
	bool DeclineSquadInvite(const FString& Name);
	bool HasPendingSquadInvite(const FString& Name) const;
	/** Username used for the local player's "self" row/slot. Empty until login succeeds. */
	FString GetSelfDisplayName() const;
	/** True only after this session explicitly confirmed the member (invite or local setup). */
	bool IsSquadMembershipConfirmedThisSession(const FString& Name) const;

	bool SaveFriends() const;
	bool ReloadFriends();
	FMSFriendsChangedDelegate OnFriendsChanged;
	/** 相手が小隊招待を承認して自動入室した時に発火(引数=参加者名。UI通知用) */
	FMSSquadInviteAcceptedDelegate OnSquadInviteAccepted;

#if WITH_DEV_AUTOMATION_TESTS
	void SetSaveSlotForAutomationTests(const FString& InSlotName) { SaveSlotName = InSlotName; }
	void ResetAndReloadForAutomationTests() { Friends.Reset(); ReloadFriends(); }
	/** Explicit fixture-only population; production/new saves remain empty until real data is added. */
	void SeedDefaultFriendsForAutomationTests();
	/** 遅延承認シミュレーションを待たずに即時発火させる(テスト用) */
	void FlushPendingAutoAcceptsForAutomationTests();
	int32 GetScheduledAutoAcceptCountForAutomationTests() const { return ScheduledAutoAccepts.Num(); }
#endif

private:
	void SeedDefaultFriends();
	FMSFriendEntry* FindMutableFriend(const FString& Name);
	/** オンラインの相手による承認をAutoAcceptDelaySeconds後にシミュレートする */
	void ScheduleSimulatedAutoAccept(const FString& Name);
	void ClearScheduledAutoAccepts();

	UPROPERTY(Transient) TArray<FMSFriendEntry> Friends;
	/** Session evidence is intentionally not persisted with bIsSquadMember. */
	TSet<FString> ConfirmedLocalSquadMembers;
	FString SaveSlotName = TEXT("NeoMSGO_Friends_v1");
	static constexpr int32 SaveUserIndex = 0;

	/** 承認シミュレーションまでの遅延秒数(実際の相手の応答を模す) */
	float AutoAcceptDelaySeconds = 1.5f;
	/** 承認シミュレーション予約中の相手名(重複予約防止+テスト即時実行用) */
	TArray<FString> ScheduledAutoAccepts;
	TArray<FTSTicker::FDelegateHandle> AutoAcceptTickerHandles;
};
