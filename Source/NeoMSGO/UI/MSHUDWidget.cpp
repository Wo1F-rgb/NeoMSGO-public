#include "UI/MSHUDWidget.h"
#include "UI/MSFactionPresentation.h"
#include "UI/MSAvatarLibrary.h"
#include "UI/MSHUDLockOnMarkerWidget.h"
#include "UI/MSHUDRingWidget.h"
#include "UI/MSHUDScopeWidget.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSHUDVectorBackdrop.h"
#include "UI/MSMapCaptureSubsystem.h"
#include "UI/MSMapRadarRangeWidget.h"
#include "UI/MSMapTypes.h"
#include "UI/MSMapWidgetBase.h"
#include "UI/MSPixelPainter.h"
#include "UI/MSPixelUI.h"
#include "UI/MSUIStyle.h"
#include "UI/MSVectorSkinWidget.h"
#include "UI/MSWeaponIconWidget.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSSupplyPack.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSDeckSubsystem.h"
#include "Weapons/MSWeaponComponent.h"
#include "AbilitySystemComponent.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
// Pixel measurements are taken from the three active-battle reference frames:
// video1-t0500, video3-t0180 and video4-t0600 (1920x1080, 2026-09-11 pass).
// The vector pass is deliberately local to the combat HUD so shared MSUIStyle
// geometry remains owned by the frontend work.
const FVector2D VideoDesignSize(1920.0f, 1080.0f);
constexpr float VideoWeaponX = 1750.0f;
constexpr float VideoWeaponY = 748.0f;
constexpr float VideoWeaponWidth = 170.0f;
constexpr float VideoWeaponHeight = 56.0f;
constexpr float VideoWeaponStep = 66.0f;
constexpr float VideoArmorX = 1600.0f;
constexpr float VideoArmorY = 984.0f;
constexpr float VideoArmorWidth = 320.0f;
constexpr float VideoArmorHeight = 64.0f;
constexpr float VideoShieldX = 1745.0f;
constexpr float VideoShieldY = 978.0f;
constexpr float VideoShieldWidth = 171.0f;
constexpr float VideoShieldHeight = 10.0f;
constexpr float ShieldLowThreshold = MSHUDPalette::ArmorLowThreshold; // 353 remaining is red in MSGO1_0681; exact transition is not visible.
constexpr float VideoBoostX = 1357.0f;
constexpr float VideoBoostY = 1014.0f;
constexpr float VideoBoostWidth = 243.0f;
constexpr float VideoBoostHeight = 34.0f;

// The tactical frame is 713x468 in the reference.  The level-wide overview
// is square, so keep it aspect-correct and center it in that frame instead of
// stretching the player's 256px local capture.
constexpr float RespawnMapDisplayX = 612.0f;
constexpr float RespawnMapDisplayY = 309.0f;
constexpr float RespawnMapDisplayWidth = 713.0f;
constexpr float RespawnMapDisplayHeight = 468.0f;
constexpr float RespawnMapTextureX = RespawnMapDisplayX + (RespawnMapDisplayWidth - RespawnMapDisplayHeight) * 0.5f;
constexpr float RespawnMapTextureY = RespawnMapDisplayY;
constexpr float RespawnMapTextureSize = RespawnMapDisplayHeight;

// Normalized canvas bounds alone resize the box, not the font. Keep each video
// label in its measured design box and scale the complete label at 720p too.
UWidget* ScaleVideoLabel(UWidgetTree* Tree, UTextBlock* Text, const FVector2D& Size)
{
	USizeBox* Design = Tree->ConstructWidget<USizeBox>();
	Design->SetWidthOverride(Size.X); Design->SetHeightOverride(Size.Y); Design->SetContent(Text);
	UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both); Fit->SetContent(Design);
	return Fit;
}

UWidget* ScaleVideoContent(UWidgetTree* Tree, UWidget* Content, const FVector2D& Size)
{
	USizeBox* Design=Tree->ConstructWidget<USizeBox>();
	Design->SetWidthOverride(Size.X);Design->SetHeightOverride(Size.Y);Design->SetContent(Content);
	UScaleBox* Fit=Tree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::Both);Fit->SetContent(Design);
	return Fit;
}

void SetDesignSlot(UCanvasPanelSlot* Slot,const FVector2D& Position,const FVector2D& Size)
{
	if(!Slot)return;
	const FVector2D Min(Position.X/VideoDesignSize.X,Position.Y/VideoDesignSize.Y);
	const FVector2D Max((Position.X+Size.X)/VideoDesignSize.X,(Position.Y+Size.Y)/VideoDesignSize.Y);
	Slot->SetAnchors(FAnchors(Min.X,Min.Y,Max.X,Max.Y));
	Slot->SetOffsets(FMargin(0.0f));
	Slot->SetAlignment(FVector2D::ZeroVector);
}

void SetRelativeSlot(UCanvasPanelSlot* Slot,const FVector2D& Position,const FVector2D& Size,
	const FVector2D& ParentSize)
{
	if(!Slot)return;
	const FVector2D Min(Position.X/ParentSize.X,Position.Y/ParentSize.Y);
	const FVector2D Max((Position.X+Size.X)/ParentSize.X,(Position.Y+Size.Y)/ParentSize.Y);
	Slot->SetAnchors(FAnchors(Min.X,Min.Y,Max.X,Max.Y));
	Slot->SetOffsets(FMargin(0.0f));
	Slot->SetAlignment(FVector2D::ZeroVector);
}

void DrawGauge(MSPixel::FPainter& Painter,float X,float Y,float Width,float Height,float Ratio,
	const FLinearColor& Fill,const FLinearColor& Frame,bool bTicks=true)
{
	const float Clamped=FMath::Clamp(Ratio,0.0f,1.0f);
	Painter.Rect(X,Y,Width,Height,FLinearColor(0.015f,0.035f,0.045f,0.82f));
	if(Clamped>0.0f)Painter.Rect(X+1.0f,Y+1.0f,FMath::Max(0.0f,(Width-2.0f)*Clamped),FMath::Max(1.0f,Height-2.0f),Fill);
	Painter.Box(X,Y,Width,Height,1.0f,Frame);
	if(bTicks)
	{
		for(int32 Index=1;Index<10;++Index)
		{
			const float TickX=X+Width*static_cast<float>(Index)/10.0f;
			Painter.Line({TickX,Y+1.0f},{TickX,Y+Height-1.0f},0.6f,FLinearColor(0.72f,0.82f,0.82f,0.30f));
		}
	}
}

void DrawAngledGauge(MSPixel::FPainter& P,float X,float Y,float W,float H,float Ratio,FLinearColor Fill)
{
	P.Polygon({{X,Y+H},{X+H*.65f,Y},{X+W,Y},{X+W-H*.65f,Y+H}},MSPixel::Hex(0xa9c3bc));
	P.Polygon({{X+3,Y+H-2},{X+H*.65f+1,Y+2},{X+W-3,Y+2},{X+W-H*.65f-1,Y+H-2}},MSPixel::Hex(0x073239));
	const float F=FMath::Clamp(Ratio,0.f,1.f)*(W-H*.65f-7);
	if(F>1) P.Polygon({{X+5,Y+H-4},{X+H*.65f+2,Y+4},{X+H*.65f+2+F,Y+4},{X+5+F,Y+H-4}},Fill);
}

template<class T> T* AddCanvas(UWidgetTree* Tree,UCanvasPanel* Root,FVector2D Pos,FVector2D Size,FVector2D Align=FVector2D::ZeroVector)
{
	T* W=Tree->ConstructWidget<T>(); auto* S=Root->AddChildToCanvas(W);
	S->SetAnchors(FAnchors(Pos.X,Pos.Y,Pos.X,Pos.Y)); S->SetAlignment(Align); S->SetPosition(FVector2D::ZeroVector); S->SetSize(Size); return W;
}

UImage* AddMinimapImage(UWidgetTree* Tree,UCanvasPanel* Root,const FVector2D& Position,const FVector2D& Size,const FLinearColor& Color)
{
	UImage* Image=Tree->ConstructWidget<UImage>();
	FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Box;Brush.TintColor=Color;Image->SetBrush(Brush);
	auto* Slot=Root->AddChildToCanvas(Image);Slot->SetAlignment(FVector2D(.5f,.5f));Slot->SetPosition(Position);Slot->SetSize(Size);
	return Image;
}
}

void UMSHUDWidget::SetViewModel(UMSHUDViewModel* V){ViewModel=V;}

FBox2d UMSHUDWidget::GetRespawnMapContentRect(const FVector2D& WorldHalfExtentsCm)
{
	const FVector2D ContentSize=UMSMapWidgetBase::GetRectContentSize(WorldHalfExtentsCm,
		FVector2D(RespawnMapTextureSize),0.0f);
	const FVector2D Min=FVector2D(RespawnMapTextureX,RespawnMapTextureY)
		+(FVector2D(RespawnMapTextureSize)-ContentSize)*.5f;
	return FBox2d(FVector2d(Min),FVector2d(Min+ContentSize));
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSHUDWidget::InitializeForAutomationTests(){if(!RootCanvas)NativeOnInitialized();}
void UMSHUDWidget::RefreshGaugesForAutomationTests(){RefreshGaugeWidgets();}
void UMSHUDWidget::RefreshCaptureForAutomationTests(){RefreshCaptureWidgets();}
void UMSHUDWidget::RefreshCombatForAutomationTests(const bool bForceHomingTargetProjected){RefreshCombatWidgets(bForceHomingTargetProjected);RefreshIncomingLockWidgets();}
FLinearColor UMSHUDWidget::GetBoostFillColorForAutomationTests() const{return BoostBar?BoostBar->GetFillColorAndOpacity():FLinearColor::Transparent;}
float UMSHUDWidget::GetBoostPercentForAutomationTests() const{return BoostBar?BoostBar->GetPercent():-1.0f;}
FName UMSHUDWidget::GetLockOnDistanceTypefaceForAutomationTests() const{return LockOnDistanceText?LockOnDistanceText->GetFont().TypefaceFontName:NAME_None;}
ESlateVisibility UMSHUDWidget::GetReticleVisibilityForAutomationTests() const{return Reticle?Reticle->GetVisibility():ESlateVisibility::Collapsed;}
ESlateVisibility UMSHUDWidget::GetScopeOverlayVisibilityForAutomationTests() const{return ScopeOverlay?ScopeOverlay->GetVisibility():ESlateVisibility::Collapsed;}
FText UMSHUDWidget::GetShieldTextForAutomationTests() const{return ShieldText?ShieldText->GetText():FText::GetEmpty();}
FText UMSHUDWidget::GetShieldValueForAutomationTests() const{return ShieldValueText?ShieldValueText->GetText():FText::GetEmpty();}
FText UMSHUDWidget::GetShieldBrokenTextForAutomationTests() const{return ShieldBrokenText?ShieldBrokenText->GetText():FText::GetEmpty();}
float UMSHUDWidget::GetShieldPercentForAutomationTests() const{return ShieldBar?ShieldBar->GetPercent():-1.0f;}
ESlateVisibility UMSHUDWidget::GetShieldVisibilityForAutomationTests() const{return ShieldText?ShieldText->GetVisibility():ESlateVisibility::Collapsed;}
FLinearColor UMSHUDWidget::GetShieldFillColorForAutomationTests() const{return ShieldBar?ShieldBar->GetFillColorAndOpacity():FLinearColor::Transparent;}
int32 UMSHUDWidget::GetIncomingDirectionCountForAutomationTests() const
{
	int32 Count=0;for(const TObjectPtr<UTextBlock>& Indicator:IncomingLockIndicators)if(Indicator&&Indicator->GetVisibility()!=ESlateVisibility::Collapsed)++Count;return Count;
}
int32 UMSHUDWidget::GetIncomingDistanceCountForAutomationTests() const
{
	int32 Count=0;for(const TObjectPtr<UTextBlock>& Distance:IncomingLockDistanceTexts)if(Distance&&Distance->GetVisibility()!=ESlateVisibility::Collapsed)++Count;return Count;
}
bool UMSHUDWidget::IsIncomingMissileWarningVisibleForAutomationTests() const{return IncomingMissileWarningText&&IncomingMissileWarningText->GetVisibility()!=ESlateVisibility::Collapsed;}
bool UMSHUDWidget::IsIncomingLockEdgeFlashVisibleForAutomationTests() const{return IncomingLockEdgeLeft&&IncomingLockEdgeLeft->GetVisibility()!=ESlateVisibility::Collapsed&&IncomingLockEdgeRight&&IncomingLockEdgeRight->GetVisibility()!=ESlateVisibility::Collapsed;}
float UMSHUDWidget::GetIncomingDirectionAngleForAutomationTests(const int32 Index) const{return IncomingLockIndicators.IsValidIndex(Index)&&IncomingLockIndicators[Index]?IncomingLockIndicators[Index]->GetRenderTransformAngle():0.0f;}
#endif

void UMSHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	bVideoHudArtPass=MSUIStyle::GetHUDLayout().bShowVitalFrame;
	RootCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=RootCanvas;
	if (bVideoHudArtPass)
	{
		UMSHUDVectorBackdrop* Art = WidgetTree->ConstructWidget<UMSHUDVectorBackdrop>();
		Art->SetHUD(this); Art->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* ArtSlot = RootCanvas->AddChildToCanvas(Art);
		ArtSlot->SetAnchors(FAnchors(0,0,1,1)); ArtSlot->SetOffsets(FMargin(0)); ArtSlot->SetZOrder(-100);
	}
	ScopeOverlay=WidgetTree->ConstructWidget<UMSHUDScopeWidget>();
	{UCanvasPanelSlot* ScopeSlot=RootCanvas->AddChildToCanvas(ScopeOverlay);ScopeSlot->SetAnchors(FAnchors(0,0,1,1));ScopeSlot->SetOffsets(FMargin(0));}
	ScopeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	BuildTopBattlePanel();
	BuildRightBottomCluster();
	// #114 removed the old circle, but the center cross remains the normal aiming reference.
	Reticle=AddCanvas<UCanvasPanel>(WidgetTree,RootCanvas,{.5f,.5f},{32,32},{.5f,.5f});
	{
		auto AddCrossBar=[this](const FVector2D& BarSize)
		{
			UImage* Bar=WidgetTree->ConstructWidget<UImage>();
			FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Box;Brush.TintColor=FLinearColor::White;Bar->SetBrush(Brush);
			UCanvasPanelSlot* BarSlot=Reticle->AddChildToCanvas(Bar);
			BarSlot->SetAnchors(FAnchors(.5f,.5f,.5f,.5f));BarSlot->SetAlignment(FVector2D(.5f,.5f));
			BarSlot->SetPosition(FVector2D::ZeroVector);BarSlot->SetSize(BarSize);
		};
		AddCrossBar({14,2});
		AddCrossBar({2,14});
	}
	Reticle->SetVisibility(ESlateVisibility::HitTestInvisible);
	HitMarker=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.5f},{120,64},{.5f,.5f});HitMarker->SetText(FText::FromString(TEXT("[     ]")));HitMarker->SetJustification(ETextJustify::Center);HitMarker->SetVisibility(ESlateVisibility::Collapsed);
	KillPopupText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.42f},{360,48},{.5f,.5f});KillPopupText->SetJustification(ETextJustify::Center);KillPopupText->SetVisibility(ESlateVisibility::Collapsed);
	KillStreakText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.47f},{360,34},{.5f,.5f});KillStreakText->SetJustification(ETextJustify::Center);KillStreakText->SetVisibility(ESlateVisibility::Collapsed);
	DamageDirectionIndicator=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.5f},{32,32},{.5f,.5f});DamageDirectionIndicator->SetText(FText::FromString(FString::Chr(0x25B2)));DamageDirectionIndicator->SetJustification(ETextJustify::Center);DamageDirectionIndicator->SetColorAndOpacity(MSHUDPalette::DamageDirection);DamageDirectionIndicator->SetVisibility(ESlateVisibility::Collapsed);
	{FSlateFontInfo Font=HitMarker->GetFont();Font.Size=38;Font.TypefaceFontName=TEXT("Bold");HitMarker->SetFont(Font);Font=KillPopupText->GetFont();Font.Size=30;KillPopupText->SetFont(Font);Font=KillStreakText->GetFont();Font.Size=20;KillStreakText->SetFont(Font);Font=DamageDirectionIndicator->GetFont();Font.Size=24;DamageDirectionIndicator->SetFont(Font);}
	// Incoming arrows deliberately sit outside the existing homing ring.  The
	// receiver component has already removed private/non-equipped threats.
	for(int32 I=0;I<10;++I)
	{
		UTextBlock* Arrow=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.5f},{28,28},{.5f,.5f});
		Arrow->SetText(FText::FromString(FString::Chr(0x25B2)));Arrow->SetJustification(ETextJustify::Center);Arrow->SetColorAndOpacity(MSHUDPalette::DamageDirection);Arrow->SetVisibility(ESlateVisibility::Collapsed);
		FSlateFontInfo Font=Arrow->GetFont();Font.Size=20;Font.TypefaceFontName=TEXT("Bold");Arrow->SetFont(Font);IncomingLockIndicators.Add(Arrow);
	}
	for(int32 I=0;I<2;++I)
	{
		UTextBlock* Distance=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.5f},{74,20},{.5f,.5f});
		Distance->SetJustification(ETextJustify::Center);Distance->SetColorAndOpacity(MSHUDPalette::DamageDirection);Distance->SetVisibility(ESlateVisibility::Collapsed);
		FSlateFontInfo Font=Distance->GetFont();Font.Size=12;Font.TypefaceFontName=TEXT("Bold");Distance->SetFont(Font);IncomingLockDistanceTexts.Add(Distance);
	}
	IncomingMissileWarningText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.18f},{360,28},{.5f,.5f});
	IncomingMissileWarningText->SetText(FText::FromString(TEXT("MISSILE LOCK")));IncomingMissileWarningText->SetJustification(ETextJustify::Center);IncomingMissileWarningText->SetColorAndOpacity(MSHUDPalette::DamageDirection);IncomingMissileWarningText->SetVisibility(ESlateVisibility::Collapsed);
	{FSlateFontInfo Font=IncomingMissileWarningText->GetFont();Font.Size=16;Font.TypefaceFontName=TEXT("Bold");IncomingMissileWarningText->SetFont(Font);}
	IncomingLockEdgeLeft=AddCanvas<UImage>(WidgetTree,RootCanvas,{0.0f,.5f},{12,320},{0.0f,.5f});
	IncomingLockEdgeRight=AddCanvas<UImage>(WidgetTree,RootCanvas,{1.0f,.5f},{12,320},{1.0f,.5f});
	UImage* IncomingEdges[2]={IncomingLockEdgeLeft,IncomingLockEdgeRight};
	for(UImage* Edge:IncomingEdges)
	{
		FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Box;Brush.TintColor=MSHUDPalette::DamageDirection;Edge->SetBrush(Brush);Edge->SetVisibility(ESlateVisibility::Collapsed);
	}
	CenterReload=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{.5f,.5f},{70,6},{.5f,-5.67f}); HomingRing=AddCanvas<UMSHUDRingWidget>(WidgetTree,RootCanvas,{.5f,.5f},{MSHUDPalette::HomingCursorSize,MSHUDPalette::HomingCursorSize},{.5f,.5f}); HomingTargetRing=AddCanvas<UMSHUDRingWidget>(WidgetTree,RootCanvas,{0,0},{MSHUDPalette::HomingTargetRingSize,MSHUDPalette::HomingTargetRingSize},{.5f,.5f}); ChargeRing=AddCanvas<UMSHUDRingWidget>(WidgetTree,RootCanvas,{.5f,.5f},{100,100},{.5f,.5f});
	ChargeRing->SetProgressOriginTop(true);
	CaptureRing=WidgetTree->ConstructWidget<UMSHUDRingWidget>(UMSHUDRingWidget::StaticClass(),TEXT("CaptureProgressFrame"));
	CaptureRing->Initialize();
	SetDesignSlot(RootCanvas->AddChildToCanvas(CaptureRing),{694,713},{MSHUDPalette::CaptureBarWidth,MSHUDPalette::CaptureBarHeight});
	CaptureText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CaptureStateText"));
	CaptureText->SetJustification(ETextJustify::Center);CaptureText->SetColorAndOpacity(FLinearColor::White);
	CaptureText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f));CaptureText->SetShadowOffset({1,1});
	{FSlateFontInfo Font=CaptureText->GetFont();Font.Size=MSHUDPalette::CaptureLabelFontSize;Font.TypefaceFontName=TEXT("Bold");CaptureText->SetFont(Font);}
	SetDesignSlot(RootCanvas->AddChildToCanvas(ScaleVideoLabel(WidgetTree,CaptureText,{520,30})),{694,722},{520,30});
	CaptureCompleteText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CaptureCompleteText"));
	CaptureCompleteText->SetText(FText::FromString(TEXT("制圧完了")));CaptureCompleteText->SetJustification(ETextJustify::Center);
	CaptureCompleteText->SetColorAndOpacity(FLinearColor(1,.46f,.05f));CaptureCompleteText->SetShadowColorAndOpacity(FLinearColor(0,0,0,1));CaptureCompleteText->SetShadowOffset({3,3});
	{auto Font=CaptureCompleteText->GetFont();Font.Size=42;Font.TypefaceFontName=TEXT("Bold");CaptureCompleteText->SetFont(Font);}
	SetDesignSlot(RootCanvas->AddChildToCanvas(ScaleVideoLabel(WidgetTree,CaptureCompleteText,{520,70})),{694,644},{520,70});
	CaptureCompleteText->SetVisibility(ESlateVisibility::Collapsed);
	SupplyText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.78f},{400,35},{.5f,0}); SupplyText->SetJustification(ETextJustify::Center);
	LockOnMarker=AddCanvas<UMSHUDLockOnMarkerWidget>(WidgetTree,RootCanvas,{0,0},{MSHUDPalette::LockOnMarkerSize,MSHUDPalette::LockOnMarkerSize},{.5f,.5f});
	LockOnMarker->SetRenderTransformAngle(0.f); LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	// Target distance (meters) sits to the right of the diamond, above its yellow leader.
	LockOnDistanceText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{0,0},{82,22},{0,0});
	LockOnDistanceText->SetJustification(ETextJustify::Left);LockOnDistanceText->SetColorAndOpacity(MSHUDPalette::LockOnMarkerNormal);
	{FSlateFontInfo DistFont=LockOnDistanceText->GetFont();DistFont.Size=14;DistFont.TypefaceFontName=TEXT("Bold");LockOnDistanceText->SetFont(DistFont);}
	LockOnDistanceText->SetVisibility(ESlateVisibility::Collapsed);
	for(int32 I=0;I<MSHUDPalette::MaxUnitBars;++I){auto* Bar=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{0,0},{MSHUDPalette::UnitBarWidth,MSHUDPalette::UnitBarHeight},{.5f,.5f});Bar->SetVisibility(ESlateVisibility::Collapsed);UnitBars.Add(Bar);}
	BuildLeftBottomCluster();
	BuildMissionIcon();
	InitializeMinimapUI();
	BuildTaskPanel();
	SystemDownText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,{.5f,.18f},{420,42},{.5f,.5f});
	SystemDownText->SetText(FText::FromString(TEXT("- システムダウン -")));SystemDownText->SetJustification(ETextJustify::Center);SystemDownText->SetColorAndOpacity(MSHUDPalette::DPWarning);
	{FSlateFontInfo Font=SystemDownText->GetFont();Font.Size=24;Font.TypefaceFontName=TEXT("Bold");SystemDownText->SetFont(Font);}
	SystemDownText->SetVisibility(ESlateVisibility::Collapsed);
	BuildModuleChargeGauge();
	BuildEmergencyRecallIndicator();
	const MSUIStyle::FHUDLayout HUDLayout=MSUIStyle::GetHUDLayout();
	ModuleStatusText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,HUDLayout.ModuleAnchor,{280,26},HUDLayout.ModuleAlignment);
	ModuleStatusText->SetJustification(ETextJustify::Right);ModuleStatusText->SetColorAndOpacity(FLinearColor(1.0f,.25f,.55f,1.0f));
	{FSlateFontInfo Font=ModuleStatusText->GetFont();Font.Size=14;Font.TypefaceFontName=TEXT("Bold");ModuleStatusText->SetFont(Font);}
	ModuleStatusText->SetVisibility(ESlateVisibility::Collapsed);
	// Debug convenience only: lets Settings be reached by click. Esc is the documented control
	// (spec: OpenSettings), but the editor's own embedded-PIE "Stop" shortcut also listens on
	// Esc and intercepts it before gameplay ever sees the key, so testers need another way in.
	{
		SettingsButton = WidgetTree->ConstructWidget<UButton>();
		UBorder* ButtonBg = WidgetTree->ConstructWidget<UBorder>();
		MSUIStyle::ApplyBrush(ButtonBg, MSUIStyle::EBrush::ListRow, FLinearColor(0.05f, 0.08f, 0.12f, 0.85f)); ButtonBg->SetPadding(FMargin(10.0f, 6.0f));
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FText::FromString(TEXT("設定")));
		ButtonBg->SetContent(ButtonText); SettingsButton->SetContent(ButtonBg);
		SettingsButton->OnClicked.AddDynamic(this, &UMSHUDWidget::HandleSettingsButtonClicked);
		UCanvasPanelSlot* ButtonSlot = RootCanvas->AddChildToCanvas(SettingsButton);
		// Anchored above the minimap (which starts at y=.03) so the two never overlap regardless
		// of resolution, since both use percentage anchors rather than mixed pixel offsets.
		ButtonSlot->SetAnchors(FAnchors(0.995f, 0.006f)); ButtonSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		ButtonSlot->SetPosition(FVector2D::ZeroVector); ButtonSlot->SetAutoSize(true);
		if(bVideoHudArtPass)
		{
			MSPixelUI::ClearButtonBrush(SettingsButton);
			FSlateFontInfo Font=ButtonText->GetFont();Font.Size=10;ButtonText->SetFont(Font);ButtonText->SetJustification(ETextJustify::Center);
			ButtonBg->SetPadding(FMargin(0));
			SettingsButton->SetContent(MSPixelUI::Frame(WidgetTree,ButtonText,EMSVectorSurface::Button,FMargin(4,2)));
			MSUIStyle::StretchButtonContent(SettingsButton);
			SettingsButton->RemoveFromParent();
			SettingsButtonVideoFit=ScaleVideoContent(WidgetTree,SettingsButton,{72,22});
			SetDesignSlot(RootCanvas->AddChildToCanvas(SettingsButtonVideoFit),{1844,353},{72,22});
		}
	}
	BuildVoiceCutIns();
	BuildRespawnPanel();
	HomingRing->SetVisibility(ESlateVisibility::HitTestInvisible); HomingTargetRing->SetVisibility(ESlateVisibility::Collapsed); ChargeRing->SetVisibility(ESlateVisibility::Collapsed); CaptureRing->SetVisibility(ESlateVisibility::Collapsed); CaptureText->SetVisibility(ESlateVisibility::Collapsed); SupplyText->SetVisibility(ESlateVisibility::Collapsed);
	if(UWorld* World=GetWorld())if(UMSUnitRegistrySubsystem* Registry=World->GetSubsystem<UMSUnitRegistrySubsystem>()){UnitRegistry=Registry;Registry->OnUnitRegistered.AddUObject(this,&UMSHUDWidget::SubscribeToUnit);Registry->ForEachRegisteredUnit([this](AMSUnitBase* Unit){SubscribeToUnit(Unit);});}
}

