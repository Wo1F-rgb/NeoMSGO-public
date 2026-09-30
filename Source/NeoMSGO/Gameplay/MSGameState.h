// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Online/MSRoomSubsystem.h"
#include "MSGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnGaugeChanged, float, PlayerGauge, float, EnemyGauge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnBattleEnded, bool, bPlayerWon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnRemainingTimeChanged, float, RemainingTime);

/** Replicated battle presentation state. Match rules belong exclusively to AMSGameMode. */
UCLASS(BlueprintType)
class NEOMSGO_API AMSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AMSGameState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Player-facing battlefield name shared by briefing, lobby, and result UI. */
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle|Map")
	FText MapDisplayName;
	/** Frozen host choices, replicated so guests never read their own lobby defaults. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MS|Room") bool bPlayerHostedRoom = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MS|Room") FMSRoomSettings HostedRoomSettings;

	/** True for space battlefields that use unrestricted three-dimensional movement. */
	UPROPERTY(ReplicatedUsing=OnRep_ZeroGravityMap, EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle|Map")
	bool bZeroGravityMap = false;

	UFUNCTION(BlueprintPure, Category="MS|Battle|Map")
	bool IsZeroGravityMap() const { return bZeroGravityMap; }

	/** Authority-side runtime setter, primarily for map/bootstrap code and deterministic tests. */
	UFUNCTION(BlueprintCallable, Category="MS|Battle|Map")
	void SetZeroGravityMap(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category="MS|Battle|Timer")
	void SetDuration(float Seconds);

	UFUNCTION(BlueprintPure, Category="MS|Battle|Timer")
	FText GetFormattedRemainingTime() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MS|Battle|Timer", meta=(ClampMin="0.0"))
	float MatchDuration = 1200.0f;

	UPROPERTY(ReplicatedUsing=OnRep_RemainingTime, BlueprintReadOnly, Category="MS|Battle|Timer")
	float RemainingTime = 1200.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Gauges, BlueprintReadOnly, Category="MS|Battle")
	float PlayerGauge = 10000.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Gauges, BlueprintReadOnly, Category="MS|Battle")
	float EnemyGauge = 10000.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Gauges, BlueprintReadOnly, Category="MS|Battle")
	float MaxGauge = 10000.0f;

	UPROPERTY(ReplicatedUsing=OnRep_BattleEnded, BlueprintReadOnly, Category="MS|Battle")
	bool bBattleEnded = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="MS|Battle")
	bool bPlayerWon = false;

	UPROPERTY(BlueprintAssignable, Category="MS|Battle")
	FMSOnGaugeChanged OnGaugeChanged;

	UPROPERTY(BlueprintAssignable, Category="MS|Battle")
	FMSOnBattleEnded OnBattleEnded;

	UPROPERTY(BlueprintAssignable, Category="MS|Battle|Timer")
	FMSOnRemainingTimeChanged OnRemainingTimeChanged;

private:
	void StartMatchTimer();
	void HandleMatchTimerTick();
	void StopMatchTimer();

	UFUNCTION()
	void HandleBattleEndedForTimer(bool bInPlayerWon);

	UFUNCTION()
	void OnRep_Gauges();

	UFUNCTION()
	void OnRep_BattleEnded();

	UFUNCTION()
	void OnRep_RemainingTime();

	UFUNCTION()
	void OnRep_ZeroGravityMap();

	void ApplyZeroGravityMapToUnits();

	FTimerHandle MatchTimerHandle;
	bool bMatchTimeExpiredNotified = false;
};
