// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"

/**
 * WeaponLibraryOpus (2026-09-24) visual assignment for the 58 previously undetailed
 * weapon/shield definitions. Generated from 3Dmodels
 * exports/WeaponLibraryOpus/manifests/da-model-map.csv. Tools/Codex/ImportWeaponLibraryOpus.py
 * writes the same assignment to the saved DAs; MSWeaponDataGenCommandlet re-applies it
 * when balance data is regenerated. Only presentation fields are described here.
 */
namespace MSWeaponLibraryOpus
{
	enum class ERole : uint8
	{
		/** Selectable weapon slot (hand, both hands, or fist). */
		Weapon,
		/** Built-in head module; never shown in the hand. */
		HeadModule,
		/** Machine-native shield on the left forearm; never a weapon slot. */
		NativeShield
	};

	struct FVisualRow
	{
		const TCHAR* AssetName;
		const TCHAR* ModelId;
		/** Projectile body model, or nullptr to keep the existing projectile visual. */
		const TCHAR* ProjectileModelId;
		EMSWeaponMeshMount Mount;
		ERole Role;
	};

	inline constexpr int32 TargetDefinitionCount = 58;
	inline constexpr int32 ModelCount = 44;
	inline const TCHAR* const MeshFolder = TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/");

	inline const FVisualRow Rows[] = {
		{TEXT("DA_Weapon_BR"), TEXT("BR_BeamRifleSA"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamCannon"), TEXT("BC_BeamCannon"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamCarbineBK2"), TEXT("BK2_BeamCarbine"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamLauncherBL4"), TEXT("BL4_BeamLauncher"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamLauncherBL4F"), TEXT("BL4_BeamLauncher"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamLauncherBL4M"), TEXT("BL4_BeamLauncher"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BeamMachineGun"), TEXT("BMG_BeamMachineGun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BurstBeamRifleBB3"), TEXT("BB3_BurstBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BurstBeamRifleBB3M"), TEXT("BB3_BurstBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_ChargeBeamRifleCB2"), TEXT("CB2_ChargeBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_ChargeLongRifleCL6"), TEXT("CL6_ChargeLongRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LongBeamRifleLB5"), TEXT("LB5_LongBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LongBeamRifleLB5F"), TEXT("LB5_LongBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_RapidBeamRifle"), TEXT("RBR_RapidBeamRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_AssaultVulcanAV2"), TEXT("AV2_AssaultVulcan"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_AssaultVulcanAV2F"), TEXT("AV2_AssaultVulcan"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_AssaultVulcanAV2M"), TEXT("AV2_AssaultVulcan"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_BurstCarbineBC4"), TEXT("BC4_BurstCarbine"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_GatlingGun"), TEXT("GT6_GatlingGun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeadVulcan"), TEXT("HV_HeadVulcanModule"), nullptr, EMSWeaponMeshMount::Hand, ERole::HeadModule},
		{TEXT("DA_Weapon_HeavyGatlingHG7"), TEXT("HG7_HeavyGatling"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeavyGatlingHG7M"), TEXT("HG7_HeavyGatling"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LightMachineGunLM6"), TEXT("LM6_LightMachineGun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LightMachineGunLM6F"), TEXT("LM6_LightMachineGun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LightMachineGunLM6M"), TEXT("LM6_LightMachineGun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LongRifleLR8"), TEXT("LR8_LongRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_LongRifleLR8F"), TEXT("LR8_LongRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_QuickSniperQS2"), TEXT("QS2_QuickSniper"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_SniperRifle"), TEXT("SR_SniperRifle"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_VulcanMG40"), TEXT("MG40_Vulcan"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_Grenade"), TEXT("GR_HandGrenade"), TEXT("GR_HandGrenade"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_GrenadeLauncherGL2"), TEXT("GL2_GrenadeLauncher"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_GrenadeLauncherGL2W"), TEXT("GL2_GrenadeLauncher"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeavyMortarEX5"), TEXT("EX5_HeavyMortar"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeavyMortarEX5F"), TEXT("EX5_HeavyMortar"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeavyMortarEX5W"), TEXT("EX5_HeavyMortar"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_SmokeGrenade"), TEXT("SMK_SmokeGrenade"), TEXT("SMK_SmokeGrenade"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_TwinGrenadeLauncher"), TEXT("TGL_TwinGrenadeLauncher"), TEXT("Shell_Projectile"), EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_ChargeGreatSwordCS1"), TEXT("CS1_ChargeGreatSword"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_GrappleKnuckleGK3"), TEXT("GK3_GrappleKnuckle"), nullptr, EMSWeaponMeshMount::Fist, ERole::Weapon},
		{TEXT("DA_Weapon_HeatAxeHA2"), TEXT("HA2_HeatAxe"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeatAxeHA2F"), TEXT("HA2_HeatAxe"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeatLancerHL1"), TEXT("HL1_HeatLancer"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_HeatSwordHS1"), TEXT("HS1_HeatSword"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_PowerKnucklePN1"), TEXT("PN1_PowerKnuckle"), nullptr, EMSWeaponMeshMount::Fist, ERole::Weapon},
		{TEXT("DA_Weapon_TwinDaggerTD1"), TEXT("TD1_TwinDagger"), nullptr, EMSWeaponMeshMount::DualHands, ERole::Weapon},
		{TEXT("DA_Weapon_heat_kunai"), TEXT("KD_ColdKunai"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_ConvergenceBeamCB9"), TEXT("CB9_ConvergenceBeam"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_FocusBeamFB2"), TEXT("FB2_FocusBeam"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_DefenderShieldDS1"), TEXT("DS1_DefenderShield"), nullptr, EMSWeaponMeshMount::Hand, ERole::NativeShield},
		{TEXT("DA_Weapon_FortressShieldFS3"), TEXT("FS3_FortressShield"), nullptr, EMSWeaponMeshMount::Hand, ERole::NativeShield},
		{TEXT("DA_Weapon_LightShieldLS2"), TEXT("LS2_LightShield"), nullptr, EMSWeaponMeshMount::Hand, ERole::NativeShield},
		{TEXT("DA_Weapon_ScatterShotSS3"), TEXT("SS3_ScatterShot"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_ScatterShotSS3F"), TEXT("SS3_ScatterShot"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_Shotgun"), TEXT("SG1_Shotgun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_WideShotgunSG2W"), TEXT("SG2W_WideShotgun"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_RepairPodDeployer"), TEXT("RPD_RepairPodDeployer"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
		{TEXT("DA_Weapon_SupplyPackDeployer"), TEXT("SPD_SupplyPackDeployer"), nullptr, EMSWeaponMeshMount::Hand, ERole::Weapon},
	};
	static_assert(UE_ARRAY_COUNT(Rows) == TargetDefinitionCount, "WeaponLibraryOpus assigns exactly 58 definitions");

	inline FSoftObjectPath MeshPath(const TCHAR* ModelId)
	{
		return FSoftObjectPath(FString(MeshFolder) + TEXT("SM_") + ModelId + TEXT(".SM_") + ModelId);
	}

	inline const FVisualRow* Find(const FString& AssetName)
	{
		for (const FVisualRow& Row : Rows)
		{
			if (AssetName == Row.AssetName)
			{
				return &Row;
			}
		}
		return nullptr;
	}

	/** Held/module/shield meshes are authored in MS metres (x100 on import): cancel MSScale once. */
	inline FTransform WeaponMeshTransform()
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.0f / MSGO::MSScale));
	}

