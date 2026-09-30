// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-2: Vital属性セット(Armor/DP)。
// 設計元: docs/UE5_Architecture.md §1.2 UMSAttributeSet_Vital ← Unity: Health.cs
//
// | Attribute               | 初期値ソース                                  | 意味 |
// |--------------------------|----------------------------------------------|------|
// | Armor / MaxArmor         | MobileSuitData.MaxArmor (GE_InitStats)       | 耐久値。0で死亡 |
// | DownPoint / MaxDownPoint | 100(共通定数だがGE_InitStats経由で設定)     | DP蓄積。閾値到達でよろけ/ダウン |
//
// PostGameplayEffectExecute の責務(§1.2):
//  - Armor 0 到達時: State.Dead 付与(GE_Dead) + OnDeath デリゲート発火。
//    **生存→死亡の遷移時のみ**通知(死体多重ヒットの誤カウント防止。Unity版 NotifyIfPlayerKill の教訓)。
//  - DownPoint が MaxDownPoint 到達時(P2-6): DownPoint=0 リセット後、接地判定
//    (UCharacterMovementComponent::IsMovingOnGround に統一。BOT特殊分岐廃止 — 計画書P2-6)で
//      接地   → GE_Stagger    (1.2s, State.Staggered)
//      非接地 → GE_KnockedDown(2.5s, State.KnockedDown)
//    を自身へ適用し、Event.Stagger + OnStagger デリゲートで通知する。
//  - 被弾タイムスタンプ(LastDamageTime)の記録(P2-6)。DP自然回復の開始遅延判定
//    (2.5s無被弾)に UMSDownPointRegenComponent が使用する(§1.2)。
//  - 被弾方向通知(FMSGameplayEffectContext::AttackerLocation)は P2-3 のContext拡張後に実装。

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MSAttributeSet_Vital.generated.h"

/**
 * Attribute標準アクセサ一式(GAS慣例マクロの集約)。
 * 例: MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Vital, Armor) は
 *     GetArmorAttribute() / GetArmor() / SetArmor() / InitArmor() を生成する。
 */
#define MS_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * 死亡通知デリゲート(Unity: Health.OnDeath 相当)。
 * @param DeadUnit  死亡したユニット(AttributeSetのOwningActor)
 * @param Killer    トドメを刺したGEのOriginalInstigator(不明経路はnullptr)
 * 生存→死亡の遷移時に1回だけ発火する。キル通知(P2-3)・リスポーン(P3-7)・戦力ゲージ(P7-3)が購読する。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMSUnitDeath, AActor*, DeadUnit, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMSUnitDamagedFrom, AActor*, DamagedUnit, AActor*, Attacker, FVector, AttackerLocation);

/**
 * よろけ/空中ダウン開始通知(Unity: Health.OnStaggerBegin(bool isKnockedDown) 相当)。
 * @param Unit          遷移したユニット(AttributeSetのOwningActor)
 * @param bKnockedDown  true=空中ダウン(2.5s) / false=接地よろけ(1.2s)
 * DP閾値到達の遷移時に1回発火する。アニメ(P5-5)・AI(P6-1)・HUDが購読する。
 * 終了はGE_Stagger/GE_KnockedDownの時限失効=State タグ除去(RegisterGameplayTagEventで観測可能)。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMSUnitStagger, AActor*, Unit, bool, bKnockedDown);

/**
 * Vital属性セット(Armor/DownPoint)。AMSUnitBase / AMSDestructibleTarget(P7-2)が保持する。
 * 初期値はGE_InitStats(SetByCaller: Data.Init.MaxArmor / Data.Init.MaxDownPoint)で注入する(§1.4)。
 */
