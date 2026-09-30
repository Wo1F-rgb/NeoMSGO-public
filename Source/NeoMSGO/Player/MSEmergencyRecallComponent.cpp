// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSEmergencyRecallComponent.h"

#include "AbilitySystemComponent.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Gameplay/MSPlayerController.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

UMSEmergencyRecallComponent::UMSEmergencyRecallComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMSEmergencyRecallComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMSEmergencyRecallComponent, bRecallActive);
	DOREPLIFETIME(UMSEmergencyRecallComponent, bAwaitingDeploy);
	DOREPLIFETIME(UMSEmergencyRecallComponent, RecallFinishAt);
	DOREPLIFETIME(UMSEmergencyRecallComponent, RecallDurationSeconds);
	DOREPLIFETIME(UMSEmergencyRecallComponent, RecallGeneration);
	DOREPLIFETIME(UMSEmergencyRecallComponent, CooldownEndTime);
}

void UMSEmergencyRecallComponent::BeginPlay()
{
	Super::BeginPlay();

	AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	if (!Player)
	{
		return;
	}
	if (UMSAttributeSet_Vital* Vital = Player->GetVitalSet())
	{
		Vital->OnDamagedFrom.AddUniqueDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerDamaged);
  Vital->OnStagger.AddUniqueDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerStaggered);
	}
	if (UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
	{
		Respawn->OnDeployed.AddUniqueDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerDeployed);
	}
	if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
	{
		DeadTagEventHandle = ASC->RegisterGameplayTagEvent(
			MSTags::State_Dead, EGameplayTagEventType::NewOrRemoved).AddUObject(
				this, &UMSEmergencyRecallComponent::HandleDeadTagChanged);
	}
}

void UMSEmergencyRecallComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupRecall(false);
	if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
	{
		if (UMSAttributeSet_Vital* Vital = Player->GetVitalSet())
		{
			Vital->OnDamagedFrom.RemoveDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerDamaged);
   Vital->OnStagger.RemoveDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerStaggered);
		}
		if (UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
		{
			Respawn->OnDeployed.RemoveDynamic(this, &UMSEmergencyRecallComponent::HandleOwnerDeployed);
		}
		if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
			ASC && DeadTagEventHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(MSTags::State_Dead,
				EGameplayTagEventType::NewOrRemoved).Remove(DeadTagEventHandle);
		}
	}
	DeadTagEventHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UMSEmergencyRecallComponent::TryStartRecall()
{
	// Online recall is a server-owned transition.  The client only requests it;
	// bRecallActive is replicated back after the server validates the pawn state.
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority())
	{
		if (bRequestPending) return false;
		bRequestPending = true;
		NoticeKind = EMSRecallNotice::None;
		ServerTryStartRecall();
		// No success until the authority publishes the active state.
		return false;
	}
	return StartRecallInternal();
}

EMSRecallReason UMSEmergencyRecallComponent::GetStartBlockReason() const
{
	const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Player || !GetWorld() || !ASC || !Movement) return EMSRecallReason::Unavailable;
	const UMSAttributeSet_Vital* Vital = Player->GetVitalSet();
	if (ASC->HasMatchingGameplayTag(MSTags::State_Dead) || (Vital && Vital->GetArmor() <= 0.0f)) return EMSRecallReason::Dead;
	if (IsAwaitingDeploy()) return EMSRecallReason::AwaitingDeployment;
	if (bRecallActive) return EMSRecallReason::AlreadyActive;
	if (MSOnline::IsEnabled())
	{
		const AMSPlayerController* PC = Cast<AMSPlayerController>(Player->GetController());
		if (!PC || !PC->IsOnlineAuthenticated() || !PC->IsOnlineDeployed()) return EMSRecallReason::NotDeployed;
	}
	FGameplayTagContainer BlockedTags;
	BlockedTags.AddTag(MSTags::State_Staggered);
	BlockedTags.AddTag(MSTags::State_KnockedDown);
	if (ASC->HasAnyMatchingGameplayTags(BlockedTags)) return EMSRecallReason::Incapacitated;
	if (GetRecallCooldownRemaining() > 0.0f) return EMSRecallReason::Cooldown;
	if (!Movement->IsMovingOnGround()) return EMSRecallReason::Airborne;
	return EMSRecallReason::None;
}

