// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSMechAnimatorComponent.h"
#include "Combat/MSHitReactionComponent.h"

#include "AbilitySystemComponent.h"
#include "Animation/MSAnimInstance.h"
#include "Animation/MSMobHandVisuals.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSUnitBase.h"
#include "Components/AudioComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/Texture2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/MSCameraComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/MSWeaponComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#endif

namespace
{
	// ブースト噴射ループの基準Frequency。MS_ThrusterHum既定60HzのSine単体はスピーカーで
	// ほぼ聞こえないため、可聴域下部の110Hzへ引き上げる(データ精度方針: 質感より体感優先)。
	constexpr float ThrusterLoopFrequencyHz = 110.0f;

	// チャージ溜め音(Unity: CreateChargeHum 基準320Hz、pitch Lerp(0.8,1.8,progress))
	constexpr float ChargeHumBaseFrequencyHz = 320.0f;

	// チャージ完了チャイム(MS_TwoTone流用。HS確認1500Hzと区別する)
	constexpr float ChargeCompleteFrequencyHz = 1800.0f;

	constexpr float FootstepFrequencyHz = 55.0f;

	// Imported stereo thruster audio has strong side energy. Keep a short audible onset,
	// but below weapon/impact levels, then settle into a quiet positional bed.
	constexpr float BoostLoopFadeDurationSeconds = 0.45f;
	constexpr float BoostLoopInitialVolume = 0.32f;
	constexpr float BoostLoopSustainVolume = 0.18f;

	// Keep the elbow below and to the side of the trigger datum. Sideways roll alone
	// left the thick MobLight cuff ABOVE the grip, intersecting BC4's rear receiver
	// in the live top view. Bend the elbow under the stock; the mount's inverse arm
	// quaternion below keeps the gun/hand registered and the barrel level.
	constexpr float MobLightBmgAimUpperUnityAngle = -50.0f;
	constexpr float MobLightTriggerRestForearmUnityAngle = -100.0f;
	constexpr float MobLightBmgAimForearmUnityAngle = -85.0f;
	constexpr float MobLightBmgUpperRoll = 15.0f;
	constexpr float MobLightBmgForearmRoll = -30.0f;
}

UMSMechAnimatorComponent::UMSMechAnimatorComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeMaterialFinder(
		TEXT("/Game/Textures/VFX/M_ThrusterSmoke.M_ThrusterSmoke"));
	ThrusterSmokeMaterial = SmokeMaterialFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AuraMaterialFinder(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	ModuleAuraMaterial = AuraMaterialFinder.Object;
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture0(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_00.T_ThrusterSmoke_00"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture1(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_01.T_ThrusterSmoke_01"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture2(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_02.T_ThrusterSmoke_02"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> SmokeTexture3(
		TEXT("/Game/Textures/VFX/T_ThrusterSmoke_03.T_ThrusterSmoke_03"));
	ThrusterSmokeTextures = {
		SmokeTexture0.Object, SmokeTexture1.Object, SmokeTexture2.Object, SmokeTexture3.Object};

	// P9追補: 実プレイ報告「ブースト音・足音・チャージ音がない」対応。
	// スケルタルメッシュ非使用(Cubeリグ)のため、AnimInstance経路では音が鳴らない。
	// 毎フレームPoseInputsを収集する本コンポーネントがユニット音の駆動点になる。
	static ConstructorHelpers::FObjectFinder<USoundBase> ThrusterFinder(
		TEXT("/Game/Audio/Imported/SFX_Thruster_Loop.SFX_Thruster_Loop"));
	ThrusterLoopSound = ThrusterFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> FootstepFinder(
		TEXT("/Game/Audio/Imported/SFX_Footstep_Metal.SFX_Footstep_Metal"));
	FootstepSound = FootstepFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ChargeHumFinder(
		TEXT("/Game/Audio/Imported/SFX_Charge_Loop.SFX_Charge_Loop"));
	ChargeLoopSound = ChargeHumFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ChargeCompleteFinder(
		TEXT("/Game/Audio/Imported/SFX_Charge_Complete.SFX_Charge_Complete"));
	ChargeCompleteSound = ChargeCompleteFinder.Object;
}

void UMSMechAnimatorComponent::BeginPlay()
{
	Super::BeginPlay();
	Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	BuildThrusterVisuals();
}

void UMSMechAnimatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetModuleBodyAura(false, FLinearColor::Transparent);
	StopAllLoopingAudio();
	Super::EndPlay(EndPlayReason);
}

void UMSMechAnimatorComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Rig)
	{
		Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	}
	GatherInputs();
	CancelAuthoredMotionIfDisabled();
	const float PreviousWalkPhase = PoseState.WalkPhaseAngle;
	PoseSettings.LimbMoveSpeedDegPerSec = LimbMoveSpeedDegPerSec;
	FMSProceduralPoseSolver::Update(PoseInputs, PoseSettings, PoseState, DeltaTime);
	ApplyPoseToRig(DeltaTime);
	SetThrusterVisible(PoseInputs.bIsBoosting);
	if (!bUsesMeshThrusterBones)
	{
		UpdateThrusterPointTransforms();
		UpdateThrusterSmoke(DeltaTime);
	}
	UpdateBoostAfterimage(DeltaTime);
	UpdateAudioFeedback(DeltaTime, PreviousWalkPhase);
}

void UMSMechAnimatorComponent::GatherInputs()
{
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	PoseInputs = FMSProceduralPoseInputs();
	if (!Unit)
	{
		return;
	}
	PoseInputs.GroundSpeed = Unit->GetVelocity().Size2D();
	// Only pose data is changed. Actor/capsule heading and replicated movement remain authoritative.
	PoseInputs.bHasDirectionalFacing = true;
	PoseInputs.bHasAuthoredMotion = (Rig && Rig->IsMotionPlaying()) || StandardSaberAttackIndex != INDEX_NONE;
	const float ActorYaw = Unit->GetActorRotation().Yaw;
	PoseInputs.MovementYaw = FMath::FindDeltaAngleDegrees(ActorYaw, Unit->GetVelocity().Rotation().Yaw);
	PoseInputs.AimYaw = FMath::FindDeltaAngleDegrees(ActorYaw, Unit->GetBaseAimRotation().Yaw);
	PoseInputs.bIsInAir = Unit->GetCharacterMovement() && Unit->GetCharacterMovement()->IsFalling();
	const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	auto Has = [ASC](const FGameplayTag& Tag) { return ASC && ASC->HasMatchingGameplayTag(Tag); };
	PoseInputs.bIsBoosting = Has(MSTags::State_Boosting);
	PoseInputs.bIsCrouching = Has(MSTags::State_Crouching);
	// Replicated tag updates can trail the shield break. The broken remnant must let
	// the forearm return to its ordinary pose even during that stale-tag frame.
	const UMSShieldComponent* Shield = Unit->GetShieldComponent();
	PoseInputs.bIsGuarding = Shield && Shield->IsShieldVisualActive()
		&& Has(MSTags::State_Guarding);
	PoseInputs.bIsStaggered = Has(MSTags::State_Staggered);
	const bool bProneReaction=Unit->GetHitReactionComponent() && Unit->GetHitReactionComponent()->IsProneReaction();
	PoseInputs.bIsStaggered |= Has(MSTags::State_GettingUp) && !bProneReaction;
	PoseInputs.bIsFlinched = Has(MSTags::State_Flinched);
	PoseInputs.bManagedHitReaction = Unit->GetHitReactionComponent()!=nullptr;
	if (const auto* Reaction=Unit->GetHitReactionComponent(); Reaction && Reaction->GetPhase()==EMSHitReactionPhase::GettingUp)
	{
		PoseInputs.HitReactionPoseWeight=1.f-FMath::Clamp(Reaction->GetPhaseElapsed()/UMSHitReactionComponent::GetUpSeconds,0.f,1.f);
		if (bProneReaction)
			PoseInputs.GetUpSupportAlpha=FMath::Sin(PI*(1.f-PoseInputs.HitReactionPoseWeight));
	}
	PoseInputs.bIsKnockedDown = Has(MSTags::State_KnockedDown) || (Has(MSTags::State_GettingUp) && bProneReaction);
	PoseInputs.bIsDead = Has(MSTags::State_Dead);
	if (const UMSWeaponComponent* Weapon = Unit->GetWeaponComponent())
	{
		PoseInputs.bIsSwitchingWeapon = Weapon->IsSwitching();
		PoseInputs.WeaponSwitchProgress = PoseInputs.bIsSwitchingWeapon ? Weapon->GetSwitchProgress01() : 0.0f;
		const UMSWeaponDataBase* Data = Weapon->GetWeaponData(Weapon->GetActiveSlotIndex());
		PoseInputs.bHasRangedWeapon = Data && !Data->IsA<UMSMeleeWeaponData>() && !Data->IsA<UMSShieldWeaponData>();

		// 構え(照準)判定: 装備しているだけでは構えない(実プレイ報告: 常時bIsAiming=trueの回帰修正)。
		// 「発射直後(クールダウン中)/チャージ中/スコープ中」を照準刺激とし、
		// 刺激が消えてからAimHoldSecondsは構えを保持する(連射間のばたつき防止)。
		if (!CameraComponent.IsValid() && GetOwner())
		{
			CameraComponent = GetOwner()->FindComponentByClass<UMSCameraComponent>();
		}
		const bool bAimStimulus = Weapon->GetFireCooldownRemaining() > 0.0f
			|| Has(MSTags::State_Charging)
			|| (CameraComponent.IsValid() && CameraComponent->IsScoped());
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (bAimStimulus)
		{
			LastAimStimulusTime = Now;
		}
		PoseInputs.bIsAiming = PoseInputs.bHasRangedWeapon
			&& (bAimStimulus || (Now - LastAimStimulusTime) < AimHoldSeconds)
			&& !PoseInputs.bIsFlinched && !PoseInputs.bIsStaggered && !PoseInputs.bIsKnockedDown && !PoseInputs.bIsDead;
	}
}

