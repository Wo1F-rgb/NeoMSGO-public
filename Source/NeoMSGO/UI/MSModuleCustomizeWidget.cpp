#include "UI/MSModuleCustomizeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Engine/AssetManager.h"
#include "InputCoreTypes.h"
#include "UI/MSSuitDetailWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"

namespace MSModuleCustomize
{
	const FLinearColor BackgroundColor(0.003f, 0.012f, 0.02f, 0.96f);
	const FLinearColor PanelColor(0.025f, 0.09f, 0.115f, 0.98f);
	const FLinearColor CardColor(0.045f, 0.13f, 0.16f, 1.0f);
	const FLinearColor AccentColor(0.18f, 0.9f, 0.84f, 1.0f);
	const FLinearColor SelectedColor(0.95f, 0.72f, 0.08f, 1.0f);
}

void UMSModuleCatalogEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UButton* Button = MSUIStyle::MakeCapsuleButton(WidgetTree, nullptr, MSModuleCustomize::CardColor, TEXT("ModuleCatalogEntryButton"));
	ModuleNameText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("---")), MSUIStyle::ETextRole::Body, false,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("ModuleCatalogEntryText"));
	Button->SetContent(ModuleNameText); Button->OnClicked.AddDynamic(this, &UMSModuleCatalogEntryWidget::HandleClicked);
	WidgetTree->RootWidget = Button;
}

void UMSModuleCatalogEntryWidget::SetModule(UMSModuleData* InModule, UMSModuleCustomizeWidget* InOwner)
{
	Module = InModule; Owner = InOwner;
	if (ModuleNameText)
	{
		if (Module && !Module->ModuleName.IsEmpty())
		{
			ModuleNameText->SetText(FText::FromString(FString::Printf(TEXT("◆  %s    SLOT ---"),
				*Module->ModuleName.ToString())));
		}
		else
		{
			ModuleNameText->SetText(FText::FromString(TEXT("---")));
		}
	}
}

void UMSModuleCatalogEntryWidget::HandleClicked()
{
	if (Owner && Module) Owner->ChooseModule(Module);
}

void UMSModuleCustomizeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true); BuildReferenceLayout(); SetVisibility(ESlateVisibility::Collapsed);
}