bool UMSEmergencyRecallComponent::StartRecallInternal()
{
	const EMSRecallReason BlockReason = GetStartBlockReason();
	if (BlockReason != EMSRecallReason::None)
	{
		PublishNotice(EMSRecallNotice::Rejected, BlockReason);
		return false;
	}
	AMSPlayerCharacter* Player = CastChecked<AMSPlayerCharacter>(GetOwner());
	UWorld* World = GetWorld();
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	UCharacterMovementComponent* Movement = Player->GetCharacterMovement();

	const UMSMobileSuitData* Data = Player->GetMobileSuitData();
	const int32 Performance = Data ? Data->EmergencyRecallPerformance : 427;
	const float RecallDuration = static_cast<float>(FMath::GetMappedRangeValueClamped(
		FVector2D(0.0, 1000.0), FVector2D(15.0, 5.0), static_cast<double>(Performance)));

	Player->StopCurrentWeaponFire();
	Movement->StopMovementImmediately();
	if (UMSCharacterMovementComponent* MSMovement = Cast<UMSCharacterMovementComponent>(Movement))
	{
		MSMovement->SetBoostInputHeld(false);
		MSMovement->SetHoverInputHeld(false);
	}

	bRecallActive = true;
	++RecallGeneration;
	RecallDurationSeconds = RecallDuration;
	bRequestPending = false;
	NoticeKind = EMSRecallNotice::None;
	bAwaitingDeploy = false;
	ASC->AddLooseGameplayTag(MSTags::State_EmergencyRecall);
	ASC->AddLooseGameplayTag(MSTags::State_Crouching);
	bRecallTagsApplied = true;
	RecallFinishAt = GetSynchronizedTime() + RecallDuration;

	const FVector RingLocation = Player->GetActorLocation() -
		FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	RecallRing = AMSTransientVfxActor::SpawnRecallRing(World, RingLocation, RecallDuration);
	World->GetTimerManager().SetTimer(
		RecallTimerHandle, this, &UMSEmergencyRecallComponent::FinishRecall, RecallDuration, false);
	if (MSOnline::IsEnabled())
	{
		Player->ForceNetUpdate();
	}
	NotifyRecallStartedOnce();
	return true;
}

void UMSEmergencyRecallComponent::CancelRecall(const EMSRecallReason Reason)
{
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority())
	{
		// A movement/Z press immediately after requesting recall must cancel the
		// pending request too. Reliable RPC ordering keeps start then cancel intact.
		if (bRecallActive || bRequestPending) ServerCancelRecall(Reason);
		return;
	}
	if (bRecallActive)
	{
		CleanupRecall(true);
		PublishNotice(EMSRecallNotice::Cancelled, Reason);
		if (MSOnline::IsEnabled() && GetOwner() && GetOwner()->HasAuthority())
		{
			GetOwner()->ForceNetUpdate();
		}
	}
}

bool UMSEmergencyRecallComponent::IsAwaitingDeploy() const
{
	if (bAwaitingDeploy)
	{
		return true;
	}
	// During reconnect the Respawn snapshot can arrive before the component's
	// replicated presentation flag. Treat the authoritative snapshot as the
	// same state so OpenResortieScreen cannot issue a second recall request.
	if (!MSOnline::IsEnabled())
	{
		return false;
	}
	const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	const UMSRespawnComponent* Respawn = Player ? Player->GetRespawnComponent() : nullptr;
	return Respawn && Respawn->GetOnlineRespawnSnapshot().bRecallAwaitingDeploy;
}

