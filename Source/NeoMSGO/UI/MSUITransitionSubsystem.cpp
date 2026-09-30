// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSUITransitionSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectGlobals.h"

namespace MSUITransition
{
	// Short timings keep menu navigation responsive while leaving a readable covered frame.
	constexpr float CloseSeconds = 0.16f;
	constexpr float CoveredHoldSeconds = 0.04f;
	constexpr float OpenSeconds = 0.18f;
	constexpr float EntranceSeconds = 0.24f;
	constexpr float EntranceDistance = 42.0f;
	constexpr int32 ViewportZOrder = 10000;

	float EaseOutCubic(const float Alpha)
	{
		const float Inverse = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
		return 1.0f - Inverse * Inverse * Inverse;
	}

	float EaseInOutCubic(const float Alpha)
	{
		const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return A < 0.5f ? 4.0f * A * A * A : 1.0f - FMath::Pow(-2.0f * A + 2.0f, 3.0f) * 0.5f;
	}
}

void UMSUITransitionOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildOverlay();
}

void UMSUITransitionOverlay::BuildOverlay()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TransitionRoot"));
	WidgetTree->RootWidget = Root;

	InputBlocker = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TransitionInputBlocker"));
	InputBlocker->SetBrushColor(FLinearColor::Transparent);
	if (UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(InputBlocker))
	{
		CanvasSlot->SetAnchors(FAnchors(0, 0, 1, 1)); CanvasSlot->SetOffsets(FMargin(0)); CanvasSlot->SetZOrder(0);
	}

	auto MakeShutter = [this](const FName Name, const bool bTop)
	{
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Panel->SetBrushColor(FLinearColor(0.012f, 0.025f, 0.04f, 1.0f));
		Panel->SetPadding(bTop ? FMargin(0, 0, 0, 3) : FMargin(0, 3, 0, 0));

		UOverlay* Detail = WidgetTree->ConstructWidget<UOverlay>();
		UBorder* InnerPlate = WidgetTree->ConstructWidget<UBorder>();
		InnerPlate->SetBrushColor(FLinearColor(0.035f, 0.065f, 0.085f, 1.0f));
		Detail->AddChildToOverlay(InnerPlate);
		UBorder* Seam = WidgetTree->ConstructWidget<UBorder>();
		Seam->SetBrushColor(FLinearColor(0.08f, 0.55f, 0.72f, 1.0f));
		if (UOverlaySlot* SeamSlot = Detail->AddChildToOverlay(Seam))
		{
			SeamSlot->SetHorizontalAlignment(HAlign_Fill);
			SeamSlot->SetVerticalAlignment(bTop ? VAlign_Bottom : VAlign_Top);
			SeamSlot->SetPadding(bTop ? FMargin(0, 0, 0, 10) : FMargin(0, 10, 0, 0));
		}
		Panel->SetContent(Detail);
		return Panel;
	};

	TopShutter = MakeShutter(TEXT("TransitionTopShutter"), true);
	BottomShutter = MakeShutter(TEXT("TransitionBottomShutter"), false);
	if (UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(TopShutter))
	{
		CanvasSlot->SetAnchors(FAnchors(0, 0, 1, 0.5f)); CanvasSlot->SetOffsets(FMargin(0)); CanvasSlot->SetZOrder(1);
	}
	if (UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(BottomShutter))
	{
		CanvasSlot->SetAnchors(FAnchors(0, 0.5f, 1, 1)); CanvasSlot->SetOffsets(FMargin(0)); CanvasSlot->SetZOrder(1);
	}
	SetVisibility(ESlateVisibility::Visible);
}

void UMSUITransitionOverlay::StartShutter(UUserWidget* InTarget, FSimpleDelegate InSwapScreen)
{
	TargetWidget = InTarget;
	SwapScreen = MoveTemp(InSwapScreen);
	ElapsedSeconds = 0.0f;
	bScreenSwapped = false;
	Mode = EMode::Shutter;
	InputBlocker->SetVisibility(ESlateVisibility::Visible);
	TopShutter->SetVisibility(ESlateVisibility::Visible);
	BottomShutter->SetVisibility(ESlateVisibility::Visible);
	SetShutterAmount(0.0f, 1080.0f);
}

