// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSVisualProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Audio/MSAudioPlayback.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Gameplay/MSRadarPod.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Data/MSWeaponData.h"
#include "Combat/MSUnitBase.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/PointLightComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** トレイル自然消滅のための遅延破棄(Unity: Destroy(gameObject, 0.15f)) */
	constexpr float VisualProjectileLingerSeconds = 0.15f;
	constexpr float ExplosiveProjectileSpeedMultiplier = 1.75f;

	float EffectiveProjectileSpeedCmPerSec(const UMSRangedWeaponData* Weapon)
	{
		if (!Weapon) return 0.0f;
		const bool bExplosive = Weapon->CategoryTag == MSTags::Weapon_Category_Bazooka ||
			Weapon->CategoryTag == MSTags::Weapon_Category_Homing;
		// RangeWorldScale is the unit contract for distance-authored weapon data.
		// Legacy weapons retain MSScale; physical-metre assets use one.
		return Weapon->ProjectileSpeed * 100.0f * Weapon->RangeWorldScale
			* (bExplosive ? ExplosiveProjectileSpeedMultiplier : 1.0f);
	}
}

AMSVisualProjectile::AMSVisualProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.0f);

	// 見た目専用。命中判定は発射時のResolveHitscanで確定済みのため、弾体はコリジョンを一切持たない
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SetActorEnableCollision(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ExhaustMaterialFinder(
		TEXT("/Game/Meshes/Weapons/HeavyWeapons/M_HeavyWeaponExhaust.M_HeavyWeaponExhaust"));
	DefaultCubeMesh = CubeFinder.Object;
	DefaultVisualMaterial = MaterialFinder.Object;
	ExhaustConeMesh = ConeFinder.Object;
	ExhaustMaterial = ExhaustMaterialFinder.Object;
	VisualBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualBody"));
	VisualBody->SetupAttachment(Root);
	VisualBody->SetStaticMesh(DefaultCubeMesh);
	VisualBody->SetMaterial(0, DefaultVisualMaterial);
	VisualBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualBody->SetGenerateOverlapEvents(false);
	VisualBody->SetCastShadow(false);
	VisualTrail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualTrail"));
	VisualTrail->SetupAttachment(Root);
	VisualTrail->SetStaticMesh(DefaultCubeMesh);
	VisualTrail->SetMaterial(0, DefaultVisualMaterial);
	VisualTrail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualTrail->SetGenerateOverlapEvents(false);
	VisualTrail->SetCastShadow(false);
	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(Root);
	GlowLight->SetAttenuationRadius(180.0f * MSGO::MSScale);
	GlowLight->SetIntensity(1800.0f);
	GlowLight->SetCastShadows(false);
	static ConstructorHelpers::FObjectFinder<USoundBase> FlightSoundFinder(
		TEXT("/Game/Audio/Imported/SFX_Projectile_Zap.SFX_Projectile_Zap"));
	FlightSound = FlightSoundFinder.Object;
}

void AMSVisualProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSVisualProjectile, ReplicatedVisuals);
}

AMSVisualProjectile* AMSVisualProjectile::LaunchVisualProjectile(
	AActor* Shooter,
	const FVector& Origin,
	const FVector& Direction,
	const UMSRangedWeaponData* Weapon,
	const float DamageMultiplier)
{
	return LaunchVisualProjectileFromOrigins(Shooter, Origin, Origin, Direction, Weapon, DamageMultiplier);
}

