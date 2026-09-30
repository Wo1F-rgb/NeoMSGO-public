#include "UI/MSSettingsWidget.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Online/MSRoomSubsystem.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "AudioDevice.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameInstance.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSPlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "PlayerMappableKeySettings.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSCreditsWidget.h"
#include "UI/MSLoginWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Voice/MSNavigatorSubsystem.h"
#include "Voice/MSVoiceSettings.h"

namespace
{
	struct FMSSettingsBindingDefinition
	{
		const TCHAR* ActionName;
		const TCHAR* DisplayName;
		const TCHAR* MappingName;
		const TCHAR* AssetPath;
		FKey DefaultKey;
	};

	const TArray<FMSSettingsBindingDefinition>& GetBindingDefinitions()
	{
		// Never add IA_Move or IA_Look here. They are composite/axis bindings and must remain protected.
		static const TArray<FMSSettingsBindingDefinition> Definitions = {
			{ TEXT("IA_Jump"), TEXT("ジャンプ"), TEXT("Jump"), TEXT("/Game/Input/Actions/MS/IA_Jump.IA_Jump"), EKeys::LeftShift },
			{ TEXT("IA_Dash"), TEXT("ダッシュ"), TEXT("Dash"), TEXT("/Game/Input/Actions/MS/IA_Dash.IA_Dash"), EKeys::SpaceBar },
			{ TEXT("IA_Crouch"), TEXT("しゃがみ"), TEXT("Crouch"), TEXT("/Game/Input/Actions/MS/IA_Crouch.IA_Crouch"), EKeys::LeftControl },
			{ TEXT("IA_Fire"), TEXT("射撃"), TEXT("Fire"), TEXT("/Game/Input/Actions/MS/IA_Fire.IA_Fire"), EKeys::LeftMouseButton },
			{ TEXT("IA_Reload"), TEXT("リロード"), TEXT("Reload"), TEXT("/Game/Input/Actions/MS/IA_Reload.IA_Reload"), EKeys::E },
			{ TEXT("IA_LockOn"), TEXT("ロックオン"), TEXT("LockOn"), TEXT("/Game/Input/Actions/MS/IA_LockOn.IA_LockOn"), EKeys::RightMouseButton },
			{ TEXT("IA_Action"), TEXT("アクション"), TEXT("Action"), TEXT("/Game/Input/Actions/MS/IA_Action.IA_Action"), EKeys::G },
			{ TEXT("IA_Weapon1"), TEXT("武器1"), TEXT("Weapon1"), TEXT("/Game/Input/Actions/MS/IA_Weapon1.IA_Weapon1"), EKeys::One },
			{ TEXT("IA_Weapon2"), TEXT("武器2"), TEXT("Weapon2"), TEXT("/Game/Input/Actions/MS/IA_Weapon2.IA_Weapon2"), EKeys::Two },
			{ TEXT("IA_Weapon3"), TEXT("武器3"), TEXT("Weapon3"), TEXT("/Game/Input/Actions/MS/IA_Weapon3.IA_Weapon3"), EKeys::Three },
			{ TEXT("IA_Module"), TEXT("固有モジュール（1秒長押し）"), TEXT("Module"), TEXT("/Game/Input/Actions/MS/IA_Module.IA_Module"), EKeys::F },
			{ TEXT("IA_ViewToggle"), TEXT("視点切替"), TEXT("ViewToggle"), TEXT("/Game/Input/Actions/MS/IA_ViewToggle.IA_ViewToggle"), EKeys::P },
		};
		return Definitions;
	}

	const FMSSettingsBindingDefinition* FindBindingDefinition(const FName MappingName)
	{
		return GetBindingDefinitions().FindByPredicate([MappingName](const FMSSettingsBindingDefinition& Definition)
		{
			return MappingName == Definition.MappingName;
		});
	}

	const TCHAR* SensitivityConfigSection = TEXT("/Script/NeoMSGO.MSPlayerCharacter");
	const TCHAR* SensitivityConfigKey = TEXT("LookSensitivityScale");
	const TCHAR* FieldOfViewConfigKey = TEXT("DefaultCameraFieldOfView");
	const TCHAR* CameraViewPresetConfigKey = TEXT("CameraViewPreset");
	const TCHAR* AudioSettingsConfigSection = TEXT("/Script/NeoMSGO.MSAudioSettings");
	const TCHAR* MasterVolumeConfigKey = TEXT("MasterVolume");

	FText CompactKeyDisplayName(const FKey& Key)
	{
		if (Key == EKeys::SpaceBar) return FText::FromString(TEXT("スペース"));
		if (Key == EKeys::LeftShift) return FText::FromString(TEXT("左Shift"));
		if (Key == EKeys::RightShift) return FText::FromString(TEXT("右Shift"));
		if (Key == EKeys::LeftControl) return FText::FromString(TEXT("左Ctrl"));
		if (Key == EKeys::RightControl) return FText::FromString(TEXT("右Ctrl"));
		if (Key == EKeys::LeftAlt) return FText::FromString(TEXT("左Alt"));
		if (Key == EKeys::RightAlt) return FText::FromString(TEXT("右Alt"));
		if (Key == EKeys::LeftMouseButton) return FText::FromString(TEXT("マウス左"));
		if (Key == EKeys::RightMouseButton) return FText::FromString(TEXT("マウス右"));
		if (Key == EKeys::MiddleMouseButton) return FText::FromString(TEXT("マウス中"));
		const FString Display = Key.GetDisplayName().ToString();
		return FText::FromString(Display.IsEmpty() ? Key.ToString() : Display);
	}

