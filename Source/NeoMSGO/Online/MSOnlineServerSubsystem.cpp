#include "Online/MSOnlineServerSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "IPAddress.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSGameMode.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Data/MSBattleAssistRules.h"
#include "UI/MSDeckSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Core/MSTeamTypes.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AI/MSBotCharacter.h"
#include "EngineUtils.h"
#include "Player/MSPlayerCharacter.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	FString Encode(const TSharedRef<FJsonObject>& Object)
	{
		FString Body; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Body)); return Body;
	}
	TSharedPtr<FJsonObject> Decode(const FString& Body)
	{
		TSharedPtr<FJsonObject> Object;
		if (Body.Len() > 262144 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object)) return nullptr;
		return Object;
	}
	bool ReadId(const TSharedPtr<FJsonValue>& Value, FPrimaryAssetId& Out)
	{
		Out = FPrimaryAssetId();
		if (!Value) return false;
		if (Value->Type == EJson::Null) return true;
		FString Text;
		if (!Value->TryGetString(Text) || Text.Len() > 256) return false;
		Out = FPrimaryAssetId(Text);
		return Out.IsValid() && Out.ToString() == Text;
	}
}

bool MSOnline::IsEnabled()
{
	FString Mode;
	if (FParse::Value(FCommandLine::Get(), TEXT("MSAuthMode="), Mode))
	{
		return Mode.Equals(TEXT("Online"), ESearchCase::IgnoreCase);
	}
	return GetDefault<UMSOnlineAccountSubsystem>()->GetAuthMode() == EMSOnlineAuthMode::Online;
}

FString MSOnline::GetAuthBaseUrl()
{
	FString BaseUrl = GetDefault<UMSOnlineAccountSubsystem>()->GetAuthBaseUrl();
	FParse::Value(FCommandLine::Get(), TEXT("MSAuthBaseUrl="), BaseUrl);
	BaseUrl.TrimStartAndEndInline();
	return BaseUrl;
}

