// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSUIStyle.h"
#include "Audio/MSAudioPreset.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

namespace MSUIStyleColor
{
	const FLinearColor PanelBackground(0.018f, 0.05f, 0.085f, 0.94f);
	const FLinearColor FrameAccent(0.12f, 0.86f, 0.95f, 1.0f);
	const FLinearColor TextPrimary = FLinearColor::White;
	const FLinearColor TextSecondary(0.72f, 0.84f, 0.88f, 1.0f);
	const FLinearColor WarningColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor SelectedAccent(0.95f, 0.53f, 0.28f, 1.0f);
}

namespace
{
	const TCHAR* UISettingsSection = TEXT("/Script/NeoMSGO.MSUISettings");
	const TCHAR* UIModeKey = TEXT("UIMode");
	int32 SelectedUIMode = INDEX_NONE;

	struct FModeBPlate
	{
		const TCHAR* AssetPath;
		FVector2D Size;
		FMargin Margin;
		ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::Box;
	};

	FModeBPlate GetModeBPlate(const MSUIStyle::EBrush Role)
	{
		// These normalized values are copied verbatim from Art/UI/ModeB/plates.json.
		switch (Role)
		{
		case MSUIStyle::EBrush::Panel:
			return { TEXT("/Game/UI/ModeB/T_UIB_PanelDark.T_UIB_PanelDark"), FVector2D(96), FMargin(0.270833f) };
		case MSUIStyle::EBrush::PanelSolid:
			return { TEXT("/Game/UI/ModeB/T_UIB_PanelDarkSolid.T_UIB_PanelDarkSolid"), FVector2D(96), FMargin(0.270833f) };
		case MSUIStyle::EBrush::PanelAttack:
			return { TEXT("/Game/UI/ModeB/T_UIB_PanelAttack.T_UIB_PanelAttack"), FVector2D(96), FMargin(0.270833f) };
		case MSUIStyle::EBrush::PanelDefense:
			return { TEXT("/Game/UI/ModeB/T_UIB_PanelDefense.T_UIB_PanelDefense"), FVector2D(96), FMargin(0.270833f) };
		case MSUIStyle::EBrush::LobbyPanel:
			return { TEXT("/Game/UI/ModeB/T_UIB_LobbyPanel.T_UIB_LobbyPanel"), FVector2D(128), FMargin(0.265625f) };
		case MSUIStyle::EBrush::ListRow:
			return { TEXT("/Game/UI/ModeB/T_UIB_ListRow.T_UIB_ListRow"), FVector2D(48), FMargin(0.291667f) };
		case MSUIStyle::EBrush::ListRowSelected:
			return { TEXT("/Game/UI/ModeB/T_UIB_ListRowSelected.T_UIB_ListRowSelected"), FVector2D(48), FMargin(0.291667f) };
		case MSUIStyle::EBrush::TabNormal:
			return { TEXT("/Game/UI/ModeB/T_UIB_TabNormal.T_UIB_TabNormal"), FVector2D(72), FMargin(0.277778f) };
		case MSUIStyle::EBrush::TabSelected:
			return { TEXT("/Game/UI/ModeB/T_UIB_TabSelected.T_UIB_TabSelected"), FVector2D(72), FMargin(0.277778f) };
		case MSUIStyle::EBrush::GaugeFrame:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFrame.T_UIB_GaugeFrame"), FVector2D(40, 30), FMargin(0.25f, 0, 0.25f, 0) };
		case MSUIStyle::EBrush::GaugeFillBoost:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFill_Boost.T_UIB_GaugeFill_Boost"), FVector2D(32, 24), FMargin(0.1875f, 0, 0.1875f, 0) };
		case MSUIStyle::EBrush::GaugeFillArmor:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFill_Armor.T_UIB_GaugeFill_Armor"), FVector2D(32, 24), FMargin(0.1875f, 0, 0.1875f, 0) };
		case MSUIStyle::EBrush::GaugeFillTeamA:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFill_TeamA.T_UIB_GaugeFill_TeamA"), FVector2D(32, 24), FMargin(0.1875f, 0, 0.1875f, 0) };
		case MSUIStyle::EBrush::GaugeFillTeamB:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFill_TeamB.T_UIB_GaugeFill_TeamB"), FVector2D(32, 24), FMargin(0.1875f, 0, 0.1875f, 0) };
		case MSUIStyle::EBrush::GaugeFillDP:
			return { TEXT("/Game/UI/ModeB/T_UIB_GaugeFill_DP.T_UIB_GaugeFill_DP"), FVector2D(32, 24), FMargin(0.1875f, 0, 0.1875f, 0) };
		case MSUIStyle::EBrush::WeaponSlot:
			return { TEXT("/Game/UI/ModeB/T_UIB_WeaponSlot.T_UIB_WeaponSlot"), FVector2D(80), FMargin(0.25f) };
		case MSUIStyle::EBrush::WeaponSlotActive:
			return { TEXT("/Game/UI/ModeB/T_UIB_WeaponSlotActive.T_UIB_WeaponSlotActive"), FVector2D(80), FMargin(0.25f) };
		case MSUIStyle::EBrush::WeaponSelectionMarker:
			return { TEXT("/Game/UI/ModeB/T_UIB_WeaponSelectionMarker.T_UIB_WeaponSelectionMarker"), FVector2D(16, 20), FMargin(0), ESlateBrushDrawType::Image };
		case MSUIStyle::EBrush::ButtonNormal:
			return { TEXT("/Game/UI/ModeB/T_UIB_ButtonNormal.T_UIB_ButtonNormal"), FVector2D(80), FMargin(0.275f) };
		case MSUIStyle::EBrush::ButtonHover:
			return { TEXT("/Game/UI/ModeB/T_UIB_ButtonHover.T_UIB_ButtonHover"), FVector2D(80), FMargin(0.275f) };
		case MSUIStyle::EBrush::ButtonPressed:
			return { TEXT("/Game/UI/ModeB/T_UIB_ButtonPressed.T_UIB_ButtonPressed"), FVector2D(80), FMargin(0.275f) };
		case MSUIStyle::EBrush::ButtonDisabled:
			return { TEXT("/Game/UI/ModeB/T_UIB_ButtonDisabled.T_UIB_ButtonDisabled"), FVector2D(80), FMargin(0.275f) };
		case MSUIStyle::EBrush::Scrim:
			return { TEXT("/Game/UI/ModeB/T_UIB_Scrim.T_UIB_Scrim"), FVector2D(8), FMargin(0), ESlateBrushDrawType::Box };
		case MSUIStyle::EBrush::LobbyBackdrop:
			// Box with a zero margin, like every other plate here: it stretches the whole 16:9
			// texture across the border with no 9-slice, so the ramp and vignette survive. The
			// obvious-looking DrawAs=Image does *not* paint through SBorder's border-image slot,
			// which is why the lobby stayed see-through the first time this was wired up.
			return { TEXT("/Game/UI/ModeB/T_UIB_LobbyBackdrop.T_UIB_LobbyBackdrop"), FVector2D(640, 360), FMargin(0), ESlateBrushDrawType::Box };
		default:
			return {};
		}
	}

