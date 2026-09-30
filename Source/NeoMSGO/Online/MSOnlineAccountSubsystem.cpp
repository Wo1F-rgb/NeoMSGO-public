// Copyright NeoMSGO Project. All Rights Reserved.

#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSCredentialStore.h"
#include "Online/MSRoomSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"

#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/MSDeckSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMSOnlineAccount, Log, All);

namespace MSOnlineAccountPrivate
{
	static const TCHAR* GuestResumeIndexSection = TEXT("/Script/NeoMSGO.MSGuestResumeIndex");
	static const TCHAR* GuestResumeSectionPrefix = TEXT("/Script/NeoMSGO.MSGuestResume.");

	static bool SerializeObject(const TSharedRef<FJsonObject>& Object, FString& OutJson)
	{
		OutJson.Reset();
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		return FJsonSerializer::Serialize(Object, Writer);
	}

	static TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	static bool CanonicalizeLegacyDeck(const FMSOnlineDeckSnapshot& Legacy,
		FMSOnlineDeckSnapshot& OutCanonical)
	{
		if (Legacy.SchemaVersion != 1 || Legacy.Slots.Num() != 4) return false;
		OutCanonical = FMSOnlineDeckSnapshot();
		OutCanonical.SchemaVersion = 3;
		OutCanonical.Revision = Legacy.Revision;
		OutCanonical.SelectedSlot = Legacy.SelectedSlot;
		for (int32 SlotIndex = 0; SlotIndex < Legacy.Slots.Num(); ++SlotIndex)
		{
			const FMSOnlineDeckSlot& LegacySlot = Legacy.Slots[SlotIndex];
			UMSMobileSuitData* Suit = nullptr;
			FMSDeckLoadout Loadout;
			if (!MSOnline::ResolveSlot(LegacySlot, Suit, Loadout, true)) return false;

			FMSOnlineDeckSlot& CanonicalSlot = OutCanonical.Slots[SlotIndex];
			CanonicalSlot.SuitId = LegacySlot.SuitId;
			CanonicalSlot.bShieldEquipped = Loadout.bShieldEquipped;
			for (int32 WeaponIndex = 0; WeaponIndex < 3; ++WeaponIndex)
			{
				if (!Loadout.EquippedWeapons.IsValidIndex(WeaponIndex)
					|| Loadout.EquippedWeapons[WeaponIndex].IsNull()) continue;
				UMSWeaponDataBase* Weapon = Loadout.EquippedWeapons[WeaponIndex].LoadSynchronous();
				if (!Weapon || Cast<UMSShieldWeaponData>(Weapon)) return false;
				CanonicalSlot.WeaponIds[WeaponIndex] = Weapon->GetPrimaryAssetId();
			}
			if (!CanonicalSlot.SuitId.IsValid()) CanonicalSlot.bShieldEquipped = false;
		}
		return true;
	}

	static FString JsonStringOrEmpty(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
	}

	static FString JsonStringOrNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		if (!Object.IsValid()) return FString();
		FString Text;
		if (Object->TryGetStringField(Field, Text)) return Text;
		double Number = 0.0;
		return Object->TryGetNumberField(Field, Number) && FMath::IsFinite(Number)
			? FString::SanitizeFloat(Number) : FString();
	}

	static bool JsonBoolOrFalse(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		bool Value = false;
		return Object.IsValid() && Object->TryGetBoolField(Field, Value) ? Value : false;
	}

	static TSharedPtr<FJsonValue> NullablePrimaryAssetId(const FPrimaryAssetId& Id)
	{
		if (Id.IsValid()) return MakeShared<FJsonValueString>(Id.ToString());
		return MakeShared<FJsonValueNull>();
	}

	static bool ParsePrimaryAssetId(const TSharedPtr<FJsonValue>& Value, FPrimaryAssetId& OutId)
	{
		OutId = FPrimaryAssetId();
		if (!Value.IsValid() || Value->IsNull()) return true;
		FString Text;
		if (Value->Type != EJson::String || !Value->TryGetString(Text) || Text.Len() > 256) return false;
		OutId = FPrimaryAssetId::FromString(Text);
		return OutId.IsValid() && OutId.ToString() == Text;
	}
}

void UMSOnlineAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	AuthMode = MSOnline::IsEnabled() ? EMSOnlineAuthMode::Online : EMSOnlineAuthMode::OfflineLocal;
	AuthBaseUrl = MSOnline::GetAuthBaseUrl();

	Identity = FMSOnlineIdentity();
	CommittedDeck = FMSOnlineDeckSnapshot();
	AccountDeckCache.Reset();
	PendingGameTicket = FPendingGameTicket();
	AuthGeneration = 0;
	RequestGeneration = 0;
	SetLoginState(EMSOnlineLoginState::LoggedOut);
}

void UMSOnlineAccountSubsystem::Deinitialize()
{
	++RequestGeneration;
	++AuthGeneration;
	Identity = FMSOnlineIdentity();
	CurrentUsername.Reset();
	AccessToken.Reset();
	GuestResumeToken.Reset();
	AccountDeckCache.Reset();
	CommittedDeck = FMSOnlineDeckSnapshot();
	PendingGameTicket = FPendingGameTicket();
	Super::Deinitialize();
}

void UMSOnlineAccountSubsystem::Login(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion)
{
	if (!IsOnlineMode())
	{
		if (Completion) Completion(false, TEXT("ローカル認証モードです。"));
		return;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("username"), Username);
	BodyObject->SetStringField(TEXT("password"), Password);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	BeginAuthRequest(TEXT("/v1/auth/login"), Body, TEXT("login"), MoveTemp(Completion));
}

