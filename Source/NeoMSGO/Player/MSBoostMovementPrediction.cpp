// Copyright NeoMSGO Project. All Rights Reserved.
#include "Player/MSBoostMovementPrediction.h"

#include "Player/MSCharacterMovementComponent.h"
#include "GameFramework/Character.h"

void FMSBoostMovementState::Advance(float DeltaSeconds, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd)
{
	if (!bInitialized || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
	bGrounded = bGroundedAtEnd;
	if (bDead)
	{
		RecoveryDelayRemaining = RecoveryDelaySeconds;
		return;
	}
	if (bRegenBlocked)
	{
		// D-melee's instant cost is separate from continuous dash consumption.
		// Its attack/lunge/recovery may outlast the normal 0.25s wait; restart
		// that existing wait for as long as the ability's tag is active.
		RecoveryDelayRemaining = RecoveryDelaySeconds;
		return;
	}
	if (bConsuming && ConsumptionPerSecond > 0.0f)
	{
		RecoveryDelayRemaining = RecoveryDelaySeconds;
		Boost = FMath::Clamp(Boost - ConsumptionPerSecond * DeltaSeconds, 0.0f, MaxBoost);
		if (Boost <= 0.0f) bOverheated = true;
		return;
	}
	// OH retains its previous rate, airborne recovery and 80% release contract.
	// Normal recovery is grounded only, with a provisional authored delay.
	float RecoveryTime = DeltaSeconds;
	if (!bOverheated)
	{
		if (!bGroundedAtStart || !bGroundedAtEnd)
		{
			RecoveryDelayRemaining = RecoveryDelaySeconds;
			return;
		}
		const float WaitingTime = FMath::Min(RecoveryTime, RecoveryDelayRemaining);
		RecoveryDelayRemaining = FMath::Max(0.0f, RecoveryDelayRemaining - WaitingTime);
		RecoveryTime -= WaitingTime;
	}
	const float Rate = bOverheated ? RechargePerSecond : NormalRechargePerSecond;
	if (Rate > 0.0f && Boost < MaxBoost)
	{
		Boost = FMath::Clamp(Boost + Rate * RecoveryTime, 0.0f, MaxBoost);
		if (bOverheated && Boost >= MaxBoost * 0.8f) bOverheated = false;
	}
}

bool FMSBoostMovementState::HasSameSimulationGate(const FMSBoostMovementState& Other) const
{
	return bInitialized == Other.bInitialized && Generation == Other.Generation &&
		bOverheated == Other.bOverheated && bDead == Other.bDead &&
		bRegenBlocked == Other.bRegenBlocked && CanUseEnergy() == Other.CanUseEnergy() &&
		bHasTakeoffBase == Other.bHasTakeoffBase &&
		(!bHasTakeoffBase || FMath::IsNearlyEqual(TakeoffBaseZ, Other.TakeoffBaseZ, 0.01f)) &&
		MaxBoost == Other.MaxBoost && ConsumptionPerSecond == Other.ConsumptionPerSecond &&
		RechargePerSecond == Other.RechargePerSecond && NormalRechargePerSecond == Other.NormalRechargePerSecond &&
		RecoveryDelaySeconds == Other.RecoveryDelaySeconds && bGrounded == Other.bGrounded &&
		FMath::IsNearlyEqual(RecoveryDelayRemaining, Other.RecoveryDelayRemaining, 0.0001f);
}

bool FMSBoostMovementState::Matches(const FMSBoostMovementState& Other) const
{
	// Energy tolerance is numerical only. It does not change CMC position thresholds.
	return HasSameSimulationGate(Other) && FMath::IsNearlyEqual(Boost, Other.Boost, 0.001f);
}

bool FMSBoostMovementState::Serialize(FArchive& Ar)
{
	Ar.SerializeBits(&bInitialized, 1);
	if (!bInitialized)
	{
		if (Ar.IsLoading()) *this = FMSBoostMovementState();
		return !Ar.IsError();
	}
	Ar << Boost << MaxBoost << ConsumptionPerSecond << RechargePerSecond << Generation;
	Ar << NormalRechargePerSecond << RecoveryDelaySeconds << RecoveryDelayRemaining;
	Ar.SerializeBits(&bGrounded, 1);
	Ar.SerializeBits(&bOverheated, 1);
	Ar.SerializeBits(&bDead, 1);
	Ar.SerializeBits(&bRegenBlocked, 1);
	Ar.SerializeBits(&bHasTakeoffBase, 1);
	if (bHasTakeoffBase) Ar << TakeoffBaseZ;
	else if (Ar.IsLoading()) TakeoffBaseZ = 0.0f;
	if (Ar.IsLoading() && (!FMath::IsFinite(Boost) || !FMath::IsFinite(MaxBoost) ||
		!FMath::IsFinite(ConsumptionPerSecond) || !FMath::IsFinite(RechargePerSecond) ||
		!FMath::IsFinite(TakeoffBaseZ) || MaxBoost < 1.0f || Boost < 0.0f || Boost > MaxBoost ||
		ConsumptionPerSecond < 0.0f || RechargePerSecond < 0.0f ||
		!FMath::IsFinite(NormalRechargePerSecond) || NormalRechargePerSecond < 0.0f ||
		!FMath::IsFinite(RecoveryDelaySeconds) || RecoveryDelaySeconds < 0.0f ||
		!FMath::IsFinite(RecoveryDelayRemaining) || RecoveryDelayRemaining < 0.0f ||
		RecoveryDelayRemaining > RecoveryDelaySeconds)) Ar.SetError();
	return !Ar.IsError();
}

void FMSCharacterSavedMove::Clear()
{
	Super::Clear();
	StartBoostState = FMSBoostMovementState();
	EndBoostState = FMSBoostMovementState();
	ClientSimulationEpoch = 0;
	bRequestBoostCorrection = bStartCanUseBoost = bEndCanUseBoost = bHasRecordedEnd = false;
	SavedInputFlags = 0;
}

void FMSCharacterSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	if (UMSCharacterMovementComponent* Movement = Character ? Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr)
	{
		StartBoostState = Movement->CaptureBoostMovementState();
		ClientSimulationEpoch = Movement->ClientSimulationEpoch;
		bRequestBoostCorrection = Movement->bRequestBoostCorrection;
		bStartCanUseBoost = Movement->CanUseBoost();
		SavedInputFlags = (Movement->bBoostInputHeld ? FLAG_Custom_0 : 0) |
			(Movement->bHoverInputHeld ? FLAG_Custom_1 : 0) |
			(Movement->bZeroGravityAscendInputHeld ? FLAG_Custom_2 : 0) |
			(Movement->bZeroGravityDescendInputHeld ? FLAG_Custom_3 : 0);
	}
}

