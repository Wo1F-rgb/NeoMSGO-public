// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_Homing.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "TimerManager.h"
#include "Weapons/MSWeaponComponent.h"

UGA_Fire_Homing::UGA_Fire_Homing()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Fire_Homing::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bFired = Shooter && FireHoming(
		Shooter, Shooter->GetActorLocation(), Shooter->GetActorForwardVector());
	// サルボ継続中はGA_Fire_Burst同様にアビリティを維持し、FinishSalvoで終了する。
	if (!bFired || !bSalvoActive)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, !bFired);
	}
}

bool UGA_Fire_Homing::FireHoming(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	return FireHomingFromOrigins(Shooter, Origin, Direction, Origin);
}

bool UGA_Fire_Homing::FireHomingFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	const AMSPlayerCharacter* PlayerShooter = Cast<AMSPlayerCharacter>(Shooter);
	const UAbilitySystemComponent* SourceASC = Shooter ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter) : nullptr;
	if (!Shooter || !Shooter->HasAuthority() || Shooter->IsActorBeingDestroyed() || bSalvoActive
		|| (SourceASC && SourceASC->HasMatchingGameplayTag(MSTags::State_Dead))
		|| (PlayerShooter && !PlayerShooter->IsGameplayInputAllowed()))
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	if (!Weapons)
	{
		return false;
	}

	const int32 SlotIndex = Weapons->GetActiveSlotIndex();
	const UMSHomingWeaponData* Weapon = Cast<UMSHomingWeaponData>(Weapons->GetWeaponData(SlotIndex));
	// AI must acquire through its real controller-target dwell before spending ammunition.
	// Player uncaptured dumbfire and generic non-component fixture fallback remain unchanged.
	if (const auto* Lock=Shooter->FindComponentByClass<UMSLockOnComponent>(); Lock && Lock->IsAIHomingOnly())
	{
		bool Manual=false, Tracking=false, Captured=false;
		if (!Lock->GetIncomingThreatState(Lock->GetHomingCapturedTarget(),Manual,Tracking,Captured) || !Captured) return false;
	}
	if (!Weapon || !Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	const float RangeCm = FMath::Max(Weapon->GetRangeWorldCm(), Weapon->GetLockOnRangeWorldCm());
	AActor* InitialTarget = SelectHomingTarget(Shooter, Origin, Direction, RangeCm);
	const bool bLaunched = LaunchHomingRound(Shooter, Weapon, Origin, Direction, VisualOrigin, InitialTarget);

	// ユーザーレビュー#68: 1トリガーでマガジン残弾を発射間隔ごとに全弾自動発射する。
	// 押しっぱなしは不要。残弾が無い(単発武器/無限弾)場合は従来どおり単発で終わる。
	const int32 RemainingRounds = Weapon->HasInfiniteAmmo() ? 0 : Weapons->GetMagazineAmmo(SlotIndex);
	if (bLaunched && RemainingRounds > 0)
	{
		SalvoShooter = Shooter;
		SalvoWeapon = Weapon;
		SalvoTarget = InitialTarget;
		SalvoOrigin = Origin;
		SalvoVisualOrigin = VisualOrigin;
		SalvoDirection = Direction.GetSafeNormal();
		SalvoStartTransform = Shooter->GetActorTransform();
		SalvoSlotIndex = SlotIndex;
		SalvoRoundsFired = 1;
		bSalvoActive = true;

		const float Interval = Weapon->BurstInterval > 0.0f
			? Weapon->BurstInterval
			: DefaultSalvoIntervalSeconds;
		// サルボ完了まで新しいトリガーが割り込めないようにする(GA_Fire_Burstと同方針)。
		Weapons->EnsureFireCooldown(FMath::Max(Weapon->FireInterval, RemainingRounds * Interval));
		Shooter->GetWorldTimerManager().SetTimer(SalvoTimer, this, &UGA_Fire_Homing::FireNextSalvoRound,
			Interval, true, Interval);
	}
	return bLaunched;
}

