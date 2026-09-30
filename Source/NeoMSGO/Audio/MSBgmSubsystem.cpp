// Copyright NeoMSGO Project. All Rights Reserved.
#include "Audio/MSBgmSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* AudioSettingsSection = TEXT("/Script/NeoMSGO.MSAudioSettings");
	const TCHAR* BgmVolumeKey = TEXT("BgmVolume");
	constexpr float DefaultBgmVolume = 0.75f;
	const TCHAR* BgmSettingsSection = TEXT("/Script/NeoMSGO.MSBgmSettings");
	const TCHAR* ExcludedAssetsKey = TEXT("ExcludedAssets");
	const TCHAR* BgmSoundClassPath =
		TEXT("/Game/Audio/MSBgmSoundClass.MSBgmSoundClass");
	const TCHAR* BgmSoundMixPath =
		TEXT("/Game/Audio/MSBgmSoundMix.MSBgmSoundMix");

	EMSBgmScene SceneFromAssetName(const FString& AssetName)
	{
		static const TPair<const TCHAR*, EMSBgmScene> Prefixes[] = {
			{ TEXT("BGM_Title_"), EMSBgmScene::Title },
			{ TEXT("BGM_Lobby_"), EMSBgmScene::Lobby },
			{ TEXT("BGM_Shop_"), EMSBgmScene::Shop },
			{ TEXT("BGM_Hangar_"), EMSBgmScene::Hangar },
			{ TEXT("BGM_Waiting_"), EMSBgmScene::Waiting },
			{ TEXT("BGM_BattleFinale_"), EMSBgmScene::BattleFinale },
			{ TEXT("BGM_Battle_"), EMSBgmScene::Battle },
			{ TEXT("BGM_CoOp_"), EMSBgmScene::CoOp },
			{ TEXT("BGM_Result_"), EMSBgmScene::Result },
		};
		for (const TPair<const TCHAR*, EMSBgmScene>& Pair : Prefixes)
		{
			if (AssetName.StartsWith(Pair.Key))
			{
				return Pair.Value;
			}
		}
		return EMSBgmScene::None;
	}

	FString SceneName(const EMSBgmScene Scene)
	{
		if (const UEnum* Enum = StaticEnum<EMSBgmScene>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Scene));
		}
		return TEXT("Unknown");
	}
}

void UMSBgmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BgmSoundClass = LoadObject<USoundClass>(nullptr, BgmSoundClassPath);
	BgmSoundMix = LoadObject<USoundMix>(nullptr, BgmSoundMixPath);
	if (!BgmSoundClass || !BgmSoundMix)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[216:BGM] Persistent routing assets are missing "
				"(SoundClass=%s SoundMix=%s). Run Tools/CreateBgmRoutingAssets.py."),
			BgmSoundClass ? TEXT("OK") : TEXT("MISSING"),
			BgmSoundMix ? TEXT("OK") : TEXT("MISSING"));
	}
	BgmVolume = DefaultBgmVolume;
	GConfig->GetFloat(AudioSettingsSection, BgmVolumeKey, BgmVolume, GGameUserSettingsIni);
	BgmVolume = FMath::Clamp(BgmVolume, 0.0f, 1.0f);
	LoadDefaultPlaylists();
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UMSBgmSubsystem::HandlePostLoadMap);

	if (UWorld* World = ResolveWorld())
	{
		ApplyRoutingToWorld(World);
		// The argument is fade time, not volume. BgmVolume is passed as the
		// volume multiplier inside ApplyBgmVolume.
		ApplyBgmVolume(0.0f);
	}
}

void UMSBgmSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	Stop(0.0f);
	if (UWorld* World = ResolveWorld())
	{
		if (BgmSoundMix && BgmSoundClass)
		{
			UGameplayStatics::ClearSoundMixClassOverride(World, BgmSoundMix, BgmSoundClass, 0.0f);
		}
		if (BgmSoundMix)
		{
			UGameplayStatics::PopSoundMixModifier(World, BgmSoundMix);
		}
	}
	Playlists.Empty();
	RoutingWorld = nullptr;
	BgmSoundMix = nullptr;
	BgmSoundClass = nullptr;
	Super::Deinitialize();
}

