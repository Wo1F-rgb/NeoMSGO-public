// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSHomingProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

AMSHomingProjectile::AMSHomingProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// コリジョンは自前のWeaponTraceトレース(B-3)で行うため、ルートは形状なしのSceneComponent
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AMSHomingProjectile::InitHoming(AActor* InInstigatorActor, AActor* InHomingTarget,
	const float InSpeed, const float InTurnRateDegPerSec,
	const float InSplashRadius, const float InSplashDamage, const float InDownPoint)
{
	InstigatorActor = InInstigatorActor;
 LaunchShieldDamageScale = 1.f + (Cast<AMSUnitBase>(InInstigatorActor) ? CastChecked<AMSUnitBase>(InInstigatorActor)->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ShieldDamageIncrease)*.01f : 0.f);
	Speed = InSpeed;
	TurnRateDegPerSec = FMath::Max(0.0f, InTurnRateDegPerSec);
	SplashRadius = InSplashRadius;
	SplashDamage = InSplashDamage;
	DownPoint = InDownPoint;
	// Direct numeric callers are test/special-projectile hooks and use the new normal
	// explosion profile by default. Weapon data overrides this below for charge/smoke.
	MinFalloff = 0.1f;
	FullDamageRadiusRatio = 0.15f;
	bUsesSingleExplosionPacket = InSplashRadius > 0.0f && InSplashDamage > 0.0f;
	SetInstigator(Cast<APawn>(InInstigatorActor));

	// 追尾対象の受理判定は UMSTeamStatics::CanLockOn のみ(B-2)。
	// 味方/中立/死亡済み/非ロック対象(占領拠点等)は追尾しない=無誘導の直進弾になる(#161対策)。
	HomingTarget = UMSTeamStatics::CanLockOn(InInstigatorActor, InHomingTarget) ? InHomingTarget : nullptr;
}

void AMSHomingProjectile::InitFromWeaponData(const UMSHomingWeaponData* Weapon,
	AActor* InInstigatorActor, AActor* InHomingTarget)
{
	if (!Weapon)
	{
		InitHoming(InInstigatorActor, InHomingTarget);
		return;
	}
	WeaponData = Weapon;

	// Unity: speed = projectileSpeed>0 ? projectileSpeed : 20f / turnRate = ... : 90f(m→cm変換)
	// Explosive weapons received the gameplay-wide 1.75x projectile-speed pass.  Apply it
	// here too for callers that still spawn the physical homing actor instead of the visual path.
	const float SpeedCm = Weapon->ProjectileSpeed > 0.0f
		? Weapon->ProjectileSpeed * 100.0f * Weapon->RangeWorldScale * 1.75f
		: DefaultSpeed;
	const float TurnRate = Weapon->HomingTurnRateDegPerSec > 0.0f
		? Weapon->HomingTurnRateDegPerSec
		: DefaultTurnRateDegPerSec;

	const bool bPowerExplosion = Weapon->Power > 0 && Weapon->GetExplosionBaseDamage() > 0.0f;
	const float BaseExplosionDamage = bPowerExplosion
		? Weapon->GetExplosionBaseDamage()
		: static_cast<float>(Weapon->SplashDamage);
	InitHoming(InInstigatorActor, InHomingTarget, SpeedCm, TurnRate,
		Weapon->SplashRadius * 100.0f * Weapon->RangeWorldScale,
		BaseExplosionDamage,
		bPowerExplosion ? static_cast<float>(Weapon->DownPoint) : 0.0f);
	MinFalloff = Weapon->GetExplosionMinFalloff();
	FullDamageRadiusRatio = Weapon->GetExplosionFullDamageRadiusRatio();
	bUsesSingleExplosionPacket = bPowerExplosion;
}

void AMSHomingProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bExploded || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 寿命(6秒)超過で自爆(Unity: MaxLifetime。爆風を発生させて消滅)
	LifetimeElapsed += DeltaSeconds;
	if (LifetimeElapsed >= MaxLifetimeSeconds)
	{
		Explode(GetActorLocation(), nullptr);
		return;
	}

	// 対象消失(死亡・破棄)なら誘導を諦めて直進(再取得はしない)
	UpdateHomingTargetValidity();

	// 旋回(90°/s制限のRotateTowards)→ 前進+着弾検出
	RotateTowardsTarget(DeltaSeconds);
	MoveAndDetectImpact(DeltaSeconds);
}

void AMSHomingProjectile::UpdateHomingTargetValidity()
{
	if (!HomingTarget.IsValid())
	{
		HomingTarget.Reset();
		return;
	}

	// 追尾維持もCanLockOn単一API(B-2)。死亡(State.Dead→CanBeLockedOn=false)・
	// 破棄・陣営不整合はすべてここで追尾解除→以後は直進。
	if (!UMSTeamStatics::CanLockOn(InstigatorActor.Get(), HomingTarget.Get()))
	{
		HomingTarget.Reset();
	}
}

