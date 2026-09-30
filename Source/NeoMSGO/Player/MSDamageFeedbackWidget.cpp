// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSDamageFeedbackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void UMSDamageFeedbackWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	}
	SetVisibility(ESlateVisibility::Collapsed);
	SetIsFocusable(false);
}

void UMSDamageFeedbackWidget::ShowDamage(const FVector2D& ScreenDirection, const float Severity)
{
	const FVector2D SafeDirection = ScreenDirection.GetSafeNormal();
	DamageDirection = SafeDirection.IsNearlyZero() ? FVector2D(0.0f, -1.0f) : SafeDirection;
	// Merge rapid hits, but cap the overlay so sustained fire cannot obscure the screen.
	PeakIntensity = FMath::Clamp(FMath::Max(PeakIntensity * GetFeedbackAlpha(), Severity), 0.22f, 1.0f);
	ElapsedSeconds = 0.0f;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Invalidate(EInvalidateWidgetReason::Paint);
}

float UMSDamageFeedbackWidget::GetFeedbackAlpha() const
{
	const float NormalizedTime = FMath::Clamp(
		ElapsedSeconds / FeedbackDurationSeconds, 0.0f, 1.0f);
	return PeakIntensity * FMath::Square(1.0f - NormalizedTime);
}

void UMSDamageFeedbackWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ElapsedSeconds += InDeltaTime;
	if (ElapsedSeconds >= FeedbackDurationSeconds)
	{
		PeakIntensity = 0.0f;
		SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UMSDamageFeedbackWidget::NativePaint(
	const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const float Alpha = GetFeedbackAlpha();
	if (Alpha <= KINDA_SMALL_NUMBER)
	{
		return BaseLayer;
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const float EdgeWidth = FMath::Clamp(FMath::Min(Size.X, Size.Y) * 0.085f, 42.0f, 110.0f);
	const FLinearColor EdgeColor(0.78f, 0.0f, 0.015f, MaximumEdgeOpacity * Alpha);
	auto DrawEdge = [&](const FVector2D& Position, const FVector2D& DrawSize)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 1,
			AllottedGeometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(Position)),
			WhiteBrush, ESlateDrawEffect::None, EdgeColor);
	};
	DrawEdge(FVector2D::ZeroVector, FVector2D(Size.X, EdgeWidth));
	DrawEdge(FVector2D(0.0f, Size.Y - EdgeWidth), FVector2D(Size.X, EdgeWidth));
	DrawEdge(FVector2D::ZeroVector, FVector2D(EdgeWidth, Size.Y));
	DrawEdge(FVector2D(Size.X - EdgeWidth, 0.0f), FVector2D(EdgeWidth, Size.Y));

	const FVector2D Center = Size * 0.5f;
	const float Radius = FMath::Min(Size.X, Size.Y) * 0.25f;
	const FVector2D Tip = Center + DamageDirection * Radius;
	const FVector2D Perpendicular(-DamageDirection.Y, DamageDirection.X);
	const float ChevronHalfWidth = FMath::Clamp(Size.Y * 0.018f, 14.0f, 28.0f);
	const float ChevronDepth = ChevronHalfWidth * 1.4f;
	const TArray<FVector2D> Chevron{
		Tip - DamageDirection * ChevronDepth + Perpendicular * ChevronHalfWidth,
		Tip,
		Tip - DamageDirection * ChevronDepth - Perpendicular * ChevronHalfWidth
	};
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2,
		AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform()), Chevron, ESlateDrawEffect::None,
		FLinearColor(1.0f, 0.055f, 0.02f, FMath::Min(0.95f, 0.4f + Alpha)),
		true, FMath::Lerp(3.0f, 7.0f, Alpha));
	return BaseLayer + 2;
}
