#include "UI/MSHUDViewModel.h"
#include "UI/MSFactionPresentation.h"
#include "UI/MSMapTypes.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Data/MSWeaponData.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "EngineUtils.h"
#include "GameplayEffect.h"
#include "Engine/GameInstance.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerState.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSSupplyPack.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSIncomingLockComponent.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/MSWeaponComponent.h"
#include "Voice/MSVoiceCutInSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	struct FUnitBarCandidate
	{
		FMSHUDUnitBarSnapshot Snapshot;
		bool bHostile = false;
		float DistanceSquared = 0.0f;
		uint32 StableOrder = 0;
	};

	void SortAndLimitUnitBarCandidates(TArray<FUnitBarCandidate>& Candidates)
	{
		Candidates.StableSort([](const FUnitBarCandidate& A, const FUnitBarCandidate& B)
		{
			if (A.bHostile != B.bHostile)
			{
				return A.bHostile;
			}
			if (!FMath::IsNearlyEqual(A.DistanceSquared, B.DistanceSquared))
			{
				return A.DistanceSquared < B.DistanceSquared;
			}
			return A.StableOrder < B.StableOrder;
		});
		Candidates.SetNum(FMath::Min(Candidates.Num(), MSHUDPalette::MaxUnitBars), EAllowShrinking::No);
	}

	int32 CutInChannelIndex(const EMSVoiceCutInChannel Channel)
	{
		return Channel == EMSVoiceCutInChannel::Right ? 1 : 0;
	}

	FText GetVoiceTypeLetter(const EMSVoiceType VoiceType)
	{
		switch(VoiceType)
		{
		case EMSVoiceType::Serious:return FText::FromString(TEXT("真"));
		case EMSVoiceType::Shy:return FText::FromString(TEXT("内"));
		case EMSVoiceType::Hotblood:
		default:return FText::FromString(TEXT("熱"));
		}
	}
}

void UMSHUDViewModel::Initialize(APlayerController* P)
{
	if(UMSHitFeedbackSubsystem* Feedback=HitFeedbackSubsystem.Get()){Feedback->OnHitConfirmed.RemoveAll(this);Feedback->OnKillConfirmed.RemoveAll(this);}
	if(UMSAttributeSet_Vital* Vital=SubscribedVital.Get()){Vital->OnDamagedFrom.RemoveDynamic(this,&UMSHUDViewModel::HandleDamagedFrom);Vital->OnDeath.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerDeath);}
	if(UMSAttributeSet_Boost* Boost=SubscribedBoost.Get())Boost->OnOverheat.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerOverheat);
	Owner=P;Pawn.Reset();SubscribedVital.Reset();SubscribedBoost.Reset();HitFeedbackSubsystem.Reset();
	LastCapturePoint.Reset();CapturePawn.Reset();CaptureCompleteUntil=0;LastCaptureProgress=1;CaptureViewerTeam=255;
	BindVoiceCutInSubsystem(nullptr);
	if(P)
	{
		if(UGameInstance* GameInstance=P->GetGameInstance())
		{
			if(UMSHitFeedbackSubsystem* Feedback=GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
			{
				HitFeedbackSubsystem=Feedback;
				Feedback->OnHitConfirmed.AddUObject(this,&UMSHUDViewModel::HandleHitConfirmed);
				Feedback->OnKillConfirmed.AddUObject(this,&UMSHUDViewModel::HandleKillConfirmed);
			}
			BindVoiceCutInSubsystem(GameInstance->GetSubsystem<UMSVoiceCutInSubsystem>());
		}
	}
	ResolvePawn();
}

void UMSHUDViewModel::BeginDestroy()
{
	if(UMSHitFeedbackSubsystem* Feedback=HitFeedbackSubsystem.Get()){Feedback->OnHitConfirmed.RemoveAll(this);Feedback->OnKillConfirmed.RemoveAll(this);}
	if(UMSAttributeSet_Vital* Vital=SubscribedVital.Get()){Vital->OnDamagedFrom.RemoveDynamic(this,&UMSHUDViewModel::HandleDamagedFrom);Vital->OnDeath.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerDeath);}
	if(UMSAttributeSet_Boost* Boost=SubscribedBoost.Get())Boost->OnOverheat.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerOverheat);
	BindVoiceCutInSubsystem(nullptr);
	Super::BeginDestroy();
}

void UMSHUDViewModel::BindVoiceCutInSubsystem(UMSVoiceCutInSubsystem* InSubsystem)
{
	if(UMSVoiceCutInSubsystem* Previous=VoiceCutInSubsystem.Get())
	{
		Previous->OnCutInRequested.RemoveAll(this);
		Previous->OnCutInCleared.RemoveAll(this);
	}
	VoiceCutInSubsystem=InSubsystem;
	LastCutInRequests[0].Reset();
	LastCutInRequests[1].Reset();
	if(InSubsystem)
	{
		InSubsystem->OnCutInRequested.AddUObject(this,&UMSHUDViewModel::HandleVoiceCutInRequested);
		InSubsystem->OnCutInCleared.AddUObject(this,&UMSHUDViewModel::HandleVoiceCutInCleared);
	}
}

void UMSHUDViewModel::HandleVoiceCutInRequested(const FMSVoiceCutInRequest& Request)
{
	LastCutInRequests[CutInChannelIndex(Request.Channel)]=Request;
}

