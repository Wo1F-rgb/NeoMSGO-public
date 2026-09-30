// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Button.h"
#include "UI/MSCreditsWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCreditsDocumentTest,
	"NeoMSGO.UI.Credits.AuthoritativeDocuments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSCreditsDocumentTest::RunTest(const FString& Parameters)
{
	const FString Loaded = UMSCreditsWidget::LoadCreditsText();
	TestTrue(TEXT("audio credits are loaded from the maintained document"),
		Loaded.Contains(TEXT("Audio Asset Sources")));
	TestTrue(TEXT("environment credits are loaded from the maintained document"),
		Loaded.Contains(TEXT("Environment Asset Credits")));
	TestFalse(TEXT("markdown table separators are removed for in-game readability"),
		Loaded.Contains(TEXT("|---|")));

	TStrongObjectPtr<UMSCreditsWidget> Widget(NewObject<UMSCreditsWidget>(GetTransientPackage()));
	Widget->Initialize();
	Widget->InitializeForAutomationTests();
	TestNotNull(TEXT("credits screen exposes a back button"), Widget->GetCloseButtonForTesting());
	TestTrue(TEXT("credits screen displays both source documents"),
		Widget->GetDisplayedTextForTesting().Contains(TEXT("Audio Asset Sources"))
		&& Widget->GetDisplayedTextForTesting().Contains(TEXT("Environment Asset Credits")));
	return true;
}

#endif
