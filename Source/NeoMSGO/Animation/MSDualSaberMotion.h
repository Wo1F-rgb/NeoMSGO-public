// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once
#include "Animation/MSStandardSaberMotion.h"

/** Gundam dual sabers. Directions/stages are wiki backed; N and D share this motion.
 * All seconds and travel distances below are NeoMSGO tuning, not measured source frames.
 */
class NEOMSGO_API FMSDualSaberMotion
{
public:
	static FMSStandardSaberTiming GetTiming(int32 AttackIndex);
	static FMSStandardSaberPose Sample(int32 AttackIndex, float TimeSeconds);
	static bool IsHandActive(int32 AttackIndex, bool bLeft);
};