void UMSOnlineAccountSubsystem::Register(const FString& Username, const FString& Password, FMSOnlineAuthCompletion Completion)
{
	if (!IsOnlineMode())
	{
		if (Completion) Completion(false, TEXT("ローカル認証モードです。"));
		return;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("username"), Username);
	BodyObject->SetStringField(TEXT("password"), Password);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	BeginAuthRequest(TEXT("/v1/auth/register"), Body, TEXT("register"), MoveTemp(Completion));
}

void UMSOnlineAccountSubsystem::LoginAsGuest(const FString& DisplayName, FMSOnlineAuthCompletion Completion)
{
	if (!IsOnlineMode())
	{
		if (Completion) Completion(false, TEXT("ローカル認証モードです。"));
		return;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	FString CleanDisplayName = DisplayName;
	CleanDisplayName.TrimStartAndEndInline();
	if (!CleanDisplayName.IsEmpty()) BodyObject->SetStringField(TEXT("display_name"), CleanDisplayName);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	BeginAuthRequest(TEXT("/v1/auth/guest"), Body, TEXT("guest"), MoveTemp(Completion));
}

void UMSOnlineAccountSubsystem::ResumeGuest(FMSOnlineAuthCompletion Completion)
{
	if (!IsOnlineMode())
	{
		if (Completion) Completion(false, TEXT("ローカル認証モードです。"));
		return;
	}
	FString ResumeToken;
	FString DisplayName;
	if (!LoadGuestResumeToken(ResumeToken, DisplayName))
	{
		if (Completion) Completion(false, CredentialStorageWarning.IsEmpty() ? TEXT("保存されたゲスト情報がありません。") : CredentialStorageWarning);
		return;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("resume_token"), ResumeToken);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	BeginAuthRequest(TEXT("/v1/auth/resume"), Body, TEXT("resume"), MoveTemp(Completion));
}

void UMSOnlineAccountSubsystem::UpgradeGuest(const FString& Username, const FString& Password,
	FMSOnlineAuthCompletion Completion)
{
	if (!IsOnlineMode() || !HasOnlineSession() || !Identity.bIsGuest || bDeckRequestPending)
	{
		if (Completion) Completion(false, TEXT("ゲストセッションがありません。"));
		return;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("username"), Username);
	BodyObject->SetStringField(TEXT("password"), Password);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	const uint64 Generation = ++RequestGeneration;
	bDeckRequestPending = false;
	SetLoginState(EMSOnlineLoginState::Pending);
	SendHttpRequest(TEXT("POST"), TEXT("/v1/auth/upgrade"), Body, true, Generation,
		[this, Generation, Completion = MoveTemp(Completion)](const bool bSucceeded, const int32 ResponseCode, const FString& ResponseBody)
		{
			HandleAuthResponse(Generation, TEXT("upgrade"), Completion, bSucceeded, ResponseCode, ResponseBody);
		});
}

void UMSOnlineAccountSubsystem::Logout()
{
	if (!IsOnlineMode() || AccessToken.IsEmpty())
	{
		ClearIdentity();
		return;
	}
	const uint64 Generation = ++RequestGeneration;
	SendHttpRequest(TEXT("POST"), TEXT("/v1/auth/logout"), TEXT(""), true, Generation,
		[](const bool, const int32, const FString&) { /* The local identity is cleared regardless of server reachability. */ });
	ClearIdentity();
}

bool UMSOnlineAccountSubsystem::RequestGameTicket(const FString& ServerId)
{
	if (!IsOnlineMode() || !HasAuthenticatedIdentity() || AccessToken.IsEmpty() || bDeckRequestPending || ServerId.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("server_id"), ServerId.TrimStartAndEnd());
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	const uint64 RequestId = RequestGeneration;
	const int64 Generation = AuthGeneration;
	CancelPendingGameTicket();
	const uint64 TicketRequest = GameTicketRequestGeneration;
	return SendHttpRequest(TEXT("POST"), TEXT("/v1/game/tickets"), Body, true, RequestId,
		[this, ServerId, Generation, TicketRequest](const bool bSucceeded, const int32 ResponseCode, const FString& ResponseBody)
		{
			if (TicketRequest != GameTicketRequestGeneration || Generation != AuthGeneration) return;
			const TSharedPtr<FJsonObject> Object = MSOnlineAccountPrivate::ParseObject(ResponseBody);
			FString Ticket;
			FString GameAddress;
			FString ResponseServerId;
			double ExpiresAt = 0.0;
			const bool bValid = bSucceeded && EHttpResponseCodes::IsOk(ResponseCode) && Object.IsValid()
				&& Object->TryGetStringField(TEXT("ticket"), Ticket)
				&& Object->TryGetStringField(TEXT("game_address"), GameAddress)
				&& Object->TryGetStringField(TEXT("server_id"), ResponseServerId)
				&& Object->TryGetNumberField(TEXT("expires_at"), ExpiresAt)
				&& !Ticket.IsEmpty() && !GameAddress.IsEmpty()
				&& ResponseServerId.Equals(ServerId.TrimStartAndEnd(), ESearchCase::IgnoreCase)
				&& ExpiresAt > static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0;
			if (bValid)
			{
				PendingGameTicket.ServerId = ResponseServerId;
				PendingGameTicket.GameAddress = GameAddress;
				PendingGameTicket.Ticket = Ticket;
				PendingGameTicket.ExpiresAtMs = static_cast<int64>(ExpiresAt);
				PendingGameTicket.Generation = Generation;
				PendingGameTicket.bConsumed = false;
				OnGameTicketReady.Broadcast(ResponseServerId, GameAddress, Generation);
			}
			OnRequestFinished.Broadcast(TEXT("game_ticket"), bValid,
				bValid ? FText::FromString(TEXT("参加チケットを取得しました。")) : FText::FromString(TEXT("参加チケットを取得できませんでした。")), Generation);
		});
}

bool UMSOnlineAccountSubsystem::SendBattleAssistRequest(const FString& Verb, const FString& Path,
	const FString& Body, TFunction<void(bool, int32, const FString&)> Completion)
{
	if (!HasOnlineSession() || !((Verb == TEXT("GET") && Path == TEXT("/v1/me/battle-assist"))
		|| (Verb == TEXT("POST") && Path == TEXT("/v1/me/battle-assist/develop")))) return false;
	return SendHttpRequest(Verb, Path, Body, true, RequestGeneration, MoveTemp(Completion));
}

bool UMSOnlineAccountSubsystem::SendRoomRequest(const FString& Verb, const FString& Path,
	const FString& Body, TFunction<void(bool, int32, const FString&)> Completion)
{
	if (!HasOnlineSession() || AccessToken.IsEmpty() ||
		!(Path == TEXT("/v1/rooms") || Path.StartsWith(TEXT("/v1/rooms/")))) return false;
	return SendHttpRequest(Verb, Path, Body, true, RequestGeneration, MoveTemp(Completion));
}

bool UMSOnlineAccountSubsystem::RequestRoomTicket(const FString& RoomId, const FString& InstanceId)
{
	FGuid RoomGuid, InstanceGuid;
	if (!HasOnlineSession() || bDeckRequestPending || CatalogHash.IsEmpty() ||
		!FGuid::Parse(RoomId, RoomGuid) || !FGuid::Parse(InstanceId, InstanceGuid)) return false;
	auto BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetStringField(TEXT("instance_id"), InstanceId);
	BodyObject->SetStringField(TEXT("catalog_hash"), CatalogHash);
	BodyObject->SetStringField(TEXT("build_id"), MSRoomProtocol::BuildId);
	BodyObject->SetNumberField(TEXT("expected_revision"), static_cast<double>(CommittedDeck.Revision));
	FString Body; MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);
	const int64 Generation = AuthGeneration;
	CancelPendingGameTicket();
	const uint64 TicketRequest = GameTicketRequestGeneration;
	return SendRoomRequest(TEXT("POST"), TEXT("/v1/rooms/") + RoomId + TEXT("/tickets"), Body,
		[this, RoomId, InstanceId, Generation, TicketRequest](bool bOk, int32 Code, const FString& ResponseBody)
		{
			if (TicketRequest != GameTicketRequestGeneration || Generation != AuthGeneration) return;
			const auto Object = MSOnlineAccountPrivate::ParseObject(ResponseBody);
			FString Ticket, Address, Id, Instance; double ExpiresAt = 0;
			const bool bValid = bOk && EHttpResponseCodes::IsOk(Code) && Object && Generation == AuthGeneration &&
				Object->TryGetStringField(TEXT("ticket"), Ticket) && Ticket.Len() >= 24 && Ticket.Len() <= 512 &&
				Object->TryGetStringField(TEXT("game_address"), Address) &&
				Object->TryGetStringField(TEXT("server_id"), Id) && Id == RoomId &&
				Object->TryGetStringField(TEXT("instance_id"), Instance) && Instance == InstanceId &&
				Object->TryGetNumberField(TEXT("expires_at"), ExpiresAt) && FMath::IsFinite(ExpiresAt) &&
				ExpiresAt > static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000 &&
				UMSRoomSubsystem::NormalizeAddress(Address, Address);
			if (bValid)
			{
				PendingGameTicket.ServerId = Id; PendingGameTicket.GameAddress = Address;
				PendingGameTicket.Ticket = MoveTemp(Ticket); PendingGameTicket.ExpiresAtMs = static_cast<int64>(ExpiresAt);
				PendingGameTicket.Generation = Generation; PendingGameTicket.bConsumed = false;
				OnGameTicketReady.Broadcast(Id, Address, Generation);
			}
			OnRequestFinished.Broadcast(TEXT("game_ticket"), bValid,
				bValid ? FText::GetEmpty() : FText::FromString(Code == 409
					? TEXT("ルームまたは編成の状態が変わりました。再検索してください。")
					: TEXT("ルームへ参加できません。接続状態を確認して再検索してください。")), Generation);
		});
}

