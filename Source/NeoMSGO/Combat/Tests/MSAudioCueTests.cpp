// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSAnimInstance.h"
#include "Audio/MSAudioPlayback.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Combat/Cues/MSGameplayCue_Fire.h"
#include "Combat/Cues/MSGameplayCue_Impact.h"
#include "Combat/Cues/MSGameplayCue_KillConfirm.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Sound/SoundBase.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAudioCueTest,
	"NeoMSGO.Combat.Audio.P9_2",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	void TestSoundReference(FAutomationTestBase& Test, const TCHAR* Label,
		const USoundBase* Sound, const TCHAR* ExpectedPath)
	{
		Test.TestNotNull(Label, Sound);
		if (Sound)
		{
			Test.TestEqual(FString::Printf(TEXT("%s asset path"), Label),
				Sound->GetPathName(), FString(ExpectedPath));
		}
	}
}

bool FMSAudioCueTest::RunTest(const FString& Parameters)
{
	const UMSGameplayCue_Fire* FireCue = GetDefault<UMSGameplayCue_Fire>();
	const UMSGameplayCue_Impact_Normal* NormalImpactCue = GetDefault<UMSGameplayCue_Impact_Normal>();
	const UMSGameplayCue_Impact_Headshot* HeadshotImpactCue = GetDefault<UMSGameplayCue_Impact_Headshot>();
	const UMSGameplayCue_Explosion* ExplosionCue = GetDefault<UMSGameplayCue_Explosion>();
	const UMSGameplayCue_KillConfirm* KillCue = GetDefault<UMSGameplayCue_KillConfirm>();

	TestSoundReference(*this, TEXT("default fire Cue sound"), FireCue->GetDefaultFireSound(),
		TEXT("/Game/Audio/Imported/SFX_Fire_Rifle.SFX_Fire_Rifle"));
	TestSoundReference(*this, TEXT("melee impact Cue sound"), NormalImpactCue->GetMeleeImpactSound(),
		TEXT("/Game/Audio/Imported/SFX_Impact_Melee.SFX_Impact_Melee"));
	TestSoundReference(*this, TEXT("other impact Cue sound"), NormalImpactCue->GetOtherImpactSound(),
		TEXT("/Game/Audio/Imported/SFX_Impact_Other.SFX_Impact_Other"));
	TestSoundReference(*this, TEXT("headshot impact Cue sound"), HeadshotImpactCue->GetHeadshotSound(),
		TEXT("/Game/Audio/Imported/SFX_Impact_Other.SFX_Impact_Other"));
	TestSoundReference(*this, TEXT("unit explosion Cue sound"), ExplosionCue->GetUnitExplosionSound(),
		TEXT("/Game/Audio/Imported/SFX_Explosion_Unit.SFX_Explosion_Unit"));
	TestSoundReference(*this, TEXT("ordnance explosion Cue sound"), ExplosionCue->GetOrdnanceExplosionSound(),
		TEXT("/Game/Audio/Imported/SFX_Explosion_Ordnance.SFX_Explosion_Ordnance"));
	TestSoundReference(*this, TEXT("BOT kill-confirm sound"), KillCue->GetDescendingToneSound(),
		TEXT("/Game/Audio/Imported/SFX_KillConfirm_Real.SFX_KillConfirm_Real"));
	TestSoundReference(*this, TEXT("player kill-confirm metallic sound"), KillCue->GetMetallicShineSound(),
		TEXT("/Game/Audio/Imported/SFX_KillConfirm_Real.SFX_KillConfirm_Real"));

	const UMSWeaponComponent* WeaponComponent = GetDefault<UMSWeaponComponent>();
	TestSoundReference(*this, TEXT("reload sound"), WeaponComponent->GetReloadSound(),
		TEXT("/Game/Audio/Imported/SFX_Reload_MetalLatch.SFX_Reload_MetalLatch"));
	const UMSAnimInstance* AnimInstance = GetDefault<UMSAnimInstance>();
	TestSoundReference(*this, TEXT("footstep sound"), AnimInstance->GetFootstepSound(),
		TEXT("/Game/Audio/Imported/SFX_Footstep_Metal.SFX_Footstep_Metal"));
	TestSoundReference(*this, TEXT("engine PCM fallback"), MSAudio::GetAudibleFallbackSound(),
		TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));

	MSAudio::ResetRecordedMetaSoundRequest();
	MSAudio::SpawnMetaSoundAtLocation(GetTransientPackage(), FireCue->GetDefaultFireSound(),
		FVector(10.0f, 20.0f, 30.0f), 880.0f);
	const MSAudio::FRecordedMetaSoundRequest& RecordedRequest = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("playback helper records one SpawnSoundAtLocation request"), RecordedRequest.RequestCount, 1);
	TestTrue(TEXT("recorded request retains its authored sound asset"), RecordedRequest.Sound == FireCue->GetDefaultFireSound());
	TestEqual(TEXT("recorded request retains its location"), RecordedRequest.Location, FVector(10.0f, 20.0f, 30.0f));
	TestEqual(TEXT("recorded request retains its Frequency input"), RecordedRequest.Frequency, 880.0f);
	TestEqual(TEXT("all one-shot voices have a finite helper lifetime"),
		RecordedRequest.OneShotDuration, 0.18f);
	TestFalse(TEXT("world fire playback remains 3D"), RecordedRequest.bIs2D);
	TestTrue(TEXT("3D one-shot playback supplies attenuation settings"),
		RecordedRequest.bHasAttenuation);
	TestEqual(TEXT("3D inner radius follows 18 m world scale"),
		RecordedRequest.AttenuationInnerRadius, 300.0f * MSGO::MSScale);
	TestEqual(TEXT("3D falloff distance follows 18 m world scale"),
		RecordedRequest.AttenuationFalloffDistance, 4700.0f * MSGO::MSScale);
	TestTrue(TEXT("3D one-shot playback supplies per-sound concurrency"),
		RecordedRequest.bHasConcurrency);
	TestEqual(TEXT("3D playback preserves imported SoundWave pitch"),
		RecordedRequest.AppliedPitchMultiplier, 1.0f);
	TestEqual(TEXT("3D playback preserves authored SoundWave volume"),
		RecordedRequest.AppliedVolumeMultiplier, 1.0f);
	TestEqual(TEXT("same-sound 3D voices are limited to three"),
		RecordedRequest.MaxConcurrentVoices, 3);
	TestTrue(TEXT("3D concurrency evicts the farthest then oldest voice"),
		RecordedRequest.bStopsFarthestThenOldest);

	MSAudio::SpawnMetaSoundAtLocation(GetTransientPackage(), AnimInstance->GetFootstepSound(),
		FVector::ZeroVector, 55.0f);
	const MSAudio::FRecordedMetaSoundRequest& FootstepRequest = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("legacy 55 Hz footstep metadata does not lower SoundWave pitch"),
		FootstepRequest.AppliedPitchMultiplier, 1.0f);

	USceneComponent* AttachComponent = NewObject<USceneComponent>(GetTransientPackage());
	MSAudio::SpawnLoopingMetaSoundAttached(AttachComponent, FireCue->GetDefaultFireSound(), 180.0f);
	const MSAudio::FRecordedLoopingMetaSoundState& LoopingState =
		MSAudio::GetRecordedLoopingMetaSoundState();
	TestEqual(TEXT("playback helper records one attached looping request"), LoopingState.StartCount, 1);
	TestTrue(TEXT("3D attached looping playback supplies attenuation settings"),
		LoopingState.bHasAttenuation);
	TestEqual(TEXT("attached loop falloff follows 18 m world scale"),
		LoopingState.AttenuationFalloffDistance, 4700.0f * MSGO::MSScale);
	TestTrue(TEXT("3D attached looping playback supplies per-sound concurrency"),
		LoopingState.bHasConcurrency);
	TestEqual(TEXT("same-sound attached loops are limited to three"),
		LoopingState.MaxConcurrentVoices, 3);
	TestTrue(TEXT("attached-loop concurrency evicts the farthest then oldest voice"),
		LoopingState.bStopsFarthestThenOldest);
	TObjectPtr<UAudioComponent> EmptyLoopComponent;
	MSAudio::StopLoopingMetaSound(EmptyLoopComponent);
	TestEqual(TEXT("loop stop remains null-safe and records the stop transition"),
		LoopingState.StopCount, 1);

	MSAudio::PlayMetaSound2D(GetTransientPackage(), NormalImpactCue->GetOtherImpactSound(),
		700.0f, 0.26f, 0.12f);
	const MSAudio::FRecordedMetaSoundRequest& ConfirmationRequest = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("2D confirmation uses the same bounded one-shot helper"),
		ConfirmationRequest.OneShotDuration, 0.12f);
	TestTrue(TEXT("hit confirmation is recorded as distance-independent 2D"), ConfirmationRequest.bIs2D);
	TestFalse(TEXT("2D confirmation does not supply attenuation settings"),
		ConfirmationRequest.bHasAttenuation);
	TestFalse(TEXT("2D confirmations do not consume the world concurrency group"),
		ConfirmationRequest.bHasConcurrency);
	TestEqual(TEXT("2D playback preserves authored SoundWave volume"),
		ConfirmationRequest.AppliedVolumeMultiplier, 1.0f);
	TestEqual(TEXT("all one-shot playback requests are recorded"), ConfirmationRequest.RequestCount, 3);

	UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>();
	Weapon->CategoryTag = MSTags::Weapon_Category_MachineGun;
	TestSoundReference(*this, TEXT("handheld MachineGun maps to LMG"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_LMG.SFX_Fire_LMG"));
	Weapon->bIsBuiltIn = true;
	TestSoundReference(*this, TEXT("built-in MachineGun maps to SMG"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_SMG.SFX_Fire_SMG"));
	Weapon->bIsBuiltIn = false;
	Weapon->bIsScopeWeapon = true;
	TestSoundReference(*this, TEXT("scoped MachineGun maps to rifle"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_Rifle.SFX_Fire_Rifle"));
	Weapon->bIsScopeWeapon = false;
	Weapon->CategoryTag = MSTags::Weapon_Category_BeamRifle;
	TestSoundReference(*this, TEXT("BeamRifle fire sound"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_BeamRifle.SFX_Fire_BeamRifle"));
	Weapon->CategoryTag = MSTags::Weapon_Category_Bazooka;
	TestSoundReference(*this, TEXT("Bazooka fire sound"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_Bazooka.SFX_Fire_Bazooka"));
	Weapon->CategoryTag = MSTags::Weapon_Category_Homing;
	TestSoundReference(*this, TEXT("Homing fire sound uses bazooka family"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_Bazooka.SFX_Fire_Bazooka"));
	Weapon->CategoryTag = MSTags::Weapon_Category_ContinuousBeam;
	TestSoundReference(*this, TEXT("ContinuousBeam fire sound uses beam family"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_BeamRifle.SFX_Fire_BeamRifle"));
	Weapon->CategoryTag = MSTags::Weapon_Category_Shotgun;
	TestSoundReference(*this, TEXT("Shotgun fire sound"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_Shotgun.SFX_Fire_Shotgun"));
	Weapon->CategoryTag = MSTags::Weapon_Category_Melee;
	TestSoundReference(*this, TEXT("Melee fire sound"), FireCue->ResolveFireSound(Weapon),
		TEXT("/Game/Audio/Imported/SFX_Fire_Melee.SFX_Fire_Melee"));
	Weapon->CategoryTag = MSTags::Weapon_Category_Supply;
	TestTrue(TEXT("other category falls back to fire default"),
		FireCue->ResolveFireSound(Weapon) == FireCue->GetDefaultFireSound());

	const UMSWeaponDataBase* MpuBeamSprayGun = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_MPU01_BeamSprayGun.DA_Weapon_MPU01_BeamSprayGun"));
	TestNotNull(TEXT("installed MPU01 BeamSprayGun data exists for fire audio"), MpuBeamSprayGun);
	if (MpuBeamSprayGun)
	{
		TestSoundReference(*this, TEXT("MPU01 BeamSprayGun uses shotgun fire sound"),
			FireCue->ResolveFireSound(MpuBeamSprayGun),
			TEXT("/Game/Audio/Imported/SFX_Fire_Shotgun.SFX_Fire_Shotgun"));
	}
	const UMSWeaponDataBase* MpuBeamSaber = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_MPU01_BeamSaber.DA_Weapon_MPU01_BeamSaber"));
	TestNotNull(TEXT("installed MPU01 BeamSaber data exists for melee audio"), MpuBeamSaber);
	if (MpuBeamSaber)
	{
		TestSoundReference(*this, TEXT("MPU01 BeamSaber uses melee swing sound"),
			FireCue->ResolveFireSound(MpuBeamSaber),
			TEXT("/Game/Audio/Imported/SFX_Fire_Melee.SFX_Fire_Melee"));
	}

	UMSMeleeWeaponData* MeleeWeapon = NewObject<UMSMeleeWeaponData>();
	MeleeWeapon->CategoryTag = MSTags::Weapon_Category_Melee;
	TestTrue(TEXT("melee impact classification"),
		NormalImpactCue->ResolveImpactSound(MeleeWeapon) == NormalImpactCue->GetMeleeImpactSound());
	TestTrue(TEXT("ranged impact classification"),
		NormalImpactCue->ResolveImpactSound(Weapon) == NormalImpactCue->GetOtherImpactSound());
	TestTrue(TEXT("weapon explosion classification"),
		ExplosionCue->ResolveExplosionSound(Weapon) == ExplosionCue->GetOrdnanceExplosionSound());
	TestTrue(TEXT("unit/no-source explosion classification"),
		ExplosionCue->ResolveExplosionSound(nullptr) == ExplosionCue->GetUnitExplosionSound());

	TestFalse(TEXT("footstep does not trigger before a contact boundary"),
		UMSAnimInstance::CrossesFootstepBoundary(0.1f, 0.2f));
	TestTrue(TEXT("footstep triggers at the next half-cycle contact"),
		UMSAnimInstance::CrossesFootstepBoundary(UE_PI - 0.1f, 0.2f));
	TestFalse(TEXT("stationary phase never triggers a footstep"),
		UMSAnimInstance::CrossesFootstepBoundary(UE_PI - 0.1f, 0.0f));

	return true;
}

#endif
