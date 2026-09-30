// Copyright NeoMSGO Project. All Rights Reserved.
#include "Combat/Cues/MSGameplayCue_Impact.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	bool ExecuteImpact(AActor* MyTarget, const FGameplayCueParameters& Parameters,
		const bool bExpectedHeadshot, USoundBase* Sound, const EMSAudioUsage Usage, const float Frequency)
	{
		// Hit/headshot confirmation is player feedback, not world ambience. Keep it 2D so
		// distance, camera orientation and attenuation can never hide a confirmed hit.
		// MyTarget is the ASC that dispatched the Cue and is not a reliable damage-role
		// identifier. EffectContext.Instigator is explicitly populated by every weapon path.
		MSAudio::PlayMetaSound2DIfLocallyControlled(Parameters.Instigator.Get(),
			MSAudioPreset::Resolve(Usage, Sound), Frequency,
			bExpectedHeadshot ? 0.32f : 0.26f, bExpectedHeadshot ? 0.24f : 0.12f);
		if (UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr)
		{
			const FVector Impact(Parameters.Location);
			const UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(Parameters.SourceObject.Get());
			const UMSRangedWeaponData* RangedWeapon = Cast<UMSRangedWeaponData>(Weapon);
			const bool bExplosive = RangedWeapon && RangedWeapon->SplashRadius > 0.0f;
			const bool bBeam = Weapon && (Weapon->IsA<UMSContinuousBeamWeaponData>() ||
				Weapon->CategoryTag == MSTags::Weapon_Category_BeamRifle);
			const EMSImpactVisualStyle Style = bExplosive
				? EMSImpactVisualStyle::Explosive
				: (bBeam ? EMSImpactVisualStyle::Beam : EMSImpactVisualStyle::Kinetic);
			const FLinearColor SparkColor = bExpectedHeadshot
				? FLinearColor::Yellow
				: (bBeam ? FLinearColor(0.18f, 0.72f, 1.0f)
					: (bExplosive ? FLinearColor(1.0f, 0.24f, 0.035f)
						: FLinearColor(1.0f, 0.61f, 0.22f)));
			// 恒久実装: 実行時生成StaticMeshComponent(発光Cube)による着弾スパーク。
			// Shipping相当でも描画される(AMSTransientVfxActor::SpawnImpactSparks)。
			AMSTransientVfxActor::SpawnImpactSparks(
				World, Impact, SparkColor, bExpectedHeadshot || bExplosive,
				bExplosive ? 0.28f : (bBeam ? 0.22f : 0.19f), Style);
		}
		return MyTarget != nullptr;
	}
}

UMSGameplayCue_Impact_Normal::UMSGameplayCue_Impact_Normal()
{
	GameplayCueTag = MSTags::Cue_Impact_Normal;
	GameplayCueName = GameplayCueTag.GetTagName();
	static ConstructorHelpers::FObjectFinder<USoundBase> MeleeFinder(
		TEXT("/Game/Audio/Imported/SFX_Impact_Melee.SFX_Impact_Melee"));
	static ConstructorHelpers::FObjectFinder<USoundBase> OtherFinder(
		TEXT("/Game/Audio/Imported/SFX_Impact_Other.SFX_Impact_Other"));
	MeleeImpactSound = MeleeFinder.Object;
	OtherImpactSound = OtherFinder.Object;
}
bool UMSGameplayCue_Impact_Normal::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	return ExecuteImpact(MyTarget, Parameters, false,
		ResolveImpactSound(Parameters.SourceObject.Get()),
		ResolveImpactSound(Parameters.SourceObject.Get()) == MeleeImpactSound
			? EMSAudioUsage::MeleeHit : EMSAudioUsage::HitNormal, 1000.0f);
}

USoundBase* UMSGameplayCue_Impact_Normal::ResolveImpactSound(const UObject* SourceObject) const
{
	const UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(SourceObject);
	return Weapon && (Weapon->CategoryTag == MSTags::Weapon_Category_Melee ||
		Weapon->IsA<UMSMeleeWeaponData>())
		? MeleeImpactSound : OtherImpactSound;
}

UMSGameplayCue_Impact_Headshot::UMSGameplayCue_Impact_Headshot()
{
	GameplayCueTag = MSTags::Cue_Impact_Headshot;
	GameplayCueName = GameplayCueTag.GetTagName();
	static ConstructorHelpers::FObjectFinder<USoundBase> OtherFinder(
		TEXT("/Game/Audio/Imported/SFX_Impact_Other.SFX_Impact_Other"));
	OtherImpactSound = OtherFinder.Object;
}
bool UMSGameplayCue_Impact_Headshot::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	return ExecuteImpact(MyTarget, Parameters, true, OtherImpactSound, EMSAudioUsage::HitHeadshot, 1000.0f);
}