bool MSOnline::ParseDeck(const TSharedPtr<FJsonObject>& Object, FMSOnlineDeckSnapshot& Out)
{
	if (!Object) return false;
	for (const auto& Field : Object->Values)
		if (Field.Key != TEXT("schema_version") && Field.Key != TEXT("revision") && Field.Key != TEXT("selected_slot") && Field.Key != TEXT("slots")) return false;
	double Schema = 0, Revision = 0, Selected = 0;
	const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
	// FJsonValueString::TryGetNumber accepts numeric text; the wire schema does not.
	for (const TCHAR* Key : { TEXT("schema_version"), TEXT("revision"), TEXT("selected_slot") })
	{
		const auto Value = Object->TryGetField(Key);
		if (!Value.IsValid() || Value->Type != EJson::Number) return false;
	}
	if (!Object->TryGetNumberField(TEXT("schema_version"), Schema) || (Schema != 1 && Schema != 2 && Schema != 3) ||
		!Object->TryGetNumberField(TEXT("revision"), Revision) || Revision < 0 || Revision > 9007199254740991.0 || FMath::FloorToDouble(Revision) != Revision ||
		!Object->TryGetNumberField(TEXT("selected_slot"), Selected) || Selected < 0 || Selected > 3 || FMath::FloorToDouble(Selected) != Selected ||
		!Object->TryGetArrayField(TEXT("slots"), Slots) || !Slots || Slots->Num() != 4) return false;
	FMSOnlineDeckSnapshot Result; Result.SchemaVersion = static_cast<int32>(Schema); Result.Revision = static_cast<int64>(Revision); Result.SelectedSlot = static_cast<int32>(Selected);
	for (int32 S = 0; S < 4; ++S)
	{
		const TSharedPtr<FJsonObject>* Slot = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Weapons = nullptr;
		if (!(*Slots)[S].IsValid() || !(*Slots)[S]->TryGetObject(Slot) || !Slot || !Slot->IsValid() ||
			!ReadId((*Slot)->TryGetField(TEXT("suit_id")), Result.Slots[S].SuitId) ||
			!(*Slot)->TryGetArrayField(TEXT("weapon_ids"), Weapons) || !Weapons || Weapons->Num() != 3) return false;
		for (const auto& Field : (*Slot)->Values)
			if (Field.Key != TEXT("suit_id") && Field.Key != TEXT("weapon_ids")
				&& !(Schema >= 2 && Field.Key == TEXT("shield_equipped"))
				&& !(Schema == 3 && Field.Key == TEXT("battleAssist"))) return false;
		if (Schema == 3)
		{
			const TSharedPtr<FJsonObject>* Assist = nullptr;
			if (!(*Slot)->TryGetObjectField(TEXT("battleAssist"), Assist) || !Assist || !Assist->IsValid() || (*Assist)->Values.Num() != 2) return false;
			for (int32 Kind = 0; Kind < 2; ++Kind)
			{
				const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
				if (!(*Assist)->TryGetArrayField(Kind == 0 ? TEXT("moduleIds") : TEXT("acModuleIds"), Ids) || !Ids || Ids->Num() > 10) return false;
				TArray<FPrimaryAssetId>& OutIds = Kind == 0 ? Result.Slots[S].BattleAssist.ModuleIds : Result.Slots[S].BattleAssist.ACModuleIds;
				for (const auto& Value : *Ids)
				{
					FPrimaryAssetId Id;
					if (!ReadId(Value, Id) || !Id.IsValid() || Id.PrimaryAssetType != FPrimaryAssetType(TEXT("MSBattleAssistModuleData")) || OutIds.Contains(Id)) return false;
					OutIds.Add(Id);
				}
			}
			for (const FPrimaryAssetId& Id : Result.Slots[S].BattleAssist.ACModuleIds)
				if (Result.Slots[S].BattleAssist.ModuleIds.Contains(Id)) return false;
			if (!Result.Slots[S].BattleAssist.ACModuleIds.IsEmpty()) return false;
		}
		if (Schema >= 2)
		{
			const TSharedPtr<FJsonValue> ShieldValue = (*Slot)->TryGetField(TEXT("shield_equipped"));
			if (!ShieldValue.IsValid() || ShieldValue->Type != EJson::Boolean
				|| !(*Slot)->TryGetBoolField(TEXT("shield_equipped"), Result.Slots[S].bShieldEquipped)) return false;
		}
		if (Result.Slots[S].SuitId.IsValid() && Result.Slots[S].SuitId.PrimaryAssetType != UMSMobileSuitData::PrimaryAssetType) return false;
		TSet<FPrimaryAssetId> SeenWeapons;
		for (int32 W = 0; W < 3; ++W)
		{
			FPrimaryAssetId& Id = Result.Slots[S].WeaponIds[W];
			if (!ReadId((*Weapons)[W], Id)) return false;
			if (Id.IsValid())
			{
				if (!Result.Slots[S].SuitId.IsValid() || Id.PrimaryAssetType != UMSWeaponDataBase::PrimaryAssetType || SeenWeapons.Contains(Id)) return false;
				SeenWeapons.Add(Id);
			}
		}
		if (!Result.Slots[S].SuitId.IsValid() && (Result.Slots[S].bShieldEquipped || !Result.Slots[S].BattleAssist.IsEmpty())) return false;
	}
	Out = MoveTemp(Result); return true;
}