void UMSOnlineAccountSubsystem::CancelPendingGameTicket()
{
	++GameTicketRequestGeneration;
	PendingGameTicket = FPendingGameTicket();
}

bool UMSOnlineAccountSubsystem::TakePendingGameTicket(const FString& ExpectedServerId, FString& OutTicket)
{
	OutTicket.Reset();
	const int64 NowMs = static_cast<int64>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000;
	if (PendingGameTicket.bConsumed || PendingGameTicket.Generation != AuthGeneration
		|| PendingGameTicket.ExpiresAtMs <= NowMs
		|| !PendingGameTicket.ServerId.Equals(ExpectedServerId.TrimStartAndEnd(), ESearchCase::IgnoreCase))
	{
		return false;
	}
	OutTicket = MoveTemp(PendingGameTicket.Ticket);
	PendingGameTicket.bConsumed = true;
	return !OutTicket.IsEmpty();
}

bool UMSOnlineAccountSubsystem::GetCachedDeck(FMSOnlineDeckSnapshot& OutSnapshot) const
{
	const FMSOnlineDeckSnapshot* Cached = AccountDeckCache.Find(GetAccountCacheKey());
	if (!Cached) return false;
	OutSnapshot = *Cached;
	return true;
}

bool UMSOnlineAccountSubsystem::PutDeck(const FMSOnlineDeckSnapshot& Candidate, FMSOnlineDeckCompletion Completion)
{
	if (bDeckRequestPending)
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("デッキの保存・読込が処理中です。"), CommittedDeck);
		return false;
	}
	if (!IsOnlineMode() || !HasAuthenticatedIdentity() || AccessToken.IsEmpty())
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("オンラインアカウントがありません。"), Candidate);
		return false;
	}
	FMSOnlineDeckSnapshot CandidateForWrite = Candidate;
	if (Candidate.SchemaVersion == 1)
	{
		if (!MSOnlineAccountPrivate::CanonicalizeLegacyDeck(Candidate, CandidateForWrite))
		{
			if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("旧デッキを現行形式へ移行できません。"), Candidate);
			return false;
		}
	}
	if (CandidateForWrite.SchemaVersion == 2)
	{
		for (const auto& Slot : CandidateForWrite.Slots) if (!Slot.BattleAssist.IsEmpty()) return false;
		CandidateForWrite.SchemaVersion = 3;
	}
	if (CandidateForWrite.SchemaVersion != 3 || CandidateForWrite.Slots.Num() != 4)
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("デッキ形式が不正です。"), Candidate);
		return false;
	}
	FString DeckJson;
	if (!SerializeDeck(CandidateForWrite, DeckJson))
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("デッキの準備に失敗しました。"), Candidate);
		return false;
	}
	const TSharedPtr<FJsonObject> DeckObject = MSOnlineAccountPrivate::ParseObject(DeckJson);
	if (!DeckObject.IsValid())
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("デッキの準備に失敗しました。"), Candidate);
		return false;
	}
	const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetNumberField(TEXT("expected_revision"), static_cast<double>(CandidateForWrite.Revision));
	BodyObject->SetObjectField(TEXT("deck"), DeckObject);
	FString Body;
	MSOnlineAccountPrivate::SerializeObject(BodyObject, Body);

	bDeckRequestPending = true;
	OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Pending);
	if (Completion) Completion(EMSOnlineDeckSyncState::Pending, TEXT("保存中…"), CandidateForWrite);
	const uint64 Generation = RequestGeneration;
	return SendHttpRequest(TEXT("PUT"), TEXT("/v1/me/deck"), Body, true, Generation,
		[this, CandidateForWrite, Completion = MoveTemp(Completion)](const bool bSucceeded, const int32 ResponseCode, const FString& ResponseBody)
		{
			bDeckRequestPending = false;
			FMSOnlineDeckSnapshot Result = CandidateForWrite;
			FString ErrorMessage;
			const TSharedPtr<FJsonObject> Object = MSOnlineAccountPrivate::ParseObject(ResponseBody);
			const TSharedPtr<FJsonObject>* DeckObject = nullptr;
			bool bCommitted = bSucceeded && EHttpResponseCodes::IsOk(ResponseCode) && Object.IsValid()
				&& Object->TryGetObjectField(TEXT("deck"), DeckObject) && DeckObject && DeckObject->IsValid();
			if (bCommitted)
			{
				bCommitted = ParseDeck(*DeckObject, Result, ErrorMessage);
			}
			if (!bCommitted)
			{
				if (ResponseCode == 409)
				{
					double CurrentRevision = 0.0;
					if (Object.IsValid() && Object->TryGetNumberField(TEXT("current_revision"), CurrentRevision)
						&& FMath::IsFinite(CurrentRevision) && CurrentRevision >= 0.0)
					{
						ErrorMessage = FString::Printf(TEXT("他の端末でデッキが更新されました（revision %lld）。再読込してください。"),
							static_cast<long long>(CurrentRevision));
					}
					else
					{
						ErrorMessage = TEXT("他の端末でデッキが更新されました。再読込してください。");
					}
				}
				if (ErrorMessage.IsEmpty()) ErrorMessage = TEXT("デッキを保存できませんでした。");
				OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Failed);
				OnRequestFinished.Broadcast(TEXT("deck_put"), false, FText::FromString(ErrorMessage), AuthGeneration);
				if (Completion) Completion(EMSOnlineDeckSyncState::Failed, ErrorMessage, CandidateForWrite);
				return;
			}
			CommittedDeck = Result;
			AccountDeckCache.Add(GetAccountCacheKey(), Result);
			SaveCommittedDeckCache();
			OnDeckCommitted.Broadcast(Result, AuthGeneration);
			OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Committed);
			OnRequestFinished.Broadcast(TEXT("deck_put"), true, FText::FromString(TEXT("保存しました。")), AuthGeneration);
			if (Completion) Completion(EMSOnlineDeckSyncState::Committed, TEXT("保存しました。"), Result);
		});
}

