#include "UI/MSDeckSubsystem.h"

#include "Core/MSGameInstance.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Data/MSBattleAssistRules.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Modules/MSBattleAssistPersistence.h"
#include "Data/MSWeaponData.h"
#include "Engine/AssetManager.h"
#include "Gameplay/MSPlayerController.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Class or Shield category: suit equipment that never occupies a numbered weapon slot. */
	bool IsNativeShieldData(const UMSWeaponDataBase* Weapon)
	{
		return Weapon && (Weapon->IsA<UMSShieldWeaponData>()
			|| Weapon->CategoryTag == MSTags::Weapon_Category_Shield);
	}

	FMSDeckLoadout MakeEmptyLoadout()
	{
		FMSDeckLoadout Loadout;
		Loadout.bShieldEquipped = false;
		return Loadout;
	}

	FMSDeckLoadout MakeDefaultLoadout(const UMSMobileSuitData* Preset)
	{
		FMSDeckLoadout Loadout = MakeEmptyLoadout();
		if (Preset)
		{
			Loadout.EquippedModule = Preset->EquippedModule;
			Loadout.bShieldEquipped = Preset->GetNativeShield() != nullptr;
			for (int32 Index = 0; Index < FMath::Min(Preset->EquippedWeapons.Num(), UMSMobileSuitData::MaxWeaponSlots); ++Index)
			{
				UMSWeaponDataBase* Weapon = Preset->EquippedWeapons[Index].LoadSynchronous();
				if (IsNativeShieldData(Weapon)) continue;
				Loadout.EquippedWeapons[Index] = Preset->EquippedWeapons[Index];
			}
		}
		return Loadout;
	}

	/** Property tags are written through FObjectAndNameAsStringProxyArchive as FStrings. */
	bool ContainsSerializedName(const TArray<uint8>& Bytes, const ANSICHAR* Name)
	{
		const int32 NameLength = FCStringAnsi::Strlen(Name);
		TArray<uint8> Pattern;
		const int32 SerializedLength = NameLength + 1; // FString length includes the terminator.
		Pattern.Append(reinterpret_cast<const uint8*>(&SerializedLength), sizeof(int32));
		Pattern.Append(reinterpret_cast<const uint8*>(Name), NameLength + 1);
		for (int32 Start = 0; Start + Pattern.Num() <= Bytes.Num(); ++Start)
		{
			if (FMemory::Memcmp(Bytes.GetData() + Start, Pattern.GetData(), Pattern.Num()) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

FMSDeckLoadout::FMSDeckLoadout()
{
	EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
}

UMSDeckSubsystem::UMSDeckSubsystem()
{
	SlotPresets.SetNum(SlotCount);
	SlotLoadouts.SetNum(SlotCount);
}

void UMSDeckSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMSOnlineAccountSubsystem>();
	Collection.InitializeDependency<UMSBattleAssistProgressSubsystem>();
	FillInitialDeck();
	bPersistenceReady = true;
	if (UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
	{
		BoundGameInstance = Instance;
		Instance->OnAccountChanged.AddUniqueDynamic(this, &UMSDeckSubsystem::HandleAccountChanged);
		UMSOnlineAccountSubsystem* Online = Instance->GetSubsystem<UMSOnlineAccountSubsystem>();
		if (Online && Online->IsOnlineMode())
		{
			BoundOnlineAccount = Online;
			Online->OnDeckCommitted.AddUniqueDynamic(this, &UMSDeckSubsystem::HandleOnlineDeckCommitted);
			// Existing NeoMSGO_Deck_v1 is shared and ownerless. Online login receives the
			// server deck; it is never imported into a new account implicitly.
			HandleAccountChanged(Instance->AccountId);
		}
		else
		{
			ReloadDeck();
		}
	}
	else
	{
		ReloadDeck();
	}
}

void UMSDeckSubsystem::Deinitialize()
{
	if (BoundGameInstance.IsValid())
	{
		BoundGameInstance->OnAccountChanged.RemoveDynamic(this, &UMSDeckSubsystem::HandleAccountChanged);
	}
	if (BoundOnlineAccount.IsValid())
	{
		BoundOnlineAccount->OnDeckCommitted.RemoveDynamic(this, &UMSDeckSubsystem::HandleOnlineDeckCommitted);
	}
	BoundOnlineAccount.Reset();
	BoundGameInstance.Reset();
	Super::Deinitialize();
}

void UMSDeckSubsystem::SetSelectedSlotIndex(const int32 SlotIndex)
{
	RequestSelectedSlotIndex(SlotIndex, nullptr);
}

bool UMSDeckSubsystem::RequestSelectedSlotIndex(const int32 SlotIndex, TFunction<void(bool bSucceeded)> Completion)
{
	if (!CanEditCurrentDeck() || !SlotPresets.IsValidIndex(SlotIndex))
	{
		if (Completion) Completion(false);
		return false;
	}
	if (SelectedSlotIndex == SlotIndex)
	{
		if (Completion) Completion(true);
		return true;
	}

	const int32 PreviousIndex = SelectedSlotIndex;
	if (IsOnlineAccountActive())
	{
		// Build the candidate from a temporary value, then restore the committed
		// selection until the server acknowledges the PUT.
		SelectedSlotIndex = SlotIndex;
		const FMSOnlineDeckSnapshot Candidate = BuildOnlineSnapshot();
		SelectedSlotIndex = PreviousIndex;
		const TWeakObjectPtr<UMSDeckSubsystem> WeakThis(this);
		const TSharedRef<TFunction<void(bool)>> CompletionHolder =
			MakeShared<TFunction<void(bool)>>(MoveTemp(Completion));
		const bool bStarted = BeginOnlineCommit(Candidate,
			[WeakThis, CompletionHolder](const bool bCommitted,
				const FMSOnlineDeckSnapshot& Snapshot) mutable
		{
			if (!WeakThis.IsValid()) return;
			const bool bApplied = bCommitted && WeakThis->ApplyOnlineSnapshot(Snapshot, true);
			if (!bApplied) WeakThis->OnDeckChanged.Broadcast(INDEX_NONE);
			if (*CompletionHolder)
			{
				TFunction<void(bool)> FinalCompletion = MoveTemp(*CompletionHolder);
				FinalCompletion(bApplied);
			}
		});
		if (!bStarted && *CompletionHolder)
		{
			TFunction<void(bool)> FinalCompletion = MoveTemp(*CompletionHolder);
			FinalCompletion(false);
		}
		return bStarted;
	}

	SelectedSlotIndex = SlotIndex;
	if (bPersistenceReady && !SaveDeck())
	{
		SelectedSlotIndex = PreviousIndex;
		if (Completion) Completion(false);
		return false;
	}
	OnDeckChanged.Broadcast(INDEX_NONE);
	if (Completion) Completion(true);
	return true;
}

bool UMSDeckSubsystem::AssignPreset(const int32 SlotIndex, UMSMobileSuitData* Preset)
{
	if (!CanEditCurrentDeck()) return false;
	if (!SlotPresets.IsValidIndex(SlotIndex) || !IsValid(Preset))
	{
		return false;
	}
	if (SlotPresets[SlotIndex] == Preset)
	{
		return true;
	}
	UMSMobileSuitData* PreviousPreset = SlotPresets[SlotIndex];
	const FMSDeckLoadout PreviousLoadout = SlotLoadouts[SlotIndex];
	if (IsOnlineAccountActive())
	{
		SlotPresets[SlotIndex] = Preset;
		SlotLoadouts[SlotIndex] = MakeDefaultLoadout(Preset);
		const FMSOnlineDeckSnapshot Candidate = BuildOnlineSnapshot();
		SlotPresets[SlotIndex] = PreviousPreset;
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		return BeginOnlineCommit(Candidate, [this, SlotIndex](const bool bCommitted, const FMSOnlineDeckSnapshot& Snapshot)
		{
			if (bCommitted) ApplyOnlineSnapshot(Snapshot, true);
			else { OnDeckChanged.Broadcast(SlotIndex); }
		});
	}
	SlotPresets[SlotIndex] = Preset;
	SlotLoadouts[SlotIndex] = MakeDefaultLoadout(Preset);
	if (bPersistenceReady && !SaveDeck())
	{
		SlotPresets[SlotIndex] = PreviousPreset;
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		return false;
	}
	OnDeckChanged.Broadcast(SlotIndex);
	return true;
}

bool UMSDeckSubsystem::RemovePreset(const int32 SlotIndex)
{
	if (!CanEditCurrentDeck()) return false;
	if (!SlotPresets.IsValidIndex(SlotIndex) || !SlotPresets[SlotIndex])
	{
		return false;
	}
	UMSMobileSuitData* PreviousPreset = SlotPresets[SlotIndex];
	const FMSDeckLoadout PreviousLoadout = SlotLoadouts[SlotIndex];
	if (IsOnlineAccountActive())
	{
		SlotPresets[SlotIndex] = nullptr;
		SlotLoadouts[SlotIndex] = MakeEmptyLoadout();
		const FMSOnlineDeckSnapshot Candidate = BuildOnlineSnapshot();
		SlotPresets[SlotIndex] = PreviousPreset;
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		return BeginOnlineCommit(Candidate, [this, SlotIndex](const bool bCommitted, const FMSOnlineDeckSnapshot& Snapshot)
		{
			if (bCommitted) ApplyOnlineSnapshot(Snapshot, true);
			else { OnDeckChanged.Broadcast(SlotIndex); }
		});
	}
	SlotPresets[SlotIndex] = nullptr;
	SlotLoadouts[SlotIndex] = MakeEmptyLoadout();
	if (bPersistenceReady && !SaveDeck())
	{
		SlotPresets[SlotIndex] = PreviousPreset;
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		return false;
	}
	OnDeckChanged.Broadcast(SlotIndex);
	return true;
}

FMSDeckLoadout UMSDeckSubsystem::GetLoadoutInSlot(const int32 SlotIndex) const
{
	return SlotLoadouts.IsValidIndex(SlotIndex) ? SlotLoadouts[SlotIndex] : MakeEmptyLoadout();
}

const AMSPlayerController* UMSDeckSubsystem::GetDeploymentController() const
{
	const UGameInstance* Instance = GetGameInstance();
	const auto* PC = Instance ? Cast<AMSPlayerController>(Instance->GetFirstLocalPlayerController()) : nullptr;
	return PC && PC->IsOnlineAuthenticated() ? PC : nullptr;
}

UMSMobileSuitData* UMSDeckSubsystem::GetDeploymentPresetInSlot(const int32 SlotIndex) const
{
	if (const auto* PC = GetDeploymentController())
	{
		const auto& Slots = PC->GetOnlineDeck().Slots;
		return Slots.IsValidIndex(SlotIndex) ? ResolveOnlineSuit(Slots[SlotIndex].SuitId) : nullptr;
	}
	return GetPresetInSlot(SlotIndex);
}

FMSDeckLoadout UMSDeckSubsystem::GetDeploymentLoadoutInSlot(const int32 SlotIndex) const
{
	if (const auto* PC = GetDeploymentController())
	{
		FMSDeckLoadout Result = MakeEmptyLoadout();
		const auto& Slots = PC->GetOnlineDeck().Slots;
		if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].SuitId.IsValid()) return Result;
		UMSMobileSuitData* Suit = ResolveOnlineSuit(Slots[SlotIndex].SuitId);
		if (!Suit) return Result;
		Result.BattleAssist = PC->GetOnlineDeck().Slots[SlotIndex].BattleAssist;
		Result.bShieldEquipped = PC->GetOnlineDeck().SchemaVersion >= 2
			? Slots[SlotIndex].bShieldEquipped : false;
		const auto& Weapons = Slots[SlotIndex].WeaponIds;
		for (int32 Index = 0; Index < FMath::Min(Weapons.Num(), Result.EquippedWeapons.Num()); ++Index)
		{
			UMSWeaponDataBase* Weapon = ResolveOnlineWeapon(Weapons[Index]);
			if (Cast<UMSShieldWeaponData>(Weapon))
			{
				Result.bShieldEquipped = true;
				continue;
			}
			// v1 stored the native shield in the three weapon IDs. Some old records
			// contain a generic Shield category entry instead of the shield class;
			// carry that legacy equip state forward when this suit has a native shield.
			if (IsNativeShieldData(Weapon))
			{
				if (PC->GetOnlineDeck().SchemaVersion < 2 && Suit->GetNativeShield())
					Result.bShieldEquipped = true;
				continue;
			}
			Result.EquippedWeapons[Index] = Weapon;
		}
		return Result;
	}
	return GetLoadoutInSlot(SlotIndex);
}

int32 UMSDeckSubsystem::GetDeploymentSelectedSlotIndex() const
{
	if (const auto* PC = GetDeploymentController())
		return DeploymentSelectionOwner.Get() == PC && DeploymentSelectedSlot != INDEX_NONE
			? DeploymentSelectedSlot : PC->GetOnlineDeck().SelectedSlot;
	return GetSelectedSlotIndex();
}

bool UMSDeckSubsystem::SetDeploymentSelectedSlotIndex(const int32 SlotIndex)
{
	if (!GetDeploymentPresetInSlot(SlotIndex)) return false;
	if (const auto* PC = GetDeploymentController())
	{
		// Selecting a sortie card is immediate UI state, not an account deck PUT.
		// The server still checks this index and its cooldown when deployment is requested.
		DeploymentSelectionOwner = PC;
		DeploymentSelectedSlot = SlotIndex;
		OnDeckChanged.Broadcast(INDEX_NONE);
		return true;
	}
	if (MSOnline::IsEnabled()) return false;
	SetSelectedSlotIndex(SlotIndex);
	return GetSelectedSlotIndex() == SlotIndex;
}

bool UMSDeckSubsystem::CommitLoadout(const int32 SlotIndex, const FMSDeckLoadout& Candidate)
{
	if (!CanEditCurrentDeck()) return false;
	UMSMobileSuitData* Preset = GetPresetInSlot(SlotIndex);
	if (!IsValid(Preset) || !SlotLoadouts.IsValidIndex(SlotIndex)
		|| Candidate.EquippedWeapons.Num() != UMSMobileSuitData::MaxWeaponSlots)
	{
		return false;
	}
	const FMSBattleAssistProgress Progress = GetBattleAssistProgress();
	FMSBattleAssistValidationResult Validation;
	if (!MSBattleAssist::ValidateLoadout(Preset, Candidate.BattleAssist, &Progress, Validation)) return false;
	if (Candidate.bShieldEquipped && !Preset->GetNativeShield()) return false;
	if (!Candidate.bShieldEquipped && !Preset->bShieldRemovable) return false;
	// The online schema has no module field. Never acknowledge a silently dropped module.
	if (!SupportsModuleEquipment() && (!Candidate.EquippedModule.IsNull()
		|| !SlotLoadouts[SlotIndex].EquippedModule.IsNull())) return false;
	if (!Candidate.EquippedModule.IsNull() && !IsValid(Candidate.EquippedModule.LoadSynchronous())) return false;
	for (const TSoftObjectPtr<UMSWeaponDataBase>& Weapon : Candidate.EquippedWeapons)
	{
		UMSWeaponDataBase* LoadedWeapon = Weapon.IsNull() ? nullptr : Weapon.LoadSynchronous();
		if (IsNativeShieldData(LoadedWeapon)) return false;
		if (!Weapon.IsNull() && !IsValid(LoadedWeapon))
		{
			return false;
		}
	}
	if (IsOnlineAccountActive())
	{
		const FMSDeckLoadout PreviousLoadout = SlotLoadouts[SlotIndex];
		SlotLoadouts[SlotIndex] = Candidate;
		const FMSOnlineDeckSnapshot OnlineCandidate = BuildOnlineSnapshot();
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		struct FCommitOutcome
		{
			bool bCompleted = false;
			bool bCommitted = false;
		};
		const TSharedRef<FCommitOutcome> Outcome = MakeShared<FCommitOutcome>();
		const bool bAccepted = BeginOnlineCommit(OnlineCandidate,
			[this, SlotIndex, Outcome](const bool bCommitted, const FMSOnlineDeckSnapshot& Snapshot)
		{
			Outcome->bCompleted = true;
			Outcome->bCommitted = bCommitted;
			if (bCommitted) ApplyOnlineSnapshot(Snapshot, true);
			else { OnDeckChanged.Broadcast(SlotIndex); }
		});
		// The legacy bool means committed. An asynchronous HTTP request remains
		// Pending and is reported through OnDeckSyncStateChanged instead.
		return bAccepted && Outcome->bCompleted && Outcome->bCommitted;
	}

	const FMSDeckLoadout PreviousLoadout = SlotLoadouts[SlotIndex];
	SlotLoadouts[SlotIndex] = Candidate;
	if (!SaveDeck())
	{
		SlotLoadouts[SlotIndex] = PreviousLoadout;
		return false;
	}
	OnDeckChanged.Broadcast(SlotIndex);
	return true;
}

UMSDeckSaveGame* UMSDeckSubsystem::CreateSaveGameObject() const
{
	UMSDeckSaveGame* Save = Cast<UMSDeckSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UMSDeckSaveGame::StaticClass()));
	if (!Save)
	{
		return nullptr;
	}

	Save->Slots.SetNum(SlotCount);
	for (FMSDeckSlotSaveData& Slot : Save->Slots) Slot.bShieldEquipped = false;
	Save->SelectedSlotIndex = SelectedSlotIndex;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const UMSMobileSuitData* Preset = SlotPresets[SlotIndex];
		if (!Preset)
		{
			continue;
		}

		FMSDeckSlotSaveData& Slot = Save->Slots[SlotIndex];
		Slot.MobileSuit = FSoftObjectPath(Preset->GetPathName());
		Slot.EquippedModule = SlotLoadouts[SlotIndex].EquippedModule.ToSoftObjectPath();
		Slot.bShieldEquipped = SlotLoadouts[SlotIndex].bShieldEquipped;
		Slot.BattleAssist = SlotLoadouts[SlotIndex].BattleAssist;
		Slot.EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
		for (int32 WeaponIndex = 0; WeaponIndex < UMSMobileSuitData::MaxWeaponSlots; ++WeaponIndex)
		{
			Slot.EquippedWeapons[WeaponIndex] = SlotLoadouts[SlotIndex].EquippedWeapons[WeaponIndex].ToSoftObjectPath();
		}
	}
	return Save;
}

