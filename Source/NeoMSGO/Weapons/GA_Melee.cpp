// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/GA_Melee.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffectTypes.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Animation/MSDualSaberMotion.h"
#include "Animation/MSAnimInstance.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Player/MSCharacterMovementComponent.h"
#include "TimerManager.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

float UGA_Melee::GetMeleeReachCmForWeapon(const UMSMeleeWeaponData* Weapon, bool bDashMelee)
{
	return Weapon ? Weapon->GetRangeWorldCm()
		* (bDashMelee ? DashMeleeReachMultiplier : 1.0f) : 0.0f;
}

UGA_Melee::UGA_Melee()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Melee::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Attacker = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;

	// D格意図はブーストダッシュ状態から取る(Unity: PlayerController.IsDashing)。
	// IsBoostDashing はしゃがみ/OH/残量無しで false を返すが、PerformMelee側でも
	// State.Crouching を最終防衛線として確認する(BOT等の直接呼び出し経路のため)。
	bool bWantsDash = false;
	if (const ACharacter* Character = Cast<ACharacter>(Attacker))
	{
		if (const UMSCharacterMovementComponent* MoveComp =
			Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			bWantsDash = MoveComp->IsBoostDashing();
		}
	}

	// P4-5チャージ機構は射撃系(UMSRangedWeaponData.bIsChargeable)専用のため、
	// 入力起動経路のチャージ倍率は1.0固定(P4-7受け入れ条件2の許容範囲)。
	const bool bSwung = Attacker && PerformMelee(Attacker,
		Attacker->GetActorLocation(), Attacker->GetActorForwardVector(), bWantsDash, 1.0f);
	// UE 5.7 clears every timer bound to a gameplay ability when EndAbility runs.
	// The melee motion/recovery timers are bound to this instance, so keep the
	// activation alive until those callbacks have released State.MeleeRecovery.
	if (IsActive() && (!bSwung || (!RecoveryASC.IsValid() && !StandardSaberActor.IsValid()
		&& !LungeActor.IsValid())))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, !bSwung);
	}
}

