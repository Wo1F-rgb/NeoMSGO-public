// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSLockOnComponent.h"
#include "Player/MSIncomingLockComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "AI/MSBotAIController.h"
#include "Player/MSPlayerCharacter.h"

#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "Player/MSCameraComponent.h"
#include "UI/MSHUDRingWidget.h"
#include "UI/MSHUDTypes.h"
#include "Weapons/MSWeaponComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 対象の照準点(IMSLockOnTargetInterface::GetLockOnAimPoint。胸元/コア中心) */
	FVector GetTargetAimPoint(const AActor* Target)
	{
		if (const IMSLockOnTargetInterface* LockOnTarget = Cast<IMSLockOnTargetInterface>(Target))
		{
			return LockOnTarget->GetLockOnAimPoint();
		}
		// CanLockOn がインターフェース実装を要求するため通常到達しないが、安全側のフォールバック
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}

	void PlayLockOnConfirm(AActor* PotentialLocalPawn)
	{
		// ConstructorHelpersはコンストラクタ外で使用不可(Fatal)。Tick経路のためLoadObjectで解決する
		static TWeakObjectPtr<USoundBase> CachedConfirmSound;
		if (!CachedConfirmSound.IsValid())
		{
			CachedConfirmSound = LoadObject<USoundBase>(nullptr,
				TEXT("/Game/Audio/Imported/SFX_LockOn_Confirm.SFX_LockOn_Confirm"));
		}
		MSAudio::PlayMetaSound2DIfLocallyControlled(
			PotentialLocalPawn, MSAudioPreset::Resolve(EMSAudioUsage::LockConfirm, CachedConfirmSound.Get()),
			1450.0f, 0.30f, 0.18f);
	}
}

UMSLockOnComponent::UMSLockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 候補選定はカメラ位置依存のため、SpringArm/カメラ更新後(PostUpdateWork相当は不要、
	// 前フレームのカメラ位置で十分)に通常Tickで実行する
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void UMSLockOnComponent::BeginPlay()
{
	Super::BeginPlay();
	ObservedDeploymentEpoch = GetDeploymentEpoch();
	if (const auto* Pawn = Cast<APawn>(GetOwner())) ObservedController = Pawn->GetController();
	if (auto* Unit = Cast<AMSUnitBase>(GetOwner()))
		if (auto* Vital = Unit->GetVitalSet()) Vital->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleLockOwnerDeath);

	// P3-5カメラを解決(スコープ状態の参照+実カメラ位置の基準。冒頭コメント2)
	if (AActor* Owner = GetOwner())
	{
		CameraComponent = Owner->FindComponentByClass<UMSCameraComponent>();
		if (UMSWeaponComponent* Weapons = Owner->FindComponentByClass<UMSWeaponComponent>())
		{
			Weapons->OnWeaponSwitched.AddUniqueDynamic(this, &UMSLockOnComponent::HandleWeaponSwitched);
		}
	}
}

void UMSLockOnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearPublishedIncomingTargets();
	if (auto* Unit = Cast<AMSUnitBase>(GetOwner()))
		if (auto* Vital = Unit->GetVitalSet()) Vital->OnDeath.RemoveDynamic(this, &ThisClass::HandleLockOwnerDeath);
	if (AActor* Owner = GetOwner())
	{
		if (UMSWeaponComponent* Weapons = Owner->FindComponentByClass<UMSWeaponComponent>())
		{
			Weapons->OnWeaponSwitched.RemoveDynamic(this, &UMSLockOnComponent::HandleWeaponSwitched);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UMSLockOnComponent::HandleWeaponSwitched(int32 NewSlotIndex)
{
	// 実プレイ報告バグ修正: 武器持ち替えでロックオン(通常/赤ロック/誘導捕捉)を解除する。
	// 持ち替え先の武器はレンジ/赤ロック距離が異なるため、古いロック状態を引き継がない。
	ClearLockOn();
	HomingCandidate.Reset(); HomingCapturedTarget.Reset(); HomingDwellTime = 0.f;
	SyncIncomingTargets();
}

void UMSLockOnComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ObservedDeploymentEpoch != GetDeploymentEpoch()) ResetForDeployment();
	if (const auto* Pawn = Cast<APawn>(GetOwner()))
	{
		if ((ObservedController.IsValid() || ObservedController.IsStale()) && ObservedController.Get() != Pawn->GetController()) ResetForDeployment();
		ObservedController = Pawn->GetController();
	}
	ReconcileManualLockSnapshot();
	if (!IsLockOwnerAlive())
	{
		SetLockedTarget(nullptr); CurrentCandidate.Reset(); HomingCandidate.Reset(); HomingCapturedTarget.Reset();
		CandidateDwellTime = HomingDwellTime = 0.f; SyncIncomingTargets(); return;
	}
	if (LockedTarget.IsValid() && ManualTargetEpoch != GetTargetDeploymentEpoch(LockedTarget.Get())) SetLockedTarget(nullptr);
	if (HomingCandidate.IsValid() && HomingTargetEpoch != GetTargetDeploymentEpoch(HomingCandidate.Get()))
	{
		HomingCandidate.Reset(); HomingCapturedTarget.Reset(); HomingDwellTime = 0.f;
	}
	// ---- 1. ロック維持チェック(Unity: ロック維持中も毎フレーム再チェック) ----
	// ロック後は照準(円錐)を外しても維持するが、以下で自動解除する:
	//  - 対象の破棄(Weak無効化)
	//  - CanLockOn false(死亡=State.Dead、陣営変化。B-2単一API)
	//  - 遮蔽(LOS不通。起点=自機胸元。B-5)
	const bool bInvalidTarget = LockedTarget.IsStale() || (LockedTarget.IsValid() &&
		(!UMSTeamStatics::CanLockOn(GetOwner(), LockedTarget.Get()) || !IsManualTargetInRange(LockedTarget.Get())));
	const bool bOccluded = LockedTarget.IsValid() && !HasLineOfSightTo(LockedTarget.Get());
	if (bOccluded)
	{
		if (LockBreakElapsed <= 0.0f) bRedLockOcclusionGraceActive = bIsRedLocked;
		LockBreakElapsed += DeltaTime;
	}
	else
	{
		LockBreakElapsed = 0.0f;
		bRedLockOcclusionGraceActive = false;
	}
	UpdateRedLockState();
	const float BreakGrace = bRedLockOcclusionGraceActive ? RedLockBreakGraceSeconds : 0.0f;
	if (bInvalidTarget || (bOccluded && LockBreakElapsed > BreakGrace))
	{
		SetLockedTarget(nullptr);
	}

	// ---- 2. 候補選定+dwell計測 ----
	FVector ViewLocation;
	FVector ViewForward;
	if (GetViewPoint(ViewLocation, ViewForward))
	{
		AActor* Best = bAIHomingOnly ? nullptr : SelectBestCandidate(ViewLocation, ViewForward, LockOnRange, GetActiveConeDegrees());
		if (Best != CurrentCandidate.Get())
		{
			// 候補が切り替わったらdwellをリセット(「同一対象を捕捉し続ける」の計測)
			CurrentCandidate = Best;
			CandidateDwellTime = 0.0f;
		}
		else if (Best)
		{
			CandidateDwellTime += DeltaTime;
		}

		// Homing capture is an independent system. It never writes LockedTarget,
		// therefore ApplySoftAim cannot constrain the camera while a missile tracks.
		// 実プレイ仕様: 円内に入っただけでは捕捉せず、同一候補をLockOnDwellTime(既定0.8s)
		// 捉え続けてロック完了。円から外れたら進捗リセット、捕捉後に外れたら捕捉解除。
		const UMSWeaponComponent* Weapons = GetOwner()
			? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
		const UMSHomingWeaponData* HomingWeapon = Weapons
			? Cast<UMSHomingWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex())) : nullptr;
		// リロード完了までロック不可: 誘導武器装備中でもリロード中(ReloadRemaining > 0)は
		// 非誘導武器と同じリセット分岐へ合流させ、捕捉/dwell/確定を一切進行させない。
		const bool bIsHomingReloadBlocked = IsActiveWeaponHomingAndReloading();
		if (HomingWeapon && HomingWeapon->CategoryTag == MSTags::Weapon_Category_Homing && !bIsHomingReloadBlocked)
		{
			const float HomingRangeCm = FMath::Max(HomingWeapon->GetRangeWorldCm(), HomingWeapon->GetLockOnRangeWorldCm());
			// タスク#220: 判定円 = HUDに描かれている誘導カーソル円そのもの(旧: 固定60°)。
			const float HomingConeDegrees = GetHomingConeDegrees();

			// 捕捉済みターゲットが円から外れた(または無効化された)ら捕捉解除
			if (HomingCapturedTarget.IsValid() && ((bAIHomingOnly && HomingCapturedTarget.Get()!=GetAIHomingIntentTarget()) || !IsWithinHomingCircle(
					HomingCapturedTarget.Get(), ViewLocation, ViewForward, HomingRangeCm, HomingConeDegrees)))
			{
				HomingCapturedTarget.Reset();
			}
			if (HomingCapturedTarget.IsStale())
			{
				HomingCapturedTarget.Reset();
			}

			if (!HomingCapturedTarget.IsValid())
			{
				AActor* BestHoming = bAIHomingOnly ? GetAIHomingIntentTarget() : SelectBestCandidate(
					ViewLocation, ViewForward, HomingRangeCm, HomingConeDegrees);
				if (BestHoming && bAIHomingOnly && !IsWithinHomingCircle(BestHoming, ViewLocation, ViewForward, HomingRangeCm, HomingConeDegrees)) BestHoming = nullptr;
				if (BestHoming != HomingCandidate.Get())
				{
					// 候補切替/円外へ外れた → dwellリセット
					HomingCandidate = BestHoming;
					HomingTargetEpoch = GetTargetDeploymentEpoch(BestHoming);
					HomingDwellTime = 0.0f;
				}
				else if (BestHoming)
				{
					HomingDwellTime += DeltaTime;
				}

				if (HomingCandidate.IsValid() && HomingDwellTime >= GetEffectiveHomingDwellSeconds())
				{
					HomingCapturedTarget = HomingCandidate;
					PlayLockOnConfirm(GetOwner());
				}
			}
			else
			{
				// 捕捉維持中はdwell計測不要(進捗=1.0)
				HomingCandidate = HomingCapturedTarget;
				HomingDwellTime = GetEffectiveHomingDwellSeconds();
			}
		}
		else
		{
			HomingCapturedTarget.Reset();
			HomingCandidate = nullptr;
			HomingDwellTime = 0.0f;
		}
	}
	else
	{
		CurrentCandidate = nullptr;
		HomingCapturedTarget.Reset();
		HomingCandidate = nullptr;
		HomingDwellTime = 0.0f;
		CandidateDwellTime = 0.0f;
	}

	// ---- 3. ソフトエイム(ロック中のみ。基準=実カメラ位置) ----
	ApplySoftAim(DeltaTime);
	SyncIncomingTargets();
}

