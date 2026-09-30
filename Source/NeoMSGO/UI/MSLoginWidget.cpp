// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSLoginWidget.h"

#include "Audio/MSBgmSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSAuthSubsystem.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSLocalServerLauncher.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSTitleWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSUITransitionSubsystem.h"
#include "Voice/MSNavigatorSubsystem.h"

const FName UMSLoginWidget::LoginScreenId(TEXT("Login"));

namespace MSLogin
{
	const FLinearColor PanelColor(0.015f, 0.03f, 0.05f, 0.97f);
	const FLinearColor FieldColor(0.04f, 0.07f, 0.1f, 1.0f);

	// PC毎に前回ログインしたユーザー名を記憶する(GameUserSettings.ini)。
	const TCHAR* RememberSection = TEXT("/Script/NeoMSGO.MSLoginWidget");
	const TCHAR* RememberUserKey = TEXT("LastUsername");

	void SaveRememberedUsername(const FString& Username)
	{
		GConfig->SetString(RememberSection, RememberUserKey, *Username, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	FString LoadRememberedUsername()
	{
		FString Username;
		GConfig->GetString(RememberSection, RememberUserKey, Username, GGameUserSettingsIni);
		return Username;
	}
}

void UMSLoginWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildReferenceLayout();
	if (UMSAuthSubsystem* Auth = ResolveAuthSubsystem())
	{
		SetOnlineControlsVisible(Auth->IsOnlineMode());
		if (ServerUrlText && Auth->IsOnlineMode())
		{
			ServerUrlText->SetText(FText::Format(FText::FromString(TEXT("接続先: {0}")),
				FText::FromString(Auth->GetOnlineAuthBaseUrl())));
		}
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSLoginWidget::BuildReferenceLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.005f, 0.012f, 0.022f, 1.0f));
	UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop);
	BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); BackdropSlot->SetOffsets(FMargin(0.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* Panel = nullptr;
	UBorder* Frame = MSUIStyle::MakeFramedPanel(WidgetTree, Panel, Content, true,
		MSUIStyleColor::FrameAccent, MSLogin::PanelColor, FMargin(28.0f, 24.0f));
	USizeBox* CenterBox = WidgetTree->ConstructWidget<USizeBox>();
	CenterBox->SetWidthOverride(600.0f);
	CenterBox->SetContent(Frame);
	UCanvasPanelSlot* CenterSlot = Root->AddChildToCanvas(CenterBox);
	CenterSlot->SetAnchors(FAnchors(0.5f, 0.5f)); CenterSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CenterSlot->SetAutoSize(true);

	UTextBlock* Title = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("パイロット認証")),
		MSUIStyle::ETextRole::H1, true);
	Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space4));
	ServerUrlText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption,
		false, MSUIStyleColor::TextSecondary, MSUIStyle::ENumericLargeSize::Standard, TEXT("LoginServerUrlText"));
	ServerUrlText->SetAutoWrapText(true);
	Content->AddChildToVerticalBox(ServerUrlText)->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space2));

	auto AddField = [this, Content](const TCHAR* Label, const FName Name, const bool bPassword)
	{
		UTextBlock* LabelText = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label),
			MSUIStyle::ETextRole::Caption, false, MSUIStyleColor::TextSecondary);
		Content->AddChildToVerticalBox(LabelText)
			->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space1));
		if (bPassword) PasswordLabel = LabelText;
		UEditableTextBox* Box = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
		Box->SetIsPassword(bPassword);
		Box->WidgetStyle.BackgroundImageNormal.TintColor = MSLogin::FieldColor;
		Box->WidgetStyle.BackgroundImageHovered.TintColor = MSLogin::FieldColor;
		Box->WidgetStyle.BackgroundImageFocused.TintColor = MSLogin::FieldColor;
		Box->WidgetStyle.SetPadding(FMargin(10.0f, 8.0f));
		Content->AddChildToVerticalBox(Box)->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space3));
		return Box;
	};
	UsernameBox = AddField(TEXT("ユーザー名"), TEXT("LoginUsernameBox"), false);
	PasswordBox = AddField(TEXT("パスワード"), TEXT("LoginPasswordBox"), true);

	ErrorText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption,
		false, MSUIStyleColor::WarningColor, MSUIStyle::ENumericLargeSize::Standard, TEXT("LoginErrorText"));
	ErrorText->SetAutoWrapText(true);
	ErrorText->SetVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(ErrorText)->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space3));
	StatusText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption,
		false, MSUIStyleColor::TextSecondary, MSUIStyle::ENumericLargeSize::Standard, TEXT("LoginStatusText"));
	StatusText->SetAutoWrapText(true);
	Content->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0, 0, 0, MSUIStyleSize::Space2));

	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Actions);
	auto AddAction = [this, Actions](const TCHAR* Label, const FName Name)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label),
			MSUIStyle::ETextRole::Body, true, MSUIStyleColor::TextPrimary,
			MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Text,
			FLinearColor(0.03f, 0.14f, 0.22f, 1.0f), Name);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetHeightOverride(44.0f);
		Size->SetContent(Button);
		UHorizontalBoxSlot* Slot = Actions->AddChildToHorizontalBox(Size);
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	LoginButton = AddAction(TEXT("ログイン"), TEXT("LoginSubmitButton"));
	LoginButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleLoginClicked);
	GuestButton = AddAction(TEXT("ゲスト"), TEXT("LoginGuestButton"));
	GuestButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleGuestClicked);
	RegisterButton = AddAction(TEXT("新規登録"), TEXT("LoginRegisterButton"));
	RegisterButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleRegisterClicked);

	UHorizontalBox* SessionActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(SessionActions)->SetPadding(FMargin(0, MSUIStyleSize::Space2, 0, 0));
	auto AddSessionAction = [this, SessionActions](const TCHAR* Label, const FName Name)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label), MSUIStyle::ETextRole::Caption,
			true, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Text,
			FLinearColor(0.02f, 0.08f, 0.13f, 1.0f), Name);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetHeightOverride(36.0f); Size->SetContent(Button);
		UHorizontalBoxSlot* Slot = SessionActions->AddChildToHorizontalBox(Size);
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(4.0f, 0.0f));
		return Button;
	};
	ReconnectButton = AddSessionAction(TEXT("ゲストで再開"), TEXT("LoginReconnectButton"));
	ReconnectButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleReconnectClicked);
	LogoutButton = AddSessionAction(TEXT("ログアウト"), TEXT("LoginLogoutButton"));
	LogoutButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleLogoutClicked);
	ContinueButton = AddSessionAction(TEXT("続けて入場"), TEXT("LoginContinueButton"));
	ContinueButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleContinueClicked);
	UTextBlock* StartServerText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("このPCでサーバーを起動")), MSUIStyle::ETextRole::Body, true);
	StartServerText->SetJustification(ETextJustify::Center);
	StartServerButton = MSUIStyle::MakeCapsuleButton(WidgetTree, StartServerText,
		FLinearColor(0.03f, 0.14f, 0.22f, 1.0f), TEXT("LoginStartLocalServerButton"));
	USizeBox* StartServerSize = WidgetTree->ConstructWidget<USizeBox>();
	StartServerSize->SetMinDesiredHeight(44.0f); StartServerSize->SetContent(StartServerButton);
	Content->AddChildToVerticalBox(StartServerSize)->SetPadding(FMargin(4.0f, 12.0f, 4.0f, 0.0f));
	StartServerButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleStartServerClicked);
	SetOnlineControlsVisible(false);
	if (ContinueButton) ContinueButton->SetVisibility(ESlateVisibility::Collapsed);

	// Reference frames f156-162 place a "return to title" exit on the post-title screen's lower left.
	UTextBlock* BackText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("タイトル画面へ戻る")),
		MSUIStyle::ETextRole::Caption, false, MSUIStyleColor::TextSecondary,
		MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	BackText->SetJustification(ETextJustify::Center);
	BackButton = MSUIStyle::MakeCapsuleButton(WidgetTree, BackText,
		FLinearColor(0.02f, 0.05f, 0.08f, 1.0f), TEXT("LoginBackButton"), MSUIStyle::EButtonSound::Back);
	BackButton->OnClicked.AddDynamic(this, &UMSLoginWidget::HandleBackClicked);
	USizeBox* BackSize = WidgetTree->ConstructWidget<USizeBox>();
	BackSize->SetWidthOverride(190.0f); BackSize->SetHeightOverride(36.0f);
	BackSize->SetContent(BackButton);
	UCanvasPanelSlot* BackSlot = Root->AddChildToCanvas(BackSize);
	BackSlot->SetAnchors(FAnchors(0.0f, 1.0f)); BackSlot->SetAlignment(FVector2D(0.0f, 1.0f));
	BackSlot->SetPosition(FVector2D(40.0f, -32.0f)); BackSlot->SetAutoSize(true);
}

