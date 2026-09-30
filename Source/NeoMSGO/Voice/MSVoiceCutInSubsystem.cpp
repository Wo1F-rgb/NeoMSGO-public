// Copyright NeoMSGO Project. All Rights Reserved.

#include "Voice/MSVoiceCutInSubsystem.h"

#include "AI/MSBotCharacter.h"
#include "Audio/MSAudioPlayback.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSGameInstance.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSTeamStatics.h"
#include "EngineUtils.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Voice/MSVoiceLibrary.h"
#include "Voice/MSVoiceSettings.h"
#include "Sound/SoundBase.h"

namespace
{
	int32 ChannelIndex(const EMSVoiceCutInChannel Channel)
	{
		return Channel == EMSVoiceCutInChannel::Right ? 1 : 0;
	}

	USoundBase* GetVoiceBlipSound()
	{
		static TWeakObjectPtr<USoundBase> CachedSound;
		if (!CachedSound.IsValid())
		{
			CachedSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/MS_Beep.MS_Beep"));
		}
		return CachedSound.Get();
	}
}

int32 MSVoiceCutInRules::GetPriority(const EMSVoiceTrigger Trigger)
{
	switch (Trigger)
	{
	case EMSVoiceTrigger::Death: return 5;
	case EMSVoiceTrigger::KillStreak: return 4;
	case EMSVoiceTrigger::Kill:
	case EMSVoiceTrigger::CaptureBase: return 3;
	case EMSVoiceTrigger::Sortie:
	case EMSVoiceTrigger::Deployed:
	case EMSVoiceTrigger::EmergencyRecall: return 2;
	case EMSVoiceTrigger::Repair:
	case EMSVoiceTrigger::SupplyPickup: return 1;
	case EMSVoiceTrigger::Damaged:
	default: return 0;
	}
}

double MSVoiceCutInRules::GetCooldownSeconds(const EMSVoiceTrigger Trigger)
{
	switch (Trigger)
	{
	case EMSVoiceTrigger::Damaged: return 8.0;
	case EMSVoiceTrigger::Kill: return 4.0;
	case EMSVoiceTrigger::Death: return 10.0;
	case EMSVoiceTrigger::Repair: return 20.0;
	case EMSVoiceTrigger::SupplyPickup: return 10.0;
	default: return 0.0;
	}
}

float MSVoiceCutInRules::GetBlipFrequency(const EMSVoiceType Type)
{
	switch (Type)
	{
	case EMSVoiceType::Serious: return 880.0f;
	case EMSVoiceType::Shy: return 1100.0f;
	case EMSVoiceType::Hotblood:
	default: return 660.0f;
	}
}

bool MSVoiceCutInRules::ShouldReplace(const int32 CurrentPriority, const int32 IncomingPriority)
{
	return IncomingPriority > CurrentPriority;
}

EMSVoiceType MSVoiceCutInRules::ResolveBotVoiceType(const FString& UnitName)
{
	const uint32 Hash = GetTypeHash(UnitName);
	return static_cast<EMSVoiceType>(Hash % 3u);
}

bool MSVoiceCutInRules::IsKillStreak(const TArrayView<const double> PreviousKillTimes, const double Now,
	const int32 RequiredKills, const double WindowSeconds)
{
	if (RequiredKills <= 1)
	{
		return true;
	}
	int32 RecentKills = 1; // Include the kill currently being processed.
	for (const double Time : PreviousKillTimes)
	{
		if (Time <= Now && Now - Time <= WindowSeconds)
		{
			++RecentKills;
		}
	}
	return RecentKills >= RequiredKills;
}

void UMSVoiceCutInSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMSHitFeedbackSubsystem>();
	if (UGameInstance* GI = GetGameInstance())
	{
		HitFeedbackSubsystem = GI->GetSubsystem<UMSHitFeedbackSubsystem>();
		if (UMSHitFeedbackSubsystem* Feedback = HitFeedbackSubsystem.Get())
		{
			Feedback->OnKillConfirmed.AddUObject(this, &UMSVoiceCutInSubsystem::HandleKillConfirmed);
		}
	}
	RefreshBindings();
}