bool UGA_Melee::PerformMelee(AActor* Attacker, const FVector& Origin, const FVector& Direction,
	const bool bWantsDashMelee, const float ChargeMultiplier)
{
	if (!Attacker)
	{
		return false;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);

	UMSWeaponComponent* Weapons = Attacker->FindComponentByClass<UMSWeaponComponent>();
	if (!Weapons)
	{
		return false;
	}

	const int32 SlotIndex = Weapons->GetActiveSlotIndex();
	const UMSMeleeWeaponData* Weapon = Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(SlotIndex));
	if (StandardSaberActor.IsValid()
		&& (StandardSaberWeapons.Get() != Weapons || StandardSaberSlotIndex != SlotIndex
			|| StandardSaberWeapon.Get() != Weapon))
	{
		// PerformMeleeが武器切替直後にも呼ばれた場合を含め、現在の武器が
		// 標準フラグなしでも旧経路へ通さず、残った標準刃を先に止める。
		ResetStandardSaberCombo(true, true);
	}
	if (!Weapon)
	{
		return false;
	}

	// 標準サーベルだけは、段入力を含むサーバー時系列経路へ分岐する。
	// 旧武器はこの分岐を通らず、従来の起動時Overlapとクールダウン規則を維持する。
	if (Weapon->UsesTimedSaberMotion())
	{
		return PerformStandardSaberMelee(Attacker, Origin, Direction, bWantsDashMelee,
			ChargeMultiplier, ASC, Weapons, SlotIndex, Weapon);
	}

	// 攻撃後硬直中は次の攻撃不可(Unity: dashMeleeRecoveryTime中の入力ブロック)
	if (ASC && ASC->HasMatchingGameplayTag(MSTags::State_MeleeRecovery))
	{
		return false;
	}

	if (!Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	// しゃがみ中はD格不可=通常格闘へ降格(Unity: しゃがみ中はIsDashing=falseのためD格が発動しない。
	// ブースト消費・突進・拡大判定のいずれも行わない)
	const bool bDashMelee = bWantsDashMelee
		&& !(ASC && ASC->HasMatchingGameplayTag(MSTags::State_Crouching));

	// D格は武器ごとのブーストを消費。残量不足時は残り全消費+強制OH(ConsumeBoostOrOverheat)だが、
	// 突進・攻撃自体は続行する(Unity: ConsumeBoost #150 と同じ「消費は常に成立」規則)
	if (bDashMelee)
	{
		const auto* AssistUnit = Cast<AMSUnitBase>(Attacker);
		UMSCombatStatics::ConsumeBoostOrOverheat(ASC, Weapon->DashMeleeBoostCost * (AssistUnit ? AssistUnit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::DashMeleeBoostReduction) : 1.f));
	}

	// D格の3倍はHP威力のみ。DPは補正・チャージ倍率を維持し、強制転倒はContextで伝える。
	float MeleeCorrection = 1.0f;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (const UMSMobileSuitData* Suit = Unit->GetMobileSuitData())
		{
			MeleeCorrection = Suit->MeleeCorrection;
		}
	}
	// P6-2: BOT弱体化のdamageMultiplier(既定0.3)をAttacker側から注入する。プレイヤーは
	// AMSUnitBase既定のGetCombatModifiers()=1.0のため挙動は変わらない。
	float ShooterDamageMultiplier = 1.0f;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		ShooterDamageMultiplier = Unit->GetCombatModifiers().DamageMultiplier;
	}
	const float BaseMultiplier = CalcDownPointMultiplier(MeleeCorrection, ChargeMultiplier)
		* FMath::Max(0.0f, ShooterDamageMultiplier);
	const float DamageMultiplier = BaseMultiplier * (bDashMelee ? DashMeleeDamageMultiplier : 1.0f);

	// 判定球: 中心 = Origin + Direction*リーチ、半径 = 1.5m/2.8m(Unity: Physics.OverlapSphere)
	const FVector Dir = Direction.GetSafeNormal();
	const float ReachCm = GetMeleeReachCmForWeapon(Weapon, bDashMelee);
	bLastAttackWasDash = bDashMelee;
	if (ASC)
	{
		FGameplayEffectContextHandle FireContext = ASC->MakeEffectContext();
		FireContext.AddInstigator(Attacker, Attacker);
		FireContext.AddSourceObject(Weapon);
		FGameplayCueParameters FireParameters(FireContext);
		FireParameters.Location = Origin;
		FireParameters.SourceObject = Weapon;
		ASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
	}
	LastHitCount = ApplyMeleeHits(Attacker, Origin, Dir, ReachCm, GetMeleeRadiusCm(bDashMelee),
		Weapon->Power * DamageMultiplier, Weapon->DownPoint * BaseMultiplier, Weapon, bDashMelee, LastSuccessfulHitCount);
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
		{
			Animator->StartMeleeMotion(Weapon->MeleeMotionType, Weapon->Power, bDashMelee);
		}
		if (UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
			Rig && Weapon->MeleeMotionType == EMSMeleeMotionType::Slash)
		{
			const FName AuthoredMotion = Weapon->IsA<UMSDualHeatSwordWeaponData>()
				? FName(TEXT("Slash_Dual")) : FName(TEXT("Slash_Single"));
			if (Rig->PlayMotion(AuthoredMotion))
			{
				UE_LOG(LogTemp, Display, TEXT("[Task235][DualHeatSword] Weapon=%s Motion=%s"),
					*GetNameSafe(Weapon), *AuthoredMotion.ToString());
			}
		}
		// Preserve compatibility for projects still assigning UMSAnimInstance to the hidden mesh.
		if (UMSAnimInstance* Anim = Cast<UMSAnimInstance>(Unit->GetMesh()->GetAnimInstance()))
		{
			Anim->StartMeleeMotion(Weapon->MeleeMotionType, Weapon->Power, bDashMelee);
		}
	}

	// D格突進: 26m/s × 0.35s の直線移動
	if (bDashMelee)
	{
		StartLunge(Attacker, Dir);
	}

	// 攻撃後硬直0.45s(D格は突進0.35sを終えた後に硬直へ入るため、タグはまとめて維持する)
	StartRecovery(Attacker, ASC,
		MeleeRecoverySeconds * (LastSuccessfulHitCount > 0 && Cast<AMSUnitBase>(Attacker)
   ? CastChecked<AMSUnitBase>(Attacker)->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::MeleeHitRecoveryReduction) : 1.f)
   + (bDashMelee ? DashMeleeLungeDurationSeconds : 0.0f));
	return true;
}

