// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSRespawnComponent.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Gameplay/MSPlayerController.h"
#include "Player/MSPlayerCharacter.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "UI/MSDeckSubsystem.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"
#include "AI/MSBotSpawnPoint.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSGameMode.h"

#include "NeoMSGO.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSShieldComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSModuleData.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace
{
	bool IsTaiganMap(const UWorld* World)
	{
		return World && World->GetMapName().EndsWith(TEXT("Lvl_NewYork_Taigan"));
	}

	bool IsTaiganUnderground(const UWorld* World, const FVector& Location)
	{
		return IsTaiganMap(World) && Location.Z < -4500.0f;
	}

	bool IsTaklamakanRuinsMap(const UWorld* World)
	{
		return World && World->GetMapName().EndsWith(TEXT("Lvl_Taklamakan_Ruins_Prototype"));
	}

	bool ResolveTaiganRearSpawn(UWorld* World, const FTransform& Target, FTransform& OutTarget)
	{
		OutTarget = Target;
		if (!IsTaiganMap(World)) return true;
		for (TActorIterator<AMSHomeBase> Base(World); Base; ++Base)
		{
			if (!Base->ActorHasTag(TEXT("NewYorkTaiganGenerated")) ||
				!Base->GetActorLocation().Equals(Target.GetLocation(), 5.0f)) continue;
			const AMSBotSpawnPoint* Marker = Base->GetDeploymentSpawnAnchor();
			const FString PointName = Base->GetPointName().ToString();
			const bool bRearUndergroundBase = PointName == TEXT("C") || PointName == TEXT("Z");
			if (!Marker) return !bRearUndergroundBase;
			if (!IsValid(Marker) || !Marker->ActorHasTag(TEXT("NewYorkTaiganGenerated")) ||
				!Marker->ServesTeam(Base->GetGenericTeamId()) ||
				(bRearUndergroundBase && Marker->GetActorLocation().Z >= -4500.0f)) return false;
			OutTarget = Marker->GetActorTransform();
			return true;
		}
		return true;
	}

	bool ResolveTaklamakanBaseSpawn(UWorld* World, const FTransform& Target,
		FTransform& OutTarget, bool& bOutAuthoredBase, float& OutBaseFloorZ)
	{
		OutTarget = Target;
		bOutAuthoredBase = false;
		OutBaseFloorZ = 0.0f;
		if (!IsTaklamakanRuinsMap(World)) return true;
		for (TActorIterator<AMSHomeBase> Base(World); Base; ++Base)
		{
			if (!Base->ActorHasTag(TEXT("MapFidelity0929Taklamakan"))) continue;
			const AMSBotSpawnPoint* Marker = Base->GetDeploymentSpawnAnchor();
			const bool bBaseSelected = Base->GetActorLocation().Equals(Target.GetLocation(), 5.0f);
			const bool bMarkerSelected = IsValid(Marker) && Marker->GetActorLocation().Equals(Target.GetLocation(), 5.0f);
			if (!bBaseSelected && !bMarkerSelected) continue;
			bOutAuthoredBase = true;
			const FString PointName = Base->GetPointName().ToString();
			const bool bKnownBase = PointName == TEXT("A") || PointName == TEXT("B") ||
				PointName == TEXT("X") || PointName == TEXT("Y");
			if (!IsValid(Marker) || Marker->GetWorld() != World ||
				!bKnownBase ||
				!Marker->ActorHasTag(TEXT("MapFidelity0929Taklamakan")) ||
				!Marker->ServesTeam(Base->GetGenericTeamId()) ||
				// The linked anchor must remain on this base's local terrace;
				// its exact XY comes from the authored actor, not image pixels.
				FVector::Dist2D(Marker->GetActorLocation(), Base->GetActorLocation()) > 10000.0f)
			{
				return false;
			}
			OutTarget = Marker->GetActorTransform();
			OutBaseFloorZ = Base->GetActorLocation().Z;
			return true;
		}
		// A tagged marker without a valid linked base must not enter the generic
		// Nav fallback, which can project it onto the roof or a distant tile.
		for (TActorIterator<AMSBotSpawnPoint> Marker(World); Marker; ++Marker)
		{
			if (Marker->ActorHasTag(TEXT("MapFidelity0929Taklamakan")) &&
				Marker->GetActorLocation().Equals(Target.GetLocation(), 5.0f)) return false;
		}
		return true;
	}
}