void UMSMechAnimatorComponent::EvaluatePose(const FMSProceduralPoseInputs& Inputs, const float DeltaSeconds)
{
	if (!Rig)
	{
		Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
		BuildThrusterVisuals();
	}
	PoseInputs = Inputs;
	PoseInputs.bHasAuthoredMotion |= Rig && Rig->IsMotionPlaying();
	CancelAuthoredMotionIfDisabled();
	const float PreviousWalkPhase = PoseState.WalkPhaseAngle;
	PoseSettings.LimbMoveSpeedDegPerSec = LimbMoveSpeedDegPerSec;
	FMSProceduralPoseSolver::Update(PoseInputs, PoseSettings, PoseState, DeltaSeconds);
	ApplyPoseToRig(DeltaSeconds);
	SetThrusterVisible(PoseInputs.bIsBoosting);
	UpdateThrusterSmoke(DeltaSeconds);
	UpdateBoostAfterimage(DeltaSeconds);
	UpdateAudioFeedback(DeltaSeconds, PreviousWalkPhase);
}

void UMSMechAnimatorComponent::UpdateAudioFeedback(const float DeltaSeconds, const float PreviousWalkPhase)
{
	UpdateBoostLoopAudio(DeltaSeconds);
	UpdateFootstepAudio(DeltaSeconds, PreviousWalkPhase);
	UpdateChargeAudio();
}

void UMSMechAnimatorComponent::UpdateBoostLoopAudio(const float DeltaSeconds)
{
	// State.Boosting(CMC/AI LooseTag)と同期してブースト噴射ループを開始/停止する。
	// 死亡中は噴射音を止める(死亡演出はしゃがみ→爆発)。
	const bool bShouldPlay = PoseInputs.bIsBoosting && !PoseInputs.bIsDead;
	if (bShouldPlay == bBoostLoopAudioActive)
	{
		if (bShouldPlay)
		{
			BoostLoopAudioElapsedSeconds = FMath::Min(BoostLoopFadeDurationSeconds,
				BoostLoopAudioElapsedSeconds + FMath::Max(DeltaSeconds, 0.0f));
			const float FadeAlpha = FMath::Clamp(
				BoostLoopAudioElapsedSeconds / BoostLoopFadeDurationSeconds, 0.0f, 1.0f);
			const float SmoothFadeAlpha = FMath::SmoothStep(0.0f, 1.0f, FadeAlpha);
			BoostLoopAudioVolumeMultiplier = FMath::Lerp(
				BoostLoopInitialVolume, BoostLoopSustainVolume, SmoothFadeAlpha);
			if (BoostLoopAudio)
			{
				BoostLoopAudio->SetVolumeMultiplier(BoostLoopAudioVolumeMultiplier);
			}
		}
		return;
	}
	if (bShouldPlay)
	{
		BoostLoopAudioElapsedSeconds = 0.0f;
		BoostLoopAudioVolumeMultiplier = BoostLoopInitialVolume;
		USceneComponent* AttachTo = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
		BoostLoopAudio = MSAudio::SpawnLoopingMetaSoundAttached(
			AttachTo, MSAudioPreset::Resolve(EMSAudioUsage::BoostLoop, ThrusterLoopSound),
			ThrusterLoopFrequencyHz, BoostLoopAudioVolumeMultiplier);
		if (BoostLoopAudio)
		{
			BoostLoopAudio->SetVolumeMultiplier(BoostLoopAudioVolumeMultiplier);
		}
	}
	else
	{
		MSAudio::StopLoopingMetaSound(BoostLoopAudio);
	}
	bBoostLoopAudioActive = bShouldPlay;
}

void UMSMechAnimatorComponent::UpdateFootstepAudio(const float DeltaSeconds, const float PreviousWalkPhase)
{
	// 歩行接地音: ソルバーの歩行位相が接地境界(半周期=π)を跨いだフレームで1回鳴らす。
	// ブースト中は噴射音を優先し、足音は鳴らさない(ユーザー仕様)。
	if (PoseInputs.bIsInAir || PoseInputs.bIsBoosting || PoseInputs.bIsDead ||
		PoseInputs.GroundSpeed < FootstepMinSpeed || !GetOwner())
	{
		return;
	}
	const float PhaseAdvance = PoseInputs.GroundSpeed * 0.01f / MSGO::MSScale * PoseSettings.StrideFrequency *
		FMath::Max(DeltaSeconds, 0.0f) * UE_PI;
	if (UMSAnimInstance::CrossesFootstepBoundary(PreviousWalkPhase, PhaseAdvance))
	{
		MSAudio::SpawnMetaSoundAtLocation(
			GetOwner(), MSAudioPreset::Resolve(EMSAudioUsage::Footstep, FootstepSound),
			GetOwner()->GetActorLocation(), FootstepFrequencyHz);
	}
}