AMSVisualProjectile* AMSVisualProjectile::LaunchVisualProjectileFromOrigins(
	AActor* Shooter, const FVector& TraceOrigin, const FVector& VisualOrigin,
	const FVector& Direction, const UMSRangedWeaponData* Weapon, const float DamageMultiplier,
	const FMSChargeShotMultipliers* ChargeMultipliers)
{
	if (!Shooter || !Weapon)
	{
		return nullptr;
	}
	UWorld* World = Shooter->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 1. 発射時に命中を確定する(B-6)。誰に当たるか/HSか/FFで無効かはこの1回のトレースで固定され、
	//    以後ターゲットが移動・消滅しても結果は変わらない(Unity: 発射時ResolveHitscan)。
	const FMSHitscanResult Trace = UMSCombatStatics::ResolveHitscan(
		Shooter, TraceOrigin, Direction, Weapon->GetRangeWorldCm(), Shooter);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Shooter;
	Params.Instigator = Cast<APawn>(Shooter);
	AMSVisualProjectile* Projectile = World->SpawnActor<AMSVisualProjectile>(
		StaticClass(), VisualOrigin, (Trace.EndPoint - VisualOrigin).GetSafeNormal().Rotation(), Params);
	if (!Projectile)
	{
		return nullptr;
	}
	Projectile->InitializeFlight(Shooter, VisualOrigin, Trace, Weapon, DamageMultiplier, ChargeMultipliers);
	return Projectile;
}

AMSVisualProjectile* AMSVisualProjectile::LaunchHomingVisualProjectile(
	AActor* Shooter,
	const FVector& Origin,
	const FVector& Direction,
	const UMSHomingWeaponData* Weapon,
	AActor* Target,
	const float DamageMultiplier)
{
	return LaunchHomingVisualProjectileFromOrigins(
		Shooter, Origin, Origin, Direction, Weapon, Target, DamageMultiplier);
}

AMSVisualProjectile* AMSVisualProjectile::LaunchHomingVisualProjectileFromOrigins(
	AActor* Shooter, const FVector& TraceOrigin, const FVector& VisualOrigin,
	const FVector& Direction, const UMSHomingWeaponData* Weapon, AActor* Target,
	const float DamageMultiplier)
{
	if (!Shooter || !Weapon)
	{
		return nullptr;
	}
	UWorld* World = Shooter->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FVector TraceDirection = Direction.GetSafeNormal();
	if (Target)
	{
		const IMSLockOnTargetInterface* Lockable = Cast<IMSLockOnTargetInterface>(Target);
		const FVector AimPoint = Lockable ? Lockable->GetLockOnAimPoint() : Target->GetActorLocation();
		TraceDirection = (AimPoint - TraceOrigin).GetSafeNormal();
	}
	const FMSHitscanResult Trace = UMSCombatStatics::ResolveHitscan(
		Shooter, TraceOrigin, TraceDirection, Weapon->GetRangeWorldCm(), Shooter);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Shooter;
	Params.Instigator = Cast<APawn>(Shooter);
	AMSVisualProjectile* Projectile = World->SpawnActor<AMSVisualProjectile>(
		StaticClass(), VisualOrigin, Direction.GetSafeNormal().Rotation(), Params);
	if (Projectile)
	{
		Projectile->InitializeHomingFlight(
			Shooter, VisualOrigin, Direction, Trace, Weapon, Target, DamageMultiplier);
	}
	return Projectile;
}