	FSlateBrush MakeModeBBrush(const FModeBPlate& Plate, const FLinearColor& FallbackColor)
	{
		FSlateBrush Brush;
		Brush.DrawAs = Plate.DrawAs;
		Brush.ImageSize = Plate.Size;
		Brush.Margin = Plate.Margin;
		Brush.TintColor = FLinearColor::White;
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Plate.AssetPath))
		{
			Brush.SetResourceObject(Texture);
		}
		else
		{
			// Missing/corrupt content must remain usable and visually equivalent to Mode A.
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.Margin = FMargin(0);
			Brush.TintColor = FallbackColor;
		}
		return Brush;
	}

	bool IsBoldByDefault(const MSUIStyle::ETextRole Role)
	{
		return Role == MSUIStyle::ETextRole::Display || Role == MSUIStyle::ETextRole::H1 ||
			Role == MSUIStyle::ETextRole::H2 || Role == MSUIStyle::ETextRole::NumericLarge;
	}

	FSlateBrush MakeRoundedBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.TintColor = Color;
		return Brush;
	}
}

MSUIStyle::EUIMode MSUIStyle::GetSelectedMode()
{
	if (SelectedUIMode == INDEX_NONE)
	{
		// New profiles start with the reference-based layout. An existing choice
		// still takes precedence when loaded from GameUserSettings.
		int32 Value = static_cast<int32>(EUIMode::TypeB);
		GConfig->GetInt(UISettingsSection, UIModeKey, Value, GGameUserSettingsIni);
		SelectedUIMode = Value == static_cast<int32>(EUIMode::TypeB)
			? static_cast<int32>(EUIMode::TypeB) : static_cast<int32>(EUIMode::TypeA);
	}
	return SelectedUIMode == static_cast<int32>(EUIMode::TypeB) ? EUIMode::TypeB : EUIMode::TypeA;
}

