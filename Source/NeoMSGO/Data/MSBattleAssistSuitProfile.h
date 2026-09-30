#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MSBattleAssistTypes.h"
#include "MSBattleAssistSuitProfile.generated.h"

class UMSBattleAssistModuleData;

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistSuitProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FPrimaryAssetId SuitId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BattleAssistSlotCapacity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BattleAssistACSlotCapacity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FPrimaryAssetId> NativeBattleAssistModuleIds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Source;
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSBattleAssistTimeRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Cost = 0;
	/** Total seconds at 0..10 used slots; -1 is unsupported, never instant respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> TotalSeconds;
};

UCLASS(BlueprintType)
class NEOMSGO_API UMSBattleAssistRulesData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RulesRevision = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UMSBattleAssistModuleData>> Modules;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FMSBattleAssistSuitProfile> SuitProfiles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FMSBattleAssistTimeRow> TimeRows;
	/** Kept false until final-era AC cost/exclusion behavior is established. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bACRulesConfirmed = false;
};
