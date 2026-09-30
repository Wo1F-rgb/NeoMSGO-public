// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Online/MSRoomSubsystem.h"
#include "UI/MSRoomCreateWidget.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomSettingsTest, "NeoMSGO.Online.Room.SettingsAndAddress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRoomCreateWidgetTest, "NeoMSGO.UI.Room.CodeOnlyControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSRoomSettingsTest::RunTest(const FString&)
{
	FMSRoomSettings Settings;
	Settings.RoomName = TEXT("Test Room");
	Settings.MapId = TEXT("NewYark");
	Settings.RuleIndex = 2;
	Settings.BotCount = 11;
	Settings.BotDifficulty = EMSBotDifficulty::Hard;
	TestEqual(TEXT("room name is stored"), Settings.RoomName, FString(TEXT("Test Room")));
	TestEqual(TEXT("map id is stored"), Settings.MapId, FName(TEXT("NewYark")));
	TestEqual(TEXT("rule index is stored"), Settings.RuleIndex, 2);
	TestEqual(TEXT("bot count supports the accepted maximum"), Settings.BotCount, 11);
	TestTrue(TEXT("room settings default to filling both teams with bots"), FMSRoomSettings().bFillTeamsWithBots);
	TestEqual(TEXT("filled rooms expose fifty combatant seats"), FMSRoomSettings().GetMaxCombatants(), MSRoomPopulation::MaxCombatants);
	Settings.bFillTeamsWithBots = false;
	TestEqual(TEXT("manual rooms keep the legacy twelve combatant limit"), Settings.GetMaxCombatants(), MSRoomPopulation::LegacyMaxCombatants);
	TestEqual(TEXT("legacy Hard difficulty keeps enum value 2"), static_cast<int32>(Settings.BotDifficulty), 2);
	Settings.BotDifficulty = EMSBotDifficulty::Mixed;
	TestEqual(TEXT("Mixed difficulty uses new enum value 3"), static_cast<int32>(Settings.BotDifficulty), 3);
	FString Address;
	TestTrue(TEXT("loopback address is accepted"), UMSRoomSubsystem::NormalizeAddress(TEXT(" 127.0.0.1 "), Address));
	TestEqual(TEXT("address is trimmed"), Address, FString(TEXT("127.0.0.1")));
	TestFalse(TEXT("travel option injection is rejected"), UMSRoomSubsystem::NormalizeAddress(TEXT("127.0.0.1?listen"), Address));
	return true;
}

bool FMSRoomCreateWidgetTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSRoomCreateWidget> Widget(NewObject<UMSRoomCreateWidget>(GetTransientPackage()));
	Widget->Initialize();
	Widget->InitializeForAutomationTests();
	TestNotNull(TEXT("room start button exists"), Widget->GetRoomStartButton());
	TestNotNull(TEXT("join button exists"), Widget->GetJoinButton());
	TestNotNull(TEXT("room name input exists"), Widget->GetRoomNameInput());
	TestNotNull(TEXT("difficulty selector exists"), Widget->GetDifficultyComboBox());
	TestEqual(TEXT("difficulty selector includes Mixed"), Widget->GetDifficultyComboBox()->GetOptionCount(), 4);
	TestTrue(TEXT("Mixed label states the tentative ratio"), Widget->GetDifficultyComboBox()->GetOptionAtIndex(3).Contains(TEXT("60%")));
	TestNotNull(TEXT("25v25 fill checkbox exists"), Widget->GetFillTeamsWithBotsCheckBox());
	TestTrue(TEXT("25v25 fill checkbox starts enabled"), Widget->GetFillTeamsWithBotsCheckBox()->IsChecked());
	TestFalse(TEXT("manual enemy-BOT count is disabled while filling"), Widget->GetBotCountSpinBox()->GetIsEnabled());
	Widget->GetFillTeamsWithBotsCheckBox()->SetIsChecked(false);
	// SetIsChecked is only a property setter; a real Slate click also emits this event.
	Widget->GetFillTeamsWithBotsCheckBox()->OnCheckStateChanged.Broadcast(false);
	TestTrue(TEXT("manual enemy-BOT count is enabled when fill is disabled"), Widget->GetBotCountSpinBox()->GetIsEnabled());
	Widget->GetBotCountSpinBox()->SetValue(11.0f);
	Widget->GetFillTeamsWithBotsCheckBox()->SetIsChecked(true);
	Widget->GetFillTeamsWithBotsCheckBox()->OnCheckStateChanged.Broadcast(true);
	TestFalse(TEXT("switching back to 25v25 disables the manual count"), Widget->GetBotCountSpinBox()->GetIsEnabled());
	TestEqual(TEXT("switching modes preserves the manual count for later"), Widget->GetBotCountSpinBox()->GetValue(), 11.0f);
	TestEqual(TEXT("join defaults to loopback"), Widget->GetJoinAddressInput()->GetText().ToString(),
		FString(TEXT("127.0.0.1")));
	TestEqual(TEXT("bot minimum is zero"), Widget->GetBotCountSpinBox()->GetMinValue(), 0.0f);
	TestEqual(TEXT("bot maximum is eleven"), Widget->GetBotCountSpinBox()->GetMaxValue(), 11.0f);
	return true;
}

#endif