bool UMSDeckSubsystem::SaveDeck() const
{
	// Online mode is account-owned. The legacy NeoMSGO_Deck_v1 slot has no
	// owner and must never be written while the online client is configured,
	// including the brief logged-out interval between two accounts.
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (Online && Online->IsOnlineMode()) return false;
	if (!bPersistenceReady || bExistingSaveRejected)
	{
		return false;
	}
	UMSDeckSaveGame* Save = CreateSaveGameObject();
	TArray<uint8> Bytes;
	return Save && UGameplayStatics::SaveGameToMemory(Save, Bytes) && MSBattleAssistPersistence::SaveBytes(SaveSlotName, Bytes);
}

bool UMSDeckSubsystem::ApplySaveGame(const UMSDeckSaveGame* Save)
{
	if (!Save || (Save->Version < 1 || Save->Version > UMSDeckSaveGame::CurrentVersion) || Save->Slots.Num() != SlotCount)
	{
		return false;
	}

	TArray<TObjectPtr<UMSMobileSuitData>> LoadedPresets;
	LoadedPresets.SetNum(SlotCount);
	TArray<FMSDeckLoadout> LoadedLoadouts;
	LoadedLoadouts.SetNum(SlotCount);

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const FMSDeckSlotSaveData& Slot = Save->Slots[SlotIndex];
		if (Slot.MobileSuit.IsNull())
		{
			// Empty deck slots have no loadout. Reject hidden equipment in malformed records.
			if (Slot.EquippedWeapons.ContainsByPredicate([](const FSoftObjectPath& Path) { return !Path.IsNull(); })
				|| Slot.EquippedWeapons.Num() > UMSMobileSuitData::MaxWeaponSlots || !Slot.EquippedModule.IsNull()
				|| (Save->Version >= 4 && Slot.bShieldEquipped) || !Slot.BattleAssist.IsEmpty())
			{
				return false;
			}
			LoadedLoadouts[SlotIndex] = MakeEmptyLoadout();
			continue;
		}

		UMSMobileSuitData* Preset = Cast<UMSMobileSuitData>(Slot.MobileSuit.ResolveObject());
		if (!Preset)
		{
			Preset = Cast<UMSMobileSuitData>(Slot.MobileSuit.TryLoad());
		}
		if (!IsValid(Preset) || Slot.EquippedWeapons.Num() > UMSMobileSuitData::MaxWeaponSlots
			|| (Save->Version >= 2 && Slot.EquippedWeapons.Num() != UMSMobileSuitData::MaxWeaponSlots))
		{
			return false;
		}

		// Older saves had no per-slot module; retain the former preset default once.
		// In v3, a null path is deliberate removal and must never fall back to the preset.
		const TSoftObjectPtr<UMSModuleData> Module = Save->Version < 3
			? Preset->EquippedModule : TSoftObjectPtr<UMSModuleData>(Slot.EquippedModule);
		if (!Module.IsNull() && !IsValid(Module.LoadSynchronous())) return false;
		LoadedLoadouts[SlotIndex] = MakeEmptyLoadout();
		LoadedLoadouts[SlotIndex].EquippedModule = Module;
		if (Save->Version >= 5)
		{
			const FMSBattleAssistProgress Progress = GetBattleAssistProgress();
			FMSBattleAssistValidationResult Validation;
			if (!MSBattleAssist::ValidateLoadout(Preset, Slot.BattleAssist, &Progress, Validation)) return false;
			LoadedLoadouts[SlotIndex].BattleAssist = Slot.BattleAssist;
		}
		else if (!Slot.BattleAssist.IsEmpty()) return false;
		if (Save->Version >= 4)
		{
			LoadedLoadouts[SlotIndex].bShieldEquipped = Slot.bShieldEquipped;
			if (LoadedLoadouts[SlotIndex].bShieldEquipped && !Preset->GetNativeShield()) return false;
			if (!LoadedLoadouts[SlotIndex].bShieldEquipped && !Preset->bShieldRemovable) return false;
		}
		LoadedPresets[SlotIndex] = Preset;
		int32 LegacyShieldCount = 0;
		for (int32 WeaponIndex = 0; WeaponIndex < Slot.EquippedWeapons.Num(); ++WeaponIndex)
		{
			const FSoftObjectPath& WeaponPath = Slot.EquippedWeapons[WeaponIndex];
			if (WeaponPath.IsNull())
			{
				continue;
			}
			UObject* WeaponObject = WeaponPath.ResolveObject();
			if (!WeaponObject)
			{
				WeaponObject = WeaponPath.TryLoad();
			}
			UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(WeaponObject);
			if (!IsValid(Weapon))
			{
				return false;
			}
			if (UMSShieldWeaponData* Shield = Cast<UMSShieldWeaponData>(Weapon))
			{
				if (Save->Version >= 4 || ++LegacyShieldCount > 1 || !Preset->GetNativeShield()
					|| Preset->GetNativeShield() != Shield)
				{
					return false;
				}
				LoadedLoadouts[SlotIndex].bShieldEquipped = true;
				continue;
			}
			// v1 could label the native shield by category without preserving its
			// UMSShieldWeaponData class. Migrate that state, but reject the same
			// ambiguity in v4 where bShieldEquipped is authoritative.
			if (Weapon && Weapon->CategoryTag == MSTags::Weapon_Category_Shield)
			{
				if (Save->Version >= 4 || ++LegacyShieldCount > 1 || !Preset->GetNativeShield()) return false;
				LoadedLoadouts[SlotIndex].bShieldEquipped = true;
				continue;
			}
			LoadedLoadouts[SlotIndex].EquippedWeapons[WeaponIndex] = Weapon;
		}
		if (Save->Version < 4)
		{
			// Legacy absence is meaningful: do not silently add a newly native shield.
			LoadedLoadouts[SlotIndex].bShieldEquipped = LegacyShieldCount > 0;
			if (!LoadedLoadouts[SlotIndex].bShieldEquipped && !Preset->bShieldRemovable) return false;
		}
	}

	// Apply only after every reference has resolved; never mutate shared catalog DataAssets.
	SlotPresets = MoveTemp(LoadedPresets);
	SlotLoadouts = MoveTemp(LoadedLoadouts);

	SelectedSlotIndex = SlotPresets.IsValidIndex(Save->SelectedSlotIndex)
		&& SlotPresets[Save->SelectedSlotIndex] ? Save->SelectedSlotIndex : 0;
	if (!SlotPresets[SelectedSlotIndex])
	{
		SelectedSlotIndex = SlotPresets.IndexOfByPredicate([](const TObjectPtr<UMSMobileSuitData>& Preset)
		{
			return Preset != nullptr;
		});
	}
	// A deliberately empty deck is valid, and keeps an in-range UI selection.
	if (SelectedSlotIndex == INDEX_NONE)
	{
		SelectedSlotIndex = FMath::Clamp(Save->SelectedSlotIndex, 0, SlotCount - 1);
	}
	return true;
}