bool MSOnline::ResolveSlot(const FMSOnlineDeckSlot& Slot, UMSMobileSuitData*& Suit, FMSDeckLoadout& Loadout,
	const bool bLegacySchema)
{
	Suit = nullptr;
	Loadout = FMSDeckLoadout();
	if (Slot.WeaponIds.Num() != 3 || (bLegacySchema && !Slot.BattleAssist.IsEmpty())) return false;
	if (!Slot.SuitId.IsValid())
	{
		Loadout.bShieldEquipped = false;
		return !Slot.WeaponIds.ContainsByPredicate([](const FPrimaryAssetId& Id) { return Id.IsValid(); })
			&& (bLegacySchema || !Slot.bShieldEquipped) && Slot.BattleAssist.IsEmpty();
	}
	if (Slot.SuitId.PrimaryAssetType != UMSMobileSuitData::PrimaryAssetType) return false;
	UAssetManager& Manager = UAssetManager::Get();
	Suit = Cast<UMSMobileSuitData>(Manager.GetPrimaryAssetPath(Slot.SuitId).TryLoad());
	if (!Suit || Suit->GetPrimaryAssetId() != Slot.SuitId || Suit->Cost < 0) return false;
	FMSBattleAssistResolvedLoadout Resolved;
	FMSBattleAssistValidationResult Error;
	if (!MSBattleAssist::ResolveLoadout(Suit, Slot.BattleAssist, Resolved, Error)) return false;
	Loadout.BattleAssist = Slot.BattleAssist;
	Loadout.EquippedWeapons.SetNum(3);
	UMSShieldWeaponData* NativeShield = Suit->GetNativeShield();
	if (!bLegacySchema)
	{
		Loadout.bShieldEquipped = Slot.bShieldEquipped;
		if (Loadout.bShieldEquipped && !NativeShield) return false;
		if (!Loadout.bShieldEquipped && !Suit->bShieldRemovable) return false;
	}
	else
	{
		Loadout.bShieldEquipped = false;
	}
	TSet<FPrimaryAssetId> Seen;
	int32 LegacyShieldCount = 0;
	for (int32 W = 0; W < 3; ++W)
	{
		const FPrimaryAssetId& Id = Slot.WeaponIds[W];
		Loadout.EquippedWeapons[W] = nullptr;
		if (!Id.IsValid()) continue;
		if (Id.PrimaryAssetType != UMSWeaponDataBase::PrimaryAssetType || Seen.Contains(Id)) return false;
		UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		if (!Weapon || Weapon->GetPrimaryAssetId() != Id) return false;
		if (UMSShieldWeaponData* Shield = Cast<UMSShieldWeaponData>(Weapon))
		{
			if (!bLegacySchema || ++LegacyShieldCount > 1 || !NativeShield || NativeShield != Shield) return false;
			Loadout.bShieldEquipped = true;
			continue;
		}
		Seen.Add(Id); Loadout.EquippedWeapons[W] = Weapon;
	}
	if (bLegacySchema && !Loadout.bShieldEquipped && !Suit->bShieldRemovable) return false;
	return true;
}

bool MSOnline::ValidateDeck(const FMSOnlineDeckSnapshot& Deck)
{
	if ((Deck.SchemaVersion < 1 || Deck.SchemaVersion > 3) || Deck.Revision < 0 || Deck.Slots.Num() != 4 || !Deck.Slots.IsValidIndex(Deck.SelectedSlot)) return false;
	int64 Cost = 0;
	for (const FMSOnlineDeckSlot& Slot : Deck.Slots)
	{
		if (Deck.SchemaVersion < 3 && !Slot.BattleAssist.IsEmpty()) return false;
		UMSMobileSuitData* Suit; FMSDeckLoadout Loadout;
		if (!ResolveSlot(Slot, Suit, Loadout, Deck.SchemaVersion == 1)) return false;
		if (Suit) Cost += Suit->Cost;
	}
	return Cost <= 1720;
}

void UMSOnlineServerSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!MSOnline::IsServerWorld(&World) || World.GetNetMode() == NM_Client) return;
	if (World.GetNetMode() == NM_ListenServer && World.GetGameInstance())
	{
		const auto* Rooms = World.GetGameInstance()->GetSubsystem<UMSRoomSubsystem>();
		if (Rooms && Rooms->GetHostedSession().IsValid())
		{
			const auto& Session = Rooms->GetHostedSession();
			ServerId = Session.RoomId; RoomInstanceId = Session.InstanceId; RoomLease = Session.Lease;
			BaseUrl = Session.BaseUrl; CatalogHash = Session.CatalogHash; RealmId = Session.RealmId;
			const auto Address = World.GetNetDriver() ? World.GetNetDriver()->GetLocalAddr() : nullptr;
			if (!Address.IsValid() || Address->GetPort() != Session.GamePort)
			{
				StartupError = FText::FromString(TEXT("ルーム用ポートが使用中です。起動中のルームを終了してから作成してください。"));
				return;
			}
			bConfigured = World.GetNetDriver() != nullptr && !CatalogHash.IsEmpty() && !RealmId.IsEmpty();
			Heartbeat();
			RoomHeartbeatTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMSOnlineServerSubsystem::TickHeartbeat), 5);
			return;
		}
	}
	FString KeyFile, CatalogFile, CatalogBody;
	BaseUrl = MSOnline::GetAuthBaseUrl();
	while (BaseUrl.EndsWith(TEXT("/"))) BaseUrl.LeftChopInline(1);
	if (!FParse::Value(FCommandLine::Get(), TEXT("MSServerId="), ServerId) ||
		!FParse::Value(FCommandLine::Get(), TEXT("MSServerKeyFile="), KeyFile) ||
		!FParse::Value(FCommandLine::Get(), TEXT("MSCatalogFile="), CatalogFile) ||
		!FFileHelper::LoadFileToString(ServerKey, *KeyFile) || !FFileHelper::LoadFileToString(CatalogBody, *CatalogFile))
 { UE_LOG(LogTemp, Warning, TEXT("Online server configuration incomplete; admission disabled")); return; }
	ServerKey.TrimStartAndEndInline();
	const auto Catalog = Decode(CatalogBody);
	if (ServerKey.Len() < 32 || ServerKey.Contains(TEXT("\n")) || ServerKey.Contains(TEXT("\r")) || !Catalog ||
		!Catalog->TryGetStringField(TEXT("catalog_hash"), CatalogHash) || CatalogHash.IsEmpty()) return;
	bConfigured = true;
	Heartbeat();
	World.GetTimerManager().SetTimer(HeartbeatTimer, this, &UMSOnlineServerSubsystem::Heartbeat, 5.0f, true);
}

void UMSOnlineServerSubsystem::Deinitialize()
{
	bConfigured = false; bHeartbeatReady = false; ServerKey.Reset(); RoomLease.Reset();
	FTSTicker::RemoveTicker(RoomHeartbeatTicker); RoomHeartbeatTicker.Reset();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(HeartbeatTimer);
	Accounts.Reset(); Super::Deinitialize();
}