bool UMSRespawnComponent::TryResolveCoveredDropLocation(UWorld* World, const FVector& GroundLocation,
	const float CapsuleRadius, const float CapsuleHalfHeight, const float DesiredDropHeight,
	const AActor* IgnoredActor, FVector& OutDropLocation)
{
	if (!World || CapsuleRadius <= 0.0f || CapsuleHalfHeight <= CapsuleRadius) return false;
	constexpr float ClearanceCm = 50.0f;
	const float StandingCenterZ = GroundLocation.Z + CapsuleHalfHeight + ClearanceCm;
	const FVector HeadTraceStart(GroundLocation.X, GroundLocation.Y, StandingCenterZ);
	const FVector HeadTraceEnd(GroundLocation.X, GroundLocation.Y,
		GroundLocation.Z + FMath::Max(DesiredDropHeight + CapsuleHalfHeight + ClearanceCm,
			2.0f * CapsuleHalfHeight + 2.0f * ClearanceCm));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MSTaiganSpawnHeadroom), false);
	if (IgnoredActor) Query.AddIgnoredActor(IgnoredActor);
	FHitResult Ceiling;
	if (!World->LineTraceSingleByChannel(Ceiling, HeadTraceStart, HeadTraceEnd, ECC_WorldStatic, Query))
	{
		// A roof opening is not a safe underground drop: the full drop could
		// place the unit above the surface layer.
		return false;
	}
	const float HighestCenterZ = Ceiling.ImpactPoint.Z - CapsuleHalfHeight - ClearanceCm;
	if (HighestCenterZ < StandingCenterZ) return false;
	const float PreferredCenterZ = FMath::Clamp(
		GroundLocation.Z + DesiredDropHeight, StandingCenterZ, HighestCenterZ);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	for (int32 Step = 0; Step <= 8; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / 8.0f;
		const FVector Candidate(GroundLocation.X, GroundLocation.Y,
			FMath::Lerp(PreferredCenterZ, StandingCenterZ, Alpha));
		if (!World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn, Capsule, Query))
		{
			OutDropLocation = Candidate;
			return true;
		}
	}
	return false;
}

UMSRespawnComponent::UMSRespawnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMSRespawnComponent::BeginPlay()
{
	Super::BeginPlay();

	AMSUnitBase* Owner = GetOwnerUnit();
	if (!Owner)
	{
		return;
	}

	// 既定スポーン地点 = 初期配置(Unity: spawnPosition相当)。以降はSetDefaultSpawnPointで差し替え可
	DefaultSpawnTransform = Owner->GetActorTransform();

	// 死亡通知(P2-2 OnDeath: 生存→死亡遷移時に1回だけ発火)を購読
	if (UMSAttributeSet_Vital* Vital = Owner->GetVitalSet())
	{
		Vital->OnDeath.AddDynamic(this, &UMSRespawnComponent::HandleOwnerDeath);
	}
	else
	{
		UE_LOG(LogNeoMSGO, Warning,
			TEXT("%s: UMSRespawnComponent - VitalSetが見つからないためOnDeathを購読できません"), *Owner->GetName());
	}
}

void UMSRespawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDeathFlow();

	if (AMSUnitBase* Owner = GetOwnerUnit())
	{
		if (UMSAttributeSet_Vital* Vital = Owner->GetVitalSet())
		{
			Vital->OnDeath.RemoveDynamic(this, &UMSRespawnComponent::HandleOwnerDeath);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UMSRespawnComponent::Deploy(UMSMobileSuitData* Preset, FTransform SpawnOverride)
{
	if (MSOnline::IsEnabled()) return;
	// 目標は呼び出し時点の引数で1回だけ確定(B-11)。再入は拒否して目標のブレを防ぐ
	if (bDeployInProgress)
	{
		UE_LOG(LogNeoMSGO, Warning, TEXT("UMSRespawnComponent::Deploy - Deploy処理中の再入を無視(目標の再確定はしない)"));
		return;
	}

	// 生存中の任意再出撃は何度でも利用できる。死亡待機を満了した権利だけを消費する。
	if (ExecuteDeploy(Preset, SpawnOverride, /*bConsumeDeathRespawnReadiness=*/false))
	{
		ActiveDeckSlotIndex = INDEX_NONE;
	}
}

void UMSRespawnComponent::DeployAtDefaultSpawn(UMSMobileSuitData* Preset)
{
	// 既定スポーン地点もこの時点の値で確定してDeployへ渡す(以降の変更に影響されない)
	Deploy(Preset, DefaultSpawnTransform);
}

bool UMSRespawnComponent::DeployDeckSlot(UMSDeckSubsystem* Deck, const int32 SlotIndex, FTransform SpawnOverride)
{
	if (MSOnline::IsEnabled()) return false;
	if (!Deck || !CanDeployDeckSlot(SlotIndex) || bDeployInProgress || !GetOwnerUnit()) return false;
	UMSMobileSuitData* Preset=Deck->GetPresetInSlot(SlotIndex);
	if (!Preset) return false;
	const FMSDeckLoadout Loadout=Deck->GetLoadoutInSlot(SlotIndex);
 FMSBattleAssistResolvedLoadout Resolved; FMSBattleAssistValidationResult Error;
 if (!MSBattleAssist::ResolveLoadout(Preset, Loadout.BattleAssist, Resolved, Error)) return false;
	const int32 PreviousSlotIndex = ActiveDeckSlotIndex;
	ActiveDeckSlotIndex=SlotIndex;
	if (ExecuteDeploy(Preset,SpawnOverride,false,&Loadout)) return true;
	ActiveDeckSlotIndex = PreviousSlotIndex;
	return false;
}

float UMSRespawnComponent::GetDeckSlotTimeRemaining(const int32 SlotIndex) const
{
	if(SlotIndex<0 || SlotIndex>=4 || !GetWorld()) return 0.0f;
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority()) return static_cast<float>(FMath::Max(0.0, OnlineRespawn.SlotReadyAt[SlotIndex] - GetSynchronizedTime()));
	return static_cast<float>(FMath::Max(0.0,DeckSlotReadyAt[SlotIndex]-GetWorld()->GetTimeSeconds()));
}

bool UMSRespawnComponent::CanDeployDeckSlot(const int32 SlotIndex) const
{
	return SlotIndex>=0 && SlotIndex<4 && CanDeploy() && GetDeckSlotTimeRemaining(SlotIndex)<=0.0f;
}

bool UMSRespawnComponent::RequestEmergencyEject()
{
	if (MSOnline::IsEnabled()) { const auto* Unit = GetOwnerUnit(); const auto* PC = Unit ? Cast<AMSPlayerController>(Unit->GetController()) : nullptr; if (!PC || !PC->IsOnlineAuthenticated()) return false; if (!Unit->HasAuthority()) { ServerEmergencyEject(); return false; } }
	if(!IsRespawnPending() || bRespawnReady || !GetWorld()) return false;
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimerHandle);
	if(!bExplosionFired) { GetWorld()->GetTimerManager().ClearTimer(ExplosionTimerHandle); HandleExplosionTimer(); }
	HandleRespawnTimer(); return true;
}

void UMSRespawnComponent::SetDefaultSpawnPoint(const FTransform& InTransform)
{
	// 死亡待機中でも確定済みのPendingSpawnTransformには触れない(B-11: 目標は1回だけ確定)
	DefaultSpawnTransform = InTransform;
}

bool UMSRespawnComponent::IsRespawnPending() const
{
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority()) return OnlineRespawn.bPending;
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(RespawnTimerHandle);
}

float UMSRespawnComponent::GetRespawnTimeRemaining() const
{
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority()) return OnlineRespawn.bPending ? static_cast<float>(FMath::Max(0.0, OnlineRespawn.ReadyAt - GetSynchronizedTime())) : 0.0f;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const float Remaining = World->GetTimerManager().GetTimerRemaining(RespawnTimerHandle);
	return FMath::Max(Remaining, 0.0f); // 非稼働時は-1が返るため0へ
}

void UMSRespawnComponent::HandleOwnerDeath(AActor* DeadUnit, AActor* Killer)
{
	if (MSOnline::IsEnabled() && GetOwner() && !GetOwner()->HasAuthority()) return;
	AMSUnitBase* Owner = GetOwnerUnit();
	if (!Owner || DeadUnit != Owner)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// OnDeathは生存→死亡遷移で1回のみだが、多重起動への防御(タイマー稼働中は無視)
	if (IsRespawnPending())
	{
		return;
	}

	// リスポーン目標をこの時点で1回だけ確定(B-11)。タイマー満了時に再解決しない
	PendingSpawnTransform = DefaultSpawnTransform;
	DeathLocation = Owner->GetActorLocation();
	bRespawnReady = false;
	bExplosionFired = false;
	if(ActiveDeckSlotIndex>=0 && ActiveDeckSlotIndex<4)
	{
		DeckSlotReadyAt[ActiveDeckSlotIndex]=World->GetTimeSeconds()+Owner->GetBattleAssistResolved().EffectiveReuseSeconds;
	}

	// 爆発演出(explosionDelay=5s)とリスポーン(respawnDelay=10s)は独立タイマー
	World->GetTimerManager().SetTimer(
		ExplosionTimerHandle, this, &UMSRespawnComponent::HandleExplosionTimer, ExplosionDelay, false);
	World->GetTimerManager().SetTimer(
		RespawnTimerHandle, this, &UMSRespawnComponent::HandleRespawnTimer, RespawnDelay, false);
	PublishOnlineRespawn();
}

