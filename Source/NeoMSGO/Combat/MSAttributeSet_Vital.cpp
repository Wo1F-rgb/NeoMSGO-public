// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSAttributeSet_Vital.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSRadarPod.h"
#include "Net/UnrealNetwork.h"

#include "AbilitySystemGlobals.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Combat/MSHitReactionComponent.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Engine/GameInstance.h"

UMSAttributeSet_Vital::UMSAttributeSet_Vital()
{
	// 既定値はUnity版 Health.cs のSerializeFieldデフォルトに合わせる(maxArmor=5000 / maxDownPoint=100)。
	// 実戦値はGE_InitStats(InitializeUnit経由)で必ず上書きされる。
	InitMaxArmor(5000.0f);
	InitArmor(5000.0f);
	InitMaxDownPoint(100.0f);
	InitDownPoint(0.0f);
}

void UMSAttributeSet_Vital::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Vital, Armor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Vital, MaxArmor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Vital, DownPoint, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMSAttributeSet_Vital, MaxDownPoint, COND_None, REPNOTIFY_Always);
}

void UMSAttributeSet_Vital::OnRep_Armor(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Vital, Armor, OldValue);
}

void UMSAttributeSet_Vital::OnRep_MaxArmor(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Vital, MaxArmor, OldValue);
}

void UMSAttributeSet_Vital::OnRep_DownPoint(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Vital, DownPoint, OldValue);
}

void UMSAttributeSet_Vital::OnRep_MaxDownPoint(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMSAttributeSet_Vital, MaxDownPoint, OldValue);
}

void UMSAttributeSet_Vital::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMSAttributeSet_Vital::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMSAttributeSet_Vital::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetArmorAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxArmor());
	}
	else if (Attribute == GetDownPointAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxDownPoint());
	}
	else if (Attribute == GetMaxArmorAttribute() || Attribute == GetMaxDownPointAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UMSAttributeSet_Vital::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetArmorAttribute())
	{
		// Executionによる直接書き込み経路もクランプを保証する(PreAttributeBaseChangeとの二重は無害)
		SetArmor(FMath::Clamp(GetArmor(), 0.0f, GetMaxArmor()));

		// 被弾(Armor減少)で回復開始遅延をリセット(P2-6。GE_InitStats等のOverride経路は被弾ではない)
		if (Data.EvaluatedData.ModifierOp == EGameplayModOp::Additive && Data.EvaluatedData.Magnitude < 0.0f)
		{
			MarkDamageReceived();
			const FGameplayEffectContextHandle& ContextHandle = Data.EffectSpec.GetEffectContext();
			const FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(ContextHandle);
			const FVector AttackerLocation = MSContext ? MSContext->GetAttackerLocation() : FVector::ZeroVector;
			OnDamagedFrom.Broadcast(GetOwningActor(), ContextHandle.GetOriginalInstigator(), AttackerLocation);
			if (GetArmor()>0 && MSContext && MSContext->IsForceKnockdown())
				if (auto* Unit=Cast<AMSUnitBase>(GetOwningActor()))
					if (auto* Reaction=Unit->GetHitReactionComponent()) Reaction->StartHeavyReaction(true);
		}

		if (GetArmor() <= 0.0f)
		{
			HandleDeath(Data);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetDownPointAttribute())
	{
		SetDownPoint(FMath::Clamp(GetDownPoint(), 0.0f, GetMaxDownPoint()));

		// DP加算(=被弾)のみが閾値遷移と回復遅延リセットの対象。
		// GE_DPRecoveryの減算(負値Additive)・GE_InitStatsの0リセット(Override)は対象外。
		if (Data.EvaluatedData.ModifierOp == EGameplayModOp::Additive && Data.EvaluatedData.Magnitude > 0.0f)
		{
			MarkDamageReceived();
			const auto* Unit=Cast<AMSUnitBase>(GetOwningActor());
			if (auto* Reaction=Unit?Unit->GetHitReactionComponent():nullptr)
			{
				if (UMSHitReactionComponent::ProtectsDownGauge(GetOwningAbilitySystemComponent())) SetDownPoint(0);
				else if (GetDownPoint()>=GetMaxDownPoint()) Reaction->StartHeavyReaction();
				else if (GetDownPoint()>=800.f && GetDownPoint()-Data.EvaluatedData.Magnitude<800.f) Reaction->StartFlinch();
			}
			else if (GetDownPoint() >= GetMaxDownPoint())
			{
				HandleDownPointThreshold();
			}
		}
	}
}

void UMSAttributeSet_Vital::NotifyShieldDamage(const FGameplayEffectContextHandle& ContextHandle)
{
	MarkDamageReceived();
	const FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(ContextHandle);
	const FVector AttackerLocation = MSContext ? MSContext->GetAttackerLocation() : FVector::ZeroVector;
	OnDamagedFrom.Broadcast(GetOwningActor(), ContextHandle.GetOriginalInstigator(), AttackerLocation);
}

