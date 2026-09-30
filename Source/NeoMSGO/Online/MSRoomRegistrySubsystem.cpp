// Copyright NeoMSGO Project. All Rights Reserved.

#include "Online/MSRoomRegistrySubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Data/MSMapDefinitionRow.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "NeoMSGO.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

namespace
{
	/** Missing fill flags are legacy 12-seat rooms; present values must be bools. */
	bool TryReadOptionalFillFlag(
		const TSharedPtr<FJsonObject>& Object, bool& OutValue, bool& bOutPresent)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		for (const TCHAR* FieldName : { TEXT("fill_teams_with_bots"), TEXT("bFillTeamsWithBots") })
		{
			if (!Object->HasField(FieldName))
			{
				continue;
			}
			const TSharedPtr<FJsonValue> JsonValue = Object->TryGetField(FieldName);
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Boolean)
			{
				return false;
			}
			bool Value = false;
			if (!Object->TryGetBoolField(FieldName, Value))
			{
				return false;
			}
			if (bOutPresent && OutValue != Value)
			{
				return false;
			}
			OutValue = Value;
			bOutPresent = true;
		}
		return true;
	}

	bool TryReadRoomFillFlag(
		const TSharedPtr<FJsonObject>& Root, bool& OutValue)
	{
		bool bRootValue = false;
		bool bRootPresent = false;
		if (!TryReadOptionalFillFlag(Root, bRootValue, bRootPresent))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
		if (Root->TryGetObjectField(TEXT("settings"), SettingsObject)
			|| Root->TryGetObjectField(TEXT("Settings"), SettingsObject))
		{
			bool bSettingsValue = false;
			bool bSettingsPresent = false;
			if (!SettingsObject || !TryReadOptionalFillFlag(*SettingsObject, bSettingsValue, bSettingsPresent))
			{
				return false;
			}
			if (bRootPresent && bSettingsPresent && bRootValue != bSettingsValue)
			{
				return false;
			}
			OutValue = bSettingsPresent ? bSettingsValue : bRootPresent ? bRootValue : false;
			return true;
		}
		OutValue = bRootPresent ? bRootValue : false;
		return true;
	}
}

void UMSRoomRegistrySubsystem::Deinitialize()
{
	StopPolling();
	OnRoomListUpdated.Clear();
	OnRoomListStatusChanged.Clear();
	Super::Deinitialize();
}

void UMSRoomRegistrySubsystem::StartPolling(FString RegistryAddress)
{
	StopPolling();
	auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	bOnlinePolling = Account && Account->IsOnlineMode();
	PollingAuthGeneration = Account ? Account->GetAuthGeneration() : -1;
	if (bOnlinePolling) RegistryAddress = Account->GetAuthBaseUrl();
	RegistryBaseUrl = NormalizeRegistryAddress(MoveTemp(RegistryAddress));
	if (RegistryBaseUrl.IsEmpty())
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("Room registry polling was not started: address is empty"));
		return;
	}

	bPolling = true;
	++PollingGeneration;
	LastManualRefreshSeconds = -DBL_MAX;
	PollRooms();

	// Menus pause standalone worlds, so their room list must use real time.
	PollingTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMSRoomRegistrySubsystem::TickPolling), PollingIntervalSeconds);
}

void UMSRoomRegistrySubsystem::StopPolling()
{
	bPolling = false;
	++PollingGeneration;
	FTSTicker::RemoveTicker(PollingTicker); PollingTicker.Reset();
	bRequestPending = false;
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
}

bool UMSRoomRegistrySubsystem::RefreshNow()
{
	if (!bPolling)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastManualRefreshSeconds) < ManualRefreshThrottleSeconds)
	{
		UE_LOG(LogNeoMSGO, Verbose, TEXT("Room registry manual refresh was throttled"));
		return false;
	}
	LastManualRefreshSeconds = Now;
	PollRooms();
	return true;
}

