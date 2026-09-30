// Copyright NeoMSGO Project. All Rights Reserved.

#include "Audio/MSAudioPlayback.h"

#include "NeoMSGO.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
	MSAudio::FRecordedMetaSoundRequest GRecordedMetaSoundRequest;
	MSAudio::FRecordedLoopingMetaSoundState GRecordedLoopingMetaSoundState;

	bool IsRunningAutomationTest()
	{
		return FAutomationTestFramework::Get().GetCurrentTest() != nullptr;
	}
}
#endif

namespace
{
	constexpr int32 MaxConcurrent3DVoicesPerSound = 3;

	USoundAttenuation* GetDefault3DAttenuation()
	{
		static USoundAttenuation* Attenuation = []
		{
			USoundAttenuation* NewAttenuation = NewObject<USoundAttenuation>(
				GetTransientPackage(), NAME_None, RF_Transient);
			// This process-lifetime profile is shared by every gameplay 3D voice. Root it so
			// audio components never retain a settings object that GC has reclaimed.
			NewAttenuation->AddToRoot();

			FSoundAttenuationSettings& Settings = NewAttenuation->Attenuation;
			Settings.bAttenuate = true;
			Settings.bSpatialize = true;
			Settings.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;
			Settings.AttenuationShape = EAttenuationShape::Sphere;
			// World distances grew with the 18 m mobile-suit conversion. Keeping the old
			// 3 m/47 m profile made footsteps and thrusters effectively inaudible beyond
			// a few new-scale body lengths. Preserve the original body-relative falloff.
			Settings.AttenuationShapeExtents = FVector(300.0f * MSGO::MSScale, 0.0f, 0.0f);
			Settings.FalloffDistance = 4700.0f * MSGO::MSScale;
			return NewAttenuation;
		}();
		return Attenuation;
	}

	USoundConcurrency* GetDefault3DConcurrency(USoundBase* Sound)
	{
		// Each authored sound receives its own shared group. This prevents a crowd of units
		// from stacking the same footstep/thruster recording without letting footsteps evict
		// unrelated weapon or charge voices.
		static TMap<TWeakObjectPtr<USoundBase>, USoundConcurrency*> ConcurrencyBySound;
		if (USoundConcurrency** Existing = ConcurrencyBySound.Find(Sound))
		{
			return *Existing;
		}

		USoundConcurrency* NewConcurrency = NewObject<USoundConcurrency>(
			GetTransientPackage(), NAME_None, RF_Transient);
		NewConcurrency->AddToRoot();
		NewConcurrency->Concurrency.MaxCount = MaxConcurrent3DVoicesPerSound;
		NewConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		ConcurrencyBySound.Add(Sound, NewConcurrency);
		return NewConcurrency;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void Record3DVoicePolicy(bool& bHasConcurrency, float& AppliedPitchMultiplier,
		int32& MaxConcurrentVoices, bool& bStopsFarthestThenOldest)
	{
		bHasConcurrency = true;
		AppliedPitchMultiplier = 1.0f;
		MaxConcurrentVoices = MaxConcurrent3DVoicesPerSound;
		bStopsFarthestThenOldest = true;
	}
#endif

	bool HasUsableAudioDevice(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (World && World->GetNetMode() == NM_DedicatedServer) return false;

		// PIE and split-screen worlds can own a device different from the editor's
		// main handle. UGameplayStatics resolves playback from the world context,
		// so checking only GetMainAudioDevice() incorrectly drops valid gameplay
		// voices before SpawnSoundAtLocation is reached.
		const bool bHasWorldDevice = World && World->GetAudioDevice().IsValid();
		const bool bHasMainDevice = GEngine && GEngine->GetMainAudioDevice().IsValid();
		if (!bHasWorldDevice && !bHasMainDevice)
		{
			UE_LOG(LogTemp, Error,
				TEXT("MSAudio: no AudioDevice is available. Remove -nosound and verify the Windows output device."));
			return false;
		}
		return true;
	}
}

UAudioComponent* MSAudio::SpawnMetaSoundAtLocation(
	const UObject* WorldContextObject,
	USoundBase* Sound,
	const FVector& Location,
	const float Frequency,
	const float FallbackVolume,
	const float OneShotDuration)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

#if WITH_DEV_AUTOMATION_TESTS
	// Audio rendering is outside Automation's scope. Record the exact request instead
	// of starting the async MetaSound compiler, whose diagnostics can otherwise arrive
	// during a later test and be attributed to the wrong test case.
	if (IsRunningAutomationTest())
	{
		GRecordedMetaSoundRequest.Sound = Sound;
		GRecordedMetaSoundRequest.Location = Location;
		GRecordedMetaSoundRequest.Frequency = Frequency;
		GRecordedMetaSoundRequest.FallbackVolume = FallbackVolume;
		GRecordedMetaSoundRequest.AppliedVolumeMultiplier = Sound ? 1.0f : FallbackVolume;
		GRecordedMetaSoundRequest.OneShotDuration = OneShotDuration;
		GRecordedMetaSoundRequest.bIs2D = false;
		GRecordedMetaSoundRequest.bHasAttenuation = true;
		GRecordedMetaSoundRequest.AttenuationInnerRadius = 300.0f * MSGO::MSScale;
		GRecordedMetaSoundRequest.AttenuationFalloffDistance = 4700.0f * MSGO::MSScale;
		Record3DVoicePolicy(GRecordedMetaSoundRequest.bHasConcurrency,
			GRecordedMetaSoundRequest.AppliedPitchMultiplier,
			GRecordedMetaSoundRequest.MaxConcurrentVoices,
			GRecordedMetaSoundRequest.bStopsFarthestThenOldest);
		++GRecordedMetaSoundRequest.RequestCount;
		return nullptr;
	}
#endif
	if (!HasUsableAudioDevice(WorldContextObject))
	{
		return nullptr;
	}

