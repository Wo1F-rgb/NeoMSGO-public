#pragma once

#include "CoreMinimal.h"
#include "Data/MSBattleAssistTypes.h"
#include "GameFramework/SaveGame.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSDeckSubsystem.generated.h"

class UMSMobileSuitData;
class UMSWeaponDataBase;
class UMSModuleData;
class UMSGameInstance;
class AMSPlayerController;

/** Mutable equipment for one deck slot. Catalog suit and weapon assets stay unchanged. */
USTRUCT(BlueprintType)
struct NEOMSGO_API FMSDeckLoadout
{
	GENERATED_BODY()

	FMSDeckLoadout();

	/** Always three entries; a null entry preserves that weapon slot number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Deck")
	TArray<TSoftObjectPtr<UMSWeaponDataBase>> EquippedWeapons;

	/** The suit's native shield is a separate machine equipment state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Deck")
	bool bShieldEquipped = false;

	/** Optional offline module; null is an explicit unequip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Deck")
	TSoftObjectPtr<UMSModuleData> EquippedModule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|Deck")
	FMSBattleAssistLoadout BattleAssist;
};

USTRUCT()
struct NEOMSGO_API FMSDeckSlotSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FSoftObjectPath MobileSuit;

	UPROPERTY(SaveGame)
	TArray<FSoftObjectPath> EquippedWeapons;

	UPROPERTY(SaveGame)
	FSoftObjectPath EquippedModule;

	UPROPERTY(SaveGame)
	FMSBattleAssistLoadout BattleAssist;

	UPROPERTY(SaveGame)
	bool bShieldEquipped = false;
};

UCLASS()
class NEOMSGO_API UMSDeckSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 5;

	UPROPERTY(SaveGame)
	int32 Version = CurrentVersion;

	UPROPERTY(SaveGame)
	TArray<FMSDeckSlotSaveData> Slots;

	UPROPERTY(SaveGame)
	int32 SelectedSlotIndex = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSDeckChangedDelegate, int32, ChangedSlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSDeckSyncStateChangedDelegate, EMSOnlineDeckSyncState, State);

/**
 * Game-instance lifetime sortie deck shared by all deck-related UI screens.
 * GetPresetInSlot retains catalog identity. Mutable equipment belongs to each slot,
 * and is passed separately to UI/combat consumers, never as a transient suit clone.
 */
UCLASS()
class NEOMSGO_API UMSDeckSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SlotCount = 4;
	static constexpr int32 DeckCostLimit = 1720;

	UMSDeckSubsystem();
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	int32 GetSlotCount() const { return SlotCount; }

	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	int32 GetSelectedSlotIndex() const { return SelectedSlotIndex; }

	UFUNCTION(BlueprintCallable, Category = "MS|Deck")
	void SetSelectedSlotIndex(int32 SlotIndex);

	/**
	 * Requests a selected-slot change and reports the result after an online PUT
	 * has been acknowledged. The committed selection is never changed before
	 * that acknowledgement. Offline callers receive the result synchronously.
	 */
	bool RequestSelectedSlotIndex(int32 SlotIndex, TFunction<void(bool bSucceeded)> Completion);

	UFUNCTION(BlueprintCallable, Category = "MS|Deck")
	bool AssignPreset(int32 SlotIndex, UMSMobileSuitData* Preset);

	UFUNCTION(BlueprintCallable, Category = "MS|Deck")
	bool RemovePreset(int32 SlotIndex);

	/** Returns a candidate copy; editing it does not alter the committed slot. */
	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	FMSDeckLoadout GetLoadoutInSlot(int32 SlotIndex) const;

	/** Match UI uses the server's admitted snapshot; hangar edits remain account data. */
	UMSMobileSuitData* GetDeploymentPresetInSlot(int32 SlotIndex) const;
	FMSDeckLoadout GetDeploymentLoadoutInSlot(int32 SlotIndex) const;
	int32 GetDeploymentSelectedSlotIndex() const;
	bool SetDeploymentSelectedSlotIndex(int32 SlotIndex);

	/** Saves a validated candidate before notifying consumers; failure restores the old loadout. */
	UFUNCTION(BlueprintCallable, Category = "MS|Deck")
	bool CommitLoadout(int32 SlotIndex, const FMSDeckLoadout& Candidate);

	/** Completion fires once with the committed result; true means request accepted. */
	bool RequestBattleAssistLoadout(int32 SlotIndex, const FMSBattleAssistLoadout& Candidate, TFunction<void(bool)> Completion);
	bool SupportsModuleEquipment() const;
	bool SaveDeck() const;
	bool ReloadDeck();