void FMSCharacterSavedMove::PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode)
{
	Super::PostUpdate(Character, PostUpdateMode);
	if (UMSCharacterMovementComponent* Movement = Character ? Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr)
	{
		EndBoostState = Movement->CaptureBoostMovementState();
		bEndCanUseBoost = Movement->CanUseBoost();
		bHasRecordedEnd = true;
		// Replay updates both boundaries: PrepMoveFor already replaced the start.
		if (!StartBoostState.HasSameSimulationGate(EndBoostState) || bStartCanUseBoost != bEndCanUseBoost)
		{
			bForceNoCombine = true;
		}
	}
}

bool FMSCharacterSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (!NewMove.IsValid()) return false;
	const FMSCharacterSavedMove& NewMSMove = static_cast<const FMSCharacterSavedMove&>(*NewMove);
	if (SavedInputFlags != NewMSMove.SavedInputFlags || ClientSimulationEpoch != NewMSMove.ClientSimulationEpoch ||
		bRequestBoostCorrection != NewMSMove.bRequestBoostCorrection ||
		!StartBoostState.HasSameSimulationGate(NewMSMove.StartBoostState) ||
		bStartCanUseBoost != NewMSMove.bStartCanUseBoost ||
		(bHasRecordedEnd && (!StartBoostState.HasSameSimulationGate(EndBoostState) || bStartCanUseBoost != bEndCanUseBoost)))
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FMSCharacterSavedMove::CombineWith(const FSavedMove_Character* OldMove, ACharacter* Character,
	APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, Character, PC, OldStartLocation);
	const FMSCharacterSavedMove& OldMSMove = static_cast<const FMSCharacterSavedMove&>(*OldMove);
	StartBoostState = OldMSMove.StartBoostState;
	bStartCanUseBoost = OldMSMove.bStartCanUseBoost;
	if (UMSCharacterMovementComponent* Movement = Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		Movement->RestoreBoostMovementState(StartBoostState);
		Movement->bCombinedBoostMove = true;
	}
}

void FMSCharacterSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	if (UMSCharacterMovementComponent* Movement = Character ? Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr)
	{
		// Restore only intent. Old energy/altitude would overwrite the authority anchor
		// or the previous replay's newly calculated takeoff from a different floor.
		Movement->UpdateFromCompressedFlags(GetCompressedFlags());
		StartBoostState = Movement->CaptureBoostMovementState();
		ClientSimulationEpoch = Movement->ClientSimulationEpoch;
		bStartCanUseBoost = Movement->CanUseBoost();
		bRequestBoostCorrection = Movement->bRequestBoostCorrection;
	}
}

uint8 FMSCharacterSavedMove::GetCompressedFlags() const
{
	return Super::GetCompressedFlags() | SavedInputFlags;
}

void FMSCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	const FMSCharacterSavedMove& MSMove = static_cast<const FMSCharacterSavedMove&>(ClientMove);
	ReportedEndState = MSMove.EndBoostState;
	ClientSimulationEpoch = MSMove.ClientSimulationEpoch;
	bRequestBoostCorrection = MSMove.bRequestBoostCorrection;
}

bool FMSCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar,
	UPackageMap* Map, ENetworkMoveType MoveType)
{
	const bool bBaseSuccess = FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, MoveType);
	Ar << ClientSimulationEpoch;
	Ar.SerializeBits(&bRequestBoostCorrection, 1);
	return ReportedEndState.Serialize(Ar) && bBaseSuccess && !Ar.IsError();
}

FMSCharacterNetworkMoveDataContainer::FMSCharacterNetworkMoveDataContainer()
{
	NewMoveData = &Moves[0];
	PendingMoveData = &Moves[1];
	OldMoveData = &Moves[2];
}

void FMSCharacterMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& Adjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
	const UMSCharacterMovementComponent& MSMovement = static_cast<const UMSCharacterMovementComponent&>(Movement);
	bHasBoostState = MSMovement.PendingResponseTimeStamp == Adjustment.TimeStamp &&
		MSMovement.PendingResponseBoostState.bInitialized;
	AuthoritativeState = bHasBoostState ? MSMovement.PendingResponseBoostState : FMSBoostMovementState();
	ClientSimulationEpoch = MSMovement.PendingResponseClientEpoch;
	ResponseSequence = MSMovement.ResponseSequence;
}

bool FMSCharacterMoveResponseDataContainer::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, Map);
	Ar.SerializeBits(&bHasBoostState, 1);
	if (bHasBoostState)
	{
		Ar << ClientSimulationEpoch << ResponseSequence;
		if (!AuthoritativeState.Serialize(Ar)) return false;
	}
	return bBaseSuccess && !Ar.IsError();
}
