// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSDamageCameraShake.h"

void UMSDamageCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(DurationSeconds);
	OutInfo.BlendIn = 0.025f;
	OutInfo.BlendOut = 0.12f;
}

void UMSDamageCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams&)
{
	ElapsedSeconds = 0.0f;
}

void UMSDamageCameraShakePattern::UpdateShakePatternImpl(
	const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedSeconds += Params.DeltaTime;
	Evaluate(ElapsedSeconds, OutResult);
}

void UMSDamageCameraShakePattern::ScrubShakePatternImpl(
	const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedSeconds = Params.AbsoluteTime;
	Evaluate(ElapsedSeconds, OutResult);
}

bool UMSDamageCameraShakePattern::IsFinishedImpl() const
{
	return ElapsedSeconds >= DurationSeconds;
}

void UMSDamageCameraShakePattern::Evaluate(
	const float TimeSeconds, FCameraShakePatternUpdateResult& OutResult) const
{
	const float Alpha = FMath::Clamp(TimeSeconds / DurationSeconds, 0.0f, 1.0f);
	const float Envelope = FMath::Square(1.0f - Alpha);
	const float FastPhase = TimeSeconds * UE_TWO_PI * 17.0f;
	const float SlowPhase = TimeSeconds * UE_TWO_PI * 11.0f;

	// Intentionally restrained: the play call supplies damage severity as the outer scale.
	OutResult.Location = FVector(
		FMath::Sin(FastPhase) * 5.0f,
		FMath::Cos(SlowPhase) * 8.0f,
		FMath::Sin(SlowPhase * 1.31f) * 5.0f) * Envelope;
	OutResult.Rotation = FRotator(
		FMath::Sin(SlowPhase) * 0.8f,
		FMath::Sin(FastPhase) * 1.15f,
		FMath::Cos(FastPhase * 0.73f) * 0.55f) * Envelope;
}

UMSDamageCameraShake::UMSDamageCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;
	UMSDamageCameraShakePattern* Pattern =
		ObjectInitializer.CreateDefaultSubobject<UMSDamageCameraShakePattern>(
			this, TEXT("ReceivedDamagePattern"));
	SetRootShakePattern(Pattern);
}
