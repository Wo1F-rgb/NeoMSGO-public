#include "UI/MSShopWidget.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUILockSubsystem.h"

namespace MSShop
{
	const FLinearColor DimColor(0.005f, 0.012f, 0.018f, 0.86f);
	const FLinearColor CardColor(0.08f, 0.045f, 0.035f, 0.97f);
	const FLinearColor AccentColor(0.92f, 0.24f, 0.22f, 1.0f);
}

void UMSShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true); BuildReferenceLayout(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSShopWidget::BuildReferenceLayout()
{
	UBorder* Dimmer = WidgetTree->ConstructWidget<UBorder>();
	Dimmer->SetBrushColor(MSShop::DimColor); Dimmer->SetPadding(FMargin(160, 80));
	WidgetTree->RootWidget = Dimmer;
	UVerticalBox* Screen = WidgetTree->ConstructWidget<UVerticalBox>(); Dimmer->SetContent(Screen);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Screen->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 18));
	UTextBlock* Title = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("SHOP / ショップ")), MSUIStyle::ETextRole::H1);
	Header->AddChildToHorizontalBox(Title)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* BackText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("戻る")), MSUIStyle::ETextRole::Body, true,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	BackText->SetJustification(ETextJustify::Center);
	UButton* Back = MSUIStyle::MakeCapsuleButton(WidgetTree, BackText, MSShop::CardColor,
		TEXT("ShopBackButton"), MSUIStyle::EButtonSound::Back);
	Back->OnClicked.AddDynamic(this, &UMSShopWidget::HandleBackClicked);
	USizeBox* BackSize = WidgetTree->ConstructWidget<USizeBox>(); BackSize->SetWidthOverride(130); BackSize->SetHeightOverride(42); BackSize->SetContent(Back);
	Header->AddChildToHorizontalBox(BackSize);

	const TCHAR* Titles[] = { TEXT("購入"), TEXT("ガシャコン"), TEXT("売却"), TEXT("買戻し"), TEXT("アイテムコード"), TEXT("交換所") };
	const TCHAR* Descriptions[] = { TEXT("設計図パックや開発素材、アイテムを購入"), TEXT("各種ガシャコンをプレイ"),
		TEXT("機体や開発素材、アイテムを売却"), TEXT("売却した開発素材とアイテムを再購入"),
		TEXT("特定のコードを入力してアイテムを取得"), TEXT("アイテムや設計図への交換") };
	const FName Names[] = { TEXT("PurchaseButton"), TEXT("GashaconButton"), TEXT("SellButton"),
		TEXT("BuybackButton"), TEXT("ItemCodeButton"), TEXT("ExchangeButton") };
	for (int32 RowIndex = 0; RowIndex < 3; ++RowIndex)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Screen->AddChildToVerticalBox(Row)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Index = RowIndex * 2 + Column;
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Names[Index]);
			MSUIStyle::ApplyContentButtonStyle(Button);
			Button->SetIsEnabled(false);
			UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
			UBorder* Card = nullptr;
			UBorder* Frame = MSUIStyle::MakeFramedPanel(WidgetTree, Card, Content, false,
				MSShop::AccentColor, MSShop::CardColor, FMargin(22, 15));
			Content->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(Titles[Index]), MSUIStyle::ETextRole::H2));
			Content->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(Descriptions[Index]), MSUIStyle::ETextRole::Caption,
				false, FLinearColor(0.95f, 0.48f, 0.45f, 1)))->SetPadding(FMargin(0, 5, 0, 0));
			Button->SetContent(Frame);
			UHorizontalBoxSlot* CardSlot = Row->AddChildToHorizontalBox(Button); CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CardSlot->SetPadding(FMargin(7));
		}
	}
}

void UMSShopWidget::Open()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
		{
			Bgm->PlayScene(EMSBgmScene::Shop);
		}
	}
	if (GetWorld() && !IsInViewport()) AddToViewport(80);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem())
		Lock->PushScreen(TEXT("Shop"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSShopWidget::Close()
{
	if (!IsOpen()) return;
	if (LobbyWidget)
	{
		UMSUITransitionSubsystem::Transition(this, LobbyWidget,
			FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseImmediate(); LobbyWidget->Open(); }));
		return;
	}
	CloseImmediate();
}

FReply UMSShopWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSShopWidget::CloseImmediate()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TEXT("Shop"));
	if (GetWorld()) UGameplayStatics::SetGamePaused(this, false);
}

UMSUILockSubsystem* UMSShopWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void UMSShopWidget::HandleBackClicked() { Close(); }

#if WITH_DEV_AUTOMATION_TESTS
void UMSShopWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
#endif