void UGA_Fire_Homing::FireNextSalvoRound()
{
	AActor* Shooter = SalvoShooter.Get();
	const UMSHomingWeaponData* Weapon = SalvoWeapon.Get();
	UMSWeaponComponent* Weapons = Shooter ? Shooter->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const UAbilitySystemComponent* ShooterASC = Shooter
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter)
		: nullptr;
	const AMSPlayerCharacter* PlayerShooter = Cast<AMSPlayerCharacter>(Shooter);
	// A retained online pawn without an admitted/deployed Controller cannot fire.
	// Generic units/BOTs keep their normal authority/death/weapon checks.
	if (!Shooter || !Shooter->HasAuthority() || Shooter->IsActorBeingDestroyed() || !Weapon || !Weapons ||
		(PlayerShooter && !PlayerShooter->IsGameplayInputAllowed()) ||
		(ShooterASC && ShooterASC->HasMatchingGameplayTag(MSTags::State_Dead)) ||
		Weapons->GetActiveSlotIndex() != SalvoSlotIndex ||
		!Weapons->ConsumeAmmoDuringBurst(SalvoSlotIndex))
	{
		FinishSalvo();
		return;
	}

	// 各弾は「発射時点」の銃口位置・照準方向から出る(GA_Fire_Burstの実プレイ報告バグ対策を踏襲)。
	FVector TraceOrigin;
	FVector Direction;
	FVector VisualOrigin;
	UMSCombatStatics::ResolveCurrentFireOrigins(Shooter, SalvoStartTransform,
		SalvoOrigin, SalvoDirection, SalvoVisualOrigin, TraceOrigin, Direction, VisualOrigin);
	AActor* FixedTarget = SalvoTarget.Get();
	if (FixedTarget && !UMSTeamStatics::CanLockOn(Shooter, FixedTarget))
	{
		FixedTarget = nullptr;
	}
	LaunchHomingRound(Shooter, Weapon, TraceOrigin, Direction, VisualOrigin, FixedTarget);
	++SalvoRoundsFired;
	if (!Weapon->HasInfiniteAmmo() && Weapons->GetMagazineAmmo(SalvoSlotIndex) <= 0)
	{
		FinishSalvo();
	}
}

void UGA_Fire_Homing::CancelSalvo()
{
	FinishSalvo();
}

void UGA_Fire_Homing::FinishSalvo()
{
	if (AActor* Shooter = SalvoShooter.Get())
	{
		Shooter->GetWorldTimerManager().ClearTimer(SalvoTimer);
	}
	bSalvoActive = false;
	SalvoTarget.Reset();
	SalvoSlotIndex = INDEX_NONE;
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_Fire_Homing::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (bSalvoActive)
	{
		FinishSalvo();
		return;
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGA_Fire_Homing::LaunchHomingRound(AActor* Shooter, const UMSHomingWeaponData* Weapon,
	const FVector& TraceOrigin, const FVector& Direction, const FVector& VisualOrigin,
	AActor* HomingTarget) const
{
	if (!Shooter || !Weapon)
	{
		return false;
	}

	float DamageMultiplier = 1.0f;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
	{
		DamageMultiplier = FMath::Max(0.0f, Unit->GetCombatModifiers().DamageMultiplier);
	}
	if (UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
	{
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(Shooter, Shooter);
		Context.AddSourceObject(Weapon);
		if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
		{
			MSContext->SetAttackerLocation(VisualOrigin);
		}
		FGameplayCueParameters FireParameters(Context);
		FireParameters.Location = VisualOrigin;
		FireParameters.SourceObject = Weapon;
		SourceASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
	}

	return AMSVisualProjectile::LaunchHomingVisualProjectileFromOrigins(
		Shooter, TraceOrigin, VisualOrigin, Direction, Weapon, HomingTarget, DamageMultiplier) != nullptr;
}

AActor* UGA_Fire_Homing::SelectHomingTarget(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const float RangeCm) const
{
	if (const UMSLockOnComponent* LockOn = Shooter->FindComponentByClass<UMSLockOnComponent>())
	{
		if (AActor* Captured = LockOn->GetHomingCapturedTarget();
			Captured && UMSTeamStatics::CanLockOn(Shooter, Captured))
		{
			return Captured;
		}
		// 実プレイ仕様: ロックオンコンポーネント搭載機(プレイヤー)は「dwell完了済みの捕捉
		// ターゲット」のみ使用する。未完了時のフォールバック走査は行わない(無誘導弾として発射)。
		return nullptr;
	}

	UWorld* World = Shooter->GetWorld();
	if (!World || RangeCm <= 0.0f)
	{
		return nullptr;
	}

	const FVector AimDirection = Direction.GetSafeNormal();
	constexpr float AimConeDegrees = 30.0f;
	const float MinAimDot = FMath::Cos(FMath::DegreesToRadians(AimConeDegrees));
	AActor* BestTarget = nullptr;
	float BestScore = -FLT_MAX;
	for (TActorIterator<AMSUnitBase> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!UMSTeamStatics::CanLockOn(Shooter, Candidate))
		{
			continue;
		}

		const IMSLockOnTargetInterface* Lockable = Cast<IMSLockOnTargetInterface>(Candidate);
		const FVector AimPoint = Lockable ? Lockable->GetLockOnAimPoint() : Candidate->GetActorLocation();
		const FVector ToCandidate = AimPoint - Origin;
		const float Distance = ToCandidate.Size();
		const float AimDot = FVector::DotProduct(AimDirection, ToCandidate.GetSafeNormal());
		if (Distance <= 0.0f || Distance > RangeCm || AimDot < MinAimDot)
		{
			continue;
		}

		const float Score = AimDot * 0.65f + (1.0f - Distance / RangeCm) * 0.35f;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}
	return BestTarget;
}
