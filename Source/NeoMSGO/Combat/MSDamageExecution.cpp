// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSDamageExecution.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSRadarPod.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSHitReactionComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "Gameplay/MSGameMode.h"
#include "Player/MSCharacterMovementComponent.h"

namespace
{
	/** キャプチャ定義(Target側 Armor/DownPoint、非Snapshot。§1.6 入力表) */
	struct FMSDamageStatics
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(Armor);
		DECLARE_ATTRIBUTE_CAPTUREDEF(DownPoint);

		FMSDamageStatics()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UMSAttributeSet_Vital, Armor, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UMSAttributeSet_Vital, DownPoint, Target, false);
		}
	};

	const FMSDamageStatics& DamageStatics()
	{
		static FMSDamageStatics Statics;
		return Statics;
	}
}

UMSDamageExecution::UMSDamageExecution()
{
	RelevantAttributesToCapture.Add(DamageStatics().ArmorDef);
	RelevantAttributesToCapture.Add(DamageStatics().DownPointDef);
}

void UMSDamageExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const FGameplayEffectContextHandle& ContextHandle = Spec.GetEffectContext();

	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	AActor* TargetActor = TargetASC ? TargetASC->GetAvatarActor() : nullptr;
	if (!TargetActor)
	{
		return;
	}
	// A listen room keeps its world ticking while the host reads Briefing.
	// Reject damage at the authoritative GAS execution point until a human has
	// successfully deployed; skipping only gauge callbacks would still let a
	// remote guest destroy a home-base core during preparation.
	if (const UWorld* World = TargetActor->GetWorld())
	{
		if (const AMSGameMode* Mode = World->GetAuthGameMode<AMSGameMode>();
			Mode && Mode->IsWaitingForRoomDeployment()) return;
	}

	// ------------------------------------------------------------------
	// 1. FF判定(B-2)。falseなら Damage=0/DP=0 で即終了。
	//    「弾が味方に当たって止まる」はトレース側(P4-2)の責務で、ここは最後の防衛線。
	// ------------------------------------------------------------------
	AActor* Shooter = ContextHandle.GetOriginalInstigator();
	if (const AMSRepairPod* Pod = Cast<AMSRepairPod>(TargetActor))
	{
		// Neutral damage sources must not destroy a team's service equipment;
		// the normal FF guard alone treats NoTeam as universally attackable.
		if (Pod->IsDestroyed() || UMSTeamStatics::SolveAttitude(
			UMSTeamStatics::GetTeamId(Shooter), Pod->GetGenericTeamId()) != ETeamAttitude::Hostile)
			return;
	}
	if (const AMSRadarPod* Radar = Cast<AMSRadarPod>(TargetActor))
	{
		if (!Radar->IsActive() || UMSTeamStatics::SolveAttitude(
			UMSTeamStatics::GetTeamId(Shooter), Radar->GetGenericTeamId()) != ETeamAttitude::Hostile)
			return;
	}
	if (!UMSTeamStatics::CanDamage(Shooter, TargetActor))
	{
		return;
	}

	float Damage = Spec.GetSetByCallerMagnitude(MSTags::Data_Damage, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);

	// ------------------------------------------------------------------
	// 2. 爆風減衰(P2-4接続点)。falloffは呼び出し側で0〜1に確定済み。非爆風=未設定は1.0。
	// ------------------------------------------------------------------
	const float SplashFalloff = Spec.GetSetByCallerMagnitude(MSTags::Data_SplashFalloff, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/1.0f);
	Damage *= SplashFalloff;

	// ------------------------------------------------------------------
	// 3. HS倍率(P2-5接続点)。判定はトレース側が確定し、Contextのフラグのみ参照する。
	// ------------------------------------------------------------------
	const FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(ContextHandle);
	if (MSContext && MSContext->IsHeadshot())
	{
		Damage *= HeadshotMultiplier;
	}

	// ------------------------------------------------------------------
	// 4. Native shield absorption. The component is authoritative and checks its
	// current state, angle, and attack/switch restrictions; State.Guarding is only
	// a presentation tag and cannot grant protection by itself.
	// ------------------------------------------------------------------
 // Armor modules apply to the matching damage packet, independently of DP.
 // SourceObject is authored by the firing ability on authority, never an RPC multiplier.
 const auto* SourceWeapon = Cast<UMSWeaponDataBase>(ContextHandle.GetSourceObject());
 const bool bExplosivePacket = Spec.Def && Spec.Def->IsA(UGE_SplashDamage::StaticClass());
 if (const auto* Unit = Cast<AMSUnitBase>(TargetActor))
 {
  if (bExplosivePacket) Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::ExplosiveDamageReduction);
  else if (SourceWeapon && SourceWeapon->GetEffectiveDamageKind() == EMSWeaponDamageKind::Melee) Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::MeleeDamageReduction);
  else if (SourceWeapon && SourceWeapon->GetEffectiveDamageKind() == EMSWeaponDamageKind::Beam)
   Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::BeamDamageReduction);
  else if (SourceWeapon && SourceWeapon->GetEffectiveDamageKind() == EMSWeaponDamageKind::Explosive) Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::ExplosiveDamageReduction);
  else if (Cast<UMSRangedWeaponData>(SourceWeapon)) Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::BallisticDamageReduction);
  if (const auto* Movement = Cast<UMSCharacterMovementComponent>(Unit->GetCharacterMovement()); Movement && Movement->IsUnderwater())
   Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::UnderwaterDamageReduction);
  // Rear hemisphere in the target's horizontal facing. Splash has no rear-hit bonus.
  if (!bExplosivePacket && MSContext && MSContext->HasAttackerLocation())
  {
   const FVector ToSource = (MSContext->GetAttackerLocation() - Unit->GetActorLocation()).GetSafeNormal2D();
   if (FVector::DotProduct(Unit->GetActorForwardVector().GetSafeNormal2D(), ToSource) < 0.f)
    Damage *= Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::BackshotDamageReduction);
  }
 }
 float ShieldAttackScale = 1.f;
 if (const auto* AttackerUnit = Cast<AMSUnitBase>(Shooter))
  ShieldAttackScale += AttackerUnit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::ShieldDamageIncrease) * .01f;
 if (MSContext && MSContext->GetShieldDamageScale() >= 0.f) ShieldAttackScale = MSContext->GetShieldDamageScale();
	float ArmorDamage = Damage;
	float ShieldDamage = 0.0f;
	bool bGuarded = false;
	if (AMSUnitBase* TargetUnit = Cast<AMSUnitBase>(TargetActor))
	{
		if (UMSShieldComponent* Shield = TargetUnit->GetShieldComponent();
			Shield && MSContext && MSContext->HasAttackerLocation())
		{
			const bool bExplosive = Spec.Def && Spec.Def->IsA(UGE_SplashDamage::StaticClass());
			if (Shield->TryAbsorbDamage(Damage, bExplosive, MSContext->GetAttackerLocation(),
				ArmorDamage, ShieldDamage, bGuarded, ShieldAttackScale))
			{
				if (UMSAttributeSet_Vital* Vital = TargetUnit->GetVitalSet())
				{
					Vital->NotifyShieldDamage(ContextHandle);
				}
			}
		}
	}

	// ------------------------------------------------------------------
	// 5. Armor -= max(1, round(Damage))(Unity: scaledDamage = Max(1, Round(...)))
	//    死亡遷移・キル通知は VitalSet::PostGameplayEffectExecute 側(重複実装禁止)。
	// ------------------------------------------------------------------
	// Preserve the legacy minimum-one rule for ordinary hits, but do not invent
	// one point of Armor loss for a fully absorbed direct shield packet.
	const float FinalDamage = (bGuarded && ArmorDamage <= 0.0f)
		? 0.0f : FMath::Max(1.0f, FMath::RoundToFloat(ArmorDamage));
	if (FinalDamage > 0.0f)
	{
		OutExecutionOutput.AddOutputModifier(
			FGameplayModifierEvaluatedData(DamageStatics().ArmorProperty, EGameplayModOp::Additive, -FinalDamage));
	}
	else if (ShieldDamage>0.f && MSContext && MSContext->IsForceKnockdown())
	{
		// A shield-absorbed D melee hit still has a physical impact; no invented Armor damage.
		if (auto* Unit=Cast<AMSUnitBase>(TargetActor))
			if (auto* Reaction=Unit->GetHitReactionComponent()) Reaction->StartHeavyReaction(true);
	}

	// ------------------------------------------------------------------
	// 6. DP同時加算。よろけ/ダウン中(P2-6接続点)は蓄積無敵猶予で0。
	// ------------------------------------------------------------------
	if (!UMSHitReactionComponent::ProtectsDownGauge(TargetASC))
	{
		float DownPoint = Spec.GetSetByCallerMagnitude(MSTags::Data_DownPoint, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);
		if (bGuarded)
		{
			if (const AMSUnitBase* TargetUnit = Cast<AMSUnitBase>(TargetActor))
			{
				if (const UMSShieldComponent* Shield = TargetUnit->GetShieldComponent(); Shield && Shield->GetShieldData())
				{
					DownPoint *= FMath::Clamp(Shield->GetShieldData()->GuardDownPointMultiplier, 0.0f, 1.0f);
				}
			}
		}
		if (DownPoint > 0.0f)
		{
			OutExecutionOutput.AddOutputModifier(
				FGameplayModifierEvaluatedData(DamageStatics().DownPointProperty, EGameplayModOp::Additive, DownPoint));
		}
	}
}
