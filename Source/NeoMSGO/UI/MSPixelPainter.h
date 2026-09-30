// Authored vector geometry for the video-reference UI. No raster UI assets.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace MSPixel
{
inline FLinearColor Hex(uint32 RGB, float Alpha = 1.f)
{
	FLinearColor C = FLinearColor::FromSRGBColor(FColor((RGB >> 16) & 255, (RGB >> 8) & 255, RGB & 255));
	C.A = Alpha;
	return C;
}

/** Paint in measured design pixels; apply the widget's actual Slate geometry once. */
class FPainter
{
public:
	FPainter(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer,
		FVector2D DesignSize, float InOpacity = 1.f)
		: Geometry(InGeometry), Elements(InElements), Layer(InLayer), Opacity(InOpacity)
	{
		Scale = Geometry.GetLocalSize() / DesignSize;
	}

	void Polygon(const TArray<FVector2D>& Points, const TArray<FLinearColor>& Colors)
	{
		if (Points.Num() < 3 || Colors.Num() != Points.Num()) return;
		const int32 Start = Vertices.Num();
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			FLinearColor Color = Colors[I]; Color.A *= Opacity;
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				Geometry.GetAccumulatedRenderTransform(), FVector2f(Points[I] * Scale),
				FVector2f(.5f, .5f), Color.ToFColor(true)));
		}
		auto Cross=[](FVector2D A,FVector2D B){return A.X*B.Y-A.Y*B.X;};
		double Area=0;
		for(int32 I=0;I<Points.Num();++I) Area+=Cross(Points[I],Points[(I+1)%Points.Num()]);
		const double Sign=Area>=0?1.0:-1.0;
		bool bConvex=true;
		for(int32 I=0;I<Points.Num();++I)
			if(Sign*Cross(Points[(I+1)%Points.Num()]-Points[I],Points[(I+2)%Points.Num()]-Points[(I+1)%Points.Num()]) < -1.e-6) bConvex=false;
		auto Triangle=[this,Start](int32 A,int32 B,int32 C)
		{
			Indices.Add(static_cast<SlateIndex>(Start+A));Indices.Add(static_cast<SlateIndex>(Start+B));Indices.Add(static_cast<SlateIndex>(Start+C));
		};
		if(bConvex)
		{
			for(int32 I=1;I+1<Points.Num();++I) Triangle(0,I,I+1);
			return;
		}
		// Concave logos and mechanical silhouettes require ears; a triangle fan fills their cutouts.
		TArray<int32,TInlineAllocator<32>> Remaining;
		for(int32 I=0;I<Points.Num();++I) Remaining.Add(I);
		while(Remaining.Num()>3)
		{
			bool bClipped=false;
			for(int32 I=0;I<Remaining.Num();++I)
			{
				const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
				if(Sign*Cross(Points[B]-Points[A],Points[C]-Points[B])<=1.e-6)continue;
				bool bContains=false;
				for(int32 J:Remaining)
				{
					if(J==A||J==B||J==C)continue;
					if(Sign*Cross(Points[B]-Points[A],Points[J]-Points[A])>=-1.e-6 &&
						Sign*Cross(Points[C]-Points[B],Points[J]-Points[B])>=-1.e-6 &&
						Sign*Cross(Points[A]-Points[C],Points[J]-Points[C])>=-1.e-6) {bContains=true;break;}
				}
				if(!bContains){Triangle(A,B,C);Remaining.RemoveAt(I);bClipped=true;break;}
			}
			if(!bClipped)break;
		}
		if(Remaining.Num()==3) Triangle(Remaining[0],Remaining[1],Remaining[2]);
	}

	void Polygon(const TArray<FVector2D>& Points, FLinearColor Color)
	{
		TArray<FLinearColor> Colors; Colors.Init(Color, Points.Num()); Polygon(Points, Colors);
	}
	void Rect(float X, float Y, float W, float H, FLinearColor Color)
	{
		Polygon({{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}}, Color);
	}
	void Gradient(float X, float Y, float W, float H, FLinearColor Top, FLinearColor Bottom)
	{
		Polygon({{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}}, {Top,Top,Bottom,Bottom});
	}
	void Line(FVector2D A, FVector2D B, float Width, FLinearColor Color)
	{
		const FVector2D D = (B-A).GetSafeNormal();
		const FVector2D N(-D.Y * Width * .5f, D.X * Width * .5f);
		Polygon({A+N,B+N,B-N,A-N},Color);
	}
	void Box(float X,float Y,float W,float H,float Width,FLinearColor Color)
	{
		Rect(X,Y,W,Width,Color); Rect(X,Y+H-Width,W,Width,Color);
		Rect(X,Y,Width,H,Color); Rect(X+W-Width,Y,Width,H,Color);
	}
	void Chamfer(float X,float Y,float W,float H,float Cut,FLinearColor Color)
	{
		Cut=FMath::Min(Cut,FMath::Min(W,H)*.45f);
		Polygon({{X+Cut,Y},{X+W-Cut,Y},{X+W,Y+Cut},{X+W,Y+H-Cut},
			{X+W-Cut,Y+H},{X+Cut,Y+H},{X,Y+H-Cut},{X,Y+Cut}},Color);
	}
	void Circle(FVector2D Center,float Radius,FLinearColor Color,int32 Segments=32)
	{
		TArray<FVector2D> P;
		for(int32 I=0;I<Segments;++I)
		{
			float A=2*UE_PI*I/Segments; P.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);
		}
		Polygon(P,Color);
	}
	void Arc(FVector2D Center,float Radius,float From,float To,float Width,FLinearColor Color,int32 Segments=32)
	{
		for(int32 I=0;I<Segments;++I)
		{
			float A=FMath::Lerp(From,To,float(I)/Segments), B=FMath::Lerp(From,To,float(I+1)/Segments);
			Line(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius,
				Center+FVector2D(FMath::Cos(B),FMath::Sin(B))*Radius,Width,Color);
		}
	}
	void Finish()
	{
		if(Vertices.IsEmpty() || !FSlateApplication::IsInitialized()) return;
		const FSlateBrush* White=FCoreStyle::Get().GetBrush("WhiteBrush");
		const FSlateResourceHandle Handle=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		FSlateDrawElement::MakeCustomVerts(Elements,Layer,Handle,Vertices,Indices,nullptr,0,0);
	}
private:
	const FGeometry& Geometry;
	FSlateWindowElementList& Elements;
	int32 Layer;
	float Opacity;
	FVector2D Scale;
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
};
}
