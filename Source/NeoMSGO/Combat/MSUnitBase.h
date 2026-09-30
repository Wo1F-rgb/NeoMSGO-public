// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-1: ASC搭載の共通ユニット基底。
// 設計元: docs/UE5_Architecture.md §1.1 クラス図 / §1.4 GE_InitStats / §3.2-3.4 / §4.1
//
// AMSUnitBase : ACharacter
//  ├─ implements IAbilitySystemInterface
//  ├─ implements IGenericTeamAgentInterface   (ACharacter派生のため直接実装。Core/MSTeamAgentActor.h の注記通り)
//  ├─ implements IMSLockOnTargetInterface     (P0-4)
//  ├─ UAbilitySystemComponent* ASC            (Owner=Avatar=自分。シングルプレイのため
//  │                                           Replication Mode 設定は不要、Minimalのまま)
//  └─ 頭部ヒットボックス "HeadHitbox"          (Profile=MSHitbox。判定ロジック自体は P2-5)
//
// 想定サブクラス(本タスクでは作成しない。§1.1 クラス図の位置づけのみ確定):
//  - AMSPlayerCharacter : AMSUnitBase  [Player/] (P3-1。MovementComponent/カメラ/LockOn/Respawn)
//  - AMSBotCharacter    : AMSUnitBase  [AI/]     (P6-1。AIController+StateTree)
//  判定・ダメージ経路はプレイヤー/BOTで完全共用(ガードレールB-10)。ASC保持者は本クラスのみ。
//
// 搭載済み(§1.1):
//  - UMSAttributeSet_Vital (P2-2。Armor/DP。初期値はGE_InitStats=UGE_InitStatsで注入)
//  - UMSDownPointRegenComponent (P2-6。DP自然回復: 被弾後2.5s無被弾で40/s)
//  - UMSAttributeSet_Boost (P3-2。Boost/MaxBoost。0到達で強制OH)
//  - UMSBoostComponent (P3-2。ブースト消費/回復駆動。CMCのIsConsumingBoostに連動)
// 後続タスクで本クラスに載るもの(§1.1):
//  - UMSWeaponComponent (P4-1) / UMSWeaponVisualComponent (P4-15)

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Core/MSLockOnTargetInterface.h"
#include "Data/MSBattleAssistTypes.h"
#include "MSUnitBase.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class UMSAttributeSet_Boost;
class UMSAttributeSet_Vital;
class UMSBoostComponent;
class UMSDownPointRegenComponent;
class UMSHitReactionComponent;
class UMSShieldComponent;
class UMSMechAnimatorComponent;
class UMSMechRigComponent;
class UMSMeshVisualComponent;
class UMSMobileSuitData;
class UMSModuleData;
class UMSWeaponComponent;
class UMSWeaponVisualComponent;
class USphereComponent;
struct FGameplayEffectSpecHandle;

/**
 * 戦闘系乗数のフック(P6-2)。既定はプレイヤー相当の無効化(全て中立値)。
 * AMSBotCharacter がオーバーライドし、階級共通の弱体化パラメータ
 * (damageMultiplier=0.3 / aimSpreadDegrees=4° / fireIntervalMultiplier=5倍・格闘のみ2倍)
 * を注入する。GA_Fire_Hitscan::ResolveAndApplyDamage / Fire、GA_Melee::PerformMelee、
 * UMSWeaponComponent::ConsumeAmmoAndStartCooldown が Shooter/Attacker/Owner をこの型に
 * キャストして参照する共通の乗算箇所(既存GA実装を破壊的に変更せず、Shooter側から
 * パラメータを供給する設計)。
 */
USTRUCT(BlueprintType)
struct FMSCombatModifiers
{
	GENERATED_BODY()

