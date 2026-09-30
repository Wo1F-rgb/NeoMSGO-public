// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_ContinuousBeam.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "Player/MSPlayerCharacter.h"
#include "TimerManager.h"
#include "Weapons/MSWeaponComponent.h"

UGA_Fire_ContinuousBeam::UGA_Fire_ContinuousBeam()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Fire_ContinuousBeam::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Shooter || !StartBeam(Shooter, Shooter->GetActorLocation(), Shooter->GetActorForwardVector()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The beam keeps ticking on its own timer; this task only detects the trigger release.
	UAbilityTask_WaitInputRelease* ReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	ReleaseTask->OnRelease.AddDynamic(this, &UGA_Fire_ContinuousBeam::OnInputReleased);
	ReleaseTask->ReadyForActivation();
}

FVector UGA_Fire_ContinuousBeam::GetBeamVisualOrigin(const FVector& Origin, const FVector& Direction)
{
	return Origin + Direction.GetSafeNormal() * BeamVisualForwardOffsetCm;
}

bool UGA_Fire_ContinuousBeam::StartBeam(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	const FVector LegacyOrigin = GetBeamVisualOrigin(Origin, Direction);
	return StartBeamFromOrigins(Shooter, LegacyOrigin, Direction, LegacyOrigin);
}

bool UGA_Fire_ContinuousBeam::StartBeamFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	const AMSPlayerCharacter* PlayerShooter = Cast<AMSPlayerCharacter>(Shooter);
	const UAbilitySystemComponent* ShooterASC = Shooter ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter) : nullptr;
	if (!Shooter || !Shooter->HasAuthority() || Shooter->IsActorBeingDestroyed() || bBeamActive
		|| (ShooterASC && ShooterASC->HasMatchingGameplayTag(MSTags::State_Dead))
		|| (PlayerShooter && !PlayerShooter->IsGameplayInputAllowed()))
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	const int32 SlotIndex = Weapons ? Weapons->GetActiveSlotIndex() : INDEX_NONE;
	const UMSContinuousBeamWeaponData* Weapon = Weapons
		? Cast<UMSContinuousBeamWeaponData>(Weapons->GetWeaponData(SlotIndex))
		: nullptr;
	if (!Weapon || Weapon->BeamTickInterval <= 0.0f || !Weapons->CanFire(SlotIndex))
	{
		return false;
	}

	BeamShooter = Shooter;
	BeamWeapon = Weapon;
	BeamOrigin = Origin;
	BeamVisualOrigin = VisualOrigin;
	BeamDirection = Direction.GetSafeNormal();
	BeamStartTransform = Shooter->GetActorTransform();
	BeamSlotIndex = SlotIndex;
	TickCount = 0;
	bBeamActive = true;
	Weapons->OnWeaponSwitched.AddDynamic(this, &UGA_Fire_ContinuousBeam::OnWeaponSwitched);

	// P4-13: ループCueはビーム開始/終了と同じ寿命でASCに保持する。
	if (UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
	{
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(Shooter, Shooter);
		Context.AddSourceObject(Weapon);
		if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
		{
			MSContext->SetAttackerLocation(BeamVisualOrigin);
		}
		FGameplayCueParameters CueParameters(Context);
		CueParameters.Location = BeamVisualOrigin + BeamDirection * Weapon->GetRangeWorldCm();
		CueParameters.SourceObject = Weapon;
		SourceASC->AddGameplayCue(MSTags::Cue_Weapon_BeamLoop, CueParameters);
	}

	// First tick fires synchronously (matches GA_Fire_Burst/GA_Fire_Charge convention); the
	// repeating timer then re-resolves the shared trace every BeamTickInterval while held.
	FireTick();
	if (bBeamActive)
	{
		Shooter->GetWorldTimerManager().SetTimer(BeamTickTimer, this, &UGA_Fire_ContinuousBeam::FireTick,
			Weapon->BeamTickInterval, true, Weapon->BeamTickInterval);
	}
	return true;
}

void UGA_Fire_ContinuousBeam::FireTick()
{
	AActor* Shooter = BeamShooter.Get();
	const UMSContinuousBeamWeaponData* Weapon = BeamWeapon.Get();
	const AMSPlayerCharacter* PlayerShooter = Cast<AMSPlayerCharacter>(Shooter);
	const UAbilitySystemComponent* ShooterASC = Shooter ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter) : nullptr;
	if (!bBeamActive || !Shooter || !Shooter->HasAuthority() || Shooter->IsActorBeingDestroyed() || !Weapon
		|| (ShooterASC && ShooterASC->HasMatchingGameplayTag(MSTags::State_Dead))
		|| (PlayerShooter && !PlayerShooter->IsGameplayInputAllowed()))
	{
		StopBeam();
		return;
	}
	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	if (!Weapons || Weapons->GetActiveSlotIndex() != BeamSlotIndex || !Weapons->ConsumeAmmoDuringBurst(BeamSlotIndex))
	{
		StopBeam();
		return;
	}

	// 実プレイ報告バグ対策(バーストと同類): 照射中に機体が移動・旋回しても、毎Tickの
	// トレース起点・方向は「現在の」カメラ/銃口(BOTは機体追従の相対再適用)を使う。
	FVector TraceOrigin;
	FVector Direction;
	FVector VisualOrigin;
	UMSCombatStatics::ResolveCurrentFireOrigins(Shooter, BeamStartTransform,
		BeamOrigin, BeamDirection, BeamVisualOrigin, TraceOrigin, Direction, VisualOrigin);
	const float TickDamage = Weapon->BeamDamagePerSecond * Weapon->BeamTickInterval;
	const float TickDownPoint = Weapon->BeamDownPointPerSecond * Weapon->BeamTickInterval;
	ResolveAndApplyDamage(Shooter, TraceOrigin, Direction, Weapon, TickDamage, TickDownPoint,
		&VisualOrigin);
	++TickCount;
	if (Weapons->IsReloading(BeamSlotIndex))
	{
		StopBeam();
	}
}

void UGA_Fire_ContinuousBeam::StopBeam()
{
	if (AActor* Shooter = BeamShooter.Get())
	{
		Shooter->GetWorldTimerManager().ClearTimer(BeamTickTimer);
		if (UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
		{
			SourceASC->RemoveGameplayCue(MSTags::Cue_Weapon_BeamLoop);
		}
		if (UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>())
		{
			Weapons->OnWeaponSwitched.RemoveDynamic(this, &UGA_Fire_ContinuousBeam::OnWeaponSwitched);
		}
	}
	bBeamActive = false;
	BeamSlotIndex = INDEX_NONE;
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_Fire_ContinuousBeam::OnInputReleased(float TimeHeld)
{
	StopBeam();
}

void UGA_Fire_ContinuousBeam::OnWeaponSwitched(int32 NewSlotIndex)
{
	StopBeam();
}

void UGA_Fire_ContinuousBeam::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bBeamActive)
	{
		StopBeam();
		return;
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
