// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSCreditsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSUIStyle.h"

namespace
{
	const FName CreditsScreenId(TEXT("Credits"));

	bool LoadCreditDocument(const TCHAR* Filename, FString& OutText)
	{
		const TArray<FString> Candidates = {
			FPaths::Combine(FPaths::ProjectDir(), TEXT("docs"), Filename),
			FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("Credits"), Filename)
		};
		for (const FString& Candidate : Candidates)
		{
			if (FFileHelper::LoadFileToString(OutText, *Candidate))
			{
				return true;
			}
		}
		return false;
	}
}

void UMSCreditsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildLayout();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMSCreditsWidget::NativeDestruct()
{
	Close();
	Super::NativeDestruct();
}

void UMSCreditsWidget::BuildLayout()
{
	if (WidgetTree->RootWidget) return;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CreditsBackground"));
	Background->SetBrushColor(MSUIStyleColor::PanelBackground);
	Background->SetPadding(FMargin(80.0f, 40.0f));
	WidgetTree->RootWidget = Background;

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CreditsPanel"));
	Background->SetContent(Root);
	UTextBlock* Title = MSUIStyle::MakeText(WidgetTree, FText::FromString(TEXT("クレジット")),
		MSUIStyle::ETextRole::H1, false, MSUIStyleColor::FrameAccent);
	Root->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 12));

	CreditsTextBox = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(
		UMultiLineEditableTextBox::StaticClass(), TEXT("CreditsDocumentText"));
	CreditsTextBox->SetIsReadOnly(true);
	CreditsTextBox->SetText(FText::FromString(LoadCreditsText()));
	Root->AddChildToVerticalBox(CreditsTextBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CreditsCloseButton"));
	UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>();
	CloseLabel->SetText(FText::FromString(TEXT("戻る")));
	CloseButton->SetContent(CloseLabel);
	CloseButton->OnClicked.AddDynamic(this, &UMSCreditsWidget::HandleCloseClicked);
	MSUIStyle::ApplyButtonSound(CloseButton, MSUIStyle::EButtonSound::Back);
	Root->AddChildToVerticalBox(CloseButton)->SetPadding(FMargin(0, 12, 0, 0));
}

FString UMSCreditsWidget::LoadCreditsText()
{
	FString Audio;
	FString Environment;
	const bool bAudioLoaded = LoadCreditDocument(TEXT("AUDIO_ASSET_CREDITS.md"), Audio);
	const bool bEnvironmentLoaded = LoadCreditDocument(TEXT("ENVIRONMENT_ASSET_CREDITS.md"), Environment);
	if (!bAudioLoaded || !bEnvironmentLoaded)
	{
		return FString::Printf(TEXT("クレジット文書を読み込めませんでした。\nAudio: %s\nEnvironment: %s"),
			bAudioLoaded ? TEXT("OK") : TEXT("missing"), bEnvironmentLoaded ? TEXT("OK") : TEXT("missing"));
	}
	return MakeDisplayText(Audio) + TEXT("\n\n") + MakeDisplayText(Environment);
}

FString UMSCreditsWidget::MakeDisplayText(FString Markdown)
{
	Markdown.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	TArray<FString> Lines;
	Markdown.ParseIntoArrayLines(Lines, false);
	TArray<FString> DisplayLines;
	for (FString Line : Lines)
	{
		if (Line.StartsWith(TEXT("|---")) || Line.StartsWith(TEXT("| ---"))) continue;
		while (Line.StartsWith(TEXT("#"))) Line.RightChopInline(1, EAllowShrinking::No);
		Line.TrimStartInline();
		Line.ReplaceInline(TEXT("`"), TEXT(""));
		Line.ReplaceInline(TEXT("**"), TEXT(""));
		if (Line.StartsWith(TEXT("|")) && Line.EndsWith(TEXT("|")))
		{
			Line = Line.Mid(1, Line.Len() - 2).Replace(TEXT("|"), TEXT("  /  "));
		}
		DisplayLines.Add(MoveTemp(Line));
	}
	return FString::Join(DisplayLines, TEXT("\n"));
}

void UMSCreditsWidget::Open()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	if (!WidgetTree->RootWidget) BuildLayout();
	if (CreditsTextBox) CreditsTextBox->SetText(FText::FromString(LoadCreditsText()));
	if (!IsInViewport() && GetWorld()) AddToViewport(130);
	SetVisibility(ESlateVisibility::Visible);
	if (!bIsOpen)
	{
		bIsOpen = true;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>())
			{
				Lock->PushScreen(CreditsScreenId,
					FMSUICloseRequestDelegate::CreateWeakLambda(this, [this]() { Close(); return true; }));
			}
		}
	}
}

void UMSCreditsWidget::Close()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (!bIsOpen) return;
	bIsOpen = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMSUILockSubsystem* Lock = GI->GetSubsystem<UMSUILockSubsystem>()) Lock->PopScreen(CreditsScreenId);
	}
}

void UMSCreditsWidget::HandleCloseClicked() { Close(); }

#if WITH_DEV_AUTOMATION_TESTS
void UMSCreditsWidget::InitializeForAutomationTests() { if (!WidgetTree->RootWidget) NativeOnInitialized(); }
FString UMSCreditsWidget::GetDisplayedTextForTesting() const
{
	return CreditsTextBox ? CreditsTextBox->GetText().ToString() : FString();
}
#endif
