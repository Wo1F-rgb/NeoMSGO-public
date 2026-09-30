#include "UI/MSHangarChrome.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSAvatarLibrary.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSPixelPainter.h"
#include "UI/MSPixelUI.h"

namespace
{
	struct FDesignRect
	{
		float X;
		float Y;
		float Width;
		float Height;
	};

	// MSGO9 f2140 is a 1920x1080 reference.  The owning screen's ScaleBox
	// applies the same factor at 1280x720, so keep every shared chrome rect in
	// design pixels and avoid viewport-size-specific offsets here.
	constexpr FDesignRect IdentityRect{397.0f, 28.0f, 154.0f, 59.0f};
	constexpr FDesignRect BreadcrumbRect{555.0f, 40.0f, 151.0f, 33.0f};
	constexpr FDesignRect NavIconRect{904.0f, 12.0f, 64.0f, 52.0f};
	constexpr FDesignRect NavLabelRect{900.0f, 66.0f, 72.0f, 18.0f};
	constexpr FDesignRect FooterNoticeRect{452.0f, 864.0f, 1016.0f, 21.0f};
	constexpr FDesignRect ChatRect{452.0f, 890.0f, 438.0f, 150.0f};
	constexpr FDesignRect PilotRect{891.0f, 890.0f, 368.0f, 150.0f};
	constexpr FDesignRect RosterRect{1260.0f, 890.0f, 208.0f, 150.0f};

	void Place(UCanvasPanel* C, UWidget* W, const FDesignRect& Rect)
	{
		if (!C || !W) return;
		UCanvasPanelSlot* S = C->AddChildToCanvas(W);
		S->SetAnchors(FAnchors(0.0f, 0.0f));
		S->SetAlignment(FVector2D::ZeroVector);
		S->SetPosition({Rect.X, Rect.Y});
		S->SetSize({Rect.Width, Rect.Height});
	}

	FDesignRect Offset(const FDesignRect& Rect, const float XOffset)
	{
		return {Rect.X + XOffset, Rect.Y, Rect.Width, Rect.Height};
	}

	FText CurrentBreadcrumbLabel(const FText& FullBreadcrumb)
	{
		FString Visible = FullBreadcrumb.ToString();
		int32 Separator = INDEX_NONE;
		if (Visible.FindLastChar(TEXT('>'), Separator))
		{
			Visible = Visible.Mid(Separator + 1);
			Visible.TrimStartAndEndInline();
		}
		return FText::FromString(Visible);
	}
}