	UTextBlock* MakeSettingsLabel(UWidgetTree* Tree, const FText& Text)
	{
		UTextBlock* Label = MSUIStyle::MakeText(Tree, Text, MSUIStyle::ETextRole::Body,
			false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		FSlateFontInfo Font = Label->GetFont(); Font.Size = 14; Label->SetFont(Font);
		return Label;
	}
}

const FName UMSSettingsWidget::SettingsScreenId(TEXT("Settings"));

void UMSSettingsKeySelector::InitializeForMapping(UMSSettingsWidget* InOwner, const FName InMappingName)
{
	SettingsOwner = InOwner;
	MappingName = InMappingName;
	OnKeySelected.AddDynamic(this, &UMSSettingsKeySelector::ForwardSelectedKey);
	OnIsSelectingKeyChanged.AddDynamic(this, &UMSSettingsKeySelector::ForwardSelectionStateChanged);
}

TSharedRef<SWidget> UMSSettingsKeySelector::RebuildWidget()
{
	// SetTextBlockVisibility before RebuildWidget only changes the UWidget
	// property path; apply it again after Slate has created its internal text.
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SetTextBlockVisibility(ESlateVisibility::Collapsed);
	return Result;
}

void UMSSettingsKeySelector::ForwardSelectedKey(const FInputChord InSelectedKey)
{
	if (SettingsOwner)
	{
		SettingsOwner->ApplySelectedKey(MappingName, InSelectedKey);
	}
}

void UMSSettingsKeySelector::ForwardSelectionStateChanged()
{
	if (SettingsOwner)
	{
		SettingsOwner->HandleKeySelectionStateChanged(MappingName, GetIsSelectingKey());
	}
}

const TArray<FName>& UMSSettingsWidget::GetRebindableActionNames()
{
	static const TArray<FName> Names = []
	{
		TArray<FName> Result;
		for (const FMSSettingsBindingDefinition& Definition : GetBindingDefinitions())
		{
			Result.Emplace(Definition.ActionName);
		}
		return Result;
	}();
	return Names;
}

void UMSSettingsWidget::PreparePlayerMappableInputActions()
{
	for (const FMSSettingsBindingDefinition& Definition : GetBindingDefinitions())
	{
		UInputAction* Action = LoadObject<UInputAction>(nullptr, Definition.AssetPath);
		if (!Action || Action->GetPlayerMappableKeySettings())
		{
			continue;
		}

		// The migrated assets predate UE 5.7's UPlayerMappableKeySettings. Attach transient metadata so
		// EnhancedInputUserSettings can register/save them without modifying anything under Content/.
		UPlayerMappableKeySettings* KeySettings = NewObject<UPlayerMappableKeySettings>(Action, NAME_None, RF_Transient);
		KeySettings->Name = Definition.MappingName;
		KeySettings->DisplayName = FText::FromString(Definition.DisplayName);
		KeySettings->DisplayCategory = FText::FromString(TEXT("Controls"));
		auto& SettingsReference = const_cast<TObjectPtr<UPlayerMappableKeySettings>&>(Action->GetPlayerMappableKeySettings());
		SettingsReference = KeySettings;
	}
}

void UMSSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	// A bare UVerticalBox as the root stretches every row to full screen width, which is why the
	// panel previously rendered as an unusably wide strip. Every other screen in this project
	// (Lobby/Sortie/PauseMenu) instead centers its content in a fixed-width box; match that here.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Background, MSUIStyle::EBrush::PanelSolid, MSUIStyleColor::PanelBackground);
	UCanvasPanelSlot* BackgroundSlot = Root->AddChildToCanvas(Background);
	BackgroundSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); BackgroundSlot->SetOffsets(FMargin(0.0f));

	USizeBox* CenterBox = WidgetTree->ConstructWidget<USizeBox>();
	CenterBox->SetWidthOverride(720.0f);
	CenterBox->SetHeightOverride(680.0f);
	UCanvasPanelSlot* CenterSlot = Root->AddChildToCanvas(CenterBox);
	CenterSlot->SetAnchors(FAnchors(0.5f, 0.5f)); CenterSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CenterSlot->SetAutoSize(true);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	CenterBox->SetContent(Content);

	UTextBlock* Title = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("設定")),
		MSUIStyle::ETextRole::H1, false, MSUIStyleColor::FrameAccent);
	Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(12.0f));

	UScrollBox* SettingsScroll = WidgetTree->ConstructWidget<UScrollBox>();
	SettingsScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	Content->AddChildToVerticalBox(SettingsScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* Sections = WidgetTree->ConstructWidget<UVerticalBox>();
	SettingsScroll->AddChild(Sections);

	auto AddSection = [this, Sections](const TCHAR* Heading)
	{
		UVerticalBox* SectionContent = WidgetTree->ConstructWidget<UVerticalBox>();
		SectionContent->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree,
			FText::FromString(Heading), MSUIStyle::ETextRole::H2, false,
			MSUIStyleColor::FrameAccent))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, MSUIStyleSize::Space2));
		UVerticalBox* SectionBody = WidgetTree->ConstructWidget<UVerticalBox>();
		SectionContent->AddChildToVerticalBox(SectionBody);
		UBorder* InnerBorder = nullptr;
		UBorder* Frame = MSUIStyle::MakeFramedPanel(WidgetTree, InnerBorder, SectionContent, false,
			MSUIStyleColor::FrameAccent, MSUIStyleColor::PanelBackground, FMargin(MSUIStyleSize::Space3));
		Sections->AddChildToVerticalBox(Frame)->SetPadding(FMargin(8.0f, 4.0f));
		return SectionBody;
	};

	// Create every section up front so the visual order remains stable even though the retained
	// feature handlers below are initialized in their original order.
	UVerticalBox* ControlsSection = AddSection(TEXT("操作"));
	UVerticalBox* CameraSection = AddSection(TEXT("カメラ"));
	UVerticalBox* DisplaySection = AddSection(TEXT("表示"));
	UVerticalBox* SoundSection = AddSection(TEXT("サウンド"));
	UVerticalBox* VoiceSection = AddSection(TEXT("ボイス"));
	UVerticalBox* ActionsSection = AddSection(TEXT("アクション"));

	UHorizontalBox* UIModeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	DisplaySection->AddChildToVerticalBox(UIModeRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* UIModeLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("UIモード")),
		MSUIStyle::ETextRole::Body);
	UIModeRow->AddChildToHorizontalBox(UIModeLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddUIModeButton = [this, UIModeRow](const TCHAR* Label)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label),
			MSUIStyle::ETextRole::Body, true, MSUIStyleColor::TextPrimary,
			MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Text);
		UIModeRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	UITypeAButton = AddUIModeButton(TEXT("A"));
	UITypeAButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleUITypeAClicked);
	UITypeBButton = AddUIModeButton(TEXT("B"));
	UITypeBButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleUITypeBClicked);

	UHorizontalBox* SensitivityRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	ControlsSection->AddChildToVerticalBox(SensitivityRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* SensitivityLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("マウス感度")));
	SensitivityRow->AddChildToHorizontalBox(SensitivityLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SensitivitySlider = WidgetTree->ConstructWidget<USlider>();
	SensitivitySlider->SetMinValue(MinMouseSensitivity);
	SensitivitySlider->SetMaxValue(MaxMouseSensitivity);
	SensitivitySlider->SetStepSize(0.01f);
	SensitivitySlider->OnValueChanged.AddDynamic(this, &UMSSettingsWidget::HandleSensitivityChanged);
	SensitivitySlider->OnMouseCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleSensitivityCaptureEnded);
	SensitivitySlider->OnControllerCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleSensitivityCaptureEnded);
	USizeBox* SliderSize = WidgetTree->ConstructWidget<USizeBox>();
	SliderSize->SetWidthOverride(200.0f);
	SliderSize->SetHeightOverride(32.0f);
	SliderSize->AddChild(SensitivitySlider);
	SensitivityRow->AddChildToHorizontalBox(SliderSize);
	SensitivityValueText = WidgetTree->ConstructWidget<UTextBlock>();
	SensitivityValueText->SetJustification(ETextJustify::Right);
	USizeBox* ValueTextSize = WidgetTree->ConstructWidget<USizeBox>();
	ValueTextSize->SetWidthOverride(44.0f);
	ValueTextSize->AddChild(SensitivityValueText);
	SensitivityRow->AddChildToHorizontalBox(ValueTextSize)->SetPadding(FMargin(6.0f, 0, 0, 0));

	UHorizontalBox* FieldOfViewRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	CameraSection->AddChildToVerticalBox(FieldOfViewRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* FieldOfViewLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("視野角")));
	FieldOfViewRow->AddChildToHorizontalBox(FieldOfViewLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FieldOfViewSlider = WidgetTree->ConstructWidget<USlider>();
	FieldOfViewSlider->SetMinValue(MinFieldOfView);
	FieldOfViewSlider->SetMaxValue(MaxFieldOfView);
	FieldOfViewSlider->SetStepSize(1.0f);
	FieldOfViewSlider->OnValueChanged.AddDynamic(this, &UMSSettingsWidget::HandleFieldOfViewChanged);
	FieldOfViewSlider->OnMouseCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleFieldOfViewCaptureEnded);
	FieldOfViewSlider->OnControllerCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleFieldOfViewCaptureEnded);
	USizeBox* FieldOfViewSliderSize = WidgetTree->ConstructWidget<USizeBox>();
	FieldOfViewSliderSize->SetWidthOverride(200.0f);
	FieldOfViewSliderSize->SetHeightOverride(32.0f);
	FieldOfViewSliderSize->AddChild(FieldOfViewSlider);
	FieldOfViewRow->AddChildToHorizontalBox(FieldOfViewSliderSize);
	FieldOfViewValueText = WidgetTree->ConstructWidget<UTextBlock>();
	FieldOfViewValueText->SetJustification(ETextJustify::Right);
	USizeBox* FieldOfViewValueSize = WidgetTree->ConstructWidget<USizeBox>();
	FieldOfViewValueSize->SetWidthOverride(44.0f);
	FieldOfViewValueSize->AddChild(FieldOfViewValueText);
	FieldOfViewRow->AddChildToHorizontalBox(FieldOfViewValueSize)->SetPadding(FMargin(6.0f, 0, 0, 0));

	UHorizontalBox* CameraViewRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	CameraSection->AddChildToVerticalBox(CameraViewRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* CameraViewLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("カメラ視点")));
	CameraViewRow->AddChildToHorizontalBox(CameraViewLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RightShoulderButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ShoulderText = WidgetTree->ConstructWidget<UTextBlock>(); ShoulderText->SetText(FText::FromString(TEXT("右肩")));
	RightShoulderButton->SetContent(ShoulderText); RightShoulderButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleRightShoulderClicked);
	CameraViewRow->AddChildToHorizontalBox(RightShoulderButton)->SetPadding(FMargin(4, 0));
	HighOverheadButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* OverheadText = WidgetTree->ConstructWidget<UTextBlock>(); OverheadText->SetText(FText::FromString(TEXT("頭上高め")));
	HighOverheadButton->SetContent(OverheadText); HighOverheadButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleHighOverheadClicked);
	CameraViewRow->AddChildToHorizontalBox(HighOverheadButton)->SetPadding(FMargin(4, 0));

	UHorizontalBox* VoiceTypeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	VoiceSection->AddChildToVerticalBox(VoiceTypeRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* VoiceTypeLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("プリセットボイス")));
	VoiceTypeRow->AddChildToHorizontalBox(VoiceTypeLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddVoiceButton = [this, VoiceTypeRow](const TCHAR* Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Label));
		Button->SetContent(Text); VoiceTypeRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	HotbloodVoiceButton=AddVoiceButton(TEXT("熱血"));HotbloodVoiceButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleHotbloodVoiceClicked);
	SeriousVoiceButton=AddVoiceButton(TEXT("真面目"));SeriousVoiceButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleSeriousVoiceClicked);
	ShyVoiceButton=AddVoiceButton(TEXT("内気"));ShyVoiceButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleShyVoiceClicked);

	UHorizontalBox* PresetVoiceEnabledRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	VoiceSection->AddChildToVerticalBox(PresetVoiceEnabledRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* PresetVoiceEnabledLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("プリセットボイス ON/OFF")));
	PresetVoiceEnabledRow->AddChildToHorizontalBox(PresetVoiceEnabledLabel)
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddPresetVoiceEnabledButton = [this, PresetVoiceEnabledRow](const TCHAR* Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));
		Button->SetContent(Text);
		PresetVoiceEnabledRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	PresetVoiceOnButton = AddPresetVoiceEnabledButton(TEXT("ON"));
	PresetVoiceOnButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandlePresetVoiceOnClicked);
	PresetVoiceOffButton = AddPresetVoiceEnabledButton(TEXT("OFF"));
	PresetVoiceOffButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandlePresetVoiceOffClicked);

	UHorizontalBox* AudioPresetRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	SoundSection->AddChildToVerticalBox(AudioPresetRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* AudioPresetLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("音声プリセット")));
	AudioPresetRow->AddChildToHorizontalBox(AudioPresetLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddAudioPresetButton = [this, AudioPresetRow](const TCHAR* Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));
		Button->SetContent(Text);
		AudioPresetRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	AudioTypeAButton = AddAudioPresetButton(TEXT("TypeA"));
	AudioTypeAButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleAudioTypeAClicked);
	AudioTypeBButton = AddAudioPresetButton(TEXT("TypeB"));
	AudioTypeBButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleAudioTypeBClicked);

	UHorizontalBox* MasterVolumeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	SoundSection->AddChildToVerticalBox(MasterVolumeRow)->SetPadding(FMargin(4.0f, 6.0f));
	MasterVolumeRow->AddChildToHorizontalBox(MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("マスター音量")), MSUIStyle::ETextRole::Body))
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	MasterVolumeSlider = WidgetTree->ConstructWidget<USlider>();
	MasterVolumeSlider->SetMinValue(MinMasterVolume);
	MasterVolumeSlider->SetMaxValue(MaxMasterVolume);
	MasterVolumeSlider->SetStepSize(0.01f);
	MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UMSSettingsWidget::HandleMasterVolumeChanged);
	MasterVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleMasterVolumeCaptureEnded);
	MasterVolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleMasterVolumeCaptureEnded);
	USizeBox* MasterVolumeSliderSize = WidgetTree->ConstructWidget<USizeBox>();
	MasterVolumeSliderSize->SetWidthOverride(200.0f);
	MasterVolumeSliderSize->SetHeightOverride(32.0f);
	MasterVolumeSliderSize->SetContent(MasterVolumeSlider);
	MasterVolumeRow->AddChildToHorizontalBox(MasterVolumeSliderSize);
	MasterVolumeValueText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(),
		MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextSecondary);
	MasterVolumeValueText->SetJustification(ETextJustify::Right);
	USizeBox* MasterVolumeValueSize = WidgetTree->ConstructWidget<USizeBox>();
	MasterVolumeValueSize->SetWidthOverride(52.0f);
	MasterVolumeValueSize->SetContent(MasterVolumeValueText);
	MasterVolumeRow->AddChildToHorizontalBox(MasterVolumeValueSize)->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));

	UHorizontalBox* BgmVolumeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	SoundSection->AddChildToVerticalBox(BgmVolumeRow)->SetPadding(FMargin(4.0f, 6.0f));
	BgmVolumeRow->AddChildToHorizontalBox(MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("BGM音量"))))
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BgmVolumeSlider = WidgetTree->ConstructWidget<USlider>();
	BgmVolumeSlider->SetMinValue(MinMasterVolume);
	BgmVolumeSlider->SetMaxValue(MaxMasterVolume);
	BgmVolumeSlider->SetStepSize(0.01f);
	BgmVolumeSlider->OnValueChanged.AddDynamic(this, &UMSSettingsWidget::HandleBgmVolumeChanged);
	BgmVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleBgmVolumeCaptureEnded);
	BgmVolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UMSSettingsWidget::HandleBgmVolumeCaptureEnded);
	USizeBox* BgmVolumeSliderSize = WidgetTree->ConstructWidget<USizeBox>();
	BgmVolumeSliderSize->SetWidthOverride(200.0f);
	BgmVolumeSliderSize->SetHeightOverride(32.0f);
	BgmVolumeSliderSize->SetContent(BgmVolumeSlider);
	BgmVolumeRow->AddChildToHorizontalBox(BgmVolumeSliderSize);
	BgmVolumeValueText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(),
		MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextSecondary);
	BgmVolumeValueText->SetJustification(ETextJustify::Right);
	USizeBox* BgmVolumeValueSize = WidgetTree->ConstructWidget<USizeBox>();
	BgmVolumeValueSize->SetWidthOverride(52.0f);
	BgmVolumeValueSize->SetContent(BgmVolumeValueText);
	BgmVolumeRow->AddChildToHorizontalBox(BgmVolumeValueSize)->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));

	UHorizontalBox* NavigatorRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	VoiceSection->AddChildToVerticalBox(NavigatorRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* NavigatorLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("ナビゲーター")));
	NavigatorRow->AddChildToHorizontalBox(NavigatorLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddNavigatorButton = [this, NavigatorRow](const TCHAR* Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));
		Button->SetContent(Text);
		NavigatorRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	NoelAndersonButton = AddNavigatorButton(*UMSNavigatorSubsystem::GetNavigatorDisplayName(EMSNavigator::NoelAnderson).ToString());
	NoelAndersonButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleNoelAndersonClicked);
	YuukiNakasatoButton = AddNavigatorButton(*UMSNavigatorSubsystem::GetNavigatorDisplayName(EMSNavigator::YuukiNakasato).ToString());
	YuukiNakasatoButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleYuukiNakasatoClicked);

	UHorizontalBox* NavigatorVoiceEnabledRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	VoiceSection->AddChildToVerticalBox(NavigatorVoiceEnabledRow)->SetPadding(FMargin(4.0f, 6.0f));
	UTextBlock* NavigatorVoiceEnabledLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("ナビゲーター ON/OFF")));
	NavigatorVoiceEnabledRow->AddChildToHorizontalBox(NavigatorVoiceEnabledLabel)
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto AddNavigatorVoiceEnabledButton = [this, NavigatorVoiceEnabledRow](const TCHAR* Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));
		Button->SetContent(Text);
		NavigatorVoiceEnabledRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	NavigatorVoiceOnButton = AddNavigatorVoiceEnabledButton(TEXT("ON"));
	NavigatorVoiceOnButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleNavigatorVoiceOnClicked);
	NavigatorVoiceOffButton = AddNavigatorVoiceEnabledButton(TEXT("OFF"));
	NavigatorVoiceOffButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleNavigatorVoiceOffClicked);

	UVerticalBox* BindingsList = WidgetTree->ConstructWidget<UVerticalBox>();
	ControlsSection->AddChildToVerticalBox(BindingsList)->SetPadding(FMargin(4.0f, 6.0f));
	KeyDisplayTexts.Reset();

	PreparePlayerMappableInputActions();
	for (const FMSSettingsBindingDefinition& Definition : GetBindingDefinitions())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		BindingsList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 2.0f));
		UTextBlock* Label = MakeSettingsLabel(WidgetTree, FText::FromString(Definition.DisplayName));
		Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UMSSettingsKeySelector* Selector = WidgetTree->ConstructWidget<UMSSettingsKeySelector>();
		Selector->SetAllowModifierKeys(false);
		Selector->SetAllowGamepadKeys(true);
		Selector->SetEscapeKeys({ EKeys::Escape });
		Selector->SetTextBlockVisibility(ESlateVisibility::Collapsed);
		Selector->SetMargin(FMargin(4.0f, 1.0f));
		FButtonStyle KeyButtonStyle = Selector->GetButtonStyle();
		FSlateBrush KeyNormal; KeyNormal.DrawAs = ESlateBrushDrawType::Box;
		KeyNormal.TintColor = FLinearColor(0.015f, 0.055f, 0.095f, 0.98f);
		FSlateBrush KeyHover = KeyNormal; KeyHover.TintColor = FLinearColor(0.045f, 0.20f, 0.25f, 1.0f);
		FSlateBrush KeyPressed = KeyNormal; KeyPressed.TintColor = FLinearColor(0.08f, 0.34f, 0.38f, 1.0f);
		FSlateBrush KeyDisabled = KeyNormal; KeyDisabled.TintColor = FLinearColor(0.015f, 0.03f, 0.05f, 0.72f);
		KeyButtonStyle.SetNormal(KeyNormal); KeyButtonStyle.SetHovered(KeyHover);
		KeyButtonStyle.SetPressed(KeyPressed); KeyButtonStyle.SetDisabled(KeyDisabled);
		Selector->SetButtonStyle(KeyButtonStyle);
		FTextBlockStyle KeyTextStyle = Selector->GetTextStyle();
		KeyTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::White);
		KeyTextStyle.Font.Size = 12;
		Selector->SetTextStyle(KeyTextStyle);
		Selector->InitializeForMapping(this, Definition.MappingName);
		USizeBox* SelectorSize = WidgetTree->ConstructWidget<USizeBox>();
		SelectorSize->SetWidthOverride(174.0f);
		SelectorSize->SetHeightOverride(30.0f);
		UOverlay* SelectorOverlay = WidgetTree->ConstructWidget<UOverlay>();
		UOverlaySlot* SelectorSlot = SelectorOverlay->AddChildToOverlay(Selector);
		SelectorSlot->SetHorizontalAlignment(HAlign_Fill); SelectorSlot->SetVerticalAlignment(VAlign_Fill);
		SelectorSlot->SetPadding(FMargin(0.0f));
		UTextBlock* KeyDisplay = MakeSettingsLabel(WidgetTree, FText::FromString(Definition.DisplayName));
		KeyDisplay->SetJustification(ETextJustify::Center);
		KeyDisplay->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		KeyDisplay->SetVisibility(ESlateVisibility::HitTestInvisible);
		UScaleBox* KeyDisplayFit = WidgetTree->ConstructWidget<UScaleBox>();
		KeyDisplayFit->SetVisibility(ESlateVisibility::HitTestInvisible);
		KeyDisplayFit->SetStretch(EStretch::ScaleToFit);
		KeyDisplayFit->SetStretchDirection(EStretchDirection::DownOnly);
		KeyDisplayFit->SetContent(KeyDisplay);
		UOverlaySlot* KeyDisplaySlot = SelectorOverlay->AddChildToOverlay(KeyDisplayFit);
		KeyDisplaySlot->SetHorizontalAlignment(HAlign_Fill); KeyDisplaySlot->SetVerticalAlignment(VAlign_Fill);
		KeyDisplaySlot->SetPadding(FMargin(3.0f, 1.0f));
		SelectorSize->AddChild(SelectorOverlay);
		Row->AddChildToHorizontalBox(SelectorSize);
		KeySelectors.Add(Selector);
		KeyDisplayTexts.Add(KeyDisplay);

		if (UInputAction* Action = LoadObject<UInputAction>(nullptr, Definition.AssetPath))
		{
			RebindableActions.Add(Action);
		}
	}

	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	ActionsSection->AddChildToVerticalBox(Actions)->SetPadding(FMargin(4.0f, 6.0f));

	// Esc opens Settings directly (never a pause menu), so the "return to suit selection at any
	// time" entry point lives here, next to Reset/Close.
	ResortieButton = WidgetTree->ConstructWidget<UButton>();
	ResortieLabel = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("再出撃")));
	ResortieButton->AddChild(ResortieLabel);
	ResortieButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleResortieClicked);
	Actions->AddChildToHorizontalBox(ResortieButton)->SetPadding(FMargin(0, 0, 6.0f, 0));

	ExitButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ExitText = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("退出")));
	ExitButton->AddChild(ExitText);
	ExitButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleExitClicked);
	Actions->AddChildToHorizontalBox(ExitButton)->SetPadding(FMargin(0, 0, 6.0f, 0));

	LogoutButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* LogoutText = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("ログアウト")));
	LogoutButton->AddChild(LogoutText);
	LogoutButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleLogoutClicked);
	Actions->AddChildToHorizontalBox(LogoutButton)->SetPadding(FMargin(0, 0, 6.0f, 0));

	CreditsButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CreditsButton"));
	UTextBlock* CreditsText = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("クレジット")));
	CreditsButton->AddChild(CreditsText);
	CreditsButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleCreditsClicked);
	Actions->AddChildToHorizontalBox(CreditsButton)->SetPadding(FMargin(0, 0, 6.0f, 0));

	UButton* ResetButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ResetText = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("初期値に戻す")));
	ResetButton->AddChild(ResetText);
	ResetButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleResetClicked);
	Actions->AddChildToHorizontalBox(ResetButton)->SetPadding(FMargin(0, 0, 6.0f, 0));

	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* CloseText = MakeSettingsLabel(WidgetTree, FText::FromString(TEXT("閉じる")));
	CloseButton->AddChild(CloseText);
	CloseButton->OnClicked.AddDynamic(this, &UMSSettingsWidget::HandleCloseClicked);
	MSUIStyle::ApplyButtonSound(CloseButton, MSUIStyle::EButtonSound::Back);
	Actions->AddChildToHorizontalBox(CloseButton)->SetPadding(FMargin(6.0f, 0, 0, 0));

	PlayerMappingContext = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSSettingsWidget::NativeDestruct()
{
	Close();
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSSettingsWidget::InitializeForAutomationTests() { if (!ResortieButton) NativeOnInitialized(); }
FText UMSSettingsWidget::GetResortieLabelForTesting() const
{
	return ResortieLabel ? ResortieLabel->GetText() : FText::GetEmpty();
}
FText UMSSettingsWidget::GetResortieTooltipForTesting() const
{
	return ResortieButton ? ResortieButton->GetToolTipText() : FText::GetEmpty();
}
#endif

void UMSSettingsWidget::OpenSettings()
{
	Open();
}

void UMSSettingsWidget::Open()
{
	if (!IsInViewport() && GetWorld())
	{
		// 設定はどの画面(戦闘HUD/ロビー40/カスタマイズ80/機体入替100)の上にも被さる
		// 最前面モーダルとして開く(遷移オーバーレイの10000だけがこれより手前)。
		AddToViewport(120);
	}
	SetVisibility(ESlateVisibility::Visible);
	SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	RefreshDisplayedSettings();
	RefreshResortiePresentation();
	if (!bIsOpen)
	{
		bIsOpen = true;
		if (UMSUILockSubsystem* UILock = GetUILockSubsystem())
		{
			UILock->PushScreen(SettingsScreenId,
				FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
		}
	}
}

void UMSSettingsWidget::Close()
{
	if (CreditsWidget && CreditsWidget->IsOpen()) CreditsWidget->Close();
	SetVisibility(ESlateVisibility::Collapsed);
	if (!bIsOpen)
	{
		return;
	}
	bIsOpen = false;
	if (UMSUILockSubsystem* UILock = GetUILockSubsystem())
	{
		UILock->PopScreen(SettingsScreenId);
	}
}

bool UMSSettingsWidget::IsSelectingKey() const
{
	for (const UMSSettingsKeySelector* Selector : KeySelectors)
	{
		if (Selector && Selector->GetIsSelectingKey()) return true;
	}
	return false;
}

FReply UMSSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// During rebinding the preprocessor lets Escape through. SInputKeySelector cancels on
	// key-up; consume the bubbled key-down here without closing Settings or stealing focus.
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (IsSelectingKey()) return FReply::Handled();
		if (UMSUILockSubsystem* UILock = GetUILockSubsystem(); UILock && UILock->RequestCloseTopScreen())
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMSSettingsWidget::ApplySelectedKey(const FName MappingName, const FInputChord& SelectedKey)
{
	if (!SelectedKey.Key.IsValid() || !FindBindingDefinition(MappingName))
	{
		return;
	}
	if (UEnhancedInputUserSettings* UserSettings = GetInputUserSettings())
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = MappingName;
		Args.Slot = EPlayerMappableKeySlot::First;
		Args.NewKey = SelectedKey.Key;
		FGameplayTagContainer FailureReason;
		UserSettings->MapPlayerKey(Args, FailureReason);
		if (FailureReason.IsEmpty())
		{
			UserSettings->ApplySettings();
			UserSettings->SaveSettings();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Settings: mapping %s to %s failed: %s"),
				*MappingName.ToString(), *SelectedKey.Key.ToString(), *FailureReason.ToString());
		}
	}
	RefreshDisplayedSettings();
}