bool UMSOnlineAccountSubsystem::ImportLocalDeck(const FMSOnlineDeckSnapshot& Candidate, FMSOnlineDeckCompletion Completion)
{
	// Import is an explicit caller action; no local deck is read by this subsystem during login.
	return PutDeck(Candidate, MoveTemp(Completion));
}

bool UMSOnlineAccountSubsystem::RefreshCommittedDeck(FMSOnlineDeckCompletion Completion)
{
	if (bDeckRequestPending)
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("デッキの保存・読込が処理中です。"), CommittedDeck);
		return false;
	}
	if (!IsOnlineMode() || !HasOnlineSession())
	{
		if (Completion) Completion(EMSOnlineDeckSyncState::Failed, TEXT("オンラインアカウントがありません。"), CommittedDeck);
		return false;
	}
	const uint64 RequestId = RequestGeneration;
	const int64 Generation = AuthGeneration;
	bDeckRequestPending = true;
	return SendHttpRequest(TEXT("GET"), TEXT("/v1/me/deck"), TEXT(""), true, RequestId,
		[this, Generation, Completion = MoveTemp(Completion)](const bool bSucceeded, const int32 ResponseCode, const FString& ResponseBody)
		{
			bDeckRequestPending = false;
			const TSharedPtr<FJsonObject> Object = MSOnlineAccountPrivate::ParseObject(ResponseBody);
			FMSOnlineDeckSnapshot Result;
			FString ErrorMessage;
			const bool bValid = bSucceeded && EHttpResponseCodes::IsOk(ResponseCode) && Object.IsValid()
				&& Object->HasTypedField<EJson::Object>(TEXT("deck"))
				&& ParseDeck(Object->GetObjectField(TEXT("deck")), Result, ErrorMessage);
			if (!bValid)
			{
				if (ErrorMessage.IsEmpty()) ErrorMessage = TEXT("デッキを読み込めませんでした。");
				OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Failed);
				OnRequestFinished.Broadcast(TEXT("deck_get"), false, FText::FromString(ErrorMessage), Generation);
				if (Completion) Completion(EMSOnlineDeckSyncState::Failed, ErrorMessage, CommittedDeck);
				return;
			}
			CommittedDeck = Result;
			AccountDeckCache.Add(GetAccountCacheKey(), Result);
			SaveCommittedDeckCache();
			OnDeckCommitted.Broadcast(Result, Generation);
			OnRequestFinished.Broadcast(TEXT("deck_get"), true, FText::FromString(TEXT("デッキを読み込みました。")), Generation);
			if (Completion) Completion(EMSOnlineDeckSyncState::Committed, TEXT("デッキを読み込みました。"), Result);
		});
}

