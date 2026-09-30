#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSPlayerController.h"
#include "UI/MSSettingsWidget.h"
#include "UI/MSUILockSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSPlayerControllerPawnlessEscapeTest,
	"NeoMSGO.Gameplay.PlayerController.EscapeWithoutPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSPlayerControllerEscapeTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSPlayerControllerEscapeTestWorld"));

		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSPlayerControllerPawnlessEscapeTest::RunTest(const FString&)
{
	MSPlayerControllerEscapeTests::FScopedWorld Scope;
	AMSPlayerController* Controller = Scope.World->SpawnActor<AMSPlayerController>();
	if (!TestNotNull(TEXT("MS player controller spawned"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	TestNull(TEXT("precondition: no Pawn is possessed"), Controller->GetPawn());

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSUILockSubsystem> UILock(NewObject<UMSUILockSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UMSSettingsWidget> Settings(NewObject<UMSSettingsWidget>());
	Settings->Initialize();
	Settings->InitializeForAutomationTests();
	Settings->SetUILockSubsystemForTesting(UILock.Get());
	Controller->SetUILockSubsystemForTesting(UILock.Get());
	Controller->SetSettingsWidgetForTesting(Settings.Get());

	Controller->HandleEscapePressed();
	TestTrue(TEXT("Esc opens Settings before Pawn spawn"), Settings->IsOpen());

	bool bOverlayClosed = false;
	UILock->PushScreen(TEXT("FocusedOverlay"),
		FMSUICloseRequestDelegate::CreateLambda([&bOverlayClosed]()
		{
			bOverlayClosed = true;
			return true;
		}));
	Controller->HandleEscapePressed();
	TestTrue(TEXT("Esc closes the top focused screen first"), bOverlayClosed);
	TestTrue(TEXT("Settings remains open below the focused screen"), Settings->IsOpen());

	Controller->HandleEscapePressed();
	TestFalse(TEXT("the next Esc closes Settings"), Settings->IsOpen());
	Controller->HandleEscapePressed();
	TestTrue(TEXT("a further Esc reopens Settings"), Settings->IsOpen());
	return true;
}

#endif