void UMSModuleCustomizeWidget::BuildReferenceLayout()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(); Background->SetBrushColor(MSModuleCustomize::BackgroundColor); Background->SetPadding(FMargin(45, 35)); WidgetTree->RootWidget = Background;
	UVerticalBox* Screen = WidgetTree->ConstructWidget<UVerticalBox>(); Background->SetContent(Screen);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(); Screen->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 12));
	Header->AddChildToHorizontalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("モジュール  module customize")), MSUIStyle::ETextRole::H1, false, MSModuleCustomize::AccentColor))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* Back = MSUIStyle::MakeCapsuleButton(WidgetTree, MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("< 戻る")), MSUIStyle::ETextRole::Body, true,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false), MSModuleCustomize::PanelColor, TEXT("ModuleCustomizeBackButton"), MSUIStyle::EButtonSound::Back);
	Back->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleBackClicked); Header->AddChildToHorizontalBox(Back)->SetPadding(FMargin(12, 0));
	NoticeText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("オンラインの装備保存・戦闘反映は未対応です。候補の詳細だけ確認できます。")), MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextSecondary, MSUIStyle::ENumericLargeSize::Standard, TEXT("ModuleCustomizeNoticeText")); NoticeText->SetAutoWrapText(true); Screen->AddChildToVerticalBox(NoticeText)->SetPadding(FMargin(0, 0, 0, 12));

	UHorizontalBox* Main = WidgetTree->ConstructWidget<UHorizontalBox>(); Screen->AddChildToVerticalBox(Main)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* CatalogColumn = WidgetTree->ConstructWidget<UVerticalBox>(); UHorizontalBoxSlot* CatalogSlot = Main->AddChildToHorizontalBox(CatalogColumn); FSlateChildSize CatalogSize(ESlateSizeRule::Fill); CatalogSize.Value = 0.85f; CatalogSlot->SetSize(CatalogSize); CatalogSlot->SetPadding(FMargin(0, 0, 18, 0));
	CatalogColumn->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("モジュールリスト")), MSUIStyle::ETextRole::H2, false, MSModuleCustomize::AccentColor))->SetPadding(FMargin(0, 0, 0, 8));
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(); CatalogColumn->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 8));
	const TCHAR* CategoryLabels[] = { TEXT("強化"), TEXT("特殊"), TEXT("補助"), TEXT("その他") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CategoryLabels); ++Index)
	{
		UButton* Tab = MSUIStyle::MakeCapsuleButton(WidgetTree, MSUIStyle::MakeText(WidgetTree, FText::FromString(CategoryLabels[Index]), MSUIStyle::ETextRole::Body, true,
			MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false), Index == 0 ? MSModuleCustomize::SelectedColor : MSModuleCustomize::PanelColor, *FString::Printf(TEXT("ModuleCategoryButton%d"), Index));
		Tabs->AddChildToHorizontalBox(Tab)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CategoryButtons.Add(Tab);
	}
	CategoryButtons[0]->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleCategoryEnhancementClicked);
	CategoryButtons[1]->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleCategorySpecialClicked);
	CategoryButtons[2]->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleCategorySupportClicked);
	CategoryButtons[3]->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleCategoryOtherClicked);
	USizeBox* CatalogBox = WidgetTree->ConstructWidget<USizeBox>(); CatalogBox->SetMinDesiredHeight(470.0f); CatalogScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ModuleCatalogScrollBox")); CatalogScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible); CatalogBox->SetContent(CatalogScrollBox); CatalogColumn->AddChildToVerticalBox(CatalogBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RemoveButton = MSUIStyle::MakeCapsuleButton(WidgetTree, MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("装備を外す")), MSUIStyle::ETextRole::Body, true,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false), MSModuleCustomize::PanelColor, TEXT("RemoveModuleButton"));
	RemoveButton->SetIsEnabled(false);
	RemoveButton->SetToolTipText(FText::FromString(TEXT("オンラインのモジュール装備保存は未対応です")));
	RemoveButton->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleRemoveClicked); CatalogColumn->AddChildToVerticalBox(RemoveButton)->SetPadding(FMargin(0, 12, 0, 0));

	UVerticalBox* Details = WidgetTree->ConstructWidget<UVerticalBox>(); UBorder* DetailsBorder = nullptr; UBorder* DetailsFrame = MSUIStyle::MakeFramedPanel(WidgetTree, DetailsBorder, Details, false, MSModuleCustomize::AccentColor, MSModuleCustomize::PanelColor, FMargin(18)); Main->AddChildToHorizontalBox(DetailsFrame)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Details->AddChildToVerticalBox(MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("モジュールスロット")), MSUIStyle::ETextRole::H2, false, MSModuleCustomize::AccentColor))->SetPadding(FMargin(0, 0, 0, 4));
	ModuleSlotText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("スロット数  ---")), MSUIStyle::ETextRole::NumericLarge, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("ModuleSlotCountText")); Details->AddChildToVerticalBox(ModuleSlotText);
	EquippedIconText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("[ オンライン未対応 ]")), MSUIStyle::ETextRole::Body, true, MSModuleCustomize::AccentColor, MSUIStyle::ENumericLargeSize::Standard, TEXT("EquippedModuleIconText")); Details->AddChildToVerticalBox(EquippedIconText)->SetPadding(FMargin(0, 4, 0, 18));
	ModuleNameText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("---")), MSUIStyle::ETextRole::H2, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("SelectedModuleNameText")); Details->AddChildToVerticalBox(ModuleNameText)->SetPadding(FMargin(0, 0, 0, 6));
	DescriptionText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("モジュールを選択してください")), MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextSecondary, MSUIStyle::ENumericLargeSize::Standard, TEXT("SelectedModuleDescriptionText")); DescriptionText->SetAutoWrapText(true); Details->AddChildToVerticalBox(DescriptionText)->SetPadding(FMargin(0, 0, 0, 18));
	ParametersText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("スロットコスト  ---\n効果時間  ---\n強制OH  ---\nクールダウン  ---")), MSUIStyle::ETextRole::Body, false, MSModuleCustomize::AccentColor, MSUIStyle::ENumericLargeSize::Standard, TEXT("SelectedModuleParametersText")); Details->AddChildToVerticalBox(ParametersText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	EquippedStateText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("現在の装備: オンライン未対応（変更なし）")), MSUIStyle::ETextRole::Body, false, MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, TEXT("EquippedModuleStateText")); Details->AddChildToVerticalBox(EquippedStateText)->SetPadding(FMargin(0, 10, 0, 8));
	EquipButtonText = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("装備（未対応）")), MSUIStyle::ETextRole::Body, true,
		MSUIStyleColor::TextPrimary, MSUIStyle::ENumericLargeSize::Standard, NAME_None, false);
	EquipButton = MSUIStyle::MakeCapsuleButton(WidgetTree, EquipButtonText, MSModuleCustomize::CardColor, TEXT("EquipModuleButton"));
	EquipButton->SetIsEnabled(false);
	EquipButton->SetToolTipText(FText::FromString(TEXT("オンラインのモジュール装備保存は未対応です")));
	EquipButton->OnClicked.AddDynamic(this, &UMSModuleCustomizeWidget::HandleEquipClicked); Details->AddChildToVerticalBox(EquipButton);
}

