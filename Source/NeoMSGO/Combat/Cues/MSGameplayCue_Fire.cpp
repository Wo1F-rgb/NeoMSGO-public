// Copyright NeoMSGO Project. All Rights Reserved.
#include "Combat/Cues/MSGameplayCue_Fire.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSUnitBase.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/MSWeaponVisualComponent.h"

UMSGameplayCue_Fire::UMSGameplayCue_Fire()
{
	GameplayCueTag = MSTags::Cue_Weapon_Fire;
	GameplayCueName = GameplayCueTag.GetTagName();
	static ConstructorHelpers::FObjectFinder<USoundBase> BeamFinder(TEXT("/Game/Audio/Imported/SFX_Fire_BeamRifle.SFX_Fire_BeamRifle"));
	static ConstructorHelpers::FObjectFinder<USoundBase> RifleFinder(TEXT("/Game/Audio/Imported/SFX_Fire_Rifle.SFX_Fire_Rifle"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SMGFinder(TEXT("/Game/Audio/Imported/SFX_Fire_SMG.SFX_Fire_SMG"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BazookaFinder(TEXT("/Game/Audio/Imported/SFX_Fire_Bazooka.SFX_Fire_Bazooka"));
	static ConstructorHelpers::FObjectFinder<USoundBase> LMGFinder(TEXT("/Game/Audio/Imported/SFX_Fire_LMG.SFX_Fire_LMG"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunFinder(TEXT("/Game/Audio/Imported/SFX_Fire_Shotgun.SFX_Fire_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MeleeFinder(TEXT("/Game/Audio/Imported/SFX_Fire_Melee.SFX_Fire_Melee"));
	BeamRifleSound = BeamFinder.Object;
	RifleSound = RifleFinder.Object;
	SMGSound = SMGFinder.Object;
	BazookaSound = BazookaFinder.Object;
	LMGSound = LMGFinder.Object;
	ShotgunSound = ShotgunFinder.Object;
	MeleeSound = MeleeFinder.Object;
}

bool UMSGameplayCue_Fire::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	const FVector Muzzle = ResolveMuzzleLocation(MyTarget, Parameters);
	USoundBase* TypeASound = ResolveFireSound(Parameters.SourceObject.Get());
	EMSAudioUsage Usage = TypeASound == BeamRifleSound ? EMSAudioUsage::FireBeamRifle
		: TypeASound == SMGSound ? EMSAudioUsage::FireSMG
		: TypeASound == BazookaSound ? EMSAudioUsage::FireBazooka
		: TypeASound == LMGSound ? EMSAudioUsage::FireLMG
		: TypeASound == ShotgunSound ? EMSAudioUsage::FireShotgun
		: TypeASound == MeleeSound ? EMSAudioUsage::MeleeAttack
		: EMSAudioUsage::FireRifle;
	if (TypeASound == BazookaSound)
	{
		const FString SourceName = Parameters.SourceObject.IsValid()
			? Parameters.SourceObject->GetName() : FString();
		if (SourceName.Contains(TEXT("Mortar")) || SourceName.Contains(TEXT("Cannon")))
		{
			Usage = EMSAudioUsage::FireCannon;
		}
		else if (SourceName.Contains(TEXT("Grenade")))
		{
			Usage = EMSAudioUsage::FireOrdnance;
		}
	}
	MSAudio::SpawnMetaSoundAtLocation(
		MyTarget, MSAudioPreset::Resolve(Usage, TypeASound), Muzzle, 1000.0f);
	// A saber swing has its own moving blade. The shared fire cue must not emit
	// a gun muzzle flash in front of it during the wind-up.
	if (Usage == EMSAudioUsage::MeleeAttack) return MyTarget != nullptr;
	if (UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr)
	{
		const AActor* Shooter = Parameters.Instigator.Get();
		const FVector Forward = Shooter ? Shooter->GetActorForwardVector() : FVector::ForwardVector;
		// 恒久実装: 実行時生成StaticMeshComponent(発光Cube)によるマズルフラッシュ。
		// Shipping相当でも描画される(AMSTransientVfxActor::SpawnMuzzleFlash)。
		AMSTransientVfxActor::SpawnMuzzleFlash(World, Muzzle, Forward);
	}
	return MyTarget != nullptr;
}

FVector UMSGameplayCue_Fire::ResolveMuzzleLocation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	AActor* Shooter = Parameters.Instigator.Get();
	if (!Shooter)
	{
		Shooter = MyTarget;
	}
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Shooter))
	{
		if (const UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent())
		{
			return Visual->GetMuzzleWorldLocation();
		}
	}
	return FVector(Parameters.Location);
}

USoundBase* UMSGameplayCue_Fire::ResolveFireSound(const UObject* SourceObject) const
{
	const UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(SourceObject);
	if (!Weapon)
	{
		return RifleSound;
	}
	if (!Weapon->CategoryTag.IsValid())
	{
		// Match the ability router's legacy-data fallback for assets predating category tags.
		if (Cast<UMSShotgunWeaponData>(Weapon))
		{
			return ShotgunSound;
		}
		if (Cast<UMSHomingWeaponData>(Weapon))
		{
			return BazookaSound;
		}
		if (Cast<UMSContinuousBeamWeaponData>(Weapon))
		{
			return BeamRifleSound;
		}
		if (Cast<UMSMeleeWeaponData>(Weapon))
		{
			return MeleeSound;
		}
	}
	if (Weapon->CategoryTag == MSTags::Weapon_Category_BeamRifle ||
		Weapon->CategoryTag == MSTags::Weapon_Category_ContinuousBeam)
	{
		return BeamRifleSound;
	}
	if (Weapon->CategoryTag == MSTags::Weapon_Category_Bazooka ||
		Weapon->CategoryTag == MSTags::Weapon_Category_Homing)
	{
		return BazookaSound;
	}
	if (Weapon->CategoryTag == MSTags::Weapon_Category_Melee)
	{
		return MeleeSound;
	}
	if (Weapon->CategoryTag == MSTags::Weapon_Category_Shotgun)
	{
		return ShotgunSound;
	}
	if (Weapon->CategoryTag == MSTags::Weapon_Category_MachineGun)
	{
		// The current tag schema has no Rifle/SMG/LMG categories. Scope is the stable
		// sniper discriminator; built-in MachineGun weapons are Vulcan-class (SMG),
		// while remaining handheld MachineGun assets use the LMG recording.
		if (const UMSRangedWeaponData* Ranged = Cast<UMSRangedWeaponData>(Weapon))
		{
			if (Ranged->bIsScopeWeapon)
			{
				return RifleSound;
			}
		}
		return Weapon->bIsBuiltIn ? SMGSound : LMGSound;
	}
	return RifleSound;
}
