#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "MSMapCaptureSubsystem.generated.h"

class AMSMapBoundsActor;
class ASceneCapture2D;
class AStaticMeshActor;
class UTextureRenderTarget2D;
class UTexture2D;

/** Owns the level-wide, one-shot orthographic image used by full-map widgets. */
UCLASS()
class NEOMSGO_API UMSMapCaptureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="MS|Map")
	UTextureRenderTarget2D* GetOverviewTexture();

	/** Lightweight authored layer; no extra scene capture or per-frame spawning. */
	UFUNCTION(BlueprintPure, Category="MS|Map")
	UTexture2D* GetUndergroundOverviewTexture() const;
	UFUNCTION(BlueprintPure, Category="MS|Map")
	bool ShouldUseUndergroundLayer(const FVector& WorldLocation, bool bWasUnderground) const;

	UFUNCTION(BlueprintPure, Category="MS|Map")
	FVector GetMapWorldCenter() const;

	UFUNCTION(BlueprintPure, Category="MS|Map")
	float GetMapWorldHalfExtent() const;
	FVector2D GetMapWorldHalfExtents() const;

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetCaptureRequestCountForTesting() const { return CaptureRequestCount; }
	const ASceneCapture2D* GetCaptureActorForTesting() const { return CaptureActor; }
	void ConfigureOverviewWaterForTesting(class USceneCaptureComponent2D* Capture)
	{ ConfigureOverviewWater(Capture); }
	int32 GetOverviewWaterProxyCountForTesting() const { return OverviewWaterProxies.Num(); }
#endif

private:
	void ResolveBounds() const;
	bool AreMapResourcesReady() const;
	void ScheduleWarmupCapture();
	bool TickWarmup(float DeltaTime);
	bool CaptureOverview();
	void ConfigureOverviewWater(class USceneCaptureComponent2D* Capture);

	/** レベルにAMSMapBoundsActorが無い/狭すぎる場合に、実コンテンツから実測した中心と半幅。 */
	mutable FVector MeasuredCenter = FVector::ZeroVector;
	mutable float MeasuredHalfExtent = 0.0f;
	mutable FVector2D MeasuredHalfExtents = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> OverviewTexture;

	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> CaptureActor;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AStaticMeshActor>> OverviewWaterProxies;
	TMap<TWeakObjectPtr<AStaticMeshActor>, TWeakObjectPtr<AStaticMeshActor>> OverviewWaterProxyBySource;

	UPROPERTY(Transient)
	mutable TObjectPtr<AMSMapBoundsActor> BoundsActor;

	mutable bool bBoundsResolved = false;
	bool bCaptureAttempted = false;
	bool bCaptureCompleted = false;
	bool bWarmupActive = false;
	bool bFallbackCaptureAttempted = false;
	int32 WarmupAttemptCount = 0;
	double NextReadinessCheckTimeSeconds = 0.0;
	int32 OverviewCaptureCount = 0;
	FTSTicker::FDelegateHandle WarmupTickerHandle;
	int32 CaptureRequestCount = 0;
};
