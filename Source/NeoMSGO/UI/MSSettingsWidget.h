#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputKeySelector.h"
#include "Audio/MSAudioPreset.h"
#include "UI/MSUIStyle.h"
#include "Voice/MSNavigatorTypes.h"
#include "Voice/MSVoiceSettings.h"
#include "Voice/MSVoiceTypes.h"
#include "MSSettingsWidget.generated.h"

class AMSPlayerCharacter;
class UButton;
class UEnhancedInputUserSettings;
class UInputAction;
class UInputMappingContext;
class USlider;
class UTextBlock;
class UMSSettingsWidget;
class UMSUILockSubsystem;
class UMSGameInstance;
class UMSLoginWidget;
class UMSCreditsWidget;

/** InputKeySelector carrying the mapping identity for its settings row. */
UCLASS()
class NEOMSGO_API UMSSettingsKeySelector : public UInputKeySelector
{
	GENERATED_BODY()

public:
	void InitializeForMapping(UMSSettingsWidget* InOwner, FName InMappingName);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void ForwardSelectedKey(FInputChord InSelectedKey);
	UFUNCTION()
	void ForwardSelectionStateChanged();

	UPROPERTY(Transient)
	TObjectPtr<UMSSettingsWidget> SettingsOwner;

	FName MappingName;
};

/** Code-only settings screen for camera options and safe, scalar key bindings. */
UCLASS()
class NEOMSGO_API UMSSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr float MinMouseSensitivity = 0.01f;
	static constexpr float MaxMouseSensitivity = 2.0f;
	/** Matches AMSPlayerCharacter's default LookSensitivityScale (P3-5). */
	static constexpr float DefaultMouseSensitivity = 0.35f;
	static constexpr float MinFieldOfView = 60.0f;
	static constexpr float MaxFieldOfView = 110.0f;
	static constexpr float DefaultFieldOfView = 60.0f;
	static constexpr float MinMasterVolume = 0.0f;
	static constexpr float MaxMasterVolume = 1.0f;
	static constexpr float DefaultMasterVolume = 1.0f;
	static constexpr float DefaultBgmVolume = 0.75f;
	static const FName SettingsScreenId;

	/** IA_Move and IA_Look are intentionally absent: their composite mappings are protected. */
	static const TArray<FName>& GetRebindableActionNames();

	/** Adds UE 5.7 player-mappable metadata in memory; no Content asset is dirtied. */
	static void PreparePlayerMappableInputActions();

	UFUNCTION(BlueprintCallable, Category = "MS|Settings")
	void OpenSettings();

	UFUNCTION(BlueprintCallable, Category = "MS|Settings")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "MS|Settings")
	void Close();

	UFUNCTION(BlueprintPure, Category = "MS|Settings")
	bool IsOpen() const { return bIsOpen; }

	/** True only while one of the actual key selectors is waiting for a key. */
	bool IsSelectingKey() const;

	void ApplySelectedKey(FName MappingName, const FInputChord& SelectedKey);
	/** Refreshes only the visible key label while a selector waits for input. */
	void HandleKeySelectionStateChanged(FName MappingName, bool bIsSelecting);