void AMSVisualProjectile::InitializeFlight(AActor* Shooter, const FVector& Origin,
	const FMSHitscanResult& Trace, const UMSRangedWeaponData* Weapon, const float DamageMultiplier,
	const FMSChargeShotMultipliers* ChargeMultipliers)
{
	ShooterActor = Shooter;
 LaunchShieldDamageScale = 1.f + (Cast<AMSUnitBase>(Shooter) ? CastChecked<AMSUnitBase>(Shooter)->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ShieldDamageIncrease)*.01f : 0.f);
	WeaponData = Weapon;
	ConfirmedHit = Trace;
	StartLocation = Origin;

	// 着弾予定点: 命中時はImpactPoint、外れは最大射程点(ResolveHitscanのEndPointがその両方を返す)
	ImpactLocation = Trace.EndPoint;

	// 威力・DP・弾速・爆風半径は発射時の独立したチャージ倍率で確定する。正の爆発はPowerと
	// legacy SplashDamageの大きい方を1 packetへまとめ、Power=0の煙は旧経路を維持する。
	const float Multiplier = ChargeMultipliers
		? FMath::Max(0.0f, ChargeMultipliers->Damage) : FMath::Max(0.0f, DamageMultiplier);
	const float DownPointMultiplier = ChargeMultipliers
		? FMath::Max(0.0f, ChargeMultipliers->DownPoint) : FMath::Max(0.0f, DamageMultiplier);
	const float SpeedMultiplier = ChargeMultipliers
		? FMath::Max(0.0f, ChargeMultipliers->ProjectileSpeed) : 1.0f;
	const float RadiusMultiplier = ChargeMultipliers
		? FMath::Max(0.0f, ChargeMultipliers->SplashRadius) : 1.0f;
	ConfirmedDamage = Weapon->Power * Multiplier;
	ConfirmedDownPoint = Weapon->DownPoint * DownPointMultiplier;
	SplashRadiusCm = Weapon->SplashRadius * 100.0f * Weapon->RangeWorldScale * RadiusMultiplier;
 if (const auto* Unit = Cast<AMSUnitBase>(Shooter); Unit && Weapon->SupportsBlastRadiusControl())
  SplashRadiusCm *= 1.f + Unit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::BlastRadiusIncrease) * .01f;
	bUsesSingleExplosionPacket = Weapon->Power > 0 && Weapon->GetExplosionBaseDamage() > 0.0f;
	const float ExplosionBaseDamage = bUsesSingleExplosionPacket
		? Weapon->GetExplosionBaseDamage()
		: static_cast<float>(Weapon->SplashDamage);
	ScaledSplashDamage = FMath::RoundToFloat(ExplosionBaseDamage * Multiplier);
	ExplosionMinFalloff = Weapon->GetExplosionMinFalloff();
	ExplosionFullDamageRadiusRatio = Weapon->GetExplosionFullDamageRadiusRatio();
	// Smoke must not gain the direct weapon DP when its legacy direct+small-splash path is used.
	ExplosionDownPoint = bUsesSingleExplosionPacket ? ConfirmedDownPoint : 0.0f;
	ExplosionDirectHitDownPointMultiplier = Weapon->DirectHitDownPointMultiplier;

	// 2. 飛翔時間 = 距離 / 弾速(m/s→cm/s)。ProjectileSpeed<=0はデータ不備(即着はGA_Fire_Hitscanの
	//    責務)だが、堅牢性のため duration 0 として次Tickで即着弾解決する。
	const float SpeedCmPerSec = EffectiveProjectileSpeedCmPerSec(Weapon) * SpeedMultiplier;
	const float DistanceCm = FVector::Dist(StartLocation, ImpactLocation);
	FlightDuration = SpeedCmPerSec > 0.0f ? DistanceCm / SpeedCmPerSec : 0.0f;
	ElapsedFlightTime = 0.0f;
	bInFlight = true;
	ConfigureVisuals(Weapon);
	StartFlightAudio();
}

void AMSVisualProjectile::StartFlightAudio()
{
	if (VisualStyle != EMSProjectileVisualStyle::BazookaShell || FlightAudio || !FlightSound)
	{
		return;
	}
	FlightAudio = MSAudio::SpawnLoopingMetaSoundAttached(GetRootComponent(), FlightSound, 180.0f);
}

void AMSVisualProjectile::InitializeHomingFlight(AActor* Shooter, const FVector& Origin,
	const FVector& InitialDirection, const FMSHitscanResult& Trace,
	const UMSHomingWeaponData* Weapon, AActor* Target, const float DamageMultiplier)
{
	InitializeFlight(Shooter, Origin, Trace, Weapon, DamageMultiplier);
	bHomingFlight = true;
	HomingTarget = Target;
	HomingDirection = InitialDirection.GetSafeNormal();
	if (HomingDirection.IsNearlyZero())
	{
		HomingDirection = GetActorForwardVector();
	}
	HomingSpeedCmPerSecond = FMath::Max(0.0f, EffectiveProjectileSpeedCmPerSec(Weapon));
	HomingTurnRateDegPerSecond = FMath::Max(0.0f, Weapon->HomingTurnRateDegPerSec);
	SetActorRotation(HomingDirection.Rotation());
}