void UMSOnlineAccountSubsystem::BeginAuthRequest(const FString& Path, const FString& Body, const FString& Operation,
	FMSOnlineAuthCompletion Completion)
{
	ClearIdentity();
	CredentialStorageWarning.Reset();
	const uint64 Generation = RequestGeneration;
	SetLoginState(EMSOnlineLoginState::Pending);
	SendHttpRequest(TEXT("POST"), Path, Body, false, Generation,
		[this, Generation, Operation, Completion = MoveTemp(Completion)](const bool bSucceeded, const int32 ResponseCode, const FString& ResponseBody)
		{
			HandleAuthResponse(Generation, Operation, Completion, bSucceeded, ResponseCode, ResponseBody);
		});
}

void UMSOnlineAccountSubsystem::HandleAuthResponse(uint64 Generation, const FString& Operation,
	FMSOnlineAuthCompletion Completion, const bool bSucceeded, const int32 ResponseCode, const FString& Body)
{
	if (Generation != RequestGeneration) return;
	FMSOnlineIdentity NewIdentity;
	FMSOnlineDeckSnapshot NewDeck;
	FString ErrorMessage;
	const bool bOk = bSucceeded && EHttpResponseCodes::IsOk(ResponseCode)
		&& ParseAuthEnvelope(Body, NewIdentity, NewDeck, ErrorMessage);
	if (!bOk)
	{
		// Upgrade preserves the existing guest on rejection. Replacement auth starts
		// from ClearIdentity, so a failed login cannot display the previous account.
		SetLoginState(Operation == TEXT("upgrade") && Identity.IsValid() && !AccessToken.IsEmpty()
			? EMSOnlineLoginState::Authenticated : EMSOnlineLoginState::Failed);
		if (ErrorMessage.IsEmpty())
		{
			const TSharedPtr<FJsonObject> ErrorEnvelope = MSOnlineAccountPrivate::ParseObject(Body);
			const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
			FString ServerMessage;
			if (ErrorEnvelope.IsValid() && ErrorEnvelope->TryGetObjectField(TEXT("error"), ErrorObject)
				&& ErrorObject && ErrorObject->IsValid()
				&& (*ErrorObject)->TryGetStringField(TEXT("message"), ServerMessage)
				&& !ServerMessage.IsEmpty())
			{
				ErrorMessage = ServerMessage;
			}
		}
		if (ErrorMessage.IsEmpty()) ErrorMessage = FString::Printf(TEXT("%sに失敗しました。"), *Operation);
		OnRequestFinished.Broadcast(FName(*Operation), false, FText::FromString(ErrorMessage), AuthGeneration);
		if (Completion) Completion(false, ErrorMessage);
		return;
	}
	ApplyIdentity(NewIdentity, NewDeck);
	OnRequestFinished.Broadcast(FName(*Operation), true, FText::FromString(TEXT("認証しました。")), AuthGeneration);
	if (Completion) Completion(true, NewIdentity.DisplayName);
}

void UMSOnlineAccountSubsystem::HandleLogoutResponse(const bool, const int32)
{
	// Logout is intentionally best effort; ClearIdentity has already removed the local token.
}

bool UMSOnlineAccountSubsystem::SendHttpRequest(const FString& Verb, const FString& Path, const FString& Body,
	const bool bWithBearer, const uint64 Generation, TFunction<void(bool, int32, const FString&)> Completion)
{
	const FString Url = BuildUrl(Path);
	if (Url.IsEmpty())
	{
		if (Completion) Completion(false, 0, FString());
		return false;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (RequestSenderForAutomationTests)
	{
		const bool bAccepted = RequestSenderForAutomationTests(Verb, Path, Body, bWithBearer ? AccessToken : FString());
		if (bAccepted && bDeferResponsesForAutomationTests)
		{
			TWeakObjectPtr<UMSOnlineAccountSubsystem> WeakAccount(this);
			DeferredResponsesForAutomationTests.Add([WeakAccount, Generation, Completion = MoveTemp(Completion)](int32 Code, const FString& ResponseBody)
			{
				if (WeakAccount.IsValid() && WeakAccount->RequestGeneration == Generation && Completion)
					Completion(true, Code, ResponseBody);
			});
		}
		else if (Completion && Generation == RequestGeneration)
		{
			Completion(bAccepted, AutomationResponseCode, AutomationResponseBody);
		}
		return bAccepted;
	}
#endif

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	Request->SetTimeout(15.0f);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (!Body.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Request->SetContentAsString(Body);
	}
	if (bWithBearer && !AccessToken.IsEmpty()) Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AccessToken));
	TWeakObjectPtr<UMSOnlineAccountSubsystem> WeakThis(this);
	const TSharedRef<TFunction<void(bool, int32, const FString&)>> CompletionHolder = MakeShared<TFunction<void(bool, int32, const FString&)>>(MoveTemp(Completion));
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Generation, CompletionHolder](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
			if (!WeakThis.IsValid() || Generation != WeakThis->RequestGeneration) return;
			const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();
			if (*CompletionHolder) (*CompletionHolder)(bSucceeded, Code, ResponseBody);
		});
	if (!Request->ProcessRequest())
	{
		if (Generation == RequestGeneration && *CompletionHolder) (*CompletionHolder)(false, 0, FString());
		return false;
	}
	return true;
}

