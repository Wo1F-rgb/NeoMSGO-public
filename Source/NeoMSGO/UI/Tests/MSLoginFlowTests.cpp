// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Core/MSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSAuthSubsystem.h"
#include "Gameplay/MSGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSLoginWidget.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSTitleWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowTitleToLoginTest,
	"NeoMSGO.UI.LoginFlow.TitleStartOpensLogin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowLoginSuccessTest,
	"NeoMSGO.UI.LoginFlow.LoginSuccessEntersLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowLoginFailureTest,
	"NeoMSGO.UI.LoginFlow.EmptyNameShowsError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowRegistrationTest,
	"NeoMSGO.UI.LoginFlow.AutoRegistrationReloginAndGuest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowBackToTitleTest,
	"NeoMSGO.UI.LoginFlow.BackButtonReturnsToTitle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowSettingsLogoutTest,
	"NeoMSGO.UI.LoginFlow.SettingsLogoutReturnsToLogin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSLoginFlowLobbyLogoutTest,
	"NeoMSGO.UI.LoginFlow.LobbyExitLogsOutToLogin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSLoginFlowTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSLoginFlowTestWorld"));
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

	/** Auth subsystem isolated to a throwaway save slot so tests never touch real accounts. */
	struct FScopedAuth
	{
		FString Slot = FString::Printf(TEXT("NeoMSGO_LoginFlowAutomation_%s"), *FGuid::NewGuid().ToString());
		TStrongObjectPtr<UMSAuthSubsystem> Auth;
		explicit FScopedAuth(UMSGameInstance* Instance)
			: Auth(NewObject<UMSAuthSubsystem>(Instance))
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
			Auth->SetSaveSlotForAutomationTests(Slot);
			Auth->ResetAndReloadForAutomationTests();
		}
		~FScopedAuth() { UGameplayStatics::DeleteGameInSlot(Slot, 0); }
	};

	template <typename WidgetType>
	TStrongObjectPtr<WidgetType> MakeWidget(UObject* Outer)
	{
		TStrongObjectPtr<WidgetType> Widget(NewObject<WidgetType>(Outer));
		Widget->Initialize();
		Widget->InitializeForAutomationTests();
		return Widget;
	}

	void EnterUsername(UMSLoginWidget* Login, const TCHAR* Username)
	{
		Login->GetUsernameBoxForTesting()->SetText(FText::FromString(Username));
	}
}

bool FMSLoginFlowTitleToLoginTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSTitleWidget> Title = MSLoginFlowTests::MakeWidget<UMSTitleWidget>(GetTransientPackage());
	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(GetTransientPackage());
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMSUILockSubsystem* UILock = NewObject<UMSUILockSubsystem>(GameInstance);
	Title->SetUILockSubsystemForTesting(UILock);
	Login->SetUILockSubsystemForTesting(UILock);
	Title->SetLoginWidgetForTesting(Login.Get());

	Title->Open();
	TestTrue(TEXT("the title screen opens"), Title->IsOpen());
	TestTrue(TEXT("the title screen locks gameplay"), UILock->IsAnyScreenOpen());
	TestFalse(TEXT("Esc never dismisses the title root"), !UILock->RequestCloseTopScreen() || !Title->IsOpen());

	UButton* Start = Title->GetStartButtonForTesting();
	if (!TestNotNull(TEXT("the title screen owns a start button"), Start)) return false;
	Start->OnClicked.Broadcast();
	TestFalse(TEXT("start closes the title screen"), Title->IsOpen());
	TestTrue(TEXT("start opens the login screen"), Login->IsOpen());

	TestNotNull(TEXT("username input exists"), Login->GetUsernameBoxForTesting());
	TestNotNull(TEXT("guest entry button exists"), Login->GetGuestButtonForTesting());
	return true;
}