void UMSLockOnComponent::ToggleLockOn()
{
	if (!IsLockOwnerAlive() || bAIHomingOnly) return;
	if (IsLockedOn())
	{
		ClearLockOn();
		return;
	}

	// 誘導武器装備中はリロード完了まで手動ロックも弾く(非誘導武器/リロード完了後は影響なし)。
	if (IsActiveWeaponHomingAndReloading())
	{
		return;
	}

	// 未ロック: 現在の最良候補へ即時ロック(Unity: 通常ロックオンのトグルON)。
	// Tick済みのCurrentCandidateではなくその場で再選定する(押下フレームの照準に忠実)。
	FVector ViewLocation;
	FVector ViewForward;
	if (GetViewPoint(ViewLocation, ViewForward))
	{
		bLogNextSelectionDiagnostics = true;
		const float ConeDegrees = GetActiveConeDegrees();
		AActor* Best = SelectBestCandidate(ViewLocation, ViewForward, LockOnRange, ConeDegrees);
		bLogNextSelectionDiagnostics = false;
		const APhysicsVolume* Volume = GetOwner() ? GetOwner()->GetPhysicsVolume() : nullptr;
		UE_LOG(LogTemp, Display,
			TEXT("MSLockOnTrace: toggle owner=%s location=%s water=%s volume=%s "
				"view=%s forward=%s range=%.1f cone=%.2f result=%s"),
			*GetNameSafe(GetOwner()),
			GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("(invalid)"),
			Volume && Volume->bWaterVolume ? TEXT("true") : TEXT("false"),
			*GetNameSafe(Volume), *ViewLocation.ToCompactString(), *ViewForward.ToCompactString(),
			LockOnRange, ConeDegrees, *GetNameSafe(Best));
		if (Best)
		{
			SetLockedTarget(Best);
			SubmitManualLockIntent(Best);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("MSLockOnTrace: toggle owner=%s failed because no camera viewpoint was available"),
			*GetNameSafe(GetOwner()));
	}
}

void UMSLockOnComponent::ClearLockOn()
{
	SetLockedTarget(nullptr);
	SubmitManualLockIntent(nullptr);
	// 解除直後に同じ候補を照準し続けていても、即時再ロックせず改めて0.8sを要求する
	CandidateDwellTime = 0.0f;
}

void UMSLockOnComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(UMSLockOnComponent, ManualLockSnapshot, COND_OwnerOnly);
}

int32 UMSLockOnComponent::GetTargetDeploymentEpoch(const AActor* Target)
{
 const auto* Unit=Cast<AMSUnitBase>(Target);
 return Unit?Unit->GetBattleAssistEpoch():0;
}

int32 UMSLockOnComponent::GetDeploymentEpoch() const { return GetTargetDeploymentEpoch(GetOwner()); }

bool UMSLockOnComponent::IsLockOwnerAlive() const
{
 if(!IsValid(GetOwner()) || bEndingPlay) return false;
 if(GetOwner()->GetNetMode()!=NM_Standalone)
  if(const auto* Player=Cast<AMSPlayerCharacter>(GetOwner()); Player && !Player->IsGameplayInputAllowed()) return false;
 const auto* Unit=Cast<AMSUnitBase>(GetOwner());
 return !Unit || !Unit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead);
}

bool UMSLockOnComponent::IsManualTargetInRange(const AActor* Target) const
{
 FVector Location,Forward;
 return Target && GetViewPoint(Location,Forward) && FVector::DistSquared(Location,GetTargetAimPoint(Target))<=FMath::Square(LockOnRange);
}

bool UMSLockOnComponent::CanApplyManualAimAssist() const
{
	const AActor* Target = LockedTarget.Get();
	return Target && IsLockOwnerAlive() && ManualTargetEpoch == GetTargetDeploymentEpoch(Target)
		&& UMSTeamStatics::CanLockOn(GetOwner(), Target) && IsManualTargetInRange(Target)
		&& HasLineOfSightTo(Target);
}

void UMSLockOnComponent::SubmitManualLockIntent(AActor* Candidate)
{
 if(!GetOwner()) return;
 if(GetOwner()->HasAuthority())
 {
  // Keep standalone legacy tools/tests usable without an equipped weapon. Network intent is strict.
  if(GetOwner()->GetNetMode()!=NM_Standalone) ApplyManualLockIntent(Candidate,GetDeploymentEpoch(),++LocalRequestSequence,GetTargetDeploymentEpoch(Candidate));
  return;
 }
 if(const auto* Pawn=Cast<APawn>(GetOwner()); Pawn && Pawn->IsLocallyControlled())
  ServerSetManualLockIntent(Candidate,GetDeploymentEpoch(),++LocalRequestSequence,GetTargetDeploymentEpoch(Candidate));
}

void UMSLockOnComponent::ServerSetManualLockIntent_Implementation(AActor* Candidate,int32 SpawnEpoch,uint32 RequestSequence,int32 TargetEpoch)
{
 ApplyManualLockIntent(Candidate,SpawnEpoch,RequestSequence,TargetEpoch);
}

bool UMSLockOnComponent::ApplyManualLockIntent(AActor* Candidate,int32 SpawnEpoch,uint32 RequestSequence,int32 TargetEpoch)
{
 if(!GetOwner() || !GetOwner()->HasAuthority()) return false;
 if(SpawnEpoch!=GetDeploymentEpoch() || RequestSequence<=AcceptedRequestSequence)
 {
  PublishManualLockSnapshot(); return false;
 }
 AcceptedRequestSequence=RequestSequence;
 bool Valid=IsLockOwnerAlive() && !bAIHomingOnly;
 if(Candidate)
 {
  const auto* Weapons=GetOwner()->FindComponentByClass<UMSWeaponComponent>();
  FVector Location,Forward;
  const bool EpochMatches=Cast<AMSUnitBase>(Candidate)
   ? TargetEpoch==GetTargetDeploymentEpoch(Candidate)
   : (TargetEpoch==INDEX_NONE || TargetEpoch==0);
  Valid=Valid && IsValid(Candidate) && EpochMatches
   && Weapons && Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) && !IsActiveWeaponHomingAndReloading()
   && GetViewPoint(Location,Forward) && IsWithinHomingCircle(Candidate,Location,Forward,LockOnRange,GetActiveConeDegrees());
 }
 SetLockedTarget(Valid?Candidate:nullptr);
 // Also acknowledge rejected intents that leave an already-null target unchanged.
 PublishManualLockSnapshot();
 return Valid;
}

void UMSLockOnComponent::PublishManualLockSnapshot()
{
 if(!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay) return;
 ManualLockSnapshot.Target=LockedTarget.Get();
 ManualLockSnapshot.SpawnEpoch=GetDeploymentEpoch();
 ManualLockSnapshot.RequestSequence=AcceptedRequestSequence;
 ++ManualLockSnapshot.Revision;
 GetOwner()->ForceNetUpdate();
}

