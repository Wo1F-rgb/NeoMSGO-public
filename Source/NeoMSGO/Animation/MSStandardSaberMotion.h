// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** GM N: right diagonal cut, returning horizontal cut, and dash horizontal cut.
 * Directions/stage count are reference-backed; seconds/distances are tuning values,
 * not measurements from the original game. See docs/Animation/BeamSaberWiki20260920.md.
 */
struct FMSStandardSaberTiming
{
	float ActiveStart, ActiveEnd, Duration;
	float LungeStart, LungeEnd, LungeDistanceCm;
};

struct FMSStandardSaberPose
{
	FQuat Torso = FQuat::Identity;
	FQuat UpperArm = FQuat::Identity;
	FQuat Forearm = FQuat::Identity;
	FQuat WeaponMount = FQuat::Identity;
	FQuat UpperArmLeft = FQuat::Identity;
	FQuat ForearmLeft = FQuat::Identity;
	FQuat WeaponMountLeft = FQuat::Identity;
	bool bDual = false;
	float BodyYaw = 0.0f;
	float ThighLeft = 0.0f, ThighRight = 0.0f;
	float KneeLeft = 0.0f, KneeRight = 0.0f;
};

class NEOMSGO_API FMSStandardSaberMotion
{
public:
	// Wire IDs preserve GM's 0/1/2 and distinguish dual stages on the existing multicast.
	static constexpr int32 DualFirst = 10;
	static constexpr int32 DualLast = 13;
	static constexpr int32 DualDash = 14;
	static constexpr int32 DualReady = 15;
	static bool IsDual(int32 Index) { return Index >= DualFirst && Index <= DualReady; }
	static bool HasFollowUp(int32 Index) { return Index == 0 || (Index >= DualFirst && Index < DualLast); }
	static bool IsAttack(int32 Index) { return (Index >= 0 && Index <= 2) || (Index >= DualFirst && Index <= DualDash); }
	static FMSStandardSaberTiming GetTiming(int32 AttackIndex);
	/** -1 is the ready stance. Other indices: 0 first, 1 follow-up, 2 dash. */
	static FMSStandardSaberPose Sample(int32 AttackIndex, float TimeSeconds);
	/** Analytic blade segment in unscaled ghost RigRoot centimetres. */
	static void SampleBlade(int32 AttackIndex, float TimeSeconds, float BladeLengthCm,
		FVector& OutStart, FVector& OutEnd, bool bLeft = false);
};