bool UGA_Melee::PerformStandardSaberMelee(AActor* Attacker, const FVector& Origin,
	const FVector& Direction, const bool bWantsDashMelee, const float ChargeMultiplier,
	UAbilitySystemComponent* ASC, UMSWeaponComponent* Weapons, const int32 SlotIndex,
	const UMSMeleeWeaponData* Weapon)
{
	// 標準経路はサーバーだけが状態、クールダウン、GEを進める。
	if (!Attacker || !Attacker->HasAuthority() || !Weapons || !Weapon)
	{
		return false;
	}

	if (StandardSaberActor.IsValid())
	{
		if (StandardSaberActor.Get() != Attacker || StandardSaberWeapons.Get() != Weapons
			|| StandardSaberSlotIndex != SlotIndex || StandardSaberWeapon.Get() != Weapon)
		{
			// スロット／武器が変わったら、残った刃判定・突進・段入力を破棄する。
			ResetStandardSaberCombo(true, true);
			return false;
		}

		if (IsStandardSaberInterrupted())
		{
			ResetStandardSaberCombo(true, true);
			return false;
		}

		// 入力時点までを先に消化する。大きなワールドtickでも、入力窓の境界を
		// タイマーのコールバック順序に依存させない。
		StandardSaberTick();
		if (!StandardSaberActor.IsValid())
		{
			return false;
		}

		const UWorld* World = Attacker->GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		const float Elapsed = Now - StandardSaberStartWorldTime;
		if (FMSStandardSaberMotion::HasFollowUp(StandardSaberAttackIndex))
		{
			if (bStandardSaberStageActive && Elapsed >= 0.30f)
			{
				// 初段の後半だけを入力バッファに取り込む。早すぎる連打は
				// 武器側ConsumeAmmoAndStartCooldownへ到達しない。
				bStandardSaberPendingFollowUp = true;
				return true;
			}

			if (!bStandardSaberStageActive && StandardSaberComboDeadlineWorldTime >= Now
				&& Elapsed >= GetStandardSaberStageDuration())
			{
				ClearStandardSaberRecovery();
				return StartStandardSaberStage(StandardSaberAttackIndex + 1);
			}
		}

		// D格は単発。初段の入力窓以外も、進行中の段を再起動しない。
		return false;
	}

	if (ASC)
	{
		FGameplayTagContainer InterruptedTags;
		InterruptedTags.AddTag(MSTags::State_Dead);
		InterruptedTags.AddTag(MSTags::State_Flinched);
		InterruptedTags.AddTag(MSTags::State_Staggered);
		InterruptedTags.AddTag(MSTags::State_KnockedDown);
		InterruptedTags.AddTag(MSTags::State_GettingUp);
		InterruptedTags.AddTag(MSTags::State_EmergencyRecall);
		if (ASC->HasAnyMatchingGameplayTags(InterruptedTags))
		{
			return false;
		}
	}
	if (ASC && ASC->HasMatchingGameplayTag(MSTags::State_MeleeRecovery))
	{
		return false;
	}
	if (Weapons->IsSwitching() || !Weapons->CanFire(SlotIndex))
	{
		return false;
	}

	// ここで初めて弾数／武器クールダウンを消費する。Startup中や硬直中の
	// 追加入力は上の分岐でreturnするため、連打でクールダウンを空振りしない。
	if (!Weapons->ConsumeAmmoAndStartCooldown(SlotIndex))
	{
		return false;
	}

	const bool bDashMelee = bWantsDashMelee
		&& !(ASC && ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	const FVector SafeDirection = Direction.GetSafeNormal();
	if (SafeDirection.IsNearlyZero())
	{
		return false;
	}

	float MeleeCorrection = 1.0f;
	float ShooterDamageMultiplier = 1.0f;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (const UMSMobileSuitData* Suit = Unit->GetMobileSuitData())
		{
			MeleeCorrection = Suit->MeleeCorrection;
		}
		ShooterDamageMultiplier = Unit->GetCombatModifiers().DamageMultiplier;
	}
	const float BaseMultiplier = CalcDownPointMultiplier(MeleeCorrection, ChargeMultiplier)
		* FMath::Max(0.0f, ShooterDamageMultiplier);

	StandardSaberActor = Attacker;
	StandardSaberWeapons = Weapons;
	StandardSaberASC = ASC;
	StandardSaberWeapon = const_cast<UMSMeleeWeaponData*>(Weapon);
	StandardSaberSlotIndex = SlotIndex;
	StandardSaberAttackIndex = Weapon->bUseGundamDualSaberMotion
		? (bDashMelee ? FMSStandardSaberMotion::DualDash : FMSStandardSaberMotion::DualFirst)
		: (bDashMelee ? 2 : 0);
	bStandardSaberDash = bDashMelee;
	bLastAttackWasDash = bDashMelee;
	StandardSaberDamage = Weapon->Power * BaseMultiplier
		* (bDashMelee ? DashMeleeDamageMultiplier : 1.0f);
	// 標準サーベルD格は威力だけ3倍。Wiki記載のDP値は通常格闘と同じ扱いにする。
	StandardSaberDownPoint = Weapon->DownPoint * BaseMultiplier;
	StandardSaberDirection = SafeDirection;
	StandardSaberFireOrigin = Origin;
	bStandardSaberPendingFollowUp = false;
	LastStandardSaberAttackIndex = INDEX_NONE;
	LastHitCount = 0;
	LastSuccessfulHitCount = 0;

	if (bDashMelee)
	{
		const auto* AssistUnit = Cast<AMSUnitBase>(Attacker);
		UMSCombatStatics::ConsumeBoostOrOverheat(ASC, Weapon->DashMeleeBoostCost * (AssistUnit ? AssistUnit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::DashMeleeBoostReduction) : 1.f));
	}

	return StartStandardSaberStage(StandardSaberAttackIndex);
}

