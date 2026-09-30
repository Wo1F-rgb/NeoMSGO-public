// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * MSGameplayTags
 *
 * NeoMSGO のネイティブGameplayTag辞書。
 * docs/UE5_Architecture.md 「2. GameplayTag辞書」で定義された全タグをC++ネイティブ定義する。
 * iniやエディタからのタグ追加は禁止(タイポ・重複防止。P0-6)。
 *
 * 命名規則(§2.1):
 *  - 階層は「大分類.中分類.葉」の最大3階層、PascalCase
 *  - C++シンボル名は MSTags:: 名前空間下でタグパスのドット(.)をアンダースコア(_)に変換する
 *    (例: State.Reloading -> MSTags::State_Reloading)
 *  - 大分類は State / Weapon / Ability / Cue / Data / Event の6種のみ
 *  - Cueタグの実タグルートは GameplayCue.MS.*(本書内の略記 Cue.* = GameplayCue.MS.*)
 */
namespace MSTags
{
	// ====================================================================
	// 2.2 State.*(ユニット状態)
	// ====================================================================
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Staggered);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Flinched);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_KnockedDown);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_GettingUp);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ReactionProtected);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Overheated);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Reloading);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Switching);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_MeleeRecovery);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_DashMeleeRegenBlock);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Guarding);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Charging);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Crouching);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Boosting);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing_Beam);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_EmergencyRecall);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Module_EffectActive);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Module_ForcedOverheat);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Module_Cooldown);

	// ====================================================================
	// 2.3 Weapon.Category.*(9カテゴリ)/ Weapon.Trait.*(補助)
	// ====================================================================
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_MachineGun);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_BeamRifle);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Bazooka);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Melee);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Shield);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Supply);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_ContinuousBeam);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Shotgun);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Homing);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Trait_BuiltIn);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Trait_Scope);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Trait_Chargeable);

	// ====================================================================
	// 2.4 Cue.*(実タグ: GameplayCue.MS.*)
	// ====================================================================
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Weapon_Fire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Weapon_Tracer);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Weapon_BeamLoop);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Weapon_ChargeLoop);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Impact_Normal);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Impact_Headshot);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Explosion);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_Kill_Confirm);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cue_HitMarker);

	// ====================================================================
	// 2.5 Data.* / Ability.* / Event.*
	// ====================================================================
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_DownPoint);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_SplashFalloff);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_DPRecovery);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_RepairPod);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_BoostConsume);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_BoostRegen);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_OverheatDuration);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Init_MaxArmor);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Init_MaxDownPoint);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Init_BoostCapacity);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Module_Duration);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Module_OverheatDuration);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Module_Cooldown);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Melee);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Guard);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Deploy);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Boost);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Module);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Death);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Stagger);
}