int32 UMSDeckSubsystem::ResolveSerializedSaveVersion(const TArray<uint8>& SaveBytes, const int32 LoadedVersion)
{
	static_assert(UMSDeckSaveGame::CurrentVersion == 5, "Extend the unversioned-save inference for the new version");
	if (ContainsSerializedName(SaveBytes, "Version")) return LoadedVersion;
	if (ContainsSerializedName(SaveBytes, "BattleAssist")) return 5;
	if (ContainsSerializedName(SaveBytes, "bShieldEquipped")) return 4;
	if (ContainsSerializedName(SaveBytes, "EquippedModule")) return 3;
	// v1 and v2 had neither field. v1 rules are the lenient superset (up to three weapons).
	return 1;
}

bool UMSDeckSubsystem::ApplySaveBytes(const TArray<uint8>& SaveBytes)
{
	UMSDeckSaveGame* Save = Cast<UMSDeckSaveGame>(UGameplayStatics::LoadGameFromMemory(SaveBytes));
	if (!Save) return false;
	// Only the in-memory copy is corrected; the file is rewritten on the next normal commit.
	Save->Version = ResolveSerializedSaveVersion(SaveBytes, Save->Version);
	return ApplySaveGame(Save);
}

bool UMSDeckSubsystem::ReloadDeck()
{
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		TArray<uint8> SaveBytes;
		if (UGameplayStatics::LoadDataFromSlot(SaveBytes, SaveSlotName, SaveUserIndex) && ApplySaveBytes(SaveBytes))
		{
			bExistingSaveRejected = false;
			OnDeckChanged.Broadcast(INDEX_NONE);
			return true;
		}
		bExistingSaveRejected = true;
		UE_LOG(LogTemp, Warning, TEXT("Deck save could not be loaded; preserving the existing file and blocking writes: %s"), *SaveSlotName);
		OnDeckChanged.Broadcast(INDEX_NONE);
		return false;
	}

	bExistingSaveRejected = false;
	FillInitialDeck();
	SaveDeck();
	OnDeckChanged.Broadcast(INDEX_NONE);
	return false;
}