float UMSEmergencyRecallComponent::GetRecallTimeRemaining() const
{
	const UWorld* World = GetWorld();
	if (!IsRecallActive() || !World)
	{
		return 0.0f;
	}
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority())
	{
		return static_cast<float>(FMath::Max(0.0, RecallFinishAt - GetSynchronizedTime()));
	}
	return FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(RecallTimerHandle));
}

float UMSEmergencyRecallComponent::GetRecallCooldownRemaining() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const double Now = MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority()
		? GetSynchronizedTime() : World->GetTimeSeconds();
	return static_cast<float>(FMath::Max(0.0, CooldownEndTime - Now));
}

void UMSEmergencyRecallComponent::FinishRecall()
{
	if (!bRecallActive)
	{
		return;
	}
	AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	CleanupRecall(false);
	if (!Player)
	{
		return;
	}
	Player->SetActorHiddenInGame(true);
	Player->SetActorEnableCollision(false);
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	bAwaitingDeploy = true;
	bAwaitingDeployPresentationApplied = true;
	PublishNotice(EMSRecallNotice::Completed, EMSRecallReason::None);
	if (MSOnline::IsEnabled() && Player->HasAuthority())
	{
		if (UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
		{
			Respawn->BeginOnlineRecall();
		}
		if (AMSPlayerController* PC = Cast<AMSPlayerController>(Player->GetController()))
		{
			PC->MarkOnlineRecallAwaitingDeployment();
		}
		Player->ForceNetUpdate();
	}
	OnRecallCompleted.Broadcast();
	OpenResortieAfterRecall();
}

void UMSEmergencyRecallComponent::CleanupRecall(const bool bStartCooldown)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(RecallTimerHandle);
		if (bStartCooldown)
		{
			CooldownEndTime = GetSynchronizedTime() + RecallCooldownSeconds;
		}
	}
	if (RecallRing)
	{
		RecallRing->Destroy();
		RecallRing = nullptr;
	}
	if (bRecallTagsApplied)
	{
		if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
		{
			if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
			{
				ASC->RemoveLooseGameplayTag(MSTags::State_EmergencyRecall);
				ASC->RemoveLooseGameplayTag(MSTags::State_Crouching);
			}
		}
		bRecallTagsApplied = false;
	}
	bRecallActive = false;
	RecallFinishAt = 0.0;
}

void UMSEmergencyRecallComponent::NotifyRecallStartedOnce()
{
	if (!IsRecallActive() || RecallGeneration <= 0 || LastNotifiedRecallGeneration == RecallGeneration) return;
	LastNotifiedRecallGeneration = RecallGeneration;
	bRequestPending = false;
	NoticeKind = EMSRecallNotice::None;
	OnRecallStarted.Broadcast();
}

float UMSEmergencyRecallComponent::GetRecallProgress() const
{
	return IsRecallActive() && RecallDurationSeconds > 0.0f
		? FMath::Clamp(1.0f - GetRecallTimeRemaining() / RecallDurationSeconds, 0.0f, 1.0f) : 0.0f;
}

EMSRecallNotice UMSEmergencyRecallComponent::GetNotice() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < NoticeExpiresAt ? NoticeKind : EMSRecallNotice::None;
}

void UMSEmergencyRecallComponent::ApplyNotice(const EMSRecallNotice Notice, const EMSRecallReason Reason)
{
	bRequestPending = false;
	NoticeKind = Notice;
	NoticeReason = Reason;
	NoticeExpiresAt = GetWorld() ? GetWorld()->GetTimeSeconds() + 3.0 : 0.0;
}

void UMSEmergencyRecallComponent::PublishNotice(const EMSRecallNotice Notice, const EMSRecallReason Reason)
{
	ApplyNotice(Notice, Reason);
	if (MSOnline::IsEnabled() && GetOwner() && GetOwner()->HasAuthority())
	{
		ClientRecallNotice(Notice, Reason, CooldownEndTime, RecallGeneration);
	}
}

