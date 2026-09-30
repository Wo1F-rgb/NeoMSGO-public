#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AudioDevice.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSSpawnSelectWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Voice/MSVoiceCutInSubsystem.h"
#include "Voice/MSVoiceLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsSensitivityMinimumTest,
	"NeoMSGO.UI.Settings.SensitivityMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsBindingListTest,
	"NeoMSGO.UI.Settings.BindingList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsUILockTest,
	"NeoMSGO.UI.Settings.UILock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsResortieButtonTest,
	"NeoMSGO.UI.Settings.ResortieButtonUsesRecallWhileAlive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsExitButtonTest,
	"NeoMSGO.UI.Settings.ExitButtonOpensLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsFieldOfViewTest,
	"NeoMSGO.UI.Settings.FieldOfView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsCameraViewPresetTest,
	"NeoMSGO.UI.Settings.CameraViewPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsPresetVoiceTypeTest,
	"NeoMSGO.UI.Settings.PresetVoiceType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsAudioPresetTest,
	"NeoMSGO.UI.Settings.AudioPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSettingsBgmVolumeTest,
	"NeoMSGO.UI.Settings.BgmVolumeIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSSettingsWidgetTests
{
	UMSSettingsWidget* MakeInitializedWidget()
	{
		UMSSettingsWidget* Widget = NewObject<UMSSettingsWidget>();
		Widget->Initialize();
		// UUserWidget::Initialize only reaches NativeOnInitialized with a local-player context,
		// which headless automation lacks; build the widget tree explicitly (same pattern as
		// Sortie/Lobby/Hangar widget tests).
		Widget->InitializeForAutomationTests();
		return Widget;
	}

	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSSettingsWidgetTestWorld"));
		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	struct FScopedConfigValue
	{
		const TCHAR* Section = nullptr;
		const TCHAR* Key = nullptr;
		FString SettingsIniPath;
		FString OriginalValue;
		bool bHadValue = false;

		FScopedConfigValue(const TCHAR* InSection, const TCHAR* InKey)
			: Section(InSection), Key(InKey), SettingsIniPath(GGameUserSettingsIni)
		{
			if (GConfig && !SettingsIniPath.IsEmpty())
			{
				bHadValue = GConfig->GetString(Section, Key, OriginalValue, SettingsIniPath);
			}
		}

		void Restore() const
		{
			if (!GConfig || SettingsIniPath.IsEmpty())
			{
				return;
			}
			if (bHadValue)
			{
				GConfig->SetString(Section, Key, *OriginalValue, SettingsIniPath);
			}
			else
			{
				GConfig->RemoveKey(Section, Key, SettingsIniPath);
			}
		}
	};

	/**
	 * Settings widgets intentionally exercise the real persistence path. Keep that coverage while
	 * restoring the exact pre-test config values and the two process-wide selection caches.
	 * FScopedConfigValue also preserves a missing key as missing, rather than writing a default.
	 */
	struct FScopedSettingsState
	{
		FScopedConfigValue LookSensitivity{
			TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("LookSensitivityScale")};
		FScopedConfigValue FieldOfView{
			TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("DefaultCameraFieldOfView")};
		FScopedConfigValue CameraViewPreset{
			TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("CameraViewPreset")};
		FScopedConfigValue MasterVolume{
			TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("MasterVolume")};
		FScopedConfigValue BgmVolume{
			TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("BgmVolume")};
		FScopedConfigValue AudioPreset{
			TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("AudioPreset")};
		FScopedConfigValue UIMode{
			TEXT("/Script/NeoMSGO.MSUISettings"), TEXT("UIMode")};

		const EMSAudioPreset OriginalAudioPreset = MSAudioPreset::GetSelectedPreset();
		const MSUIStyle::EUIMode OriginalUIMode = MSUIStyle::GetSelectedMode();
		float OriginalMasterVolume = UMSSettingsWidget::DefaultMasterVolume;
		bool bTransientAudioWasTouched = false;

		FScopedSettingsState()
		{
			if (GConfig && !GGameUserSettingsIni.IsEmpty())
			{
				GConfig->GetFloat(TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("MasterVolume"),
					OriginalMasterVolume, GGameUserSettingsIni);
				OriginalMasterVolume = FMath::Clamp(OriginalMasterVolume, 0.0f, 1.0f);
			}
		}

		void MarkTransientAudioTouched()
		{
			bTransientAudioWasTouched = true;
		}

		~FScopedSettingsState()
		{
			LookSensitivity.Restore();
			FieldOfView.Restore();
			CameraViewPreset.Restore();
			MasterVolume.Restore();
			BgmVolume.Restore();
			AudioPreset.Restore();
			UIMode.Restore();
			if (GConfig && !GGameUserSettingsIni.IsEmpty())
			{
				GConfig->Flush(false, GGameUserSettingsIni);
			}

			// These caches are independent of GConfig and must be restored without another disk write.
			MSAudioPreset::SetSelectedPreset(OriginalAudioPreset, false);
			MSUIStyle::SetSelectedMode(OriginalUIMode, false);

			// The BGM test changes the main device transiently via the master slider. There is no
			// game-thread getter for the audio-thread transient value, so restore the persisted user
			// value used by the production startup path. This guard is only armed by that test.
			if (bTransientAudioWasTouched && GEngine)
			{
				FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
				if (AudioDevice.IsValid())
				{
					AudioDevice->SetTransientPrimaryVolume(OriginalMasterVolume);
				}
			}
		}
	};
}