void UMSLockOnComponent::OnRep_ManualLockSnapshot() { ReconcileManualLockSnapshot(); }

void UMSLockOnComponent::ReconcileManualLockSnapshot()
{
 const auto* Pawn=Cast<APawn>(GetOwner());
 if(!Pawn || Pawn->HasAuthority() || !Pawn->IsLocallyControlled()) return;
 if(ManualLockSnapshot.Revision==AppliedSnapshotRevision && LockedTarget.Get()==ManualLockSnapshot.Target.Get()) return;
 // Module spawn snapshot and component replication can arrive in either order; retry next tick.
 if(ManualLockSnapshot.SpawnEpoch!=GetDeploymentEpoch() || ManualLockSnapshot.RequestSequence<LocalRequestSequence) return;
 AppliedSnapshotRevision=ManualLockSnapshot.Revision;
 SetLockedTarget(ManualLockSnapshot.Target.Get());
}

void UMSLockOnComponent::ResetForDeployment()
{
 ClearPublishedIncomingTargets();
 CurrentCandidate.Reset(); HomingCandidate.Reset(); HomingCapturedTarget.Reset();
 SetLockedTarget(nullptr);
 CandidateDwellTime=HomingDwellTime=0.f;
 ObservedDeploymentEpoch=GetDeploymentEpoch();
 ManualTargetEpoch=HomingTargetEpoch=0;
 LocalRequestSequence=AcceptedRequestSequence=0;
 AppliedSnapshotRevision=0;
 PublishManualLockSnapshot();
}

void UMSLockOnComponent::HandleLockOwnerDeath(AActor*,AActor*) { ResetForDeployment(); }

void UMSLockOnComponent::InvalidateIncomingTarget(AActor* Target)
{
 if(!GetOwner() || !GetOwner()->HasAuthority()) return;
 if(LockedTarget.Get()==Target) SetLockedTarget(nullptr);
 if(HomingCandidate.Get()==Target || HomingCapturedTarget.Get()==Target)
 {
  HomingCandidate.Reset(); HomingCapturedTarget.Reset(); HomingDwellTime=0.f;
 }
 SyncIncomingTargets();
}

bool UMSLockOnComponent::GetIncomingThreatState(const AActor* Target,bool& Manual,bool& Tracking,bool& Captured) const
{
 Manual=Tracking=Captured=false;
 if(!GetOwner() || !GetOwner()->HasAuthority() || !IsLockOwnerAlive() || !IsValid(Target)
  || !UMSTeamStatics::CanLockOn(GetOwner(),Target)) return false;
 if(bAIHomingOnly && Target!=GetAIHomingIntentTarget()) return false;
 Manual=LockedTarget.Get()==Target && ManualTargetEpoch==GetTargetDeploymentEpoch(Target) && IsManualTargetInRange(Target);
 if(Manual && !HasLineOfSightTo(Target)) Manual=bRedLockOcclusionGraceActive && LockBreakElapsed<=RedLockBreakGraceSeconds;
 const auto* Weapons=GetOwner()->FindComponentByClass<UMSWeaponComponent>();
 const auto* Homing=Weapons?Cast<UMSHomingWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex())):nullptr;
 FVector Location,Forward;
 if(Homing && Homing->CategoryTag==MSTags::Weapon_Category_Homing && !IsActiveWeaponHomingAndReloading()
  && HomingCandidate.Get()==Target && HomingTargetEpoch==GetTargetDeploymentEpoch(Target)
  && GetViewPoint(Location,Forward)
  && IsWithinHomingCircle(Target,Location,Forward,FMath::Max(Homing->GetRangeWorldCm(),Homing->GetLockOnRangeWorldCm()),GetHomingConeDegrees()))
 {
  Tracking=true; Captured=HomingCapturedTarget.Get()==Target;
 }
 return Manual||Tracking||Captured;
}

void UMSLockOnComponent::ClearPublishedIncomingTargets()
{
 if(!GetOwner() || !GetOwner()->HasAuthority()) return;
 auto Previous=MoveTemp(PublishedIncomingTargets); PublishedIncomingTargets.Reset();
 for(const auto& Target:Previous)
  if(Target.IsValid()) if(auto* Receiver=Target->FindComponentByClass<UMSIncomingLockComponent>()) Receiver->RemoveSource(this);
}

void UMSLockOnComponent::SyncIncomingTargets()
{
 if(!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay) return;
 TArray<TWeakObjectPtr<AActor>> Next;
 if(LockedTarget.IsValid()) Next.AddUnique(LockedTarget);
 if(HomingCandidate.IsValid()) Next.AddUnique(HomingCandidate);
 auto Previous=PublishedIncomingTargets;
 PublishedIncomingTargets=Next;
 for(const auto& Target:Previous)
  if(!Next.Contains(Target) && Target.IsValid())
   if(auto* Receiver=Target->FindComponentByClass<UMSIncomingLockComponent>()) Receiver->RemoveSource(this);
 for(const auto& Target:Next)
  if(Target.IsValid()) if(auto* Receiver=Target->FindComponentByClass<UMSIncomingLockComponent>()) Receiver->RefreshSource(this);
}

void UMSLockOnComponent::NotifyTargetEmergencyDodged(AActor* DodgingTarget)
{
	if (DodgingTarget && LockedTarget.Get() == DodgingTarget
		&& (bIsRedLocked || bRedLockOcclusionGraceActive))
	{
		RedLockSuppressedTarget = DodgingTarget;
		bIsRedLocked = false;
		LockBreakElapsed = 0.0f;
		bRedLockOcclusionGraceActive = false;
	}
}