void UMSSettingsWidget::HandleKeySelectionStateChanged(const FName MappingName, const bool bIsSelecting)
{
	const TArray<FMSSettingsBindingDefinition>& Definitions = GetBindingDefinitions();
	const int32 Index = Definitions.IndexOfByPredicate([MappingName](const FMSSettingsBindingDefinition& Definition)
	{
		return MappingName == Definition.MappingName;
	});
	if (!KeyDisplayTexts.IsValidIndex(Index) || !KeyDisplayTexts[Index])
	{
		return;
	}

	if (bIsSelecting)
	{
		KeyDisplayTexts[Index]->SetText(FText::FromString(TEXT("キーを入力…（Escで取消）")));
		FSlateFontInfo Font = KeyDisplayTexts[Index]->GetFont();
		Font.Size = 11;
		KeyDisplayTexts[Index]->SetFont(Font);
	}
	else
	{
		KeyDisplayTexts[Index]->SetText(CompactKeyDisplayName(KeySelectors.IsValidIndex(Index) && KeySelectors[Index]
			? KeySelectors[Index]->GetSelectedKey().Key : FKey()));
		FSlateFontInfo Font = KeyDisplayTexts[Index]->GetFont();
		Font.Size = 14;
		KeyDisplayTexts[Index]->SetFont(Font);
	}
}