bool FMSSettingsSensitivityMinimumTest::RunTest(const FString&)
{
	// NativeOnInitialized applies this same constant directly via USlider::SetMinValue.
	TestEqual(TEXT("the sensitivity slider minimum is exactly 0.01"),
		UMSSettingsWidget::MinMouseSensitivity, 0.01f);
	return true;
}

bool FMSSettingsBgmVolumeTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedSettingsState SettingsState;
	SettingsState.MarkTransientAudioTouched();
	UMSSettingsWidget* Widget = MSSettingsWidgetTests::MakeInitializedWidget();
	USlider* Master = Widget->GetMasterVolumeSliderForTesting();
	USlider* Bgm = Widget->GetBgmVolumeSliderForTesting();
	if (!TestNotNull(TEXT("settings constructs a master volume slider"), Master)
		|| !TestNotNull(TEXT("settings constructs a BGM volume slider"), Bgm))
	{
		return false;
	}
	TestNotEqual(TEXT("BGM and master/SE controls are separate widgets"), Bgm, Master);
	Master->SetValue(0.82f);
	Bgm->SetValue(0.31f);
	TestEqual(TEXT("BGM slider accepts its independent value"), Bgm->GetValue(), 0.31f);
	TestEqual(TEXT("changing BGM does not alter master/SE volume"),
		Master->GetValue(), 0.82f);
	return true;
}

bool FMSSettingsBindingListTest::RunTest(const FString&)
{
	const TArray<FName>& Actual = UMSSettingsWidget::GetRebindableActionNames();
	const TSet<FName> Expected = {
		TEXT("IA_Jump"), TEXT("IA_Dash"), TEXT("IA_Crouch"), TEXT("IA_Fire"), TEXT("IA_Reload"),
		TEXT("IA_LockOn"), TEXT("IA_Action"), TEXT("IA_Weapon1"), TEXT("IA_Weapon2"), TEXT("IA_Weapon3"),
		TEXT("IA_Module"), TEXT("IA_ViewToggle")
	};
	TestEqual(TEXT("there are exactly twelve rebindable actions"), Actual.Num(), 12);
	TestEqual(TEXT("the twelve-action list contains no duplicates"), TSet<FName>(Actual).Num(), 12);
	for (const FName Name : Expected)
	{
		TestTrue(*FString::Printf(TEXT("required action %s is present"), *Name.ToString()), Actual.Contains(Name));
	}
	TestFalse(TEXT("IA_Move is deliberately excluded"), Actual.Contains(TEXT("IA_Move")));
	TestFalse(TEXT("IA_Look is deliberately excluded"), Actual.Contains(TEXT("IA_Look")));
	for (const FName Name : Actual)
	{
		TestTrue(*FString::Printf(TEXT("unexpected action %s is absent"), *Name.ToString()), Expected.Contains(Name));
	}
	return true;
}

