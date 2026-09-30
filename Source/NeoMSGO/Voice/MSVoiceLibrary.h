// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Voice/MSVoiceTypes.h"

namespace MSVoiceLibrary
{
	NEOMSGO_API TArrayView<const FText> GetLines(EMSVoiceType Type, EMSVoiceTrigger Trigger);
	NEOMSGO_API FText PickLine(EMSVoiceType Type, EMSVoiceTrigger Trigger, int32 RandomSeed);
	NEOMSGO_API FText PickLine(EMSVoiceType Type, EMSVoiceTrigger Trigger, int32 RandomSeed,
		int32& OutLineIndex);
	NEOMSGO_API const TCHAR* GetVoiceTypeId(EMSVoiceType Type);
	NEOMSGO_API const TCHAR* GetTriggerId(EMSVoiceTrigger Trigger);
	NEOMSGO_API FString GetPilotVoiceAssetPath(EMSVoiceType Type, EMSVoiceTrigger Trigger, int32 LineIndex);
}
