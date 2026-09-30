// Copyright NeoMSGO Project. All Rights Reserved.

#include "Online/MSRoomSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Gameplay/MSPlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Core/MSGameInstance.h"
#include "Data/MSMapDefinitionRow.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GameFramework/PlayerController.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "NeoMSGO.h"
#include "Online/MSRoomDescriptor.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

// Direct-IP rooms deliberately avoid OnlineSubsystem/session dependencies.

namespace
{
	/**
	 * The room API added this field after the legacy 12-seat contract. Missing is
	 * intentionally treated as legacy false; a present value must be a JSON bool.
	 */
	bool TryReadOptionalFillFlag(
		const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName,
		bool& OutValue, bool& bOutPresent)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		if (!Object->HasField(FieldName))
		{
			return true;
		}
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(FieldName);
		if (!Value.IsValid() || Value->Type != EJson::Boolean)
		{
			return false;
		}
		bOutPresent = true;
		return Object->TryGetBoolField(FieldName, OutValue);
	}

	bool TryReadFillFlagFromResponse(
		const TSharedPtr<FJsonObject>& Root,
		const TSharedPtr<FJsonObject>& Settings,
		bool& OutValue)
	{
		bool bRootValue = false;
		bool bSettingsValue = false;
		bool bRootPresent = false;
		bool bSettingsPresent = false;
		if (!TryReadOptionalFillFlag(Root, TEXT("fill_teams_with_bots"), bRootValue, bRootPresent)
			|| !TryReadOptionalFillFlag(Settings, TEXT("fill_teams_with_bots"), bSettingsValue, bSettingsPresent))
		{
			return false;
		}
		// Some adapters return the same setting at the top level and under
		// settings. Refuse contradictory responses instead of guessing.
		if (bRootPresent && bSettingsPresent && bRootValue != bSettingsValue)
		{
			return false;
		}
		OutValue = bSettingsPresent ? bSettingsValue : bRootPresent ? bRootValue : false;
		return true;
	}
}

void UMSRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMSOnlineAccountSubsystem>();
	if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>()) { Account->OnAccountChanged.AddDynamic(this, &UMSRoomSubsystem::HandleOnlineAccountChanged); Account->OnGameTicketReady.AddDynamic(this, &UMSRoomSubsystem::HandleOnlineTicketReady); Account->OnRequestFinished.AddDynamic(this, &UMSRoomSubsystem::HandleOnlineRequestFinished); }
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMSRoomSubsystem::HandlePostLoadMap);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UMSRoomSubsystem::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UMSRoomSubsystem::HandleTravelFailure);
	}
	OnlineRoomTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMSRoomSubsystem::TickOnlineRoom), 0.25f);
}

void UMSRoomSubsystem::Deinitialize()
{
	if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>()) { Account->OnAccountChanged.RemoveDynamic(this, &UMSRoomSubsystem::HandleOnlineAccountChanged); Account->OnGameTicketReady.RemoveDynamic(this, &UMSRoomSubsystem::HandleOnlineTicketReady); Account->OnRequestFinished.RemoveDynamic(this, &UMSRoomSubsystem::HandleOnlineRequestFinished); }
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	if (GEngine) { GEngine->OnNetworkFailure().RemoveAll(this); GEngine->OnTravelFailure().RemoveAll(this); }
	FTSTicker::RemoveTicker(OnlineRoomTicker); OnlineRoomTicker.Reset();
	CloseOnlineRoom();
	EndRegistryHosting();
	Super::Deinitialize();
}