void UMSBgmSubsystem::LoadDefaultPlaylists()
{
	Playlists.Empty();
	TArray<FString> ExcludedAssetNames;
	GConfig->GetArray(BgmSettingsSection, ExcludedAssetsKey, ExcludedAssetNames, GGameIni);
	const TSet<FString> ExcludedAssets(ExcludedAssetNames);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game/Audio/BGM")));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	// GameInstance subsystems initialize before the editor/game Asset Registry
	// has necessarily discovered local BGM packages. Scan only this narrow path
	// synchronously so the first Title PlayScene call cannot observe an empty pool.
	AssetRegistry.ScanPathsSynchronous({ TEXT("/Game/Audio/BGM") }, true);
	AssetRegistry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	for (const FAssetData& Asset : Assets)
	{
		if (!Asset.IsInstanceOf(USoundBase::StaticClass()))
		{
			continue;
		}
		const FString AssetName = Asset.AssetName.ToString();
		const EMSBgmScene Scene = SceneFromAssetName(AssetName);
		if (Scene == EMSBgmScene::None || ExcludedAssets.Contains(AssetName))
		{
			if (ExcludedAssets.Contains(AssetName))
			{
				UE_LOG(LogTemp, Display, TEXT("[216:BGM] Excluded confirmed legacy duplicate: %s"), *AssetName);
			}
			continue;
		}
		if (USoundBase* Sound = Cast<USoundBase>(Asset.GetAsset()))
		{
			Playlists.FindOrAdd(Scene).Sounds.Add(Sound);
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[216:BGM] Asset Registry discovered %d entries under /Game/Audio/BGM"),
		Assets.Num());
	for (const TPair<EMSBgmScene, FMSBgmPlaylist>& Pair : Playlists)
	{
		UE_LOG(LogTemp, Display, TEXT("[216:BGM] Pool %s: %d track(s)"),
			*SceneName(Pair.Key), Pair.Value.Sounds.Num());
	}
}

void UMSBgmSubsystem::PlayScene(const EMSBgmScene Scene)
{
	// Repeated UI refresh/Open calls must never restart or reshuffle a track that
	// is still playing. The GameInstance subsystem can retain CurrentScene after
	// a travel or audio-device teardown, so the scene alone is not enough to
	// decide that playback is still alive.
	if (Scene == CurrentScene)
	{
		// Automation tests can exercise playlist selection without a world or an
		// audio device. Preserve the no-op semantics for that synthetic case.
		if (!ResolveWorld() ||
			(bHasActiveTrack && IsValid(CurrentComponent) && CurrentComponent->IsPlaying()))
		{
			return;
		}
	}
	if (Scene == EMSBgmScene::None)
	{
		Stop();
		return;
	}

	CurrentScene = Scene;
	const FMSBgmPlaylist* Playlist = Playlists.Find(Scene);
	if (!Playlist || Playlist->Sounds.IsEmpty())
	{
		Stop();
		return;
	}

	CurrentTrackIndex = ChooseTrackIndex(*Playlist);
	PlayNextTrack(false);
}

int32 UMSBgmSubsystem::ChooseTrackIndex(const FMSBgmPlaylist& Playlist) const
{
	const int32 Num = Playlist.Sounds.Num();
	if (Num <= 1)
	{
		return Num == 1 ? 0 : INDEX_NONE;
	}
	const int32* Previous = LastTrackIndices.Find(CurrentScene);
	if (!Previous || !Playlist.Sounds.IsValidIndex(*Previous))
	{
		return FMath::RandRange(0, Num - 1);
	}
	int32 Choice = FMath::RandRange(0, Num - 2);
	if (Choice >= *Previous)
	{
		++Choice;
	}
	return Choice;
}

void UMSBgmSubsystem::PlayNextTrack(const bool bAdvance)
{
	const FMSBgmPlaylist* Playlist = Playlists.Find(CurrentScene);
	if (!Playlist || Playlist->Sounds.IsEmpty())
	{
		Stop();
		return;
	}
	if (bAdvance)
	{
		CurrentTrackIndex = ChooseTrackIndex(*Playlist);
	}
	if (!Playlist->Sounds.IsValidIndex(CurrentTrackIndex))
	{
		CurrentTrackIndex = 0;
	}
	USoundBase* Sound = Playlist->Sounds[CurrentTrackIndex];
	if (!Sound)
	{
		return;
	}

	// Scene changes and end-of-track advances are deliberately hard cuts. Stop
	// the old component before creating the new one so the two tracks never
	// overlap, even for a single audio frame.
	StopCurrentTrack(bAdvance ? TEXT("track-ended") : TEXT("scene-change"));
#if WITH_DEV_AUTOMATION_TESTS
	++PlaybackStartCount;
#endif

	if (UWorld* World = ResolveWorld())
	{
		CurrentComponent = UGameplayStatics::CreateSound2D(
			World, Sound, 1.0f, 1.0f, 0.0f, nullptr,
			/* bPersistAcrossLevelTransition */ true,
			/* bAutoDestroy */ true);
		if (CurrentComponent)
		{
			CurrentComponent->SoundClassOverride = BgmSoundClass;
			CurrentComponent->SetVolumeMultiplier(1.0f);
			CurrentComponent->bIsUISound = true;
			CurrentComponent->OnAudioFinishedNative.AddUObject(
				this, &UMSBgmSubsystem::HandleTrackFinished);
			CurrentComponent->OnAudioPlaybackPercentNative.AddUObject(
				this, &UMSBgmSubsystem::HandleTrackPlaybackPercent);
			CurrentComponent->Play();
		}
	}
	bHasActiveTrack = true;
	LastTrackIndices.FindOrAdd(CurrentScene) = CurrentTrackIndex;
	UE_LOG(LogTemp, Display, TEXT("[216:BGM] Playing scene=%s asset=%s transition=immediate"),
		*SceneName(CurrentScene), *Sound->GetPathName());
	if (IsValid(CurrentComponent))
	{
		const float EffectiveVolumeMultiplier =
			CurrentComponent->VolumeMultiplier * BgmVolume;
		UE_LOG(LogTemp, Display,
			TEXT("[216:BGM:ACTIVE] component=%s IsPlaying=%s "
				"ComponentVolume=%.3f BgmClassVolume=%.3f "
				"EffectiveVolumeMultiplier=%.3f SoundClass=%s"),
			*CurrentComponent->GetPathName(),
			CurrentComponent->IsPlaying() ? TEXT("true") : TEXT("false"),
			CurrentComponent->VolumeMultiplier, BgmVolume,
			EffectiveVolumeMultiplier,
			BgmSoundClass ? *BgmSoundClass->GetPathName() : TEXT("None"));
	}
}

void UMSBgmSubsystem::ApplyRoutingToWorld(UWorld* World)
{
	if (!World || !BgmSoundMix)
	{
		return;
	}
	if (RoutingWorld.Get() != World)
	{
		UGameplayStatics::PushSoundMixModifier(World, BgmSoundMix);
		RoutingWorld = World;
	}
}

void UMSBgmSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!LoadedWorld || !GameInstance)
	{
		return;
	}
	bool bOwnedByThisGameInstance = LoadedWorld->GetGameInstance() == GameInstance;
	if (!bOwnedByThisGameInstance && GEngine)
	{
		if (const FWorldContext* Context =
			GEngine->GetWorldContextFromWorld(LoadedWorld))
		{
			bOwnedByThisGameInstance = Context->OwningGameInstance == GameInstance;
		}
	}
	if (!bOwnedByThisGameInstance)
	{
		return;
	}

	// SoundMix routing must be re-applied after travel. The persistent
	// CreateSound2D component keeps its current playback position, so do not
	// restart a full-duration timer here and shift the next-track boundary.
	ApplyRoutingToWorld(LoadedWorld);
	if (LoadedWorld == ResolveWorld())
	{
		ApplyBgmVolume(0.0f);
	}

	if (CurrentScene == EMSBgmScene::None)
	{
		return;
	}
	if (!IsValid(CurrentComponent) || !CurrentComponent->IsPlaying())
	{
		// A failed or externally stopped component must not be hidden by the
		// retained CurrentScene value after travel.
		PlayNextTrack(false);
	}
}