void UMSLoginWidget::Open()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
		{
			Bgm->PlayScene(EMSBgmScene::Title);
		}
	}
	if (GetWorld() && !IsInViewport()) AddToViewport(90);
	SetVisibility(ESlateVisibility::Visible);
	// このPCで前回ログインした名前を自動入力する(空欄なら手入力)。
	const FString RememberedUser = MSLogin::LoadRememberedUsername();
	if (UsernameBox && UsernameBox->GetText().IsEmpty() && !RememberedUser.IsEmpty())
	{
		UsernameBox->SetText(FText::FromString(RememberedUser));
	}
	if (UsernameBox) UsernameBox->SetKeyboardFocus();
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(bLocalServerStarting ? FText::FromString(TEXT("このPCのサーバーを起動・確認しています…")) : FText::GetEmpty());
	if (ContinueButton) ContinueButton->SetVisibility(ESlateVisibility::Collapsed);
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (bIsOpen) return;
	bIsOpen = true;
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem())
	{
		// Esc returns to the title screen, matching the explicit back button.
		Lock->PushScreen(LoginScreenId,
			FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { HandleBackClicked(); return true; }));
	}
}

void UMSLoginWidget::Close()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (!bIsOpen) return;
	bIsOpen = false;
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(LoginScreenId);
}