void MSUIStyle::SetSelectedMode(const EUIMode Mode, const bool bSave)
{
	SelectedUIMode = Mode == EUIMode::TypeB
		? static_cast<int32>(EUIMode::TypeB) : static_cast<int32>(EUIMode::TypeA);
	if (bSave)
	{
		GConfig->SetInt(UISettingsSection, UIModeKey, SelectedUIMode, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FSlateBrush MSUIStyle::GetBrush(const EBrush Role, const FLinearColor& ModeAColor)
{
	if (GetSelectedMode() == EUIMode::TypeB)
	{
		return MakeModeBBrush(GetModeBPlate(Role), ModeAColor);
	}
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Box;
	Brush.TintColor = ModeAColor;
	return Brush;
}

void MSUIStyle::ApplyBrush(UBorder* Border, const EBrush Role, const FLinearColor& ModeAColor)
{
	if (Border)
	{
		if (GetSelectedMode() == EUIMode::TypeB)
		{
			Border->SetBrush(GetBrush(Role, ModeAColor));
			Border->SetBrushColor(FLinearColor::White);
		}
		else
		{
			// Preserve the legacy UBorder contract: callers and regression tests inspect BrushColor,
			// while the plain brush itself supplies only the Box draw mode.
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.TintColor = FLinearColor::White;
			Border->SetBrush(Brush);
			Border->SetBrushColor(ModeAColor);
		}
	}
}

void MSUIStyle::ApplyProgressBarStyle(UProgressBar* ProgressBar, const EBrush FillRole)
{
	if (!ProgressBar || GetSelectedMode() != EUIMode::TypeB)
	{
		return;
	}
	FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
	Style.SetBackgroundImage(GetBrush(EBrush::GaugeFrame, FLinearColor::Transparent));
	Style.SetFillImage(GetBrush(FillRole, FLinearColor::White));
	ProgressBar->SetWidgetStyle(Style);
	// Texture pixels carry the authored palette; avoid multiplying them by the Mode A fill tint.
	ProgressBar->SetFillColorAndOpacity(FLinearColor::White);
}

MSUIStyle::FHUDLayout MSUIStyle::GetHUDLayout()
{
	FHUDLayout Layout;
	if (GetSelectedMode() == EUIMode::TypeB)
	{
		// 1920x1080 reference: the weapon stack begins near y=0.69 and the two vital
		// gauges form one bottom-right instrument cluster instead of a detached center bar.
		Layout.WeaponAnchorY = 0.69f;
		Layout.WeaponPanelWidth = 304.0f;
		Layout.WeaponSlotHeight = 56.0f;
		Layout.WeaponSlotSpacing = 3.0f;
		Layout.WeaponCardPadding = FMargin(5.0f, 2.0f);
		Layout.BoostAnchorX = 0.985f;
		Layout.BoostPosition = FVector2D(-270.0f, -1.0f);
		Layout.BoostLabelPosition = FVector2D(-500.0f, -1.0f);
		Layout.ModuleAnchor = FVector2D(.82f, .925f);
		Layout.ModuleAlignment = FVector2D(.5f, 1.0f);
		Layout.bShowVitalFrame = true;
		Layout.bShowMinimapNorth = true;
		Layout.bShowMinimapCardinals = true;
		Layout.bShowWeaponFireMode = true;
		Layout.bDifferentiateMinimapMarkers = true;
		Layout.bShowDensityInstrumentRules = true;
		Layout.bShowTeamRoster = true;
		Layout.bShowTimedBattleLog = true;
		Layout.bShowMinimapRoleBubbles = true;
		Layout.bShowWeaponSelectionMarker = true;
		Layout.bShowSystemDownWarning = true;
		Layout.bShowTaskPrefix = true;
		Layout.TaskMissionPosition = FVector2D(45.0f, 0.0f);
		Layout.TaskTextPosition = FVector2D(98.0f, 1.0f);
		Layout.TaskTextSize = FVector2D(142.0f, 20.0f);
		Layout.MissionBadgeAnchor = FVector2D(.955f, .52f);
		Layout.MissionBadgePosition = FVector2D::ZeroVector;
	}
	return Layout;
}

MSUIStyle::FFrontendLayout MSUIStyle::GetFrontendLayout()
{
	FFrontendLayout Layout;
	if (GetSelectedMode() == EUIMode::TypeB)
	{
		// Round 2 follows the reference's deck/storage split: a 2x2 dense deck at left and a
		// searchable owned-suit inventory at right. These values live here so Customize never
		// asks which EUIMode is active while constructing its children.
		Layout.CustomizeScreenPadding = FMargin(120.0f, 70.0f);
		Layout.CustomizeMinorTabInset = 90.0f;
		Layout.CustomizePreviewHeight = 116.0f;
		Layout.CustomizeCatalogWidth = 430.0f;
		Layout.CustomizeDeckColumns = 2;
		Layout.bUseIntegratedSuitCatalog = true;
		Layout.bUseDenseSuitCards = true;
	}
	return Layout;
}

void MSUIStyle::ApplyContentButtonStyle(UButton* Button)
{
	if (!Button || GetSelectedMode() != EUIMode::TypeB)
	{
		return;
	}
	// This used to install a NoDrawType brush in all four states. The intent was to drop the
	// engine's default grey box, but nothing replaced it, so the cards lost their frames and the
	// 3D level behind the menu showed through them. The authored Mode B button plates are the
	// replacement that was missing -- they carry the frame, so the grey box is still gone.
	FButtonStyle Style = Button->GetStyle();
	Style.SetNormal(GetBrush(EBrush::ButtonNormal, MSUIStyleColor::PanelBackground));
	Style.SetHovered(GetBrush(EBrush::ButtonHover, MSUIStyleColor::PanelBackground));
	Style.SetPressed(GetBrush(EBrush::ButtonPressed, MSUIStyleColor::PanelBackground));
	Style.SetDisabled(GetBrush(EBrush::ButtonDisabled, MSUIStyleColor::PanelBackground));
	Button->SetStyle(Style);
	Button->SetClipping(EWidgetClipping::ClipToBounds);
}

void MSUIStyle::StretchButtonContent(UButton* Button)
{
	if (UWidget* Content = Button ? Button->GetContent() : nullptr)
	{
		if (UButtonSlot* Slot = Cast<UButtonSlot>(Content->Slot))
		{
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
			// Framed cards already own their padding. An extra slot inset separates the
			// visible frame from the clickable area, especially at smaller resolutions.
			Slot->SetPadding(FMargin(0.0f));
		}
	}
}

UWidget* MSUIStyle::MakeFrontendWorkArea(UWidgetTree* Tree, UWidget* Content)
{
	if (!Tree || !Content || GetSelectedMode() == EUIMode::TypeA)
	{
		return Content;
	}
	UBorder* Inner = nullptr;
	return MakeFramedPanel(Tree, Inner, Content, true, MSUIStyleColor::FrameAccent,
		MSUIStyleColor::PanelBackground, FMargin(MSUIStyleSize::Space3));
}

float MSUIStyle::GetFontSize(const ETextRole Role, const ENumericLargeSize NumericSize)
{
	switch (Role)
	{
	case ETextRole::Display: return MSUIStyleSize::Display;
	case ETextRole::H1: return MSUIStyleSize::H1;
	case ETextRole::H2: return MSUIStyleSize::H2;
	case ETextRole::Body: return MSUIStyleSize::Body;
	case ETextRole::Caption: return MSUIStyleSize::Caption;
	case ETextRole::Micro: return MSUIStyleSize::Micro;
	case ETextRole::NumericLarge:
		return NumericSize == ENumericLargeSize::Emphasized
			? MSUIStyleSize::NumericLargeEmphasized : MSUIStyleSize::NumericLarge;
	default: return MSUIStyleSize::Body;
	}
}

UTextBlock* MSUIStyle::MakeText(UWidgetTree* Tree, const FText& Text, const ETextRole Role,
	const bool bForceBold, const FLinearColor& Color, const ENumericLargeSize NumericSize, const FName Name,
	const bool bAllowWrap)
{
	check(Tree);
	UTextBlock* Result = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Result->SetText(Text);
	Result->SetColorAndOpacity(FSlateColor(Color));
	FSlateFontInfo Font = Result->GetFont();
	Font.Size = GetFontSize(Role, NumericSize);
	Font.TypefaceFontName = IsBoldByDefault(Role) || bForceBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
	Result->SetFont(Font);
	// Most menu text is data-driven (unit names, rule descriptions and loadouts). Keep it
	// inside the geometry allotted by its panel even when localization or a larger font
	// makes the measured line wider than the reference layout. Short fixed captions (button
	// labels, tab headers) pass bAllowWrap=false to guarantee a single line instead.
	Result->SetAutoWrapText(bAllowWrap);
	Result->SetClipping(EWidgetClipping::ClipToBounds);
	return Result;
}

UBorder* MSUIStyle::MakeFramedPanel(UWidgetTree* Tree, UBorder*& OutInnerBorder,
	UWidget* Content, const bool bEmphasized, const FLinearColor& FrameColor,
	const FLinearColor& InPanelColor, const FMargin& PanelPadding)
{
	check(Tree);
	UBorder* OuterBorder = Tree->ConstructWidget<UBorder>();
	ApplyBrush(OuterBorder, EBrush::Panel, FrameColor);
	OuterBorder->SetPadding(FMargin(bEmphasized ? MSUIStyleSize::BorderEmphasized : MSUIStyleSize::BorderNormal));
	OuterBorder->SetClipping(EWidgetClipping::ClipToBounds);

	OutInnerBorder = Tree->ConstructWidget<UBorder>();
	ApplyBrush(OutInnerBorder, EBrush::Panel, InPanelColor);
	OutInnerBorder->SetPadding(PanelPadding);
	OutInnerBorder->SetClipping(EWidgetClipping::ClipToBounds);
	if (Content)
	{
		OutInnerBorder->SetContent(Content);
	}
	OuterBorder->SetContent(OutInnerBorder);
	return OuterBorder;
}

UCanvasPanel* MSUIStyle::MakeChamferedFrame(UWidgetTree* Tree, UBorder*& OutInnerBorder,
	const EChamferCorner Corners, UWidget* Content, const float ChamferSize)
{
	check(Tree);
	UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>();
	UBorder* Frame = MakeFramedPanel(Tree, OutInnerBorder, Content);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	FrameSlot->SetOffsets(FMargin(0.0f));

	auto AddCornerMask = [Tree, Root, ChamferSize](const FAnchors& Anchors, const FVector2D& Alignment)
	{
		UImage* Mask = Tree->ConstructWidget<UImage>();
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = MSUIStyleColor::PanelBackground;
		Mask->SetBrush(Brush);
		// Same code-only transform pattern as MSHUDWidget's lock-on diamond.
		Mask->SetRenderTransformPivot(FVector2D(0.5f));
		Mask->SetRenderTransformAngle(45.0f);
		UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Mask);
		Slot->SetAnchors(Anchors);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(FVector2D::ZeroVector);
		Slot->SetSize(FVector2D(ChamferSize));
	};

	// The authored Mode B plate already has transparent chamfered corners. Mode A retains the
	// legacy masks exactly, so selecting Type A is a visual regression-free path.
	if (GetSelectedMode() == EUIMode::TypeA)
	{
		if (EnumHasAnyFlags(Corners, EChamferCorner::TopLeft)) AddCornerMask(FAnchors(0.0f, 0.0f), FVector2D(0.5f));
		if (EnumHasAnyFlags(Corners, EChamferCorner::TopRight)) AddCornerMask(FAnchors(1.0f, 0.0f), FVector2D(0.5f));
		if (EnumHasAnyFlags(Corners, EChamferCorner::BottomRight)) AddCornerMask(FAnchors(1.0f, 1.0f), FVector2D(0.5f));
		if (EnumHasAnyFlags(Corners, EChamferCorner::BottomLeft)) AddCornerMask(FAnchors(0.0f, 1.0f), FVector2D(0.5f));
	}
	return Root;
}

UButton* MSUIStyle::MakeCapsuleButton(UWidgetTree* Tree, UWidget* Content,
	const FLinearColor& NormalColor, const FName Name, const EButtonSound Sound)
{
	check(Tree);
	UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	FButtonStyle Style = Button->GetStyle();
	if (GetSelectedMode() == EUIMode::TypeB)
	{
		auto ButtonPlate = [](const TCHAR* Path, const FMargin Margin)
		{
			FModeBPlate Plate{ Path, FVector2D(80), Margin };
			return MakeModeBBrush(Plate, MSUIStyleColor::PanelBackground);
		};
		Style.SetNormal(ButtonPlate(TEXT("/Game/UI/ModeB/T_UIB_ButtonNormal.T_UIB_ButtonNormal"), FMargin(0.275f)));
		Style.SetHovered(ButtonPlate(TEXT("/Game/UI/ModeB/T_UIB_ButtonHover.T_UIB_ButtonHover"), FMargin(0.275f)));
		Style.SetPressed(ButtonPlate(TEXT("/Game/UI/ModeB/T_UIB_ButtonPressed.T_UIB_ButtonPressed"), FMargin(0.275f)));
		Style.SetDisabled(ButtonPlate(TEXT("/Game/UI/ModeB/T_UIB_ButtonDisabled.T_UIB_ButtonDisabled"), FMargin(0.275f)));
	}
	else
	{
		Style.SetNormal(MakeRoundedBrush(NormalColor));
		Style.SetHovered(MakeRoundedBrush(MSUIStyleColor::FrameAccent));
		Style.SetPressed(MakeRoundedBrush(MSUIStyleColor::SelectedAccent));
	}
	Button->SetStyle(Style);
	ApplyButtonSound(Button, Sound);
	Button->SetClipping(EWidgetClipping::ClipToBounds);
	if (Content)
	{
		Button->SetContent(Content);
	}
	return Button;
}

void MSUIStyle::ApplyButtonSound(UButton* Button, const EButtonSound Sound)
{
	if (!Button)
	{
		return;
	}

	USoundBase* SoundAsset = nullptr;
	if (Sound == EButtonSound::Click)
	{
		SoundAsset = MSAudioPreset::Resolve(EMSAudioUsage::UIClick, LoadObject<USoundBase>(
			nullptr, TEXT("/Game/Audio/Imported/SFX_UI_Click.SFX_UI_Click")));
	}
	else if (Sound == EButtonSound::Back)
	{
		SoundAsset = MSAudioPreset::Resolve(EMSAudioUsage::UIBack, LoadObject<USoundBase>(
			nullptr, TEXT("/Game/Audio/Imported/SFX_UI_Back.SFX_UI_Back")));
	}
	FSlateSound SlateSound;
	if (SoundAsset)
	{
		SlateSound.SetResourceObject(SoundAsset);
	}
	FButtonStyle Style = Button->GetStyle();
	Style.SetPressedSound(SlateSound);
	// カーソルを合わせただけで鳴るホバー音は廃止(実プレイで煩わしいというフィードバック)。
	// クリック音(Pressed)のみ残す。
	Style.SetHoveredSound(FSlateSound());
	Button->SetStyle(Style);
}
