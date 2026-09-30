#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSBattleAssistRules.h"
#include "UI/MSDeckSubsystem.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistSharedWireTest, "NeoMSGO.Online.BattleAssist.SharedWireFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistSharedWireTest::RunTest(const FString&)
{
	FString Body;
	if (!TestTrue(TEXT("read identical Node/UE JSON fixtures"), FFileHelper::LoadFileToString(Body,
		*FPaths::Combine(FPaths::ProjectDir(), TEXT("Backend/test/fixtures/battle-assist-wire.json"))))) return false;
	TArray<TSharedPtr<FJsonValue>> Cases;
	if (!TestTrue(TEXT("parse fixture list"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Cases))) return false;
	for (const auto& Value : Cases)
	{
		const auto Case = Value->AsObject();
		FMSOnlineDeckSnapshot Result;
		TestEqual(*Case->GetStringField(TEXT("name")), MSOnline::ParseDeck(Case->GetObjectField(TEXT("deck")), Result), Case->GetBoolField(TEXT("valid")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBattleAssistAsyncCommitTest, "NeoMSGO.Online.BattleAssist.AsyncCommitRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSBattleAssistAsyncCommitTest::RunTest(const FString&)
{
	const FString Command = FCommandLine::Get();
	// New standalone instance binds the same real account/deck/progress delegates as production.
	FCommandLine::Set(*(Command + TEXT(" -MSAuthMode=Online")));
	UMSGameInstance* Instance = NewObject<UMSGameInstance>(GEngine); Instance->AddToRoot(); Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	ON_SCOPE_EXIT {
		MSBattleAssist::SetRulesDataForTests(nullptr); Instance->Shutdown();
		if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		Instance->RemoveFromRoot(); FCommandLine::Set(*Command);
	};
	auto* Account = Instance->GetSubsystem<UMSOnlineAccountSubsystem>();
	auto* Deck = Instance->GetSubsystem<UMSDeckSubsystem>();
	auto* Progress = Instance->GetSubsystem<UMSBattleAssistProgressSubsystem>();
	if (!TestNotNull(TEXT("account"), Account) || !TestNotNull(TEXT("deck"), Deck) || !TestNotNull(TEXT("progress"), Progress)) return false;
	Account->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	Account->SetAuthBaseUrlForAutomationTests(TEXT("http://127.0.0.1:17890"));
	const auto Suits = Deck->GetCatalogPresets();
	if (!TestTrue(TEXT("registered production suit for response resolution"), !Suits.IsEmpty())) return false;
	UMSMobileSuitData* Suit = Suits[0];
	UMSBattleAssistModuleData* Module = NewObject<UMSBattleAssistModuleData>(Instance, TEXT("AsyncTestModule"));
	Module->FamilyId = TEXT("async_test"); Module->Support = EMSModuleSupport::Implemented; Module->Evidence = EMSModuleEvidence::Confirmed;
	FMSBattleAssistEffect Effect; Effect.Value = 4; Effect.Unit = TEXT("percent"); Module->Effects.Add(Effect);
	UMSBattleAssistRulesData* Rules = NewObject<UMSBattleAssistRulesData>(Instance); Rules->Modules.Add(Module);
	FMSBattleAssistSuitProfile Profile; Profile.SuitId = Suit->GetPrimaryAssetId(); Profile.BattleAssistSlotCapacity = 3; Rules->SuitProfiles.Add(Profile);
	FMSBattleAssistTimeRow Row; Row.Cost = Suit->Cost; Row.TotalSeconds.Init(104, 11); Row.TotalSeconds[0] = 100; Rules->TimeRows.Add(Row);
	MSBattleAssist::SetRulesDataForTests(Rules);
	const FString Id = Module->GetPrimaryAssetId().ToString();
	const auto DeckJson = [Suit, Id](int32 Revision, bool bEquipped) {
		const FString Empty = TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false,\"battleAssist\":{\"moduleIds\":[],\"acModuleIds\":[]}}");
		const FString First = FString::Printf(TEXT("{\"suit_id\":\"%s\",\"weapon_ids\":[null,null,null],\"shield_equipped\":%s,\"battleAssist\":{\"moduleIds\":[%s],\"acModuleIds\":[]}}"),
			*Suit->GetPrimaryAssetId().ToString(), Suit->GetNativeShield() ? TEXT("true") : TEXT("false"), bEquipped ? *(TEXT("\"") + Id + TEXT("\"")) : TEXT(""));
		return FString::Printf(TEXT("{\"schema_version\":3,\"revision\":%d,\"selected_slot\":0,\"slots\":[%s,%s,%s,%s]}"), Revision, *First, *Empty, *Empty, *Empty);
	};
	const FString Ledger = TEXT("{\"gp\":0,\"revision\":1,\"materials\":{},\"developed_module_ids\":[\"") + Id + TEXT("\"]}");
	Account->SetRequestSenderForAutomationTests([Account, Ledger](const FString&, const FString& Path, const FString&, const FString&) {
		if (Path == TEXT("/v1/me/battle-assist")) Account->SetResponseForAutomationTests(200, Ledger);
		return true;
	});
	Account->SetResponseForAutomationTests(200, TEXT("{\"realm_id\":\"battle-assist-fixture\",\"account_id\":\"fixture\",\"username\":\"fixture\",\"display_name\":\"fixture\",\"token\":\"fixture-token\",\"deck\":") + DeckJson(0, false) + TEXT("}"));
	Account->Login(TEXT("fixture"), TEXT("fixture-password"), nullptr);
	if (!TestTrue(TEXT("authenticated account fixture"), Account->HasOnlineSession())) return false;
	TestTrue(TEXT("progress loaded through authenticated transport"), Progress->GetProgress().DevelopedModuleIds.Contains(Module->GetPrimaryAssetId()));
	Account->SetDeferredResponsesForAutomationTests(true);
	FMSBattleAssistLoadout Candidate; Candidate.ModuleIds.Add(Module->GetPrimaryAssetId());
	int32 Calls = 0; bool Result = false;
	TestTrue(TEXT("async request accepted"), Deck->RequestBattleAssistLoadout(0, Candidate, [&](bool Value) { ++Calls; Result = Value; }));
	TestTrue(TEXT("pending does not mutate committed deck or fire completion"), Deck->GetLoadoutInSlot(0).BattleAssist.IsEmpty() && Calls == 0);
	Account->CompleteNextResponseForAutomationTests(200, TEXT("{\"deck\":") + DeckJson(1, true) + TEXT("}"));
	TestTrue(TEXT("success commits once"), Calls == 1 && Result && Deck->GetLoadoutInSlot(0).BattleAssist == Candidate);
	Calls = 0;
	TestTrue(TEXT("clear request starts"), Deck->RequestBattleAssistLoadout(0, FMSBattleAssistLoadout(), [&](bool Value) { ++Calls; Result = Value; }));
	Account->CompleteNextResponseForAutomationTests(409, TEXT("{\"error\":{\"message\":\"fixture conflict\"}}"));
	TestTrue(TEXT("failure reports once and retains old loadout"), Calls == 1 && !Result && Deck->GetLoadoutInSlot(0).BattleAssist == Candidate);
	TestTrue(TEXT("refresh pending"), Progress->ReloadProgress());
	Account->Logout();
	Account->CompleteNextResponseForAutomationTests(200, Ledger);
	TestTrue(TEXT("stale account progress cannot leak after logout"), Progress->GetProgress().DevelopedModuleIds.IsEmpty() && !Progress->IsPending());
	return true;
}
#endif
