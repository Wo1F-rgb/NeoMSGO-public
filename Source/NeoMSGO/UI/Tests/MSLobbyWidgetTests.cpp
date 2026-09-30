// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AI/MSBotCharacter.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "Data/MSMapDefinitionRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSFriendSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Online/MSRoomSubsystem.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSCommunityWidget.h"
#include "UI/MSRoomCreateWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyRulesTest, "NeoMSGO.UI.Lobby.BattleRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyRosterTest, "NeoMSGO.UI.Lobby.RegisteredRosterOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyLockAndBackgroundTest, "NeoMSGO.UI.Lobby.LockAndOpaqueBackground",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyReferenceLayoutTest, "NeoMSGO.UI.Lobby.ReferenceLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyReferenceCanvasTest, "NeoMSGO.UI.Lobby.ReferenceCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyMissionNavigationTest, "NeoMSGO.UI.Lobby.MissionRoomChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyOnlineIdentityAndJoinTest, "NeoMSGO.UI.Lobby.OnlineIdentityAndJoinRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyRoomFailureTest, "NeoMSGO.UI.Lobby.RoomFailureSurvivesWidgetRecreation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyBottomStripLayoutTest, "NeoMSGO.UI.Lobby.BottomStripSurvivesReopen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyPilotListTest, "NeoMSGO.UI.Lobby.PilotListAndCommunityNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyPartyPanelTest, "NeoMSGO.UI.Lobby.PartyPanelSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyPartyCapacityTest, "NeoMSGO.UI.Lobby.PartyCapacityRespectsSelfSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyMapSelectionTest, "NeoMSGO.UI.Lobby.MapSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyMapTravelDecisionTest, "NeoMSGO.UI.Lobby.MapTravelDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLobbyPendingSortieTest, "NeoMSGO.UI.Lobby.PendingSortie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSLobbyWidgetTests
{
	FString CollectText(const UWidget* Widget)
	{
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget)) return Text->GetText().ToString();
		FString Result;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (!Result.IsEmpty()) Result += TEXT("\n");
				Result += CollectText(Panel->GetChildAt(Index));
			}
		}
		return Result;
	}

	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSLobbyWidgetTestWorld"));
		UMSGameInstance* GameInstance = NewObject<UMSGameInstance>(GEngine);
		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World); World->SetGameInstance(GameInstance); GameInstance->Init();
			FURL URL; URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld() { GameInstance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};
}

bool FMSLobbyRulesTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	AMSGameMode* Mode = Scope.World->GetAuthGameMode<AMSGameMode>();
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	const TArray<FMSBattleRuleRow>& Rules = UMSLobbyWidget::GetBattleRules();
	TestEqual(TEXT("exactly three battle-rule rows exist"), Rules.Num(), 3);
	const float Expected[][4] = {{400,2500,10000,1200},{0,2500,10000,900},{500,0,10000,600}};
	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("rule %d applies"), Index), UMSLobbyWidget::ApplyBattleRule(Rules[Index], Mode, State));
		TestEqual(TEXT("ApplyRules receives kill damage"), Mode->GetKillDamage(), Expected[Index][0]);
		TestEqual(TEXT("ApplyRules receives base damage"), Mode->GetHomeBaseDestroyDamage(), Expected[Index][1]);
		TestEqual(TEXT("ApplyRules receives max gauge"), Mode->GetConfiguredMaxGauge(), Expected[Index][2]);
		TestEqual(TEXT("SetDuration receives duration"), State->RemainingTime, Expected[Index][3]);
		TestTrue(TEXT("one ordinary death cannot exhaust a selected rule's full gauge"),
			Rules[Index].KillDamage <= 0.0f || Rules[Index].MaxGauge > Rules[Index].KillDamage);
		Mode->ApplyGaugeDamage(MSTeam::PlayerTeamId(), Rules[Index].KillDamage);
		TestFalse(TEXT("one player-team death does not end the selected battle rule"), State->bBattleEnded);
	}
	return true;
}

bool FMSLobbyRoomFailureTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	auto* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	auto* Rooms = Scope.GameInstance->GetSubsystem<UMSRoomSubsystem>();
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Scope.GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	auto* Controller = Scope.World->SpawnActor<AMSPlayerController>();
	Controller->SetPlayer(LocalPlayer);
	FString Failure;
	for (int32 Iteration = 0; Iteration < 2; ++Iteration)
	{
		TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World));
		Lobby->SetOwningPlayer(Controller);
		Lobby->Initialize(); Lobby->InitializeForAutomationTests(); Lobby->Open();
		if (Iteration == 0)
		{
			TestFalse(TEXT("real join request fails when online session is absent"), Rooms->JoinOnlineRoom(
				TEXT("aaaaaaaa-1111-4222-8333-444444444444"), TEXT("bbbbbbbb-1111-4222-8333-444444444444")));
			Failure = Rooms->GetRoomStatusText().ToString();
			TestFalse(TEXT("operation provides a concrete reason"), Failure.IsEmpty());
		}
		const FString Text = MSLobbyWidgetTests::CollectText(Lobby->GetRootWidget());
		TestTrue(TEXT("current or recreated lobby displays failure title"), Text.Contains(TEXT("ルーム接続エラー")));
		TestTrue(TEXT("failure reason survives screen recreation in game instance"), !Failure.IsEmpty() && Text.Contains(Failure));
		if (Iteration == 1)
		{
			Rooms->NotifyLocalAdmission(Controller);
			const FString Cleared = MSLobbyWidgetTests::CollectText(Lobby->GetRootWidget());
			TestFalse(TEXT("successful admission clears the old error through status notification"), Cleared.Contains(TEXT("ルーム接続エラー")));
			TestTrue(TEXT("normal lobby notice is restored"), Cleared.Contains(TEXT("機体をカスタマイズして、戦場へ。")));
		}
		Lobby->Close(); Lobby->RemoveFromParent();
	}
	return true;
}

bool FMSLobbyRosterTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	UMSUnitRegistrySubsystem* Registry = Scope.World->GetSubsystem<UMSUnitRegistrySubsystem>();
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	AMSBotCharacter* BotA = Scope.World->SpawnActor<AMSBotCharacter>();
	AMSBotCharacter* BotB = Scope.World->SpawnActor<AMSBotCharacter>();
	const TArray<FString> Names = UMSLobbyWidget::BuildRosterNames(Registry);
	TestEqual(TEXT("roster has exactly the three registered real units"), Names.Num(), 3);
	TestEqual(TEXT("roster count equals registry count (no fake players)"), Names.Num(), Registry->GetRegisteredUnitCount());
	TestTrue(TEXT("player is represented"), Names.Contains(Player->GetName()));
	TestTrue(TEXT("first BOT is represented"), Names.Contains(BotA->GetName()));
	TestTrue(TEXT("second BOT is represented"), Names.Contains(BotB->GetName()));
	return true;
}

bool FMSLobbyLockAndBackgroundTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Widget(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests();
	TestEqual(TEXT("lobby background is fully opaque"), Widget->GetBackgroundColor().A, 1.0f);
	UMSGameInstance* Instance = NewObject<UMSGameInstance>();
	UMSUILockSubsystem* Lock = NewObject<UMSUILockSubsystem>(Instance);
	Lock->PushScreen(TEXT("Lobby"));
	TestFalse(TEXT("Lobby PushScreen disables gameplay"), Lock->IsGameplayActive());
	Lock->PopScreen(TEXT("Lobby"));
	TestTrue(TEXT("Lobby PopScreen restores gameplay"), Lock->IsGameplayActive());
	return true;
}

bool FMSLobbyReferenceLayoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Widget(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests();
	const TPair<FName, FString> Cards[] = {
		{TEXT("MissionButton"), TEXT("ミッション")}, {TEXT("CustomizeButton"), TEXT("カスタマイズ")},
		{TEXT("TrainingButton"), TEXT("大規模戦演習")}, {TEXT("GashaconButton"), TEXT("ガシャコン")},
		{TEXT("ShopButton"), TEXT("ショップ")} };
	for (const TPair<FName, FString>& Card : Cards)
	{
		UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(Card.Key));
		TestNotNull(*FString::Printf(TEXT("%s card exists"), *Card.Value), Button);
		if (Button) TestTrue(*FString::Printf(TEXT("%s title exists"), *Card.Value), MSLobbyWidgetTests::CollectText(Button).Contains(Card.Value));
	}
	TestTrue(TEXT("customize stub is enabled"), CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("CustomizeButton")))->GetIsEnabled());
	TestTrue(TEXT("shop stub is enabled"), CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("ShopButton")))->GetIsEnabled());
	TestFalse(TEXT("training is disabled"), CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("TrainingButton")))->GetIsEnabled());
	TestFalse(TEXT("gashacon is disabled"), CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("GashaconButton")))->GetIsEnabled());
	const FName GlobalButtons[] = { TEXT("GlobalShopButton"), TEXT("GlobalChallengeButton"), TEXT("GlobalRankButton"),
		TEXT("GlobalRankingButton"), TEXT("GlobalSquadButton"), TEXT("GlobalCommunityButton"),
		TEXT("GlobalPresentButton"), TEXT("GlobalExitButton") };
	for (const FName Name : GlobalButtons)
	{
		UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(Name));
		TestNotNull(*FString::Printf(TEXT("%s exists"), *Name.ToString()), Button);
		// GlobalExitButton doubles as the lobby's logout entry point (login flow).
		if (Button) TestEqual(*FString::Printf(TEXT("%s enabled state is correct"), *Name.ToString()),
			Button->GetIsEnabled(), Name == TEXT("GlobalCommunityButton") || Name == TEXT("GlobalExitButton"));
	}
	return true;
}

bool FMSLobbyReferenceCanvasTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Widget(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests();

	auto CheckSize = [this, &Widget](const TCHAR* Name, const float Width, const float Height)
	{
		USizeBox* Size = Cast<USizeBox>(Widget->GetWidgetFromName(Name));
		TestNotNull(*FString::Printf(TEXT("%s size box exists"), Name), Size);
		if (Size)
		{
			TestTrue(*FString::Printf(TEXT("%s width uses the design surface"), Name),
				FMath::IsNearlyEqual(Size->GetWidthOverride(), Width));
			TestTrue(*FString::Printf(TEXT("%s height uses the design surface"), Name),
				FMath::IsNearlyEqual(Size->GetHeightOverride(), Height));
		}
	};
	auto CheckCanvas = [this, &Widget](const TCHAR* Name, const FVector2D& Position, const FVector2D& Size)
	{
		UWidget* Child = Widget->GetWidgetFromName(Name);
		UCanvasPanelSlot* Slot = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s canvas slot exists"), Name), Slot);
		if (Slot)
		{
			TestTrue(*FString::Printf(TEXT("%s design position is stable"), Name),
				Slot->GetPosition().Equals(Position, 0.1f));
			TestTrue(*FString::Printf(TEXT("%s design size is stable"), Name),
				Slot->GetSize().Equals(Size, 0.1f));
		}
	};
	auto CheckVerticalPadding = [this, &Widget](const TCHAR* Name, const float Left, const float Top, const float Bottom)
	{
		UWidget* Child = Widget->GetWidgetFromName(Name);
		UVerticalBoxSlot* Slot = Child ? Cast<UVerticalBoxSlot>(Child->Slot) : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s vertical slot exists"), Name), Slot);
		if (Slot)
		{
			const FMargin Padding = Slot->GetPadding();
			TestTrue(*FString::Printf(TEXT("%s left padding is stable"), Name), FMath::IsNearlyEqual(Padding.Left, Left));
			TestTrue(*FString::Printf(TEXT("%s top padding is stable"), Name), FMath::IsNearlyEqual(Padding.Top, Top));
			TestTrue(*FString::Printf(TEXT("%s bottom padding is stable"), Name), FMath::IsNearlyEqual(Padding.Bottom, Bottom));
		}
	};

	CheckSize(TEXT("LobbyDesignSurface"), 1920.0f, 1080.0f);
	CheckCanvas(TEXT("LobbyPanel"), FVector2D::ZeroVector, FVector2D(1920.0f, 1080.0f));
	CheckCanvas(TEXT("GlobalNavigationRail"), FVector2D(920.0f, 34.0f), FVector2D(570.0f, 52.0f));
	// Measured from the final-era MSGO7 00:59 frame; see PixelReferenceMeasurements.md.
	CheckSize(TEXT("LobbyMainColumnsSize"), 1014.0f, 457.0f);
	CheckVerticalPadding(TEXT("LobbyMainColumnsSize"), 452.0f, 248.0f, 0.0f);
	CheckSize(TEXT("LobbyMissionSize"), 423.0f, 228.0f);
	CheckVerticalPadding(TEXT("LobbyMissionSize"), 0.0f, 0.0f, 16.0f);
	CheckSize(TEXT("LobbyMediumRow1"), 423.0f, 99.0f);
	CheckVerticalPadding(TEXT("LobbyMediumRow1"), 0.0f, 0.0f, 17.0f);
	CheckSize(TEXT("LobbyMediumRow2"), 423.0f, 97.0f);
	CheckVerticalPadding(TEXT("LobbyMediumRow2"), 0.0f, 0.0f, 0.0f);
	CheckSize(TEXT("LobbyPassSize"), 579.0f, 139.0f);
	CheckSize(TEXT("LobbyMapSize"), 579.0f, 85.0f);
	CheckSize(TEXT("LobbyNoticePartySize"), 579.0f, 212.0f);
	CheckSize(TEXT("LobbyNotificationSize"), 1016.0f, 25.0f);
	CheckVerticalPadding(TEXT("LobbyNotificationSize"), 452.0f, 158.0f, 0.0f);
	CheckSize(TEXT("BottomStripSizeBox"), 1016.0f, 150.0f);
	CheckVerticalPadding(TEXT("BottomStripSizeBox"), 452.0f, 2.0f, 0.0f);
	// Secondary screens share the existing authored 1920x1080 surface, but their root frame is
	// constrained to the clear center band so the common navigation rail cannot mask their headers.
	auto CheckSecondaryPage = [this, &Widget](const TCHAR* FrameName, const TCHAR* PanelName)
	{
		UScaleBox* Frame = Cast<UScaleBox>(Widget->GetWidgetFromName(FrameName));
		TestNotNull(*FString::Printf(TEXT("%s fixed page frame exists"), FrameName), Frame);
		if (Frame)
		{
			UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot);
			TestNotNull(*FString::Printf(TEXT("%s uses the design canvas"), FrameName), FrameSlot);
			if (FrameSlot)
			{
				TestTrue(*FString::Printf(TEXT("%s clears the common navigation"), FrameName),
					FrameSlot->GetPosition().Equals(FVector2D(452.0f, 230.0f), 0.1f));
				TestTrue(*FString::Printf(TEXT("%s keeps the fixed safe size"), FrameName),
					FrameSlot->GetSize().Equals(FVector2D(1016.0f, 630.0f), 0.1f));
			}
		}
		UWidget* Panel = Widget->GetWidgetFromName(PanelName);
		UCanvasPanelSlot* PanelSlot = Panel ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s fills the fixed page canvas"), PanelName), PanelSlot);
		if (PanelSlot)
		{
			TestTrue(*FString::Printf(TEXT("%s has breathable page insets"), PanelName),
				PanelSlot->GetPosition().Equals(FVector2D(44.0f, 30.0f), 0.1f));
			TestTrue(*FString::Printf(TEXT("%s remains inside the frame"), PanelName),
				PanelSlot->GetSize().Equals(FVector2D(924.0f, 570.0f), 0.1f));
		}
	};
	CheckSecondaryPage(TEXT("SquadPageScale"), TEXT("SquadPanel"));
	CheckSecondaryPage(TEXT("MissionChoicePageScale"), TEXT("MissionChoicePanel"));
	CheckSecondaryPage(TEXT("OperationPageScale"), TEXT("OperationPanel"));
	USizeBox* RuleCardsSize = Cast<USizeBox>(Widget->GetWidgetFromName(TEXT("OperationRuleCardsSize")));
	TestNotNull(TEXT("operation rules have a bounded card band"), RuleCardsSize);
	if (RuleCardsSize)
		TestTrue(TEXT("operation rule cards do not stretch to fill the page"),
			FMath::IsNearlyEqual(RuleCardsSize->GetHeightOverride(), 248.0f));
	return true;
}

bool FMSLobbyMissionNavigationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Widget(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests();
	UButton* Mission = CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("MissionButton")));
	UVerticalBox* Lobby = CastChecked<UVerticalBox>(Widget->GetWidgetFromName(TEXT("LobbyPanel")));
	UVerticalBox* Choices = CastChecked<UVerticalBox>(Widget->GetWidgetFromName(TEXT("MissionChoicePanel")));
	UVerticalBox* Operation = CastChecked<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OperationPanel")));
	TestEqual(TEXT("operation starts hidden"), Operation->GetVisibility(), ESlateVisibility::Collapsed);
	Mission->OnClicked.Broadcast();
	TestEqual(TEXT("mission hides lobby"), Lobby->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("mission opens room choice selection"), Choices->GetVisibility(), ESlateVisibility::Visible);
	TestNotNull(TEXT("room creation is discoverable by widget name"),
		Widget->GetWidgetFromName(TEXT("MissionCreateRoomButton")));
	TestNotNull(TEXT("room join is discoverable by widget name"),
		Widget->GetWidgetFromName(TEXT("MissionJoinRoomButton")));
	Widget->GetQuickSortieButton()->OnClicked.Broadcast();
	TestEqual(TEXT("quick sortie preserves the legacy operation selection"),
		Operation->GetVisibility(), ESlateVisibility::Visible);
	return true;
}

