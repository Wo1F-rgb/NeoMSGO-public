// Copyright NeoMSGO Project. All Rights Reserved.

#include "Online/MSRoomRegistryServer.h"

#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerConstants.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY(LogMSRoomRegistry)

FGuid FMSRoomRegistryStore::RegisterRoom(FMSRoomDescriptor Descriptor, const double NowUnixTime)
{
	if (!Descriptor.RoomId.IsValid())
	{
		Descriptor.RoomId = FGuid::NewGuid();
	}
	Descriptor.LastHeartbeatUnixTime = NowUnixTime;
	const FGuid RegisteredRoomId = Descriptor.RoomId;
	FScopeLock Lock(&RoomsMutex);
	Rooms.Add(Descriptor.RoomId, MoveTemp(Descriptor));
	return RegisteredRoomId;
}

bool FMSRoomRegistryStore::Heartbeat(const FGuid& RoomId, const double NowUnixTime)
{
	FScopeLock Lock(&RoomsMutex);
	if (FMSRoomDescriptor* Descriptor = Rooms.Find(RoomId))
	{
		Descriptor->LastHeartbeatUnixTime = NowUnixTime;
		return true;
	}
	return false;
}

bool FMSRoomRegistryStore::Unregister(const FGuid& RoomId)
{
	FScopeLock Lock(&RoomsMutex);
	return Rooms.Remove(RoomId) > 0;
}

TArray<FMSRoomDescriptor> FMSRoomRegistryStore::GetRooms() const
{
	FScopeLock Lock(&RoomsMutex);
	TArray<FMSRoomDescriptor> Result;
	Rooms.GenerateValueArray(Result);
	return Result;
}

int32 FMSRoomRegistryStore::SweepExpired(const double NowUnixTime)
{
	FScopeLock Lock(&RoomsMutex);
	int32 RemovedCount = 0;
	for (auto It = Rooms.CreateIterator(); It; ++It)
	{
		if ((NowUnixTime - It.Value().LastHeartbeatUnixTime) >= EntryTtlSeconds)
		{
			It.RemoveCurrent();
			++RemovedCount;
		}
	}
	return RemovedCount;
}

void FMSRoomRegistryStore::Reset()
{
	FScopeLock Lock(&RoomsMutex);
	Rooms.Reset();
}

FMSRoomRegistryHttpServer::~FMSRoomRegistryHttpServer()
{
	Stop();
}

bool FMSRoomRegistryHttpServer::Start(const uint32 Port)
{
	if (bRunning)
	{
		return true;
	}

	ListenPort = Port;
#if WITH_EDITOR
	// Prevent UnrealEd from loading the project's playable startup map. The registry
	// process only needs the engine tick that services HTTP and the TTL sweep.
	StartupMapVetoHandle = FEditorDelegates::OnEditorLoadDefaultStartupMap.AddLambda(
		[](FCanLoadMap& CanLoadDefaultStartupMap)
		{
			CanLoadDefaultStartupMap.SetFalse();
		});
#endif

	// HTTPServer defaults to localhost in UE 5.7. This special process is explicitly the
	// Tailscale/LAN registry, so override the process-local listener default before binding.
	GConfig->SetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"),
		TEXT("any"), GEngineIni);
	Router = FHttpServerModule::Get().GetHttpRouter(Port, true);
	if (!Router.IsValid())
	{
		UE_LOG(LogMSRoomRegistry, Error, TEXT("Failed to bind HTTP router on 0.0.0.0:%u"), Port);
		Stop();
		return false;
	}

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rooms/register")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMSRoomRegistryHttpServer::HandleRegister)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rooms/heartbeat")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMSRoomRegistryHttpServer::HandleHeartbeat)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rooms/unregister")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMSRoomRegistryHttpServer::HandleUnregister)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rooms")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FMSRoomRegistryHttpServer::HandleGetRooms)));

	if (RouteHandles.ContainsByPredicate([](const FHttpRouteHandle& Handle) { return !Handle.IsValid(); }))
	{
		UE_LOG(LogMSRoomRegistry, Error, TEXT("Failed to bind one or more room registry routes"));
		Stop();
		return false;
	}

	FHttpServerModule::Get().StartAllListeners();
	if (!FHttpServerModule::Get().GetHttpRouter(Port, true).IsValid())
	{
		UE_LOG(LogMSRoomRegistry, Error, TEXT("HTTP listener could not start on 0.0.0.0:%u"), Port);
		Stop();
		return false;
	}
	SweepTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMSRoomRegistryHttpServer::TickSweep), 1.0f);

	float VerificationLifetimeSeconds = 0.0f;
	if (FParse::Value(
			FCommandLine::Get(), TEXT("RegistryLifetimeSeconds="), VerificationLifetimeSeconds)
		&& VerificationLifetimeSeconds > 0.0f)
	{
		VerificationLifetimeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(
				this, &FMSRoomRegistryHttpServer::TickVerificationLifetime),
			VerificationLifetimeSeconds);
		UE_LOG(LogMSRoomRegistry, Display,
			TEXT("Registry verification lifetime is %.1f seconds"), VerificationLifetimeSeconds);
	}

	bRunning = true;
	UE_LOG(LogMSRoomRegistry, Display,
		TEXT("Room registry HTTP server listening on 0.0.0.0:%u (TTL=15s, sweep=1s)"), Port);
	return true;
}

