#include "UI/MSCustomizeWidget.h"

#include "Audio/MSBgmSubsystem.h"
#include "InputCoreTypes.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/MSModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSBattleAssistCustomizeWidget.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSSuitSwapWidget.h"
#include "UI/MSUITransitionSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"
#include "UI/MSPixelUI.h"
#include "UI/MSWeaponIconWidget.h"
#include "UI/MSHangarChrome.h"
#include "UI/MSPixelPainter.h"

namespace MSCustomize
{
	const FLinearColor DimColor(0.004f, 0.012f, 0.02f, 0.91f);
	const FLinearColor PanelColor(0.025f, 0.075f, 0.095f, 0.98f);
	const FLinearColor CardColor(0.035f, 0.11f, 0.145f, 1.0f);
	const FLinearColor AccentColor(0.24f, 0.92f, 0.86f, 1.0f);
	const FLinearColor CardFrameColor(0.95f, 0.53f, 0.28f, 1.0f);
	const FLinearColor WarningColor(1.0f, 0.12f, 0.08f, 1.0f);

	FString ResolveWeaponName(const TSoftObjectPtr<UMSWeaponDataBase>& WeaponRef)
	{
		const UMSWeaponDataBase* Weapon = WeaponRef.Get();
		if (!Weapon && !WeaponRef.IsNull()) Weapon = WeaponRef.LoadSynchronous();
		return Weapon && !Weapon->WeaponName.IsEmpty() ? Weapon->WeaponName.ToString() : TEXT("---");
	}

	FString TypeLabel(const EMSMobileSuitUnitType Type)
	{
		switch (Type)
		{
		case EMSMobileSuitUnitType::Assault: return TEXT("強襲");
		case EMSMobileSuitUnitType::Heavy: return TEXT("重撃");
		case EMSMobileSuitUnitType::Support: return TEXT("支援");
		case EMSMobileSuitUnitType::Bombardment: return TEXT("砲撃");
		default: return TEXT("狙撃");
		}
	}

	FString DenseStats(const UMSMobileSuitData* Preset)
	{
		if (!Preset) return FString();
		return FString::Printf(
			TEXT("%s  COST %d\nARMOR %d  DP %d\nSPEED %.1f  BOOST %.1f\nCAP %.0f  LOAD %d\nRETURN %d"),
			*TypeLabel(Preset->UnitType), Preset->Cost, Preset->MaxArmor, Preset->MaxDownPoint,
			Preset->WalkSpeed, Preset->BoostSpeed, Preset->BoostCapacity, Preset->LoadCapacity,
			Preset->EmergencyRecallPerformance);
	}
}

namespace
{
	int64 OnlineAuthGeneration(const UUserWidget* Widget)
	{
		const UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		const UMSOnlineAccountSubsystem* Account = GameInstance ? GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
		return Account && Account->IsOnlineMode() ? Account->GetAuthGeneration() : INDEX_NONE;
	}

	bool IsOnlineAuthGenerationCurrent(const UUserWidget* Widget, const int64 ExpectedGeneration)
	{
		return ExpectedGeneration == INDEX_NONE || OnlineAuthGeneration(Widget) == ExpectedGeneration;
	}

	const FVector2D CustomizeCanvasSize(1920.0f, 1080.0f);

