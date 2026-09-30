// Copyright NeoMSGO Project. All Rights Reserved.
// Real Account/Deck/widget flow with only HTTP transport responses controlled.
// Live Build12 confirmed the AV2F detail value was correct; this guards the data
// contract and does not claim to test the small rendered suffix's readability.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSuitDetailOnlinePutFlowTest,
 "NeoMSGO.UI.SuitDetail.OnlinePutCompletionFlow",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSuitDetailOnlinePut
{
 struct FCommandScope
 {
  FString Previous = FCommandLine::Get();
  FCommandScope() { FCommandLine::Append(TEXT(" -MSAuthMode=Online")); }
  ~FCommandScope() { FCommandLine::Set(*Previous); }
 };
 struct FWorldScope
 {
  TStrongObjectPtr<UMSGameInstance> Instance{NewObject<UMSGameInstance>(GEngine)};
  FString Realm = TEXT("suit-detail-put-regression");
  FString AccountId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
  UWorld* World = nullptr;
  FWorldScope()
  {
   Instance->InitializeStandalone(); World = Instance->GetWorld();
   World->InitializeActorsForPlay(FURL()); World->BeginPlay();
  }
  ~FWorldScope()
  {
   // Only this generated account's cache is removed. No shared offline deck or
   // guest credential is created by this non-guest Online session.
   for (const FString& Id : {AccountId, AccountId + TEXT("b")})
   {
    const FString Cache = FString::Printf(TEXT("NeoMSGO_OnlineDeck_%08x"), GetTypeHash(Realm + TEXT("/") + Id));
    UGameplayStatics::DeleteGameInSlot(Cache, 0);
   }
   if (World && World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
   Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
  }
 };
 FString DeckJson(const UMSMobileSuitData* Suit, const UMSWeaponDataBase* Weapon, int64 Revision)
 {
  return FString::Printf(TEXT("{\"schema_version\":1,\"revision\":%lld,\"selected_slot\":0,\"slots\":[")
   TEXT("{\"suit_id\":\"%s\",\"weapon_ids\":[\"%s\",null,null]},")
   TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]},")
   TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]},")
   TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]}]}"),
   static_cast<long long>(Revision), *Suit->GetPrimaryAssetId().ToString(), *Weapon->GetPrimaryAssetId().ToString());
 }
}