bool UMSOnlineAccountSubsystem::ParseAuthEnvelope(const FString& Body, FMSOnlineIdentity& OutIdentity,
	FMSOnlineDeckSnapshot& OutDeck, FString& OutError)
{
	const TSharedPtr<FJsonObject> Object = MSOnlineAccountPrivate::ParseObject(Body);
	if (!Object.IsValid())
	{
		OutError = TEXT("サーバー応答を読み取れませんでした。");
		return false;
	}
	OutIdentity = FMSOnlineIdentity();
	OutIdentity.RealmId = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("realm_id"));
	OutIdentity.AccountId = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("account_id"));
	OutIdentity.DisplayName = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("display_name"));
	OutIdentity.bIsGuest = MSOnlineAccountPrivate::JsonBoolOrFalse(Object, TEXT("is_guest"));
	const FString ParsedCurrentUsername = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("username"));
	const FString ParsedAccessToken = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("token"));
	const FString ParsedGuestResumeToken = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("guest_resume_token"));
	const FString ParsedExpiresAt = MSOnlineAccountPrivate::JsonStringOrNumber(Object, TEXT("expires_at"));
	const FString ParsedCatalogHash = MSOnlineAccountPrivate::JsonStringOrEmpty(Object, TEXT("catalog_hash"));
	if (OutIdentity.DisplayName.IsEmpty()) OutIdentity.DisplayName = ParsedCurrentUsername;
	if (OutIdentity.RealmId.IsEmpty() || OutIdentity.AccountId.IsEmpty() || ParsedAccessToken.IsEmpty())
	{
		OutError = TEXT("認証情報が不足しています。");
		return false;
	}
	const TSharedPtr<FJsonObject>* DeckObject = nullptr;
	if (!Object->TryGetObjectField(TEXT("deck"), DeckObject) || !DeckObject || !DeckObject->IsValid()
		|| !ParseDeck(*DeckObject, OutDeck, OutError))
	{
		if (OutError.IsEmpty()) OutError = TEXT("デッキ情報を読み取れませんでした。");
		return false;
	}
	CurrentUsername = ParsedCurrentUsername;
	AccessToken = ParsedAccessToken;
	GuestResumeToken = ParsedGuestResumeToken;
	ExpiresAt = ParsedExpiresAt;
	CatalogHash = ParsedCatalogHash;
	return true;
}

bool UMSOnlineAccountSubsystem::ParseDeck(const TSharedPtr<FJsonObject>& DeckObject,
	FMSOnlineDeckSnapshot& OutSnapshot, FString& OutError) const
{
	if (!MSOnline::ParseDeck(DeckObject, OutSnapshot))
	{
		OutError = TEXT("デッキのschema・field・型が不正です。");
		return false;
	}
	return true;
}

bool UMSOnlineAccountSubsystem::SerializeDeck(const FMSOnlineDeckSnapshot& Snapshot, FString& OutJson) const
{
	if (Snapshot.SchemaVersion != 3 || Snapshot.Slots.Num() != 4) return false;
	const TSharedRef<FJsonObject> DeckObject = MakeShared<FJsonObject>();
	DeckObject->SetNumberField(TEXT("schema_version"), Snapshot.SchemaVersion);
	DeckObject->SetNumberField(TEXT("revision"), static_cast<double>(Snapshot.Revision));
	DeckObject->SetNumberField(TEXT("selected_slot"), Snapshot.SelectedSlot);
	TArray<TSharedPtr<FJsonValue>> Slots;
	Slots.Reserve(4);
	for (const FMSOnlineDeckSlot& Slot : Snapshot.Slots)
	{
		if (Slot.WeaponIds.Num() != 3) return false;
		const TSharedRef<FJsonObject> SlotObject = MakeShared<FJsonObject>();
		SlotObject->SetField(TEXT("suit_id"), MSOnlineAccountPrivate::NullablePrimaryAssetId(Slot.SuitId));
		TArray<TSharedPtr<FJsonValue>> Weapons;
		Weapons.Reserve(3);
		for (const FPrimaryAssetId& WeaponId : Slot.WeaponIds) Weapons.Add(MSOnlineAccountPrivate::NullablePrimaryAssetId(WeaponId));
		SlotObject->SetArrayField(TEXT("weapon_ids"), MoveTemp(Weapons));
		SlotObject->SetBoolField(TEXT("shield_equipped"), Slot.bShieldEquipped);
		const auto Assist = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> ModuleIds, ACModuleIds;
		for (const auto& Id : Slot.BattleAssist.ModuleIds) ModuleIds.Add(MakeShared<FJsonValueString>(Id.ToString()));
		for (const auto& Id : Slot.BattleAssist.ACModuleIds) ACModuleIds.Add(MakeShared<FJsonValueString>(Id.ToString()));
		Assist->SetArrayField(TEXT("moduleIds"), ModuleIds);
		Assist->SetArrayField(TEXT("acModuleIds"), ACModuleIds);
		SlotObject->SetObjectField(TEXT("battleAssist"), Assist);
		Slots.Add(MakeShared<FJsonValueObject>(SlotObject));
	}
	DeckObject->SetArrayField(TEXT("slots"), MoveTemp(Slots));
	return MSOnlineAccountPrivate::SerializeObject(DeckObject, OutJson);
}

void UMSOnlineAccountSubsystem::SetLoginState(const EMSOnlineLoginState NewState)
{
	if (LoginState == NewState) return;
	LoginState = NewState;
	OnLoginStateChanged.Broadcast(NewState);
}

