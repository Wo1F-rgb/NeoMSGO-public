// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSCommunityWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MSAvatarLibrary.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"

namespace MSCommunity
{
	const FName ScreenId(TEXT("Community"));
	const FLinearColor Backdrop(0.008f, 0.018f, 0.035f, 0.985f);
	const FLinearColor Panel(0.018f, 0.05f, 0.085f, 0.98f);
	const FLinearColor Row(0.026f, 0.075f, 0.115f, 0.96f);
	const FLinearColor Cyan(0.20f, 0.78f, 0.96f, 1.0f);
	const FLinearColor OfflineText(0.36f, 0.43f, 0.47f, 1.0f);
}

void UMSCommunityFriendRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>();
	RowSize->SetHeightOverride(40);
	WidgetTree->RootWidget = RowSize;
	RowBackground = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(RowBackground, MSUIStyle::EBrush::ListRow, MSCommunity::Row);
	RowBackground->SetPadding(FMargin(8, 5));
	RowSize->SetContent(RowBackground);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	RowBackground->SetContent(Row);
	// 立ち絵アバター(MSAvatarLibraryで解決できた名前のみRefreshPresentationで表示)
	AvatarBox = WidgetTree->ConstructWidget<USizeBox>();
	AvatarBox->SetWidthOverride(30); AvatarBox->SetHeightOverride(30);
	AvatarImage = WidgetTree->ConstructWidget<UImage>();
	AvatarBox->SetContent(AvatarImage);
	AvatarBox->SetVisibility(ESlateVisibility::Collapsed);
	Row->AddChildToHorizontalBox(AvatarBox)->SetPadding(FMargin(0, 0, 8, 0));
	USizeBox* DotSize = WidgetTree->ConstructWidget<USizeBox>();
	DotSize->SetWidthOverride(10); DotSize->SetHeightOverride(10);
	StateDot = WidgetTree->ConstructWidget<UBorder>();
	DotSize->SetContent(StateDot);
	Row->AddChildToHorizontalBox(DotSize)->SetPadding(FMargin(0, 5, 8, 0));
	NameText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Body);
	NameText->SetAutoWrapText(false);
	Row->AddChildToHorizontalBox(NameText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RankText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption);
	RankText->SetAutoWrapText(false);
	USizeBox* RankWidth = WidgetTree->ConstructWidget<USizeBox>(); RankWidth->SetWidthOverride(82); RankWidth->SetContent(RankText);
	Row->AddChildToHorizontalBox(RankWidth)->SetPadding(FMargin(8, 2));
	StateText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption, true);
	StateText->SetAutoWrapText(false);
	USizeBox* StateWidth = WidgetTree->ConstructWidget<USizeBox>(); StateWidth->SetWidthOverride(76); StateWidth->SetContent(StateText);
	Row->AddChildToHorizontalBox(StateWidth)->SetPadding(FMargin(10, 2));
	RateText = MSUIStyle::MakeText(WidgetTree, FText::GetEmpty(), MSUIStyle::ETextRole::Caption);
	RateText->SetAutoWrapText(false);
	USizeBox* RateWidth = WidgetTree->ConstructWidget<USizeBox>(); RateWidth->SetWidthOverride(92); RateWidth->SetContent(RateText);
	Row->AddChildToHorizontalBox(RateWidth)->SetPadding(FMargin(10, 2, 12, 2));
	UTextBlock* InviteLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("小隊に招待")),
		MSUIStyle::ETextRole::Caption);
	InviteLabel->SetAutoWrapText(false);
	InviteLabel->SetJustification(ETextJustify::Center);
	InviteButton = MSUIStyle::MakeCapsuleButton(WidgetTree, InviteLabel, FLinearColor(0.04f, 0.22f, 0.32f, 1.0f));
	InviteButton->OnClicked.AddDynamic(this, &UMSCommunityFriendRowWidget::HandleInviteClicked);
	USizeBox* InviteWidth = WidgetTree->ConstructWidget<USizeBox>(); InviteWidth->SetWidthOverride(170); InviteWidth->SetContent(InviteButton);
	Row->AddChildToHorizontalBox(InviteWidth);
}