void AMSHomingProjectile::RotateTowardsTarget(const float DeltaSeconds)
{
	const AActor* Target = HomingTarget.Get();
	if (!Target)
	{
		return;
	}

	// 照準基準点(Unity: Health.AimPoint相当)。機体は胸元、コアはコア中心
	const IMSLockOnTargetInterface* LockOnTarget = Cast<IMSLockOnTargetInterface>(Target);
	const FVector AimPoint = LockOnTarget ? LockOnTarget->GetLockOnAimPoint() : Target->GetActorLocation();

	const FVector ToTarget = AimPoint - GetActorLocation();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	// Unity: Quaternion.RotateTowards(current, desired, turnRate * dt)
	const FQuat CurrentQuat = GetActorQuat();
	const FQuat DesiredQuat = ToTarget.GetSafeNormal().ToOrientationQuat();
	const float MaxStepRad = FMath::DegreesToRadians(TurnRateDegPerSec) * DeltaSeconds;
	const float AngleRad = CurrentQuat.AngularDistance(DesiredQuat);

	const FQuat NewQuat = (AngleRad <= MaxStepRad || AngleRad <= UE_KINDA_SMALL_NUMBER)
		? DesiredQuat
		: FQuat::Slerp(CurrentQuat, DesiredQuat, MaxStepRad / AngleRad);
	SetActorRotation(NewQuat);
}

bool AMSHomingProjectile::MoveAndDetectImpact(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = GetActorLocation();
	const FVector End = Start + GetActorForwardVector() * FMath::Max(0.0f, Speed) * DeltaSeconds;

	// 進行方向のWeaponTraceで障害物/機体への着弾を検出(Unity: Physics.Raycast。B-3単一チャンネル)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MSHomingProjectile), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(this);
	if (const AActor* Shooter = InstigatorActor.Get())
	{
		Params.AddIgnoredActor(Shooter);
	}

	FHitResult Hit;
	while (!Start.Equals(End) &&
		World->LineTraceSingleByChannel(Hit, Start, End, MSCollision::WeaponTraceChannel, Params))
	{
		if (!UMSCombatStatics::ShouldIgnoreWeaponTraceOccluder(Hit.GetActor()))
		{
			Explode(Hit.ImpactPoint, Hit.GetActor());
			return true;
		}

		// Water volumes report a blocking WeaponTrace hit in the live map, but
		// they are gameplay volumes rather than projectile occluders. Continue
		// along the same frame segment to the next blocking actor.
		Params.AddIgnoredActor(Hit.GetActor());
	}

	SetActorLocation(End);

	// 近接信管(Unity: HitRadius=1.5m。追尾対象へ十分接近したら着弾扱い)
	if (const AActor* Target = HomingTarget.Get())
	{
		const IMSLockOnTargetInterface* LockOnTarget = Cast<IMSLockOnTargetInterface>(Target);
		const FVector AimPoint = LockOnTarget ? LockOnTarget->GetLockOnAimPoint() : Target->GetActorLocation();
		if (FVector::DistSquared(End, AimPoint) <= FMath::Square(ProximityHitRadius))
		{
			Explode(End, HomingTarget.Get());
			return true;
		}
	}

	return false;
}

void AMSHomingProjectile::Explode(const FVector& Origin, AActor* DirectHitActor)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;

	// P4-13: 着弾と寿命切れ自爆を同じ爆発Cue経路へ集約する。
	if (AActor* SourceActor = InstigatorActor.Get())
	{
		if (UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SourceActor))
		{
			FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
			Context.AddInstigator(SourceActor, this);
			Context.AddSourceObject(WeaponData.Get());
			if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
			{
				MSContext->SetAttackerLocation(SourceActor->GetActorLocation());
			}
			FGameplayCueParameters CueParameters(Context);
			CueParameters.Location = Origin;
			CueParameters.SourceObject = WeaponData.Get();
			SourceASC->ExecuteGameplayCue(MSTags::Cue_Explosion, CueParameters);
		}
	}

	// 爆風(P2-4)。FF除外はGE側 UMSDamageExecution 手順1の単一実装(B-2)に委ねる。
	// 直撃対象も範囲内なら爆風を受ける(Unity版仕様: bExcludeDirectHitActor=false)。
	if (SplashRadius > 0.0f && SplashDamage > 0.0f)
	{
		const TArray<AActor*> IgnoreActors = { this };
		AActor* ExplosionCenterActor = bUsesSingleExplosionPacket ? DirectHitActor : nullptr;
		const int32 DamagedCount = UMSCombatStatics::ApplySplashDamage(this, Origin, SplashRadius, SplashDamage,
			IgnoreActors, InstigatorActor.Get(), DownPoint, ExplosionCenterActor, /*bExcludeDirectHitActor=*/false,
			MinFalloff, FullDamageRadiusRatio, LaunchShieldDamageScale);
		if (DamagedCount > 0)
		{
			if (AActor* Shooter = InstigatorActor.Get())
			{
				if (UGameInstance* GI = Shooter->GetGameInstance())
				{
					if (UMSHitFeedbackSubsystem* Feedback = GI->GetSubsystem<UMSHitFeedbackSubsystem>())
					{
						Feedback->BroadcastHitConfirmed(Shooter, false);
					}
				}
			}
		}
	}

	Destroy();
}
