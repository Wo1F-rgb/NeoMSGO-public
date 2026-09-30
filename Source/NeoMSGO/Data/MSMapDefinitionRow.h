// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MSMapDefinitionRow.generated.h"

/** Broad environment type used to style map-selection thumbnails. */
UENUM(BlueprintType)
enum class EMSMapBiome : uint8
{
	Colony,
	City,
	Desert,
	Forest,
	Coast,
	Port,
	Space
};

/** Code-defined map metadata shared by map selection and travel. */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSMapDefinitionRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	FName MapId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	FText DisplayName;

	/** Long package name. Empty while the corresponding level is unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	FString LevelPackagePath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	EMSMapBiome Biome = EMSMapBiome::Colony;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	FLinearColor ThumbnailColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	bool bPlayable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	bool bSpace = false;

	/** Hide archived maps from selection without breaking saved IDs or direct level travel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map")
	bool bVisibleInSelection = true;

	static const TArray<FMSMapDefinitionRow>& GetMapDefinitions();
	static const TArray<FMSMapDefinitionRow>& GetVisibleMapDefinitions();
	static const FMSMapDefinitionRow* FindMapById(FName MapId);
	static const FMSMapDefinitionRow* FindMapByLevelName(const FString& ShortLevelName);
};
