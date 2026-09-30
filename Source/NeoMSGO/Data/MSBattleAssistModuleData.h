#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MSBattleAssistTypes.h"
#include "Data/MSMobileSuitData.h"
#include "MSBattleAssistModuleData.generated.h"

class FDataValidationContext;

UCLASS(BlueprintType)
class NEOMSGO_API UMSBattleAssistModuleData : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	static const FPrimaryAssetType PrimaryAssetType;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MultiLine=true)) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMSModuleCategory Category = EMSModuleCategory::Assist;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FamilyId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Rank = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SlotCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<EMSMobileSuitUnitType> AllowedUnitTypes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> ConflictingFamilies;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FPrimaryAssetId> ExplicitConflictIds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FMSBattleAssistEffect> Effects;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRecipeKnown = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int64 DevelopmentCostGP = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FName,int32> DevelopmentMaterials;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SourceUrl;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SourceRevision;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMSModuleEvidence Evidence = EMSModuleEvidence::Unknown;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMSModuleSupport Support = EMSModuleSupport::UnresolvedValue;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