bool FMSSettingsUILockTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSUILockSubsystem* UILock = NewObject<UMSUILockSubsystem>(GameInstance);
	UMSSettingsWidget* Widget = MSSettingsWidgetTests::MakeInitializedWidget();
	Widget->SetUILockSubsystemForTesting(UILock);

	TestFalse(TEXT("the UI lock starts empty"), UILock->IsAnyScreenOpen());
	Widget->Open();
	TestTrue(TEXT("Open pushes Settings into the UI lock"), UILock->IsAnyScreenOpen());
	TestFalse(TEXT("Open disables gameplay through the shared lock"), UILock->IsGameplayActive());

	bool bOverlayClosed = false;
	UILock->PushScreen(TEXT("Overlay"), FMSUICloseRequestDelegate::CreateLambda([&bOverlayClosed]()
	{
		bOverlayClosed = true;
		return true;
	}));
	TestTrue(TEXT("Esc closes a screen above Settings first"), UILock->RequestCloseTopScreen());
	TestTrue(TEXT("the overlapping screen receives the first close request"), bOverlayClosed);
	TestTrue(TEXT("Settings remains open after the overlapping screen closes"), Widget->IsOpen());
	TestTrue(TEXT("the second Esc closes Settings"), UILock->RequestCloseTopScreen());
	TestFalse(TEXT("Settings is closed by its registered close delegate"), Widget->IsOpen());
	TestFalse(TEXT("Close pops Settings from the UI lock"), UILock->IsAnyScreenOpen());
	TestTrue(TEXT("Close restores gameplay after the final screen"), UILock->IsGameplayActive());
	return true;
}