	/** ダメージ/ダウンポイントへの乗数(SetByCaller magnitudeへ乗算前に適用) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageMultiplier = 1.0f;
	/** 射撃方向へのランダム拡散角度(度)。0で無効 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float AimSpreadDegrees = 0.0f;
	/** 射撃武器のFireIntervalへの乗数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FireIntervalMultiplier = 1.0f;
	/** 格闘武器のFireIntervalへの乗数(射撃用とは独立) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MeleeFireIntervalMultiplier = 1.0f;
};

/** Atomic replicated spawn snapshot: values are resolved from IDs on authority only. */
USTRUCT()
struct FMSBattleAssistSpawnSnapshot
{
 GENERATED_BODY()
 UPROPERTY() FMSBattleAssistResolvedLoadout Resolved;
 UPROPERTY() int32 Epoch = 0;
};

UCLASS(Abstract)
class NEOMSGO_API AMSUnitBase : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface, public IMSLockOnTargetInterface
{
	GENERATED_BODY()

public:
	AMSUnitBase(const FObjectInitializer& ObjectInitializer);

	/** HeadHitboxコンポーネント名(P2-5のHS判定「同一ユニット配下のヒット群にHeadHitboxがあればHS」で参照) */
	static const FName HeadHitboxComponentName;

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End IGenericTeamAgentInterface

	//~ Begin IMSLockOnTargetInterface
	/** 生存中のみロック可(State.Dead 中は false。陣営判定は UMSTeamStatics::CanLockOn 側の責務) */
	virtual bool CanBeLockedOn() const override;
	/** 機体胸元(カプセル底から AimPointHeightAboveBase 上方)。Unity: Health.AimPoint / losEyeHeight=1.4m 相当 */
	virtual FVector GetLockOnAimPoint() const override;
	//~ End IMSLockOnTargetInterface

	/**
	 * 機体データを適用してユニットを初期化する(スポーン/リスポーン/Deploy時の単一入口)。
	 *  1. MobileSuitData を差し替え
	 *  2. GE_InitStats(InitStatsEffectClass)でAttribute初期値をASCへ注入(§1.4)
	 * プレイヤーのDeploy(P3-7)・BOTスポーン(P6-1/P6-7)の両方がこのAPIを通る(B-10)。
	 * Data が null の場合は既定の MobileSuitData(エディタ設定値)で初期化する。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Unit")
	virtual void InitializeUnit(UMSMobileSuitData* Data);

	/** Resolves IDs before any spawn mutation; false leaves the current spawn intact. */
 bool InitializeUnitWithBattleAssist(UMSMobileSuitData* Data, const FMSBattleAssistLoadout& Loadout);
 const FMSBattleAssistResolvedLoadout& GetBattleAssistResolved() const { return BattleAssistSnapshot.Resolved; }
 int32 GetBattleAssistEpoch() const { return BattleAssistSnapshot.Epoch; }
 float GetBattleAssistEffectValue(EMSBattleAssistEffectKind Kind, float DefaultValue = 0.f) const;
 float GetBattleAssistReductionScale(EMSBattleAssistEffectKind Kind) const;

	/** Snapshot from the deployed deck slot; null explicitly removes the module. */
	void SetEquippedModule(UMSModuleData* Module);
	UFUNCTION(BlueprintPure, Category = "MS|Unit")
	UMSModuleData* GetEquippedModule() const;

	/** 現在適用中の機体データ */
	UFUNCTION(BlueprintPure, Category = "MS|Unit")
	UMSMobileSuitData* GetMobileSuitData() const { return MobileSuitData; }