void UMSHUDViewModel::HandleVoiceCutInCleared(const EMSVoiceCutInChannel)
{
	// The request remains cached for the 0.3 second HUD fade-out after the subsystem's 3 second hold.
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSHUDViewModel::SetVoiceCutInSubsystemForTesting(UMSVoiceCutInSubsystem* InSubsystem)
{
	BindVoiceCutInSubsystem(InSubsystem);
}
#endif

FMSHUDCutInsSnapshot UMSHUDViewModel::GetCutIns()
{
	FMSHUDCutInsSnapshot Result;
	const double Now=GetCurrentTime();
	for(int32 Index=0;Index<2;++Index)
	{
		const EMSVoiceCutInChannel Channel=Index==0?EMSVoiceCutInChannel::Left:EMSVoiceCutInChannel::Right;
		FMSVoiceCutInRequest ActiveRequest;
		if(UMSVoiceCutInSubsystem* Subsystem=VoiceCutInSubsystem.Get();Subsystem&&Subsystem->GetActiveRequest(Channel,ActiveRequest))
		{
			LastCutInRequests[Index]=ActiveRequest;
		}
		FMSHUDCutInSnapshot& Snapshot=Index==0?Result.Left:Result.Right;
		if(!LastCutInRequests[Index].IsSet())continue;
		const FMSVoiceCutInRequest& Request=LastCutInRequests[Index].GetValue();
		Snapshot.Elapsed=static_cast<float>(FMath::Max(0.0,Now-Request.RequestTime));
		Snapshot.bVisible=Snapshot.Elapsed<CutInHoldSeconds+CutInFadeOutSeconds;
		Snapshot.SpeakerName=Request.SpeakerName;
		Snapshot.Line=Request.Line;
		Snapshot.PortraitColor=Request.PortraitColor;
		Snapshot.VoiceTypeLetter=GetVoiceTypeLetter(Request.VoiceType);
		if(!Snapshot.bVisible)LastCutInRequests[Index].Reset();
	}
	return Result;
}

AMSPlayerCharacter* UMSHUDViewModel::ResolvePawn()
{
	AMSPlayerCharacter* Current=Owner.IsValid()?Cast<AMSPlayerCharacter>(Owner->GetPawn()):nullptr;
	if(Pawn.Get()!=Current){Pawn=Current;RefreshVitalSubscription(Current);}
	return Current;
}

void UMSHUDViewModel::RefreshVitalSubscription(AMSPlayerCharacter* CurrentPawn)
{
	UMSAttributeSet_Vital* NewVital=CurrentPawn?CurrentPawn->GetVitalSet():nullptr;
	UMSAttributeSet_Boost* NewBoost=CurrentPawn?CurrentPawn->GetBoostSet():nullptr;
	if(SubscribedVital.Get()==NewVital&&SubscribedBoost.Get()==NewBoost)return;
	if(UMSAttributeSet_Vital* OldVital=SubscribedVital.Get()){OldVital->OnDamagedFrom.RemoveDynamic(this,&UMSHUDViewModel::HandleDamagedFrom);OldVital->OnDeath.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerDeath);}
	if(UMSAttributeSet_Boost* OldBoost=SubscribedBoost.Get())OldBoost->OnOverheat.RemoveDynamic(this,&UMSHUDViewModel::HandlePlayerOverheat);
	SubscribedVital=NewVital;
	SubscribedBoost=NewBoost;
	if(NewVital){NewVital->OnDamagedFrom.AddUniqueDynamic(this,&UMSHUDViewModel::HandleDamagedFrom);NewVital->OnDeath.AddUniqueDynamic(this,&UMSHUDViewModel::HandlePlayerDeath);}
	if(NewBoost)NewBoost->OnOverheat.AddUniqueDynamic(this,&UMSHUDViewModel::HandlePlayerOverheat);
}

double UMSHUDViewModel::GetCurrentTime() const
{
	const UWorld* World=Owner.IsValid()?Owner->GetWorld():nullptr;
	return World?World->GetTimeSeconds():0.0;
}

bool UMSHUDViewModel::IsLocalCombatActor(const AActor* Actor) const
{
	if(!IsValid(Actor)||!Pawn.IsValid())return false;
	if(Actor==Pawn.Get()||Actor==Owner.Get())return true;
	for(const AActor* Cursor=Actor->GetOwner();Cursor;Cursor=Cursor->GetOwner())if(Cursor==Pawn.Get()||Cursor==Owner.Get())return true;
	return Actor->GetInstigator()==Pawn.Get();
}

void UMSHUDViewModel::HandleHitConfirmed(AActor* Shooter,const bool bIsHeadshot)
{
	ResolvePawn();if(IsLocalCombatActor(Shooter))ProcessHitConfirmed(bIsHeadshot,GetCurrentTime());
}

void UMSHUDViewModel::HandleKillConfirmed(AActor* Killer)
{
	ResolvePawn();if(IsLocalCombatActor(Killer))ProcessKillConfirmed(GetCurrentTime());
}

void UMSHUDViewModel::HandleDamagedFrom(AActor* DamagedUnit,AActor*,const FVector AttackerLocation)
{
	AMSPlayerCharacter* Current=ResolvePawn();if(Current&&DamagedUnit==Current)ProcessDamageDirection(AttackerLocation,GetCurrentTime());
}

void UMSHUDViewModel::ProcessHitConfirmed(const bool bIsHeadshot,const double NowSeconds)
{
	bHitMarkerActive=true;bHitMarkerHeadshot=bIsHeadshot;HitMarkerStartTime=NowSeconds;
}

int32 UMSHUDViewModel::AdvanceKillStreak(const int32 CurrentStreak,const double PreviousKillTime,const double CurrentKillTime)
{
	return FMath::Max(1,CurrentStreak+1);
}

void UMSHUDViewModel::ProcessKillConfirmed(const double NowSeconds)
{
	KillStreak=FMath::Max(1,KillStreak+1);LastKillTime=NowSeconds;KillPopupStartTime=NowSeconds;bKillPopupActive=true;
	TotalScore+=MSHUDPalette::KillScorePoints;
}

void UMSHUDViewModel::ProcessPlayerDeath(){KillStreak=0;LastKillTime=-1.0e9;bKillPopupActive=false;}
void UMSHUDViewModel::HandlePlayerDeath(AActor* DeadUnit,AActor*){if(DeadUnit==Pawn.Get())ProcessPlayerDeath();}
void UMSHUDViewModel::HandlePlayerOverheat(AActor* Unit){if(Unit==Pawn.Get()){bBoostRecoveringFromOverheat=true;UE_LOG(LogTemp,Log,TEXT("[HUD] Boost overheat received for local pawn %s -> red while overheated, RECOVERY after tag release"),*GetNameSafe(Unit));}}

float UMSHUDViewModel::CalculateRelativeDamageAngle(const FVector& UnitLocation,const FRotator& UnitRotation,const FVector& AttackerLocation)
{
	const FVector LocalDirection=UnitRotation.UnrotateVector(AttackerLocation-UnitLocation);
	if(LocalDirection.IsNearlyZero())return 0.0f;
	return FRotator::ClampAxis(FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y,LocalDirection.X)));
}