#if WITH_DEV_AUTOMATION_TESTS
UMSDeckSaveGame* UMSDeckSubsystem::CreateSaveGameForAutomationTests() const
{
	return CreateSaveGameObject();
}

bool UMSDeckSubsystem::RestoreSaveGameForAutomationTests(const UMSDeckSaveGame* Save)
{
	return ApplySaveGame(Save);
}

bool UMSDeckSubsystem::RestoreSaveBytesForAutomationTests(const TArray<uint8>& SaveBytes)
{
	return ApplySaveBytes(SaveBytes);
}
#endif

UMSMobileSuitData* UMSDeckSubsystem::GetPresetInSlot(const int32 SlotIndex) const
{
	return SlotPresets.IsValidIndex(SlotIndex) ? SlotPresets[SlotIndex].Get() : nullptr;
}

int32 UMSDeckSubsystem::GetCurrentDeckCost() const
{
	int32 Total = 0;
	for (const UMSMobileSuitData* Preset : SlotPresets)
	{
		if (Preset)
		{
			Total += Preset->Cost;
		}
	}
	return Total;
}

TArray<UMSMobileSuitData*> UMSDeckSubsystem::GetCatalogPresets() const
{
	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> Ids;
	Manager.GetPrimaryAssetIdList(UMSMobileSuitData::PrimaryAssetType, Ids);
	Ids.Sort([](const FPrimaryAssetId& A, const FPrimaryAssetId& B)
	{
		return A.ToString() < B.ToString();
	});

	TArray<UMSMobileSuitData*> Presets;
	Presets.Reserve(Ids.Num());
	for (const FPrimaryAssetId& Id : Ids)
	{
		UMSMobileSuitData* Preset = Cast<UMSMobileSuitData>(Manager.GetPrimaryAssetObject(Id));
		if (!Preset)
		{
			Preset = Cast<UMSMobileSuitData>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		}
		if (Preset)
		{
			Presets.Add(Preset);
		}
	}
	return Presets;
}