bool UGA_Melee::StartStandardSaberStage(const int32 AttackIndex)
{
	AActor* Attacker = StandardSaberActor.Get();
	UMSMeleeWeaponData* Weapon = StandardSaberWeapon.Get();
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!Attacker || !World || !Weapon)
	{
		ResetStandardSaberCombo(true, true);
		return false;
	}
	if (AttackIndex == 1 || (AttackIndex > FMSStandardSaberMotion::DualFirst && AttackIndex <= FMSStandardSaberMotion::DualLast))
	{
		// 二段目は既に許可されたバーストの追加弾だけを消費し、初段の
		// FireCooldownRemaining(.7s)を再判定しない。入力バッファ時点では
		// まだ消費せず、実際に二段目へ遷移する境界で一度だけ呼ぶ。
		if (!StandardSaberWeapons.IsValid()
			|| !StandardSaberWeapons->ConsumeAmmoDuringBurst(StandardSaberSlotIndex))
		{
			ResetStandardSaberCombo(true, true);
			return false;
		}
	}

	StandardSaberAttackIndex = AttackIndex;
	StandardSaberStartWorldTime = World->GetTimeSeconds();
	StandardSaberLastProcessedSeconds = 0.0f;
	StandardSaberComboDeadlineWorldTime = -1.0f;
	bStandardSaberStageActive = true;
	bStandardSaberPendingFollowUp = false;
	StandardSaberHitActors.Reset();
	LastStandardSaberAttackIndex = AttackIndex;
	LastHitCount = 0;
	LastSuccessfulHitCount = 0;
	if (UAbilitySystemComponent* ASC = StandardSaberASC.Get())
	{
		// 標準モーションのDuration中はCMCの通常移動／ブーストを止める。
		// 段の終了時にEndRecoveryで解除し、旧格闘の0.45秒を重ねない。
		ASC->SetLooseGameplayTagCount(MSTags::State_MeleeRecovery, 1,
			EGameplayTagReplicationState::TagOnly);
		if (bStandardSaberDash)
		{
			ASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 1,
				EGameplayTagReplicationState::TagOnly);
		}
		RecoveryASC = ASC;
	}

	if (UAbilitySystemComponent* ASC = StandardSaberASC.Get())
	{
		FGameplayEffectContextHandle FireContext = ASC->MakeEffectContext();
		FireContext.AddInstigator(Attacker, Attacker);
		FireContext.AddSourceObject(Weapon);
		FGameplayCueParameters FireParameters(FireContext);
		FireParameters.Location = StandardSaberFireOrigin;
		FireParameters.SourceObject = Weapon;
		ASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);
	}

	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
		{
			Animator->StartStandardSaberMotion(AttackIndex);
		}
	}

	World->GetTimerManager().SetTimer(StandardSaberTimer, this, &UGA_Melee::StandardSaberTick,
		1.0f / 120.0f, /*bLoop=*/true, 1.0f / 120.0f);
	return true;
}

void UGA_Melee::CancelMelee()
{
	// The standard saber owns its own stage timer and embeds D-stage lunge in
	// the sampled motion.  It never creates a legacy LungeTimer; any stale
	// legacy lunge is cleaned independently below for switch/cancel safety.
	if (StandardSaberActor.IsValid())
	{
		ResetStandardSaberCombo(true, true);
	}

	// Legacy D-melee has a separate timer and procedural/authored motion path.
	// Cancel it only while that lunge is still alive; an ordinary legacy swing
	// keeps its existing recovery semantics.
	AActor* Attacker = LungeActor.Get();
	if (!Attacker)
	{
		// Releasing fire after the lunge must not skip the D recovery. Death,
		// however, cancels the action even during its post-lunge recovery tail.
		if (UAbilitySystemComponent* ASC = RecoveryASC.Get();
			ASC && ASC->HasMatchingGameplayTag(MSTags::State_DashMeleeRegenBlock)
			&& ASC->HasMatchingGameplayTag(MSTags::State_Dead))
		{
			if (UWorld* World = ASC->GetWorld()) World->GetTimerManager().ClearTimer(RecoveryTimer);
			EndRecovery();
		}
		return;
	}

	Attacker->GetWorldTimerManager().ClearTimer(LungeTimer);
	Attacker->GetWorldTimerManager().ClearTimer(RecoveryTimer);
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (UMSMechRigComponent* Rig = Unit->GetMechRigComponent())
		{
			Rig->StopMotion(false);
		}
	}
	LungeActor.Reset();
	LungeDirection = FVector::ForwardVector;
	LungeElapsedSeconds = 0.0f;
	EndRecovery();
}

bool UGA_Melee::IsStandardSaberInterrupted() const
{
	AActor* Attacker = StandardSaberActor.Get();
	UAbilitySystemComponent* ASC = StandardSaberASC.Get();
	UMSWeaponComponent* Weapons = StandardSaberWeapons.Get();
	if (!Attacker || !Attacker->HasAuthority() || !ASC || !Weapons)
	{
		return true;
	}
	FGameplayTagContainer InterruptedTags;
	InterruptedTags.AddTag(MSTags::State_Dead);
	InterruptedTags.AddTag(MSTags::State_Flinched);
	InterruptedTags.AddTag(MSTags::State_Staggered);
	InterruptedTags.AddTag(MSTags::State_KnockedDown);
	InterruptedTags.AddTag(MSTags::State_GettingUp);
	InterruptedTags.AddTag(MSTags::State_EmergencyRecall);
	if (ASC->HasAnyMatchingGameplayTags(InterruptedTags))
	{
		return true;
	}
	if (Weapons->GetActiveSlotIndex() != StandardSaberSlotIndex || Weapons->IsSwitching())
	{
		return true;
	}
	const UMSMeleeWeaponData* CurrentWeapon =
		Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(StandardSaberSlotIndex));
	return CurrentWeapon != StandardSaberWeapon.Get() || !CurrentWeapon
		|| !CurrentWeapon->UsesTimedSaberMotion();
}

