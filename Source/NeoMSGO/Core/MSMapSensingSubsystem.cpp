#include "Core/MSMapSensingSubsystem.h"

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSRadarPod.h"
#include "NeoMSGO.h"

TStatId UMSMapSensingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMSMapSensingSubsystem, STATGROUP_Tickables);
}

bool UMSMapSensingSubsystem::IsContactWithinHold(const double LastSeenAt, const double Now)
{
	return LastSeenAt >= 0.0 && Now >= LastSeenAt && Now - LastSeenAt <= LostContactHoldSeconds;
}

bool UMSMapSensingSubsystem::HasFriendlyLineOfSight(UWorld* World, const AMSUnitBase* Friendly,
	const AMSUnitBase* Enemy)
{
	if (!World || !IsValid(Friendly) || !IsValid(Enemy) ||
		FVector::DistSquared(Friendly->GetActorLocation(), Enemy->GetActorLocation()) > FMath::Square(VisualRangeCm))
		return false;
	const FVector Start = Friendly->GetLockOnAimPoint();
	const FVector End = Enemy->GetLockOnAimPoint();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MSMapSight), false);
	Query.AddIgnoredActor(Friendly);
	for (int32 Step = 0; Step < 16; ++Step)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, MSCollision::WeaponTraceChannel, Query))
			return true;
		const AActor* HitActor = Hit.GetActor();
		if (HitActor == Enemy || (HitActor && HitActor->IsAttachedTo(Enemy))) return true;
		const APhysicsVolume* Volume = Cast<APhysicsVolume>(HitActor);
		if (HitActor && (HitActor->IsA<APawn>() || (Volume && Volume->bWaterVolume)))
		{
			Query.AddIgnoredActor(HitActor);
			continue;
		}
		return false;
	}
	return false;
}

void UMSMapSensingSubsystem::Tick(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !World->HasBegunPlay()) return;
	UpdateAccumulator += FMath::Max(0.0f, DeltaTime);
	if (UpdateAccumulator < UpdateIntervalSeconds) return;
	UpdateAccumulator = FMath::Fmod(UpdateAccumulator, UpdateIntervalSeconds);
	UpdateTeamContacts();
}