void AMSVisualProjectile::ConfigureVisuals(const UMSRangedWeaponData* Weapon)
{
	if (!Weapon || !VisualBody || !VisualTrail || !GlowLight)
	{
		return;
	}
	FMSProjectileVisualReplication State;
	State.BodyColor = Weapon->TracerColor;
	State.TrailColor = State.BodyColor * 1.6f;
	if (Weapon->CategoryTag == MSTags::Weapon_Category_Bazooka || Weapon->CategoryTag == MSTags::Weapon_Category_Homing)
	{
		State.Style = EMSProjectileVisualStyle::BazookaShell;
		State.BodyColor = FLinearColor(1.0f, 0.32f, 0.04f);
		State.TrailColor = FLinearColor(1.0f, 0.75f, 0.12f);
		State.BodyScale = FVector(0.34f, 0.13f, 0.13f) * MSGO::MSScale;
		State.TrailLocation = FVector(-52.0f, 0, 0) * MSGO::MSScale;
		State.TrailScale = FVector(0.75f, 0.075f, 0.075f) * MSGO::MSScale;
		State.GlowAttenuationRadius = 90.0f * MSGO::MSScale;
		State.GlowIntensity = 300.0f;
	}
	else if (Weapon->CategoryTag == MSTags::Weapon_Category_BeamRifle
		|| Weapon->CategoryTag == MSTags::Weapon_Category_ContinuousBeam)
	{
		State.Style = EMSProjectileVisualStyle::BeamBolt;
		State.BodyColor = FLinearColor(0.15f, 0.75f, 1.0f);
		State.TrailColor = FLinearColor(0.55f, 0.95f, 1.0f);
		State.BodyScale = FVector(0.8f, 0.045f, 0.045f) * MSGO::MSScale;
		State.TrailLocation = FVector(-60.0f, 0, 0) * MSGO::MSScale;
		State.TrailScale = FVector(1.1f, 0.025f, 0.025f) * MSGO::MSScale;
		State.GlowAttenuationRadius = 100.0f * MSGO::MSScale;
		State.GlowIntensity = 180.0f;
	}
	else
	{
		State.Style = EMSProjectileVisualStyle::Tracer;
		State.BodyColor = State.BodyColor.IsAlmostBlack() ? FLinearColor(1.0f, 0.75f, 0.15f) : State.BodyColor;
		State.TrailColor = FLinearColor(1.0f, 0.85f, 0.35f);
		State.BodyScale = FVector(0.42f, 0.025f, 0.025f) * MSGO::MSScale;
		State.TrailLocation = FVector(-35.0f, 0, 0) * MSGO::MSScale;
		State.TrailScale = FVector(0.55f, 0.015f, 0.015f) * MSGO::MSScale;
		State.GlowIntensity = 1000.0f;
	}

	State.ProjectileMeshPath = Weapon->ProjectileMesh.ToSoftObjectPath();
	State.bHasAuthoredMesh = !State.ProjectileMeshPath.IsNull();
	State.ProjectileMeshTransform = Weapon->ProjectileMeshTransform;
	State.bAuthoredExhaust = Weapon->bProjectileMeshExhaust;
	if (State.bHasAuthoredMesh)
	{
		// Authored projectile materials carry the paint scheme. Keep the light restrained so
		// bloom does not flatten the mesh's material contrast.
		State.GlowAttenuationRadius = 72.0f * MSGO::MSScale;
		State.GlowIntensity = 100.0f;
		State.TrailColor = FLinearColor(1.0f, 0.38f, 0.05f, 1.0f);
		State.TrailIntensity = 2.0f;
	}
	ReplicatedVisuals = State;
	ApplyVisualReplication(ReplicatedVisuals);
}

