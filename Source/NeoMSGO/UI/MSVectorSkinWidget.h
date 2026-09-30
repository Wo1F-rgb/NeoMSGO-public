#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSVectorSkinWidget.generated.h"

UENUM()
enum class EMSVectorSurface : uint8
{
	Panel, GoldPanel, Button, MissionCard, CustomizeCard, TrainingCard, GachaCard, ShopCard,
	NavigationIcon, PassPanel, NoticePanel, MapThumbnail, BridgeBackdrop, HangarBackdrop, DeckCard, RespawnPanel,
	WeaponPanel, WeaponImage, BattleModulePanel, BattleModuleIcon
};

/** Procedural metal, glass, icons and room shapes. All content remains live UMG. */
UCLASS()
class NEOMSGO_API UMSVectorSkinWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetSurface(EMSVectorSurface InSurface, int32 InVariant = 0);
	void SetAccent(FLinearColor InAccent);
protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,
		const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,
		const FWidgetStyle& Style,bool bEnabled) const override;
private:
	EMSVectorSurface Surface=EMSVectorSurface::Panel;
	int32 Variant=0;
	FLinearColor Accent=FLinearColor(.05f,.35f,.32f,1.f);
};