void UMSOnlineAccountSubsystem::ApplyIdentity(const FMSOnlineIdentity& NewIdentity, const FMSOnlineDeckSnapshot& NewDeck)
{
	if (Identity.bIsGuest && !NewIdentity.bIsGuest)
	{
		MSCredentialStore::RemoveGuest(GetCredentialDirectory(), GetGuestResumeServerScope(), Identity.RealmId);
		const FString OldSection = FString::Printf(TEXT("/Script/NeoMSGO.MSGuestResume.%s"), *GetGuestResumeSlot());
		GConfig->RemoveKey(*OldSection, TEXT("ResumeToken"), GGameUserSettingsIni);
		GConfig->RemoveKey(*OldSection, TEXT("ProtectedResumeToken"), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	Identity = NewIdentity;
	const FString NewKey = GetAccountCacheKey();
	++AuthGeneration;
	// Token rotation (including guest upgrade) invalidates old session handoffs.
	PendingGameTicket = FPendingGameTicket();
	CommittedDeck = NewDeck;
	AccountDeckCache.Add(NewKey, NewDeck);
	SaveCommittedDeckCache();
	// Deck/account listeners query HasOnlineSession() from their callbacks, so
	// publish the authenticated state before the identity-change notifications.
	SetLoginState(EMSOnlineLoginState::Authenticated);
	if (Identity.bIsGuest && !GuestResumeToken.IsEmpty()) SaveGuestResumeToken();
	OnAccountChanged.Broadcast(Identity, AuthGeneration);
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
	{
		Instance->SetAuthenticatedAccount(Identity, CurrentUsername);
	}
	OnDeckCommitted.Broadcast(NewDeck, AuthGeneration);
}

void UMSOnlineAccountSubsystem::ClearIdentity()
{
	const bool bHadSession = HasOnlineSession() || Identity.IsValid();
	++RequestGeneration;
	bDeckRequestPending = false;
	if (bHadSession) ++AuthGeneration;
	Identity = FMSOnlineIdentity();
	CommittedDeck = FMSOnlineDeckSnapshot();
	CurrentUsername.Reset();
	AccessToken.Reset();
	ExpiresAt.Reset();
	CatalogHash.Reset();
	GuestResumeToken.Reset();
	SetLoginState(EMSOnlineLoginState::LoggedOut);
	PendingGameTicket = FPendingGameTicket();
	OnAccountChanged.Broadcast(Identity, AuthGeneration);
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance())) Instance->ClearAuthenticatedAccount();
}

bool UMSOnlineAccountSubsystem::SaveGuestResumeToken()
{
	if (!Identity.bIsGuest || GuestResumeToken.IsEmpty()) return false;
	const FString ServerScope = GetGuestResumeServerScope();
	if (!MSCredentialStore::SaveGuest(GetCredentialDirectory(), ServerScope, Identity.RealmId, Identity.DisplayName, GuestResumeToken))
	{
		CredentialStorageWarning = TEXT("ゲスト再開情報を安全に保存できませんでした。終了前に通常アカウントへ登録してください。");
		return false;
	}
	// Only retire the matching old record after the dedicated encrypted file has
	// survived a physical read/decrypt. Do not change unrelated settings/realms.
	const FString Section = FString::Printf(TEXT("/Script/NeoMSGO.MSGuestResume.%s"), *GetGuestResumeSlot());
	FString StoredScope;
	if (GConfig->GetString(*Section, TEXT("ServerScope"), StoredScope, GGameUserSettingsIni) && StoredScope == ServerScope)
	{
		GConfig->RemoveKey(*Section, TEXT("ResumeToken"), GGameUserSettingsIni);
		GConfig->RemoveKey(*Section, TEXT("ProtectedResumeToken"), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	CredentialStorageWarning.Reset();
	return true;
}

void UMSOnlineAccountSubsystem::SaveCommittedDeckCache() const
{
	if (Identity.AccountId.IsEmpty() || Identity.RealmId.IsEmpty() || CommittedDeck.SchemaVersion != 3
		|| CommittedDeck.Slots.Num() != 4)
	{
		return;
	}
	FString DeckJson;
	if (!SerializeDeck(CommittedDeck, DeckJson)) return;
	UMSOnlineDeckCacheSaveGame* Save = Cast<UMSOnlineDeckCacheSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSOnlineDeckCacheSaveGame::StaticClass()));
	if (!Save) return;
	Save->RealmId = Identity.RealmId;
	Save->AccountId = Identity.AccountId;
	Save->SchemaVersion = 3;
	Save->Revision = CommittedDeck.Revision;
	Save->DeckJson = MoveTemp(DeckJson);
	const FString SlotName = FString::Printf(TEXT("NeoMSGO_OnlineDeck_%08x"), GetTypeHash(GetAccountCacheKey()));
	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}