FReply UMSLoginWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		HandleBackClicked();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

UMSLoginWidget* UMSLoginWidget::OpenLoginScreen(APlayerController* PlayerController)
{
	if (!PlayerController) return nullptr;
	TArray<UUserWidget*> Existing;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayerController, Existing, StaticClass(), false);
	UMSLoginWidget* Widget = Existing.IsEmpty()
		? CreateWidget<UMSLoginWidget>(PlayerController) : Cast<UMSLoginWidget>(Existing[0]);
	if (Widget) Widget->Open();
	return Widget;
}

void UMSLoginWidget::ShowError(const FText& Message)
{
	if (!ErrorText) return;
	ErrorText->SetText(Message);
	ErrorText->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UMSLoginWidget::HandleLoginClicked()
{
	const FString Username = UsernameBox ? UsernameBox->GetText().ToString() : FString();
	if (Username.TrimStartAndEnd().IsEmpty())
	{
		ShowError(FText::FromString(TEXT("ユーザー名を入力してください。")));
		return;
	}
	if (UMSAuthSubsystem* OnlineAuth = ResolveAuthSubsystem(); OnlineAuth && OnlineAuth->IsOnlineMode())
	{
		BeginOnlineLogin(false);
		return;
	}
	// 未登録名はUMSAuthSubsystem::Login内で自動登録されるため、重複エラーは存在しない。
	UMSAuthSubsystem* Auth = ResolveAuthSubsystem();
	if (!Auth || !Auth->Login(Username))
	{
		ShowError(FText::FromString(TEXT("ログインに失敗しました。もう一度お試しください。")));
		return;
	}
	MSLogin::SaveRememberedUsername(Username.TrimStartAndEnd());
	EnterLobby();
}

void UMSLoginWidget::HandleGuestClicked()
{
	if (UMSAuthSubsystem* OnlineAuth = ResolveAuthSubsystem(); OnlineAuth && OnlineAuth->IsOnlineMode())
	{
		if (bOnlineRequestPending) return;
		bOnlineRequestPending = true;
		SetOnlineControlsVisible(true);
		if (StatusText) StatusText->SetText(FText::FromString(TEXT("ゲスト認証中…")));
		TWeakObjectPtr<UMSLoginWidget> WeakThis(this);
		OnlineAuth->LoginAsGuestOnline(FString(), [WeakThis](const bool bSucceeded, const FString& Message)
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->FinishOnlineRequest(bSucceeded, Message);
		});
		return;
	}
	// 設定不要のゲスト入場: ゲストNの空き番号で自動登録+ログインする。
	UMSAuthSubsystem* Auth = ResolveAuthSubsystem();
	const FString GuestName = Auth ? Auth->LoginAsGuest() : FString();
	if (GuestName.IsEmpty())
	{
		ShowError(FText::FromString(TEXT("ゲストログインに失敗しました。もう一度お試しください。")));
		return;
	}
	MSLogin::SaveRememberedUsername(GuestName);
	EnterLobby();
}