void UMSOnlineServerSubsystem::Heartbeat()
{
	if (!bConfigured || bHeartbeatPending) return;
	auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("server_id"), ServerId);
	Body->SetStringField(TEXT("catalog_hash"), CatalogHash);
	int32 Players = 0; for (const auto& Pair : Accounts) if (Pair.Value.ActiveController.IsValid()) ++Players;
	Body->SetNumberField(TEXT("players"), Players);
	const bool bRoom = !RoomInstanceId.IsEmpty();
	if (bRoom)
	{
		const auto* Rooms = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UMSRoomSubsystem>() : nullptr;
		if (!Rooms || Rooms->GetHostedSession().InstanceId != RoomInstanceId) { bHeartbeatReady = false; return; }
		Body->SetStringField(TEXT("instance_id"), RoomInstanceId);
		Body->SetStringField(TEXT("build_id"), MSRoomProtocol::BuildId);
		// Filling BOTs are replaceable seats, not fixed reserved player slots.
		// Before first deployment there may be none; afterwards report the actual
		// population, including dead BOTs waiting on their existing respawn timer.
		int32 BotPlayers = Rooms->GetRoomSettings().BotCount;
		if (Rooms->GetRoomSettings().bFillTeamsWithBots)
		{
			BotPlayers = 0;
			for (TActorIterator<AMSBotCharacter> It(GetWorld()); It; ++It)
				if (IsValid(*It)) ++BotPlayers;
		}
		auto Population = MakeShared<FJsonObject>();
		Population->SetNumberField(TEXT("humans"), Players);
		Population->SetNumberField(TEXT("bots"), BotPlayers);
		Body->SetObjectField(TEXT("players"), Population);
		const auto* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>();
		Body->SetStringField(TEXT("state"), Rooms->IsHostAdmitted() && GetWorld()->GetNetDriver()
			? (Mode && Mode->HasOnlineBattleStarted() ? TEXT("playing") : TEXT("ready")) : TEXT("starting"));
	}
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + (bRoom ? TEXT("/v1/rooms/") + ServerId + TEXT("/heartbeat") : FString(TEXT("/v1/server/heartbeat"))));
	Request->SetVerb(TEXT("POST")); Request->SetTimeout(8.0f);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(bRoom ? TEXT("X-MS-Room-Lease") : TEXT("X-NeoMSGO-Server-Key"), bRoom ? RoomLease : ServerKey);
	Request->SetContentAsString(Encode(Body)); bHeartbeatPending = true;
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		bHeartbeatPending = false;
		const auto Object = bOk && Response && Response->GetResponseCode() == 200 ? Decode(Response->GetContentAsString()) : nullptr;
		FString Realm, Hash; bool Accepted = false;
		bHeartbeatReady = bConfigured && Object && Object->TryGetBoolField(TEXT("ok"), Accepted) && Accepted &&
			Object->TryGetStringField(TEXT("realm_id"), Realm) && !Realm.IsEmpty() &&
			Object->TryGetStringField(TEXT("catalog_hash"), Hash) && Hash == CatalogHash && (RealmId.IsEmpty() || RealmId == Realm);
		if (bHeartbeatReady) RealmId = Realm;
		else UE_LOG(LogTemp, Warning, TEXT("Online server heartbeat rejected/unavailable; admission disabled (HTTP %d)"), Response ? Response->GetResponseCode() : 0);
	});
	if (!Request->ProcessRequest()) { bHeartbeatPending = false; bHeartbeatReady = false; }
}

bool UMSOnlineServerSubsystem::TickHeartbeat(float)
{
	if (!bConfigured) return false;
	Heartbeat(); return true;
}