bool UMSRoomSubsystem::HostRoom(const FMSRoomSettings& Settings)
{
	if (MSOnline::IsEnabled()) return CreateOnlineRoom(Settings);
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const FMSMapDefinitionRow* Map = FMSMapDefinitionRow::FindMapById(Settings.MapId);
	if (!Instance || !Map || !Map->bPlayable || Map->LevelPackagePath.IsEmpty())
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("Room: host rejected because map '%s' is unavailable"), *Settings.MapId.ToString());
		return false;
	}

	RoomSettings = Settings;
	RoomSettings.RoomName.TrimStartAndEndInline();
	RoomSettings.RuleIndex = FMath::Max(0, RoomSettings.RuleIndex);
	RoomSettings.BotCount = FMath::Clamp(RoomSettings.BotCount, 0, 11);
	if (RoomSettings.bFillTeamsWithBots)
	{
		// The manual enemy-BOT count has no meaning in the automatic 25v25 mode.
		RoomSettings.BotCount = 0;
	}
	Instance->PendingRoomSettings = RoomSettings;
	Instance->bPendingRoomHost = true;
	Instance->bPendingRoomJoin = false;
	// Reuse the proven post-travel briefing/deployment path without changing quick sortie behavior.
	Instance->bPendingSortie = true;
	Instance->PendingRuleIndex = RoomSettings.RuleIndex;
	Instance->SelectedMapId = RoomSettings.MapId;

	const FString Options = FString::Printf(TEXT("listen?MapId=%s?RoomHost=1?RuleIndex=%d?BotCount=%d?BotDifficulty=%d?FillTeamsWithBots=%d"),
		*RoomSettings.MapId.ToString(), RoomSettings.RuleIndex, RoomSettings.BotCount,
		static_cast<int32>(RoomSettings.BotDifficulty), RoomSettings.bFillTeamsWithBots ? 1 : 0);
	UE_LOG(LogNeoMSGO, Display, TEXT("Room: hosting '%s' map=%s rule=%d bots=%d difficulty=%d"),
		*RoomSettings.RoomName, *RoomSettings.MapId.ToString(), RoomSettings.RuleIndex,
		RoomSettings.BotCount, static_cast<int32>(RoomSettings.BotDifficulty));

	FMSRoomDescriptor Descriptor;
	Descriptor.RoomId = FGuid::NewGuid();
	Descriptor.BuildId = MSRoomProtocol::BuildId;
	Descriptor.Settings = RoomSettings;
	Descriptor.HostDisplayName = Instance->LoggedInUsername.IsEmpty()
		? TEXT("Guest") : Instance->LoggedInUsername;
	Descriptor.Address = AdvertisedRoomAddress;
	Descriptor.CurrentPlayerCount = 1;
	Descriptor.MaxPlayerCount = RoomSettings.GetMaxCombatants();
	Descriptor.State = EMSRoomState::Waiting;
	BeginRegistryHosting(Descriptor);

	UGameplayStatics::OpenLevel(Instance, FName(*Map->LevelPackagePath), true, Options);
	return true;
}

bool UMSRoomSubsystem::NormalizeAddress(const FString& Address, FString& OutAddress)
{
	OutAddress = Address;
	OutAddress.TrimStartAndEndInline();
	if (OutAddress.IsEmpty() || OutAddress.Contains(TEXT(" ")) || OutAddress.Contains(TEXT("?")))
	{
		OutAddress.Reset();
		return false;
	}
	return true;
}