	void PlaceOnCustomizeCanvas(UCanvasPanel* Canvas, UWidget* Child, const float X, const float Y,
		const float Width, const float Height)
	{
		if (!Canvas || !Child) return;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(Width, Height));
	}

	UTextBlock* CustomizeText(UWidgetTree* Tree, const FText& Value, const float PixelHeight,
		const FLinearColor& Color = FLinearColor::White, const FName Name = NAME_None)
	{
		return MSPixelUI::Text(Tree, Value, PixelHeight, Color, Name);
	}

	UButton* CustomizeButton(UWidgetTree* Tree, const FText& Label, const EMSVectorSurface Surface,
		const FName Name, const float PixelHeight = 18.0f, const FLinearColor& Color = FLinearColor::White,
		const bool bEnabled = true, const int32 Variant = 0)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		MSPixelUI::ClearButtonBrush(Button);
		UTextBlock* Text = CustomizeText(Tree, Label, PixelHeight, Color);
		Text->SetJustification(ETextJustify::Center);
		if (Variant == 1) Text->SetShadowOffset(FVector2D::ZeroVector);
		UScaleBox* LabelFit = Tree->ConstructWidget<UScaleBox>(); LabelFit->SetStretch(EStretch::ScaleToFit); LabelFit->SetStretchDirection(EStretchDirection::DownOnly); LabelFit->SetContent(Text);
		Button->SetContent(MSPixelUI::Frame(Tree, LabelFit, Surface, FMargin(7.0f, 3.0f), Variant));
		MSUIStyle::StretchButtonContent(Button);
		Button->SetIsEnabled(bEnabled);
		return Button;
	}

	UOverlay* CustomizePanel(UWidgetTree* Tree, UWidget* Content, const EMSVectorSurface Surface = EMSVectorSurface::Panel,
		const FMargin Padding = FMargin(10.0f))
	{
		return MSPixelUI::Frame(Tree, Content, Surface, Padding);
	}
}

void UMSCustomizeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); BindDeckSubsystem();
	RefreshAll(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSCustomizeWidget::BuildReferenceLayout()
{
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("CustomizeScaleToFit"));
	Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::Both);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(1920); Design->SetHeightOverride(1080);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CustomizeDesignCanvas"));
	Design->SetContent(Canvas); Fit->SetContent(Design); WidgetTree->RootWidget = Fit;
	PlaceOnCustomizeCanvas(Canvas, MSPixelUI::Skin(WidgetTree, EMSVectorSurface::HangarBackdrop, TEXT("CustomizeHangarBackdrop")), 0,0,1920,1080);
	HeaderIdentityText = MSHangarChrome::Build(this,Canvas,FText::FromString(TEXT("格納庫")));
	UButton* Back = CustomizeButton(WidgetTree,FText::FromString(TEXT("戻る")),EMSVectorSurface::Button,TEXT("CustomizeBackButton"),18,MSPixel::Hex(0xA62949),true,1);
	Back->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleBackClicked); PlaceOnCustomizeCanvas(Canvas,Back,1352,225,114,30);
	// MSGO9 source frame 2140: the parent is four full cards. The tall model and
	// action menu belong to SuitDetail, entered by clicking one of these cards.
	const TCHAR* Labels[] = {TEXT("地上デッキ"),TEXT("宇宙デッキ"),TEXT("新機体開発"),TEXT("所持機体"),TEXT("機体セット"),TEXT("所持アイテム"),TEXT("エネルギー回復")};
	const FName Names[] = {TEXT("GroundDeckTab"),TEXT("SpaceDeckTab"),TEXT("DevelopTab"),TEXT("OwnedSuitTab"),TEXT("SuitSetTab"),TEXT("OwnedItemTab"),TEXT("EnergyRecoveryTab")};
	const float X[] = {452,606,768,880,994,1106,1219};
	const float W[] = {149,152,110,111,108,109,108};
	for(int32 I=0;I<UE_ARRAY_COUNT(Labels);++I)
	{
		UButton* B=CustomizeButton(WidgetTree,FText::FromString(Labels[I]),EMSVectorSurface::Button,Names[I],I<2?19:13,
			I<2?MSPixel::Hex(0x44325B):MSPixel::Hex(0x81F9E7),I==0||I==2||I==3,I<2?1:0);
		PlaceOnCustomizeCanvas(Canvas,B,X[I],I<2?239:246,W[I],I<2?30:20);
		if(I==2) B->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleDevelopClicked);
		if(I==3) B->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleOwnedClicked);
	}
	UCanvasPanel* CostRow=WidgetTree->ConstructWidget<UCanvasPanel>();
	PlaceOnCustomizeCanvas(Canvas,CustomizePanel(WidgetTree,CostRow,EMSVectorSurface::GoldPanel,FMargin(0)),452,276,1016,50);
	PlaceOnCustomizeCanvas(CostRow,CustomizeText(WidgetTree,FText::FromString(TEXT("MUコスト          ミッションユニット1          ミッションユニット2")),13,MSPixel::Hex(0xD9F3B8)),10,6,680,16);
	PlaceOnCustomizeCanvas(CostRow,CustomizeText(WidgetTree,FText::FromString(TEXT("--- / ---                  ---                            ---")),16,MSPixel::Hex(0xF8F4E6)),18,25,660,22);
	DeckCostText=CustomizeText(WidgetTree,FText::GetEmpty(),16,FLinearColor::White,TEXT("DeckCostText"));
	DeckCostText->SetJustification(ETextJustify::Center); PlaceOnCustomizeCanvas(CostRow,DeckCostText,703,3,303,44);
	for(int32 I=0;I<CardCount;++I)
	{
		UButton* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),*FString::Printf(TEXT("SuitCard%d"),I)); MSPixelUI::ClearButtonBrush(B);
		UCanvasPanel* Card=WidgetTree->ConstructWidget<UCanvasPanel>();
		UOverlay* Frame=MSPixelUI::Frame(WidgetTree,Card,EMSVectorSurface::DeckCard,FMargin(0));
		CardSkins.Add(CastChecked<UMSVectorSkinWidget>(Frame->GetChildAt(0))); B->SetContent(Frame); MSUIStyle::StretchButtonContent(B);
		PlaceOnCustomizeCanvas(Canvas,B,452+255*I,330,248,387); CardButtons.Add(B);
		UTextBlock* Number=CustomizeText(WidgetTree,FText::FromString(FString::Printf(TEXT("%02d"),I+1)),18,MSPixel::Hex(0x172127),*FString::Printf(TEXT("Slot%dNumberText"),I));
		Number->SetShadowOffset(FVector2D::ZeroVector); Number->SetJustification(ETextJustify::Center); PlaceOnCustomizeCanvas(Card,Number,20,1,208,18); SlotNumberTexts.Add(Number);
		UImage* Image=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),*FString::Printf(TEXT("Slot%dPreviewImage"),I)); Image->SetVisibility(ESlateVisibility::Hidden);
		PlaceOnCustomizeCanvas(Card,Image,14,20,220,135); PreviewImages.Add(Image);
		UMSMobileSuitPreviewRenderer* Renderer=nullptr;
		if(GetWorld()) { Renderer=NewObject<UMSMobileSuitPreviewRenderer>(this); Renderer->Initialize(GetWorld(),Image); Renderer->ConfigureDeckCardView(); }
		PreviewRenderers.Add(Renderer);
		// Empty module sockets retain their geometry; fill is driven only by actual loadout.
		for(int32 Row=0;Row<2;++Row) for(int32 Col=0;Col<10;++Col)
		{
			UBorder* Socket=WidgetTree->ConstructWidget<UBorder>(); MSPixelUI::PlainBorder(Socket,MSPixel::Hex(0x617176,.72f));
			PlaceOnCustomizeCanvas(Card,Socket,15+Col*21.5f,157+Row*22,18,18);
		}
		for(int32 J=0;J<4;++J)
		{
			const float WX=15+(J%2)*108, WY=201+(J/2)*45;
			PlaceOnCustomizeCanvas(Card,MSPixelUI::Skin(WidgetTree,EMSVectorSurface::Panel),WX,WY,105,43);
			PlaceOnCustomizeCanvas(Card,CustomizeText(WidgetTree,FText::FromString(J<3?FString::Printf(TEXT("SLOT%d"),J+1):TEXT("ABILITY")),10,MSPixel::Hex(0x99E7EA)),WX+3,WY,99,12);
			if(J<3)
			{
				UMSWeaponIconWidget* Icon=WidgetTree->ConstructWidget<UMSWeaponIconWidget>(UMSWeaponIconWidget::StaticClass(),*FString::Printf(TEXT("Deck%dWeaponIcon%d"),I,J));
				Icon->Initialize(); PlaceOnCustomizeCanvas(Card,Icon,WX+5,WY+12,95,27); WeaponIcons.Add(Icon);
			}
			else
			{
				UTextBlock* Module=CustomizeText(WidgetTree,FText::GetEmpty(),11,MSPixel::Hex(0xCEECEB),*FString::Printf(TEXT("Deck%dModuleText"),I));
				Module->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); PlaceOnCustomizeCanvas(Card,Module,WX+4,WY+15,97,25); ModuleTexts.Add(Module);
			}
		}
		UTextBlock* Name=CustomizeText(WidgetTree,FText::GetEmpty(),14,MSPixel::Hex(0xFAF5DD),*FString::Printf(TEXT("Slot%dNameText"),I));
		Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); PlaceOnCustomizeCanvas(Card,Name,18,288,212,21); SlotNameTexts.Add(Name);
		UTextBlock* Shield=CustomizeText(WidgetTree,FText::FromString(TEXT("SHIELD  盾なし")),11,MSPixel::Hex(0x78F3EE),*FString::Printf(TEXT("Deck%dShieldText"),I));
		Shield->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); PlaceOnCustomizeCanvas(Card,Shield,18,310,212,17); ShieldTexts.Add(Shield);
		UTextBlock* Stats=CustomizeText(WidgetTree,FText::GetEmpty(),14,MSPixel::Hex(0xFAF4E8),*FString::Printf(TEXT("Slot%dStatsText"),I));
		PlaceOnCustomizeCanvas(Card,Stats,18,329,211,43); SlotStatsTexts.Add(Stats);
	}
	CardButtons[0]->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleCard0Clicked);
	CardButtons[1]->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleCard1Clicked);
	CardButtons[2]->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleCard2Clicked);
	CardButtons[3]->OnClicked.AddDynamic(this,&UMSCustomizeWidget::HandleCard3Clicked);
}