void UMSVoiceCutInSubsystem::Deinitialize()
{
	if (UMSHitFeedbackSubsystem* Feedback = HitFeedbackSubsystem.Get())
	{
		Feedback->OnKillConfirmed.RemoveAll(this);
	}
	for (const TWeakObjectPtr<AMSPlayerCharacter>& PlayerPtr : BoundPlayers)
	{
		if (AMSPlayerCharacter* Player = PlayerPtr.Get())
		{
			if (UMSAttributeSet_Vital* Vital = Player->GetVitalSet())
			{
				Vital->OnDeath.RemoveDynamic(this, &UMSVoiceCutInSubsystem::HandleUnitDeath);
				Vital->OnDamagedFrom.RemoveDynamic(this, &UMSVoiceCutInSubsystem::HandleUnitDamaged);
			}
			if (UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
			{
				Respawn->OnDeployed.RemoveDynamic(this, &UMSVoiceCutInSubsystem::HandleDeployed);
			}
		}
	}
	for (const TWeakObjectPtr<AMSCapturePoint>& PointPtr : BoundCapturePoints)
	{
		if (AMSCapturePoint* Point = PointPtr.Get())
		{
			Point->OnCaptureOwnerChanged.RemoveAll(this);
		}
	}
	for (const TWeakObjectPtr<AMSRepairPod>& PodPtr : BoundRepairPods)
	{
		if (AMSRepairPod* Pod = PodPtr.Get()) Pod->OnPlayerRepaired.RemoveAll(this);
	}
	for (const TWeakObjectPtr<AMSSupplyPack>& PackPtr : BoundSupplyPacks)
	{
		if (AMSSupplyPack* Pack = PackPtr.Get()) Pack->OnSupplyPickedUp.RemoveAll(this);
	}
	for (const TWeakObjectPtr<UMSEmergencyRecallComponent>& RecallPtr : BoundRecallComponents)
	{
		if (UMSEmergencyRecallComponent* Recall = RecallPtr.Get()) Recall->OnRecallStarted.RemoveAll(this);
	}
	BoundPlayers.Reset();
	BoundCapturePoints.Reset();
	BoundRepairPods.Reset();
	BoundSupplyPacks.Reset();
	BoundRecallComponents.Reset();
	Super::Deinitialize();
}

void UMSVoiceCutInSubsystem::Tick(float)
{
	RefreshBindings();
	ClearExpiredRequests(GetNow());
}

TStatId UMSVoiceCutInSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMSVoiceCutInSubsystem, STATGROUP_Tickables);
}

UWorld* UMSVoiceCutInSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

bool UMSVoiceCutInSubsystem::GetActiveRequest(const EMSVoiceCutInChannel Channel,
	FMSVoiceCutInRequest& OutRequest) const
{
	const FChannelState& State = Channels[ChannelIndex(Channel)];
	if (!State.Request.IsSet())
	{
		return false;
	}
	OutRequest = State.Request.GetValue();
	return true;
}