void UMSLockOnComponent::UpdateRedLockState()
{
	bIsRedLocked = false;
	AActor* Target = LockedTarget.Get();
	if (!Target || RedLockSuppressedTarget.Get() == Target || !CanApplyManualAimAssist())
	{
		return;
	}

	const UMSWeaponComponent* Weapons = GetOwner()
		? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const UMSWeaponDataBase* Weapon = Weapons
		? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	if (!Weapon || Weapon->RedLockDistance <= 0.0f)
	{
		return;
	}
	// Weapon data stores the MSGO weapon lock distance in scaled UE centimeters. For example the
	// standard 300 m red-lock range is 30,000 cm * MSScale in the 18 m mobile-suit world.
	bIsRedLocked = IsWithinRedLockDistance(
		FVector::Dist(GetOwnAimOrigin(), GetTargetAimPoint(Target)), Weapon->RedLockDistance);
}

bool UMSLockOnComponent::IsWithinRedLockDistance(const float DistanceCm, const float RedLockDistanceCm)
{
	return RedLockDistanceCm > 0.0f && DistanceCm <= RedLockDistanceCm;
}

bool UMSLockOnComponent::IsActiveWeaponHomingAndReloading() const
{
	const UMSWeaponComponent* Weapons = GetOwner()
		? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	if (!Weapons)
	{
		return false;
	}
	const int32 ActiveSlot = Weapons->GetActiveSlotIndex();
	const UMSHomingWeaponData* HomingWeapon = Cast<UMSHomingWeaponData>(Weapons->GetWeaponData(ActiveSlot));
	if (!HomingWeapon || HomingWeapon->CategoryTag != MSTags::Weapon_Category_Homing)
	{
		return false;
	}
	if (Weapons->IsReloading(ActiveSlot))
	{
		return true;
	}
	// 実プレイ報告バグ対策: 連装ミサイルの全弾発射直後、オートリロード開始までの間隙
	// フレームはIsReloadingがまだfalseのため、dwellが完了してロック確定音だけ鳴っていた。
	// 弾切れ(残弾0)もリロード中と同じ「ロック不可」として扱い、音だけ鳴る事象を塞ぐ。
	return !HomingWeapon->HasInfiniteAmmo() && Weapons->GetMagazineAmmo(ActiveSlot) <= 0;
}

float UMSLockOnComponent::GetDwellProgress() const
{
	if (IsLockedOn())
	{
		return 1.0f;
	}
	if (!CurrentCandidate.IsValid() || LockOnDwellTime <= 0.0f)
	{
		return CurrentCandidate.IsValid() ? 1.0f : 0.0f;
	}
	return FMath::Clamp(CandidateDwellTime / LockOnDwellTime, 0.0f, 1.0f);
}

float UMSLockOnComponent::GetEffectiveHomingDwellSeconds() const
{
 const auto* Unit = Cast<AMSUnitBase>(GetOwner());
 return FMath::Max(0.f, LockOnDwellTime - (Unit ? Unit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::MissileLockTimeReduction) : 0.f));
}

float UMSLockOnComponent::GetHomingDwellProgress() const
{
	if (HomingCapturedTarget.IsValid())
	{
		return 1.0f;
	}
	if (!HomingCandidate.IsValid())
	{
		return 0.0f;
	}
	if (LockOnDwellTime <= 0.0f)
	{
		return 1.0f;
	}
	return GetEffectiveHomingDwellSeconds() <= 0.f ? 1.f : FMath::Clamp(HomingDwellTime / GetEffectiveHomingDwellSeconds(), 0.0f, 1.0f);
}

bool UMSLockOnComponent::IsWithinHomingCircle(const AActor* Target, const FVector& ViewLocation,
	const FVector& ViewForward, const float MaxRangeCm, const float ConeDegrees) const
{
	if (!Target || !UMSTeamStatics::CanLockOn(GetOwner(), Target))
	{
		return false;
	}
	const FVector AimPoint = GetTargetAimPoint(Target);
	const FVector ToTarget = AimPoint - ViewLocation;
	const float Distance = ToTarget.Size();
	if (Distance <= KINDA_SMALL_NUMBER || Distance > MaxRangeCm)
	{
		return false;
	}
	const float MinCosAngle = FMath::Cos(FMath::DegreesToRadians(ConeDegrees * 0.5f));
	if (FVector::DotProduct(ViewForward.GetSafeNormal(), ToTarget / Distance) < MinCosAngle)
	{
		return false;
	}
	return HasLineOfSightTo(Target);
}

float UMSLockOnComponent::ComputeFocalLengthPixels(const float ViewportWidthPx, const float ViewportHeightPx,
	const float FieldOfViewDegrees, const float CameraAspectRatio,
	const TEnumAsByte<EAspectRatioAxisConstraint> AxisConstraint)
{
	const float SizeX = FMath::Max(ViewportWidthPx, 1.0f);
	const float SizeY = FMath::Max(ViewportHeightPx, 1.0f);
	// UE の FOV は「水平FOV」。tan(半角) が投影行列のスケール分母になる。
	const float TanHalfXFOV = FMath::Tan(
		FMath::DegreesToRadians(FMath::Clamp(FieldOfViewDegrees, 1.0f, 179.0f) * 0.5f));

	// FMinimalViewInfo::CalculateProjectionMatrixGivenView と同一の分岐。
	const bool bMaintainXFOV = ((SizeX > SizeY) && (AxisConstraint == AspectRatio_MajorAxisFOV))
		|| (AxisConstraint == AspectRatio_MaintainXFOV);
	if (bMaintainXFOV)
	{
		// XAxisMultiplier=1 → 水平半幅 SizeX/2 が tan(水平半角) に対応する
		return (SizeX * 0.5f) / TanHalfXFOV;
	}

	// MaintainYFOV: 垂直FOVを固定。エンジンは水平FOVをカメラの想定アスペクト比で垂直へ変換する。
	const float SafeAspect = CameraAspectRatio > KINDA_SMALL_NUMBER ? CameraAspectRatio : (SizeX / SizeY);
	const float TanHalfYFOV = TanHalfXFOV / SafeAspect;
	return (SizeY * 0.5f) / TanHalfYFOV;
}