bool FMSSuitDetailOnlinePutFlowTest::RunTest(const FString&)
{
 using namespace MSSuitDetailOnlinePut;
 FCommandScope CommandScope;
 FString Mode; FParse::Value(FCommandLine::Get(), TEXT("MSAuthMode="), Mode);
 // Reject a pre-existing conflicting argument before subsystem initialization
 // could open the shared OfflineLocal save. Preserve engine flags (-nullrhi).
 if (!TestTrue(TEXT("effective auth mode is Online"), Mode.Equals(TEXT("Online"), ESearchCase::IgnoreCase))) return false;
 FWorldScope Scope;
 auto* Account = Scope.Instance->GetSubsystem<UMSOnlineAccountSubsystem>();
 auto* Deck = Scope.Instance->GetSubsystem<UMSDeckSubsystem>();
 if (!TestNotNull(TEXT("real account subsystem exists"), Account) || !TestNotNull(TEXT("real deck subsystem exists"), Deck)) return false;
 TStrongObjectPtr<UMSMobileSuitData> Suit(LoadObject<UMSMobileSuitData>(nullptr,
  TEXT("/Game/Data/MobileSuits/DA_MS_AssaultCommander.DA_MS_AssaultCommander")));
 TStrongObjectPtr<UMSWeaponDataBase> AV2(LoadObject<UMSWeaponDataBase>(nullptr,
  TEXT("/Game/Data/Weapons/DA_Weapon_AssaultVulcanAV2.DA_Weapon_AssaultVulcanAV2")));
 TStrongObjectPtr<UMSWeaponDataBase> AV2F(LoadObject<UMSWeaponDataBase>(nullptr,
  TEXT("/Game/Data/Weapons/DA_Weapon_AssaultVulcanAV2F.DA_Weapon_AssaultVulcanAV2F")));
 if (!TestNotNull(TEXT("real catalog suit exists"), Suit.Get()) || !TestNotNull(TEXT("real AV2 exists"), AV2.Get())
  || !TestNotNull(TEXT("real AV2F exists"), AV2F.Get())) return false;
 TestNotEqual(TEXT("the two real weapon names are distinguishable"), AV2->WeaponName.ToString(), AV2F->WeaponName.ToString());
 int32 PutRequests = 0; FString LastPutBody;
 // Only the HTTP transport is replaced. Login/JSON parsing, Account generation,
 // Deck application, real delegates and both production widgets run unchanged.
 Account->SetRequestSenderForAutomationTests([&PutRequests, &LastPutBody](const FString& Verb, const FString& Path,
  const FString& Body, const FString&)
 {
  if (Verb == TEXT("PUT") && Path == TEXT("/v1/me/deck")) { ++PutRequests; LastPutBody = Body; }
  return true;
 });
 Account->SetResponseForAutomationTests(200, FString::Printf(
  TEXT("{\"realm_id\":\"%s\",\"account_id\":\"%s\",\"username\":\"fixture\",\"display_name\":\"Fixture\",\"is_guest\":false,\"token\":\"synthetic-fixture-token\",\"deck\":%s}"),
  *Scope.Realm, *Scope.AccountId, *DeckJson(Suit.Get(), AV2.Get(), 3)));
 bool bAuthenticated = false;
 Account->Login(TEXT("fixture"), TEXT("synthetic-password"), [&bAuthenticated](bool bSuccess, const FString&) { bAuthenticated = bSuccess; });
 if (!TestTrue(TEXT("real login completion authenticates and applies the initial deck"), bAuthenticated && Account->HasOnlineSession())
  || !TestEqual(TEXT("real deck resolved the initial weapon"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), AV2.Get())) return false;

 TStrongObjectPtr<UMSSuitDetailWidget> Detail(NewObject<UMSSuitDetailWidget>(Scope.World));
 TStrongObjectPtr<UMSHangarWidget> Hangar(NewObject<UMSHangarWidget>(Scope.World));
 Detail->Initialize(); Detail->InitializeForAutomationTests();
 Hangar->Initialize(); Hangar->InitializeForAutomationTests(nullptr);
 Detail->SetDeckSubsystemForTesting(Deck); Hangar->SetDeckSubsystemForTesting(Deck);
 Hangar->SetWeaponCatalog({AV2.Get(), AV2F.Get()}); Detail->SetHangarWidgetForTesting(Hangar.Get());
 Detail->Open(0);
 UTextBlock* WeaponText = Cast<UTextBlock>(Detail->GetWidgetFromName(TEXT("SuitDetailWeaponName0")));
 UButton* CustomizeButton = Cast<UButton>(Detail->GetWidgetFromName(TEXT("WeaponCustomizeButton")));
 UButton* EquipButton = Cast<UButton>(Hangar->GetWidgetFromName(TEXT("EquipWeaponButton")));
 if (!TestNotNull(TEXT("actual detail weapon text exists"), WeaponText) || !TestNotNull(TEXT("customize button exists"), CustomizeButton)
  || !TestNotNull(TEXT("equip button exists"), EquipButton)) return false;
 TestEqual(TEXT("Detail begins from the same account slot"), WeaponText->GetText().ToString(), AV2->WeaponName.ToString());
 CustomizeButton->OnClicked.Broadcast();
 TestTrue(TEXT("real button opens Hangar and hides Detail"), Hangar->IsOpen() && !Detail->IsOpen());
 Account->SetDeferredResponsesForAutomationTests(true);
 Hangar->ChooseCatalogWeapon(AV2F.Get()); EquipButton->OnClicked.Broadcast();
 TestEqual(TEXT("real equipment action issues exactly one PUT"), PutRequests, 1);
 TestTrue(TEXT("HTTP completion is actually pending"), Account->IsDeckRequestPending());
 TSharedPtr<FJsonObject> Request;
 const auto Reader = TJsonReaderFactory<>::Create(LastPutBody);
 if (!TestTrue(TEXT("real PUT body is valid JSON"), FJsonSerializer::Deserialize(Reader, Request) && Request.IsValid())) return false;
 TestEqual(TEXT("PUT uses committed revision 3"), Request->GetIntegerField(TEXT("expected_revision")), 3);
 const TSharedPtr<FJsonObject> SentDeck = Request->GetObjectField(TEXT("deck"));
 TestEqual(TEXT("PUT carries the selected real AV2F id"),
  SentDeck->GetArrayField(TEXT("slots"))[0]->AsObject()->GetArrayField(TEXT("weapon_ids"))[0]->AsString(), AV2F->GetPrimaryAssetId().ToString());
 TestEqual(TEXT("pending Account still exposes AV2"), Account->GetCommittedDeck().Slots[0].WeaponIds[0], AV2->GetPrimaryAssetId());
 TestEqual(TEXT("pending Deck still exposes AV2"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), AV2.Get());
 TestEqual(TEXT("pending hidden Detail retains committed AV2"), WeaponText->GetText().ToString(), AV2->WeaponName.ToString());
 Hangar->Close(); TestTrue(TEXT("pending save cannot close Hangar"), Hangar->IsOpen());
 if (!TestTrue(TEXT("real deferred PUT callback completes"), Account->CompleteNextResponseForAutomationTests(200,
  TEXT("{\"deck\":") + DeckJson(Suit.Get(), AV2F.Get(), 4) + TEXT("}")))) return false;
 TestEqual(TEXT("Account commits revision 4"), Account->GetCommittedDeck().Revision, int64(4));
 TestEqual(TEXT("Account commits AV2F"), Account->GetCommittedDeck().Slots[0].WeaponIds[0], AV2F->GetPrimaryAssetId());
 TestEqual(TEXT("Deck applies AV2F before returning from PUT callback"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), AV2F.Get());
 TestEqual(TEXT("hidden Detail receives the committed AV2F before Hangar closes"), WeaponText->GetText().ToString(), AV2F->WeaponName.ToString());
 TestTrue(TEXT("Hangar shows committed AV2F"), Hangar->GetEquippedComparisonTextForTesting().ToString().Contains(AV2F->WeaponName.ToString()));
 Hangar->Close();
 TestTrue(TEXT("normal return shows Detail"), Detail->IsOpen() && !Hangar->IsOpen());
 TestEqual(TEXT("return reads AV2F from Deck without a display override"), WeaponText->GetText().ToString(), AV2F->WeaponName.ToString());
 Detail->HandleModuleCustomizeChanged(); // An unrelated real refresh must not revert the name.
 TestEqual(TEXT("subsequent refresh still reads committed AV2F"), WeaponText->GetText().ToString(), AV2F->WeaponName.ToString());

 // Browsing a different candidate and closing is a cancellation, not a save.
 CustomizeButton->OnClicked.Broadcast(); Hangar->ChooseCatalogWeapon(AV2.Get()); Hangar->Close();
 TestEqual(TEXT("browsing cancellation issues no PUT"), PutRequests, 1);
 TestEqual(TEXT("browsing cancellation keeps AV2F"), WeaponText->GetText().ToString(), AV2F->WeaponName.ToString());

 // A real rejected response preserves all committed views through the same path.
 CustomizeButton->OnClicked.Broadcast(); Hangar->ChooseCatalogWeapon(AV2.Get()); EquipButton->OnClicked.Broadcast();
 TestEqual(TEXT("retry action issues the second PUT"), PutRequests, 2);
 TestTrue(TEXT("real failed PUT response is delivered"), Account->CompleteNextResponseForAutomationTests(409,
  TEXT("{\"error\":{\"code\":\"revision_conflict\",\"message\":\"fixture conflict\"},\"current_revision\":5}")));
 TestEqual(TEXT("failure preserves Account revision"), Account->GetCommittedDeck().Revision, int64(4));
 TestEqual(TEXT("failure preserves Deck AV2F"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), AV2F.Get());
 Hangar->Close();
 TestEqual(TEXT("failure return preserves Detail AV2F"), WeaponText->GetText().ToString(), AV2F->WeaponName.ToString());

 // Replace the account while an actual PUT response is still queued. The
 // transport callback must be discarded by generation, not broadcast manually.
 CustomizeButton->OnClicked.Broadcast(); Hangar->ChooseCatalogWeapon(AV2.Get()); EquipButton->OnClicked.Broadcast();
 TestEqual(TEXT("generation-change setup has a third in-flight PUT"), PutRequests, 3);
 bool bAccountBAuthenticated = false;
 Account->Login(TEXT("fixture-b"), TEXT("synthetic-password"),
  [&bAccountBAuthenticated](bool bSuccess, const FString&) { bAccountBAuthenticated = bSuccess; });
 TestFalse(TEXT("replacement authentication clears old account identity"), Account->HasOnlineSession());
 TestNull(TEXT("replacement authentication clears the old account deck"), Deck->GetPresetInSlot(0));
 TestEqual(TEXT("old account weapon disappears from Detail during replacement"), WeaponText->GetText().ToString(), FString(TEXT("---")));
 TestTrue(TEXT("stale PUT transport callback is drained"), Account->CompleteNextResponseForAutomationTests(200,
  TEXT("{\"deck\":") + DeckJson(Suit.Get(), AV2.Get(), 5) + TEXT("}")));
 TestNull(TEXT("stale PUT cannot restore old account deck"), Deck->GetPresetInSlot(0));
 TestEqual(TEXT("stale PUT cannot restore old account weapon text"), WeaponText->GetText().ToString(), FString(TEXT("---")));
 const FString AccountBReply = FString::Printf(
  TEXT("{\"realm_id\":\"%s\",\"account_id\":\"%sb\",\"username\":\"fixture-b\",\"display_name\":\"Fixture B\",\"is_guest\":false,\"token\":\"synthetic-b-token\",\"deck\":%s}"),
  *Scope.Realm, *Scope.AccountId, *DeckJson(Suit.Get(), AV2.Get(), 1));
 TestTrue(TEXT("replacement login HTTP callback completes"), Account->CompleteNextResponseForAutomationTests(200, AccountBReply));
 TestTrue(TEXT("real login handler authenticates account B"), bAccountBAuthenticated);
 TestEqual(TEXT("account B uses its own revision"), Account->GetCommittedDeck().Revision, int64(1));
 TestEqual(TEXT("account B uses its own weapon in Deck"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), AV2.Get());
 TestEqual(TEXT("account B uses its own weapon in Detail"), WeaponText->GetText().ToString(), AV2->WeaponName.ToString());
 Detail->Close();
 return true;
}
#endif