void AMSVisualProjectile::ApplyVisualReplication(const FMSProjectileVisualReplication& State)
{
	if (!VisualBody || !VisualTrail || !GlowLight)
	{
		return;
	}

	VisualStyle = State.Style;
	GlowLight->SetLightColor(State.BodyColor);
	GlowLight->SetAttenuationRadius(State.GlowAttenuationRadius);
	GlowLight->SetIntensity(State.GlowIntensity);

	UStaticMesh* AuthoredMesh = nullptr;
	if (State.bHasAuthoredMesh && !State.ProjectileMeshPath.IsNull())
	{
		AuthoredMesh = TSoftObjectPtr<UStaticMesh>(State.ProjectileMeshPath).LoadSynchronous();
	}
	bUsingAuthoredProjectileMesh = AuthoredMesh != nullptr;
	if (bUsingAuthoredProjectileMesh)
	{
		VisualBody->SetStaticMesh(AuthoredMesh);
		// Constructor fallback material is an override on slot 0. Clear it before using the
		// imported mesh so every authored paint slot remains visible.
		VisualBody->SetMaterial(0, nullptr);
		VisualBody->SetRelativeTransform(State.ProjectileMeshTransform);

		// Unpowered shells/grenades: no rocket flame. The trail is only hidden; impact,
		// damage and explosion handling never read it.
		VisualTrail->SetVisibility(State.bAuthoredExhaust);
		if (!State.bAuthoredExhaust)
		{
			return;
		}
		if (ExhaustConeMesh)
		{
			VisualTrail->SetStaticMesh(ExhaustConeMesh);
			VisualTrail->SetMaterial(0, ExhaustMaterial ? ExhaustMaterial : DefaultVisualMaterial);
			if (UMaterialInstanceDynamic* TrailMaterial = VisualTrail->CreateAndSetMaterialInstanceDynamic(0))
			{
				TrailMaterial->SetVectorParameterValue(TEXT("Color"), State.TrailColor);
				TrailMaterial->SetScalarParameterValue(TEXT("Intensity"), State.TrailIntensity);
			}
			const FBoxSphereBounds Bounds = AuthoredMesh->GetBounds();
			const float NozzleRearX = Bounds.Origin.X - Bounds.BoxExtent.X - 6.0f;
			const FTransform& BodyTransform = State.ProjectileMeshTransform;
			const FVector Nozzle = BodyTransform.TransformPosition(FVector(NozzleRearX, 0.0f, 0.0f));
			const FVector RearAxis = BodyTransform.GetRotation().RotateVector(FVector::ForwardVector).GetSafeNormal();
			// Engine/Cone's long axis and apex are local +Z. A +90 pitch points the apex
			// toward the projectile's rear (-X); place the cone centre half its length behind
			// the mesh's rear bound so the wide base starts at the nozzle.
			const float ConeHalfLength = ExhaustConeMesh->GetBounds().BoxExtent.Z * 0.55f;
			VisualTrail->SetRelativeLocation(Nozzle - RearAxis * ConeHalfLength);
			VisualTrail->SetRelativeRotation(
				BodyTransform.GetRotation() * FRotator(90.0f, 0.0f, 0.0f).Quaternion());
			// Cone.Cone is authored at roughly 100cm; this is deliberately direct world-cm
			// sizing and therefore does not receive another MSScale multiplication.
			VisualTrail->SetRelativeScale3D(FVector(0.10f, 0.10f, 0.55f));
		}
		else
		{
			VisualTrail->SetStaticMesh(DefaultCubeMesh);
			VisualTrail->SetMaterial(0, DefaultVisualMaterial);
			VisualTrail->SetRelativeLocation(State.TrailLocation);
			VisualTrail->SetRelativeRotation(FRotator::ZeroRotator);
			VisualTrail->SetRelativeScale3D(State.TrailScale);
		}
		return;
	}

	// No authored mesh: preserve the existing Cube body/trail path byte-for-byte in shape,
	// scale, colors, and MSScale handling.
	VisualTrail->SetVisibility(true);
	VisualBody->SetStaticMesh(DefaultCubeMesh);
	VisualBody->SetMaterial(0, DefaultVisualMaterial);
	VisualBody->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	VisualBody->SetRelativeScale3D(State.BodyScale);
	VisualTrail->SetStaticMesh(DefaultCubeMesh);
	VisualTrail->SetMaterial(0, DefaultVisualMaterial);
	VisualTrail->SetRelativeLocation(State.TrailLocation);
	VisualTrail->SetRelativeRotation(FRotator::ZeroRotator);
	VisualTrail->SetRelativeScale3D(State.TrailScale);
	if (UMaterialInstanceDynamic* BodyMaterial = VisualBody->CreateAndSetMaterialInstanceDynamic(0))
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), State.BodyColor);
	}
	if (UMaterialInstanceDynamic* TrailMaterial = VisualTrail->CreateAndSetMaterialInstanceDynamic(0))
	{
		TrailMaterial->SetVectorParameterValue(TEXT("Color"), State.TrailColor);
	}
}