void UMSOnlineServerSubsystem::ConsumeTicket(AMSPlayerController* Controller, const FString& Ticket, const FString& ConnectionId)
{
	if (!Controller || !IsReady()) { if (Controller) Controller->RejectOnlineAuthentication(TEXT("server_unavailable")); return; }
	auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("server_id"), ServerId);
	Body->SetStringField(TEXT("catalog_hash"), CatalogHash); Body->SetStringField(TEXT("ticket"), Ticket);
	Body->SetStringField(TEXT("connection_id"), ConnectionId);
	const bool bRoom = !RoomInstanceId.IsEmpty();
	if (bRoom) { Body->SetStringField(TEXT("instance_id"), RoomInstanceId); Body->SetStringField(TEXT("build_id"), MSRoomProtocol::BuildId); }
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + (bRoom ? TEXT("/v1/rooms/") + ServerId + TEXT("/consume-ticket") : FString(TEXT("/v1/server/tickets/consume"))));
	Request->SetVerb(TEXT("POST")); Request->SetTimeout(10.0f);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(bRoom ? TEXT("X-MS-Room-Lease") : TEXT("X-NeoMSGO-Server-Key"), bRoom ? RoomLease : ServerKey);
	Request->SetContentAsString(Encode(Body));
	TWeakObjectPtr<AMSPlayerController> WeakController(Controller);
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this, WeakController, ConnectionId](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		AMSPlayerController* PC = WeakController.Get();
		if (!PC || PC->GetWorld() != GetWorld() || !PC->IsAuthenticationPending(ConnectionId) || !bConfigured) return;
		const auto Object = bOk && Response && Response->GetResponseCode() == 200 ? Decode(Response->GetContentAsString()) : nullptr;
		FMSOnlineIdentity Identity; FMSOnlineDeckSnapshot Deck; FString Hash, Connection;
		if (!RoomInstanceId.IsEmpty())
		{
			FString RoomId, Instance;
			if (!Object || !Object->TryGetStringField(TEXT("room_id"), RoomId) || RoomId != ServerId ||
				!Object->TryGetStringField(TEXT("instance_id"), Instance) || Instance != RoomInstanceId)
			{ PC->RejectOnlineAuthentication(TEXT("room_instance_rejected")); return; }
		}
		const TSharedPtr<FJsonObject>* DeckObject;
		if (!Object || !Object->TryGetStringField(TEXT("account_id"), Identity.AccountId) ||
			!Object->TryGetStringField(TEXT("realm_id"), Identity.RealmId) || !Identity.IsValid() || Identity.RealmId != RealmId ||
			!Object->TryGetStringField(TEXT("display_name"), Identity.DisplayName) ||
			!Object->TryGetBoolField(TEXT("is_guest"), Identity.bIsGuest) ||
			!Object->TryGetStringField(TEXT("connection_id"), Connection) || Connection != ConnectionId ||
			!Object->TryGetStringField(TEXT("catalog_hash"), Hash) || Hash != CatalogHash ||
			!Object->TryGetObjectField(TEXT("deck"), DeckObject) || !MSOnline::ParseDeck(*DeckObject, Deck) || !MSOnline::ValidateDeck(Deck))
		{ PC->RejectOnlineAuthentication(TEXT("ticket_or_catalog_rejected")); return; }
		if (!Admit(PC, Identity, Deck)) { PC->RejectOnlineAuthentication(TEXT("room_full_or_account_already_connected")); return; }
		PC->CompleteOnlineAuthentication(Identity, Deck);
	});
	if (!Request->ProcessRequest()) Controller->RejectOnlineAuthentication(TEXT("authentication_unavailable"));
}

FMSOnlineMatchAccount* UMSOnlineServerSubsystem::FindAccount(const FMSOnlineIdentity& Identity)
{
	return Accounts.Find(Identity.RealmId + TEXT(":") + Identity.AccountId);
}

int32 UMSOnlineServerSubsystem::CountActivePlayers(const FGenericTeamId TeamId, const AMSPlayerController* Excluding) const
{
	int32 Count = 0;
	for (const auto& Pair : Accounts)
		if (Pair.Value.ActiveController.IsValid() && Pair.Value.ActiveController.Get() != Excluding
			&& Pair.Value.TeamId == TeamId.GetId()) ++Count;
	return Count;
}

void UMSOnlineServerSubsystem::RefreshTeamBots()
{
	if (const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AMSGameMode>() : nullptr)
		if (auto* Manager = Mode->GetBotSpawnManager()) Manager->RefreshTeamPopulation();
}

