// Copyright NeoMSGO Project. All Rights Reserved.
#include "Combat/Cues/MSGameplayCue_KillConfirm.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Core/MSGameplayTags.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UMSGameplayCue_KillConfirm::UMSGameplayCue_KillConfirm()
{
	GameplayCueTag = MSTags::Cue_Kill_Confirm;
	GameplayCueName = GameplayCueTag.GetTagName();
	static ConstructorHelpers::FObjectFinder<USoundBase> KillConfirmFinder(
		TEXT("/Game/Audio/Imported/SFX_KillConfirm_Real.SFX_KillConfirm_Real"));
	KillConfirmSound = KillConfirmFinder.Object;
}
bool UMSGameplayCue_KillConfirm::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	MSAudio::PlayMetaSound2DIfLocallyControlled(
		Parameters.Instigator.Get(), MSAudioPreset::Resolve(EMSAudioUsage::KillConfirm, KillConfirmSound),
		1000.0f, 0.40f, 0.32f);
	return MyTarget != nullptr;
}
