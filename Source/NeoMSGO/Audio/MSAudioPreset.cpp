// Copyright NeoMSGO Project. All Rights Reserved.
#include "Audio/MSAudioPreset.h"

#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* AudioPresetConfigSection = TEXT("/Script/NeoMSGO.MSAudioSettings");
	const TCHAR* AudioPresetConfigKey = TEXT("AudioPreset");
	int32 SelectedPreset = INDEX_NONE;

	const TCHAR* TypeBPath(const EMSAudioUsage Usage)
	{
		switch (Usage)
		{
		case EMSAudioUsage::FireBeamRifle: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BeamShot126.TypeB_BeamShot126");
		case EMSAudioUsage::FireRifle: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BeamShot132.TypeB_BeamShot132");
		case EMSAudioUsage::FireSMG: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BeamShot154.TypeB_BeamShot154");
		case EMSAudioUsage::FireLMG: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BeamShot181.TypeB_BeamShot181");
		case EMSAudioUsage::FireShotgun: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BeamShot195.TypeB_BeamShot195");
		case EMSAudioUsage::FireBazooka: return TEXT("/Game/Audio/Imported/TypeB/TypeB_FireBazooka.TypeB_FireBazooka");
		case EMSAudioUsage::FireCannon: return TEXT("/Game/Audio/Imported/TypeB/TypeB_FireCannon.TypeB_FireCannon");
		case EMSAudioUsage::FireOrdnance: return TEXT("/Game/Audio/Imported/TypeB/TypeB_FireOrdnance.TypeB_FireOrdnance");
		case EMSAudioUsage::MeleeAttack: return TEXT("/Game/Audio/Imported/TypeB/TypeB_MeleeAttack.TypeB_MeleeAttack");
		case EMSAudioUsage::MeleeHit: return TEXT("/Game/Audio/Imported/TypeB/TypeB_MeleeHit.TypeB_MeleeHit");
		case EMSAudioUsage::MeleeDash: return TEXT("/Game/Audio/Imported/TypeB/TypeB_MeleeDash.TypeB_MeleeDash");
		case EMSAudioUsage::HitNormal: return TEXT("/Game/Audio/Imported/TypeB/TypeB_HitNormal.TypeB_HitNormal");
		case EMSAudioUsage::HitHeadshot: return TEXT("/Game/Audio/Imported/TypeB/TypeB_HitHeadshot.TypeB_HitHeadshot");
		case EMSAudioUsage::KillConfirm: return TEXT("/Game/Audio/Imported/TypeB/TypeB_KillConfirm.TypeB_KillConfirm");
		case EMSAudioUsage::LockConfirm: return TEXT("/Game/Audio/Imported/TypeB/TypeB_LockConfirm.TypeB_LockConfirm");
		case EMSAudioUsage::LockWarning: return TEXT("/Game/Audio/Imported/TypeB/TypeB_LockWarning.TypeB_LockWarning");
		case EMSAudioUsage::Footstep: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Footstep.TypeB_Footstep");
		case EMSAudioUsage::BoostStart: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BoostStart.TypeB_BoostStart");
		case EMSAudioUsage::BoostLoop: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BoostLoop.TypeB_BoostLoop");
		case EMSAudioUsage::Charge: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Charge.TypeB_Charge");
		case EMSAudioUsage::Reload: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Reload.TypeB_Reload");
		case EMSAudioUsage::AmmoEmpty: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmmoEmpty.TypeB_AmmoEmpty");
		case EMSAudioUsage::ExplosionSmall: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Boom02.TypeB_Boom02");
		case EMSAudioUsage::ExplosionLarge: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BoomBig.TypeB_BoomBig");
		case EMSAudioUsage::Sortie: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Sortie.TypeB_Sortie");
		case EMSAudioUsage::UIClick: return TEXT("/Game/Audio/Imported/TypeB/TypeB_UIClick.TypeB_UIClick");
		case EMSAudioUsage::UIHover: return TEXT("/Game/Audio/Imported/TypeB/TypeB_UIHover.TypeB_UIHover");
		// bgm——006が本家TypeB由来の連番BGM。H0rizonは仮置きだったため差し替え(2026-07-19)。
		case EMSAudioUsage::BGM: return TEXT("/Game/Audio/Imported/TypeB/TypeB_BGM006.TypeB_BGM006");
		case EMSAudioUsage::UnderwaterAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_Underwater_Audio.TypeB_Underwater_Audio");
		case EMSAudioUsage::DesertAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmbienceDesert.TypeB_AmbienceDesert");
		case EMSAudioUsage::JungleAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmbienceJungle.TypeB_AmbienceJungle");
		case EMSAudioUsage::MountainWindAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmbienceMountainWind.TypeB_AmbienceMountainWind");
		case EMSAudioUsage::StrongForestWindAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmbienceStrongForestWind.TypeB_AmbienceStrongForestWind");
		case EMSAudioUsage::WindyForestAmbience: return TEXT("/Game/Audio/Imported/TypeB/TypeB_AmbienceWindyForest.TypeB_AmbienceWindyForest");
		default: return TEXT("");
		}
	}
}

EMSAudioPreset MSAudioPreset::GetSelectedPreset()
{
	if (SelectedPreset != INDEX_NONE)
	{
		return SelectedPreset == static_cast<int32>(EMSAudioPreset::TypeB)
			? EMSAudioPreset::TypeB : EMSAudioPreset::TypeA;
	}
	int32 Value = static_cast<int32>(EMSAudioPreset::TypeA);
	GConfig->GetInt(AudioPresetConfigSection, AudioPresetConfigKey, Value, GGameUserSettingsIni);
	SelectedPreset = Value == static_cast<int32>(EMSAudioPreset::TypeB)
		? static_cast<int32>(EMSAudioPreset::TypeB) : static_cast<int32>(EMSAudioPreset::TypeA);
	return SelectedPreset == static_cast<int32>(EMSAudioPreset::TypeB)
		? EMSAudioPreset::TypeB : EMSAudioPreset::TypeA;
}

void MSAudioPreset::SetSelectedPreset(const EMSAudioPreset Preset, const bool bSave)
{
	SelectedPreset = Preset == EMSAudioPreset::TypeB
		? static_cast<int32>(EMSAudioPreset::TypeB) : static_cast<int32>(EMSAudioPreset::TypeA);
	if (bSave)
	{
		GConfig->SetInt(AudioPresetConfigSection, AudioPresetConfigKey, static_cast<int32>(Preset), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

USoundBase* MSAudioPreset::Resolve(const EMSAudioUsage Usage, USoundBase* TypeASound)
{
	return ResolveForPreset(Usage, GetSelectedPreset(), TypeASound);
}

USoundBase* MSAudioPreset::ResolveForPreset(
	const EMSAudioUsage Usage, const EMSAudioPreset Preset, USoundBase* TypeASound)
{
	if (Preset != EMSAudioPreset::TypeB)
	{
		return TypeASound;
	}
	const TCHAR* Path = TypeBPath(Usage);
	if (!Path || !Path[0])
	{
		return TypeASound;
	}
	USoundBase* TypeBSound = LoadObject<USoundBase>(nullptr, Path);
	return TypeBSound ? TypeBSound : TypeASound;
}

const TCHAR* MSAudioPreset::GetTypeBAssetPath(const EMSAudioUsage Usage)
{
	return TypeBPath(Usage);
}
