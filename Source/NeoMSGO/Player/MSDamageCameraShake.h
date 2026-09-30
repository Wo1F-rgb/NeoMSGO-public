// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "Camera/CameraShakeBase.h"
#include "MSDamageCameraShake.generated.h"

/** Short, single-instance shake used by local received-damage feedback. */
UCLASS()
class NEOMSGO_API UMSDamageCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(
		const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(
		const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	void Evaluate(float TimeSeconds, FCameraShakePatternUpdateResult& OutResult) const;

	float ElapsedSeconds = 0.0f;
	static constexpr float DurationSeconds = 0.32f;
};

/**
 * Received-damage camera shake.
 * bSingleInstance prevents automatic fire from stacking shakes into an unusable view.
 */
UCLASS()
class NEOMSGO_API UMSDamageCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UMSDamageCameraShake(const FObjectInitializer& ObjectInitializer);
};