void UMSHUDWidget::BuildVoiceCutIns()
{
	if(bVideoHudArtPass)
	{
		// The live reference uses a compact two-piece communication cut-in in the
		// upper middle of the 16:9 frame. Keep the canvas in design pixels so the
		// complete portrait, frame and typography scale together at 1280x720.
		for(int32 Index=0;Index<2;++Index)
		{
			const bool bRight=Index==1;
			const float BaseY=bRight?258.0f:90.0f;
			UCanvasPanel* Panel=WidgetTree->ConstructWidget<UCanvasPanel>();
			UCanvasPanelSlot* PanelSlot=RootCanvas->AddChildToCanvas(Panel);
			PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(0));
			PanelSlot->SetAlignment(FVector2D::ZeroVector);PanelSlot->SetZOrder(100);

			auto AddDesign=[this,Panel](UWidget* Child,const FVector2D& Position,const FVector2D& Size)->UCanvasPanelSlot*
			{
				UCanvasPanelSlot* Slot=Panel->AddChildToCanvas(Child);SetDesignSlot(Slot,Position,Size);return Slot;
			};

			// Vector frame and the existing avatar texture are separate layers. The
			// frame is a shape pass; no screenshot or generated image is used here.
			UMSVectorSkinWidget* PortraitFrame=WidgetTree->ConstructWidget<UMSVectorSkinWidget>();
			PortraitFrame->SetSurface(EMSVectorSurface::Panel);
			PortraitFrame->SetAccent(MSPixel::Hex(0x52e8d5,.95f));
			PortraitFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
			AddDesign(PortraitFrame,{668.0f,BaseY},{148.0f,148.0f});

			UOverlay* PortraitContent=WidgetTree->ConstructWidget<UOverlay>();
			UBorder* PortraitFill=WidgetTree->ConstructWidget<UBorder>();
			FSlateBrush PortraitBrush;PortraitBrush.DrawAs=ESlateBrushDrawType::Box;
			PortraitBrush.TintColor=bRight?MSPixel::Hex(0x172e49,.96f):MSPixel::Hex(0x123d42,.96f);
			PortraitFill->SetBrush(PortraitBrush);
			PortraitContent->AddChildToOverlay(PortraitFill);
			UImage* PortraitImage=WidgetTree->ConstructWidget<UImage>();
			UOverlaySlot* ImageSlot=PortraitContent->AddChildToOverlay(PortraitImage);
			ImageSlot->SetPadding(FMargin(4.0f));
			ImageSlot->SetHorizontalAlignment(HAlign_Fill);ImageSlot->SetVerticalAlignment(VAlign_Fill);
			PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
			UTextBlock* Letter=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Display,true,FLinearColor::White);
			FSlateFontInfo LetterFont=Letter->GetFont();LetterFont.Size=64;Letter->SetFont(LetterFont);
			Letter->SetJustification(ETextJustify::Center);Letter->SetVisibility(ESlateVisibility::Collapsed);
			UOverlaySlot* LetterSlot=PortraitContent->AddChildToOverlay(ScaleVideoLabel(WidgetTree,Letter,{148.0f,148.0f}));
			LetterSlot->SetHorizontalAlignment(HAlign_Fill);LetterSlot->SetVerticalAlignment(VAlign_Center);
			AddDesign(PortraitContent,{671.0f,BaseY+3.0f},{142.0f,142.0f});

			UMSVectorSkinWidget* DialogueFrame=WidgetTree->ConstructWidget<UMSVectorSkinWidget>();
			DialogueFrame->SetSurface(EMSVectorSurface::Panel);
			DialogueFrame->SetAccent(MSPixel::Hex(0x52e8d5,.95f));
			DialogueFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
			AddDesign(DialogueFrame,{827.0f,BaseY},{266.0f,58.0f});

			UTextBlock* SpeakerName=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Caption,true,FLinearColor::White);
			FSlateFontInfo SpeakerFont=SpeakerName->GetFont();SpeakerFont.Size=8;SpeakerName->SetFont(SpeakerFont);
			SpeakerName->SetClipping(EWidgetClipping::ClipToBounds);SpeakerName->SetJustification(ETextJustify::Left);
			AddDesign(ScaleVideoLabel(WidgetTree,SpeakerName,{246.0f,13.0f}),{837.0f,BaseY+5.0f},{246.0f,13.0f});

			// PRESET VOICE / ENEMY VOICE labels are implementation metadata, not
			// part of the recorded game's communication box.
			UTextBlock* Caption=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Caption,true,MSUIStyleColor::TextSecondary);
			Caption->SetVisibility(ESlateVisibility::Collapsed);
			AddDesign(Caption,{837.0f,BaseY+5.0f},{246.0f,13.0f});

			UTextBlock* Body=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Body,true,FLinearColor::White);
			FSlateFontInfo BodyFont=Body->GetFont();BodyFont.Size=10;Body->SetFont(BodyFont);
			Body->SetAutoWrapText(true);Body->SetWrapTextAt(246.0f);Body->SetClipping(EWidgetClipping::ClipToBounds);
			Body->SetJustification(ETextJustify::Left);
			UScaleBox* BodyFit=WidgetTree->ConstructWidget<UScaleBox>();BodyFit->SetStretch(EStretch::ScaleToFit);BodyFit->SetStretchDirection(EStretchDirection::DownOnly);BodyFit->SetContent(Body);
			AddDesign(ScaleVideoContent(WidgetTree,BodyFit,{246.0f,34.0f}),{837.0f,BaseY+18.0f},{246.0f,34.0f});

			Panel->SetVisibility(ESlateVisibility::Collapsed);
			VoiceCutInPanels.Add(Panel);VoiceCutInPortraitFills.Add(PortraitFill);VoiceCutInPortraitImages.Add(PortraitImage);VoiceCutInLetters.Add(Letter);
			VoiceCutInNames.Add(SpeakerName);VoiceCutInCaptions.Add(Caption);VoiceCutInBodies.Add(Body);
		}
		return;
	}

	for(int32 Index=0;Index<2;++Index)
	{
		const bool bRight=Index==1;
		UCanvasPanel* Panel=WidgetTree->ConstructWidget<UCanvasPanel>();
		UCanvasPanelSlot* PanelSlot=RootCanvas->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(bRight?1.0f:0.0f,bRight?0.22f:0.28f));
		PanelSlot->SetAlignment(FVector2D(bRight?1.0f:0.0f,0.0f));
		PanelSlot->SetPosition(FVector2D::ZeroVector);PanelSlot->SetSize(FVector2D(420.0f,458.0f));PanelSlot->SetZOrder(100);

		UOverlay* PortraitContent=WidgetTree->ConstructWidget<UOverlay>();
		UBorder* PortraitFill=WidgetTree->ConstructWidget<UBorder>();
		PortraitFill->SetBrushColor(FLinearColor::White);
		PortraitContent->AddChildToOverlay(PortraitFill);
		UImage* PortraitImage=WidgetTree->ConstructWidget<UImage>();
		UOverlaySlot* ImageSlot=PortraitContent->AddChildToOverlay(PortraitImage);ImageSlot->SetHorizontalAlignment(HAlign_Fill);ImageSlot->SetVerticalAlignment(VAlign_Fill);
		PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		UTextBlock* Letter=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Display,true,FLinearColor::White);
		FSlateFontInfo LetterFont=Letter->GetFont();LetterFont.Size=112;Letter->SetFont(LetterFont);Letter->SetJustification(ETextJustify::Center);
		UOverlaySlot* LetterSlot=PortraitContent->AddChildToOverlay(Letter);LetterSlot->SetHorizontalAlignment(HAlign_Fill);LetterSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* PortraitInner=nullptr;
		UBorder* PortraitFrame=MSUIStyle::MakeFramedPanel(WidgetTree,PortraitInner,PortraitContent,true,
			MSUIStyleColor::FrameAccent,MSUIStyleColor::PanelBackground,FMargin(MSUIStyleSize::BorderEmphasized));
		UCanvasPanelSlot* PortraitSlot=Panel->AddChildToCanvas(PortraitFrame);
		PortraitSlot->SetPosition(FVector2D(bRight?120.0f:0.0f,0.0f));PortraitSlot->SetSize(FVector2D(300.0f,340.0f));

		UBorder* NamePlate=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(NamePlate,MSUIStyle::EBrush::ListRow,FLinearColor(0.01f,0.015f,0.02f,0.97f));NamePlate->SetPadding(FMargin(12.0f,5.0f));
		UVerticalBox* NameContent=WidgetTree->ConstructWidget<UVerticalBox>();NamePlate->SetContent(NameContent);
		UTextBlock* SpeakerName=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::H2,true,MSUIStyleColor::TextPrimary);
		SpeakerName->SetJustification(bRight?ETextJustify::Right:ETextJustify::Left);NameContent->AddChildToVerticalBox(SpeakerName);
		UTextBlock* Caption=MSUIStyle::MakeText(WidgetTree,
			FText::FromString(bRight?TEXT("ENEMY VOICE // HOSTILE"):TEXT("PRESET VOICE // ALLY")),
			MSUIStyle::ETextRole::Caption,true,MSUIStyleColor::TextSecondary);
		Caption->SetJustification(bRight?ETextJustify::Right:ETextJustify::Left);NameContent->AddChildToVerticalBox(Caption);
		UCanvasPanelSlot* NameSlot=Panel->AddChildToCanvas(NamePlate);NameSlot->SetPosition(FVector2D(bRight?120.0f:0.0f,340.0f));NameSlot->SetSize(FVector2D(300.0f,54.0f));

		const FLinearColor DialogueBackground=bRight?FLinearColor(0.015f,0.025f,0.045f,0.88f):FLinearColor(0.94f,0.97f,1.0f,0.78f);
		const FLinearColor DialogueText=bRight?MSUIStyleColor::TextPrimary:FLinearColor(0.025f,0.055f,0.09f,1.0f);
		UTextBlock* Body=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Body,true,DialogueText);
		Body->SetAutoWrapText(true);Body->SetWrapTextAt(392.0f);Body->SetJustification(bRight?ETextJustify::Right:ETextJustify::Left);
		UBorder* Dialogue=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(Dialogue,MSUIStyle::EBrush::LobbyPanel,DialogueBackground);Dialogue->SetPadding(FMargin(14.0f,8.0f));Dialogue->SetContent(Body);Dialogue->SetClipping(EWidgetClipping::ClipToBounds);
		UCanvasPanelSlot* DialogueSlot=Panel->AddChildToCanvas(Dialogue);DialogueSlot->SetPosition(FVector2D(0.0f,394.0f));DialogueSlot->SetSize(FVector2D(420.0f,64.0f));

		Panel->SetVisibility(ESlateVisibility::Collapsed);
		VoiceCutInPanels.Add(Panel);VoiceCutInPortraitFills.Add(PortraitFill);VoiceCutInPortraitImages.Add(PortraitImage);VoiceCutInLetters.Add(Letter);
		VoiceCutInNames.Add(SpeakerName);VoiceCutInCaptions.Add(Caption);VoiceCutInBodies.Add(Body);
	}
}

