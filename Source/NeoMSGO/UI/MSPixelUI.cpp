#include "UI/MSPixelUI.h"
#include "UI/MSUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"

UMSVectorSkinWidget* MSPixelUI::Skin(UWidgetTree* Tree,EMSVectorSurface Surface,FName Name,int32 Variant)
{
	UMSVectorSkinWidget* Widget=Tree->ConstructWidget<UMSVectorSkinWidget>(UMSVectorSkinWidget::StaticClass(),Name);
	Widget->SetSurface(Surface,Variant); Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Widget;
}
UOverlay* MSPixelUI::Frame(UWidgetTree* Tree,UWidget* Content,EMSVectorSurface Surface,FMargin Padding,int32 Variant)
{
	UOverlay* Root=Tree->ConstructWidget<UOverlay>();
	UOverlaySlot* Back=Root->AddChildToOverlay(Skin(Tree,Surface,NAME_None,Variant));
	Back->SetHorizontalAlignment(HAlign_Fill); Back->SetVerticalAlignment(VAlign_Fill);
	if(Content)
	{
		UOverlaySlot* Fore=Root->AddChildToOverlay(Content);
		Fore->SetHorizontalAlignment(HAlign_Fill); Fore->SetVerticalAlignment(VAlign_Fill); Fore->SetPadding(Padding);
	}
	return Root;
}
void MSPixelUI::ClearButtonBrush(UButton* Button)
{
	FButtonStyle Style=Button->GetStyle();
	FSlateBrush Clear; Clear.DrawAs=ESlateBrushDrawType::NoDrawType;
	Style.SetNormal(Clear); Style.SetHovered(Clear); Style.SetPressed(Clear); Style.SetDisabled(Clear);
	Style.NormalPadding=FMargin(0); Style.PressedPadding=FMargin(0);
	Button->SetStyle(Style); Button->SetClipping(EWidgetClipping::ClipToBounds);
	MSUIStyle::StretchButtonContent(Button);
}
void MSPixelUI::PlainBorder(UBorder* Border,FLinearColor Color)
{
	FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.TintColor=FLinearColor::White;
	Border->SetBrush(Brush); Border->SetBrushColor(Color);
}
UTextBlock* MSPixelUI::Text(UWidgetTree* Tree,const FText& Content,float PixelHeight,FLinearColor Color,FName Name)
{
	UTextBlock* Text=MSUIStyle::MakeText(Tree,Content,MSUIStyle::ETextRole::Body,false,Color,
		MSUIStyle::ENumericLargeSize::Standard,Name,false);
	FSlateFontInfo Font=Text->GetFont(); Font.Size=FMath::Max(6,FMath::RoundToInt(PixelHeight*.75f)); Text->SetFont(Font);
	Text->SetAutoWrapText(false); Text->SetShadowOffset(FVector2D(1,1)); Text->SetShadowColorAndOpacity(FLinearColor(0,0,0,.8f));
	return Text;
}
