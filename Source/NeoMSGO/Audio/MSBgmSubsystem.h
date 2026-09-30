// Copyright NeoMSGO Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSBgmSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundClass;
class USoundMix;
class USoundWave;

USTRUCT()
struct FMSBgmPlaylist
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Sounds;
};

UENUM(BlueprintType)
enum class EMSBgmScene : uint8
{
	None,
	Title,
	Lobby,
	Shop,
	Hangar,
	Waiting,
	Battle,
	BattleFinale,
	// Reserved until the cooperative game mode is implemented; do not hook this scene yet.
	CoOp,
	Result,
};

/**
 * Game-instance-wide 2D BGM player. BGM uses persistent SoundClass and SoundMix
 * assets, keeping its persisted volume independent from master/SE settings.
 */
UCLASS()
class NEOMSGO_API UMSBgmSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="MS|Audio|BGM")
	void PlayScene(EMSBgmScene Scene);

	UFUNCTION(BlueprintCallable, Category="MS|Audio|BGM")
	void Stop(float FadeOutSeconds = -1.0f);

	UFUNCTION(BlueprintCallable, Category="MS|Audio|BGM")
	void SetBgmVolume(float NewVolume, bool bSave = true);

	UFUNCTION(BlueprintPure, Category="MS|Audio|BGM")
	float GetBgmVolume() const { return BgmVolume; }

	UFUNCTION(BlueprintPure, Category="MS|Audio|BGM")
	EMSBgmScene GetCurrentScene() const { return CurrentScene; }

#if WITH_DEV_AUTOMATION_TESTS
	void SetPlaylistForTesting(EMSBgmScene Scene, const TArray<USoundBase*>& Sounds);
	void AdvanceTrackForTesting() { PlayNextTrack(true); }
	void SimulateCurrentTrackLostForTesting();
	UAudioComponent* InstallSyntheticLoopTrackForTesting(USoundWave* Sound);
	void FeedPlaybackPercentForTesting(float PlaybackPercent);
	void FeedPlaybackPercentFromComponentForTesting(
		UAudioComponent* Component, float PlaybackPercent);
	void FeedFinishedFromComponentForTesting(UAudioComponent* Component);
	int32 GetPlaybackStartCountForTesting() const { return PlaybackStartCount; }
	int32 GetImmediateStopCountForTesting() const { return ImmediateStopCount; }
	USoundBase* GetCurrentSoundForTesting() const;
	int32 GetPlaylistSizeForTesting(EMSBgmScene Scene) const;
#endif

private:
	void LoadDefaultPlaylists();
	void PlayNextTrack(bool bAdvance);
	int32 ChooseTrackIndex(const FMSBgmPlaylist& Playlist) const;
	void HandleTrackFinished(UAudioComponent* FinishedComponent);
	void HandleTrackPlaybackPercent(
		const UAudioComponent* Component, const USoundWave* PlayingSound,
		float PlaybackPercent);
	void AdvanceTrackFromComponent(UAudioComponent* FinishedComponent);
	void StopCurrentTrack(const TCHAR* Reason);
	void ApplyBgmVolume(float FadeSeconds);
	void ApplyRoutingToWorld(UWorld* World);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	UWorld* ResolveWorld() const;

	UPROPERTY(Transient)
	TMap<EMSBgmScene, FMSBgmPlaylist> Playlists;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> CurrentComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> BgmSoundClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundMix> BgmSoundMix;

	EMSBgmScene CurrentScene = EMSBgmScene::None;
	int32 CurrentTrackIndex = INDEX_NONE;
	TMap<EMSBgmScene, int32> LastTrackIndices;
	float BgmVolume = 1.0f;
	bool bHasActiveTrack = false;
	float LastTrackPlaybackPercent = -1.0f;
	bool bHasObservedTrackProgress = false;
	TWeakObjectPtr<UWorld> RoutingWorld;
	FDelegateHandle PostLoadMapHandle;

#if WITH_DEV_AUTOMATION_TESTS
	int32 PlaybackStartCount = 0;
	int32 ImmediateStopCount = 0;
#endif
};
