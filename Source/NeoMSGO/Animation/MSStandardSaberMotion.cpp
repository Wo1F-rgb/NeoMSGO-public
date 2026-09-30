// Copyright NeoMSGO Project. All Rights Reserved.
#include "Animation/MSStandardSaberMotion.h"
#include "Animation/MSDualSaberMotion.h"

namespace
{
	struct FStandardSaberKey
	{
		float T, BodyYaw, Lean, ArmPitch, ArmYaw, Elbow, BladePitch, BladeYaw;
		float ThighL, ThighR, KneeL, KneeR;
	};
	// The wrist follows the cutting plane, rather than inheriting the old gun-level
	// pitch cancellation. Intermediate keys keep a >180 degree sweep on its intended side.
	constexpr FStandardSaberKey Ready{0, 0, 0, 12, 8, 28, 48, 12, 0, 0, 0, 0};
	constexpr FStandardSaberKey First[] = {
		Ready,
		{.14f, 24, 3, 100, 42, 24, 66, 48, 10, -8, 14, 8},
		{.23f, 0, 8, 70, 0, 18, 14, 5, 18, -14, 22, 12},
		{.32f, -28, 10, 38, -56, 20, -34, -76, 22, -16, 26, 10},
		{.43f, -22, 7, 36, -48, 26, -28, -70, 18, -12, 22, 10},
		{.70f, 0, 0, 12, 8, 28, 48, 12, 0, 0, 0, 0}};
	constexpr FStandardSaberKey Second[] = {
		Ready,
		{.10f, -28, 3, 58, -60, 38, 5, -115, -8, 12, 10, 16},
		{.20f, -2, 6, 60, -6, 16, 2, -2, -14, 20, 12, 22},
		{.30f, 30, 7, 60, 60, 22, 8, 98, -16, 24, 12, 26},
		{.43f, 25, 4, 50, 55, 28, 16, 90, -12, 18, 10, 20},
		{.70f, 0, 0, 12, 8, 28, 48, 12, 0, 0, 0, 0}};
	constexpr FStandardSaberKey Dash[] = {
		Ready,
		{.12f, -36, 13, 52, -62, 40, 0, -120, -18, 32, 24, 34},
		{.22f, -3, 15, 60, -4, 14, 0, -2, -22, 38, 28, 40},
		{.32f, 38, 12, 56, 65, 18, 5, 108, -20, 34, 26, 38},
		{.51f, 28, 7, 48, 56, 26, 14, 96, -12, 22, 18, 28},
		{.85f, 0, 0, 12, 8, 28, 48, 12, 0, 0, 0, 0}};

	FMSStandardSaberPose MakePose(const FStandardSaberKey& K)
	{
		FMSStandardSaberPose P;
		P.BodyYaw = K.BodyYaw;
		P.Torso = FRotator(-K.Lean, K.BodyYaw, 0).Quaternion();
		// Flat rig root joints use absolute RigRoot-space orientation. Elbow/wrist are local.
		P.UpperArm = FRotator(K.ArmPitch, K.ArmYaw, 0).Quaternion();
		P.Forearm = FRotator(K.Elbow, 0, 0).Quaternion();
		const FQuat Blade = FRotator(K.BladePitch, K.BladeYaw, 0).Quaternion();
		P.WeaponMount = (P.UpperArm * P.Forearm).Inverse() * Blade;
		P.ThighLeft = K.ThighL; P.ThighRight = K.ThighR;
		P.KneeLeft = K.KneeL; P.KneeRight = K.KneeR;
		return P;
	}
}

FMSStandardSaberTiming FMSStandardSaberMotion::GetTiming(const int32 AttackIndex)
{
	if (IsDual(AttackIndex)) return FMSDualSaberMotion::GetTiming(AttackIndex);
	if (AttackIndex == 2) return {.12f, .32f, .85f, 0, .32f, 250};
	if (AttackIndex == 1) return {.10f, .30f, .70f, .04f, .22f, 20};
	return {.14f, .32f, .70f, .06f, .24f, 25};
}

FMSStandardSaberPose FMSStandardSaberMotion::Sample(const int32 AttackIndex, const float TimeSeconds)
{
	if (IsDual(AttackIndex)) return FMSDualSaberMotion::Sample(AttackIndex, TimeSeconds);
	if (AttackIndex < 0 || AttackIndex > 2) return MakePose(Ready);
	const FStandardSaberKey* Keys = AttackIndex == 2 ? Dash : AttackIndex == 1 ? Second : First;
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, Keys[5].T);
	int32 Next = 1;
	while (Next < 5 && Time > Keys[Next].T) ++Next;
	const FStandardSaberKey& A = Keys[Next - 1];
	const FStandardSaberKey& B = Keys[Next];
	float Alpha = FMath::Clamp((Time - A.T) / (B.T - A.T), 0.0f, 1.0f);
	// Constant arc progression during the hit window; eased anticipation and return.
	if (Next == 1 || Next >= 4) Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);
	FStandardSaberKey K = A;
#define MS_SABER_LERP(Field) K.Field = FMath::Lerp(A.Field, B.Field, Alpha)
	MS_SABER_LERP(BodyYaw); MS_SABER_LERP(Lean);
	MS_SABER_LERP(ArmPitch); MS_SABER_LERP(ArmYaw); MS_SABER_LERP(Elbow);
	MS_SABER_LERP(BladePitch); MS_SABER_LERP(BladeYaw);
	MS_SABER_LERP(ThighL); MS_SABER_LERP(ThighR); MS_SABER_LERP(KneeL); MS_SABER_LERP(KneeR);
#undef MS_SABER_LERP
	return MakePose(K);
}

void FMSStandardSaberMotion::SampleBlade(const int32 AttackIndex, const float TimeSeconds,
	const float BladeLengthCm, FVector& OutStart, FVector& OutEnd, const bool bLeft)
{
	const FMSStandardSaberPose Pose = Sample(AttackIndex, TimeSeconds);
	const FQuat Body = FRotator(0, Pose.BodyYaw, 0).Quaternion();
	const FQuat Arm = bLeft ? Pose.UpperArmLeft : Pose.UpperArm;
	const FVector Shoulder = Body.RotateVector(FVector(0, bLeft ? -42 : 42, 45));
	const FVector Elbow = Shoulder + Arm.RotateVector(FVector(0, 0, -32));
	const FQuat Forearm = Arm * (bLeft ? Pose.ForearmLeft : Pose.Forearm);
	OutStart = Elbow + Forearm.RotateVector(FVector(15, 0, -28));
	const FQuat Blade = Forearm * (bLeft ? Pose.WeaponMountLeft : Pose.WeaponMount);
	OutEnd = OutStart + Blade.RotateVector(FVector(FMath::Max(0.0f, BladeLengthCm), 0, 0));
}
