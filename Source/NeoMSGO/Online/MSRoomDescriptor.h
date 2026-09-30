// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/MSRoomSubsystem.h"
#include "MSRoomDescriptor.generated.h"

UENUM(BlueprintType)
enum class EMSRoomState : uint8
{
	Waiting,
	InProgress,
	Full,
	Unresponsive
};

/** Public discovery data only. Authentication and account credentials never belong here. */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSRoomDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FGuid RoomId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString InstanceId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString BuildId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString CatalogHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FMSRoomSettings Settings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString HostDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	FString Address;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	int32 CurrentPlayerCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	int32 MaxPlayerCount = MSRoomPopulation::MaxCombatants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	EMSRoomState State = EMSRoomState::Waiting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Room")
	double LastHeartbeatUnixTime = 0.0;
};