bool FMSLoginFlowLoginSuccessTest::RunTest(const FString&)
{
	MSLoginFlowTests::FScopedWorld Scope;
	MSLoginFlowTests::FScopedAuth Auth(Scope.GameInstance);
	TestTrue(TEXT("precondition: account registers"), Auth.Auth->RegisterAccount(TEXT("AcePilot")));
	Scope.GameInstance->Logout();
	TestFalse(TEXT("precondition: logged out before UI login"), Scope.GameInstance->IsLoggedIn());

	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("player spawned"), Player)) return false;
	// OpenLobbyScreen gates on IsLocallyControlled, so mark the controller as local
	// (same pattern as the Settings Exit/Resortie button tests).
	APlayerController* PC = Scope.World->SpawnActor<APlayerController>();
	PC->Possess(Player);
	PC->SetAsLocalPlayerController();
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSLoginFlowTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	Lobby->Close();
	Player->SetLobbyWidgetForTesting(Lobby.Get());

	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(Scope.World);
	Login->SetAuthSubsystemForTesting(Auth.Auth.Get());
	Login->SetLobbyPlayerForTesting(Player);
	Login->Open();

	MSLoginFlowTests::EnterUsername(Login.Get(), TEXT("AcePilot"));
	Login->GetLoginButtonForTesting()->OnClicked.Broadcast();

	TestTrue(TEXT("login marks the GameInstance as authenticated"), Scope.GameInstance->IsLoggedIn());
	TestEqual(TEXT("the canonical username is stored"), Scope.GameInstance->LoggedInUsername, FString(TEXT("AcePilot")));
	TestFalse(TEXT("the login screen closes on success"), Login->IsOpen());
	TestTrue(TEXT("the lobby opens after a successful login"), Lobby->IsOpen());
	TestTrue(TEXT("no error message remains"), Login->GetErrorTextForTesting().IsEmpty());
	return true;
}