UCLASS()
class NEOMSGO_API UMSAttributeSet_Vital : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMSAttributeSet_Vital();

	//~ Begin UAttributeSet Interface
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UAttributeSet Interface

	/** 耐久値。0で死亡(Unity: Health.CurrentArmor) */
	UPROPERTY(ReplicatedUsing=OnRep_Armor, BlueprintReadOnly, Category = "MS|Vital")
	FGameplayAttributeData Armor;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Vital, Armor)

	/** 最大耐久値(Unity: Health.maxArmor。MobileSuitData.MaxArmor由来) */
	UPROPERTY(ReplicatedUsing=OnRep_MaxArmor, BlueprintReadOnly, Category = "MS|Vital")
	FGameplayAttributeData MaxArmor;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Vital, MaxArmor)

	/** ダウンポイント蓄積(Unity: Health.currentDownPoint。HUDはリフレクションでなく本属性を直接読む — 計画書P8-1) */
	UPROPERTY(ReplicatedUsing=OnRep_DownPoint, BlueprintReadOnly, Category = "MS|Vital")
	FGameplayAttributeData DownPoint;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Vital, DownPoint)

	/** DP閾値(Unity: Health.maxDownPoint = 100) */
	UPROPERTY(ReplicatedUsing=OnRep_MaxDownPoint, BlueprintReadOnly, Category = "MS|Vital")
	FGameplayAttributeData MaxDownPoint;
	MS_ATTRIBUTE_ACCESSORS(UMSAttributeSet_Vital, MaxDownPoint)

	/** 死亡通知(生存→死亡遷移時のみ1回)。BP/C++両対応 */
	UPROPERTY(BlueprintAssignable, Category = "MS|Vital")
	FOnMSUnitDeath OnDeath;

	/** Armorが実際に減少したときの攻撃元通知。回復および初期化では発火しない。 */
	UPROPERTY(BlueprintAssignable, Category = "MS|Vital")
	FOnMSUnitDamagedFrom OnDamagedFrom;

	/** よろけ/空中ダウン開始通知(DP閾値到達の遷移時に1回)。BP/C++両対応 */
	UPROPERTY(BlueprintAssignable, Category = "MS|Vital")
	FOnMSUnitStagger OnStagger;

	/**
	 * 最終被弾時刻(ワールド秒。Unity: Health.lastDamageTime 相当)。
	 * Armor減少またはDP加算のたびに更新される。未被弾時は大きな負値
	 * (=スポーン直後からDP>0なら即回復可能)。UMSDownPointRegenComponentが参照する。
	 */
	double GetLastDamageTime() const { return LastDamageTime; }

	/** Fully absorbed native-shield hits still reset damage recovery and notify observers. */
	void NotifyShieldDamage(const FGameplayEffectContextHandle& ContextHandle);

private:
	UFUNCTION() void OnRep_Armor(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxArmor(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DownPoint(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxDownPoint(const FGameplayAttributeData& OldValue);
	/** Armor/DownPointの共通クランプ(現在値・基礎値の両経路から呼ぶ) */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	/**
	 * 死亡遷移処理: GE_Dead適用(State.Dead付与)→ Event.Death送出 → OnDeath発火。
	 * 既にState.Dead保持中(死体への多重ヒット)は何もしない。
	 */
	void HandleDeath(const struct FGameplayEffectModCallbackData& Data);

	/**
	 * DP閾値到達処理(P2-6。Unity: Health.AddDownPoint の閾値分岐):
	 *  DownPoint=0リセット → 接地判定(IsMovingOnGround)→ GE_Stagger/GE_KnockedDown適用
	 *  → Event.Stagger送出(EventMagnitude: 1=空中ダウン/0=よろけ) → OnStagger発火。
	 * State.Dead / State.Staggered / State.KnockedDown 保持中は何もしない(Unity: IsDead||IsStaggeredガード)。
	 * 非Characterの保持者(通常はDP属性を使わない)は非接地=空中ダウン扱い(Unity版フォールバック踏襲)。
	 */
	void HandleDownPointThreshold();

	/** 最終被弾時刻を現在ワールド秒で記録する */
	void MarkDamageReceived();

	/** 最終被弾時刻(ワールド秒)。「未被弾」を表す初期値は十分過去 */
	double LastDamageTime = -1.0e9;
};