void UMSBgmSubsystem::HandleTrackFinished(UAudioComponent* FinishedComponent)
{
	AdvanceTrackFromComponent(FinishedComponent);
}

void UMSBgmSubsystem::HandleTrackPlaybackPercent(
	const UAudioComponent* Component, const USoundWave* PlayingSound,
	const float PlaybackPercent)
{
	if (Component != CurrentComponent.Get() || !bHasActiveTrack)
	{
		return;
	}

	// UE wraps playback percent to zero for looping waves. Do not use a single
	// high initial value as the boundary: a first callback can report 1.0
	// before the audio thread has provided a real progress sample.
	const bool bObservedWrap = bHasObservedTrackProgress &&
		LastTrackPlaybackPercent > 0.9f && PlaybackPercent < 0.1f;
	if (bObservedWrap && PlayingSound && PlayingSound->IsLooping())
	{
		AdvanceTrackFromComponent(const_cast<UAudioComponent*>(Component));
		return;
	}

	if (PlaybackPercent > 0.0f && PlaybackPercent < 1.0f)
	{
		bHasObservedTrackProgress = true;
	}
	LastTrackPlaybackPercent = PlaybackPercent;
}

void UMSBgmSubsystem::AdvanceTrackFromComponent(UAudioComponent* FinishedComponent)
{
	// Stopping a component explicitly also emits OnAudioFinishedNative. Only
	// the currently owned component may advance the playlist, and StopCurrentTrack
	// removes this delegate before an explicit stop to prevent a double advance.
	if (!IsValid(FinishedComponent) ||
		FinishedComponent != CurrentComponent.Get() || !bHasActiveTrack ||
		CurrentScene == EMSBgmScene::None)
	{
		return;
	}

	FinishedComponent->OnAudioFinishedNative.RemoveAll(this);
	FinishedComponent->OnAudioPlaybackPercentNative.RemoveAll(this);
	if (FinishedComponent->IsPlaying())
	{
		FinishedComponent->Stop();
	}
	CurrentComponent = nullptr;
	bHasActiveTrack = false;
	LastTrackPlaybackPercent = -1.0f;
	bHasObservedTrackProgress = false;
	PlayNextTrack(true);
}