void UMSRespawnComponent::HandleExplosionTimer()
{
	bExplosionFired = true;

	// TODO(P5/P7): Cue.Explosion(Niagara。lifetime 0.04-0.06s相当の控えめ演出 — B-7)を
	// DeathLocationで再生する。本タスク(P3-7)はタイミング通知の土台のみ。
	OnExplosionDue.Broadcast(GetOwnerUnit(), DeathLocation);
}

void UMSRespawnComponent::HandleRespawnTimer()
{
	bRespawnReady = true;
	PublishOnlineRespawn();

	if (bAutoRespawn)
	{
		// 死亡時に確定済みの目標のみを使用(B-11)。Preset=null=現在機体のまま再初期化
		ExecuteDeploy(nullptr, PendingSpawnTransform, /*bConsumeDeathRespawnReadiness=*/true);
	}
	else
	{
		OnRespawnReady.Broadcast(GetOwnerUnit());
	}
	// bAutoRespawn=false時は再出撃UI(P8-8)がCanDeploy()を見てDeployを呼ぶ
}

bool UMSRespawnComponent::ExecuteDeploy(UMSMobileSuitData* Preset, const FTransform& ConfirmedTarget,
	const bool bConsumeDeathRespawnReadiness, const FMSDeckLoadout* Loadout)
{
	AMSUnitBase* Owner = GetOwnerUnit();
	if (!Owner)
	{
		return false;
	}

 FMSBattleAssistResolvedLoadout Resolved; FMSBattleAssistValidationResult Error;
 if (!MSBattleAssist::ResolveLoadout(Preset ? Preset : Owner->GetMobileSuitData(),
  Loadout ? Loadout->BattleAssist : FMSBattleAssistLoadout(), Resolved, Error)) return false;
	TGuardValue<bool> ReentryGuard(bDeployInProgress, true);
	FTransform DeploymentTarget;
	if (!ResolveTaiganRearSpawn(GetWorld(), ConfirmedTarget, DeploymentTarget))
	{
		UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taigan base has no valid explicit deployment anchor"), *Owner->GetName());
		return false;
	}
	bool bTaklamakanAuthoredBase = false;
	float TaklamakanBaseFloorZ = 0.0f;
	if (!ResolveTaklamakanBaseSpawn(GetWorld(), DeploymentTarget, DeploymentTarget,
		bTaklamakanAuthoredBase, TaklamakanBaseFloorZ))
	{
		UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taklamakan base has no valid explicit deployment anchor"), *Owner->GetName());
		return false;
	}
	const bool bTaiganUnderground = IsTaiganUnderground(GetWorld(), DeploymentTarget.GetLocation());
	const bool bTaiganSurface = IsTaiganMap(GetWorld()) && !bTaiganUnderground;
	FString GroundResolveMethod;
	FString GroundHitActor;
	FVector UndergroundGround = FVector::ZeroVector;
	FVector UndergroundDrop = FVector::ZeroVector;
	FVector SurfaceGround = FVector::ZeroVector;
	FVector TaklamakanGround = FVector::ZeroVector;
	FVector TaklamakanDrop = FVector::ZeroVector;
	if (bTaklamakanAuthoredBase)
	{
		// Resolve the linked base's actual floor, not a fixed pixel-derived
		// marker offset. A short trace below the marker cannot select the roof.
		const FVector MarkerLocation = DeploymentTarget.GetLocation();
		FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MSTaklamakanBaseFloor), true);
		FloorQuery.AddIgnoredActor(Owner);
		FHitResult Floor;
		const bool bFloorHit = GetWorld()->LineTraceSingleByChannel(Floor,
			MarkerLocation,
			FVector(MarkerLocation.X, MarkerLocation.Y, TaklamakanBaseFloorZ - 500.0f),
			ECC_WorldStatic, FloorQuery);
		const AActor* FloorActor = bFloorHit ? Floor.GetActor() : nullptr;
		if (!bFloorHit || !IsValid(FloorActor) ||
			!FloorActor->ActorHasTag(TEXT("MapFidelity0929Taklamakan")) ||
			FloorActor->IsA<AMSHomeBase>() || FloorActor->IsA<AMSBotSpawnPoint>() ||
			FMath::Abs(Floor.ImpactPoint.Z - TaklamakanBaseFloorZ) > 200.0f)
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taklamakan covered deploy rejected; authored floor missing near %s"),
				*Owner->GetName(), *MarkerLocation.ToCompactString());
			return false;
		}
		TaklamakanGround = Floor.ImpactPoint;
		float CapsuleRadius = 0.0f;
		float CapsuleHalfHeight = 0.0f;
		if (const UCapsuleComponent* Capsule = Owner->GetCapsuleComponent())
		{
			Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
		}
		if (CapsuleRadius <= 0.0f || CapsuleHalfHeight <= CapsuleRadius)
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taklamakan covered deploy rejected; invalid capsule"), *Owner->GetName());
			return false;
		}
		// The marker itself should be near a standing capsule centre on this
		// floor; a high or buried marker is not a valid covered spawn anchor.
		if (MarkerLocation.Z < TaklamakanGround.Z + CapsuleHalfHeight ||
			MarkerLocation.Z > TaklamakanGround.Z + 2.0f * CapsuleHalfHeight)
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taklamakan covered deploy rejected; anchor height differs from floor"),
				*Owner->GetName());
			return false;
		}
		FCollisionQueryParams CapsuleQuery(SCENE_QUERY_STAT(MSTaklamakanStandingCapsule), false);
		CapsuleQuery.AddIgnoredActor(Owner);
		const FVector StandingCenter = TaklamakanGround + FVector(0.0f, 0.0f, CapsuleHalfHeight + 50.0f);
		if (GetWorld()->OverlapBlockingTestByChannel(StandingCenter, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), CapsuleQuery) ||
			!TryResolveCoveredDropLocation(GetWorld(), TaklamakanGround, CapsuleRadius,
				CapsuleHalfHeight, DropSpawnHeight, Owner, TaklamakanDrop))
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taklamakan covered deploy rejected; no safe capsule space below roof at %s"),
				*Owner->GetName(), *TaklamakanGround.ToCompactString());
			return false;
		}
		GroundResolveMethod = TEXT("taklamakan_authored_floor_trace");
		GroundHitActor = GetNameSafe(FloorActor);
	}
	if (bTaiganUnderground)
	{
		UndergroundGround = ResolveGroundLocation(
			DeploymentTarget.GetLocation(), GroundResolveMethod, GroundHitActor);
		float CapsuleRadius = 0.0f;
		float CapsuleHalfHeight = 0.0f;
		if (const UCapsuleComponent* Capsule = Owner->GetCapsuleComponent())
		{
			Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
		}
		if (GroundResolveMethod != TEXT("taigan_underground_trace") ||
			!TryResolveCoveredDropLocation(GetWorld(), UndergroundGround, CapsuleRadius,
				CapsuleHalfHeight, DropSpawnHeight, Owner, UndergroundDrop))
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: underground deploy rejected at %s; floor=%s hit=%s"),
				*Owner->GetName(), *DeploymentTarget.GetLocation().ToCompactString(),
				*GroundResolveMethod, *GroundHitActor);
			return false;
		}
	}
	if (bTaiganSurface)
	{
		SurfaceGround = ResolveGroundLocation(
			DeploymentTarget.GetLocation(), GroundResolveMethod, GroundHitActor);
		float CapsuleRadius = 0.0f;
		float CapsuleHalfHeight = 0.0f;
		if (const UCapsuleComponent* Capsule = Owner->GetCapsuleComponent())
		{
			Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
		}
		// Surface sorties must have a clear fall column. Otherwise a high air
		// teleport can land on a roof even when the selected floor is correct.
		const FVector StandingCenter = SurfaceGround + FVector(0.0f, 0.0f, CapsuleHalfHeight + 50.0f);
		const FVector AirCenter = SurfaceGround + FVector(0.0f, 0.0f, DropSpawnHeight);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MSTaiganSurfaceDrop), false);
		Query.AddIgnoredActor(Owner);
		FHitResult ColumnHit;
		if (GroundResolveMethod != TEXT("taigan_surface_trace") ||
			CapsuleRadius <= 0.0f || CapsuleHalfHeight <= CapsuleRadius ||
			GetWorld()->SweepSingleByChannel(ColumnHit, StandingCenter, AirCenter,
				FQuat::Identity, ECC_WorldStatic,
				FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), Query))
		{
			UE_LOG(LogNeoMSGO, Error, TEXT("%s: Taigan surface deploy rejected at %s; floor=%s hit=%s column=%s"),
				*Owner->GetName(), *DeploymentTarget.GetLocation().ToCompactString(),
				*GroundResolveMethod, *GroundHitActor, *GetNameSafe(ColumnHit.GetActor()));
			return false;
		}
	}
	// A successful sortie consumes the completed online recall transition.  Keep this
	// separate from the death timer state so a living pawn cannot clear the server gate.
	bOnlineRecallAwaitingDeploy = false;

	// Deploy成立: 死亡フローの残タイマー(自動リスポーン・未発火の爆発演出)を破棄
	ClearDeathFlow();
	if (bConsumeDeathRespawnReadiness)
	{
		bRespawnReady = false;
	}

	// 機体切替+State.Dead/State.Overheated除去+Attribute全快(単一入口。P2-1/P2-2)
	Owner->InitializeUnitWithBattleAssist(Preset, Loadout ? Loadout->BattleAssist : FMSBattleAssistLoadout());
	if (UMSShieldComponent* Shield = Owner->GetShieldComponent())
	{
		// Deck shield choice is applied after suit initialization so the native
		// asset is resolved once, before the pawn returns to play.
		Shield->SetDeckShieldEnabled(!Loadout || Loadout->bShieldEquipped);
		Shield->ResetForRespawn();
	}
	if (Loadout)
	{
		Owner->SetEquippedModule(Loadout->EquippedModule.LoadSynchronous());
		TArray<UMSWeaponDataBase*> Weapons;
		for (const auto& Ref:Loadout->EquippedWeapons) Weapons.Add(Ref.LoadSynchronous());
		if (UMSWeaponComponent* Component=Owner->GetWeaponComponent()) Component->InitializeSlots(Weapons);
		if (UMSWeaponVisualComponent* Visual=Owner->GetWeaponVisualComponent()) Visual->RefreshFromWeaponComponent();
	}

	// 上空降下方式(B-11): 目標の上空DropSpawnHeightへテレポートし、重力に任せて着地させる。
	// 固定Z座標へ直接置くと台座・地形へ埋没するため(Unity版の既知バグの再発防止)。
	const FVector GroundLocation = bTaklamakanAuthoredBase ? TaklamakanGround : bTaiganUnderground ? UndergroundGround
		: bTaiganSurface ? SurfaceGround : ResolveGroundLocation(
			DeploymentTarget.GetLocation(), GroundResolveMethod, GroundHitActor);
	const FVector SpawnLocation = bTaklamakanAuthoredBase ? TaklamakanDrop : bTaiganUnderground ? UndergroundDrop :
		GroundLocation + FVector(0.0f, 0.0f, DropSpawnHeight);
	const FRotator SpawnRotation(0.0f, DeploymentTarget.Rotator().Yaw, 0.0f); // カプセルは直立のためYawのみ
	Owner->SetActorLocationAndRotation(SpawnLocation, SpawnRotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// 旧速度(死亡時の落下・ノックバック)を持ち越さず、重力降下を開始する
	if (UCharacterMovementComponent* Movement = Owner->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Falling);
	}

	// 視点も出撃方向へ合わせる(Controllerが居る場合のみ。BOT等では不要)
	if (AController* Controller = Owner->GetController())
	{
		Controller->SetControlRotation(SpawnRotation);
	}

	UE_LOG(LogNeoMSGO, Log,
		TEXT("%s: Deploy complete suit=%s marker=(%s) ground=(%s) deploy=(%s) method=%s hit=%s drop=%.0fcm"),
		*Owner->GetName(),
		Owner->GetMobileSuitData() ? *Owner->GetMobileSuitData()->GetName() : TEXT("(none)"),
		*DeploymentTarget.GetLocation().ToCompactString(),
		*GroundLocation.ToCompactString(),
		*SpawnLocation.ToCompactString(),
		*GroundResolveMethod,
		*GroundHitActor,
		SpawnLocation.Z - GroundLocation.Z);

	if (MSOnline::IsEnabled() && Owner->HasAuthority()) { ++OnlineRespawn.DeployRevision; PublishOnlineRespawn(); }
	if (Owner->HasAuthority() && Cast<AMSPlayerCharacter>(Owner) &&
		Owner->GetController() && Owner->GetController()->IsPlayerController())
	{
		if (AMSGameMode* Mode = GetWorld()->GetAuthGameMode<AMSGameMode>())
			Mode->NotifyRoomDeployment();
	}
	OnDeployed.Broadcast(Owner);
	return true;
}