void UMSCustomizeWidget::BindDeckSubsystem()
{
	UMSDeckSubsystem* Resolved = ResolveDeckSubsystem();
	if (DeckSubsystem == Resolved) return;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSCustomizeWidget::HandleDeckChanged);
	DeckSubsystem = Resolved;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSCustomizeWidget::HandleDeckChanged);
}

void UMSCustomizeWidget::RefreshAll()
{
	if(DeckSubsystem) SelectedSlotIndex=DeckSubsystem->GetSelectedSlotIndex();
	MSHangarChrome::Refresh(this);
	for(int32 I=0;I<CardCount;++I) RefreshCard(I);
	RefreshDeckCost();
}

void UMSCustomizeWidget::RefreshCard(const int32 SlotIndex)
{
	if(!SlotNameTexts.IsValidIndex(SlotIndex)) return;
	UMSMobileSuitData* Preset=DeckSubsystem?DeckSubsystem->GetPresetInSlot(SlotIndex):nullptr;
	const FMSDeckLoadout Loadout=DeckSubsystem?DeckSubsystem->GetLoadoutInSlot(SlotIndex):FMSDeckLoadout();
	SlotNameTexts[SlotIndex]->SetText(Preset?Preset->UnitName:FText::FromString(TEXT("機体を入れる")));
	CardButtons[SlotIndex]->SetToolTipText(SlotNameTexts[SlotIndex]->GetText());
	CardSkins[SlotIndex]->SetSurface(EMSVectorSurface::DeckCard,SlotIndex==SelectedSlotIndex?1:0);
	SlotNumberTexts[SlotIndex]->SetColorAndOpacity(SlotIndex==SelectedSlotIndex?MSPixel::Hex(0x875007):MSPixel::Hex(0x172127));
	PreviewImages[SlotIndex]->SetVisibility(Preset?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
	if(PreviewRenderers[SlotIndex]) PreviewRenderers[SlotIndex]->SetMobileSuit(Preset, Loadout.bShieldEquipped);
	FMSBattleAssistProgress ProgressValue;
	const FMSBattleAssistProgress* Progress = nullptr;
	if (DeckSubsystem && DeckSubsystem->GetGameInstance())
	{
		if (UMSBattleAssistProgressSubsystem* ProgressSubsystem = DeckSubsystem->GetGameInstance()->GetSubsystem<UMSBattleAssistProgressSubsystem>())
		{
			ProgressValue = ProgressSubsystem->GetProgress(); Progress = &ProgressValue;
		}
	}
	const FString ReuseTime = MSBattleAssistUI::ResolveEffectiveTime(Preset, Loadout.BattleAssist, Progress);
	SlotStatsTexts[SlotIndex]->SetText(FText::FromString(Preset?FString::Printf(TEXT("COST %d   TIME %s\n%s   ARMOR %d"),Preset->Cost,*ReuseTime,*MSCustomize::TypeLabel(Preset->UnitType),Preset->MaxArmor):TEXT("COST ---   TIME ---\n- EMPTY -")));
	for(int32 I=0;I<3;++I)
	{
		const UMSWeaponDataBase* Weapon=Loadout.EquippedWeapons.IsValidIndex(I)?Loadout.EquippedWeapons[I].LoadSynchronous():nullptr;
		const bool bShieldWeapon = Weapon && Cast<UMSShieldWeaponData>(Weapon);
		UMSWeaponIconWidget* Icon=WeaponIcons[SlotIndex*3+I]; Icon->SetVisibility(Weapon && !bShieldWeapon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
		if(Weapon && !bShieldWeapon) { Icon->SetWeaponPhoto(Weapon,MSPixel::Hex(0xA6D1E8)); Icon->SetToolTipText(Weapon->WeaponName); }
		else Icon->SetToolTipText(FText::GetEmpty());
	}
	if (ShieldTexts.IsValidIndex(SlotIndex))
	{
		const UMSShieldWeaponData* Shield = Preset ? Preset->GetNativeShield() : nullptr;
		if (!Shield)
		{
			ShieldTexts[SlotIndex]->SetText(FText::FromString(TEXT("SHIELD  盾なし")));
			ShieldTexts[SlotIndex]->SetColorAndOpacity(FSlateColor(MSPixel::Hex(0x839095)));
		}
		else
		{
			// The card is intentionally compact: state and durability must remain
			// visible when the renderer ellipsizes a long authored shield name.
			const FString State = !Preset->bShieldRemovable ? TEXT("固定装備") : (Loadout.bShieldEquipped ? TEXT("装着中") : TEXT("未装備"));
			ShieldTexts[SlotIndex]->SetText(FText::FromString(FString::Printf(TEXT("SHIELD  %s  %d"), *State, FMath::RoundToInt(Shield->MaxDurability))));
			ShieldTexts[SlotIndex]->SetColorAndOpacity(FSlateColor(MSPixel::Hex(0x78F3EE)));
		}
	}
	const UMSModuleData* Module=Preset?Loadout.EquippedModule.LoadSynchronous():nullptr;
	ModuleTexts[SlotIndex]->SetText(Module?Module->ModuleName:FText::FromString(TEXT("---")));
}

void UMSCustomizeWidget::RefreshDeckCost()
{
	if (!DeckCostText) return;
	const int32 Cost = DeckSubsystem ? DeckSubsystem->GetCurrentDeckCost() : 0;
	DeckCostText->SetText(FText::FromString(FString::Printf(TEXT("デッキコスト\n%d/%d"), Cost, UMSDeckSubsystem::DeckCostLimit)));
	DeckCostText->SetColorAndOpacity(FSlateColor(Cost > UMSDeckSubsystem::DeckCostLimit ? MSCustomize::WarningColor : FLinearColor::White));
}

void UMSCustomizeWidget::Open()
{
	if (SuitDetailWidget) SuitDetailWidget->CancelPendingSelection();
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	if (UGameInstance* GI = GetGameInstance())
		if (UMSBgmSubsystem* Bgm = GI->GetSubsystem<UMSBgmSubsystem>())
			Bgm->PlayScene(EMSBgmScene::Hangar);
	BindDeckSubsystem(); RefreshAll();
	if (GetWorld() && !IsInViewport()) AddToViewport(80);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	UMSUITransitionSubsystem::PlayEntrance(this);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGamePaused(this, true);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem())
		Lock->PushScreen(TEXT("Customize"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSCustomizeWidget::Close()
{
	if (SuitDetailWidget) SuitDetailWidget->CancelPendingSelection();
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	if (!IsOpen()) return;
	if (LobbyWidget)
	{
		UMSUITransitionSubsystem::Transition(this, LobbyWidget,
			FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseImmediate(); LobbyWidget->Open(); }));
		return;
	}
	CloseImmediate();
}

FReply UMSCustomizeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSCustomizeWidget::CloseImmediate()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TEXT("Customize"));
	if (GetWorld()) UGameplayStatics::SetGamePaused(this, false);
}

