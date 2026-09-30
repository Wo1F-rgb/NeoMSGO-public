#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Combat/MSAttributeSet_Vital.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Core/MSGameInstance.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSCustomizeWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSSortieWidget.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieStructureTest, "NeoMSGO.UI.Sortie.StructureAndCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieDeathOrderTest, "NeoMSGO.UI.SpawnSelect.DeathRequestsSpawnSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieRepeatedLivingDeployTest, "NeoMSGO.UI.Sortie.RepeatedLivingDeploy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieWeaponDisplayNameTest, "NeoMSGO.UI.Sortie.WeaponDisplayNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieShieldCardReadoutTest, "NeoMSGO.UI.Sortie.NativeShieldCardReadout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieCatalogToolsTest, "NeoMSGO.UI.Sortie.CatalogCategorySearchAndSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieSharedDeckAndCostTest, "NeoMSGO.UI.Sortie.SharedDeckEditsAndCostRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSortieOnlineAdmissionClockTest, "NeoMSGO.UI.Sortie.OnlineAdmissionClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSortieWidgetTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSortieWidgetTestWorld"));
		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	struct FScopedOnlineWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSortieOnlineClockWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);

		FScopedOnlineWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			FURL URL;
			URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FScopedOnlineWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	TStrongObjectPtr<UMSSortieWidget> MakeWidget(UObject* Outer = GetTransientPackage())
	{
		TStrongObjectPtr<UMSSortieWidget> Widget(NewObject<UMSSortieWidget>(Outer));
		Widget->Initialize();
		Widget->InitializeForAutomationTests();
		return Widget;
	}

	void TickWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.1f); World->Tick(LEVELTICK_All, Step); Seconds -= Step; ++GFrameCounter;
		}
	}

	void CollectButtonLabels(UWidget* Widget, TArray<FString>& OutLabels)
	{
		if (const UButton* Button = Cast<UButton>(Widget))
		{
			if (const UTextBlock* Label = Cast<UTextBlock>(Button->GetContent())) OutLabels.Add(Label->GetText().ToString());
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index) CollectButtonLabels(Panel->GetChildAt(Index), OutLabels);
		}
	}

	int32 CountImages(UWidget* Widget)
	{
		int32 Count = Cast<UImage>(Widget) ? 1 : 0;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index) Count += CountImages(Panel->GetChildAt(Index));
		}
		return Count;
	}
}

