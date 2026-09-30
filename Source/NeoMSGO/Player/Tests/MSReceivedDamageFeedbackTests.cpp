// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/MSDamageCameraShake.h"
#include "Player/MSDamageFeedbackWidget.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSReceivedDamageFeedbackTest,
	"NeoMSGO.Player.Feedback.ReceivedDamage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSReceivedDamageFeedbackTest::RunTest(const FString&)
{
	TestTrue(TEXT("small hits remain visible without reaching full strength"),
		FMath::IsWithinInclusive(
			AMSPlayerCharacter::ComputeReceivedDamageFeedbackSeverity(10.0f, 1000.0f),
			0.22f, 0.5f));
	TestEqual(TEXT("large hits clamp feedback strength"),
		AMSPlayerCharacter::ComputeReceivedDamageFeedbackSeverity(1000.0f, 1000.0f), 1.0f);

	const FVector2D Front = AMSPlayerCharacter::ComputeReceivedDamageScreenDirection(
		FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	const FVector2D Right = AMSPlayerCharacter::ComputeReceivedDamageScreenDirection(
		FVector::ZeroVector, FVector(0.0f, 100.0f, 0.0f), FRotator::ZeroRotator);
	TestTrue(TEXT("front damage points indicator upward"), Front.Equals(FVector2D(0.0f, -1.0f), 0.01f));
	TestTrue(TEXT("right damage points indicator right"), Right.Equals(FVector2D(1.0f, 0.0f), 0.01f));

	const UMSDamageCameraShake* Shake = GetDefault<UMSDamageCameraShake>();
	TestTrue(TEXT("received-damage shake is single-instance to prevent stacking"), Shake->bSingleInstance);
	TestTrue(TEXT("screen-edge flash stays well below opaque"),
		UMSDamageFeedbackWidget::MaximumEdgeOpacity <= 0.35f);
	TestTrue(TEXT("overlay lifetime is short"), UMSDamageFeedbackWidget::FeedbackDurationSeconds < 0.65f);
	return true;
}

#endif
