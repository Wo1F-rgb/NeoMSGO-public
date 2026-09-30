// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/MSProceduralPoseSolver.h"
#include "MSMechAnimatorComponent.generated.h"

class UAudioComponent;
class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class UMSCameraComponent;
class UMSMechRigComponent;
class UMSMeleeWeaponData;
class UMeshComponent;
class UMaterialInterface;
class UPoseableMeshComponent;
class USoundBase;
class UStaticMesh;
class UTexture2D;

/** Drives UMSMechRigComponent pivots from the shared procedural pose solver every frame. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSMechAnimatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSMechAnimatorComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="MS|Animation|Procedural|Melee")
	void StartMeleeMotion(EMSMeleeMotionType MotionType, int32 WeaponPower, bool bDashMelee);

	/** Server starts the same dedicated N-saber pose on all viewing clients. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Saber")
	void StartStandardSaberMotion(int32 AttackIndex);

	UFUNCTION(BlueprintCallable, Category="MS|Animation|Saber")
	void StopStandardSaberMotion();

	UFUNCTION(BlueprintPure, Category="MS|Animation|Saber")
	int32 GetStandardSaberAttackIndex() const { return StandardSaberAttackIndex; }

	UFUNCTION(BlueprintPure, Category="MS|Animation|Saber")
	float GetStandardSaberElapsedSeconds() const;

	/** Authoring preview of the exact runtime sampler; does not cause hits or movement. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Saber")
	void PreviewStandardSaberPose(int32 AttackIndex, float TimeSeconds);

	UFUNCTION(BlueprintCallable, Category="MS|Animation|Procedural|Melee")
	void StartKickMotion();

	const FMSProceduralPoseState& GetPoseState() const { return PoseState; }
	const FMSProceduralPoseInputs& GetPoseInputs() const { return PoseInputs; }

	/** Deterministic test hook using explicit inputs; also applies the resulting joint pose. */
	void EvaluatePose(const FMSProceduralPoseInputs& Inputs, float DeltaSeconds);
	/** Development-only override used to keep the former >90-degree knee path under quaternion regression. */
#if WITH_DEV_AUTOMATION_TESTS
	void SetCrouchKneeAnglesForAutomationTests(const float InCrouchKneeAngle, const float InKneelKneeAngle)
	{
		PoseSettings.CrouchKneeAngle = InCrouchKneeAngle;
		PoseSettings.KneelKneeAngle = InKneelKneeAngle;
	}
