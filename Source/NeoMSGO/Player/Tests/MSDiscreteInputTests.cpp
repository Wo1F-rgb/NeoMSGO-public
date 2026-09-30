// Copyright NeoMSGO Project. All Rights Reserved.
#include "Player/Tests/MSDiscreteInputTests.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSPlayerState.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSUILockSubsystem.h"
#include "Weapons/MSWeaponComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDiscreteWeaponSplitFrameTest,
 "NeoMSGO.Player.DiscreteInput.Weapon2SplitFrames",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDiscreteWeaponSameFrameTest,
 "NeoMSGO.Player.DiscreteInput.Weapon2SameFramePulse",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSDiscreteInputTests
{
 struct FFixture
 {
  FString PreviousCommand = FCommandLine::Get();
  UGameInstance* Instance = nullptr;
  UWorld* World = nullptr;
  AMSDiscreteInputTestController* PC = nullptr;
  AMSPlayerCharacter* Pawn = nullptr;
  UEnhancedPlayerInput* Input = nullptr;
  UInputAction* Action = nullptr;
  UMSWeaponComponent* Weapons = nullptr;

  FFixture()
  {
   // Isolate Enhanced Input evaluation from HTTP/RPC and frontend login. This is
   // the existing debug frontend opt-out, not an alternative weapon input path.
   // Preserve engine flags such as -nullrhi: FApp caches rendering capability.
   // FParse::Value uses the first occurrence, so Prepare rejects a conflicting
   // pre-existing auth mode instead of assuming this appended value overrides it.
   FCommandLine::Append(TEXT(" -MSAuthMode=OfflineLocal -MSDebugUser=DiscreteInputFixture"));
   Instance = NewObject<UGameInstance>(GEngine); Instance->AddToRoot(); Instance->InitializeStandalone();
   World = Instance->GetWorld();
   World->InitializeActorsForPlay(FURL()); World->BeginPlay();
   if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
   ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
   Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
   PC = World->SpawnActor<AMSDiscreteInputTestController>();
   PC->PlayerState = World->SpawnActor<AMSPlayerState>(); PC->SetPlayer(LocalPlayer);
   if (!PC->PlayerInput) PC->InitInputSystem(); // Uses the real DefaultInput.ini class.
   Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
   Pawn = World->SpawnActor<AMSPlayerCharacter>();
   PC->Possess(Pawn);
   Pawn->PawnClientRestart(); // Creates/binds the real Pawn input component and IMC.
   Weapons = Pawn->GetWeaponComponent();
   auto* First = NewObject<UMSRangedWeaponData>(Pawn);
   auto* Second = NewObject<UMSRangedWeaponData>(Pawn);
   First->MagazineSize = 5; Second->MagazineSize = 7;
   Weapons->InitializeSlots({First, Second});
   Action = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/MS/IA_Weapon2.IA_Weapon2"));
  }
  ~FFixture()
  {
   if (World && World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
   Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
   Instance->RemoveFromRoot(); FCommandLine::Set(*PreviousCommand);
  }
  bool Prepare(FAutomationTestBase& Test)
  {
   FString EffectiveAuthMode;
   FParse::Value(FCommandLine::Get(), TEXT("MSAuthMode="), EffectiveAuthMode);
   if (!Test.TestTrue(TEXT("fixture's effective auth mode is OfflineLocal (first command-line value wins)"),
       EffectiveAuthMode.Equals(TEXT("OfflineLocal"), ESearchCase::IgnoreCase))) return false;
   if (!Test.TestNotNull(TEXT("configured PlayerInput is UEnhancedPlayerInput"), Input) ||
       !Test.TestNotNull(TEXT("real IA_Weapon2 asset exists"), Action)) return false;
   auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
   auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
   auto* Component = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
   if (!Test.TestNotNull(TEXT("real IMC_MSPlayer asset exists"), Context) ||
       !Test.TestNotNull(TEXT("real local Enhanced Input subsystem exists"), Subsystem) ||
       !Test.TestNotNull(TEXT("PawnClientRestart created EnhancedInputComponent"), Component)) return false;
   Test.TestTrue(TEXT("real Pawn lifecycle registered IMC_MSPlayer"), Subsystem->HasMappingContext(Context));
   Test.TestTrue(TEXT("real asset maps keyboard Two to IA_Weapon2"), Context->GetMappings().ContainsByPredicate(
    [this](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action && Mapping.Key == EKeys::Two; }));
   Test.TestTrue(TEXT("actual engine input stack contains Pawn bindings"), PC->HasPawnInputInEngineStack(Component));
   const bool bRealBinding = Component->GetActionEventBindings().ContainsByPredicate([this](const auto& Binding)
   { return Binding->GetAction() == Action && Binding->GetTriggerEvent() == ETriggerEvent::Started && Binding->GetUObject() == Pawn; });
   if (!Test.TestTrue(TEXT("real Pawn binds IA_Weapon2 Started to its own handler"), bRealBinding)) return false;
   // This second listener only observes dispatch. It never invokes the Pawn handler
   // or the weapon component and does not substitute for the real binding above.
   Component->BindAction(Action, ETriggerEvent::Started, PC, &AMSDiscreteInputTestController::ObserveWeapon2Started);
   FModifyContextOptions Options; Options.bForceImmediately = true;
   Subsystem->RequestRebuildControlMappings(Options, EInputMappingRebuildType::RebuildWithFlush);
   // This public query reads the rebuilt PlayerInput mappings, not the source IMC.
   Test.TestTrue(TEXT("rebuilt live mappings contain keyboard Two for the exact action asset"),
    Subsystem->QueryKeysMappedToAction(Action).Contains(EKeys::Two));
   // UE5.7 exposes all live keys but only player-mappable trigger records through
   // subsystem APIs. Read the reflected UPROPERTY after verifying its exact array
   // element type, so non-player-mappable actions retain the same live-trigger check
   // without replacing the configured input class or downcasting it to a test class.
   const FArrayProperty* MappingProperty = FindFProperty<FArrayProperty>(Input->GetClass(), TEXT("EnhancedActionMappings"));
   if (!Test.TestNotNull(TEXT("live mappings remain a reflected array"), MappingProperty)) return false;
   const FStructProperty* MappingElement = CastField<FStructProperty>(MappingProperty->Inner);
   if (!Test.TestTrue(TEXT("live mapping array has the exact FEnhancedActionKeyMapping element type"),
       MappingElement && MappingElement->Struct == FEnhancedActionKeyMapping::StaticStruct())) return false;
   const TArray<FEnhancedActionKeyMapping>& LiveMappings =
    *MappingProperty->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(Input);
   int32 VerifiedLiveMappings = 0;
   for (const FEnhancedActionKeyMapping& Mapping : LiveMappings)
    if (Mapping.Action == Action && Mapping.Key == EKeys::Two)
    {
     ++VerifiedLiveMappings;
     Test.TestEqual(TEXT("live Two mapping has no explicit trigger overriding keyboard evaluation"), Mapping.Triggers.Num(), 0);
    }
   Test.TestTrue(TEXT("at least one live Two mapping had its triggers inspected"), VerifiedLiveMappings > 0);
   Test.TestEqual(TEXT("Weapon2 is the real Boolean action"), Action->ValueType, EInputActionValueType::Boolean);
   Test.TestEqual(TEXT("real action has no explicit triggers"), Action->Triggers.Num(), 0);
   Test.TestFalse(TEXT("keyboard Two is not registered as ButtonAxis"), EKeys::Two.IsButtonAxis());
   Test.TestTrue(TEXT("real Pawn has gameplay permission"), Pawn->IsGameplayInputAllowed());
   if (auto* Lock = Instance->GetSubsystem<UMSUILockSubsystem>())
    Test.TestTrue(TEXT("no frontend screen locks the fixture"), Lock->IsGameplayActive());
   Test.TestEqual(TEXT("fresh two-slot loadout begins at zero without switching"), Weapons->GetActiveSlotIndex(), 0);
   // One idle evaluation finishes rebuild bookkeeping before the first event.
   Evaluate();
   Test.TestEqual(TEXT("idle evaluation never switches a weapon"), Weapons->GetActiveSlotIndex(), 0);
   Test.TestEqual(TEXT("idle evaluation never dispatches Started"), PC->Weapon2StartedCount, 0);
   return true;
  }
  void Send(EInputEvent Event)
  {
   // Same key event API used after viewport routing. Press/release both carry
   // AmountDepressed=1 like the observed Sky trace; UPlayerInput handles release.
   PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::Two,
    Event, 1.0f, false, FPlatformTime::Cycles64()));
  }
  void Evaluate()
  {
   ++GFrameCounter;
   PC->EvaluateDiscreteInputFrame(1.0f / 60.0f);
  }
  void Report(FAutomationTestBase& Test, const TCHAR* Case)
  {
   const auto* ActionState = Input->FindActionInstanceData(Action);
   const float Value = ActionState ? ActionState->GetValue().GetMagnitude() : 0.0f;
   Test.AddInfo(FString::Printf(TEXT("%s: Started=%d, slot=%d, keyRaw=%.3f, actionValue=%.3f, trigger=%s"),
    Case, PC->Weapon2StartedCount, Weapons->GetActiveSlotIndex(), Input->GetRawKeyValue(EKeys::Two), Value,
    ActionState ? *UEnum::GetValueAsString(ActionState->GetTriggerEvent()) : TEXT("NoInstance")));
  }
 };
}