void UMSModuleCustomizeWidget::Open(UMSMobileSuitData* Preset, const int32 SlotIndex)
{
	if (!Preset) return; ActivePreset = Preset;
	if (ModuleCatalog.IsEmpty()) RefreshCatalogFromAssetManager();
	UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	ActiveSlotIndex = SlotIndex != INDEX_NONE ? SlotIndex : (Deck ? Deck->GetSelectedSlotIndex() : INDEX_NONE);
	bLastSaveFailed = false;
	SelectedModule = GetCurrentModule();
	if (!SelectedModule) SelectedModule = ModuleCatalog.IsEmpty() ? nullptr : ModuleCatalog[0].Get();
	SelectCategory(0); RefreshDetails();
	if (GetWorld() && !IsInViewport()) AddToViewport(100);
	SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PushScreen(TEXT("ModuleCustomize"), FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
}

void UMSModuleCustomizeWidget::Close()
{
	if (!IsOpen()) return; SetVisibility(ESlateVisibility::Collapsed);
	if (UMSUILockSubsystem* Lock = ResolveUILockSubsystem()) Lock->PopScreen(TEXT("ModuleCustomize"));
	if (ReturnSuitDetailWidget) ReturnSuitDetailWidget->HandleModuleCustomizeClosed();
}

FReply UMSModuleCustomizeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMSModuleCustomizeWidget::SetModuleCatalog(const TArray<UMSModuleData*>& Modules)
{
	ModuleCatalog.Reset(); for (UMSModuleData* Module : Modules) if (IsValid(Module)) ModuleCatalog.Add(Module);
	RefreshCatalogRows();
}

void UMSModuleCustomizeWidget::RefreshCatalogFromAssetManager()
{
	UAssetManager& Manager = UAssetManager::Get(); TArray<FPrimaryAssetId> Ids; Manager.GetPrimaryAssetIdList(UMSModuleData::PrimaryAssetType, Ids);
	Ids.Sort([](const FPrimaryAssetId& A, const FPrimaryAssetId& B) { return A.ToString() < B.ToString(); });
	TArray<UMSModuleData*> Modules;
	for (const FPrimaryAssetId& Id : Ids)
	{
		UMSModuleData* Module = Cast<UMSModuleData>(Manager.GetPrimaryAssetObject(Id));
		if (!Module) Module = Cast<UMSModuleData>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		if (Module) Modules.Add(Module);
	}
	SetModuleCatalog(Modules);
}

void UMSModuleCustomizeWidget::RefreshCatalogRows()
{
	FilteredModules.Reset();
	// UMSModuleData currently has no category field; all entries intentionally live in the Enhancement tab.
	if (SelectedCategoryIndex == 0) FilteredModules = ModuleCatalog;
	if (!CatalogScrollBox) return; CatalogScrollBox->ClearChildren();
	for (UMSModuleData* Module : FilteredModules)
	{
		UMSModuleCatalogEntryWidget* Entry = WidgetTree->ConstructWidget<UMSModuleCatalogEntryWidget>(); Entry->Initialize(); Entry->SetModule(Module, this);
		if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(CatalogScrollBox->AddChild(Entry))) EntrySlot->SetPadding(FMargin(0, 0, 0, 6));
	}
	CatalogScrollBox->ScrollToStart();
}