UTextBlock* MSHangarChrome::Build(UUserWidget* Owner, UCanvasPanel* Canvas, const FText& Breadcrumb)
{
	if (!Owner || !Canvas) return nullptr;
	UWidgetTree* T = Owner->WidgetTree;
	UTextBlock* Identity = MSPixelUI::Text(T,FText::GetEmpty(),14,MSPixel::Hex(0x8CF7DD),TEXT("HangarIdentityText"));
	Identity->SetJustification(ETextJustify::Center);
	Identity->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Identity->SetAutoWrapText(false);
	Identity->SetToolTipText(FText::FromString(TEXT("パイロット名 / 接続状態は未確認")));
	Place(Canvas,MSPixelUI::Frame(T,Identity,EMSVectorSurface::GoldPanel,FMargin(7,6)),IdentityRect);
	UTextBlock* Crumb = MSPixelUI::Text(T,CurrentBreadcrumbLabel(Breadcrumb),16,MSPixel::Hex(0xF7DE75),TEXT("HangarBreadcrumbText"));
	Crumb->SetJustification(ETextJustify::Center);
	Crumb->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Crumb->SetAutoWrapText(false);
	Crumb->SetToolTipText(Breadcrumb);
	Place(Canvas,MSPixelUI::Frame(T,Crumb,EMSVectorSurface::GoldPanel,FMargin(6,5)),BreadcrumbRect);
	const TCHAR* Nav[] = { TEXT("ショップ"),TEXT("チャレンジ"),TEXT("階級"),TEXT("ランキング"),TEXT("部隊"),TEXT("コミュニティ"),TEXT("プレゼント"),TEXT("ブリッジ") };
	for (int32 I=0; I<UE_ARRAY_COUNT(Nav); ++I)
	{
		const float XOffset = 72.0f * I;
		const FName IconName(*FString::Printf(TEXT("HangarNavIcon%d"), I));
		Place(Canvas,MSPixelUI::Skin(T,EMSVectorSurface::NavigationIcon,IconName,I),Offset(NavIconRect,XOffset));
		UTextBlock* Label=MSPixelUI::Text(T,FText::FromString(Nav[I]),10,MSPixel::Hex(0xF3F6E8),
			*FString::Printf(TEXT("HangarNavLabel%d"), I));
		Label->SetJustification(ETextJustify::Center);
		Label->SetAutoWrapText(false);
		Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Label->SetToolTipText(FText::FromString(TEXT("未接続 / 表示のみ")));
		Place(Canvas,Label,Offset(NavLabelRect,XOffset));
	}
	UTextBlock* Hint=MSPixelUI::Text(T,FText::FromString(TEXT("詳細表示：未接続")),13,MSPixel::Hex(0x71EAE0),TEXT("HangarFooterHint"));
	Hint->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Hint->SetToolTipText(FText::FromString(TEXT("詳細表示は未接続です。")));
	const FDesignRect NoticeHintRect{FooterNoticeRect.X, FooterNoticeRect.Y, 188.0f, FooterNoticeRect.Height};
	Place(Canvas,MSPixelUI::Frame(T,Hint,EMSVectorSurface::Panel,FMargin(5,2)),NoticeHintRect);
	// Original broadcast messages and other players' chat are not local game state.
	const FDesignRect NoticeStatusRect{FooterNoticeRect.X + 191.0f, FooterNoticeRect.Y, FooterNoticeRect.Width - 191.0f, FooterNoticeRect.Height};
	Place(Canvas,MSPixelUI::Skin(T,EMSVectorSurface::Panel,TEXT("HangarFooterNoticePanel")),NoticeStatusRect);
	UCanvasPanel* Chat=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HangarChatPanel"));
	Chat->SetToolTipText(FText::FromString(TEXT("チャット履歴 / 接続状態は未確認")));
	Place(Canvas,MSPixelUI::Frame(T,Chat,EMSVectorSurface::Panel,FMargin(0)),ChatRect);
	Place(Chat,MSPixelUI::Text(T,FText::FromString(TEXT("FILTER :   ロビー    部隊    小隊    個人")),12,MSPixel::Hex(0x8FE4DD),TEXT("HangarChatFilter")),{6.0f,4.0f,422.0f,18.0f});
	Place(Chat,MSPixelUI::Text(T,FText::FromString(TEXT("チャット履歴：接続未確認")),12,MSPixel::Hex(0x91B8B7),TEXT("HangarChatStatus")),{6.0f,27.0f,422.0f,18.0f});
	Place(Chat,MSPixelUI::Text(T,FText::FromString(TEXT("ロビー")),12,MSPixel::Hex(0xDAE5F8)),{7.0f,129.0f,54.0f,17.0f});
	Place(Chat,MSPixelUI::Skin(T,EMSVectorSurface::Panel),{63.0f,126.0f,369.0f,22.0f});
	UCanvasPanel* Pilot=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HangarPilotPanel"));
	Pilot->SetToolTipText(FText::FromString(TEXT("パイロット情報 / GP・BC・接続状態は未確認")));
	Place(Canvas,MSPixelUI::Frame(T,Pilot,EMSVectorSurface::Panel,FMargin(0)),PilotRect);
	Place(Pilot,MSPixelUI::Text(T,FText::FromString(TEXT("P I L O T   I N F O R M A T I O N")),10,MSPixel::Hex(0x8FE4DD)),{30.0f,1.0f,321.0f,16.0f});
	UImage* Avatar=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("HangarPilotAvatar"));
	Place(Pilot,Avatar,{10.0f,34.0f,110.0f,110.0f});
	UTextBlock* Stats=MSPixelUI::Text(T,FText::GetEmpty(),16,MSPixel::Hex(0xF2F3E4),TEXT("HangarPilotStats"));
	Stats->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Stats->SetAutoWrapText(false);
	Stats->SetToolTipText(FText::FromString(TEXT("GP / BC は未取得、接続状態は未確認")));
	Place(Pilot,Stats,{134.0f,33.0f,222.0f,106.0f});
	UCanvasPanel* Roster=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HangarRosterPanel"));
	Roster->SetToolTipText(FText::FromString(TEXT("保存済みパイロット / 接続状態は未確認")));
	Place(Canvas,MSPixelUI::Frame(T,Roster,EMSVectorSurface::Panel,FMargin(0)),RosterRect);
	Place(Roster,MSPixelUI::Text(T,FText::FromString(TEXT("P I L O T   L I S T")),10,MSPixel::Hex(0x8FE4DD)),{37.0f,1.0f,159.0f,16.0f});
	Place(Roster,MSPixelUI::Text(T,FText::FromString(TEXT("保存済み / 接続未確認")),10,MSPixel::Hex(0x91B8B7),TEXT("HangarPilotRosterStatus")),{14.0f,18.0f,180.0f,16.0f});
	UTextBlock* Names=MSPixelUI::Text(T,FText::GetEmpty(),14,MSPixel::Hex(0xEAF3E7),TEXT("HangarPilotRoster"));
	Names->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); Names->SetAutoWrapText(false); Place(Roster,Names,{14.0f,37.0f,180.0f,99.0f});
	Refresh(Owner); return Identity;
}