void UMSSettingsWidget::HandleSensitivityChanged(const float NewValue)
{
	CurrentSensitivity = FMath::Clamp(NewValue, MinMouseSensitivity, MaxMouseSensitivity);
	if (SensitivityValueText)
	{
		SensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), CurrentSensitivity)));
	}
	ApplySensitivityToPlayer(false);
}

void UMSSettingsWidget::HandleSensitivityCaptureEnded()
{
	ApplySensitivityToPlayer(true);
}

void UMSSettingsWidget::HandleFieldOfViewChanged(const float NewValue)
{
	CurrentFieldOfView = FMath::Clamp(NewValue, MinFieldOfView, MaxFieldOfView);
	if (FieldOfViewValueText)
	{
		FieldOfViewValueText->SetText(FText::AsNumber(FMath::RoundToInt(CurrentFieldOfView)));
	}
	ApplyFieldOfViewToPlayer(false);
}

void UMSSettingsWidget::HandleFieldOfViewCaptureEnded()
{
	ApplyFieldOfViewToPlayer(true);
}

FReply UMSSettingsWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (UMSUILockSubsystem* UILock = GetUILockSubsystem(); UILock && UILock->RequestCloseTopScreen())
		{
			return FReply::Handled();
		}
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSSettingsWidget::HandleMasterVolumeChanged(const float NewValue)
{
	CurrentMasterVolume = FMath::Clamp(NewValue, MinMasterVolume, MaxMasterVolume);
	if (MasterVolumeValueText)
	{
		MasterVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CurrentMasterVolume * 100.0f))));
	}
	ApplyMasterVolume(false);
}

