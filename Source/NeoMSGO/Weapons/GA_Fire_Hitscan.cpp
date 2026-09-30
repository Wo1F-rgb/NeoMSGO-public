// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_Hitscan.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Gameplay/MSRadarPod.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/GameInstance.h"
#include "Weapons/MSWeaponComponent.h"

UGA_Fire_Hitscan::UGA_Fire_Hitscan()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Fire_Hitscan::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bFired = Shooter && Fire(Shooter, Shooter->GetActorLocation(), Shooter->GetActorForwardVector());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bFired);
}

bool UGA_Fire_Hitscan::Fire(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	return FireFromOrigins(Shooter, Origin, Direction, Origin);
}

bool UGA_Fire_Hitscan::FireFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	if (!Shooter)
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	if (!Weapons)
	{
		return false;
	}

	const int32 SlotIndex = Weapons->GetActiveSlotIndex();
	const UMSRangedWeaponData* Weapon = Cast<UMSRangedWeaponData>(Weapons->GetWeaponData(SlotIndex));
	if (!Weapon || !Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	// P6-2: BOT弱体化のaimSpreadDegrees(既定4°)をShooter側から注入する。プレイヤーは
	// AMSUnitBase既定のGetCombatModifiers()=0°のため挙動は変わらない。
	FVector FireDirection = Direction;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
	{
		const float SpreadDegrees = Unit->GetCombatModifiers().AimSpreadDegrees;
		if (SpreadDegrees > 0.0f)
		{
			FireDirection = FMath::VRandCone(Direction.GetSafeNormal(), FMath::DegreesToRadians(FMath::Clamp(SpreadDegrees, 0.0f, 90.0f)));
		}
	}

	ResolveAndApplyPellet(Shooter, Origin, FireDirection, Weapon, 1.0f, &VisualOrigin);
	return true;
}

bool UGA_Fire_Hitscan::ResolveAndApplyPellet(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const UMSRangedWeaponData* Weapon, float DamageMultiplier,
	const FVector* VisualOrigin, const bool bEmitFireCue,
	const FMSChargeShotMultipliers* ChargeMultipliers) const
{
	if (!Weapon)
	{
		return false;
	}

	const float Multiplier = FMath::Max(0.0f, DamageMultiplier);
	const float DownPointMultiplier = ChargeMultipliers
		? FMath::Max(0.0f, ChargeMultipliers->DownPoint) : Multiplier;
	if (!Weapon->IsHitscan())
	{
		// P6-2: BOT弱体化のdamageMultiplier(既定0.3)は即着ダメージ経路(ResolveAndApplyDamage)と
		// 同様に、可視弾の確定ダメージ(発射時点で固定)にも適用する必要がある。
		float ShooterDamageMultiplier = 1.0f;
		if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
		{
			ShooterDamageMultiplier = Unit->GetCombatModifiers().DamageMultiplier;
		}
		// Visual-projectile weapons do not enter ResolveAndApplyDamage until impact, so emit their
		// launch cue here and carry the muzzle origin explicitly.
		if (UAbilitySystemComponent* SourceASC = bEmitFireCue
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter) : nullptr)
		{
			FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
			Context.AddInstigator(Shooter, Shooter);
			Context.AddSourceObject(Weapon);
			if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
			{
				MSContext->SetAttackerLocation(VisualOrigin ? *VisualOrigin : Origin);
			}
			FGameplayCueParameters FireParameters(Context);
			FireParameters.Location = VisualOrigin ? *VisualOrigin : Origin;
			FireParameters.SourceObject = Weapon;
			SourceASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
		}
		const float ShooterScale = FMath::Max(0.0f, ShooterDamageMultiplier);
		FMSChargeShotMultipliers EffectiveChargeMultipliers;
		if (ChargeMultipliers)
		{
			EffectiveChargeMultipliers = *ChargeMultipliers;
			EffectiveChargeMultipliers.Damage = Multiplier * ShooterScale;
			EffectiveChargeMultipliers.DownPoint = DownPointMultiplier * ShooterScale;
		}
		return AMSVisualProjectile::LaunchVisualProjectileFromOrigins(
			Shooter, Origin, VisualOrigin ? *VisualOrigin : Origin, Direction, Weapon,
			Multiplier * ShooterScale,
			ChargeMultipliers ? &EffectiveChargeMultipliers : nullptr) != nullptr;
	}

	return ResolveAndApplyDamage(Shooter, Origin, Direction, Weapon,
		Weapon->Power * Multiplier, Weapon->DownPoint * DownPointMultiplier,
		VisualOrigin, bEmitFireCue);
}

