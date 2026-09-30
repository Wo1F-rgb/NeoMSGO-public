#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/GameInstance.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarCategoryTest, "NeoMSGO.UI.Hangar.CategoryFiltering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarSuitCatalogTest, "NeoMSGO.UI.Hangar.SuitSpecificCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarWeaponSelectionTest, "NeoMSGO.UI.Hangar.WeaponSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarWeaponPickerSyncTest, "NeoMSGO.UI.Hangar.WeaponPickerSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarDeckLoadoutFlowTest, "NeoMSGO.UI.Hangar.DeckLoadoutFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarCapacityTest, "NeoMSGO.UI.Hangar.CapacityWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarWeaponParametersTest, "NeoMSGO.UI.Hangar.WeaponParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarReloadPerformanceTest, "NeoMSGO.UI.Hangar.ReloadPerformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarSaveFailureTest, "NeoMSGO.UI.Hangar.SaveFailureMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarOnlineSyncStateTest, "NeoMSGO.UI.Hangar.OnlineSyncStateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarPendingCloseStackTest, "NeoMSGO.UI.Hangar.PendingCloseScreenStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarNativeShieldTest, "NeoMSGO.UI.Hangar.NativeShieldIndependentToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarLegacyStandaloneLoadoutTest, "NeoMSGO.UI.Hangar.LegacyStandaloneShieldSlotMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobileSuitPreviewCategoryColorTest, "NeoMSGO.UI.MobileSuitPreview.CategoryColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobileSuitPreviewVisualMeshTest, "NeoMSGO.UI.MobileSuitPreview.VisualMeshSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSHangarWidgetTests
{
	TStrongObjectPtr<UMSHangarWidget> MakeWidget(UMSDeckSubsystem* Deck, UWorld* World = nullptr)
	{
		UObject* Outer = World ? static_cast<UObject*>(World)
			: (Deck && Deck->GetWorld() ? static_cast<UObject*>(Deck->GetWorld()) : GetTransientPackage());
		TStrongObjectPtr<UMSHangarWidget> Widget(NewObject<UMSHangarWidget>(Outer));
		Widget->Initialize(); Widget->InitializeForAutomationTests(nullptr); Widget->SetDeckSubsystemForTesting(Deck);
		return Widget;
	}

	UMSRangedWeaponData* MakeWeapon(UObject* Outer, const TCHAR* Name, FGameplayTag Category, int32 Weight)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->WeaponName = FText::FromString(Name); Weapon->CategoryTag = Category; Weapon->Weight = Weight;
		return Weapon;
	}

	struct FScopedDeckSaveSlot
	{
		FString Name = FString::Printf(TEXT("NeoMSGO_HangarLoadoutTest_%s"), *FGuid::NewGuid().ToString());
		explicit FScopedDeckSaveSlot(UMSDeckSubsystem* Deck)
		{
			Deck->SetSaveSlotForAutomationTests(Name);
			Deck->EnablePersistenceForAutomationTests();
		}
		~FScopedDeckSaveSlot() { UGameplayStatics::DeleteGameInSlot(Name, 0); }
	};

	/** Every mutating Hangar test uses the same runtime deck ownership path. */
	struct FDeckFixture
	{
		TStrongObjectPtr<UMSGameInstance> GameInstance;
		TStrongObjectPtr<UMSDeckSubsystem> Deck;
		TStrongObjectPtr<UMSMobileSuitData> CatalogSuit;
		TUniquePtr<FScopedDeckSaveSlot> SaveSlot;
		UWorld* World = nullptr;

		FDeckFixture()
			: GameInstance(NewObject<UMSGameInstance>(GEngine))
			, Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()))
			, CatalogSuit(NewObject<UMSMobileSuitData>(GetTransientPackage()))
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHangarWidgetDeckTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance.Get();
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance.Get());
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			CatalogSuit->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
			Deck->AssignPreset(0, CatalogSuit.Get());
			SaveSlot = MakeUnique<FScopedDeckSaveSlot>(Deck.Get());
		}

		~FDeckFixture()
		{
			if (GameInstance) GameInstance->Shutdown();
			if (World)
			{
				if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		void SetLoadout(const FMSDeckLoadout& Loadout)
		{
			Deck->CommitLoadout(0, Loadout);
		}
	};
}

bool FMSHangarCategoryTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	UMSRangedWeaponData* MachineGunA = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG A"), MSTags::Weapon_Category_MachineGun, 5);
	UMSRangedWeaponData* MachineGunB = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG B"), MSTags::Weapon_Category_MachineGun, 6);
	UMSRangedWeaponData* MachineGunC = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG C"), MSTags::Weapon_Category_MachineGun, 7);
	UMSRangedWeaponData* MachineGunD = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG D"), MSTags::Weapon_Category_MachineGun, 8);
	UMSRangedWeaponData* MachineGunE = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG E"), MSTags::Weapon_Category_MachineGun, 9);
	UMSRangedWeaponData* MachineGunF = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("MG F"), MSTags::Weapon_Category_MachineGun, 10);
	UMSRangedWeaponData* BeamRifle = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("BR"), MSTags::Weapon_Category_BeamRifle, 7);
	Widget->SetWeaponCatalog({ MachineGunA, BeamRifle, MachineGunB, MachineGunC, MachineGunD, MachineGunE, MachineGunF });
	Widget->OpenForDeckSlot(0);

	TestEqual(TEXT("shield is not exposed as a numbered weapon category"), Widget->GetCategoryTabCount(), UMSHangarWidget::CategoryCount);
	TestNotNull(TEXT("weapon picker is a manual UScrollBox for native C++ entries"), Widget->GetWeaponListView());
	TestEqual(TEXT("default MachineGun category contains all matching weapons"), Widget->GetFilteredWeaponCount(), 6);
	for (int32 Index = 0; Index < Widget->GetFilteredWeaponCount(); ++Index)
		TestTrue(TEXT("default list source contains only MachineGun weapons"), Widget->GetFilteredWeapon(Index)->CategoryTag == MSTags::Weapon_Category_MachineGun);
	TestEqual(TEXT("first page shows at most five video-style rows"), Widget->GetWeaponListView()->GetChildrenCount(), 5);
	TestEqual(TEXT("six candidates produce two pages"), Widget->GetWeaponPageCount(), 2);
	TestEqual(TEXT("page indicator starts at one"), Widget->GetCurrentWeaponPage(), 1);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponNextPageButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("next page shows the remaining candidate"), Widget->GetWeaponListView()->GetChildrenCount(), 1);
	TestEqual(TEXT("next page updates the page indicator"), Widget->GetCurrentWeaponPage(), 2);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponPreviousPageButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("previous page restores the first five rows"), Widget->GetWeaponListView()->GetChildrenCount(), 5);

	Widget->SelectCategory(MSTags::Weapon_Category_BeamRifle);
	TestEqual(TEXT("category switch leaves only the BeamRifle weapon"), Widget->GetFilteredWeaponCount(), 1);
	TestTrue(TEXT("filtered weapon has selected tag"), Widget->GetFilteredWeapon(0)->CategoryTag == MSTags::Weapon_Category_BeamRifle);
	TestEqual(TEXT("ScrollBox rows are rebuilt on category switch"), Widget->GetWeaponListView()->GetChildrenCount(), 1);
	return true;
}

bool FMSHangarSuitCatalogTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	UMSRangedWeaponData* EquippedBazooka = MSHangarWidgetTests::MakeWeapon(Fixture.CatalogSuit.Get(), TEXT("Suit Bazooka"), MSTags::Weapon_Category_Bazooka, 5);
	FMSDeckLoadout InitialLoadout = Fixture.Deck->GetLoadoutInSlot(0); InitialLoadout.EquippedWeapons[0] = EquippedBazooka; Fixture.SetLoadout(InitialLoadout);
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	UMSRangedWeaponData* SameCategory = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Alternate Bazooka"), MSTags::Weapon_Category_Bazooka, 6);
	UMSRangedWeaponData* UnequippedBeamRifle = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Catalog BR"), MSTags::Weapon_Category_BeamRifle, 7);
	Widget->SetWeaponCatalog({ EquippedBazooka, SameCategory, UnequippedBeamRifle });
	Widget->OpenForDeckSlot(0);
	Widget->SelectCategory(MSTags::Weapon_Category_Bazooka);
	TestEqual(TEXT("equipped category exposes equipped and same-category weapons"), Widget->GetFilteredWeaponCount(), 2);
	Widget->SelectCategory(MSTags::Weapon_Category_BeamRifle);
	TestEqual(TEXT("unequipped category exposes matching catalog rows"), Widget->GetFilteredWeaponCount(), 1);
	TestTrue(TEXT("unequipped category contains the actual matching catalog weapon"), Widget->GetFilteredWeapon(0) == UnequippedBeamRifle);
	return true;
}

bool FMSHangarWeaponSelectionTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	UMSRangedWeaponData* Weapon = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Selected Weapon"), MSTags::Weapon_Category_MachineGun, 12);
	Widget->SetWeaponCatalog({ Weapon });
	Widget->OpenForDeckSlot(0);

	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton1")))->OnClicked.Broadcast();
	Widget->ChooseCatalogWeapon(Weapon);
	TestNull(TEXT("catalog row selection does not equip immediately"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get());
	TestTrue(TEXT("catalog row selection is retained for comparison"), Widget->GetSelectedWeapon() == Weapon);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("EquipWeaponButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("equip confirmation writes the requested deck slot"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get() == Weapon);
	TestEqual(TEXT("deck loadout keeps exactly three numbered slots"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons.Num(), UMSMobileSuitData::MaxWeaponSlots);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("RemoveWeaponButton")))->OnClicked.Broadcast();
	TestNull(TEXT("remove confirmation clears the selected slot"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get());
	return true;
}

bool FMSHangarWeaponPickerSyncTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	UMSRangedWeaponData* EquippedBeam = MSHangarWidgetTests::MakeWeapon(
		Fixture.CatalogSuit.Get(), TEXT("Equipped Beam"), MSTags::Weapon_Category_BeamRifle, 5);
	EquippedBeam->Power = 640; EquippedBeam->Range = 780; EquippedBeam->MagazineSize = 12;
	UMSRangedWeaponData* EquippedMachineGun = MSHangarWidgetTests::MakeWeapon(
		Fixture.CatalogSuit.Get(), TEXT("Equipped MachineGun"), MSTags::Weapon_Category_MachineGun, 6);
	EquippedMachineGun->Power = 320; EquippedMachineGun->Range = 500; EquippedMachineGun->MagazineSize = 30;
	FMSDeckLoadout InitialLoadout = Fixture.Deck->GetLoadoutInSlot(0);
	InitialLoadout.EquippedWeapons[0] = EquippedBeam;
	InitialLoadout.EquippedWeapons[1] = EquippedMachineGun;
	Fixture.SetLoadout(InitialLoadout);

	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	TArray<UMSWeaponDataBase*> Catalog;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Catalog.Add(MSHangarWidgetTests::MakeWeapon(Widget.Get(), *FString::Printf(TEXT("Beam Candidate %d"), Index + 1),
			MSTags::Weapon_Category_BeamRifle, 7 + Index));
	}
	UMSRangedWeaponData* MachineGunCandidate = MSHangarWidgetTests::MakeWeapon(
		Widget.Get(), TEXT("MachineGun Candidate"), MSTags::Weapon_Category_MachineGun, 8);
	MachineGunCandidate->Power = 410; MachineGunCandidate->Range = 560; MachineGunCandidate->MagazineSize = 30;
	Catalog.Add(EquippedBeam);
	Catalog.Add(EquippedMachineGun);
	Catalog.Add(MachineGunCandidate);
	Widget->SetWeaponCatalog(Catalog);
	Widget->OpenForDeckSlot(0);

	TestEqual(TEXT("opening the frame selects the equipped weapon category"),
		Widget->GetSelectedCategory(), FGameplayTag(MSTags::Weapon_Category_BeamRifle));
	TestTrue(TEXT("opening the frame selects the equipped weapon for comparison"), Widget->GetSelectedWeapon() == EquippedBeam);
	TestEqual(TEXT("the secondary remove action is hidden when the equipped weapon is selected"),
		CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("RemoveWeaponButton")))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("the equipped weapon is brought into the page containing it"), Widget->GetCurrentWeaponPage(), 2);
	TestEqual(TEXT("the synced last page shows the remaining beam candidate row"), Widget->GetWeaponListView()->GetChildrenCount(), 1);
	TestTrue(TEXT("the synced weapon immediately supplies right-side performance data"),
		Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("威力 6400")));

	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton1")))->OnClicked.Broadcast();
	TestEqual(TEXT("changing slots switches to the equipped weapon category"),
		Widget->GetSelectedCategory(), FGameplayTag(MSTags::Weapon_Category_MachineGun));
	TestTrue(TEXT("changing slots selects that slot's equipped weapon"), Widget->GetSelectedWeapon() == EquippedMachineGun);
	TestEqual(TEXT("the new category starts on the page containing its equipped weapon"), Widget->GetCurrentWeaponPage(), 1);
	TestTrue(TEXT("the new slot keeps its own comparison values"),
		Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("威力 3200")));

	Widget->ChooseCatalogWeapon(MachineGunCandidate);
	TestTrue(TEXT("candidate selection replaces the comparison without changing the committed deck"),
		Widget->GetSelectedWeapon() == MachineGunCandidate
		&& Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get() == EquippedMachineGun);
	TestTrue(TEXT("the candidate comparison is populated after selection"),
		Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("威力 4100")));
	return true;
}

bool FMSHangarDeckLoadoutFlowTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSMobileSuitData> CatalogSuit(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	TStrongObjectPtr<UMSRangedWeaponData> CatalogWeapon(MSHangarWidgetTests::MakeWeapon(CatalogSuit.Get(), TEXT("Catalog Weapon"), MSTags::Weapon_Category_MachineGun, 5));
	TStrongObjectPtr<UMSRangedWeaponData> Replacement(MSHangarWidgetTests::MakeWeapon(GetTransientPackage(), TEXT("Replacement"), MSTags::Weapon_Category_MachineGun, 6));
	CatalogSuit->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	CatalogSuit->EquippedWeapons[0] = CatalogWeapon.Get();
	TestTrue(TEXT("two deck slots may reference the same immutable catalog suit"), Deck->AssignPreset(0, CatalogSuit.Get()) && Deck->AssignPreset(1, CatalogSuit.Get()));
	MSHangarWidgetTests::FScopedDeckSaveSlot SaveSlot(Deck.Get());

	TStrongObjectPtr<UMSHangarWidget> Widget(NewObject<UMSHangarWidget>(GetTransientPackage()));
	Widget->Initialize();
	Widget->InitializeForAutomationTests(nullptr);
	Widget->SetDeckSubsystemForTesting(Deck.Get());
	Widget->SetWeaponCatalog({ CatalogWeapon.Get(), Replacement.Get() });

	Widget->OpenForDeckSlot(0);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton2")))->OnClicked.Broadcast();
	TestEqual(TEXT("the empty third weapon slot is selectable"), Widget->GetSelectedSlotIndex(), 2);
	Widget->ChooseCatalogWeapon(Replacement.Get());
	TestEqual(TEXT("candidate preview includes the existing and new numbered slots"), Widget->GetPreviewLoad(), 11);
	Widget->Close();
	TestTrue(TEXT("Back discards an uncommitted candidate"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get() == CatalogWeapon.Get());
	TestNull(TEXT("discarding an empty-slot candidate keeps it empty"), Deck->GetLoadoutInSlot(0).EquippedWeapons[2].Get());
	TestTrue(TEXT("Back leaves the catalog suit equipment unchanged"), CatalogSuit->EquippedWeapons[0].Get() == CatalogWeapon.Get());

	Widget->OpenForDeckSlot(0);
	Widget->ChooseCatalogWeapon(Replacement.Get());
	Widget->ConfirmSelectedWeapon();
	TestTrue(TEXT("Equip commits the candidate through the selected deck slot"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get() == Replacement.Get());
	TestTrue(TEXT("committed equipment does not mutate catalog defaults"), CatalogSuit->EquippedWeapons[0].Get() == CatalogWeapon.Get());
	TestTrue(TEXT("the same catalog suit in another deck slot keeps its own loadout"), Deck->GetLoadoutInSlot(1).EquippedWeapons[0].Get() == CatalogWeapon.Get());

	Widget->OpenForDeckSlot(0);
	Widget->ChooseCatalogWeapon(Replacement.Get());
	TestEqual(TEXT("selecting the equipped weapon previews the shared removal candidate"), Widget->GetPreviewLoad(), 0);
	Widget->Close();

	Widget->OpenForDeckSlot(0);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton1")))->OnClicked.Broadcast();
	Widget->ChooseCatalogWeapon(Replacement.Get());
	TestEqual(TEXT("moving an equipped weapon previews one copy of its weight"), Widget->GetPreviewLoad(), 6);
	Widget->ConfirmSelectedWeapon();
	TestNull(TEXT("moving equipment clears its previous numbered slot"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get());
	TestTrue(TEXT("moving equipment writes only the selected numbered slot"), Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get() == Replacement.Get());
	TestTrue(TEXT("the other deck slot still retains its independent weapon"), Deck->GetLoadoutInSlot(1).EquippedWeapons[0].Get() == CatalogWeapon.Get());
	TestTrue(TEXT("catalog defaults remain unchanged after move"), CatalogSuit->EquippedWeapons[0].Get() == CatalogWeapon.Get());

	Widget->OpenForDeckSlot(0);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton1")))->OnClicked.Broadcast();
	Widget->ChooseCatalogWeapon(Replacement.Get());
	TestEqual(TEXT("the same-slot candidate previews the removal load"), Widget->GetPreviewLoad(), 0);
	Widget->ConfirmSelectedWeapon();
	TestNull(TEXT("confirming the same-slot candidate removes the weapon"), Deck->GetLoadoutInSlot(0).EquippedWeapons[1].Get());
	return true;
}

bool FMSHangarSaveFailureTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	UMSRangedWeaponData* Candidate = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Unsaved Weapon"), MSTags::Weapon_Category_MachineGun, 9);
	Widget->SetWeaponCatalog({ Candidate }); Widget->OpenForDeckSlot(0); Widget->ChooseCatalogWeapon(Candidate);
	Fixture.Deck->SetSaveSlotForAutomationTests(TEXT(""));
	Widget->ConfirmSelectedWeapon();
	TestTrue(TEXT("save failure is visible in the weapon frame"), Widget->GetSaveStatusTextForTesting().ToString().Contains(TEXT("保存できませんでした")));
	TestTrue(TEXT("save failure explains that equipment is retained"), Widget->GetSaveStatusTextForTesting().ToString().Contains(TEXT("装備は保持されています")));
	TestNull(TEXT("failed save leaves the committed deck equipment unchanged"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get());
	TestEqual(TEXT("failed save keeps the committed load unchanged"), Widget->GetCurrentLoad(), 0);
	return true;
}

bool FMSHangarOnlineSyncStateTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	TStrongObjectPtr<UMSOnlineAccountSubsystem> Account(NewObject<UMSOnlineAccountSubsystem>(Fixture.GameInstance.Get()));
	TStrongObjectPtr<UMSRangedWeaponData> CandidateWeapon(MSHangarWidgetTests::MakeWeapon(
		Fixture.CatalogSuit.Get(), TEXT("Online candidate"), MSTags::Weapon_Category_MachineGun, 9));
	TStrongObjectPtr<UMSRangedWeaponData> BrowsedWeapon(MSHangarWidgetTests::MakeWeapon(
		Fixture.CatalogSuit.Get(), TEXT("Browsed candidate"), MSTags::Weapon_Category_MachineGun, 10));
	FMSOnlineIdentity AccountA;
	AccountA.AccountId = TEXT("account-a");
	Account->SetIdentityForAutomationTests(AccountA);
	Widget->SetWeaponCatalog({ CandidateWeapon.Get(), BrowsedWeapon.Get() });
	Widget->OpenForDeckSlot(0);
	// Bind the injected account after Open: BindDeckSubsystem resolves the
	// runtime GameInstance account and would otherwise replace the deterministic
	// test delegate source before the sync transitions are broadcast.
	Widget->SetOnlineAccountForTesting(Account.Get());

	FMSDeckLoadout Candidate = Fixture.Deck->GetLoadoutInSlot(0);
	Candidate.EquippedWeapons[0] = CandidateWeapon.Get();
	Widget->BeginOnlineSaveForAutomationTests(Candidate, 0, Account->GetAuthGeneration());
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Pending);
	TestTrue(TEXT("pending sync is shown while the online save is in flight"),
		Widget->GetSaveStatusTextForTesting().ToString().Contains(TEXT("オンラインへ保存中")));
	const FLinearColor PendingColor = Widget->GetSaveStatusColorForTesting().GetSpecifiedColor();
	TestTrue(TEXT("pending save uses the normal readable status color"), PendingColor.B > PendingColor.R);
	TestFalse(TEXT("equipment actions are disabled while the save is pending"),
		CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("EquipWeaponButton")))->GetIsEnabled());

	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Committed);
	TestTrue(TEXT("committed sync reports a successful save"),
		Widget->GetSaveStatusTextForTesting().ToString().Contains(TEXT("オンラインへ保存しました")));
	const FLinearColor CommittedColor = Widget->GetSaveStatusColorForTesting().GetSpecifiedColor();
	TestTrue(TEXT("committed save uses a calm green status color"), CommittedColor.G > CommittedColor.R && CommittedColor.G > CommittedColor.B * 0.7f);
	TestTrue(TEXT("committed candidate becomes the displayed equipment"),
		Widget->GetEquippedComparisonTextForTesting().ToString().Contains(TEXT("Online candidate")));
	// Browsing a different selection after a completed save must not make that
	// new, still-uncommitted preview look like the saved result. Pending and
	// failed paths below continue to exercise their own status messages.
	Widget->ChooseCatalogWeapon(BrowsedWeapon.Get());
	TestTrue(TEXT("browsing after a committed save clears only the stale success notice"),
		Widget->GetSaveStatusTextForTesting().IsEmpty());

	// A failed PUT keeps the previous committed equipment visible and includes
	// the server's retryable detail in the status panel.
	Widget->BeginOnlineSaveForAutomationTests(Candidate, 0, Account->GetAuthGeneration());
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Pending);
	Account->OnRequestFinished.Broadcast(TEXT("deck_put"), false, FText::FromString(TEXT("競合: revision 4")), Account->GetAuthGeneration());
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Failed);
	const FString FailedStatus = Widget->GetSaveStatusTextForTesting().ToString();
	const FLinearColor FailedColor = Widget->GetSaveStatusColorForTesting().GetSpecifiedColor();
	TestTrue(TEXT("failed sync exposes the server reason"), FailedStatus.Contains(TEXT("競合: revision 4")));
	TestTrue(TEXT("failed sync tells the user to retry or reload"), FailedStatus.Contains(TEXT("再試行または再読込")));
	TestTrue(TEXT("failed sync uses a red status color"), FailedColor.R > FailedColor.G * 2.0f);

	// A stale result after an account switch must not apply account A's
	// candidate to account B's still-open Hangar screen.
	FMSDeckLoadout StaleCandidate = Fixture.Deck->GetLoadoutInSlot(0);
	StaleCandidate.EquippedWeapons[0].Reset();
	Widget->BeginOnlineSaveForAutomationTests(StaleCandidate, 0, Account->GetAuthGeneration());
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Pending);
	Account->OnAccountChanged.Broadcast(FMSOnlineIdentity(), Account->GetAuthGeneration() + 1);
	TestTrue(TEXT("account generation change discards the pending save"),
		Widget->GetSaveStatusTextForTesting().ToString().Contains(TEXT("保存結果を破棄")));
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Committed);
	TestTrue(TEXT("a stale commit does not overwrite the post-switch display"),
		Widget->GetEquippedComparisonTextForTesting().ToString().Contains(TEXT("Online candidate")));
	return true;
}

bool FMSHangarPendingCloseStackTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	UMSUILockSubsystem* Lock = Fixture.GameInstance->GetSubsystem<UMSUILockSubsystem>();
	if (!TestNotNull(TEXT("real GameInstance UI screen stack exists"), Lock)) return false;
	TestFalse(TEXT("fixture starts without an interactive screen"), Lock->IsAnyScreenOpen());
	Widget->OpenForDeckSlot(0); // Registers the production Hangar close delegate.
	TestTrue(TEXT("opening Hangar registers a screen"), Lock->IsAnyScreenOpen());
	UMSOnlineAccountSubsystem* Account = Fixture.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("real account subsystem exists for the pending generation"), Account)) return false;
	Widget->BeginOnlineSaveForAutomationTests(Fixture.Deck->GetLoadoutInSlot(0), 0, Account->GetAuthGeneration());
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Pending);
	// Exercise the same stack entry used by Escape, not a direct Widget.Close().
	TestTrue(TEXT("Escape is consumed by the pending screen"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("pending Hangar remains visible"), Widget->IsOpen());
	TestTrue(TEXT("rejected close retains its screen entry"), Lock->IsAnyScreenOpen());
	TestFalse(TEXT("pending save keeps gameplay locked"), Lock->IsGameplayActive());
	TestTrue(TEXT("a second Escape still reaches Hangar"), Lock->RequestCloseTopScreen());
	TestTrue(TEXT("a second rejected close keeps the same screen visible"), Widget->IsOpen());
	TestFalse(TEXT("repeated Escape cannot unlock gameplay"), Lock->IsGameplayActive());

	// This is a UI state/delegate regression; transaction/HTTP behavior is tested
	// independently. A terminal committed notification releases the pending gate.
	Fixture.Deck->OnDeckSyncStateChanged.Broadcast(EMSOnlineDeckSyncState::Committed);
	TestTrue(TEXT("completed save can close through the same screen stack"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("completed Hangar is collapsed"), Widget->IsOpen());
	TestFalse(TEXT("normal close removes the screen entry"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("normal close restores gameplay permission"), Lock->IsGameplayActive());
	TestFalse(TEXT("no stale screen delegate remains after closing"), Lock->RequestCloseTopScreen());
	return true;
}

bool FMSHangarCapacityTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture; Fixture.CatalogSuit->LoadCapacity = 20;
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	Widget->OpenForDeckSlot(0);
	const FLinearColor NormalColor = Widget->GetLoadTextColor().GetSpecifiedColor();
	TestFalse(TEXT("empty loadout is not over capacity"), Widget->IsOverCapacity());

	UMSRangedWeaponData* HeavyWeapon = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Heavy"), MSTags::Weapon_Category_Bazooka, 25);
	Widget->SetWeaponCatalog({ HeavyWeapon });
	Widget->ChooseCatalogWeapon(HeavyWeapon);
	TestEqual(TEXT("selection previews changed load before confirmation"), Widget->GetPreviewLoad(), 25);
	TestEqual(TEXT("current load stays unchanged before confirmation"), Widget->GetCurrentLoad(), 0);
	TestTrue(TEXT("preview load above LoadCapacity enters warning state"), Widget->IsOverCapacity());
	Widget->ConfirmSelectedWeapon();
	TestEqual(TEXT("current load is the equipped weapon weight"), Widget->GetCurrentLoad(), 25);
	TestTrue(TEXT("load above LoadCapacity enters warning state"), Widget->IsOverCapacity());
	TestNotEqual(TEXT("over-capacity display color differs from normal"), Widget->GetLoadTextColor().GetSpecifiedColor(), NormalColor);
	return true;
}

bool FMSHangarWeaponParametersTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture; TStrongObjectPtr<UMSHangarWidget> Widget=MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World); Widget->OpenForDeckSlot(0);
	UMSRangedWeaponData* Weapon=MSHangarWidgetTests::MakeWeapon(Widget.Get(),TEXT("Rifle"),MSTags::Weapon_Category_BeamRifle,8);Weapon->Power=420;Weapon->Range=750;Weapon->FireInterval=.25f;Weapon->MagazineSize=12;Weapon->ReserveAmmo=48;Weapon->ReloadTime=1.8f;
	Widget->SetWeaponCatalog({ Weapon }); Widget->ChooseCatalogWeapon(Weapon);const FString Parameters=Widget->GetWeaponParametersTextForTesting().ToString();TestEqual(TEXT("actual HP damage remains unchanged"),Weapon->Power,420);TestEqual(TEXT("weapon data converts to display power"),Weapon->GetDisplayPower(),4200);TestTrue(TEXT("power uses the display scale"),Parameters.Contains(TEXT("威力 4200")));TestTrue(TEXT("power bar retains its meaningful fraction after display conversion"),FMath::IsNearlyEqual(Widget->GetPowerBarPercentForTesting(true),420.0f/5000.0f));TestTrue(TEXT("legacy range keeps the old world distance"),FMath::IsNearlyEqual(Weapon->GetRangeWorldCm(),750.0f*100.0f*MSGO::MSScale));TestTrue(TEXT("legacy world-meter range is displayed"),Parameters.Contains(FString::Printf(TEXT("射程 %.0f"),750.0f*MSGO::MSScale)));TestTrue(TEXT("1.8 seconds is displayed as original reload performance 820"),Parameters.Contains(TEXT("リロード 820")));TestTrue(TEXT("supplementary weapon parameters stay on one visible line"), !Parameters.Contains(TEXT("\n")));
	Weapon->RangeWorldScale=1.0f;Weapon->LockOnRange=600.0f;Widget->ChooseCatalogWeapon(Weapon);const FString CalibratedParameters=Widget->GetWeaponParametersTextForTesting().ToString();TestTrue(TEXT("calibrated range maps exact meters to world centimeters"),FMath::IsNearlyEqual(Weapon->GetRangeWorldCm(),75000.0f));TestTrue(TEXT("calibrated lock-on range uses the same meter contract"),FMath::IsNearlyEqual(Weapon->GetLockOnRangeWorldCm(),60000.0f));TestTrue(TEXT("calibrated range appears in hangar meters"),CalibratedParameters.Contains(TEXT("射程 750")));TestTrue(TEXT("calibrated display lock-on is in meters"),FMath::IsNearlyEqual(Weapon->GetDisplayLockOnRangeMeters(),600.0f));return true;
}

bool FMSHangarNativeShieldTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Fixture.CatalogSuit.Get());
	Shield->WeaponName = FText::FromString(TEXT("NATIVE TEST SHIELD"));
	Shield->CategoryTag = MSTags::Weapon_Category_Shield;
	Shield->MaxDurability = 1380.0f;
	Fixture.CatalogSuit->NativeShield = Shield;
	Fixture.CatalogSuit->bShieldRemovable = true;
	UMSRangedWeaponData* MachineGun = MSHangarWidgetTests::MakeWeapon(Fixture.CatalogSuit.Get(), TEXT("NATIVE TEST MACHINE GUN"), MSTags::Weapon_Category_MachineGun, 7);
	FMSDeckLoadout Initial = Fixture.Deck->GetLoadoutInSlot(0);
	Initial.bShieldEquipped = true;
	Fixture.SetLoadout(Initial);
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World);
	Widget->SetWeaponCatalog({ Shield, MachineGun });
	Widget->OpenForDeckSlot(0);

	TestTrue(TEXT("native shield support is read from the suit"), Widget->IsNativeShieldSupported());
	TestTrue(TEXT("native shield starts equipped independently"), Widget->IsNativeShieldEquipped());
	TestTrue(TEXT("native shield status puts equipped state before its long name"), Widget->GetNativeShieldStatusTextForTesting().ToString().StartsWith(TEXT("装着中 | ")));
	TestTrue(TEXT("native shield readout includes its durability"), Widget->GetNativeShieldDurabilityTextForTesting().ToString().Contains(TEXT("1380")));
	TestTrue(TEXT("equipped shield button exposes the remove action"), Widget->GetNativeShieldToggleTextForTesting().ToString().Contains(TEXT("シールドを外す")));
	TestEqual(TEXT("shield asset never enters the weapon picker while a normal weapon remains available"), Widget->GetFilteredWeaponCount(), 1);
	TestEqual(TEXT("the remaining picker row is the normal weapon"), Widget->GetFilteredWeapon(0), static_cast<UMSWeaponDataBase*>(MachineGun));
	Widget->SelectWeaponForSlot(0, Shield);
	TestTrue(TEXT("programmatic shield selection leaves weapon slots untouched"), Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons.Num() == UMSMobileSuitData::MaxWeaponSlots && Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[0].IsNull());

	const FMSDeckLoadout BeforeWeapons = Fixture.Deck->GetLoadoutInSlot(0);
	Widget->ToggleNativeShield();
	const FMSDeckLoadout Unequipped = Fixture.Deck->GetLoadoutInSlot(0);
	TestFalse(TEXT("shield toggle commits unequip"), Unequipped.bShieldEquipped);
	TestTrue(TEXT("unequipping shield preserves every weapon slot"), Unequipped.EquippedWeapons == BeforeWeapons.EquippedWeapons);
	TestTrue(TEXT("unequipped state is visible in the readout"), Widget->GetNativeShieldStatusTextForTesting().ToString().Contains(TEXT("未装備")));
	TestTrue(TEXT("unequipped shield button exposes the equip action"), Widget->GetNativeShieldToggleTextForTesting().ToString().Contains(TEXT("シールドを装着する")));
	Widget->ToggleNativeShield();
	TestTrue(TEXT("shield can be re-equipped without changing weapons"), Fixture.Deck->GetLoadoutInSlot(0).bShieldEquipped && Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons == BeforeWeapons.EquippedWeapons);
	return true;
}

bool FMSHangarLegacyStandaloneLoadoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSMobileSuitData> Suit(NewObject<UMSMobileSuitData>(GetTransientPackage()));
	Suit->UnitName = FText::FromString(TEXT("LEGACY STANDALONE SUIT"));
	Suit->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	UMSRangedWeaponData* RifleA = MSHangarWidgetTests::MakeWeapon(Suit.Get(), TEXT("Legacy Rifle A"), MSTags::Weapon_Category_MachineGun, 5);
	UMSShieldWeaponData* LegacyShield = NewObject<UMSShieldWeaponData>(Suit.Get());
	LegacyShield->WeaponName = FText::FromString(TEXT("Legacy Numbered Shield"));
	LegacyShield->CategoryTag = MSTags::Weapon_Category_Shield;
	LegacyShield->Weight = 99;
	UMSRangedWeaponData* RifleB = MSHangarWidgetTests::MakeWeapon(Suit.Get(), TEXT("Legacy Rifle B"), MSTags::Weapon_Category_BeamRifle, 6);
	Suit->EquippedWeapons[0] = RifleA;
	Suit->EquippedWeapons[1] = LegacyShield;
	Suit->EquippedWeapons[2] = RifleB;

	TStrongObjectPtr<UMSHangarWidget> Widget(NewObject<UMSHangarWidget>(GetTransientPackage()));
	Widget->Initialize();
	Widget->InitializeForAutomationTests(nullptr);
	Widget->SetWeaponCatalog({ RifleA, LegacyShield, RifleB });
	Widget->Open(Suit.Get());

	TestTrue(TEXT("legacy numbered shield still discovers native suit support"), Widget->IsNativeShieldSupported());
	TestTrue(TEXT("legacy numbered shield migrates to the independent equipped state"), Widget->IsNativeShieldEquipped());
	TestEqual(TEXT("legacy shield weight is excluded from the three weapon load"), Widget->GetCurrentLoad(), 11);
	TestTrue(TEXT("legacy slot zero keeps its original weapon"), Widget->GetSelectedWeapon() == RifleA);
	TestEqual(TEXT("legacy shield is absent from the weapon picker"), Widget->GetFilteredWeaponCount(), 1);

	UButton* LegacyShieldSlotButton = Cast<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton1")));
	if (!TestNotNull(TEXT("legacy shield slot button keeps the generated zero-based widget name"), LegacyShieldSlotButton)) return false;
	LegacyShieldSlotButton->OnClicked.Broadcast();
	TestNull(TEXT("legacy shield slot remains empty at its original number"), Widget->GetSelectedWeapon());
	UButton* FollowingWeaponSlotButton = Cast<UButton>(Widget->GetWidgetFromName(TEXT("WeaponSlotButton2")));
	if (!TestNotNull(TEXT("weapon after the legacy shield has a generated slot button"), FollowingWeaponSlotButton)) return false;
	FollowingWeaponSlotButton->OnClicked.Broadcast();
	TestTrue(TEXT("weapon after the legacy shield keeps its original number"), Widget->GetSelectedWeapon() == RifleB);
	return true;
}

bool FMSHangarReloadPerformanceTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	auto NormalizeSpaces = [](FString Value)
	{
		while (Value.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
		return Value;
	};
	UMSRangedWeaponData* Slow = MSHangarWidgetTests::MakeWeapon(Fixture.CatalogSuit.Get(), TEXT("Slow"), MSTags::Weapon_Category_MachineGun, 5);
	Slow->MagazineSize = 30; Slow->ReloadTime = 3.0f;
	FMSDeckLoadout InitialLoadout = Fixture.Deck->GetLoadoutInSlot(0); InitialLoadout.EquippedWeapons[0] = Slow; Fixture.SetLoadout(InitialLoadout);
	TStrongObjectPtr<UMSHangarWidget> Widget = MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(), Fixture.World); Widget->OpenForDeckSlot(0);
	UMSRangedWeaponData* Fast = MSHangarWidgetTests::MakeWeapon(Widget.Get(), TEXT("Fast"), MSTags::Weapon_Category_MachineGun, 5);
	Fast->MagazineSize = 45; Fast->ReloadTime = 1.4f;
	Widget->SetWeaponCatalog({ Slow, Fast });
	Widget->ChooseCatalogWeapon(Fast);
	TestTrue(TEXT("equipped 3-second weapon shows 700"), NormalizeSpaces(Widget->GetEquippedComparisonTextForTesting().ToString()).Contains(TEXT("リロード 700")));
	TestTrue(TEXT("1.4 seconds shows 860 with an improvement arrow"), Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("リロード 860 ▲")));
	TestEqual(TEXT("faster reload fills more of the performance bar"), Widget->GetReloadBarPercentForTesting(true), 0.86f);
	TestEqual(TEXT("equipped reload uses the same performance scale"), Widget->GetReloadBarPercentForTesting(false), 0.70f);
	Fast->ReloadTime = 1.12f;
	Widget->ChooseCatalogWeapon(Fast);
	TestTrue(TEXT("fractional float seconds round to original upgraded value 888"), Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("リロード 888 ▲")));
	Widget->ConfirmSelectedWeapon();
	TestTrue(TEXT("equipped panel refreshes to the selected performance"), NormalizeSpaces(Widget->GetEquippedComparisonTextForTesting().ToString()).Contains(TEXT("リロード 888")));
	TestEqual(TEXT("presentation conversion never changes runtime timer duration"), Fast->ReloadTime, 1.12f);
	Widget->ChooseCatalogWeapon(Slow);
	TestTrue(TEXT("slower selection is a performance decrease"), Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("リロード 700 ▼")));
	Slow->MagazineSize = 0;
	Widget->ChooseCatalogWeapon(Slow);
	TestTrue(TEXT("infinite-ammo weapons have no reload performance"), Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("リロード ---")));
	TestEqual(TEXT("infinite ammo is not portrayed as maximum reload performance"), Widget->GetReloadBarPercentForTesting(true), 0.0f);
	TestFalse(TEXT("infinite ammo does not report a false reload downgrade"), Widget->GetWeaponParametersTextForTesting().ToString().Contains(TEXT("リロード --- ▼")));
	Widget->ChooseCatalogWeapon(Fast);
	TestTrue(TEXT("finite candidate has a nonzero bar immediately before clearing"), Widget->GetReloadBarPercentForTesting(true) > 0.0f);
	Widget->ClearSelectedSlot();
	TestTrue(TEXT("cleared selection has no stale numeric text"), Widget->GetWeaponParametersTextForTesting().IsEmpty());
	TestEqual(TEXT("cleared selection resets the reload bar"), Widget->GetReloadBarPercentForTesting(true), 0.0f);
	return true;
}