void UMSSettingsWidget::HandleMasterVolumeCaptureEnded()
{
	ApplyMasterVolume(true);
}

void UMSSettingsWidget::HandleCreditsClicked()
{
	if (!CreditsWidget)
	{
		CreditsWidget = CreateWidget<UMSCreditsWidget>(GetOwningPlayer(), UMSCreditsWidget::StaticClass());
		if (!CreditsWidget)
		{
			// Headless automation has no owning local player, but can still exercise the code-only UI.
			CreditsWidget = NewObject<UMSCreditsWidget>(this);
			CreditsWidget->Initialize();
		}
	}
	if (CreditsWidget) CreditsWidget->Open();
}

void UMSSettingsWidget::HandleBgmVolumeChanged(const float NewValue)
{
	CurrentBgmVolume = FMath::Clamp(NewValue, MinMasterVolume, MaxMasterVolume);
	if (BgmVolumeValueText)
	{
		BgmVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CurrentBgmVolume * 100.0f))));
	}
	ApplyBgmVolume(false);
}

void UMSSettingsWidget::HandleBgmVolumeCaptureEnded()
{
	ApplyBgmVolume(true);
}

void UMSSettingsWidget::HandleRightShoulderClicked()
{
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer()) Player->SetCameraViewPreset(EMSCameraViewPreset::RightShoulder, true);
	else
	{
		GConfig->SetInt(SensitivityConfigSection, CameraViewPresetConfigKey, static_cast<int32>(EMSCameraViewPreset::RightShoulder), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSSettingsWidget::HandleHighOverheadClicked()
{
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer()) Player->SetCameraViewPreset(EMSCameraViewPreset::HighOverhead, true);
	else
	{
		GConfig->SetInt(SensitivityConfigSection, CameraViewPresetConfigKey, static_cast<int32>(EMSCameraViewPreset::HighOverhead), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSSettingsWidget::HandleHotbloodVoiceClicked(){ApplyVoiceType(EMSVoiceType::Hotblood);}
void UMSSettingsWidget::HandleSeriousVoiceClicked(){ApplyVoiceType(EMSVoiceType::Serious);}
void UMSSettingsWidget::HandleShyVoiceClicked(){ApplyVoiceType(EMSVoiceType::Shy);}
void UMSSettingsWidget::HandleAudioTypeAClicked(){ApplyAudioPreset(EMSAudioPreset::TypeA);}
void UMSSettingsWidget::HandleAudioTypeBClicked(){ApplyAudioPreset(EMSAudioPreset::TypeB);}
void UMSSettingsWidget::HandleUITypeAClicked(){ApplyUIMode(MSUIStyle::EUIMode::TypeA);}
void UMSSettingsWidget::HandleUITypeBClicked(){ApplyUIMode(MSUIStyle::EUIMode::TypeB);}

void UMSSettingsWidget::ApplyAudioPreset(const EMSAudioPreset Preset)
{
	MSAudioPreset::SetSelectedPreset(Preset, true);
	RefreshAudioPresetButtons();
}

void UMSSettingsWidget::RefreshAudioPresetButtons()
{
	const EMSAudioPreset Selected = MSAudioPreset::GetSelectedPreset();
	auto ApplyState = [Selected](UButton* Button, const EMSAudioPreset Preset)
	{
		if (Button)
		{
			Button->SetBackgroundColor(Preset == Selected
				? FLinearColor(0.12f, 0.65f, 1.0f, 1.0f)
				: FLinearColor(0.24f, 0.27f, 0.31f, 1.0f));
		}
	};
	ApplyState(AudioTypeAButton, EMSAudioPreset::TypeA);
	ApplyState(AudioTypeBButton, EMSAudioPreset::TypeB);
}

void UMSSettingsWidget::ApplyUIMode(const MSUIStyle::EUIMode Mode)
{
	MSUIStyle::SetSelectedMode(Mode, true);
	RefreshUIModeButtons();
}

void UMSSettingsWidget::RefreshUIModeButtons()
{
	const MSUIStyle::EUIMode Selected = MSUIStyle::GetSelectedMode();
	auto ApplyState = [Selected](UButton* Button, const MSUIStyle::EUIMode Mode)
	{
		if (Button)
		{
			Button->SetBackgroundColor(Mode == Selected
				? FLinearColor(0.12f, 0.65f, 1.0f, 1.0f)
				: FLinearColor(0.24f, 0.27f, 0.31f, 1.0f));
		}
	};
	ApplyState(UITypeAButton, MSUIStyle::EUIMode::TypeA);
	ApplyState(UITypeBButton, MSUIStyle::EUIMode::TypeB);
}

void UMSSettingsWidget::HandleNoelAndersonClicked(){ApplyNavigator(EMSNavigator::NoelAnderson);}
void UMSSettingsWidget::HandleYuukiNakasatoClicked(){ApplyNavigator(EMSNavigator::YuukiNakasato);}
void UMSSettingsWidget::HandlePresetVoiceOnClicked(){ApplyVoiceEnabled(EMSVoiceCategory::Preset, true);}
void UMSSettingsWidget::HandlePresetVoiceOffClicked(){ApplyVoiceEnabled(EMSVoiceCategory::Preset, false);}
void UMSSettingsWidget::HandleNavigatorVoiceOnClicked(){ApplyVoiceEnabled(EMSVoiceCategory::Navigator, true);}
void UMSSettingsWidget::HandleNavigatorVoiceOffClicked(){ApplyVoiceEnabled(EMSVoiceCategory::Navigator, false);}

void UMSSettingsWidget::ApplyNavigator(const EMSNavigator Navigator)
{
	if (UMSGameInstance* GameInstance = ResolveMSGameInstance())
	{
		if (UMSNavigatorSubsystem* NavigatorSubsystem = GameInstance->GetSubsystem<UMSNavigatorSubsystem>())
		{
			NavigatorSubsystem->SetSelectedNavigator(Navigator, true);
		}
	}
	RefreshNavigatorButtons();
}

void UMSSettingsWidget::RefreshNavigatorButtons()
{
	EMSNavigator Selected = EMSNavigator::NoelAnderson;
	if (const UMSGameInstance* GameInstance = ResolveMSGameInstance())
	{
		if (const UMSNavigatorSubsystem* NavigatorSubsystem = GameInstance->GetSubsystem<UMSNavigatorSubsystem>())
		{
			Selected = NavigatorSubsystem->GetSelectedNavigator();
		}
	}
	auto ApplyState = [Selected](UButton* Button, const EMSNavigator Navigator)
	{
		if (Button)
		{
			Button->SetBackgroundColor(Navigator == Selected
				? FLinearColor(0.12f, 0.65f, 1.0f, 1.0f)
				: FLinearColor(0.24f, 0.27f, 0.31f, 1.0f));
		}
	};
	ApplyState(NoelAndersonButton, EMSNavigator::NoelAnderson);
	ApplyState(YuukiNakasatoButton, EMSNavigator::YuukiNakasato);
}

void UMSSettingsWidget::ApplyVoiceEnabled(const EMSVoiceCategory Category, const bool bEnabled)
{
	MSVoiceSettings::SetEnabled(Category, bEnabled, true);
	RefreshVoiceEnabledButtons();
}

void UMSSettingsWidget::RefreshVoiceEnabledButtons()
{
	auto ApplyState = [](UButton* OnButton, UButton* OffButton, const bool bEnabled)
	{
		const FLinearColor SelectedColor(0.12f, 0.65f, 1.0f, 1.0f);
		const FLinearColor UnselectedColor(0.24f, 0.27f, 0.31f, 1.0f);
		if (OnButton)
		{
			OnButton->SetBackgroundColor(bEnabled ? SelectedColor : UnselectedColor);
		}
		if (OffButton)
		{
			OffButton->SetBackgroundColor(bEnabled ? UnselectedColor : SelectedColor);
		}
	};
	ApplyState(PresetVoiceOnButton, PresetVoiceOffButton,
		MSVoiceSettings::IsEnabled(EMSVoiceCategory::Preset));
	ApplyState(NavigatorVoiceOnButton, NavigatorVoiceOffButton,
		MSVoiceSettings::IsEnabled(EMSVoiceCategory::Navigator));
}

UMSGameInstance* UMSSettingsWidget::ResolveMSGameInstance() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if(TestGameInstance)return TestGameInstance;
#endif
	return Cast<UMSGameInstance>(GetGameInstance());
}

void UMSSettingsWidget::ApplyVoiceType(const EMSVoiceType VoiceType)
{
	if(UMSGameInstance* GameInstance=ResolveMSGameInstance())GameInstance->SelectedVoiceType=VoiceType;
	RefreshVoiceTypeButtons();
}

void UMSSettingsWidget::RefreshVoiceTypeButtons()
{
	const UMSGameInstance* GameInstance=ResolveMSGameInstance();
	const EMSVoiceType Selected=GameInstance?GameInstance->SelectedVoiceType:EMSVoiceType::Hotblood;
	auto ApplyState=[Selected](UButton* Button,const EMSVoiceType Type)
	{
		if(Button)Button->SetBackgroundColor(Type==Selected?FLinearColor(0.12f,0.65f,1.0f,1.0f):FLinearColor(0.24f,0.27f,0.31f,1.0f));
	};
	ApplyState(HotbloodVoiceButton,EMSVoiceType::Hotblood);ApplyState(SeriousVoiceButton,EMSVoiceType::Serious);ApplyState(ShyVoiceButton,EMSVoiceType::Shy);
}

void UMSSettingsWidget::HandleCloseClicked()
{
	Close();
}

AMSPlayerCharacter* UMSSettingsWidget::ResolveResortiePlayer() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestResortiePlayer)
	{
		return TestResortiePlayer;
	}
#endif
	return Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());
}