bool FMSSettingsResortieButtonTest::RunTest(const FString&)
{
	// Living offline pilots must complete the same recall as online pilots;
	// the settings entry must not provide an instant heal/teleport bypass.
	MSSettingsWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("player spawned"), Player)) return false;
	UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Player);
	Data->EmergencyRecallPerformance = 1000;
	Player->InitializeUnit(Data);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	APlayerController* PC = Scope.World->SpawnActor<APlayerController>();
	PC->Possess(Player);
	// A bare headless test world has no ULocalPlayer/NetDriver, so APlayerController::IsLocalController()
	// would otherwise read this controller as remote and OpenResortieScreen's locality gate would
	// silently no-op. SetAsLocalPlayerController is the same public API GameModeBase uses to mark the
	// real local player controller; it lets this test exercise the actual production gate instead of
	// bypassing it.
	PC->SetAsLocalPlayerController();
	TestTrue(TEXT("precondition: the controller is recognized as local"), PC->IsLocalController());
	TestFalse(TEXT("precondition: the player is alive (no State.Dead)"),
		Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));

	TStrongObjectPtr<UMSSpawnSelectWidget> Sortie(NewObject<UMSSpawnSelectWidget>(GetTransientPackage()));
	Sortie->Initialize();
	Sortie->InitializeForAutomationTests();
	TestFalse(TEXT("precondition: the sortie screen starts closed"), Sortie->IsOpen());
	Player->SetSpawnSelectWidgetForTesting(Sortie.Get());

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSUILockSubsystem* UILock = NewObject<UMSUILockSubsystem>(GameInstance);
	UMSSettingsWidget* Settings = MSSettingsWidgetTests::MakeInitializedWidget();
	Settings->SetUILockSubsystemForTesting(UILock);
	Settings->SetResortiePlayerForTesting(Player);
	Settings->Open();
	TestTrue(TEXT("precondition: Settings is open"), Settings->IsOpen());
	TestEqual(TEXT("offline living pilot has the recall label"),
		Settings->GetResortieLabelForTesting().ToString(), FString(TEXT("帰投して再出撃")));
	TestFalse(TEXT("recall requirement has a tooltip"), Settings->GetResortieTooltipForTesting().IsEmpty());

	UButton* ResortieButton = Settings->GetResortieButtonForTesting();
	if (!TestNotNull(TEXT("Settings owns a Resortie button"), ResortieButton)) return false;
	ResortieButton->OnClicked.Broadcast();

	TestFalse(TEXT("Resortie closes the Settings screen"), Settings->IsOpen());
	TestFalse(TEXT("airborne menu request cannot bypass recall"), Sortie->IsOpen());
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	TestEqual(TEXT("menu failure reaches HUD feedback state"), Recall->GetNoticeReason(), EMSRecallReason::Airborne);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Settings->Open(); ResortieButton->OnClicked.Broadcast();
	TestTrue(TEXT("grounded menu request starts recall"), Recall->IsRecallActive());
	TestFalse(TEXT("menu cannot open sortie during recall"), Sortie->IsOpen());
	for (int32 Index = 0; Index < 52; ++Index) { Scope.World->Tick(LEVELTICK_All, .1f); ++GFrameCounter; }
	TestTrue(TEXT("completed recall opens suit selection"), Sortie->IsOpen());
	TestTrue(TEXT("completed recall awaits deployment"), Recall->IsAwaitingDeploy());
	Settings->Open();
	TestEqual(TEXT("completed recall uses the normal sortie label"), Settings->GetResortieLabelForTesting().ToString(), FString(TEXT("再出撃")));
	ResortieButton->OnClicked.Broadcast();
	TestTrue(TEXT("completed recall can reopen selection without another countdown"), Sortie->IsOpen());
	TestFalse(TEXT("reopening after completion cannot start a second recall"), Recall->IsRecallActive());
	TestFalse(TEXT("the player is still alive after opening the sortie screen"),
		Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestEqual(TEXT("the player character resolved the injected sortie widget"),
		Player->GetSpawnSelectWidget(), Sortie.Get());
	return true;
}

bool FMSSettingsExitButtonTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("exit test player spawned"), Player)) return false;
	APlayerController* PC = Scope.World->SpawnActor<APlayerController>();
	PC->Possess(Player);
	PC->SetAsLocalPlayerController();

	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Lobby->Initialize();
	Lobby->InitializeForAutomationTests();
	Lobby->Close();
	Player->SetLobbyWidgetForTesting(Lobby.Get());

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSUILockSubsystem* UILock = NewObject<UMSUILockSubsystem>(GameInstance);
	UMSSettingsWidget* Settings = MSSettingsWidgetTests::MakeInitializedWidget();
	Settings->SetUILockSubsystemForTesting(UILock);
	Settings->SetResortiePlayerForTesting(Player);
	Settings->Open();

	UButton* ExitButton = Settings->GetExitButtonForTesting();
	if (!TestNotNull(TEXT("Settings owns an Exit button"), ExitButton)) return false;
	TestFalse(TEXT("precondition: lobby starts closed"), Lobby->IsOpen());
	ExitButton->OnClicked.Broadcast();

	TestFalse(TEXT("Exit closes Settings"), Settings->IsOpen());
	TestTrue(TEXT("Exit opens MSLobbyWidget"), Lobby->IsOpen());
	TestEqual(TEXT("Exit retains the player's lobby widget"), Player->GetLobbyWidget(), Lobby.Get());
	return true;
}

bool FMSSettingsFieldOfViewTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedSettingsState SettingsState;
	MSSettingsWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("FOV test player spawned"), Player)) return false;
	UMSSettingsWidget* Widget = MSSettingsWidgetTests::MakeInitializedWidget();
	Widget->SetResortiePlayerForTesting(Player);
	USlider* Slider = Widget->GetFieldOfViewSliderForTesting();
	if (!TestNotNull(TEXT("Settings displays a FOV slider"), Slider)) return false;
	TestEqual(TEXT("FOV slider minimum is 60"), Slider->GetMinValue(), 60.0f);
	TestEqual(TEXT("FOV slider maximum is 110"), Slider->GetMaxValue(), 110.0f);

	Slider->SetValue(96.0f);
	TestEqual(TEXT("moving the FOV slider updates the camera immediately"),
		Player->GetMSCamera()->FieldOfView, 96.0f);
	Slider->OnMouseCaptureEnd.Broadcast();
	float Persisted = 0.0f;
	GConfig->GetFloat(TEXT("/Script/NeoMSGO.MSPlayerCharacter"), TEXT("DefaultCameraFieldOfView"),
		Persisted, GGameUserSettingsIni);
	TestEqual(TEXT("FOV value is persisted to game user settings"), Persisted, 96.0f);

	return true;
}

bool FMSSettingsCameraViewPresetTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedSettingsState SettingsState;
	MSSettingsWidgetTests::FScopedWorld Scope;AMSPlayerCharacter* Player=Scope.World->SpawnActor<AMSPlayerCharacter>();
	UMSSettingsWidget* Widget=MSSettingsWidgetTests::MakeInitializedWidget();Widget->SetResortiePlayerForTesting(Player);
	if(!TestNotNull(TEXT("right shoulder choice exists"),Widget->GetRightShoulderButtonForTesting())||!TestNotNull(TEXT("high overhead choice exists"),Widget->GetHighOverheadButtonForTesting()))return false;
	Widget->GetHighOverheadButtonForTesting()->OnClicked.Broadcast();TestEqual(TEXT("settings applies high overhead preset"),Player->GetCameraViewPreset(),EMSCameraViewPreset::HighOverhead);TestTrue(TEXT("high preset raises spring arm socket"),Player->GetCameraBoom()->SocketOffset.Z>100.0);
	int32 Persisted=-1;GConfig->GetInt(TEXT("/Script/NeoMSGO.MSPlayerCharacter"),TEXT("CameraViewPreset"),Persisted,GGameUserSettingsIni);TestEqual(TEXT("camera preset persists to game user settings"),Persisted,static_cast<int32>(EMSCameraViewPreset::HighOverhead));
	Widget->GetRightShoulderButtonForTesting()->OnClicked.Broadcast();TestEqual(TEXT("right shoulder can be restored"),Player->GetCameraViewPreset(),EMSCameraViewPreset::RightShoulder);return true;
}

bool FMSSettingsPresetVoiceTypeTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player=Scope.World->SpawnActor<AMSPlayerCharacter>();
	if(!TestNotNull(TEXT("voice test player spawned"),Player))return false;
	UMSGameInstance* GameInstance=NewObject<UMSGameInstance>();
	UMSSettingsWidget* Widget=MSSettingsWidgetTests::MakeInitializedWidget();Widget->SetGameInstanceForTesting(GameInstance);
	if(!TestNotNull(TEXT("hotblood choice exists"),Widget->GetHotbloodVoiceButtonForTesting())||
		!TestNotNull(TEXT("serious choice exists"),Widget->GetSeriousVoiceButtonForTesting())||
		!TestNotNull(TEXT("shy choice exists"),Widget->GetShyVoiceButtonForTesting()))return false;
	TestEqual(TEXT("preset voice defaults to Hotblood"),GameInstance->SelectedVoiceType,EMSVoiceType::Hotblood);
	Widget->GetShyVoiceButtonForTesting()->OnClicked.Broadcast();
	TestEqual(TEXT("clicking 内気 stores Shy in UMSGameInstance"),GameInstance->SelectedVoiceType,EMSVoiceType::Shy);

	UMSVoiceCutInSubsystem* VoiceSubsystem=NewObject<UMSVoiceCutInSubsystem>(GameInstance);
	VoiceSubsystem->ProcessKillConfirmed(Player,Player,1.0);
	FMSVoiceCutInRequest Request;
	TestTrue(TEXT("subsequent self event creates a cut-in"),VoiceSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left,Request));
	TestEqual(TEXT("subsequent self cut-in uses the selected Shy type"),Request.VoiceType,EMSVoiceType::Shy);
	bool bLineComesFromSelectedTable=false;
	for(const FText& Candidate:MSVoiceLibrary::GetLines(EMSVoiceType::Shy,EMSVoiceTrigger::Kill))
	{
		if(Candidate.EqualTo(Request.Line)){bLineComesFromSelectedTable=true;break;}
	}
	TestTrue(TEXT("subsequent self line comes from the Shy/Kill table"),bLineComesFromSelectedTable);
	TestEqual(TEXT("Shy portrait uses the required green family"),Request.PortraitColor,FLinearColor(0.18f,0.72f,0.38f,1.0f));
	return true;
}

bool FMSSettingsAudioPresetTest::RunTest(const FString&)
{
	MSSettingsWidgetTests::FScopedSettingsState SettingsState;
	MSAudioPreset::SetSelectedPreset(EMSAudioPreset::TypeA, false);
	UMSSettingsWidget* Widget = MSSettingsWidgetTests::MakeInitializedWidget();
	if (!TestNotNull(TEXT("TypeA audio choice exists"), Widget->GetAudioTypeAButtonForTesting()) ||
		!TestNotNull(TEXT("TypeB audio choice exists"), Widget->GetAudioTypeBButtonForTesting()))
	{
		return false;
	}
	Widget->GetAudioTypeBButtonForTesting()->OnClicked.Broadcast();
	TestEqual(TEXT("TypeB is applied immediately"), MSAudioPreset::GetSelectedPreset(), EMSAudioPreset::TypeB);
	int32 Persisted = INDEX_NONE;
	GConfig->GetInt(TEXT("/Script/NeoMSGO.MSAudioSettings"), TEXT("AudioPreset"),
		Persisted, GGameUserSettingsIni);
	TestEqual(TEXT("TypeB persists in GameUserSettings.ini"),
		Persisted, static_cast<int32>(EMSAudioPreset::TypeB));
	Widget->GetAudioTypeAButtonForTesting()->OnClicked.Broadcast();
	TestEqual(TEXT("TypeA can be restored"), MSAudioPreset::GetSelectedPreset(), EMSAudioPreset::TypeA);
	if (!TestNotNull(TEXT("Mode A UI choice exists"), Widget->GetUITypeAButtonForTesting()) ||
		!TestNotNull(TEXT("Mode B UI choice exists"), Widget->GetUITypeBButtonForTesting()))
	{
		return false;
	}
	Widget->GetUITypeBButtonForTesting()->OnClicked.Broadcast();
	TestEqual(TEXT("Mode B UI is applied for subsequent screen construction"),
		MSUIStyle::GetSelectedMode(), MSUIStyle::EUIMode::TypeB);
	Persisted = INDEX_NONE;
	GConfig->GetInt(TEXT("/Script/NeoMSGO.MSUISettings"), TEXT("UIMode"), Persisted, GGameUserSettingsIni);
	TestEqual(TEXT("Mode B UI persists in GameUserSettings.ini"),
		Persisted, static_cast<int32>(MSUIStyle::EUIMode::TypeB));
	Widget->GetUITypeAButtonForTesting()->OnClicked.Broadcast();
	TestEqual(TEXT("Mode A UI can be restored"), MSUIStyle::GetSelectedMode(), MSUIStyle::EUIMode::TypeA);
	return true;
}

#endif