bool UMSRoomSubsystem::JoinRoom(const FString& Address)
{
	if (MSOnline::IsEnabled()) return JoinOnlineServer(Address);
	FString CleanAddress;
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	APlayerController* PlayerController = Instance ? Instance->GetFirstLocalPlayerController() : nullptr;
	if (!NormalizeAddress(Address, CleanAddress) || !Instance || !PlayerController)
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("Room: join rejected (address or local player is unavailable)"));
		return false;
	}

	// A listen server must never join another room from its own UI: ClientTravel would tear down
	// the running GameNetDriver and disconnect every client that is already playing here. This is
	// reachable by a stray click/Enter on the join button while hosting, which reads to the user as
	// "the first window died when I opened the second one".
	const UWorld* World = Instance->GetWorld();
	const ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
	if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer)
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("Room: join rejected because this instance is hosting; leave the room before joining another"));
		return false;
	}

	// Debounce re-entrant joins: each ClientTravel spawns a PendingNetDriver, and repeated
	// activation stacks them up until the travel fails outright. This is deliberately time-based
	// rather than keyed on bPendingRoomJoin, so a failed/refused travel can always be retried.
	const double Now = FPlatformTime::Seconds();
	if (LastJoinAttemptSeconds > 0.0 && (Now - LastJoinAttemptSeconds) < JoinDebounceSeconds)
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("Room: join ignored because a join was just requested"));
		return false;
	}
	LastJoinAttemptSeconds = Now;

	Instance->bPendingRoomHost = false;
	Instance->bPendingRoomJoin = true;
	Instance->bPendingSortie = true;
	Instance->PendingRuleIndex = 0;
	UE_LOG(LogNeoMSGO, Display, TEXT("Room: joining %s"), *CleanAddress);
	PlayerController->ClientTravel(CleanAddress, TRAVEL_Absolute);
	return true;
}

void UMSRoomSubsystem::LeaveRoom()
{
	UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	if (!Instance)
	{
		return;
	}
	if (bReturningToLobby) return;
	bReturningToLobby = true;
	if (auto* Account = Instance->GetSubsystem<UMSOnlineAccountSubsystem>()) Account->CancelPendingGameTicket();
	++RoomOperationGeneration;
	CloseOnlineRoom();
	PendingOnlineServer.Reset(); PendingRoomInstance.Reset(); PendingOnlineGeneration = -1;
	SetRoomStatus(false, FText::GetEmpty());
	EndRegistryHosting();
	Instance->bPendingRoomHost = false;
	Instance->bPendingRoomJoin = false;
	Instance->bPendingSortie = false;
	RoomSettings = FMSRoomSettings();
	Instance->PendingRoomSettings = RoomSettings;
	// OpenLevel ends the local net driver for both a guest and a listen host.
	// ClientTravel on a host controller is not a reliable room shutdown boundary.
	UGameplayStatics::OpenLevel(Instance, TEXT("/Game/Maps/Lvl_GrandBattlefield"), true);
}

void UMSRoomSubsystem::NotifyHostSessionEnded(const UWorld* EndedWorld)
{
	if (HostedSession.IsValid() && EndedWorld && EndedWorld == RegisteredHostWorld.Get()) CloseOnlineRoom();
	if (!bRegistryHosting || !EndedWorld)
	{
		return;
	}

	// HostRoom starts advertisement immediately before travel. Ignore the old standalone world's
	// EndPlay; only the actual listen-server world owns the advertised session.
	if (EndedWorld == RegisteredHostWorld.Get())
	{
		EndRegistryHosting();
	}
}

void UMSRoomSubsystem::BeginRegistryHosting(const FMSRoomDescriptor& Descriptor)
{
	if (bRegistryHosting)
	{
		EndRegistryHosting();
	}
	HostedRoomId = Descriptor.RoomId;
	bRegistryHosting = HostedRoomId.IsValid();
	if (!bRegistryHosting)
	{
		return;
	}

	FString JsonBody;
	FJsonObjectConverter::UStructToJsonObjectString(Descriptor, JsonBody, 0, 0, 0, nullptr, false);
	if (!SendRegistryPost(TEXT("/rooms/register"), JsonBody))
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("Room registry registration could not be started; room hosting will continue"));
	}
	StartHeartbeatTicker();
}

void UMSRoomSubsystem::EndRegistryHosting()
{
	if (!bRegistryHosting)
	{
		return;
	}
	StopHeartbeatTicker();
	const FString Body = FString::Printf(
		TEXT("{\"RoomId\":\"%s\"}"), *HostedRoomId.ToString(EGuidFormats::DigitsWithHyphens));
	SendRegistryPost(TEXT("/rooms/unregister"), Body);
	bRegistryHosting = false;
	HostedRoomId.Invalidate();
	RegisteredHostWorld.Reset();
}