void UMSVoiceCutInSubsystem::ProcessKillConfirmed(AActor* Killer, AMSPlayerCharacter* LocalPlayer, const double Now)
{
	if (!IsValid(Killer) || !IsValid(LocalPlayer))
	{
		return;
	}
	if (Killer == LocalPlayer)
	{
		const bool bStreak = MSVoiceCutInRules::IsKillStreak(LocalKillTimes, Now);
		LocalKillTimes.RemoveAll([Now](const double Time) { return Now - Time > 10.0; });
		LocalKillTimes.Add(Now);
		const EMSVoiceTrigger Trigger = bStreak ? EMSVoiceTrigger::KillStreak : EMSVoiceTrigger::Kill;
		SubmitRequest(MakeRequest(Killer, LocalPlayer, Trigger, EMSVoiceCutInChannel::Left, Now), Now);
		return;
	}
	if (Cast<AMSBotCharacter>(Killer) && UMSTeamStatics::GetAttitude(LocalPlayer, Killer) == ETeamAttitude::Friendly &&
		FVector::DistSquared(LocalPlayer->GetActorLocation(), Killer->GetActorLocation()) <= FMath::Square(FriendlyBotKillRangeCm))
	{
		SubmitRequest(MakeRequest(Killer, LocalPlayer, EMSVoiceTrigger::Kill,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}

void UMSVoiceCutInSubsystem::ProcessUnitDeath(AActor* DeadUnit, AActor* Killer,
	AMSPlayerCharacter* LocalPlayer, const double Now)
{
	if (!IsValid(LocalPlayer) || DeadUnit != LocalPlayer)
	{
		return;
	}
	SubmitRequest(MakeRequest(LocalPlayer, LocalPlayer, EMSVoiceTrigger::Death,
		EMSVoiceCutInChannel::Left, Now), Now);
	if (IsValid(Killer) && Killer != LocalPlayer)
	{
		FMSVoiceCutInRequest EnemyKill = MakeRequest(Killer, LocalPlayer, EMSVoiceTrigger::Kill,
			EMSVoiceCutInChannel::Right, Now, 5);
		TrySubmitRequest(EnemyKill, EMSVoiceTrigger::Death, Now);
	}
}

void UMSVoiceCutInSubsystem::ProcessUnitDamaged(AActor* DamagedUnit, AActor*,
	AMSPlayerCharacter* LocalPlayer, const double Now)
{
	if (IsValid(LocalPlayer) && DamagedUnit == LocalPlayer)
	{
		SubmitRequest(MakeRequest(LocalPlayer, LocalPlayer, EMSVoiceTrigger::Damaged,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}

bool UMSVoiceCutInSubsystem::SubmitRequest(const FMSVoiceCutInRequest& Request, const double Now)
{
	return TrySubmitRequest(Request, Request.Trigger, Now);
}

EMSVoiceType UMSVoiceCutInSubsystem::ResolveVoiceType(const AActor* Speaker,
	const AMSPlayerCharacter* LocalPlayer, const UGameInstance* GameInstance)
{
	if (Speaker && Speaker == LocalPlayer)
	{
		if (const UMSGameInstance* MSGI = Cast<UMSGameInstance>(GameInstance))
		{
			return MSGI->SelectedVoiceType;
		}
		return EMSVoiceType::Hotblood;
	}
	const FString SpeakerName = Speaker ? Speaker->GetName() : TEXT("Unknown");
	return MSVoiceCutInRules::ResolveBotVoiceType(SpeakerName);
}

FLinearColor UMSVoiceCutInSubsystem::GetPortraitColor(const EMSVoiceType Type)
{
	switch (Type)
	{
	case EMSVoiceType::Serious: return FLinearColor(0.15f, 0.55f, 1.0f, 1.0f);
	case EMSVoiceType::Shy: return FLinearColor(0.18f, 0.72f, 0.38f, 1.0f);
	case EMSVoiceType::Hotblood:
	default: return FLinearColor(1.0f, 0.2f, 0.08f, 1.0f);
	}
}

double UMSVoiceCutInSubsystem::GetNow() const
{
	return GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : FPlatformTime::Seconds();
}

AMSPlayerCharacter* UMSVoiceCutInSubsystem::ResolveLocalPlayer() const
{
	return GetWorld() ? Cast<AMSPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0)) : nullptr;
}

void UMSVoiceCutInSubsystem::RefreshBindings()
{
	UWorld* World = GetWorld();
	AMSPlayerCharacter* Player = ResolveLocalPlayer();
	if (!World || !Player)
	{
		return;
	}
	if (!BoundPlayers.Contains(Player))
	{
		BoundPlayers.Add(Player);
		if (UMSAttributeSet_Vital* Vital = Player->GetVitalSet())
		{
			Vital->OnDeath.AddUniqueDynamic(this, &UMSVoiceCutInSubsystem::HandleUnitDeath);
			Vital->OnDamagedFrom.AddUniqueDynamic(this, &UMSVoiceCutInSubsystem::HandleUnitDamaged);
		}
		if (UMSRespawnComponent* Respawn = Player->GetRespawnComponent())
		{
			Respawn->OnDeployed.AddUniqueDynamic(this, &UMSVoiceCutInSubsystem::HandleDeployed);
		}
		if (UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent())
		{
			Recall->OnRecallStarted.AddUObject(this, &UMSVoiceCutInSubsystem::HandleRecallStarted);
			BoundRecallComponents.Add(Recall);
		}
	}
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
	{
		if (!BoundCapturePoints.Contains(*It))
		{
			It->OnCaptureOwnerChanged.AddUniqueDynamic(this, &UMSVoiceCutInSubsystem::HandleCaptureOwnerChanged);
			BoundCapturePoints.Add(*It);
		}
	}
	for (TActorIterator<AMSRepairPod> It(World); It; ++It)
	{
		if (!BoundRepairPods.Contains(*It))
		{
			It->OnPlayerRepaired.AddUObject(this, &UMSVoiceCutInSubsystem::HandlePlayerRepaired);
			BoundRepairPods.Add(*It);
		}
	}
	for (TActorIterator<AMSSupplyPack> It(World); It; ++It)
	{
		if (!BoundSupplyPacks.Contains(*It))
		{
			It->OnSupplyPickedUp.AddUObject(this, &UMSVoiceCutInSubsystem::HandleSupplyPickedUp);
			BoundSupplyPacks.Add(*It);
		}
	}
}

void UMSVoiceCutInSubsystem::ClearExpiredRequests(const double Now)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
	{
		FChannelState& State = Channels[Index];
		if (State.Request.IsSet() && Now >= State.DisplayUntil)
		{
			State.Request.Reset();
			OnCutInCleared.Broadcast(Index == 0 ? EMSVoiceCutInChannel::Left : EMSVoiceCutInChannel::Right);
		}
	}
}

bool UMSVoiceCutInSubsystem::TrySubmitRequest(const FMSVoiceCutInRequest& Request,
	const EMSVoiceTrigger CooldownKey, const double Now)
{
	ClearExpiredRequests(Now);
	const double Cooldown = MSVoiceCutInRules::GetCooldownSeconds(CooldownKey);
	const uint16 CooldownMapKey = (static_cast<uint16>(CooldownKey) << 1) |
		static_cast<uint16>(ChannelIndex(Request.Channel));
	if (const double* Previous = LastTriggerTimes.Find(CooldownMapKey); Previous && Now - *Previous < Cooldown)
	{
		return false;
	}
	FChannelState& State = Channels[ChannelIndex(Request.Channel)];
	if (State.Request.IsSet() && !MSVoiceCutInRules::ShouldReplace(State.Request->Priority, Request.Priority))
	{
		return false;
	}
	State.Request = Request;
	State.DisplayUntil = Now + DisplayDurationSeconds;
	LastTriggerTimes.Add(CooldownMapKey, Now);
	OnCutInRequested.Broadcast(State.Request.GetValue());
	if (MSVoiceSettings::IsEnabled(EMSVoiceCategory::Preset))
	{
		const FString VoiceAssetPath = MSVoiceLibrary::GetPilotVoiceAssetPath(
			Request.VoiceType, Request.Trigger, Request.LineIndex);
		USoundBase* VoiceSound = VoiceAssetPath.IsEmpty()
			? nullptr : LoadObject<USoundBase>(nullptr, *VoiceAssetPath);
		if (VoiceSound)
		{
			MSAudio::PlayMetaSound2D(this, VoiceSound, 1.0f, 1.0f, 3.0f);
		}
		else
		{
			if (!VoiceAssetPath.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Pilot voice asset failed to load: %s"), *VoiceAssetPath);
			}
			MSAudio::PlayMetaSound2D(this, GetVoiceBlipSound(),
				MSVoiceCutInRules::GetBlipFrequency(Request.VoiceType));
		}
	}
	return true;
}

FMSVoiceCutInRequest UMSVoiceCutInSubsystem::MakeRequest(AActor* Speaker, AMSPlayerCharacter* LocalPlayer,
	const EMSVoiceTrigger Trigger, const EMSVoiceCutInChannel Channel, const double Now,
	const int32 PriorityOverride) const
{
	FMSVoiceCutInRequest Request;
	Request.SpeakerName = FText::FromString(Speaker == LocalPlayer ? TEXT("プレイヤー") :
		(Speaker ? Speaker->GetName() : TEXT("Unknown")));
	Request.VoiceType = ResolveVoiceType(Speaker, LocalPlayer, GetGameInstance());
	Request.Trigger = Trigger;
	Request.Channel = Channel;
	Request.PortraitColor = GetPortraitColor(Request.VoiceType);
	Request.Priority = PriorityOverride == INDEX_NONE ? MSVoiceCutInRules::GetPriority(Trigger) : PriorityOverride;
	Request.RequestTime = Now;
	const uint32 Seed = HashCombine(GetTypeHash(Request.SpeakerName.ToString()),
		HashCombine(static_cast<uint32>(Trigger), static_cast<uint32>(Now * 1000.0)));
	Request.Line = MSVoiceLibrary::PickLine(Request.VoiceType, Trigger, static_cast<int32>(Seed),
		Request.LineIndex);
	return Request;
}

void UMSVoiceCutInSubsystem::HandleKillConfirmed(AActor* Killer)
{
	ProcessKillConfirmed(Killer, ResolveLocalPlayer(), GetNow());
}

void UMSVoiceCutInSubsystem::HandleUnitDeath(AActor* DeadUnit, AActor* Killer)
{
	ProcessUnitDeath(DeadUnit, Killer, ResolveLocalPlayer(), GetNow());
}

void UMSVoiceCutInSubsystem::HandleUnitDamaged(AActor* DamagedUnit, AActor* Attacker, FVector)
{
	ProcessUnitDamaged(DamagedUnit, Attacker, ResolveLocalPlayer(), GetNow());
}

void UMSVoiceCutInSubsystem::HandleDeployed(AMSUnitBase* Unit)
{
	AMSPlayerCharacter* Player = ResolveLocalPlayer();
	if (!Player || Unit != Player)
	{
		return;
	}
	int32& Count = DeploymentCounts.FindOrAdd(Unit);
	const EMSVoiceTrigger Trigger = Count++ == 0 ? EMSVoiceTrigger::Sortie : EMSVoiceTrigger::Deployed;
	SubmitRequest(MakeRequest(Player, Player, Trigger, EMSVoiceCutInChannel::Left, GetNow()), GetNow());
}

void UMSVoiceCutInSubsystem::HandleCaptureOwnerChanged(AMSCapturePoint* CapturePoint,
	const EMSCaptureOwner NewOwner)
{
	AMSPlayerCharacter* Player = ResolveLocalPlayer();
	if (Player && CapturePoint && UMSTeamStatics::CaptureOwnerToTeamId(NewOwner) == Player->GetGenericTeamId() &&
		FVector::DistSquared(Player->GetActorLocation(), CapturePoint->GetActorLocation()) <= FMath::Square(CaptureRangeCm))
	{
		const double Now = GetNow();
		SubmitRequest(MakeRequest(Player, Player, EMSVoiceTrigger::CaptureBase,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}

void UMSVoiceCutInSubsystem::HandlePlayerRepaired(AMSUnitBase* Unit)
{
	AMSPlayerCharacter* Player = ResolveLocalPlayer();
	if (Player && Unit == Player)
	{
		const double Now = GetNow();
		SubmitRequest(MakeRequest(Player, Player, EMSVoiceTrigger::Repair,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}

void UMSVoiceCutInSubsystem::HandleSupplyPickedUp(AMSUnitBase* Consumer)
{
	AMSPlayerCharacter* Player = ResolveLocalPlayer();
	if (Player && Consumer == Player)
	{
		const double Now = GetNow();
		SubmitRequest(MakeRequest(Player, Player, EMSVoiceTrigger::SupplyPickup,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}

void UMSVoiceCutInSubsystem::HandleRecallStarted()
{
	if (AMSPlayerCharacter* Player = ResolveLocalPlayer())
	{
		const double Now = GetNow();
		SubmitRequest(MakeRequest(Player, Player, EMSVoiceTrigger::EmergencyRecall,
			EMSVoiceCutInChannel::Left, Now), Now);
	}
}
