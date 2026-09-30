#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Data/MSModuleData.h"
#include "Misc/CommandLine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSSortieWidget.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckSlotOperationsTest, "NeoMSGO.UI.Deck.SlotOperations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckCostLimitTest, "NeoMSGO.UI.Deck.CostLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckSortieWidgetBridgeTest, "NeoMSGO.UI.Deck.SortieWidgetBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckPersistenceRoundTripTest, "NeoMSGO.UI.Deck.Persistence.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckPersistenceFallbackTest, "NeoMSGO.UI.Deck.Persistence.InvalidDataFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckIndependentLoadoutsTest, "NeoMSGO.UI.Deck.Persistence.IndependentLoadoutsAndV1Migration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckCommitFailureTest, "NeoMSGO.UI.Deck.Persistence.CommitFailureRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckEmptyRoundTripTest, "NeoMSGO.UI.Deck.Persistence.EmptyDeckRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckSelectionCompletionTest, "NeoMSGO.UI.Deck.SelectionCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckModulePersistenceTest, "NeoMSGO.UI.Deck.Persistence.ModuleSlotsMigrationAndRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckShieldMigrationTest, "NeoMSGO.UI.Deck.Persistence.NativeShieldMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// UMSDeckSubsystemはClassWithin=GameInstanceのため、テストでも専用GameInstanceをOuterにする
	UMSDeckSubsystem* MakeTestDeck()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		return NewObject<UMSDeckSubsystem>(GameInstance);
	}

	struct FScopedDeckSaveSlot
	{
		FString Name = FString::Printf(TEXT("NeoMSGO_DeckLoadoutTest_%s"), *FGuid::NewGuid().ToString());
		explicit FScopedDeckSaveSlot(UMSDeckSubsystem* Deck)
		{
			Deck->SetSaveSlotForAutomationTests(Name);
			Deck->EnablePersistenceForAutomationTests();
		}
		~FScopedDeckSaveSlot() { UGameplayStatics::DeleteGameInSlot(Name, 0); }
	};
}

bool FMSDeckSlotOperationsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	TStrongObjectPtr<UMSMobileSuitData> Preset(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	TestEqual(TEXT("deck always exposes four slots"), Deck->GetSlotCount(), UMSDeckSubsystem::SlotCount);
	TestTrue(TEXT("assigning a valid slot succeeds"), Deck->AssignPreset(2, Preset.Get()));
	TestEqual(TEXT("assigned preset is returned by identity"), Deck->GetPresetInSlot(2), Preset.Get());
	TestTrue(TEXT("removing an occupied slot succeeds"), Deck->RemovePreset(2));
	TestNull(TEXT("removed slot is empty"), Deck->GetPresetInSlot(2));
	TestFalse(TEXT("invalid slot assignment is rejected"), Deck->AssignPreset(UMSDeckSubsystem::SlotCount, Preset.Get()));
	return true;
}

bool FMSDeckCostLimitTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	TStrongObjectPtr<UMSMobileSuitData> A(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	TStrongObjectPtr<UMSMobileSuitData> B(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	A->Cost = 900;
	B->Cost = 821;
	Deck->AssignPreset(0, A.Get());
	Deck->AssignPreset(1, B.Get());
	TestEqual(TEXT("deck cost sums occupied slots"), Deck->GetCurrentDeckCost(), 1721);
	TestTrue(TEXT("cost above 1720 is over the sortie limit"), Deck->IsOverCostLimit());
	B->Cost = 820;
	TestEqual(TEXT("cost limit is exactly 1720"), Deck->GetCurrentDeckCost(), UMSDeckSubsystem::DeckCostLimit);
	TestFalse(TEXT("cost equal to limit remains valid"), Deck->IsOverCostLimit());
	return true;
}

bool FMSDeckSortieWidgetBridgeTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSSortieWidget> Widget(NewObject<UMSSortieWidget>(GetTransientPackage()));
	Widget->Initialize();
	Widget->InitializeForAutomationTests();
	TStrongObjectPtr<UMSMobileSuitData> Preset(NewObject<UMSMobileSuitData>(Widget.Get()));
	Widget->SetSelectedSlotIndex(1);
	Widget->AssignPresetToSelectedSlot(Preset.Get());
	TestEqual(TEXT("widget assignment writes the shared deck"), Widget->GetDeckSubsystem()->GetPresetInSlot(1), Preset.Get());
	TestEqual(TEXT("widget reads the same shared deck pointer"), Widget->GetPresetInSlot(1), Preset.Get());
	return true;
}

bool FMSDeckPersistenceRoundTripTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Source(MakeTestDeck());
	UPackage* TestPackage = CreatePackage(*FString::Printf(TEXT("/Engine/Transient/MSDeckPersistenceRoundTrip_%s"),
		*FGuid::NewGuid().ToString()));
	UMSMobileSuitData* First = NewObject<UMSMobileSuitData>(TestPackage, TEXT("FirstSuit"));
	UMSMobileSuitData* Last = NewObject<UMSMobileSuitData>(TestPackage, TEXT("LastSuit"));
	UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(TestPackage, TEXT("SavedWeapon"));
	First->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	First->EquippedWeapons[1] = Weapon;
	Source->AssignPreset(0, First);
	Source->AssignPreset(3, Last);
	Source->SetSelectedSlotIndex(3);

	TStrongObjectPtr<UMSDeckSaveGame> Captured(Source->CreateSaveGameForAutomationTests());
	TArray<uint8> Bytes;
	TestTrue(TEXT("save game serializes to bytes"), UGameplayStatics::SaveGameToMemory(Captured.Get(), Bytes));
	TStrongObjectPtr<USaveGame> Deserialized(UGameplayStatics::LoadGameFromMemory(Bytes));
	const UMSDeckSaveGame* Loaded = Cast<UMSDeckSaveGame>(Deserialized.Get());
	TestNotNull(TEXT("serialized bytes deserialize as the deck save class"), Loaded);

	TStrongObjectPtr<UMSDeckSubsystem> Restored(MakeTestDeck());
	TestTrue(TEXT("valid deserialized deck is accepted"), Restored->RestoreSaveGameForAutomationTests(Loaded));
	TestEqual(TEXT("slot order survives process-style serialization round trip"), Restored->GetPresetInSlot(0), First);
	TestNull(TEXT("empty slots survive the round trip"), Restored->GetPresetInSlot(1));
	TestEqual(TEXT("last occupied slot survives the round trip"), Restored->GetPresetInSlot(3), Last);
	TestEqual(TEXT("last selected sortie suit slot is restored"), Restored->GetSelectedSlotIndex(), 3);
	const FMSDeckLoadout RestoredLoadout = Restored->GetLoadoutInSlot(0);
	TestEqual(TEXT("restored equipment preserves all three slot numbers"), RestoredLoadout.EquippedWeapons.Num(), UMSMobileSuitData::MaxWeaponSlots);
	TestTrue(TEXT("weapon slot order is restored in per-slot equipment"), RestoredLoadout.EquippedWeapons[1].Get() == Weapon);
	TestNull(TEXT("empty weapon slot zero is preserved"), RestoredLoadout.EquippedWeapons[0].Get());
	TestTrue(TEXT("catalog default equipment remains unchanged"), First->EquippedWeapons[1].Get() == Weapon);
	return true;
}

bool FMSDeckIndependentLoadoutsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Engine/Transient/MSDeckIndependent_%s"), *FGuid::NewGuid().ToString()));
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Package, TEXT("SameCatalogSuit"));
	UMSRangedWeaponData* DefaultWeapon = NewObject<UMSRangedWeaponData>(Package, TEXT("DefaultWeapon"));
	UMSRangedWeaponData* FirstWeapon = NewObject<UMSRangedWeaponData>(Package, TEXT("FirstWeapon"));
	UMSRangedWeaponData* SecondWeapon = NewObject<UMSRangedWeaponData>(Package, TEXT("SecondWeapon"));
	Suit->EquippedWeapons = { DefaultWeapon };
	const TArray<TSoftObjectPtr<UMSWeaponDataBase>> CatalogDefaults = Suit->EquippedWeapons;

	// Reproduce the legacy failure: two save records refer to one suit asset but carry different equipment.
	TStrongObjectPtr<UMSDeckSaveGame> Legacy(NewObject<UMSDeckSaveGame>());
	Legacy->Version = 1;
	Legacy->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	Legacy->SelectedSlotIndex = 2;
	Legacy->Slots[0].MobileSuit = FSoftObjectPath(Suit);
	Legacy->Slots[0].EquippedWeapons = { FSoftObjectPath(), FSoftObjectPath(FirstWeapon) };
	Legacy->Slots[2].MobileSuit = FSoftObjectPath(Suit);
	Legacy->Slots[2].EquippedWeapons = { FSoftObjectPath(SecondWeapon), FSoftObjectPath(), FSoftObjectPath() };
	TArray<uint8> LegacyBytes;
	TestTrue(TEXT("legacy v1 records serialize"), UGameplayStatics::SaveGameToMemory(Legacy.Get(), LegacyBytes));
	TStrongObjectPtr<USaveGame> LegacyReloaded(UGameplayStatics::LoadGameFromMemory(LegacyBytes));
	TestTrue(TEXT("v1 records migrate without resetting existing equipment"), Deck->RestoreSaveGameForAutomationTests(Cast<UMSDeckSaveGame>(LegacyReloaded.Get())));
	TestEqual(TEXT("first slot keeps the catalog identity"), Deck->GetPresetInSlot(0), Suit);
	TestEqual(TEXT("same-type second slot keeps the same catalog identity"), Deck->GetPresetInSlot(2), Suit);
	TestEqual(TEXT("first slot migrates its own weapon"), Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get(), static_cast<UMSWeaponDataBase*>(FirstWeapon));
	TestEqual(TEXT("second slot migrates its different weapon"), Deck->GetLoadoutInSlot(2).EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(SecondWeapon));
	TestTrue(TEXT("v1 migration does not modify catalog defaults"), Suit->EquippedWeapons == CatalogDefaults);

	FMSDeckLoadout Candidate = Deck->GetLoadoutInSlot(0);
	Candidate.EquippedWeapons[1].Reset();
	Candidate.EquippedWeapons[2] = FirstWeapon;
	TestEqual(TEXT("editing a candidate leaves committed equipment unchanged"), Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get(), static_cast<UMSWeaponDataBase*>(FirstWeapon));
	FScopedDeckSaveSlot SavedSlot(Deck.Get());
	TestTrue(TEXT("valid loadout commit succeeds through the real save API"), Deck->CommitLoadout(0, Candidate));
	TestTrue(TEXT("reassigning the same catalog to the same slot preserves customization"), Deck->AssignPreset(0, Suit));
	TestEqual(TEXT("same-slot assignment does not reset customization"), Deck->GetLoadoutInSlot(0).EquippedWeapons[2].Get(), static_cast<UMSWeaponDataBase*>(FirstWeapon));
	TStrongObjectPtr<UMSDeckSaveGame> Written(Cast<UMSDeckSaveGame>(UGameplayStatics::LoadGameFromSlot(SavedSlot.Name, 0)));
	if (!TestNotNull(TEXT("committed deck can be read back from the isolated save file"), Written.Get())) return false;
	TestEqual(TEXT("the next successful commit writes current schema"), Written->Version, UMSDeckSaveGame::CurrentVersion);
	TStrongObjectPtr<UMSDeckSubsystem> Restored(MakeTestDeck());
	TestTrue(TEXT("saved v2 restores"), Restored->RestoreSaveGameForAutomationTests(Written.Get()));
	TestNull(TEXT("slot zero stays empty rather than compacting weapons"), Restored->GetLoadoutInSlot(0).EquippedWeapons[0].Get());
	TestNull(TEXT("removed middle slot stays empty"), Restored->GetLoadoutInSlot(0).EquippedWeapons[1].Get());
	TestEqual(TEXT("first suit weapon stays in slot three"), Restored->GetLoadoutInSlot(0).EquippedWeapons[2].Get(), static_cast<UMSWeaponDataBase*>(FirstWeapon));
	TestEqual(TEXT("second same-type suit remains independent after save round trip"), Restored->GetLoadoutInSlot(2).EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(SecondWeapon));
	TestEqual(TEXT("migration and commit preserve selected deck slot"), Restored->GetSelectedSlotIndex(), 2);
	TestTrue(TEXT("commit and reload leave the shared catalog byte-for-byte equipment paths unchanged"), Suit->EquippedWeapons == CatalogDefaults);
	return true;
}

bool FMSDeckShieldMigrationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Engine/Transient/MSDeckShield_%s"), *FGuid::NewGuid().ToString()));
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Package, TEXT("ShieldSuit"));
	UMSRangedWeaponData* GunA = NewObject<UMSRangedWeaponData>(Package, TEXT("ShieldGunA"));
	UMSRangedWeaponData* GunB = NewObject<UMSRangedWeaponData>(Package, TEXT("ShieldGunB"));
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Package, TEXT("NativeShield"));
	Suit->EquippedWeapons = { GunA, Shield, GunB };
	TStrongObjectPtr<UMSDeckSaveGame> Legacy(NewObject<UMSDeckSaveGame>());
	Legacy->Version = 3;
	Legacy->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	Legacy->Slots[0].MobileSuit = FSoftObjectPath(Suit);
	Legacy->Slots[0].EquippedWeapons = { FSoftObjectPath(GunA), FSoftObjectPath(Shield), FSoftObjectPath(GunB) };
	TestTrue(TEXT("legacy shield save serializes"), Deck->RestoreSaveGameForAutomationTests(Legacy.Get()));
	const FMSDeckLoadout Migrated = Deck->GetLoadoutInSlot(0);
	TestTrue(TEXT("legacy native shield becomes independent equipped state"), Migrated.bShieldEquipped);
	TestEqual(TEXT("legacy gun before shield keeps its index"), Migrated.EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(GunA));
	TestNull(TEXT("legacy shield leaves a null weapon slot"), Migrated.EquippedWeapons[1].Get());
	TestEqual(TEXT("legacy gun after shield keeps its index"), Migrated.EquippedWeapons[2].Get(), static_cast<UMSWeaponDataBase*>(GunB));

	FMSDeckLoadout Unequipped = Migrated;
	Unequipped.bShieldEquipped = false;
	FScopedDeckSaveSlot SavedSlot(Deck.Get());
	TestTrue(TEXT("removable native shield can be unequipped"), Deck->CommitLoadout(0, Unequipped));
	TStrongObjectPtr<UMSDeckSaveGame> Written(Cast<UMSDeckSaveGame>(UGameplayStatics::LoadGameFromSlot(SavedSlot.Name, 0)));
	TestNotNull(TEXT("canonical v4 save is written"), Written.Get());
	if (Written)
	{
		TestEqual(TEXT("canonical save uses v4"), Written->Version, UMSDeckSaveGame::CurrentVersion);
		TestFalse(TEXT("canonical save stores shield separately"), Written->Slots[0].bShieldEquipped);
	}
	return true;
}

bool FMSDeckCommitFailureTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	TStrongObjectPtr<UMSDeckSubsystem> NotificationProbe(MakeTestDeck());
	TStrongObjectPtr<UMSMobileSuitData> Suit(NewObject<UMSMobileSuitData>());
	TStrongObjectPtr<UMSRangedWeaponData> Weapon(NewObject<UMSRangedWeaponData>());
	Suit->EquippedWeapons = { Weapon.Get() };
	Deck->AssignPreset(0, Suit.Get());
	Deck->AssignPreset(1, Suit.Get());
	// The probe's reflected slot setter records the notified slot without adding a test-only UObject class.
	Deck->OnDeckChanged.AddDynamic(NotificationProbe.Get(), &UMSDeckSubsystem::SetSelectedSlotIndex);
	const FMSDeckLoadout Original = Deck->GetLoadoutInSlot(0);
	FScopedDeckSaveSlot SavedSlot(Deck.Get());
	TestTrue(TEXT("baseline deck is saved"), Deck->SaveDeck());
	FMSDeckLoadout Candidate = Original;
	Candidate.EquippedWeapons[0].Reset();
	Candidate.EquippedWeapons[2] = Weapon.Get();
	// UE's real SaveDataToSlot rejects an empty name; no fabricated success/failure backend is used.
	Deck->SetSaveSlotForAutomationTests(TEXT(""));
	TestFalse(TEXT("save failure is reported to the commit caller"), Deck->CommitLoadout(1, Candidate));
	TestEqual(TEXT("failed commit does not broadcast its slot as a successful change"), NotificationProbe->GetSelectedSlotIndex(), 0);
	TestTrue(TEXT("failed commit restores all previous slot entries"), Deck->GetLoadoutInSlot(1).EquippedWeapons == Original.EquippedWeapons);
	TestTrue(TEXT("failed commit leaves other same-type slot untouched"), Deck->GetLoadoutInSlot(0).EquippedWeapons == Original.EquippedWeapons);
	TestFalse(TEXT("failed removal does not claim success"), Deck->RemovePreset(0));
	TestEqual(TEXT("failed removal retains the suit"), Deck->GetPresetInSlot(0), Suit.Get());
	Deck->SetSelectedSlotIndex(1);
	TestEqual(TEXT("failed selection save retains the committed selection"), Deck->GetSelectedSlotIndex(), 0);
	Deck->SetSaveSlotForAutomationTests(SavedSlot.Name);
	Candidate.EquippedWeapons.SetNum(4);
	TestFalse(TEXT("oversized candidate is rejected"), Deck->CommitLoadout(0, Candidate));
	Candidate = Original;
	Candidate.EquippedWeapons[0] = TSoftObjectPtr<UMSWeaponDataBase>(FSoftObjectPath(Suit.Get()));
	TestFalse(TEXT("candidate with a non-weapon asset is rejected"), Deck->CommitLoadout(0, Candidate));
	TestFalse(TEXT("empty deck slot cannot receive equipment"), Deck->CommitLoadout(3, Original));
	TestFalse(TEXT("invalid deck slot cannot receive equipment"), Deck->CommitLoadout(-1, Original));
	TStrongObjectPtr<UMSDeckSaveGame> Corrupt(Deck->CreateSaveGameForAutomationTests());
	Corrupt->Slots[1].EquippedWeapons[0] = FSoftObjectPath(Suit.Get());
	TestFalse(TEXT("wrong asset type in saved weapon is rejected"), Deck->RestoreSaveGameForAutomationTests(Corrupt.Get()));
	TestTrue(TEXT("failed apply is atomic for previously visited slots"), Deck->GetLoadoutInSlot(0).EquippedWeapons == Original.EquippedWeapons);
	TestTrue(TEXT("failed apply retains the later slot too"), Deck->GetLoadoutInSlot(1).EquippedWeapons == Original.EquippedWeapons);
	Corrupt->Slots[1].EquippedWeapons[0] = FSoftObjectPath(Weapon.Get());
	Corrupt->Slots[1].EquippedWeapons.SetNum(4);
	TestFalse(TEXT("oversized saved equipment is rejected"), Deck->RestoreSaveGameForAutomationTests(Corrupt.Get()));
	Corrupt->Slots[1].EquippedWeapons.SetNum(2);
	TestFalse(TEXT("v2 requires all three numbered slots"), Deck->RestoreSaveGameForAutomationTests(Corrupt.Get()));
	Corrupt->Slots[1].EquippedWeapons.SetNum(3);
	Corrupt->SelectedSlotIndex = MAX_int32;
	TestTrue(TEXT("out-of-range saved selection is safely normalized"), Deck->RestoreSaveGameForAutomationTests(Corrupt.Get()));
	TestEqual(TEXT("normalized selection uses a real occupied slot"), Deck->GetSelectedSlotIndex(), 0);
	TestTrue(TEXT("a later successful commit recovers after the save destination is restored"), Deck->CommitLoadout(1, Original));
	TestEqual(TEXT("successful commit broadcasts the exact changed deck slot"), NotificationProbe->GetSelectedSlotIndex(), 1);
	return true;
}

bool FMSDeckEmptyRoundTripTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	Deck->SetSelectedSlotIndex(3);
	TStrongObjectPtr<UMSDeckSaveGame> Save(Deck->CreateSaveGameForAutomationTests());
	TArray<uint8> Bytes;
	TestTrue(TEXT("empty deck serializes"), UGameplayStatics::SaveGameToMemory(Save.Get(), Bytes));
	TStrongObjectPtr<USaveGame> Reloaded(UGameplayStatics::LoadGameFromMemory(Bytes));
	TStrongObjectPtr<UMSDeckSubsystem> Restored(MakeTestDeck());
	TestTrue(TEXT("deliberately empty deck is valid"), Restored->RestoreSaveGameForAutomationTests(Cast<UMSDeckSaveGame>(Reloaded.Get())));
	for (int32 Index = 0; Index < UMSDeckSubsystem::SlotCount; ++Index)
	{
		TestNull(TEXT("empty deck does not receive a replacement catalog suit"), Restored->GetPresetInSlot(Index));
		TestEqual(TEXT("empty deck still exposes three equipment positions"), Restored->GetLoadoutInSlot(Index).EquippedWeapons.Num(), 3);
	}
	TestEqual(TEXT("empty deck keeps its valid selection"), Restored->GetSelectedSlotIndex(), 3);
	return true;
}

bool FMSDeckSelectionCompletionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	TStrongObjectPtr<UMSMobileSuitData> Preset(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	Deck->AssignPreset(0, Preset.Get());
	Deck->AssignPreset(1, Preset.Get());

	bool bCompleted = false;
	bool bSucceeded = false;
	TestTrue(TEXT("selection request accepts an occupied target"),
		Deck->RequestSelectedSlotIndex(1, [&bCompleted, &bSucceeded](const bool bSuccess)
		{
			bCompleted = true;
			bSucceeded = bSuccess;
		}));
	TestTrue(TEXT("offline selection completion is delivered"), bCompleted);
	TestTrue(TEXT("offline selection completion reports success"), bSucceeded);
	TestEqual(TEXT("successful completion changes the committed selection"), Deck->GetSelectedSlotIndex(), 1);

	bCompleted = false;
	bSucceeded = true;
	TestFalse(TEXT("selection request rejects an invalid target"),
		Deck->RequestSelectedSlotIndex(UMSDeckSubsystem::SlotCount, [&bCompleted, &bSucceeded](const bool bSuccess)
		{
			bCompleted = true;
			bSucceeded = bSuccess;
		}));
	TestTrue(TEXT("rejected selection still completes synchronously"), bCompleted);
	TestFalse(TEXT("rejected selection completion reports failure"), bSucceeded);
	TestEqual(TEXT("rejected selection retains the committed index"), Deck->GetSelectedSlotIndex(), 1);
	return true;
}