void UMSSettingsWidget::RefreshResortiePresentation()
{
	if (!ResortieButton || !ResortieLabel)
	{
		return;
	}

	const AMSPlayerCharacter* Player = ResolveResortiePlayer();
	const UMSEmergencyRecallComponent* Recall = Player
		? Player->GetEmergencyRecallComponent() : nullptr;
	const bool bLivingPilot = Player && Player->GetVitalSet() && Player->GetVitalSet()->GetArmor() > 0.0f
		&& Recall && !Recall->IsAwaitingDeploy();

	if (bLivingPilot)
	{
		ResortieLabel->SetText(FText::FromString(TEXT("帰投して再出撃")));
		ResortieButton->SetToolTipText(FText::FromString(TEXT("地上で帰投後、機体と出撃地点を選択")));
	}
	else
	{
		ResortieLabel->SetText(FText::FromString(TEXT("再出撃")));
		ResortieButton->SetToolTipText(FText::GetEmpty());
	}
}

void UMSSettingsWidget::HandleResortieClicked()
{
	// Close first so the sortie screen becomes the topmost UI-lock entry.
	Close();
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer())
	{
		Player->OpenResortieScreen();
	}
}

void UMSSettingsWidget::HandleResetClicked()
{
	// Unity: KeyBindingManager.ResetToDefaults() = RemoveAllBindingOverrides() + clear PlayerPrefs.
	// Move/Look are never touched here since they were never added as rebindable mappings.
	if (UEnhancedInputUserSettings* UserSettings = GetInputUserSettings())
	{
		if (UEnhancedPlayerMappableKeyProfile* Profile = UserSettings->GetActiveKeyProfile())
		{
			Profile->ResetToDefault();
		}
		UserSettings->ApplySettings();
		UserSettings->SaveSettings();
	}
	CurrentSensitivity = DefaultMouseSensitivity;
	ApplySensitivityToPlayer(true);
	CurrentFieldOfView = DefaultFieldOfView;
	ApplyFieldOfViewToPlayer(true);
	CurrentMasterVolume = DefaultMasterVolume;
	ApplyMasterVolume(true);
	CurrentBgmVolume = DefaultBgmVolume;
	ApplyBgmVolume(true);
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer()) Player->SetCameraViewPreset(EMSCameraViewPreset::RightShoulder, true);
	else GConfig->SetInt(SensitivityConfigSection, CameraViewPresetConfigKey, static_cast<int32>(EMSCameraViewPreset::RightShoulder), GGameUserSettingsIni);
	ApplyVoiceType(EMSVoiceType::Hotblood);
	ApplyAudioPreset(EMSAudioPreset::TypeA);
	ApplyUIMode(MSUIStyle::EUIMode::TypeB);
	MSVoiceSettings::SetEnabled(EMSVoiceCategory::Preset, true, true);
	MSVoiceSettings::SetEnabled(EMSVoiceCategory::Navigator, true, true);
	RefreshDisplayedSettings();
}