void UMSLoginWidget::HandleRegisterClicked()
{
	if (UMSAuthSubsystem* Auth = ResolveAuthSubsystem())
	{
		if (Auth->IsOnlineMode())
		{
			BeginOnlineLogin(true);
			return;
		}
		const FString Username = UsernameBox ? UsernameBox->GetText().ToString() : FString();
		if (Username.TrimStartAndEnd().IsEmpty() || !Auth->RegisterAccount(Username))
		{
			ShowError(FText::FromString(TEXT("登録に失敗しました。")));
			return;
		}
		ShowError(FText::GetEmpty());
		if (StatusText) StatusText->SetText(FText::FromString(TEXT("登録しました。ログインしてください。")));
	}
}

void UMSLoginWidget::HandleReconnectClicked()
{
	if (bOnlineRequestPending) return;
	UMSAuthSubsystem* Auth = ResolveAuthSubsystem();
	if (!Auth || !Auth->IsOnlineMode()) return;
	bOnlineRequestPending = true;
	if (StatusText) StatusText->SetText(FText::FromString(TEXT("前回のゲスト情報で再接続中…")));
	TWeakObjectPtr<UMSLoginWidget> WeakThis(this);
	Auth->ResumeGuestOnline([WeakThis](const bool bSucceeded, const FString& Message)
	{
		if (!WeakThis.IsValid()) return;
		WeakThis->FinishOnlineRequest(bSucceeded, Message);
	});
}

void UMSLoginWidget::HandleStartServerClicked()
{
	UMSAuthSubsystem* Auth = ResolveAuthSubsystem();
	if (!Auth || !Auth->IsOnlineMode() || bOnlineRequestPending || bLocalServerStarting) return;
	bLocalServerStarting = true; bOnlineRequestPending = true;
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(FText::FromString(TEXT("このPCのサーバーを起動・確認しています…")));
	for (UButton* Button : {StartServerButton.Get(), LoginButton.Get(), GuestButton.Get(), RegisterButton.Get(), ReconnectButton.Get(), LogoutButton.Get()})
		if (Button) Button->SetIsEnabled(false);
	MSLocalServerLauncher::Start(Auth->GetOnlineAuthBaseUrl(), [WeakThis = TWeakObjectPtr<UMSLoginWidget>(this)](bool bSucceeded, const FString& Message)
	{
		if (!WeakThis.IsValid()) return;
		auto* Self = WeakThis.Get(); Self->bLocalServerStarting = false; Self->bOnlineRequestPending = false;
		for (UButton* Button : {Self->StartServerButton.Get(), Self->LoginButton.Get(), Self->GuestButton.Get(), Self->RegisterButton.Get(), Self->ReconnectButton.Get(), Self->LogoutButton.Get()})
			if (Button) Button->SetIsEnabled(true);
		Self->ShowError(bSucceeded ? FText::GetEmpty() : FText::FromString(Message));
		if (Self->StatusText) Self->StatusText->SetText(bSucceeded ? FText::FromString(Message) : FText::GetEmpty());
	});
}