bool UMSDeckSubsystem::SupportsModuleEquipment() const
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	return !MSOnline::IsEnabled() && (!Online || !Online->IsOnlineMode());
}

bool UMSDeckSubsystem::CanEditCurrentDeck() const
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	return !Online || !Online->IsOnlineMode()
		|| (Online->HasOnlineSession() && bOnlineDeckReady && !Online->IsDeckRequestPending());
}

bool UMSDeckSubsystem::IsOnlineAccountActive() const
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	return Online && Online->IsOnlineMode() && Online->HasOnlineSession();
}

void UMSDeckSubsystem::HandleAccountChanged(FString ChangedAccountId)
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (!Online || !Online->IsOnlineMode()) return;
	if (ChangedAccountId.IsEmpty() || !Online->HasOnlineSession())
	{
		ActiveAccountKey.Reset();
		bOnlineDeckReady = false;
		SlotPresets.Init(nullptr, SlotCount);
		SlotLoadouts.Init(MakeEmptyLoadout(), SlotCount);
		SelectedSlotIndex = 0;
		OnDeckChanged.Broadcast(INDEX_NONE);
		return;
	}

	const FMSOnlineIdentity& Identity = Online->GetIdentity();
	const FString NewAccountKey = Identity.RealmId.ToLower() + TEXT("/") + Identity.AccountId.ToLower();
	if (ActiveAccountKey == NewAccountKey && bOnlineDeckReady) return;
	ActiveAccountKey = NewAccountKey;
	FMSOnlineDeckSnapshot Snapshot;
	if (Online->GetCachedDeck(Snapshot) && ApplyOnlineSnapshot(Snapshot, false))
	{
		OnDeckChanged.Broadcast(INDEX_NONE);
		return;
	}
	// Do not turn unresolved server assets into a writable default deck.
	bOnlineDeckReady = false;
	SlotPresets.Init(nullptr, SlotCount);
	SlotLoadouts.Init(MakeEmptyLoadout(), SlotCount);
	SelectedSlotIndex = 0;
	OnDeckChanged.Broadcast(INDEX_NONE);
}