void UGA_Melee::StandardSaberTick()
{
	AActor* Attacker = StandardSaberActor.Get();
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!Attacker || !World)
	{
		ResetStandardSaberCombo(false, true);
		return;
	}
	if (IsStandardSaberInterrupted())
	{
		ResetStandardSaberCombo(true, true);
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (!bStandardSaberStageActive)
	{
		if (Now > StandardSaberComboDeadlineWorldTime + KINDA_SMALL_NUMBER)
		{
			ResetStandardSaberCombo(false, false);
		}
		return;
	}

	const float TargetElapsed = FMath::Max(0.0f, Now - StandardSaberStartWorldTime);
	const float TimingDuration = GetStandardSaberStageDuration();
	const float ProcessUntil = FMath::Min(TargetElapsed, TimingDuration);
	while (StandardSaberLastProcessedSeconds < ProcessUntil - KINDA_SMALL_NUMBER)
	{
		const float StepEnd = FMath::Min(ProcessUntil,
			StandardSaberLastProcessedSeconds + (1.0f / 120.0f));
		ProcessStandardSaberInterval(StandardSaberLastProcessedSeconds, StepEnd);
		StandardSaberLastProcessedSeconds = StepEnd;
		if (!StandardSaberActor.IsValid() || !bStandardSaberStageActive)
		{
			return;
		}
	}

	if (TargetElapsed >= TimingDuration - KINDA_SMALL_NUMBER)
	{
		FinishStandardSaberStage();
	}
}

float UGA_Melee::GetStandardSaberStageDuration() const
{
 const auto Timing = FMSStandardSaberMotion::GetTiming(StandardSaberAttackIndex);
 const auto* Unit = Cast<AMSUnitBase>(StandardSaberActor.Get());
 const float Scale = LastSuccessfulHitCount > 0 && Unit ? Unit->GetBattleAssistReductionScale(EMSBattleAssistEffectKind::MeleeHitRecoveryReduction) : 1.f;
 // Preserve all active blade/lunge frames; shorten only the post-hit recovery tail, once per stage.
 const float RecoveryStart = FMath::Max(Timing.ActiveEnd, Timing.LungeEnd);
 return RecoveryStart + FMath::Max(0.f, Timing.Duration - RecoveryStart) * Scale;
}

void UGA_Melee::ProcessStandardSaberInterval(const float FromSeconds, const float ToSeconds)
{
	AActor* Attacker = StandardSaberActor.Get();
	if (!Attacker || ToSeconds <= FromSeconds)
	{
		return;
	}

	const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(StandardSaberAttackIndex);
	if (Timing.LungeEnd > Timing.LungeStart && Timing.LungeDistanceCm > 0.0f)
	{
		const float LungeFrom = FMath::Clamp((FromSeconds - Timing.LungeStart)
			/ (Timing.LungeEnd - Timing.LungeStart), 0.0f, 1.0f);
		const float LungeTo = FMath::Clamp((ToSeconds - Timing.LungeStart)
			/ (Timing.LungeEnd - Timing.LungeStart), 0.0f, 1.0f);
		const float DistanceCm = (LungeTo - LungeFrom) * Timing.LungeDistanceCm * MSGO::MSScale;
		if (DistanceCm > 0.0f)
		{
			// 現在位置から積分するため、Timerのフレーム間隔に依存しない。
			Attacker->AddActorWorldOffset(StandardSaberDirection * DistanceCm, /*bSweep=*/true);
		}
	}

	const float ActiveFrom = FMath::Max(FromSeconds, Timing.ActiveStart);
	const float ActiveTo = FMath::Min(ToSeconds, Timing.ActiveEnd);
	if (ActiveTo < ActiveFrom)
	{
		return;
	}

	const bool bDual = FMSStandardSaberMotion::IsDual(StandardSaberAttackIndex);
	for (int32 Hand = 0; Hand < (bDual ? 2 : 1); ++Hand)
	{
		const bool bLeft = Hand == 1;
		if (bDual && !FMSDualSaberMotion::IsHandActive(StandardSaberAttackIndex, bLeft)) continue;
		FVector Start0, End0, Start1, End1;
		SampleStandardSaberBlade(ActiveFrom, Start0, End0, bLeft);
		SampleStandardSaberBlade(ActiveTo, Start1, End1, bLeft);
		ApplyStandardSaberBladeSegment(Start0, End0);
		if (!Start0.Equals(Start1, KINDA_SMALL_NUMBER) || !End0.Equals(End1, KINDA_SMALL_NUMBER))
		{
			ApplyStandardSaberBladeSegment(Start1, End1);
			if (bDual)
			{
				// Cover travel between the two actual blade samples. Adjacent longitudinal
				// tracks are <= one collision diameter apart; both hands share the stage set.
				const int32 Tracks = FMath::Max(1, FMath::CeilToInt(FMath::Max(
					FVector::Distance(Start0, End0), FVector::Distance(Start1, End1)) / (20.0f * MSGO::MSScale)));
				for (int32 Track = 0; Track <= Tracks; ++Track)
				{
					const float Alpha = static_cast<float>(Track) / Tracks;
					ApplyStandardSaberBladeSegment(FMath::Lerp(Start0, End0, Alpha), FMath::Lerp(Start1, End1, Alpha));
				}
			}
		}
	}
}

