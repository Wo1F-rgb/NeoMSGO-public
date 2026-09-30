#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSBattleAssistRules.h"
#include "UI/MSDeckSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistProgressTransactionTest, "NeoMSGO.Modules.Progress.AtomicDevelopment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistProgressTransactionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSBattleAssistProgressSubsystem> Progress(NewObject<UMSBattleAssistProgressSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSBattleAssistRulesData> Rules(NewObject<UMSBattleAssistRulesData>());
	TStrongObjectPtr<UMSBattleAssistModuleData> Module(NewObject<UMSBattleAssistModuleData>());
	Module->FamilyId = TEXT("test_development"); Module->Support = EMSModuleSupport::Implemented;
	Module->Evidence = EMSModuleEvidence::Confirmed; Module->bRecipeKnown = true;
	Module->DevelopmentCostGP = 100; Module->DevelopmentMaterials.Add(TEXT("circuit"), 2);
	FMSBattleAssistEffect Effect; Effect.Value = 4; Effect.Unit = TEXT("percent"); Module->Effects.Add(Effect);
	Rules->Modules.Add(Module.Get()); MSBattleAssist::SetRulesDataForTests(Rules.Get());
	const FString Slot = TEXT("BattleAssistDevelopmentTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Progress->SetSaveSlotForTests(Slot);
	ON_SCOPE_EXIT { MSBattleAssist::SetRulesDataForTests(nullptr); UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	FMSBattleAssistProgress Initial; Initial.GP = 1000; Initial.Materials.Add(TEXT("circuit"), 5);
	Progress->SetProgressForTests(Initial);
	TestTrue(TEXT("development writes before publishing state"), Progress->TryDevelopModule(Module->GetPrimaryAssetId()));
	TestEqual(TEXT("one GP cost"), Progress->GetProgress().GP, int64(900));
	TestEqual(TEXT("one material cost"), Progress->GetProgress().Materials.FindRef(TEXT("circuit")), 3);
	TestTrue(TEXT("idempotent retry"), Progress->TryDevelopModule(Module->GetPrimaryAssetId()));
	TestEqual(TEXT("retry does not charge"), Progress->GetProgress().GP, int64(900));
	TestEqual(TEXT("retry does not increment revision"), Progress->GetProgress().Revision, int64(1));
	TStrongObjectPtr<UMSBattleAssistProgressSubsystem> Reopened(NewObject<UMSBattleAssistProgressSubsystem>(Instance.Get()));
	Reopened->SetSaveSlotForTests(Slot);
	TestTrue(TEXT("separate subsystem reloads persisted ledger"), Reopened->ReloadProgress());
	TestTrue(TEXT("development survives reload"), Reopened->GetProgress().DevelopedModuleIds.Contains(Module->GetPrimaryAssetId()));
	Initial.Materials[TEXT("circuit")] = 1; Progress->SetProgressForTests(Initial);
	TestFalse(TEXT("insufficient material does not partially charge GP"), Progress->TryDevelopModule(Module->GetPrimaryAssetId()));
	TestEqual(TEXT("insufficient failure retains GP"), Progress->GetProgress().GP, int64(1000));
	Initial.Materials[TEXT("circuit")] = 5; Progress->SetProgressForTests(Initial);
	const TArray<FMSBattleAssistEffect> OriginalEffects = Module->Effects;
	const FName OriginalFamily = Module->FamilyId;
	for (int32 Corruption = 0; Corruption < 8; ++Corruption)
	{
		Module->Effects = OriginalEffects; Module->FamilyId = OriginalFamily; Module->SlotCost = 1;
		switch (Corruption)
		{
		case 0: Module->Effects.Reset(); break;
		case 1: Module->Effects[0].Value = -1.f; break;
		case 2: Module->Effects[0].Unit = NAME_None; break;
		case 3: Module->FamilyId = NAME_None; break;
		case 4: Module->SlotCost = 0; break;
		case 5: Module->SlotCost = 11; break;
		case 6: Module->SlotCost = -1; break;
		case 7: Module->Effects[0].Value = std::numeric_limits<float>::quiet_NaN(); break;
		}
		TestFalse(TEXT("corrupt module definition cannot consume a development transaction"), Progress->TryDevelopModule(Module->GetPrimaryAssetId()));
		const FMSBattleAssistProgress Rejected = Progress->GetProgress();
		TestTrue(TEXT("corrupt definition preserves complete wallet and unlock ledger"), Rejected.GP == Initial.GP
			&& Rejected.Materials.OrderIndependentCompareEqual(Initial.Materials)
			&& Rejected.DevelopedModuleIds == Initial.DevelopedModuleIds && Rejected.Revision == Initial.Revision);
	}
	Module->Effects = OriginalEffects; Module->FamilyId = OriginalFamily; Module->SlotCost = 1;
	Progress->SetSaveSlotForTests(TEXT("Invalid:SaveSlot"));
	TestFalse(TEXT("save failure rejects development"), Progress->TryDevelopModule(Module->GetPrimaryAssetId()));
	TestEqual(TEXT("save failure retains GP"), Progress->GetProgress().GP, int64(1000));
	TestTrue(TEXT("save failure retains unlock set"), Progress->GetProgress().DevelopedModuleIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistProgressParseTest, "NeoMSGO.Modules.Progress.StrictResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistProgressParseTest::RunTest(const FString&)
{
	FMSBattleAssistProgress Parsed;
	const FString Valid = TEXT("{\"gp\":100,\"revision\":2,\"developed_module_ids\":[\"MSBattleAssistModuleData:Test\"],\"materials\":{\"chip_1\":3}}");
	TestTrue(TEXT("valid server-owned ledger"), UMSBattleAssistProgressSubsystem::ParseProgressForTests(Valid, Parsed));
	TestEqual(TEXT("correct ledger balance"), Parsed.GP, int64(100));
	for (const FString& Invalid : { Valid.Replace(TEXT("\"gp\":100"), TEXT("\"gp\":-1")),
		Valid.Replace(TEXT("\"gp\":100"), TEXT("\"gp\":\"100\"")),
		Valid.Replace(TEXT("\"revision\":2"), TEXT("\"revision\":0.5")),
		Valid.Replace(TEXT("MSBattleAssistModuleData:Test"), TEXT("MSWeaponData:Test")),
		Valid.Replace(TEXT("\"chip_1\":3"), TEXT("\"chip_1\":-1")),
		Valid.Replace(TEXT("\"gp\":100"), TEXT("\"gp\":100,\"extra\":true")) })
	{
		TestFalse(TEXT("malformed response rejected"), UMSBattleAssistProgressSubsystem::ParseProgressForTests(Invalid, Parsed));
		TestEqual(TEXT("malformed response leaves previous result"), Parsed.GP, int64(100));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistProgressRevisionTest, "NeoMSGO.Modules.Progress.ExplicitRevisionAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistProgressRevisionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSBattleAssistProgressSubsystem> Progress(NewObject<UMSBattleAssistProgressSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSBattleAssistRulesData> Rules(NewObject<UMSBattleAssistRulesData>());
	TStrongObjectPtr<UMSBattleAssistModuleData> First(NewObject<UMSBattleAssistModuleData>());
	TStrongObjectPtr<UMSBattleAssistModuleData> Second(NewObject<UMSBattleAssistModuleData>());
	for (auto* Module : { First.Get(), Second.Get() })
	{
		Module->FamilyId = Module->GetFName(); Module->Support = EMSModuleSupport::Implemented;
		Module->Evidence = EMSModuleEvidence::Confirmed; Module->bRecipeKnown = true;
		Module->DevelopmentCostGP = 100; Module->DevelopmentMaterials.Add(TEXT("circuit"), 2);
		FMSBattleAssistEffect Effect; Effect.Value = 4; Effect.Unit = TEXT("percent"); Module->Effects.Add(Effect);
		Rules->Modules.Add(Module);
	}
	MSBattleAssist::SetRulesDataForTests(Rules.Get());
	const FString Slot = TEXT("BattleAssistRevisionTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Progress->SetSaveSlotForTests(Slot);
	ON_SCOPE_EXIT { MSBattleAssist::SetRulesDataForTests(nullptr); UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	FMSBattleAssistProgress Initial; Initial.GP = 1000; Initial.Materials.Add(TEXT("circuit"), 10); Initial.Revision = 7;
	Progress->SetProgressForTests(Initial);
	int32 Calls = 0; bool Result = false; bool CommittedAtCallback = false;
	TestTrue(TEXT("caller A compare-and-swap succeeds"), Progress->TryDevelopModule(First->GetPrimaryAssetId(), 7,
		[&](bool bSuccess, FText) { ++Calls; Result = bSuccess; CommittedAtCallback = Progress->GetProgress().Revision == 8 && UGameplayStatics::DoesSaveGameExist(Slot, 0); }));
	TestTrue(TEXT("one synchronous completion after durable commit"), Calls == 1 && Result && CommittedAtCallback);
	TArray<uint8> Before, After;
	TestTrue(TEXT("capture committed bytes"), UGameplayStatics::LoadDataFromSlot(Before, Slot, 0));
	Calls = 0; Result = true;
	TestFalse(TEXT("caller B stale revision fails"), Progress->TryDevelopModule(Second->GetPrimaryAssetId(), 7,
		[&](bool bSuccess, FText) { ++Calls; Result = bSuccess; }));
	TestTrue(TEXT("stale request completes once with failure"), Calls == 1 && !Result);
	TestTrue(TEXT("stale request leaves balances and unlocks unchanged"), Progress->GetProgress().GP == 900
		&& Progress->GetProgress().Materials.FindRef(TEXT("circuit")) == 8 && Progress->GetProgress().Revision == 8
		&& !Progress->GetProgress().DevelopedModuleIds.Contains(Second->GetPrimaryAssetId()));
	TestTrue(TEXT("read bytes after revision rejection"), UGameplayStatics::LoadDataFromSlot(After, Slot, 0));
	TestTrue(TEXT("revision rejection leaves exact file bytes unchanged"), Before == After);
	Calls = 0; Result = false;
	TestTrue(TEXT("caller A retry is idempotent with original revision"), Progress->TryDevelopModule(First->GetPrimaryAssetId(), 7,
		[&](bool bSuccess, FText) { ++Calls; Result = bSuccess; }));
	TestTrue(TEXT("retry completes once without paying again"), Calls == 1 && Result && Progress->GetProgress().GP == 900 && Progress->GetProgress().Revision == 8);
	Progress->SetSaveSlotForTests(TEXT("")); Calls = 0; Result = true;
	TestFalse(TEXT("save failure reports through explicit completion"), Progress->TryDevelopModule(Second->GetPrimaryAssetId(), 8,
		[&](bool bSuccess, FText) { ++Calls; Result = bSuccess; }));
	TestTrue(TEXT("save failure completion once and wallet retained"), Calls == 1 && !Result && Progress->GetProgress().GP == 900 && Progress->GetProgress().Revision == 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistDeckPersistenceTest, "NeoMSGO.UI.Deck.Persistence.BattleAssistV5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistDeckPersistenceTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSMobileSuitData> Suit(NewObject<UMSMobileSuitData>()); Suit->Cost = 200;
	TStrongObjectPtr<UMSBattleAssistModuleData> Module(NewObject<UMSBattleAssistModuleData>());
	Module->FamilyId = TEXT("test_persist"); Module->Support = EMSModuleSupport::Implemented; Module->Evidence = EMSModuleEvidence::Confirmed;
	FMSBattleAssistEffect Effect; Effect.Value = 4; Effect.Unit = TEXT("percent"); Module->Effects.Add(Effect);
	TStrongObjectPtr<UMSBattleAssistRulesData> Rules(NewObject<UMSBattleAssistRulesData>());
	Rules->Modules.Add(Module.Get()); FMSBattleAssistSuitProfile Profile; Profile.SuitId = Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity = 3;
	Rules->SuitProfiles.Add(Profile); FMSBattleAssistTimeRow Row; Row.Cost = 200; Row.TotalSeconds.Init(54, 11); Row.TotalSeconds[0] = 50; Rules->TimeRows.Add(Row);
	MSBattleAssist::SetRulesDataForTests(Rules.Get());
	const FString Slot = TEXT("BattleAssistDeckTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { MSBattleAssist::SetRulesDataForTests(nullptr); UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	Deck->SetSaveSlotForAutomationTests(Slot); Deck->EnablePersistenceForAutomationTests();
	FMSBattleAssistProgress Progress; Progress.DevelopedModuleIds.Add(Module->GetPrimaryAssetId()); Deck->SetBattleAssistProgressForAutomationTests(Progress);
	TestTrue(TEXT("assign suit"), Deck->AssignPreset(0, Suit.Get()));
	FMSBattleAssistLoadout Loadout; Loadout.ModuleIds.Add(Module->GetPrimaryAssetId()); int32 Calls = 0; bool Committed = false;
	TestTrue(TEXT("request accepted"), Deck->RequestBattleAssistLoadout(0, Loadout, [&](bool Result) { ++Calls; Committed = Result; }));
	TestTrue(TEXT("offline completion once after persisted commit"), Committed && Calls == 1);
	TArray<uint8> Bytes; TestTrue(TEXT("raw saved bytes"), UGameplayStatics::LoadDataFromSlot(Bytes, Slot, 0));
	TestEqual(TEXT("v5 inferred even with omitted Version default"), UMSDeckSubsystem::ResolveSerializedSaveVersion(Bytes, 5), 5);
	TestTrue(TEXT("clear equipment"), Deck->RequestBattleAssistLoadout(0, FMSBattleAssistLoadout(), nullptr));
	TestTrue(TEXT("raw v5 roundtrip"), Deck->RestoreSaveBytesForAutomationTests(Bytes));
	TestTrue(TEXT("IDs survive reload"), Deck->GetLoadoutInSlot(0).BattleAssist == Loadout);
#if PLATFORM_WINDOWS
	// Force a real replacement failure after writing the temporary bytes: Windows denies
	// replacing a destination opened without FILE_SHARE_DELETE. Neither committed state nor bytes may change.
	const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".sav")));
	{
		HANDLE Locked = ::CreateFileW(*Filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (TestTrue(TEXT("lock isolated save against replacement"), Locked != INVALID_HANDLE_VALUE))
		{
			ON_SCOPE_EXIT { ::CloseHandle(Locked); };
			TArray<uint8> BeforeFailure, AfterFailure;
			TestTrue(TEXT("capture existing disk bytes"), UGameplayStatics::LoadDataFromSlot(BeforeFailure, Slot, 0));
			TestFalse(TEXT("OS replacement failure rejects commit"), Deck->RequestBattleAssistLoadout(0, FMSBattleAssistLoadout(), nullptr));
			TestTrue(TEXT("OS failure retains committed module"), Deck->GetLoadoutInSlot(0).BattleAssist == Loadout);
			TestTrue(TEXT("old disk bytes remain readable"), UGameplayStatics::LoadDataFromSlot(AfterFailure, Slot, 0));
			TestTrue(TEXT("OS failure preserves exact old disk bytes"), BeforeFailure == AfterFailure);
		}
	}
#endif
	Deck->SetSaveSlotForAutomationTests(TEXT("Invalid:SaveSlot"));
	TestFalse(TEXT("failed clear rolls back"), Deck->RequestBattleAssistLoadout(0, FMSBattleAssistLoadout(), nullptr));
	TestTrue(TEXT("failed clear retains original"), Deck->GetLoadoutInSlot(0).BattleAssist == Loadout);
	UMSDeckSaveGame* Save = Deck->CreateSaveGameForAutomationTests(); Save->Slots[0].BattleAssist = FMSBattleAssistLoadout();
	for (int32 Version = 1; Version <= 4; ++Version)
	{
		Save->Version = Version;
		TestTrue(TEXT("legacy version migrates"), Deck->RestoreSaveGameForAutomationTests(Save));
		TestTrue(TEXT("legacy migration has no battle modules"), Deck->GetLoadoutInSlot(0).BattleAssist.IsEmpty());
	}
	return true;
}
#endif
