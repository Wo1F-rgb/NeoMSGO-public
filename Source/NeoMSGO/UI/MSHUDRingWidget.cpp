#include "UI/MSHUDRingWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"
#include "UI/MSHUDTypes.h"
#include "UI/MSPixelPainter.h"

void UMSHUDRingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 描画はNativePaintのみで行う。子ウィジェットは不要だが、空のルートを与えてツリーを健全に保つ。
	WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();
}

void UMSHUDRingWidget::SetRing(const float InDiameter,const float InProgress,const FLinearColor& InColor,
	const bool bInOutline,const float InArcThickness,const float InOutlineThickness)
{
	Diameter=InDiameter;Progress=FMath::Clamp(InProgress,0.0f,1.0f);Color=InColor;bOutline=bInOutline;
	ArcThickness=FMath::Max(1.0f,InArcThickness);OutlineThickness=FMath::Max(1.0f,InOutlineThickness);
	bHorizontalBar=false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UMSHUDRingWidget::SetAmmoArc(const float InProgress,const bool bInReloading,const bool bInVisible)
{
	AmmoArcProgress=FMath::Clamp(InProgress,0.0f,1.0f);
	bAmmoArcReloading=bInReloading;
	bAmmoArcVisible=bInVisible;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UMSHUDRingWidget::SetHorizontalBar(const float InProgress,const FLinearColor& InColor)
{
	Progress=FMath::Clamp(InProgress,0.0f,1.0f);Color=InColor;bHorizontalBar=true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UMSHUDRingWidget::NativePaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
	const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const
{
	const int32 RetLayer=Super::NativePaint(Args,AllottedGeometry,MyCullingRect,OutDrawElements,LayerId,InWidgetStyle,bParentEnabled);

	// 中心は必ずウィジェット領域の幾何中心(クロスヘア=画面中心と一致させる前提。アンカー/
	// アライメントを{.5,.5}にしたスロットへ置けば、リング中心=スロット中心=画面中心になる)
	const FVector2D LocalSize=AllottedGeometry.GetLocalSize();
	const FVector2D Center=LocalSize*0.5f;
	if(bHorizontalBar)
	{
		// MSGO6: bevelled 231x22 px frame at 854x480. Authored geometry, no screenshot texture.
		MSPixel::FPainter Painter(AllottedGeometry,OutDrawElements,RetLayer+1,{520,50});
		Painter.Polygon({{23,0},{520,0},{513,28},{495,48},{0,48},{0,34}},
			{MSPixel::Hex(0x798985,.95f),MSPixel::Hex(0x63706D,.9f),MSPixel::Hex(0x313F3D,.9f),MSPixel::Hex(0x23302E,.9f),MSPixel::Hex(0x26332F,.9f),MSPixel::Hex(0x566760,.9f)});
		Painter.Polygon({{30,6},{499,6},{482,42},{9,42}},MSPixel::Hex(0x071514,.9f));
		if(Progress>KINDA_SMALL_NUMBER)
		{
			const float End=15.0f+480.0f*Progress;
			const float TopEnd=FMath::Min(End,494.0f),BottomEnd=FMath::Min(End,480.0f),LeftTop=FMath::Min(30.0f,TopEnd);
			FLinearColor Light=Color*1.15f;Light.A=.98f;
			FLinearColor Dark=Color*.55f;Dark.A=.96f;
			Painter.Polygon({{LeftTop,9},{TopEnd,9},{BottomEnd,39},{15,39}},{Dark,Dark,Light,Light});
			Painter.Line({LeftTop,10},{TopEnd,10},2,Color);
		}
		Painter.Line({30,5},{499,5},1.2f,MSPixel::Hex(0xB6C8C3,.7f));
		Painter.Line({9,43},{482,43},1,MSPixel::Hex(0x687F79,.7f));
		Painter.Finish();
		return RetLayer+2;
	}

	const float Radius=ComputeDrawRadius(Diameter,ArcThickness);

	auto MakeCirclePoints=[&](const float InRadius,const float StartRad,const float EndRad,const int32 Segments)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Segments+1);
		for(int32 I=0;I<=Segments;++I)
		{
			const float A=FMath::Lerp(StartRad,EndRad,static_cast<float>(I)/Segments);
			Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*InRadius);
		}
		return Points;
	};

	// 実プレイ報告対応: ゲージは「左が0で右側に増える」向きに統一する。
	// 進捗円弧は6時(下中央)を起点に右回り(下→右→上→左)へ伸ばす。
	const float ProgressStartAngle=bStartAtTop ? -UE_HALF_PI : UE_HALF_PI;

	// 1. アウトライン(未進捗部分の薄い全周円)
	if(bOutline)
	{
		const FLinearColor OutlineColor(Color.R,Color.G,Color.B,Color.A*0.34f);
		FSlateDrawElement::MakeLines(OutDrawElements,RetLayer+1,AllottedGeometry.ToPaintGeometry(),
			MakeCirclePoints(Radius,ProgressStartAngle,ProgressStartAngle-2.0f*UE_PI,FullCircleSegments),
			ESlateDrawEffect::None,OutlineColor,/*bAntialias=*/true,OutlineThickness);
	}

	// 2. 進捗円弧(角度マスク: 下中央からProgress*360°、先端は右側から上へ進む)
	if(Progress>KINDA_SMALL_NUMBER)
	{
		const int32 ArcSegments=FMath::Max(2,FMath::CeilToInt(FullCircleSegments*Progress));
		FSlateDrawElement::MakeLines(OutDrawElements,RetLayer+2,AllottedGeometry.ToPaintGeometry(),
			MakeCirclePoints(Radius,ProgressStartAngle,ProgressStartAngle-2.0f*UE_PI*Progress,ArcSegments),
			ESlateDrawEffect::None,Color,/*bAntialias=*/true,ArcThickness);
	}

	if(bAmmoArcVisible&&AmmoArcProgress>KINDA_SMALL_NUMBER)
	{
		// 下側の弧: 左端(=0)から右端(=満量)へ伸ばす。
		const float AmmoRadius=MSHUDPalette::AmmoArcDiameter*0.5f-MSHUDPalette::AmmoArcThickness*0.5f;
		const float AmmoSpan=FMath::DegreesToRadians(MSHUDPalette::AmmoArcDegrees);
		const float AmmoStart=UE_HALF_PI+AmmoSpan*0.5f;
		const int32 AmmoSegments=FMath::Max(2,FMath::CeilToInt(static_cast<float>(MSHUDPalette::AmmoArcSegments)*AmmoArcProgress));
		FSlateDrawElement::MakeLines(OutDrawElements,RetLayer+3,AllottedGeometry.ToPaintGeometry(),
			MakeCirclePoints(AmmoRadius,AmmoStart,AmmoStart-AmmoSpan*AmmoArcProgress,AmmoSegments),
			ESlateDrawEffect::None,bAmmoArcReloading?MSHUDPalette::AmmoArcReloading:MSHUDPalette::AmmoArc,
			true,MSHUDPalette::AmmoArcThickness);
	}

	return RetLayer+3;
}