void UMSEmergencyRecallComponent::ClientRecallNotice_Implementation(
	const EMSRecallNotice Notice, const EMSRecallReason Reason, const double CooldownUntil, const int32 Generation)
{
	if (Generation < RecallGeneration) return;
	// Reliable terminal feedback may precede an older replicated active snapshot.
	// Retire that generation so it cannot resurrect the ring or erase the notice.
	if (Notice == EMSRecallNotice::Cancelled || Notice == EMSRecallNotice::Completed)
	{
		LastTerminatedRecallGeneration = FMath::Max(LastTerminatedRecallGeneration, Generation);
		RemoveRecallPresentation();
	}
	CooldownEndTime = CooldownUntil;
	ApplyNotice(Notice, Reason);
}

FText UMSEmergencyRecallComponent::GetReasonText(const EMSRecallReason Reason)
{
	switch (Reason)
	{
	case EMSRecallReason::Airborne: return FText::FromString(TEXT("着地してから帰投できます"));
	case EMSRecallReason::Incapacitated: return FText::FromString(TEXT("行動不能中は帰投できません"));
	case EMSRecallReason::Dead: return FText::FromString(TEXT("撃破されました"));
	case EMSRecallReason::Cooldown: return FText::FromString(TEXT("帰投の再試行を待っています"));
	case EMSRecallReason::NotDeployed: return FText::FromString(TEXT("出撃後に帰投できます"));
	case EMSRecallReason::AlreadyActive: return FText::FromString(TEXT("すでに緊急帰投中です"));
	case EMSRecallReason::AwaitingDeployment: return FText::FromString(TEXT("帰投済みです。出撃機体を選択してください"));
	case EMSRecallReason::ManualCancel: return FText::FromString(TEXT("緊急帰投を中断しました"));
	case EMSRecallReason::Movement: return FText::FromString(TEXT("移動により緊急帰投を中断"));
	case EMSRecallReason::Action: return FText::FromString(TEXT("操作により緊急帰投を中断"));
	case EMSRecallReason::Damage: return FText::FromString(TEXT("被弾により緊急帰投を中断"));
	case EMSRecallReason::Unavailable: return FText::FromString(TEXT("現在は緊急帰投できません"));
	default: return FText::GetEmpty();
	}
}

void UMSEmergencyRecallComponent::ApplyRecallPresentation()
{
	AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	if (ASC && !bRecallTagsApplied)
	{
		ASC->AddLooseGameplayTag(MSTags::State_EmergencyRecall);
		ASC->AddLooseGameplayTag(MSTags::State_Crouching);
		bRecallTagsApplied = true;
	}
	if (!Player || !GetWorld() || RecallRing)
	{
		return;
	}
	const float Remaining = GetRecallTimeRemaining();
	if (Remaining <= 0.0f)
	{
		return;
	}
	const FVector RingLocation = Player->GetActorLocation() -
		FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	RecallRing = AMSTransientVfxActor::SpawnRecallRing(GetWorld(), RingLocation, Remaining);
}

void UMSEmergencyRecallComponent::RemoveRecallPresentation()
{
	if (RecallRing)
	{
		RecallRing->Destroy();
		RecallRing = nullptr;
	}
	if (!bRecallTagsApplied)
	{
		return;
	}
	if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
	{
		if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
		{
			// Remove exactly the count this component added; another owner (e.g. manual
			// crouch input) keeps its own loose-tag count intact.
			ASC->RemoveLooseGameplayTag(MSTags::State_EmergencyRecall);
			ASC->RemoveLooseGameplayTag(MSTags::State_Crouching);
		}
	}
	bRecallTagsApplied = false;
}

void UMSEmergencyRecallComponent::OpenResortieAfterRecall()
{
	AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
	if (Player && Player->IsLocallyControlled() && IsAwaitingDeploy())
	{
		Player->OpenResortieScreen();
	}
}