FVector UMSRespawnComponent::ResolveGroundLocation(const FVector& Candidate, FString& OutResolveMethod,
	FString& OutHitActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutResolveMethod = TEXT("no_world");
		OutHitActor = TEXT("none");
		return Candidate;
	}
	if (IsTaiganUnderground(World, Candidate))
	{
		// The authored rear marker is a capsule centre below the subway roof.
		// Starting a 500,000 cm trace above it would hit the surface layer first.
		FHitResult UndergroundFloor;
		FCollisionQueryParams UndergroundQuery(SCENE_QUERY_STAT(MSTaiganRespawnFloor), false);
		UndergroundQuery.AddIgnoredActor(GetOwner());
		if (World->LineTraceSingleByChannel(UndergroundFloor,
			Candidate + FVector(0.0f, 0.0f, 900.0f),
			Candidate - FVector(0.0f, 0.0f, 15000.0f), ECC_WorldStatic, UndergroundQuery))
		{
			OutResolveMethod = TEXT("taigan_underground_trace");
			OutHitActor = GetNameSafe(UndergroundFloor.GetActor());
			return UndergroundFloor.ImpactPoint;
		}
		OutResolveMethod = TEXT("taigan_underground_no_floor");
		OutHitActor = TEXT("none");
		return Candidate;
	}
	if (IsTaiganMap(World))
	{
		// Direct targets (capture points and BOT saved defaults) specify a
		// floor. Explicit authored spawn markers instead specify a standing
		// capsule centre; convert only those markers to their selected floor.
		float ExpectedFloorZ = Candidate.Z;
		for (TActorIterator<AMSBotSpawnPoint> Marker(World); Marker; ++Marker)
		{
			if (!Marker->ActorHasTag(TEXT("NewYorkTaiganGenerated")) ||
				!Marker->GetActorLocation().Equals(Candidate, 5.0f)) continue;
			if (const AMSUnitBase* Unit = GetOwnerUnit())
			{
				if (const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent())
					ExpectedFloorZ -= Capsule->GetScaledCapsuleHalfHeight();
			}
			break;
		}
		constexpr float FloorBandCm = 500.0f;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MSTaiganSurfaceFloor), false);
		Query.AddIgnoredActor(GetOwner());
		FHitResult Floor;
		if (World->LineTraceSingleByChannel(Floor,
			FVector(Candidate.X, Candidate.Y, ExpectedFloorZ + FloorBandCm),
			FVector(Candidate.X, Candidate.Y, ExpectedFloorZ - FloorBandCm),
			ECC_WorldStatic, Query))
		{
			OutResolveMethod = TEXT("taigan_surface_trace");
			OutHitActor = GetNameSafe(Floor.GetActor());
			return Floor.ImpactPoint;
		}
		OutResolveMethod = TEXT("taigan_surface_no_floor");
		OutHitActor = TEXT("none");
		return Candidate;
	}

	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		if (NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(
				Candidate, Projected, FVector(DropSpawnHeight, DropSpawnHeight, DropSpawnHeight)))
			{
				OutResolveMethod = TEXT("navmesh");
				OutHitActor = TEXT("navigation");
				return Projected.Location;
			}
		}
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSPlayerRespawnGroundTrace), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(GetOwner());
	const FVector ProbeOffset(0.0f, 0.0f, GroundProbeHeight);
	if (World->LineTraceSingleByChannel(
		Hit, Candidate + ProbeOffset, Candidate - ProbeOffset, ECC_WorldStatic, QueryParams))
	{
		OutResolveMethod = TEXT("ground_trace");
		OutHitActor = GetNameSafe(Hit.GetActor());
		return Hit.ImpactPoint;
	}

	OutResolveMethod = TEXT("candidate_fallback");
	OutHitActor = TEXT("none");
	return Candidate;
}