	/**
	 * Version of a serialized deck save. UE omits top-level properties equal to the class
	 * default, and every writer so far saved Version == CurrentVersion (then 1..4), so older
	 * files load with today's default. When the Version tag is absent this infers the writer
	 * era from the struct fields that are always serialized inside the Slots array:
	 * bShieldEquipped (v4), EquippedModule (v3), neither (v1/v2, read with v1 rules).
	 * Extend this whenever CurrentVersion changes.
	 */
	static int32 ResolveSerializedSaveVersion(const TArray<uint8>& SaveBytes, int32 LoadedVersion);

	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	UMSMobileSuitData* GetPresetInSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	int32 GetCurrentDeckCost() const;

	UFUNCTION(BlueprintPure, Category = "MS|Deck")
	bool IsOverCostLimit() const { return GetCurrentDeckCost() > DeckCostLimit; }

	/** Enumerates the mobile-suit primary assets in the deterministic catalog order. */
	UFUNCTION(BlueprintCallable, Category = "MS|Deck")
	TArray<UMSMobileSuitData*> GetCatalogPresets() const;

	UPROPERTY(BlueprintAssignable, Category = "MS|Deck")
	FMSDeckChangedDelegate OnDeckChanged;

	/** Online commits expose Pending until the server has returned Committed or Failed. */
	UPROPERTY(BlueprintAssignable, Category = "MS|Deck")
	FMSDeckSyncStateChangedDelegate OnDeckSyncStateChanged;

#if WITH_DEV_AUTOMATION_TESTS
	void SetBattleAssistProgressForAutomationTests(const FMSBattleAssistProgress& InProgress) { TestBattleAssistProgress = InProgress; }
	UMSDeckSaveGame* CreateSaveGameForAutomationTests() const;
	bool RestoreSaveGameForAutomationTests(const UMSDeckSaveGame* Save);
	/** Same file boundary as ReloadDeck (version inference + migration) without touching a slot. */
	bool RestoreSaveBytesForAutomationTests(const TArray<uint8>& SaveBytes);
	void SetSaveSlotForAutomationTests(const FString& InSaveSlotName) { SaveSlotName = InSaveSlotName; }
	void EnablePersistenceForAutomationTests() { bPersistenceReady = true; }
#endif

private:
	FMSBattleAssistProgress GetBattleAssistProgress() const;
#if WITH_DEV_AUTOMATION_TESTS
	TOptional<FMSBattleAssistProgress> TestBattleAssistProgress;
#endif
	const AMSPlayerController* GetDeploymentController() const;
	TWeakObjectPtr<const AMSPlayerController> DeploymentSelectionOwner;
	int32 DeploymentSelectedSlot = INDEX_NONE;
	bool IsOnlineAccountActive() const;
	bool CanEditCurrentDeck() const;
	UFUNCTION()
	void HandleAccountChanged(FString ChangedAccountId);
	UFUNCTION()
	void HandleOnlineDeckCommitted(FMSOnlineDeckSnapshot Deck, int64 Generation);
	bool ApplyOnlineSnapshot(const FMSOnlineDeckSnapshot& Snapshot, bool bBroadcast);
	FMSOnlineDeckSnapshot BuildOnlineSnapshot() const;
	bool BeginOnlineCommit(const FMSOnlineDeckSnapshot& Candidate,
		TFunction<void(bool bCommitted, const FMSOnlineDeckSnapshot& Snapshot)> Completion);
	UMSMobileSuitData* ResolveOnlineSuit(const FPrimaryAssetId& Id) const;
	UMSWeaponDataBase* ResolveOnlineWeapon(const FPrimaryAssetId& Id) const;
	void FillInitialDeck();
	UMSDeckSaveGame* CreateSaveGameObject() const;
	bool ApplySaveGame(const UMSDeckSaveGame* Save);
	bool ApplySaveBytes(const TArray<uint8>& SaveBytes);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMSMobileSuitData>> SlotPresets;

	UPROPERTY(Transient)
	TArray<FMSDeckLoadout> SlotLoadouts;

	UPROPERTY(Transient)
	int32 SelectedSlotIndex = 0;

	// Keep the existing filename: Versions 1-3 migrate into slot state; the next commit writes Version 4.
	FString SaveSlotName = TEXT("NeoMSGO_Deck_v1");
	static constexpr int32 SaveUserIndex = 0;
	bool bPersistenceReady = false;
	// A rejected existing save must not be silently replaced by a fallback deck or later UI edits.
	bool bExistingSaveRejected = false;
	bool bOnlineDeckReady = false;
	FString ActiveAccountKey;
	TWeakObjectPtr<UMSGameInstance> BoundGameInstance;
	TWeakObjectPtr<UMSOnlineAccountSubsystem> BoundOnlineAccount;
};
