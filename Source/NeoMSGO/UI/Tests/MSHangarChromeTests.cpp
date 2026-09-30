#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "UI/MSCreditsWidget.h"
#include "UI/MSHangarChrome.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarChromeIdentityTest, "NeoMSGO.UI.HangarChrome.IdentityDisplayName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHangarChromeIdentityTest::RunTest(const FString&)
{
	UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	ON_SCOPE_EXIT
	{
		if (World && World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GameInstance->Shutdown();
		if (World && GEngine) GEngine->DestroyWorldContext(World);
		if (World) World->DestroyWorld(false);
		GameInstance->RemoveFromRoot();
	};
	if (!TestNotNull(TEXT("identity test has a GameInstance world"), World)) return false;

	UMSOnlineAccountSubsystem* Online = GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("identity test has the online account subsystem"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	FMSOnlineIdentity Identity;
	Identity.AccountId = TEXT("guest_uuid");
	Identity.RealmId = TEXT("home");
	Identity.DisplayName = TEXT("Pilot Display");
	Identity.bIsGuest = true;
	Online->SetIdentityForAutomationTests(Identity);
	GameInstance->SetAuthenticatedAccount(Identity, Identity.AccountId);

	// UUserWidget is abstract in UE5.7; use a concrete code-only screen as the
	// owner so this identity test never relies on an abstract-object ensure.
	TStrongObjectPtr<UMSCreditsWidget> Owner(NewObject<UMSCreditsWidget>(World));
	Owner->Initialize();
	UCanvasPanel* Canvas = Owner->WidgetTree->ConstructWidget<UCanvasPanel>();
	Owner->WidgetTree->RootWidget = Canvas;
	UTextBlock* IdentityText = MSHangarChrome::Build(Owner.Get(), Canvas, FText::FromString(TEXT("格納庫")));
	if (!TestNotNull(TEXT("Hangar chrome creates the identity label"), IdentityText)) return false;
	TestEqual(TEXT("online chrome prefers the server display name"), IdentityText->GetText().ToString(),
		FString(TEXT("Pilot Display\n接続未確認")));

	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::OfflineLocal);
	GameInstance->LoggedInUsername = TEXT("legacy-offline-name");
	MSHangarChrome::Refresh(Owner.Get());
	TestEqual(TEXT("offline chrome keeps the legacy local username"), IdentityText->GetText().ToString(),
		FString(TEXT("legacy-offline-name\n接続未確認")));
	if (UTextBlock* Stats = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarPilotStats"))))
	{
		TestEqual(TEXT("pilot footer keeps unknown currency values explicit"), Stats->GetText().ToString(),
			FString(TEXT("legacy-offline-name\nGP              ---\nBC              ---\n接続            未確認")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarChromeDesignLayoutTest, "NeoMSGO.UI.HangarChrome.DesignLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHangarChromeDesignLayoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSCreditsWidget> Owner(NewObject<UMSCreditsWidget>());
	Owner->Initialize();
	UCanvasPanel* Canvas = Owner->WidgetTree->ConstructWidget<UCanvasPanel>();
	Owner->WidgetTree->RootWidget = Canvas;
	if (!TestNotNull(TEXT("design layout owner canvas"), Canvas)) return false;
	UTextBlock* Identity = MSHangarChrome::Build(Owner.Get(), Canvas,
		FText::FromString(TEXT("格納庫 > カスタマイズ")));
	if (!TestNotNull(TEXT("design layout identity"), Identity)) return false;
	UTextBlock* Breadcrumb = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarBreadcrumbText")));
	if (!TestNotNull(TEXT("design layout breadcrumb"), Breadcrumb)) return false;
	TestEqual(TEXT("breadcrumb shows the current screen name"), Breadcrumb->GetText().ToString(), FString(TEXT("カスタマイズ")));
	TestEqual(TEXT("breadcrumb tooltip keeps the full screen path"), Breadcrumb->GetToolTipText().ToString(),
		FString(TEXT("格納庫 > カスタマイズ")));

	auto CheckSlot = [this, OwnerPtr = Owner.Get()](const FName Name, const FVector2D ExpectedPosition,
		const FVector2D ExpectedSize, const FString& Label)
	{
		UWidget* Widget = OwnerPtr->GetWidgetFromName(Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s widget"), *Label), Widget)) return;
		const UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		for (UWidget* SlotOwner = Widget; SlotOwner && !Slot; SlotOwner = SlotOwner->GetParent())
		{
			Slot = Cast<UCanvasPanelSlot>(SlotOwner->Slot);
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s canvas slot"), *Label), Slot)) return;
		TestTrue(*FString::Printf(TEXT("%s position is fixed in design pixels"), *Label),
			Slot->GetPosition().Equals(ExpectedPosition, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s size is fixed in design pixels"), *Label),
			Slot->GetSize().Equals(ExpectedSize, 0.01f));
	};

	CheckSlot(TEXT("HangarNavIcon0"), FVector2D(904.0f, 12.0f), FVector2D(64.0f, 52.0f), TEXT("first navigation icon"));
	CheckSlot(TEXT("HangarNavIcon7"), FVector2D(1408.0f, 12.0f), FVector2D(64.0f, 52.0f), TEXT("last navigation icon"));
	CheckSlot(TEXT("HangarNavLabel0"), FVector2D(900.0f, 66.0f), FVector2D(72.0f, 18.0f), TEXT("first navigation label"));
	CheckSlot(TEXT("HangarNavLabel7"), FVector2D(1404.0f, 66.0f), FVector2D(72.0f, 18.0f), TEXT("last navigation label"));
	CheckSlot(TEXT("HangarBreadcrumbText"), FVector2D(555.0f, 40.0f), FVector2D(151.0f, 33.0f), TEXT("breadcrumb header"));
	CheckSlot(TEXT("HangarFooterHint"), FVector2D(452.0f, 864.0f), FVector2D(188.0f, 21.0f), TEXT("footer notice hint"));
	CheckSlot(TEXT("HangarChatPanel"), FVector2D(452.0f, 890.0f), FVector2D(438.0f, 150.0f), TEXT("chat footer"));
	CheckSlot(TEXT("HangarPilotPanel"), FVector2D(891.0f, 890.0f), FVector2D(368.0f, 150.0f), TEXT("pilot footer"));
	CheckSlot(TEXT("HangarRosterPanel"), FVector2D(1260.0f, 890.0f), FVector2D(208.0f, 150.0f), TEXT("roster footer"));

	if (UTextBlock* Hint = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarFooterHint"))))
	{
		TestEqual(TEXT("unconnected footer hint is honest"), Hint->GetText().ToString(), FString(TEXT("詳細表示：未接続")));
	}
	if (UTextBlock* ChatStatus = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarChatStatus"))))
	{
		TestEqual(TEXT("chat empty state names the unknown connection"), ChatStatus->GetText().ToString(),
			FString(TEXT("チャット履歴：接続未確認")));
		TestEqual(TEXT("chat status uses the measured design font"), ChatStatus->GetFont().Size, 9.0f);
	}
	if (UTextBlock* RosterStatus = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarPilotRosterStatus"))))
	{
		TestEqual(TEXT("roster status names saved and unconfirmed state"), RosterStatus->GetText().ToString(),
			FString(TEXT("保存済み / 接続未確認")));
	}
	if (UTextBlock* NavLabel = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarNavLabel0"))))
	{
		TestEqual(TEXT("navigation label uses the measured design font"), NavLabel->GetFont().Size, 8.0f);
		TestEqual(TEXT("navigation label exposes the unconnected state"), NavLabel->GetToolTipText().ToString(),
			FString(TEXT("未接続 / 表示のみ")));
	}
	if (UTextBlock* Stats = Cast<UTextBlock>(Owner->GetWidgetFromName(TEXT("HangarPilotStats"))))
	{
		TestEqual(TEXT("pilot stats use the measured design font"), Stats->GetFont().Size, 12.0f);
	}
	return true;
}

#endif