void UMSDeckSubsystem::HandleOnlineDeckCommitted(const FMSOnlineDeckSnapshot Deck, const int64 Generation)
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	const UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (!Online || !Online->IsOnlineMode() || !Online->HasOnlineSession() || Generation != Online->GetAuthGeneration()) return;
	const FMSOnlineIdentity& Identity = Online->GetIdentity();
	const FString AccountKey = Identity.RealmId.ToLower() + TEXT("/") + Identity.AccountId.ToLower();
	if (ActiveAccountKey != AccountKey) ActiveAccountKey = AccountKey;
	if (ApplyOnlineSnapshot(Deck, false)) OnDeckChanged.Broadcast(INDEX_NONE);
}

bool UMSDeckSubsystem::ApplyOnlineSnapshot(const FMSOnlineDeckSnapshot& Snapshot, const bool bBroadcast)
{
	if ((Snapshot.SchemaVersion < 1 || Snapshot.SchemaVersion > 3) || Snapshot.Slots.Num() != SlotCount
		|| Snapshot.SelectedSlot < 0 || Snapshot.SelectedSlot >= SlotCount)
	{
		return false;
	}
	TArray<TObjectPtr<UMSMobileSuitData>> NewPresets;
	TArray<FMSDeckLoadout> NewLoadouts;
	NewPresets.SetNum(SlotCount);
	NewLoadouts.Init(MakeEmptyLoadout(), SlotCount);
	int32 TotalCost = 0;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const FMSOnlineDeckSlot& OnlineSlot = Snapshot.Slots[SlotIndex];
		if (OnlineSlot.WeaponIds.Num() != UMSMobileSuitData::MaxWeaponSlots) return false;
		if (OnlineSlot.SuitId.IsValid())
		{
			NewPresets[SlotIndex] = ResolveOnlineSuit(OnlineSlot.SuitId);
			if (!NewPresets[SlotIndex] || NewPresets[SlotIndex]->Cost < 0) return false;
			TotalCost += NewPresets[SlotIndex]->Cost;
		}
		else
		{
			if (OnlineSlot.WeaponIds.ContainsByPredicate([](const FPrimaryAssetId& Id) { return Id.IsValid(); })
				|| (Snapshot.SchemaVersion >= 2 && OnlineSlot.bShieldEquipped) || !OnlineSlot.BattleAssist.IsEmpty()) return false;
			continue;
		}
		FMSBattleAssistValidationResult Validation;
		FMSBattleAssistResolvedLoadout Resolved;
		if (Snapshot.SchemaVersion < 3 && !OnlineSlot.BattleAssist.IsEmpty()) return false;
		// This is an authenticated server response. The server owns development admission.
		if (!MSBattleAssist::ResolveLoadout(NewPresets[SlotIndex], OnlineSlot.BattleAssist, Resolved, Validation)) return false;
		NewLoadouts[SlotIndex].BattleAssist = OnlineSlot.BattleAssist;
		int32 LegacyShieldCount = 0;
		UMSShieldWeaponData* NativeShield = NewPresets[SlotIndex]->GetNativeShield();
		if (Snapshot.SchemaVersion >= 2)
		{
			NewLoadouts[SlotIndex].bShieldEquipped = OnlineSlot.bShieldEquipped;
			if (NewLoadouts[SlotIndex].bShieldEquipped && !NativeShield) return false;
			if (!NewLoadouts[SlotIndex].bShieldEquipped && !NewPresets[SlotIndex]->bShieldRemovable) return false;
		}
		TSet<FPrimaryAssetId> SeenWeapons;
		for (int32 WeaponIndex = 0; WeaponIndex < UMSMobileSuitData::MaxWeaponSlots; ++WeaponIndex)
		{
			const FPrimaryAssetId& WeaponId = OnlineSlot.WeaponIds[WeaponIndex];
			if (!WeaponId.IsValid()) continue;
			if (!NewPresets[SlotIndex]) return false;
			UMSWeaponDataBase* Weapon = ResolveOnlineWeapon(WeaponId);
			if (UMSShieldWeaponData* Shield = Cast<UMSShieldWeaponData>(Weapon))
			{
				if (Snapshot.SchemaVersion >= 2 || ++LegacyShieldCount > 1 || !NativeShield || NativeShield != Shield) return false;
				NewLoadouts[SlotIndex].bShieldEquipped = true;
				continue;
			}
			// The v1 online format had the same category-only shield records as
			// local files. Preserve their equip state only when the suit can supply
			// its native shield; v2 has a separate authoritative flag.
			if (Weapon && Weapon->CategoryTag == MSTags::Weapon_Category_Shield)
			{
				if (Snapshot.SchemaVersion >= 2 || ++LegacyShieldCount > 1 || !NativeShield) return false;
				NewLoadouts[SlotIndex].bShieldEquipped = true;
				continue;
			}
			if (SeenWeapons.Contains(WeaponId)) return false;
			SeenWeapons.Add(WeaponId);
			NewLoadouts[SlotIndex].EquippedWeapons[WeaponIndex] = Weapon;
			if (!NewLoadouts[SlotIndex].EquippedWeapons[WeaponIndex]) return false;
		}
		if (Snapshot.SchemaVersion == 1)
		{
			NewLoadouts[SlotIndex].bShieldEquipped = LegacyShieldCount > 0;
			if (!NewLoadouts[SlotIndex].bShieldEquipped && !NewPresets[SlotIndex]->bShieldRemovable) return false;
		}
	}
	if (TotalCost > DeckCostLimit) return false;
	SlotPresets = MoveTemp(NewPresets);
	SlotLoadouts = MoveTemp(NewLoadouts);
	SelectedSlotIndex = Snapshot.SelectedSlot;
	bExistingSaveRejected = false;
	bOnlineDeckReady = true;
	if (bBroadcast) OnDeckChanged.Broadcast(INDEX_NONE);
	return true;
}

