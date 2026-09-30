#pragma once
#include "CoreMinimal.h"

class UUserWidget;
class UCanvasPanel;
class UTextBlock;

/** Common final-period hangar header and pilot strip, in 1920 x 1080 design pixels. */
namespace MSHangarChrome
{
	NEOMSGO_API UTextBlock* Build(UUserWidget* Owner, UCanvasPanel* Canvas, const FText& Breadcrumb);
	NEOMSGO_API void Refresh(UUserWidget* Owner);
}