void AMSVisualProjectile::OnRep_VisualReplication()
{
	ApplyVisualReplication(ReplicatedVisuals);
}

void AMSVisualProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInFlight)
	{
		return;
	}

	if (bHomingFlight)
	{
		ElapsedFlightTime += DeltaSeconds;
		FVector CurrentTarget = ImpactLocation;
		if (const AActor* Target = HomingTarget.Get())
		{
			const IMSLockOnTargetInterface* Lockable = Cast<IMSLockOnTargetInterface>(Target);
			CurrentTarget = Lockable ? Lockable->GetLockOnAimPoint() : Target->GetActorLocation();
			ImpactLocation = CurrentTarget;
		}

		const FVector ToTarget = CurrentTarget - GetActorLocation();
		const FVector DesiredDirection = ToTarget.GetSafeNormal();
		if (!DesiredDirection.IsNearlyZero())
		{
			const float MaxTurnRadians = FMath::DegreesToRadians(HomingTurnRateDegPerSecond) * DeltaSeconds;
			const FQuat CurrentRotation = HomingDirection.ToOrientationQuat();
			const FQuat DesiredRotation = DesiredDirection.ToOrientationQuat();
			const float AngleRadians = CurrentRotation.AngularDistance(DesiredRotation);
			const FQuat NewRotation = AngleRadians <= MaxTurnRadians || AngleRadians <= UE_KINDA_SMALL_NUMBER
				? DesiredRotation
				: FQuat::Slerp(CurrentRotation, DesiredRotation, MaxTurnRadians / AngleRadians);
			HomingDirection = NewRotation.GetForwardVector().GetSafeNormal();
			SetActorRotation(NewRotation);
		}

		const float StepDistance = HomingSpeedCmPerSecond * DeltaSeconds;
		if (ToTarget.SizeSquared() <= FMath::Square(StepDistance) ||
			(FlightDuration <= 0.0f && StepDistance <= 0.0f))
		{
			SetActorLocation(CurrentTarget);
			ResolveImpact();
			return;
		}
		SetActorLocation(GetActorLocation() + HomingDirection * StepDistance);

		// A curved route may be longer than the launch-time direct distance. Bound bad data/targets.
		if (ElapsedFlightTime >= FMath::Max(FlightDuration * 3.0f, 6.0f))
		{
			ResolveImpact();
		}
		return;
	}

	// 3. 見た目の弾: 発射点→着弾予定点をFlightDuration秒かけてLerp移動(Unity: VisualProjectileのt>=1到達判定)
	ElapsedFlightTime += DeltaSeconds;
	const float Alpha = FlightDuration > 0.0f
		? FMath::Clamp(ElapsedFlightTime / FlightDuration, 0.0f, 1.0f)
		: 1.0f;
	SetActorLocation(FMath::Lerp(StartLocation, ImpactLocation, Alpha));

	if (Alpha >= 1.0f)
	{
		ResolveImpact();
	}
}