void UMSMechAnimatorComponent::UpdateChargeAudio()
{
	// State.Charging(GA_Fire_Charge LooseTag)と連動: 溜め中はMS_ChargeHumループを再生し、
	// 進行度でFrequencyを上げる(Unity: pitch Lerp(0.8,1.8,progress))。満充填で完了チャイム1回。
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	UAbilitySystemComponent* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
	// 死亡中はチャージ音を鳴らさない(死亡演出中のループ残留=「鳴り続ける音」バグ防止)
	const bool bCharging = ASC && !PoseInputs.bIsDead && ASC->HasMatchingGameplayTag(MSTags::State_Charging);

	float Progress = 0.0f;
	if (bCharging)
	{
		if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UGA_Fire_Charge::StaticClass()))
		{
			if (const UGA_Fire_Charge* ChargeAbility = Cast<UGA_Fire_Charge>(Spec->GetPrimaryInstance()))
			{
				Progress = ChargeAbility->GetChargeProgress01();
			}
		}
		// プレイヤーはGA_Fire_ChargeをASCへGiveAbilityせず直接保持するため、Specからは進行度が
		// 取れない(常に0=ピッチ固定・完了チャイム不発)。キャラクター側のゲッターへフォールバック。
		if (Progress <= 0.0f)
		{
			if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwner()))
			{
				Progress = Player->GetChargeFireProgress01();
			}
		}
	}

	if (bCharging != bChargeLoopAudioActive)
	{
		if (bCharging)
		{
			USceneComponent* AttachTo = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
			ChargeLoopAudio = MSAudio::SpawnLoopingMetaSoundAttached(
				AttachTo, MSAudioPreset::Resolve(EMSAudioUsage::Charge, ChargeLoopSound),
				ChargeHumBaseFrequencyHz);
			bChargeCompleteSoundPlayed = false;
		}
		else
		{
			MSAudio::StopLoopingMetaSound(ChargeLoopAudio);
		}
		bChargeLoopAudioActive = bCharging;
	}

	if (!bCharging)
	{
		return;
	}

	const float PitchScale = FMath::Lerp(0.8f, 1.8f, FMath::Clamp(Progress, 0.0f, 1.0f));
	if (ChargeLoopAudio)
	{
		ChargeLoopAudio->SetFloatParameter(TEXT("Frequency"), ChargeHumBaseFrequencyHz * PitchScale);
		ChargeLoopAudio->SetPitchMultiplier(PitchScale);
	}
	if (Progress >= 1.0f && !bChargeCompleteSoundPlayed)
	{
		bChargeCompleteSoundPlayed = true;
		MSAudio::PlayMetaSound2D(GetOwner(),
			MSAudioPreset::Resolve(EMSAudioUsage::ChargeComplete, ChargeCompleteSound),
			ChargeCompleteFrequencyHz);
	}
}

void UMSMechAnimatorComponent::StopAllLoopingAudio()
{
	MSAudio::StopLoopingMetaSound(BoostLoopAudio);
	MSAudio::StopLoopingMetaSound(ChargeLoopAudio);
	bBoostLoopAudioActive = false;
	bChargeLoopAudioActive = false;
}

void UMSMechAnimatorComponent::StartMeleeMotion(const EMSMeleeMotionType MotionType, const int32 WeaponPower, const bool bDashMelee)
{
	FMSProceduralPoseSolver::StartMeleeMotion(PoseState, PoseSettings, MotionType, WeaponPower, bDashMelee);
	if (PoseInputs.bIsFlinched || PoseInputs.bIsStaggered || PoseInputs.bIsKnockedDown || PoseInputs.bIsDead)
	{
		PoseState.bMeleeMotionActive = false;
	}
}

void UMSMechAnimatorComponent::StartKickMotion()
{
	FMSProceduralPoseSolver::StartKickMotion(PoseState, PoseInputs);
}

void UMSMechAnimatorComponent::StartStandardSaberMotion(const int32 AttackIndex)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FMSStandardSaberMotion::IsAttack(AttackIndex)) return;
	MulticastStandardSaberMotion(AttackIndex, GetWorld()->GetTimeSeconds());
}

void UMSMechAnimatorComponent::StopStandardSaberMotion()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MulticastStandardSaberMotion(INDEX_NONE, 0.0);
	}
	else StandardSaberAttackIndex = INDEX_NONE;
}

void UMSMechAnimatorComponent::MulticastStandardSaberMotion_Implementation(
	const int32 AttackIndex, const double ServerStartTime)
{
	StandardSaberAttackIndex = AttackIndex;
	StandardSaberStartTime = ServerStartTime;
	if (!Rig) Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	if (Rig && AttackIndex != INDEX_NONE) Rig->StopMotion(false);
	PoseState.bMeleeMotionActive = false;
	const UMSWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	StandardSaberWeapon = Weapons ? Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex())) : nullptr;
}

float UMSMechAnimatorComponent::GetStandardSaberElapsedSeconds() const
{
	if (!GetWorld() || StandardSaberAttackIndex == INDEX_NONE) return 0.0f;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0, Now - StandardSaberStartTime);
}

void UMSMechAnimatorComponent::PreviewStandardSaberPose(const int32 AttackIndex, const float TimeSeconds)
{
	if (!Rig) Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	const float PreviousReadyAlpha = StandardSaberReadyAlpha;
	StandardSaberReadyAlpha = 1.0f;
	ApplyStandardSaberPose(AttackIndex, TimeSeconds, true);
	StandardSaberReadyAlpha = PreviousReadyAlpha;
}

void UMSMechAnimatorComponent::ApplyStandardSaberPose(const int32 AttackIndex,
	const float TimeSeconds, const bool bIncludeLegs)
{
	if (!Rig) return;
	const UMSWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const UMSMeleeWeaponData* Weapon = Weapons ? Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex())) : nullptr;
	const int32 PoseIndex = AttackIndex == INDEX_NONE && Weapon && Weapon->bUseGundamDualSaberMotion
		? FMSStandardSaberMotion::DualReady : AttackIndex;
	const FMSStandardSaberPose Pose = FMSStandardSaberMotion::Sample(PoseIndex, TimeSeconds);
	auto Set = [this](const FName Name, const FQuat& Q)
	{
		if (USceneComponent* Joint = Rig->GetJoint(Name)) Joint->SetRelativeRotation(Q);
	};
	if (AttackIndex != INDEX_NONE)
	{
		ResetDirectionalFacingForAuthoredMotion();
		const FQuat Body = FRotator(0, Pose.BodyYaw, 0).Quaternion();
		for (const FName Name : {FName(TEXT("Head")), FName(TEXT("Backpack")),
			FName(TEXT("Shoulder_L")), FName(TEXT("Shoulder_R")), FName(TEXT("UpperArm_L")), FName(TEXT("UpperArm_R"))})
		{
			if (USceneComponent* Joint = Rig->GetJoint(Name))
			{
				Joint->SetRelativeLocation(Body.RotateVector(Rig->GetJointRestRelativeLocation(Name)));
				Joint->SetRelativeRotation(Body);
			}
		}
		Set(TEXT("Torso"), Pose.Torso);
		// The free/shield arm remains bent close to the body throughout the swing.
		Set(TEXT("UpperArm_L"), Body * FRotator(18, -12, -8).Quaternion());
		Set(TEXT("Forearm_L"), FRotator(38, 0, 0).Quaternion());
		if (bIncludeLegs || AttackIndex == FMSStandardSaberMotion::DualDash)
		{
			// Gundam N's D is a whole-body clockwise turn. The ghost rig has flat
			// hip/thigh roots: rotate their offsets as well as their orientations so
			// the legs stay attached to the turning pelvis. Knee/ankle flex stays local.
			const bool bFullBodyTurn = AttackIndex == FMSStandardSaberMotion::DualDash;
			const FQuat LowerBody = bFullBodyTurn ? Body : FQuat::Identity;
			if (bFullBodyTurn)
			{
				for (const FName Name : {FName(TEXT("Hip")), FName(TEXT("Thigh_L")), FName(TEXT("Thigh_R"))})
				{
					if (USceneComponent* Joint = Rig->GetJoint(Name))
						Joint->SetRelativeLocation(Body.RotateVector(Rig->GetJointRestRelativeLocation(Name)));
				}
				Set(TEXT("Hip"), Body);
			}
			Set(TEXT("Thigh_L"), LowerBody * FRotator(Pose.ThighLeft, 0, 0).Quaternion());
			Set(TEXT("Thigh_R"), LowerBody * FRotator(Pose.ThighRight, 0, 0).Quaternion());
			Set(TEXT("Shin_L"), FRotator(-Pose.KneeLeft, 0, 0).Quaternion());
			Set(TEXT("Shin_R"), FRotator(-Pose.KneeRight, 0, 0).Quaternion());
			Set(TEXT("Foot_L"), FRotator(Pose.KneeLeft - Pose.ThighLeft, 0, 0).Quaternion());
			Set(TEXT("Foot_R"), FRotator(Pose.KneeRight - Pose.ThighRight, 0, 0).Quaternion());
		}
	}
	const float Alpha = AttackIndex == INDEX_NONE ? StandardSaberReadyAlpha : 1.0f;
	auto Arm = [this, &Set, Alpha](const FName Name, const FQuat& Q)
	{
		if (const USceneComponent* Joint = Rig->GetJoint(Name))
			Set(Name, FQuat::Slerp(Joint->GetRelativeTransform().GetRotation(), Q, Alpha).GetNormalized());
	};
	const FQuat ReadyFacing = AttackIndex == INDEX_NONE
		? FRotator(0, PoseState.UpperBodyYaw, 0).Quaternion() : FQuat::Identity;
	Arm(TEXT("UpperArm_R"), ReadyFacing * Pose.UpperArm);
	Arm(TEXT("Forearm_R"), Pose.Forearm);
	Arm(TEXT("WeaponMount"), Pose.WeaponMount);
	if (Pose.bDual)
	{
		Arm(TEXT("UpperArm_L"), ReadyFacing * Pose.UpperArmLeft);
		Arm(TEXT("Forearm_L"), Pose.ForearmLeft);
		Arm(TEXT("WeaponMount_L"), Pose.WeaponMountLeft);
	}
}