void UMSRoomRegistrySubsystem::PollRooms()
{
	if (!bPolling)
	{
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	++PollInvocationCountForAutomationTests;
#endif

	auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (bOnlinePolling && Account && PollingAuthGeneration != Account->GetAuthGeneration())
	{
		++PollingGeneration; bRequestPending = false; LastKnownRooms.Reset();
		PollingAuthGeneration = Account->GetAuthGeneration();
		OnRoomListUpdated.Broadcast(LastKnownRooms);
	}
	if (bRequestPending) return;
	const FString Url = RegistryBaseUrl + (bOnlinePolling ? TEXT("/v1/rooms") : TEXT("/rooms"));
#if WITH_DEV_AUTOMATION_TESTS
	if (RequestSenderForAutomationTests)
	{
		if (!RequestSenderForAutomationTests(Url)) ApplyRoomsResponse(false, 0, FString());
		return;
	}
#endif

	if (bOnlinePolling)
	{
		bRequestPending = true; OnRoomListStatusChanged.Broadcast();
		const uint32 Generation = PollingGeneration;
		TWeakObjectPtr<UMSRoomRegistrySubsystem> WeakThis(this);
		if (!Account || !Account->SendRoomRequest(TEXT("GET"), TEXT("/v1/rooms"), FString(),
			[WeakThis, Generation](bool bOk, int32 Code, const FString& Body)
			{
				auto* Self = WeakThis.Get();
				if (Self && Self->bPolling && Self->PollingGeneration == Generation) Self->ApplyRoomsResponse(bOk, Code, Body);
			})) ApplyRoomsResponse(false, 0, FString());
		return;
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(RequestTimeoutSeconds);
	ActiveRequestGeneration = PollingGeneration;
	Request->OnProcessRequestComplete().BindUObject(
		this, &UMSRoomRegistrySubsystem::HandleRoomsResponse);
	ActiveRequest = Request;
	bRequestPending = true; OnRoomListStatusChanged.Broadcast();
	if (!Request->ProcessRequest())
	{
		ActiveRequest.Reset();
		ApplyRoomsResponse(false, 0, FString());
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("Room registry GET could not be started: %s; keeping %d cached rooms"),
			*Url, LastKnownRooms.Num());
	}
}

void UMSRoomRegistrySubsystem::HandleRoomsResponse(
	FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bSucceeded)
{
	if (!bPolling || Request != ActiveRequest || ActiveRequestGeneration != PollingGeneration)
	{
		return;
	}

	ActiveRequest.Reset();
	ApplyRoomsResponse(bSucceeded, Response ? Response->GetResponseCode() : 0, Response ? Response->GetContentAsString() : FString());
}

bool UMSRoomRegistrySubsystem::TickPolling(float)
{
	if (!bPolling) return false;
	PollRooms(); return true;
}

void UMSRoomRegistrySubsystem::ApplyRoomsResponse(bool bSucceeded, int32 Code, const FString& Json)
{
	bRequestPending = false;
	if (!bSucceeded || !EHttpResponseCodes::IsOk(Code))
	{
		LastErrorText = FText::FromString(TEXT("一覧を取得できません。表示中の情報は前回取得時点のものです。接続を確認して再検索してください。"));
		OnRoomListStatusChanged.Broadcast();
		return;
	}

	TArray<FMSRoomDescriptor> ParsedRooms;
	if (!ParseRoomList(Json, ParsedRooms))
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("Room registry returned invalid JSON; keeping %d cached rooms"),
			LastKnownRooms.Num());
		LastErrorText = FText::FromString(TEXT("ルーム一覧の形式を確認できませんでした。ゲームのバージョンを確認してください。"));
		OnRoomListStatusChanged.Broadcast();
		return;
	}
	LastKnownRooms = MoveTemp(ParsedRooms);
	LastErrorText = FText::GetEmpty();
	OnRoomListUpdated.Broadcast(LastKnownRooms);
	OnRoomListStatusChanged.Broadcast();
}

