// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

/** Move-boundary working state. Only authority writes GAS; client copies are prediction only. */
struct NEOMSGO_API FMSBoostMovementState
{
	float Boost = 0.0f;
	float MaxBoost = 100.0f;
	float ConsumptionPerSecond = 0.0f;
	/** Existing charge/module rate retained for OH only. */
	float RechargePerSecond = 0.0f;
	float NormalRechargePerSecond = 0.0f;
	float RecoveryDelaySeconds = 0.25f;
	float RecoveryDelayRemaining = 0.25f;
	float TakeoffBaseZ = 0.0f;
	uint32 Generation = 0;
	bool bInitialized = false;
	bool bOverheated = false;
	bool bDead = false;
	/** D-melee owns a temporary regeneration gate, including ordinary/OH recovery. */
	bool bRegenBlocked = false;
	bool bHasTakeoffBase = false;
	bool bGrounded = false;

	bool CanUseEnergy() const { return bInitialized && !bOverheated && !bDead && Boost > 0.0f; }
	/** Both endpoints must be grounded: landing cannot recharge time spent airborne. */
	void Advance(float DeltaSeconds, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd);
	bool Matches(const FMSBoostMovementState& Other) const;
	bool HasSameSimulationGate(const FMSBoostMovementState& Other) const;
	bool Serialize(FArchive& Ar);
};

class NEOMSGO_API FMSCharacterSavedMove final : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* Character,
		APlayerController* PC, const FVector& OldStartLocation) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
	virtual uint8 GetCompressedFlags() const override;

	FMSBoostMovementState StartBoostState;
	FMSBoostMovementState EndBoostState;
	uint32 ClientSimulationEpoch = 0;
	bool bRequestBoostCorrection = false;
	bool bStartCanUseBoost = false;
	bool bEndCanUseBoost = false;
	bool bHasRecordedEnd = false;
	uint8 SavedInputFlags = 0;
};

struct NEOMSGO_API FMSCharacterNetworkMoveData : FCharacterNetworkMoveData
{
	FMSBoostMovementState ReportedEndState;
	uint32 ClientSimulationEpoch = 0;
	bool bRequestBoostCorrection = false;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType MoveType) override;
};

struct NEOMSGO_API FMSCharacterNetworkMoveDataContainer : FCharacterNetworkMoveDataContainer
{
	FMSCharacterNetworkMoveDataContainer();
	FMSCharacterNetworkMoveData Moves[3];
};

struct NEOMSGO_API FMSCharacterMoveResponseDataContainer : FCharacterMoveResponseDataContainer
{
	FMSBoostMovementState AuthoritativeState;
	uint32 ClientSimulationEpoch = 0;
	uint32 ResponseSequence = 0;
	bool bHasBoostState = false;
	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
};
