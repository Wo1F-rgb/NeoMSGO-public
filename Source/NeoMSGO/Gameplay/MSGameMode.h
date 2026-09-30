// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GenericTeamAgentInterface.h"
#include "Gameplay/MSCapturePoint.h"
#include "MSGameMode.generated.h"

class AMSHomeBase;
class AMSUnitBase;
class UMSBotSpawnManagerComponent;
class UMSUnitRegistrySubsystem;
class UMSGameInstance;
struct FMSMapDefinitionRow;
struct FMSBattleRuleRow;

/** Server-authoritative battle gauge rules and victory conditions. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMSGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Real-terrain maps (Daibao) ship no APlayerStart, so the engine default falls back to the
	 * world origin, the spawn collides with terrain, and no player pawn exists at all — which
	 * silently kills the whole briefing/spawn-select UI because it lives on AMSPlayerCharacter.
	 * Fall back to a home base of the player's own faction instead.
	 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	UFUNCTION(BlueprintCallable, Category="MS|Battle")
	void ApplyRules(float KillDamageValue, float HomeBaseDamageValue, float MaxGaugeValue);

	/** Apply a named rule with its COST policy, including lobby pending-sortie reapplication. */
	void ApplyBattleRule(const FMSBattleRuleRow& Rule);

	/** DamagedTeam is the faction whose gauge receives damage, not the damage source's faction. */
	UFUNCTION(BlueprintCallable, Category="MS|Battle")
	void ApplyGaugeDamage(FGenericTeamId DamagedTeam, float Amount);

	UFUNCTION(BlueprintCallable, Category="MS|Battle")
	void NotifyMatchTimeExpired();
	/** Begin the home-server match only after a verified first deployment. */
	void NotifyOnlineDeployment();
	bool HasOnlineBattleStarted() const { return bOnlineBattleStarted; }
	/** Begin a hosted room once the first human sortie has actually succeeded. */
	void NotifyRoomDeployment();
	UFUNCTION(BlueprintPure, Category="MS|Room")
	bool IsWaitingForRoomDeployment() const;

	UFUNCTION(BlueprintCallable, Category="MS|Battle")
	void EndBattle(bool bInPlayerWon);

	UFUNCTION(BlueprintPure, Category="MS|Battle")
	float GetKillDamage() const { return KillDamage; }

	float GetDeathGaugeDamage(int32 UnitCost) const;

	UFUNCTION(BlueprintPure, Category="MS|Battle")
	float GetHomeBaseDestroyDamage() const { return HomeBaseDestroyDamage; }

	UFUNCTION(BlueprintPure, Category="MS|Battle")
	float GetConfiguredMaxGauge() const { return MaxGauge; }

	/** MapId option, GameInstance selection, then current level name. */
	static const FMSMapDefinitionRow* ResolveMapDefinition(const FString& MapName,
		const FString& Options, const UMSGameInstance* GameInstance);

	/** B2: BOT初期配備マネージャ(StartPlayで起動。docs/BotDeployment_Design.md) */
	UFUNCTION(BlueprintPure, Category="MS|Battle")
	UMSBotSpawnManagerComponent* GetBotSpawnManager() const { return BotSpawnManager; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Battle")
	TObjectPtr<UMSBotSpawnManagerComponent> BotSpawnManager;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle", meta=(ClampMin="0.0"))
	float KillDamage = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle", meta=(ClampMin="0.0"))
	float HomeBaseDestroyDamage = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle", meta=(ClampMin="0.0"))
	float MaxGauge = 10000.0f;

private:
	void NormalizeDaibaoHomeBaseFactions();
	void SubscribeToUnit(AMSUnitBase* Unit);
	bool AreAllHomeBasesDestroyed(FGenericTeamId Team) const;

	UFUNCTION()
	void HandleUnitDeath(AActor* DeadUnit, AActor* Killer);

	UFUNCTION()
	void HandleHomeBaseDestroyed(AMSHomeBase* HomeBase, EMSCaptureOwner HomeBaseFaction);

	UFUNCTION()
	void HandleHomeBaseDamaged(AMSHomeBase* HomeBase, EMSCaptureOwner HomeBaseFaction);

	/**
	 * Drains the gauge up to HomeBaseDestroyDamage * LostRatio for one base and returns the
	 * amount still owed. Each base can therefore never cost its team more than the rule value,
	 * no matter how the damage is split between hits and the final destruction event.
	 */
	void SyncHomeBaseGaugeDamage(AMSHomeBase* HomeBase, EMSCaptureOwner HomeBaseFaction, float LostRatio);

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMSHomeBase>> HomeBases;

	/** Gauge already charged for each home base (monotonic within one ApplyRules cycle). */
	TMap<TWeakObjectPtr<AMSHomeBase>, float> HomeBaseGaugeApplied;

	TWeakObjectPtr<UMSUnitRegistrySubsystem> UnitRegistry;
	FText ResolvedMapDisplayName;
	FName ResolvedRoomMapId;
	bool bHasResolvedMapDisplayName = false;
	bool bRoomHost = false;
	bool bRoomBattleStarted = false;
	bool bOnlineBattleStarted = false;
	bool bScaleKillDamageByCost = false;
	int32 RoomRuleIndex = 0;
	int32 RoomBotCount = 0;
	bool bRoomFillTeamsWithBots = true;
	uint8 RoomBotDifficulty = 1;
};