void UMSRoomSubsystem::SendRegistryHeartbeat()
{
	if (!bRegistryHosting || !HostedRoomId.IsValid())
	{
		return;
	}
	const FString Body = FString::Printf(
		TEXT("{\"RoomId\":\"%s\"}"), *HostedRoomId.ToString(EGuidFormats::DigitsWithHyphens));
	SendRegistryPost(TEXT("/rooms/heartbeat"), Body);
}

bool UMSRoomSubsystem::TickRegistryHeartbeat(float)
{
	if (!bRegistryHosting)
	{
		return false;
	}
	SendRegistryHeartbeat();
	return true;
}

bool UMSRoomSubsystem::SendRegistryPost(const FString& Path, const FString& JsonBody)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (RegistryRequestSenderForAutomationTests)
	{
		return RegistryRequestSenderForAutomationTests(Path, JsonBody);
	}
#endif

	FString BaseUrl = RegistryBaseUrl;
	BaseUrl.TrimStartAndEndInline();
	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}
	if (BaseUrl.IsEmpty())
	{
		return false;
	}

	const FString Url = BaseUrl + Path;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetContentAsString(JsonBody);
	Request->OnProcessRequestComplete().BindLambda(
		[Url](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
			const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
			if (!bSucceeded || !EHttpResponseCodes::IsOk(Code))
			{
				UE_LOG(LogNeoMSGO, Warning,
					TEXT("Room registry request failed: %s (HTTP %d); room remains playable by direct IP"),
					*Url, Code);
			}
			else
			{
				UE_LOG(LogNeoMSGO, Verbose, TEXT("Room registry request succeeded: %s"), *Url);
			}
		});
	return Request->ProcessRequest();
}

void UMSRoomSubsystem::StartHeartbeatTicker()
{
	if (!bRegistryHosting)
	{
		return;
	}
	StopHeartbeatTicker();
	RegistryHeartbeatTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMSRoomSubsystem::TickRegistryHeartbeat),
		HeartbeatIntervalSeconds);
}

void UMSRoomSubsystem::StopHeartbeatTicker()
{
	if (RegistryHeartbeatTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RegistryHeartbeatTickerHandle);
	}
	RegistryHeartbeatTickerHandle.Reset();
}

void UMSRoomSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	if (LoadedWorld->GetNetMode() == NM_ListenServer)
	{
		RegisteredHostWorld = LoadedWorld;
	}
	else if (LoadedWorld->GetNetMode() == NM_Standalone)
	{
		bReturningToLobby = false;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSRoomSubsystem::BeginRegistryHostForAutomationTests(const FMSRoomDescriptor& Descriptor)
{
	BeginRegistryHosting(Descriptor);
}

void UMSRoomSubsystem::SendHeartbeatForAutomationTests()
{
	SendRegistryHeartbeat();
}

void UMSRoomSubsystem::EndRegistryHostForAutomationTests()
{
	EndRegistryHosting();
}
#endif

bool UMSRoomSubsystem::JoinOnlineServer(const FString& ServerId)
{
 auto* Instance = Cast<UMSGameInstance>(GetGameInstance());
 auto* Account = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
 if (!CanStartOnlineOperation()) return false;
 if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone) return false;
 if (GEngine) if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(GetWorld())) if (Context->PendingNetGame) return false;
 if (LastJoinAttemptSeconds > 0 && FPlatformTime::Seconds() - LastJoinAttemptSeconds < JoinDebounceSeconds) return false;
 LastJoinAttemptSeconds = FPlatformTime::Seconds();
 const FString Id = ServerId.TrimStartAndEnd();
 if (Id.IsEmpty() || Id.Len() > 64) return false;
 for (const TCHAR C : Id) if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) return false;
 PendingOnlineServer = Id; PendingOnlineGeneration = Account->GetAuthGeneration();
 OnlineOperationStartedAt = FPlatformTime::Seconds(); SetRoomStatus(true, FText::FromString(TEXT("参加を準備しています…")));
 if (!Account->RequestGameTicket(Id)) { PendingOnlineServer.Reset(); SetRoomStatus(false, FText::FromString(TEXT("参加を開始できません。"))); return false; }
 return true;
}
void UMSRoomSubsystem::HandleOnlineTicketReady(FString ServerId, FString GameAddress, int64 Generation)
{
 if (PendingOnlineServer != ServerId || PendingOnlineGeneration != Generation) return;
 auto* Instance = Cast<UMSGameInstance>(GetGameInstance());
 auto* Account = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
 FString Address;
 if (!Account || Account->GetAuthGeneration() != Generation || !NormalizeAddress(GameAddress, Address)) { PendingOnlineServer.Reset(); return; }
 auto* PC = Instance->GetFirstLocalPlayerController(); if (!PC) { PendingOnlineServer.Reset(); return; }
 if (HostedSession.IsValid() && HostedSession.RoomId == ServerId)
 {
  // The local host consumes this ticket in-place. Travelling to our own address would destroy the room.
  PendingOnlineServer.Reset();
  return;
 }
 PendingOnlineServer.Reset();
 Instance->bPendingRoomJoin = true; Instance->bPendingRoomHost = false;
 Instance->bPendingSortie = true; Instance->PendingRuleIndex = 0;
 PC->ClientTravel(Address, TRAVEL_Absolute);
}
void UMSRoomSubsystem::HandleOnlineRequestFinished(FName Operation, bool bSuccess, FText Message, int64 Generation)
{
 if (Operation == TEXT("game_ticket") && !bSuccess && Generation == PendingOnlineGeneration)
  FailOnlineRoom(Message, HostedSession.IsValid());
}

void UMSRoomSubsystem::HandleOnlineAccountChanged(FMSOnlineIdentity Identity, int64 Generation)
{
 if (PendingOnlineGeneration >= 0 && Generation != PendingOnlineGeneration)
 {
  ++RoomOperationGeneration;
  const bool bConnected = GetWorld() && GetWorld()->GetNetMode() != NM_Standalone;
  if (bConnected) LeaveRoom();
  else { CloseOnlineRoom(); PendingOnlineServer.Reset(); PendingOnlineGeneration = -1; SetRoomStatus(false, FText::GetEmpty()); }
 }
}

void UMSRoomSubsystem::SetRoomStatus(bool bPending, const FText& Message)
{
	bRoomOperationPending = bPending; RoomStatusText = Message; OnRoomStatusChanged.Broadcast();
}

bool UMSRoomSubsystem::CanStartOnlineOperation()
{
	auto* Instance = GetGameInstance();
	auto* Account = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (!Account || !Account->HasOnlineSession()) { SetRoomStatus(false, FText::FromString(TEXT("ログインしてください。"))); return false; }
	if (bRoomOperationPending || HostedSession.IsValid() || bReturningToLobby) return false;
	if (Account->IsDeckRequestPending()) { SetRoomStatus(false, FText::FromString(TEXT("編成の保存が終わるまでお待ちください。"))); return false; }
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone || !Instance->GetFirstLocalPlayerController())
	{ SetRoomStatus(false, FText::FromString(TEXT("現在のルームから退出してください。"))); return false; }
	if (GEngine) if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(GetWorld())) if (Context->PendingNetGame) return false;
	return true;
}

