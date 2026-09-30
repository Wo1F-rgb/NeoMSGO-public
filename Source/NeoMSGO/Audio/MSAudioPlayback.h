// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class AActor;
class USceneComponent;
class USoundBase;

namespace MSAudio
{
	/**
	 * Spawns a one-shot MetaSound and updates its public Frequency input.
	 * @param FallbackVolume 併走再生するエンジンPCMフォールバックの音量。低周波数の
	 *        MetaSound(爆発70Hz等)はスピーカーでほぼ聞こえないため、イベント側が引き上げられる。
	 */
	NEOMSGO_API UAudioComponent* SpawnMetaSoundAtLocation(
		const UObject* WorldContextObject,
		USoundBase* Sound,
		const FVector& Location,
		float Frequency,
		float FallbackVolume = 0.16f,
		float OneShotDuration = 0.18f);

	/**
	 * UI/確認音向けの2D再生(距離減衰なし。撃破確認・ロックオン確認・チャージ完了等)。
	 * 実プレイ報告「撃破時のシャキン音が聞こえない」対応: 位置依存を排除して確実に聞かせる。
	 */
	NEOMSGO_API void PlayMetaSound2D(
		const UObject* WorldContextObject,
		USoundBase* Sound,
		float Frequency,
		float FallbackVolume = 0.25f,
		float OneShotDuration = 0.18f);

	/** Plays private confirmation feedback only for a pawn owned by the local human player. */
	NEOMSGO_API void PlayMetaSound2DIfLocallyControlled(
		AActor* PotentialLocalPawn,
		USoundBase* Sound,
		float Frequency,
		float FallbackVolume = 0.25f,
		float OneShotDuration = 0.18f);

	/**
	 * ループ音(SFX_Thruster_Loop/SFX_Charge_Loop)をコンポーネントへアタッチ再生する。
	 * 停止は StopLoopingMetaSound で行う(ブースト噴射・チャージ・弾頭飛翔音)。
	 */
	NEOMSGO_API UAudioComponent* SpawnLoopingMetaSoundAttached(
		USceneComponent* AttachToComponent,
		USoundBase* Sound,
		float Frequency,
		float VolumeMultiplier = 1.0f);

	/** ループ再生の停止+コンポーネント破棄(null安全。呼び出し後ポインタはnullになる)。 */
	NEOMSGO_API void StopLoopingMetaSound(TObjectPtr<UAudioComponent>& AudioComponent);

	/** Engine SoundWave played quietly alongside MetaSounds as a guaranteed audible fallback. */
	NEOMSGO_API USoundBase* GetAudibleFallbackSound();

#if WITH_DEV_AUTOMATION_TESTS
	struct FRecordedMetaSoundRequest
	{
		TWeakObjectPtr<USoundBase> Sound;
		FVector Location = FVector::ZeroVector;
		float Frequency = 0.0f;
		float FallbackVolume = 0.0f;
		/** Component multiplier applied to the authored sound (or the PCM fallback when Sound is null). */
		float AppliedVolumeMultiplier = 1.0f;
		float OneShotDuration = 0.0f;
		bool bIs2D = false;
		bool bHasAttenuation = false;
		bool bHasConcurrency = false;
		float AppliedPitchMultiplier = 1.0f;
		int32 MaxConcurrentVoices = 0;
		bool bStopsFarthestThenOldest = false;
		float AttenuationInnerRadius = 0.0f;
		float AttenuationFalloffDistance = 0.0f;
		int32 RequestCount = 0;
	};

	/** ループ音のテスト観測(開始/停止回数と最後の開始要求内容) */
	struct FRecordedLoopingMetaSoundState
	{
		TWeakObjectPtr<USoundBase> Sound;
		float Frequency = 0.0f;
		bool bHasAttenuation = false;
		bool bHasConcurrency = false;
		float AppliedPitchMultiplier = 1.0f;
		int32 MaxConcurrentVoices = 0;
		bool bStopsFarthestThenOldest = false;
		float VolumeMultiplier = 1.0f;
		float AttenuationInnerRadius = 0.0f;
		float AttenuationFalloffDistance = 0.0f;
		int32 StartCount = 0;
		int32 StopCount = 0;
	};

	NEOMSGO_API void ResetRecordedMetaSoundRequest();
	NEOMSGO_API const FRecordedMetaSoundRequest& GetRecordedMetaSoundRequest();
	NEOMSGO_API const FRecordedLoopingMetaSoundState& GetRecordedLoopingMetaSoundState();
#endif
}