float UMSLockOnComponent::ComputeConeDegreesForScreenRadius(
	const float ScreenRadiusPx, const float FocalLengthPx)
{
	if (ScreenRadiusPx <= 0.0f || FocalLengthPx <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return 2.0f * FMath::RadiansToDegrees(FMath::Atan(ScreenRadiusPx / FocalLengthPx));
}

float UMSLockOnComponent::ComputeScreenRadiusForConeDegrees(
	const float ConeDegrees, const float FocalLengthPx)
{
	if (ConeDegrees <= 0.0f || FocalLengthPx <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FocalLengthPx * FMath::Tan(FMath::DegreesToRadians(FMath::Min(ConeDegrees, 179.0f) * 0.5f));
}

float UMSLockOnComponent::GetHomingConeDegrees() const
{
	// ---- 1. 現在のカメラ諸元(FOVスライダー#18/スコープ遷移の実効値) ----
	// UMSCameraComponent::GetCameraView が毎フレーム FieldOfView を補間するので、
	// このメンバがそのまま「今フレームの実FOV」になる。
	float FieldOfViewDegrees = 60.0f;
	float CameraAspectRatio = 16.0f / 9.0f;
	TEnumAsByte<EAspectRatioAxisConstraint> AxisConstraint(
		GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint);
	if (CameraComponent)
	{
		FieldOfViewDegrees = CameraComponent->FieldOfView;
		CameraAspectRatio = CameraComponent->AspectRatio;
		if (CameraComponent->bOverrideAspectRatioAxisConstraint)
		{
			AxisConstraint = CameraComponent->AspectRatioAxisConstraint;
		}
	}
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
			{
				if (!CameraComponent || !CameraComponent->bOverrideAspectRatioAxisConstraint)
				{
					AxisConstraint = LocalPlayer->AspectRatioAxisConstraint;
				}
			}
		}
	}

	// ---- 2. ビューポート解像度(px)とUMGのDPIスケール ----
	// HUDリングは Slate 単位で配置され、描画時に DPI スケールが掛かる。
	// 判定はピクセル空間で行うので、描画半径にも同じスケールを掛ける。
	float ViewportWidthPx = FallbackViewportWidthPx;
	float ViewportHeightPx = FallbackViewportHeightPx;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			FVector2D ViewportSize = FVector2D::ZeroVector;
			ViewportClient->GetViewportSize(ViewportSize);
			if (ViewportSize.X > 1.0 && ViewportSize.Y > 1.0)
			{
				ViewportWidthPx = static_cast<float>(ViewportSize.X);
				ViewportHeightPx = static_cast<float>(ViewportSize.Y);
			}
		}
	}
	// UWidgetLayoutLibrary::GetViewportScale と同一の算出(UMGが実際にウィジェットへ掛ける倍率)。
	const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
		FIntPoint(FMath::TruncToInt32(ViewportWidthPx), FMath::TruncToInt32(ViewportHeightPx)));

	// ---- 3. 描かれている円の半径(px) → 円錐全角 ----
	const float RingRadiusPx = UMSHUDRingWidget::ComputeDrawRadius(
		MSHUDPalette::HomingCursorSize, MSHUDPalette::HomingCursorThickness) * FMath::Max(DPIScale, 0.01f);
	const float FocalLengthPx = ComputeFocalLengthPixels(
		ViewportWidthPx, ViewportHeightPx, FieldOfViewDegrees, CameraAspectRatio, AxisConstraint);
	return ComputeConeDegreesForScreenRadius(RingRadiusPx, FocalLengthPx);
}

float UMSLockOnComponent::GetActiveConeDegrees() const
{
	// P3-5スコープ状態と連携(計画書P3-6完了条件: 30°/17°)
	return (CameraComponent && CameraComponent->IsScoped()) ? ScopedLockOnConeDegrees : LockOnConeDegrees;
}

bool UMSLockOnComponent::GetSoftAimTargetLocation(FVector& OutAimPoint) const
{
	if (!CanApplyManualAimAssist())
	{
		return false;
	}
	OutAimPoint = GetTargetAimPoint(LockedTarget.Get());
	return true;
}

bool UMSLockOnComponent::ComputeSoftAimLookRotation(FRotator& OutRotation) const
{
	FVector AimPoint;
	if (!GetSoftAimTargetLocation(AimPoint) || !CameraComponent)
	{
		return false;
	}

	// 基準は実カメラ位置(UMSCameraComponentワールド座標)。SpringArmピボット基準は
	// SocketOffset(肩越しオフセット)ぶん照準が右へずれるため禁止(ヘッダ冒頭コメント2)。
	const FVector ToTarget = AimPoint - CameraComponent->GetComponentLocation();
	if (ToTarget.IsNearlyZero())
	{
		return false;
	}
	OutRotation = ToTarget.Rotation();
	return true;
}

float UMSLockOnComponent::ComputeLockOnScore(
	const float AngleDeg, const float ConeDegrees, const float Distance, const float MaxDistance)
{
	// 視野角は円錐の全角として扱う(角度差の比較対象は半角)
	const float HalfCone = FMath::Max(ConeDegrees * 0.5f, KINDA_SMALL_NUMBER);
	const float SafeMaxDistance = FMath::Max(MaxDistance, KINDA_SMALL_NUMBER);

	const float AngleScore = 1.0f - FMath::Clamp(AngleDeg / HalfCone, 0.0f, 1.0f);
	const float DistanceScore = 1.0f - FMath::Clamp(Distance / SafeMaxDistance, 0.0f, 1.0f);

	// Unity: score = angleScore*0.35f + distanceScore*0.65f(距離重視)
	return AngleScore * AngleScoreWeight + DistanceScore * DistanceScoreWeight;
}