FMSOnlineDeckSnapshot UMSDeckSubsystem::BuildOnlineSnapshot() const
{
	FMSOnlineDeckSnapshot Snapshot;
	if (const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance()))
	{
		if (const UMSOnlineAccountSubsystem* Online = Instance->GetSubsystem<UMSOnlineAccountSubsystem>())
		{
			Snapshot.Revision = Online->GetCommittedDeck().Revision;
		}
	}
	Snapshot.SelectedSlot = FMath::Clamp(SelectedSlotIndex, 0, SlotCount - 1);
	Snapshot.Slots.SetNum(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		FMSOnlineDeckSlot& OnlineSlot = Snapshot.Slots[SlotIndex];
		if (SlotPresets.IsValidIndex(SlotIndex) && SlotPresets[SlotIndex]) OnlineSlot.SuitId = SlotPresets[SlotIndex]->GetPrimaryAssetId();
		OnlineSlot.WeaponIds.SetNum(UMSMobileSuitData::MaxWeaponSlots);
		if (!SlotLoadouts.IsValidIndex(SlotIndex)) continue;
		if (!OnlineSlot.SuitId.IsValid())
		{
			OnlineSlot.bShieldEquipped = false;
			continue;
		}
		OnlineSlot.BattleAssist = SlotLoadouts[SlotIndex].BattleAssist;
		const UMSMobileSuitData* Preset = SlotPresets[SlotIndex];
		OnlineSlot.bShieldEquipped = Preset && Preset->GetNativeShield()
			&& SlotLoadouts[SlotIndex].bShieldEquipped;
		for (int32 WeaponIndex = 0; WeaponIndex < UMSMobileSuitData::MaxWeaponSlots; ++WeaponIndex)
		{
			if (SlotLoadouts[SlotIndex].EquippedWeapons.IsValidIndex(WeaponIndex))
			{
				if (UMSWeaponDataBase* Weapon = SlotLoadouts[SlotIndex].EquippedWeapons[WeaponIndex].LoadSynchronous())
				{
					if (IsNativeShieldData(Weapon)) continue;
					OnlineSlot.WeaponIds[WeaponIndex] = Weapon->GetPrimaryAssetId();
				}
			}
		}
	}
	return Snapshot;
}