void UMSModuleCustomizeWidget::SelectCategory(const int32 CategoryIndex)
{
	if (CategoryIndex < 0 || CategoryIndex >= 4) return; SelectedCategoryIndex = CategoryIndex;
	for (int32 Index = 0; Index < CategoryButtons.Num(); ++Index)
	{
		const FLinearColor Tint = Index == SelectedCategoryIndex ? MSModuleCustomize::SelectedColor : MSModuleCustomize::PanelColor;
		CategoryButtons[Index]->SetBackgroundColor(Tint);
	}
	RefreshCatalogRows();
}

UMSDeckSubsystem* UMSModuleCustomizeWidget::ResolveDeckSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDeckSubsystem) return TestDeckSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSDeckSubsystem>() : nullptr;
}

bool UMSModuleCustomizeWidget::IsModuleEquipmentSupported() const
{
	const UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	return Deck && ActivePreset && Deck->SupportsModuleEquipment()
		&& Deck->GetPresetInSlot(ActiveSlotIndex) == ActivePreset;
}

UMSModuleData* UMSModuleCustomizeWidget::GetCurrentModule() const
{
	const UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	return IsModuleEquipmentSupported() ? Deck->GetLoadoutInSlot(ActiveSlotIndex).EquippedModule.LoadSynchronous() : nullptr;
}

bool UMSModuleCustomizeWidget::IsModuleEquipped(const UMSModuleData* Module) const
{
	return Module && GetCurrentModule() == Module;
}

void UMSModuleCustomizeWidget::ChooseModule(UMSModuleData* Module)
{
	if (ModuleCatalog.Contains(Module)) { SelectedModule = Module; RefreshDetails(); }
}