UMSDeckSubsystem* UMSCustomizeWidget::ResolveDeckSubsystem() const
{
	return DeckSubsystem ? DeckSubsystem.Get() : (GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr);
}

UMSUILockSubsystem* UMSCustomizeWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}

void UMSCustomizeWidget::HandleDeckChanged(const int32 ChangedSlotIndex)
{
	if (ChangedSlotIndex == INDEX_NONE) RefreshAll();
	else { RefreshCard(ChangedSlotIndex); RefreshDeckCost(); }
}
void UMSCustomizeWidget::HandleOwnedClicked()
{
	if (SuitDetailWidget) SuitDetailWidget->CancelPendingSelection();
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	if(EnsureSuitSwapWidget()) { SetVisibility(ESlateVisibility::Collapsed); SuitSwapWidget->OpenOwnedList(); }
}
void UMSCustomizeWidget::HandleDevelopClicked()
{
	if (SuitDetailWidget) SuitDetailWidget->CancelPendingSelection();
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	if(EnsureSuitSwapWidget()) { SetVisibility(ESlateVisibility::Collapsed); SuitSwapWidget->OpenDevelopment(); }
}
void UMSCustomizeWidget::HandleBackClicked() { Close(); }
void UMSCustomizeWidget::HandleCard0Clicked() { HandleCardClicked(0); }
void UMSCustomizeWidget::HandleCard1Clicked() { HandleCardClicked(1); }
void UMSCustomizeWidget::HandleCard2Clicked() { HandleCardClicked(2); }
void UMSCustomizeWidget::HandleCard3Clicked() { HandleCardClicked(3); }
void UMSCustomizeWidget::HandleCardClicked(const int32 SlotIndex)
{
	if (!DeckSubsystem || SlotIndex < 0 || SlotIndex >= UMSDeckSubsystem::SlotCount) return;
	if (DeckSubsystem->GetPresetInSlot(SlotIndex))
	{
		if (EnsureSuitDetailWidget())
		{
			// SuitDetail owns the selection request and hides this parent only
			// after the online PUT has been acknowledged.
			PendingDetailSlotIndex = SlotIndex;
			++SelectionRequestGeneration;
			SuitDetailWidget->Open(SlotIndex);
		}
	}
	else if (EnsureSuitSwapWidget())
	{
		const uint64 RequestGeneration = ++SelectionRequestGeneration;
		const int64 AuthGeneration = OnlineAuthGeneration(this);
		PendingDetailSlotIndex = INDEX_NONE;
		const TWeakObjectPtr<UMSCustomizeWidget> WeakThis(this);
		DeckSubsystem->RequestSelectedSlotIndex(SlotIndex,
			[WeakThis, SlotIndex, RequestGeneration, AuthGeneration](const bool bSucceeded)
		{
			if (!WeakThis.IsValid() || WeakThis->SelectionRequestGeneration != RequestGeneration) return;
			if (!IsOnlineAuthGenerationCurrent(WeakThis.Get(), AuthGeneration)) return;
			if (!bSucceeded || !WeakThis->DeckSubsystem || WeakThis->DeckSubsystem->GetPresetInSlot(SlotIndex))
			{
				WeakThis->RefreshAll();
				return;
			}
			WeakThis->SetVisibility(ESlateVisibility::Collapsed);
			if (WeakThis->SuitSwapWidget) WeakThis->SuitSwapWidget->Open(SlotIndex);
		});
	}
}