FRotator UMSLockOnComponent::ComputeLimitedAimRotation(const FRotator& Current, const FRotator& Desired,
	const float DeltaTime, const float InterpSpeed, const float MaxCorrectionDegrees)
{
	if (DeltaTime <= 0.0f || InterpSpeed <= 0.0f || MaxCorrectionDegrees <= 0.0f)
	{
		return Current;
	}
	// A hard error gate made yellow lock stop entirely once a moving target was more
	// than four degrees away. Limit angular travel instead: it follows continuously
	// but cannot snap straight to a distant target in one frame.
	const FRotator Interpolated = FMath::RInterpTo(Current, Desired, DeltaTime, InterpSpeed);
	const FRotator Step = (Interpolated - Current).GetNormalized();
	const float StepDegrees = FMath::Sqrt(FMath::Square(Step.Pitch) + FMath::Square(Step.Yaw));
	const float MaxStepDegrees = MaxCorrectionDegrees * (DeltaTime / 0.1f);
	if (StepDegrees <= MaxStepDegrees || StepDegrees <= KINDA_SMALL_NUMBER) return Interpolated;
	return (Current + Step * (MaxStepDegrees / StepDegrees)).GetNormalized();
}

bool UMSLockOnComponent::GetViewPoint(FVector& OutLocation, FVector& OutForward) const
{
 if(bAIHomingOnly)
 {
  const AActor* Target=GetAIHomingIntentTarget();
  if(!Target) return false;
  OutLocation=GetOwnAimOrigin(); OutForward=(GetTargetAimPoint(Target)-OutLocation).GetSafeNormal();
  return !OutForward.IsNearlyZero();
 }
 // Remote authority computes capture from the replicated control rotation. A remote
 // camera component is presentation state and may not tick on a dedicated server.
 if (const auto* Pawn = Cast<APawn>(GetOwner()); Pawn && Pawn->HasAuthority()
  && Pawn->GetNetMode() != NM_Standalone && !Pawn->IsLocallyControlled() && Pawn->GetController())
 {
  FRotator ViewRotation; Pawn->GetActorEyesViewPoint(OutLocation, ViewRotation);
  OutForward = ViewRotation.Vector(); return true;
 }

	if (!CameraComponent)
	{
		return false;
	}
	OutLocation = CameraComponent->GetComponentLocation();
	OutForward = CameraComponent->GetForwardVector();
	return true;
}

AActor* UMSLockOnComponent::GetAIHomingIntentTarget() const
{
 const auto* Pawn=Cast<APawn>(GetOwner());
 const auto* AI=Pawn?Cast<AMSBotAIController>(Pawn->GetController()):nullptr;
 return AI && !AI->IsActionBlocked() ? AI->GetCurrentTarget() : nullptr;
}

AActor* UMSLockOnComponent::SelectBestCandidate(const FVector& ViewLocation, const FVector& ViewForward,
	const float MaxRangeCm, const float ConeDegrees) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return nullptr;
	}

	// 候補収集: カメラ中心・ロック距離のWeaponTrace球Overlap(B-3: 弾を止めるものはロックも遮る、
	// と同一チャンネル。機体カプセル=MSPawnCapsule / 本拠点コア=MSCore がヒットする)
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSLockOnGather), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Owner);
	World->OverlapMultiByChannel(Overlaps, ViewLocation, FQuat::Identity,
		MSCollision::WeaponTraceChannel, FCollisionShape::MakeSphere(MaxRangeCm), QueryParams);
	if (bLogNextSelectionDiagnostics)
	{
		UE_LOG(LogTemp, Display,
			TEXT("MSLockOnTrace: gather overlaps=%d origin=%s range=%.1f cone=%.2f"),
			Overlaps.Num(), *ViewLocation.ToCompactString(), MaxRangeCm, ConeDegrees);
	}

	const float HalfConeRad = FMath::DegreesToRadians(ConeDegrees * 0.5f);
	const float MinCosAngle = FMath::Cos(HalfConeRad);
	const FVector ViewDir = ViewForward.GetSafeNormal();

	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;
	TSet<const AActor*> Visited;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Visited.Contains(Candidate))
		{
			continue;
		}
		Visited.Add(Candidate);

		// 陣営+ロック対象契約(敵対 かつ ユニット/本拠点コア かつ 生存)。B-2単一API。
		// 味方/中立/占領拠点/設置物/死亡済みはここで構造的に除外、本拠点コアは通過(B-5)
		if (!UMSTeamStatics::CanLockOn(Owner, Candidate))
		{
			if (bLogNextSelectionDiagnostics)
			{
				UE_LOG(LogTemp, Display,
					TEXT("MSLockOnTrace: reject candidate=%s class=%s reason=CanLockOn"),
					*GetNameSafe(Candidate), *GetNameSafe(Candidate->GetClass()));
			}
			continue;
		}

		// 視野角: 照準点への方向とカメラ前方の角度が視野半角以内か
		const FVector AimPoint = GetTargetAimPoint(Candidate);
		const FVector ToTarget = AimPoint - ViewLocation;
		const float Distance = ToTarget.Size();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > MaxRangeCm)
		{
			if (bLogNextSelectionDiagnostics)
			{
				UE_LOG(LogTemp, Display,
					TEXT("MSLockOnTrace: reject candidate=%s reason=distance value=%.1f max=%.1f"),
					*GetNameSafe(Candidate), Distance, MaxRangeCm);
			}
			continue;
		}
		const float CosAngle = FVector::DotProduct(ViewDir, ToTarget / Distance);
		if (CosAngle < MinCosAngle)
		{
			if (bLogNextSelectionDiagnostics)
			{
				const float Angle = FMath::RadiansToDegrees(
					FMath::Acos(FMath::Clamp(CosAngle, -1.0f, 1.0f)));
				UE_LOG(LogTemp, Display,
					TEXT("MSLockOnTrace: reject candidate=%s reason=cone angle=%.2f halfCone=%.2f"),
					*GetNameSafe(Candidate), Angle, ConeDegrees * 0.5f);
			}
			continue;
		}

		// LOS: 壁遮蔽の候補は除外(起点=自機胸元。B-5)
		if (!HasLineOfSightTo(Candidate))
		{
			if (bLogNextSelectionDiagnostics)
			{
				UE_LOG(LogTemp, Display,
					TEXT("MSLockOnTrace: reject candidate=%s reason=LOS"), *GetNameSafe(Candidate));
			}
			continue;
		}

		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosAngle, -1.0f, 1.0f)));
		const float Score = ComputeLockOnScore(AngleDeg, ConeDegrees, Distance, MaxRangeCm);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	return Best;
}

