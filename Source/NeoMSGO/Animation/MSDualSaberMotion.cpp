// Copyright NeoMSGO Project. All Rights Reserved.
#include "Animation/MSDualSaberMotion.h"
#include "Core/MSScale.h"

namespace
{
	struct FHandKey { float ArmPitch, ArmYaw, Elbow, BladePitch, BladeYaw; };
	struct FDualKey { float BodyYaw, Lean; FHandKey R, L; float Stride, Knee; };
	constexpr FHandKey ReadyR{18, 22, 32, 42, 32}, ReadyL{18, -22, 32, 42, -32};
	constexpr FDualKey Ready{0, 0, ReadyR, ReadyL, 0, 0};
	// Blade angles are absolute RigRoot-space. +Y is the pilot's right, +Z up.
	// Each arc has an explicit midpoint, preserving the intended full turn on D.
	constexpr FDualKey Cuts[5][3] = {
		{{-24, 5, {26,-52,32,-42,-64}, ReadyL, 14,18},
		 {0,8, {62,0,18,8,0}, ReadyL, 22,26},
		 {28,6, {104,52,22,58,64}, ReadyL, 18,22}},
		{{24,5, ReadyR, {26,52,32,-42,64}, -14,18},
		 {0,8, ReadyR, {62,0,18,8,0}, -22,26},
		 {-28,6, ReadyR, {104,-52,22,58,-64}, -18,22}},
		{{0,6, {58,68,30,8,92}, {58,-68,30,8,-92}, 10,20},
		 {0,10, {66,32,18,4,38}, {66,-32,18,4,-38}, 16,28},
		 {0,8, {68,-8,34,12,-12}, {68,8,34,12,12}, 12,24}},
		{{0,7, {28,-30,34,-38,-48}, {28,30,34,-38,48}, -10,20},
		 {0,10, {64,20,18,10,30}, {64,-20,18,10,-30}, -16,28},
		 {0,5, {106,62,26,58,78}, {106,-62,26,58,-78}, -12,24}},
		{{-48,12, {64,15,24,0,28}, {64,-145,24,0,-152}, 28,32},
		 {132,14, {64,195,20,0,208}, {64,35,20,0,28}, -20,38},
		 {312,9, {64,375,26,0,388}, {64,215,26,0,208}, 22,30}}
	};
	FHandKey LerpHand(const FHandKey& A, const FHandKey& B, float Alpha)
	{
		return {FMath::Lerp(A.ArmPitch,B.ArmPitch,Alpha), FMath::Lerp(A.ArmYaw,B.ArmYaw,Alpha),
			FMath::Lerp(A.Elbow,B.Elbow,Alpha), FMath::Lerp(A.BladePitch,B.BladePitch,Alpha),
			FMath::Lerp(A.BladeYaw,B.BladeYaw,Alpha)};
	}
}

FMSStandardSaberTiming FMSDualSaberMotion::GetTiming(int32 AttackIndex)
{
	// User-requested forward travel tuning, not a measured original-game N distance.
	// Timing stores design-body cm; the authority consumer applies MSScale exactly once.
	if (AttackIndex == FMSStandardSaberMotion::DualDash) return {.14f,.42f,.90f,0,.38f,6000.0f / MSGO::MSScale};
	const float WorldDistanceCm = AttackIndex == FMSStandardSaberMotion::DualFirst ? 400.0f : 200.0f;
	return {.14f,.34f,.70f,.06f,.26f, WorldDistanceCm / MSGO::MSScale};
}

bool FMSDualSaberMotion::IsHandActive(int32 AttackIndex, bool bLeft)
{
	if (AttackIndex == FMSStandardSaberMotion::DualFirst) return !bLeft;
	if (AttackIndex == FMSStandardSaberMotion::DualFirst + 1) return bLeft;
	return AttackIndex >= FMSStandardSaberMotion::DualFirst + 2 && AttackIndex <= FMSStandardSaberMotion::DualDash;
}