float UMSHUDViewModel::CalculateIncomingLockRelativeAngle(const float DirectionYawDegrees,const float ViewYawDegrees)
{
	// The component publishes a receiver-to-source world bearing.  The HUD uses
	// the active camera, rather than the mech body, so looking around does not
	// leave an incoming arrow attached to the actor's old facing direction.
	return FMath::UnwindDegrees(DirectionYawDegrees-ViewYawDegrees);
}

void UMSHUDViewModel::ProcessDamageDirection(const FVector& AttackerLocation,const double NowSeconds)
{
	if(AMSPlayerCharacter* Current=ResolvePawn())
	{
		DamageDirectionAngle=CalculateRelativeDamageAngle(Current->GetActorLocation(),Current->GetActorRotation(),AttackerLocation);
		DamageDirectionStartTime=NowSeconds;bDamageDirectionActive=true;
	}
}

FMSHUDHitMarkerSnapshot UMSHUDViewModel::GetHitMarker() const
{
	FMSHUDHitMarkerSnapshot S;S.bHeadshot=bHitMarkerHeadshot;S.Elapsed=FMath::Max(0.0,GetCurrentTime()-HitMarkerStartTime);S.bVisible=bHitMarkerActive&&S.Elapsed<MSHUDPalette::HitMarkerDuration;return S;
}

FMSHUDKillPopupSnapshot UMSHUDViewModel::GetKillPopup() const
{
	FMSHUDKillPopupSnapshot S;S.Elapsed=FMath::Max(0.0,GetCurrentTime()-KillPopupStartTime);S.bVisible=bKillPopupActive&&S.Elapsed<MSHUDPalette::KillPopupDuration;S.Text=FText::FromString(TEXT("撃破!"));S.KillStreak=KillStreak;return S;
}