bool FMSLobbyOnlineIdentityAndJoinTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	UMSOnlineAccountSubsystem* Online = Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>();
	if (!TestNotNull(TEXT("online account subsystem is available"), Online)) return false;
	Online->SetAuthModeForAutomationTests(EMSOnlineAuthMode::Online);
	FMSOnlineIdentity Identity;
	Identity.AccountId = TEXT("guest-account");
	Identity.RealmId = TEXT("home");
	Identity.DisplayName = TEXT("ゲスト1");
	Identity.bIsGuest = true;
	Online->SetIdentityForAutomationTests(Identity);
	// This value intentionally resembles the backend username that must never
	// leak into a public pilot label.
	Scope.GameInstance->LoggedInUsername = TEXT("guest_8c09bcd1-4645-4347-9abc");
	Scope.GameInstance->DisplayName = Identity.DisplayName;

	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World));
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Scope.GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	APlayerController* LocalController = Scope.World->SpawnActor<APlayerController>();
	LocalController->SetPlayer(LocalPlayer);
	Lobby->SetOwningPlayer(LocalController);
	Lobby->Initialize(); Lobby->InitializeForAutomationTests(); Lobby->Open();
	const FString PilotStats = MSLobbyWidgetTests::CollectText(Lobby->GetWidgetFromName(TEXT("PilotStatsText")));
	TestTrue(TEXT("online pilot card uses the public identity display name"), PilotStats.Contains(TEXT("ゲスト1")));
	TestFalse(TEXT("online pilot card does not expose the backend username"), PilotStats.Contains(TEXT("guest_8c09bcd1")));

	UButton* Mission = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MissionButton")));
	Mission->OnClicked.Broadcast();
	TestTrue(TEXT("online mission choice exposes public room search"),
		MSLobbyWidgetTests::CollectText(Lobby->GetQuickSortieButton()).Contains(TEXT("ルームを検索")));
	TestEqual(TEXT("online mission choice keeps room creation available"), Lobby->GetCreateRoomButton()->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("online mission choice hides the legacy direct-IP row"), Lobby->GetJoinRoomButton()->GetVisibility(), ESlateVisibility::Collapsed);
	Lobby->GetQuickSortieButton()->OnClicked.Broadcast();
	TestNotNull(TEXT("online quick sortie opens the room browser"), Lobby->GetRoomCreateWidget());
	if (Lobby->GetRoomCreateWidget())
	{
		TestTrue(TEXT("room browser is open"), Lobby->GetRoomCreateWidget()->GetVisibility() != ESlateVisibility::Collapsed);
		TestTrue(TEXT("room browser shows search title"),
			MSLobbyWidgetTests::CollectText(Lobby->GetRoomCreateWidget()->GetWidgetFromName(TEXT("RoomJoinTitle"))).Contains(TEXT("ルームを検索")));
	}
	return true;
}

bool FMSLobbyBottomStripLayoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSLobbyWidget> Widget(NewObject<UMSLobbyWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests();
	USizeBox* BottomStrip = CastChecked<USizeBox>(Widget->GetWidgetFromName(TEXT("BottomStripSizeBox")));
	UVerticalBoxSlot* BottomStripSlot = CastChecked<UVerticalBoxSlot>(BottomStrip->Slot);
	const float InitialHeight = BottomStrip->GetHeightOverride();
	TestTrue(TEXT("bottom strip has an explicit positive height"), InitialHeight > 0.0f);
	TestEqual(TEXT("bottom strip occupies only its fixed height"), BottomStripSlot->GetSize().SizeRule, ESlateSizeRule::Automatic);

	for (int32 ReopenIndex = 0; ReopenIndex < 2; ++ReopenIndex)
	{
		Widget->Open();
		Widget->Close();
		Widget->Open();
		TestEqual(*FString::Printf(TEXT("bottom strip height is stable after reopen %d"), ReopenIndex + 1),
			BottomStrip->GetHeightOverride(), InitialHeight);
		TestEqual(*FString::Printf(TEXT("bottom strip size rule is stable after reopen %d"), ReopenIndex + 1),
			BottomStripSlot->GetSize().SizeRule, ESlateSizeRule::Automatic);
	}
	return true;
}