bool UMSCustomizeWidget::EnsureSuitDetailWidget()
{
	if (!SuitDetailWidget) if (APlayerController* PC = GetOwningPlayer()) SuitDetailWidget = CreateWidget<UMSSuitDetailWidget>(PC);
	if (SuitDetailWidget) SuitDetailWidget->SetCustomizeWidget(this); return SuitDetailWidget != nullptr;
}

bool UMSCustomizeWidget::EnsureSuitSwapWidget()
{
	if (!SuitSwapWidget) if (APlayerController* PC = GetOwningPlayer()) SuitSwapWidget = CreateWidget<UMSSuitSwapWidget>(PC);
	if (SuitSwapWidget) SuitSwapWidget->SetCustomizeWidget(this); return SuitSwapWidget != nullptr;
}

void UMSCustomizeWidget::HandleSuitDetailOpened(const int32 SlotIndex)
{
	if (PendingDetailSlotIndex != SlotIndex) return;
	PendingDetailSlotIndex = INDEX_NONE;
	if (IsOpen()) SetVisibility(ESlateVisibility::Collapsed);
}
void UMSCustomizeWidget::HandleSuitDetailClosed()
{
	if (SuitDetailWidget) SuitDetailWidget->CancelPendingSelection();
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	RefreshAll(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
}
void UMSCustomizeWidget::HandleSuitSwapClosed()
{
	++SelectionRequestGeneration;
	PendingDetailSlotIndex = INDEX_NONE;
	RefreshAll(); SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
}

FText UMSCustomizeWidget::GetDisplayedSlotName(const int32 SlotIndex) const
{
	return SlotNameTexts.IsValidIndex(SlotIndex) && SlotNameTexts[SlotIndex] ? SlotNameTexts[SlotIndex]->GetText() : FText::GetEmpty();
}
FText UMSCustomizeWidget::GetDisplayedShieldText(const int32 SlotIndex) const
{
	return ShieldTexts.IsValidIndex(SlotIndex) && ShieldTexts[SlotIndex] ? ShieldTexts[SlotIndex]->GetText() : FText::GetEmpty();
}
FText UMSCustomizeWidget::GetDisplayedDeckCost() const { return DeckCostText ? DeckCostText->GetText() : FText::GetEmpty(); }

void UMSCustomizeWidget::NativeDestruct()
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSCustomizeWidget::HandleDeckChanged);
	for (UMSMobileSuitPreviewRenderer* Renderer : PreviewRenderers) if (Renderer) Renderer->Shutdown();
	Super::NativeDestruct();
}

#if WITH_DEV_AUTOMATION_TESTS
void UMSCustomizeWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
void UMSCustomizeWidget::SetDeckSubsystemForTesting(UMSDeckSubsystem* InSubsystem)
{
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.RemoveDynamic(this, &UMSCustomizeWidget::HandleDeckChanged);
	DeckSubsystem = InSubsystem;
	if (DeckSubsystem) DeckSubsystem->OnDeckChanged.AddUniqueDynamic(this, &UMSCustomizeWidget::HandleDeckChanged);
	RefreshAll();
}
void UMSCustomizeWidget::SetSuitDetailWidgetForTesting(UMSSuitDetailWidget* InWidget)
{
	SuitDetailWidget = InWidget; if (SuitDetailWidget) SuitDetailWidget->SetCustomizeWidget(this);
}
void UMSCustomizeWidget::SetSuitSwapWidgetForTesting(UMSSuitSwapWidget* InWidget)
{
	SuitSwapWidget = InWidget; if (SuitSwapWidget) SuitSwapWidget->SetCustomizeWidget(this);
}
FLinearColor UMSCustomizeWidget::GetDeckCostTextColorForAutomationTests() const
{
	return DeckCostText ? DeckCostText->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
}
#endif
