// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UWidget;
class UWidgetTree;

namespace MSUIStyle
{
	enum class EUIMode : uint8
	{
		TypeA,
		TypeB
	};

	/** Semantic plate names. Widgets ask for intent; only the resolver knows about Mode B assets. */
	enum class EBrush : uint8
	{
		Panel,
		PanelSolid,
		PanelAttack,
		PanelDefense,
		LobbyPanel,
		ListRow,
		ListRowSelected,
		TabNormal,
		TabSelected,
		GaugeFrame,
		GaugeFillBoost,
		GaugeFillArmor,
		GaugeFillTeamA,
		GaugeFillTeamB,
		GaugeFillDP,
		WeaponSlot,
		WeaponSlotActive,
		WeaponSelectionMarker,
		ButtonNormal,
		ButtonHover,
		ButtonPressed,
		ButtonDisabled,
		Scrim,
		/** Opaque frontend backdrop. Distinct from Scrim on purpose: a scrim is translucent by
		    definition, so using it as the lobby's root background let the live level show
		    through the menu. */
		LobbyBackdrop
	};

	/** Combat HUD geometry is selected here so widgets never scatter Mode B conditionals. */
	struct FHUDLayout
	{
		float WeaponAnchorY = 0.575f;
		float WeaponPanelWidth = 280.0f;
		float WeaponSlotHeight = 48.0f;
		float WeaponSlotSpacing = 4.0f;
		FMargin WeaponCardPadding = FMargin(0.0f);
		float BoostAnchorX = 0.62f;
		FVector2D BoostPosition = FVector2D::ZeroVector;
		FVector2D BoostLabelPosition = FVector2D(-8.0f, 0.0f);
		FVector2D ModuleAnchor = FVector2D(.985f, .535f);
		FVector2D ModuleAlignment = FVector2D(1.0f, 1.0f);
		bool bShowVitalFrame = false;
		bool bShowMinimapNorth = false;
		bool bShowMinimapCardinals = false;
		bool bShowWeaponFireMode = false;
		bool bDifferentiateMinimapMarkers = false;
		bool bShowDensityInstrumentRules = false;
		bool bShowTeamRoster = false;
		bool bShowTimedBattleLog = false;
		bool bShowMinimapRoleBubbles = false;
		bool bShowWeaponSelectionMarker = false;
		bool bShowSystemDownWarning = false;
		bool bShowTaskPrefix = false;
		FVector2D TaskMissionPosition = FVector2D(0.0f, 0.0f);
		FVector2D TaskMissionSize = FVector2D(50.0f, 22.0f);
		FVector2D TaskTextPosition = FVector2D(56.0f, 1.0f);
		FVector2D TaskTextSize = FVector2D(184.0f, 20.0f);
		FVector2D MissionBadgeAnchor = FVector2D(.5f, 1.0f);
		FVector2D MissionBadgePosition = FVector2D(0.0f, -10.0f);
	};

	/** Front-end geometry selected beside the semantic brushes. Legacy values are Mode A. */
	struct FFrontendLayout
	{
		FMargin CustomizeScreenPadding = FMargin(120.0f, 55.0f);
		float CustomizeMinorTabInset = 180.0f;
		float CustomizePreviewHeight = 225.0f;
		float CustomizeCatalogWidth = 0.0f;
		int32 CustomizeDeckColumns = 4;
		bool bUseIntegratedSuitCatalog = false;
		bool bUseDenseSuitCards = false;
	};

	enum class ETextRole : uint8
	{
		Display,
		H1,
		H2,
		Body,
		Caption,
		Micro,
		NumericLarge
	};

	enum class EButtonSound : uint8
	{
		Click,
		Back,
		None
	};

	enum class ENumericLargeSize : uint8
	{
		Standard,
		Emphasized
	};

	enum class EChamferCorner : uint8
	{
		None = 0,
		TopLeft = 1 << 0,
		TopRight = 1 << 1,
		BottomRight = 1 << 2,
		BottomLeft = 1 << 3,
		All = 0x0f
	};
	ENUM_CLASS_FLAGS(EChamferCorner);
}

namespace MSUIStyleSize
{
	inline constexpr float Display = 30.0f;
	inline constexpr float H1 = 24.0f;
	inline constexpr float H2 = 18.0f;
	inline constexpr float Body = 14.0f;
	inline constexpr float Caption = 12.0f;
	inline constexpr float Micro = 11.0f;
	inline constexpr float NumericLarge = 22.0f;
	inline constexpr float NumericLargeEmphasized = 27.0f;

