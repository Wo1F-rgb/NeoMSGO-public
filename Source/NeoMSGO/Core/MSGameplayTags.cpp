// Copyright Epic Games, Inc. All Rights Reserved.

#include "MSGameplayTags.h"

// docs/UE5_Architecture.md 「2. GameplayTag辞書」と1対1で対応させること。
// 新規タグの追加・変更は必ず本書の改訂を伴う(§2.1-4)。

namespace MSTags
{
	// ====================================================================
	// 2.2 State.*(ユニット状態)
	// ====================================================================
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "死亡。GE_Deadで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Staggered, "State.Staggered", "よろけ(接地DP閾値)。GE_Staggerで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Flinched, "State.Flinched", "DP第1段階の怯み。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_KnockedDown, "State.KnockedDown", "空中ダウン。GE_KnockedDownで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_GettingUp, "State.GettingUp", "転倒からの起き上がり段階。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_ReactionProtected, "State.ReactionProtected", "重反応の回復まで新たなDP反応を保護する。HP防御ではない。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Overheated, "State.Overheated", "ブーストOH。GE_Overheatで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reloading, "State.Reloading", "リロード中(現在スロット)。WeaponComponent LooseTagで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Switching, "State.Switching", "持ち替え中。WeaponComponent LooseTagで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_MeleeRecovery, "State.MeleeRecovery", "格闘後硬直。GE_MeleeRecoveryで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_DashMeleeRegenBlock, "State.DashMeleeRegenBlock", "D格の突進・攻撃・硬直中はブースト自然回復を止める。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Guarding, "State.Guarding", "機体固有盾の自動防御姿勢。MSShieldComponentが状態を管理。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Charging, "State.Charging", "チャージ武器チャージ中。GA_Fire_Chargeで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Crouching, "State.Crouching", "しゃがみ。トグルLooseTagで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Boosting, "State.Boosting", "ダッシュ/ホバー中(アニメ・VFX参照用)。MovementComponent LooseTagで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Firing_Beam, "State.Firing.Beam", "照射ビーム照射中。GA_Fire_ContinuousBeamで付与。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_EmergencyRecall, "State.EmergencyRecall", "緊急帰投カウントダウン中。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Module_EffectActive, "State.Module.EffectActive", "固有モジュール効果中。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Module_ForcedOverheat, "State.Module.ForcedOverheat", "固有モジュール終了後の時間固定OH中。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Module_Cooldown, "State.Module.Cooldown", "固有モジュールの再使用待機中。");

