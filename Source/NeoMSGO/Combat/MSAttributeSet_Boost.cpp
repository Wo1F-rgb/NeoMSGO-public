// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSUnitBase.h"

#include "GameplayEffectExtension.h"
#include "Combat/MSBoostGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "NeoMSGO.h"
#include "Net/UnrealNetwork.h"

UMSAttributeSet_Boost::UMSAttributeSet_Boost()
{
	// 既定値はUnity版 PlayerController.cs / MobileSuitData 既定(boostCapacity=100)。
	// 実戦値はGE_InitStats(InitializeUnit経由)で必ず上書きされる。満タンで開始。
	InitMaxBoost(100.0f);
	InitBoost(100.0f);
}

void UMSAttributeSet_Boost::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Boost, Boost, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Boost, MaxBoost, COND_None, REPNOTIFY_Always);
}

void UMSAttributeSet_Boost::OnRep_Boost(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Boost, Boost, OldValue);
}

void UMSAttributeSet_Boost::OnRep_MaxBoost(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Boost, MaxBoost, OldValue);
}

void UMSAttributeSet_Boost::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMSAttributeSet_Boost::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMSAttributeSet_Boost::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetBoostAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxBoost());
	}
	else if (Attribute == GetMaxBoostAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UMSAttributeSet_Boost::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute != GetBoostAttribute())
	{
		return;
	}

	// 直接書き込み経路もクランプを保証(PreAttributeBaseChangeとの二重は無害。VitalSetと同パターン)
	SetBoost(FMath::Clamp(GetBoost(), 0.0f, GetMaxBoost()));

	// ユーザー指定仕様(実プレイ4回目の修正): OHは残量が80%まで回復するまで解除しない。
	// 回復GE(正のAdditive)経由で80%に到達したらState.Overheatedを明示的に除去する
	// (色=赤の解除と使用可否ゲートはState.Overheatedタグ参照のため、ここが単一の解除点)。
	if (Data.EvaluatedData.ModifierOp == EGameplayModOp::Additive &&
		Data.EvaluatedData.Magnitude > 0.0f &&
		GetBoost() >= GetMaxBoost() * OverheatReleaseRatio)
	{
		if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
			ASC && ASC->HasMatchingGameplayTag(MSTags::State_Overheated))
		{
			FGameplayTagContainer ReleaseTags;
			ReleaseTags.AddTag(MSTags::State_Overheated);
			ASC->RemoveActiveEffectsWithGrantedTags(ReleaseTags);
			UE_LOG(LogNeoMSGO, Log, TEXT("[Boost] Overheat released on %s (boost %.1f/%.1f >= %.0f%%)"),
				*GetNameSafe(GetOwningActor()), GetBoost(), GetMaxBoost(), OverheatReleaseRatio * 100.0f);
		}
	}

	// 消費(負のAdditive)で0到達 → 強制OH(Unity: currentBoost<=0 で isOverheated=true)。
	// 残量不足の全額請求(ConsumeBoostOrOverheat)は0クランプでここに到達する=残り全消費+強制OH。
	// 回復(正値)やGE_InitStatsのOverrideではOHしない。
	if (Data.EvaluatedData.ModifierOp == EGameplayModOp::Additive &&
		Data.EvaluatedData.Magnitude < 0.0f &&
		GetBoost() <= 0.0f)
	{
		TriggerOverheat();
	}
}

void UMSAttributeSet_Boost::TriggerOverheat()
{
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// 非OH→OHの遷移時のみ(多重OH・タイマー延長なし。Unity: overheatTimer上書きなしの単純遷移)
	if (ASC->HasMatchingGameplayTag(MSTags::State_Overheated))
	{
		return;
	}

	// GE_Overheat(HasDuration=SetByCaller Data.OverheatDuration)でState.Overheatedを付与。
	// 解除は「残量80%回復」時の明示除去(PostGameplayEffectExecute)またはリスポーン
	// (InitializeUnitのRemoveActiveEffectsWithGrantedTags)。Durationは回復が完全に
	// 停止した場合のフェイルセーフとして長めに設定する(3秒固定解除はユーザー指定仕様
	// 「80%までブースト使用不可」と矛盾するため廃止。実プレイ4回目の修正)。
	const FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGE_Overheat::StaticClass(), 1.0f, Context);
	if (Spec.IsValid())
	{
		// The module's forced-OH failsafe is 90s. Keep normal OH at one third of that;
		// both effects normally end earlier as soon as boost recovery reaches 80%.
		constexpr float OverheatFailsafeDurationSeconds = 30.0f;
		const auto* Unit = Cast<AMSUnitBase>(GetOwningActor());
  Spec.Data->SetSetByCallerMagnitude(MSTags::Data_OverheatDuration, OverheatFailsafeDurationSeconds *
   (Unit ? Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::OverheatRecoveryReduction) : 1.f));
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	// HUD(P8-1)・アニメ/VFX・AI向け通知
	// 実プレイ検証用ログ: OH遷移が実際に発生した証拠(実プレイ報告3回目のトレース用)
	UE_LOG(LogNeoMSGO, Log, TEXT("[Boost] Overheat triggered on %s (30s failsafe, State.Overheated applied=%d)"),
		*GetNameSafe(GetOwningActor()),
		ASC->HasMatchingGameplayTag(MSTags::State_Overheated) ? 1 : 0);
	OnOverheat.Broadcast(GetOwningActor());
}