void UMSMechAnimatorComponent::UpdateStandardSaberPose(const float DeltaSeconds)
{
	const UMSWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UMSWeaponComponent>() : nullptr;
	const UMSMeleeWeaponData* Weapon = Weapons ? Cast<UMSMeleeWeaponData>(Weapons->GetWeaponData(Weapons->GetActiveSlotIndex())) : nullptr;
	const UAbilitySystemComponent* SaberASC = GetOwner() ? GetOwner()->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
	const bool bDisabled = PoseInputs.bIsFlinched || PoseInputs.bIsDead || PoseInputs.bIsKnockedDown || PoseInputs.bIsStaggered
		|| (SaberASC && SaberASC->HasMatchingGameplayTag(MSTags::State_EmergencyRecall));
	const bool bReady = Weapon && Weapon->UsesTimedSaberMotion() && !bDisabled && !Weapons->IsSwitching();
	StandardSaberReadyAlpha = FMath::FInterpConstantTo(StandardSaberReadyAlpha, bReady ? 1.0f : 0.0f,
		DeltaSeconds, 7.0f);
	if (StandardSaberAttackIndex != INDEX_NONE)
	{
		const float Elapsed = GetStandardSaberElapsedSeconds();
		if (!bReady || Weapon != StandardSaberWeapon.Get()
			|| Elapsed >= FMSStandardSaberMotion::GetTiming(StandardSaberAttackIndex).Duration)
		{
			StandardSaberAttackIndex = INDEX_NONE;
		}
		else
		{
			ApplyStandardSaberPose(StandardSaberAttackIndex, Elapsed, !PoseInputs.bIsCrouching);
			return;
		}
	}
	if (bReady && StandardSaberReadyAlpha > 0.0f) ApplyStandardSaberPose(INDEX_NONE, 0, false);
}

void UMSMechAnimatorComponent::CancelAuthoredMotionIfDisabled()
{
	if (Rig && Rig->IsMotionPlaying()
		&& (PoseInputs.bIsFlinched || PoseInputs.bIsDead || PoseInputs.bIsKnockedDown || PoseInputs.bIsStaggered))
	{
		Rig->StopMotion(true);
		ResetDirectionalFacingForAuthoredMotion();
		PoseInputs.bHasAuthoredMotion = false;
	}
}

void UMSMechAnimatorComponent::ResetDirectionalFacingForAuthoredMotion()
{
	if (!Rig) Rig = GetOwner() ? GetOwner()->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	if (Rig)
	{
		const FName Roots[] = {TEXT("Hip"), TEXT("Thigh_L"), TEXT("Thigh_R"), TEXT("Torso"),
			TEXT("Head"), TEXT("Backpack"), TEXT("Shoulder_L"), TEXT("Shoulder_R"),
			TEXT("UpperArm_L"), TEXT("UpperArm_R")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Roots); ++Index)
		{
			if (USceneComponent* Joint = Rig->GetJoint(Roots[Index]))
			{
				const float Previous = Index < 3 ? LastAppliedLowerBodyYaw : LastAppliedUpperBodyYaw;
				Joint->SetRelativeRotation(FRotator(0.0f, -Previous, 0.0f).Quaternion()
					* Joint->GetRelativeRotation().Quaternion());
				Joint->SetRelativeLocation(Rig->GetJointRestRelativeLocation(Roots[Index]));
			}
		}
	}
	PoseState.LowerBodyYaw = PoseState.UpperBodyYaw = 0.0f;
	LastAppliedLowerBodyYaw = LastAppliedUpperBodyYaw = 0.0f;
}

void UMSMechAnimatorComponent::ApplyRotationTarget(const FName JointName, const FQuat& Target,
	const float DeltaSeconds)
{
	if (USceneComponent* Joint = Rig ? Rig->GetJoint(JointName) : nullptr)
	{
		const bool bLowerRoot = JointName == TEXT("Hip") || JointName == TEXT("Thigh_L")
			|| JointName == TEXT("Thigh_R");
		const bool bUpperRoot = JointName == TEXT("Torso") || JointName == TEXT("Head")
			|| JointName == TEXT("Backpack") || JointName == TEXT("Shoulder_L")
			|| JointName == TEXT("Shoulder_R") || JointName == TEXT("UpperArm_L")
			|| JointName == TEXT("UpperArm_R");
		const float PreviousFacing = bLowerRoot ? LastAppliedLowerBodyYaw
			: bUpperRoot ? LastAppliedUpperBodyYaw : 0.0f;
		const float Facing = bLowerRoot ? PoseState.LowerBodyYaw : bUpperRoot ? PoseState.UpperBodyYaw : 0.0f;
		const FQuat FacingRotation = FRotator(0.0f, Facing, 0.0f).Quaternion();
		const FQuat Current = (FRotator(0.0f, -PreviousFacing, 0.0f).Quaternion()
			* Joint->GetRelativeTransform().GetRotation()).GetNormalized();
		// #183 invariant: Target is rebuilt from this frame's complete solver output and the
		// canonical identity-relative rig pose. Current is used only as the interpolation origin;
		// it is never added to Target, so crouch/stand cycles cannot accumulate joint rotation.
		// Keep the interpolation in quaternion space. A knee bending past 90 degrees
		// has an equivalent Euler representation with 180-degree yaw/roll. Feeding
		// those decomposed angles back into separate scalar interpolators made the
		// knee twist every frame, both while holding crouch and while standing up.
		// QInterpConstantTo caps a step at one radian even for a long frame.
		// Apply the complete angular budget so hitches and deterministic large-dt
		// evaluations keep the existing degrees-per-second transition contract.
		const double Angle = Current.AngularDistance(Target);
		const double MaxStep = FMath::DegreesToRadians(FMath::Max(LimbMoveSpeedDegPerSec, 0.0f))
			* FMath::Max(DeltaSeconds, 0.0f);
		const double Alpha = Angle <= UE_SMALL_NUMBER ? 1.0 : FMath::Min(MaxStep / Angle, 1.0);
		const FQuat LocalPose = FQuat::Slerp(Current, Target, Alpha).GetNormalized();
		Joint->SetRelativeRotation(FacingRotation * LocalPose);
		if (bLowerRoot || bUpperRoot)
		{
			// The source rig has flat root joints. Rotate their offsets as one assembly so
			// shoulders/arms/backpack do not detach from the torso as it turns.
			Joint->SetRelativeLocation(FacingRotation.RotateVector(Rig->GetJointRestRelativeLocation(JointName)));
		}
	}
}