	// ====================================================================
	// 2.3 Weapon.Category.*(9カテゴリ)/ Weapon.Trait.*(補助)
	// ====================================================================
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_MachineGun, "Weapon.Category.MachineGun", "実弾連射・内蔵頭部バルカン含む。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_BeamRifle, "Weapon.Category.BeamRifle", "ビームライフル。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Bazooka, "Weapon.Category.Bazooka", "爆風・内蔵肩部ロケットポッド含む。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Melee, "Weapon.Category.Melee", "格闘。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Shield, "Weapon.Category.Shield", "機体固有シールドのアセット分類。武器3枠には入れない。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Supply, "Weapon.Category.Supply", "設置武器。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_ContinuousBeam, "Weapon.Category.ContinuousBeam", "照射ビーム。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Shotgun, "Weapon.Category.Shotgun", "ショットガン。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Category_Homing, "Weapon.Category.Homing", "誘導武器。");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Trait_BuiltIn, "Weapon.Trait.BuiltIn", "WeaponDataのbool群のタグ表現(UI/クエリ用)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Trait_Scope, "Weapon.Trait.Scope", "WeaponDataのbool群のタグ表現(UI/クエリ用)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Trait_Chargeable, "Weapon.Trait.Chargeable", "WeaponDataのbool群のタグ表現(UI/クエリ用)。");

	// ====================================================================
	// 2.4 Cue.*(実タグ: GameplayCue.MS.*)
	// ====================================================================
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Weapon_Fire, "GameplayCue.MS.Weapon.Fire", "Burst: 発射音+マズルフラッシュ(カテゴリ別SEはCueParameters経由でWeaponData参照)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Weapon_Tracer, "GameplayCue.MS.Weapon.Tracer", "Burst: 弾道トレーサー/即着ビーム描画。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Weapon_BeamLoop, "GameplayCue.MS.Weapon.BeamLoop", "Looping: 照射ビームのビジュアル+ループ音(始点は前方1.2mオフセット。B-6)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Weapon_ChargeLoop, "GameplayCue.MS.Weapon.ChargeLoop", "Looping: チャージハム(pitch/volumeをチャージ進行度で駆動)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Impact_Normal, "GameplayCue.MS.Impact.Normal", "Burst: 通常着弾(火花+命中音)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Impact_Headshot, "GameplayCue.MS.Impact.Headshot", "Burst: HS着弾(高音2トーン)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Explosion, "GameplayCue.MS.Explosion", "Burst: 爆発。Niagara lifetime 0.04〜0.06s、一瞬・控えめ厳守(B-7)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_Kill_Confirm, "GameplayCue.MS.Kill.Confirm", "Burst: キル確認(下降トーン/金属音)。死亡遷移時のみ発火。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cue_HitMarker, "GameplayCue.MS.HitMarker", "Burst: プレイヤーのヒットマーカーHUD通知(HS別はCueParametersのbool)。");

	// ====================================================================
	// 2.5 Data.* / Ability.* / Event.*
	// ====================================================================
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "SetByCaller用(§1.6)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_DownPoint, "Data.DownPoint", "SetByCaller用(§1.6)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_SplashFalloff, "Data.SplashFalloff", "SetByCaller用(§1.6)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_DPRecovery, "Data.DPRecovery", "SetByCaller用(GE_DPRecovery §1.4。DP自然回復の減算量=-40×Δt)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_RepairPod, "Data.RepairPod", "SetByCaller Armor recovery per repair-pod tick.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_BoostConsume, "Data.BoostConsume", "SetByCaller用(GE_BoostConsume P3-2。ブースト消費量=-rate×Δt または -D格コスト。負値)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_BoostRegen, "Data.BoostRegen", "SetByCaller用(GE_BoostRegen P3-2。ブースト回復量=+rate×Δt。正値)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_OverheatDuration, "Data.OverheatDuration", "SetByCaller用(GE_Overheat P3-2。OHペナルティ秒=MobileSuitData.BoostOverheatPenalty)。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Init_MaxArmor, "Data.Init.MaxArmor", "SetByCaller用(GE_InitStats §1.4)。MobileSuitData.MaxArmor。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Init_MaxDownPoint, "Data.Init.MaxDownPoint", "SetByCaller用(GE_InitStats §1.4)。MobileSuitData.MaxDownPoint。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Init_BoostCapacity, "Data.Init.BoostCapacity", "SetByCaller用(GE_InitStats §1.4 / P3-2)。MobileSuitData.BoostCapacity。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Module_Duration, "Data.Module.Duration", "固有モジュール効果時間のSetByCaller用。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Module_OverheatDuration, "Data.Module.OverheatDuration", "固有モジュール強制OH時間のSetByCaller用。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Module_Cooldown, "Data.Module.Cooldown", "固有モジュールCooldown時間のSetByCaller用。");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Fire, "Ability.Fire", "GAのAbilityTags。Block指定は§1.3マトリクス。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Melee, "Ability.Melee", "GAのAbilityTags。Block指定は§1.3マトリクス。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Guard, "Ability.Guard", "GAのAbilityTags。Block指定は§1.3マトリクス。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Deploy, "Ability.Deploy", "GAのAbilityTags。Block指定は§1.3マトリクス。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Boost, "Ability.Boost", "GAのAbilityTags。Block指定は§1.3マトリクス。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Module, "Ability.Module", "固有モジュール発動GAのAbilityTag。");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Death, "Event.Death", "GameplayEventでアニメ/AIへ通知。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Stagger, "Event.Stagger", "GameplayEventでアニメ/AIへ通知。");
}