void UMSHUDWidget::RefreshVoiceCutIns()
{
	if(!ViewModel||VoiceCutInPanels.Num()!=2)return;
	const FMSHUDCutInsSnapshot CutIns=ViewModel->GetCutIns();
	const FMSHUDCutInSnapshot Snapshots[2]={CutIns.Left,CutIns.Right};
	const FMSHUDEmergencyRecallSnapshot Recall=ViewModel->GetEmergencyRecall();
	const bool bCountdownVisible=ViewModel->GetRespawn().bVisible || (Recall.bVisible && Recall.bActive);
	for(int32 Index=0;Index<2;++Index)
	{
		// Death and recall callouts must leave the shared RESPAWN area readable.
		// The offset is a NeoMSGO readability aid, not a measured original position.
		// Anchor the offset in design pixels so it also scales correctly at 720p.
		if(bVideoHudArtPass)
			SetDesignSlot(CastChecked<UCanvasPanelSlot>(VoiceCutInPanels[Index]->Slot),
				bCountdownVisible?FVector2D(-650.0f,240.0f):FVector2D::ZeroVector,VideoDesignSize);
		const FMSHUDCutInSnapshot& Snapshot=Snapshots[Index];
		VoiceCutInPanels[Index]->SetVisibility(Snapshot.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		if(!Snapshot.bVisible)continue;
		float Opacity=1.0f;
		if(Snapshot.Elapsed<UMSHUDViewModel::CutInFadeInSeconds)
		{
			Opacity=Snapshot.Elapsed/UMSHUDViewModel::CutInFadeInSeconds;
		}
		else if(Snapshot.Elapsed>UMSHUDViewModel::CutInHoldSeconds)
		{
			Opacity=1.0f-FMath::Clamp((Snapshot.Elapsed-UMSHUDViewModel::CutInHoldSeconds)/UMSHUDViewModel::CutInFadeOutSeconds,0.0f,1.0f);
		}
		VoiceCutInPanels[Index]->SetRenderOpacity(Opacity);
		VoiceCutInPortraitFills[Index]->SetBrushColor(Snapshot.PortraitColor);
		// 立ち絵が解決できる話者は画像を出し、漢字プレースホルダーは画像が無い場合のみ残す。
		UTexture2D* Portrait=MSAvatarLibrary::ResolveAvatarTexture(Snapshot.SpeakerName.ToString());
		if(Portrait){VoiceCutInPortraitImages[Index]->SetBrushFromTexture(Portrait,false);}
		VoiceCutInPortraitImages[Index]->SetVisibility(Portrait?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		VoiceCutInLetters[Index]->SetVisibility(Portrait?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
		VoiceCutInLetters[Index]->SetText(Snapshot.VoiceTypeLetter);
		VoiceCutInNames[Index]->SetText(Snapshot.SpeakerName);
		VoiceCutInBodies[Index]->SetText(Snapshot.Line);
	}
}

void UMSHUDWidget::BuildModuleChargeGauge()
{
	UCanvasPanel* Inner = WidgetTree->ConstructWidget<UCanvasPanel>();
	UTextBlock* Label = MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("TRANSZOMM  CHARGE")), MSUIStyle::ETextRole::Caption,
		true, MSUIStyleColor::TextPrimary);
	UCanvasPanelSlot* LabelSlot = Inner->AddChildToCanvas(Label);
	LabelSlot->SetPosition(FVector2D::ZeroVector);
	LabelSlot->SetSize(FVector2D(250.0f, 18.0f));
	Label->SetJustification(ETextJustify::Center);

	ModuleChargeBar = WidgetTree->ConstructWidget<UProgressBar>();
	ModuleChargeBar->SetFillColorAndOpacity(MSUIStyleColor::FrameAccent);
	ModuleChargeBar->SetPercent(0.0f);
	UCanvasPanelSlot* BarSlot = Inner->AddChildToCanvas(ModuleChargeBar);
	BarSlot->SetPosition(FVector2D(0.0f, 22.0f));
	BarSlot->SetSize(FVector2D(250.0f, 10.0f));

	UBorder* InnerBorder = nullptr;
	ModuleChargePanel = MSUIStyle::MakeFramedPanel(WidgetTree, InnerBorder, Inner, true,
		MSUIStyleColor::FrameAccent, MSUIStyleColor::PanelBackground, FMargin(MSUIStyleSize::Space2));
	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(ModuleChargePanel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.58f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(270.0f, 54.0f));
	ModuleChargePanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHUDWidget::BuildEmergencyRecallIndicator()
{
	// MSGO9 134-141s: living full-HP recall uses the same top RESPAWN frame
	// as death, without the death tactical map. The original has no manual-cancel hint.
	EmergencyRecallActivePanel = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* ActiveRoot = RootCanvas->AddChildToCanvas(EmergencyRecallActivePanel);
	ActiveRoot->SetAnchors(FAnchors(0,0,1,1)); ActiveRoot->SetOffsets(FMargin(0)); ActiveRoot->SetZOrder(80);
	UMSVectorSkinWidget* Frame = WidgetTree->ConstructWidget<UMSVectorSkinWidget>();
	Frame->SetSurface(EMSVectorSurface::RespawnPanel);
	SetDesignSlot(EmergencyRecallActivePanel->AddChildToCanvas(Frame), {834,85}, {252,100});
	auto AddRecallText = [this](const FText& Text, float FontSize, const FVector2D& Position,
		const FVector2D& Size, FLinearColor Color)
	{
		UTextBlock* Label = MSUIStyle::MakeText(WidgetTree, Text, MSUIStyle::ETextRole::Body, true, Color);
		FSlateFontInfo Font = Label->GetFont(); Font.Size = FMath::RoundToInt(FontSize * .75f);
		Font.TypefaceFontName = TEXT("Bold"); Label->SetFont(Font);
		Label->SetJustification(ETextJustify::Center); Label->SetClipping(EWidgetClipping::ClipToBounds);
		SetDesignSlot(EmergencyRecallActivePanel->AddChildToCanvas(Label), Position, Size);
		return Label;
	};
	AddRecallText(FText::FromString(TEXT("R E S P A W N")),14,{843,89},{234,20},MSPixel::Hex(0xA1F9FB));
	AddRecallText(FText::FromString(TEXT("再出撃まで")),13,{846,111},{70,18},MSPixel::Hex(0x84D7E6));
	EmergencyRecallCountdownText = AddRecallText(FText::GetEmpty(),37,{882,125},{155,48},MSPixel::Hex(0xCCFEFC));
	// The recorded timer uses wide, slanted digits, unlike ordinary HUD labels.
	FSlateFontInfo RecallDigits = FCoreStyle::GetDefaultFontStyle(TEXT("BoldItalic"),28);
	RecallDigits.LetterSpacing = 180;
	EmergencyRecallCountdownText->SetFont(RecallDigits);
	EmergencyRecallCountdownText->SetShadowOffset(FVector2D(1,1));
	EmergencyRecallCountdownText->SetShadowColorAndOpacity(MSPixel::Hex(0x3297C0,.8f));
	AddRecallText(FText::FromString(TEXT("秒")),13,{1043,146},{17,18},MSPixel::Hex(0x84D7E6));
	EmergencyRecallActivePanel->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	EmergencyRecallText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(),
		MSUIStyle::ETextRole::Body, true, MSUIStyleColor::SelectedAccent);
	EmergencyRecallText->SetJustification(ETextJustify::Center);
	EmergencyRecallText->SetAutoWrapText(false);
	Content->AddChildToVerticalBox(EmergencyRecallText)->SetHorizontalAlignment(HAlign_Fill);
	EmergencyRecallDetailText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(),
		MSUIStyle::ETextRole::Caption, false, MSUIStyleColor::TextPrimary);
	EmergencyRecallDetailText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(EmergencyRecallDetailText)->SetHorizontalAlignment(HAlign_Fill);

	UBorder* InnerBorder = nullptr;
	EmergencyRecallPanel = MSUIStyle::MakeFramedPanel(WidgetTree, InnerBorder,
		Content, true, MSUIStyleColor::SelectedAccent,
		MSUIStyleColor::PanelBackground, FMargin(MSUIStyleSize::Space3, MSUIStyleSize::Space2));
	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(EmergencyRecallPanel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.68f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(FVector2D(420.0f, 60.0f));
	EmergencyRecallPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHUDWidget::RefreshEmergencyRecallIndicator()
{
	if (!ViewModel || !EmergencyRecallPanel) return;
	const FMSHUDEmergencyRecallSnapshot Recall = ViewModel->GetEmergencyRecall();
	EmergencyRecallActivePanel->SetVisibility(Recall.bVisible && Recall.bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	EmergencyRecallPanel->SetVisibility(Recall.bVisible && !Recall.bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!Recall.bVisible) return;
	const int32 Hundredths = FMath::Max(0, FMath::CeilToInt(Recall.RemainingSeconds * 100.0f));
	EmergencyRecallCountdownText->SetText(FText::FromString(FString::Printf(TEXT("%d’%02d"), Hundredths / 100, Hundredths % 100)));
	EmergencyRecallText->SetText(Recall.StatusText);
	EmergencyRecallDetailText->SetText(Recall.DetailText);
}

void UMSHUDWidget::BuildRespawnPanel()
{
	if (!bVideoHudArtPass) return;

	// MSGO8 frame 17281: the respawn countdown occupies a compact upper-center
	// panel while the battlefield and the rest of the HUD remain visible behind it.
	RespawnPanel = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanelSlot* RootSlot = RootCanvas->AddChildToCanvas(RespawnPanel);
	RootSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	RootSlot->SetOffsets(FMargin(0));
	RootSlot->SetZOrder(80);

	// The recorded death state keeps the battlefield HUD alive while a tactical
	// map and the four deck slots fade in behind the compact timer.  This is a
	// presentation layer only: the existing SpawnSelect/DeploymentMap widgets
	// remain responsible for focus, selection and the server deployment gate.
	{
		UMSVectorSkinWidget* MapFrame=WidgetTree->ConstructWidget<UMSVectorSkinWidget>();
		MapFrame->SetSurface(EMSVectorSurface::Panel);
		MapFrame->SetAccent(MSPixel::Hex(0x52E8D5,.92f));
		MapFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* MapFrameSlot=RespawnPanel->AddChildToCanvas(MapFrame);
		SetDesignSlot(MapFrameSlot,{450,283},{1032,525});MapFrameSlot->SetZOrder(0);

		RespawnTacticalMapImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("RespawnTacticalMapImage"));
		FSlateBrush MapBrush;MapBrush.DrawAs=ESlateBrushDrawType::Image;MapBrush.TintColor=FLinearColor(1,1,1,.82f);
		RespawnTacticalMapImage->SetBrush(MapBrush);RespawnTacticalMapImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* MapImageSlot=RespawnPanel->AddChildToCanvas(RespawnTacticalMapImage);
		SetDesignSlot(MapImageSlot,{RespawnMapTextureX,RespawnMapTextureY},{RespawnMapTextureSize,RespawnMapTextureSize});MapImageSlot->SetZOrder(1);
		RespawnRadarRangeLayer=WidgetTree->ConstructWidget<UMSMapRadarRangeWidget>();
		UCanvasPanelSlot* RespawnRadarSlot=RespawnPanel->AddChildToCanvas(RespawnRadarRangeLayer);
		SetDesignSlot(RespawnRadarSlot,{RespawnMapTextureX,RespawnMapTextureY},
			{RespawnMapTextureSize,RespawnMapTextureSize});RespawnRadarSlot->SetZOrder(2);
		RespawnRadarRangeLayer->SetVisibility(ESlateVisibility::Collapsed);

		// A restrained grid keeps the map readable at 720p while the captured
		// terrain and live point markers remain the source of world information.
		FSlateBrush GridBrush;GridBrush.DrawAs=ESlateBrushDrawType::Box;GridBrush.TintColor=MSPixel::Hex(0xA5E3D8,.28f);
		for(int32 Column=1;Column<14;++Column)
		{
			UImage* Line=WidgetTree->ConstructWidget<UImage>();Line->SetBrush(GridBrush);Line->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* LineSlot=RespawnPanel->AddChildToCanvas(Line);
			SetDesignSlot(LineSlot,FVector2D::ZeroVector,FVector2D::ZeroVector);LineSlot->SetZOrder(2);
			RespawnGridVerticalLines.Add(Line);
		}
		for(int32 Row=1;Row<9;++Row)
		{
			UImage* Line=WidgetTree->ConstructWidget<UImage>();Line->SetBrush(GridBrush);Line->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* LineSlot=RespawnPanel->AddChildToCanvas(Line);
			SetDesignSlot(LineSlot,FVector2D::ZeroVector,FVector2D::ZeroVector);LineSlot->SetZOrder(2);
			RespawnGridHorizontalLines.Add(Line);
		}
		auto AddMapLabel=[this](const FString& Value,const FVector2D& Position,const FVector2D& Size,int32 FontSize)
		{
			UTextBlock* Text=MSUIStyle::MakeText(WidgetTree,FText::FromString(Value),MSUIStyle::ETextRole::Caption,true,MSPixel::Hex(0xBDF7EA,.9f));
			FSlateFontInfo Font=Text->GetFont();Font.Size=FMath::RoundToInt(FontSize*.75f);Font.TypefaceFontName=TEXT("Bold");Text->SetFont(Font);
			Text->SetJustification(ETextJustify::Center);Text->SetClipping(EWidgetClipping::ClipToBounds);
			// Keep respawn text as a direct canvas child. Nested ScaleBoxes were
			// centered by Slate inside their anchor slot, moving labels away from
			// the square overview on the real 1920px canvas.
			UCanvasPanelSlot* Slot=RespawnPanel->AddChildToCanvas(Text);SetDesignSlot(Slot,Position,Size);Slot->SetZOrder(3);
			return Text;
		};
		AddMapLabel(TEXT("TACTICAL MAP"),{470,293},{150,18},10);
		RespawnTacticalMissionText=MSUIStyle::MakeText(WidgetTree,FText::GetEmpty(),MSUIStyle::ETextRole::Caption,true,MSPixel::Hex(0xD9F8F0,.95f));
		FSlateFontInfo MissionFont=RespawnTacticalMissionText->GetFont();MissionFont.Size=9;MissionFont.TypefaceFontName=TEXT("Bold");RespawnTacticalMissionText->SetFont(MissionFont);
		RespawnTacticalMissionText->SetJustification(ETextJustify::Center);RespawnTacticalMissionText->SetClipping(EWidgetClipping::ClipToBounds);
		UCanvasPanelSlot* MissionSlot=RespawnPanel->AddChildToCanvas(RespawnTacticalMissionText);
		SetDesignSlot(MissionSlot,{742,286},{440,20});MissionSlot->SetZOrder(3);
		for(int32 Column=0;Column<14;++Column)
		{
			UTextBlock* Label=AddMapLabel(FString::Printf(TEXT("%d"),Column+1),FVector2D::ZeroVector,{33.4f,16},8);
			Label->SetVisibility(ESlateVisibility::Collapsed);
			RespawnGridColumnLabels.Add(Label);
		}
		for(int32 Row=0;Row<9;++Row)
		{
			UTextBlock* Label=AddMapLabel(FString::Printf(TEXT("%c"),TCHAR('A'+Row)),FVector2D::ZeroVector,{18,24},8);
			Label->SetVisibility(ESlateVisibility::Collapsed);
			RespawnGridRowLabels.Add(Label);
		}
		FSlateBrush PlayerBrush;PlayerBrush.DrawAs=ESlateBrushDrawType::Box;PlayerBrush.TintColor=MSHUDPalette::MinimapPlayer;
		RespawnTacticalPlayerMarker=WidgetTree->ConstructWidget<UImage>();RespawnTacticalPlayerMarker->SetBrush(PlayerBrush);RespawnTacticalPlayerMarker->SetRenderTransformAngle(45.0f);RespawnTacticalPlayerMarker->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* PlayerMarkerSlot=RespawnPanel->AddChildToCanvas(RespawnTacticalPlayerMarker);SetDesignSlot(PlayerMarkerSlot,{RespawnMapTextureX+RespawnMapTextureSize*.5f-7.0f,RespawnMapTextureY+RespawnMapTextureSize*.5f-7.0f},{14,14});PlayerMarkerSlot->SetZOrder(5);
		FSlateBrush UnitBrush;UnitBrush.DrawAs=ESlateBrushDrawType::Box;UnitBrush.TintColor=FLinearColor::White;
		for(int32 Index=0;Index<MSHUDPalette::MaxMinimapEnemyMarkers;++Index)
		{
			UImage* Marker=WidgetTree->ConstructWidget<UImage>();Marker->SetBrush(UnitBrush);Marker->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* MarkerSlot=RespawnPanel->AddChildToCanvas(Marker);SetDesignSlot(MarkerSlot,{RespawnMapTextureX+RespawnMapTextureSize*.5f-4.5f,RespawnMapTextureY+RespawnMapTextureSize*.5f-4.5f},{9,9});MarkerSlot->SetZOrder(5);RespawnTacticalUnitMarkers.Add(Marker);
		}

		UBorder* ListFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("RespawnMSListFrame"));
		FSlateBrush ListBrush;ListBrush.DrawAs=ESlateBrushDrawType::Box;ListBrush.TintColor=MSPixel::Hex(0x071C24,.93f);ListFrame->SetBrush(ListBrush);ListFrame->SetPadding(FMargin(0));
		// Keep the list's nested card coordinates in a fixed 128x468 design box;
		// the parent ListFrame supplies the viewport/DPI scale at 720p.
		UCanvasPanelSlot* ListSlot=RespawnPanel->AddChildToCanvas(ListFrame);SetDesignSlot(ListSlot,{1334,309},{128,468});ListSlot->SetZOrder(10);
		UTextBlock* ListTitle=MSUIStyle::MakeText(WidgetTree,FText::FromString(TEXT("MS LIST")),MSUIStyle::ETextRole::Caption,true,MSPixel::Hex(0x52E8D5,.95f));
		FSlateFontInfo ListTitleFont=ListTitle->GetFont();ListTitleFont.Size=9;ListTitleFont.TypefaceFontName=TEXT("Bold");ListTitle->SetFont(ListTitleFont);ListTitle->SetJustification(ETextJustify::Center);
		UCanvasPanel* ListCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();
		USizeBox* ListCanvasSize=WidgetTree->ConstructWidget<USizeBox>();
		ListCanvasSize->SetWidthOverride(128.0f);ListCanvasSize->SetHeightOverride(468.0f);ListCanvasSize->SetContent(ListCanvas);ListFrame->SetContent(ListCanvasSize);
		UCanvasPanelSlot* ListTitleSlot=ListCanvas->AddChildToCanvas(ListTitle);SetRelativeSlot(ListTitleSlot,{6,6},{116,20},{128,468});
		RespawnMSListFrames.Reserve(4);RespawnMSListNames.Reserve(4);RespawnMSListTimers.Reserve(4);
		for(int32 Index=0;Index<4;++Index)
		{
			UBorder* Card=WidgetTree->ConstructWidget<UBorder>();FSlateBrush CardBrush;CardBrush.DrawAs=ESlateBrushDrawType::Box;CardBrush.TintColor=MSPixel::Hex(0x153A43,.88f);Card->SetBrush(CardBrush);Card->SetPadding(FMargin(0));
			UCanvasPanelSlot* CardSlot=ListCanvas->AddChildToCanvas(Card);SetRelativeSlot(CardSlot,{7,34+Index*101.f},{114,88},{128,468});
			UCanvasPanel* CardCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();
			USizeBox* CardCanvasSize=WidgetTree->ConstructWidget<USizeBox>();
			CardCanvasSize->SetWidthOverride(114.0f);CardCanvasSize->SetHeightOverride(88.0f);CardCanvasSize->SetContent(CardCanvas);Card->SetContent(CardCanvasSize);
			UTextBlock* Number=MSUIStyle::MakeText(WidgetTree,FText::FromString(FString::Printf(TEXT("0%d"),Index+1)),MSUIStyle::ETextRole::Micro,true,MSPixel::Hex(0x52E8D5,.85f));
			FSlateFontInfo NumberFont=Number->GetFont();NumberFont.Size=8;NumberFont.TypefaceFontName=TEXT("Bold");Number->SetFont(NumberFont);UCanvasPanelSlot* NumberSlot=CardCanvas->AddChildToCanvas(Number);SetRelativeSlot(NumberSlot,{6,4},{24,14},{114,88});
			UTextBlock* Name=MSUIStyle::MakeText(WidgetTree,FText::FromString(TEXT("EMPTY")),MSUIStyle::ETextRole::Caption,true,FLinearColor::White);
			FSlateFontInfo NameFont=Name->GetFont();NameFont.Size=8;NameFont.TypefaceFontName=TEXT("Bold");Name->SetFont(NameFont);
			Name->SetAutoWrapText(true);Name->SetWrapTextAt(102.0f);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetClipping(EWidgetClipping::ClipToBounds);
			// Allow the Japanese fallback font's second line its full painted height.
			UCanvasPanelSlot* NameSlot=CardCanvas->AddChildToCanvas(Name);SetRelativeSlot(NameSlot,{6,18},{102,34},{114,88});
			UTextBlock* Timer=MSUIStyle::MakeText(WidgetTree,FText::FromString(TEXT("--")),MSUIStyle::ETextRole::Micro,true,MSPixel::Hex(0xBDF7EA,.9f));
			FSlateFontInfo TimerFont=Timer->GetFont();TimerFont.Size=8;TimerFont.TypefaceFontName=TEXT("Bold");Timer->SetFont(TimerFont);Timer->SetAutoWrapText(false);Timer->SetClipping(EWidgetClipping::ClipToBounds);Timer->SetJustification(ETextJustify::Center);
			UCanvasPanelSlot* TimerSlot=CardCanvas->AddChildToCanvas(Timer);SetRelativeSlot(TimerSlot,{7,54},{100,32},{114,88});
			RespawnMSListFrames.Add(Card);RespawnMSListNames.Add(Name);RespawnMSListTimers.Add(Timer);
		}
	}

	UMSVectorSkinWidget* Frame = WidgetTree->ConstructWidget<UMSVectorSkinWidget>();
	Frame->SetSurface(EMSVectorSurface::RespawnPanel);
	Frame->SetAccent(MSPixel::Hex(0x52E8D5, .92f));
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* RespawnFrameSlot=RespawnPanel->AddChildToCanvas(Frame);
	SetDesignSlot(RespawnFrameSlot, {835, 87}, {250, 95}); RespawnFrameSlot->SetZOrder(20);

	auto AddText = [this](const FText& Initial, float FontSize, const FVector2D& Size, const FLinearColor& Color)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, Initial, MSUIStyle::ETextRole::Body, true, Color);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FMath::RoundToInt(FontSize * .75f);
		Font.TypefaceFontName = TEXT("Bold");
		Text->SetFont(Font);
		Text->SetJustification(ETextJustify::Center);
		Text->SetClipping(EWidgetClipping::ClipToBounds);
		return TPair<UTextBlock*, UWidget*>(Text, Text);
	};

	const auto Title = AddText(FText::FromString(TEXT("R E S P A W N")), 14, {234, 20}, MSPixel::Hex(0xA1F9FB));
	RespawnTitleText = Title.Key;
	UCanvasPanelSlot* TitleSlot=RespawnPanel->AddChildToCanvas(Title.Key);
	SetDesignSlot(TitleSlot, {843, 89}, {234, 20}); TitleSlot->SetZOrder(21);
	const auto Caption = AddText(FText::FromString(TEXT("再出撃まで")),13,{70,18},MSPixel::Hex(0x84D7E6));
	UCanvasPanelSlot* CaptionSlot=RespawnPanel->AddChildToCanvas(Caption.Key);
	SetDesignSlot(CaptionSlot,{846,111},{70,18}); CaptionSlot->SetZOrder(21);
	const auto Countdown = AddText(FText::GetEmpty(), 37, {155, 48}, MSPixel::Hex(0xCCFEFC));
	RespawnCountdownText = Countdown.Key;
	UCanvasPanelSlot* CountdownSlot=RespawnPanel->AddChildToCanvas(Countdown.Key);
	SetDesignSlot(CountdownSlot, {905, 125}, {155, 48}); CountdownSlot->SetZOrder(21);
	const auto Seconds = AddText(FText::FromString(TEXT("秒")),13,{17,18},MSPixel::Hex(0x84D7E6));
	UCanvasPanelSlot* SecondsSlot=RespawnPanel->AddChildToCanvas(Seconds.Key);
	SetDesignSlot(SecondsSlot,{1043,146},{17,18}); SecondsSlot->SetZOrder(21);
	UMSVectorSkinWidget* HintFrame=WidgetTree->ConstructWidget<UMSVectorSkinWidget>(); HintFrame->SetSurface(EMSVectorSurface::Panel);
	UCanvasPanelSlot* HintFrameSlot=RespawnPanel->AddChildToCanvas(HintFrame);
	SetDesignSlot(HintFrameSlot,{835,195},{250,23}); HintFrameSlot->SetZOrder(20);
	const auto Hint = AddText(FText::FromString(TEXT("[F] キーを押すと強制脱出できる")), 13, {238, 20}, MSPixel::Hex(0xDCF6F4));
	RespawnHintText = Hint.Key;
	UCanvasPanelSlot* HintSlot=RespawnPanel->AddChildToCanvas(Hint.Key);
	SetDesignSlot(HintSlot, {841, 196}, {238, 20}); HintSlot->SetZOrder(21);

	RespawnPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHUDWidget::RebuildRespawnCaptureMarkers()
{
	if (!RespawnPanel) return;
	bool bSameOrder = RespawnCapturePointOrder.Num() == MinimapCapturePoints.Num();
	for (int32 Index = 0; bSameOrder && Index < MinimapCapturePoints.Num(); ++Index)
	{
		bSameOrder = RespawnCapturePointOrder[Index].Get() == MinimapCapturePoints[Index];
	}
	if (bSameOrder) return;

	for (UWidget* Marker : RespawnTacticalCaptureMarkers)
		if (Marker) Marker->RemoveFromParent();
	RespawnTacticalCaptureMarkers.Reset();
	RespawnTacticalCaptureMarkerShapes.Reset();
	RespawnCapturePointOrder.Reset();
	RespawnCapturePointOrder.Reserve(MinimapCapturePoints.Num());
	RespawnTacticalCaptureMarkers.Reserve(MinimapCapturePoints.Num());
	RespawnTacticalCaptureMarkerShapes.Reserve(MinimapCapturePoints.Num());

	const FVector2D MapCenter(RespawnMapTextureX + RespawnMapTextureSize * 0.5f,
		RespawnMapTextureY + RespawnMapTextureSize * 0.5f);
	for (int32 Index = 0; Index < MinimapCapturePoints.Num(); ++Index)
	{
		AMSCapturePoint* Point = MinimapCapturePoints[Index];
		RespawnCapturePointOrder.Add(Point);
		if (!IsValid(Point)) continue;

		const bool bHomeBase = Point->IsA<AMSHomeBase>();
		const float MarkerSize = (bHomeBase ? MSMapPalette::HomeBaseMarkerSizePx : MSMapPalette::CapturePointMarkerSizePx) * .72f;
		const FText Number = UMSMapWidgetBase::ResolveCapturePointNumber(Point, Index);
		const FMSMapCapturePointInteractionBounds Bounds = UMSMapWidgetBase::GetCapturePointInteractionBounds(
			MapCenter, MarkerSize, Number, bHomeBase);
		const FVector2D InteractionSize = Bounds.HalfExtents * 2.0f;
		const FMSMapCapturePointMarker Marker = UMSMapWidgetBase::ConstructCapturePointMarker(
			WidgetTree, RespawnPanel, MapCenter, MarkerSize, Number, bHomeBase);
		if (!Marker.Root) continue;

		// The marker compound is authored in map pixels.  Fit the complete root,
		// including its number/letter label, at the 720p design scale before the
		// root is positioned on the HUD canvas.
		Marker.Root->RemoveFromParent();
		UWidget* Fit = ScaleVideoContent(WidgetTree, Marker.Root, InteractionSize);
		UCanvasPanelSlot* MarkerSlot = RespawnPanel->AddChildToCanvas(Fit);
		SetDesignSlot(MarkerSlot, MapCenter - InteractionSize * .5f, InteractionSize);
		MarkerSlot->SetZOrder(4);
		Fit->SetVisibility(ESlateVisibility::Collapsed);
		RespawnTacticalCaptureMarkers.Add(Fit);
		RespawnTacticalCaptureMarkerShapes.Add(Marker.Shape);
	}
}

