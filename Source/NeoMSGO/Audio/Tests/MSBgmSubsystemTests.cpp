// Copyright NeoMSGO Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/MSBgmSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmSameSceneDoesNotRestartTest,
	"NeoMSGO.Audio.BGM.SameSceneDoesNotRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmSameSceneDoesNotRestartTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSBgmSubsystem* Subsystem = NewObject<UMSBgmSubsystem>(GameInstance);
	USoundWave* Sound = NewObject<USoundWave>(Subsystem);
	Subsystem->SetPlaylistForTesting(EMSBgmScene::Lobby, { Sound });

	Subsystem->PlayScene(EMSBgmScene::Lobby);
	const int32 StartsAfterFirstCall = Subsystem->GetPlaybackStartCountForTesting();
	Subsystem->PlayScene(EMSBgmScene::Lobby);

	TestEqual(TEXT("First PlayScene starts exactly one track"), StartsAfterFirstCall, 1);
	TestEqual(TEXT("Repeated same-scene PlayScene does not restart"),
		Subsystem->GetPlaybackStartCountForTesting(), StartsAfterFirstCall);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmSceneChangeIsImmediateTest,
	"NeoMSGO.Audio.BGM.SceneChangeIsImmediate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmSceneChangeIsImmediateTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSBgmSubsystem* Subsystem = NewObject<UMSBgmSubsystem>(GameInstance);
	USoundWave* LobbySound = NewObject<USoundWave>(Subsystem);
	USoundWave* BattleSound = NewObject<USoundWave>(Subsystem);
	Subsystem->SetPlaylistForTesting(EMSBgmScene::Lobby, { LobbySound });
	Subsystem->SetPlaylistForTesting(EMSBgmScene::Battle, { BattleSound });

	Subsystem->PlayScene(EMSBgmScene::Lobby);
	Subsystem->PlayScene(EMSBgmScene::Battle);

	TestEqual(TEXT("Scene switch starts the second track"),
		Subsystem->GetPlaybackStartCountForTesting(), 2);
	TestEqual(TEXT("Scene switch immediately stops the previous track"),
		Subsystem->GetImmediateStopCountForTesting(), 1);
	TestEqual(TEXT("Current scene changes to Battle"),
		Subsystem->GetCurrentScene(), EMSBgmScene::Battle);

	Subsystem->Stop(2.0f);
	TestEqual(TEXT("Stop ignores the legacy fade argument and stops immediately"),
		Subsystem->GetImmediateStopCountForTesting(), 2);
	TestEqual(TEXT("Stop clears the current scene"),
		Subsystem->GetCurrentScene(), EMSBgmScene::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmSameSceneRecoversAfterTravelLossTest,
	"NeoMSGO.Audio.BGM.SameSceneRecoversAfterTravelLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmSameSceneRecoversAfterTravelLossTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game, false, TEXT("MSBgmTravelRecoveryTestWorld"));
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	if (!TestNotNull(TEXT("BGM travel recovery test world exists"), World) ||
		!TestNotNull(TEXT("BGM travel recovery test GameInstance exists"), GameInstance))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.OwningGameInstance = GameInstance;
	Context.SetCurrentWorld(World);
	World->SetGameInstance(GameInstance);
	GameInstance->Init();

	UMSBgmSubsystem* Subsystem =
		GameInstance->GetSubsystem<UMSBgmSubsystem>();
	if (!TestNotNull(TEXT("BGM subsystem exists in the travel recovery world"), Subsystem))
	{
		GameInstance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	USoundWave* Sound = NewObject<USoundWave>(Subsystem);
	Subsystem->SetPlaylistForTesting(EMSBgmScene::Battle, { Sound });
	Subsystem->PlayScene(EMSBgmScene::Battle);
	TestEqual(TEXT("Initial Battle scene starts one track"),
		Subsystem->GetPlaybackStartCountForTesting(), 1);

	// If a component is lost during map travel while the GameInstance subsystem
	// and CurrentScene survive, the production guard must treat that state as
	// lost and recreate the same scene on the next request.
	Subsystem->SimulateCurrentTrackLostForTesting();
	Subsystem->PlayScene(EMSBgmScene::Battle);
	TestEqual(TEXT("Same-scene request recreates a track after travel loss"),
		Subsystem->GetPlaybackStartCountForTesting(), 2);

	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmLoopBoundaryRequiresProgressTest,
	"NeoMSGO.Audio.BGM.LoopBoundaryRequiresProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmLoopBoundaryRequiresProgressTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSBgmSubsystem* Subsystem = NewObject<UMSBgmSubsystem>(GameInstance);
	USoundWave* Sound = LoadObject<USoundWave>(
		nullptr, TEXT("/Game/Audio/BGM/BGM_Battle_18.BGM_Battle_18"));
	if (!TestNotNull(TEXT("looping BGM test asset loads"), Sound))
	{
		return false;
	}
	Subsystem->SetPlaylistForTesting(EMSBgmScene::Battle, { Sound });
	Subsystem->PlayScene(EMSBgmScene::Battle);
	UAudioComponent* OldComponent =
		Subsystem->InstallSyntheticLoopTrackForTesting(Sound);

	Subsystem->FeedPlaybackPercentForTesting(1.0f);
	TestEqual(TEXT("An initial 1.0 playback callback does not advance"),
		Subsystem->GetPlaybackStartCountForTesting(), 1);
	Subsystem->Stop();
	Subsystem->FeedPlaybackPercentFromComponentForTesting(OldComponent, 0.02f);
	Subsystem->FeedFinishedFromComponentForTesting(OldComponent);
	TestEqual(TEXT("Explicit Stop prevents late callbacks from the old component"),
		Subsystem->GetPlaybackStartCountForTesting(), 1);

	Subsystem->PlayScene(EMSBgmScene::Battle);
	UAudioComponent* ReplacedComponent =
		Subsystem->InstallSyntheticLoopTrackForTesting(Sound);
	Subsystem->FeedPlaybackPercentForTesting(0.50f);
	Subsystem->FeedPlaybackPercentForTesting(0.99f);
	TestEqual(TEXT("A high playback percentage does not advance early"),
		Subsystem->GetPlaybackStartCountForTesting(), 2);
	Subsystem->FeedPlaybackPercentForTesting(0.02f);
	TestEqual(TEXT("A high-to-low wrap after observed progress advances once"),
		Subsystem->GetPlaybackStartCountForTesting(), 3);
	Subsystem->FeedPlaybackPercentFromComponentForTesting(ReplacedComponent, 0.02f);
	Subsystem->FeedFinishedFromComponentForTesting(ReplacedComponent);
	TestEqual(TEXT("Late callbacks from a replaced component do not advance again"),
		Subsystem->GetPlaybackStartCountForTesting(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmRandomPoolAndNoImmediateRepeatTest,
	"NeoMSGO.Audio.BGM.RandomPoolAndNoImmediateRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmRandomPoolAndNoImmediateRepeatTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSBgmSubsystem* Subsystem = NewObject<UMSBgmSubsystem>(GameInstance);
	USoundWave* LegacySound = NewObject<USoundWave>(
		Subsystem, TEXT("BGM_Battle_01"));
	USoundWave* NewSound = NewObject<USoundWave>(
		Subsystem, TEXT("BGM_Battle_17"));
	USoundWave* AdditionalSound = NewObject<USoundWave>(
		Subsystem, TEXT("BGM_Battle_18"));
	Subsystem->SetPlaylistForTesting(
		EMSBgmScene::Battle, { LegacySound, NewSound, AdditionalSound });

	TestEqual(TEXT("Legacy and new assets occupy one Battle pool"),
		Subsystem->GetPlaylistSizeForTesting(EMSBgmScene::Battle), 3);
	Subsystem->PlayScene(EMSBgmScene::Battle);

	TSet<USoundBase*> Heard;
	USoundBase* Previous = Subsystem->GetCurrentSoundForTesting();
	Heard.Add(Previous);
	for (int32 Iteration = 0; Iteration < 32; ++Iteration)
	{
		Subsystem->AdvanceTrackForTesting();
		USoundBase* Current = Subsystem->GetCurrentSoundForTesting();
		TestNotEqual(TEXT("A pool with multiple tracks never immediately repeats"),
			Current, Previous);
		Heard.Add(Current);
		Previous = Current;
	}
	TestEqual(TEXT("Every end-of-track advance immediately stops the old track"),
		Subsystem->GetImmediateStopCountForTesting(), 32);
	TestTrue(TEXT("Random selection reaches multiple members of the combined pool"),
		Heard.Num() > 1);
	TestTrue(TEXT("Combined pool includes the legacy asset"), Heard.Contains(LegacySound));
	TestTrue(TEXT("Combined pool includes a newly imported asset"), Heard.Contains(NewSound));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmSceneEnumCoverageTest,
	"NeoMSGO.Audio.BGM.SceneEnumCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmSceneEnumCoverageTest::RunTest(const FString&)
{
	TestEqual(TEXT("Title scene is defined"),
		StaticEnum<EMSBgmScene>()->GetNameStringByValue(
			static_cast<int64>(EMSBgmScene::Title)), FString(TEXT("Title")));
	TestTrue(TEXT("Shop scene is defined"), EMSBgmScene::Shop != EMSBgmScene::None);
	TestTrue(TEXT("CoOp scene is reserved for a future game mode"),
		EMSBgmScene::CoOp != EMSBgmScene::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMSBgmPersistentRoutingAssetsTest,
	"NeoMSGO.Audio.BGM.PersistentRoutingAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSBgmPersistentRoutingAssetsTest::RunTest(const FString&)
{
	USoundClass* BgmClass = LoadObject<USoundClass>(
		nullptr, TEXT("/Game/Audio/MSBgmSoundClass.MSBgmSoundClass"));
	USoundMix* BgmMix = LoadObject<USoundMix>(
		nullptr, TEXT("/Game/Audio/MSBgmSoundMix.MSBgmSoundMix"));
	if (!TestNotNull(TEXT("Persistent BGM SoundClass asset exists"), BgmClass) ||
		!TestNotNull(TEXT("Persistent BGM SoundMix asset exists"), BgmMix))
	{
		return false;
	}

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({ TEXT("/Game/Audio/BGM") }, true);
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/Audio/BGM"));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	int32 RoutedWaveCount = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (!Asset.IsInstanceOf(USoundWave::StaticClass()))
		{
			continue;
		}
		USoundWave* Wave = Cast<USoundWave>(Asset.GetAsset());
		if (TestNotNull(*FString::Printf(TEXT("%s loads"), *Asset.AssetName.ToString()), Wave))
		{
			TestEqual(*FString::Printf(TEXT("%s uses only the BGM class"), *Asset.AssetName.ToString()),
				Wave->SoundClassObject.Get(), BgmClass);
			++RoutedWaveCount;
		}
	}
	TestEqual(TEXT("All 79 imported BGM waves are routed"), RoutedWaveCount, 79);

	USoundBase* Gameplay3D = LoadObject<USoundBase>(
		nullptr, TEXT("/Game/Audio/Imported/SFX_Thruster_Loop.SFX_Thruster_Loop"));
	USoundBase* Voice = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/MS_Beep.MS_Beep"));
	if (TestNotNull(TEXT("Known 3D gameplay SE loads"), Gameplay3D))
	{
		TestNotEqual(TEXT("3D gameplay SE is not routed through the BGM class"),
			Gameplay3D->SoundClassObject.Get(), BgmClass);
	}
	if (TestNotNull(TEXT("Known voice blip loads"), Voice))
	{
		TestNotEqual(TEXT("Voice is not routed through the BGM class"),
			Voice->SoundClassObject.Get(), BgmClass);
	}
	return true;
}

#endif
