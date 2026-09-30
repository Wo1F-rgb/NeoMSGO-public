// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSCameraComponent.h"

#include "Data/MSWeaponData.h"
#include "Core/MSScale.h"
#include "GameFramework/SpringArmComponent.h"

UMSCameraComponent::UMSCameraComponent()
{
	// 起動時は通常FOVから開始(GetCameraView内の補間はこの値を起点にする)
	FieldOfView = DefaultFieldOfView;
}

void UMSCameraComponent::SetScoped(bool bInScoped, float InScopedFieldOfView, float InScopedSensitivityMultiplier)
{
	bScoped = bInScoped;

	if (bInScoped)
	{
		// ON時のみパラメータを受領(OFF時の引数は無視=直前のスコープ武器値を保持)。
		// クランプ幅はUMSWeaponData側のmeta(ClampMin/Max)と一致させる。
		ScopedFieldOfView = FMath::Clamp(InScopedFieldOfView, 1.0f, 170.0f);
		ScopedSensitivityMultiplier = FMath::Clamp(InScopedSensitivityMultiplier, 0.01f, 1.0f);
	}
	// FOVの実遷移はGetCameraView()での補間に委ねる(即時書き込みしない。ヘッダ冒頭コメント参照)
}

void UMSCameraComponent::SetScopedFromWeapon(const UMSRangedWeaponData* WeaponData, bool bInScoped)
{
	if (bInScoped && WeaponData && WeaponData->bIsScopeWeapon)
	{
		SetScoped(true, WeaponData->ScopedFieldOfView, WeaponData->ScopedSensitivityMultiplier);
	}
	else
	{
		// 非スコープ武器への持ち替え・null武器はスコープ解除として扱う
		SetScoped(false, 0.0f, 0.0f);
	}
}

float UMSCameraComponent::GetLookSensitivityMultiplier() const
{
	return bScoped ? ScopedSensitivityMultiplier : 1.0f;
}

void UMSCameraComponent::SetDefaultFieldOfView(const float NewFieldOfView)
{
	DefaultFieldOfView = FMath::Clamp(NewFieldOfView, 60.0f, 110.0f);
	// Settings feedback must be visible immediately. Scoped view remains controlled by weapon data.
	if (!bScoped)
	{
		FieldOfView = DefaultFieldOfView;
	}
}

void UMSCameraComponent::SetViewPreset(const EMSCameraViewPreset NewPreset)
{
	ViewPreset = NewPreset;
	if (USpringArmComponent* Boom = Cast<USpringArmComponent>(GetAttachParent()))
	{
		if (ViewPreset == EMSCameraViewPreset::HighOverhead)
		{
			// 実プレイ指摘により高さを下げた(旧: ArmLength=520/OffsetZ=185)
			Boom->TargetArmLength = 380.0f * MSGO::MSScale;
			Boom->SocketOffset = FVector(-20.0f, 0.0f, 110.0f) * MSGO::MSScale;
		}
		else
		{
			Boom->TargetArmLength = 450.0f * MSGO::MSScale;
			Boom->SocketOffset = FVector(0.0f, 55.0f, 20.0f) * MSGO::MSScale;
		}
	}
}

void UMSCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	// カメラマネージャ(APlayerCameraManager::UpdateCamera)から毎フレーム呼ばれる。
	// ここで補間したFieldOfViewをSuperがDesiredViewへ反映するため、
	// 後段で上書きされない安定したFOV遷移になる(MIGRATION_NOTES 表#5対策)。
	const float TargetFOV = bScoped ? ScopedFieldOfView : DefaultFieldOfView;
	FieldOfView = FMath::FInterpTo(FieldOfView, TargetFOV, DeltaTime, ScopeZoomSpeed);

	Super::GetCameraView(DeltaTime, DesiredView);
}