void UMSModuleCustomizeWidget::RefreshDetails()
{
	const bool bSupported = IsModuleEquipmentSupported();
	const UMSModuleData* Equipped = GetCurrentModule();
	if (NoticeText) NoticeText->SetText(FText::FromString(bLastSaveFailed
		? TEXT("保存できませんでした。装備は変更されていません。")
		: bSupported ? TEXT("この編成枠のモジュールを保存します。スロット数・コストは未設定です。")
		: TEXT("オンラインの装備保存・戦闘反映は未対応です。候補の詳細だけ確認できます。")));
	if (ModuleNameText) ModuleNameText->SetText(SelectedModule ? SelectedModule->ModuleName : FText::FromString(TEXT("---")));
	if (DescriptionText) DescriptionText->SetText(SelectedModule ? SelectedModule->Description : FText::FromString(TEXT("モジュールを選択してください")));
	if (ParametersText) ParametersText->SetText(SelectedModule ? FText::FromString(FString::Printf(
		TEXT("スロットコスト  ---\n効果時間  %.2f秒\n強制OH  %.2f秒\nクールダウン  %.2f秒"),
		SelectedModule->EffectDurationSeconds, SelectedModule->ForcedOverheatSeconds, SelectedModule->CooldownSeconds))
		: FText::FromString(TEXT("スロットコスト  ---\n効果時間  ---\n強制OH  ---\nクールダウン  ---")));
	if (EquippedStateText) EquippedStateText->SetText(FText::FromString(bSupported
		? FString::Printf(TEXT("現在の装備: %s"), Equipped ? *Equipped->ModuleName.ToString() : TEXT("なし"))
		: TEXT("現在の装備: オンライン未対応")));
	if (ModuleSlotText) ModuleSlotText->SetText(FText::FromString(TEXT("スロット数  ---")));
	if (EquippedIconText) EquippedIconText->SetText(FText::FromString(bSupported
		? (Equipped ? TEXT("[ 装備中 ]") : TEXT("[ 未装備 ]")) : TEXT("[ オンライン未対応 ]")));
	if (EquipButtonText) EquipButtonText->SetText(FText::FromString(!bSupported ? TEXT("装備（未対応）")
		: IsModuleEquipped(SelectedModule) ? TEXT("装備中") : TEXT("装備")));
	if (EquipButton)
	{
		EquipButton->SetIsEnabled(bSupported && SelectedModule && !IsModuleEquipped(SelectedModule));
		EquipButton->SetToolTipText(bSupported ? FText::GetEmpty() : FText::FromString(TEXT("オンラインのモジュール装備保存は未対応です")));
	}
	if (RemoveButton)
	{
		RemoveButton->SetIsEnabled(bSupported && Equipped);
		RemoveButton->SetToolTipText(bSupported ? FText::GetEmpty() : FText::FromString(TEXT("オンラインのモジュール装備保存は未対応です")));
	}
}

void UMSModuleCustomizeWidget::NotifyModuleChanged()
{
	RefreshCatalogRows(); RefreshDetails(); if (ReturnSuitDetailWidget) ReturnSuitDetailWidget->HandleModuleCustomizeChanged();
}

void UMSModuleCustomizeWidget::CommitModule(UMSModuleData* Module)
{
	if (!IsModuleEquipmentSupported()) return;
	UMSDeckSubsystem* Deck = ResolveDeckSubsystem();
	FMSDeckLoadout Candidate = Deck->GetLoadoutInSlot(ActiveSlotIndex);
	Candidate.EquippedModule = Module;
	bLastSaveFailed = !Deck->CommitLoadout(ActiveSlotIndex, Candidate);
	if (bLastSaveFailed) RefreshDetails();
	else NotifyModuleChanged();
}

void UMSModuleCustomizeWidget::HandleEquipClicked()
{
	if (SelectedModule && ModuleCatalog.Contains(SelectedModule)) CommitModule(SelectedModule);
}

void UMSModuleCustomizeWidget::HandleRemoveClicked()
{
	CommitModule(nullptr);
}

void UMSModuleCustomizeWidget::HandleBackClicked() { Close(); }
void UMSModuleCustomizeWidget::HandleCategoryEnhancementClicked() { SelectCategory(0); }
void UMSModuleCustomizeWidget::HandleCategorySpecialClicked() { SelectCategory(1); }
void UMSModuleCustomizeWidget::HandleCategorySupportClicked() { SelectCategory(2); }
void UMSModuleCustomizeWidget::HandleCategoryOtherClicked() { SelectCategory(3); }
UMSUILockSubsystem* UMSModuleCustomizeWidget::ResolveUILockSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestUILockSubsystem) return TestUILockSubsystem;
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSUILockSubsystem>() : nullptr;
}
FText UMSModuleCustomizeWidget::GetDisplayedModuleName() const { return ModuleNameText ? ModuleNameText->GetText() : FText::GetEmpty(); }

#if WITH_DEV_AUTOMATION_TESTS
void UMSModuleCustomizeWidget::InitializeForAutomationTests(UMSMobileSuitData* Preset, const TArray<UMSModuleData*>& Modules)
{
	if (!WidgetTree->RootWidget) NativeOnInitialized(); ActivePreset = Preset; SetModuleCatalog(Modules);
}
#endif