bool UMSRoomRegistrySubsystem::ParseRoomList(
	const FString& Json, TArray<FMSRoomDescriptor>& OutRooms) const
{
	TArray<TSharedPtr<FJsonValue>> Values;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Values))
	{
		return false;
	}

	OutRooms.Reset(Values.Num());
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			return false;
		}
		FMSRoomDescriptor Descriptor;
		if (bOnlinePolling)
		{
			FString RoomId, MapId, Status = TEXT("ready"); FGuid Instance;
			double Rule = -1, Bots = -1, Difficulty = -1, Players = -1, Maximum = -1;
			bool bFillTeamsWithBots = false;
			if (!Object->TryGetStringField(TEXT("room_id"), RoomId) || !FGuid::Parse(RoomId, Descriptor.RoomId) ||
				!Object->TryGetStringField(TEXT("instance_id"), Descriptor.InstanceId) || !FGuid::Parse(Descriptor.InstanceId, Instance) ||
				!Object->TryGetStringField(TEXT("name"), Descriptor.Settings.RoomName) ||
				!Object->TryGetStringField(TEXT("host_name"), Descriptor.HostDisplayName) ||
				!Object->TryGetStringField(TEXT("game_address"), Descriptor.Address) || !UMSRoomSubsystem::NormalizeAddress(Descriptor.Address, Descriptor.Address) ||
				!Object->TryGetStringField(TEXT("map_id"), MapId) ||
				!Object->TryGetStringField(TEXT("build_id"), Descriptor.BuildId) ||
				!Object->TryGetStringField(TEXT("catalog_hash"), Descriptor.CatalogHash) ||
				!Object->TryGetNumberField(TEXT("rule_id"), Rule) || Rule < 0 || Rule > 2 || FMath::FloorToDouble(Rule) != Rule ||
				!Object->TryGetNumberField(TEXT("bot_count"), Bots) || Bots < 0 || Bots > 11 || FMath::FloorToDouble(Bots) != Bots ||
				!Object->TryGetNumberField(TEXT("difficulty"), Difficulty) || Difficulty < 0 || Difficulty > 3 || FMath::FloorToDouble(Difficulty) != Difficulty ||
				!Object->TryGetNumberField(TEXT("players"), Players) || Players < 0 || FMath::FloorToDouble(Players) != Players ||
				!Object->TryGetNumberField(TEXT("max_players"), Maximum) || !TryReadRoomFillFlag(Object, bFillTeamsWithBots)) return false;
			const int32 ExpectedMaxCombatants = bFillTeamsWithBots
				? MSRoomPopulation::MaxCombatants : MSRoomPopulation::LegacyMaxCombatants;
			if (Maximum != ExpectedMaxCombatants || Players > Maximum ||
				(bFillTeamsWithBots && Bots != 0)) return false;
			// The room API uses status=playing; state is accepted for the current
			// backend response and older room adapters. Do not turn a live match
			// into the default Waiting label when the status is present.
			if (!Object->TryGetStringField(TEXT("status"), Status) && !Object->TryGetStringField(TEXT("state"), Status))
			{
				Status = TEXT("ready");
			}
			Status.TrimStartAndEndInline(); Status.ToLowerInline();
			if (Status == TEXT("starting"))
			{
				// Starting rooms must remain undiscoverable even if an adapter leaks one.
				continue;
			}
			if (Status != TEXT("ready") && Status != TEXT("playing")) return false;
			Descriptor.Settings.MapId = FName(*MapId); Descriptor.Settings.RuleIndex = static_cast<int32>(Rule);
			Descriptor.Settings.BotCount = static_cast<int32>(Bots); Descriptor.Settings.BotDifficulty = static_cast<EMSBotDifficulty>(static_cast<int32>(Difficulty));
			Descriptor.Settings.bFillTeamsWithBots = bFillTeamsWithBots;
			Descriptor.CurrentPlayerCount = static_cast<int32>(Players); Descriptor.MaxPlayerCount = ExpectedMaxCombatants;
			const auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
			const auto* Map = FMSMapDefinitionRow::FindMapById(Descriptor.Settings.MapId);
			Descriptor.State = !Map || !Map->bPlayable || Descriptor.BuildId != MSRoomProtocol::BuildId || !Account || Descriptor.CatalogHash != Account->GetCatalogHash()
				? EMSRoomState::Unresponsive : (bFillTeamsWithBots
					? Players >= MSRoomPopulation::MaxCombatants
					: Players + Bots >= MSRoomPopulation::LegacyMaxCombatants) ? EMSRoomState::Full
				: Status == TEXT("playing") ? EMSRoomState::InProgress : EMSRoomState::Waiting;
		}
		else if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), FMSRoomDescriptor::StaticStruct(), &Descriptor, 0, 0))
		{
			return false;
		}
		else
		{
			// The local registry uses the generic UStruct JSON shape, while remote
			// adapters use snake_case. Normalize both before consumers see a room.
			bool bFillTeamsWithBots = false;
			if (!TryReadRoomFillFlag(Object, bFillTeamsWithBots))
			{
				return false;
			}
			if (bFillTeamsWithBots && Descriptor.Settings.BotCount != 0)
			{
				return false;
			}
			Descriptor.Settings.bFillTeamsWithBots = bFillTeamsWithBots;
			Descriptor.MaxPlayerCount = Descriptor.Settings.GetMaxCombatants();
		}
		OutRooms.Add(MoveTemp(Descriptor));
	}
	return true;
}

FString UMSRoomRegistrySubsystem::NormalizeRegistryAddress(FString Address) const
{
	Address.TrimStartAndEndInline();
	while (Address.EndsWith(TEXT("/")))
	{
		Address.LeftChopInline(1);
	}
	if (!Address.IsEmpty()
		&& !Address.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		&& !Address.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		Address = TEXT("http://") + Address;
	}
	return Address;
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSRoomRegistrySubsystem::SetLastKnownRoomsForAutomationTests(
	const TArray<FMSRoomDescriptor>& Rooms)
{
	LastKnownRooms = Rooms;
	OnRoomListUpdated.Broadcast(LastKnownRooms);
}

bool UMSRoomRegistrySubsystem::IsPollingTimerActiveForAutomationTests() const
{
	return PollingTicker.IsValid();
}
#endif