void UMSLoginWidget::HandleLogoutClicked()
{
	if (bLocalServerStarting) return;
	if (UMSAuthSubsystem* Auth = ResolveAuthSubsystem()) Auth->LogoutOnline();
	// Logout can be pressed while an online login/guest/resume request is still
	// in flight.  The account generation invalidates the old callback, so the
	// widget must release its own pending guard here as well.
	bOnlineRequestPending = false;
	if (PasswordBox) PasswordBox->SetText(FText::GetEmpty());
	if (LoginButton) LoginButton->SetIsEnabled(true);
	if (RegisterButton) RegisterButton->SetIsEnabled(true);
	if (GuestButton) GuestButton->SetIsEnabled(true);
	if (ContinueButton) ContinueButton->SetVisibility(ESlateVisibility::Collapsed);
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(FText::FromString(TEXT("ログアウトしました。")));
}

void UMSLoginWidget::HandleContinueClicked()
{
	if (bOnlineRequestPending) return;
	if (ContinueButton) ContinueButton->SetVisibility(ESlateVisibility::Collapsed);
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(FText::GetEmpty());
	EnterLobby();
}

void UMSLoginWidget::BeginOnlineLogin(const bool bRegister)
{
	if (bOnlineRequestPending) return;
	UMSAuthSubsystem* Auth = ResolveAuthSubsystem();
	const FString Username = UsernameBox ? UsernameBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString Password = PasswordBox ? PasswordBox->GetText().ToString() : FString();
	if (!Auth || !Auth->IsOnlineMode()) return;
	if (Username.IsEmpty()) { ShowError(FText::FromString(TEXT("ユーザー名を入力してください。"))); return; }
	if (Password.Len() < 10 || Password.Len() > 128)
	{
		ShowError(FText::FromString(TEXT("パスワードは10〜128文字で入力してください。")));
		return;
	}
	bOnlineRequestPending = true;
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(FText::FromString(bRegister ? TEXT("登録中…") : TEXT("ログイン中…")));
	if (LoginButton) LoginButton->SetIsEnabled(false);
	if (RegisterButton) RegisterButton->SetIsEnabled(false);
	if (GuestButton) GuestButton->SetIsEnabled(false);
	if (StartServerButton) StartServerButton->SetIsEnabled(false);
	TWeakObjectPtr<UMSLoginWidget> WeakThis(this);
	FMSOnlineAuthCompletion Completion = [WeakThis](const bool bSucceeded, const FString& Message)
	{
		if (!WeakThis.IsValid()) return;
		WeakThis->FinishOnlineRequest(bSucceeded, Message);
	};
	if (bRegister) Auth->RegisterOnline(Username, Password, MoveTemp(Completion));
	else Auth->LoginOnline(Username, Password, MoveTemp(Completion));
}

void UMSLoginWidget::BeginOnlineGuestResume()
{
	HandleReconnectClicked();
}