void UGA_Melee::SampleStandardSaberBlade(const float TimeSeconds, FVector& OutStart, FVector& OutEnd, const bool bLeft) const
{
	AActor* Attacker = StandardSaberActor.Get();
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		const UMSMeshVisualComponent* MeshVisual = Unit->GetMeshVisualComponent();
		const UMSWeaponVisualComponent* WeaponVisual = Unit->GetWeaponVisualComponent();
		const UStaticMeshComponent* Held = WeaponVisual ? (bLeft ? WeaponVisual->GetImportedLeftHeldMeshComponent()
			: WeaponVisual->GetImportedHeldMeshComponent()) : nullptr;
		FTransform Mount;
		if (MeshVisual && Held && Held->IsVisible() && Held->GetStaticMesh()
			&& Held->GetAttachParent() == MeshVisual->GetPoseableMesh()
			&& Held->GetAttachSocketName() == (bLeft ? TEXT("weapon_mount_l") : TEXT("weapon_mount_r"))
			&& MeshVisual->SampleStandardSaberMountWorld(StandardSaberAttackIndex, TimeSeconds, Mount, bLeft))
		{
			// Imported saber is authored along +Z. Absolute weapon scale intentionally
			// excludes the FBX skeleton's 100x root scale, just as its real attachment does.
			const FTransform WeaponTransform(Mount.GetRotation() * Held->GetRelativeRotation().Quaternion(),
				Mount.TransformPosition(Held->GetRelativeLocation()), Held->GetComponentScale());
			OutStart = WeaponTransform.GetLocation();
			OutEnd = WeaponTransform.TransformPosition(FVector(0, 0, Held->GetStaticMesh()->GetBoundingBox().Max.Z));
			return;
		}
	}
	FMSStandardSaberMotion::SampleBlade(StandardSaberAttackIndex, TimeSeconds, 100.0f, OutStart, OutEnd, bLeft);
	OutStart = TransformStandardSaberPoint(Attacker, OutStart);
	OutEnd = TransformStandardSaberPoint(Attacker, OutEnd);
}

FVector UGA_Melee::TransformStandardSaberPoint(AActor* Attacker, const FVector& RigPoint) const
{
	if (!Attacker)
	{
		return FVector::ZeroVector;
	}

	FVector WorldPoint = FVector::ZeroVector;
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (const UMSMechRigComponent* Rig = Unit->GetMechRigComponent())
		{
			if (const USceneComponent* RigRoot = Rig->GetRigRoot())
			{
				WorldPoint = RigRoot->GetComponentTransform().TransformPosition(RigPoint);
			}
			else
			{
				FTransform ActorTransform = Attacker->GetActorTransform();
				ActorTransform.SetScale3D(ActorTransform.GetScale3D() * MSGO::MSScale);
				WorldPoint = ActorTransform.TransformPosition(RigPoint);
			}
		}
		else
		{
			FTransform ActorTransform = Attacker->GetActorTransform();
			ActorTransform.SetScale3D(ActorTransform.GetScale3D() * MSGO::MSScale);
			WorldPoint = ActorTransform.TransformPosition(RigPoint);
		}
	}
	else
	{
		FTransform ActorTransform = Attacker->GetActorTransform();
		ActorTransform.SetScale3D(ActorTransform.GetScale3D() * MSGO::MSScale);
		WorldPoint = ActorTransform.TransformPosition(RigPoint);
	}

	return WorldPoint;
}

void UGA_Melee::ApplyStandardSaberBladeSegment(const FVector& SegmentStart, const FVector& SegmentEnd)
{
	AActor* Attacker = StandardSaberActor.Get();
	UMSMeleeWeaponData* Weapon = StandardSaberWeapon.Get();
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	UAbilitySystemComponent* SourceASC = StandardSaberASC.Get();
	if (!Attacker || !Weapon || !World || !SourceASC || IsStandardSaberInterrupted())
	{
		return;
	}

	const float RadiusCm = 12.0f * MSGO::MSScale;
	const FVector Segment = SegmentEnd - SegmentStart;
	const float SegmentLength = Segment.Size();
	const FVector SafeDirection = Segment.GetSafeNormal();
	const FVector Center = (SegmentStart + SegmentEnd) * 0.5f;
	const float HalfHeight = FMath::Max(RadiusCm, SegmentLength * 0.5f + RadiusCm);
	const FQuat Rotation = SafeDirection.IsNearlyZero()
		? FQuat::Identity
		: FQuat::FindBetweenNormals(FVector::UpVector, SafeDirection);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSStandardSaberOverlap), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Attacker);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Center, Rotation,
		MSCollision::WeaponTraceChannel,
		FCollisionShape::MakeCapsule(RadiusCm, HalfHeight), QueryParams);

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Attacker, Attacker);
	Context.AddSourceObject(Weapon);
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
	{
		MSContext->SetAttackerLocation(Attacker->GetActorLocation());
		MSContext->SetForceKnockdown(bStandardSaberDash);
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target || StandardSaberHitActors.Contains(Target))
		{
			continue;
		}
		StandardSaberHitActors.Add(Target);

		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!TargetASC)
		{
			continue;
		}

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, FMath::Max(0.0f, StandardSaberDamage));
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, FMath::Max(0.0f, StandardSaberDownPoint));
		SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);

		FGameplayCueParameters ImpactParameters(Context);
		ImpactParameters.Instigator = Attacker;
		ImpactParameters.Location = Target->GetActorLocation();
		ImpactParameters.SourceObject = Weapon;
		SourceASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, ImpactParameters);
		if (UGameInstance* GameInstance = Attacker->GetGameInstance())
		{
			if (UMSHitFeedbackSubsystem* HitFeedback = GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
			{
				HitFeedback->BroadcastHitConfirmed(Attacker, false);
			}
		}
		++LastHitCount;
		if (UMSTeamStatics::CanDamage(Attacker, Target)) ++LastSuccessfulHitCount;
	}
}