void UMSHUDWidget::RefreshRespawnPanel()
{
	if (!RespawnPanel || !ViewModel) return;
	const FMSHUDRespawnSnapshot Snapshot = ViewModel->GetRespawn();
	RespawnPanel->SetVisibility(Snapshot.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (RespawnTacticalMissionText)
	{
		RespawnTacticalMissionText->SetText(ViewModel->GetTask().TaskText);
	}
	if (!Snapshot.bVisible)
	{
		if (RespawnTacticalMapImage) RespawnTacticalMapImage->SetVisibility(ESlateVisibility::Collapsed);
		for (UWidget* Marker : RespawnTacticalCaptureMarkers) if (Marker) Marker->SetVisibility(ESlateVisibility::Collapsed);
		for (UImage* Marker : RespawnTacticalUnitMarkers) if (Marker) Marker->SetVisibility(ESlateVisibility::Collapsed);
		if (RespawnTacticalPlayerMarker) RespawnTacticalPlayerMarker->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// The small corner map follows the player.  Death presentation instead uses
	// the world-wide overview and its own center/extent, so distant points stay
	// on the same coordinates as the captured terrain.
	RefreshMinimapCapturePoints();
	RebuildRespawnCaptureMarkers();
	UWorld* World = GetWorld();
	UMSMapCaptureSubsystem* MapSubsystem = World ? World->GetSubsystem<UMSMapCaptureSubsystem>() : nullptr;
	UTextureRenderTarget2D* OverviewTexture = MapSubsystem ? MapSubsystem->GetOverviewTexture() : nullptr;
	AMSPlayerCharacter* Player=Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());
	const FGenericTeamId ViewerTeam=Player?UMSTeamStatics::GetTeamId(Player):FGenericTeamId::NoTeam;
	const FVector MapWorldCenter = MapSubsystem ? MapSubsystem->GetMapWorldCenter() : FVector::ZeroVector;
	const float MapWorldHalfExtent = MapSubsystem ? MapSubsystem->GetMapWorldHalfExtent() : 0.0f;
	const FVector2D MapWorldHalfExtents = MapSubsystem ? MapSubsystem->GetMapWorldHalfExtents() : FVector2D::ZeroVector;
	const FBox2d ContentRect=GetRespawnMapContentRect(MapWorldHalfExtents);
	const FVector2D ContentMin=ContentRect.Min;
	const FVector2D ContentSize=ContentRect.GetSize();
	const bool bMapReady=OverviewTexture&&ContentSize.X>0.0f&&ContentSize.Y>0.0f;
	if (RespawnTacticalMapImage)
	{
		if (OverviewTexture) RespawnTacticalMapImage->SetBrushResourceObject(OverviewTexture);
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(RespawnTacticalMapImage->Slot),ContentMin,ContentSize);
		RespawnTacticalMapImage->SetVisibility(bMapReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	for(int32 Column=0;Column<RespawnGridVerticalLines.Num();++Column)
	{
		UImage* Line=RespawnGridVerticalLines[Column];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Line->Slot),
			{ContentMin.X+ContentSize.X*(Column+1)/14.0f,ContentMin.Y},{.8f,ContentSize.Y});
		Line->SetVisibility(bMapReady?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	for(int32 Row=0;Row<RespawnGridHorizontalLines.Num();++Row)
	{
		UImage* Line=RespawnGridHorizontalLines[Row];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Line->Slot),
			{ContentMin.X,ContentMin.Y+ContentSize.Y*(Row+1)/9.0f},{ContentSize.X,.8f});
		Line->SetVisibility(bMapReady?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	for(int32 Column=0;Column<RespawnGridColumnLabels.Num();++Column)
	{
		UTextBlock* Label=RespawnGridColumnLabels[Column];
		const float CellWidth=ContentSize.X/14.0f;
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Label->Slot),
			{ContentMin.X+CellWidth*Column,ContentMin.Y-18.0f},{CellWidth,16.0f});
		Label->SetVisibility(bMapReady?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	for(int32 Row=0;Row<RespawnGridRowLabels.Num();++Row)
	{
		UTextBlock* Label=RespawnGridRowLabels[Row];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Label->Slot),
			{ContentMin.X-24.0f,ContentMin.Y+ContentSize.Y*(Row+.5f)/9.0f-12.0f},{18.0f,24.0f});
		Label->SetVisibility(bMapReady?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	if(RespawnRadarRangeLayer)
	{
		RespawnRadarRangeLayer->SetVisibility(Player&&OverviewTexture&&MapWorldHalfExtent>0.0f
			? ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		if(Player&&OverviewTexture&&MapWorldHalfExtent>0.0f)
		{
			static const TArray<FMSMapRadarRange> NoRadars;
			const AMSPlayerController* RadarController=Cast<AMSPlayerController>(GetOwningPlayer());
			const TArray<FMSMapRadarRange>& Radars=RadarController&&RadarController->AreMapContactsFresh()
				?RadarController->GetMapRadarRanges():NoRadars;
			RespawnRadarRangeLayer->SetOverviewView(MapWorldCenter,MapWorldHalfExtents,
				FVector2D(RespawnMapTextureSize),ViewerTeam,Radars);
		}
	}
	for(int32 Index=0;Index<RespawnTacticalCaptureMarkers.Num();++Index)
	{
		UWidget* Marker=RespawnTacticalCaptureMarkers[Index];
		AMSCapturePoint* Point=MinimapCapturePoints.IsValidIndex(Index)?MinimapCapturePoints[Index]:nullptr;
		if(!Marker||!IsValid(Point)||!OverviewTexture||MapWorldHalfExtent<=0.0f){if(Marker)Marker->SetVisibility(ESlateVisibility::Collapsed);continue;}
		const FVector2D Local=UMSMapWidgetBase::WorldToMapLocalRectCentered(Point->GetActorLocation(),MapWorldCenter,MapWorldHalfExtents,
			FVector2D(RespawnMapTextureSize),0.0f);
		const bool bHomeBase=Point->IsA<AMSHomeBase>();
		const float MarkerSize=(bHomeBase?MSMapPalette::HomeBaseMarkerSizePx:MSMapPalette::CapturePointMarkerSizePx)*.72f;
		const FMSMapCapturePointInteractionBounds Bounds=UMSMapWidgetBase::GetCapturePointInteractionBounds(Local,MarkerSize,
			UMSMapWidgetBase::ResolveCapturePointNumber(Point,Index),bHomeBase);
		const FVector2D InteractionSize=Bounds.HalfExtents*2.0f;
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Marker->Slot),{RespawnMapTextureX+Local.X-InteractionSize.X*.5f,RespawnMapTextureY+Local.Y-InteractionSize.Y*.5f},InteractionSize);
		if(UImage* Shape=RespawnTacticalCaptureMarkerShapes.IsValidIndex(Index)?RespawnTacticalCaptureMarkerShapes[Index]:nullptr)
			Shape->SetColorAndOpacity(UMSMapWidgetBase::ResolveCapturePointColor(Point,ViewerTeam));
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if(RespawnTacticalPlayerMarker)
	{
		if(Player&&OverviewTexture&&MapWorldHalfExtent>0.0f)
		{
			const FVector2D Local=UMSMapWidgetBase::WorldToMapLocalRectCentered(Player->GetActorLocation(),MapWorldCenter,MapWorldHalfExtents,
				FVector2D(RespawnMapTextureSize),0.0f);
			SetDesignSlot(CastChecked<UCanvasPanelSlot>(RespawnTacticalPlayerMarker->Slot),
				{RespawnMapTextureX+Local.X-7.0f,RespawnMapTextureY+Local.Y-7.0f},{14,14});
			RespawnTacticalPlayerMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else RespawnTacticalPlayerMarker->SetVisibility(ESlateVisibility::Collapsed);
	}
	int32 RespawnUnitIndex=0;
	const AMSPlayerController* ContactController=Cast<AMSPlayerController>(GetOwningPlayer());
	const bool bFreshContacts=ContactController&&ContactController->AreMapContactsFresh();
	if(Player&&OverviewTexture&&MapWorldHalfExtent>0.0f&&bFreshContacts)
	for(const FMSMapFriendlyContact& Friendly:ContactController->GetMapFriendlyContacts())
	{
		if(RespawnUnitIndex>=RespawnTacticalUnitMarkers.Num())break;
		if(Friendly.UnitId==0||Friendly.UnitId==Player->GetUniqueID()||Friendly.Location.ContainsNaN())continue;
		const FVector2D Local=UMSMapWidgetBase::WorldToMapLocalRectCentered(Friendly.Location,MapWorldCenter,MapWorldHalfExtents,
			FVector2D(RespawnMapTextureSize),0.0f);
		UImage* Marker=RespawnTacticalUnitMarkers[RespawnUnitIndex++];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Marker->Slot),{RespawnMapTextureX+Local.X-4.5f,RespawnMapTextureY+Local.Y-4.5f},{9,9});
		Marker->SetColorAndOpacity(MSMapPalette::FriendlyContactColor(Friendly.BotTier));
		Marker->SetRenderTransformAngle(45.0f);Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if(Player&&OverviewTexture&&MapWorldHalfExtent>0.0f)if(UMSUnitRegistrySubsystem* Registry=UnitRegistry.Get())Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		if(RespawnUnitIndex>=RespawnTacticalUnitMarkers.Num()||!IsValid(Unit)||Unit==Player)return;
		const UAbilitySystemComponent* ASC=Unit->GetAbilitySystemComponent();
		const FMSHUDMinimapUnitMarker Style=UMSHUDViewModel::MakeMinimapUnitMarker(ViewerTeam,UMSTeamStatics::GetTeamId(Unit));
		if(!Style.bFriendly||(ASC&&ASC->HasMatchingGameplayTag(MSTags::State_Dead)))return;
		const FVector2D Local=UMSMapWidgetBase::WorldToMapLocalRectCentered(Unit->GetActorLocation(),MapWorldCenter,MapWorldHalfExtents,
			FVector2D(RespawnMapTextureSize),0.0f);
		UImage* Marker=RespawnTacticalUnitMarkers[RespawnUnitIndex++];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Marker->Slot),{RespawnMapTextureX+Local.X-4.5f,RespawnMapTextureY+Local.Y-4.5f},{9,9});
		Marker->SetColorAndOpacity(Style.Color);Marker->SetRenderTransformAngle(Style.bFriendly?45.0f:0.0f);Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	});
	if (Player && OverviewTexture && MapWorldHalfExtent > 0.0f && bFreshContacts)
	for (const FMSMapContact& Contact : ContactController->GetMapContacts())
	{
		if (RespawnUnitIndex >= RespawnTacticalUnitMarkers.Num()) break;
		if (Contact.ContactId == 0 || Contact.LastKnownLocation.ContainsNaN()) continue;
		const FVector2D Local = UMSMapWidgetBase::WorldToMapLocalRectCentered(Contact.LastKnownLocation,
			MapWorldCenter, MapWorldHalfExtents, FVector2D(RespawnMapTextureSize), 0.0f);
		UImage* Marker = RespawnTacticalUnitMarkers[RespawnUnitIndex++];
		SetDesignSlot(CastChecked<UCanvasPanelSlot>(Marker->Slot),
			{RespawnMapTextureX+Local.X-4.5f, RespawnMapTextureY+Local.Y-4.5f}, {9,9});
		Marker->SetColorAndOpacity(MSMapPalette::Enemy);
		Marker->SetRenderTransformAngle(0.0f);
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	for(int32 Index=RespawnUnitIndex;Index<RespawnTacticalUnitMarkers.Num();++Index)if(RespawnTacticalUnitMarkers[Index])RespawnTacticalUnitMarkers[Index]->SetVisibility(ESlateVisibility::Collapsed);
	if (RespawnMSListNames.Num()==4 && RespawnMSListTimers.Num()==4)
	{
		UMSDeckSubsystem* Deck=GetGameInstance()?GetGameInstance()->GetSubsystem<UMSDeckSubsystem>():nullptr;
		const UMSRespawnComponent* Respawn=Player?Player->GetRespawnComponent():nullptr;
		const int32 SelectedSlot=Deck?Deck->GetDeploymentSelectedSlotIndex():INDEX_NONE;
		for(int32 Index=0;Index<4;++Index)
		{
			const UMSMobileSuitData* Preset=Deck?Deck->GetDeploymentPresetInSlot(Index):nullptr;
			const FText PresetName=Preset?Preset->UnitName:FText::FromString(TEXT("EMPTY"));
			RespawnMSListNames[Index]->SetText(PresetName);RespawnMSListNames[Index]->SetToolTipText(Preset?PresetName:FText::GetEmpty());
			const float Remaining=Respawn?FMath::Max(0.0f,Respawn->GetDeckSlotTimeRemaining(Index)):0.0f;
			const FText TimerText=!Preset?FText::FromString(TEXT("EMPTY"))
				:Remaining>0.0f?FText::FromString(FString::Printf(TEXT("再出撃まで\n%02d秒"),FMath::CeilToInt(Remaining)))
				:FText::FromString(TEXT("READY"));
			RespawnMSListTimers[Index]->SetText(TimerText);
			if(RespawnMSListFrames.IsValidIndex(Index))
			{
				FSlateBrush CardBrush;CardBrush.DrawAs=ESlateBrushDrawType::Box;
				CardBrush.TintColor=Index==SelectedSlot?MSPixel::Hex(0x8A7024,.95f):MSPixel::Hex(0x153A43,.88f);
				RespawnMSListFrames[Index]->SetBrush(CardBrush);
			}
		}
	}
	if (!Snapshot.bVisible || !RespawnCountdownText) return;
	const int32 Hundredths=FMath::CeilToInt(Snapshot.RemainingSeconds*100.0f);
	// The reference uses a double-prime hundredths separator (6''78), which
	// remains legible at 1280x720 inside the fixed countdown box.
	RespawnCountdownText->SetText(FText::FromString(FString::Printf(TEXT("%d''%02d"), Hundredths/100,Hundredths%100)));
}