bool FMSMobileSuitPreviewCategoryColorTest::RunTest(const FString&)
{
	TSet<FString> Colors;UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>();
	for(int32 Index=0;Index<5;++Index){Suit->UnitType=static_cast<EMSMobileSuitUnitType>(Index);Colors.Add(UMSMobileSuitPreviewRenderer::GetCategoryColor(Suit).ToString());}
	TestEqual(TEXT("all five suit categories have distinct rig colors"),Colors.Num(),5);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSPreviewRendererWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
	UMSMobileSuitPreviewRenderer* Renderer=NewObject<UMSMobileSuitPreviewRenderer>();UImage* Image=NewObject<UImage>();TestTrue(TEXT("runtime renderer initializes without uasset"),Renderer->Initialize(World,Image));TestNotNull(TEXT("runtime render target is created"),Renderer->GetRenderTarget());TestNotNull(TEXT("Cube rig preview actor is spawned"),Renderer->GetPreviewActor());Renderer->SetMobileSuit(Suit);Renderer->Shutdown();
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

bool FMSMobileSuitPreviewVisualMeshTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSPreviewVisualMeshWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL());
	UMSMobileSuitPreviewRenderer* Renderer = NewObject<UMSMobileSuitPreviewRenderer>(); UImage* Image = NewObject<UImage>();
	TestTrue(TEXT("preview renderer initializes"), Renderer->Initialize(World, Image));
	TestTrue(TEXT("preview camera keeps the approved composition at 18m scale"),
		Renderer->GetCaptureComponent() && Renderer->GetCaptureComponent()->GetRelativeLocation().Equals(
			FVector(330.0f, 270.0f, 125.0f) * MSGO::MSScale, 0.1f));
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Renderer);
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Suit);
	Shield->WeaponName = FText::FromString(TEXT("Preview Shield"));
	// Preview now renders the data's authored mesh; a meshless fixture must not fabricate the old board.
	Shield->WeaponMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/SM_DS1_DefenderShield.SM_DS1_DefenderShield")));
	Shield->CategoryTag = MSTags::Weapon_Category_Shield;
	Suit->NativeShield = Shield;
	Renderer->SetMobileSuit(Suit);
	TestFalse(TEXT("VisualMesh unset keeps mesh preview hidden"), Renderer->IsUsingVisualMesh());
	TestTrue(TEXT("VisualMesh unset keeps the Cube rig visible"), Renderer->AreRigPartsVisible());
	Renderer->SetMobileSuit(Suit, true);
	TestTrue(TEXT("Cube preview shows the native shield when equipped"), Renderer->IsNativeShieldVisible());
	Renderer->SetMobileSuit(Suit, false);
	TestFalse(TEXT("Cube preview hides the native shield when unequipped"), Renderer->IsNativeShieldVisible());
	USkeletalMesh* Taurus = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Meshes/MobileSuits/TAUrus/SK_TAUrus.SK_TAUrus"));
	if (Taurus)
	{
		Suit->VisualMesh = Taurus; Renderer->SetMobileSuit(Suit, true);
		TestTrue(TEXT("VisualMesh set shows the actual skeletal mesh"), Renderer->IsUsingVisualMesh());
		TestFalse(TEXT("VisualMesh set hides the Cube rig"), Renderer->AreRigPartsVisible());
		TestTrue(TEXT("VisualMesh preview keeps an equipped native shield visible"), Renderer->IsNativeShieldVisible());
		Renderer->SetMobileSuit(Suit, false);
		TestFalse(TEXT("VisualMesh preview hides an unequipped native shield"), Renderer->IsNativeShieldVisible());
		const UPoseableMeshComponent* Visual = Renderer->GetVisualMeshComponent();
		const FBoxSphereBounds WorldBounds = Visual->CalcBounds(Visual->GetComponentTransform());
		TestTrue(TEXT("preview VisualMesh world height remains 1800 UU within five percent"),
			FMath::IsNearlyEqual(WorldBounds.BoxExtent.Z * 2.0f, Suit->VisualMeshTargetHeightCm,
				Suit->VisualMeshTargetHeightCm * 0.05f));
	}
	else AddInfo(TEXT("SK_TAUrus not present - VisualMesh-set branch skipped"));
	// Real production assets deliberately share a bone count but not a bind pose.
	UMSMobileSuitData* MPU = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"));
	if (TestNotNull(TEXT("MPU01 production asset is available"), MPU))
	{
		UMSMobileSuitData* Mob = LoadObject<UMSMobileSuitData>(nullptr,
			TEXT("/Game/Data/MobileSuits/DA_MS_StrikerMk1.DA_MS_StrikerMk1"));
		if (TestNotNull(TEXT("previous mob production asset is available"), Mob))
		{
			USkeletalMesh* MobMesh = Mob->VisualMesh.LoadSynchronous();
			USkeletalMesh* MPUMesh = MPU->VisualMesh.LoadSynchronous();
			if (TestNotNull(TEXT("previous mob skeletal mesh loads"), MobMesh) && MPUMesh)
			{
				TestEqual(TEXT("regression uses the same bone count"), MobMesh->GetRefSkeleton().GetNum(), MPUMesh->GetRefSkeleton().GetNum());
				const TArray<FTransform>& A = MobMesh->GetRefSkeleton().GetRefBonePose();
				const TArray<FTransform>& B = MPUMesh->GetRefSkeleton().GetRefBonePose();
				bool bDifferentBind = false;
				for (int32 I = 0; I < FMath::Min(A.Num(), B.Num()); ++I) bDifferentBind |= !A[I].Equals(B[I]);
				TestTrue(TEXT("regression uses different bind poses"), bDifferentBind);
				Renderer->SetMobileSuit(Mob);
			}
		}
		Renderer->SetMobileSuit(MPU);
		UPoseableMeshComponent* Visual = Renderer->GetVisualMeshComponent();
		USkeletalMesh* Mesh = MPU->VisualMesh.LoadSynchronous();
		if (TestNotNull(TEXT("MPU01 skeletal asset loads"), Mesh))
		{
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			TArray<FTransform> ComponentPose;
			for (int32 I = 0; I < Ref.GetNum(); ++I)
			{
				const int32 Parent = Ref.GetParentIndex(I);
				ComponentPose.Add(Parent == INDEX_NONE ? Ref.GetRefBonePose()[I]
					: Ref.GetRefBonePose()[I] * ComponentPose[Parent]);
				TestTrue(FString::Printf(TEXT("switched preview restores bind bone %s"), *Ref.GetBoneName(I).ToString()),
					Visual->GetBoneTransformByName(Ref.GetBoneName(I), EBoneSpaces::ComponentSpace)
					.Equals(ComponentPose[I], 0.01f));
			}
		}
		const FTransform CameraBefore = Renderer->GetCaptureComponent()->GetComponentTransform();
		const FTransform MeshBefore = Visual->GetComponentTransform();
		Renderer->RotatePreview(90.0f);
		TestEqual(TEXT("rotation accumulates yaw"), Renderer->GetPreviewYaw(), 90.0f);
		TestFalse(TEXT("rotation changes the actual suit transform"), Visual->GetComponentTransform().Equals(MeshBefore));
		TestTrue(TEXT("rotation leaves the camera fixed"), Renderer->GetCaptureComponent()->GetComponentTransform().Equals(CameraBefore));
		Renderer->SetMobileSuit(MPU);
		TestEqual(TEXT("refreshing the same suit preserves viewing angle"), Renderer->GetPreviewYaw(), 90.0f);
		Renderer->SetMobileSuit(Suit);
		TestEqual(TEXT("selecting another suit resets viewing angle"), Renderer->GetPreviewYaw(), 0.0f);
	}
	Renderer->Shutdown();
	TestFalse(TEXT("shutdown renderer can be recognized for reinitialization"), Renderer->IsInitialized());
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarAdmittedReadOnlyTest, "NeoMSGO.UI.Hangar.AdmittedLoadoutReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHangarAdmittedReadOnlyTest::RunTest(const FString&)
{
	MSHangarWidgetTests::FDeckFixture Fixture;
	UMSRangedWeaponData* AccountWeapon=MSHangarWidgetTests::MakeWeapon(Fixture.CatalogSuit.Get(),TEXT("Account equipment"),MSTags::Weapon_Category_MachineGun,5);
	UMSRangedWeaponData* MatchWeapon=MSHangarWidgetTests::MakeWeapon(Fixture.CatalogSuit.Get(),TEXT("Admitted equipment"),MSTags::Weapon_Category_MachineGun,9);
	FMSDeckLoadout Account=Fixture.Deck->GetLoadoutInSlot(0);Account.EquippedWeapons[0]=AccountWeapon;Fixture.SetLoadout(Account);
	FMSDeckLoadout Match=Account;Match.EquippedWeapons[0]=MatchWeapon;
	TStrongObjectPtr<UMSHangarWidget> Widget=MSHangarWidgetTests::MakeWidget(Fixture.Deck.Get(),Fixture.World);
	Widget->SetWeaponCatalog({AccountWeapon,MatchWeapon});Widget->OpenReadOnlyLoadout(Fixture.CatalogSuit.Get(),Match);
	TestTrue(TEXT("match equipment is explicitly read-only"),Widget->IsReadOnly());
	TestTrue(TEXT("admitted snapshot is displayed, not the newer account equipment"),Widget->GetSelectedWeapon()==MatchWeapon);
	const int32 MatchLoad=Widget->GetCurrentLoad();
	Widget->ChooseCatalogWeapon(AccountWeapon);Widget->ConfirmSelectedWeapon();Widget->ClearSelectedSlot();
	TestTrue(TEXT("attempted commits never overwrite the account deck"),Fixture.Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get()==AccountWeapon);
	TestEqual(TEXT("attempted commits never alter admitted load"),Widget->GetCurrentLoad(),MatchLoad);
	TestFalse(TEXT("equip button cannot apply a candidate in an admitted match"),CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("EquipWeaponButton")))->GetIsEnabled());
	Widget->Close();Widget->OpenForDeckSlot(0);Widget->ChooseCatalogWeapon(MatchWeapon);
	TestFalse(TEXT("normal hangar restores editing after leaving read-only view"),Widget->IsReadOnly());
	TestTrue(TEXT("normal account hangar can equip again"),CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("EquipWeaponButton")))->GetIsEnabled());
	return true;
}

#endif