bool UMSOnlineServerSubsystem::Admit(AMSPlayerController* Controller, const FMSOnlineIdentity& Identity, FMSOnlineDeckSnapshot& Deck)
{
	const FString Key = Identity.RealmId + TEXT(":") + Identity.AccountId;
	// Tickets may race while the discovery heartbeat still reports a free seat.
	// The game thread serializes this final check and the following account insertion.
	int32 ActiveHumans = 0;
	for (const auto& Pair : Accounts) if (Pair.Value.ActiveController.IsValid() && Pair.Value.ActiveController.Get() != Controller) ++ActiveHumans;
	const auto* State = GetWorld() ? GetWorld()->GetGameState<AMSGameState>() : nullptr;
	const bool bFill = State && State->bPlayerHostedRoom && State->HostedRoomSettings.bFillTeamsWithBots;
	const int32 Bots = State && State->bPlayerHostedRoom && !bFill ? State->HostedRoomSettings.BotCount : 0;
	const int32 Capacity = bFill ? MSRoomPopulation::MaxCombatants : MSRoomPopulation::LegacyMaxCombatants;
	if (!Controller || ActiveHumans + Bots >= Capacity) return false;
	if (FMSOnlineMatchAccount* Existing = Accounts.Find(Key))
	{
		if (Existing->ActiveController.IsValid() && Existing->ActiveController.Get() != Controller) return false;
		if (bFill && CountActivePlayers(FGenericTeamId(Existing->TeamId), Controller) >= MSRoomPopulation::TeamSize) return false;
		Deck = Existing->Deck; Existing->ActiveController = Controller;
		if (Existing->bWithdrawnForBotFill)
		{
			// Keep health, weapons and respawn deadlines on reconnect, while the
			// disconnected pawn never occupies an extra combat slot beside a BOT.
			if (auto* Pawn = Cast<AMSPlayerCharacter>(Existing->RetainedPawn.Get()))
			{
				const bool bAlive = Pawn->GetVitalSet() && Pawn->GetVitalSet()->GetArmor() > 0;
				Pawn->SetGenericTeamId(FGenericTeamId(Existing->TeamId));
				Pawn->SetActorHiddenInGame(Existing->bRetainedWasHidden || !bAlive);
				Pawn->SetActorEnableCollision(Existing->bRetainedHadCollision && bAlive);
				Pawn->GetCharacterMovement()->SetMovementMode(bAlive
					? static_cast<EMovementMode>(Existing->RetainedMovementMode) : MOVE_None);
				Pawn->ForceNetUpdate();
			}
			Existing->bWithdrawnForBotFill = false;
		}
	}
	else
	{
		FString TeamMode; FParse::Value(FCommandLine::Get(), TEXT("MSTeamMode="), TeamMode);
		FGenericTeamId Team = TeamMode.Equals(TEXT("Coop"), ESearchCase::IgnoreCase)
			|| (bFill ? CountActivePlayers(MSTeam::PlayerTeamId()) <= CountActivePlayers(MSTeam::EnemyTeamId()) : Accounts.Num() % 2 == 0)
			? MSTeam::PlayerTeamId() : MSTeam::EnemyTeamId();
		if (bFill && CountActivePlayers(Team, Controller) >= MSRoomPopulation::TeamSize) return false;
		FMSOnlineMatchAccount& NewAccount = Accounts.Add(Key); NewAccount.Deck = Deck; NewAccount.ActiveController = Controller;
		NewAccount.TeamId = Team.GetId();
	}
	RefreshTeamBots();
	return true;
}

void UMSOnlineServerSubsystem::Release(AMSPlayerController* Controller)
{
	if (!Controller || !GetWorld()) return;
	FMSOnlineMatchAccount* Account = nullptr;
	for (auto& Pair : Accounts) if (Pair.Value.ActiveController.Get() == Controller) { Account = &Pair.Value; break; }
	if (!Account) return;
	Account->ActiveController.Reset();
	if (Controller->GetPawn()) Account->RetainedPawn = Controller->GetPawn();
	const auto* State = GetWorld()->GetGameState<AMSGameState>();
	if (State && State->bPlayerHostedRoom && State->HostedRoomSettings.bFillTeamsWithBots)
	{
		if (auto* Pawn = Cast<AMSPlayerCharacter>(Account->RetainedPawn.Get()))
		{
			Account->bWithdrawnForBotFill = true;
			Account->bRetainedWasHidden = Pawn->IsHidden();
			Account->bRetainedHadCollision = Pawn->GetActorEnableCollision();
			Account->RetainedMovementMode = Pawn->GetCharacterMovement()->MovementMode;
			Pawn->SuspendOnlineInputs();
			Pawn->SetGenericTeamId(FGenericTeamId::NoTeam);
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
			Pawn->GetCharacterMovement()->DisableMovement();
			Pawn->ForceNetUpdate();
		}
	}
	RefreshTeamBots();
}

bool MSOnline::IsServerWorld(const UWorld* World)
{
 FString Id;
 return IsEnabled() && World && (World->GetNetMode() != NM_Standalone || FParse::Value(FCommandLine::Get(), TEXT("MSServerId="), Id));
}