UEnhancedInputUserSettings* UMSSettingsWidget::GetInputUserSettings() const
{
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				return Subsystem->GetUserSettings();
			}
		}
	}
	return nullptr;
}

UInputMappingContext* UMSSettingsWidget::GetPlayerMappingContext() const
{
	return PlayerMappingContext;
}

UMSUILockSubsystem* UMSSettingsWidget::GetUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem)
	{
		return TestUILockSubsystem;
	}
#endif
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UMSUILockSubsystem>();
	}
	return nullptr;
}

void UMSSettingsWidget::RefreshDisplayedSettings()
{
	PreparePlayerMappableInputActions();
	UEnhancedInputUserSettings* UserSettings = GetInputUserSettings();
	if (UserSettings && GetPlayerMappingContext())
	{
		UserSettings->RegisterInputMappingContext(GetPlayerMappingContext());
	}

	const TArray<FMSSettingsBindingDefinition>& Definitions = GetBindingDefinitions();
	for (int32 Index = 0; Index < Definitions.Num() && Index < KeySelectors.Num(); ++Index)
	{
		FKey DisplayKey = Definitions[Index].DefaultKey;
		if (UserSettings)
		{
			TArray<FKey> MappedKeys;
			if (const UEnhancedPlayerMappableKeyProfile* Profile = UserSettings->GetActiveKeyProfile();
				Profile && Profile->GetMappedKeysInRow(Definitions[Index].MappingName, MappedKeys) > 0)
			{
				DisplayKey = MappedKeys[0];
			}
		}
		KeySelectors[Index]->SetSelectedKey(FInputChord(DisplayKey));
		if (KeyDisplayTexts.IsValidIndex(Index) && KeyDisplayTexts[Index])
		{
			KeyDisplayTexts[Index]->SetText(CompactKeyDisplayName(DisplayKey));
		}
	}

	CurrentSensitivity = 0.35f;
	GConfig->GetFloat(SensitivityConfigSection, SensitivityConfigKey, CurrentSensitivity, GGameUserSettingsIni);
	CurrentSensitivity = FMath::Clamp(CurrentSensitivity, MinMouseSensitivity, MaxMouseSensitivity);
	if (const AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwningPlayerPawn()))
	{
		CurrentSensitivity = Player->GetLookSensitivityScale();
	}
	if (SensitivitySlider)
	{
		SensitivitySlider->SetValue(CurrentSensitivity);
	}
	if (SensitivityValueText)
	{
		SensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), CurrentSensitivity)));
	}

	CurrentFieldOfView = DefaultFieldOfView;
	GConfig->GetFloat(SensitivityConfigSection, FieldOfViewConfigKey, CurrentFieldOfView, GGameUserSettingsIni);
	CurrentFieldOfView = FMath::Clamp(CurrentFieldOfView, MinFieldOfView, MaxFieldOfView);
	if (const AMSPlayerCharacter* Player = ResolveResortiePlayer())
	{
		CurrentFieldOfView = Player->GetDefaultCameraFieldOfView();
	}
	if (FieldOfViewSlider)
	{
		FieldOfViewSlider->SetValue(CurrentFieldOfView);
	}
	if (FieldOfViewValueText)
	{
		FieldOfViewValueText->SetText(FText::AsNumber(FMath::RoundToInt(CurrentFieldOfView)));
	}

	CurrentMasterVolume = DefaultMasterVolume;
	GConfig->GetFloat(AudioSettingsConfigSection, MasterVolumeConfigKey,
		CurrentMasterVolume, GGameUserSettingsIni);
	CurrentMasterVolume = FMath::Clamp(CurrentMasterVolume, MinMasterVolume, MaxMasterVolume);
	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetValue(CurrentMasterVolume);
	}
	if (MasterVolumeValueText)
	{
		MasterVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CurrentMasterVolume * 100.0f))));
	}
	ApplyMasterVolume(false);

	CurrentBgmVolume = DefaultBgmVolume;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UMSBgmSubsystem* Bgm = GameInstance->GetSubsystem<UMSBgmSubsystem>())
		{
			CurrentBgmVolume = Bgm->GetBgmVolume();
		}
	}
	if (BgmVolumeSlider)
	{
		BgmVolumeSlider->SetValue(CurrentBgmVolume);
	}
	if (BgmVolumeValueText)
	{
		BgmVolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CurrentBgmVolume * 100.0f))));
	}
	ApplyBgmVolume(false);
	RefreshVoiceTypeButtons();
	RefreshAudioPresetButtons();
	RefreshUIModeButtons();
	RefreshNavigatorButtons();
	RefreshVoiceEnabledButtons();
}