void FMSRoomRegistryHttpServer::Stop()
{
	if (VerificationLifetimeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(VerificationLifetimeTickerHandle);
		VerificationLifetimeTickerHandle.Reset();
	}
	if (SweepTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SweepTickerHandle);
		SweepTickerHandle.Reset();
	}
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			if (Handle.IsValid())
			{
				Router->UnbindRoute(Handle);
			}
		}
	}
	RouteHandles.Reset();
	Router.Reset();
#if WITH_EDITOR
	if (StartupMapVetoHandle.IsValid())
	{
		FEditorDelegates::OnEditorLoadDefaultStartupMap.Remove(StartupMapVetoHandle);
		StartupMapVetoHandle.Reset();
	}
#endif
	if (bRunning && FHttpServerModule::IsAvailable())
	{
		FHttpServerModule::Get().StopAllListeners();
		UE_LOG(LogMSRoomRegistry, Display, TEXT("Room registry HTTP server stopped (port %u)"), ListenPort);
	}
	bRunning = false;
}

bool FMSRoomRegistryHttpServer::HandleRegister(
	const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FMSRoomDescriptor Descriptor;
	FText FailureReason;
	const FString Body = RequestBodyToString(Request);
	FString NormalizedBody = Body;
	TSharedPtr<FJsonObject> RegisterObject;
	const TSharedRef<TJsonReader<>> RegisterReader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(RegisterReader, RegisterObject) && RegisterObject.IsValid())
	{
		FString RequestedRoomId;
		if ((RegisterObject->TryGetStringField(TEXT("RoomId"), RequestedRoomId)
			|| RegisterObject->TryGetStringField(TEXT("roomId"), RequestedRoomId))
			&& RequestedRoomId.IsEmpty())
		{
			RegisterObject->RemoveField(TEXT("RoomId"));
			RegisterObject->RemoveField(TEXT("roomId"));
			NormalizedBody.Reset();
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&NormalizedBody);
			FJsonSerializer::Serialize(RegisterObject.ToSharedRef(), Writer);
		}
	}
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(
		NormalizedBody, &Descriptor, 0, 0, false, &FailureReason))
	{
		OnComplete(FHttpServerResponse::Error(
			EHttpServerResponseCodes::BadRequest, TEXT("invalid_descriptor"), FailureReason.ToString()));
		return true;
	}

	const FGuid RoomId = Store.RegisterRoom(MoveTemp(Descriptor), GetCurrentUnixTime());
	const FString ResponseBody = FString::Printf(
		TEXT("{\"RoomId\":\"%s\"}"), *RoomId.ToString(EGuidFormats::DigitsWithHyphens));
	TUniquePtr<FHttpServerResponse> Response =
		FHttpServerResponse::Create(ResponseBody, TEXT("application/json; charset=utf-8"));
	Response->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(Response));
	UE_LOG(LogMSRoomRegistry, Verbose, TEXT("Registered room %s"), *RoomId.ToString());
	return true;
}

