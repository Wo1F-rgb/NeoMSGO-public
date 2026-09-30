// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSBoostComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSBoostGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Player/MSCharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

UMSBoostComponent::UMSBoostComponent()
{
	// タイマー駆動のためTick不要(P2-6 UMSDownPointRegenComponentと同パターン)
	PrimaryComponentTick.bCanEverTick = false;
}

void UMSBoostComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshMovementDriver();

	if (UWorld* World = GetWorld())
	{
		LastStepTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(
			BoostTimerHandle, this, &UMSBoostComponent::BoostStep, BoostStepInterval, /*bLoop=*/true);
	}
}

void UMSBoostComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BoostTimerHandle);
	}

	// 自分がバインドした残量問い合わせを解除(CMCが先に破棄されるケースはWeakPtrで無害)
	if (CachedMoveComp.IsValid())
	{
		CachedMoveComp->BoostAvailableDelegate.Unbind();
	}

	Super::EndPlay(EndPlayReason);
}

void UMSBoostComponent::SetRates(float InConsumptionPerSecond, float InRechargePerSecond)
{
	ConsumptionPerSecond = FMath::Max(0.0f, InConsumptionPerSecond);
	RechargePerSecond = FMath::Max(0.0f, InRechargePerSecond);
	RecoveryState = FMSBoostMovementState();
	RecoveryState.RecoveryDelaySeconds = FMath::Max(0.0f, RecoveryDelaySeconds);
	RecoveryState.RecoveryDelayRemaining = RecoveryState.RecoveryDelaySeconds;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		++InitializationGeneration;
	}
	LastStepTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (ResolveOwnerAbilitySystem() && CachedMoveComp.IsValid())
	{
		CachedMoveComp->ResetBoostPrediction();
	}
}

bool UMSBoostComponent::UsesMovementClock() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (MovementClockOverrideForTest.IsSet()) return MovementClockOverrideForTest.GetValue();
#endif
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character && GetNetMode() != NM_Standalone &&
		Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()) &&
		(Character->GetLocalRole() == ROLE_AutonomousProxy ||
			(Character->HasAuthority() && Cast<APlayerController>(Character->GetController())));
}

void UMSBoostComponent::RefreshMovementDriver()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;
	const bool bMovementClock = UsesMovementClock();
	if (!bMovementDriverKnown || LastMovementController != Character->GetController() ||
		LastMovementRole != Character->GetLocalRole() || bLastUsedMovementClock != bMovementClock)
	{
		bMovementDriverKnown = true;
		LastMovementController = Character->GetController();
		LastMovementRole = Character->GetLocalRole();
		bLastUsedMovementClock = bMovementClock;
		LastStepTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Character->HasAuthority()) ++InitializationGeneration;
		if (ResolveOwnerAbilitySystem() && CachedMoveComp.IsValid()) CachedMoveComp->ResetBoostPrediction();
	}
}

bool UMSBoostComponent::ReadMovementState(FMSBoostMovementState& OutState)
{
	if (!ResolveOwnerAbilitySystem()) return false;
	const float LiveBoost = CachedBoostSet->GetBoost();
	const bool bDashMeleeRecovery = CachedASC->HasMatchingGameplayTag(MSTags::State_DashMeleeRegenBlock);
	if (bDashMeleeRecovery != RecoveryState.bRegenBlocked)
		RecoveryState.RecoveryDelayRemaining = FMath::Max(0.0f, RecoveryDelaySeconds);
	// D-melee and other instant costs bypass our periodic GE; they must restart the wait too.
	if (!RecoveryState.bInitialized || LiveBoost < RecoveryState.Boost - KINDA_SMALL_NUMBER)
		RecoveryState.RecoveryDelayRemaining = FMath::Max(0.0f, RecoveryDelaySeconds);
	OutState = RecoveryState;
	OutState.Boost = LiveBoost;
	OutState.MaxBoost = CachedBoostSet->GetMaxBoost();
	OutState.ConsumptionPerSecond = GetEffectiveConsumptionPerSecond();
	// Store the OH rate even before OH begins, so client prediction can cross depletion.
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const bool bForcedOH = CachedASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat);
	OutState.RechargePerSecond = RechargePerSecond / (Unit && !bForcedOH
		? FMath::Max(.01f, Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::OverheatRecoveryReduction)) : 1.0f);
	constexpr float StandardEffectiveRecharge = 45.0f; // InitializeUnit already applies raw charge x3.
	OutState.NormalRechargePerSecond = OutState.MaxBoost * FMath::Max(0.0f, NormalRecoveryFractionPerSecond)
		* RechargePerSecond / StandardEffectiveRecharge;
	OutState.RecoveryDelaySeconds = FMath::Max(0.0f, RecoveryDelaySeconds);
	OutState.RecoveryDelayRemaining = FMath::Clamp(OutState.RecoveryDelayRemaining, 0.0f, OutState.RecoveryDelaySeconds);
	OutState.bOverheated = CachedASC->HasMatchingGameplayTag(MSTags::State_Overheated);
	OutState.bDead = CachedASC->HasMatchingGameplayTag(MSTags::State_Dead);
	OutState.bRegenBlocked = bDashMeleeRecovery;
	OutState.Generation = GetOwner() && GetOwner()->HasAuthority() ? InitializationGeneration : 0;
	OutState.bInitialized = true;
	RecoveryState = OutState;
	return true;
}

