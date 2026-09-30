#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MSWeaponIconWidget.generated.h"

class UMSWeaponDataBase;

/**
 * 武器カテゴリ別シルエットアイコン(実プレイ報告: アルファベット略号→武器の形の図形へ)。
 * 正規化(0..1)矩形の集合をカテゴリ契約として保持し、NativePaintでは
 * MSPixel::FPainterによる銃身・スコープ・マガジン等の機械シルエットを描く。
 * ライフル=銃身+ストック、バズーカ=太い筒、剣=刃+柄、盾=盾形、マシンガン=銃身+マガジン等。
 * HUD武器スロットと武器選択(ハンガー)画面の両方が使用する。
 */
UCLASS()
class NEOMSGO_API UMSWeaponIconWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/**
	 * 武器データそのものから形状を解決する新しい入口。
	 * CategoryTagだけでなく、内蔵/スコープ/バースト/チャージ等の実データと
	 * DataAsset名のモデル識別子を使うため、HUDと格納庫で同じ武器は同じ形になる。
	 * nullptr は空slotとして扱い、NativePaintは何も描画しない。
	 */
	void SetWeapon(const UMSWeaponDataBase* InWeapon,const FLinearColor& InColor);
	/** Hangar/catalog presentation only. HUD continues to call SetWeapon. */
	void SetWeaponPhoto(const UMSWeaponDataBase* InWeapon,const FLinearColor& InColor);

	/** 旧呼出し互換。カテゴリだけ分かる表示ではカテゴリ既定の形状を使う。 */
	void SetIcon(const FGameplayTag& InCategory,const FLinearColor& InColor);

	/** カテゴリ別シルエット矩形(0..1正規化。Min=左上/Max=右下)。テストからも参照する純関数 */
	static TArray<FBox2D> GetSilhouetteBoxes(const FGameplayTag& Category);

	/**
	 * Non-HUD photo layout uses cover semantics: the source keeps its aspect ratio,
	 * the shorter side fills the widget, and the overflow is clipped by NativePaint.
	 * This pure helper keeps the crop contract testable without constructing Slate.
	 */
	static FBox2D CalculatePhotoCoverRect(const FVector2D& AllottedSize,const FVector2D& SourceSize);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
		const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const override;

private:
	void TryResolvePendingPhoto();

	UPROPERTY(Transient) TObjectPtr<class UTexture2D> WeaponPhoto;
	TWeakObjectPtr<UMSWeaponDataBase> PendingPhotoWeapon;
	int32 PendingPhotoRetries=0;
	float PendingPhotoRetryAccumulator=0.0f;
	FSlateBrush PhotoBrush;
	FGameplayTag Category;
	FLinearColor Color=FLinearColor::White;
	TArray<FBox2D> Boxes;
	/** 内部で解決済みの武器形状。実データを保持せず、寿命の短いプレビューも安全に描画する。 */
	uint8 ProfileId=0;
	bool bHasWeapon=false;
	// Photo surfaces must never flash the legacy HUD silhouette while rendering.
	bool bPhotoPresentation=false;
};