bool FMSRoomRegistryHttpServer::HandleHeartbeat(
	const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FGuid RoomId;
	if (!ParseRoomId(RequestBodyToString(Request), RoomId))
	{
		OnComplete(FHttpServerResponse::Error(
			EHttpServerResponseCodes::BadRequest, TEXT("invalid_room_id"), TEXT("RoomId is required")));
		return true;
	}
	if (!Store.Heartbeat(RoomId, GetCurrentUnixTime()))
	{
		OnComplete(FHttpServerResponse::Error(
			EHttpServerResponseCodes::NotFound, TEXT("room_not_found"), TEXT("RoomId is not registered")));
		return true;
	}
	TUniquePtr<FHttpServerResponse> Response =
		FHttpServerResponse::Create(TEXT("{\"ok\":true}"), TEXT("application/json; charset=utf-8"));
	Response->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMSRoomRegistryHttpServer::HandleUnregister(
	const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FGuid RoomId;
	if (!ParseRoomId(RequestBodyToString(Request), RoomId))
	{
		OnComplete(FHttpServerResponse::Error(
			EHttpServerResponseCodes::BadRequest, TEXT("invalid_room_id"), TEXT("RoomId is required")));
		return true;
	}
	Store.Unregister(RoomId);
	TUniquePtr<FHttpServerResponse> Response =
		FHttpServerResponse::Create(TEXT("{\"ok\":true}"), TEXT("application/json; charset=utf-8"));
	Response->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMSRoomRegistryHttpServer::HandleGetRooms(
	const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
{
	const TArray<FMSRoomDescriptor> Rooms = Store.GetRooms();
	FString ResponseBody(TEXT("["));
	for (int32 Index = 0; Index < Rooms.Num(); ++Index)
	{
		FString RoomJson;
		if (!FJsonObjectConverter::UStructToJsonObjectString(Rooms[Index], RoomJson, 0, 0, 0, nullptr, false))
		{
			OnComplete(FHttpServerResponse::Error(
				EHttpServerResponseCodes::ServerError, TEXT("serialization_failed")));
			return true;
		}
		if (Index > 0)
		{
			ResponseBody += TEXT(",");
		}
		ResponseBody += RoomJson;
	}
	ResponseBody += TEXT("]");

	TUniquePtr<FHttpServerResponse> Response =
		FHttpServerResponse::Create(ResponseBody, TEXT("application/json; charset=utf-8"));
	Response->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMSRoomRegistryHttpServer::TickSweep(float)
{
	const int32 RemovedCount = Store.SweepExpired(GetCurrentUnixTime());
	if (RemovedCount > 0)
	{
		UE_LOG(LogMSRoomRegistry, Display, TEXT("TTL sweep removed %d expired room(s)"), RemovedCount);
	}
	return true;
}

bool FMSRoomRegistryHttpServer::TickVerificationLifetime(float)
{
	VerificationLifetimeTickerHandle.Reset();
	UE_LOG(LogMSRoomRegistry, Display,
		TEXT("Registry verification lifetime elapsed; requesting a clean process exit"));
	FPlatformMisc::RequestExit(false);
	return false;
}

double FMSRoomRegistryHttpServer::GetCurrentUnixTime()
{
	// Keep sub-second precision so a room cannot expire almost one second before
	// the advertised 15-second TTL at a Unix-second boundary.
	return FDateTime::UtcNow().ToUnixTimestampDecimal();
}

FString FMSRoomRegistryHttpServer::RequestBodyToString(const FHttpServerRequest& Request)
{
	if (Request.Body.IsEmpty())
	{
		return FString();
	}
	const FUTF8ToTCHAR Converted(
		reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	return FString(Converted.Length(), Converted.Get());
}

bool FMSRoomRegistryHttpServer::ParseRoomId(const FString& Body, FGuid& OutRoomId)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		FString RoomIdText;
		if ((JsonObject->TryGetStringField(TEXT("RoomId"), RoomIdText)
			|| JsonObject->TryGetStringField(TEXT("roomId"), RoomIdText))
			&& FGuid::Parse(RoomIdText, OutRoomId))
		{
			return true;
		}
	}

	FString RawValue = Body;
	RawValue.TrimStartAndEndInline();
	RawValue.RemoveFromStart(TEXT("\""));
	RawValue.RemoveFromEnd(TEXT("\""));
	return FGuid::Parse(RawValue, OutRoomId);
}
