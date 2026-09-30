// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/MSScale.h"
#include "Player/MSCharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMovementReverseInputResponseTest,
	"NeoMSGO.Player.Movement.ReverseInputResponse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSMovementReverseInputResponseTest::RunTest(const FString& Parameters)
{
	const UMSCharacterMovementComponent* Movement = GetDefault<UMSCharacterMovementComponent>();
	// 体感優先チューニング: 機体比60 m/s^2 (Unity 40 m/s^2 より機敏な左右切り返し)。
	const float ExpectedAcceleration = 6000.0f * MSGO::MSScale;
	TestEqual(TEXT("18 m world acceleration uses feel-first 60 m/s^2 body-relative response"),
		Movement->MaxAcceleration, ExpectedAcceleration);
	TestEqual(TEXT("walking braking matches acceleration for immediate opposite-input response"),
		Movement->BrakingDecelerationWalking, ExpectedAcceleration);
	TestEqual(TEXT("releasing direction in the air preserves horizontal momentum"),
		Movement->BrakingDecelerationFalling, 0.0f);

	// Both speed and acceleration carry MSScale, so it cancels: 14 / 60 ≈ 0.233 s.
	// The old unscaled 4000 cm/s^2 value took ~3.26 s at scaled boost speed.
	const float ScaledDefaultBoostSpeed = 1400.0f * MSGO::MSScale;
	const float SecondsToStopBeforeReverse =
		ScaledDefaultBoostSpeed / Movement->BrakingDecelerationWalking;
	TestTrue(TEXT("opposite input brakes default boost momentum within 0.24 seconds"),
		SecondsToStopBeforeReverse <= (1400.0f / 6000.0f) + KINDA_SMALL_NUMBER);

	// 降下の体感: 重力は機体比-20 m/s^2 (=-2000 cm/s^2 × MSScale)。GravityScaleで表現。
	// 標準版へ一度戻したが「重力が小さくなった」と再指摘(2026-07-19)があり、MSScale版へ再度戻した。
	TestEqual(TEXT("gravity scale restores body-relative -20 m/s^2 fall in the 18 m world"),
		Movement->GravityScale, 2000.0f * MSGO::MSScale / 980.0f, 0.001f);
	return true;
}

#endif
