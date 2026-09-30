// Copyright NeoMSGO Project. All Rights Reserved.
//
// 占領拠点。設計元: docs/UE5_Architecture.md §4.3
//
// P0-4時点では EMSCaptureOwner(拠点の「所有状態」型)のみを定義する。
// AMSCapturePoint 本体(Progress ∈ [-1,+1] 等)は P7-1 で本ヘッダに追加する。
//
// 【禁止事項】EMSCaptureOwner を FF判定・ロック判定に直接使うことは禁止(B-2)。
// 陣営システムとの接続は UMSTeamStatics::CaptureOwnerToTeamId / TeamIdToCaptureOwner のみ。
// Unity版の「CapturePoint.Owner enum を陣営表現に流用」した設計に起因する混同を
// 型レベルで再発防止するための分離である。

#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Actor.h"
#include "MSCapturePoint.generated.h"

/** 拠点の所有状態。ユニットの敵味方(FGenericTeamId)とは別の型 */
UENUM(BlueprintType)
enum class EMSCaptureOwner : uint8
{
	Neutral,
	TeamPlayer,
	TeamEnemy
};

class AMSCapturePoint;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCaptureOwnerChanged, AMSCapturePoint*, CapturePoint,
	EMSCaptureOwner, NewOwner);

/** Replicated, non-lockable numeric capture point. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSCapturePoint : public AActor
{
	GENERATED_BODY()

public:
	AMSCapturePoint();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** State-only capture update. Unit discovery intentionally lives in Tick(). */
	UFUNCTION(BlueprintCallable, Category="MS|Capture")
	void TickCaptureLogic(int32 AllyCount, int32 EnemyCount, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	static float CalculateCaptureSpeedMultiplier(int32 CapturerCount, int32 EffectiveCapturerLimit = 3);

	UFUNCTION(BlueprintCallable, Category="MS|Capture")
	void SetFixedOwner(EMSCaptureOwner NewOwner);

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	virtual bool IsCaptureLocked() const { return false; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	FGenericTeamId GetOwnerTeamId() const;

	/** Deployment policy belongs to the gameplay point, never to map presentation code. */
	UFUNCTION(BlueprintPure, Category="MS|Capture")
	virtual bool CanDeployFor(FGenericTeamId TeamId) const;

	/** Finds the nearest deployable point in the ground plane. OutDistance2D is MAX_flt when none exists. */
	static AMSCapturePoint* FindNearestDeployablePoint(UWorld* World, FGenericTeamId TeamId,
		const FVector& WorldLocation, float& OutDistance2D);

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	float GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	float GetCaptureRadius() const { return CaptureRadius; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	UStaticMeshComponent* GetRangeRing() const { return RangeRing; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	EMSCaptureOwner GetCurrentOwner() const { return CurrentOwner; }

	/** Player-facing identifier used by map markers and capture UI. */
	UFUNCTION(BlueprintPure, Category="MS|Capture")
	FText GetPointName() const { return PointName; }

	UFUNCTION(BlueprintCallable, Category="MS|Capture")
	void SetPointName(const FText& NewPointName) { PointName = NewPointName; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	bool IsLocalPlayerInRange() const { return LastAllyCount > 0; }

	UFUNCTION(BlueprintPure, Category="MS|Capture")
	bool IsEnemyInRange() const { return LastEnemyCount > 0; }

	UFUNCTION(BlueprintNativeEvent, Category="MS|Capture")
	void UpdateVisuals();
	virtual void UpdateVisuals_Implementation();

	UPROPERTY(BlueprintAssignable, Category="MS|Capture")
	FOnCaptureOwnerChanged OnCaptureOwnerChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Capture", meta=(ClampMin="0.0", Units="cm"))
	float CaptureRadius = 800.0f * MSGO::MSScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Capture", meta=(ClampMin="0.0"))
	float CaptureRate = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Capture", meta=(ClampMin="0.0"))
	float DecayRate = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Capture", meta=(ClampMin="1"))
	int32 MaxEffectiveCapturers = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|Capture")
	FText PointName = NSLOCTEXT("NeoMSGO", "DefaultCapturePointName", "拠点");

	UPROPERTY(ReplicatedUsing=OnRep_Progress, VisibleAnywhere, BlueprintReadOnly, Category="MS|Capture")
	float Progress = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_CurrentOwner, VisibleAnywhere, BlueprintReadOnly, Category="MS|Capture")
	EMSCaptureOwner CurrentOwner = EMSCaptureOwner::Neutral;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Visual")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Visual")
	TObjectPtr<UStaticMeshComponent> FlagPole;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Visual")
	TObjectPtr<UStaticMeshComponent> Flag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Visual")
	TObjectPtr<UStaticMeshComponent> RangeRing;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<class UInstancedStaticMeshComponent> RangeOutline;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<class UTextRenderComponent> PointLabel;

private:
	UFUNCTION()
	void OnRep_Progress();

	UFUNCTION()
	void OnRep_CurrentOwner(EMSCaptureOwner PreviousOwner);

	void SetOwnerAndNotify(EMSCaptureOwner NewOwner);
	void UpdateRangeRingGeometry();
	void ApplyVisualColor(UStaticMeshComponent* Component, UMaterialInstanceDynamic*& Material,
		const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlagMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	int32 LastAllyCount = 0;
	int32 LastEnemyCount = 0;
	bool bProgressChangedThisFrame = false;
};