void UMSLoginWidget::FinishOnlineRequest(const bool bSucceeded, const FString& Message)
{
	bOnlineRequestPending = false;
	if (PasswordBox) PasswordBox->SetText(FText::GetEmpty());
	if (LoginButton) LoginButton->SetIsEnabled(true);
	if (RegisterButton) RegisterButton->SetIsEnabled(true);
	if (GuestButton) GuestButton->SetIsEnabled(true);
	if (StartServerButton) StartServerButton->SetIsEnabled(true);
	if (!bSucceeded)
	{
		ShowError(FText::FromString(Message.IsEmpty() ? TEXT("認証に失敗しました。再試行してください。") : Message));
		if (StatusText) StatusText->SetText(FText::GetEmpty());
		return;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSOnlineAccountSubsystem* Online = GI->GetSubsystem<UMSOnlineAccountSubsystem>())
		{
			if (UsernameBox) MSLogin::SaveRememberedUsername(UsernameBox->GetText().ToString().TrimStartAndEnd());
			const FString Warning = Online->GetCredentialStorageWarning();
			if (!Warning.IsEmpty())
			{
				ShowError(FText::FromString(Warning));
				if (StatusText) StatusText->SetText(FText::FromString(TEXT("認証しました。内容を確認して入場してください。")));
				if (ContinueButton) ContinueButton->SetVisibility(ESlateVisibility::Visible);
				return;
			}
		}
	}
	ShowError(FText::GetEmpty());
	if (StatusText) StatusText->SetText(FText::FromString(TEXT("認証しました。")));
	if (UsernameBox) MSLogin::SaveRememberedUsername(UsernameBox->GetText().ToString().TrimStartAndEnd());
	EnterLobby();
}

void UMSLoginWidget::SetOnlineControlsVisible(const bool bVisible)
{
	const ESlateVisibility OnlineVisibility = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (ServerUrlText) ServerUrlText->SetVisibility(OnlineVisibility);
	if (PasswordLabel) PasswordLabel->SetVisibility(OnlineVisibility);
	if (PasswordBox) PasswordBox->SetVisibility(OnlineVisibility);
	if (RegisterButton) RegisterButton->SetVisibility(OnlineVisibility);
	if (ReconnectButton) ReconnectButton->SetVisibility(OnlineVisibility);
	if (LogoutButton) LogoutButton->SetVisibility(OnlineVisibility);
	if (StartServerButton)
	{
		const ESlateVisibility ServerVisibility = bVisible && MSLocalServerLauncher::IsAvailable()
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
		StartServerButton->SetVisibility(ServerVisibility);
		StartServerButton->SetIsEnabled(!bOnlineRequestPending);
		if (StartServerButton->GetParent()) StartServerButton->GetParent()->SetVisibility(ServerVisibility);
	}
}

void UMSLoginWidget::EnterLobby()
{
	ShowError(FText::GetEmpty());
	Close();
	if (AMSPlayerCharacter* Player = ResolveLobbyPlayer()) Player->OpenLobbyScreen();
	// ログイン成功後、選択中ナビゲーターのログインボイスを再生する。
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSNavigatorSubsystem* Navigator = GI->GetSubsystem<UMSNavigatorSubsystem>())
		{
			Navigator->PlayLine(EMSNavigatorTrigger::Login);
		}
	}
}

bool UMSLoginWidget::EnsureTitleWidget()
{
	if (!TitleWidget)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, UMSTitleWidget::StaticClass(), false);
		TitleWidget = Existing.IsEmpty() ? nullptr : Cast<UMSTitleWidget>(Existing[0]);
		if (!TitleWidget)
		{
			APlayerController* PC = GetOwningPlayer();
			if (!PC) return false;
			TitleWidget = CreateWidget<UMSTitleWidget>(PC);
		}
	}
	return TitleWidget != nullptr;
}

void UMSLoginWidget::HandleBackClicked()
{
	if (!EnsureTitleWidget()) { Close(); return; }
	Close();
	TitleWidget->Open();
}

UMSAuthSubsystem* UMSLoginWidget::ResolveAuthSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestAuthSubsystem) return TestAuthSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSAuthSubsystem>() : nullptr;
}

UMSUILockSubsystem* UMSLoginWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

AMSPlayerCharacter* UMSLoginWidget::ResolveLobbyPlayer() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestLobbyPlayer) return TestLobbyPlayer;
#endif
	return Cast<AMSPlayerCharacter>(GetOwningPlayerPawn());
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSLoginWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }

FString UMSLoginWidget::GetErrorTextForTesting() const
{
	return (ErrorText && ErrorText->GetVisibility() != ESlateVisibility::Collapsed)
		? ErrorText->GetText().ToString() : FString();
}
#endif