	/**
	 * Projectile bodies stay in world centimetres (no MSScale). Shell_Projectile is authored
	 * flight +X; thrown grenades reuse their held mesh (+Z forward), so pitch -90 maps +Z to +X.
	 */
	inline FTransform ProjectileMeshTransform(const TCHAR* ProjectileModelId)
	{
		const bool bHeldAxes = ProjectileModelId && FCString::Strcmp(ProjectileModelId, TEXT("Shell_Projectile")) != 0;
		return bHeldAxes ? FTransform(FRotator(-90.0f, 0.0f, 0.0f)) : FTransform::Identity;
	}

	/** Writes only the presentation fields. Used by MSWeaponDataGenCommandlet on regeneration. */
	inline void ApplyVisual(UMSWeaponDataBase& Asset, const FVisualRow& Row)
	{
		Asset.WeaponMesh = TSoftObjectPtr<UStaticMesh>(MeshPath(Row.ModelId));
		Asset.WeaponMeshTransform = WeaponMeshTransform();
		Asset.WeaponMeshMount = Row.Mount;
		UMSRangedWeaponData* Ranged = Cast<UMSRangedWeaponData>(&Asset);
		if (Ranged && Row.ProjectileModelId)
		{
			Ranged->ProjectileMesh = TSoftObjectPtr<UStaticMesh>(MeshPath(Row.ProjectileModelId));
			Ranged->ProjectileMeshTransform = ProjectileMeshTransform(Row.ProjectileModelId);
			// Unpowered shells and thrown grenades: no rocket flame.
			Ranged->bProjectileMeshExhaust = false;
		}
	}
}