bool FMSLoginFlowLoginFailureTest::RunTest(const FString&)
{
	MSLoginFlowTests::FScopedWorld Scope;
	MSLoginFlowTests::FScopedAuth Auth(Scope.GameInstance);
	Scope.GameInstance->Logout();

	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(Scope.World);
	Login->SetAuthSubsystemForTesting(Auth.Auth.Get());
	Login->Open();

	// パスワードレス仕様で失敗しうる入力は空名のみ。認証サブシステムに届く前に弾く。
	MSLoginFlowTests::EnterUsername(Login.Get(), TEXT("   "));
	Login->GetLoginButtonForTesting()->OnClicked.Broadcast();
	TestFalse(TEXT("an empty name does not log in"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("the login screen stays open"), Login->IsOpen());
	TestFalse(TEXT("an empty name shows an error"), Login->GetErrorTextForTesting().IsEmpty());
	return true;
}

bool FMSLoginFlowRegistrationTest::RunTest(const FString&)
{
	MSLoginFlowTests::FScopedWorld Scope;
	MSLoginFlowTests::FScopedAuth Auth(Scope.GameInstance);

	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("player spawned"), Player)) return false;
	APlayerController* PC = Scope.World->SpawnActor<APlayerController>();
	PC->Possess(Player);
	PC->SetAsLocalPlayerController();
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSLoginFlowTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	Lobby->Close();
	Player->SetLobbyWidgetForTesting(Lobby.Get());

	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(Scope.World);
	Login->SetAuthSubsystemForTesting(Auth.Auth.Get());
	Login->SetLobbyPlayerForTesting(Player);
	Login->Open();

	// 初めての名前はログインボタンだけで自動登録され、そのままロビーへ入る。
	MSLoginFlowTests::EnterUsername(Login.Get(), TEXT("NewPilot"));
	Login->GetLoginButtonForTesting()->OnClicked.Broadcast();
	TestTrue(TEXT("a first-time name auto-registers and logs in"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("auto-registration opens the lobby"), Lobby->IsOpen());

	// 同じ名前での再ログインは「既に使われています」にならず、そのまま入れる。
	Scope.GameInstance->Logout();
	Lobby->Close();
	Login->Open();
	MSLoginFlowTests::EnterUsername(Login.Get(), TEXT("newpilot"));
	Login->GetLoginButtonForTesting()->OnClicked.Broadcast();
	TestTrue(TEXT("an existing name logs back in without a duplicate error"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("no error message is shown for an existing name"), Login->GetErrorTextForTesting().IsEmpty());
	TestTrue(TEXT("logging back in reopens the lobby"), Lobby->IsOpen());

	// ゲストボタンは名前入力すら不要でロビーへ入る。
	Scope.GameInstance->Logout();
	Lobby->Close();
	Login->Open();
	MSLoginFlowTests::EnterUsername(Login.Get(), TEXT(""));
	Login->GetGuestButtonForTesting()->OnClicked.Broadcast();
	TestTrue(TEXT("guest entry logs in without any setup"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("the guest username follows the ゲストN format"),
		Scope.GameInstance->LoggedInUsername.StartsWith(TEXT("ゲスト")));
	TestTrue(TEXT("guest entry opens the lobby"), Lobby->IsOpen());
	return true;
}

bool FMSLoginFlowBackToTitleTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSTitleWidget> Title = MSLoginFlowTests::MakeWidget<UMSTitleWidget>(GetTransientPackage());
	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(GetTransientPackage());
	Login->SetTitleWidgetForTesting(Title.Get());
	Login->Open();

	UButton* Back = Login->GetBackButtonForTesting();
	if (!TestNotNull(TEXT("the login screen owns a back-to-title button"), Back)) return false;
	Back->OnClicked.Broadcast();
	TestFalse(TEXT("back closes the login screen"), Login->IsOpen());
	TestTrue(TEXT("back reopens the title screen"), Title->IsOpen());
	return true;
}

bool FMSLoginFlowSettingsLogoutTest::RunTest(const FString&)
{
	MSLoginFlowTests::FScopedWorld Scope;
	Scope.GameInstance->SetAuthenticatedUser(TEXT("AcePilot"));

	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("player spawned"), Player)) return false;
	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSLoginFlowTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	Lobby->Open();
	Player->SetLobbyWidgetForTesting(Lobby.Get());
	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(Scope.World);
	Login->Close();

	UMSUILockSubsystem* UILock = NewObject<UMSUILockSubsystem>(Scope.GameInstance);
	TStrongObjectPtr<UMSSettingsWidget> Settings(NewObject<UMSSettingsWidget>(Scope.World));
	Settings->Initialize();
	Settings->InitializeForAutomationTests();
	Settings->SetUILockSubsystemForTesting(UILock);
	Settings->SetGameInstanceForTesting(Scope.GameInstance);
	Settings->SetResortiePlayerForTesting(Player);
	Settings->SetLoginWidgetForTesting(Login.Get());
	Settings->Open();

	UButton* Logout = Settings->GetLogoutButtonForTesting();
	if (!TestNotNull(TEXT("Settings owns a logout button"), Logout)) return false;
	TestTrue(TEXT("precondition: logged in"), Scope.GameInstance->IsLoggedIn());
	Logout->OnClicked.Broadcast();

	TestFalse(TEXT("logout closes Settings"), Settings->IsOpen());
	TestFalse(TEXT("logout closes the lobby"), Lobby->IsOpen());
	TestFalse(TEXT("logout clears the GameInstance login state"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("logout reopens the login screen"), Login->IsOpen());
	return true;
}

bool FMSLoginFlowLobbyLogoutTest::RunTest(const FString&)
{
	MSLoginFlowTests::FScopedWorld Scope;
	Scope.GameInstance->SetAuthenticatedUser(TEXT("AcePilot"));

	TStrongObjectPtr<UMSLobbyWidget> Lobby = MSLoginFlowTests::MakeWidget<UMSLobbyWidget>(Scope.World);
	TStrongObjectPtr<UMSLoginWidget> Login = MSLoginFlowTests::MakeWidget<UMSLoginWidget>(Scope.World);
	Login->Close();
	Lobby->SetLoginWidgetForTesting(Login.Get());
	Lobby->Open();

	UButton* Exit = Lobby->GetLogoutButtonForTesting();
	if (!TestNotNull(TEXT("the lobby global rail owns an EXIT button"), Exit)) return false;
	TestTrue(TEXT("the EXIT button is enabled"), Exit->GetIsEnabled());
	Exit->OnClicked.Broadcast();

	TestFalse(TEXT("EXIT closes the lobby"), Lobby->IsOpen());
	TestFalse(TEXT("EXIT clears the GameInstance login state"), Scope.GameInstance->IsLoggedIn());
	TestTrue(TEXT("EXIT reopens the login screen"), Login->IsOpen());
	return true;
}

#endif