bool FMSDeckPersistenceFallbackTest::RunTest(const FString&)
{
	const FString FirstRunSlot = FString::Printf(TEXT("NeoMSGO_DeckFirstRun_%s"), *FGuid::NewGuid().ToString());
	const FString VersionSlot = FString::Printf(TEXT("NeoMSGO_DeckVersionFallback_%s"), *FGuid::NewGuid().ToString());
	const FString CorruptSlot = FString::Printf(TEXT("NeoMSGO_DeckCorruptFallback_%s"), *FGuid::NewGuid().ToString());
	TestFalse(TEXT("first-run fixture starts without save data"), UGameplayStatics::DoesSaveGameExist(FirstRunSlot, 0));
	TStrongObjectPtr<UMSDeckSubsystem> FirstRunDeck(MakeTestDeck());
	FirstRunDeck->SetSaveSlotForAutomationTests(FirstRunSlot);
	FirstRunDeck->EnablePersistenceForAutomationTests();
	TestFalse(TEXT("missing data reports default-deck fallback"), FirstRunDeck->ReloadDeck());
	TestNotNull(TEXT("missing data produces a non-empty default deck"), FirstRunDeck->GetPresetInSlot(0));
	TestTrue(TEXT("first-run default is persisted"), UGameplayStatics::DoesSaveGameExist(FirstRunSlot, 0));

	UMSDeckSaveGame* WrongVersion = CastChecked<UMSDeckSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSDeckSaveGame::StaticClass()));
	WrongVersion->Version = UMSDeckSaveGame::CurrentVersion + 1;
	WrongVersion->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	TestTrue(TEXT("version-mismatch fixture is written"), UGameplayStatics::SaveGameToSlot(WrongVersion, VersionSlot, 0));
	TArray<uint8> OriginalVersionBytes;
	TestTrue(TEXT("original unsupported-version bytes are captured"), UGameplayStatics::LoadDataFromSlot(OriginalVersionBytes, VersionSlot, 0));

	TStrongObjectPtr<UMSDeckSubsystem> VersionDeck(MakeTestDeck());
	TStrongObjectPtr<UMSMobileSuitData> ExistingSuit(NewObject<UMSMobileSuitData>());
	VersionDeck->AssignPreset(0, ExistingSuit.Get());
	VersionDeck->SetSaveSlotForAutomationTests(VersionSlot);
	VersionDeck->EnablePersistenceForAutomationTests();
	TestFalse(TEXT("version mismatch reports failure"), VersionDeck->ReloadDeck());
	TestEqual(TEXT("version mismatch retains the current in-memory deck"), VersionDeck->GetPresetInSlot(0), ExistingSuit.Get());
	TestFalse(TEXT("unsupported version blocks later overwrite"), VersionDeck->SaveDeck());
	TestFalse(TEXT("unsupported version blocks a later equipment commit"), VersionDeck->CommitLoadout(0, FMSDeckLoadout()));
	TestFalse(TEXT("unsupported version blocks a later removal"), VersionDeck->RemovePreset(0));
	TArray<uint8> PreservedVersionBytes;
	TestTrue(TEXT("unsupported save remains readable as original bytes"), UGameplayStatics::LoadDataFromSlot(PreservedVersionBytes, VersionSlot, 0));
	TestTrue(TEXT("unsupported save is preserved byte for byte"), PreservedVersionBytes == OriginalVersionBytes);

	UMSDeckSaveGame* CorruptSave = CastChecked<UMSDeckSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSDeckSaveGame::StaticClass()));
	CorruptSave->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	CorruptSave->Slots[0].MobileSuit = FSoftObjectPath(TEXT("/Game/Invalid/MissingSuit.MissingSuit"));
	TestTrue(TEXT("broken-reference corruption fixture is written"), UGameplayStatics::SaveGameToSlot(CorruptSave, CorruptSlot, 0));
	TArray<uint8> OriginalCorruptBytes;
	TestTrue(TEXT("original corrupt bytes are captured"), UGameplayStatics::LoadDataFromSlot(OriginalCorruptBytes, CorruptSlot, 0));
	TStrongObjectPtr<UMSDeckSubsystem> CorruptDeck(MakeTestDeck());
	CorruptDeck->AssignPreset(0, ExistingSuit.Get());
	CorruptDeck->SetSaveSlotForAutomationTests(CorruptSlot);
	CorruptDeck->EnablePersistenceForAutomationTests();
	TestFalse(TEXT("unresolved suit reference reports failure"), CorruptDeck->ReloadDeck());
	TestEqual(TEXT("unresolved data retains the current in-memory deck"), CorruptDeck->GetPresetInSlot(0), ExistingSuit.Get());
	TestFalse(TEXT("unresolved save blocks later overwrite"), CorruptDeck->SaveDeck());
	TArray<uint8> PreservedCorruptBytes;
	TestTrue(TEXT("unresolved save remains readable as original bytes"), UGameplayStatics::LoadDataFromSlot(PreservedCorruptBytes, CorruptSlot, 0));
	TestTrue(TEXT("unresolved save is preserved byte for byte"), PreservedCorruptBytes == OriginalCorruptBytes);

	UGameplayStatics::DeleteGameInSlot(FirstRunSlot, 0);
	UGameplayStatics::DeleteGameInSlot(VersionSlot, 0);
	UGameplayStatics::DeleteGameInSlot(CorruptSlot, 0);
	return true;
}

bool FMSDeckModulePersistenceTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeTestDeck());
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Engine/Transient/MSDeckModules_%s"), *FGuid::NewGuid().ToString()));
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Package, TEXT("SharedSuit"));
	UMSModuleData* DefaultModule = NewObject<UMSModuleData>(Package, TEXT("DefaultModule"));
	UMSModuleData* CustomModule = NewObject<UMSModuleData>(Package, TEXT("CustomModule"));
	Suit->EquippedModule = DefaultModule;
	for (int32 Version : {1, 2})
	{
		TStrongObjectPtr<UMSDeckSaveGame> Legacy(NewObject<UMSDeckSaveGame>());
		Legacy->Version = Version;
		Legacy->Slots.SetNum(UMSDeckSubsystem::SlotCount);
		for (int32 Index : {0, 1})
		{
			Legacy->Slots[Index].MobileSuit = FSoftObjectPath(Suit);
			Legacy->Slots[Index].EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
		}
		TArray<uint8> Bytes;
		TestTrue(TEXT("legacy module-less save serializes"), UGameplayStatics::SaveGameToMemory(Legacy.Get(), Bytes));
		TStrongObjectPtr<USaveGame> Reloaded(UGameplayStatics::LoadGameFromMemory(Bytes));
		TestTrue(TEXT("legacy save migrates"), Deck->RestoreSaveGameForAutomationTests(Cast<UMSDeckSaveGame>(Reloaded.Get())));
		TestEqual(TEXT("legacy default module survives migration"), Deck->GetLoadoutInSlot(0).EquippedModule.Get(), DefaultModule);
	}
	FScopedDeckSaveSlot SavedSlot(Deck.Get());
	FMSDeckLoadout Candidate = Deck->GetLoadoutInSlot(0);
	Candidate.EquippedModule = CustomModule;
	TestEqual(TEXT("candidate selection does not change committed module"), Deck->GetLoadoutInSlot(0).EquippedModule.Get(), DefaultModule);
	TestTrue(TEXT("module commit uses real isolated save"), Deck->CommitLoadout(0, Candidate));
	TestEqual(TEXT("same suit second slot retains default"), Deck->GetLoadoutInSlot(1).EquippedModule.Get(), DefaultModule);
	TestEqual(TEXT("shared asset remains unchanged"), Suit->EquippedModule.Get(), DefaultModule);
	Candidate.EquippedModule.Reset();
	Deck->SetSaveSlotForAutomationTests(TEXT(""));
	TestFalse(TEXT("failed module removal is reported"), Deck->CommitLoadout(0, Candidate));
	TestEqual(TEXT("failed removal rolls back module"), Deck->GetLoadoutInSlot(0).EquippedModule.Get(), CustomModule);
	Deck->SetSaveSlotForAutomationTests(SavedSlot.Name);
	TestTrue(TEXT("successful removal persists"), Deck->CommitLoadout(0, Candidate));
	TestTrue(TEXT("real saved deck reloads"), Deck->ReloadDeck());
	TestTrue(TEXT("explicit null stays removed instead of preset fallback"), Deck->GetLoadoutInSlot(0).EquippedModule.IsNull());
	TestEqual(TEXT("same suit second slot survives reload"), Deck->GetLoadoutInSlot(1).EquippedModule.Get(), DefaultModule);
	TStrongObjectPtr<UMSDeckSaveGame> Save(Deck->CreateSaveGameForAutomationTests());
	TestEqual(TEXT("module save is v4"), Save->Version, UMSDeckSaveGame::CurrentVersion);
	Save->Slots[0].EquippedModule = FSoftObjectPath(Suit);
	TestFalse(TEXT("wrong module type is rejected atomically"), Deck->RestoreSaveGameForAutomationTests(Save.Get()));
	TestTrue(TEXT("rejected load retains explicit removal"), Deck->GetLoadoutInSlot(0).EquippedModule.IsNull());
	Save->Slots[0].EquippedModule.Reset();
	Save->Slots[3].EquippedModule = FSoftObjectPath(CustomModule);
	TestFalse(TEXT("module in empty suit slot is rejected"), Deck->RestoreSaveGameForAutomationTests(Save.Get()));
	Candidate.EquippedModule = TSoftObjectPtr<UMSModuleData>(FSoftObjectPath(Suit));
	TestFalse(TEXT("wrong module type candidate is rejected"), Deck->CommitLoadout(0, Candidate));
	const FString PreviousCommandLine = FCommandLine::Get();
	FCommandLine::Append(TEXT(" -MSAuthMode=Online"));
	TestFalse(TEXT("online module equipment is unsupported"), Deck->SupportsModuleEquipment());
	Candidate.EquippedModule = CustomModule;
	TestFalse(TEXT("online weapon commit cannot silently discard a module candidate"), Deck->CommitLoadout(0, Candidate));
	Candidate.EquippedModule.Reset();
	TestFalse(TEXT("online commit cannot silently remove a local equipped module"), Deck->CommitLoadout(1, Candidate));
	FCommandLine::Set(*PreviousCommandLine);
	TestEqual(TEXT("online rejection preserved local module"), Deck->GetLoadoutInSlot(1).EquippedModule.Get(), DefaultModule);
	return true;
}

#endif
