// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-3: ダメージ経路用の拡張GameplayEffectContext。
// 設計元: docs/UE5_Architecture.md §1.6 入力表「FMSGameplayEffectContext」
//
// 保持する追加情報:
//  - bIsHeadshot      : HS判定結果(判定ロジック自体はP2-5。トレース側=UMSCombatStatics::ResolveHitscanが
//                       確定してここへ格納する。ExecutionはフラグだけをみてHS倍率1.5を適用する)
//  - AttackerLocation : 攻撃発生位置(盾正面判定§1.6手順4、被弾方向HUD通知§1.2で使用)。
//                       未設定(bHasAttackerLocation=false)の経路は盾軽減しない(Unity版仕様踏襲)。
//
// 生成経路: UAbilitySystemGlobals::AllocGameplayEffectContext を UMSAbilitySystemGlobals が
// オーバーライドしているため、ASC->MakeEffectContext() は常に本型を返す(Config/DefaultGame.ini)。

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "MSGameplayEffectContext.generated.h"

USTRUCT()
struct NEOMSGO_API FMSGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

public:
	FMSGameplayEffectContext() = default;

	/** HS判定結果(P2-5のトレース側が設定。Executionは手順3で×1.5に使用) */
	bool IsHeadshot() const { return bIsHeadshot; }
	void SetHeadshot(const bool bInHeadshot) { bIsHeadshot = bInHeadshot; }
	bool IsForceKnockdown() const { return bForceKnockdown; }
	void SetForceKnockdown(bool bValue) { bForceKnockdown = bValue; }

	/** AttackerLocationが設定済みか(未設定経路は盾軽減スキップ。§1.6手順4) */
	bool HasAttackerLocation() const { return bHasAttackerLocation; }
	FVector GetAttackerLocation() const { return AttackerLocation; }
	void SetAttackerLocation(const FVector& InLocation)
	{
		AttackerLocation = InLocation;
		bHasAttackerLocation = true;
	}

 // Optional launch-time attack snapshot. Negative means instantaneous legacy caller.
 float GetShieldDamageScale() const { return ShieldDamageScale; }
 void SetShieldDamageScale(float Value) { ShieldDamageScale = FMath::Max(0.f, Value); }
	//~ Begin FGameplayEffectContext Interface
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FMSGameplayEffectContext::StaticStruct();
	}

	virtual FMSGameplayEffectContext* Duplicate() const override
	{
		FMSGameplayEffectContext* NewContext = new FMSGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// HitResultはディープコピー(基底Duplicateと同じ規約)
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	// シングルプレイのため実送信はないが、GAS規約としてNetSerializeは正しく実装しておく
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
	//~ End FGameplayEffectContext Interface

	/**
	 * Handleから本型を安全に取り出す(非FMS型のContextならnullptr)。
	 * Execution/PostGameplayEffectExecute からの読み取り入口。
	 */
	static const FMSGameplayEffectContext* Get(const FGameplayEffectContextHandle& Handle);
	static FMSGameplayEffectContext* Get(FGameplayEffectContextHandle& Handle);

protected:
	UPROPERTY() bool bForceKnockdown = false;
 UPROPERTY() float ShieldDamageScale = -1.f;
	/** 攻撃発生位置(発射者の射点/爆心。盾正面判定・被弾方向通知用) */
	UPROPERTY()
	FVector AttackerLocation = FVector::ZeroVector;

	/** ヘッドショットか(§1.6手順3) */
	UPROPERTY()
	bool bIsHeadshot = false;

	/** AttackerLocationが有効値か(ZeroVectorとの区別のため明示フラグ) */
	UPROPERTY()
	bool bHasAttackerLocation = false;
};

template <>
struct TStructOpsTypeTraits<FMSGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FMSGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