void AMSVisualProjectile::ResolveImpact()
{
	if (bImpactResolved)
	{
		return;
	}
	bInFlight = false;
	bImpactResolved = true;
	MSAudio::StopLoopingMetaSound(FlightAudio);
	bool bDispatchedImpactCue = false;
	TWeakObjectPtr<UAbilitySystemComponent> PendingHitConfirmASC;
	FGameplayCueParameters PendingHitConfirmParameters;
	if (AActor* Shooter = ShooterActor.Get())
	{
		if (UAbilitySystemComponent* SourceASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
		{
			FGameplayEffectContextHandle CueContext = SourceASC->MakeEffectContext();
			CueContext.AddInstigator(Shooter, this);
			CueContext.AddSourceObject(WeaponData.Get());
			CueContext.AddHitResult(ConfirmedHit.Hit, true);
			FGameplayCueParameters CueParameters(CueContext);
			CueParameters.Instigator = Shooter;
			CueParameters.Location = ImpactLocation;
			CueParameters.SourceObject = WeaponData.Get();
			if (VisualStyle == EMSProjectileVisualStyle::BazookaShell)
			{
				// Explosive projectiles need both pieces of feedback: the spatial explosion for
				// everyone and, when damage is actually confirmed below, the shooter's private
				// 2D hit-confirm Cue. Previously this branch only executed Cue_Explosion, so the
				// Impact Cue (and its local-controller audio policy) was never reached for missiles.
				SourceASC->ExecuteGameplayCue(MSTags::Cue_Explosion, CueParameters);
				PendingHitConfirmASC = SourceASC;
				PendingHitConfirmParameters = CueParameters;
			}
			else
			{
				const FGameplayTag ImpactTag = ConfirmedHit.bIsHeadshot
					? MSTags::Cue_Impact_Headshot : MSTags::Cue_Impact_Normal;
				SourceASC->ExecuteGameplayCue(ImpactTag, CueParameters);
			}
			bDispatchedImpactCue = true;
		}
	}
	if (UWorld* World = bDispatchedImpactCue ? nullptr : GetWorld())
	{
		// 恒久実装: 実行時生成StaticMeshComponent(発光Cube)による着弾演出。Shipping相当でも描画される。
		const bool bExplosion = VisualStyle == EMSProjectileVisualStyle::BazookaShell;
		if (bExplosion)
		{
			// B-7規定(UE5_MIGRATION_PLAN §1.1): 爆発は「一瞬・控えめ」0.04〜0.06s厳守
			// SpawnExplosion applies MSScale internally. SplashRadiusCm is already world-scaled,
			// so convert it back to the function's unscaled-radius contract to avoid double scaling.
			const float VisualRadiusCm = SplashRadiusCm > 0.0f
				? SplashRadiusCm / MSGO::MSScale
				: 58.0f;
			AMSTransientVfxActor::SpawnExplosion(World, ImpactLocation, VisualRadiusCm, 0.06f);
		}
		else
		{
			AMSTransientVfxActor::SpawnImpactSparks(World, ImpactLocation, FLinearColor(1.0f, 0.82f, 0.31f), false, 0.14f);
		}
	}

	// 4. 実ダメージ適用は着弾時のみ(Unity: WeaponFx.ResolveImpactの順序 = 直撃遅延適用 → 爆風 → 演出)
	bool bAppliedHostileDamage = false;
	if (ConfirmedHit.bBlockingHit)
	{
		// An explosive weapon is one packet: the direct target is passed to the splash
		// resolver as the center and must not also receive a separate GE_Damage packet.
		if (bUsesSingleExplosionPacket)
		{
			bAppliedHostileDamage |= ApplyConfirmedSplashDamage() > 0;
		}
		else
		{
			if (ConfirmedHit.bCanDamage)
			{
				bAppliedHostileDamage |= ApplyConfirmedDirectDamage();
			}
			// Power-zero smoke and legacy non-explosive splash retain their old path/profile.
			bAppliedHostileDamage |= ApplyConfirmedSplashDamage() > 0;
		}
	}
	if (bAppliedHostileDamage)
	{
		if (UAbilitySystemComponent* SourceASC = PendingHitConfirmASC.Get())
		{
			const FGameplayTag ImpactTag = ConfirmedHit.bCanDamage && ConfirmedHit.bIsHeadshot
				? MSTags::Cue_Impact_Headshot : MSTags::Cue_Impact_Normal;
			SourceASC->ExecuteGameplayCue(ImpactTag, PendingHitConfirmParameters);
		}
		if (AActor* Shooter = ShooterActor.Get())
		{
			if (UGameInstance* GameInstance = Shooter->GetGameInstance())
			{
				if (UMSHitFeedbackSubsystem* Feedback = GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
				{
					Feedback->BroadcastHitConfirmed(
						Shooter, ConfirmedHit.bCanDamage && ConfirmedHit.bIsHeadshot);
				}
			}
		}
	}

	// ヒット演出通知(P4-13でGameplayCue発火に接続)。外れ弾も最大射程消滅の通知として発火する
	OnImpactResolved.Broadcast(this, ConfirmedHit);

	// 到達後は非表示+遅延破棄(Unity: bodyRenderer.enabled=false + Destroy(gameObject, 0.15f))
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	SetLifeSpan(VisualProjectileLingerSeconds);
}

bool AMSVisualProjectile::ApplyConfirmedDirectDamage()
{
	// 発射時に確定したターゲットへ適用する。着弾前にターゲットが消滅していた場合のみスキップ
	AActor* HitActor = ConfirmedHit.Hit.GetActor();
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		if (AMSRadarPod* Radar = Cast<AMSRadarPod>(HitActor))
			return Radar->ApplyWeaponDamage(ShooterActor.Get(), ConfirmedDamage);
		return false;
	}

	AActor* Shooter = ShooterActor.Get();
	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter);

	// GE_Damage呼び出し契約(MSDamageGameplayEffects.h)。GA_Fire_Hitscanの即着経路と同一
	FGameplayEffectContextHandle Context = SourceASC
		? SourceASC->MakeEffectContext()
		: FGameplayEffectContextHandle(new FMSGameplayEffectContext());
	Context.AddInstigator(Shooter, Shooter);
	Context.AddHitResult(ConfirmedHit.Hit, true);
 Context.AddSourceObject(WeaponData.Get());
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
	{
		MSContext->SetHeadshot(ConfirmedHit.bIsHeadshot);
  MSContext->SetShieldDamageScale(LaunchShieldDamageScale);
		MSContext->SetAttackerLocation(StartLocation);
	}

	FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
	Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, FMath::Max(0.0f, ConfirmedDamage));
	Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, FMath::Max(0.0f, ConfirmedDownPoint));
	if (SourceASC)
	{
		SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
	}
	else
	{
		TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
	}
	return true;
}

int32 AMSVisualProjectile::ApplyConfirmedSplashDamage()
{
	if (SplashRadiusCm <= 0.0f || ScaledSplashDamage <= 0.0f)
	{
		return 0;
	}

	// 正の爆発では直撃対象を単一packetの中心として扱う。Power=0煙/legacy splashは
	// 旧来のdirect+small-splash挙動とprofile(外周0.3・内側full半径なし)を保ち、DPも0のままにする。
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(this);
	AActor* DirectHitActor = bUsesSingleExplosionPacket ? ConfirmedHit.Hit.GetActor() : nullptr;
	return UMSCombatStatics::ApplySplashDamage(
		this,
		ImpactLocation,
		SplashRadiusCm,
		ScaledSplashDamage,
		IgnoreActors,
		ShooterActor.Get(),
		ExplosionDownPoint,
		DirectHitActor,
		/*bExcludeDirectHitActor=*/false,
		ExplosionMinFalloff,
		ExplosionFullDamageRadiusRatio, LaunchShieldDamageScale, ExplosionDirectHitDownPointMultiplier);
}