void UMSCommunityFriendRowWidget::Configure(const FMSFriendEntry& Entry, UMSCommunityWidget* InOwner,
	const bool bInviteVisible, const bool bInSessionMembershipConfirmed)
{
	CachedEntry = Entry;
	FriendName = Entry.Name;
	OwnerWidget = InOwner;
	bShowInvite = bInviteVisible;
	bSessionMembershipConfirmed = bInSessionMembershipConfirmed;
	RefreshPresentation();
}

void UMSCommunityFriendRowWidget::RefreshPresentation()
{
	if (!NameText) return;
	// FriendEntry is a local save model.  Its state and RATE are not a live
	// presence query, so do not present either value as a current connection.
	const FLinearColor TextColor = MSCommunity::OfflineText;
	if (AvatarBox && AvatarImage)
	{
		UTexture2D* AvatarTexture = MSAvatarLibrary::ResolveAvatarTexture(CachedEntry.Name);
		if (AvatarTexture) AvatarImage->SetBrushFromTexture(AvatarTexture, false);
		AvatarBox->SetVisibility(AvatarTexture ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	StateDot->SetBrushColor(UMSCommunityWidget::GetStateColor(EMSFriendState::Offline));
	NameText->SetText(FText::FromString(CachedEntry.Name)); NameText->SetColorAndOpacity(TextColor);
	RankText->SetText(FText::FromString(TEXT("RANK ---")));
	RankText->SetColorAndOpacity(TextColor);
	StateText->SetText(FText::FromString(TEXT("接続未確認")));
	StateText->SetColorAndOpacity(TextColor);
	RateText->SetText(FText::FromString(TEXT("RATE ---")));
	RateText->SetColorAndOpacity(TextColor);
	InviteButton->SetVisibility(bShowInvite ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// Saved membership is historical.  Only an explicit session confirmation
	// disables a new invitation; pending invitations remain local records too.
	const bool bCanInvite = bShowInvite && !bSessionMembershipConfirmed && !CachedEntry.bIsSquadMember &&
		!CachedEntry.bHasPendingSquadInvite;
	InviteButton->SetIsEnabled(bCanInvite);
	InviteButton->SetToolTipText(bCanInvite
		? FText::FromString(TEXT("現在の接続は未確認です。招待はローカル記録として扱われます"))
		: FText::FromString(CachedEntry.bHasPendingSquadInvite
			? TEXT("招待のローカル記録があります（接続未確認）")
			: (CachedEntry.bIsSquadMember && !bSessionMembershipConfirmed
				? TEXT("保存済みの小隊所属です（現在の参加は未確認）")
				: TEXT("この小隊参加は現在のセッションで確認済みです"))));
}

void UMSCommunityFriendRowWidget::HandleInviteClicked()
{
	if (OwnerWidget) OwnerWidget->InviteFriend(FriendName);
}

void UMSCommunityWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSCommunityWidget::BuildLayout()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	MSUIStyle::ApplyBrush(Background, MSUIStyle::EBrush::PanelSolid, MSCommunity::Backdrop);
	Background->SetPadding(FMargin(72, 42));
	WidgetTree->RootWidget = Background;
	UVerticalBox* Screen = WidgetTree->ConstructWidget<UVerticalBox>();
	Background->SetContent(Screen);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Screen->AddChildToVerticalBox(Header);
	Header->AddChildToHorizontalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("COMMUNITY  /  PILOT NETWORK")),
		MSUIStyle::ETextRole::H1, true, MSCommunity::Cyan))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* CloseLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("閉じる")), MSUIStyle::ETextRole::Body);
	CloseLabel->SetAutoWrapText(false);
	CloseLabel->SetJustification(ETextJustify::Center);
	UButton* CloseButton = MSUIStyle::MakeCapsuleButton(WidgetTree, CloseLabel,
		FLinearColor(0.24f, 0.08f, 0.09f, 1), TEXT("CommunityCloseButton"), MSUIStyle::EButtonSound::Back);
	CloseButton->OnClicked.AddDynamic(this, &UMSCommunityWidget::HandleCloseClicked);
	USizeBox* CloseWidth = WidgetTree->ConstructWidget<USizeBox>(); CloseWidth->SetWidthOverride(130); CloseWidth->SetContent(CloseButton);
	Header->AddChildToHorizontalBox(CloseWidth);

	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	Screen->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 14, 0, 8));
	auto MakeTab = [this, Tabs](const TCHAR* Label, const FName Name)
	{
		UTextBlock* Text = MSUIStyle::MakeText(WidgetTree, FText::FromString(Label), MSUIStyle::ETextRole::H2);
		Text->SetAutoWrapText(false);
		Text->SetJustification(ETextJustify::Center);
		UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, Text, MSCommunity::Panel, Name);
		Tabs->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return Button;
	};
	FriendsTabButton = MakeTab(TEXT("フレンド"), TEXT("FriendsTabButton"));
	SquadTabButton = MakeTab(TEXT("小隊"), TEXT("SquadTabButton"));
	FriendsTabButton->OnClicked.AddDynamic(this, &UMSCommunityWidget::HandleFriendsTabClicked);
	SquadTabButton->OnClicked.AddDynamic(this, &UMSCommunityWidget::HandleSquadTabClicked);

	UHorizontalBox* AddBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	Screen->AddChildToVerticalBox(AddBar)->SetPadding(FMargin(0, 0, 0, 8));
	FriendNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("FriendNameInput"));
	FriendNameInput->SetHintText(FText::FromString(TEXT("パイロット名を入力")));
	// UEditableTextBox defaults to the engine's white field, which hides this
	// hint against the community panel. Keep the input in the shared pixel UI
	// palette and give its focused state a real, visible frame.
	FEditableTextBoxStyle FriendNameInputStyle = FriendNameInput->GetWidgetStyle();
	FriendNameInputStyle.TextStyle.Font.Size = FMath::RoundToInt(MSUIStyle::GetFontSize(MSUIStyle::ETextRole::Body));
	FriendNameInputStyle.TextStyle.Font.TypefaceFontName = FName(TEXT("Regular"));
	FriendNameInputStyle.TextStyle.ColorAndOpacity = FSlateColor(FLinearColor::White);
	FriendNameInputStyle.ForegroundColor = FSlateColor(FLinearColor::White);
	FriendNameInputStyle.FocusedForegroundColor = FSlateColor(FLinearColor::White);
	FriendNameInputStyle.ReadOnlyForegroundColor = FSlateColor(MSCommunity::OfflineText);
	FriendNameInputStyle.BackgroundColor = FSlateColor(FLinearColor::White);
	FriendNameInputStyle.Padding = FMargin(12.0f, 6.0f);
	auto MakeInputBrush = [](const FLinearColor& Fill, const FLinearColor& Outline, const float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(4.0f, FSlateColor(Outline), OutlineWidth);
		return Brush;
	};
	FriendNameInputStyle.BackgroundImageNormal = MakeInputBrush(MSCommunity::Row,
		FLinearColor(0.08f, 0.25f, 0.30f, 1.0f), 1.0f);
	FriendNameInputStyle.BackgroundImageHovered = MakeInputBrush(
		FLinearColor(0.035f, 0.11f, 0.15f, 1.0f), MSCommunity::Cyan, 1.0f);
	FriendNameInputStyle.BackgroundImageFocused = MakeInputBrush(MSCommunity::Panel, MSCommunity::Cyan, 2.0f);
	FriendNameInputStyle.BackgroundImageReadOnly = MakeInputBrush(MSCommunity::Panel, MSCommunity::OfflineText, 1.0f);
	FriendNameInput->SetWidgetStyle(FriendNameInputStyle);
	AddBar->AddChildToHorizontalBox(FriendNameInput)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* AddLabel = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("フレンド追加")), MSUIStyle::ETextRole::Body);
	AddLabel->SetAutoWrapText(false);
	UButton* AddButton = MSUIStyle::MakeCapsuleButton(WidgetTree, AddLabel, FLinearColor(0.03f, 0.28f, 0.38f, 1));
	AddButton->OnClicked.AddDynamic(this, &UMSCommunityWidget::HandleAddFriendClicked);
	USizeBox* AddWidth = WidgetTree->ConstructWidget<USizeBox>(); AddWidth->SetWidthOverride(180); AddWidth->SetContent(AddButton);
	AddBar->AddChildToHorizontalBox(AddWidth)->SetPadding(FMargin(8, 0, 0, 0));

	UBorder* ListInner = nullptr;
	UBorder* ListFrame = MSUIStyle::MakeFramedPanel(WidgetTree, ListInner, nullptr, false,
		MSCommunity::Cyan, MSCommunity::Panel, FMargin(7));
	Screen->AddChildToVerticalBox(ListFrame)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* ListStack = WidgetTree->ConstructWidget<UVerticalBox>();
	ListInner->SetContent(ListStack);
	FriendsScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FriendsScroll"));
	SquadScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SquadScroll"));
	ListStack->AddChildToVerticalBox(FriendsScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ListStack->AddChildToVerticalBox(SquadScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Screen->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("CHAT / SYSTEM LOG")),
		MSUIStyle::ETextRole::Caption, false, MSCommunity::Cyan))->SetPadding(FMargin(0, 10, 0, 4));
	ChatScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CommunityChatLog"));
	USizeBox* ChatSize = WidgetTree->ConstructWidget<USizeBox>(); ChatSize->SetHeightOverride(82);
	ChatSize->SetContent(ChatScroll); Screen->AddChildToVerticalBox(ChatSize);
	ShowTab(EMSCommunityTab::Friends);
}