void UMSUITransitionOverlay::StartEntrance(UUserWidget* InTarget)
{
	TargetWidget = InTarget;
	SwapScreen.Unbind();
	ElapsedSeconds = 0.0f;
	bScreenSwapped = true;
	Mode = EMode::Entrance;
	InputBlocker->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TopShutter->SetVisibility(ESlateVisibility::Collapsed);
	BottomShutter->SetVisibility(ESlateVisibility::Collapsed);
	SetEntranceAmount(0.0f);
}

void UMSUITransitionOverlay::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Mode == EMode::Idle) return;
	ElapsedSeconds += FMath::Max(0.0f, InDeltaTime);

	if (Mode == EMode::Entrance)
	{
		const float Alpha = MSUITransition::EaseOutCubic(ElapsedSeconds / MSUITransition::EntranceSeconds);
		SetEntranceAmount(Alpha);
		if (ElapsedSeconds >= MSUITransition::EntranceSeconds) Finish();
		return;
	}

	const float ViewportHeight = FMath::Max(MyGeometry.GetLocalSize().Y, 1.0f);
	if (ElapsedSeconds < MSUITransition::CloseSeconds)
	{
		SetShutterAmount(MSUITransition::EaseOutCubic(ElapsedSeconds / MSUITransition::CloseSeconds), ViewportHeight);
		return;
	}

	SetShutterAmount(1.0f, ViewportHeight);
	if (!bScreenSwapped)
	{
		bScreenSwapped = true;
		SwapScreen.ExecuteIfBound();
		SetEntranceAmount(0.0f);
	}

	const float OpenStart = MSUITransition::CloseSeconds + MSUITransition::CoveredHoldSeconds;
	if (ElapsedSeconds >= OpenStart)
	{
		const float Alpha = MSUITransition::EaseInOutCubic((ElapsedSeconds - OpenStart) / MSUITransition::OpenSeconds);
		SetShutterAmount(1.0f - Alpha, ViewportHeight);
		SetEntranceAmount(Alpha);
		if (ElapsedSeconds >= OpenStart + MSUITransition::OpenSeconds) Finish();
	}
}

void UMSUITransitionOverlay::SetShutterAmount(const float ClosedAmount, const float ViewportHeight)
{
	const float Offset = ViewportHeight * 0.5f * (1.0f - FMath::Clamp(ClosedAmount, 0.0f, 1.0f));
	if (TopShutter) TopShutter->SetRenderTranslation(FVector2D(0.0f, -Offset));
	if (BottomShutter) BottomShutter->SetRenderTranslation(FVector2D(0.0f, Offset));
}

void UMSUITransitionOverlay::SetEntranceAmount(const float Amount)
{
	if (!TargetWidget) return;
	const float Alpha = FMath::Clamp(Amount, 0.0f, 1.0f);
	TargetWidget->SetRenderOpacity(Alpha);
	TargetWidget->SetRenderTranslation(FVector2D(0.0f, MSUITransition::EntranceDistance * (1.0f - Alpha)));
}

void UMSUITransitionOverlay::Finish()
{
	SetEntranceAmount(1.0f);
	if (TargetWidget)
	{
		TargetWidget->SetRenderOpacity(1.0f);
		TargetWidget->SetRenderTranslation(FVector2D::ZeroVector);
	}
	SwapScreen.Unbind();
	TargetWidget = nullptr;
	Mode = EMode::Idle;
	RemoveFromParent();
}

void UMSUITransitionOverlay::Cancel()
{
	// Travel invalidates the pending destination. Restore its appearance without
	// invoking a delegate that belongs to the old controller or world.
	Finish();
}

void UMSUITransitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(
		this, &UMSUITransitionSubsystem::HandlePreLoadMap);
}

void UMSUITransitionSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString&)
{
	if (WorldContext.OwningGameInstance == GetGameInstance()) ResetOverlay();
}

void UMSUITransitionSubsystem::ResetOverlay()
{
	if (Overlay) Overlay->Cancel();
	Overlay = nullptr;
}

bool UMSUITransitionSubsystem::CanAnimate(const UUserWidget* Context)
{
	if (!Context || GIsAutomationTesting || IsRunningCommandlet()) return false;
	const UGameInstance* GameInstance = Context->GetGameInstance();
	return GameInstance && GameInstance->GetWorld() && GEngine && GEngine->GameViewport;
}

bool UMSUITransitionSubsystem::Transition(UUserWidget* Context, UUserWidget* Target, FSimpleDelegate SwapScreen)
{
	if (!CanAnimate(Context))
	{
		SwapScreen.ExecuteIfBound();
		return true;
	}
	UMSUITransitionSubsystem* Subsystem = Context->GetGameInstance()->GetSubsystem<UMSUITransitionSubsystem>();
	return Subsystem ? Subsystem->BeginTransition(Context, Target, MoveTemp(SwapScreen)) : false;
}

void UMSUITransitionSubsystem::PlayEntrance(UUserWidget* Widget)
{
	if (!CanAnimate(Widget)) return;
	if (UMSUITransitionSubsystem* Subsystem = Widget->GetGameInstance()->GetSubsystem<UMSUITransitionSubsystem>())
	{
		Subsystem->BeginEntrance(Widget);
	}
}

bool UMSUITransitionSubsystem::BeginTransition(UUserWidget* Context, UUserWidget* Target, FSimpleDelegate SwapScreen)
{
	UMSUITransitionOverlay* TransitionOverlay = GetOrCreateOverlay(Context);
	if (!TransitionOverlay || TransitionOverlay->IsAnimating()) return false;
	TransitionOverlay->AddToViewport(MSUITransition::ViewportZOrder);
	if (!TransitionOverlay->IsInViewport())
	{
		SwapScreen.ExecuteIfBound();
		return true;
	}
	TransitionOverlay->StartShutter(Target, MoveTemp(SwapScreen));
	return true;
}

void UMSUITransitionSubsystem::BeginEntrance(UUserWidget* Widget)
{
	UMSUITransitionOverlay* TransitionOverlay = GetOrCreateOverlay(Widget);
	// An Open() invoked at the covered midpoint is already animated by StartShutter.
	if (!TransitionOverlay || TransitionOverlay->IsAnimating()) return;
	TransitionOverlay->AddToViewport(MSUITransition::ViewportZOrder);
	if (!TransitionOverlay->IsInViewport()) return;
	TransitionOverlay->StartEntrance(Widget);
}

UMSUITransitionOverlay* UMSUITransitionSubsystem::GetOrCreateOverlay(UUserWidget* Context)
{
	APlayerController* PlayerController = Context ? Context->GetOwningPlayer() : nullptr;
	if (!PlayerController && GetGameInstance()) PlayerController = GetGameInstance()->GetPrimaryPlayerController(false);
	// The subsystem survives OpenLevel; its Slate driver and owning controller do not.
	if (Overlay && (Overlay->GetOwningPlayer() != PlayerController
		|| Overlay->GetWorld() != (Context ? Context->GetWorld() : nullptr))) ResetOverlay();
	if (Overlay) return Overlay;
	if (PlayerController) Overlay = CreateWidget<UMSUITransitionOverlay>(PlayerController);
	return Overlay;
}

bool UMSUITransitionSubsystem::IsTransitioning() const
{
	return Overlay && Overlay->IsAnimating();
}

void UMSUITransitionSubsystem::Deinitialize()
{
	if (PreLoadMapHandle.IsValid()) FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	PreLoadMapHandle.Reset();
	ResetOverlay();
	Super::Deinitialize();
}