void UMSMechAnimatorComponent::ApplyPitch(const FName JointName, const float UnityAngle, const float DeltaSeconds,
	const float Yaw, const float Roll)
{
	ApplyRotationTarget(JointName, FRotator(-UnityAngle, Yaw, Roll).Quaternion(), DeltaSeconds);
}

void UMSMechAnimatorComponent::ApplyPoseToRig(const float DeltaSeconds)
{
	if (!Rig || !Rig->GetRigRoot())
	{
		return;
	}
	// CrouchGroundOffsetはsolverがactor空間cmへ変換済み。RigRootの自己スケールでは拡大されない。
	Rig->GetRigRoot()->SetRelativeLocation(
		FVector(0.0f, 0.0f, Rig->GetRigRootRestRelativeZ() - PoseState.CrouchGroundOffset));
	for (const FName JointName : {FName(TEXT("Hip")), FName(TEXT("Head")), FName(TEXT("Backpack")),
		FName(TEXT("Shoulder_L")), FName(TEXT("Shoulder_R"))})
	{
		ApplyPitch(JointName, 0.0f, DeltaSeconds);
	}
	ApplyPitch(TEXT("Torso"), PoseState.DashLeanAngle + PoseState.TorsoLeanPitch, DeltaSeconds,
		PoseState.MeleeTorsoTwistAngle);

	const float LegWalk = PoseState.WalkSwingAlpha * PoseSettings.LegSwingAngle;
	const float ArmWalk = -PoseState.WalkSwingAlpha * PoseSettings.ArmSwingAngle;
	// Time reversal negates sin(phase) but leaves cos(phase) unchanged. Negating both
	// would only swap the legs' phase and still animate a forward step while backing up.
	const float PhaseCos = FMath::Cos(PoseState.WalkPhaseAngle);
	const float KneeL = FMath::Max(0.0f, -PhaseCos) * PoseSettings.KneeBendAngle * PoseState.WalkIntensity;
	const float KneeR = FMath::Max(0.0f, PhaseCos) * PoseSettings.KneeBendAngle * PoseState.WalkIntensity;
	const float Dash = PoseState.DashPoseAlpha;
	float ThighL = FMath::Lerp(LegWalk, PoseSettings.DashLegTrailAngle, Dash);
	float ThighR = FMath::Lerp(-LegWalk, PoseSettings.DashLegTrailAngle, Dash);
	float ShinL = FMath::Lerp(KneeL, PoseSettings.DashKneeBendAngle, Dash);
	float ShinR = FMath::Lerp(KneeR, PoseSettings.DashKneeBendAngle, Dash);
	ThighL += PoseSettings.bLeftKneels ? PoseState.LeftCrouchThighAngle : -PoseState.LeftCrouchThighAngle;
	ThighR += PoseSettings.bLeftKneels ? -PoseState.RightCrouchThighAngle : PoseState.RightCrouchThighAngle;
	ShinL += PoseState.LeftCrouchKneeAngle;
	ShinR += PoseState.RightCrouchKneeAngle;
	ThighL -= PoseState.MeleeLungeThighAngle;
	ShinL += PoseState.MeleeLungeKneeAngle;
	ThighR -= PoseState.KickThighAngle;
	ShinR -= PoseState.KickShinAngle;
	ThighL += PoseState.ThighKickAngle;
	ThighR += PoseState.ThighKickAngle;
	ShinL += PoseState.ShinKickAngle;
	ShinR += PoseState.ShinKickAngle;
	ApplyPitch(TEXT("Thigh_L"), ThighL, DeltaSeconds);
	ApplyPitch(TEXT("Thigh_R"), ThighR, DeltaSeconds);
	ApplyPitch(TEXT("Shin_L"), ShinL, DeltaSeconds);
	ApplyPitch(TEXT("Shin_R"), ShinR, DeltaSeconds);
	ApplyPitch(TEXT("Foot_L"), -(ThighL + ShinL) * PoseSettings.FootLevelRatio * (1.0f - PoseState.CrouchPoseAlpha), DeltaSeconds);
	ApplyPitch(TEXT("Foot_R"), -(ThighR + ShinR) * PoseSettings.FootLevelRatio, DeltaSeconds);

	float UpperL = FMath::Lerp(ArmWalk, -PoseSettings.DashLegTrailAngle * 0.5f, Dash);
	float ForeL = -PoseSettings.ElbowRestAngle + FMath::Min(0.0f, UpperL) * 0.5f;
	UpperL = FMath::Lerp(UpperL, -PoseSettings.GuardUpperArmAngle, PoseState.GuardPoseAlpha) + PoseState.ArmSlackAngle;
	ForeL = FMath::Lerp(ForeL, -PoseSettings.GuardForearmAngle, PoseState.GuardPoseAlpha);
	float BaseR = FMath::Lerp(-ArmWalk, -PoseSettings.DashLegTrailAngle * 0.5f, Dash);
	// 腕キューブはローカル-Z方向へ伸びる。ApplyPitchはUnity角を符号反転してUEピッチへ渡すため、
	// 「負のUnity角 → 正のUEピッチ → 腕が機体前方へ回る」(MSProceduralPoseSolver.h AimArmAngle参照)。
	// 携行(ranged rest)は歩行スイングへの加算オフセット: 通常時も腕が前後にスイングし続ける。
	// 武器を持つ側の判定: MSWeaponVisualComponentは武器をWeaponMountジョイントへ装着する。
	// WeaponMountの親前腕がどちらかで武器側の腕を決める(現行リグはForearm_R=右手持ち)。
	const USceneComponent* MountJoint = Rig->GetJoint(TEXT("WeaponMount"));
	const bool bWeaponOnLeftArm = MountJoint && MountJoint->GetAttachParent() == Rig->GetJoint(TEXT("Forearm_L"));

	// 携行(ranged rest)オフセットは武器側の腕にのみ加算(歩行スイングは両腕とも継続)
	const float RestUpperR = BaseR + (bWeaponOnLeftArm ? 0.0f
		: PoseSettings.RangedRestUpperArmAngle * PoseState.RangedRestPoseAlpha);
	const float RestForeR = (-PoseSettings.ElbowRestAngle + FMath::Min(0.0f, BaseR) * 0.5f)
		+ (bWeaponOnLeftArm ? 0.0f : PoseSettings.RangedRestForearmAngle * PoseState.RangedRestPoseAlpha);
	float UpperR = RestUpperR;
	float ForeR = RestForeR;
	// ユーザー仕様: 構えで前方へ上げるのは「武器を持っている側の手」のみ。
	// 反対の手(シールド側/空き手)は下垂+歩行スイング(シールド時はガードポーズ)を維持する。
	// 大型インポート武器(TAU-rusチャージブラスター)は肩担ぎに見えないよう脇抱えの低い構えを使う。
	const UMSWeaponVisualComponent* WeaponVisual =
		GetOwner() ? GetOwner()->FindComponentByClass<UMSWeaponVisualComponent>() : nullptr;
	const AMSUnitBase* Unit = Cast<AMSUnitBase>(GetOwner());
	const UMSWeaponComponent* Weapons = Unit ? Unit->GetWeaponComponent() : nullptr;
	const UMSWeaponDataBase* ActiveWeapon = Weapons
		? Weapons->GetWeaponData(Weapons->GetActiveSlotIndex()) : nullptr;
	const UMSMeshVisualComponent* MeshVisual = Unit ? Unit->GetMeshVisualComponent() : nullptr;
	const UMSMobHandVisuals* MobHands = MeshVisual ? MeshVisual->GetMobHandVisuals() : nullptr;
	const bool bMobLightTriggerWeapon = MobHands && MobHands->IsUsingMobLightHands()
		&& ActiveWeapon && !ActiveWeapon->bIsBuiltIn
		&& ActiveWeapon->WeaponMeshMount == EMSWeaponMeshMount::Hand
		&& WeaponVisual && WeaponVisual->IsImportedHeldMeshVisible()
		&& UMSMobHandVisuals::ResolvePoseForWeapon(ActiveWeapon).Right == EMSMobHandPose::Trigger;
	const bool bMobLightTriggerRightPose = bMobLightTriggerWeapon && !bWeaponOnLeftArm
		&& !PoseInputs.bIsFlinched && !PoseInputs.bIsDead && !PoseInputs.bIsKnockedDown && !PoseInputs.bIsStaggered
		&& !PoseInputs.bHasAuthoredMotion;
	const float AimArmTarget = bMobLightTriggerRightPose ? MobLightBmgAimUpperUnityAngle
		: (WeaponVisual && WeaponVisual->IsImportedHeldMeshVisible())
		? PoseSettings.UnderarmAimArmAngle : PoseSettings.AimArmAngle;
	const float AimForearmTarget = bMobLightTriggerRightPose
		? MobLightBmgAimForearmUnityAngle : 0.0f;
	if (bWeaponOnLeftArm)
	{
		UpperL = FMath::Lerp(UpperL + PoseSettings.RangedRestUpperArmAngle * PoseState.RangedRestPoseAlpha,
			AimArmTarget, PoseState.AimPoseAlpha);
		ForeL = FMath::Lerp(ForeL + PoseSettings.RangedRestForearmAngle * PoseState.RangedRestPoseAlpha,
			AimForearmTarget, PoseState.AimPoseAlpha);
	}
	else
	{
		UpperR = FMath::Lerp(RestUpperR, AimArmTarget, PoseState.AimPoseAlpha);
		const float TriggerRestForeR = bMobLightTriggerRightPose
			? MobLightTriggerRestForearmUnityAngle * PoseState.RangedRestPoseAlpha : RestForeR;
		ForeR = FMath::Lerp(TriggerRestForeR, AimForearmTarget, PoseState.AimPoseAlpha);
	}
	UpperR -= PoseSettings.WeaponDrawArmAngle * PoseState.WeaponSwitchPoseAlpha;
	ForeR -= PoseSettings.WeaponDrawElbowAngle * PoseState.WeaponSwitchPoseAlpha;
	UpperR += PoseState.ArmSlackAngle;
	ApplyPitch(TEXT("UpperArm_L"), UpperL, DeltaSeconds);
	// Bend the elbow across the chest instead of extending the whole arm at the threat.
	// Imported rigs can mirror the semantic left chain, so derive inward from its actual side.
	const UPoseableMeshComponent* GuardMesh = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
	const bool bShieldArmOnRight = GuardMesh && GuardMesh->GetBoneIndex(TEXT("forearm_l")) != INDEX_NONE
		&& GetOwner()->GetActorTransform().InverseTransformPosition(
			GuardMesh->GetBoneLocation(TEXT("forearm_l"), EBoneSpaces::WorldSpace)).Y > 0.0f;
	ApplyPitch(TEXT("Forearm_L"), ForeL, DeltaSeconds,
		(bShieldArmOnRight ? -45.0f : 45.0f) * PoseState.GuardPoseAlpha);
	if (PoseState.MeleeMotionAlpha > 0.0f)
	{
		if (PoseState.ActiveMeleeMotionType == EMSMeleeMotionType::Slash)
		{
			ApplyPitch(TEXT("UpperArm_R"), -PoseState.MeleeRaiseAngle, DeltaSeconds, -PoseState.MeleeShoulderAngle);
			ApplyPitch(TEXT("Forearm_R"), 0.0f, DeltaSeconds, -PoseState.MeleeElbowAngle);
		}
		else
		{
			ApplyPitch(TEXT("UpperArm_R"), -PoseState.MeleeShoulderAngle, DeltaSeconds);
			ApplyPitch(TEXT("Forearm_R"), -PoseState.MeleeElbowAngle, DeltaSeconds);
		}
	}
	else
	{
		ApplyPitch(TEXT("UpperArm_R"), UpperR, DeltaSeconds, 0.0f,
			bMobLightTriggerRightPose ? MobLightBmgUpperRoll : 0.0f);
		ApplyPitch(TEXT("Forearm_R"), ForeR, DeltaSeconds, 0.0f,
			bMobLightTriggerRightPose ? MobLightBmgForearmRoll : 0.0f);
		if (bMobLightTriggerRightPose)
		{
			// The trigger-grip fit turns the elbow sideways. Cancel the complete current local
			// arm rotation at the mount, rather than negating Euler yaw/roll values;
			// quaternion multiplication order matters once the forearm leaves XZ.
			const USceneComponent* UpperArmR = Rig->GetJoint(TEXT("UpperArm_R"));
			const USceneComponent* ForearmR = Rig->GetJoint(TEXT("Forearm_R"));
			if (UpperArmR && ForearmR)
			{
				// UpperArm_R carries the cosmetic upper-body facing on its relative
				// rotation. Strip that root-facing term before inverting the arm pose,
				// so the mount remains aimed with the upper body instead of world zero.
				const FQuat FacingInverse = FRotator(0.0f, -PoseState.UpperBodyYaw, 0.0f).Quaternion();
				const FQuat UpperLocal = (FacingInverse * UpperArmR->GetRelativeTransform().GetRotation()).GetNormalized();
				const FQuat ForeLocal = ForearmR->GetRelativeTransform().GetRotation();
				ApplyRotationTarget(TEXT("WeaponMount"), (UpperLocal * ForeLocal).Inverse(), DeltaSeconds);
			}
		}
		else
		{
			// Weapon geometry is authored along local +X. Cancel the accumulated arm pitch at
			// the mount so the gun and muzzle remain level while the weapon-side limb rises forward.
			ApplyPitch(TEXT("WeaponMount"),
				bWeaponOnLeftArm ? -(UpperL + ForeL) : -(UpperR + ForeR), DeltaSeconds);
		}
	}
	LastAppliedLowerBodyYaw = PoseState.LowerBodyYaw;
	LastAppliedUpperBodyYaw = PoseState.UpperBodyYaw;
	UpdateStandardSaberPose(DeltaSeconds);
}