#if WITH_DEV_AUTOMATION_TESTS
	/** Native C++ widgets normally initialize with a local-player context; headless tests supply none. */
	void InitializeForAutomationTests();
	USlider* GetSensitivitySliderForTesting() const { return SensitivitySlider; }
	USlider* GetFieldOfViewSliderForTesting() const { return FieldOfViewSlider; }
	USlider* GetMasterVolumeSliderForTesting() const { return MasterVolumeSlider; }
	USlider* GetBgmVolumeSliderForTesting() const { return BgmVolumeSlider; }
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
	UButton* GetResortieButtonForTesting() const { return ResortieButton; }
	FText GetResortieLabelForTesting() const;
	FText GetResortieTooltipForTesting() const;
	UButton* GetExitButtonForTesting() const { return ExitButton; }
	UButton* GetLogoutButtonForTesting() const { return LogoutButton; }
	UButton* GetCreditsButtonForTesting() const { return CreditsButton; }
	UMSCreditsWidget* GetCreditsWidgetForTesting() const { return CreditsWidget; }
	void SetLoginWidgetForTesting(UMSLoginWidget* InWidget) { TestLoginWidget = InWidget; }
	UButton* GetRightShoulderButtonForTesting() const { return RightShoulderButton; }
	UButton* GetHighOverheadButtonForTesting() const { return HighOverheadButton; }
	UButton* GetHotbloodVoiceButtonForTesting() const { return HotbloodVoiceButton; }
	UButton* GetSeriousVoiceButtonForTesting() const { return SeriousVoiceButton; }
	UButton* GetShyVoiceButtonForTesting() const { return ShyVoiceButton; }
	UButton* GetNoelAndersonButtonForTesting() const { return NoelAndersonButton; }
	UButton* GetYuukiNakasatoButtonForTesting() const { return YuukiNakasatoButton; }
	UButton* GetPresetVoiceOnButtonForTesting() const { return PresetVoiceOnButton; }
	UButton* GetPresetVoiceOffButtonForTesting() const { return PresetVoiceOffButton; }
	UButton* GetNavigatorVoiceOnButtonForTesting() const { return NavigatorVoiceOnButton; }
	UButton* GetNavigatorVoiceOffButtonForTesting() const { return NavigatorVoiceOffButton; }
	UButton* GetAudioTypeAButtonForTesting() const { return AudioTypeAButton; }
	UButton* GetAudioTypeBButtonForTesting() const { return AudioTypeBButton; }
	UButton* GetUITypeAButtonForTesting() const { return UITypeAButton; }
	UButton* GetUITypeBButtonForTesting() const { return UITypeBButton; }
	void SetGameInstanceForTesting(UMSGameInstance* InGameInstance) { TestGameInstance = InGameInstance; }
	/** Headless tests have no LocalPlayer, so GetOwningPlayerPawn cannot resolve the player. */
	void SetResortiePlayerForTesting(AMSPlayerCharacter* InPlayer) { TestResortiePlayer = InPlayer; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION()
	void HandleSensitivityChanged(float NewValue);

	UFUNCTION()
	void HandleSensitivityCaptureEnded();

	UFUNCTION()
	void HandleFieldOfViewChanged(float NewValue);

	UFUNCTION()
	void HandleFieldOfViewCaptureEnded();

	UFUNCTION()
	void HandleMasterVolumeChanged(float NewValue);

	UFUNCTION()
	void HandleMasterVolumeCaptureEnded();

	UFUNCTION()
	void HandleBgmVolumeChanged(float NewValue);

	UFUNCTION()
	void HandleBgmVolumeCaptureEnded();

	UFUNCTION() void HandleRightShoulderClicked();
	UFUNCTION() void HandleHighOverheadClicked();
	UFUNCTION() void HandleHotbloodVoiceClicked();
	UFUNCTION() void HandleSeriousVoiceClicked();
	UFUNCTION() void HandleShyVoiceClicked();
	UFUNCTION() void HandleAudioTypeAClicked();
	UFUNCTION() void HandleAudioTypeBClicked();
	UFUNCTION() void HandleUITypeAClicked();
	UFUNCTION() void HandleUITypeBClicked();
	UFUNCTION() void HandleNoelAndersonClicked();
	UFUNCTION() void HandleYuukiNakasatoClicked();
	UFUNCTION() void HandlePresetVoiceOnClicked();
	UFUNCTION() void HandlePresetVoiceOffClicked();
	UFUNCTION() void HandleNavigatorVoiceOnClicked();
	UFUNCTION() void HandleNavigatorVoiceOffClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleResetClicked();

	/** Closes Settings and requests living recall, or opens selection after recall/death. */
	UFUNCTION()
	void HandleResortieClicked();

	/** Closes Settings and returns to the lobby without firing a battle-end event. */
	UFUNCTION()
	void HandleExitClicked();

	/** Clears the GameInstance login state and returns to the login screen. */
	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleCreditsClicked();

	AMSPlayerCharacter* ResolveResortiePlayer() const;

	UEnhancedInputUserSettings* GetInputUserSettings() const;
	UInputMappingContext* GetPlayerMappingContext() const;
	UMSUILockSubsystem* GetUILockSubsystem() const;
	void RefreshDisplayedSettings();
	void RefreshResortiePresentation();
	void ApplySensitivityToPlayer(bool bSave);
	void ApplyFieldOfViewToPlayer(bool bSave);
	void ApplyMasterVolume(bool bSave);
	void ApplyBgmVolume(bool bSave);
	void ApplyVoiceType(EMSVoiceType VoiceType);
	UMSGameInstance* ResolveMSGameInstance() const;
	void RefreshVoiceTypeButtons();
	void ApplyAudioPreset(EMSAudioPreset Preset);
	void RefreshAudioPresetButtons();
	void ApplyUIMode(MSUIStyle::EUIMode Mode);
	void RefreshUIModeButtons();
	void ApplyNavigator(EMSNavigator Navigator);
	void RefreshNavigatorButtons();
	void ApplyVoiceEnabled(EMSVoiceCategory Category, bool bEnabled);
	void RefreshVoiceEnabledButtons();

	UPROPERTY(Transient)
	TObjectPtr<USlider> SensitivitySlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SensitivityValueText;

	UPROPERTY(Transient)
	TObjectPtr<USlider> FieldOfViewSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FieldOfViewValueText;

	UPROPERTY(Transient)
	TObjectPtr<USlider> MasterVolumeSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MasterVolumeValueText;

	UPROPERTY(Transient)
	TObjectPtr<USlider> BgmVolumeSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BgmVolumeValueText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMSSettingsKeySelector>> KeySelectors;

	/** Compact, localized text drawn over each selector; the selector keeps all input behavior. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> KeyDisplayTexts;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> RebindableActions;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ResortieButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ResortieLabel;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ExitButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LogoutButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CreditsButton;

	UPROPERTY(Transient)
	TObjectPtr<UMSCreditsWidget> CreditsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMSLoginWidget> TestLoginWidget;

	UPROPERTY(Transient) TObjectPtr<UButton> RightShoulderButton;
	UPROPERTY(Transient) TObjectPtr<UButton> HighOverheadButton;
	UPROPERTY(Transient) TObjectPtr<UButton> HotbloodVoiceButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SeriousVoiceButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ShyVoiceButton;
	UPROPERTY(Transient) TObjectPtr<UButton> AudioTypeAButton;
	UPROPERTY(Transient) TObjectPtr<UButton> AudioTypeBButton;
	UPROPERTY(Transient) TObjectPtr<UButton> UITypeAButton;
	UPROPERTY(Transient) TObjectPtr<UButton> UITypeBButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NoelAndersonButton;
	UPROPERTY(Transient) TObjectPtr<UButton> YuukiNakasatoButton;
	UPROPERTY(Transient) TObjectPtr<UButton> PresetVoiceOnButton;
	UPROPERTY(Transient) TObjectPtr<UButton> PresetVoiceOffButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NavigatorVoiceOnButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NavigatorVoiceOffButton;

	UPROPERTY(Transient)
	TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<AMSPlayerCharacter> TestResortiePlayer;
	UPROPERTY(Transient)
	TObjectPtr<UMSGameInstance> TestGameInstance;

	float CurrentSensitivity = 0.35f;
	float CurrentFieldOfView = 60.0f;
	float CurrentMasterVolume = 1.0f;
	float CurrentBgmVolume = DefaultBgmVolume;
	bool bIsOpen = false;
};