void UMSMapSensingSubsystem::UpdateTeamContacts()
{
	UWorld* World = GetWorld();
	UMSUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UMSUnitRegistrySubsystem>() : nullptr;
	if (!World || !Registry) return;
	TArray<AMSUnitBase*> Units;
	Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
		if (IsValid(Unit) && (!ASC || !ASC->HasMatchingGameplayTag(MSTags::State_Dead))) Units.Add(Unit);
	});
	TArray<AMSRadarPod*> Radars;
	for (TActorIterator<AMSRadarPod> It(World); It; ++It) if (IsValid(*It)) Radars.Add(*It);
	TMap<uint8, TArray<AMSPlayerController*>> Viewers;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AMSPlayerController* Controller = Cast<AMSPlayerController>(It->Get());
		if (!Controller) continue;
		FGenericTeamId Team = UMSTeamStatics::GetTeamId(Controller->GetPawn());
		if (Team == FGenericTeamId::NoTeam) Team = Controller->GetOnlineAssignedTeamId();
		if (Team != FGenericTeamId::NoTeam) Viewers.FindOrAdd(Team.GetId()).Add(Controller);
	}
	const double Now = World->GetTimeSeconds();
	for (const auto& TeamViewers : Viewers)
	{
		const uint8 TeamId = TeamViewers.Key;
		TMap<TWeakObjectPtr<AMSUnitBase>, FSighting>& Sightings = LastSeenByTeam.FindOrAdd(TeamId);
		TSet<TWeakObjectPtr<AMSUnitBase>> DetectedThisUpdate;
		int32 TraceCount = 0;
		for (AMSUnitBase* Enemy : Units)
		{
			if (UMSTeamStatics::SolveAttitude(FGenericTeamId(TeamId), UMSTeamStatics::GetTeamId(Enemy))
				!= ETeamAttitude::Hostile) continue;
			bool bDetected = false;
			for (const AMSRadarPod* Radar : Radars)
			{
				if (Radar->IsActive() && Radar->GetGenericTeamId() == FGenericTeamId(TeamId) &&
					AMSRadarPod::IsInsideRadarRange(Radar->GetActorLocation(), Enemy->GetActorLocation(),
						Radar->GetRadarRadiusCm())) { bDetected = true; break; }
			}
			if (!bDetected)
			{
				for (AMSUnitBase* Friendly : Units)
				{
					if (TraceCount >= MaxLineOfSightTracesPerTeamUpdate) break;
					if (UMSTeamStatics::GetTeamId(Friendly) != FGenericTeamId(TeamId) ||
						FVector::DistSquared(Friendly->GetActorLocation(), Enemy->GetActorLocation())
							> FMath::Square(VisualRangeCm)) continue;
					++TraceCount;
					if (HasFriendlyLineOfSight(World, Friendly, Enemy)) { bDetected = true; break; }
				}
			}
			if (bDetected)
			{
				FSighting& Sighting = Sightings.FindOrAdd(TWeakObjectPtr<AMSUnitBase>(Enemy));
				Sighting.LastKnownLocation = Enemy->GetActorLocation();
				Sighting.LastSeenAt = Now;
				DetectedThisUpdate.Add(TWeakObjectPtr<AMSUnitBase>(Enemy));
			}
		}
		TArray<FMSMapContact> Contacts;
		for (auto It = Sightings.CreateIterator(); It; ++It)
		{
			AMSUnitBase* Unit = It.Key().Get();
			if (!IsValid(Unit) || !Units.Contains(Unit) ||
				UMSTeamStatics::SolveAttitude(FGenericTeamId(TeamId), UMSTeamStatics::GetTeamId(Unit))
					!= ETeamAttitude::Hostile || !IsContactWithinHold(It.Value().LastSeenAt, Now))
			{
				It.RemoveCurrent();
				continue;
			}
			FMSMapContact& Contact = Contacts.AddDefaulted_GetRef();
			Contact.ContactId = Unit->GetUniqueID();
			Contact.LastKnownLocation = It.Value().LastKnownLocation;
			Contact.bCurrentlyDetected = DetectedThisUpdate.Contains(TWeakObjectPtr<AMSUnitBase>(Unit));
		}
		TArray<FMSMapRadarRange> FriendlyRadars;
		for (const AMSRadarPod* Radar : Radars)
		{
			if (!IsValid(Radar) || !Radar->IsActive() ||
				Radar->GetGenericTeamId() != FGenericTeamId(TeamId)) continue;
			FMSMapRadarRange& Range = FriendlyRadars.AddDefaulted_GetRef();
			Range.RadarId = Radar->GetUniqueID();
			Range.Center = Radar->GetActorLocation();
			Range.RadiusCm = Radar->GetRadarRadiusCm();
		}
		for (AMSPlayerController* Controller : TeamViewers.Value)
		{
			TArray<FMSMapFriendlyContact> Friendlies;
			for (const AMSUnitBase* Unit : Units)
			{
				if (Unit == Controller->GetPawn() ||
					UMSTeamStatics::GetTeamId(Unit) != FGenericTeamId(TeamId)) continue;
				FMSMapFriendlyContact& Friendly = Friendlies.AddDefaulted_GetRef();
				Friendly.UnitId = Unit->GetUniqueID();
				Friendly.Location = Unit->GetActorLocation();
				if (const AMSBotCharacter* Bot = Cast<AMSBotCharacter>(Unit))
					Friendly.BotTier = static_cast<uint8>(Bot->GetEnemyTier());
			}
			Controller->PublishMapContacts(Contacts, Friendlies, FriendlyRadars, TeamId);
		}
	}
}
