#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Data/MSBattleAssistTypes.h"
#include "Online/MSOnlineTypes.h"
#include "MSBattleAssistProgressSubsystem.generated.h"

UCLASS()
class UMSBattleAssistProgressSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 Version = 1;
	UPROPERTY(SaveGame) FMSBattleAssistProgress Progress;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSBattleAssistProgressChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSBattleAssistOperationCompleted, bool, bSuccess, FText, Message);

/** Account-wide development ledger. Equip operations never consume this balance. */
UCLASS()
class NEOMSGO_API UMSBattleAssistProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure) FMSBattleAssistProgress GetProgress() const { return Progress; }
	UFUNCTION(BlueprintPure) bool IsPending() const { return bPending; }
	UFUNCTION(BlueprintPure) FText GetLastError() const { return LastError; }
	UFUNCTION(BlueprintCallable) bool ReloadProgress();
	UFUNCTION(BlueprintCallable) bool TryDevelopModule(FPrimaryAssetId ModuleId);
	/** Explicit compare-and-swap request. Offline completion is synchronous; online completion
	 * follows acknowledgement. Completion fires once, including rejection/account cancellation.
	 * The Blueprint convenience method uses the currently displayed revision and the delegates. */
	bool TryDevelopModule(FPrimaryAssetId ModuleId, int64 ExpectedRevision, TFunction<void(bool, FText)> Completion);
	UPROPERTY(BlueprintAssignable) FMSBattleAssistProgressChanged OnProgressChanged;
	UPROPERTY(BlueprintAssignable) FMSBattleAssistOperationCompleted OnOperationCompleted;
#if WITH_DEV_AUTOMATION_TESTS
	void SetSaveSlotForTests(const FString& Slot) { SaveSlotName = Slot; }
	void SetProgressForTests(const FMSBattleAssistProgress& Value) { Progress = Value; bRejectedSave = false; }
	static bool ParseProgressForTests(const FString& Body, FMSBattleAssistProgress& Out) { return ParseProgress(Body, Out); }
#endif
private:
	UFUNCTION() void HandleAccountChanged(FMSOnlineIdentity Identity, int64 Generation);
	bool SendProgressRequest(const FString& Verb, const FString& Path, const FString& Body);
	bool SaveProgress(const FMSBattleAssistProgress& Candidate) const;
	bool Finish(bool bSuccess, const FText& Message);
	static bool ParseProgress(const FString& Body, FMSBattleAssistProgress& Out);
	UPROPERTY(Transient) FMSBattleAssistProgress Progress;
	FText LastError;
	bool bPending = false;
	bool bRejectedSave = false;
	bool bShuttingDown = false;
	TFunction<void(bool, FText)> DevelopmentCompletion;
	uint64 OperationGeneration = 0;
	// Deliberately distinct from online caches and the ownerless historical deck slot.
	FString SaveSlotName = TEXT("NeoMSGO_BattleAssist_OfflinePrototype_v1");
};