FMSHUDDamageDirectionSnapshot UMSHUDViewModel::GetDamageDirection() const
{
	FMSHUDDamageDirectionSnapshot S;S.Elapsed=FMath::Max(0.0,GetCurrentTime()-DamageDirectionStartTime);S.bVisible=bDamageDirectionActive&&S.Elapsed<MSHUDPalette::DamageDirectionDuration;S.RelativeAngleDegrees=DamageDirectionAngle;return S;
}
FMSHUDIncomingLockSnapshot UMSHUDViewModel::GetIncomingLock()
{
	FMSHUDIncomingLockSnapshot Result;
	const AMSPlayerCharacter* Player=ResolvePawn();
	if(!Player||!Owner.IsValid())return Result;
	if(const UMSRespawnComponent* Respawn=Player->GetRespawnComponent();Respawn&&Respawn->IsRespawnPending())return Result;
	const UMSIncomingLockComponent* Incoming=Player->GetIncomingLockComponent();
	if(!Incoming)return Result;
	const FMSIncomingLockSnapshot& IncomingSnapshot=Incoming->GetSnapshot();
	Result.bIncomingLock=IncomingSnapshot.bIncomingLock;
	Result.bMissilePrelockWarning=IncomingSnapshot.bIncomingMissile;
	Result.bVisible=Result.bIncomingLock||Result.bMissilePrelockWarning||!IncomingSnapshot.Entries.IsEmpty();
	if(!Result.bVisible)return Result;
	const float ViewYaw=Owner->PlayerCameraManager
		?Owner->PlayerCameraManager->GetCameraRotation().Yaw
		:Owner->GetControlRotation().Yaw;
	Result.DirectionIndicators.Reserve(FMath::Min(IncomingSnapshot.Entries.Num(),10));
	Result.DistanceIndicators.Reserve(2);
	for(const FMSIncomingLockEntry& Entry:IncomingSnapshot.Entries)
	{
		if(Result.DirectionIndicators.Num()>=10)break;
		FMSHUDIncomingLockIndicatorSnapshot Indicator;
		Indicator.RelativeAngleDegrees=CalculateIncomingLockRelativeAngle(Entry.DirectionYawDegrees,ViewYaw);
		Indicator.bHasDistance=Entry.bHasDistance&&FMath::IsFinite(Entry.DistanceMeters)&&Entry.DistanceMeters>=0.0f;
		Indicator.DistanceMeters=Indicator.bHasDistance?Entry.DistanceMeters:-1.0f;
		Result.DirectionIndicators.Add(Indicator);
		if(Indicator.bHasDistance&&Result.DistanceIndicators.Num()<2)Result.DistanceIndicators.Add(Indicator);
	}
	return Result;
}
float UMSHUDViewModel::SafeRatio(float V,float M){return M>0?FMath::Clamp(V/M,0.f,1.f):0.f;}
void UMSHUDViewModel::EvaluateDownGauge(float DP,float Capacity,bool bHeavy,float& P,FLinearColor& C)
{
	const bool bYellow=DP>=800.f;
	P=bHeavy?0.f:bYellow?SafeRatio(Capacity-DP,FMath::Max(1.f,Capacity-800.f)):SafeRatio(800.f-DP,800.f);
	C=bHeavy?FLinearColor(1.f,.3f,.12f):bYellow?FLinearColor(1.f,.78f,.12f):FLinearColor(.18f,.9f,.45f);
}
void UMSHUDViewModel::EvaluateDP(float R,bool S,bool K,float& P,FLinearColor& C){P=(S||K)?1.f:FMath::Clamp(R,0.f,1.f); C=(S||K||R>=MSHUDPalette::DPWarningThreshold)?MSHUDPalette::DPWarning:MSHUDPalette::DPNormal;}
FText UMSHUDViewModel::ComposeSlotStatus(bool R,bool E,const FText& A,bool Sel,bool L,const FText& T){FString S=R?TEXT("RELOAD"):(E?TEXT("EMPTY"):A.ToString()); if(Sel&&L) S+=FString::Printf(TEXT("  LOCK:%s"),*T.ToString()); return FText::FromString(S);}
FLinearColor UMSHUDViewModel::SelectUnitBarColor(bool bAlly){return bAlly?MSHUDPalette::AllyUnitBar:MSHUDPalette::EnemyUnitBar;}
FMSHUDLockOnMarkerSnapshot UMSHUDViewModel::MakeLockOnMarkerSnapshot(const AActor* Target,bool bProjected,const FVector2D& Position,float DistanceMeters,bool bRedLocked){FMSHUDLockOnMarkerSnapshot S;S.bVisible=IsValid(Target)&&bProjected;S.bRedLocked=S.bVisible&&bRedLocked;S.ScreenPosition=Position;S.DistanceMeters=DistanceMeters;return S;}
FMSHUDTaskSnapshot UMSHUDViewModel::MakeTaskSnapshot(const int32 UncapturedPointCount)
{
	FMSHUDTaskSnapshot S;
	S.bDefense=UncapturedPointCount<=0;
	S.MissionType=FText::FromString(S.bDefense?TEXT("防衛"):TEXT("攻撃"));
	S.MissionTypeEnglish=FText::FromString(S.bDefense?TEXT("DEFENCE"):TEXT("ATTACK"));
	S.TaskText=FText::FromString(S.bDefense?TEXT("本拠点を防衛せよ!"):TEXT("拠点を制圧せよ!"));
	return S;
}
FMSHUDBattleSnapshot UMSHUDViewModel::GetBattle()
{
	FMSHUDBattleSnapshot S;
	const UWorld* World=Owner.IsValid()?Owner->GetWorld():nullptr;
	const AMSGameState* State=World?World->GetGameState<AMSGameState>():nullptr;
	if(!State)return S;
	S.TimeText=State->GetFormattedRemainingTime();
	S.TeamAId=MSFactionPresentation::ResolveMatchTeam(Owner.Get());
	S.TeamBId=MSFactionPresentation::OpposingTeam(S.TeamAId);
	if(!MSTeam::IsValidCombatTeam(S.TeamAId))return S;
	S.bValid=true;
	const bool bEnemySide=S.TeamAId==MSTeam::EnemyTeamId();
	const float FriendlyGauge=bEnemySide?State->EnemyGauge:State->PlayerGauge;
	const float HostileGauge=bEnemySide?State->PlayerGauge:State->EnemyGauge;
	S.TeamARatio=SafeRatio(FriendlyGauge,State->MaxGauge);S.TeamBRatio=SafeRatio(HostileGauge,State->MaxGauge);
	S.Tactical=FMath::RoundToInt32(FriendlyGauge);
	return S;
}
FMSHUDArmorSnapshot UMSHUDViewModel::GetArmorNumbers()
{
	FMSHUDArmorSnapshot S;
	const AMSPlayerCharacter* P=ResolvePawn();const auto* V=P?P->GetVitalSet():nullptr;
	if(V){S.Armor=FMath::RoundToInt32(FMath::Max(0.0f,V->GetArmor()));S.MaxArmor=FMath::RoundToInt32(FMath::Max(0.0f,V->GetMaxArmor()));}
	return S;
}
FMSHUDShieldSnapshot UMSHUDViewModel::GetShield()
{
	FMSHUDShieldSnapshot S;
	const AMSPlayerCharacter* Player=ResolvePawn();
	const UMSShieldComponent* Shield=Player?Player->GetShieldComponent():nullptr;
	if(!Shield) return S;
	S.bSupported=Shield->GetShieldData()!=nullptr || Shield->GetMaxDurability()>0.0f;
	S.bEquipped=Shield->IsShieldEquipped();
	S.bBroken=Shield->IsShieldBroken();
	S.bGuarding=Shield->IsGuarding();
	S.Durability=FMath::Max(0.0f,Shield->GetCurrentDurability());
	S.MaxDurability=FMath::Max(0.0f,Shield->GetMaxDurability());
	S.Ratio=S.MaxDurability>0.0f?FMath::Clamp(S.Durability/S.MaxDurability,0.0f,1.0f):0.0f;
	return S;
}
FMSHUDTaskSnapshot UMSHUDViewModel::GetTask()
{
	const AMSPlayerCharacter* P=ResolvePawn();UWorld* W=P?P->GetWorld():nullptr;
	int32 Uncaptured=0;
	const bool bEnemySide=P && P->GetGenericTeamId()==MSTeam::EnemyTeamId();
	const EMSCaptureOwner FriendlyOwner=bEnemySide?EMSCaptureOwner::TeamEnemy:EMSCaptureOwner::TeamPlayer;
	if(W)for(TActorIterator<AMSCapturePoint> It(W);It;++It){const AMSCapturePoint* C=*It;if(Cast<AMSHomeBase>(C))continue;if(C->GetCurrentOwner()!=FriendlyOwner)++Uncaptured;}
	return MakeTaskSnapshot(Uncaptured);
}
FMSHUDScoreSnapshot UMSHUDViewModel::GetScore() const{FMSHUDScoreSnapshot S;S.Score=TotalScore;return S;}
FMSHUDModuleSnapshot UMSHUDViewModel::GetModule()
{
	FMSHUDModuleSnapshot Snapshot;
	const AMSPlayerCharacter* Player = ResolvePawn();
	const UMSModuleData* ModuleData = Player ? Player->GetEquippedModule() : nullptr;
	UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	if (!ModuleData || !ASC)
	{
		return Snapshot;
	}
	if (ModuleData->ModuleName.IsEmpty())
	{
		return Snapshot;
	}
	Snapshot.bVisible = true;
	Snapshot.ModuleName = ModuleData->ModuleName;

	auto GetRemaining = [ASC](const FGameplayTag& Tag)
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(Tag);
		const TArray<float> Times = ASC->GetActiveEffectsTimeRemaining(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags));
		float Result = 0.0f;
		for (const float Time : Times)
		{
			Result = FMath::Max(Result, Time);
		}
		return Result;
	};

	if (ASC->HasMatchingGameplayTag(MSTags::State_Module_EffectActive))
	{
		Snapshot.RemainingSeconds = GetRemaining(MSTags::State_Module_EffectActive);
		Snapshot.StateText = FText::Format(FText::FromString(TEXT("{0}  ACTIVE  {1}s")),
			Snapshot.ModuleName, FText::AsNumber(FMath::CeilToInt(Snapshot.RemainingSeconds)));
	}
	else if (ASC->HasMatchingGameplayTag(MSTags::State_Overheated))
	{
		Snapshot.RemainingSeconds = GetRemaining(MSTags::State_Overheated);
		Snapshot.StateText = FText::Format(FText::FromString(TEXT("{0}  OVERHEAT  {1}s")),
			Snapshot.ModuleName, FText::AsNumber(FMath::CeilToInt(Snapshot.RemainingSeconds)));
	}
	else if (ASC->HasMatchingGameplayTag(MSTags::State_Module_Cooldown))
	{
		Snapshot.RemainingSeconds = GetRemaining(MSTags::State_Module_Cooldown);
		Snapshot.StateText = FText::Format(FText::FromString(TEXT("{0}  COOLDOWN  {1}s")),
			Snapshot.ModuleName, FText::AsNumber(FMath::CeilToInt(Snapshot.RemainingSeconds)));
	}
	else
	{
		Snapshot.StateText = FText::Format(FText::FromString(TEXT("{0}  READY")), Snapshot.ModuleName);
	}
	return Snapshot;
}
FMSHUDModuleChargeSnapshot UMSHUDViewModel::GetModuleCharge()
{
	FMSHUDModuleChargeSnapshot Snapshot;
	if (const AMSPlayerCharacter* Player = ResolvePawn())
	{
		Snapshot.bVisible = Player->IsModuleChargeActive();
		Snapshot.Progress = FMath::Clamp(Player->GetModuleChargeProgress01(), 0.0f, 1.0f);
	}
	return Snapshot;
}
FMSHUDEmergencyRecallSnapshot UMSHUDViewModel::GetEmergencyRecall()
{
	FMSHUDEmergencyRecallSnapshot Snapshot;
	if (const AMSPlayerCharacter* Player = ResolvePawn())
	{
		// Death owns this screen area even when a final damage/cancel notice arrived.
		const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
		if ((ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead))
			|| (Player->GetVitalSet() && Player->GetVitalSet()->GetArmor() <= 0.0f)) return Snapshot;
		if (const UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent())
		{
			Snapshot.bActive = Recall->IsRecallActive();
			Snapshot.bPending = Recall->IsRequestPending();
			Snapshot.bAwaitingDeployment = Recall->IsAwaitingDeploy();
			Snapshot.RemainingSeconds = Snapshot.bActive
				? FMath::Max(0.0f, Recall->GetRecallTimeRemaining()) : 0.0f;
			Snapshot.DurationSeconds = Recall->GetRecallDuration();
			Snapshot.Progress = Recall->GetRecallProgress();
			Snapshot.CooldownSeconds = Recall->GetRecallCooldownRemaining();
			const EMSRecallNotice Notice = Recall->GetNotice();
			Snapshot.bVisible = Snapshot.bActive || Snapshot.bPending || Snapshot.CooldownSeconds > 0.0f
				|| Notice != EMSRecallNotice::None;
			if (Snapshot.bActive)
			{
				Snapshot.StatusText = FText::FromString(FString::Printf(TEXT("緊急帰投  残り %.1f 秒"), Snapshot.RemainingSeconds));
				// Repeated recall input is a no-op; never advertise a manual cancellation.
			}
			else if (Snapshot.bPending)
			{
				Snapshot.StatusText = FText::FromString(TEXT("緊急帰投を確認中…"));
				// Repeated recall input is a no-op; never advertise a manual cancellation.
			}
			else if (Snapshot.bAwaitingDeployment || Notice == EMSRecallNotice::Completed)
			{
				Snapshot.StatusText = FText::FromString(TEXT("帰投完了"));
				Snapshot.DetailText = FText::FromString(TEXT("出撃機体を選択してください"));
			}
			else
			{
				Snapshot.StatusText = Notice != EMSRecallNotice::None
					? UMSEmergencyRecallComponent::GetReasonText(Recall->GetNoticeReason())
					: FText::FromString(TEXT("緊急帰投"));
				if (Snapshot.CooldownSeconds > 0.0f)
					Snapshot.DetailText = FText::FromString(FString::Printf(TEXT("再試行まで %.1f 秒"), Snapshot.CooldownSeconds));
			}
		}
	}
	return Snapshot;
}
FMSHUDRespawnSnapshot UMSHUDViewModel::GetRespawn()
{
	FMSHUDRespawnSnapshot Snapshot;
	if (const AMSPlayerCharacter* Player = ResolvePawn())
	{
		if (const UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
		{
			Snapshot.bVisible = Respawn->IsRespawnPending();
			Snapshot.RemainingSeconds = Snapshot.bVisible
				? FMath::Max(0.0f, Respawn->GetRespawnTimeRemaining()) : 0.0f;
		}
	}
	return Snapshot;
}
FMSHUDMinimapProjection UMSHUDViewModel::ProjectWorldToMinimap(const FVector& WorldPosition,const FVector& CenterPosition,const float OrthoWidth,const FVector2D& MapSize)
{
	FMSHUDMinimapProjection Result;
	if(OrthoWidth<=0.0f||MapSize.X<=0.0f||MapSize.Y<=0.0f)return Result;
	const float HalfWidth=OrthoWidth*.5f;
	const float HalfHeight=HalfWidth*MapSize.Y/MapSize.X;
	const FVector Delta=(WorldPosition-CenterPosition).RotateAngleAxis(-MSMapPalette::WorldNorthYawDeg,FVector::UpVector);
	const float NormalizedX=Delta.Y/HalfWidth;
	const float NormalizedY=-Delta.X/HalfHeight;
	Result.bInBounds=FMath::Abs(NormalizedX)<=1.0f&&FMath::Abs(NormalizedY)<=1.0f;
	Result.Position=FVector2D(
		(FMath::Clamp(NormalizedX,-1.0f,1.0f)+1.0f)*.5f*MapSize.X,
		(FMath::Clamp(NormalizedY,-1.0f,1.0f)+1.0f)*.5f*MapSize.Y);
	return Result;
}
bool UMSHUDViewModel::IsInsideLocalMinimapCircle(const FMSHUDMinimapProjection& Projection,
	const FVector2D& MapSize,const float EdgePaddingPx)
{
	if(!Projection.bInBounds || MapSize.X<=0.0f || MapSize.Y<=0.0f)return false;
	const float Radius=FMath::Max(0.0f,FMath::Min(MapSize.X,MapSize.Y)*.5f-EdgePaddingPx);
	return (Projection.Position-MapSize*.5f).SizeSquared()<=FMath::Square(Radius);
}
FMSHUDMinimapUnitMarker UMSHUDViewModel::MakeMinimapUnitMarker(const FGenericTeamId ViewerTeam,const FGenericTeamId UnitTeam)
{
	FMSHUDMinimapUnitMarker Result;
	const ETeamAttitude::Type Attitude=UMSTeamStatics::SolveAttitude(ViewerTeam,UnitTeam);
	Result.bVisible=Attitude==ETeamAttitude::Friendly||Attitude==ETeamAttitude::Hostile;
	Result.bFriendly=Attitude==ETeamAttitude::Friendly;
	Result.Color=Attitude==ETeamAttitude::Friendly?MSMapPalette::Friendly:
		(Attitude==ETeamAttitude::Hostile?MSMapPalette::Enemy:FLinearColor::Transparent);
	return Result;
}
// ユーザー指定仕様(4回目の修正): OH中(State.Overheated=80%回復まで継続)は実回復量を赤で表示、
// 80%到達=タグ解除で通常色+使用可能。黄色の中間フェーズは廃止(Unity版もOH中赤/通常の2色)。
FLinearColor UMSHUDViewModel::EvaluateBoostColor(const float Ratio,const bool bOverheated,bool& bInOutRecovering){if(bOverheated){bInOutRecovering=true;return MSHUDPalette::BoostOverheated;}bInOutRecovering=false;return MSHUDPalette::BoostFill;}
FMSHUDGaugeSnapshot UMSHUDViewModel::GetGauges()
{
	FMSHUDGaugeSnapshot S;AMSPlayerCharacter* P=ResolvePawn();if(!P)return S;
	if(BoostRecoveryPawn.Get()!=P){BoostRecoveryPawn=P;bBoostRecoveringFromOverheat=false;}
	const auto* V=P->GetVitalSet();const auto* B=P->GetBoostSet();auto* A=P->GetAbilitySystemComponent();
	S.bStaggered=A&&A->HasMatchingGameplayTag(MSTags::State_Staggered);S.bKnockedDown=A&&A->HasMatchingGameplayTag(MSTags::State_KnockedDown);
	S.bFlinched=A&&A->HasMatchingGameplayTag(MSTags::State_Flinched);S.bGettingUp=A&&A->HasMatchingGameplayTag(MSTags::State_GettingUp);
	if(V){S.Armor=SafeRatio(V->GetArmor(),V->GetMaxArmor());EvaluateDownGauge(V->GetDownPoint(),V->GetMaxDownPoint(),S.bStaggered||S.bKnockedDown||S.bGettingUp,S.DP,S.DPColor);}
	if(B)S.Boost=SafeRatio(B->GetBoost(),B->GetMaxBoost());
	const bool bOverheated=(A&&A->HasMatchingGameplayTag(MSTags::State_Overheated))||(B&&B->GetBoost()<=UE_KINDA_SMALL_NUMBER&&bBoostRecoveringFromOverheat);
	if(bOverheated)
	{
		S.BoostColor=EvaluateBoostColor(S.Boost,true,bBoostRecoveringFromOverheat);
	}
	else if(bBoostRecoveringFromOverheat&&S.Boost<1.0f-KINDA_SMALL_NUMBER)
	{
		// OHタグ解除後から満タンまでを、バー内RECOVERY表示用の内部状態色で伝える。
		S.BoostColor=MSHUDPalette::BoostRecovering;
	}
	else
	{
		S.BoostColor=EvaluateBoostColor(S.Boost,false,bBoostRecoveringFromOverheat);
	}
	return S;
}
TArray<FMSHUDWeaponSlotSnapshot> UMSHUDViewModel::GetWeaponSlots()
{
	TArray<FMSHUDWeaponSlotSnapshot> Out;
	const AMSPlayerCharacter* Player=ResolvePawn();
	const UMSWeaponComponent* Weapons=Player?Player->GetWeaponComponent():nullptr;
	const UMSLockOnComponent* LockOn=Player?Player->GetLockOnComponent():nullptr;
	if(!Weapons)return Out;
	Out.Reserve(Weapons->GetSlotCount());
	for(int32 Index=0;Index<Weapons->GetSlotCount();++Index)
	{
		FMSHUDWeaponSlotSnapshot Snapshot;
		const UMSWeaponDataBase* Data=Weapons->GetWeaponData(Index);
		const FGameplayTag Category=Data?Data->CategoryTag:FGameplayTag();
		Snapshot.bHasWeapon=Data!=nullptr;
		Snapshot.Name=Data?Data->WeaponName:FText::FromString(TEXT("EMPTY"));
		Snapshot.Icon=FMSHUDTypes::GetIconStyle(Category);
		Snapshot.FireMode=Data?FMSHUDTypes::GetFireModeLabel(Category):FText::GetEmpty();
		Snapshot.bSelected=Index==Weapons->GetActiveSlotIndex();
		Snapshot.bReloading=Weapons->IsReloading(Index);
		Snapshot.ReloadRemaining=Weapons->GetReloadRemaining(Index);
		Snapshot.ReloadDuration=Weapons->GetReloadDuration(Index);
		Snapshot.ReloadProgress=Weapons->GetReloadProgress01(Index);
		Snapshot.Ammo=Weapons->GetAmmoDisplay(Index);
		const FText Target=LockOn&&LockOn->GetLockedTarget()?FText::FromString(LockOn->GetLockedTarget()->GetName()):FText::GetEmpty();
		Snapshot.Status=ComposeSlotStatus(Snapshot.bReloading,Weapons->IsEmpty(Index),Snapshot.Ammo,
			Snapshot.bSelected,LockOn&&LockOn->IsLockedOn(),Target);
		Out.Add(MoveTemp(Snapshot));
	}
	return Out;
}
float UMSHUDViewModel::CalculateAmmoArcProgress(const int32 CurrentAmmo,const int32 MaximumAmmo,const bool bReloading,const float ReloadProgress)
{
	if(MaximumAmmo<=0)return 0.0f;
	const float CurrentRatio=FMath::Clamp(static_cast<float>(CurrentAmmo)/static_cast<float>(MaximumAmmo),0.0f,1.0f);
	return bReloading?FMath::Lerp(CurrentRatio,1.0f,FMath::Clamp(ReloadProgress,0.0f,1.0f)):CurrentRatio;
}

FMSHUDCombatSnapshot UMSHUDViewModel::GetCombat()
{
	FMSHUDCombatSnapshot S;const AMSPlayerCharacter* P=ResolvePawn();const UMSWeaponComponent* W=P?P->GetWeaponComponent():nullptr;const UMSLockOnComponent* L=P?P->GetLockOnComponent():nullptr;
	if(!W)return S;
	const int32 I=W->GetActiveSlotIndex();const UMSWeaponDataBase* D=W->GetWeaponData(I);
	S.bSwitching=W->IsSwitching();S.bReloading=I>=0&&W->IsReloading(I);
	S.ReloadRemaining=W->GetReloadRemaining(I);S.ReloadDuration=W->GetReloadDuration(I);S.ReloadProgress=W->GetReloadProgress01(I);
	S.bAmmoArcVisible=D&&!D->HasInfiniteAmmo();
	S.AmmoArcProgress=CalculateAmmoArcProgress(W->GetMagazineAmmo(I),D?D->MagazineSize:0,S.bReloading,S.ReloadProgress);
	S.bHoming=D&&D->CategoryTag==MSTags::Weapon_Category_Homing;S.bLocked=L&&L->IsLockedOn();S.bHomingTargetInCircle=L&&L->GetHomingCandidate()!=nullptr;S.bHomingCaptured=L&&L->IsHomingTargetCaptured();S.DwellProgress=L?L->GetHomingDwellProgress():0.0f;S.bCharging=P&&P->IsChargeFireActive();S.ChargeProgress=P?P->GetChargeFireProgress01():0.0f;S.bScoped=P&&P->GetMSCamera()&&P->GetMSCamera()->IsScoped();return S;
}
FMSHUDCaptureSnapshot UMSHUDViewModel::GetCapture()
{
	FMSHUDCaptureSnapshot S;
	AMSPlayerCharacter* P=ResolvePawn(); UWorld* W=P?P->GetWorld():nullptr;
	if(!W || P->IsHidden()){LastCapturePoint.Reset();CaptureCompleteUntil=0;return S;}
	const UAbilitySystemComponent* ASC=P->GetAbilitySystemComponent();
	if((ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead)) || (P->GetVitalSet() && P->GetVitalSet()->GetArmor()<=0.0f)){LastCapturePoint.Reset();CaptureCompleteUntil=0;return S;}
	const FGenericTeamId ViewerTeam=UMSTeamStatics::GetTeamId(P);
	if(ViewerTeam!=MSTeam::PlayerTeamId() && ViewerTeam!=MSTeam::EnemyTeamId()) return S;
	const bool bEnemySide=ViewerTeam==MSTeam::EnemyTeamId();
	if(CapturePawn.Get()!=P || CaptureViewerTeam!=ViewerTeam.GetId())
	{CapturePawn=P;CaptureViewerTeam=ViewerTeam.GetId();LastCapturePoint.Reset();CaptureCompleteUntil=0;}
	const double Now=W->GetTimeSeconds();
	if(AMSCapturePoint* Previous=LastCapturePoint.Get())
	{
		const float Progress=Previous->GetProgress()*(bEnemySide?-1.f:1.f);
		if(LastCaptureProgress<1.f-KINDA_SMALL_NUMBER && Progress>=1.f-KINDA_SMALL_NUMBER && Previous->GetOwnerTeamId()==ViewerTeam)
			CaptureCompleteUntil=Now+1.8;
		LastCaptureProgress=Progress;
		if(Now<CaptureCompleteUntil && Progress>=1.f-KINDA_SMALL_NUMBER)
		{S.bVisible=true;S.bCompleted=true;S.Progress=1;S.Name=Previous->GetPointName();S.State=FText::FromString(TEXT("制圧完了"));return S;}
	}
	AMSCapturePoint* Nearest=nullptr; float NearestDistance=MAX_flt;
	for(TActorIterator<AMSCapturePoint> It(W);It;++It)
	{
		AMSCapturePoint* C=*It;
		if(Cast<AMSHomeBase>(C) || C->IsCaptureLocked()) continue;
		const float Distance=FVector::DistSquared2D(P->GetActorLocation(),C->GetActorLocation());
		if(Distance>FMath::Square(C->GetCaptureRadius())) continue;
		const float RelativeProgress=C->GetProgress()*(bEnemySide?-1.0f:1.0f);
		if(C->GetOwnerTeamId()==ViewerTeam && RelativeProgress>=1.0f-KINDA_SMALL_NUMBER) continue;
		if(Distance<NearestDistance){Nearest=C;NearestDistance=Distance;}
	}
	if(!Nearest){LastCapturePoint.Reset();CaptureCompleteUntil=0;return S;}
	const float RelativeProgress=Nearest->GetProgress()*(bEnemySide?-1.0f:1.0f);
	LastCapturePoint=Nearest;LastCaptureProgress=RelativeProgress;
	S.bVisible=true;
	// MSGO6: hostile fill shrinks to zero before friendly fill grows. Neutral is not half full.
	S.Progress=FMath::Clamp(FMath::Abs(RelativeProgress),0.0f,1.0f);
	S.bHostileProgress=RelativeProgress<0.0f;
	S.Name=Nearest->GetPointName(); S.State=FText::FromString(TEXT("制圧中"));
	if(const UMSUnitRegistrySubsystem* Registry=W->GetSubsystem<UMSUnitRegistrySubsystem>())
	{
		const FVector Location=Nearest->GetActorLocation();
		S.bContested=Registry->CountUnitsOfTeamInRange(MSTeam::PlayerTeamId(),Location,Nearest->GetCaptureRadius())>0
			&& Registry->CountUnitsOfTeamInRange(MSTeam::EnemyTeamId(),Location,Nearest->GetCaptureRadius())>0;
	}
	return S;
}
FMSHUDLockOnMarkerSnapshot UMSHUDViewModel::GetLockOnMarker(){auto* P=ResolvePawn();auto* L=P?P->GetLockOnComponent():nullptr;AActor* Target=L?L->GetLockedTarget():nullptr;FVector2D Position;bool bProjected=false;float DistanceMeters=0;if(Target&&Owner.IsValid()){FVector Aim=Target->GetActorLocation();if(const auto* LockTarget=Cast<IMSLockOnTargetInterface>(Target))Aim=LockTarget->GetLockOnAimPoint();bProjected=UGameplayStatics::ProjectWorldToScreen(Owner.Get(),Aim,Position,false);if(bProjected)Position/=FMath::Max(UWidgetLayoutLibrary::GetViewportScale(Owner.Get()),KINDA_SMALL_NUMBER);if(P)DistanceMeters=WorldDistanceToMeters(FVector::Dist(P->GetActorLocation(),Target->GetActorLocation()));}return MakeLockOnMarkerSnapshot(Target,bProjected,Position,DistanceMeters,L&&L->IsRedLocked());}
FMSHUDHomingTargetSnapshot UMSHUDViewModel::GetHomingTargetMarker(){FMSHUDHomingTargetSnapshot S;auto* P=ResolvePawn();auto* L=P?P->GetLockOnComponent():nullptr;AActor* Target=L?L->GetHomingCandidate():nullptr;if(!Target||!Owner.IsValid())return S;FVector Aim=Target->GetActorLocation();if(const auto* LockTarget=Cast<IMSLockOnTargetInterface>(Target))Aim=LockTarget->GetLockOnAimPoint();S.bVisible=UGameplayStatics::ProjectWorldToScreen(Owner.Get(),Aim,S.ScreenPosition,false);if(S.bVisible)S.ScreenPosition/=FMath::Max(UWidgetLayoutLibrary::GetViewportScale(Owner.Get()),KINDA_SMALL_NUMBER);S.Progress=FMath::Clamp(L->GetHomingDwellProgress(),0.0f,1.0f);S.bCaptured=L->IsHomingTargetCaptured();return S;}
FLinearColor UMSHUDViewModel::SelectHomingCursorColor(const bool bTargetInCircle){return bTargetInCircle?MSHUDPalette::HomingCursorTracking:MSHUDPalette::HomingCursorIdle;}
TArray<FMSHUDUnitBarSnapshot> UMSHUDViewModel::GetUnitBars()
{
	TArray<FMSHUDUnitBarSnapshot> Out;
	AMSPlayerCharacter* P=ResolvePawn();
	UWorld* W=P?P->GetWorld():nullptr;
	if(!P||!W||!Owner.IsValid())return Out;

	const FGenericTeamId PlayerTeam=UMSTeamStatics::GetTeamId(P);
	const float Scale=FMath::Max(UWidgetLayoutLibrary::GetViewportScale(Owner.Get()),KINDA_SMALL_NUMBER);
	TArray<FUnitBarCandidate> Candidates;
	uint32 StableOrder=0;
	if(UMSUnitRegistrySubsystem* Registry=W->GetSubsystem<UMSUnitRegistrySubsystem>())
	{
		Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
		{
			if(Unit==P)return;
			const UAbilitySystemComponent* ASC=Unit->GetAbilitySystemComponent();
			if(!ASC||ASC->HasMatchingGameplayTag(MSTags::State_Dead))return;

			FVector2D Position;
			if(!UGameplayStatics::ProjectWorldToScreen(Owner.Get(),
				Unit->GetActorLocation()+FVector(0,0,MSHUDPalette::UnitBarWorldOffsetZ),Position,false))return;

			const bool bAlly=UMSTeamStatics::IsOnTeam(Unit,PlayerTeam);
			const UMSAttributeSet_Vital* Vital=Unit->GetVitalSet();
			FUnitBarCandidate& Candidate=Candidates.AddDefaulted_GetRef();
			Candidate.Snapshot.ScreenPosition=Position/Scale;
			Candidate.Snapshot.ArmorRatio=Vital?SafeRatio(Vital->GetArmor(),Vital->GetMaxArmor()):0;
			Candidate.Snapshot.FillColor=SelectUnitBarColor(bAlly);
			Candidate.bHostile=!bAlly;
			Candidate.DistanceSquared=FVector::DistSquared(P->GetActorLocation(),Unit->GetActorLocation());
			Candidate.StableOrder=StableOrder++;
		});
	}

	// The default match has 11 other allies and 12 enemies. Registry insertion order must not let
	// allies consume the fixed 16-widget pool before all standard-match enemies are considered.
	SortAndLimitUnitBarCandidates(Candidates);
	Out.Reserve(Candidates.Num());
	for(FUnitBarCandidate& Candidate:Candidates)Out.Add(MoveTemp(Candidate.Snapshot));
	return Out;
}
FMSHUDSupplySnapshot UMSHUDViewModel::GetSupply(){FMSHUDSupplySnapshot S; if(auto* P=ResolvePawn())if(auto* Pack=P->GetNearbySupplyPack()){S.bVisible=true;S.Prompt=FText::FromString(Pack->SupplyPackType==EMSSupplyPackType::Repair?TEXT("[G] リペアパックを回収"):TEXT("[G] 弾薬パックを回収"));}return S;}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDUnitBarEnemyPriorityTest,
	"NeoMSGO.UI.HUD.UnitBars.EnemyPriorityAtCapacity",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMSHUDUnitBarEnemyPriorityTest::RunTest(const FString&)
{
	TArray<FUnitBarCandidate> Candidates;
	uint32 StableOrder=0;
	auto AddCandidate=[&Candidates,&StableOrder](const bool bHostile,const float DistanceSquared)
	{
		FUnitBarCandidate& Candidate=Candidates.AddDefaulted_GetRef();
		Candidate.bHostile=bHostile;
		Candidate.DistanceSquared=DistanceSquared;
		Candidate.StableOrder=StableOrder++;
		Candidate.Snapshot.FillColor=bHostile?MSHUDPalette::EnemyUnitBar:MSHUDPalette::AllyUnitBar;
	};

	// Reproduce the default registration order: local player, 11 allies, then 12 enemies.
	// The former first-16 policy retained only five of these enemies.
	for(int32 Index=0;Index<11;++Index)AddCandidate(false,FMath::Square(100.0f+Index));
	for(int32 Index=0;Index<12;++Index)AddCandidate(true,FMath::Square(1000.0f-Index*10.0f));
	SortAndLimitUnitBarCandidates(Candidates);

	TestEqual(TEXT("fixed widget capacity is respected"),Candidates.Num(),MSHUDPalette::MaxUnitBars);
	int32 EnemyCount=0;
	for(const FUnitBarCandidate& Candidate:Candidates)if(Candidate.bHostile)++EnemyCount;
	TestEqual(TEXT("all 12 standard-match enemies survive the 16-bar limit"),EnemyCount,12);
	TestEqual(TEXT("remaining four slots are available to allies"),Candidates.Num()-EnemyCount,4);
	TestTrue(TEXT("the nearest enemy is ordered first"),Candidates[0].bHostile&&
		FMath::IsNearlyEqual(Candidates[0].DistanceSquared,FMath::Square(890.0f)));
	return true;
}
#endif