void UMSBgmSubsystem::Stop(const float FadeOutSeconds)
{
	StopCurrentTrack(TEXT("stop"));
	CurrentScene = EMSBgmScene::None;
	CurrentTrackIndex = INDEX_NONE;
	UE_LOG(LogTemp, Display,
		TEXT("[216:BGM] Stopped transition=immediate requestedFade=%.2fs"),
		FadeOutSeconds);
}

void UMSBgmSubsystem::StopCurrentTrack(const TCHAR* Reason)
{
	if (!bHasActiveTrack && !CurrentComponent)
	{
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	++ImmediateStopCount;
#endif
	if (IsValid(CurrentComponent))
	{
		UAudioComponent* ComponentToStop = CurrentComponent;
		ComponentToStop->OnAudioFinishedNative.RemoveAll(this);
		ComponentToStop->OnAudioPlaybackPercentNative.RemoveAll(this);
		const bool bWasPlaying = ComponentToStop->IsPlaying();
		const FString ComponentPath = ComponentToStop->GetPathName();
		ComponentToStop->Stop();
		UE_LOG(LogTemp, Display,
			TEXT("[216:BGM:STOPPED] reason=%s component=%s "
				"WasPlaying=%s IsPlayingAfterStop=%s"),
			Reason, *ComponentPath,
			bWasPlaying ? TEXT("true") : TEXT("false"),
			ComponentToStop->IsPlaying() ? TEXT("true") : TEXT("false"));
	}
	CurrentComponent = nullptr;
	bHasActiveTrack = false;
	LastTrackPlaybackPercent = -1.0f;
	bHasObservedTrackProgress = false;
}

void UMSBgmSubsystem::SetBgmVolume(const float NewVolume, const bool bSave)
{
	BgmVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBgmVolume(0.05f);
	if (bSave)
	{
		GConfig->SetFloat(AudioSettingsSection, BgmVolumeKey, BgmVolume, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSBgmSubsystem::ApplyBgmVolume(const float FadeSeconds)
{
	if (UWorld* World = ResolveWorld(); World && BgmSoundMix && BgmSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(
			World, BgmSoundMix, BgmSoundClass, BgmVolume, 1.0f, FadeSeconds, false);
		UE_LOG(LogTemp, Display,
			TEXT("[216:BGM:VOLUME] IsPlaying=%s ComponentVolume=%.3f "
				"BgmClassVolume=%.3f FadeSeconds=%.3f"),
			IsValid(CurrentComponent) && CurrentComponent->IsPlaying() ? TEXT("true") : TEXT("false"),
			IsValid(CurrentComponent) ? CurrentComponent->VolumeMultiplier : 0.0f,
			BgmVolume, FadeSeconds);
	}
}

UWorld* UMSBgmSubsystem::ResolveWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSBgmSubsystem::SimulateCurrentTrackLostForTesting()
{
	if (IsValid(CurrentComponent))
	{
		CurrentComponent->OnAudioFinishedNative.RemoveAll(this);
		CurrentComponent->OnAudioPlaybackPercentNative.RemoveAll(this);
		CurrentComponent->Stop();
	}
	CurrentComponent = nullptr;
	bHasActiveTrack = true;
	LastTrackPlaybackPercent = -1.0f;
	bHasObservedTrackProgress = false;
}

UAudioComponent* UMSBgmSubsystem::InstallSyntheticLoopTrackForTesting(USoundWave* Sound)
{
	CurrentComponent = NewObject<UAudioComponent>(this);
	CurrentComponent->Sound = Sound;
	bHasActiveTrack = true;
	LastTrackPlaybackPercent = -1.0f;
	bHasObservedTrackProgress = false;
	return CurrentComponent.Get();
}

void UMSBgmSubsystem::FeedPlaybackPercentForTesting(const float PlaybackPercent)
{
	if (IsValid(CurrentComponent))
	{
		HandleTrackPlaybackPercent(
			CurrentComponent, Cast<USoundWave>(CurrentComponent->Sound), PlaybackPercent);
	}
}

void UMSBgmSubsystem::FeedPlaybackPercentFromComponentForTesting(
	UAudioComponent* Component, const float PlaybackPercent)
{
	if (IsValid(Component))
	{
		HandleTrackPlaybackPercent(
			Component, Cast<USoundWave>(Component->Sound), PlaybackPercent);
	}
}

void UMSBgmSubsystem::FeedFinishedFromComponentForTesting(UAudioComponent* Component)
{
	HandleTrackFinished(Component);
}

void UMSBgmSubsystem::SetPlaylistForTesting(
	const EMSBgmScene Scene, const TArray<USoundBase*>& Sounds)
{
	TArray<TObjectPtr<USoundBase>>& Playlist = Playlists.FindOrAdd(Scene).Sounds;
	Playlist.Reset();
	for (USoundBase* Sound : Sounds)
	{
		Playlist.Add(Sound);
	}
	LastTrackIndices.Remove(Scene);
}

USoundBase* UMSBgmSubsystem::GetCurrentSoundForTesting() const
{
	const FMSBgmPlaylist* Playlist = Playlists.Find(CurrentScene);
	return Playlist && Playlist->Sounds.IsValidIndex(CurrentTrackIndex)
		? Playlist->Sounds[CurrentTrackIndex] : nullptr;
}

int32 UMSBgmSubsystem::GetPlaylistSizeForTesting(const EMSBgmScene Scene) const
{
	const FMSBgmPlaylist* Playlist = Playlists.Find(Scene);
	return Playlist ? Playlist->Sounds.Num() : 0;
}
#endif
