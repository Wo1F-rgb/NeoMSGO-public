// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-5: プレイヤーカメラ(スコープFOV遷移+感度倍率)。
// 設計元: docs/UE5_MIGRATION_PLAN.md P3-5 / docs/ArchitectureMap.md セクション5
//         ← Unity: CinemachineVirtualCamera(スコープFOV) + WeaponController のスコープ処理
//
// スコープFOV遷移の適用タイミング(MIGRATION_NOTES 表#5の再発防止):
//   Unity版では CinemachineBrain が毎LateUpdateでレンズ値を上書きするため、
//   通常のUpdateでFOVを書くと反映前に消されるバグがあり、WaitForEndOfFrame後の
//   書き込みで修正した。UE5では APlayerCameraManager::UpdateCamera が毎フレーム
//   UCameraComponent::GetCameraView() を「呼び出して」FOVを取得する(押し付けない)
//   ため、GetCameraView() 内で補間するのが構造的に安定
//   (=カメラマネージャ更新の一部として適用され、後段に上書きされる余地がない)。
//
// スコープ状態はGAS外・カメラ/入力レイヤーの責務(ArchitectureMap セクション5の移行方針)。
// 実際の武器切替システム(P4-1)は未実装のため、本コンポーネントはON/OFF APIのみ提供する。

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "MSCameraComponent.generated.h"

class UMSRangedWeaponData;

UENUM(BlueprintType)
enum class EMSCameraViewPreset : uint8
{
	RightShoulder,
	HighOverhead
};

/**
 * プレイヤー追従カメラ(Unity: CinemachineVirtualCamera + 3rdPersonFollow のレンズ部)。
 * スコープON/OFFに応じたFOV遷移と、Look入力用の感度倍率を提供する。
 */
UCLASS(ClassGroup = (MS), meta = (BlueprintSpawnableComponent))
class NEOMSGO_API UMSCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UMSCameraComponent();

	/**
	 * スコープON/OFF切替(P4-1武器切替システムから呼ばれる想定の土台API)。
	 * @param bInScoped                     trueでスコープFOVへ、falseで通常FOVへ遷移開始
	 * @param InScopedFieldOfView           スコープ時FOV 度(UMSRangedWeaponData::ScopedFieldOfView)
	 * @param InScopedSensitivityMultiplier スコープ時感度倍率(UMSRangedWeaponData::ScopedSensitivityMultiplier)
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetScoped(bool bInScoped, float InScopedFieldOfView, float InScopedSensitivityMultiplier);

	/**
	 * 武器データからのスコープ切替ヘルパー(P4-1接続用)。
	 * 非スコープ武器(bIsScopeWeapon=false)またはnullを渡すとスコープ解除として扱う。
	 * スコープ関連フィールドは射撃武器系(UMSRangedWeaponData)のみが持つ。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetScopedFromWeapon(const UMSRangedWeaponData* WeaponData, bool bInScoped);

	/** 現在スコープ中か */
	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	bool IsScoped() const { return bScoped; }

	/** Look入力に乗算する感度倍率(スコープ中はScopedSensitivityMultiplier、通常時1.0) */
	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	float GetLookSensitivityMultiplier() const;

	/** Changes the normal (non-scoped) FOV used by GetCameraView. */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetDefaultFieldOfView(float NewFieldOfView);

	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	float GetDefaultFieldOfView() const { return DefaultFieldOfView; }

	/** Applies the persisted third-person composition to the attached spring arm. */
	UFUNCTION(BlueprintCallable, Category = "MS|Camera")
	void SetViewPreset(EMSCameraViewPreset NewPreset);

	UFUNCTION(BlueprintPure, Category = "MS|Camera")
	EMSCameraViewPreset GetViewPreset() const { return ViewPreset; }

	//~ Begin UCameraComponent Interface
	/** カメラマネージャ更新時に呼ばれる。ここでFOVを目標値へ補間する(冒頭コメント参照) */
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
	//~ End UCameraComponent Interface

protected:
	/** 通常時(非スコープ)FOV 度。データ精度方針により厳密一致不要(近似60°) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Camera",
		meta = (ClampMin = "1.0", ClampMax = "170.0"))
	float DefaultFieldOfView = 60.0f;

	/** FOV遷移速度(FInterpTo補間速度。Unity: scopeZoomSpeed=12f) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Camera", meta = (ClampMin = "0.1"))
	float ScopeZoomSpeed = 12.0f;

private:
	/** スコープ中フラグ */
	bool bScoped = false;

	/** スコープ時FOV 度(SetScopedで武器データ値を受領。既定はUnity: scopedFieldOfView=12) */
	float ScopedFieldOfView = 12.0f;

	/** スコープ時感度倍率(既定はUnity: scopedSensitivityMultiplier=0.35) */
	float ScopedSensitivityMultiplier = 0.35f;

	EMSCameraViewPreset ViewPreset = EMSCameraViewPreset::RightShoulder;
};
