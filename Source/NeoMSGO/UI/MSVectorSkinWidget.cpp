#include "UI/MSVectorSkinWidget.h"
#include "UI/MSPixelPainter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Button.h"

namespace
{
using MSPixel::FPainter;
using MSPixel::Hex;

void MetalFrame(FPainter& P,float W,float H,bool Gold=false)
{
	// A single translucent fill: stacking filled chamfers would make the glass opaque.
	P.Chamfer(1,1,W-2,H-2,9,Hex(0x0b2930,.86f));
	const TArray<FVector2D> Rim{{10,1},{W-10,1},{W-1,10},{W-1,H-10},
		{W-10,H-1},{10,H-1},{1,H-10},{1,10}};
	for(int I=0;I<Rim.Num();++I) P.Line(Rim[I],Rim[(I+1)%Rim.Num()],1.5f,Hex(Gold?0xb19a49:0x527f78,.95f));
	P.Line({10,2},{W-12,2},1,Hex(Gold?0xf7e889:0x93d4c8,.9f));
	P.Line({2,12},{2,H-14},1,Hex(0xb1dfd4,.6f));
	P.Line({12,H-3},{W-12,H-3},1,Hex(0x659f96,.7f));
	for(float Y=10;Y<H-8;Y+=3) P.Rect(7,Y,W-14,.45f,Hex(0x7cb8ab,.10f));
	for(int I=0;I<3;++I) P.Line({W-27.f+I*5,H-6},{W-23.f+I*5,H-2},1,Hex(0x8ac0b7,.7f));
}

FVector2D Q(const TArray<FVector2D>& V,float U,float T)
{
	return FMath::Lerp(FMath::Lerp(V[0],V[1],U),FMath::Lerp(V[3],V[2],U),T);
}

// Bevels and illumination are authored in the same projected quadrilateral as the screen.
void Monitor(FPainter& P,const TArray<FVector2D>& V,int Variant=0)
{
	auto Quad=[&](float U0,float T0,float U1,float T1,FLinearColor A,FLinearColor B)
	{
		P.Polygon({Q(V,U0,T0),Q(V,U1,T0),Q(V,U1,T1),Q(V,U0,T1)},{A,A,B,B});
	};
	P.Polygon(V,Hex(0x070f17));
	Quad(.015f,.02f,.99f,.99f,Hex(0x172e35),Hex(0x101b23));
	Quad(.025f,.035f,.975f,.96f,Hex(0x93b6ac),Hex(0x284b52));
	Quad(.039f,.06f,.962f,.935f,Hex(0x1c3442),Hex(0x3b6d75));
	Quad(.052f,.083f,.948f,.913f,Hex(0x6aeadd),Hex(0x176078));
	// Blue, curved glass with light concentrated at its physical perimeter.
	constexpr int32 Columns=10,Rows=8;
	auto GlassColor=[Variant](float U,float T)
	{
		const float Edge=FMath::Min(FMath::Min(U,1-U),FMath::Min(T,1-T));
		const float Halo=FMath::Exp(-Edge*17.f);
		const float Curve=FMath::Pow(FMath::Abs(U-.5f)*2.f,3.f)*.24f;
		return FMath::Lerp(FMath::Lerp(Hex(Variant>=10?0x288895:0x123577),Hex(Variant>=10?0x5cbdbe:0x20639c),T*.45f+Curve),Hex(0x89fff0),Halo*.77f);
	};
	for(int32 Y=0;Y<Rows;++Y) for(int32 X=0;X<Columns;++X)
	{
		float U=float(X)/Columns,T=float(Y)/Rows,NU=float(X+1)/Columns,NT=float(Y+1)/Rows;
		P.Polygon({Q(V,.061f+U*.878f,.102f+T*.789f),Q(V,.061f+NU*.878f,.102f+T*.789f),Q(V,.061f+NU*.878f,.102f+NT*.789f),Q(V,.061f+U*.878f,.102f+NT*.789f)},
			{GlassColor(U,T),GlassColor(NU,T),GlassColor(NU,NT),GlassColor(U,NT)});
	}
	for(int I=0;I<25;++I) P.Line(Q(V,.066f,.12f+I*.0305f),Q(V,.935f,.12f+I*.0305f),.65f,Hex(0x87cceb,.19f));
	// Fine map display, rather than a uniformly bright rectangular wire grid.
	const float CenterU=.50f+float(Variant%3-1)*.07f;
	for(int Ring=0;Ring<2;++Ring) for(int I=0;I<36;++I)
	{
		float A=I*UE_PI/18,B=(I+1)*UE_PI/18,R=Ring==0?.19f:.24f;
		P.Line(Q(V,CenterU+FMath::Cos(A)*R,.49f+FMath::Sin(A)*R*1.24f),Q(V,CenterU+FMath::Cos(B)*R,.49f+FMath::Sin(B)*R*1.24f),Ring==0?1.3f:.6f,Hex(0x94d6ed,Ring==0?.42f:.20f));
	}
	P.Line(Q(V,.10f,.49f),Q(V,.88f,.49f),1,Hex(0x8ad7ec,.3f));
	P.Line(Q(V,CenterU,.15f),Q(V,CenterU,.85f),1,Hex(0x8ad7ec,.3f));
	for(int I=0;I<7;++I)
	{
		float U=.09f+I*.12f,T=.39f+FMath::Sin(I*1.1f+Variant)*.2f,NT=.39f+FMath::Sin((I+1)*1.1f+Variant)*.2f;
		P.Line(Q(V,U,T),Q(V,U+.12f,NT),1.4f,Hex(0x9adcf1,.47f));
	}
	for(int I=0;I<6;++I)
	{
		float U=.12f+I*.022f;
		Quad(U,.17f,U+.009f,.215f,Hex(0x7ee2ee,.30f),Hex(0x397fc9,.30f));
		P.Line(Q(V,.1f,.71f+I*.018f),Q(V,.24f+(I%3)*.027f,.71f+I*.018f),.8f,Hex(0x9be9ee,.29f));
	}
	P.Line(Q(V,.053f,.081f),Q(V,.948f,.081f),2.2f,Hex(0xb7fff0,.78f));
	P.Line(Q(V,.055f,.91f),Q(V,.945f,.91f),2.8f,Hex(0x8bffe7,.65f));
	P.Line(Q(V,.039f,.96f),Q(V,.96f,.96f),2,Hex(0x080e14));
}

void Console(FPainter& P,float X,float Y,float W,float H)
{
	P.Polygon({{X+4,Y+H-6},{X+W+24,Y+H-20},{X+W+55,Y+H+16},{X-28,Y+H+27}},Hex(0x061719,.50f));
	P.Polygon({{X,Y},{X+W,Y-18},{X+W+28,Y+H},{X-16,Y+H}},
		{Hex(0x829f92),Hex(0x557c76),Hex(0x172d30),Hex(0x263d3b)});
	P.Polygon({{X+W-4,Y-10},{X+W+12,Y+5},{X+W+28,Y+H},{X+W+7,Y+H-8}},
		{Hex(0x3b5c59),Hex(0x243f41),Hex(0x102729),Hex(0x1b3636)});
	P.Polygon({{X+5,Y+10},{X+W-6,Y-6},{X+W+8,Y+H-8},{X,Y+H-8}},
		{Hex(0x98c5b1),Hex(0x7ca695),Hex(0x3d5d56),Hex(0x526f61)});
	P.Polygon({{X+8,Y+10},{X+W-10,Y-4},{X+W-12,Y+H*.46f},{X+8,Y+H*.52f}},Hex(0x102a31));
	Monitor(P,{{X+13,Y+13},{X+W-16,Y+1},{X+W-18,Y+H*.39f},{X+13,Y+H*.44f}},11);
	P.Line({X+3,Y+H*.54f},{X+W-1,Y+H*.46f},2,Hex(0xc1d9be,.65f));
	// Small button bank, recessed service hatch, and ventilation strips.
	for(int Row=0;Row<3;++Row) for(int Col=0;Col<4;++Col)
	{
		float BX=X+13+Col*(W-30)/5,BY=Y+H*.58f+Row*8;
		P.Rect(BX-1,BY-1,7,5,Hex(0x294a46));P.Rect(BX,BY,5,2,Hex((Row+Col)%4==0?0xbee2b1:0x609c91));
	}
	P.Polygon({{X+12,Y+H*.79f},{X+W-15,Y+H*.76f},{X+W-13,Y+H*.94f},{X+12,Y+H*.96f}},Hex(0x38564f));
	for(int I=0;I<3;++I) P.Line({X+19,Y+H*.82f+I*5},{X+W-22,Y+H*.80f+I*5},1,Hex(0x142e2d));
	P.Line({X+2,Y+H-8},{X+W+8,Y+H-8},1.5f,Hex(0xa3b7a0));
}

void Chair(FPainter& P,float X,float Y,float S)
{
	auto Poly=[&](TArray<FVector2D> V,const TArray<FLinearColor>& C){for(auto& Pt:V)Pt=FVector2D(X,Y)+Pt*S;P.Polygon(V,C);};
	// Seat rail and cast shadow establish its position on the floor.
	Poly({{-6,174},{81,151},{94,225},{-32,270}},{Hex(0x162824),Hex(0x1f352e),Hex(0x0d1b1b),Hex(0x243831)});
	for(int I=0;I<12;++I)
	{
		float T=float(I)/12;
		P.Line({X+(-4-22*T)*S,Y+(178+83*T)*S},{X+(78+12*T)*S,Y+(159+64*T)*S},1.5f*S,Hex(0x6b8271,.55f));
	}
	Poly({{-15,174},{83,151},{116,205},{-12,248}},{Hex(0x071817,.5f),Hex(0x071817,.5f),Hex(0x071817,.08f),Hex(0x071817,.08f)});
	Poly({{0,0},{65,-5},{76,105},{3,118}},{Hex(0x85b686),Hex(0x496a49),Hex(0x112a22),Hex(0x243d2b)});
	Poly({{6,5},{60,0},{63,88},{15,100}},{Hex(0x77b565),Hex(0x609551),Hex(0x2b552f),Hex(0x49783e)});
	Poly({{10,11},{54,7},{57,53},{13,61}},{Hex(0x689f50),Hex(0x5b8c43),Hex(0x3b6837),Hex(0x446e37)});
	Poly({{0,33},{-12,49},{-7,107},{15,100}},{Hex(0x446c3c),Hex(0x395332),Hex(0x192e25),Hex(0x30552c)});
	Poly({{63,28},{82,41},{87,99},{64,89}},{Hex(0x436d3b),Hex(0x2d4f30),Hex(0x163524),Hex(0x355f2e)});
	Poly({{15,100},{64,89},{91,124},{12,140}},{Hex(0x416835),Hex(0x517940),Hex(0x3e6332),Hex(0x5e8543)});
	Poly({{18,106},{59,98},{74,118},{18,127}},{Hex(0x658d4c),Hex(0x4c733c),Hex(0x436b35),Hex(0x5c8142)});
	P.Line({X+18*S,Y+13*S},{X+53*S,Y+10*S},2*S,Hex(0xb5d691,.55f));
	P.Line({X+17*S,Y+68*S},{X+57*S,Y+63*S},1.2f*S,Hex(0x234628));
	P.Line({X+18*S,Y+72*S},{X+58*S,Y+67*S},1*S,Hex(0x6e9851));
	P.Rect(X+42*S,Y+135*S,12*S,32*S,Hex(0x122624));
	Poly({{10,159},{72,147},{92,174},{70,182},{10,182},{0,171}},{Hex(0x6c9357),Hex(0x527945),Hex(0x284c2d),Hex(0x193326),Hex(0x355635),Hex(0x50754a)});
	P.Line({X+17*S,Y+165*S},{X+70*S,Y+156*S},2*S,Hex(0x8bb86c,.65f));
}

void Bridge(FPainter& P)
{
	// Final-era MSGO7 00:59: front-facing bridge, overhead beam and horizontal star window.
	P.Gradient(0,0,1920,1080,Hex(0x152b32),Hex(0x698b68));
	P.Rect(0,367,1920,272,Hex(0x294249));
	P.Gradient(0,400,1750,235,Hex(0x191f32),Hex(0x243447));
	for(int I=0;I<140;++I)
	{
		float X=FMath::Fmod(I*137.7f,1740.f),Y=406+FMath::Fmod(I*31.2f,188.f);
		P.Rect(X,Y,(I%5==0)?2.f:1.f,1,Hex(0xb0b7cf,.44f));
	}
	P.Polygon({{0,781},{1070,654},{1920,690},{1920,1080},{0,1080}},
		{Hex(0x354c42),Hex(0x587853),Hex(0x54795e),Hex(0x739c6e),Hex(0x496b58)});
	for(int I=-8;I<=10;++I)
	{
		FVector2D B(1020+I*217,1100),A(1090+I*36,663);
		P.Line(A,B,2,Hex(0x253f36,.8f)); P.Line(A+FVector2D(3,0),B+FVector2D(3,0),1,Hex(0x93ad85,.55f));
	}
	for(int I=1;I<=10;++I)
	{
		float Y=664+I*I*4.2f;
		P.Line({0,Y+78},{1920,Y-28},1,Hex(0x344f3e,.7f));
	}
	P.Polygon({{0,0},{1920,0},{1920,228},{1250,275},{570,235},{0,205}},Hex(0x172c32));
	Monitor(P,{{-80,-105},{1260,-30},{1181,127},{-70,41}},7);
	Monitor(P,{{1804,-15},{1990,-10},{1981,183},{1680,157}},8);
	P.Polygon({{0,44},{1192,112},{1152,177},{0,112}},{Hex(0x3b5a5b),Hex(0x243f45),Hex(0x10202b),Hex(0x1c323a)});
	P.Polygon({{0,65},{1181,129},{1164,151},{0,91}},{Hex(0x17272f),Hex(0x213940),Hex(0x304951),Hex(0x102129)});
	P.Line({0,48},{1168,119},4,Hex(0x57746d));
	P.Polygon({{0,119},{1146,183},{1090,222},{0,171}},Hex(0x102127));
	P.Line({0,173},{1089,223},5,Hex(0x324d50));
	P.Polygon({{1262,0},{1394,0},{1309,210},{1795,175},{1743,242},{1160,275},{1090,230},{1194,140}},
		{Hex(0x11222a),Hex(0x2f4850),Hex(0x1c3038),Hex(0x243c43),Hex(0x10242a),Hex(0x25464c),Hex(0x1d343b),Hex(0x304950)});
	P.Polygon({{1310,210},{1794,175},{1772,207},{1321,244}},{Hex(0x09141d),Hex(0x0d1e25),Hex(0x34545a),Hex(0x26434b)});
	for(int I=0;I<7;++I) P.Line({1328.f+I*65,219.f-I*4.7f},{1361.f+I*65,216.f-I*4.7f},2,Hex(0x5d7778,.40f));
	P.Line({1269,21},{1191,208},4,Hex(0x4d6966));
	P.Line({1315,211},{1776,181},5,Hex(0x0e2026));
	P.Polygon({{1748,376},{1791,364},{1920,626},{1822,610}},Hex(0x1c3034));
	P.Line({1755,390},{1810,611},5,Hex(0x668877));
	Monitor(P,{{-107,210},{79,218},{110,372},{-97,367}},1);
	Monitor(P,{{145,218},{552,234},{563,372},{176,370}},2);
	Monitor(P,{{167,119},{511,139},{505,289},{192,289}},3);
	Monitor(P,{{569,227},{862,237},{895,358},{574,350}},4);
	Monitor(P,{{1270,257},{1502,267},{1468,371},{1282,359}},5);
	Monitor(P,{{1518,263},{1731,278},{1686,373},{1507,365}},6);
	Monitor(P,{{1741,286},{1908,290},{1855,374},{1713,369}},7);
	Monitor(P,{{1898,205},{1999,210},{1995,320},{1855,315}},8);
	P.Line({0,393},{1737,387},6,Hex(0x152b31));
	P.Line({0,400},{1729,394},2,Hex(0x657e77));
	P.Polygon({{0,604},{930,577},{961,759},{0,812}},{Hex(0x455f56),Hex(0x405e59),Hex(0x182e30),Hex(0x0c2024)});
	P.Polygon({{0,598},{909,562},{938,623},{0,680}},{Hex(0x5c8d7b),Hex(0x9ac0a6),Hex(0x274d46),Hex(0x2d5148)});
	P.Polygon({{0,586},{896,548},{941,605},{0,662}},{Hex(0x94bfa6),Hex(0x8ab79b),Hex(0x517f6b),Hex(0x456d5e)});
	for(int I=0;I<8;++I)
	{
		float X=I*118.f;
		P.Polygon({{X,568-I*3.f},{X+104,563-I*3.f},{X+112,612-I*3.f},{X,630-I*4.f}},
			{Hex(0xa2c5af),Hex(0x8cb29c),Hex(0x567e70),Hex(0x6e9680)});
		P.Polygon({{X+8,576-I*3.f},{X+94,572-I*3.f},{X+103,610-I*3.f},{X+6,622-I*4.f}},
			{Hex(0x6d9781),Hex(0x7fa68c),Hex(0x71967e),Hex(0x5a826e)});
		P.Line({X+7,573-I*3.f},{X+95,569-I*3.f},1,Hex(0xd4e0bd,.52f));
		P.Line({X+9,623-I*4.f},{X+101,607-I*3.f},2,Hex(0x31524c));
		Console(P,X+4,647-I*3,102,150);
	}
	Console(P,203,641,90,180);Monitor(P,{{295,610},{467,602},{473,704},{299,718}},4);
	Chair(P,288,650,1.1f);Chair(P,730,615,.99f);
	for(int I=0;I<3;++I){Console(P,1455+I*175,540,145,148);Chair(P,1563+I*165,587-I*21,.68f);}
	P.Polygon({{926,676},{1222,649},{1280,770},{1004,807}},
		{Hex(0x749782),Hex(0x608a77),Hex(0x243f39),Hex(0x203b35)});
	P.Polygon({{992,712},{1229,689},{1255,751},{1014,780}},{Hex(0x9bc6b0),Hex(0x8eb5a0),Hex(0x578874),Hex(0x75a893)});
	P.Polygon({{1036,729},{1216,713},{1231,745},{1045,762}},Hex(0xf0ffe3));
	P.Polygon({{1446,796},{1920,744},{1920,1030},{1618,929},{1409,826}},{Hex(0x657e62),Hex(0x557b58),Hex(0x26452e),Hex(0x344f39),Hex(0x425f45)});
	P.Polygon({{1455,808},{1920,754},{1920,1012},{1629,917},{1423,824}},{Hex(0x7c9c71),Hex(0x8faf7a),Hex(0x406b47),Hex(0x5f8259),Hex(0x728f62)});
	P.Line({1455,812},{1920,758},2,Hex(0xacba91,.55f));
	P.Line({1428,827},{1629,917},2,Hex(0x9cad83,.55f));
	for(int I=0;I<28;++I)
	{
		float X=30+FMath::Fmod(I*83.f,1850.f),Y=824+FMath::Fmod(I*31.f,236.f);
		P.Line({X,Y},{X+13+float(I%3)*8,Y-2},.8f,Hex(0xa6b99a,.17f));
	}
	P.Polygon({{0,821},{226,788},{518,1005},{0,1080}},{Hex(0x041215,.58f),Hex(0x071818,.33f),Hex(0x15281b,.04f),Hex(0x031116,.58f)});
	P.Line({1110,789},{1464,1080},16,Hex(0x8ca681));P.Line({1110,789},{1464,1080},3,Hex(0x506f53));
	P.Polygon({{-55,955},{427,897},{525,982},{510,1080},{-55,1080}},Hex(0x466c66));
	Monitor(P,{{-22,978},{416,925},{471,998},{440,1130}},10);
	// Interior ambient shade keeps the geometry behind the functional foreground.
	P.Gradient(0,0,1920,1080,Hex(0x061620,.10f),Hex(0x183c2e,.08f));
}

void Hangar(FPainter& P)
{
	P.Gradient(0,0,1920,1080,Hex(0x040b12),Hex(0x152c3e));
	P.Polygon({{0,0},{720,228},{1100,261},{1920,48},{1920,860},{0,978}},Hex(0x0d1b28));
	P.Polygon({{0,848},{1115,758},{1920,836},{1920,1080},{0,1080}},Hex(0x283e4b));
	for(int I=0;I<15;++I)
	{
		float X=I*150.f-90;
		P.Line({1070+(X-960)*.14f,759},{X,1080},2,Hex(0x101f2c));
	}
	for(int I=0;I<8;++I) P.Line({0,819+I*I*5.f},{1920,792+I*I*5.f},2,Hex(0x465e6b,.6f));
	for(int Bay=0;Bay<9;++Bay)
	{
		float X=-80+Bay*242.f;
		P.Polygon({{X,288},{X+210,305},{X+216,810},{X+2,826}},
			{Hex(0x243139),Hex(0x17262d),Hex(0x101d26),Hex(0x172832)});
		for(int Rib=0;Rib<8;++Rib)
		{
			float Y=320+Rib*60.f;
			P.Polygon({{X+11,Y},{X+199,Y+13},{X+200,Y+20},{X+11,Y+7}},
				{Hex(0x0a141d),Hex(0x111e27),Hex(0x3a4c57),Hex(0x344653)});
			P.Line({X+13,Y+24},{X+195,Y+36},1,Hex(0x48616b,.24f));
		}
		P.Line({X+9,305},{X+10,809},3,Hex(0x08121a));
		for(int Rivet=0;Rivet<10;++Rivet) P.Circle({X+19,320+Rivet*48.f},1.3f,Hex(0x73868b,.42f),8);
	}
	for(int I=0;I<7;++I)
	{
		float X=-70+I*330.f;
		P.Polygon({{X,0},{X+70,0},{X+102,939},{X+35,960}},Hex(0x070f17));
		P.Line({X+18,0},{X+66,947},5,Hex(0x3d5562));
		P.Line({X+50,0},{X+84,940},3,Hex(0x1e3441));
		for(int J=0;J<8;++J)
		{
			float Y=J*115.f;
			P.Line({X+17+Y*.05f,Y},{X+65+Y*.035f,Y+95},3,Hex(0x294350));
			P.Line({X+65+Y*.035f,Y},{X+22+Y*.05f,Y+95},2,Hex(0x456071,.6f));
		}
	}
	for(int I=0;I<5;++I)
	{
		float Y=112+I*159.f;
		P.Polygon({{0,Y},{1150,Y+80},{1920,Y+24},{1920,Y+58},{1150,Y+120},{0,Y+38}},Hex(0x101f2b));
		P.Line({0,Y+36},{1148,Y+116},3,Hex(0x4a6070,.62f));
		P.Line({1148,Y+116},{1920,Y+55},3,Hex(0x405763,.62f));
	}
	P.Polygon({{575,0},{677,0},{1164,318},{1117,367}},{Hex(0x111e28),Hex(0x334d59),Hex(0x1f3442),Hex(0x172831)});
	P.Polygon({{663,0},{695,0},{1175,302},{1164,318}},Hex(0x527583));
	P.Polygon({{917,0},{1008,0},{1328,305},{1254,349}},{Hex(0x17262f),Hex(0x364d58),Hex(0x213541),Hex(0x111f2c)});
	P.Polygon({{0,0},{570,0},{1098,237},{774,159}},Hex(0x060f19,.9f));
	P.Polygon({{1202,0},{1920,0},{1920,87},{1222,246}},Hex(0x050d15,.94f));
	for(int Rail=0;Rail<6;++Rail)
	{
		float Y=104+Rail*14.f;
		P.Line({0,Y},{612,Y+154},2,Hex(0x293c48,.62f));
	}
	// Left gantry: paired rail, support posts and a thin hazard strip.
	P.Polygon({{0,249},{633,391},{634,410},{0,270}},{Hex(0x4b5144),Hex(0x393f37),Hex(0x18242a),Hex(0x242c2c)});
	for(int Post=0;Post<10;++Post)
	{
		float X=Post*66.f,Y=251+Post*14.7f;
		P.Line({X,Y},{X,Y-40},3,Hex(0x1d3039));
		P.Line({X+2,Y},{X+2,Y-40},1,Hex(0x5c6c70,.6f));
		P.Polygon({{X,Y+3},{X+19,Y+7},{X+23,Y+14},{X+4,Y+10}},Hex(0x8a8542,.5f));
	}
	P.Line({0,211},{633,353},3,Hex(0x42525c));
	P.Line({690,17},{1162,303},2,Hex(0x8ea7a9));
	P.Rect(1420,454,386,331,Hex(0x0a151f));
	P.Box(1426,460,372,321,5,Hex(0x304650));
	P.Line({1615,465},{1615,780},2,Hex(0x05090d));
	for(int I=0;I<18;++I)
		P.Polygon({{1428+I*20.f,746},{1436+I*20.f,746},{1450+I*20.f,763},{1442+I*20.f,763}},Hex(0x696434));
	P.Polygon({{645,839},{1214,824},{1531,939},{973,1001},{446,942}},Hex(0x152431));
	P.Polygon({{646,826},{1215,811},{1531,922},{973,982},{446,925}},
		{Hex(0x4c606c),Hex(0x60737a),Hex(0x314853),Hex(0x375460),Hex(0x445d69)});
	P.Line({647,829},{1212,814},2,Hex(0x97a6a4,.55f));
	P.Line({1212,814},{1525,923},2,Hex(0x738889,.55f));
	P.Polygon({{694,840},{1198,830},{1429,917},{966,962},{558,919}},Hex(0x283e4d));
	for(int I=0;I<7;++I)
	{
		float X=210+I*262.f,Y=270+(I%3)*76.f;
		P.Rect(X,Y,19,11,Hex(0x436878)); P.Rect(X+3,Y+2,12,5,Hex(0x9ee9dd));
	}
	// Grated floor seams and a recessed circular service turntable.
	for(int I=0;I<52;++I)
	{
		float A=I*2*UE_PI/52,B=(I+1)*2*UE_PI/52;
		P.Line({988+FMath::Cos(A)*407,898+FMath::Sin(A)*57},{988+FMath::Cos(B)*407,898+FMath::Sin(B)*57},2,Hex(0x102632));
		P.Line({988+FMath::Cos(A)*395,898+FMath::Sin(A)*53},{988+FMath::Cos(B)*395,898+FMath::Sin(B)*53},1,Hex(0x81949a,.5f));
	}
	for(int I=0;I<30;++I)
	{
		float X=80+FMath::Fmod(I*74.f,1740.f),Y=896+FMath::Fmod(I*41.f,167.f);
		P.Line({X,Y},{X+18+float(I%4)*11,Y-3},1,Hex(0x8b9a9d,.16f));
	}
	// MSGO9 f2140: narrow steel plates, joints, gantry rails and cable runs
	// provide the hangar's density. These are authored room geometry, not a bitmap.
	for(int Bay=0;Bay<7;++Bay)
	{
		const float X=-70+Bay*330.f;
		for(int Joint=0;Joint<9;++Joint)
		{
			const float Y=Joint*106.f, BX=X+29+Y*.044f;
			P.Polygon({{BX-9,Y+8},{BX+32,Y+9},{BX+34,Y+34},{BX-7,Y+33}},
				{Hex(0x354652,.8f),Hex(0x24333f),Hex(0x0b1621),Hex(0x14222d)});
			for(float Offset : {0.f,23.f})
			{
				P.Circle({BX+Offset,Y+15},2,Hex(0x09121a),8);
				P.Circle({BX+Offset-.4f,Y+14.5f},.8f,Hex(0x6e7a80,.7f),6);
			}
			for(int Rib=0;Rib<5;++Rib)
				P.Line({BX-5,Y+44+Rib*9.f},{BX+26,Y+46+Rib*9.f},.75f,Hex(0x71808b,.20f));
		}
	}
	for(int Cable=0;Cable<12;++Cable)
	{
		const float X=264+Cable*11.5f;
		P.Line({X,0},{X+9,688},Cable%3==0?2.f:.8f,Hex(0x060e16,.85f));
		P.Line({X+2,0},{X+11,688},.55f,Hex(0x73858e,.35f));
	}
	// Suspended maintenance walkway at the left and a foreshortened floor grate.
	for(int Rail=0;Rail<3;++Rail)
		P.Line({0,340+Rail*21.f},{523,450+Rail*14.f},1.6f,Hex(0x78838a,.55f));
	for(int Post=0;Post<16;++Post)
	{
		const float X=Post*34.f,Y=340+Post*7.2f;
		P.Line({X,Y},{X,Y+61-Post*1.1f},2,Hex(0x73818a,.5f));
	}
	for(int Grate=0;Grate<52;++Grate)
	{
		const float X=Grate*39.f;
		P.Line({560+(X-960)*.22f,917},{X,1080},.75f,Hex(0x0c1823,.65f));
	}
	for(int Row=0;Row<16;++Row)
		P.Line({0,925+Row*Row*.65f},{1920,902+Row*Row*.70f},.7f,Hex(0x83929b,.16f));
	P.Polygon({{0,0},{240,0},{525,1080},{0,1080}},{Hex(0x00040c,.7f),Hex(0x010812,.03f),Hex(0x010810,.04f),Hex(0x01060e,.44f)});
	P.Polygon({{1710,0},{1920,0},{1920,1080},{1450,1080}},{Hex(0x00040c,.04f),Hex(0x010812,.58f),Hex(0x010810,.48f),Hex(0x01060e,.03f)});
	P.Gradient(0,0,1920,1080,Hex(0x01070e,.22f),Hex(0x01070e,.03f));
}

void Robot(FPainter& P,float X,float Y,float S,bool Violet)
{
	auto Poly=[&](TArray<FVector2D> V,uint32 C){for(auto& Pt:V) Pt=FVector2D(X,Y)+Pt*S;P.Polygon(V,Hex(C));};
	uint32 Plate=Violet?0x93868a:0x879995,Light=Violet?0xd3b1aa:0xd4d9c3;
	Poly({{34,49},{90,49},{104,112},{19,112}},0x17262b);
	Poly({{34,54},{84,49},{89,90},{30,98}},Plate);
	Poly({{33,59},{85,55},{79,70},{33,75}},0x424e4c);
	Poly({{49,18},{80,15},{89,44},{48,49}},Light);
	Poly({{43,29},{83,24},{91,36},{46,41}},Plate);
	Poly({{49,33},{82,30},{83,36},{51,39}},0x192a2a);
	Poly({{54,34},{79,32},{77,36},{55,38}},Violet?0xe0b34a:0x95db5a);
	Poly({{11,47},{36,43},{39,85},{8,91},{0,76}},Plate);
	Poly({{89,45},{121,38},{141,75},{112,87},{98,73}},Light);
	Poly({{8,88},{31,85},{28,135},{7,155},{0,141}},Light);
	Poly({{113,86},{138,75},{152,120},{138,139},{117,116}},Plate);
	Poly({{27,113},{56,108},{53,178},{37,204},{14,198}},Plate);
	Poly({{60,110},{91,111},{108,174},{112,194},{81,201},{69,163}},Light);
	Poly({{9,197},{36,192},{45,216},{0,220}},0x263b3c);
	Poly({{82,196},{111,187},{130,211},{78,216}},0x384947);
	Poly({{94,100},{146,97},{164,115},{102,124}},0x263134);
	Poly({{121,100},{197,88},{200,96},{126,111}},0x6f7975);
	Poly({{35,54},{49,52},{42,86},{30,98}},Violet?0xb3a0a0:0xb3beb0);
	Poly({{72,53},{84,49},{89,90},{76,92}},Violet?0x5f5963:0x4d6160);
	Poly({{46,77},{68,73},{74,93},{44,98}},0x263840);
	Poly({{48,78},{65,76},{68,83},{46,87}},Violet?0x786044:0x727d5b);
	Poly({{91,47},{119,42},{131,65},{112,71}},Violet?0xdcc4bb:0xe1e4cf);
	Poly({{114,76},{136,72},{146,113},{134,118}},Violet?0x635c68:0x4d6366);
	Poly({{15,49},{30,46},{30,61},{8,72}},Violet?0xc1a6a0:0xbdc9b8);
	Poly({{8,98},{21,94},{18,123},{5,136}},Violet?0x6e6a75:0x667b75);
	Poly({{29,116},{48,112},{43,155},{29,163}},Violet?0xb3a0a5:0xa9b6ae);
	Poly({{29,164},{46,156},{47,172},{34,181},{22,176}},0x263b42);
	Poly({{72,120},{88,116},{99,165},{83,169}},Violet?0xb8a5a8:0xb5c4b7);
	Poly({{80,172},{103,166},{108,183},{86,186}},0x4e6268);
	Poly({{96,104},{154,97},{157,105},{102,116}},0x81908d);
	Poly({{169,91},{198,88},{201,96},{173,100}},0x263941);

}

void DrawNavigationIcon(FPainter& P,int32 Variant)
{
	const bool Green=Variant==4 || Variant==6;
	const FLinearColor Metal=Hex(Green?0x92b889:0x87b6c8),Mid=Hex(Green?0x49684d:0x365c76),Dark=Hex(Green?0x172922:0x111e31);
	P.Polygon({{7,0},{56,0},{64,13},{60,43},{52,49},{10,49},{2,41},{0,13}},Hex(0x080f17,.98f));
	P.Polygon({{8,2},{55,2},{61,14},{57,41},{51,46},{11,46},{5,39},{3,14}},
		{Hex(0x90a9ab),Metal,Mid,Dark,Hex(0x172931),Hex(0x233842),Mid,Metal});
	P.Polygon({{10,6},{53,6},{58,15},{54,39},{49,42},{13,42},{8,36},{7,15}},
		{Metal,Mid,Mid,Dark,Dark,Dark,Mid,Metal});
	P.Polygon({{11,8},{52,8},{55,17},{9,17}},{Hex(0xdefafa,.27f),Hex(0xb6eaf4,.30f),Hex(0x598ea9,.05f),Hex(0x598ea9,.05f)});
	P.Line({12,5},{51,5},1.4f,Hex(0xd4eeee,.90f));
	P.Line({8,16},{12,36},1,Hex(0x81d6dd,.42f));
	P.Line({15,43},{48,43},1,Hex(Green?0x92c686:0x71a6cc,.75f));
	P.Rect(6,35,3,3,Hex(0xb5d9d4,.85f));P.Rect(54,35,3,3,Hex(0x91b6bd,.75f));
	const FLinearColor C=Hex(0xb2e3e2);
	if(Variant==0) // shop basket
	{
		P.Line({20,10},{46,10},3,C); P.Line({22,13},{25,24},2,C); P.Line({44,13},{41,24},2,C);
		P.Line({24,24},{42,24},2,C); P.Circle({27,29},2.4f,C); P.Circle({40,29},2.4f,C);
		for(int I=0;I<4;++I) P.Line({27+I*4.f,13},{28+I*3.2f,22},1.4f,C);
	}
	else if(Variant==1) // task sheets
	{
		P.Box(23,8,20,24,2,C); P.Line({19,12},{19,33},1,C);
		for(int I=0;I<4;++I) P.Line({26,13+I*4.f},{39,13+I*4.f},1,C);
	}
	else if(Variant==2 || Variant==3)
	{
		P.Circle({33,18},10,C); P.Circle({33,18},7,Hex(0x29434c));
		if(Variant==2) P.Polygon({{33,8},{36,16},{44,16},{38,21},{40,29},{33,24},{26,29},{28,21},{22,16},{30,16}},C);
		else {P.Rect(31,16,4,15,C);P.Rect(25,29,16,3,C);P.Arc({33,13},14,0,UE_PI,2,C,12);}
	}
	else if(Variant==4) // group/gear
	{
		P.Circle({32,18},11,C);P.Circle({32,18},7,Hex(0x29434c));
		for(int I=0;I<8;++I){float A=I*UE_PI*.25f;P.Line(FVector2D(32,18)+FVector2D(FMath::Cos(A),FMath::Sin(A))*9,FVector2D(32,18)+FVector2D(FMath::Cos(A),FMath::Sin(A))*15,3,C);}
	}
	else if(Variant==5)
	{
		P.Circle({32,13},5,C);P.Chamfer(25,19,14,12,3,C);
		P.Circle({20,16},4,C);P.Chamfer(14,22,10,9,2,C);P.Circle({44,16},4,C);P.Chamfer(40,22,10,9,2,C);
	}
	else if(Variant==6)
	{
		P.Rect(21,16,24,16,C);P.Rect(19,12,28,5,C);P.Rect(31,12,4,21,Hex(0x3c5c5c));
		P.Arc({28,11},5,UE_PI,2*UE_PI,2,C,8);P.Arc({38,11},5,UE_PI,2*UE_PI,2,C,8);
	}
	else if(Variant==7)
	{
		P.Box(23,8,18,23,2,C);P.Line({18,19},{36,19},3,C);P.Line({18,19},{25,12},3,C);P.Line({18,19},{25,26},3,C);
	}
	else
	{
		P.Circle({32,18},11,C); P.Circle({32,18},6,Hex(0x29434c));
		for(int I=0;I<8;++I){float A=I*UE_PI*.25f;P.Line(FVector2D(32,18)+FVector2D(FMath::Cos(A),FMath::Sin(A))*10,FVector2D(32,18)+FVector2D(FMath::Cos(A),FMath::Sin(A))*14,3,C);}
	}
}

void Card(FPainter& P,float W,float H,EMSVectorSurface Surface)
{
	bool Mission=Surface==EMSVectorSurface::MissionCard;
	uint32 Rim=Mission?0xae234f:0xb3a44b;
	P.Chamfer(0,0,W,H,14,Hex(0x0b0e12));
	P.Chamfer(2,2,W-4,H-4,12,Hex(Rim));
	P.Line({15,2},{W-15,2},1.4f,Hex(Mission?0xf384a5:0xfff3a5,.95f));
	P.Chamfer(5,5,W-10,H-10,10,Hex(0xefe9d7));
	P.Chamfer(7,7,W-14,H-14,9,Hex(0x242c30));
	P.Chamfer(10,6,W-16,H-12,8,Hex(0x101b20));
	P.Polygon({{26,8},{W-13,8},{W-6,18},{W-6,H-15},{W-15,H-7},{26,H-7}},Hex(0x1e3235));
	P.Gradient(29,13,W-44,H-28,Hex(0x344842),Hex(0x070c11));
	for(float Y=14;Y<H-10;Y+=3)P.Rect(30,Y,W-46,.5f,Hex(0xd8d6aa,.15f));
	if(Mission)
	{
		P.Polygon({{W*.46f,10},{W*.56f,10},{W-8,H*.55f},{W-8,H*.66f}},Hex(0xa38642,.8f));
		P.Line({W*.52f,15},{W-16,H*.67f},6,Hex(0xffde81));
		P.Line({W*.64f,H*.65f},{W-12,18},7,Hex(0xdb6bae));
		Robot(P,34,19,H/237.f,false);Robot(P,W*.56f,28,H/260.f,true);
		P.Gradient(28,H*.59f,W-41,H*.35f,Hex(0x060913,.05f),Hex(0x030810,.94f));
		P.Rect(43,9,W-76,22,Hex(0x3c1021,.75f));
	}
	else if(Surface==EMSVectorSurface::CustomizeCard)
	{
		P.Polygon({{55,13},{114,13},{129,40},{66,55}},Hex(0x63736c));
		P.Rect(53,29,72,31,Hex(0x344446));
		for(int Rib=0;Rib<6;++Rib) {P.Rect(51+Rib*13,24,5,37,Hex(0x112a30));P.Line({53.f+Rib*13,24},{53.f+Rib*13,56},1,Hex(0x9ea99b));}
		P.Line({44,61},{165,20},9,Hex(0x66836c,.65f));
		P.Line({44,58},{165,17},1,Hex(0xc6d1b4,.64f));
		P.Box(72,30,25,23,2,Hex(0x7b9486));P.Rect(77,35,15,13,Hex(0x254958));
		for(int I=0;I<5;++I) P.Line({52+I*17.f,14},{61+I*17.f,23},4,Hex(0xdfd55a));
	}
	else if(Surface==EMSVectorSurface::TrainingCard)
	{
		P.Line({W-20,12},{W*.45f,H*.65f},4,Hex(0xb2b779));Robot(P,45,5,.32f,false);
	}
	else
	{
		uint32 Ticket=Surface==EMSVectorSurface::GachaCard?0xc4b548:0x975643;
		P.Polygon({{69,13},{120,20},{113,45},{61,36}},Hex(Ticket));
		P.Polygon({{129,25},{183,38},{164,68},{114,51}},Hex(Surface==EMSVectorSurface::GachaCard?0x99a846:0x566783));
		P.Line({76,22},{112,28},2,Hex(0xe5dab1));P.Line({77,28},{109,33},1,Hex(0x4b5543));
		P.Polygon({{69,14},{119,21},{113,26},{67,19}},Hex(0xffe9aa,.55f));
		P.Polygon({{129,26},{181,39},{176,43},{124,31}},Hex(0xc8d9df,.57f));
		for(int I=0;I<4;++I) P.Line({75.f+I*8,19.f+I},{71.f+I*8,35.f+I},.75f,Hex(0xf0e2ab,.5f));
		P.Polygon({{143,37},{157,40},{160,47},{149,52},{137,45}},Hex(0xddcc72));
		P.Polygon({{144,39},{154,41},{154,46},{148,48},{141,44}},Hex(0x283e58));
	}
	// White-gold docking strip, bevel and angular cutout seen on the final-era cards.
	P.Polygon({{14,5},{26,5},{26,H*.39f},{13,H*.53f},{23,H*.62f},{23,H-5},{12,H-5},{5,H-14},{5,14}},
		{Hex(0xffffe3),Hex(0xf6f1c6),Hex(0xdedba6),Hex(0xc6c28c),Hex(0xf2ecc1),Hex(0xa4aa82),Hex(0xd5d8ae),Hex(0xfffbe0),Hex(0xffffe9)});
	P.Line({9,15},{9,H-15},1,Hex(0xc7ba78));
	P.Line({27,9},{27,H*.38f},2,Hex(0x02090d));
	P.Polygon({{27,H*.40f},{14,H*.53f},{25,H*.63f},{29,H*.59f},{21,H*.52f},{29,H*.44f}},Hex(0x080e15));
	P.Circle({16,19},6,Hex(0x51554c));P.Circle({16,19},4,Hex(0xe9dfa2));P.Circle({16,19},2,Hex(0x777861));
	if(!Mission)
	{
		P.Gradient(29,H*.42f,W-42,H*.51f,Hex(0x03060a,.04f),Hex(0x000207,.97f));
		P.Line({W-8,17},{W-8,H-17},1,Hex(0xecdd9f,.48f));
	}
	P.Polygon({{W-20,6},{W-9,6},{W-5,12},{W-5,22},{W-8,22},{W-8,14},{W-15,9}},Hex(Mission?0xe45d88:0xf8e5a8));
	P.Line({17,8},{17,18},2,Hex(0x303636));
	P.Line({35,H-6},{W-14,H-6},1,Hex(Rim));
}
}

void UMSVectorSkinWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UMSVectorSkinWidget::SetSurface(EMSVectorSurface InSurface,int32 InVariant)
{
	Surface=InSurface;Variant=InVariant;InvalidateLayoutAndVolatility();
}
void UMSVectorSkinWidget::SetAccent(FLinearColor InAccent)
{
	Accent=InAccent;InvalidateLayoutAndVolatility();
}
int32 UMSVectorSkinWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,
	FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
	const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
	FVector2D Size=Geometry.GetLocalSize();if(Size.X<1 || Size.Y<1)return Base;
	const bool Room=Surface==EMSVectorSurface::BridgeBackdrop || Surface==EMSVectorSurface::HangarBackdrop;
	FVector2D Design=Room?FVector2D(1920,1080):(Surface==EMSVectorSurface::NavigationIcon?FVector2D(64,52):Size);
	if(Surface==EMSVectorSurface::BattleModuleIcon) Design=FVector2D(128,96);
	FPainter P(Geometry,Elements,Base+1,Design,Style.GetColorAndOpacityTint().A * (bEnabled ? 1.f : .65f));
	float W=Design.X,H=Design.Y;
	if(Surface==EMSVectorSurface::BridgeBackdrop) Bridge(P);
	else if(Surface==EMSVectorSurface::HangarBackdrop) Hangar(P);
	else if(Surface==EMSVectorSurface::NavigationIcon) DrawNavigationIcon(P,Variant);
	else if(Surface==EMSVectorSurface::BattleModulePanel)
	{
		// MSGO-5 05:50: dark blue glass, thin scan lines, chamfered outer frame.
		const bool SelectedRow = H < 100 && Accent.R > .8f && Accent.G > .5f;
		P.Chamfer(0,0,W,H,H<100?3:12,SelectedRow?Hex(0x817313,.97f):Hex(0x08172a,H<100?.95f:.78f));
		P.Box(2,2,W-4,H-4,SelectedRow?3:2,SelectedRow?Hex(0xffe54c):Hex(0x234566,.85f));
		P.Gradient(9,9,W-18,FMath::Min(63.f,H-18),SelectedRow?Hex(0xd2b31e,.62f):Hex(0x204567,.72f),Hex(0x08192d,.25f));
		for(float Y=8;Y<H-8;Y+=4) P.Rect(7,Y,W-14,1,Hex(0x3c6394,.15f));
		P.Line({14,2},{W-18,2},2,Hex(0x578caf,.6f));
		P.Line({5,H-7},{W-12,H-7},2,Hex(0x2c5483,.8f));
	}
	else if(Surface==EMSVectorSurface::BattleModuleIcon)
	{
		// Small cartridge constructed from projected plates, never a pasted reference image.
		P.Gradient(0,0,128,96,Hex(0x1469a3),Hex(0x06355e));
		for(int I=0;I<24;++I) P.Line({0.f,float(I*4)},{128.f,float(I*4)},1,Hex(0x6bd0f3,.15f));
		P.Box(1,1,126,94,2,Hex(0x407e99));
		if(Variant>=3)
		{
			P.Polygon({{36,18},{60,10},{91,29},{65,38}},Hex(0xc6e0de));
			P.Polygon({{36,18},{65,38},{65,87},{36,65}},Hex(0x789c9e));
			P.Polygon({{65,38},{91,29},{91,76},{65,87}},Hex(0x385d6b));
			for(int I=0;I<4;++I)
			{
				const float X=40.f+I*6;
				P.Line({X,25.f+I*4},{X,64.f+I*4},3,Hex(0xb7d0cc));
				P.Line({69.f+I*5,41.f-I*2},{69.f+I*5,80.f-I*2},2,Hex(0x1c3b4d));
			}
		}
		else
		{
			P.Polygon({{21,48},{74,15},{96,18},{111,37},{56,77},{34,72}},Hex(0xa18b3c));
			P.Polygon({{23,46},{75,15},{96,18},{52,49}},Hex(0xf3dfa0));
			P.Polygon({{52,49},{96,18},{110,37},{61,71}},Hex(0xcab365));
			P.Polygon({{24,47},{52,49},{61,71},{55,82},{31,77},{19,62}},Hex(0x8d742f));
			P.Polygon({{32,52},{47,54},{53,68},{49,73},{35,69},{28,60}},Hex(0x433e29));
			P.Polygon({{37,52},{58,39},{72,41},{50,57}},Hex(0xead395));
			P.Line({59,31},{75,29},4,Hex(0x8c7839));
			P.Line({42,43},{57,41},4,Hex(0x8c7839));
			P.Line({23,47},{75,16},2,Hex(0xffedb2));
		}
	}
	else if(Surface==EMSVectorSurface::WeaponPanel)
	{
		// MSGO5 f14098: dark blue scan-lined shell, not the green lobby glass.
		P.Chamfer(0,0,W,H,12,Hex(0x06101e,.96f));
		P.Chamfer(2,2,W-4,H-4,10,Hex(0x173758,.84f));
		P.Chamfer(7,7,W-14,H-14,6,Hex(0x081827,.88f));
		P.Gradient(8,7,W-16,70,Hex(0x234a68,.76f),Hex(0x102841,.45f));
		for(float Y=9;Y<H-8;Y+=4) P.Line({8,Y},{W-8,Y},.65f,Hex(0x5c89aa,.16f));
		for(float X=16;X<W-12;X+=8) P.Line({X,8},{X,H-8},.4f,Hex(0x35577c,.11f));
		P.Line({12,3},{W-15,3},1,Hex(0x647f99,.8f));
		P.Line({8,H-7},{W-12,H-7},2,Hex(0x304777,.85f));
	}
	else if(Surface==EMSVectorSurface::WeaponImage)
	{
		P.Rect(0,0,W,H,Hex(0x5c9cac));
		P.Gradient(2,2,W-4,H-4,Hex(0x0b3458),Hex(0x116278));
		for(float Y=4;Y<H-3;Y+=4) P.Line({3,Y},{W-3,Y},.6f,Hex(0x6ac5de,.22f));
		for(float X=3;X<W-3;X+=16) P.Line({X,3},{X,H-3},.5f,Hex(0x69a8cb,.12f));
		P.Line({3,3},{W-4,3},1,Hex(0x9ad6d5,.8f));
	}
	else if(Surface==EMSVectorSurface::DeckCard)
	{
		// MSGO9 frame 2140: white metal face with orange/red edges, not a uniformly cream rim.
		P.Chamfer(0,0,W,H,11,Hex(Variant ? 0xed5a22 : 0x995126));
		P.Chamfer(2,2,W-4,H-4,10,Hex(0xe9b791));
		P.Chamfer(6,5,W-12,H-10,7,Hex(0xb36c49));
		P.Chamfer(9,8,W-18,H-16,5,Hex(0xefedeb));
		P.Rect(13,20,W-26,H-42,Hex(0x12364b));
		P.Gradient(14,21,W-28,H*.39f,Hex(0x0c3556),Hex(0x2b6982));
		for(float Y=23;Y<H-28;Y+=3) P.Line({14,Y},{W-14,Y},.6f,Hex(0x71b5b9,.18f));
		P.Rect(13,H-80,W-26,57,Hex(0x92958c));
		P.Rect(13,H-102,W-26,22,Hex(0x66623f));
		P.Line({16,3},{W-16,3},1,Hex(0xfffae7));
		P.Line({16,H-4},{W-16,H-4},2,Hex(0xd59b6c));
		for(const FVector2D B : {FVector2D(15,12),FVector2D(W-15,12),FVector2D(15,H-13),FVector2D(W-15,H-13)}) P.Circle(B,2.5f,Hex(0x7a5543));
	}
	else if(Surface>=EMSVectorSurface::MissionCard && Surface<=EMSVectorSurface::ShopCard) Card(P,W,H,Surface);
	else if(Surface==EMSVectorSurface::PassPanel)
	{
		MetalFrame(P,W,H,true);P.Gradient(4,3,W-8,22,Hex(0xd7b734),Hex(0x7b5d0a));
		P.Circle({68,H*.52f},50,Hex(0x062735));P.Arc({68,H*.52f},48,0,2*UE_PI,3,Hex(0x89cbc2));
		P.Arc({68,H*.52f},42,-UE_PI*.7f,UE_PI*.4f,2,Hex(0xe9dc64));
		P.Polygon({{44,H*.52f-25},{92,H*.52f-25},{80,H*.52f-11},{59,H*.52f-11},{53,H*.52f+4},{78,H*.52f+4},{69,H*.52f+16},{49,H*.52f+16},{41,H*.52f+32},{28,H*.52f+32}},Hex(0xebd961));
		P.Line({127,35},{127,H-16},1,Hex(0x618b82));P.Line({W-155,34},{W-155,H-20},1,Hex(0x52776f));
		P.Box(W-130,43,45,50,1,Hex(0xd6c483));P.Box(W-72,55,49,50,1,Hex(0x95b9d1));
	}
	else if(Surface==EMSVectorSurface::NoticePanel)
	{
		P.Chamfer(0,0,W,H,8,Hex(0x061516));
		P.Box(1,1,W-2,H-2,1.5f,Hex(0x4ea594));
		P.Chamfer(5,5,W-10,H-10,7,Hex(0x171b1d));
		P.Gradient(12,18,W-24,H-38,Hex(0x24272a),Hex(0x070c11));
		P.Polygon({{W*.21f,4},{W*.80f,4},{W*.71f,32},{W*.31f,32}},
			{Hex(0x7ee4f8),Hex(0x60c9e9),Hex(0x2487b1),Hex(0x2999be)});
		P.Polygon({{W*.17f,H-6},{W*.83f,H-6},{W*.71f,H-32},{W*.29f,H-32}},
			{Hex(0x3684a3),Hex(0x3783a6),Hex(0x79dfff),Hex(0x8ceaff)});
		P.Line({W*.21f,5},{W*.8f,5},2,Hex(0xc4ffff,.7f));
		P.Line({W*.29f,H-32},{W*.71f,H-32},1,Hex(0xceffff,.75f));
		for(int I=0;I<12;++I)
		{
			float X=W*.31f+I*W*.031f;
			P.Line({X,7},{X+12,27},.75f,Hex(0xd5ffff,.21f));
			P.Line({X,H-29},{X-12,H-9},.75f,Hex(0xd5ffff,.15f));
		}
		P.Line({12,39},{W*.22f,39},1,Hex(0x566164,.34f));
		P.Line({W*.78f,39},{W-12,39},1,Hex(0x566164,.34f));
		P.Rect(3,H-22,W-6,19,Hex(0x0e3030,.95f));
		P.Polygon({{W*.32f,H-19},{W*.29f,H-19},{W*.275f,H-12},{W*.29f,H-5},{W*.32f,H-5},{W*.302f,H-12}},Hex(0x35caa3));
		P.Polygon({{W*.66f,H-19},{W*.69f,H-19},{W*.705f,H-12},{W*.69f,H-5},{W*.66f,H-5},{W*.678f,H-12}},Hex(0x35caa3));
	}
	else if(Surface==EMSVectorSurface::MapThumbnail)
	{
		P.Gradient(0,0,W,H,Hex(Variant%2==0?0x86977a:0x6c8b98),Hex(0x243539));
		if(Variant%3==2)P.Polygon({{W*.38f,0},{W*.65f,0},{W*.8f,H},{W*.45f,H}},Hex(0x347c8e));
		for(int I=0;I<18;++I)
		{
			float X=FMath::Fmod(I*24.7f,W-10),Y=H*.2f+FMath::Fmod(I*11.9f,H*.6f);
			P.Rect(X,Y,5+(I%3)*4,4+(I%4)*3,Hex(0xc0c0ae,.8f));P.Line({X,Y},{X+8,Y},1,Hex(0xe0deca));
		}
		P.Box(0,0,W,H,1,Hex(0xcbcbc2));
	}
	else if(Surface==EMSVectorSurface::RespawnPanel)
	{
		// MSGO9 137.49s, native 1920x1080: return/respawn panel, sampled RGB.
		P.Chamfer(0,0,W,H,13,Hex(0x6AAACB,.92f));
		P.Chamfer(2,2,W-4,H-4,11,Hex(0x2766A5));
		P.Chamfer(5,5,W-10,H-10,9,Hex(0x4C8BCA));
		P.Chamfer(7,7,W-14,H-14,8,Hex(0x364A64));
		P.Polygon({{20,7},{W-20,7},{W-31,20},{31,20}},Hex(0x3297C0));
		P.Line({15,H-10},{W-15,H-10},1,Hex(0x577BBA));
	}
	else if(Surface==EMSVectorSurface::Button)
	{
		if(Variant == 1)
		{
			P.Chamfer(0,2,W,H-2,5,Hex(0x2c4ea5));P.Chamfer(1,0,W-2,H-4,4,Hex(0xe8fdff));
			P.Chamfer(3,2,W-6,H-8,3,Hex(0xb8b8e0));P.Gradient(7,3,W-14,H*.4f,Hex(0xe9e6fa),Hex(0xb8b8e0));
			P.Line({7,H-6},{W-7,H-6},1,Hex(0x7588c1));
		}
		else
		{
			P.Chamfer(0,0,W,H,7,Hex(0x060d11));P.Chamfer(1,1,W-2,H-2,6,Hex(0xb9c9d2));
			P.Chamfer(3,3,W-6,H-6,4,Hex(0x365970));P.Gradient(6,4,W-12,FMath::Max(1.f,H-8),Hex(0x326a79),Hex(0x132b39));
			P.Line({8,4},{W-8,4},1,Hex(0xd1e2e2));P.Line({8,H-5},{W-8,H-5},1,Hex(0x609289));
		}
	}
	else MetalFrame(P,W,H,Surface==EMSVectorSurface::GoldPanel);
	if (!Room)
	{
		for (const UWidget* Parent=GetParent(); Parent; Parent=Parent->GetParent())
		{
			if (const UButton* Button=Cast<UButton>(Parent))
			{
				if (Button->GetIsEnabled() && Button->IsPressed()) P.Chamfer(1,1,W-2,H-2,5,Hex(0x000b1a,.22f));
				else if (Button->GetIsEnabled() && Button->IsHovered()) P.Chamfer(1,1,W-2,H-2,5,Hex(0xe4ffff,.14f));
				break;
			}
		}
	}
	P.Finish();return Base+1;
}