	inline constexpr float Space1 = 4.0f;
	inline constexpr float Space2 = 8.0f;
	inline constexpr float Space3 = 12.0f;
	inline constexpr float Space4 = 16.0f;
	inline constexpr float Space5 = 22.0f;
	inline constexpr float BorderNormal = 2.0f;
	inline constexpr float BorderEmphasized = 3.0f;
}

namespace MSUIStyleColor
{
	NEOMSGO_API extern const FLinearColor PanelBackground;
	NEOMSGO_API extern const FLinearColor FrameAccent;
	NEOMSGO_API extern const FLinearColor TextPrimary;
	NEOMSGO_API extern const FLinearColor TextSecondary;
	NEOMSGO_API extern const FLinearColor WarningColor;
	NEOMSGO_API extern const FLinearColor SelectedAccent;
}

namespace MSUIStyle
{
	NEOMSGO_API float GetFontSize(ETextRole Role,
		ENumericLargeSize NumericSize = ENumericLargeSize::Standard);

	/** Persistent UI mode, stored beside the existing audio preset in GameUserSettings.ini. */
	NEOMSGO_API EUIMode GetSelectedMode();
	NEOMSGO_API void SetSelectedMode(EUIMode Mode, bool bSave = true);

	/** Returns a flat Mode A brush or the authored Mode B 9-slice for the semantic role. */
	NEOMSGO_API FSlateBrush GetBrush(EBrush Role,
		const FLinearColor& ModeAColor = MSUIStyleColor::PanelBackground);
	NEOMSGO_API void ApplyBrush(UBorder* Border, EBrush Role,
		const FLinearColor& ModeAColor = MSUIStyleColor::PanelBackground);
	/** Applies authored gauge frame/fill images in Mode B and leaves Mode A untouched. */
	NEOMSGO_API void ApplyProgressBarStyle(UProgressBar* ProgressBar, EBrush FillRole);
	NEOMSGO_API FHUDLayout GetHUDLayout();
	NEOMSGO_API FFrontendLayout GetFrontendLayout();

	/** Uses the authored card-button frame in Mode B. */
	NEOMSGO_API void ApplyContentButtonStyle(UButton* Button);
	/** Stretches a button's content over the whole button. UButtonSlot centres its child at the
	    child's desired size by default, which shrinks a card's visible frame to the size of its
	    label and leaves most of the allotted button area empty. Call after SetContent(). */
	NEOMSGO_API void StretchButtonContent(UButton* Button);
	/** Mode B gets one compact front-end instrument frame; Mode A returns Content unchanged. */
	NEOMSGO_API UWidget* MakeFrontendWorkArea(UWidgetTree* Tree, UWidget* Content);

	NEOMSGO_API UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, ETextRole Role,
		bool bForceBold = false, const FLinearColor& Color = MSUIStyleColor::TextPrimary,
		ENumericLargeSize NumericSize = ENumericLargeSize::Standard, const FName Name = NAME_None,
		bool bAllowWrap = true);

	NEOMSGO_API UBorder* MakeFramedPanel(UWidgetTree* Tree, UBorder*& OutInnerBorder,
		UWidget* Content = nullptr, bool bEmphasized = false,
		const FLinearColor& FrameColor = MSUIStyleColor::FrameAccent,
		const FLinearColor& InPanelColor = MSUIStyleColor::PanelBackground,
		const FMargin& PanelPadding = FMargin(MSUIStyleSize::Space3));

	NEOMSGO_API UCanvasPanel* MakeChamferedFrame(UWidgetTree* Tree, UBorder*& OutInnerBorder,
		EChamferCorner Corners = EChamferCorner::All, UWidget* Content = nullptr,
		float ChamferSize = MSUIStyleSize::Space3);

	NEOMSGO_API UButton* MakeCapsuleButton(UWidgetTree* Tree, UWidget* Content = nullptr,
		const FLinearColor& NormalColor = MSUIStyleColor::PanelBackground,
		const FName Name = NAME_None, EButtonSound Sound = EButtonSound::Click);

	/** Applies an authored pressed sound to any button, including non-capsule styles. */
	NEOMSGO_API void ApplyButtonSound(UButton* Button, EButtonSound Sound = EButtonSound::Click);
}
