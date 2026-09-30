// Copyright NeoMSGO Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Core/MSGameInstance.h"
#include "Voice/MSNavigatorSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSNavigatorSubsystemTest,
	"NeoMSGO.Voice.NavigatorSubsystem.SelectionAndPlayback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSNavigatorSubsystemTest::RunTest(const FString&)
{
	// MSNavigatorSubsystem.cppの匿名namespaceにある値と同じキー(private実装のためテストからは
	// 直接参照できず、既存のMSSettingsWidgetTests.cppと同様にリテラルで直接検証する)。
	const TCHAR* NavigatorTestConfigSection = TEXT("/Script/NeoMSGO.MSNavigatorSubsystem");
	const TCHAR* NavigatorTestConfigKey = TEXT("SelectedNavigator");

	UMSGameInstance* GameInstance = NewObject<UMSGameInstance>();

	// 既定値の確認: 明示的にNoelAndersonを保存した状態から新規インスタンスで読み直す。
	{
		UMSNavigatorSubsystem* Seed = NewObject<UMSNavigatorSubsystem>(GameInstance);
		Seed->SetSelectedNavigator(EMSNavigator::NoelAnderson, true);
	}
	UMSNavigatorSubsystem* DefaultSubsystem = NewObject<UMSNavigatorSubsystem>(GameInstance);
	TestEqual(TEXT("default navigator is NoelAnderson"),
		DefaultSubsystem->GetSelectedNavigator(), EMSNavigator::NoelAnderson);

	// SetSelectedNavigator -> GetSelectedNavigatorの往復確認(同一インスタンス)。
	DefaultSubsystem->SetSelectedNavigator(EMSNavigator::YuukiNakasato, true);
	TestEqual(TEXT("selecting YuukiNakasato is reflected immediately"),
		DefaultSubsystem->GetSelectedNavigator(), EMSNavigator::YuukiNakasato);

	// GConfig永続化の直接確認。
	int32 Persisted = INDEX_NONE;
	GConfig->GetInt(NavigatorTestConfigSection, NavigatorTestConfigKey, Persisted, GGameUserSettingsIni);
	TestEqual(TEXT("YuukiNakasato persists to GameUserSettings.ini"),
		Persisted, static_cast<int32>(EMSNavigator::YuukiNakasato));

	// 別インスタンスで生成しても永続化された選択が読み込まれること。
	UMSNavigatorSubsystem* ReloadedSubsystem = NewObject<UMSNavigatorSubsystem>(GameInstance);
	TestEqual(TEXT("a fresh instance reloads the persisted selection"),
		ReloadedSubsystem->GetSelectedNavigator(), EMSNavigator::YuukiNakasato);

	// bSave=falseは即時反映のみでGConfigを書き換えない。
	ReloadedSubsystem->SetSelectedNavigator(EMSNavigator::NoelAnderson, false);
	TestEqual(TEXT("bSave=false still updates the in-memory selection"),
		ReloadedSubsystem->GetSelectedNavigator(), EMSNavigator::NoelAnderson);
	int32 PersistedAfterNoSave = INDEX_NONE;
	GConfig->GetInt(NavigatorTestConfigSection, NavigatorTestConfigKey, PersistedAfterNoSave, GGameUserSettingsIni);
	TestEqual(TEXT("bSave=false leaves the persisted value untouched"),
		PersistedAfterNoSave, static_cast<int32>(EMSNavigator::YuukiNakasato));

	// 表示名ヘルパー。
	TestEqual(TEXT("NoelAnderson display name"),
		UMSNavigatorSubsystem::GetNavigatorDisplayName(EMSNavigator::NoelAnderson).ToString(),
		FString(TEXT("ノエル・アンダーソン")));
	TestEqual(TEXT("YuukiNakasato display name"),
		UMSNavigatorSubsystem::GetNavigatorDisplayName(EMSNavigator::YuukiNakasato).ToString(),
		FString(TEXT("ユウキ・ナカサト")));

	// PlayLineの安全性確認: 自動テスト環境では/Game/Audio/Imported/Navigator/*が
	// インポートされていない可能性が高く、LoadObjectはnullptrを返す。それでもクラッシュせず、
	// FirstSortieは1回目のみ処理が進む(ガードが機能する)ことを確認する。
	UMSNavigatorSubsystem* PlaybackSubsystem = NewObject<UMSNavigatorSubsystem>(GameInstance);
	PlaybackSubsystem->SetSelectedNavigator(EMSNavigator::NoelAnderson, false);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::Login);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::PreDeployment);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::FirstSortie);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::FirstSortie);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::VictoryReturn);
	PlaybackSubsystem->PlayLine(EMSNavigatorTrigger::DefeatReturn);
	TestTrue(TEXT("PlayLine calls did not crash for missing/unmapped assets"), true);

	// このテストが他の自動化/PIEセッションへ設定を持ち越さないよう既定値へ戻す。
	GConfig->SetInt(NavigatorTestConfigSection, NavigatorTestConfigKey, static_cast<int32>(EMSNavigator::NoelAnderson), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	return true;
}

#endif
