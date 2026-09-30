#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MSHUDTypes.h"
#include "MSHUDRingWidget.generated.h"

/**
 * 円形HUDリング(誘導捕捉サークル/チャージリング/占領リング)。
 * 実プレイ報告バグ修正: 旧実装は48本の短い棒(UImage)の集合で「点線の多角形」に見えていた。
 * 現実装はNativePaintでアンチエイリアスされた折れ線円(高分割)を描く=滑らかな円の縁。
 * 進捗(0..1)は12時方向から時計回りの円弧の伸び(角度マスク)で表現する。
 */
UCLASS()
class NEOMSGO_API UMSHUDRingWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/**
	 * @param InDiameter  リング直径(px)。ウィジェットスロットサイズと一致させる想定
	 * @param InProgress  0..1。円弧の伸び(1.0で全周)
	 * @param InColor     円弧色。アウトラインは同色の低アルファ
	 * @param bInOutline  未進捗部分に薄いアウトライン円を描くか
	 * @param InArcThickness     円弧の線幅 px(誘導カーソルは太線=MSHUDPalette::HomingCursorThickness)
	 * @param InOutlineThickness アウトラインの線幅 px
	 */
	void SetRing(float InDiameter,float InProgress,const FLinearColor& InColor,bool bInOutline=true,
		float InArcThickness=4.0f,float InOutlineThickness=2.0f);
	/** 画面中央下側に、現在武器のマガジン残弾弧を重ねる。 */
	void SetAmmoArc(float InProgress,bool bInReloading,bool bInVisible=true);
	/** CaptureRingを下中央の制圧横バーとして再利用する描画モード。 */
	void SetHorizontalBar(float InProgress,const FLinearColor& InColor);
	/** Selects 12 o'clock as the progress origin. Other HUD rings retain their existing 6 o'clock origin. */
	void SetProgressOriginTop(bool bInStartAtTop) { bStartAtTop=bInStartAtTop; Invalidate(EInvalidateWidgetReason::Paint); }

	/**
	 * リングが実際に描く円の半径(Slate単位)。線幅の内側寄せぶんを差し引いた値で、
	 * NativePaint の描画半径と完全に同一(単一実装)。
	 * ロック判定(UMSLockOnComponent::GetHomingConeDegrees)がこの値を基準にするため、
	 * 描画半径の式を変えるときは必ずこの関数を通すこと。
	 */
	static float ComputeDrawRadius(const float InDiameter,const float InArcThickness)
	{
		return FMath::Max(2.0f,InDiameter*0.5f-InArcThickness*0.5f);
	}

	/** テスト用: 現在の描画パラメータ */
	float GetProgress() const{return Progress;}
	FLinearColor GetColor() const{return Color;}
	float GetArcThickness() const{return ArcThickness;}
	float GetAmmoArcProgress() const{return AmmoArcProgress;}
	FLinearColor GetAmmoArcColor() const{return bAmmoArcReloading?MSHUDPalette::AmmoArcReloading:MSHUDPalette::AmmoArc;}
	bool IsAmmoArcVisible() const{return bAmmoArcVisible;}
	bool IsProgressOriginTop() const{return bStartAtTop;}
	static constexpr bool HasLegacyReticleRendering(){return false;}

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
		const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const override;

private:
	float Diameter=100,Progress=0; FLinearColor Color=FLinearColor::White; bool bOutline=true;
	/** 円弧/アウトラインの線幅 px(SetRingで用途ごとに上書き) */
	float ArcThickness=4.0f,OutlineThickness=2.0f;
	float AmmoArcProgress=0.0f;
	bool bAmmoArcReloading=false,bAmmoArcVisible=false,bHorizontalBar=false;
	bool bStartAtTop=false;
	/** 全周の分割数(96分割=3.75°刻み。棒の集合ではなく連続したアンチエイリアス折れ線) */
	static constexpr int32 FullCircleSegments=96;
};