void UMSMechAnimatorComponent::BuildThrusterVisuals()
{
	AActor* Owner = GetOwner();
	USceneComponent* Backpack = Rig ? Rig->GetJoint(TEXT("Backpack")) : nullptr;
	if (!Owner || !Backpack || !ThrusterSmokeMaterial || ThrusterSmokeTextures.IsEmpty() ||
		!ThrusterVisuals.IsEmpty())
	{
		return;
	}

	MeshThrusterBoneNames.Reset();
	bUsesMeshThrusterBones = false;
	if (const UMSMeshVisualComponent* MeshVisual = Owner->FindComponentByClass<UMSMeshVisualComponent>())
	{
		if (UPoseableMeshComponent* PoseableMesh = MeshVisual->GetPoseableMesh();
			PoseableMesh && MeshVisual->IsMeshVisualActive())
		{
			for (int32 BoneIndex = 0; BoneIndex < PoseableMesh->GetNumBones(); ++BoneIndex)
			{
				const FName BoneName = PoseableMesh->GetBoneName(BoneIndex);
				if (BoneName.ToString().StartsWith(TEXT("thruster_"), ESearchCase::CaseSensitive))
				{
					MeshThrusterBoneNames.Add(BoneName);
				}
			}
			bUsesMeshThrusterBones = !MeshThrusterBoneNames.IsEmpty();
		}
	}
	ThrusterPointWorldTransforms.SetNum(bUsesMeshThrusterBones ? MeshThrusterBoneNames.Num() : 2);

	// Kenney CC0のアルファ付き煙を常にカメラへ正対させる。動く煙はUpdateThrusterSmokeが
	// 低頻度で生成するため、ここはノズル直後を埋める低アルファの小型Billboardだけにする。
	int32 TextureIndex = 0;
	auto AddSmokeBase = [this, Owner, &TextureIndex](const FString& Name,
		const float SizeCm, const FLinearColor& Color, const float Opacity)
	{
		UMaterialBillboardComponent* Puff = NewObject<UMaterialBillboardComponent>(Owner, FName(*Name));
		Owner->AddInstanceComponent(Puff);
		Puff->SetupAttachment(Owner->GetRootComponent());
		Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puff->SetCanEverAffectNavigation(false);
		Puff->SetCastShadow(false);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ThrusterSmokeMaterial, this);
		Material->SetTextureParameterValue(TEXT("SmokeTexture"),
			ThrusterSmokeTextures[TextureIndex++ % ThrusterSmokeTextures.Num()]);
		Material->SetVectorParameterValue(TEXT("SmokeTint"), Color);
		Material->SetScalarParameterValue(TEXT("SmokeOpacity"), Opacity);
		Puff->AddElement(Material, nullptr, false, SizeCm, SizeCm, nullptr);
		Puff->RegisterComponent();
		ThrusterVisuals.Add(Puff);
	};

	for (int32 PointIndex = 0; PointIndex < ThrusterPointWorldTransforms.Num(); ++PointIndex)
	{
		AddSmokeBase(FString::Printf(TEXT("ThrusterSmoke_%d_Warm"), PointIndex),
			18.0f, FLinearColor(1.0f, 0.72f, 0.45f), 0.25f);
		AddSmokeBase(FString::Printf(TEXT("ThrusterSmoke_%d_Light"), PointIndex),
			24.0f, FLinearColor(0.82f, 0.84f, 0.86f), 0.18f);
		AddSmokeBase(FString::Printf(TEXT("ThrusterSmoke_%d_Grey"), PointIndex),
			30.0f, FLinearColor(0.56f, 0.59f, 0.62f), 0.12f);
	}
	UpdateThrusterPointTransforms();
	SetThrusterVisible(false);
}