void UMSHUDWidget::BuildTopBattlePanel()
{
	// 上段=自軍(赤橙)、下段=敵軍(青)。陣営ラベルはNativeTickで機体データから更新する。
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	const bool bVideo=bVideoHudArtPass;
	UBorder* Panel=WidgetTree->ConstructWidget<UBorder>();
	if(bVideo)
	{
		FSlateBrush Clear;Clear.DrawAs=ESlateBrushDrawType::NoDrawType;Panel->SetBrush(Clear);Panel->SetBrushColor(FLinearColor::Transparent);Panel->SetPadding(FMargin(0.0f));
	}
	else
	{
		MSUIStyle::ApplyBrush(Panel,MSUIStyle::EBrush::Panel,MSHUDPalette::PanelBackground);Panel->SetPadding(FMargin(10,6));
	}
	UCanvasPanel* Inner=WidgetTree->ConstructWidget<UCanvasPanel>();Panel->SetContent(Inner);
	const bool bShowDensity=Layout.bShowDensityInstrumentRules&&!bVideo;
	UImage* DensityFill=WidgetTree->ConstructWidget<UImage>();FSlateBrush DensityBrush;DensityBrush.DrawAs=ESlateBrushDrawType::Box;DensityBrush.TintColor=FLinearColor(.01f,.28f,.38f,1.0f);DensityFill->SetBrush(DensityBrush);DensityFill->SetVisibility(bShowDensity?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	UCanvasPanelSlot* DensitySlot=Inner->AddChildToCanvas(DensityFill);DensitySlot->SetAnchors(FAnchors(0,0,1,1));DensitySlot->SetOffsets(FMargin(0));DensitySlot->SetZOrder(-1);
	UImage* DensityRule=WidgetTree->ConstructWidget<UImage>();FSlateBrush RuleBrush;RuleBrush.DrawAs=ESlateBrushDrawType::Box;RuleBrush.TintColor=MSHUDPalette::HUDAccent;DensityRule->SetBrush(RuleBrush);DensityRule->SetVisibility(bShowDensity?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	UCanvasPanelSlot* RuleSlot=Inner->AddChildToCanvas(DensityRule);RuleSlot->SetAnchors(FAnchors(0,0,1,0));RuleSlot->SetOffsets(FMargin(0,0,0,6));RuleSlot->SetZOrder(0);
	for(const float RuleX : {108.0f,482.0f})
	{
		UImage* Separator=WidgetTree->ConstructWidget<UImage>();Separator->SetBrush(RuleBrush);Separator->SetVisibility(bShowDensity?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		UCanvasPanelSlot* SeparatorSlot=Inner->AddChildToCanvas(Separator);SeparatorSlot->SetPosition({RuleX,0});SeparatorSlot->SetSize({12,54});SeparatorSlot->SetZOrder(5);
	}
	for(const float GridX : {160.0f,200.0f,240.0f,280.0f,320.0f,360.0f,400.0f,440.0f})
	{
		UImage* GridTick=WidgetTree->ConstructWidget<UImage>();GridTick->SetBrush(RuleBrush);GridTick->SetRenderOpacity(.55f);GridTick->SetVisibility(bShowDensity?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		UCanvasPanelSlot* GridSlot=Inner->AddChildToCanvas(GridTick);GridSlot->SetPosition({GridX,10});GridSlot->SetSize({8,38});GridSlot->SetZOrder(5);
	}
	UCanvasPanelSlot* PanelSlot=RootCanvas->AddChildToCanvas(Panel);
	if(bVideo)
	{
		PanelSlot->SetAnchors(FAnchors(0.0f,0.0f,1.0f,1.0f));PanelSlot->SetOffsets(FMargin(0.0f));PanelSlot->SetAlignment(FVector2D::ZeroVector);
	}
	else
	{
		PanelSlot->SetAnchors(FAnchors(.5f,.012f));PanelSlot->SetAlignment(FVector2D(.5f,0));PanelSlot->SetPosition(FVector2D::ZeroVector);PanelSlot->SetSize(FVector2D(620,66));
	}
	auto AddText=[this,Inner,bVideo](const FVector2D& Pos,const FVector2D& Size,const int32 FontSize,const FLinearColor& Color,const FString& Str,const FName Name=NAME_None)->UTextBlock*
	{
		UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);T->SetText(FText::FromString(Str));T->SetColorAndOpacity(Color);
		FSlateFontInfo Font=T->GetFont();Font.Size=FontSize;T->SetFont(Font);
		UCanvasPanelSlot* S=Inner->AddChildToCanvas(bVideo?ScaleVideoLabel(WidgetTree,T,Size):T);if(bVideo)SetDesignSlot(S,FVector2D(520.0f,0.0f)+Pos,Size);else{S->SetPosition(Pos);S->SetSize(Size);}return T;
	};
	auto AddGauge=[this,Inner,bVideo](const FVector2D& Pos,const FLinearColor& Fill,const MSUIStyle::EBrush FillRole)->UProgressBar*
	{
		UProgressBar* B=WidgetTree->ConstructWidget<UProgressBar>();B->SetFillColorAndOpacity(Fill);if(!bVideo)MSUIStyle::ApplyProgressBarStyle(B,FillRole);B->SetPercent(1.0f);
		UCanvasPanelSlot* S=Inner->AddChildToCanvas(B);if(bVideo)SetDesignSlot(S,FVector2D(520.0f,0.0f)+Pos,FVector2D(290.0f,16.0f));else{S->SetPosition(Pos);S->SetSize(FVector2D(290,16));}return B;
	};
	AddText({bVideo?30.0:0.0,bVideo?5.0:0.0},{100,14},9,MSHUDPalette::HUDAccent,TEXT("TIME"));
	TimeText=AddText({bVideo?30.0:0.0,bVideo?26.0:14.0},{110,36},bVideo?20:26,FLinearColor::White,TEXT("--'--"));
	AddText({120,4},{60,12},9,MSFactionPresentation::UnassignedColor,TEXT("--"),TEXT("PlayerFactionLabel"));
	AddText({120,30},{60,12},9,MSFactionPresentation::UnassignedColor,TEXT("--"),TEXT("EnemyFactionLabel"));
	TeamABar=AddGauge({185,4},MSFactionPresentation::UnassignedColor,MSUIStyle::EBrush::GaugeFillTeamA);
	TeamBBar=AddGauge({185,30},MSFactionPresentation::UnassignedColor,MSUIStyle::EBrush::GaugeFillTeamB);
	if(bVideo){TeamABar->SetVisibility(ESlateVisibility::Collapsed);TeamBBar->SetVisibility(ESlateVisibility::Collapsed);}
	AddText({bVideo?448.0:495.0,bVideo?19.0:0.0},{bVideo?70.0:105.0,12},9,MSHUDPalette::HUDAccent,TEXT("TACTICAL"));
	TacticalText=AddText({bVideo?448.0:495.0,bVideo?30.0:14.0},{bVideo?80.0:105.0,32},bVideo?16:22,FLinearColor::White,TEXT("0"));
	TacticalText->SetJustification(ETextJustify::Right);
}

void UMSHUDWidget::BuildTaskPanel()
{
	// TASK strip sits directly under the square minimap (same right-edge anchor as the minimap canvas).
	TaskPanel=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(TaskPanel,MSUIStyle::EBrush::Panel,MSHUDPalette::PanelBackground);TaskPanel->SetPadding(FMargin(8,4));
	UCanvasPanel* Inner=WidgetTree->ConstructWidget<UCanvasPanel>();TaskPanel->SetContent(Inner);
	UCanvasPanelSlot* PanelSlot=RootCanvas->AddChildToCanvas(TaskPanel);
	PanelSlot->SetAnchors(FAnchors(.98f,.03f));PanelSlot->SetAlignment(FVector2D(1,0));PanelSlot->SetPosition(FVector2D(0,MSHUDPalette::MinimapSize+8));PanelSlot->SetSize(FVector2D(MSHUDPalette::MinimapSize,30));
	auto AddText=[this,Inner](const FVector2D& Pos,const FVector2D& Size,const int32 FontSize)->UTextBlock*
	{
		UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>();FSlateFontInfo Font=T->GetFont();Font.Size=FontSize;T->SetFont(Font);
		UCanvasPanelSlot* S=Inner->AddChildToCanvas(T);S->SetPosition(Pos);S->SetSize(Size);return T;
	};
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	UTextBlock* TaskLabel=AddText({0,0},{42,22},10);TaskLabel->SetText(FText::FromString(TEXT("TASK")));TaskLabel->SetColorAndOpacity(MSHUDPalette::HUDAccent);TaskLabel->SetVisibility(Layout.bShowTaskPrefix?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	TaskMissionText=AddText(Layout.TaskMissionPosition,Layout.TaskMissionSize,13);TaskMissionText->SetColorAndOpacity(MSHUDPalette::MissionDefense);
	TaskText=AddText(Layout.TaskTextPosition,Layout.TaskTextSize,Layout.bShowTaskPrefix?11:12);TaskText->SetColorAndOpacity(FLinearColor::White);TaskText->SetClipping(EWidgetClipping::ClipToBounds);
	if(bVideoHudArtPass)
	{
		TaskPanel->RemoveFromParent();Inner->RemoveFromParent();
		UWidget* Frame=MSPixelUI::Frame(WidgetTree,Inner,EMSVectorSurface::Panel,FMargin(4,2));
		TaskPanelVideoFit=ScaleVideoContent(WidgetTree,Frame,{256,30});
		SetDesignSlot(RootCanvas->AddChildToCanvas(TaskPanelVideoFit),{1664,318},{256,30});
	}
}

void UMSHUDWidget::BuildRightBottomCluster()
{
	// Weapon slots stack down the right edge; ARMOR numerals (largest HUD font) and the BOOST/ARMOR bar row sit below.
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	const bool bVideo=bVideoHudArtPass;
	WeaponPanel=AddCanvas<UVerticalBox>(WidgetTree,RootCanvas,{.985f,Layout.WeaponAnchorY},{Layout.WeaponPanelWidth,0},{1,0}); CastChecked<UCanvasPanelSlot>(WeaponPanel->Slot)->SetAutoSize(true);
	if(bVideo)WeaponPanel->SetVisibility(ESlateVisibility::Collapsed);
	auto MakeText=[this,bVideo](const FAnchors& Anchor,const FVector2D& Align,const FVector2D& Pos,const FVector2D& Size,const int32 FontSize,const FLinearColor& Color)->UTextBlock*
	{
		UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>();FSlateFontInfo Font=T->GetFont();Font.Size=FontSize;T->SetFont(Font);T->SetColorAndOpacity(Color);
		T->SetClipping(EWidgetClipping::ClipToBounds);
		UCanvasPanelSlot* S=RootCanvas->AddChildToCanvas(bVideo?ScaleVideoLabel(WidgetTree,T,Size):T);if(bVideo)SetDesignSlot(S,Pos,Size);else{S->SetAnchors(Anchor);S->SetAlignment(Align);S->SetPosition(Pos);S->SetSize(Size);}return T;
	};
	UTextBlock* ArmorLabel=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1818,1005):FVector2D(0,-80),{95,14},bVideo?9:9,MSHUDPalette::HUDAccent);
	ArmorLabel->SetText(FText::FromString(TEXT("ARMOR")));ArmorLabel->SetJustification(ETextJustify::Right);
	ArmorMaxText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1730,999):FVector2D(0,-26),bVideo?FVector2D(88,24):FVector2D(95,34),bVideo?14:(int32)MSHUDPalette::ArmorMaxFontSize,FLinearColor::White);
	ArmorMaxText->SetJustification(ETextJustify::Right);ArmorMaxText->SetText(FText::FromString(TEXT("/0")));
	ArmorValueText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1588,982):FVector2D(-95,-26),bVideo?FVector2D(147,43):FVector2D(220,54),bVideo?32:(int32)MSHUDPalette::ArmorValueFontSize,bVideo?FLinearColor::White:MSHUDPalette::ArmorFill);
	if(bVideo){ArmorValueText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("BoldItalic"),32));ArmorValueText->SetShadowOffset(FVector2D(1,1));}
	ArmorValueText->SetJustification(ETextJustify::Right);ArmorValueText->SetText(FText::FromString(TEXT("0")));
	// A single Mode B plate visually binds BOOST, DP and ARMOR. Mode A receives a transparent
	// fallback and therefore keeps its legacy appearance pixel-for-pixel.
	UBorder* VitalFrame=WidgetTree->ConstructWidget<UBorder>();
	if(!bVideo)MSUIStyle::ApplyBrush(VitalFrame,MSUIStyle::EBrush::Panel,FLinearColor::Transparent);else{FSlateBrush Clear;Clear.DrawAs=ESlateBrushDrawType::NoDrawType;VitalFrame->SetBrush(Clear);VitalFrame->SetBrushColor(FLinearColor::Transparent);}
	UCanvasPanel* VitalDensityCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();VitalFrame->SetPadding(FMargin(5.0f));VitalFrame->SetContent(VitalDensityCanvas);
	UImage* VitalDensityFill=WidgetTree->ConstructWidget<UImage>();FSlateBrush VitalFillBrush;VitalFillBrush.DrawAs=ESlateBrushDrawType::Box;VitalFillBrush.TintColor=FLinearColor(.01f,.26f,.34f,.96f);VitalDensityFill->SetBrush(VitalFillBrush);
	UCanvasPanelSlot* VitalFillSlot=VitalDensityCanvas->AddChildToCanvas(VitalDensityFill);VitalFillSlot->SetAnchors(FAnchors(0,0,1,1));VitalFillSlot->SetOffsets(FMargin(0));
	UImage* VitalRule=WidgetTree->ConstructWidget<UImage>();FSlateBrush VitalRuleBrush;VitalRuleBrush.DrawAs=ESlateBrushDrawType::Box;VitalRuleBrush.TintColor=MSHUDPalette::HUDAccent;VitalRule->SetBrush(VitalRuleBrush);
	UCanvasPanelSlot* VitalRuleSlot=VitalDensityCanvas->AddChildToCanvas(VitalRule);VitalRuleSlot->SetAnchors(FAnchors(0,1,1,1));VitalRuleSlot->SetOffsets(FMargin(0,-3,0,3));
	VitalFrame->SetVisibility(Layout.bShowVitalFrame&&!bVideo?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	UCanvasPanelSlot* VitalFrameSlot=RootCanvas->AddChildToCanvas(VitalFrame);VitalFrameSlot->SetAnchors(FAnchors(.985f,.99f));VitalFrameSlot->SetAlignment(FVector2D(1,1));VitalFrameSlot->SetPosition(FVector2D(0,8));VitalFrameSlot->SetSize(FVector2D(570,112));VitalFrameSlot->SetZOrder(-1);
	UTextBlock* BoostLabel=MakeText(FAnchors(Layout.BoostAnchorX,.975f),{1,.5f},bVideo?FVector2D(VideoBoostX+14,VideoBoostY+19):Layout.BoostLabelPosition,bVideo?FVector2D(68,14):FVector2D(60,14),bVideo?8:9,bVideo?MSPixel::Hex(0x7ee5d1):MSHUDPalette::BoostFill);
	BoostLabel->SetText(FText::FromString(TEXT("BOOST")));
	BoostBar=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{Layout.BoostAnchorX,.975f},{MSHUDPalette::BoostBarWidth,MSHUDPalette::BoostBarHeight},{1,.5f});CastChecked<UCanvasPanelSlot>(BoostBar->Slot)->SetPosition(Layout.BoostPosition);
	if(!bVideo)MSUIStyle::ApplyProgressBarStyle(BoostBar,MSUIStyle::EBrush::GaugeFillBoost);
	if(bVideo)BoostBar->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* BoostStatus=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoostStatusText"));
	BoostStatus->SetJustification(ETextJustify::Center);BoostStatus->SetVisibility(ESlateVisibility::Collapsed);
	{FSlateFontInfo Font=BoostStatus->GetFont();Font.Size=MSHUDPalette::BoostStatusFontSize;Font.TypefaceFontName=TEXT("Bold");BoostStatus->SetFont(Font);}
	UCanvasPanelSlot* BoostStatusSlot=RootCanvas->AddChildToCanvas(BoostStatus);if(bVideo)SetDesignSlot(BoostStatusSlot,FVector2D(VideoBoostX,VideoBoostY-20),FVector2D(VideoBoostWidth,18));else{BoostStatusSlot->SetAnchors(FAnchors(Layout.BoostAnchorX,.975f));BoostStatusSlot->SetAlignment(FVector2D(1,.5f));BoostStatusSlot->SetPosition(Layout.BoostPosition);BoostStatusSlot->SetSize(FVector2D(MSHUDPalette::BoostBarWidth,16));}
	ArmorBar=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{.985f,.99f},{250,14},{1,1}); ArmorBar->SetFillColorAndOpacity(MSHUDPalette::ArmorFill);if(!bVideo)MSUIStyle::ApplyProgressBarStyle(ArmorBar,MSUIStyle::EBrush::GaugeFillArmor);if(bVideo)ArmorBar->SetVisibility(ESlateVisibility::Collapsed);
	// DPバーはブーストゲージ(x=-270)の直上に置く(ユーザー指定仕様)
	DPBar=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{.985f,.99f},{250,10},{1,1}); CastChecked<UCanvasPanelSlot>(DPBar->Slot)->SetPosition(FVector2D(-270,-18));
	if(!bVideo)MSUIStyle::ApplyProgressBarStyle(DPBar,MSUIStyle::EBrush::GaugeFillDP);if(bVideo)DPBar->SetVisibility(ESlateVisibility::Collapsed);
	DPStateText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1300,1000):FVector2D(-530,-18),bVideo?FVector2D(50,16):FVector2D(100,14),bVideo?8:10,MSHUDPalette::DPNormal);DPStateText->SetJustification(ETextJustify::Right);DPStateText->SetText(FText::FromString(TEXT("DP")));
	// The reference places a slim, separate shield instrument directly above ARMOR.
	// Keep the native component/VM as the only durability source; these are display widgets.
	ShieldValueText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1741,947):FVector2D(-83,-100),bVideo?FVector2D(96,29):FVector2D(86,24),bVideo?23:19,FLinearColor::White);
	ShieldValueText->SetJustification(ETextJustify::Right);
	if(bVideo){ShieldValueText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("BoldItalic"),23));ShieldValueText->SetShadowOffset(FVector2D(1,1));}
	ShieldText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1837,955):FVector2D(0,-103),bVideo?FVector2D(78,18):FVector2D(78,17),bVideo?10:10,MSHUDPalette::HUDAccent);
	ShieldText->SetJustification(ETextJustify::Right);ShieldText->SetText(FText::FromString(TEXT("SHIELD")));
	if(bVideo)ShieldText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10));
	ShieldBar=AddCanvas<UProgressBar>(WidgetTree,RootCanvas,{.985f,.99f},{171,10},{1,1});
	if(bVideo)SetDesignSlot(CastChecked<UCanvasPanelSlot>(ShieldBar->Slot),{VideoShieldX,VideoShieldY},{VideoShieldWidth,VideoShieldHeight});
	else CastChecked<UCanvasPanelSlot>(ShieldBar->Slot)->SetPosition(FVector2D(0,-85));
	ShieldBar->SetFillColorAndOpacity(MSPixel::Hex(0x38D6B5));
	ShieldBrokenText=MakeText(FAnchors(.985f,.99f),{1,1},bVideo?FVector2D(1820,989):FVector2D(0,-72),bVideo?FVector2D(95,12):FVector2D(90,12),bVideo?9:9,MSHUDPalette::ArmorLow);
	ShieldBrokenText->SetJustification(ETextJustify::Right);ShieldBrokenText->SetText(FText::FromString(TEXT("シールド大破")));
	ShieldValueText->SetVisibility(ESlateVisibility::Collapsed);ShieldText->SetVisibility(ESlateVisibility::Collapsed);
	ShieldBar->SetVisibility(ESlateVisibility::Collapsed);ShieldBrokenText->SetVisibility(ESlateVisibility::Collapsed);
	if(bVideo)
	{
		RespawnRecoveryText=MakeText(FAnchors(0,0),FVector2D::ZeroVector,{1618,1000},{165,16},8,MSPixel::Hex(0x52E8D5,.95f));
		RespawnRecoveryText->SetText(FText::FromString(TEXT("RECOVERY")));RespawnRecoveryText->SetJustification(ETextJustify::Left);RespawnRecoveryText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMSHUDWidget::BuildLeftBottomCluster()
{
	// SCORE panel stacked above the kill log at the bottom-left, mirroring the reference frame.
	UVerticalBox* Cluster=WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* ClusterSlot=RootCanvas->AddChildToCanvas(Cluster);
	ClusterSlot->SetAnchors(FAnchors(0,1));ClusterSlot->SetAlignment(FVector2D(0,1));ClusterSlot->SetPosition(FVector2D(24,-24));ClusterSlot->SetAutoSize(true);
	UTextBlock* ScoreLabel=WidgetTree->ConstructWidget<UTextBlock>();ScoreLabel->SetText(FText::FromString(TEXT("SCORE")));ScoreLabel->SetColorAndOpacity(MSHUDPalette::HUDAccent);
	{FSlateFontInfo Font=ScoreLabel->GetFont();Font.Size=10;ScoreLabel->SetFont(Font);}
	Cluster->AddChildToVerticalBox(ScoreLabel);
	ScoreValueText=WidgetTree->ConstructWidget<UTextBlock>();ScoreValueText->SetText(FText::FromString(TEXT("0 PT")));
	{FSlateFontInfo Font=ScoreValueText->GetFont();Font.Size=20;ScoreValueText->SetFont(Font);}
	UVerticalBoxSlot* ScoreSlot=Cluster->AddChildToVerticalBox(ScoreValueText);ScoreSlot->SetPadding(FMargin(0,0,0,8));
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	UCanvasPanel* Roster=WidgetTree->ConstructWidget<UCanvasPanel>();
	USizeBox* RosterSize=WidgetTree->ConstructWidget<USizeBox>();RosterSize->SetWidthOverride(318.0f);RosterSize->SetHeightOverride(78.0f);RosterSize->SetContent(Roster);
	RosterSize->SetVisibility(Layout.bShowTeamRoster?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	UVerticalBoxSlot* RosterSlot=Cluster->AddChildToVerticalBox(RosterSize);RosterSlot->SetPadding(FMargin(0,0,0,7));
	for(int32 I=0;I<6;++I)
	{
		const int32 Column=I/3,Row=I%3;const float X=Column*159.0f,Y=Row*25.0f;
		UBorder* RowBg=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(RowBg,MSUIStyle::EBrush::ListRow,FLinearColor(.01f,.035f,.055f,.82f));RowBg->SetPadding(FMargin(4,1));
		UCanvasPanelSlot* BgSlot=Roster->AddChildToCanvas(RowBg);BgSlot->SetPosition(FVector2D(X,Y));BgSlot->SetSize(FVector2D(155,23));
		UCanvasPanel* RowCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();RowBg->SetContent(RowCanvas);
		UTextBlock* Name=WidgetTree->ConstructWidget<UTextBlock>();Name->SetClipping(EWidgetClipping::ClipToBounds);Name->SetColorAndOpacity(MSHUDPalette::MinimapFriendly);
		UTextBlock* Suit=WidgetTree->ConstructWidget<UTextBlock>();Suit->SetClipping(EWidgetClipping::ClipToBounds);Suit->SetColorAndOpacity(FLinearColor(.78f,.88f,.92f,1));
		for(UTextBlock* Text:{Name,Suit}){FSlateFontInfo Font=Text->GetFont();Font.Size=9;Text->SetFont(Font);}
		UCanvasPanelSlot* NameSlot=RowCanvas->AddChildToCanvas(Name);NameSlot->SetPosition(FVector2D::ZeroVector);NameSlot->SetSize(FVector2D(88,18));
		UCanvasPanelSlot* SuitSlot=RowCanvas->AddChildToCanvas(Suit);SuitSlot->SetPosition(FVector2D(90,0));SuitSlot->SetSize(FVector2D(57,18));
		TeamRosterNames.Add(Name);TeamRosterSuits.Add(Suit);
	}
	KillLogPanel=WidgetTree->ConstructWidget<UVerticalBox>();
	KillLogBackground=WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(KillLogBackground,
		Layout.bShowTimedBattleLog?MSUIStyle::EBrush::Scrim:MSUIStyle::EBrush::Panel,
		Layout.bShowTimedBattleLog?FLinearColor(.005f,.012f,.018f,.78f):FLinearColor::Transparent);
	KillLogBackground->SetPadding(Layout.bShowTimedBattleLog?FMargin(6,4):FMargin(0));
	KillLogBackground->SetContent(KillLogPanel);KillLogBackground->SetVisibility(ESlateVisibility::Collapsed);
	Cluster->AddChildToVerticalBox(KillLogBackground);
	for(int32 I=0;I<MSHUDPalette::MaxKillLogLines;++I){UTextBlock* Line=WidgetTree->ConstructWidget<UTextBlock>();Line->SetVisibility(ESlateVisibility::Collapsed);Line->SetClipping(EWidgetClipping::ClipToBounds);if(Layout.bShowTimedBattleLog){FSlateFontInfo Font=Line->GetFont();Font.Size=10;Line->SetFont(Font);}KillLogPanel->AddChildToVerticalBox(Line);KillLogLines.Add(Line);}
}

void UMSHUDWidget::RefreshTeamRoster()
{
	if(TeamRosterNames.IsEmpty())return;
	TArray<AMSUnitBase*> Allies;const AMSPlayerCharacter* Player=Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());
	if(Player)if(UMSUnitRegistrySubsystem* Registry=UnitRegistry.Get())Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		if(IsValid(Unit)&&UMSTeamStatics::IsOnTeam(Unit,UMSTeamStatics::GetTeamId(Player)))Allies.Add(Unit);
	});
	Allies.Sort([](const AMSUnitBase& A,const AMSUnitBase& B){return A.GetUnitDisplayName()<B.GetUnitDisplayName();});
	for(int32 I=0;I<TeamRosterNames.Num();++I)
	{
		const bool bVisible=Allies.IsValidIndex(I);TeamRosterNames[I]->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);TeamRosterSuits[I]->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		if(!bVisible)continue;const AMSUnitBase* Unit=Allies[I];TeamRosterNames[I]->SetText(FText::FromString(Unit->GetUnitDisplayName()));const UMSMobileSuitData* Suit=Unit->GetMobileSuitData();TeamRosterSuits[I]->SetText(Suit?Suit->UnitName:FText::GetEmpty());
	}
}

void UMSHUDWidget::BuildMissionIcon()
{
	if(bVideoHudArtPass)
	{
		MissionIconImage=WidgetTree->ConstructWidget<UImage>();
		FSlateBrush Circle;Circle.DrawAs=ESlateBrushDrawType::RoundedBox;Circle.OutlineSettings.RoundingType=ESlateBrushRoundingType::HalfHeightRadius;Circle.TintColor=FLinearColor::White;
		MissionIconImage->SetBrush(Circle);SetDesignSlot(RootCanvas->AddChildToCanvas(MissionIconImage),{1038,992},{64,64});
		MissionIconText=WidgetTree->ConstructWidget<UTextBlock>();MissionIconText->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font=MissionIconText->GetFont();Font.Size=14;MissionIconText->SetFont(Font);
		SetDesignSlot(RootCanvas->AddChildToCanvas(ScaleVideoLabel(WidgetTree,MissionIconText,{80,25})),{1030,1007},{80,25});
		MissionIconSubText=WidgetTree->ConstructWidget<UTextBlock>();MissionIconSubText->SetJustification(ETextJustify::Center);
		Font=MissionIconSubText->GetFont();Font.Size=7;MissionIconSubText->SetFont(Font);
		SetDesignSlot(RootCanvas->AddChildToCanvas(ScaleVideoLabel(WidgetTree,MissionIconSubText,{80,14})),{1030,1031},{80,14});
		return;
	}
	// Circular mission-type icon at the bottom-center (防衛/DEFENCE badge in the reference frame).
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	MissionIconImage=AddCanvas<UImage>(WidgetTree,RootCanvas,Layout.MissionBadgeAnchor,{MSHUDPalette::MissionIconSize,MSHUDPalette::MissionIconSize},{.5f,1});
	CastChecked<UCanvasPanelSlot>(MissionIconImage->Slot)->SetPosition(Layout.MissionBadgePosition);
	FSlateBrush Circle;Circle.DrawAs=ESlateBrushDrawType::RoundedBox;Circle.OutlineSettings.RoundingType=ESlateBrushRoundingType::HalfHeightRadius;Circle.TintColor=FLinearColor::White;
	MissionIconImage->SetBrush(Circle);MissionIconImage->SetColorAndOpacity(MSHUDPalette::MissionDefense);
	MissionIconText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,Layout.MissionBadgeAnchor,{120,24},{.5f,1});
	CastChecked<UCanvasPanelSlot>(MissionIconText->Slot)->SetPosition(Layout.MissionBadgePosition+FVector2D(0,-28));
	MissionIconText->SetJustification(ETextJustify::Center);
	{FSlateFontInfo Font=MissionIconText->GetFont();Font.Size=15;MissionIconText->SetFont(Font);}
	MissionIconSubText=AddCanvas<UTextBlock>(WidgetTree,RootCanvas,Layout.MissionBadgeAnchor,{120,14},{.5f,1});
	CastChecked<UCanvasPanelSlot>(MissionIconSubText->Slot)->SetPosition(Layout.MissionBadgePosition+FVector2D(0,-8));
	MissionIconSubText->SetJustification(ETextJustify::Center);
	{FSlateFontInfo Font=MissionIconSubText->GetFont();Font.Size=8;MissionIconSubText->SetFont(Font);}
}

void UMSHUDWidget::NativeDestruct()
{
	if(UMSUnitRegistrySubsystem* Registry=UnitRegistry.Get()){Registry->OnUnitRegistered.RemoveAll(this);Registry->ForEachRegisteredUnit([this](AMSUnitBase* Unit){if(UMSAttributeSet_Vital* Vital=Unit->GetVitalSet())Vital->OnDeath.RemoveDynamic(this,&UMSHUDWidget::HandleUnitDeath);});}
	if(MinimapCapture){MinimapCapture->TextureTarget=nullptr;MinimapCapture->DestroyComponent();MinimapCapture=nullptr;}
	MinimapCapturePlayer.Reset();UnitRegistry.Reset(); Super::NativeDestruct();
}

FVector2D UMSHUDWidget::GetOverviewMinimapCanvasSize(const FVector2D& WorldHalfExtentsCm)
{
	const FVector2D Square(MSHUDPalette::MinimapSize);
	const FVector2D Content = UMSMapWidgetBase::GetRectContentSize(WorldHalfExtentsCm, Square, 0.0f);
	return Content.X > 0.0f && Content.Y > 0.0f ? Content : Square;
}