bool UMSDeckSubsystem::BeginOnlineCommit(const FMSOnlineDeckSnapshot& Candidate,
	TFunction<void(bool bCommitted, const FMSOnlineDeckSnapshot& Snapshot)> Completion)
{
	const UMSGameInstance* Instance = Cast<UMSGameInstance>(GetGameInstance());
	UMSOnlineAccountSubsystem* Online = Instance ? Instance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (!Online || Online->IsDeckRequestPending()) return false;
	TWeakObjectPtr<UMSDeckSubsystem> WeakThis(this);
	return Online->PutDeck(Candidate,
		[WeakThis, Completion = MoveTemp(Completion)](const EMSOnlineDeckSyncState State, const FString&, const FMSOnlineDeckSnapshot& Snapshot)
		{
			if (!WeakThis.IsValid()) return;
			if (State != EMSOnlineDeckSyncState::Pending && Completion)
				Completion(State == EMSOnlineDeckSyncState::Committed, Snapshot);
			WeakThis->OnDeckSyncStateChanged.Broadcast(State);
		});
}

UMSMobileSuitData* UMSDeckSubsystem::ResolveOnlineSuit(const FPrimaryAssetId& Id) const
{
	if (!Id.IsValid()) return nullptr;
	UAssetManager& Manager = UAssetManager::Get();
	UMSMobileSuitData* Suit = Cast<UMSMobileSuitData>(Manager.GetPrimaryAssetObject(Id));
	if (!Suit) Suit = Cast<UMSMobileSuitData>(Manager.GetPrimaryAssetPath(Id).TryLoad());
	return Suit && Suit->GetPrimaryAssetId() == Id ? Suit : nullptr;
}

UMSWeaponDataBase* UMSDeckSubsystem::ResolveOnlineWeapon(const FPrimaryAssetId& Id) const
{
	if (!Id.IsValid()) return nullptr;
	UAssetManager& Manager = UAssetManager::Get();
	UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(Manager.GetPrimaryAssetObject(Id));
	if (!Weapon) Weapon = Cast<UMSWeaponDataBase>(Manager.GetPrimaryAssetPath(Id).TryLoad());
	return Weapon && Weapon->GetPrimaryAssetId() == Id ? Weapon : nullptr;
}

void UMSDeckSubsystem::FillInitialDeck()
{
	SlotPresets.Reset();
	SlotPresets.SetNumZeroed(SlotCount);
	SlotLoadouts.Reset();
	SlotLoadouts.Init(MakeEmptyLoadout(), SlotCount);
	SelectedSlotIndex = 0;
	const TArray<UMSMobileSuitData*> Catalog = GetCatalogPresets();
	for (int32 SlotIndex = 0; SlotIndex < SlotCount && SlotIndex < Catalog.Num(); ++SlotIndex)
	{
		SlotPresets[SlotIndex] = Catalog[SlotIndex];
		SlotLoadouts[SlotIndex] = MakeDefaultLoadout(Catalog[SlotIndex]);
	}
}


bool UMSDeckSubsystem::RequestBattleAssistLoadout(int32 SlotIndex, const FMSBattleAssistLoadout& Candidate, TFunction<void(bool)> Completion)
{
	const auto Fail = [&Completion]() { if (Completion) Completion(false); return false; };
	if (!CanEditCurrentDeck() || !SlotLoadouts.IsValidIndex(SlotIndex)) return Fail();
	UMSMobileSuitData* Suit = GetPresetInSlot(SlotIndex);
	const FMSBattleAssistProgress Progress = GetBattleAssistProgress();
	FMSBattleAssistValidationResult Error;
	if (!Suit || !MSBattleAssist::ValidateLoadout(Suit, Candidate, &Progress, Error)) return Fail();
	FMSDeckLoadout Loadout = GetLoadoutInSlot(SlotIndex);
	Loadout.BattleAssist = Candidate;
	if (!IsOnlineAccountActive())
	{
		const bool bSuccess = CommitLoadout(SlotIndex, Loadout);
		if (Completion) Completion(bSuccess);
		return bSuccess;
	}
	FMSOnlineDeckSnapshot Snapshot = BuildOnlineSnapshot();
	Snapshot.Slots[SlotIndex].BattleAssist = Candidate;
	// A shared completion guard also covers transports which fail synchronously.
	const auto Done = MakeShared<bool>(false);
	const auto Callback = MakeShared<TFunction<void(bool)>>(MoveTemp(Completion));
	const auto Finish = [Done, Callback](bool bSuccess) { if (!*Done) { *Done = true; if (*Callback) (*Callback)(bSuccess); } };
	const bool bAccepted = BeginOnlineCommit(Snapshot, [this, Finish](bool bSuccess, const FMSOnlineDeckSnapshot& Committed)
	{
		if (bSuccess) bSuccess = ApplyOnlineSnapshot(Committed, true);
		Finish(bSuccess);
	});
	if (!bAccepted) Finish(false);
	return bAccepted;
}

FMSBattleAssistProgress UMSDeckSubsystem::GetBattleAssistProgress() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestBattleAssistProgress.IsSet()) return TestBattleAssistProgress.GetValue();
#endif
	const auto* Subsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetProgress() : FMSBattleAssistProgress();
}