void UMSCommunityWidget::Open()
{
	EnsureSubsystemBinding();
	if (GetWorld() && !IsInViewport()) AddToViewport(60);
	if (LobbyWidget) LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
	RefreshLists();
	ShowTab(CurrentTab);
	SetVisibility(ESlateVisibility::Visible);
	SetKeyboardFocus();
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (!bScreenRegistered)
	{
		if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem())
		{
			Lock->PushScreen(MSCommunity::ScreenId, FMSUICloseRequestDelegate::CreateWeakLambda(this,
				[this]() { Close(); return true; }));
			RegisteredUILock = Lock;
			bScreenRegistered = true;
		}
	}
}

UMSCommunityWidget* UMSCommunityWidget::CreateForPlayer(APlayerController* OwningPlayer)
{
	return OwningPlayer ? CreateWidget<UMSCommunityWidget>(OwningPlayer, StaticClass()) : nullptr;
}

void UMSCommunityWidget::Close()
{
	const bool bWasOpen = IsOpen();
	SetVisibility(ESlateVisibility::Collapsed);
	if (bScreenRegistered)
	{
		UMSUILockSubsystem* Lock = RegisteredUILock.Get();
		if (!Lock) Lock = ResolveUILockSubsystem();
		if (Lock) Lock->PopScreen(MSCommunity::ScreenId);
		RegisteredUILock.Reset();
		bScreenRegistered = false;
	}
	if (LobbyWidget)
	{
		if (bWasOpen)
		{
			LobbyWidget->RefreshPilotList();
			LobbyWidget->SetVisibility(ESlateVisibility::Visible);
			LobbyWidget->SetKeyboardFocus();
		}
	}
}

