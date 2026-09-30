#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleGameplayTagsTest, "NeoMSGO.Core.GameplayTags.ModuleNativeTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSModuleGameplayTagsTest::RunTest(const FString&)
{
	TestEqual(TEXT("effect tag"), MSTags::State_Module_EffectActive.GetTag().ToString(), FString(TEXT("State.Module.EffectActive")));
	TestEqual(TEXT("forced overheat tag"), MSTags::State_Module_ForcedOverheat.GetTag().ToString(), FString(TEXT("State.Module.ForcedOverheat")));
	TestEqual(TEXT("cooldown state tag"), MSTags::State_Module_Cooldown.GetTag().ToString(), FString(TEXT("State.Module.Cooldown")));
	TestEqual(TEXT("duration data tag"), MSTags::Data_Module_Duration.GetTag().ToString(), FString(TEXT("Data.Module.Duration")));
	TestEqual(TEXT("overheat duration data tag"), MSTags::Data_Module_OverheatDuration.GetTag().ToString(), FString(TEXT("Data.Module.OverheatDuration")));
	TestEqual(TEXT("cooldown data tag"), MSTags::Data_Module_Cooldown.GetTag().ToString(), FString(TEXT("Data.Module.Cooldown")));
	TestEqual(TEXT("module ability tag"), MSTags::Ability_Module.GetTag().ToString(), FString(TEXT("Ability.Module")));
	return true;
}

#endif