	// UE 5.7's SpawnSoundAtLocation overload has no initial FAudioParameter array.
	// MetaSound inputs support live updates, so send Frequency immediately after creation.
	UAudioComponent* AudioComponent = nullptr;
	if (Sound)
	{
		AudioComponent = UGameplayStatics::SpawnSoundAtLocation(
			WorldContextObject, Sound, Location, FRotator::ZeroRotator,
			1.0f, 1.0f, 0.0f, GetDefault3DAttenuation(), GetDefault3DConcurrency(Sound));
		if (AudioComponent)
		{
			AudioComponent->SetFloatParameter(TEXT("Frequency"), Frequency);
			if (Sound->IsA<USoundWave>())
			{
				// Frequency is a legacy MetaSound oscillator input, not the authored sample rate.
				// In particular, the 55 Hz footstep value used to force every imported footstep to
				// 0.45x speed, turning nearby BOT footsteps into a continuous low rumble. FallbackVolume
				// is only for the engine tone when the authored asset is missing; applying it to an
				// imported wave made fire/melee voices play at 16% of their authored loudness.
				AudioComponent->SetVolumeMultiplier(1.0f);
			}
			else
			{
				// The generated one-shot MetaSounds are bare oscillators and otherwise never stop.
				AudioComponent->FadeOut(FMath::Max(OneShotDuration, 0.02f), 0.0f);
			}
			UE_LOG(LogNeoMSGO, Verbose, TEXT("MSAudio one-shot 3D: %s duration=%g authored=%g"),
				*Sound->GetPathName(), OneShotDuration, Sound->GetDuration());
		}
		else
		{
			UE_LOG(LogNeoMSGO, Warning, TEXT("MSAudio failed to spawn 3D sound: %s"), *Sound->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogNeoMSGO, Error, TEXT("MSAudio received a null authored sound; using PCM fallback only."));
	}

	// Keep the engine tone strictly as a missing-asset diagnostic. Co-playing it with a real
	// recording would reintroduce the synthetic beep that the imported audio replaces.
	if (!Sound)
	{
		if (USoundBase* Fallback = GetAudibleFallbackSound(); Fallback && FallbackVolume > 0.0f)
		{
			UGameplayStatics::SpawnSoundAtLocation(
				WorldContextObject, Fallback, Location, FRotator::ZeroRotator,
				FallbackVolume, 1.0f, 0.0f, GetDefault3DAttenuation(),
				GetDefault3DConcurrency(Fallback));
		}
	}
	return AudioComponent;
}

void MSAudio::PlayMetaSound2D(
	const UObject* WorldContextObject,
	USoundBase* Sound,
	const float Frequency,
	const float FallbackVolume,
	const float OneShotDuration)
{
	if (!WorldContextObject)
	{
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (IsRunningAutomationTest())
	{
		GRecordedMetaSoundRequest.Sound = Sound;
		GRecordedMetaSoundRequest.Location = FVector::ZeroVector;
		GRecordedMetaSoundRequest.Frequency = Frequency;
		GRecordedMetaSoundRequest.FallbackVolume = FallbackVolume;
		GRecordedMetaSoundRequest.AppliedVolumeMultiplier = Sound ? 1.0f : FallbackVolume;
		GRecordedMetaSoundRequest.OneShotDuration = OneShotDuration;
		GRecordedMetaSoundRequest.bIs2D = true;
		GRecordedMetaSoundRequest.bHasAttenuation = false;
		GRecordedMetaSoundRequest.bHasConcurrency = false;
		GRecordedMetaSoundRequest.AppliedPitchMultiplier = 1.0f;
		GRecordedMetaSoundRequest.MaxConcurrentVoices = 0;
		GRecordedMetaSoundRequest.bStopsFarthestThenOldest = false;
		++GRecordedMetaSoundRequest.RequestCount;
		return;
	}
#endif
	if (!HasUsableAudioDevice(WorldContextObject))
	{
		return;
	}

	if (Sound)
	{
		if (UAudioComponent* AudioComponent = UGameplayStatics::SpawnSound2D(WorldContextObject, Sound))
		{
			AudioComponent->SetFloatParameter(TEXT("Frequency"), Frequency);
			if (Sound->IsA<USoundWave>())
			{
				// Preserve the pitch authored into imported recordings. Frequency only applies to
				// MetaSound graphs that expose an input with that name.
				// Keep authored UI/combat SoundWave loudness. FallbackVolume is reserved for
				// the diagnostic engine tone played when no authored asset was loaded.
				AudioComponent->SetVolumeMultiplier(1.0f);
			}
			else
			{
				AudioComponent->FadeOut(FMath::Max(OneShotDuration, 0.02f), 0.0f);
			}
			UE_LOG(LogNeoMSGO, Verbose, TEXT("MSAudio one-shot 2D: %s duration=%g authored=%g"),
				*Sound->GetPathName(), OneShotDuration, Sound->GetDuration());
		}
		else
		{
			UE_LOG(LogNeoMSGO, Warning, TEXT("MSAudio failed to spawn 2D sound: %s"), *Sound->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogNeoMSGO, Error, TEXT("MSAudio received a null authored sound; using PCM fallback only."));
	}
	if (!Sound)
	{
		if (USoundBase* Fallback = GetAudibleFallbackSound(); Fallback && FallbackVolume > 0.0f)
		{
			UGameplayStatics::SpawnSound2D(WorldContextObject, Fallback, FallbackVolume, 1.0f);
		}
	}
}

void MSAudio::PlayMetaSound2DIfLocallyControlled(
	AActor* PotentialLocalPawn,
	USoundBase* Sound,
	const float Frequency,
	const float FallbackVolume,
	const float OneShotDuration)
{
	const APawn* Pawn = Cast<APawn>(PotentialLocalPawn);
	const APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	// AIController::IsLocalController() is true in standalone/listen-server worlds, so
	// IsLocallyControlled alone leaks private hit confirms from every locally simulated BOT.
	if (PlayerController && PlayerController->IsLocalController())
	{
		PlayMetaSound2D(PotentialLocalPawn, Sound, Frequency, FallbackVolume, OneShotDuration);
	}
}

UAudioComponent* MSAudio::SpawnLoopingMetaSoundAttached(
	USceneComponent* AttachToComponent,
	USoundBase* Sound,
	const float Frequency,
	const float VolumeMultiplier)
{
	if (!AttachToComponent || !Sound)
	{
		return nullptr;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (IsRunningAutomationTest())
	{
		GRecordedLoopingMetaSoundState.Sound = Sound;
		GRecordedLoopingMetaSoundState.Frequency = Frequency;
		GRecordedLoopingMetaSoundState.VolumeMultiplier = VolumeMultiplier;
		GRecordedLoopingMetaSoundState.bHasAttenuation = true;
		GRecordedLoopingMetaSoundState.AttenuationInnerRadius = 300.0f * MSGO::MSScale;
		GRecordedLoopingMetaSoundState.AttenuationFalloffDistance = 4700.0f * MSGO::MSScale;
		Record3DVoicePolicy(GRecordedLoopingMetaSoundState.bHasConcurrency,
			GRecordedLoopingMetaSoundState.AppliedPitchMultiplier,
			GRecordedLoopingMetaSoundState.MaxConcurrentVoices,
			GRecordedLoopingMetaSoundState.bStopsFarthestThenOldest);
		++GRecordedLoopingMetaSoundState.StartCount;
		return nullptr;
	}
#endif
	if (!HasUsableAudioDevice(AttachToComponent))
	{
		return nullptr;
	}

	// 実プレイ報告「チャージ音がチャージ中ずっと鳴っていない」対応: このAPIはループ再生専用なので、
	// 非ループ設定のままインポートされた波形(TypeB_Charge等)が渡されても途切れないよう、
	// 再生前にループ属性を強制する(メモリ上のみ。アセットの保存は行わない)。
	if (USoundWave* Wave = Cast<USoundWave>(Sound))
	{
		Wave->bLooping = true;
	}

	UAudioComponent* AudioComponent = UGameplayStatics::SpawnSoundAttached(
		Sound, AttachToComponent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset, false, VolumeMultiplier, 1.0f, 0.0f,
		GetDefault3DAttenuation(), GetDefault3DConcurrency(Sound));
	if (AudioComponent)
	{
		AudioComponent->SetFloatParameter(TEXT("Frequency"), Frequency);
	}
	return AudioComponent;
}

void MSAudio::StopLoopingMetaSound(TObjectPtr<UAudioComponent>& AudioComponent)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (IsRunningAutomationTest())
	{
		++GRecordedLoopingMetaSoundState.StopCount;
		AudioComponent = nullptr;
		return;
	}
#endif
	if (UAudioComponent* Component = AudioComponent.Get(); IsValid(Component))
	{
		Component->Stop();
		Component->DestroyComponent();
	}
	AudioComponent = nullptr;
}

USoundBase* MSAudio::GetAudibleFallbackSound()
{
	static TWeakObjectPtr<USoundBase> CachedFallback;
	if (!CachedFallback.IsValid())
	{
		CachedFallback = LoadObject<USoundBase>(
			nullptr, TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	}
	return CachedFallback.Get();
}

#if WITH_DEV_AUTOMATION_TESTS
void MSAudio::ResetRecordedMetaSoundRequest()
{
	GRecordedMetaSoundRequest = FRecordedMetaSoundRequest();
	GRecordedLoopingMetaSoundState = FRecordedLoopingMetaSoundState();
}

const MSAudio::FRecordedMetaSoundRequest& MSAudio::GetRecordedMetaSoundRequest()
{
	return GRecordedMetaSoundRequest;
}

const MSAudio::FRecordedLoopingMetaSoundState& MSAudio::GetRecordedLoopingMetaSoundState()
{
	return GRecordedLoopingMetaSoundState;
}
#endif