bool UGA_Fire_Hitscan::ResolveAndApplyDamage(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const UMSRangedWeaponData* Weapon, float Damage, float DownPointAmount,
	const FVector* VisualOrigin, const bool bEmitFireCue) const
{
	if (!Shooter || !Weapon)
	{
		return false;
	}

	const FMSHitscanResult Trace = UMSCombatStatics::ResolveHitscan(
		Shooter, Origin, Direction, Weapon->GetRangeWorldCm(), Shooter);

	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter);
	FGameplayEffectContextHandle Context = SourceASC
		? SourceASC->MakeEffectContext()
		: FGameplayEffectContextHandle(new FMSGameplayEffectContext());
	Context.AddInstigator(Shooter, Shooter);
	Context.AddSourceObject(Weapon);
	Context.AddHitResult(Trace.Hit, true);
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
	{
		MSContext->SetHeadshot(Trace.bIsHeadshot);
		MSContext->SetAttackerLocation(VisualOrigin ? *VisualOrigin : Origin);
	}

	// P4-13: 全Hitscan系(Player/BOT、Shotgun/Burst/Charge/ContinuousBeamを含む)の共通発火点。
	if (SourceASC)
	{
		if (bEmitFireCue)
		{
			FGameplayCueParameters FireParameters(Context);
			FireParameters.Location = VisualOrigin ? *VisualOrigin : Origin;
			FireParameters.SourceObject = Weapon;
			SourceASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
		}

		FGameplayCueParameters TracerParameters(Context);
		TracerParameters.Location = Trace.EndPoint;
		TracerParameters.SourceObject = Weapon;
		SourceASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Tracer, TracerParameters);
	}
	if (!Trace.bBlockingHit || !Trace.bCanDamage)
	{
		// Friendly and structure hits stop the shot, but neither receives damage.
		return true;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Trace.Hit.GetActor());
	if (!TargetASC)
	{
		if (AMSRadarPod* Radar = Cast<AMSRadarPod>(Trace.Hit.GetActor()))
		{
			const float Multiplier = Cast<AMSUnitBase>(Shooter)
				? CastChecked<AMSUnitBase>(Shooter)->GetCombatModifiers().DamageMultiplier : 1.0f;
			Radar->ApplyWeaponDamage(Shooter, Damage * FMath::Max(0.0f, Multiplier));
		}
		return true;
	}

	// P6-2: BOT弱体化のdamageMultiplier(既定0.3)をShooter側から注入する。プレイヤーは
	// AMSUnitBase既定のGetCombatModifiers()=1.0のため挙動は変わらない。全ランタイプ
	// (Hitscan/Shotgun/Burst/Charge/ContinuousBeam)がResolveAndApplyPellet経由でここへ集約する。
	float ShooterDamageMultiplier = 1.0f;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
	{
		ShooterDamageMultiplier = Unit->GetCombatModifiers().DamageMultiplier;
	}

	FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
	Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, FMath::Max(0.0f, Damage) * FMath::Max(0.0f, ShooterDamageMultiplier));
	Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, FMath::Max(0.0f, DownPointAmount) * FMath::Max(0.0f, ShooterDamageMultiplier));
	if (SourceASC)
	{
		SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
	}
	else
	{
		TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
	}

	if (SourceASC)
	{
		FGameplayCueParameters ImpactParameters(Context);
		ImpactParameters.Instigator = Shooter;
		ImpactParameters.Location = Trace.Hit.ImpactPoint;
		ImpactParameters.Normal = Trace.Hit.ImpactNormal;
		ImpactParameters.PhysicalMaterial = Trace.Hit.PhysMaterial;
		ImpactParameters.SourceObject = Weapon;
		const FGameplayTag ImpactTag = Trace.bIsHeadshot
			? MSTags::Cue_Impact_Headshot
			: MSTags::Cue_Impact_Normal;
		SourceASC->ExecuteGameplayCue(ImpactTag, ImpactParameters);
		SourceASC->ExecuteGameplayCue(MSTags::Cue_HitMarker, ImpactParameters);
	}

	if (UGameInstance* GameInstance = Shooter->GetGameInstance())
	{
		if (UMSHitFeedbackSubsystem* HitFeedback = GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
		{
			HitFeedback->BroadcastHitConfirmed(Shooter, Trace.bIsHeadshot);
		}
	}
	return true;
}
