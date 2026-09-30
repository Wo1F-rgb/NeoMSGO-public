// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MSVoiceTypes.generated.h"

UENUM(BlueprintType)
enum class EMSVoiceType : uint8
{
	Hotblood,
	Serious,
	Shy
};

UENUM(BlueprintType)
enum class EMSVoiceTrigger : uint8
{
	Sortie,
	Deployed,
	Kill,
	KillStreak,
	CaptureBase,
	Damaged,
	Death,
	Repair,
	SupplyPickup,
	EmergencyRecall
};

UENUM(BlueprintType)
enum class EMSVoiceCutInChannel : uint8
{
	Left,
	Right
};

USTRUCT(BlueprintType)
struct NEOMSGO_API FMSVoiceCutInRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	FText SpeakerName;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	EMSVoiceType VoiceType = EMSVoiceType::Hotblood;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	EMSVoiceTrigger Trigger = EMSVoiceTrigger::Sortie;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	FText Line;

	/** Stable zero-based index into MSVoiceLibrary::GetLines, also used by the imported WAV name. */
	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	int32 LineIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	EMSVoiceCutInChannel Channel = EMSVoiceCutInChannel::Left;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	FLinearColor PortraitColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	int32 Priority = 0;

	UPROPERTY(BlueprintReadOnly, Category="MS|Voice")
	double RequestTime = 0.0;
};
