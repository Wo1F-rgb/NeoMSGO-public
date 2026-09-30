#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/MSPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSOnlineTypes.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeploymentDeckSnapshotSeparationTest,
	"NeoMSGO.UI.DeploymentDeck.SnapshotSeparationAndReturnSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSDeploymentDeckTests
{
	/**
	 * A real GameInstance/local-player/controller path is required here: the deployment
	 * accessors intentionally discover the admitted snapshot from the local controller.
	 */
	struct FScopedWorld
	{
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		UWorld* World = nullptr;
		AMSPlayerController* PlayerController = nullptr;

		FScopedWorld()
		{
			GameInstance->AddToRoot();
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			if (!World)
			{
				return;
			}

			World->InitializeActorsForPlay(FURL());
			ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
			PlayerController = World->SpawnActor<AMSPlayerController>();
			if (PlayerController)
			{
				PlayerController->SetPlayer(LocalPlayer);
			}
		}

		~FScopedWorld()
		{
			if (GameInstance)
			{
				GameInstance->Shutdown();
			}
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			if (World)
			{
				World->DestroyWorld(false);
			}
			if (GameInstance)
			{
				GameInstance->RemoveFromRoot();
			}
		}
	};

	struct FScopedSaveSlot
	{
		FString Name = FString::Printf(TEXT("NeoMSGO_DeploymentDeckTest_%s"), *FGuid::NewGuid().ToString());

		explicit FScopedSaveSlot(UMSDeckSubsystem* Deck)
		{
			Deck->SetSaveSlotForAutomationTests(Name);
			Deck->EnablePersistenceForAutomationTests();
		}

		~FScopedSaveSlot()
		{
			UGameplayStatics::DeleteGameInSlot(Name, 0);
		}
	};

	bool SetAdmittedSnapshot(FAutomationTestBase& Test, AMSPlayerController* PlayerController,
		const FMSOnlineDeckSnapshot& Snapshot)
	{
		if (!Test.TestNotNull(TEXT("test controller exposes the online authentication property"), PlayerController))
		{
			return false;
		}

		FProperty* AuthProperty = PlayerController->GetClass()->FindPropertyByName(TEXT("bOnlineAuthenticated"));
		FBoolProperty* AuthBool = CastField<FBoolProperty>(AuthProperty);
		if (!Test.TestNotNull(TEXT("online authentication is a real reflected property"), AuthBool))
		{
			return false;
		}
		AuthBool->SetPropertyValue_InContainer(PlayerController, true);

		FProperty* DeckProperty = PlayerController->GetClass()->FindPropertyByName(TEXT("OnlineDeck"));
		FStructProperty* DeckStruct = CastField<FStructProperty>(DeckProperty);
		if (!Test.TestNotNull(TEXT("online deck is a real reflected replicated struct"), DeckStruct))
		{
			return false;
		}
		void* Destination = DeckStruct->ContainerPtrToValuePtr<void>(PlayerController);
		if (!Test.TestNotNull(TEXT("online deck reflected storage is addressable"), Destination))
		{
			return false;
		}
		*static_cast<FMSOnlineDeckSnapshot*>(Destination) = Snapshot;
		return true;
	}
}