void UMSMechAnimatorComponent::RefreshThrusterVisuals()
{
	for (UMaterialBillboardComponent* Visual : ThrusterVisuals)
	{
		if (Visual)
		{
			Visual->DestroyComponent();
		}
	}
	ThrusterVisuals.Reset();
	MeshThrusterBoneNames.Reset();
	ThrusterPointWorldTransforms.Reset();
	bUsesMeshThrusterBones = false;
	BuildThrusterVisuals();
}

void UMSMechAnimatorComponent::UpdateThrusterPointTransforms()
{
	AActor* Owner = GetOwner();
	USceneComponent* Backpack = Rig ? Rig->GetJoint(TEXT("Backpack")) : nullptr;
	if (!Owner || !Backpack)
	{
		return;
	}

	if (bUsesMeshThrusterBones)
	{
		const UMSMeshVisualComponent* MeshVisual = Owner->FindComponentByClass<UMSMeshVisualComponent>();
		UPoseableMeshComponent* PoseableMesh = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
		if (!PoseableMesh)
		{
			return;
		}
		// BuildThrusterVisuals may have bailed out early (e.g. smoke assets not yet loaded when
		// this Tick runs), leaving the array unsized. Guard defensively instead of trusting it.
		if (ThrusterPointWorldTransforms.Num() != MeshThrusterBoneNames.Num())
		{
			ThrusterPointWorldTransforms.SetNum(MeshThrusterBoneNames.Num());
		}
		for (int32 Index = 0; Index < MeshThrusterBoneNames.Num(); ++Index)
		{
			ThrusterPointWorldTransforms[Index] = PoseableMesh->GetBoneTransformByName(
				MeshThrusterBoneNames[Index], EBoneSpaces::WorldSpace);
		}
	}
	else
	{
		if (ThrusterPointWorldTransforms.Num() != 2)
		{
			ThrusterPointWorldTransforms.SetNum(2);
		}
		const FVector Direction = -Owner->GetActorForwardVector();
		const FVector Up = Owner->GetActorUpVector();
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(Direction, Up).ToQuat();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const float SideY = Index == 0 ? -10.0f : 10.0f;
			const FVector Nozzle = Backpack->GetComponentTransform().TransformPosition(
				FVector(-18.0f, SideY, -10.0f));
			ThrusterPointWorldTransforms[Index] = FTransform(Rotation, Nozzle);
		}
	}

	static constexpr float DirectionOffsets[] = {2.0f, 10.0f, 19.0f};
	static constexpr float UpOffsets[] = {0.0f, 3.0f, 7.0f};
	for (int32 PointIndex = 0; PointIndex < ThrusterPointWorldTransforms.Num(); ++PointIndex)
	{
		const FTransform& Point = ThrusterPointWorldTransforms[PointIndex];
		const FVector Direction = Point.GetUnitAxis(EAxis::X); // authored nozzle exhaust axis
		const FVector Up = Point.GetUnitAxis(EAxis::Z);
		for (int32 LayerIndex = 0; LayerIndex < 3; ++LayerIndex)
		{
			const int32 VisualIndex = PointIndex * 3 + LayerIndex;
			if (ThrusterVisuals.IsValidIndex(VisualIndex) && ThrusterVisuals[VisualIndex])
			{
				ThrusterVisuals[VisualIndex]->SetWorldLocation(Point.GetLocation() +
					Direction * DirectionOffsets[LayerIndex] + Up * UpOffsets[LayerIndex]);
			}
		}
	}
}

void UMSMechAnimatorComponent::UpdateThrusterEffectsAfterMeshPose(const float DeltaSeconds)
{
	if (!bUsesMeshThrusterBones)
	{
		return;
	}
	UpdateThrusterPointTransforms();
	UpdateThrusterSmoke(DeltaSeconds);
}

void UMSMechAnimatorComponent::UpdateThrusterSmoke(const float DeltaSeconds)
{
	ThrusterSmokeCooldownRemaining = FMath::Max(
		0.0f, ThrusterSmokeCooldownRemaining - FMath::Max(DeltaSeconds, 0.0f));
	AActor* Owner = GetOwner();
	if (!Owner || ThrusterPointWorldTransforms.IsEmpty() || !PoseInputs.bIsBoosting || PoseInputs.bIsDead ||
		ThrusterSmokeCooldownRemaining > 0.0f)
	{
		return;
	}
	ThrusterSmokeCooldownRemaining = ThrusterSmokeIntervalSeconds;
	for (const FTransform& Point : ThrusterPointWorldTransforms)
	{
		AMSTransientVfxActor::SpawnThrusterSmokePuff(GetWorld(), Point.GetLocation(),
			Point.GetUnitAxis(EAxis::X), Point.GetUnitAxis(EAxis::Z),
			ThrusterSmokeVariant++, ThrusterSmokeLifetimeSeconds);
	}
}

void UMSMechAnimatorComponent::UpdateBoostAfterimage(const float DeltaSeconds)
{
	// 実プレイレビュー: 通常ブーストの青残像は完全に廃止する。固有モジュール発動中のみ、
	// 非ブースト移動も含めてモジュール色の残像を出す。静止中・死亡中の抑止は維持する。
	AfterimageCooldownRemaining = FMath::Max(0.0f, AfterimageCooldownRemaining - FMath::Max(DeltaSeconds, 0.0f));
	const AActor* Owner = GetOwner();
	if (!Owner || !bModuleAfterimageOverrideEnabled || PoseInputs.bIsDead ||
		PoseInputs.GroundSpeed < AfterimageMinSpeed || AfterimageCooldownRemaining > 0.0f)
	{
		return;
	}
	AfterimageCooldownRemaining = AfterimageIntervalSeconds;
	AMSTransientVfxActor::SpawnBoostAfterimage(GetWorld(), Owner,
		ModuleAfterimageOverrideColor, AfterimageLifetimeSeconds);
}