void UMSHUDWidget::InitializeMinimapUI()
{
	const FVector2D MapSize(MSHUDPalette::MinimapSize);
	MinimapFrame=AddCanvas<UBorder>(WidgetTree,RootCanvas,{.98f,.03f},MapSize+FVector2D(12),{1,0});
	CastChecked<UCanvasPanelSlot>(MinimapFrame->Slot)->SetPosition(FVector2D(6,-6));
	MSUIStyle::ApplyBrush(MinimapFrame,MSUIStyle::EBrush::Panel,FLinearColor::Transparent);
	MinimapCanvas=AddCanvas<UCanvasPanel>(WidgetTree,RootCanvas,{.98f,.03f},MapSize,{1,0});
	MinimapCanvas->SetClipping(EWidgetClipping::ClipToBounds);
	MinimapImage=AddMinimapImage(WidgetTree,MinimapCanvas,MapSize*.5f,MapSize,FLinearColor::White);
	MinimapRadarRangeLayer=WidgetTree->ConstructWidget<UMSMapRadarRangeWidget>();
	UCanvasPanelSlot* RadarSlot=MinimapCanvas->AddChildToCanvas(MinimapRadarRangeLayer);
	RadarSlot->SetPosition(FVector2D::ZeroVector);RadarSlot->SetSize(MapSize);RadarSlot->SetZOrder(2);
	MinimapRadarRangeLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	// Seven translucent rays form a code-only approximation of the Unity FOV wedge.
	for(int32 I=-3;I<=3;++I)
	{
		UImage* Ray=AddMinimapImage(WidgetTree,MinimapCanvas,MapSize*.5f,{3,42},MSHUDPalette::MinimapFOV);
		CastChecked<UCanvasPanelSlot>(Ray->Slot)->SetAlignment(FVector2D(.5f,1));
		Ray->SetRenderTransformPivot(FVector2D(.5f,1));Ray->SetRenderTransformAngle(I*10.0f);MinimapFOVRays.Add(Ray);
	}
	RefreshMinimapCapturePoints();
	for(int32 Number=1;Number<=4;++Number)
	{
		const FMSMapEntranceMarker Marker=UMSMapWidgetBase::ConstructEntranceMarker(WidgetTree,MinimapCanvas,Number,13.0f);
		MinimapEntranceMarkers.Add(Marker.Root);MinimapEntranceShapes.Add(Marker.Shape);
	}
	for(int32 I=0;I<MSHUDPalette::MaxMinimapEnemyMarkers;++I)
	{
		// Brush tint is multiplied by SetColorAndOpacity; neutral tint keeps ally blue visible.
		UImage* Marker=AddMinimapImage(WidgetTree,MinimapCanvas,FVector2D::ZeroVector,{9,9},FLinearColor::White);
		Marker->SetVisibility(ESlateVisibility::Collapsed);MinimapUnitMarkers.Add(Marker);
	}
	for(int32 I=0;I<MSHUDPalette::MaxMinimapPickupMarkers;++I)
	{
		UImage* Marker=AddMinimapImage(WidgetTree,MinimapCanvas,FVector2D::ZeroVector,{8,8},MSHUDPalette::MinimapPickup);
		Marker->SetRenderTransformAngle(45);Marker->SetVisibility(ESlateVisibility::Collapsed);MinimapPickupMarkers.Add(Marker);
	}
	// Three orange primitives make an arrow pointing toward the top of the map at yaw zero.
	MinimapPlayerMarker=WidgetTree->ConstructWidget<UCanvasPanel>();
	auto* PlayerSlot=MinimapCanvas->AddChildToCanvas(MinimapPlayerMarker);PlayerSlot->SetAlignment(FVector2D(.5f,.5f));PlayerSlot->SetPosition(MapSize*.5f);PlayerSlot->SetSize(FVector2D(MSHUDPalette::MinimapPlayerIconSize));
	AddMinimapImage(WidgetTree,MinimapPlayerMarker,{11,8},{4,15},MSHUDPalette::MinimapPlayer);
	UImage* Left=AddMinimapImage(WidgetTree,MinimapPlayerMarker,{7,12},{3,11},MSHUDPalette::MinimapPlayer);Left->SetRenderTransformAngle(-45);
	UImage* Right=AddMinimapImage(WidgetTree,MinimapPlayerMarker,{15,12},{3,11},MSHUDPalette::MinimapPlayer);Right->SetRenderTransformAngle(45);
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	auto AddCardinal=[this,Layout](const TCHAR* Label,const FVector2D& Position)
	{
		UTextBlock* Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Label));Text->SetJustification(ETextJustify::Center);Text->SetColorAndOpacity(MSHUDPalette::HUDAccent);
		FSlateFontInfo Font=Text->GetFont();Font.Size=10;Font.TypefaceFontName=TEXT("Bold");Text->SetFont(Font);
		UCanvasPanelSlot* Slot=MinimapCanvas->AddChildToCanvas(Text);Slot->SetPosition(Position);Slot->SetSize(FVector2D(24,16));
		Text->SetVisibility(Layout.bShowMinimapCardinals?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		MinimapCardinals.Add(Text);
	};
	AddCardinal(TEXT("N"),FVector2D(MapSize.X*.5f-12,2));
	AddCardinal(TEXT("E"),FVector2D(MapSize.X-24,MapSize.Y*.5f-8));
	AddCardinal(TEXT("S"),FVector2D(MapSize.X*.5f-12,MapSize.Y-16));
	AddCardinal(TEXT("W"),FVector2D(0,MapSize.Y*.5f-8));
	// One alpha mask clips the terrain, FOV and every marker to the same local
	// circle. Its high Z order also covers capture markers rebuilt later.
	MinimapCircleMaskTexture=UTexture2D::CreateTransient(256,256,PF_B8G8R8A8);
	if(MinimapCircleMaskTexture)
	{
		MinimapCircleMaskTexture->SRGB=true;MinimapCircleMaskTexture->Filter=TF_Bilinear;
		FColor* Pixels=static_cast<FColor*>(MinimapCircleMaskTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for(int32 Y=0;Y<256;++Y)for(int32 X=0;X<256;++X)
		{
			const float Distance=FVector2D(X+0.5f-128.0f,Y+0.5f-128.0f).Size();
			Pixels[Y*256+X]=FColor(3,7,12,static_cast<uint8>(FMath::RoundToInt(FMath::Clamp((Distance-125.0f)*127.5f,0.0f,255.0f))));
		}
		MinimapCircleMaskTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		MinimapCircleMaskTexture->UpdateResource();
		MinimapCircleMask=AddMinimapImage(WidgetTree,MinimapCanvas,MapSize*.5f,MapSize,FLinearColor::White);
		MinimapCircleMask->SetBrushResourceObject(MinimapCircleMaskTexture);
		CastChecked<UCanvasPanelSlot>(MinimapCircleMask->Slot)->SetZOrder(100);
	}
	if(bVideoHudArtPass)
	{
		MinimapFrame->SetVisibility(ESlateVisibility::Collapsed);
		MinimapCanvas->RemoveFromParent();
		MinimapVideoDesign=WidgetTree->ConstructWidget<USizeBox>();
		MinimapVideoDesign->SetWidthOverride(MapSize.X);MinimapVideoDesign->SetHeightOverride(MapSize.Y);
		MinimapVideoDesign->SetContent(MinimapCanvas);
		MinimapVideoFit=WidgetTree->ConstructWidget<UScaleBox>();
		MinimapVideoFit->SetStretch(EStretch::ScaleToFit);MinimapVideoFit->SetStretchDirection(EStretchDirection::Both);
		MinimapVideoFit->SetContent(MinimapVideoDesign);
		SetDesignSlot(RootCanvas->AddChildToCanvas(MinimapVideoFit),{1609,4},{308,308});
	}
	MinimapEntranceLegend=WidgetTree->ConstructWidget<UTextBlock>();
	MinimapEntranceLegend->SetText(FText::FromString(TEXT("◆ 1–4 地下入口")));
	MinimapEntranceLegend->SetJustification(ETextJustify::Center);
	MinimapEntranceLegend->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f,0.96f,1.0f)));
	{FSlateFontInfo Font=MinimapEntranceLegend->GetFont();Font.Size=10;MinimapEntranceLegend->SetFont(Font);}
	if(bVideoHudArtPass)
		SetDesignSlot(RootCanvas->AddChildToCanvas(MinimapEntranceLegend),{1609,314},{308,16});
	else
	{
		UCanvasPanelSlot* LegendSlot=RootCanvas->AddChildToCanvas(MinimapEntranceLegend);
		LegendSlot->SetAnchors(FAnchors(.98f,.03f));LegendSlot->SetAlignment(FVector2D(1,0));
		LegendSlot->SetPosition(FVector2D(0,MapSize.Y+3));LegendSlot->SetSize(FVector2D(MapSize.X,16));
	}
	MinimapEntranceLegend->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHUDWidget::UpdateMinimapLayout(const FVector2D& CanvasSize)
{
	if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapCanvas->Slot))LayoutSlot->SetSize(CanvasSize);
	if(MinimapFrame)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapFrame->Slot))LayoutSlot->SetSize(CanvasSize+FVector2D(12));
	if(MinimapVideoDesign)
	{
		MinimapVideoDesign->SetWidthOverride(CanvasSize.X);
		MinimapVideoDesign->SetHeightOverride(CanvasSize.Y);
		const FVector2D VideoSize=CanvasSize*(308.0f/MSHUDPalette::MinimapSize);
		VideoMinimapFrameHeightPx=VideoSize.Y+7.0f;
		const float TaskTop=VideoMinimapFrameHeightPx+(bMinimapOverviewMode?21.0f:3.0f);
		if(MinimapVideoFit)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapVideoFit->Slot))
			SetDesignSlot(LayoutSlot,{1609,4},VideoSize);
		if(MinimapEntranceLegend)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapEntranceLegend->Slot))
			SetDesignSlot(LayoutSlot,{1609,VideoMinimapFrameHeightPx+2.0f},{VideoSize.X,16});
		if(TaskPanelVideoFit)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(TaskPanelVideoFit->Slot))
			SetDesignSlot(LayoutSlot,{1664,TaskTop},{256,30});
		if(SettingsButtonVideoFit)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(SettingsButtonVideoFit->Slot))
			SetDesignSlot(LayoutSlot,{1844,TaskTop+35.0f},{72,22});
	}
	else if(MinimapEntranceLegend)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapEntranceLegend->Slot))
	{
		LayoutSlot->SetPosition(FVector2D(0,CanvasSize.Y+3));LayoutSlot->SetSize(FVector2D(CanvasSize.X,16));
	}
	if(!MinimapVideoDesign&&TaskPanel)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(TaskPanel->Slot))
		LayoutSlot->SetPosition(FVector2D(0,CanvasSize.Y+8));
	if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapImage->Slot))
	{
		LayoutSlot->SetPosition(CanvasSize*.5f);LayoutSlot->SetSize(CanvasSize);
	}
	if(MinimapRadarRangeLayer)if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapRadarRangeLayer->Slot))LayoutSlot->SetSize(CanvasSize);
	const FVector2D CardinalPositions[]={
		{CanvasSize.X*.5f-12,2},{CanvasSize.X-24,CanvasSize.Y*.5f-8},
		{CanvasSize.X*.5f-12,CanvasSize.Y-16},{0,CanvasSize.Y*.5f-8}};
	for(int32 Index=0;Index<MinimapCardinals.Num()&&Index<4;++Index)
		if(UCanvasPanelSlot* LayoutSlot=Cast<UCanvasPanelSlot>(MinimapCardinals[Index]->Slot))LayoutSlot->SetPosition(CardinalPositions[Index]);
}