bool FMSLobbyPilotListTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	UMSFriendSubsystem* Friends = Scope.GameInstance->GetSubsystem<UMSFriendSubsystem>();
	TestNotNull(TEXT("friend subsystem is available"), Friends);
	const TArray<FMSFriendEntry> Sorted = UMSLobbyWidget::BuildPilotListEntries(Friends);
	TestEqual(TEXT("pilot list is sourced from every friend"), Sorted.Num(), Friends->GetFriends().Num());
	bool bSeenNonSquad = false;
	for (const FMSFriendEntry& Entry : Sorted)
	{
		if (!Entry.bIsSquadMember) bSeenNonSquad = true;
		if (Entry.bIsSquadMember) TestFalse(TEXT("no squad member appears below ordinary friends"), bSeenNonSquad);
	}

	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World));
	Lobby->Initialize(); Lobby->InitializeForAutomationTests(); Lobby->Open();
	TestEqual(TEXT("displayed roster count tracks friend entries"), Lobby->GetDisplayedRosterCount(), Friends->GetFriends().Num());
	TestNotNull(TEXT("PILOT LIST scroll exists"), Lobby->GetWidgetFromName(TEXT("LobbyRosterBox")));
	UButton* More = Cast<UButton>(Lobby->GetWidgetFromName(TEXT("PilotListMoreButton")));
	TestNotNull(TEXT("MORE button exists"), More);

	TStrongObjectPtr<UMSCommunityWidget> Community(NewObject<UMSCommunityWidget>(Scope.World));
	Community->Initialize(); Community->InitializeForAutomationTests();
	Lobby->SetCommunityWidgetForTesting(Community.Get());
	if (More) More->OnClicked.Broadcast();
	TestTrue(TEXT("MORE opens community"), Community->IsOpen());
	Community->Close();
	CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("GlobalCommunityButton")))->OnClicked.Broadcast();
	TestTrue(TEXT("global community button opens community"), Community->IsOpen());
	Community->Close(); Lobby->Close();
	return true;
}

bool FMSLobbyPartyPanelTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	UMSFriendSubsystem* Friends = Scope.GameInstance->GetSubsystem<UMSFriendSubsystem>();
	TestNotNull(TEXT("friend subsystem is available"), Friends);

	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World));
	Lobby->Initialize(); Lobby->InitializeForAutomationTests(); Lobby->Open();
	TestEqual(TEXT("party panel always shows six slots"), Lobby->GetPartySlotCount(), 6);

	Friends->AddFriend(TEXT("PartyFriendOne"), 5, 1500, EMSFriendState::Lobby);
	Friends->AddFriend(TEXT("PartyFriendTwo"), 5, 1500, EMSFriendState::Lobby);
	Friends->SetFriendSquadMember(TEXT("PartyFriendOne"), true);

	Lobby->Open();
	TestEqual(TEXT("party panel still shows six slots after refresh"), Lobby->GetPartySlotCount(), 6);

	UHorizontalBox* RowTop = Cast<UHorizontalBox>(Lobby->GetWidgetFromName(TEXT("PartyRowTop")));
	UHorizontalBox* RowBottom = Cast<UHorizontalBox>(Lobby->GetWidgetFromName(TEXT("PartyRowBottom")));
	TestNotNull(TEXT("party row top exists"), RowTop);
	TestNotNull(TEXT("party row bottom exists"), RowBottom);
	const FString PartyText = RowTop ? MSLobbyWidgetTests::CollectText(RowTop) : FString();
	const FString PartyTextBottom = RowBottom ? MSLobbyWidgetTests::CollectText(RowBottom) : FString();
	TestTrue(TEXT("squad member name appears in the party panel"),
		PartyText.Contains(TEXT("PartyFriendOne")) || PartyTextBottom.Contains(TEXT("PartyFriendOne")));
	TestFalse(TEXT("non-squad friend does not appear in the party panel"),
		PartyText.Contains(TEXT("PartyFriendTwo")) || PartyTextBottom.Contains(TEXT("PartyFriendTwo")));
	TestTrue(TEXT("self slot marker appears in the party panel"),
		PartyText.Contains(TEXT("★")) || PartyTextBottom.Contains(TEXT("★")));

	Lobby->Close();
	return true;
}