bool FMSDiscreteWeaponSplitFrameTest::RunTest(const FString&)
{
 MSDiscreteInputTests::FFixture Fixture;
 if (!Fixture.Prepare(*this)) return false;
 Fixture.Send(IE_Pressed); Fixture.Evaluate(); Fixture.Report(*this, TEXT("A pressed/process"));
 TestEqual(TEXT("A: real Started handler dispatches once while the key is held"), Fixture.PC->Weapon2StartedCount, 1);
 TestEqual(TEXT("A: real Pawn handler changes the actual weapon slot to 1"), Fixture.Weapons->GetActiveSlotIndex(), 1);
 Fixture.Send(IE_Released); Fixture.Evaluate(); Fixture.Report(*this, TEXT("A next-frame release/process"));
 TestEqual(TEXT("A: release does not dispatch another Started"), Fixture.PC->Weapon2StartedCount, 1);
 TestEqual(TEXT("A: selected weapon remains slot 1 after release"), Fixture.Weapons->GetActiveSlotIndex(), 1);
 return true;
}

bool FMSDiscreteWeaponSameFrameTest::RunTest(const FString&)
{
 MSDiscreteInputTests::FFixture Fixture;
 if (!Fixture.Prepare(*this)) return false;
 Fixture.Send(IE_Pressed); Fixture.Send(IE_Released); Fixture.Evaluate(); Fixture.Report(*this, TEXT("B same-frame pulse/process"));
 // UE 5.7 characterization, not an acceptance claim for a successful switch:
 // EnhancedPlayerInput caches KeysPressedThisTick only for ButtonAxis keys, and
 // a normal keyboard key's raw accumulator is zeroed by its same-frame release.
 TestEqual(TEXT("B: UE5.7 same-frame keyboard pulse does not dispatch Started"), Fixture.PC->Weapon2StartedCount, 0);
 TestEqual(TEXT("B: actual weapon stays at slot 0 after the lost pulse"), Fixture.Weapons->GetActiveSlotIndex(), 0);
 Fixture.Evaluate();
 TestEqual(TEXT("B: no delayed weapon switch appears on the following evaluation"), Fixture.Weapons->GetActiveSlotIndex(), 0);
 return true;
}
#endif
