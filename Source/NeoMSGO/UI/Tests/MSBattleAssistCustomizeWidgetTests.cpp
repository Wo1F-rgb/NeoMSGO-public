#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/GameInstance.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSBattleAssistCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistCustomizeLayoutTest,
	"NeoMSGO.UI.BattleAssistCustomize.LayoutAndUnsupportedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistCustomizeTimePreviewTest,
	"NeoMSGO.UI.BattleAssistCustomize.EffectiveTimePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistCustomizeAccountCancelTest,
	"NeoMSGO.UI.BattleAssistCustomize.AccountChangeCancelsPendingCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSBattleAssistCustomizeTests
{
	struct FFixture
	{
		TStrongObjectPtr<UMSBattleAssistRulesData> Rules;
		TStrongObjectPtr<UMSBattleAssistModuleData> Supported;
		TStrongObjectPtr<UMSBattleAssistModuleData> Unsupported;
		TArray<TObjectPtr<UMSBattleAssistModuleData>> PageModules;
		TStrongObjectPtr<UMSMobileSuitData> Suit;
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UMSBattleAssistProgressSubsystem> Progress;

		FFixture()
		{
			Rules.Reset(NewObject<UMSBattleAssistRulesData>(GetTransientPackage()));
			Supported.Reset(NewObject<UMSBattleAssistModuleData>(Rules.Get(), TEXT("SupportedBattleAssist")));
			Supported->DisplayName = FText::FromString(TEXT("クイックロックオン"));
			Supported->Description = FText::FromString(TEXT("ロックオン時間を短縮する"));
			Supported->Category = EMSModuleCategory::Assist;
			Supported->FamilyId = TEXT("lock"); Supported->SlotCost = 2;
			Supported->Support = EMSModuleSupport::Implemented; Supported->Evidence = EMSModuleEvidence::Confirmed;
			FMSBattleAssistEffect Effect; Effect.Kind = EMSBattleAssistEffectKind::MissileLockTimeReduction; Effect.Value = .1f; Effect.Unit = TEXT("seconds");
			Supported->Effects.Add(Effect); Supported->bRecipeKnown = true; Supported->DevelopmentCostGP = 120;

			Unsupported.Reset(NewObject<UMSBattleAssistModuleData>(Rules.Get(), TEXT("UnresolvedBattleAssist")));
			Unsupported->DisplayName = FText::FromString(TEXT("未確定モジュール")); Unsupported->Category = EMSModuleCategory::Attack;
			Unsupported->FamilyId = TEXT("unknown"); Unsupported->Support = EMSModuleSupport::RequiresFoundation; Unsupported->Evidence = EMSModuleEvidence::Unknown;

			Rules->Modules.Add(Supported.Get()); Rules->Modules.Add(Unsupported.Get());
			for (int32 Index = 0; Index < 7; ++Index)
			{
				UMSBattleAssistModuleData* PageModule = NewObject<UMSBattleAssistModuleData>(Rules.Get(), *FString::Printf(TEXT("PageBattleAssist%d"), Index));
				PageModule->DisplayName = FText::FromString(FString::Printf(TEXT("強化モジュール%d"), Index + 1)); PageModule->Category = EMSModuleCategory::Attack;
				PageModule->FamilyId = *FString::Printf(TEXT("page_family_%d"), Index); PageModule->SlotCost = 1;
				FMSBattleAssistEffect Armor; Armor.Kind = EMSBattleAssistEffectKind::BallisticDamageReduction; Armor.Value=6; Armor.Unit=TEXT("percent");
				PageModule->Support = EMSModuleSupport::Implemented; PageModule->Evidence = EMSModuleEvidence::Confirmed; PageModule->Effects.Add(Armor);
				PageModules.Add(PageModule); Rules->Modules.Add(PageModule);
			}
			Suit.Reset(NewObject<UMSMobileSuitData>(GetTransientPackage(), TEXT("BattleAssistTestSuit")));
			Suit->UnitName = FText::FromString(TEXT("TEST MS")); Suit->Cost = 300; Suit->ReuseDelaySeconds = 30.0f;
			FMSBattleAssistSuitProfile Profile; Profile.SuitId = Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity = 6; Rules->SuitProfiles.Add(Profile);
			FMSBattleAssistTimeRow Row; Row.Cost = Suit->Cost; Row.TotalSeconds = { 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110 }; Rules->TimeRows.Add(Row);
			GameInstance.Reset(NewObject<UGameInstance>(GetTransientPackage())); Progress.Reset(NewObject<UMSBattleAssistProgressSubsystem>(GameInstance.Get()));
			FMSBattleAssistProgress Value; Value.DevelopedModuleIds.Add(Supported->GetPrimaryAssetId()); Value.GP = 1000; Progress->SetProgressForTests(Value);
		for (UMSBattleAssistModuleData* PageModule : PageModules) Value.DevelopedModuleIds.Add(PageModule->GetPrimaryAssetId());
		Progress->SetProgressForTests(Value);
			MSBattleAssist::SetRulesDataForTests(Rules.Get());
		}

		~FFixture() { MSBattleAssist::SetRulesDataForTests(nullptr); }
	};

	template <typename T>
	TStrongObjectPtr<T> MakeWidget()
	{
		TStrongObjectPtr<T> Widget(NewObject<T>(GetTransientPackage())); Widget->Initialize(); Widget->InitializeForAutomationTests(); return Widget;
	}
}