void UMSSettingsWidget::HandleExitClicked()
{
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone && GetGameInstance())
	{
		if (auto* Rooms = GetGameInstance()->GetSubsystem<UMSRoomSubsystem>()) { Close(); Rooms->LeaveRoom(); return; }
	}
	// UI transition only: do not call EndBattle or broadcast a battle-end event.
	Close();
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer())
	{
		Player->OpenLobbyScreen();
	}
}

void UMSSettingsWidget::HandleLogoutClicked()
{
	// Close settings and any lobby beneath it, clear the transient login state, then
	// return to the login screen so another account can sign in.
	Close();
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer())
	{
		if (UMSLobbyWidget* Lobby = Player->GetLobbyWidget(); Lobby && Lobby->IsOpen()) Lobby->Close();
	}
	if (UMSGameInstance* GameInstance = ResolveMSGameInstance()) GameInstance->Logout();
#if WITH_DEV_AUTOMATION_TESTS
	if (TestLoginWidget) { TestLoginWidget->Open(); return; }
#endif
	UMSLoginWidget::OpenLoginScreen(GetOwningPlayer());
}

void UMSSettingsWidget::ApplySensitivityToPlayer(const bool bSave)
{
	if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwningPlayerPawn()))
	{
		Player->SetLookSensitivityScale(CurrentSensitivity, bSave);
	}
	else if (bSave)
	{
		GConfig->SetFloat(SensitivityConfigSection, SensitivityConfigKey, CurrentSensitivity, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSSettingsWidget::ApplyFieldOfViewToPlayer(const bool bSave)
{
	if (AMSPlayerCharacter* Player = ResolveResortiePlayer())
	{
		Player->SetDefaultCameraFieldOfView(CurrentFieldOfView, bSave);
	}
	else if (bSave)
	{
		GConfig->SetFloat(SensitivityConfigSection, FieldOfViewConfigKey, CurrentFieldOfView, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSSettingsWidget::ApplyMasterVolume(const bool bSave)
{
	CurrentMasterVolume = FMath::Clamp(CurrentMasterVolume, MinMasterVolume, MaxMasterVolume);

	// UE 5.7 exposes this directly on FAudioDevice and marshals the change to the audio thread.
	// It is lower-risk here than introducing a SoundMix/SoundClass asset dependency: the project
	// already treats the main audio device as optional, and the transient primary multiplier affects
	// every existing sound path without changing pitch or authored per-sound volume relationships.
	if (GEngine)
	{
		FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
		if (AudioDevice.IsValid())
		{
			AudioDevice->SetTransientPrimaryVolume(CurrentMasterVolume);
		}
	}

	if (bSave)
	{
		GConfig->SetFloat(AudioSettingsConfigSection, MasterVolumeConfigKey,
			CurrentMasterVolume, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMSSettingsWidget::ApplyBgmVolume(const bool bSave)
{
	CurrentBgmVolume = FMath::Clamp(CurrentBgmVolume, MinMasterVolume, MaxMasterVolume);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSBgmSubsystem* Bgm = GameInstance->GetSubsystem<UMSBgmSubsystem>())
		{
			Bgm->SetBgmVolume(CurrentBgmVolume, bSave);
		}
	}
}