bool UMSOnlineAccountSubsystem::LoadGuestResumeToken(FString& OutToken, FString& OutDisplayName)
{
	OutToken.Reset();
	OutDisplayName.Reset();
	CredentialStorageWarning.Reset();
	const FString ServerScope = GetGuestResumeServerScope();
	FString Realm;
	const MSCredentialStore::ELoadResult LoadResult = MSCredentialStore::LoadGuest(
		GetCredentialDirectory(), ServerScope, Realm, OutDisplayName, OutToken);
	if (LoadResult == MSCredentialStore::ELoadResult::Success) return true;
	if (LoadResult == MSCredentialStore::ELoadResult::Failed)
	{
		CredentialStorageWarning = TEXT("保存されたゲスト情報を安全に復元できません。このWindowsユーザーでの再認証が必要です。");
		return false; // Corruption/decryption failure must not select another old account.
	}

	// One-time migration of an exact server-scoped development record. The old
	// index is never used once a dedicated credential envelope exists.
	TArray<FString> Slots;
	GConfig->GetArray(MSOnlineAccountPrivate::GuestResumeIndexSection, TEXT("Slots"), Slots, GGameUserSettingsIni);
	for (const FString& Slot : Slots)
	{
		const FString Section = FString::Printf(TEXT("%s%s"), MSOnlineAccountPrivate::GuestResumeSectionPrefix, *Slot);
		FString StoredScope;
		if (!GConfig->GetString(*Section, TEXT("ServerScope"), StoredScope, GGameUserSettingsIni) || StoredScope != ServerScope) continue;
		FString OldRealm, Name, Protected, LegacySecret;
		GConfig->GetString(*Section, TEXT("RealmId"), OldRealm, GGameUserSettingsIni);
		GConfig->GetString(*Section, TEXT("DisplayName"), Name, GGameUserSettingsIni);
		GConfig->GetString(*Section, TEXT("ProtectedResumeToken"), Protected, GGameUserSettingsIni);
		const bool bHadPlaintext = GConfig->GetString(*Section, TEXT("ResumeToken"), LegacySecret, GGameUserSettingsIni);
		if (bHadPlaintext)
		{
			GConfig->RemoveKey(*Section, TEXT("ResumeToken"), GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
		if (!Protected.IsEmpty())
		{
			if (!MSCredentialStore::Unprotect(Protected, StoredScope + TEXT("|") + OldRealm, LegacySecret))
			{
				CredentialStorageWarning = TEXT("以前のゲスト情報を安全に復元できません。");
				return false;
			}
		}
		if (LegacySecret.IsEmpty()) continue; // Record retired by an earlier upgrade.
		if (!MSCredentialStore::SaveGuest(GetCredentialDirectory(), ServerScope, OldRealm, Name, LegacySecret)
			|| MSCredentialStore::LoadGuest(GetCredentialDirectory(), ServerScope, Realm, OutDisplayName, OutToken) != MSCredentialStore::ELoadResult::Success)
		{
			CredentialStorageWarning = TEXT("ゲスト情報を安全な保存形式へ移行できませんでした。");
			OutToken.Reset(); OutDisplayName.Reset();
			return false;
		}
		GConfig->RemoveKey(*Section, TEXT("ProtectedResumeToken"), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		return true;
	}
	return false;
}

FString UMSOnlineAccountSubsystem::GetCredentialDirectory() const
{
	// ProjectSavedDir follows the process -UserDir override used by separate clients.
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineCredentials"));
}

FString UMSOnlineAccountSubsystem::GetGuestResumeServerScope() const
{
	FString ServerId;
	FParse::Value(FCommandLine::Get(), TEXT("MSResumeServerId="), ServerId);
	if (ServerId.IsEmpty()) FParse::Value(FCommandLine::Get(), TEXT("MSServerId="), ServerId);
	ServerId.TrimStartAndEndInline();
	if (ServerId.IsEmpty()) ServerId = TEXT("home");
	return AuthBaseUrl.ToLower() + TEXT("|") + ServerId.ToLower();
}

FString UMSOnlineAccountSubsystem::GetGuestResumeSlot() const
{
	FString Scope = GetGuestResumeServerScope();
	if (!Identity.RealmId.IsEmpty()) Scope += TEXT("|") + Identity.RealmId.ToLower();
	return FString::Printf(TEXT("%08x"), GetTypeHash(Scope));
}

FString UMSOnlineAccountSubsystem::GetAccountCacheKey() const
{
	if (Identity.AccountId.IsEmpty()) return FString();
	return Identity.RealmId.ToLower() + TEXT("/") + Identity.AccountId.ToLower();
}

FString UMSOnlineAccountSubsystem::BuildUrl(const FString& Path) const
{
	FString Base = AuthBaseUrl;
	Base.TrimStartAndEndInline();
	while (Base.EndsWith(TEXT("/"))) Base.LeftChopInline(1);
	FString Suffix = Path;
	while (Suffix.StartsWith(TEXT("/"))) Suffix.RightChopInline(1);
	return Base.IsEmpty() ? FString() : Base + TEXT("/") + Suffix;
}

#if WITH_DEV_AUTOMATION_TESTS
bool UMSOnlineAccountSubsystem::CompleteNextResponseForAutomationTests(int32 Code, const FString& Body)
{
	if (DeferredResponsesForAutomationTests.IsEmpty()) return false;
	TFunction<void(int32, const FString&)> Callback = MoveTemp(DeferredResponsesForAutomationTests[0]);
	DeferredResponsesForAutomationTests.RemoveAt(0);
	Callback(Code, Body);
	return true;
}

void UMSOnlineAccountSubsystem::SetIdentityForAutomationTests(const FMSOnlineIdentity& InIdentity)
{
	Identity = InIdentity;
	CommittedDeck = FMSOnlineDeckSnapshot();
	++AuthGeneration;
	SetLoginState(Identity.AccountId.IsEmpty() ? EMSOnlineLoginState::LoggedOut : EMSOnlineLoginState::Authenticated);
}

void UMSOnlineAccountSubsystem::SetDeckForAutomationTests(const FMSOnlineDeckSnapshot& InSnapshot)
{
	CommittedDeck = InSnapshot;
	AccountDeckCache.Add(GetAccountCacheKey(), InSnapshot);
}

void UMSOnlineAccountSubsystem::SetPendingTicketForAutomationTests(const FString& ServerId, const FString& GameAddress,
	const FString& Ticket)
{
	PendingGameTicket.ServerId = ServerId;
	PendingGameTicket.GameAddress = GameAddress;
	PendingGameTicket.Ticket = Ticket;
	PendingGameTicket.ExpiresAtMs = static_cast<int64>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000 + 60000;
	PendingGameTicket.Generation = AuthGeneration;
	PendingGameTicket.bConsumed = false;
}
#endif
