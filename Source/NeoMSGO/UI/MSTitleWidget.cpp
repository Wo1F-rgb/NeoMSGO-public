// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSTitleWidget.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSLoginWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUITransitionSubsystem.h"

const FName UMSTitleWidget::TitleScreenId(TEXT("Title"));
const TCHAR* UMSTitleWidget::BackgroundTexturePath = TEXT("/Game/UI/Title/T_TitleBackground.T_TitleBackground");

void UMSTitleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildReferenceLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSTitleWidget::BuildReferenceLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;

	// Opaque fallback stays behind the background image so a missing texture never
	// exposes the paused lobby level behind the title screen.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.005f, 0.01f, 0.02f, 1.0f));
	UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop);
	BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); BackdropSlot->SetOffsets(FMargin(0.0f));

	BackgroundImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TitleBackgroundImage"));
	if (UTexture2D* Background = LoadObject<UTexture2D>(nullptr, BackgroundTexturePath))
	{
		BackgroundImage->SetBrushFromTexture(Background, false);
	}
	else
	{
		BackgroundImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	UCanvasPanelSlot* ImageSlot = Root->AddChildToCanvas(BackgroundImage);
	ImageSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); ImageSlot->SetOffsets(FMargin(0.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* ContentSlot = Root->AddChildToCanvas(Content);
	ContentSlot->SetAnchors(FAnchors(0.5f, 0.78f)); ContentSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	ContentSlot->SetAutoSize(true);

	UTextBlock* StartText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("ゲームスタート")),
		MSUIStyle::ETextRole::H1, true, MSUIStyleColor::TextPrimary,
		MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	StartText->SetJustification(ETextJustify::Center);
	StartButton = MSUIStyle::MakeCapsuleButton(WidgetTree, StartText,
		FLinearColor(0.02f, 0.12f, 0.2f, 0.92f), TEXT("TitleStartButton"));
	StartButton->OnClicked.AddDynamic(this, &UMSTitleWidget::HandleStartClicked);
	USizeBox* StartSize = WidgetTree->ConstructWidget<USizeBox>();
	StartSize->SetWidthOverride(340.0f); StartSize->SetHeightOverride(58.0f);
	StartSize->SetContent(StartButton);
	Content->AddChildToVerticalBox(StartSize)->SetHorizontalAlignment(HAlign_Center);

	UTextBlock* Hint = MSUIStyle::MakeText(WidgetTree,
		FText::FromString(TEXT("クリックしてログイン画面へ")), MSUIStyle::ETextRole::Caption,
		false, MSUIStyleColor::TextSecondary);
	Hint->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0, MSUIStyleSize::Space2, 0, 0));
}

void UMSTitleWidget::Open()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
		{
			Bgm->PlayScene(EMSBgmScene::Title);
		}
	}
	if (GetWorld() && !IsInViewport()) AddToViewport(90);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (bIsOpen) return;
	bIsOpen = true;
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem())
	{
		// The title screen is the logged-out root: Esc must never dismiss it, so the
		// close-request delegate refuses (returning false keeps the screen on the stack).
		Lock->PushScreen(TitleScreenId,
			FMSUICloseRequestDelegate::CreateWeakLambda(this, []() { return false; }));
	}
}

void UMSTitleWidget::Close()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (!bIsOpen) return;
	bIsOpen = false;
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TitleScreenId);
}

UMSTitleWidget* UMSTitleWidget::OpenTitleScreen(APlayerController* PlayerController)
{
	if (!PlayerController) return nullptr;
	TArray<UUserWidget*> Existing;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayerController, Existing, StaticClass(), false);
	UMSTitleWidget* Widget = Existing.IsEmpty()
		? CreateWidget<UMSTitleWidget>(PlayerController) : Cast<UMSTitleWidget>(Existing[0]);
	if (Widget) Widget->Open();
	return Widget;
}

bool UMSTitleWidget::EnsureLoginWidget()
{
	if (!LoginWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSLoginWidget::StaticClass(), false);
		LoginWidget = Existing.IsEmpty() ? nullptr : Cast<UMSLoginWidget>(Existing[0]);
		if (!LoginWidget)
		{
			APlayerController* PC = GetOwningPlayer();
			if (!PC) return false;
			LoginWidget = CreateWidget<UMSLoginWidget>(PC);
		}
	}
	return LoginWidget != nullptr;
}

void UMSTitleWidget::HandleStartClicked()
{
	if (!EnsureLoginWidget()) return;
	Close();
	LoginWidget->Open();
}

UMSUILockSubsystem* UMSTitleWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSTitleWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
#endif