bool FMSSortieStructureTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget();
	UMSSortieWidget* Widget = WidgetGuard.Get();
	TestNotNull(TEXT("code-only sortie widget initializes"), Widget);
	TestEqual(TEXT("exactly four sortie slots exist"), Widget->GetSortieSlotCount(), 4);
	TestTrue(TEXT("sortie card reserves a separate native shield readout"), Widget->GetSlotShieldTextForTests(0).ToString().Contains(TEXT("盾なし")));
	TestNotNull(TEXT("catalog is a UScrollBox (a UListView cannot host a native C++ entry class "
		"in non-cooked builds; see UListViewBase::RebuildWidget's Blueprint-class check)"), Widget->GetCatalogListView());

	TArray<FPrimaryAssetId> RegisteredIds;
	UAssetManager::Get().GetPrimaryAssetIdList(UMSMobileSuitData::PrimaryAssetType, RegisteredIds);
	TestEqual(TEXT("catalog source is enumerated from MobileSuit primary assets"), Widget->GetCatalogItemCount(), RegisteredIds.Num());

	UMSMobileSuitData* A = NewObject<UMSMobileSuitData>(Widget); A->UnitName = FText::FromString(TEXT("A"));
	UMSMobileSuitData* B = NewObject<UMSMobileSuitData>(Widget); B->UnitName = FText::FromString(TEXT("B"));
	Widget->SetCatalogPresets({ A, B });
	TestEqual(TEXT("catalog accepts the enumerated preset source"), Widget->GetCatalogItemCount(), 2);
	TestEqual(TEXT("the scroll box actually rendered one row widget per catalog preset"),
		Widget->GetCatalogListView()->GetChildrenCount(), 2);
	Widget->SetSelectedSlotIndex(2); Widget->AssignPresetToSelectedSlot(B);
	TestEqual(TEXT("the active slot can switch among all four slots"), Widget->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("catalog selection assigns the active slot"), Widget->GetPresetInSlot(2), B);
	TestNotNull(TEXT("briefing modal has a close button"), Cast<UButton>(Widget->GetWidgetFromName(TEXT("SortieModalCloseButton"))));
	TestNotNull(TEXT("MU cost summary exists"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DummyMuCostText"))));
	TestNotNull(TEXT("mission unit 1 summary exists"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DummyMissionUnit1Text"))));
	TestNotNull(TEXT("mission unit 2 summary exists"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DummyMissionUnit2Text"))));
	TestEqual(TEXT("T8 confirmation modal does not contain a mobile-suit preview image"),
		MSSortieWidgetTests::CountImages(Widget->GetRootWidget()), 0);
	TArray<FString> ButtonLabels; MSSortieWidgetTests::CollectButtonLabels(Widget->GetRootWidget(), ButtonLabels);
	TestFalse(TEXT("button enumeration contains no deploy button"), ButtonLabels.Contains(TEXT("出撃")));
	TestFalse(TEXT("button enumeration contains no deployment-map launch button"), ButtonLabels.Contains(TEXT("配置地点を選んで出撃")));
	TestFalse(TEXT("button enumeration contains no hangar launch button"), ButtonLabels.Contains(TEXT("格納庫 / カスタマイズ")));
	return true;
}

bool FMSSortieOnlineAdmissionClockTest::RunTest(const FString&)
{
	MSSortieWidgetTests::FScopedOnlineWorld Scope;
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online sortie fixture has an account subsystem"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);

	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("online sortie fixture has a real GameState"), State)) return false;
	State->bBattleEnded = false;
	State->MatchDuration = 1200.0f;
	State->RemainingTime = State->MatchDuration;

	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget(Scope.World);
	UMSSortieWidget* Widget = WidgetGuard.Get();
	Widget->SetBriefingWidget(nullptr);
	TestEqual(TEXT("full-duration online sortie shows deployment readiness instead of local zero seconds"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("出撃準備")));

	State->RemainingTime = 734.0f;
	Widget->SetBriefingWidget(nullptr);
	TestEqual(TEXT("online sortie uses authoritative battle time"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("戦闘中 12'14")));

	State->bBattleEnded = true;
	Widget->SetBriefingWidget(nullptr);
	TestEqual(TEXT("online sortie reflects battle completion"),
		Widget->GetDisplayedCountdownText().ToString(), FString(TEXT("戦闘終了")));
	return true;
}

bool FMSSortieDeathOrderTest::RunTest(const FString&)
{
	MSSortieWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	TestNotNull(TEXT("player spawned"), Player);
	TestEqual(TEXT("before death no automatic screen was requested"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::None);
	Player->GetVitalSet()->OnDeath.Broadcast(Player, nullptr);
	TestEqual(TEXT("death notification keeps the battlefield presentation first"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::None);
	TestTrue(TEXT("death starts the disabled-suit phase"),Player->IsDeathPresentationActive());
	MSSortieWidgetTests::TickWorld(Scope.World,7.5f);
	TestEqual(TEXT("completed death wait requests spawn selection"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::SpawnSelect);
	TestEqual(TEXT("production death policy is spawn selection, never deployment map"), AMSPlayerCharacter::GetAutomaticDeathScreen(), EMSPlayerDeathScreen::SpawnSelect);
	TestNotEqual(TEXT("deployment map is not the automatic death screen"), Player->GetLastDeathScreenRequest(), EMSPlayerDeathScreen::DeploymentMap);
	return true;
}

bool FMSSortieRepeatedLivingDeployTest::RunTest(const FString&)
{
	MSSortieWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	UMSMobileSuitData* First = NewObject<UMSMobileSuitData>(Player);
	First->UnitName = FText::FromString(TEXT("First")); First->MaxArmor = 2000;
	UMSMobileSuitData* Second = NewObject<UMSMobileSuitData>(Player);
	Second->UnitName = FText::FromString(TEXT("Second")); Second->MaxArmor = 3000;
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	Respawn->SetAutoRespawnEnabled(false);

	TestTrue(TEXT("living player starts deploy-ready"), Respawn->CanDeploy());
	Respawn->DeployAtDefaultSpawn(First);
	TestTrue(TEXT("first living deploy does not consume readiness"), Respawn->CanDeploy());
	TestEqual(TEXT("first preset applied"), Player->GetMobileSuitData(), First);
	Respawn->DeployAtDefaultSpawn(Second);
	TestTrue(TEXT("second consecutive living deploy still leaves readiness available"), Respawn->CanDeploy());
	TestEqual(TEXT("second preset applied"), Player->GetMobileSuitData(), Second);
	return true;
}

bool FMSSortieWeaponDisplayNameTest::RunTest(const FString&)
{
	// Regression: deck cards previously rendered TSoftObjectPtr<UMSWeaponDataBase>.Get() and fell
	// back to the raw asset name (DL_WEAPON_...) whenever the weapon asset was not yet loaded.
	// The card must always show UMSWeaponDataBase::WeaponName (display name).
	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget();
	UMSSortieWidget* Widget = WidgetGuard.Get();

	UMSMobileSuitData* Preset = NewObject<UMSMobileSuitData>(Widget);
	Preset->UnitName = FText::FromString(TEXT("NamedLoadout"));

	// 1) In-memory weapon (always resolvable via .Get()). UMSWeaponDataBase is abstract,
	//    so instantiate the concrete ranged subclass.
	UMSRangedWeaponData* InMemory = NewObject<UMSRangedWeaponData>(Widget);
	InMemory->WeaponName = FText::FromString(TEXT("TEST-DISPLAY-NAME"));
	Preset->EquippedWeapons.Add(InMemory);

	// 2) Every registered on-disk weapon primary asset, referenced purely by soft path so the
	//    widget itself is responsible for resolving the display name (LoadSynchronous path).
	TArray<FPrimaryAssetId> WeaponIds;
	UAssetManager::Get().GetPrimaryAssetIdList(UMSWeaponDataBase::PrimaryAssetType, WeaponIds);
	int32 DiskWeaponCount = 0;
	for (const FPrimaryAssetId& Id : WeaponIds)
	{
		const FSoftObjectPath Path = UAssetManager::Get().GetPrimaryAssetPath(Id);
		if (!Path.IsValid() || DiskWeaponCount >= UMSMobileSuitData::MaxWeaponSlots - 1)
		{
			continue;
		}
		Preset->EquippedWeapons.Add(TSoftObjectPtr<UMSWeaponDataBase>(Path));
		++DiskWeaponCount;
	}

	Widget->SetSelectedSlotIndex(0);
	Widget->AssignPresetToSelectedSlot(Preset);

	const TArray<FText> Displayed = Widget->GetSlotWeaponNameTextsForTests(0);
	TestEqual(TEXT("one weapon row is rendered per equipped weapon"),
		Displayed.Num(), 1 + DiskWeaponCount);
	if (Displayed.Num() >= 1)
	{
		TestEqual(TEXT("in-memory weapon renders its display name"),
			Displayed[0].ToString(), FString(TEXT("TEST-DISPLAY-NAME")));
	}
	for (int32 Index = 1; Index < Displayed.Num(); ++Index)
	{
		const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef = Preset->EquippedWeapons[Index];
		const UMSWeaponDataBase* Weapon = WeaponRef.Get();
		TestNotNull(TEXT("widget refresh synchronously loaded the soft-referenced weapon"), Weapon);
		if (!Weapon)
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("on-disk weapon %s has a non-empty display name"),
			*WeaponRef.GetAssetName()), Weapon->WeaponName.IsEmpty());
		TestEqual(FString::Printf(TEXT("deck card shows WeaponName for %s"), *WeaponRef.GetAssetName()),
			Displayed[Index].ToString(), Weapon->WeaponName.ToString());
		TestFalse(FString::Printf(TEXT("deck card never shows the raw class/asset name for %s"),
			*WeaponRef.GetAssetName()),
			Displayed[Index].ToString().StartsWith(TEXT("DL_WEAPON")) ||
			Displayed[Index].ToString() == WeaponRef.GetAssetName());
	}
	return true;
}

bool FMSSortieShieldCardReadoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget();
	UMSSortieWidget* Widget = WidgetGuard.Get();
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Deck.Get());
	Suit->UnitName = FText::FromString(TEXT("LONG SHIELD SUIT"));
	UMSShieldWeaponData* Shield = NewObject<UMSShieldWeaponData>(Suit);
	Shield->WeaponName = FText::FromString(TEXT("LONG_NATIVE_SHIELD_NAME_THAT_MUST_NOT_HIDE_STATE"));
	Shield->CategoryTag = MSTags::Weapon_Category_Shield;
	Shield->MaxDurability = 1380.0f;
	Suit->NativeShield = Shield;
	Suit->bShieldRemovable = true;
	Deck->AssignPreset(0, Suit);
	Widget->SetDeckSubsystemForTesting(Deck.Get());

	const FString CompactShieldText = Widget->GetSlotShieldTextForTests(0).ToString();
	TestTrue(TEXT("sortie shield card keeps equipped state visible"), CompactShieldText.Contains(TEXT("装着中")));
	TestTrue(TEXT("sortie shield card keeps durability visible"), CompactShieldText.Contains(TEXT("1380")));
	TestFalse(TEXT("sortie shield card does not let a long name displace state"), CompactShieldText.Contains(TEXT("LONG_NATIVE_SHIELD_NAME")));
	return true;
}

bool FMSSortieCatalogToolsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget();
	UMSSortieWidget* Widget = WidgetGuard.Get();
	UMSMobileSuitData* Assault = NewObject<UMSMobileSuitData>(Widget); Assault->UnitName = FText::FromString(TEXT("ALPHA ASSAULT")); Assault->Cost = 300; Assault->UnitType = EMSMobileSuitUnitType::Assault;
	UMSMobileSuitData* Heavy = NewObject<UMSMobileSuitData>(Widget); Heavy->UnitName = FText::FromString(TEXT("BETA HEAVY")); Heavy->Cost = 700; Heavy->UnitType = EMSMobileSuitUnitType::Heavy;
	UMSMobileSuitData* Support = NewObject<UMSMobileSuitData>(Widget); Support->UnitName = FText::FromString(TEXT("GAMMA SUPPORT")); Support->Cost = 500; Support->UnitType = EMSMobileSuitUnitType::Support;
	Widget->SetCatalogPresets({ Assault, Heavy, Support });
	TestEqual(TEXT("COST defaults to descending order"), Widget->GetFilteredCatalogPreset(0), Heavy);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SortieCostSortButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("COST sort toggles to ascending order"), Widget->GetFilteredCatalogPreset(0), Assault);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SortieCategoryHeavyButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("category tab filters owned suits"), Widget->GetFilteredCatalogItemCount(), 1);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SortieCategoryAllButton")))->OnClicked.Broadcast();
	UEditableTextBox* Search = CastChecked<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("SortieSearchInput")));
	Search->SetText(FText::FromString(TEXT("support")));
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SortieSearchButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("case-insensitive partial-name search filters owned suits"), Widget->GetFilteredCatalogItemCount(), 1);
	TestEqual(TEXT("search selects the matching catalog row"), Widget->GetFilteredCatalogPreset(0), Support);
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("SortieSearchClearButton")))->OnClicked.Broadcast();
	TestEqual(TEXT("clear restores all owned suits"), Widget->GetFilteredCatalogItemCount(), 3);
	return true;
}

bool FMSSortieSharedDeckAndCostTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSSortieWidget> WidgetGuard = MSSortieWidgetTests::MakeWidget();
	UMSSortieWidget* Widget = WidgetGuard.Get(); Widget->SetDeckSubsystemForTesting(Deck.Get());
	TStrongObjectPtr<UMSCustomizeWidget> Customize(NewObject<UMSCustomizeWidget>(GetTransientPackage()));
	Customize->Initialize(); Customize->InitializeForAutomationTests(); Customize->SetDeckSubsystemForTesting(Deck.Get());
	UMSMobileSuitData* Current = NewObject<UMSMobileSuitData>(Deck.Get()); Current->UnitName = FText::FromString(TEXT("CURRENT")); Current->Cost = 500;
	UMSMobileSuitData* Other = NewObject<UMSMobileSuitData>(Deck.Get()); Other->UnitName = FText::FromString(TEXT("OTHER")); Other->Cost = 1000;
	UMSMobileSuitData* Valid = NewObject<UMSMobileSuitData>(Deck.Get()); Valid->UnitName = FText::FromString(TEXT("VALID")); Valid->Cost = 600;
	UMSMobileSuitData* TooExpensive = NewObject<UMSMobileSuitData>(Deck.Get()); TooExpensive->UnitName = FText::FromString(TEXT("TOO EXPENSIVE")); TooExpensive->Cost = 800;
	Deck->AssignPreset(0, Current); Deck->AssignPreset(1, Other); Widget->SetSelectedSlotIndex(0);
	Widget->SelectCatalogPreset(TooExpensive);
	TestTrue(TEXT("projected over-limit cost is red"), Widget->GetDeckCostTextColorForAutomationTests().Equals(FLinearColor(1.0f, 0.18f, 0.12f, 1.0f)));
	TestFalse(TEXT("over-limit replacement is rejected"), Widget->ReplaceWithSelectedCatalogPreset());
	TestEqual(TEXT("rejected replacement preserves subsystem identity"), Deck->GetPresetInSlot(0), Current);
	Widget->SelectCatalogPreset(Valid);
	TestTrue(TEXT("within-limit replacement succeeds"), Widget->ReplaceWithSelectedCatalogPreset());
	TestEqual(TEXT("replacement writes UMSDeckSubsystem"), Deck->GetPresetInSlot(0), Valid);
	TestTrue(TEXT("customize screen observes the shared subsystem replacement"), Customize->GetDisplayedSlotName(0).ToString().Contains(Valid->UnitName.ToString()));
	TestTrue(TEXT("remove writes UMSDeckSubsystem"), Widget->RemovePresetFromSelectedSlot());
	TestNull(TEXT("customize shared slot is empty after removal"), Deck->GetPresetInSlot(0));
	Widget->SelectCatalogPreset(Current);
	TestTrue(TEXT("insert into empty slot succeeds"), Widget->InsertSelectedCatalogPreset());
	TestEqual(TEXT("insert writes the same shared subsystem"), Deck->GetPresetInSlot(0), Current);
	return true;
}

#endif