double UMSEmergencyRecallComponent::GetSynchronizedTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* State = World->GetGameState())
	{
		return State->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void UMSEmergencyRecallComponent::OnRep_RecallState()
{
	if (IsRecallActive())
	{
		ApplyRecallPresentation();
		NotifyRecallStartedOnce();
		return;
	}

	RemoveRecallPresentation();
	if (IsAwaitingDeploy())
	{
		if (!bAwaitingDeployPresentationApplied)
		{
			if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
			{
				Player->SetActorHiddenInGame(true);
				Player->SetActorEnableCollision(false);
				if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
			}
			bAwaitingDeployPresentationApplied = true;
			ApplyNotice(EMSRecallNotice::Completed, EMSRecallReason::None);
			OnRecallCompleted.Broadcast();
			OpenResortieAfterRecall();
		}
	}
	else if (bAwaitingDeployPresentationApplied)
	{
		if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
		{
			Player->SetActorHiddenInGame(false);
			Player->SetActorEnableCollision(true);
		}
		bAwaitingDeployPresentationApplied = false;
	}
}

void UMSEmergencyRecallComponent::ServerTryStartRecall_Implementation()
{
	if (MSOnline::IsEnabled() && GetOwner() && GetOwner()->HasAuthority())
	{
		StartRecallInternal();
	}
}

void UMSEmergencyRecallComponent::ServerCancelRecall_Implementation(const EMSRecallReason Reason)
{
	if (MSOnline::IsEnabled() && GetOwner() && GetOwner()->HasAuthority())
	{
		// Client input may describe voluntary actions, never damage/death or an
		// authority rejection. Those reasons originate on the server only.
		const EMSRecallReason ValidReason = Reason == EMSRecallReason::Movement || Reason == EMSRecallReason::Action
			? Reason : EMSRecallReason::ManualCancel;
		CancelRecall(ValidReason);
	}
}

void UMSEmergencyRecallComponent::HandleOwnerDamaged(AActor* DamagedUnit, AActor*, FVector)
{
	if (DamagedUnit == GetOwner())
	{
		const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner());
  // DP callbacks follow Armor callbacks; the separate OnStagger handler cancels
  // after the same packet has evaluated its DP. Never swallow death.
  if (Player && Player->GetVitalSet() && Player->GetVitalSet()->GetArmor() > 0.f
   && Player->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::RecallInterruptRequiresStagger) > 0.f) return;
		CancelRecall(Player && Player->GetVitalSet() && Player->GetVitalSet()->GetArmor() <= 0.0f
			? EMSRecallReason::Dead : EMSRecallReason::Damage);
	}
}

void UMSEmergencyRecallComponent::HandleOwnerStaggered(AActor* Unit, bool)
{
 if (Unit == GetOwner()) CancelRecall(EMSRecallReason::Incapacitated);
}

void UMSEmergencyRecallComponent::HandleOwnerDeployed(AMSUnitBase* Unit)
{
	if (Unit == GetOwner())
	{
		bAwaitingDeploy = false;
		bAwaitingDeployPresentationApplied = false;
		bRequestPending = false;
		NoticeKind = EMSRecallNotice::None;
		GetOwner()->SetActorHiddenInGame(false);
		GetOwner()->SetActorEnableCollision(true);
	}
}

void UMSEmergencyRecallComponent::HandleDeadTagChanged(const FGameplayTag, const int32 NewCount)
{
	if (NewCount > 0)
	{
		CancelRecall(EMSRecallReason::Dead);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSEmergencyRecallComponent::ApplyReplicatedStateForTesting(const bool bActive, const bool bAwaiting, const int32 Generation)
{
	bRecallActive = bActive;
	bAwaitingDeploy = bAwaiting;
	RecallGeneration = Generation;
	OnRep_RecallState();
}

void UMSEmergencyRecallComponent::ApplyAuthorityNoticeForTesting(const EMSRecallNotice Notice, const EMSRecallReason Reason)
{
	ClientRecallNotice_Implementation(Notice, Reason, CooldownEndTime, RecallGeneration);
}
#endif