bool UMSLockOnComponent::HasLineOfSightTo(const AActor* Target) const
{
	UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !Target)
	{
		return false;
	}

	// 起点=自機胸元(カメラ基準は禁止。カメラだけ遮蔽された敵のロックが誤って切れる。B-5)
	// 終点=対象の胸元(GetLockOnAimPoint)
	const FVector TraceStart = GetOwnAimOrigin();
	const FVector TraceEnd = GetTargetAimPoint(Target);

	// 自機Actorは無視(自機の頭部コライダー等を障害物と誤認しない。ヘッダ冒頭コメント3)
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSLockOnLOS), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Owner);

	// Pawnは一時的に視線を横切る動的ユニットなので遮蔽物にしない。ただし単純なMultiTraceでは
	// PawnのBlockより奥にある壁を返さないため、命中Pawnだけを順次無視して同じ線分を再トレースする。
	// これにより「ユニットの奥に静的障害物」がある場合も、その障害物を確実に検出できる。
	for (;;)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
			Hit, TraceStart, TraceEnd, MSCollision::WeaponTraceChannel, QueryParams))
		{
			if (bLogNextSelectionDiagnostics)
			{
				UE_LOG(LogTemp, Display,
					TEXT("MSLockOnTrace: LOS target=%s clear start=%s end=%s"),
					*GetNameSafe(Target), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString());
			}
			return true;
		}

		const AActor* HitActor = Hit.GetActor();
		if (bLogNextSelectionDiagnostics)
		{
			const UPrimitiveComponent* HitComponent = Hit.GetComponent();
			UE_LOG(LogTemp, Display,
				TEXT("MSLockOnTrace: LOS target=%s hitActor=%s hitClass=%s component=%s "
					"profile=%s objectType=%d blocking=%s location=%s"),
				*GetNameSafe(Target), *GetNameSafe(HitActor),
				HitActor ? *GetNameSafe(HitActor->GetClass()) : TEXT("None"),
				*GetNameSafe(HitComponent),
				HitComponent ? *HitComponent->GetCollisionProfileName().ToString() : TEXT("None"),
				HitComponent ? static_cast<int32>(HitComponent->GetCollisionObjectType()) : -1,
				Hit.bBlockingHit ? TEXT("true") : TEXT("false"), *Hit.Location.ToCompactString());
		}
		if (HitActor == Target || (HitActor && HitActor->IsAttachedTo(Target)))
		{
			return true;
		}
		// A water PhysicsVolume is gameplay metadata, not visual cover. Daibao's water
		// brush reports a blocking WeaponTrace hit while the owner is inside it even
		// though its collision profile is OverlapAll. Continue the same LOS trace past
		// water volumes; ordinary PhysicsVolumes and world geometry remain occluders.
		if (ShouldIgnoreLineOfSightOccluder(HitActor))
		{
			QueryParams.AddIgnoredActor(HitActor);
			continue;
		}
		if (HitActor && HitActor->IsA<APawn>())
		{
			QueryParams.AddIgnoredActor(HitActor);
			continue;
		}

		// Pawn以外のBlock(地形・建物・構造物等)は実際の視線遮蔽。
		return false;
	}
}

bool UMSLockOnComponent::ShouldIgnoreLineOfSightOccluder(const AActor* HitActor)
{
	const APhysicsVolume* HitVolume = Cast<APhysicsVolume>(HitActor);
	return HitVolume && HitVolume->bWaterVolume;
}

FVector UMSLockOnComponent::GetOwnAimOrigin() const
{
	const AActor* Owner = GetOwner();
	if (const IMSLockOnTargetInterface* OwnerLockOn = Cast<IMSLockOnTargetInterface>(Owner))
	{
		// AMSUnitBase: カプセル底+140cm=胸元(Unity: losEyeHeight=1.4m)
		return OwnerLockOn->GetLockOnAimPoint();
	}
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void UMSLockOnComponent::SetLockedTarget(AActor* NewTarget)
{
	AActor* OldTarget = LockedTarget.Get();
	if (OldTarget == NewTarget && !LockedTarget.IsStale())
	{
		return;
	}

	LockedTarget = NewTarget;
	ManualTargetEpoch = GetTargetDeploymentEpoch(NewTarget);
	LockBreakElapsed = 0.0f;
	bRedLockOcclusionGraceActive = false;
	if (NewTarget != OldTarget)
	{
		RedLockSuppressedTarget.Reset();
	}
	UpdateRedLockState();
	if (NewTarget && NewTarget != OldTarget)
	{
		PlayLockOnConfirm(GetOwner());
	}
	OnLockOnTargetChanged.Broadcast(OldTarget, NewTarget);
	PublishManualLockSnapshot();
	SyncIncomingTargets();
}

void UMSLockOnComponent::ApplySoftAim(const float DeltaTime)
{
	if (!IsLockedOn() || SoftAimInterpSpeed <= 0.0f)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AController* Controller = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	if (!Controller || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	FRotator TargetRotation;
	if (!ComputeSoftAimLookRotation(TargetRotation))
	{
		return;
	}

	// 実カメラ→対象胸元の視線回転へ緩やかに吸い付ける(Unity: SetSoftAimTarget相当)
	const float InterpSpeed = SoftAimInterpSpeed * (bIsRedLocked ? RedAimInterpMultiplier : 1.0f);
	const float MaxCorrection = bIsRedLocked ? RedAimCorrectionMaxDegrees : NormalAimCorrectionMaxDegrees;
	const FRotator NewRotation = ComputeLimitedAimRotation(
		Controller->GetControlRotation(), TargetRotation, DeltaTime, InterpSpeed, MaxCorrection);
	Controller->SetControlRotation(NewRotation);
}