void UMSBoostComponent::NotifyGroundingChanged(bool bGrounded)
{
	// Landing between timer samples cannot count the preceding airborne interval.
	if (!bGrounded) RecoveryState.bGrounded = false;
	RecoveryState.RecoveryDelayRemaining = FMath::Max(0.0f, RecoveryDelaySeconds);
}

void UMSBoostComponent::AdvanceMovementClock(float DeltaSeconds, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd)
{
	// Never apply a predicted GE from movement/replay. Only authority changes GAS.
	if (GetOwner() && GetOwner()->HasAuthority() && UsesMovementClock() && ResolveOwnerAbilitySystem())
		ApplyPeriodicStep(DeltaSeconds, bConsuming, bGroundedAtStart, bGroundedAtEnd);
}

float UMSBoostComponent::GetEffectiveConsumptionPerSecond() const
{
	const UAbilitySystemComponent* ASC = CachedASC.IsValid() ? CachedASC.Get()
		: UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	return ConsumptionPerSecond * (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Module_EffectActive)
		? ModuleConsumptionMultiplier : 1.0f);
}

float UMSBoostComponent::GetEffectiveRechargePerSecond() const
{
 const auto* Unit = Cast<AMSUnitBase>(GetOwner());
 const auto* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
 // Ordinary OH is shortened by increasing its actual recovery to the 80% threshold.
 // Transzomm forced OH is a distinct effect, never treated as EXAM.
 if (ASC && ASC->HasMatchingGameplayTag(MSTags::State_Overheated)
  && !ASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat))
  return RechargePerSecond / FMath::Max(.01f, Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::OverheatRecoveryReduction));
 return RechargePerSecond;
}

bool UMSBoostComponent::ResolveOwnerAbilitySystem()
{
	if (CachedASC.IsValid() && CachedBoostSet.IsValid())
	{
		return true;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!ASC)
	{
		return false;
	}

	const UMSAttributeSet_Boost* BoostSet = ASC->GetSet<UMSAttributeSet_Boost>();
	if (!BoostSet)
	{
		return false;
	}

	CachedASC = ASC;
	CachedBoostSet = BoostSet;

	// CMC(P3-1)へ残量問い合わせをバインド(IsBoostDashing等の「残量>0」条件が有効になる)
	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UMSCharacterMovementComponent* MoveComp = Cast<UMSCharacterMovementComponent>(OwnerCharacter->GetCharacterMovement()))
		{
			CachedMoveComp = MoveComp;
			MoveComp->BoostAvailableDelegate.BindUObject(this, &UMSBoostComponent::HasBoostRemaining);
		}
	}
	return true;
}

bool UMSBoostComponent::HasBoostRemaining() const
{
	return CachedBoostSet.IsValid() && CachedBoostSet->GetBoost() > 0.0f;
}

void UMSBoostComponent::BoostStep()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	RefreshMovementDriver();
	const double Now = World->GetTimeSeconds();
	const float DeltaTime = static_cast<float>(Now - LastStepTime);
	LastStepTime = Now;
	if (UsesMovementClock()) return;

	// The server owns simulated pawns' energy.  Keep the autonomous owner's
	// normal GAS step available for an active prediction scope, but do not run a
	// second local GE simulation for other clients' pawns; their replicated GAS
	// state remains the source of truth.
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && !OwnerCharacter->HasAuthority() && !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	// Δtはワールド時刻の実差分(タイマー起床間隔の揺らぎ・時間膨張に頑健。P2-6と同パターン)
	if (DeltaTime <= 0.0f || !ResolveOwnerAbilitySystem())
	{
		return;
	}

	// 消費判定はCMCへ委譲(Unity: HandleBoostState =(wantsDash||wantsHover)かつ
	// 非OH・非しゃがみ・非よろけ・残量あり。#131: しゃがみ中のEN消費バグ再発防止)
	const bool bConsuming = CachedMoveComp.IsValid() && CachedMoveComp->IsConsumingBoost();
	const bool bGrounded = CachedMoveComp.IsValid() && CachedMoveComp->IsMovingOnGround();
	ApplyPeriodicStep(DeltaTime, bConsuming, RecoveryState.bGrounded, bGrounded);
}

void UMSBoostComponent::ApplyPeriodicStep(float DeltaTime, bool bConsuming, bool bGroundedAtStart, bool bGroundedAtEnd)
{
	FMSBoostMovementState State;
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f || !ReadMovementState(State)) return;
	const float PreviousBoost = State.Boost;
	State.Advance(DeltaTime, bConsuming, bGroundedAtStart, bGroundedAtEnd);
	const float Change = State.Boost - PreviousBoost;
	RecoveryState = State;
	if (FMath::IsNearlyZero(Change)) return;
	UAbilitySystemComponent* ASC = CachedASC.Get();
	const bool bDebit = Change < 0.0f;
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(bDebit
		? UGE_BoostConsume::StaticClass() : UGE_BoostRegen::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(bDebit ? MSTags::Data_BoostConsume : MSTags::Data_BoostRegen, Change);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data, ASC->GetPredictionKeyForNewAction());
	}
	// GAS remains authoritative for clamping/tags and rejected unscoped client effects.
	RecoveryState.Boost = CachedBoostSet->GetBoost();
	RecoveryState.bOverheated = ASC->HasMatchingGameplayTag(MSTags::State_Overheated);
}
