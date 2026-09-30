// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Online/MSRoomDescriptor.h"

class IHttpRouter;
struct FHttpServerRequest;

DECLARE_LOG_CATEGORY_EXTERN(LogMSRoomRegistry, Log, All);

/** Thread-safe in-memory registry separated from HTTP so TTL behavior is deterministic in tests. */
class NEOMSGO_API FMSRoomRegistryStore
{
public:
	FGuid RegisterRoom(FMSRoomDescriptor Descriptor, double NowUnixTime);
	bool Heartbeat(const FGuid& RoomId, double NowUnixTime);
	bool Unregister(const FGuid& RoomId);
	TArray<FMSRoomDescriptor> GetRooms() const;
	int32 SweepExpired(double NowUnixTime);
	void Reset();

	static constexpr double EntryTtlSeconds = 15.0;

private:
	mutable FCriticalSection RoomsMutex;
	TMap<FGuid, FMSRoomDescriptor> Rooms;
};

/** HTTP adapter used only by the -RoomRegistryServer process mode. */
class NEOMSGO_API FMSRoomRegistryHttpServer
{
public:
	~FMSRoomRegistryHttpServer();

	bool Start(uint32 Port);
	void Stop();
	bool IsRunning() const { return bRunning; }

	FMSRoomRegistryStore& GetStore() { return Store; }

private:
	bool HandleRegister(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleHeartbeat(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleUnregister(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleGetRooms(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool TickSweep(float DeltaTime);
	bool TickVerificationLifetime(float DeltaTime);

	static double GetCurrentUnixTime();
	static FString RequestBodyToString(const FHttpServerRequest& Request);
	static bool ParseRoomId(const FString& Body, FGuid& OutRoomId);

	FMSRoomRegistryStore Store;
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	FTSTicker::FDelegateHandle SweepTickerHandle;
	FTSTicker::FDelegateHandle VerificationLifetimeTickerHandle;
#if WITH_EDITOR
	FDelegateHandle StartupMapVetoHandle;
#endif
	uint32 ListenPort = 0;
	bool bRunning = false;
};
