#pragma once
#include "CoreMinimal.h"
#include "UI/MSVectorSkinWidget.h"

class UWidgetTree;
class UOverlay;
class UButton;
class UBorder;
class UTextBlock;

namespace MSPixelUI
{
	NEOMSGO_API UMSVectorSkinWidget* Skin(UWidgetTree* Tree, EMSVectorSurface Surface, FName Name=NAME_None, int32 Variant=0);
	NEOMSGO_API UOverlay* Frame(UWidgetTree* Tree,UWidget* Content,EMSVectorSurface Surface=EMSVectorSurface::Panel,FMargin Padding=FMargin(8),int32 Variant=0);
	NEOMSGO_API void ClearButtonBrush(UButton* Button);
	NEOMSGO_API void PlainBorder(UBorder* Border,FLinearColor Color);
	NEOMSGO_API UTextBlock* Text(UWidgetTree* Tree,const FText& Content,float PixelHeight,FLinearColor Color=FLinearColor::White,FName Name=NAME_None);
}