bool UMSRoomSubsystem::CreateOnlineRoom(const FMSRoomSettings& Settings)
{
	if (!CanStartOnlineOperation()) return false;
	const FMSMapDefinitionRow* Map = FMSMapDefinitionRow::FindMapById(Settings.MapId);
	if (!Map || !Map->bPlayable || Map->LevelPackagePath.IsEmpty() || Settings.BotCount < 0 || Settings.BotCount > 11 ||
		Settings.RuleIndex < 0 || Settings.RuleIndex > 2 || static_cast<uint8>(Settings.BotDifficulty) > 3)
	{ SetRoomStatus(false, FText::FromString(TEXT("マップ・ルール・敵Botの設定を確認してください。"))); return false; }
	RoomSettings = Settings; RoomSettings.RoomName.TrimStartAndEndInline();
	if (RoomSettings.bFillTeamsWithBots)
	{
		// Keep the API's effective bot_count consistent with the disabled UI field.
		RoomSettings.BotCount = 0;
	}
	if (RoomSettings.RoomName.IsEmpty()) RoomSettings.RoomName = TEXT("対戦ルーム");
	if (RoomSettings.RoomName.Len() > 48) { SetRoomStatus(false, FText::FromString(TEXT("ルーム名は48文字以内にしてください。"))); return false; }
	auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
	PendingOnlineGeneration = Account->GetAuthGeneration();
	const uint32 Operation = ++RoomOperationGeneration;
	OnlineOperationStartedAt = FPlatformTime::Seconds();
	SetRoomStatus(true, FText::FromString(TEXT("ルームを作成しています…")));
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), RoomSettings.RoomName);
	Body->SetStringField(TEXT("map_id"), RoomSettings.MapId.ToString());
	Body->SetNumberField(TEXT("rule_id"), RoomSettings.RuleIndex);
	Body->SetNumberField(TEXT("bot_count"), RoomSettings.BotCount);
	Body->SetBoolField(TEXT("fill_teams_with_bots"), RoomSettings.bFillTeamsWithBots);
	Body->SetNumberField(TEXT("difficulty"), static_cast<int32>(RoomSettings.BotDifficulty));
	Body->SetStringField(TEXT("build_id"), MSRoomProtocol::BuildId);
	Body->SetStringField(TEXT("catalog_hash"), Account->GetCatalogHash());
	Body->SetNumberField(TEXT("expected_revision"), static_cast<double>(Account->GetCommittedDeck().Revision));
	int32 Port = 7787; FParse::Value(FCommandLine::Get(), TEXT("Port="), Port);
	if (Port < 1024 || Port > 65535) { SetRoomStatus(false, FText::FromString(TEXT("ルームの接続ポートが不正です。"))); return false; }
	Body->SetNumberField(TEXT("game_port"), Port);
	Body->SetStringField(TEXT("idempotency_key"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	FString Json; FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Json));
	TWeakObjectPtr<UMSRoomSubsystem> WeakThis(this);
	const bool bSent = Account->SendRoomRequest(TEXT("POST"), TEXT("/v1/rooms"), Json,
		[WeakThis, Operation, Port](bool bOk, int32 Code, const FString& ResponseBody)
		{
			auto* Self = WeakThis.Get(); if (!Self || Self->RoomOperationGeneration != Operation) return;
			auto* CurrentAccount = Self->GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
			TSharedPtr<FJsonObject> Object;
			FMSHostedRoomSession Session; FGuid RoomGuid, InstanceGuid;
			const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
			FString MapId, BuildId, Hash; double Rule = -1, Bots = -1, Difficulty = -1, Maximum = -1;
			bool bResponseFillTeamsWithBots = false;
			const bool bValid = bOk && EHttpResponseCodes::IsOk(Code) && CurrentAccount &&
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResponseBody), Object) && Object &&
				Object->TryGetStringField(TEXT("room_id"), Session.RoomId) && FGuid::Parse(Session.RoomId, RoomGuid) &&
				Object->TryGetStringField(TEXT("instance_id"), Session.InstanceId) && FGuid::Parse(Session.InstanceId, InstanceGuid) &&
				Object->TryGetStringField(TEXT("room_lease"), Session.Lease) && Session.Lease.Len() >= 24 && Session.Lease.Len() <= 512 &&
				Object->TryGetObjectField(TEXT("settings"), SettingsObject) && SettingsObject &&
				(*SettingsObject)->TryGetStringField(TEXT("map_id"), MapId) && FName(*MapId) == Self->RoomSettings.MapId &&
				(*SettingsObject)->TryGetNumberField(TEXT("rule_id"), Rule) && Rule == Self->RoomSettings.RuleIndex &&
				(*SettingsObject)->TryGetNumberField(TEXT("bot_count"), Bots) && Bots == Self->RoomSettings.BotCount &&
				(*SettingsObject)->TryGetNumberField(TEXT("difficulty"), Difficulty) && Difficulty == static_cast<int32>(Self->RoomSettings.BotDifficulty) &&
				(*SettingsObject)->TryGetStringField(TEXT("build_id"), BuildId) && BuildId == MSRoomProtocol::BuildId &&
				(*SettingsObject)->TryGetStringField(TEXT("catalog_hash"), Hash) && Hash == CurrentAccount->GetCatalogHash() &&
				Object->TryGetNumberField(TEXT("max_players"), Maximum) && Maximum == Self->RoomSettings.GetMaxCombatants() &&
				TryReadFillFlagFromResponse(Object, *SettingsObject, bResponseFillTeamsWithBots) &&
				bResponseFillTeamsWithBots == Self->RoomSettings.bFillTeamsWithBots;
			if (!bValid)
			{
				Self->FailOnlineRoom(FText::FromString(Code == 409 ? TEXT("作成済みルームが終了するまでお待ちください。") : TEXT("ルームを作成できません。共通サーバーへの接続を確認してください。")), false);
				return;
			}
			Session.BaseUrl = CurrentAccount->GetAuthBaseUrl(); while (Session.BaseUrl.EndsWith(TEXT("/"))) Session.BaseUrl.LeftChopInline(1);
			Session.CatalogHash = Hash; Session.RealmId = CurrentAccount->GetIdentity().RealmId; Session.GamePort = Port;
			Self->HostedSession = MoveTemp(Session); Self->bHostAdmitted = false; Self->bHostTicketRequested = false;
			Self->TravelToHostedMap();
		});
	if (!bSent) FailOnlineRoom(FText::FromString(TEXT("ルーム作成を開始できません。")), false);
	return bSent;
}