void UMSHUDWidget::ToggleMinimapMode()
{
	bMinimapOverviewMode=!bMinimapOverviewMode;
	if(MinimapCircleMask)MinimapCircleMask->SetVisibility(bMinimapOverviewMode?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
	if(MinimapCapture)MinimapCapture->bCaptureEveryFrame=!bMinimapOverviewMode;
}

void UMSHUDWidget::EnsureMinimapCapture(AMSPlayerCharacter* Player)
{
	if(!IsValid(Player)||MinimapCapturePlayer.Get()==Player&&IsValid(MinimapCapture))return;
	if(MinimapCapture){MinimapCapture->TextureTarget=nullptr;MinimapCapture->DestroyComponent();MinimapCapture=nullptr;}
	if(!MinimapRenderTarget)
	{
		MinimapRenderTarget=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
		MinimapRenderTarget->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
		MinimapRenderTarget->ClearColor=FLinearColor(.01f,.015f,.02f,1);
		MinimapRenderTarget->InitAutoFormat(256,256);MinimapRenderTarget->UpdateResourceImmediate(true);
		MinimapImage->SetBrushResourceObject(MinimapRenderTarget);
	}
	MinimapCapture=NewObject<USceneCaptureComponent2D>(Player,NAME_None,RF_Transient);
	Player->AddInstanceComponent(MinimapCapture);MinimapCapture->SetupAttachment(Player->GetRootComponent());
	MinimapCapture->ProjectionType=ECameraProjectionMode::Orthographic;MinimapCapture->OrthoWidth=MSHUDPalette::MinimapOrthoWidth;
	MinimapCapture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;MinimapCapture->TextureTarget=MinimapRenderTarget;
	MinimapCapture->bCaptureEveryFrame=true;MinimapCapture->bCaptureOnMovement=false;MinimapCapture->MaxViewDistanceOverride=20000.0f*MSGO::MSScale;
	// UE has no runtime layer matching Unity's Minimap layer. Capturing the normal scene keeps terrain/floor readable;
	// units and pickups are intentionally represented by UMG overlays, avoiding per-frame ShowOnlyActors maintenance.
	MinimapCapture->RegisterComponent();MinimapCapturePlayer=Player;
}

void UMSHUDWidget::RefreshMinimapPickups()
{
	MinimapPickupActors.Reset();UWorld* World=GetWorld();if(!World)return;
	TArray<AActor*> Found;UGameplayStatics::GetAllActorsOfClass(World,AMSRepairPod::StaticClass(),Found);
	for(AActor* Actor:Found)if(IsValid(Actor))MinimapPickupActors.Add(Actor);
	Found.Reset();UGameplayStatics::GetAllActorsOfClass(World,AMSSupplyPack::StaticClass(),Found);
	for(AActor* Actor:Found)if(IsValid(Actor))MinimapPickupActors.Add(Actor);
}

void UMSHUDWidget::RefreshMinimapCapturePoints()
{
	UWorld* World=GetWorld();if(!World||!MinimapCanvas)return;
	TArray<TObjectPtr<AMSCapturePoint>> FoundPoints;
	for(TActorIterator<AMSCapturePoint> It(World);It;++It)FoundPoints.Add(*It);
	FoundPoints.Sort([](const AMSCapturePoint& A,const AMSCapturePoint& B)
	{
		const int32 NameOrder=A.GetPointName().ToString().Compare(B.GetPointName().ToString(),ESearchCase::IgnoreCase);
		return NameOrder!=0?NameOrder<0:A.GetName()<B.GetName();
	});
	bool bUnchanged=FoundPoints.Num()==MinimapCapturePoints.Num();
	for(int32 Index=0;bUnchanged&&Index<FoundPoints.Num();++Index)bUnchanged=FoundPoints[Index]==MinimapCapturePoints[Index];
	if(bUnchanged)return;
	for(UCanvasPanel* Marker:MinimapCapturePointMarkers)if(Marker)Marker->RemoveFromParent();
	MinimapCapturePoints=MoveTemp(FoundPoints);MinimapCapturePointMarkers.Reset();MinimapCapturePointShapes.Reset();MinimapCaptureRoleBubbles.Reset();
	for(int32 Index=0;Index<MinimapCapturePoints.Num();++Index)
	{
		AMSCapturePoint* Point=MinimapCapturePoints[Index];const bool bHomeBase=Point->IsA<AMSHomeBase>();
		const float FullMapSize=bHomeBase?MSMapPalette::HomeBaseMarkerSizePx:MSMapPalette::CapturePointMarkerSizePx;
		const FMSMapCapturePointMarker Marker=UMSMapWidgetBase::ConstructCapturePointMarker(WidgetTree,MinimapCanvas,
			FVector2D::ZeroVector,FullMapSize*.72f,UMSMapWidgetBase::ResolveCapturePointNumber(Point,Index),bHomeBase);
		if(Marker.Root)Marker.Root->SetVisibility(ESlateVisibility::Collapsed);
		UTextBlock* RoleBubble=WidgetTree->ConstructWidget<UTextBlock>();RoleBubble->SetText(FText::FromString(TEXT("防衛")));RoleBubble->SetJustification(ETextJustify::Center);RoleBubble->SetColorAndOpacity(FLinearColor::White);RoleBubble->SetVisibility(ESlateVisibility::Collapsed);RoleBubble->SetClipping(EWidgetClipping::ClipToBounds);
		{FSlateFontInfo Font=RoleBubble->GetFont();Font.Size=8;Font.TypefaceFontName=TEXT("Bold");RoleBubble->SetFont(Font);}
		UCanvasPanelSlot* BubbleSlot=Marker.Root->AddChildToCanvas(RoleBubble);BubbleSlot->SetPosition(FVector2D(-16,-22));BubbleSlot->SetSize(FVector2D(32,14));BubbleSlot->SetZOrder(5);
		MinimapCapturePointMarkers.Add(Marker.Root);MinimapCapturePointShapes.Add(Marker.Shape);MinimapCaptureRoleBubbles.Add(RoleBubble);
	}
}

void UMSHUDWidget::UpdateMinimap(const float DeltaTime)
{
	AMSPlayerCharacter* Player=Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());if(!IsValid(Player))return;
	UMSMapCaptureSubsystem* Map=GetWorld()?GetWorld()->GetSubsystem<UMSMapCaptureSubsystem>():nullptr;
	const FVector2D MapSize(MSHUDPalette::MinimapSize);
	bMinimapUndergroundLayer=Map&&Map->GetUndergroundOverviewTexture()&&
		Map->ShouldUseUndergroundLayer(Player->GetActorLocation(),bMinimapUndergroundLayer);
	const FVector2D CanvasSize=bMinimapOverviewMode&&Map
		?GetOverviewMinimapCanvasSize(Map->GetMapWorldHalfExtents()):MapSize;
	UpdateMinimapLayout(CanvasSize);
	UTexture2D* UndergroundTexture=bMinimapUndergroundLayer&&Map?Map->GetUndergroundOverviewTexture():nullptr;
	if(!bMinimapOverviewMode&&!UndergroundTexture)EnsureMinimapCapture(Player);
	UTexture* DisplayTexture=UndergroundTexture?static_cast<UTexture*>(UndergroundTexture):
		(bMinimapOverviewMode?static_cast<UTexture*>(Map?Map->GetOverviewTexture():nullptr):MinimapRenderTarget.Get());
	if(DisplayTexture && MinimapImage->GetBrush().GetResourceObject()!=DisplayTexture)
		MinimapImage->SetBrushResourceObject(DisplayTexture);
	// The authored underground sheet and the live local capture share the overview's north-up frame.
	if(UndergroundTexture&&!bMinimapOverviewMode&&Map)
	{
		FSlateBrush Brush=MinimapImage->GetBrush();
		Brush.SetUVRegion(UMSMapWidgetBase::GetLocalAtlasUVRegion(Player->GetActorLocation(),
			Map->GetMapWorldCenter(),Map->GetMapWorldHalfExtents(),MSHUDPalette::MinimapOrthoWidth));
		MinimapImage->SetBrush(Brush);
	}
	else
	{
		FSlateBrush Brush=MinimapImage->GetBrush();
		Brush.SetUVRegion(FBox2d(FVector2d(0,0),FVector2d(1,1)));
		MinimapImage->SetBrush(Brush);
	}
	MinimapImage->SetRenderTransformAngle(0.0f);
	if(MinimapCapture)MinimapCapture->bCaptureEveryFrame=!bMinimapOverviewMode&&!UndergroundTexture;
	if(MinimapCapture && !bMinimapOverviewMode && !UndergroundTexture)
	{
		MinimapCapture->bCaptureEveryFrame=true;
		MinimapCapture->SetWorldLocation(Player->GetActorLocation()+FVector(0,0,MSHUDPalette::MinimapCaptureHeight));
		MinimapCapture->SetWorldRotation(FRotator(-90,MSMapPalette::WorldNorthYawDeg,0));
	}
	if(MinimapRadarRangeLayer)
	{
		static const TArray<FMSMapRadarRange> NoRadars;
		const AMSPlayerController* RadarController=Cast<AMSPlayerController>(GetOwningPlayer());
		const TArray<FMSMapRadarRange>& Radars=RadarController&&RadarController->AreMapContactsFresh()
			?RadarController->GetMapRadarRanges():NoRadars;
		const FGenericTeamId ViewerTeam=UMSTeamStatics::GetTeamId(Player);
		if(bMinimapOverviewMode&&Map)
			MinimapRadarRangeLayer->SetOverviewView(Map->GetMapWorldCenter(),
				Map->GetMapWorldHalfExtents(),CanvasSize,ViewerTeam,Radars);
		else MinimapRadarRangeLayer->SetLocalView(Player->GetActorLocation(),
			MSHUDPalette::MinimapOrthoWidth,CanvasSize,ViewerTeam,Radars);
	}
	auto Project=[&](const FVector& WorldPosition)
	{
		if(!bMinimapOverviewMode || !Map)
			return UMSHUDViewModel::ProjectWorldToMinimap(WorldPosition,Player->GetActorLocation(),MSHUDPalette::MinimapOrthoWidth,CanvasSize);
		FMSHUDMinimapProjection Result;
		const FVector2D Extents=Map->GetMapWorldHalfExtents();
		const FVector Relative=(WorldPosition-Map->GetMapWorldCenter()).RotateAngleAxis(-MSMapPalette::WorldNorthYawDeg,FVector::UpVector);
		Result.bInBounds=FMath::Abs(Relative.X)<=Extents.X&&FMath::Abs(Relative.Y)<=Extents.Y;
		Result.Position=UMSMapWidgetBase::WorldToMapLocalRectCentered(WorldPosition,Map->GetMapWorldCenter(),Extents,CanvasSize,0.0f);
		return Result;
	};
	auto IsVisible=[this,CanvasSize](const FMSHUDMinimapProjection& Projection)
	{
		return Projection.bInBounds && (bMinimapOverviewMode ||
			UMSHUDViewModel::IsInsideLocalMinimapCircle(Projection,CanvasSize));
	};
	const float PlayerYaw=Player->GetActorRotation().Yaw;
	MinimapPlayerMarker->SetRenderTransformAngle(PlayerYaw-MSMapPalette::WorldNorthYawDeg);
	CastChecked<UCanvasPanelSlot>(MinimapPlayerMarker->Slot)->SetPosition(Project(Player->GetActorLocation()).Position);
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	for(int32 I=0;I<MinimapFOVRays.Num();++I){MinimapFOVRays[I]->SetRenderTransformAngle(PlayerYaw-MSMapPalette::WorldNorthYawDeg+(I-3)*10.0f);MinimapFOVRays[I]->SetVisibility(bMinimapOverviewMode?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}

	int32 EnemyIndex=0;
	const AMSPlayerController* ContactController=Cast<AMSPlayerController>(GetOwningPlayer());
	const bool bFreshContacts=ContactController&&ContactController->AreMapContactsFresh();
	if(bFreshContacts)for(const FMSMapFriendlyContact& Friendly:ContactController->GetMapFriendlyContacts())
	{
		if(EnemyIndex>=MinimapUnitMarkers.Num())break;
		if(Friendly.UnitId==0||Friendly.UnitId==Player->GetUniqueID()||Friendly.Location.ContainsNaN())continue;
		const FMSHUDMinimapProjection Projected=Project(Friendly.Location);
		if(!IsVisible(Projected))continue;
		UImage* Marker=MinimapUnitMarkers[EnemyIndex++];
		Marker->SetColorAndOpacity(UMSMapWidgetBase::FriendlyDotColor(Friendly.BotTier));
		Marker->SetRenderTransformAngle(Layout.bDifferentiateMinimapMarkers?45.0f:0.0f);
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);
	}
	else if(UMSUnitRegistrySubsystem* Registry=UnitRegistry.Get())Registry->ForEachRegisteredUnit([&](AMSUnitBase* Unit)
	{
		if(EnemyIndex>=MinimapUnitMarkers.Num()||Unit==Player)return;
		const FMSHUDMinimapUnitMarker Style=UMSHUDViewModel::MakeMinimapUnitMarker(UMSTeamStatics::GetTeamId(Player),UMSTeamStatics::GetTeamId(Unit));
		const UAbilitySystemComponent* ASC=Unit->GetAbilitySystemComponent();if(!Style.bVisible||(ASC&&ASC->HasMatchingGameplayTag(MSTags::State_Dead)))return;
		if(!Style.bFriendly)return;
		const FMSHUDMinimapProjection Projected=Project(Unit->GetActorLocation());
		if(!IsVisible(Projected))return;UImage* Marker=MinimapUnitMarkers[EnemyIndex++];Marker->SetColorAndOpacity(UMSMapWidgetBase::FriendlyDotColor(255));Marker->SetRenderTransformAngle(Layout.bDifferentiateMinimapMarkers&&Style.bFriendly?45.0f:0.0f);Marker->SetVisibility(ESlateVisibility::HitTestInvisible);CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);
	});
	if (bFreshContacts) for (const FMSMapContact& Contact : ContactController->GetMapContacts())
	{
		if (EnemyIndex >= MinimapUnitMarkers.Num()) break;
		if (Contact.ContactId == 0 || Contact.LastKnownLocation.ContainsNaN()) continue;
		const FMSHUDMinimapProjection Projected = Project(Contact.LastKnownLocation);
		if (!IsVisible(Projected)) continue;
		UImage* Marker = MinimapUnitMarkers[EnemyIndex++];
		Marker->SetColorAndOpacity(MSMapPalette::Enemy);
		Marker->SetRenderTransformAngle(0.0f);
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
		CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);
	}
	for(int32 I=EnemyIndex;I<MinimapUnitMarkers.Num();++I)MinimapUnitMarkers[I]->SetVisibility(ESlateVisibility::Collapsed);
	const TArray<FMSMapEntranceLocation> Entrances=UMSMapWidgetBase::GatherEntranceLocations(GetWorld(),bMinimapUndergroundLayer);
	for(int32 Index=0;Index<MinimapEntranceMarkers.Num();++Index)
	{
		UCanvasPanel* Marker=MinimapEntranceMarkers[Index];
		const FMSMapEntranceLocation* Entrance=Entrances.FindByPredicate(
			[Index](const FMSMapEntranceLocation& Entry){return Entry.Number==Index+1;});
		if(!Marker||!Entrance){if(Marker)Marker->SetVisibility(ESlateVisibility::Collapsed);continue;}
		const FMSHUDMinimapProjection Projected=Project(Entrance->Location);
		if(!IsVisible(Projected)){Marker->SetVisibility(ESlateVisibility::Collapsed);continue;}
		if(UImage* Shape=MinimapEntranceShapes.IsValidIndex(Index)?MinimapEntranceShapes[Index]:nullptr)
			Shape->SetColorAndOpacity(bMinimapUndergroundLayer?FLinearColor(1.0f,.73f,.18f):FLinearColor(.12f,.86f,1.0f));
		CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);
		Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if(MinimapEntranceLegend)
	{
		MinimapEntranceLegend->SetText(FText::FromString(bMinimapUndergroundLayer
			?TEXT("◆ 1–4 地上出口"):TEXT("◆ 1–4 地下入口")));
		MinimapEntranceLegend->SetVisibility(bMinimapOverviewMode&&!Entrances.IsEmpty()
			?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}

	MinimapPickupPollAccumulator+=FMath::Max(0.0f,DeltaTime);
	if(MinimapPickupPollAccumulator>=MSHUDPalette::MinimapPickupPollInterval){MinimapPickupPollAccumulator=FMath::Fmod(MinimapPickupPollAccumulator,MSHUDPalette::MinimapPickupPollInterval);RefreshMinimapPickups();RefreshMinimapCapturePoints();}
	const FGenericTeamId ViewerTeam=UMSTeamStatics::GetTeamId(Player);
	for(int32 Index=0;Index<MinimapCapturePoints.Num();++Index)
	{
		AMSCapturePoint* Point=MinimapCapturePoints[Index];UCanvasPanel* Marker=MinimapCapturePointMarkers.IsValidIndex(Index)?MinimapCapturePointMarkers[Index]:nullptr;
		if(!IsValid(Point)||!Marker)continue;
		const FMSHUDMinimapProjection Projected=Project(Point->GetActorLocation());
		if(!IsVisible(Projected)){Marker->SetVisibility(ESlateVisibility::Collapsed);continue;}
		if(UImage* Shape=MinimapCapturePointShapes.IsValidIndex(Index)?MinimapCapturePointShapes[Index]:nullptr)Shape->SetColorAndOpacity(UMSMapWidgetBase::ResolveCapturePointColor(Point,ViewerTeam));
		if(UTextBlock* Bubble=MinimapCaptureRoleBubbles.IsValidIndex(Index)?MinimapCaptureRoleBubbles[Index]:nullptr)Bubble->SetVisibility(Layout.bShowMinimapRoleBubbles&&Point->GetOwnerTeamId()==ViewerTeam?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	int32 PickupIndex=0;
	for(AActor* Pickup:MinimapPickupActors)
	{
		if(PickupIndex>=MinimapPickupMarkers.Num()||!IsValid(Pickup))continue;
		const FMSHUDMinimapProjection Projected=Project(Pickup->GetActorLocation());
		if(!IsVisible(Projected))continue;UImage* Marker=MinimapPickupMarkers[PickupIndex++];const bool bRepair=Pickup->IsA<AMSRepairPod>();Marker->SetColorAndOpacity(Layout.bDifferentiateMinimapMarkers?(bRepair?MSHUDPalette::MinimapFriendly:MSHUDPalette::MinimapAmmoPickup):MSHUDPalette::MinimapPickup);Marker->SetRenderTransformAngle(Layout.bDifferentiateMinimapMarkers?(bRepair?45.0f:0.0f):45.0f);Marker->SetVisibility(ESlateVisibility::HitTestInvisible);CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetPosition(Projected.Position);
	}
	for(int32 I=PickupIndex;I<MinimapPickupMarkers.Num();++I)MinimapPickupMarkers[I]->SetVisibility(ESlateVisibility::Collapsed);
}

void UMSHUDWidget::SubscribeToUnit(AMSUnitBase* Unit)
{
	if(IsValid(Unit))if(UMSAttributeSet_Vital* Vital=Unit->GetVitalSet())Vital->OnDeath.AddUniqueDynamic(this,&UMSHUDWidget::HandleUnitDeath);
}

void UMSHUDWidget::HandleUnitDeath(AActor* DeadUnit,AActor*)
{
	if(!IsValid(DeadUnit))return;
	// BOT等は表示名(ガンオンN)を優先し、無ければアクター名にフォールバック(タスク#145)
	const AMSUnitBase* DeadMSUnit=Cast<AMSUnitBase>(DeadUnit);
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();FText EventTime;
	if(Layout.bShowTimedBattleLog)if(const AMSGameState* State=GetWorld()?GetWorld()->GetGameState<AMSGameState>():nullptr)EventTime=State->GetFormattedRemainingTime();
	const bool bPlayerDeath=Layout.bShowTimedBattleLog?DeadUnit==GetOwningPlayerPawn():DeadUnit->IsA<AMSPlayerCharacter>();
	FMSHUDKillLog::AddEntry(KillLogEntries,FMSHUDKillLog::MakeTimedEntry(EventTime,DeadMSUnit?DeadMSUnit->GetUnitDisplayName():DeadUnit->GetName(),bPlayerDeath));
	if(DeadUnit==GetOwningPlayerPawn()&&Layout.bShowSystemDownWarning)SystemDownRemaining=3.0f;
}

void UMSHUDWidget::HandleSettingsButtonClicked()
{
	if (AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(GetOwningPlayerPawn())) Player->OpenSettingsScreen();
}

void UMSHUDWidget::RebuildWeaponCards(int32 Count)
{
	WeaponPanel->ClearChildren();
	for(const TObjectPtr<UCanvasPanel>& Card:VideoWeaponCards)if(Card)Card->RemoveFromParent();
	VideoWeaponCards.Reset();
	WeaponNames.Reset();WeaponFireModes.Reset();WeaponSelectionMarkers.Reset();WeaponIcons.Reset();WeaponStatuses.Reset();WeaponReloads.Reset();WeaponCardFrames.Reset();WeaponCardSelections.Reset();
	VideoWeaponColors.SetNum(Count);VideoWeaponSelections.SetNumZeroed(Count);VideoWeaponReloading.SetNumZeroed(Count);
	if(bVideoHudArtPass)
	{
		// Active-battle reference: three narrow, name-free instrument rows on the right edge.
		// Measurements are from video1-t0500/video3-t0180/video4-t0600 at 1920x1080.
		WeaponPanel->SetVisibility(ESlateVisibility::Collapsed);
		for(int32 I=0;I<Count;++I)
		{
			UCanvasPanel* Card=WidgetTree->ConstructWidget<UCanvasPanel>();
			SetDesignSlot(RootCanvas->AddChildToCanvas(Card),
				{VideoWeaponX,VideoWeaponY+VideoWeaponStep*I},{VideoWeaponWidth,VideoWeaponHeight});
			VideoWeaponCards.Add(Card);
			UBorder* CardFrame=WidgetTree->ConstructWidget<UBorder>();
			FSlateBrush Clear;Clear.DrawAs=ESlateBrushDrawType::NoDrawType;CardFrame->SetBrush(Clear);CardFrame->SetBrushColor(FLinearColor::Transparent);CardFrame->SetPadding(FMargin(0.0f));
			UCanvasPanelSlot* FrameSlot=Card->AddChildToCanvas(CardFrame);FrameSlot->SetAnchors(FAnchors(0,0,1,1));FrameSlot->SetOffsets(FMargin(0.0f));FrameSlot->SetAlignment(FVector2D::ZeroVector);CardFrame->SetVisibility(ESlateVisibility::HitTestInvisible);WeaponCardFrames.Add(CardFrame);
			WeaponCardSelections.Add(false);
			auto Add=[this,Card](const FVector2D& Pos,const FVector2D& Size,const float FontSize)
			{
				UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>();
				FSlateFontInfo Font=T->GetFont();Font.Size=FontSize;Font.TypefaceFontName=TEXT("Bold");T->SetFont(Font);T->SetClipping(EWidgetClipping::ClipToBounds);
				SetRelativeSlot(Card->AddChildToCanvas(ScaleVideoLabel(WidgetTree,T,Size)),Pos,Size,{VideoWeaponWidth,VideoWeaponHeight});return T;
			};
			UTextBlock* FireMode=Add({5,6},{26,16},9);FireMode->SetColorAndOpacity(MSHUDPalette::HUDAccent);FireMode->SetJustification(ETextJustify::Left);WeaponFireModes.Add(FireMode);
			UTextBlock* Name=Add({4,2},{VideoWeaponWidth-8,14},10);Name->SetVisibility(ESlateVisibility::Collapsed);WeaponNames.Add(Name);
			UImage* Selection=WidgetTree->ConstructWidget<UImage>();Selection->SetVisibility(ESlateVisibility::Collapsed);WeaponSelectionMarkers.Add(Selection);Card->AddChildToCanvas(Selection);
			UBorder* IconFrame=WidgetTree->ConstructWidget<UBorder>();IconFrame->SetBrush(Clear);IconFrame->SetBrushColor(FLinearColor::Transparent);IconFrame->SetPadding(FMargin(0.0f));
			SetRelativeSlot(Card->AddChildToCanvas(IconFrame),{80,31},{82,24},{VideoWeaponWidth,VideoWeaponHeight});
			UMSWeaponIconWidget* IconWidget=WidgetTree->ConstructWidget<UMSWeaponIconWidget>();IconFrame->SetContent(IconWidget);WeaponIcons.Add(IconWidget);
			UTextBlock* Status=Add({72,1},{95,24},13);Status->SetJustification(ETextJustify::Right);Status->SetColorAndOpacity(FLinearColor::White);WeaponStatuses.Add(Status);
			UProgressBar* Reload=WidgetTree->ConstructWidget<UProgressBar>();SetRelativeSlot(Card->AddChildToCanvas(Reload),{37,51},{128,3},{VideoWeaponWidth,VideoWeaponHeight});Reload->SetFillColorAndOpacity(MSHUDPalette::Reload);Reload->SetVisibility(ESlateVisibility::Collapsed);WeaponReloads.Add(Reload);
		}
		return;
	}
	WeaponPanel->SetVisibility(ESlateVisibility::Visible);
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	const float ContentWidth=Layout.WeaponPanelWidth-10.0f;
	// 文字重なり対策(実プレイ報告バグ): 旧実装はTextBlockのフォントを未指定(UMG既定24pt)のまま
	// 高さ17-20pxの枠へ流し込んでいたため、武器名とアイコン/残弾行が互いに重なって描画されていた。
	// 枠高に収まるフォントを明示し、残弾テキストはRenderTranslationではなくCanvas座標で
	// アイコンの右(x=48)へ配置する。
	for(int32 I=0;I<Count;++I)
	{
		USizeBox* CardSize=WidgetTree->ConstructWidget<USizeBox>();CardSize->SetHeightOverride(Layout.WeaponSlotHeight);
		UBorder* CardFrame=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(CardFrame,MSUIStyle::EBrush::WeaponSlot,FLinearColor::Transparent);CardFrame->SetPadding(Layout.WeaponCardPadding);WeaponCardFrames.Add(CardFrame);
		WeaponCardSelections.Add(false);
		UCanvasPanel* Card=WidgetTree->ConstructWidget<UCanvasPanel>();CardFrame->SetContent(Card);CardSize->SetContent(CardFrame);
		auto* VS=WeaponPanel->AddChildToVerticalBox(CardSize);VS->SetPadding(FMargin(0,0,0,Layout.WeaponSlotSpacing));VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		auto Add=[&](const FVector2D& Pos,const FVector2D& Size,const float FontSize){UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>();FSlateFontInfo Font=T->GetFont();Font.Size=FontSize;T->SetFont(Font);auto* S=Card->AddChildToCanvas(T);S->SetPosition(Pos);S->SetSize(Size);return T;};
		// Generated bitmap avoids relying on the runtime font's optional U+25B6 glyph.
		UImage* Selection=WidgetTree->ConstructWidget<UImage>();Selection->SetBrush(MSUIStyle::GetBrush(MSUIStyle::EBrush::WeaponSelectionMarker,FLinearColor::Transparent));Selection->SetVisibility(ESlateVisibility::Collapsed);auto* SelectionSlot=Card->AddChildToCanvas(Selection);SelectionSlot->SetPosition({5,21});SelectionSlot->SetSize({16,20});SelectionSlot->SetZOrder(10);WeaponSelectionMarkers.Add(Selection);
		const float FireModeX=4.0f;
		UTextBlock* FireMode=Add({FireModeX,2},{34,18},MSHUDPalette::WeaponStatusFontSize);FireMode->SetJustification(ETextJustify::Center);FireMode->SetColorAndOpacity(MSHUDPalette::HUDAccent);FireMode->SetVisibility(Layout.bShowWeaponFireMode?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);WeaponFireModes.Add(FireMode);
		const float NameX=Layout.bShowWeaponFireMode?FireModeX+38.0f:4.0f;
		WeaponNames.Add(Add({NameX,2},{ContentWidth-NameX,18},MSHUDPalette::WeaponNameFontSize));
		UBorder* IconFrame=WidgetTree->ConstructWidget<UBorder>();MSUIStyle::ApplyBrush(IconFrame,MSUIStyle::EBrush::ListRow,FLinearColor(.04f,.1f,.14f,.95f));IconFrame->SetPadding(FMargin(3,1));
		auto* IconSlot=Card->AddChildToCanvas(IconFrame);IconSlot->SetPosition({4,22});IconSlot->SetSize({40,19});
		// 実プレイ報告: アルファベット略号→武器カテゴリ別シルエット(色は従来のカテゴリ色を維持)
		UMSWeaponIconWidget* IconWidget=WidgetTree->ConstructWidget<UMSWeaponIconWidget>();IconFrame->SetContent(IconWidget);WeaponIcons.Add(IconWidget);
		WeaponStatuses.Add(Add({48,23},{ContentWidth-48,17},MSHUDPalette::WeaponStatusFontSize));
		UProgressBar* R=WidgetTree->ConstructWidget<UProgressBar>();auto* RS=Card->AddChildToCanvas(R);RS->SetPosition({0,Layout.WeaponSlotHeight-8});RS->SetSize({ContentWidth,4});R->SetFillColorAndOpacity(MSHUDPalette::Reload);WeaponReloads.Add(R);
		UImage* SlotRule=WidgetTree->ConstructWidget<UImage>();FSlateBrush SlotRuleBrush;SlotRuleBrush.DrawAs=ESlateBrushDrawType::Box;SlotRuleBrush.TintColor=FLinearColor(.02f,.35f,.42f,1.0f);SlotRule->SetBrush(SlotRuleBrush);SlotRule->SetVisibility(Layout.bShowDensityInstrumentRules?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);auto* SlotRuleSlot=Card->AddChildToCanvas(SlotRule);SlotRuleSlot->SetPosition({0,Layout.WeaponSlotHeight-5});SlotRuleSlot->SetSize({ContentWidth,4});
		FSlateBrush SlotRailBrush=SlotRuleBrush;SlotRailBrush.TintColor=MSHUDPalette::HUDAccent;
		UImage* SlotRail=WidgetTree->ConstructWidget<UImage>();SlotRail->SetBrush(SlotRailBrush);SlotRail->SetVisibility(Layout.bShowDensityInstrumentRules?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);auto* SlotRailSlot=Card->AddChildToCanvas(SlotRail);SlotRailSlot->SetPosition({ContentWidth-4,4});SlotRailSlot->SetSize({3,Layout.WeaponSlotHeight-10});
		UImage* SlotTick=WidgetTree->ConstructWidget<UImage>();SlotTick->SetBrush(SlotRailBrush);SlotTick->SetRenderOpacity(.72f);SlotTick->SetVisibility(Layout.bShowDensityInstrumentRules?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);auto* SlotTickSlot=Card->AddChildToCanvas(SlotTick);SlotTickSlot->SetPosition({ContentWidth-11,10});SlotTickSlot->SetSize({4,Layout.WeaponSlotHeight-22});
	}
}

void UMSHUDWidget::RefreshGaugeWidgets()
{
	if(!ViewModel||!BoostBar)return;
	const FMSHUDGaugeSnapshot Gauges=ViewModel->GetGauges();
	VideoGaugeSnapshot=Gauges;
	const bool bOverheated=Gauges.BoostColor==MSHUDPalette::BoostOverheated;
	const bool bRecovering=Gauges.BoostColor==MSHUDPalette::BoostRecovering;
	BoostBar->SetPercent(Gauges.Boost);BoostBar->SetFillColorAndOpacity(bRecovering?MSHUDPalette::BoostFill:Gauges.BoostColor);
	if(UTextBlock* Status=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoostStatusText"))))
	{
		Status->SetVisibility(bOverheated||bRecovering?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		Status->SetText(FText::FromString(bOverheated?TEXT("OVER HEAT"):TEXT("RECOVERY")));
		Status->SetColorAndOpacity(bOverheated?MSHUDPalette::BoostOverheated:MSHUDPalette::BoostRecovering);
	}
	ArmorBar->SetPercent(Gauges.Armor);ArmorBar->SetFillColorAndOpacity(Gauges.Armor<=MSHUDPalette::ArmorLowThreshold?MSHUDPalette::ArmorLow:MSHUDPalette::ArmorFill);ArmorValueText->SetColorAndOpacity(Gauges.Armor<=MSHUDPalette::ArmorLowThreshold?MSHUDPalette::ArmorLow:(bVideoHudArtPass?FLinearColor::White:MSHUDPalette::ArmorFill));
	const FMSHUDShieldSnapshot Shield=ViewModel->GetShield();
	VideoShieldSnapshot=Shield;
	const bool bRespawning=ViewModel->GetRespawn().bVisible;
	bShieldInstrumentVisible=Shield.bSupported&&Shield.bEquipped&&!bRespawning;
	const float ShieldRatio=Shield.bBroken?0.0f:FMath::Clamp(Shield.Ratio,0.0f,1.0f);
	const bool bLowShield=Shield.bBroken||ShieldRatio<=ShieldLowThreshold;
	const FLinearColor ShieldFill=bLowShield?MSHUDPalette::ArmorLow:MSPixel::Hex(0x38D6B5);
	if(ShieldBar){ShieldBar->SetPercent(ShieldRatio);ShieldBar->SetFillColorAndOpacity(ShieldFill);ShieldBar->SetVisibility(bShieldInstrumentVisible&&!bVideoHudArtPass?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
	if(ShieldValueText)
	{
		ShieldValueText->SetText(FText::FromString(FString::FromInt(Shield.bBroken?0:FMath::RoundToInt32(Shield.Durability))));
		ShieldValueText->SetColorAndOpacity(Shield.bBroken?MSHUDPalette::ArmorLow:FLinearColor::White);
		ShieldValueText->SetVisibility(bShieldInstrumentVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	if(ShieldText)ShieldText->SetVisibility(bShieldInstrumentVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	if(ShieldBrokenText)ShieldBrokenText->SetVisibility(bShieldInstrumentVisible&&Shield.bBroken?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	DPBar->SetPercent(Gauges.DP);DPBar->SetFillColorAndOpacity(Gauges.DPColor);
	DPStateText->SetColorAndOpacity(Gauges.DPColor);DPStateText->SetText(FText::FromString(Gauges.bGettingUp?TEXT("起き上がり"):Gauges.bKnockedDown?TEXT("転倒"):Gauges.bStaggered?TEXT("よろけ"):Gauges.bFlinched?TEXT("怯み"):TEXT("DG")));
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();
	// The video death panel replaces the legacy warning in the same screen area.
	const bool bShowingDeathPanel=bVideoHudArtPass&&bRespawning;
	if(bVideoHudArtPass)
	{
		// At f17275 the numeric HP has already disappeared; RECOVERY is the
		// remaining status over the lower-right instrument until deployment.
		ArmorValueText->SetVisibility(bShowingDeathPanel?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
		ArmorMaxText->SetVisibility(bShowingDeathPanel?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
		if(RespawnRecoveryText)RespawnRecoveryText->SetVisibility(bShowingDeathPanel?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	SystemDownText->SetVisibility(!bShowingDeathPanel&&Layout.bShowSystemDownWarning&&(Gauges.bKnockedDown||SystemDownRemaining>0.0f)
		?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void UMSHUDWidget::RefreshBattleWidgets()
{
	VideoBattleSnapshot=ViewModel?ViewModel->GetBattle():FMSHUDBattleSnapshot();
	const FMSHUDBattleSnapshot& Battle=VideoBattleSnapshot;
	if(TimeText)TimeText->SetText(Battle.TimeText);
	if(TacticalText)TacticalText->SetText(FText::FromString(FString::Printf(TEXT("%d"),Battle.Tactical)));
	auto SetRow=[this](const TCHAR* Name,UProgressBar* Bar,const FGenericTeamId Team,const float Ratio)
	{
		const FLinearColor Color=MSFactionPresentation::GaugeColor(Team);
		if(Bar){Bar->SetPercent(Ratio);Bar->SetFillColorAndOpacity(Color);}
		if(UTextBlock* Label=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Name))))
		{
			Label->SetText(FText::FromString(MSFactionPresentation::EnglishName(Team)));
			Label->SetColorAndOpacity(Color);
		}
	};
	// Rows stay ally-first; affiliation controls their label/color, never suit provenance.
	SetRow(TEXT("PlayerFactionLabel"),TeamABar,Battle.TeamAId,Battle.TeamARatio);
	SetRow(TEXT("EnemyFactionLabel"),TeamBBar,Battle.TeamBId,Battle.TeamBRatio);
}

void UMSHUDWidget::RefreshCaptureWidgets()
{
	if(!ViewModel||!CaptureRing||!CaptureText)return;
	auto Cap=ViewModel->GetCapture();CaptureRing->SetVisibility(Cap.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);CaptureText->SetVisibility(Cap.bVisible&&!Cap.bCompleted?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);CaptureRing->SetHorizontalBar(Cap.Progress,Cap.bHostileProgress?MSHUDPalette::CaptureHostileFill:MSHUDPalette::CaptureFill);CaptureText->SetText(Cap.State);
	if(CaptureCompleteText)CaptureCompleteText->SetVisibility(Cap.bVisible&&Cap.bCompleted?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void UMSHUDWidget::NativeTick(const FGeometry& G,float D)
{
	Super::NativeTick(G,D);UpdateMinimap(D);RefreshTeamRoster();SystemDownRemaining=FMath::Max(0.0f,SystemDownRemaining-D);FMSHUDKillLog::TickEntries(KillLogEntries,D);if(KillLogBackground)KillLogBackground->SetVisibility(KillLogEntries.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);for(int32 I=0;I<KillLogLines.Num();++I){const bool bVisible=KillLogEntries.IsValidIndex(I);KillLogLines[I]->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(bVisible){KillLogLines[I]->SetText(KillLogEntries[I].Text);KillLogLines[I]->SetColorAndOpacity(KillLogEntries[I].Color);}}if(!ViewModel)return;
	auto Hit=ViewModel->GetHitMarker();HitMarker->SetVisibility(Hit.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Hit.bVisible){const float Alpha=1.0f-FMath::Clamp(Hit.Elapsed/MSHUDPalette::HitMarkerDuration,0.0f,1.0f);FLinearColor Color=Hit.bHeadshot?MSHUDPalette::HitMarkerHeadshot:MSHUDPalette::HitMarkerNormal;Color.A*=Alpha;HitMarker->SetColorAndOpacity(Color);const float Scale=Hit.bHeadshot?1.35f:1.0f;HitMarker->SetRenderScale(FVector2D(Scale));}
	auto Kill=ViewModel->GetKillPopup();KillPopupText->SetVisibility(Kill.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);KillStreakText->SetVisibility(Kill.bVisible&&Kill.KillStreak>=2?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Kill.bVisible){float Alpha=1.0f,Scale=1.0f;if(Kill.Elapsed<MSHUDPalette::KillPopupPopInDuration){const float T=Kill.Elapsed/MSHUDPalette::KillPopupPopInDuration;Alpha=T;Scale=FMath::Lerp(1.4f,1.0f,T);}else if(Kill.Elapsed>MSHUDPalette::KillPopupPopInDuration+MSHUDPalette::KillPopupHoldDuration){const float FadeDuration=MSHUDPalette::KillPopupDuration-MSHUDPalette::KillPopupPopInDuration-MSHUDPalette::KillPopupHoldDuration;Alpha=1.0f-FMath::Clamp((Kill.Elapsed-MSHUDPalette::KillPopupPopInDuration-MSHUDPalette::KillPopupHoldDuration)/FadeDuration,0.0f,1.0f);}KillPopupText->SetText(Kill.Text);KillPopupText->SetColorAndOpacity(FLinearColor(1.0f,.82f,.15f,1.0f));KillPopupText->SetRenderOpacity(Alpha);KillPopupText->SetRenderScale(FVector2D(Scale));KillStreakText->SetText(FText::Format(FText::FromString(TEXT("{0}機連続撃破")),FText::AsNumber(Kill.KillStreak)));KillStreakText->SetColorAndOpacity(FLinearColor(1.0f,.45f,.15f,1.0f));KillStreakText->SetRenderOpacity(Alpha);KillStreakText->SetRenderScale(FVector2D(Scale));}
	auto Damage=ViewModel->GetDamageDirection();DamageDirectionIndicator->SetVisibility(Damage.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Damage.bVisible){const float Radians=FMath::DegreesToRadians(Damage.RelativeAngleDegrees);CastChecked<UCanvasPanelSlot>(DamageDirectionIndicator->Slot)->SetPosition(FVector2D(FMath::Sin(Radians),-FMath::Cos(Radians))*MSHUDPalette::DamageDirectionRadius);DamageDirectionIndicator->SetRenderTransformAngle(Damage.RelativeAngleDegrees);FLinearColor Color=MSHUDPalette::DamageDirection;Color.A*=1.0f-FMath::Clamp(Damage.Elapsed/MSHUDPalette::DamageDirectionDuration,0.0f,1.0f);DamageDirectionIndicator->SetColorAndOpacity(Color);}
	RefreshIncomingLockWidgets(D);
	auto Marker=ViewModel->GetLockOnMarker(); LockOnMarker->SetRedLocked(Marker.bRedLocked); LockOnMarker->SetVisibility(Marker.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed); if(Marker.bVisible)CastChecked<UCanvasPanelSlot>(LockOnMarker->Slot)->SetPosition(Marker.ScreenPosition);
	LockOnDistanceText->SetVisibility(Marker.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	if(Marker.bVisible){CastChecked<UCanvasPanelSlot>(LockOnDistanceText->Slot)->SetPosition(Marker.ScreenPosition+FVector2D(MSHUDPalette::LockOnMarkerSize*.5f+7,-19));LockOnDistanceText->SetText(FText::FromString(FString::Printf(TEXT("%dm"),FMath::RoundToInt32(Marker.DistanceMeters))));}
	auto Bars=ViewModel->GetUnitBars(); for(int32 I=0;I<UnitBars.Num();++I){const bool bVisible=Bars.IsValidIndex(I);UnitBars[I]->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(bVisible){UnitBars[I]->SetPercent(Bars[I].ArmorRatio);UnitBars[I]->SetFillColorAndOpacity(Bars[I].FillColor);CastChecked<UCanvasPanelSlot>(UnitBars[I]->Slot)->SetPosition(Bars[I].ScreenPosition);}}
	auto Module=ViewModel->GetModule();ModuleStatusText->SetVisibility(Module.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Module.bVisible)ModuleStatusText->SetText(Module.StateText);
	auto ModuleCharge=ViewModel->GetModuleCharge();ModuleChargePanel->SetVisibility(ModuleCharge.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);ModuleChargeBar->SetPercent(ModuleCharge.Progress);
	RefreshEmergencyRecallIndicator();
	RefreshRespawnPanel();
	RefreshVoiceCutIns();
	RefreshGaugeWidgets();
	const MSUIStyle::FHUDLayout Layout=MSUIStyle::GetHUDLayout();auto Slots=ViewModel->GetWeaponSlots();if(Slots.Num()!=WeaponNames.Num())RebuildWeaponCards(Slots.Num());
	const AMSPlayerCharacter* LocalPlayer=Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());
	const UMSWeaponComponent* LocalWeapons=LocalPlayer?LocalPlayer->GetWeaponComponent():nullptr;
	for(int32 I=0;I<Slots.Num();++I){WeaponNames[I]->SetText(Slots[I].Name);WeaponFireModes[I]->SetText(Slots[I].FireMode);WeaponSelectionMarkers[I]->SetVisibility(!bVideoHudArtPass&&Layout.bShowWeaponSelectionMarker&&Slots[I].bSelected?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);WeaponNames[I]->SetVisibility(bVideoHudArtPass?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);WeaponNames[I]->SetColorAndOpacity(Slots[I].bSelected?MSHUDPalette::Selected:MSHUDPalette::Unselected);if(bVideoHudArtPass){VideoWeaponSelections[I]=Slots[I].bSelected;VideoWeaponReloading[I]=Slots[I].bReloading;VideoWeaponColors[I]=Slots[I].Icon.Color;}else if(WeaponCardSelections[I]!=Slots[I].bSelected){WeaponCardSelections[I]=Slots[I].bSelected;MSUIStyle::ApplyBrush(WeaponCardFrames[I],Slots[I].bSelected?MSUIStyle::EBrush::WeaponSlotActive:MSUIStyle::EBrush::WeaponSlot,FLinearColor::Transparent);}const FLinearColor IconColor=bVideoHudArtPass?MSPixel::Hex(0x9BE1CC,.95f):Slots[I].Icon.Color;WeaponIcons[I]->SetWeapon(LocalWeapons?LocalWeapons->GetWeaponData(I):nullptr,IconColor);WeaponIcons[I]->SetVisibility(Slots[I].bHasWeapon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);const bool bShowReload=Slots[I].bReloading;const FText VideoStatus=bShowReload?FText::FromString(TEXT("RELOAD")):FText::FromString(Slots[I].Ammo.ToString().Replace(TEXT(" "),TEXT("")));WeaponStatuses[I]->SetText(bVideoHudArtPass?VideoStatus:Slots[I].Status);WeaponStatuses[I]->SetColorAndOpacity(bShowReload?MSHUDPalette::Reload:FLinearColor::White);WeaponReloads[I]->SetPercent(Slots[I].ReloadProgress);WeaponReloads[I]->SetVisibility(bVideoHudArtPass?(bShowReload?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed):(bShowReload?ESlateVisibility::Visible:ESlateVisibility::Hidden));}
	RefreshCombatWidgets();
	if(bVideoHudArtPass)
	{
		for(int32 I=0;I<WeaponFireModes.Num();++I)
		{
			UWidget* Design=WeaponFireModes[I]->GetParent();UWidget* Fit=Design?Design->GetParent():nullptr;
			if(Fit)SetRelativeSlot(Cast<UCanvasPanelSlot>(Fit->Slot),{Slots[I].bSelected?5.0:59.0,6},{26,16},{VideoWeaponWidth,VideoWeaponHeight});
		}
	}
	RefreshBattleWidgets();
	auto ArmorNum=ViewModel->GetArmorNumbers();ArmorValueText->SetText(FText::FromString(FString::Printf(TEXT("%d"),ArmorNum.Armor)));ArmorMaxText->SetText(FText::FromString(FString::Printf(TEXT("/%d"),ArmorNum.MaxArmor)));
	auto Task=ViewModel->GetTask();const FLinearColor MissionColor=Task.bDefense?MSHUDPalette::MissionDefense:MSHUDPalette::MissionAttack;
	TaskMissionText->SetText(Task.MissionType);TaskMissionText->SetColorAndOpacity(MissionColor);TaskText->SetText(Task.TaskText);
	MissionIconImage->SetColorAndOpacity(MissionColor);MissionIconText->SetText(Task.MissionType);MissionIconSubText->SetText(Task.MissionTypeEnglish);
	ScoreValueText->SetText(FText::FromString(FString::Printf(TEXT("%d PT"),ViewModel->GetScore().Score)));
	RefreshCaptureWidgets();auto Sup=ViewModel->GetSupply();SupplyText->SetVisibility(Sup.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);SupplyText->SetText(Sup.Prompt);
}

int32 UMSHUDWidget::PaintVideoBackdrop(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
	const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const
{
	if(!bVideoHudArtPass||AllottedGeometry.GetLocalSize().X<=0.0f||AllottedGeometry.GetLocalSize().Y<=0.0f)
	{
		return LayerId;
	}

	// SObjectWidget paints its children before NativePaint. This method is routed
	// through a separate, lowest-Z canvas child instead of the HUD parent.
	MSPixel::FPainter Painter(AllottedGeometry,OutDrawElements,LayerId,VideoDesignSize,InWidgetStyle.GetColorAndOpacityTint().A);
	const FLinearColor Metal=MSPixel::Hex(0x9EB7B5,.92f);
	const FLinearColor MetalDim=MSPixel::Hex(0x4D686B,.78f);
	const FLinearColor Glass=MSPixel::Hex(0x07161A,.88f);
	const FLinearColor Accent=MSPixel::Hex(0x52E8D5,.95f);

	// Top battle instrument: measured frame bounds x=520..1090, y=0..72.
	Painter.Polygon({{546,0},{1063,0},{1090,27},{1088,43},{1071,61},{1067,69},{952,69},{942,59},{669,59},{658,72},{619,72},{612,67},{550,67},{526,43},{521,27}},MSPixel::Hex(0x35474A,.95f));
	Painter.Polygon({{548,0},{1060,0},{1085,27},{1064,63},{951,63},{941,53},{665,53},{656,66},{622,66},{615,61},{552,61},{527,27}},MSPixel::Hex(0x90A6A0,.92f));
	Painter.Polygon({{551,0},{1058,0},{1078,27},{1059,57},{955,57},{944,47},{663,47},{653,60},{624,60},{617,55},{556,55},{534,27}},MSPixel::Hex(0x184D51,.96f));
	Painter.Gradient(553,30,499,22,MSPixel::Hex(0x2C797B,.75f),MSPixel::Hex(0x4CB4AA,.70f));
	Painter.Chamfer(633,2,321,47,17,Glass);
	Painter.Line({638,4},{929,4},1,Metal);Painter.Line({638,45},{935,45},1,Accent);
	Painter.Line({553,59},{611,59},1,Metal);Painter.Line({961,59},{1058,59},1,Metal);
	DrawGauge(Painter,705.0f,8.0f,225.0f,10.0f,VideoBattleSnapshot.TeamARatio,
		MSFactionPresentation::GaugeColor(VideoBattleSnapshot.TeamAId),MetalDim,false);
	DrawGauge(Painter,705.0f,30.0f,225.0f,10.0f,VideoBattleSnapshot.TeamBRatio,
		MSFactionPresentation::GaugeColor(VideoBattleSnapshot.TeamBId),MetalDim,false);
	Painter.Line({647.0f,23.0f},{935.0f,23.0f},.5f,MetalDim);
	// The local circle keeps the original square; overview trims the frame to the atlas's aspect.
	const float MapFrameHeight=VideoMinimapFrameHeightPx;
	Painter.Rect(1605,0,315,MapFrameHeight,MSPixel::Hex(0x1E2929,.90f));
	Painter.Box(1605,0,315,MapFrameHeight,1,Metal);Painter.Box(1608,3,312,MapFrameHeight-5.0f,.5f,MetalDim);
	for(const FVector2D Corner : {FVector2D(1614,8),FVector2D(1910,8),FVector2D(1614,MapFrameHeight-11.0f),FVector2D(1910,MapFrameHeight-11.0f)})
	{
		Painter.Line(Corner,Corner+FVector2D(Corner.X<1700?9:-9,0),1,Metal);
		Painter.Line(Corner,Corner+FVector2D(0,Corner.Y<100?9:-9),1,Metal);
	}
	Painter.Arc({1070,1024},34,0,2*UE_PI,2,MissionIconImage?MissionIconImage->GetColorAndOpacity():Accent);

	// Right weapon instrument: three name-free rows, with the selection arrow on the left.
	for(int32 Index=0;Index<VideoWeaponCards.Num();++Index)
	{
		const float Y=VideoWeaponY+VideoWeaponStep*Index;
		const bool bSelected=VideoWeaponSelections.IsValidIndex(Index)&&VideoWeaponSelections[Index];
		const bool bReloading=VideoWeaponReloading.IsValidIndex(Index)&&VideoWeaponReloading[Index];
		const FLinearColor Category=VideoWeaponColors.IsValidIndex(Index)?VideoWeaponColors[Index]:Accent;
		const float StartX=VideoWeaponX+(bSelected?0.f:54.f);
		Painter.Rect(StartX,Y+3.0f,1920.f-StartX,VideoWeaponHeight-6.0f,
			FLinearColor(0.015f,0.035f,0.042f,bSelected?0.48f:0.28f));
		Painter.Line({StartX+34.f,Y+24.f},{1915.f,Y+24.f},2.f,MSPixel::Hex(0xe0c863));
		FLinearColor RowAccent=bSelected?Category:Metal;RowAccent.A*=bSelected?.9f:.55f;
		Painter.Line({VideoWeaponX+37.0f,Y+VideoWeaponHeight-5.0f},{VideoWeaponX+VideoWeaponWidth-5.0f,Y+VideoWeaponHeight-5.0f},1.0f,
			bReloading?MSHUDPalette::Reload:RowAccent);
		if(bSelected)
		{
			for(int32 Chevron=0;Chevron<2;++Chevron)
			{
				const float X=VideoWeaponX+57.f+Chevron*8.f;
				Painter.Line({X+8,Y+34},{X,Y+43},2,Metal);Painter.Line({X,Y+43},{X+8,Y+52},2,Metal);
			}
		}
	}

	// Bottom-right instrument: the reference leaves the scene visible behind fine metal/gold rules;
	// there is no enclosing rectangle around either vital cluster.
	const FLinearColor Armor=VideoGaugeSnapshot.Armor<=MSHUDPalette::ArmorLowThreshold
		?MSHUDPalette::ArmorLow:MSPixel::Hex(0x23DEA0);
	DrawAngledGauge(Painter,VideoArmorX+3.f,1018.f,VideoArmorWidth-5.f,26.f,VideoGaugeSnapshot.Armor,Armor);
	if(bShieldInstrumentVisible)
	{
		// 1340 SHIELD in the reference sits above a narrow double-edged bar.
		const float Ratio=VideoShieldSnapshot.bBroken?0.0f:FMath::Clamp(VideoShieldSnapshot.Ratio,0.0f,1.0f);
		const FLinearColor ShieldFill=VideoShieldSnapshot.bBroken||Ratio<=ShieldLowThreshold
			?MSHUDPalette::ArmorLow:MSPixel::Hex(0x38D6B5);
		DrawAngledGauge(Painter,VideoShieldX,VideoShieldY,VideoShieldWidth,VideoShieldHeight,Ratio,ShieldFill);
		Painter.Line({VideoShieldX+9,VideoShieldY+VideoShieldHeight+1},{VideoShieldX+VideoShieldWidth-2,VideoShieldY+VideoShieldHeight+1},.6f,MetalDim);
	}
	// Retain the requested DP line directly above BOOST.
	DrawGauge(Painter,VideoBoostX+8.f,1007.f,VideoBoostWidth-16.f,4.0f,VideoGaugeSnapshot.DP,
		VideoGaugeSnapshot.DPColor,MetalDim,false);

	// BOOST is a separate narrow instrument immediately to the left of ARMOR.
	const FLinearColor Boost=VideoGaugeSnapshot.BoostColor==MSHUDPalette::BoostOverheated
		?MSHUDPalette::BoostOverheated:MSPixel::Hex(0xFFD020,.98f);
	DrawAngledGauge(Painter,VideoBoostX,VideoBoostY,VideoBoostWidth,20.f,VideoGaugeSnapshot.Boost,Boost);
	Painter.Finish();
	return LayerId+1;
}

void UMSHUDWidget::RefreshIncomingLockWidgets(const float DeltaTime)
{
	IncomingLockFlashTime+=FMath::Max(0.0f,DeltaTime);
	for(const TObjectPtr<UTextBlock>& Arrow:IncomingLockIndicators)if(Arrow)Arrow->SetVisibility(ESlateVisibility::Collapsed);
	for(const TObjectPtr<UTextBlock>& Distance:IncomingLockDistanceTexts)if(Distance)Distance->SetVisibility(ESlateVisibility::Collapsed);
	if(IncomingMissileWarningText)IncomingMissileWarningText->SetVisibility(ESlateVisibility::Collapsed);
	if(IncomingLockEdgeLeft)IncomingLockEdgeLeft->SetVisibility(ESlateVisibility::Collapsed);
	if(IncomingLockEdgeRight)IncomingLockEdgeRight->SetVisibility(ESlateVisibility::Collapsed);
	if(!ViewModel){bIncomingMissileWarningWasVisible=false;return;}
	const FMSHUDIncomingLockSnapshot Snapshot=ViewModel->GetIncomingLock();
	const bool bMissileWarning=Snapshot.bMissilePrelockWarning;
	if(bMissileWarning&&!bIncomingMissileWarningWasVisible)
	{
		// Type A is the authored warning; the preset resolver selects the existing
		// Type B replacement. Original timing/mix parity is not verified here.
		static TWeakObjectPtr<USoundBase> CachedLockWarningSound;
		if(!CachedLockWarningSound.IsValid())
		{
			CachedLockWarningSound=LoadObject<USoundBase>(nullptr,
				TEXT("/Game/Audio/Imported/SFX_LockOn_Warning.SFX_LockOn_Warning"));
		}
		MSAudio::PlayMetaSound2DIfLocallyControlled(GetOwningPlayerPawn(),
			MSAudioPreset::Resolve(EMSAudioUsage::LockWarning,CachedLockWarningSound.Get()),
			1450.0f,0.30f,0.18f);
	}
	bIncomingMissileWarningWasVisible=bMissileWarning;
	if(!Snapshot.bVisible)return;
	constexpr float IndicatorRadius=300.0f;
	constexpr float DistanceRadius=328.0f;
	for(int32 Index=0;Index<Snapshot.DirectionIndicators.Num()&&Index<IncomingLockIndicators.Num()&&Index<10;++Index)
	{
		const FMSHUDIncomingLockIndicatorSnapshot& Indicator=Snapshot.DirectionIndicators[Index];
		const float Radians=FMath::DegreesToRadians(Indicator.RelativeAngleDegrees);
		UTextBlock* Arrow=IncomingLockIndicators[Index];
		if(!Arrow)continue;
		CastChecked<UCanvasPanelSlot>(Arrow->Slot)->SetPosition(FVector2D(FMath::Sin(Radians),-FMath::Cos(Radians))*IndicatorRadius);
		Arrow->SetRenderTransformAngle(Indicator.RelativeAngleDegrees);Arrow->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	for(int32 Index=0;Index<Snapshot.DistanceIndicators.Num()&&Index<IncomingLockDistanceTexts.Num()&&Index<2;++Index)
	{
		const FMSHUDIncomingLockIndicatorSnapshot& Indicator=Snapshot.DistanceIndicators[Index];
		if(!Indicator.bHasDistance)continue;
		const float Radians=FMath::DegreesToRadians(Indicator.RelativeAngleDegrees);
		UTextBlock* Distance=IncomingLockDistanceTexts[Index];
		if(!Distance)continue;
		CastChecked<UCanvasPanelSlot>(Distance->Slot)->SetPosition(FVector2D(FMath::Sin(Radians),-FMath::Cos(Radians))*DistanceRadius);
		Distance->SetText(FText::FromString(FString::Printf(TEXT("%.0fm"),FMath::Max(0.0f,Indicator.DistanceMeters))));Distance->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if(IncomingMissileWarningText&&Snapshot.bMissilePrelockWarning)IncomingMissileWarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if(Snapshot.bIncomingLock)
	{
		const float Pulse=0.18f+0.42f*(0.5f+0.5f*FMath::Sin(IncomingLockFlashTime*UE_TWO_PI*2.5f));
		if(IncomingLockEdgeLeft){IncomingLockEdgeLeft->SetRenderOpacity(Pulse);IncomingLockEdgeLeft->SetVisibility(ESlateVisibility::HitTestInvisible);}
		if(IncomingLockEdgeRight){IncomingLockEdgeRight->SetRenderOpacity(Pulse);IncomingLockEdgeRight->SetVisibility(ESlateVisibility::HitTestInvisible);}
	}
}

// 誘導カーソル(大円=太線・敵在円で赤)/捕捉ターゲットリング(黄ゲージ)/チャージリングの更新。
// NativeTickと自動テスト(RefreshCombatForAutomationTests)の共通経路。
void UMSHUDWidget::RefreshCombatWidgets(const bool bForceHomingTargetProjected)
{
	// 実プレイ報告対応: カーソル付近の棒状リロードバー(CenterReload)は廃止。
	// リロード中の表示は残弾円弧の赤色化(SetAmmoArcのbReloading)に一本化する。
	auto C=ViewModel->GetCombat();ScopeOverlay->SetVisibility(C.bScoped?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);Reticle->SetVisibility(C.bScoped?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);CenterReload->SetVisibility(ESlateVisibility::Collapsed);HomingRing->SetVisibility(!C.bScoped&&!C.bSwitching?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);HomingRing->SetRing(MSHUDPalette::HomingCursorSize,C.bHoming?1.0f:0.0f,UMSHUDViewModel::SelectHomingCursorColor(C.bHomingTargetInCircle),false,MSHUDPalette::HomingCursorThickness);HomingRing->SetAmmoArc(C.AmmoArcProgress,C.bReloading,C.bAmmoArcVisible);auto HomingTarget=ViewModel->GetHomingTargetMarker();if(bForceHomingTargetProjected&&C.bHomingTargetInCircle&&!HomingTarget.bVisible){HomingTarget.bVisible=true;HomingTarget.Progress=C.DwellProgress;}const bool bShowHomingTarget=!C.bScoped&&!C.bSwitching&&C.bHoming&&HomingTarget.bVisible;HomingTargetRing->SetVisibility(bShowHomingTarget?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(bShowHomingTarget){CastChecked<UCanvasPanelSlot>(HomingTargetRing->Slot)->SetPosition(HomingTarget.ScreenPosition);HomingTargetRing->SetRing(MSHUDPalette::HomingTargetRingSize,HomingTarget.Progress,MSHUDPalette::HomingTargetProgress,true,MSHUDPalette::HomingTargetArcThickness,2.0f);}ChargeRing->SetVisibility(!C.bScoped&&!C.bSwitching&&C.bCharging?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);const bool bChargeComplete=C.ChargeProgress>=.999f;ChargeRing->SetRing(100,C.ChargeProgress,bChargeComplete?FLinearColor(.25f,1.f,.35f,1.f):FLinearColor::LerpUsingHSV(FLinearColor(.1f,.5f,1,.8f),FLinearColor(1,.75f,.1f,.95f),C.ChargeProgress));
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDReticleAmmoArcTest,"NeoMSGO.UI.HUD.ReticleAmmoArc",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDReticleAmmoArcTest::RunTest(const FString&)
{
	TestFalse(TEXT("legacy circular reticle rendering is removed"),UMSHUDRingWidget::HasLegacyReticleRendering());
	TestEqual(TEXT("three rounds in a six-round magazine produce a half arc"),UMSHUDViewModel::CalculateAmmoArcProgress(3,6,false,0.0f),0.5f);
	TestEqual(TEXT("reload halfway restores the half-full magazine arc to three quarters"),UMSHUDViewModel::CalculateAmmoArcProgress(3,6,true,0.5f),0.75f);
	TestEqual(TEXT("zero-capacity weapons do not display an ammo arc"),UMSHUDViewModel::CalculateAmmoArcProgress(0,0,false,0.0f),0.0f);

	UMSHUDRingWidget* Ring=NewObject<UMSHUDRingWidget>();
	Ring->SetAmmoArc(0.5f,false,true);
	TestEqual(TEXT("ring receives the calculated magazine progress"),Ring->GetAmmoArcProgress(),0.5f);
	TestEqual(TEXT("loaded magazine arc is yellow"),Ring->GetAmmoArcColor(),MSHUDPalette::AmmoArc);
	Ring->SetAmmoArc(0.75f,true,true);
	TestEqual(TEXT("reloading magazine arc is red"),Ring->GetAmmoArcColor(),MSHUDPalette::AmmoArcReloading);

	UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();
	TestEqual(TEXT("normal HUD initializes with the center cross visible"),HUD->GetReticleVisibilityForAutomationTests(),ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("lock-on distance uses the bold typeface"),HUD->GetLockOnDistanceTypefaceForAutomationTests(),FName(TEXT("Bold")));
	return true;
}
#endif