FReply UMSCommunityWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSCommunityWidget::ShowTab(const EMSCommunityTab Tab)
{
	CurrentTab = Tab;
	if (FriendsScroll) FriendsScroll->SetVisibility(Tab == EMSCommunityTab::Friends ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (SquadScroll) SquadScroll->SetVisibility(Tab == EMSCommunityTab::Squad ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (FriendsTabButton) FriendsTabButton->SetBackgroundColor(Tab == EMSCommunityTab::Friends ? MSCommunity::Cyan : FLinearColor::White);
	if (SquadTabButton) SquadTabButton->SetBackgroundColor(Tab == EMSCommunityTab::Squad ? MSCommunity::Cyan : FLinearColor::White);
}

UMSFriendSubsystem* UMSCommunityWidget::ResolveFriendSubsystem() const
{
	if (FriendSubsystemOverride) return FriendSubsystemOverride;
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSFriendSubsystem>() : nullptr;
}

UMSUILockSubsystem* UMSCommunityWidget::ResolveUILockSubsystem() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void UMSCommunityWidget::RefreshLists()
{
	if (!FriendsScroll || !SquadScroll) return;
	FriendsScroll->ClearChildren(); SquadScroll->ClearChildren();
	UMSFriendSubsystem* Friends = ResolveFriendSubsystem();
	if (!Friends) return;
	TArray<FMSFriendEntry> Entries = Friends->GetFriends();
	Entries.StableSort([Friends](const FMSFriendEntry& A, const FMSFriendEntry& B)
	{
		const bool bAConfirmed = Friends->IsSquadMembershipConfirmedThisSession(A.Name);
		const bool bBConfirmed = Friends->IsSquadMembershipConfirmedThisSession(B.Name);
		if (bAConfirmed != bBConfirmed) return bAConfirmed;
		return A.Name < B.Name;
	});
	for (const FMSFriendEntry& Entry : Entries)
	{
		const bool bSessionMembershipConfirmed = Friends->IsSquadMembershipConfirmedThisSession(Entry.Name);
		UMSCommunityFriendRowWidget* Row = WidgetTree->ConstructWidget<UMSCommunityFriendRowWidget>();
		Row->Initialize(); Row->Configure(Entry, this, true, bSessionMembershipConfirmed);
		FriendsScroll->AddChild(Row);
		if (bSessionMembershipConfirmed)
		{
			UMSCommunityFriendRowWidget* SquadRow = WidgetTree->ConstructWidget<UMSCommunityFriendRowWidget>();
			SquadRow->Initialize(); SquadRow->Configure(Entry, this, false, true);
			SquadScroll->AddChild(SquadRow);
		}
	}
}

bool UMSCommunityWidget::InviteFriend(const FString& FriendName)
{
	UMSFriendSubsystem* Friends = ResolveFriendSubsystem();
	const FMSFriendEntry* Entry = Friends ? Friends->FindFriend(FriendName) : nullptr;
	if (!Entry) return false;
	if (Friends->IsSquadMembershipConfirmedThisSession(Entry->Name))
	{
		AddNotification(FString::Printf(TEXT("%sさんはすでに小隊に参加しています"), *Entry->Name));
		return false;
	}
	if (Entry->bIsSquadMember)
	{
		AddNotification(FString::Printf(TEXT("%sさんの保存済み所属は現在の参加として確認できないため招待できません"), *Entry->Name));
		return false;
	}
	if (Entry->bHasPendingSquadInvite)
	{
		AddNotification(FString::Printf(TEXT("%sさんには招待のローカル記録があります（接続未確認）"), *Entry->Name));
		return false;
	}
	int32 ConfirmedSquadMemberCount = 0;
	for (const FMSFriendEntry& Friend : Friends->GetFriends())
	{
		if (Friends->IsSquadMembershipConfirmedThisSession(Friend.Name)) ++ConfirmedSquadMemberCount;
	}
	if (ConfirmedSquadMemberCount >= UMSFriendSubsystem::MaxInvitedSquadMembers)
	{
		AddNotification(FString::Printf(TEXT("小隊が満員のため%sさんを招待できません"), *Entry->Name));
		return false;
	}
	// The current friend subsystem still protects its saved capacity counter.
	// Surface that limitation explicitly instead of letting an invite appear to
	// succeed while an old saved membership silently rejects it.
	if (Friends->GetSquadMemberCount() >= UMSFriendSubsystem::MaxInvitedSquadMembers)
	{
		AddNotification(TEXT("保存済みの小隊所属が上限に達しているため招待できません（現在の参加は未確認です）"));
		return false;
	}
	const FString DisplayName = Entry->Name;
	EnsureSubsystemBinding();
	if (!Friends->SendSquadInvite(DisplayName)) return false;
	// 相手がAcceptすると OnSquadInviteAccepted 経由で自動入室がUIに反映される(タスク#144)
	AddNotification(FString::Printf(TEXT("ローカル記録: %sさんへの小隊招待を保存しました（接続未確認）"), *DisplayName));
	RefreshLists();
	return true;
}

void UMSCommunityWidget::EnsureSubsystemBinding()
{
	UMSFriendSubsystem* Friends = ResolveFriendSubsystem();
	if (!Friends || BoundFriendSubsystem.Get() == Friends) return;
	UnbindSubsystemDelegates();
	BoundFriendSubsystem = Friends;
	FriendsChangedHandle = Friends->OnFriendsChanged.AddUObject(this, &UMSCommunityWidget::HandleFriendsChanged);
	SquadInviteAcceptedHandle = Friends->OnSquadInviteAccepted.AddUObject(this, &UMSCommunityWidget::HandleSquadInviteAccepted);
}

void UMSCommunityWidget::UnbindSubsystemDelegates()
{
	if (UMSFriendSubsystem* Friends = BoundFriendSubsystem.Get())
	{
		Friends->OnFriendsChanged.Remove(FriendsChangedHandle);
		Friends->OnSquadInviteAccepted.Remove(SquadInviteAcceptedHandle);
	}
	BoundFriendSubsystem.Reset();
	FriendsChangedHandle.Reset();
	SquadInviteAcceptedHandle.Reset();
}

void UMSCommunityWidget::HandleFriendsChanged()
{
	RefreshLists();
}

void UMSCommunityWidget::HandleSquadInviteAccepted(const FString& FriendName)
{
	// The current subsystem only simulates the remote response locally.  Keep
	// that boundary visible instead of presenting it as network confirmation.
	AddNotification(FString::Printf(TEXT("ローカル確認: %sさんの招待承認をシミュレートし、小隊参加表示を更新しました"), *FriendName));
}

bool UMSCommunityWidget::AddFriendByName(const FString& FriendName)
{
	UMSFriendSubsystem* Friends = ResolveFriendSubsystem();
	if (!Friends || !Friends->AddFriend(FriendName))
	{
		AddNotification(TEXT("フレンドを追加できません"));
		return false;
	}
	AddNotification(FString::Printf(TEXT("%sさんをフレンドに追加しました"), *FriendName.TrimStartAndEnd()));
	RefreshLists();
	return true;
}

void UMSCommunityWidget::AddNotification(const FString& Message)
{
	if (!ChatScroll) return;
	ChatScroll->AddChild(MSUIStyle::MakeText(WidgetTree, FText::FromString(Message),
		MSUIStyle::ETextRole::Caption, false, FLinearColor(0.78f, 0.92f, 0.96f)));
	ChatScroll->ScrollToEnd();
}

int32 UMSCommunityWidget::GetFriendRowCount() const { return FriendsScroll ? FriendsScroll->GetChildrenCount() : 0; }
int32 UMSCommunityWidget::GetSquadRowCount() const { return SquadScroll ? SquadScroll->GetChildrenCount() : 0; }
FString UMSCommunityWidget::GetLastNotification() const
{
	if (!ChatScroll || ChatScroll->GetChildrenCount() == 0) return FString();
	if (const UTextBlock* Text = Cast<UTextBlock>(ChatScroll->GetChildAt(ChatScroll->GetChildrenCount() - 1))) return Text->GetText().ToString();
	return FString();
}

FText UMSCommunityWidget::GetStateText(const EMSFriendState State)
{
	switch (State)
	{
	case EMSFriendState::Lobby: return FText::FromString(TEXT("ロビー"));
	case EMSFriendState::Reserved: return FText::FromString(TEXT("予約中"));
	case EMSFriendState::InBattle: return FText::FromString(TEXT("戦闘中"));
	case EMSFriendState::Ready: return FText::FromString(TEXT("準備完了"));
	default: return FText::FromString(TEXT("オフライン"));
	}
}

FLinearColor UMSCommunityWidget::GetStateColor(const EMSFriendState State)
{
	switch (State)
	{
	case EMSFriendState::Lobby: return FLinearColor(0.16f, 0.82f, 0.72f, 1);
	case EMSFriendState::Reserved: return FLinearColor(0.95f, 0.57f, 0.08f, 1);
	case EMSFriendState::InBattle: return FLinearColor(0.94f, 0.22f, 0.08f, 1);
	case EMSFriendState::Ready: return FLinearColor(0.96f, 0.10f, 0.18f, 1);
	default: return FLinearColor(0.28f, 0.32f, 0.35f, 1);
	}
}

void UMSCommunityWidget::HandleCloseClicked() { Close(); }
void UMSCommunityWidget::HandleFriendsTabClicked() { ShowTab(EMSCommunityTab::Friends); }
void UMSCommunityWidget::HandleSquadTabClicked() { ShowTab(EMSCommunityTab::Squad); }
void UMSCommunityWidget::HandleAddFriendClicked()
{
	if (FriendNameInput && AddFriendByName(FriendNameInput->GetText().ToString())) FriendNameInput->SetText(FText::GetEmpty());
}

void UMSCommunityWidget::NativeDestruct()
{
	Close();
	UnbindSubsystemDelegates();
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSCommunityWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
#endif