void UMSRoomSubsystem::TravelToHostedMap()
{
	auto* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const auto* Map = FMSMapDefinitionRow::FindMapById(RoomSettings.MapId);
	if (!Instance || !Map || !HostedSession.IsValid()) { FailOnlineRoom(FText::FromString(TEXT("マップを開始できません。")), false); return; }
	Instance->PendingRoomSettings = RoomSettings; Instance->bPendingRoomHost = true; Instance->bPendingRoomJoin = false;
	Instance->bPendingSortie = true; Instance->PendingRuleIndex = RoomSettings.RuleIndex; Instance->SelectedMapId = RoomSettings.MapId;
	SetRoomStatus(true, FText::FromString(TEXT("マップを読み込んでいます…")));
	const FString Options = FString::Printf(TEXT("listen?MapId=%s?RoomHost=1?RuleIndex=%d?BotCount=%d?BotDifficulty=%d?FillTeamsWithBots=%d"),
		*RoomSettings.MapId.ToString(), RoomSettings.RuleIndex, RoomSettings.BotCount,
		static_cast<int32>(RoomSettings.BotDifficulty), RoomSettings.bFillTeamsWithBots ? 1 : 0);
	UGameplayStatics::OpenLevel(Instance, FName(*Map->LevelPackagePath), true, Options);
}

