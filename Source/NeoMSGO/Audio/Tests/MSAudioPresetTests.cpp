// Copyright NeoMSGO Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/MSAudioPreset.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAudioPresetResolutionTest,
	"NeoMSGO.Audio.Preset.ResolutionAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSAudioPresetResolutionTest::RunTest(const FString&)
{
	USoundBase* TypeA = NewObject<USoundWave>();
	TestEqual(TEXT("TypeA always returns the authored sound"),
		MSAudioPreset::ResolveForPreset(EMSAudioUsage::FireBeamRifle, EMSAudioPreset::TypeA, TypeA), TypeA);

	USoundBase* TypeB = MSAudioPreset::ResolveForPreset(
		EMSAudioUsage::FireBeamRifle, EMSAudioPreset::TypeB, TypeA);
	TestNotNull(TEXT("mapped TypeB asset loads"), TypeB);
	TestNotEqual(TEXT("mapped TypeB replaces TypeA"), TypeB, TypeA);
	if (TypeB)
	{
		TestTrue(TEXT("resolved object comes from TypeB content"),
			TypeB->GetPathName().StartsWith(TEXT("/Game/Audio/Imported/TypeB/")));
	}

	TestEqual(TEXT("unmapped TypeB usage falls back to TypeA"),
		MSAudioPreset::ResolveForPreset(EMSAudioUsage::UIBack, EMSAudioPreset::TypeB, TypeA), TypeA);
	TestEqual(TEXT("null TypeA remains safe for an unmapped usage"),
		MSAudioPreset::ResolveForPreset(EMSAudioUsage::ModuleActivate, EMSAudioPreset::TypeB, nullptr),
		static_cast<USoundBase*>(nullptr));

	int32 MappedUsageCount = 0;
	const UEnum* UsageEnum = StaticEnum<EMSAudioUsage>();
	for (int32 Index = 0; Index < UsageEnum->NumEnums() - 1; ++Index)
	{
		const EMSAudioUsage Usage = static_cast<EMSAudioUsage>(UsageEnum->GetValueByIndex(Index));
		const TCHAR* Path = MSAudioPreset::GetTypeBAssetPath(Usage);
		if (Path && Path[0])
		{
			++MappedUsageCount;
			TestNotNull(*FString::Printf(TEXT("mapped asset loads: %s"), Path),
				MSAudioPreset::ResolveForPreset(Usage, EMSAudioPreset::TypeB, nullptr));
		}
	}
	TestTrue(TEXT("combat, movement, UI and ambience usages are covered"), MappedUsageCount >= 30);
	return true;
}

#endif
