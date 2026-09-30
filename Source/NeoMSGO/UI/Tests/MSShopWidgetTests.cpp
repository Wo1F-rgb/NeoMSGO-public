#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSShopWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShopLobbyNavigationTest, "NeoMSGO.UI.Shop.LobbyNavigationAndSkeleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShopEscapeTest, "NeoMSGO.UI.Shop.EscapeCloseRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSShopWidgetTests
{
	template <typename T>
	TStrongObjectPtr<T> MakeWidget()
	{
		TStrongObjectPtr<T> Widget(NewObject<T>(GetTransientPackage()));
		Widget->Initialize(); Widget->InitializeForAutomationTests(); return Widget;
	}
}

bool FMSShopLobbyNavigationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSShopWidgetTests::MakeWidget<UMSLobbyWidget>();
	TStrongObjectPtr<UMSShopWidget> Shop = MSShopWidgetTests::MakeWidget<UMSShopWidget>();
	Lobby->SetShopWidgetForTesting(Shop.Get()); Lobby->Open();
	CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("ShopButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("lobby shop button opens the shop"), Shop->IsOpen());
	TestFalse(TEXT("opening the shop hides the lobby"), Lobby->IsOpen());

	const FName DisabledButtons[] = { TEXT("PurchaseButton"), TEXT("GashaconButton"), TEXT("SellButton"),
		TEXT("BuybackButton"), TEXT("ItemCodeButton"), TEXT("ExchangeButton") };
	for (const FName Name : DisabledButtons)
	{
		UButton* Button = Cast<UButton>(Shop->GetWidgetFromName(Name));
		TestNotNull(*FString::Printf(TEXT("%s exists"), *Name.ToString()), Button);
		if (Button) TestFalse(*FString::Printf(TEXT("%s is disabled"), *Name.ToString()), Button->GetIsEnabled());
	}

	CastChecked<UButton>(Shop->GetWidgetFromName(TEXT("ShopBackButton")))->OnClicked.Broadcast();
	TestFalse(TEXT("back closes the shop"), Shop->IsOpen());
	TestTrue(TEXT("back restores the lobby"), Lobby->IsOpen());
	return true;
}

bool FMSShopEscapeTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSShopWidgetTests::MakeWidget<UMSLobbyWidget>();
	TStrongObjectPtr<UMSShopWidget> Shop = MSShopWidgetTests::MakeWidget<UMSShopWidget>();
	TStrongObjectPtr<UMSUILockSubsystem> Lock(NewObject<UMSUILockSubsystem>(NewObject<UGameInstance>()));
	Shop->SetLobbyWidget(Lobby.Get()); Shop->SetUILockSubsystemForTesting(Lock.Get()); Shop->Open();
	TestTrue(TEXT("shop registers its close request"), Lock->IsAnyScreenOpen());
	TestTrue(TEXT("Esc close request is consumed"), Lock->RequestCloseTopScreen());
	TestFalse(TEXT("Esc closes the shop"), Shop->IsOpen());
	TestTrue(TEXT("Esc returns to the lobby"), Lobby->IsOpen());
	TestFalse(TEXT("shop is removed from the UI lock"), Lock->IsAnyScreenOpen());
	return true;
}

#endif