void UMSMechAnimatorComponent::SetModuleAfterimageOverride(const bool bEnabled, const FLinearColor Color)
{
	bModuleAfterimageOverrideEnabled = bEnabled;
	ModuleAfterimageOverrideColor = Color;
}

void UMSMechAnimatorComponent::SetModuleBodyAura(const bool bEnabled, const FLinearColor Color)
{
	// Always unwind the previous application first. This also makes repeated activation/data refresh safe.
	for (int32 Index = 0; Index < ModuleAuraMeshes.Num(); ++Index)
	{
		if (UMeshComponent* Mesh = ModuleAuraMeshes[Index])
		{
			Mesh->SetOverlayMaterial(OriginalOverlayMaterials.IsValidIndex(Index)
				? OriginalOverlayMaterials[Index].Get() : nullptr);
		}
	}
	ModuleAuraMeshes.Reset();
	OriginalOverlayMaterials.Reset();
	ModuleAuraMaterials.Reset();
	bModuleBodyAuraEnabled = false;
	ModuleBodyAuraColor = FLinearColor::Transparent;

	if (!bEnabled || !ModuleAuraMaterial || !GetOwner())
	{
		return;
	}

	TArray<UMeshComponent*> TargetMeshes;
	if (UMSMeshVisualComponent* MeshVisual = GetOwner()->FindComponentByClass<UMSMeshVisualComponent>())
	{
		if (UPoseableMeshComponent* PoseableMesh = MeshVisual->GetPoseableMesh();
			PoseableMesh && MeshVisual->IsMeshVisualActive() && PoseableMesh->IsVisible())
		{
			TargetMeshes.Add(PoseableMesh);
		}
	}
	if (TargetMeshes.IsEmpty())
	{
		if (!Rig)
		{
			Rig = GetOwner()->FindComponentByClass<UMSMechRigComponent>();
		}
		if (Rig)
		{
			for (const FName PartName : Rig->GetPartNames())
			{
				if (UStaticMeshComponent* Part = Rig->GetPart(PartName); Part && Part->IsVisible())
				{
					TargetMeshes.Add(Part);
				}
			}
		}
	}

	for (UMeshComponent* Mesh : TargetMeshes)
	{
		UMaterialInstanceDynamic* Aura = UMaterialInstanceDynamic::Create(ModuleAuraMaterial, this);
		if (!Mesh || !Aura)
		{
			continue;
		}
		Aura->SetVectorParameterValue(TEXT("Color"), Color);
		Aura->SetVectorParameterValue(TEXT("BaseColor"), Color);
		Aura->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
		ModuleAuraMeshes.Add(Mesh);
		OriginalOverlayMaterials.Add(Mesh->GetOverlayMaterial());
		ModuleAuraMaterials.Add(Aura);
		Mesh->SetOverlayMaterial(Aura);
	}

	bModuleBodyAuraEnabled = !ModuleAuraMeshes.IsEmpty();
	ModuleBodyAuraColor = bModuleBodyAuraEnabled ? Color : FLinearColor::Transparent;
}

void UMSMechAnimatorComponent::SetThrusterVisible(const bool bVisible)
{
	for (UMaterialBillboardComponent* Smoke : ThrusterVisuals)
	{
		if (Smoke) Smoke->SetVisibility(bVisible, true);
	}
}

bool UMSMechAnimatorComponent::AreThrustersVisible() const
{
	return !ThrusterVisuals.IsEmpty() && ThrusterVisuals[0] && ThrusterVisuals[0]->IsVisible();
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostLoopAudioVolumeTest,
	"NeoMSGO.Animation.Audio.BoostLoopVolumeEnvelope",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTranszommBodyAuraTest,
	"NeoMSGO.Animation.Module.TranszommBodyAura",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBoostLoopAudioVolumeTest::RunTest(const FString& Parameters)
{
	UMSMechAnimatorComponent* Animator = NewObject<UMSMechAnimatorComponent>();
	FMSProceduralPoseInputs Inputs;
	Inputs.bIsBoosting = true;

	Animator->EvaluatePose(Inputs, 1.0f / 60.0f);
	TestTrue(TEXT("boost onset stays below weapon and confirmation levels"),
		FMath::IsNearlyEqual(Animator->GetBoostLoopAudioVolumeMultiplier(),
			BoostLoopInitialVolume, KINDA_SMALL_NUMBER));

	Animator->EvaluatePose(Inputs, BoostLoopFadeDurationSeconds);
	TestTrue(TEXT("boost settles at the sustained low volume after the fade"),
		FMath::IsNearlyEqual(Animator->GetBoostLoopAudioVolumeMultiplier(),
			BoostLoopSustainVolume, KINDA_SMALL_NUMBER));

	Inputs.bIsBoosting = false;
	Animator->EvaluatePose(Inputs, 1.0f / 60.0f);
	TestFalse(TEXT("boost loop stops when boosting ends"), Animator->IsBoostLoopAudioActive());

	Inputs.bIsBoosting = true;
	Animator->EvaluatePose(Inputs, 1.0f / 60.0f);
	TestTrue(TEXT("restarting boost resets the balanced onset envelope"),
		FMath::IsNearlyEqual(Animator->GetBoostLoopAudioVolumeMultiplier(), BoostLoopInitialVolume));

	return true;
}

bool FMSTranszommBodyAuraTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSTranszommAuraTestWorld"));
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.OwningGameInstance = GameInstance;
	Context.SetCurrentWorld(World);
	World->SetGameInstance(GameInstance);
	GameInstance->Init();
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->GetBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	UMSMechAnimatorComponent* Animator = Unit ? Unit->GetMechAnimatorComponent() : nullptr;
	UMSMechRigComponent* TestRig = Unit ? Unit->FindComponentByClass<UMSMechRigComponent>() : nullptr;
	UStaticMeshComponent* Torso = TestRig ? TestRig->GetPart(TEXT("Torso")) : nullptr;

	if (TestNotNull(TEXT("Transzomm test unit has an animator"), Animator)
		&& TestNotNull(TEXT("Transzomm test unit has a Cube-rig torso"), Torso))
	{
		const FLinearColor AuraColor(1.0f, 0.08f, 0.24f, 0.18f);
		const FLinearColor OriginalTeamColor = TestRig->GetPartColor(TEXT("Torso"));
		UMaterialInterface* OriginalBaseMaterial = Torso->GetMaterial(0);
		UMaterialInterface* OriginalOverlayMaterial = Torso->GetOverlayMaterial();

		Animator->SetModuleBodyAura(true, AuraColor);
		UMaterialInstanceDynamic* AuraMID =
			Cast<UMaterialInstanceDynamic>(Torso->GetOverlayMaterial());
		if (TestNotNull(TEXT("active Transzomm installs an emissive overlay MID"), AuraMID))
		{
			TestTrue(TEXT("active Transzomm EmissiveColor is the configured red-pink value"),
				AuraMID->K2_GetVectorParameterValue(TEXT("EmissiveColor")).Equals(AuraColor, 0.001f));
		}
		TestEqual(TEXT("the aura preserves the underlying team material"),
			Torso->GetMaterial(0), OriginalBaseMaterial);

		Animator->SetModuleBodyAura(false, FLinearColor::Transparent);
		TestEqual(TEXT("ending Transzomm restores the original overlay material"),
			Torso->GetOverlayMaterial(), OriginalOverlayMaterial);
		TestTrue(TEXT("ending Transzomm preserves the original team color"),
			TestRig->GetPartColor(TEXT("Torso")).Equals(OriginalTeamColor, 0.001f));
	}

	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