bool FMSDeploymentDeckSnapshotSeparationTest::RunTest(const FString&)
{
	using namespace MSDeploymentDeckTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("deployment fixture has a real GameInstance world"), Scope.World)
		|| !TestNotNull(TEXT("deployment fixture has a local PlayerController"), Scope.PlayerController))
	{
		return false;
	}

	UMSDeckSubsystem* Deck = Scope.GameInstance->GetSubsystem<UMSDeckSubsystem>();
	if (!TestNotNull(TEXT("deployment fixture owns the shared deck subsystem"), Deck))
	{
		return false;
	}
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("deployment fixture owns the account subsystem"), Online))
	{
		return false;
	}
	// The account half of this fixture is intentionally local and writable.  The
	// controller's admitted snapshot below remains independent of this setting.
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::OfflineLocal);
	if (!TestEqual(TEXT("the local controller is discoverable by the GameInstance"),
		Scope.GameInstance->GetFirstLocalPlayerController(), static_cast<APlayerController*>(Scope.PlayerController)))
	{
		return false;
	}

	FScopedSaveSlot SaveSlot(Deck);
	TStrongObjectPtr<UMSMobileSuitData> AccountSuitA(NewObject<UMSMobileSuitData>(Deck));
	TStrongObjectPtr<UMSMobileSuitData> AccountSuitB(NewObject<UMSMobileSuitData>(Deck));
	TStrongObjectPtr<UMSRangedWeaponData> AccountWeaponA(NewObject<UMSRangedWeaponData>(Deck));
	TStrongObjectPtr<UMSRangedWeaponData> AccountWeaponB(NewObject<UMSRangedWeaponData>(Deck));
	AccountSuitA->UnitName = FText::FromString(TEXT("ACCOUNT-EDIT-A"));
	AccountSuitB->UnitName = FText::FromString(TEXT("ACCOUNT-EDIT-B"));
	AccountSuitA->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	AccountSuitB->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	AccountSuitA->EquippedWeapons[0] = AccountWeaponA.Get();
	AccountSuitB->EquippedWeapons[0] = AccountWeaponB.Get();
	if (!TestTrue(TEXT("account-side slot A can be populated through the real deck API"), Deck->AssignPreset(0, AccountSuitA.Get()))
		|| !TestTrue(TEXT("account-side slot B can be populated through the real deck API"), Deck->AssignPreset(1, AccountSuitB.Get())))
	{
		return false;
	}
	Deck->SetSelectedSlotIndex(1);
	const int32 AccountSelectedSlotBefore = Deck->GetSelectedSlotIndex();
	TestEqual(TEXT("the account fixture deliberately selects a different slot"), AccountSelectedSlotBefore, 1);

	const TArray<UMSMobileSuitData*> Catalog = Deck->GetCatalogPresets();
	if (!TestTrue(TEXT("the test uses at least two registered mobile-suit assets for snapshot resolution"), Catalog.Num() >= 2))
	{
		return false;
	}
	UMSMobileSuitData* MatchSuitA = Catalog[0];
	UMSMobileSuitData* MatchSuitB = Catalog[1];
	if (!TestNotNull(TEXT("first admitted suit asset is resolvable"), MatchSuitA)
		|| !TestNotNull(TEXT("second admitted suit asset is resolvable"), MatchSuitB))
	{
		return false;
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FPrimaryAssetId> RegisteredWeaponIds;
	AssetManager.GetPrimaryAssetIdList(UMSWeaponDataBase::PrimaryAssetType, RegisteredWeaponIds);
	UMSWeaponDataBase* MatchWeaponA = nullptr;
	UMSWeaponDataBase* MatchWeaponB = nullptr;
	FPrimaryAssetId MatchWeaponAId;
	FPrimaryAssetId MatchWeaponBId;
	for (const FPrimaryAssetId& Id : RegisteredWeaponIds)
	{
		UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(AssetManager.GetPrimaryAssetObject(Id));
		if (!Weapon)
		{
			Weapon = Cast<UMSWeaponDataBase>(AssetManager.GetPrimaryAssetPath(Id).TryLoad());
		}
		if (!Weapon)
		{
			continue;
		}
		if (!MatchWeaponA)
		{
			MatchWeaponA = Weapon;
			MatchWeaponAId = Id;
		}
		else if (Weapon != MatchWeaponA)
		{
			MatchWeaponB = Weapon;
			MatchWeaponBId = Id;
			break;
		}
	}
	if (!TestNotNull(TEXT("first admitted weapon asset is resolvable"), MatchWeaponA)
		|| !TestNotNull(TEXT("second admitted weapon asset is resolvable"), MatchWeaponB))
	{
		return false;
	}

	FMSOnlineDeckSnapshot MatchSnapshot;
	MatchSnapshot.SchemaVersion = 1;
	MatchSnapshot.Revision = 9021;
	MatchSnapshot.SelectedSlot = 0;
	MatchSnapshot.Slots.SetNum(UMSDeckSubsystem::SlotCount);
	for (FMSOnlineDeckSlot& Slot : MatchSnapshot.Slots)
	{
		Slot.WeaponIds.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	}
	MatchSnapshot.Slots[0].SuitId = MatchSuitA->GetPrimaryAssetId();
	MatchSnapshot.Slots[0].WeaponIds[0] = MatchWeaponAId;
	MatchSnapshot.Slots[0].WeaponIds[2] = MatchWeaponBId;
	// Slot 1 is fully empty; slot 2 is occupied with three explicit empty weapon positions.
	MatchSnapshot.Slots[2].SuitId = MatchSuitB->GetPrimaryAssetId();
	const FMSOnlineDeckSnapshot OnlineBefore = MatchSnapshot;
	TestEqual(TEXT("the match fixture deliberately starts on slot zero"), OnlineBefore.SelectedSlot, 0);
	if (!SetAdmittedSnapshot(*this, Scope.PlayerController, MatchSnapshot))
	{
		return false;
	}

	TestEqual(TEXT("account edit slot remains separate from the admitted suit"), Deck->GetPresetInSlot(0), AccountSuitA.Get());
	TestEqual(TEXT("account edit weapon remains separate from the admitted weapon"),
		Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(AccountWeaponA.Get()));
	TestEqual(TEXT("deployment resolves the admitted suit from PlayerController snapshot"),
		Deck->GetDeploymentPresetInSlot(0), MatchSuitA);
	const FMSDeckLoadout MatchLoadout = Deck->GetDeploymentLoadoutInSlot(0);
	TestEqual(TEXT("deployment resolves the admitted first weapon"), MatchLoadout.EquippedWeapons[0].Get(), MatchWeaponA);
	TestNull(TEXT("deployment preserves an empty middle weapon slot"), MatchLoadout.EquippedWeapons[1].Get());
	TestEqual(TEXT("deployment resolves the admitted third weapon"), MatchLoadout.EquippedWeapons[2].Get(), MatchWeaponB);
	TestNull(TEXT("deployment preserves an empty admitted suit slot"), Deck->GetDeploymentPresetInSlot(1));
	const FMSDeckLoadout EmptySlotLoadout = Deck->GetDeploymentLoadoutInSlot(2);
	TestNull(TEXT("an occupied admitted suit can retain an entirely empty weapon loadout"), EmptySlotLoadout.EquippedWeapons[0].Get());
	TestEqual(TEXT("deployment selection starts at the snapshot-selected slot"), Deck->GetDeploymentSelectedSlotIndex(), OnlineBefore.SelectedSlot);
	TestFalse(TEXT("empty admitted slot cannot become the deployment selection"), Deck->SetDeploymentSelectedSlotIndex(1));
	TestEqual(TEXT("rejecting an empty selection leaves the snapshot selection unchanged"), Deck->GetDeploymentSelectedSlotIndex(), OnlineBefore.SelectedSlot);

	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect(NewObject<UMSSpawnSelectWidget>(Scope.World));
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	SpawnSelect->Initialize();
	SpawnSelect->InitializeForAutomationTests();
	Deployment->Initialize();
	Deployment->InitializeForAutomationTests();
	SpawnSelect->SetDeckSubsystemForTesting(Deck);
	Deployment->SetDeckSubsystemForTesting(Deck);
	SpawnSelect->SetDeploymentMapWidgetForTesting(Deployment.Get());
	SpawnSelect->OpenPreOperation(0.0f);
	TestEqual(TEXT("spawn selection starts on the admitted snapshot slot"), SpawnSelect->GetSelectedSlotIndex(), 0);
	if (UButton* DeployButton = Cast<UButton>(SpawnSelect->GetWidgetFromName(TEXT("SpawnDeployButton"))) )
	{
		DeployButton->OnClicked.Broadcast();
	}
	else
	{
		AddError(TEXT("spawn selection did not build its deployment button"));
		return false;
	}
	TestFalse(TEXT("the one-click deployment transition closes spawn selection"), SpawnSelect->IsOpen());
	TestTrue(TEXT("the one-click deployment transition opens the map"), Deployment->IsOpen());

	UButton* SlotBButton = Cast<UButton>(Deployment->GetWidgetFromName(TEXT("DeploymentMSListSlot2Button")));
	if (!TestNotNull(TEXT("deployment map exposes the occupied third slot button"), SlotBButton))
	{
		return false;
	}
	SlotBButton->OnClicked.Broadcast();
	TestEqual(TEXT("one map click changes the visible deployment slot immediately"), Deployment->GetSelectedDeckSlotIndex(), 2);
	TestEqual(TEXT("one map click updates the shared deployment selection"), Deck->GetDeploymentSelectedSlotIndex(), 2);
	TestEqual(TEXT("one map click does not edit the account deck selection"), Deck->GetSelectedSlotIndex(), AccountSelectedSlotBefore);
	TestEqual(TEXT("one map click does not edit the admitted revision"), Scope.PlayerController->GetOnlineDeck().Revision, OnlineBefore.Revision);
	TestEqual(TEXT("one map click does not edit the admitted selected slot field"), Scope.PlayerController->GetOnlineDeck().SelectedSlot, OnlineBefore.SelectedSlot);
	TestEqual(TEXT("one map click keeps the account suit pointer unchanged"), Deck->GetPresetInSlot(0), AccountSuitA.Get());
	TestEqual(TEXT("one map click keeps the account weapon pointer unchanged"),
		Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(AccountWeaponA.Get()));

	UButton* ReturnButton = Cast<UButton>(Deployment->GetWidgetFromName(TEXT("ReturnToSuitSelectionButton")));
	if (!TestNotNull(TEXT("deployment map exposes its return button"), ReturnButton))
	{
		return false;
	}
	ReturnButton->OnClicked.Broadcast();
	TestFalse(TEXT("return closes the deployment map"), Deployment->IsOpen());
	TestTrue(TEXT("return restores spawn selection"), SpawnSelect->IsOpen());
	TestEqual(TEXT("return synchronizes spawn selection to the map's chosen slot"), SpawnSelect->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("return displays the same admitted suit chosen on the map"), SpawnSelect->GetSelectedPreset(), MatchSuitB);
	TestEqual(TEXT("return preserves the immediate shared deployment selection"), Deck->GetDeploymentSelectedSlotIndex(), 2);
	TestEqual(TEXT("return still preserves the account-side selected slot"), Deck->GetSelectedSlotIndex(), AccountSelectedSlotBefore);
	TestEqual(TEXT("return still preserves the admitted revision"), Scope.PlayerController->GetOnlineDeck().Revision, OnlineBefore.Revision);

	SpawnSelect->Close();
	Deployment->Close();
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	TStrongObjectPtr<UMSBriefingWidget> Briefing(CreateWidget<UMSBriefingWidget>(Scope.PlayerController));
	TStrongObjectPtr<UMSSortieWidget> Sortie(CreateWidget<UMSSortieWidget>(Scope.PlayerController));
	if (!TestNotNull(TEXT("online briefing was created"), Briefing.Get()) || !TestNotNull(TEXT("online suit confirmation was created"), Sortie.Get())) return false;
	Briefing->InitializeForAutomationTests(); Sortie->InitializeForAutomationTests();
	Briefing->SetSortieWidgetForTesting(Sortie.Get()); Sortie->SetBriefingWidget(Briefing.Get());
	Briefing->Open(); Sortie->Open();
	TestEqual(TEXT("briefing suit confirmation shares deployment selection"), Sortie->GetSelectedSlotIndex(), 2);
	TestEqual(TEXT("briefing suit confirmation displays the admitted suit"), Sortie->GetPresetInSlot(0), MatchSuitA);
	Sortie->SetSelectedSlotIndex(0);
	TestEqual(TEXT("suit confirmation changes only deployment selection"), Deck->GetDeploymentSelectedSlotIndex(), 0);
	TestEqual(TEXT("suit confirmation preserves account selection"), Deck->GetSelectedSlotIndex(), AccountSelectedSlotBefore);
	TestFalse(TEXT("admitted suit cannot be removed from account through confirmation"), Sortie->RemovePresetFromSelectedSlot());
	TestFalse(TEXT("admitted replace action is disabled"), CastChecked<UButton>(Sortie->GetWidgetFromName(TEXT("SortieReplaceButton")))->GetIsEnabled());
	Sortie->Close();
	CastChecked<UButton>(Briefing->GetWidgetFromName(TEXT("EquipmentChangeButton")))->OnClicked.Broadcast();
	UMSHangarWidget* Equipment = Briefing->GetEquipmentWidget();
	if (!TestNotNull(TEXT("online equipment confirmation opens"), Equipment)) return false;
	TestTrue(TEXT("online equipment is read-only"), Equipment->IsReadOnly());
	TestEqual(TEXT("equipment confirmation uses the same admitted first weapon"), Equipment->GetSelectedWeapon(), MatchWeaponA);
	Equipment->ClearSelectedSlot(); Equipment->ConfirmSelectedWeapon(); Equipment->Close();
	TestEqual(TEXT("equipment return preserves account weapon"), Deck->GetLoadoutInSlot(0).EquippedWeapons[0].Get(), static_cast<UMSWeaponDataBase*>(AccountWeaponA.Get()));
	TestEqual(TEXT("equipment return preserves admitted revision"), Scope.PlayerController->GetOnlineDeck().Revision, OnlineBefore.Revision);
	TestEqual(TEXT("equipment return preserves deployment selection"), Deck->GetDeploymentSelectedSlotIndex(), 0);
	Briefing->CloseForBattleEnd();
	return true;
}

#endif
