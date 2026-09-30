// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MSAudioPreset.generated.h"

class USoundBase;

UENUM(BlueprintType)
enum class EMSAudioPreset : uint8
{
	TypeA,
	TypeB,
};

/** Stable semantic keys used by gameplay/UI instead of TypeB asset names. */
UENUM(BlueprintType)
enum class EMSAudioUsage : uint8
{
	FireBeamRifle,
	FireRifle,
	FireSMG,
	FireLMG,
	FireShotgun,
	FireBazooka,
	FireCannon,
	FireOrdnance,
	MeleeAttack,
	MeleeHit,
	MeleeDash,
	HitNormal,
	HitHeadshot,
	KillConfirm,
	LockConfirm,
	LockWarning,
	Footstep,
	BoostStart,
	BoostLoop,
	Charge,
	ChargeComplete,
	Reload,
	AmmoEmpty,
	ExplosionSmall,
	ExplosionLarge,
	Sortie,
	ModuleActivate,
	UIClick,
	UIHover,
	UIBack,
	BGM,
	UnderwaterAmbience,
	DesertAmbience,
	JungleAmbience,
	MountainWindAmbience,
	StrongForestWindAmbience,
	WindyForestAmbience,
};

namespace MSAudioPreset
{
	/** Reads the persisted user setting. Invalid/missing values safely become TypeA. */
	NEOMSGO_API EMSAudioPreset GetSelectedPreset();

	/** Applies immediately to subsequent playback and optionally writes GameUserSettings.ini. */
	NEOMSGO_API void SetSelectedPreset(EMSAudioPreset Preset, bool bSave = true);

	/** TypeB lookup with guaranteed TypeA fallback (also covers missing/corrupt TypeB assets). */
	NEOMSGO_API USoundBase* Resolve(EMSAudioUsage Usage, USoundBase* TypeASound);

	/** Explicit-preset variant used by deterministic automation tests. */
	NEOMSGO_API USoundBase* ResolveForPreset(
		EMSAudioUsage Usage, EMSAudioPreset Preset, USoundBase* TypeASound);

	/** Empty means the usage intentionally falls back to TypeA. */
	NEOMSGO_API const TCHAR* GetTypeBAssetPath(EMSAudioUsage Usage);
}
