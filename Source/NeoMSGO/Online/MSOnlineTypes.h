#pragma once

#include "CoreMinimal.h"
#include "Data/MSBattleAssistTypes.h"
#include "MSOnlineTypes.generated.h"

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSOnlineIdentity
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString AccountId;
	UPROPERTY(BlueprintReadOnly) FString RealmId;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) bool bIsGuest = false;
	bool IsValid() const { return !AccountId.IsEmpty() && !RealmId.IsEmpty(); }
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSOnlineDeckSlot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FPrimaryAssetId SuitId;
	UPROPERTY(BlueprintReadOnly) TArray<FPrimaryAssetId> WeaponIds;
	UPROPERTY(BlueprintReadOnly) bool bShieldEquipped = false;
	UPROPERTY(BlueprintReadOnly) FMSBattleAssistLoadout BattleAssist;
	// A newly created slot is empty; an occupied suit gets its native default in
	// the deck builder or from the explicit schema2 field.
	FMSOnlineDeckSlot() { WeaponIds.SetNum(3); bShieldEquipped = false; }
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSOnlineDeckSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 SchemaVersion = 3;
	UPROPERTY(BlueprintReadOnly) int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly) int32 SelectedSlot = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FMSOnlineDeckSlot> Slots;
	FMSOnlineDeckSnapshot() { Slots.SetNum(4); }
};