bool UMSRoomSubsystem::JoinOnlineRoom(const FString& RoomId, const FString& InstanceId)
{
	FGuid RoomGuid, InstanceGuid;
	if (!FGuid::Parse(RoomId, RoomGuid) || !FGuid::Parse(InstanceId, InstanceGuid) || !CanStartOnlineOperation()) return false;
	auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
	PendingOnlineServer = RoomId; PendingRoomInstance = InstanceId; PendingOnlineGeneration = Account->GetAuthGeneration();
	OnlineOperationStartedAt = FPlatformTime::Seconds(); ++RoomOperationGeneration;
	SetRoomStatus(true, FText::FromString(TEXT("ルームへ接続しています…")));
	const bool bSent = Account->RequestRoomTicket(RoomId, InstanceId);
	if (!bSent) FailOnlineRoom(FText::FromString(TEXT("参加を開始できません。編成と接続状態を確認してください。")), false);
	return bSent;
}

void UMSRoomSubsystem::CloseOnlineRoom()
{
	if (HostedSession.IsValid())
	{
		auto Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(HostedSession.BaseUrl + TEXT("/v1/rooms/") + HostedSession.RoomId + TEXT("/close"));
		Request->SetVerb(TEXT("POST")); Request->SetTimeout(5);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetHeader(TEXT("X-MS-Room-Lease"), HostedSession.Lease);
		auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("instance_id"), HostedSession.InstanceId);
		FString Json; FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Json));
		Request->SetContentAsString(Json); Request->ProcessRequest();
	}
	HostedSession = FMSHostedRoomSession(); bHostAdmitted = false; bHostTicketRequested = false;
	RegisteredHostWorld.Reset();
}

bool UMSRoomSubsystem::TickOnlineRoom(float)
{
	if (!MSOnline::IsEnabled() || !bRoomOperationPending) return true;
	if (FPlatformTime::Seconds() - OnlineOperationStartedAt > 120)
	{ FailOnlineRoom(FText::FromString(TEXT("ルームへの接続が時間切れになりました。再検索してください。")), true); return true; }
	if (HostedSession.IsValid() && !bHostTicketRequested && GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer && GetWorld()->GetNetDriver())
	{
		auto* Server = GetWorld()->GetSubsystem<UMSOnlineServerSubsystem>();
		auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>();
		if (Server && !Server->GetStartupError().IsEmpty()) { FailOnlineRoom(Server->GetStartupError(), true); return true; }
		if (Server && Server->IsReady() && Account)
		{
			bHostTicketRequested = true; PendingOnlineServer = HostedSession.RoomId;
			if (!Account->RequestRoomTicket(HostedSession.RoomId, HostedSession.InstanceId))
				FailOnlineRoom(FText::FromString(TEXT("ルーム作成者の認証を開始できません。")), true);
		}
	}
	return true;
}

void UMSRoomSubsystem::NotifyLocalAdmission(AMSPlayerController* Controller)
{
	if (!Controller || !Controller->IsLocalController() || Controller->GetGameInstance() != GetGameInstance()) return;
	bHostAdmitted = HostedSession.IsValid(); PendingOnlineServer.Reset(); PendingRoomInstance.Reset();
	SetRoomStatus(false, FText::GetEmpty());
}

void UMSRoomSubsystem::FailOnlineRoom(const FText& Message, bool bReturnToLobby)
{
	++RoomOperationGeneration;
	if (auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr) Account->CancelPendingGameTicket();
	if (bReturnToLobby) LeaveRoom(); else { CloseOnlineRoom(); PendingOnlineServer.Reset(); PendingRoomInstance.Reset(); }
	SetRoomStatus(false, Message);
}

void UMSRoomSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver*, ENetworkFailure::Type, const FString&)
{
	if (World && World->GetGameInstance() == GetGameInstance() && !bReturningToLobby && MSOnline::IsEnabled())
		FailOnlineRoom(FText::FromString(TEXT("ルームとの接続が終了しました。再検索して参加するか、ルームを作成してください。")), true);
}

void UMSRoomSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type, const FString&)
{
	if (World && World->GetGameInstance() == GetGameInstance() && !bReturningToLobby && MSOnline::IsEnabled())
		FailOnlineRoom(FText::FromString(TEXT("ルームのマップへ移動できませんでした。")), true);
}