bool FMSBattleAssistCustomizeLayoutTest::RunTest(const FString&)
{
	MSBattleAssistCustomizeTests::FFixture Fixture;
	TStrongObjectPtr<UMSBattleAssistCustomizeWidget> Widget = MSBattleAssistCustomizeTests::MakeWidget<UMSBattleAssistCustomizeWidget>();
	Widget->SetProgressSubsystemForTesting(Fixture.Progress.Get()); Widget->Open(Fixture.Suit.Get(), 0);
	TestEqual(TEXT("default 強化 tab exposes only its mapped entries"), Widget->GetDisplayedTab(), 0);
	TestTrue(TEXT("other tab exposes unresolved entries"), CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("BattleAssistTab3")))->GetIsEnabled());
	TestTrue(TEXT("unresolved entries remain visible for evidence review"), Widget->GetDisplayedModuleCount() > 0);
	TestTrue(TEXT("unsupported module explains read-only state"), Widget->GetDisplayedReasonText().ToString().Contains(TEXT("閲覧のみ")));
	TestEqual(TEXT("opening a page starts with its first row selected"), Widget->GetDisplayedSelectedRow(), 0);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("BattleAssistNextPageButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("advancing a page deliberately clears the previous row selection"), Widget->GetDisplayedSelectedRow(), INDEX_NONE);
	TestTrue(TEXT("unselected page does not retain an old action reason"), Widget->GetDisplayedReasonText().IsEmpty());
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("BattleAssistPreviousPageButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("returning to a page keeps selection explicit"), Widget->GetDisplayedSelectedRow(), INDEX_NONE);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("BattleAssistTab2")))->OnClicked.Broadcast();
	TestEqual(TEXT("補助 tab contains the supported lock-on entry"), Widget->GetDisplayedModuleCount(), 1);
	TestNotNull(TEXT("six-page row control exists"), Widget->GetWidgetFromName(TEXT("BattleAssistModuleRow5")));
	TestNotNull(TEXT("development control exists for recipe-backed entries"), Widget->GetWidgetFromName(TEXT("BattleAssistDevelopButton")));
	return true;
}

bool FMSBattleAssistCustomizeTimePreviewTest::RunTest(const FString&)
{
	MSBattleAssistCustomizeTests::FFixture Fixture;
	FMSBattleAssistLoadout Empty;
	TestEqual(TEXT("empty loadout preserves base TIME"), MSBattleAssistUI::ResolveEffectiveTime(Fixture.Suit.Get(), Empty, nullptr), FString(TEXT("30")));
	FMSBattleAssistLoadout Equipped; Equipped.ModuleIds.Add(Fixture.Supported->GetPrimaryAssetId());
	const FMSBattleAssistProgress Progress = Fixture.Progress->GetProgress();
	TestEqual(TEXT("occupied slots use the rules table delta"), MSBattleAssistUI::ResolveEffectiveTime(Fixture.Suit.Get(), Equipped, &Progress), FString(TEXT("46")));
	FMSBattleAssistLoadout Locked; Locked.ModuleIds.Add(Fixture.Unsupported->GetPrimaryAssetId());
	TestEqual(TEXT("unsupported loadout has no fabricated TIME"), MSBattleAssistUI::ResolveEffectiveTime(Fixture.Suit.Get(), Locked, &Progress), FString(TEXT("---")));
	return true;
}

bool FMSBattleAssistCustomizeAccountCancelTest::RunTest(const FString&)
{
	MSBattleAssistCustomizeTests::FFixture Fixture;
	TStrongObjectPtr<UMSBattleAssistCustomizeWidget> Widget = MSBattleAssistCustomizeTests::MakeWidget<UMSBattleAssistCustomizeWidget>();
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Fixture.GameInstance.Get()));
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(Fixture.GameInstance.Get()));
	Deck->AssignPreset(0, Fixture.Suit.Get());
	Widget->SetSuitForTesting(Fixture.Suit.Get(), 0); Widget->SetDeckSubsystemForTesting(Deck.Get()); Widget->SetOnlineAccountForTesting(Account.Get()); Widget->Open(Fixture.Suit.Get(), 0);
	FMSBattleAssistLoadout Candidate; Candidate.ModuleIds.Add(Fixture.Supported->GetPrimaryAssetId());
	Widget->BeginPendingCommitForAutomationTests(Candidate, 10);
	TestTrue(TEXT("a pending commit is visible before account change"), Widget->IsCommitPendingForAutomationTests());
	FMSOnlineIdentity Identity; Identity.AccountId = TEXT("new-account");
	Account->OnAccountChanged.Broadcast(Identity, 11);
	TestFalse(TEXT("account change clears the pending state immediately"), Widget->IsCommitPendingForAutomationTests());
	TestTrue(TEXT("account change exposes a concrete cancellation reason"), Widget->GetDisplayedReasonText().ToString().Contains(TEXT("アカウントが切り替わった")));
	Widget->CompletePendingCommitForAutomationTests(true);
	TestTrue(TEXT("a delayed old success cannot overwrite the cancellation reason"), Widget->GetDisplayedReasonText().ToString().Contains(TEXT("アカウントが切り替わった")));

	Widget->BeginPendingCommitForAutomationTests(Candidate, INDEX_NONE);
	Deck->OnDeckChanged.Broadcast(INDEX_NONE);
	TestTrue(TEXT("a normal deck broadcast does not cancel an offline pending request"), Widget->IsCommitPendingForAutomationTests());
	Widget->CompletePendingCommitForAutomationTests(true);
	TestFalse(TEXT("a normal completion still clears pending state"), Widget->IsCommitPendingForAutomationTests());
	return true;
}

#endif
