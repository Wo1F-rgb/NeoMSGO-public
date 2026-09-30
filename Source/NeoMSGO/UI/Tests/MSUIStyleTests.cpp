#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/MSUIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIStyleTextTest, "NeoMSGO.UI.Style.TextRoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSUIStyleTextTest::RunTest(const FString&)
{
	UWidgetTree* Tree = NewObject<UWidgetTree>();
	struct FRoleExpectation
	{
		MSUIStyle::ETextRole Role;
		float Size;
		FName Weight;
	};
	const FRoleExpectation Expectations[] =
	{
		{ MSUIStyle::ETextRole::Display, MSUIStyleSize::Display, TEXT("Bold") },
		{ MSUIStyle::ETextRole::H1, MSUIStyleSize::H1, TEXT("Bold") },
		{ MSUIStyle::ETextRole::H2, MSUIStyleSize::H2, TEXT("Bold") },
		{ MSUIStyle::ETextRole::Body, MSUIStyleSize::Body, TEXT("Regular") },
		{ MSUIStyle::ETextRole::Caption, MSUIStyleSize::Caption, TEXT("Regular") },
		{ MSUIStyle::ETextRole::Micro, MSUIStyleSize::Micro, TEXT("Regular") },
		{ MSUIStyle::ETextRole::NumericLarge, MSUIStyleSize::NumericLarge, TEXT("Bold") }
	};
	for (const FRoleExpectation& Expectation : Expectations)
	{
		UTextBlock* RoleText = MSUIStyle::MakeText(Tree, FText::GetEmpty(), Expectation.Role);
		TestEqual(TEXT("role font size"), RoleText->GetFont().Size, Expectation.Size);
		TestEqual(TEXT("role default weight"), RoleText->GetFont().TypefaceFontName, Expectation.Weight);
	}

	UTextBlock* Heading = MSUIStyle::MakeText(Tree, FText::FromString(TEXT("Heading")), MSUIStyle::ETextRole::H1);
	TestNotNull(TEXT("text helper creates a text block"), Heading);
	TestEqual(TEXT("H1 size"), Heading->GetFont().Size, MSUIStyleSize::H1);
	TestEqual(TEXT("H1 is bold by role"), Heading->GetFont().TypefaceFontName, FName(TEXT("Bold")));
	TestEqual(TEXT("primary text color"), Heading->GetColorAndOpacity().GetSpecifiedColor(), MSUIStyleColor::TextPrimary);
	TestTrue(TEXT("styled text wraps to its allotted width"), Heading->GetAutoWrapText());
	TestEqual(TEXT("styled text cannot paint outside its allotted geometry"), Heading->GetClipping(),
		EWidgetClipping::ClipToBounds);

	UTextBlock* BoldBody = MSUIStyle::MakeText(Tree, FText::GetEmpty(), MSUIStyle::ETextRole::Body, true);
	TestEqual(TEXT("body size"), BoldBody->GetFont().Size, MSUIStyleSize::Body);
	TestEqual(TEXT("all roles can request bold"), BoldBody->GetFont().TypefaceFontName, FName(TEXT("Bold")));
	UTextBlock* LargeNumber = MSUIStyle::MakeText(Tree, FText::GetEmpty(), MSUIStyle::ETextRole::NumericLarge,
		false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Emphasized);
	TestEqual(TEXT("emphasized numeric size"), LargeNumber->GetFont().Size, MSUIStyleSize::NumericLargeEmphasized);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIStyleFramedPanelTest, "NeoMSGO.UI.Style.FramedPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSUIStyleFramedPanelTest::RunTest(const FString&)
{
	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeA, false);
	UWidgetTree* Tree = NewObject<UWidgetTree>();
	UTextBlock* Content = Tree->ConstructWidget<UTextBlock>();
	UBorder* Inner = nullptr;
	UBorder* Outer = MSUIStyle::MakeFramedPanel(Tree, Inner, Content, true);
	TestNotNull(TEXT("outer border created"), Outer);
	TestTrue(TEXT("outer contains inner border"), Outer->GetContent() == Inner);
	TestTrue(TEXT("inner contains supplied content"), Inner && Inner->GetContent() == Content);
	TestEqual(TEXT("outer accent color"), Outer->GetBrushColor(), MSUIStyleColor::FrameAccent);
	TestEqual(TEXT("inner panel color"), Inner->GetBrushColor(), MSUIStyleColor::PanelBackground);
	TestEqual(TEXT("emphasized border thickness"), Outer->GetPadding().Left, MSUIStyleSize::BorderEmphasized);
	TestEqual(TEXT("outer frame clips overflowing children"), Outer->GetClipping(), EWidgetClipping::ClipToBounds);
	TestEqual(TEXT("inner panel clips overflowing content"), Inner->GetClipping(), EWidgetClipping::ClipToBounds);
	UBorder* NormalInner = nullptr;
	UBorder* NormalOuter = MSUIStyle::MakeFramedPanel(Tree, NormalInner);
	TestEqual(TEXT("normal border thickness"), NormalOuter->GetPadding().Left, MSUIStyleSize::BorderNormal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIStyleChamferedFrameTest, "NeoMSGO.UI.Style.ChamferedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSUIStyleChamferedFrameTest::RunTest(const FString&)
{
	UWidgetTree* Tree = NewObject<UWidgetTree>();
	UBorder* Inner = nullptr;
	UCanvasPanel* Frame = MSUIStyle::MakeChamferedFrame(Tree, Inner,
		MSUIStyle::EChamferCorner::TopLeft | MSUIStyle::EChamferCorner::BottomRight);
	TestNotNull(TEXT("chamfer helper creates a canvas"), Frame);
	TestEqual(TEXT("frame plus two selected corner masks"), Frame->GetChildrenCount(), 3);
	TestTrue(TEXT("first child is the framed border"), Cast<UBorder>(Frame->GetChildAt(0)) != nullptr);
	for (int32 Index = 1; Index < Frame->GetChildrenCount(); ++Index)
	{
		const UImage* Mask = Cast<UImage>(Frame->GetChildAt(Index));
		TestNotNull(TEXT("chamfer is an image"), Mask);
		if (Mask)
		{
			TestEqual(TEXT("chamfer uses the HUD marker's 45 degree transform"), Mask->GetRenderTransformAngle(), 45.0f);
			TestEqual(TEXT("chamfer mask color"), Mask->GetBrush().TintColor.GetSpecifiedColor(), MSUIStyleColor::PanelBackground);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIStyleCapsuleButtonTest, "NeoMSGO.UI.Style.CapsuleButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSUIStyleCapsuleButtonTest::RunTest(const FString&)
{
	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeA, false);
	UWidgetTree* Tree = NewObject<UWidgetTree>();
	UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
	UButton* Button = MSUIStyle::MakeCapsuleButton(Tree, Label);
	TestNotNull(TEXT("capsule helper creates a button"), Button);
	TestTrue(TEXT("button contains supplied label"), Button->GetContent() == Label);
	const FButtonStyle& Style = Button->GetStyle();
	TestEqual(TEXT("normal brush is rounded"), Style.Normal.DrawAs, ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("normal brush uses half-height radius"), Style.Normal.OutlineSettings.RoundingType,
		ESlateBrushRoundingType::HalfHeightRadius);
	TestEqual(TEXT("normal capsule color"), Style.Normal.TintColor.GetSpecifiedColor(), MSUIStyleColor::PanelBackground);
	TestEqual(TEXT("pressed capsule color"), Style.Pressed.TintColor.GetSpecifiedColor(), MSUIStyleColor::SelectedAccent);
	TestEqual(TEXT("capsule clips overflowing content"), Button->GetClipping(), EWidgetClipping::ClipToBounds);
	UObject* ClickSound = Style.PressedSlateSound.GetResourceObject();
	TestNotNull(TEXT("default capsule pressed sound"), ClickSound);
	if (ClickSound)
	{
		TestEqual(TEXT("default capsule uses UI click"), ClickSound->GetPathName(),
			FString(TEXT("/Game/Audio/Imported/SFX_UI_Click.SFX_UI_Click")));
	}

	UButton* BackButton = MSUIStyle::MakeCapsuleButton(Tree, nullptr,
		MSUIStyleColor::PanelBackground, NAME_None, MSUIStyle::EButtonSound::Back);
	UObject* BackSound = BackButton->GetStyle().PressedSlateSound.GetResourceObject();
	TestNotNull(TEXT("back capsule pressed sound"), BackSound);
	if (BackSound)
	{
		TestEqual(TEXT("back capsule uses UI back"), BackSound->GetPathName(),
			FString(TEXT("/Game/Audio/Imported/SFX_UI_Back.SFX_UI_Back")));
	}

	UButton* SilentButton = MSUIStyle::MakeCapsuleButton(Tree, nullptr,
		MSUIStyleColor::PanelBackground, NAME_None, MSUIStyle::EButtonSound::None);
	TestNull(TEXT("None leaves the pressed sound empty"),
		SilentButton->GetStyle().PressedSlateSound.GetResourceObject());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUIStyleModeBBrushTest, "NeoMSGO.UI.Style.ModeBBrushResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMSUIStyleModeBBrushTest::RunTest(const FString&)
{
	const FLinearColor LegacyColor(0.12f, 0.23f, 0.34f, 0.45f);
	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeA, false);
	const FSlateBrush TypeA = MSUIStyle::GetBrush(MSUIStyle::EBrush::LobbyPanel, LegacyColor);
	TestNull(TEXT("Mode A has no texture resource"), TypeA.GetResourceObject());
	TestEqual(TEXT("Mode A retains the exact legacy color"), TypeA.TintColor.GetSpecifiedColor(), LegacyColor);
	const MSUIStyle::FHUDLayout TypeAHUD = MSUIStyle::GetHUDLayout();
	const MSUIStyle::FFrontendLayout TypeAFrontend = MSUIStyle::GetFrontendLayout();
	TestEqual(TEXT("Mode A retains the legacy weapon stack anchor"), TypeAHUD.WeaponAnchorY, 0.575f);
	TestEqual(TEXT("Mode A retains the detached boost anchor"), TypeAHUD.BoostAnchorX, 0.62f);
	TestFalse(TEXT("Mode A does not add the Mode B vital frame"), TypeAHUD.bShowVitalFrame);
	TestFalse(TEXT("Mode A does not add density instrument rules"), TypeAHUD.bShowDensityInstrumentRules);
	TestFalse(TEXT("Mode A does not add density roster"), TypeAHUD.bShowTeamRoster);
	TestFalse(TEXT("Mode A does not add weapon selection triangle"), TypeAHUD.bShowWeaponSelectionMarker);
	TestEqual(TEXT("Mode A retains mission badge anchor"), TypeAHUD.MissionBadgeAnchor, FVector2D(.5f, 1.0f));
	TestEqual(TEXT("Mode A retains customize left padding"), TypeAFrontend.CustomizeScreenPadding.Left, 120.0f);
	TestEqual(TEXT("Mode A retains customize preview height"), TypeAFrontend.CustomizePreviewHeight, 225.0f);

	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeB, false);
	const FSlateBrush TypeB = MSUIStyle::GetBrush(MSUIStyle::EBrush::LobbyPanel, LegacyColor);
	TestNotNull(TEXT("Mode B resolves the imported texture"), TypeB.GetResourceObject());
	TestEqual(TEXT("Mode B stretchable plate uses Box"), TypeB.DrawAs, ESlateBrushDrawType::Box);
	TestEqual(TEXT("Mode B left margin comes from plates.json"), TypeB.Margin.Left, 0.265625f);
	TestEqual(TEXT("Mode B top margin comes from plates.json"), TypeB.Margin.Top, 0.265625f);
	const FSlateBrush WeaponSlot = MSUIStyle::GetBrush(MSUIStyle::EBrush::WeaponSlotActive, LegacyColor);
	TestNotNull(TEXT("Mode B selected weapon slot resolves its authored plate"), WeaponSlot.GetResourceObject());
	TestEqual(TEXT("weapon slot margin comes from plates.json"), WeaponSlot.Margin.Left, 0.25f);
	const MSUIStyle::FHUDLayout TypeBHUD = MSUIStyle::GetHUDLayout();
	const MSUIStyle::FFrontendLayout TypeBFrontend = MSUIStyle::GetFrontendLayout();
	TestEqual(TEXT("Mode B moves the weapon stack to the reference lower-right band"), TypeBHUD.WeaponAnchorY, 0.69f);
	TestEqual(TEXT("Mode B joins boost to the right-side vital cluster"), TypeBHUD.BoostAnchorX, 0.985f);
	TestTrue(TEXT("Mode B enables the shared vital frame"), TypeBHUD.bShowVitalFrame);
	TestTrue(TEXT("Mode B enables density instrument rules"), TypeBHUD.bShowDensityInstrumentRules);
	TestTrue(TEXT("Mode B enables actual allied roster"), TypeBHUD.bShowTeamRoster);
	TestTrue(TEXT("Mode B enables timed battle log"), TypeBHUD.bShowTimedBattleLog);
	TestTrue(TEXT("Mode B enables minimap role bubbles"), TypeBHUD.bShowMinimapRoleBubbles);
	TestTrue(TEXT("Mode B enables weapon selection triangle"), TypeBHUD.bShowWeaponSelectionMarker);
	TestTrue(TEXT("Mode B enables system-down warning"), TypeBHUD.bShowSystemDownWarning);
	// Lobby_Round2_Design: the integrated inventory needs the wider 2x2 deck layout.
	TestEqual(TEXT("Mode B leaves room for the deck and inventory"), TypeBFrontend.CustomizeScreenPadding.Left, 120.0f);
	TestEqual(TEXT("Mode B uses dense deck previews"), TypeBFrontend.CustomizePreviewHeight, 116.0f);
	TestEqual(TEXT("Mode B uses two deck columns"), TypeBFrontend.CustomizeDeckColumns, 2);
	TestTrue(TEXT("Mode B enables the integrated suit inventory"), TypeBFrontend.bUseIntegratedSuitCatalog);
	UWidgetTree* ButtonTree = NewObject<UWidgetTree>();
	UButton* ContentButton = ButtonTree->ConstructWidget<UButton>();
	MSUIStyle::ApplyContentButtonStyle(ContentButton);
	TestEqual(TEXT("Mode B content button draws the restored card frame"),
		ContentButton->GetStyle().Normal.DrawAs, ESlateBrushDrawType::Box);
	TestNotNull(TEXT("Mode B card frame uses its authored texture"),
		ContentButton->GetStyle().Normal.GetResourceObject());
	UTextBlock* WorkContent = ButtonTree->ConstructWidget<UTextBlock>();
	TestTrue(TEXT("Mode B wraps front-end content in one compact frame"),
		MSUIStyle::MakeFrontendWorkArea(ButtonTree, WorkContent) != WorkContent);
	MSUIStyle::SetSelectedMode(MSUIStyle::EUIMode::TypeA, false);
	return true;
}

#endif