void MSHangarChrome::Refresh(UUserWidget* Owner)
{
	if (!Owner) return;
	UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
	const UMSFriendSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UMSFriendSubsystem>() : nullptr;
	const UMSOnlineAccountSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	FString Name = Friends && !Friends->GetSelfDisplayName().IsEmpty()
		? Friends->GetSelfDisplayName() : TEXT("未ログイン");
	// Online guest identities use a stable account id internally, while the
	// Hangar chrome must show the server-provided display name.  Keep the
	// existing Friend/GameInstance fallback for offline and incomplete identities.
	if (Online && Online->IsOnlineMode() && !Online->GetIdentity().DisplayName.IsEmpty())
	{
		Name = Online->GetIdentity().DisplayName;
	}
	if(UTextBlock* T=Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarIdentityText")))) T->SetText(FText::FromString(Name+TEXT("\n接続未確認")));
	if(UTextBlock* T=Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarPilotStats"))))
		T->SetText(FText::FromString(Name+TEXT("\nGP              ---\nBC              ---\n接続            未確認")));
	if(UImage* I=Cast<UImage>(Owner->GetWidgetFromName(TEXT("HangarPilotAvatar"))))
	{
		UTexture2D* A=MSAvatarLibrary::ResolveAvatarTexture(Name); if(A) I->SetBrushFromTexture(A,false); I->SetVisibility(A?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
	}
	if(UTextBlock* T=Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarPilotRoster"))))
	{
		FString Names; int32 Count=0;
		for(const FMSFriendEntry& Entry:UMSLobbyWidget::BuildPilotListEntries(Friends))
		{
			if(Count++==5) break; if(!Names.IsEmpty()) Names+=TEXT("\n"); Names+=Entry.Name;
		}
		if (Names.IsEmpty()) Names = TEXT("保存済みパイロットなし");
		T->SetText(FText::FromString(Names)); T->SetToolTipText(FText::FromString(TEXT("保存済みパイロット / 接続状態は未確認")));
	}
}