bool FMSLobbyPartyCapacityTest::RunTest(const FString&)
{
	const FString Slot = FString::Printf(TEXT("NeoMSGO_LobbyPartyCapacity_%s"), *FGuid::NewGuid().ToString());
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UMSFriendSubsystem> Friends(NewObject<UMSFriendSubsystem>(Instance.Get()));
	Friends->SetSaveSlotForAutomationTests(Slot);
	Friends->ResetAndReloadForAutomationTests();
	Friends->SeedDefaultFriendsForAutomationTests();

	TStrongObjectPtr<UMSCommunityWidget> Widget(NewObject<UMSCommunityWidget>(GetTransientPackage()));
	Widget->Initialize(); Widget->InitializeForAutomationTests(); Widget->SetFriendSubsystemForTesting(Friends.Get());

	// Fill the squad to five members: the sixth slot is reserved for the local player.
	for (const FMSFriendEntry& Entry : TArray<FMSFriendEntry>(Friends->GetFriends()))
	{
		if (Friends->GetSquadMemberCount() >= 5) break;
		Friends->SetFriendSquadMember(Entry.Name, true);
	}
	TestEqual(TEXT("squad holds exactly five invited members"), Friends->GetSquadMemberCount(), 5);

	FString RejectedName;
	for (const FMSFriendEntry& Entry : Friends->GetFriends()) if (!Entry.bIsSquadMember) { RejectedName = Entry.Name; break; }
	TestFalse(TEXT("inviting a sixth friend is rejected because self occupies one of six slots"),
		Widget->InviteFriend(RejectedName));
	TestFalse(TEXT("rejected friend remains outside the squad"), Friends->FindFriend(RejectedName)->bIsSquadMember);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

bool FMSLobbyMapSelectionTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	Scope.GameInstance->GetSubsystem<UMSOnlineAccountSubsystem>()->SetAuthModeForAutomationTests(EMSOnlineAuthMode::OfflineLocal);
	Scope.GameInstance->SelectedMapId = TEXT("NewYark");
	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World));
	Lobby->Initialize();
	Lobby->InitializeForAutomationTests();
	Lobby->Open();

	const TArray<FMSMapDefinitionRow>& Definitions = FMSMapDefinitionRow::GetVisibleMapDefinitions();
	TestEqual(TEXT("one map card is generated for every visible definition"), Lobby->GetMapCardCount(), Definitions.Num());
	TestEqual(TEXT("eight visible definitions use two pages"), Lobby->GetMapPageCount(), 2);
	TestEqual(TEXT("first page is initially visible"), Lobby->GetCurrentMapPage(), 0);
	TestEqual(TEXT("page label shows current and total pages"),
		CastChecked<UTextBlock>(Lobby->GetWidgetFromName(TEXT("MapPageLabel")))->GetText().ToString(), FString(TEXT("1 / 2")));
	TestFalse(TEXT("archived GameInstance map has no selection card"), Lobby->IsMapCardSelected(1));
	TestFalse(TEXT("other playable map is not initially highlighted"), Lobby->IsMapCardSelected(0));

	UButton* FirstMap = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MapCard0")));
	UButton* SecondMap = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MapCard1")));
	UButton* DaibaoMap = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MapCard2")));
	UButton* UnavailableMap = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MapCard3")));
	TestTrue(TEXT("NewYorkTaigan map card is enabled"), SecondMap->GetIsEnabled());
	TestTrue(TEXT("new map title appears on its card"),
		MSLobbyWidgetTests::CollectText(SecondMap).Contains(TEXT("対岸の銃火")));
	TestTrue(TEXT("Daibao map card is enabled"), DaibaoMap->GetIsEnabled());
	TestFalse(TEXT("unavailable map card is disabled"), UnavailableMap->GetIsEnabled());
	SecondMap->OnClicked.Broadcast();
	TestEqual(TEXT("new map card chooses the new ID"), Scope.GameInstance->SelectedMapId, FName(TEXT("NewYorkTaigan")));
	TestTrue(TEXT("new map card is highlighted"), Lobby->IsMapCardSelected(1));
	FirstMap->OnClicked.Broadcast();
	TestEqual(TEXT("clicking a playable card updates SelectedMapId"), Scope.GameInstance->SelectedMapId, Definitions[0].MapId);
	TestFalse(TEXT("highlight leaves the new map"), Lobby->IsMapCardSelected(1));
	TestTrue(TEXT("highlight moves to the clicked map"), Lobby->IsMapCardSelected(0));

	UnavailableMap->OnClicked.Broadcast();
	TestEqual(TEXT("an unavailable card cannot change SelectedMapId"), Scope.GameInstance->SelectedMapId, Definitions[0].MapId);
	TestTrue(TEXT("unavailable card contains its status"),
		MSLobbyWidgetTests::CollectText(UnavailableMap).Contains(TEXT("配信準備中")));

	UButton* PreviousPage = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("PreviousMapPageButton")));
	UButton* NextPage = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("NextMapPageButton")));
	TestFalse(TEXT("previous page is disabled at the first page"), PreviousPage->GetIsEnabled());
	TestTrue(TEXT("next page is enabled at the first page"), NextPage->GetIsEnabled());
	NextPage->OnClicked.Broadcast();
	TestEqual(TEXT("next page advances the map list"), Lobby->GetCurrentMapPage(), 1);
	TestEqual(TEXT("second page label is updated"),
		CastChecked<UTextBlock>(Lobby->GetWidgetFromName(TEXT("MapPageLabel")))->GetText().ToString(), FString(TEXT("2 / 2")));
	TestEqual(TEXT("first-page card is collapsed on page two"), FirstMap->GetVisibility(), ESlateVisibility::Collapsed);
	UButton* SpaceMap = CastChecked<UButton>(Lobby->GetWidgetFromName(TEXT("MapCard7")));
	TestEqual(TEXT("space map is visible on page two"), SpaceMap->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("space map contains both availability and space labels"),
		MSLobbyWidgetTests::CollectText(SpaceMap).Contains(TEXT("配信準備中")) &&
		MSLobbyWidgetTests::CollectText(SpaceMap).Contains(TEXT("宇宙")));
	TestFalse(TEXT("next page is disabled at the last page"), NextPage->GetIsEnabled());
	TestTrue(TEXT("previous page is enabled at the last page"), PreviousPage->GetIsEnabled());
	PreviousPage->OnClicked.Broadcast();
	TestEqual(TEXT("previous page returns to page one"), Lobby->GetCurrentMapPage(), 0);

	Lobby->Close();
	Lobby->Open();
	TestTrue(TEXT("reopening restores the GameInstance selection highlight"), Lobby->IsMapCardSelected(0));
	TestFalse(TEXT("reopening keeps the old card unselected"), Lobby->IsMapCardSelected(1));
	Lobby->Close();
	return true;
}

bool FMSLobbyMapTravelDecisionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSGameInstance> Instance(NewObject<UMSGameInstance>());
	FString DestinationLevelName;

	Instance->SelectedMapId = TEXT("UnrealizerColony");
	TestFalse(TEXT("selecting the current map keeps the legacy in-world flow"),
		UMSLobbyWidget::PrepareMapTravel(Instance.Get(), 2, TEXT("Lvl_GrandBattlefield"), DestinationLevelName));
	TestTrue(TEXT("same-map flow does not set pending sortie"), !Instance->bPendingSortie);
	TestTrue(TEXT("same-map flow has no travel destination"), DestinationLevelName.IsEmpty());

	Instance->SelectedMapId = TEXT("NewYark");
	TestTrue(TEXT("selecting another known map chooses the OpenLevel flow"),
		UMSLobbyWidget::PrepareMapTravel(Instance.Get(), 2, TEXT("UEDPIE_0_Lvl_GrandBattlefield"), DestinationLevelName));
	TestEqual(TEXT("OpenLevel receives the destination short level name"), DestinationLevelName,
		FString(TEXT("Lvl_Biome_City")));
	TestTrue(TEXT("map travel stores pending sortie"), Instance->bPendingSortie);
	TestEqual(TEXT("map travel stores the selected rule index"), Instance->PendingRuleIndex, 2);
	Instance->bPendingSortie = false;
	Instance->SelectedMapId = TEXT("NewYorkTaigan");
	TestTrue(TEXT("new Taigan map uses the OpenLevel flow"),
		UMSLobbyWidget::PrepareMapTravel(Instance.Get(), 1, TEXT("UEDPIE_0_Lvl_GrandBattlefield"), DestinationLevelName));
	TestEqual(TEXT("new Taigan map travel targets its own level"), DestinationLevelName,
		FString(TEXT("Lvl_NewYork_Taigan")));
	TestTrue(TEXT("new Taigan travel stores pending sortie"), Instance->bPendingSortie);
	return true;
}

bool FMSLobbyPendingSortieTest::RunTest(const FString&)
{
	MSLobbyWidgetTests::FScopedWorld Scope;
	AMSGameMode* Mode = Scope.World->GetAuthGameMode<AMSGameMode>();
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	Scope.GameInstance->bPendingSortie = true;
	Scope.GameInstance->PendingRuleIndex = 1;
	FText RuleName;

	TestTrue(TEXT("pending sortie applies its saved battle rule"),
		UMSLobbyWidget::ConsumePendingBattleRule(Scope.GameInstance, Mode, State, RuleName));
	TestFalse(TEXT("pending sortie is reset after consumption"), Scope.GameInstance->bPendingSortie);
	TestEqual(TEXT("consumed rule name is available for the briefing header"), RuleName.ToString(),
		UMSLobbyWidget::GetBattleRules()[1].Name.ToString());
	TestEqual(TEXT("pending rule reaches the new GameMode"), Mode->GetKillDamage(),
		UMSLobbyWidget::GetBattleRules()[1].KillDamage);
	TestEqual(TEXT("pending rule reaches the new GameState"), State->RemainingTime,
		UMSLobbyWidget::GetBattleRules()[1].DurationSeconds);

	TestFalse(TEXT("a consumed pending sortie cannot run twice"),
		UMSLobbyWidget::ConsumePendingBattleRule(Scope.GameInstance, Mode, State, RuleName));
	return true;
}

#endif