	/** Vital属性セット(Armor/DP。P2-2)。OnDeathデリゲート購読・HUD表示はここから辿る */
	UFUNCTION(BlueprintPure, Category = "MS|GAS")
	UMSAttributeSet_Vital* GetVitalSet() const { return VitalSet; }
	UMSHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	/** Boost属性セット(Boost/MaxBoost。P3-2)。OnOverheat購読・HUDゲージ表示はここから辿る */
	UFUNCTION(BlueprintPure, Category = "MS|GAS")
	UMSAttributeSet_Boost* GetBoostSet() const { return BoostSet; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	UMSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	/** Machine-native shield state; independent of the three selectable slots. */
	UFUNCTION(BlueprintPure, Category = "MS|Shield")
	UMSShieldComponent* GetShieldComponent() const { return ShieldComponent; }

	UFUNCTION(BlueprintPure, Category = "MS|Weapon")
	UMSWeaponVisualComponent* GetWeaponVisualComponent() const { return WeaponVisualComponent; }

	UFUNCTION(BlueprintPure, Category = "MS|Visual")
	UMSMechRigComponent* GetMechRigComponent() const { return MechRigComponent; }

	UFUNCTION(BlueprintPure, Category = "MS|Animation")
	UMSMechAnimatorComponent* GetMechAnimatorComponent() const { return MechAnimatorComponent; }

	UFUNCTION(BlueprintPure, Category = "MS|Visual")
	UMSMeshVisualComponent* GetMeshVisualComponent() const { return MeshVisualComponent; }

	/**
	 * 戦闘系乗数(P6-2)。既定(このクラス/プレイヤー)は全て中立値。
	 * AMSBotCharacter が階級共通の弱体化パラメータを返すようオーバーライドする。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Combat")
	virtual FMSCombatModifiers GetCombatModifiers() const { return FMSCombatModifiers(); }

	/**
	 * UI表示用のパイロット名(キルログ等)。未設定ならアクター名にフォールバックする。
	 * BOTはスポーン時に MSDummyNames::MakeDummyName で「ガンオンN」が設定される(タスク#145)。
	 */
	UFUNCTION(BlueprintPure, Category = "MS|Unit")
	FString GetUnitDisplayName() const { return UnitDisplayName.IsEmpty() ? GetName() : UnitDisplayName; }

	UFUNCTION(BlueprintCallable, Category = "MS|Unit")
	void SetUnitDisplayName(const FString& InName) { UnitDisplayName = InName; }

protected:
	//~ Begin AActor/APawn Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	//~ End AActor/APawn Interface

	/**
	 * Attribute初期値GE(GE_InitStats)を自身へ適用する注入経路。
	 * InitStatsEffectClass 未設定(P2-2でGE実体とAttributeSetが入るまで)の間は
	 * 警告ログのみでスキップする。経路自体は本タスクで完成しており、P2-2は
	 * GE_InitStats アセット作成+FillInitStatsSpec 実装のみで初期値注入が動作する。
	 * @return GEを適用したら true
	 */
	virtual bool ApplyInitStatsEffect();

	/**
	 * GE_InitStats のSpecへ MobileSuitData 由来の値を SetByCaller で書き込むフック。
	 * 基底(P2-2): Data.Init.MaxArmor / Data.Init.MaxDownPoint(Vital)。
	 * P3-2(Boost)でBoostCapacity等を追加する(オーバーライド時はSuper呼び出し必須)。
	 */
	virtual void FillInitStatsSpec(FGameplayEffectSpecHandle& SpecHandle) const;

protected:
	/** UI表示用パイロット名(空ならGetName()フォールバック) */
	UPROPERTY(Transient)
	FString UnitDisplayName;

	/** ASC。Owner=Avatar=自分(InitAbilityActorInfoはPossessedBy/PostInitializeComponentsで実施) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * Vital属性セット(Armor/MaxArmor/DownPoint/MaxDownPoint。§1.2)。
	 * ASCと同一Actorのサブオブジェクトとして生成し、InitAbilityActorInfoで自動登録される。
	 */
	UPROPERTY()
	TObjectPtr<UMSAttributeSet_Vital> VitalSet;

	/**
	 * Boost属性セット(Boost/MaxBoost。P3-2 §1.2)。
	 * 消費で0到達時の強制OH(GE_Overheat+State.Overheated)遷移を自身で担う。
	 */
	UPROPERTY()
	TObjectPtr<UMSAttributeSet_Boost> BoostSet;

	/**
	 * 頭部ヒットボックス(§1.1 / §3.3。Profile=MSHitbox: WeaponTraceのみBlock)。
	 * 位置・半径はP2-5(HS判定)/P5-1(スケルトン確定)で機体ごとに調整する。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Combat")
	TObjectPtr<USphereComponent> HeadHitbox;

	/**
	 * DP自然回復(P2-6。被弾後2.5s無被弾で40/s回復)。
	 * VitalSetの被弾タイムスタンプを参照する軽量Tick(§1.2)。プレイヤー/BOT共通(B-10)。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Combat")
	TObjectPtr<UMSDownPointRegenComponent> DownPointRegenComponent;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UMSHitReactionComponent> HitReactionComponent;

	/**
	 * ブースト消費/回復駆動(P3-2。Unity: HandleBoostState)。消費条件は
	 * UMSCharacterMovementComponent::IsConsumingBoost へ委譲(しゃがみ/よろけ/OH/残量込み)。
	 * レートはInitializeUnitで機体データから設定。プレイヤー/BOT共通(B-10)。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Combat")
	TObjectPtr<UMSBoostComponent> BoostComponent;

	/** Three-slot ammunition and switching state shared by player and BOT. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Combat")
	TObjectPtr<UMSWeaponComponent> WeaponComponent;

	/** Native machine shield durability/guard state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Combat")
	TObjectPtr<UMSShieldComponent> ShieldComponent;

	/** Runtime Cube props synchronized with WeaponComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Visual")
	TObjectPtr<UMSWeaponVisualComponent> WeaponVisualComponent;

	/** Runtime Cube hierarchy replacing the template skeletal placeholder. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Visual")
	TObjectPtr<UMSMechRigComponent> MechRigComponent;

	/** Shared-solver consumer which drives the runtime Cube joints. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Animation")
	TObjectPtr<UMSMechAnimatorComponent> MechAnimatorComponent;

	/** 実メッシュ表示(TAURUS試験導入)。VisualMesh未設定機体では何もしない */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Visual")
	TObjectPtr<UMSMeshVisualComponent> MeshVisualComponent;

	/**
	 * 機体データ(P1-2)。数値パラメータはDataAssetに集約(B-9)。
	 * エディタでの既定機体指定用。実戦ではInitializeUnitで差し替わる。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Unit")
	TObjectPtr<UMSMobileSuitData> MobileSuitData;

	UPROPERTY(Transient)
	TObjectPtr<UMSModuleData> EquippedModule;

	/**
	 * Attribute初期値注入GE(GE_InitStats。Instant)。実体はP2-2で作成し、
	 * サブクラス/BPのデフォルトとして設定する。§1.4
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|GAS")
	TSubclassOf<UGameplayEffect> InitStatsEffectClass;

	/**
	 * 所属チーム(1=Player, 2=Enemy, 255=NoTeam。Core/MSTeamTypes.h)。
	 * デフォルトNoTeam=「未設定が観測可能」(P0-4パターン踏襲)。
	 * AMSPlayerCharacter=TeamPlayer / AMSBotCharacter=階級・陣営に応じ設定(P3-1/P6-1)。
	 */
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_TeamId, BlueprintReadOnly, Category = "MS|Team")
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	UFUNCTION()
	void OnRep_TeamId();

	/** ロックオン照準点のカプセル底からの高さ(cm)。Unity: losEyeHeight=1.4m 相当 */
	UPROPERTY(EditDefaultsOnly, Category = "MS|Combat", meta = (ClampMin = "0.0", Units = "cm"))
	float AimPointHeightAboveBase = 140.0f * MSGO::MSScale;

private:
 UPROPERTY(ReplicatedUsing=OnRep_BattleAssistSnapshot)
 FMSBattleAssistSpawnSnapshot BattleAssistSnapshot;
 TOptional<FMSBattleAssistResolvedLoadout> PendingBattleAssist;
 UFUNCTION() void OnRep_BattleAssistSnapshot();
	/** InitAbilityActorInfo(Owner=Avatar=自分)。多重呼び出し安全 */
	void InitAbilityActorInfoIfNeeded();
};