void UGA_Melee::FinishStandardSaberStage()
{
	AActor* Attacker = StandardSaberActor.Get();
	if (!Attacker)
	{
		ResetStandardSaberCombo(false, true);
		return;
	}

	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
	{
		if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
		{
			Animator->StopStandardSaberMotion();
		}
	}

	bStandardSaberStageActive = false;
	const float Now = Attacker->GetWorld() ? Attacker->GetWorld()->GetTimeSeconds() : 0.0f;
	if (FMSStandardSaberMotion::HasFollowUp(StandardSaberAttackIndex))
	{
		if (bStandardSaberPendingFollowUp)
		{
			// バッファ済みならタグを切らずに、そのまま二段目へ接続する。
			StartStandardSaberStage(StandardSaberAttackIndex + 1);
			return;
		}
		EndRecovery();
		StandardSaberComboDeadlineWorldTime = Now + 0.30f;
		return;
	}

	// D格／2段目は単発で終了し、タイマーと段状態を解放する。
	EndRecovery();
	ResetStandardSaberCombo(false, false);
}

void UGA_Melee::ClearStandardSaberRecovery()
{
	if (AActor* Attacker = StandardSaberActor.Get())
	{
		Attacker->GetWorldTimerManager().ClearTimer(RecoveryTimer);
	}
	EndRecovery();
}

void UGA_Melee::ResetStandardSaberCombo(const bool bStopMotion, const bool bClearRecovery)
{
	AActor* Attacker = StandardSaberActor.Get();
	if (bStopMotion)
	{
		if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Attacker))
		{
			if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
			{
				Animator->StopStandardSaberMotion();
			}
		}
	}
	if (Attacker)
	{
		Attacker->GetWorldTimerManager().ClearTimer(StandardSaberTimer);
		if (bClearRecovery)
		{
			Attacker->GetWorldTimerManager().ClearTimer(RecoveryTimer);
		}
	}
	if (bClearRecovery)
	{
		EndRecovery();
	}
	StandardSaberActor.Reset();
	StandardSaberWeapons.Reset();
	StandardSaberASC.Reset();
	StandardSaberWeapon.Reset();
	StandardSaberSlotIndex = INDEX_NONE;
	StandardSaberAttackIndex = INDEX_NONE;
	StandardSaberStartWorldTime = 0.0f;
	StandardSaberLastProcessedSeconds = 0.0f;
	StandardSaberComboDeadlineWorldTime = -1.0f;
	bStandardSaberStageActive = false;
	bStandardSaberPendingFollowUp = false;
	bStandardSaberDash = false;
	StandardSaberDamage = 0.0f;
	StandardSaberDownPoint = 0.0f;
	StandardSaberHitActors.Reset();
	LastHitCount = 0;
	LastSuccessfulHitCount = 0;
	FinishActiveAbilityAfterMelee();
}

int32 UGA_Melee::ApplyMeleeHits(AActor* Attacker, const FVector& TraceStart, const FVector& TraceDirection,
	const float ReachCm, const float RadiusCm,
	const float Damage, const float DownPointAmount, const UMSMeleeWeaponData* Weapon,
	const bool bForceKnockdown, int32& OutSuccessfulHitCount) const
{
	OutSuccessfulHitCount = 0;
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!World)
	{
		return 0;
	}

	// WeaponTraceチャンネルのSphereOverlap(B-3。爆風P2-4と同じ収集方式)
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSMeleeOverlap), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Attacker);

	// Real melee data has 5-6.5 m reach. A sphere placed only at that endpoint leaves a
	// blind zone and misses a target at the bot's 2.4 m stop distance. Cover the complete swing.
	const FVector SafeDirection = TraceDirection.GetSafeNormal();
	const float HalfHeight = FMath::Max(RadiusCm, ReachCm * 0.5f + RadiusCm);
	const FVector Center = TraceStart + SafeDirection * ReachCm * 0.5f;
	const FQuat CapsuleRotation = FQuat::FindBetweenNormals(FVector::UpVector, SafeDirection);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Center, CapsuleRotation,
		MSCollision::WeaponTraceChannel, FCollisionShape::MakeCapsule(RadiusCm, HalfHeight), QueryParams);

	// 共通Context(GA_Fire_Hitscan::ResolveAndApplyDamageと同構成。AttackerLocation=攻撃者位置
	// で盾正面判定§1.6手順4が機能する)。FF判定はUMSDamageExecution手順1が単一実装(B-2)。
	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);
	FGameplayEffectContextHandle Context = SourceASC
		? SourceASC->MakeEffectContext()
		: FGameplayEffectContextHandle(new FMSGameplayEffectContext());
	Context.AddInstigator(Attacker, Attacker);
	Context.AddSourceObject(Weapon);
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Context))
	{
		MSContext->SetAttackerLocation(Attacker->GetActorLocation());
		MSContext->SetForceKnockdown(bForceKnockdown);
	}

	// アクター単位へ集約(カプセル+HeadHitboxの二重適用防止。Unity: HashSet<Health>)
	TSet<AActor*> AppliedActors;
	int32 AppliedCount = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target || AppliedActors.Contains(Target))
		{
			continue;
		}
		AppliedActors.Add(Target);

		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!TargetASC)
		{
			continue; // 壁/建屋など非ダメージ対象
		}

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, FMath::Max(0.0f, Damage));
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, FMath::Max(0.0f, DownPointAmount));
		if (SourceASC)
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
		}
		else
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
		}
		if (SourceASC)
		{
			FGameplayCueParameters ImpactParameters(Context);
			ImpactParameters.Instigator = Attacker;
			ImpactParameters.Location = Target->GetActorLocation();
			ImpactParameters.SourceObject = Weapon;
			SourceASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, ImpactParameters);
		}
		// 実プレイ報告バグ修正: 格闘武器だけHitFeedbackSubsystemへの通知が抜けており、
		// HUDヒットマーカーが出なかった(射撃/ホーミングと同じ通知を追加)。
		if (UGameInstance* GameInstance = Attacker->GetGameInstance())
		{
			if (UMSHitFeedbackSubsystem* HitFeedback = GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>())
			{
				HitFeedback->BroadcastHitConfirmed(Attacker, false);
			}
		}
		++AppliedCount;
		if (UMSTeamStatics::CanDamage(Attacker, Target)) ++OutSuccessfulHitCount;
	}

	return AppliedCount;
}

