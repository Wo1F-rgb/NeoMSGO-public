// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Fire_Shotgun.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSWeaponComponent.h"

void UGA_Fire_Shotgun::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Shooter = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bFired = Shooter && FireShotgun(Shooter, Shooter->GetActorLocation(), Shooter->GetActorForwardVector());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bFired);
}

bool UGA_Fire_Shotgun::FireShotgun(AActor* Shooter, const FVector& Origin, const FVector& Direction)
{
	return FireShotgunFromOrigins(Shooter, Origin, Direction, Origin);
}

bool UGA_Fire_Shotgun::FireShotgunFromOrigins(AActor* Shooter, const FVector& Origin,
	const FVector& Direction, const FVector& VisualOrigin)
{
	if (!Shooter || Direction.IsNearlyZero())
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Shooter->FindComponentByClass<UMSWeaponComponent>();
	if (!Weapons)
	{
		return false;
	}

	const int32 SlotIndex = Weapons->GetActiveSlotIndex();
	const UMSShotgunWeaponData* Weapon = Cast<UMSShotgunWeaponData>(Weapons->GetWeaponData(SlotIndex));
	if (!Weapon || Weapon->PelletCount <= 0 ||
		!Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}
	if (UAbilitySystemComponent* SourceASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter))
	{
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(Shooter, Shooter);
		Context.AddSourceObject(Weapon);
		FGameplayCueParameters FireParameters(Context);
		FireParameters.Location = VisualOrigin;
		FireParameters.SourceObject = Weapon;
		SourceASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
	}

	FRandomStream Random(FMath::Rand());
	for (int32 PelletIndex = 0; PelletIndex < Weapon->PelletCount; ++PelletIndex)
	{
		const FVector PelletDirection = MakePelletDirection(Direction, Weapon->PelletSpreadDegrees, Random);
		ResolveAndApplyPellet(Shooter, Origin, PelletDirection, Weapon, 1.0f, &VisualOrigin,
			/*bEmitFireCue=*/false);
	}
	return true;
}

FVector UGA_Fire_Shotgun::MakePelletDirection(const FVector& AimDirection, float SpreadDegrees,
	FRandomStream& Random)
{
	const FVector NormalizedAim = AimDirection.GetSafeNormal();
	if (NormalizedAim.IsNearlyZero() || SpreadDegrees <= 0.0f)
	{
		return NormalizedAim;
	}
	return Random.VRandCone(NormalizedAim, FMath::DegreesToRadians(FMath::Clamp(SpreadDegrees, 0.0f, 90.0f)));
}