void UMSRespawnComponent::ClearDeathFlow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExplosionTimerHandle);
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
}

AMSUnitBase* UMSRespawnComponent::GetOwnerUnit() const
{
	AMSUnitBase* Owner = Cast<AMSUnitBase>(GetOwner());
	if (!Owner)
	{
		UE_LOG(LogNeoMSGO, Error,
			TEXT("UMSRespawnComponent - AMSUnitBase系以外(%s)に搭載されています。InitializeUnit経路が使えません"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("null"));
	}
	return Owner;
}

void UMSRespawnComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(UMSRespawnComponent, OnlineRespawn, COND_OwnerOnly);
}
double UMSRespawnComponent::GetSynchronizedTime() const
{
 const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
 return State ? State->GetServerWorldTimeSeconds() : GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}
void UMSRespawnComponent::PublishOnlineRespawn()
{
 if (!MSOnline::IsEnabled() || !GetOwner() || !GetOwner()->HasAuthority()) return;
 OnlineRespawn.bReady = bRespawnReady;
 OnlineRespawn.bRecallAwaitingDeploy = bOnlineRecallAwaitingDeploy;
 OnlineRespawn.bAlive = !bOnlineRecallAwaitingDeploy && GetOwnerUnit() && GetOwnerUnit()->GetVitalSet() && GetOwnerUnit()->GetVitalSet()->GetArmor() > 0;
 OnlineRespawn.bPending = !bRespawnReady && IsRespawnPending();
 OnlineRespawn.ReadyAt = OnlineRespawn.bPending ? GetSynchronizedTime() + GetRespawnTimeRemaining() : 0;
 for (int32 I = 0; I < 4; ++I) OnlineRespawn.SlotReadyAt[I] = DeckSlotReadyAt[I];
 GetOwner()->ForceNetUpdate();
}
void UMSRespawnComponent::OnRep_OnlineRespawn()
{
 const bool bWasReady = bRespawnReady; bRespawnReady = OnlineRespawn.bReady;
 if (OnlineRespawn.bRecallAwaitingDeploy)
 {
  if (auto* Player = Cast<AMSPlayerCharacter>(GetOwner()))
  {
   // The owner-only respawn snapshot may arrive before the Recall component's
   // presentation flag after reconnect. Restore the hidden/collision state here
   // and let OpenResortieScreen consult the same authoritative snapshot.
   Player->SetActorHiddenInGame(true);
   Player->SetActorEnableCollision(false);
   if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
   {
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
   }
   if (bRespawnReady) Player->OpenResortieScreen();
  }
 }
 if (!OnlineRespawn.bAlive && !OnlineRespawn.bRecallAwaitingDeploy && OnlineRespawn.DeployRevision > 0)
 {
  if (auto* Player = Cast<AMSPlayerCharacter>(GetOwner())) Player->ApplyOnlineDeathPresentation();
  if (bRespawnReady) OnRespawnReady.Broadcast(GetOwnerUnit());
 }
 if (OnlineRespawn.bAlive && OnlineRespawn.DeployRevision != ObservedDeployRevision)
 {
  ObservedDeployRevision = OnlineRespawn.DeployRevision;
  if (auto* Player = Cast<AMSPlayerCharacter>(GetOwner())) Player->ApplyOnlineDeploymentPresentation();
  OnDeployed.Broadcast(GetOwnerUnit());
 }
 else if (!bWasReady && bRespawnReady) OnRespawnReady.Broadcast(GetOwnerUnit());
}
void UMSRespawnComponent::ServerEmergencyEject_Implementation() { RequestEmergencyEject(); }
bool UMSRespawnComponent::DeployVerifiedOnlineSlot(int32 SlotIndex, UMSMobileSuitData* Preset, const FMSDeckLoadout& Loadout, const FTransform& Spawn)
{
 auto* Unit = GetOwnerUnit(); auto* PC = Unit ? Cast<AMSPlayerController>(Unit->GetController()) : nullptr;
 if (!MSOnline::IsEnabled() || !Unit || !Unit->HasAuthority() || !PC || !PC->IsOnlineAuthenticated() || !Preset || !CanDeployDeckSlot(SlotIndex) || bDeployInProgress) return false;
 FMSBattleAssistResolvedLoadout Resolved; FMSBattleAssistValidationResult Error;
 if (!MSBattleAssist::ResolveLoadout(Preset, Loadout.BattleAssist, Resolved, Error)) return false;
 const int32 PreviousSlotIndex = ActiveDeckSlotIndex;
 ActiveDeckSlotIndex = SlotIndex;
 if (ExecuteDeploy(Preset, Spawn, false, &Loadout)) return true;
 ActiveDeckSlotIndex = PreviousSlotIndex;
 return false;
}

void UMSRespawnComponent::BeginOnlineRecall()
{
	AMSUnitBase* Unit = GetOwnerUnit();
	if (!MSOnline::IsEnabled() || !Unit || !Unit->HasAuthority()) return;

	// Recall is a deliberate return-to-sortie transition, not a death.  Keep the
	// existing slot readiness and do not start or cancel the death timers.
	ClearDeathFlow();
	bRespawnReady = true;
	bOnlineRecallAwaitingDeploy = true;
	PublishOnlineRespawn();
}