void UGA_Melee::StartLunge(AActor* Attacker, const FVector& Direction)
{
	if (!Attacker)
	{
		return;
	}

	LungeActor = Attacker;
	LungeDirection = Direction;
	LungeElapsedSeconds = 0.0f;
	Attacker->GetWorldTimerManager().SetTimer(LungeTimer, this, &UGA_Melee::LungeTick,
		LungeTickIntervalSeconds, /*bLoop=*/true, LungeTickIntervalSeconds);
}

void UGA_Melee::LungeTick()
{
	AActor* Attacker = LungeActor.Get();
	if (!Attacker)
	{
		return; // ワールドごとタイマーが破棄されるため明示クリア不要
	}

	// 最終刻みは残時間分だけ移動し、合計移動量を 26m/s × 0.35s = 9.1m に一致させる
	const float StepSeconds = FMath::Min(LungeTickIntervalSeconds,
		DashMeleeLungeDurationSeconds - LungeElapsedSeconds);
	if (StepSeconds > 0.0f)
	{
		Attacker->AddActorWorldOffset(LungeDirection * DashMeleeLungeSpeedCms * StepSeconds, /*bSweep=*/true);
		LungeElapsedSeconds += StepSeconds;
	}

	if (LungeElapsedSeconds >= DashMeleeLungeDurationSeconds - KINDA_SMALL_NUMBER)
	{
		Attacker->GetWorldTimerManager().ClearTimer(LungeTimer);
		// A later ordinary swing owns its own recovery.  Do not let a completed
		// dash leave an actor handle which CancelMelee mistakes for a live lunge.
		LungeActor.Reset();
		LungeDirection = FVector::ForwardVector;
		LungeElapsedSeconds = 0.0f;
		FinishActiveAbilityAfterMelee();
	}
}

void UGA_Melee::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (bEndingMeleeAbility) return;
	TGuardValue<bool> EndingGuard(bEndingMeleeAbility, true);
	// An external GAS cancellation also clears this ability's timers. Release
	// their movement-blocking tag and sampled blade/lunge state first.
	CancelMelee();
	if (RecoveryASC.IsValid())
	{
		if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(RecoveryTimer);
		EndRecovery();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Melee::StartRecovery(AActor* Attacker, UAbilitySystemComponent* ASC, const float Seconds)
{
	if (!Attacker || !ASC)
	{
		return;
	}

	// State.MeleeRecovery: 次の攻撃はPerformMelee冒頭で、移動キャンセルは
	// UMSCharacterMovementComponent::IsIncapacitated が本タグでブロックする
	ASC->SetLooseGameplayTagCount(MSTags::State_MeleeRecovery, 1,
		EGameplayTagReplicationState::TagOnly);
	if (bLastAttackWasDash)
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 1,
			EGameplayTagReplicationState::TagOnly);
	}
	RecoveryASC = ASC;
	if (Seconds <= KINDA_SMALL_NUMBER)
	{
		// TimerManager treats non-positive durations as ClearTimer. Never leave
		// the movement-blocking tag set without a callback to release it.
		EndRecovery();
		return;
	}
	Attacker->GetWorldTimerManager().SetTimer(RecoveryTimer, this, &UGA_Melee::EndRecovery,
		Seconds, /*bLoop=*/false);
}

void UGA_Melee::EndRecovery()
{
	if (UAbilitySystemComponent* ASC = RecoveryASC.Get())
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_MeleeRecovery, 0,
			EGameplayTagReplicationState::TagOnly);
		ASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 0,
			EGameplayTagReplicationState::TagOnly);
	}
	RecoveryASC.Reset();
	FinishActiveAbilityAfterMelee();
}

void UGA_Melee::FinishActiveAbilityAfterMelee()
{
	// Direct player callers own this object without a GAS activation. For GAS
	// callers, finish only after both the legacy lunge and any timed combo have
	// ended; ending early would clear their outstanding TimerManager callbacks.
	if (bEndingMeleeAbility || !IsActive() || RecoveryASC.IsValid() || LungeActor.IsValid()
		|| StandardSaberActor.IsValid())
	{
		return;
	}
	if (const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(),
			/*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
	}
}