#endif
	/** Remove cosmetic yaw before an authored motion writes absolute joint rotations. */
	void ResetDirectionalFacingForAuthoredMotion();

	UFUNCTION(BlueprintPure, Category="MS|Animation|Procedural")
	bool AreThrustersVisible() const;

	/** Enables the module-only afterimage and supplies its red-pink tint. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Module")
	void SetModuleAfterimageOverride(bool bEnabled, FLinearColor Color);
	bool IsModuleAfterimageOverrideEnabled() const { return bModuleAfterimageOverrideEnabled; }
	FLinearColor GetModuleAfterimageOverrideColor() const { return ModuleAfterimageOverrideColor; }

	/** Applies the Transzomm red-pink emissive overlay without replacing team/imported materials. */
	UFUNCTION(BlueprintCallable, Category="MS|Animation|Module")
	void SetModuleBodyAura(bool bEnabled, FLinearColor Color);
	bool IsModuleBodyAuraEnabled() const { return bModuleBodyAuraEnabled; }
	FLinearColor GetModuleBodyAuraColor() const { return ModuleBodyAuraColor; }

	// ---- P9追補: 実プレイ報告「ブースト音/足音/チャージ音がない」対応のテスト観測API ----
	bool IsBoostLoopAudioActive() const { return bBoostLoopAudioActive; }
	float GetBoostLoopAudioVolumeMultiplier() const { return BoostLoopAudioVolumeMultiplier; }
	bool IsChargeLoopAudioActive() const { return bChargeLoopAudioActive; }
	USoundBase* GetThrusterLoopSound() const { return ThrusterLoopSound; }
	USoundBase* GetFootstepSound() const { return FootstepSound; }
	USoundBase* GetChargeLoopSound() const { return ChargeLoopSound; }
	USoundBase* GetChargeCompleteSound() const { return ChargeCompleteSound; }

	/**
	 * 音フィードバック更新(Tick/EvaluatePoseの両方から呼ぶ)。
	 * ブースト噴射ループ・歩行接地音・チャージ溜め音/完了音をPoseInputs/PoseStateと同期する。
	 * @param PreviousWalkPhase ソルバー更新前のWalkPhaseAngle(接地境界の跨ぎ検出用)
	 */
	void UpdateAudioFeedback(float DeltaSeconds, float PreviousWalkPhase);

	/** Rebuilds nozzle bindings after the optional real-mesh visual changes. */
	void RefreshThrusterVisuals();

	/** Called by MeshVisual immediately after TransferPose so VFX use this frame's bone transforms. */
	void UpdateThrusterEffectsAfterMeshPose(float DeltaSeconds);

	// Thruster binding observations used by Animation automation tests.
	int32 GetThrusterPointCountForAutomationTests() const { return ThrusterPointWorldTransforms.Num(); }
	bool UsesMeshThrusterBonesForAutomationTests() const { return bUsesMeshThrusterBones; }
	const TArray<FName>& GetThrusterBoneNamesForAutomationTests() const { return MeshThrusterBoneNames; }
	FTransform GetThrusterPointTransformForAutomationTests(int32 Index) const
	{
		return ThrusterPointWorldTransforms.IsValidIndex(Index)
			? ThrusterPointWorldTransforms[Index] : FTransform::Identity;
	}

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStandardSaberMotion(int32 AttackIndex, double ServerStartTime);
	void ApplyStandardSaberPose(int32 AttackIndex, float TimeSeconds, bool bIncludeLegs);
	void UpdateStandardSaberPose(float DeltaSeconds);
	int32 StandardSaberAttackIndex = INDEX_NONE;
	double StandardSaberStartTime = 0.0;
	float StandardSaberReadyAlpha = 0.0f;
	TWeakObjectPtr<const UMSMeleeWeaponData> StandardSaberWeapon;

	void GatherInputs();
	void CancelAuthoredMotionIfDisabled();
	void UpdateBoostLoopAudio(float DeltaSeconds);
	void UpdateFootstepAudio(float DeltaSeconds, float PreviousWalkPhase);
	void UpdateChargeAudio();
	void StopAllLoopingAudio();
	void ApplyPoseToRig(float DeltaSeconds);
	void BuildThrusterVisuals();
	void UpdateThrusterPointTransforms();
	void SetThrusterVisible(bool bVisible);
	void UpdateThrusterSmoke(float DeltaSeconds);
	/** 固有モジュール発動中のみ、一定間隔でフェードする残像ゴーストをスポーンする。 */
	void UpdateBoostAfterimage(float DeltaSeconds);
	void ApplyPitch(FName JointName, float UnityAngle, float DeltaSeconds, float Yaw = 0.0f, float Roll = 0.0f);
	/** Applies a complete local quaternion with the same facing compensation and smooth limb interpolation as ApplyPitch. */
	void ApplyRotationTarget(FName JointName, const FQuat& Target, float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UMSMechRigComponent> Rig;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialBillboardComponent>> ThrusterVisuals;

	/** Dynamic thruster_* bindings. Empty means the legacy Backpack two-point layout is active. */
	TArray<FName> MeshThrusterBoneNames;
	TArray<FTransform> ThrusterPointWorldTransforms;
	bool bUsesMeshThrusterBones = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	/** CC0煙テクスチャをアルファブレンドするBillboard用マテリアル。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ThrusterSmokeMaterial;

	/** Emissive overlay shared by Cube-rig parts and real-mesh PoseableMesh visuals. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ModuleAuraMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> ThrusterSmokeTextures;

	FMSProceduralPoseInputs PoseInputs;
	FMSProceduralPoseSettings PoseSettings;
	FMSProceduralPoseState PoseState;
	float LastAppliedLowerBodyYaw = 0.0f;
	float LastAppliedUpperBodyYaw = 0.0f;

	float LimbMoveSpeedDegPerSec = 200.0f;

	/** 照準刺激(発射/チャージ/スコープ)が消えた後も構えを保持する秒数(連射間のばたつき防止) */
	float AimHoldSeconds = 1.5f;

	/** 最後に照準刺激を観測したワールド時刻(未観測=遠い過去) */
	double LastAimStimulusTime = -1.0e9;

	/** スコープ状態参照用カメラ(プレイヤーのみ搭載。BOTはnullのまま=スコープ刺激なし) */
	TWeakObjectPtr<UMSCameraComponent> CameraComponent;

	// ---- P9追補: ユニット音(ブースト噴射ループ/足音/チャージ音) ----
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ThrusterLoopSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ChargeLoopSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ChargeCompleteSound;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BoostLoopAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ChargeLoopAudio;

	/** ブースト噴射ループ再生中か(実オーディオデバイス無し環境でも状態遷移を観測できるようにする) */
	bool bBoostLoopAudioActive = false;

	/** Seconds elapsed since the current boost loop started. */
	float BoostLoopAudioElapsedSeconds = 0.0f;

	/** Last multiplier applied to the boost loop; retained for deterministic tests. */
	float BoostLoopAudioVolumeMultiplier = 1.0f;

	/** チャージ溜めループ再生中か */
	bool bChargeLoopAudioActive = false;

	/** 現チャージでの完了音再生済みフラグ(満充填の一度だけ鳴らす) */
	bool bChargeCompleteSoundPlayed = false;

	/** 足音を鳴らす最低地上速度 cm/s(UMSAnimInstance::FootstepMinSpeedと同値) */
	float FootstepMinSpeed = 50.0f;

	// ---- 固有モジュール残像 ----

	/** 残像スポーン間隔 秒 */
	float AfterimageIntervalSeconds = 0.22f;

	/** 残像1体の寿命 秒(縮みながらフェード) */
	float AfterimageLifetimeSeconds = 0.20f;

	/** 残像を残す最低移動速度 cm/s(モジュール発動中でも静止時は残さない) */
	float AfterimageMinSpeed = 200.0f;

	/** 次の残像スポーンまでの残り秒 */
	float AfterimageCooldownRemaining = 0.0f;

	bool bModuleAfterimageOverrideEnabled = false;
	FLinearColor ModuleAfterimageOverrideColor = FLinearColor::Transparent;

	bool bModuleBodyAuraEnabled = false;
	FLinearColor ModuleBodyAuraColor = FLinearColor::Transparent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> ModuleAuraMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalOverlayMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ModuleAuraMaterials;

	/** 小型煙パフの低頻度スポーン間隔。左右を同時に1組だけ生成する。 */
	float ThrusterSmokeIntervalSeconds = 0.14f;
	float ThrusterSmokeLifetimeSeconds = 0.48f;
	float ThrusterSmokeCooldownRemaining = 0.0f;
	int32 ThrusterSmokeVariant = 0;
};