void UMSAttributeSet_Vital::HandleDeath(const FGameplayEffectModCallbackData& Data)
{
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// 生存→死亡の遷移時のみ(§1.2)。死体への多重ヒットでは再発火しない。
	if (ASC->HasMatchingGameplayTag(MSTags::State_Dead))
	{
		return;
	}

	// GE_Dead(Infinite)でState.Deadを付与。除去はリスポーン=InitializeUnit(P3-7)側。
	const FGameplayEffectContextHandle DeadContext = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle DeadSpec = ASC->MakeOutgoingSpec(UGE_Dead::StaticClass(), 1.0f, DeadContext);
	if (DeadSpec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*DeadSpec.Data);
	}

	AActor* DeadUnit = GetOwningActor();
	if (auto* Unit=Cast<AMSUnitBase>(DeadUnit))
		if (auto* Reaction=Unit->GetHitReactionComponent()) Reaction->ResetReaction();
	AActor* Killer = Data.EffectSpec.GetEffectContext().GetOriginalInstigator();

	// Event.Death をGameplayEventで通知(アニメ/AI向け。§2.5)
	FGameplayEventData Payload;
	Payload.EventTag = MSTags::Event_Death;
	Payload.Instigator = Killer;
	Payload.Target = DeadUnit;
	ASC->HandleGameplayEvent(MSTags::Event_Death, &Payload);

	// 機体死亡の爆発Cueは即時発火しない。既存の死亡演出(#65: しゃがみ3秒→爆発→再出撃/非表示)
	// が UMSRespawnComponent::HandleExplosionTimer→OnExplosionDue経由で正しいタイミングで
	// Cue_Explosionを発火する(AMSBotCharacter::HandleExplosionDue / AMSPlayerCharacter::
	// HandleDeathExplosionDue)。ここで即時発火すると3秒待機中に爆発音/VFXが先行してしまう。

	// P4-13: 死亡遷移が確定した時だけ、Killer側ASCへ撃破確認を返す。
	// Destroying service equipment disables support but is not a unit kill.
	if (Killer && !Cast<AMSRepairPod>(DeadUnit) && !Cast<AMSRadarPod>(DeadUnit))
	{
		if (UAbilitySystemComponent* KillerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Killer))
		{
			FGameplayEffectContextHandle KillContext = KillerASC->MakeEffectContext();
			KillContext.AddInstigator(Killer, Killer);
			KillContext.AddSourceObject(DeadUnit);
			FGameplayCueParameters CueParameters(KillContext);
			CueParameters.Instigator = Killer;
			CueParameters.Location = DeadUnit ? DeadUnit->GetActorLocation() : FVector::ZeroVector;
			KillerASC->ExecuteGameplayCue(MSTags::Cue_Kill_Confirm, CueParameters);
		}

		if (const UWorld* World = Killer->GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UMSHitFeedbackSubsystem* Feedback = GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
				{
					Feedback->BroadcastKillConfirmed(Killer);
				}
			}
		}
	}

	// キル通知(P2-3)・リスポーン(P3-7)・戦力ゲージ(P7-3)向けデリゲート
	OnDeath.Broadcast(DeadUnit, Killer);
}

void UMSAttributeSet_Vital::HandleDownPointThreshold()
{
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// Unity: AddDownPoint の IsDead || IsStaggered ガード相当。
	// (通常経路ではExecution側のDP出力0化で到達しないが、LooseTag等の直接加算経路への防衛線)
	FGameplayTagContainer BlockedTags;
	BlockedTags.AddTag(MSTags::State_Dead);
	BlockedTags.AddTag(MSTags::State_Staggered);
	BlockedTags.AddTag(MSTags::State_KnockedDown);
	if (ASC->HasAnyMatchingGameplayTags(BlockedTags))
	{
		return;
	}

	// 閾値到達でDPは0へリセット(Unity: currentDownPoint = 0)
	SetDownPoint(0.0f);

	// 接地判定はCharacterMovementのIsMovingOnGroundに統一(計画書P2-6ガードレール。BOT特殊分岐廃止)。
	// 非Character保持者(通常はDP属性を使わない)は非接地=空中ダウン扱い(Unity版フォールバック踏襲)。
	bool bGrounded = false;
	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwningActor()))
	{
		const UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();
		bGrounded = MoveComp && MoveComp->IsMovingOnGround();
	}
	const bool bKnockedDown = !bGrounded;

	// 接地→GE_Stagger(1.2s)/非接地→GE_KnockedDown(2.5s)。時限解除はGEのDuration失効に委ねる
	const TSubclassOf<UGameplayEffect> StaggerEffectClass = bKnockedDown
		? TSubclassOf<UGameplayEffect>(UGE_KnockedDown::StaticClass())
		: TSubclassOf<UGameplayEffect>(UGE_Stagger::StaticClass());

	const FGameplayEffectContextHandle StaggerContext = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle StaggerSpec = ASC->MakeOutgoingSpec(StaggerEffectClass, 1.0f, StaggerContext);
	if (StaggerSpec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*StaggerSpec.Data);
	}

	// Event.Stagger をGameplayEventで通知(アニメP5-5/AI P6-1向け。§2.5。1=空中ダウン/0=よろけ)
	FGameplayEventData Payload;
	Payload.EventTag = MSTags::Event_Stagger;
	Payload.Target = GetOwningActor();
	Payload.EventMagnitude = bKnockedDown ? 1.0f : 0.0f;
	ASC->HandleGameplayEvent(MSTags::Event_Stagger, &Payload);

	// Unity: OnStaggerBegin(isKnockedDown) 相当
	OnStagger.Broadcast(GetOwningActor(), bKnockedDown);
}

void UMSAttributeSet_Vital::MarkDamageReceived()
{
	const AActor* OwningActor = GetOwningActor();
	const UWorld* World = OwningActor ? OwningActor->GetWorld() : nullptr;
	if (World)
	{
		LastDamageTime = World->GetTimeSeconds();
	}
}