FMSStandardSaberPose FMSDualSaberMotion::Sample(int32 AttackIndex, float TimeSeconds)
{
	const int32 Stage = AttackIndex - FMSStandardSaberMotion::DualFirst;
	FDualKey K = Ready;
	if (Stage >= 0 && Stage < 5)
	{
		const auto Timing = GetTiming(AttackIndex);
		const float Mid = (Timing.ActiveStart + Timing.ActiveEnd) * .5f;
		const float Hold = Timing.ActiveEnd + .10f;
		const FDualKey* A = &Ready;
		const FDualKey* B = &Cuts[Stage][0];
		float From = 0, To = Timing.ActiveStart;
		if (TimeSeconds >= Hold) { A = &Cuts[Stage][2]; B = &Ready; From = Hold; To = Timing.Duration; }
		else if (TimeSeconds >= Timing.ActiveEnd) { A = B = &Cuts[Stage][2]; From = Timing.ActiveEnd; To = Hold; }
		else if (TimeSeconds >= Mid) { A = &Cuts[Stage][1]; B = &Cuts[Stage][2]; From = Mid; To = Timing.ActiveEnd; }
		else if (TimeSeconds >= Timing.ActiveStart) { A = &Cuts[Stage][0]; B = &Cuts[Stage][1]; From = Timing.ActiveStart; To = Mid; }
		float Alpha = FMath::Clamp((TimeSeconds - From) / (To - From),0.0f,1.0f);
		if (From == 0 || From == Hold) Alpha = FMath::SmoothStep(0.0f,1.0f,Alpha);
		K = {FMath::Lerp(A->BodyYaw,B->BodyYaw,Alpha), FMath::Lerp(A->Lean,B->Lean,Alpha),
			LerpHand(A->R,B->R,Alpha), LerpHand(A->L,B->L,Alpha),
			FMath::Lerp(A->Stride,B->Stride,Alpha), FMath::Lerp(A->Knee,B->Knee,Alpha)};
		// Recover the full clockwise spin by the shortest remaining 48 degrees.
		if (Stage == 4 && From == Hold)
		{
			K.BodyYaw = FMath::Lerp(A->BodyYaw,360.0f,Alpha);
			K.R.ArmYaw = FMath::Lerp(A->R.ArmYaw,ReadyR.ArmYaw + 360.0f,Alpha);
			K.L.ArmYaw = FMath::Lerp(A->L.ArmYaw,ReadyL.ArmYaw + 360.0f,Alpha);
			K.R.BladeYaw = FMath::Lerp(A->R.BladeYaw,ReadyR.BladeYaw + 360.0f,Alpha);
			K.L.BladeYaw = FMath::Lerp(A->L.BladeYaw,ReadyL.BladeYaw + 360.0f,Alpha);
		}
	}
	FMSStandardSaberPose P;
	P.bDual = true; P.BodyYaw = K.BodyYaw;
	P.Torso = FRotator(-K.Lean,K.BodyYaw,0).Quaternion();
	auto Hand = [](const FHandKey& H, FQuat& Arm, FQuat& Forearm, FQuat& Mount)
	{
		Arm = FRotator(H.ArmPitch,H.ArmYaw,0).Quaternion();
		Forearm = FRotator(H.Elbow,0,0).Quaternion();
		Mount = (Arm * Forearm).Inverse() * FRotator(H.BladePitch,H.BladeYaw,0).Quaternion();
	};
	Hand(K.R,P.UpperArm,P.Forearm,P.WeaponMount);
	Hand(K.L,P.UpperArmLeft,P.ForearmLeft,P.WeaponMountLeft);
	P.ThighLeft = K.Stride; P.ThighRight = -K.Stride * .7f;
	P.KneeLeft = K.Knee; P.KneeRight = K.Knee * .8f;
	return P;
}
