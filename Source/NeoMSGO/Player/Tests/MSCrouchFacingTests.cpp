// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "Gameplay/MSPlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/Tests/MSDiscreteInputTests.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCrouchInputAndPoseTest,
	"NeoMSGO.Player.CrouchFacing.MPU01InputAndPose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCrouchFacingRotationTest,
	"NeoMSGO.Player.CrouchFacing.ControlYawFreezeAndConvergence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCrouchRecallOwnershipInputTest,
	"NeoMSGO.Player.CrouchFacing.RecallTagOwnershipThroughInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	constexpr const TCHAR* CrouchFacingMPU01DataPath =
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS");
	constexpr const TCHAR* CrouchFacingActionPath =
		TEXT("/Game/Input/Actions/MS/IA_Crouch.IA_Crouch");
	constexpr const TCHAR* PlayerContextPath =
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer");

	/**
	 * Uses the same standalone game instance and real Enhanced Input path as the
	 * discrete input tests. The fixture deliberately fails when the playable
	 * MPU01 data/mesh is unavailable; this is an asset acceptance test.
	 */
	struct FMSCrouchFacingFixture
	{
		FString PreviousCommand = FCommandLine::Get();
		UGameInstance* Instance = nullptr;
		UWorld* World = nullptr;
		AMSDiscreteInputTestController* PC = nullptr;
		AMSPlayerCharacter* Pawn = nullptr;
		UEnhancedPlayerInput* Input = nullptr;
		UInputAction* CrouchAction = nullptr;
		UInputMappingContext* PlayerContext = nullptr;
		UMSMobileSuitData* MPU01Data = nullptr;
		UMSMechAnimatorComponent* Animator = nullptr;
		UMSMeshVisualComponent* Visual = nullptr;
		UMSMechRigComponent* Rig = nullptr;
		UPoseableMeshComponent* Poseable = nullptr;

		FMSCrouchFacingFixture()
		{
			FCommandLine::Append(TEXT(" -MSAuthMode=OfflineLocal -MSDebugUser=MSCrouchFacingFixture"));
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->AddToRoot();
			Instance->InitializeStandalone();
			World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}

			ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
			PC = World->SpawnActor<AMSDiscreteInputTestController>();
			PC->PlayerState = World->SpawnActor<AMSPlayerState>();
			PC->SetPlayer(LocalPlayer);
			if (!PC->PlayerInput)
			{
				PC->InitInputSystem();
			}
			Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput);

			Pawn = World->SpawnActor<AMSPlayerCharacter>();
			PC->Possess(Pawn);
			Pawn->PawnClientRestart();
		}

		~FMSCrouchFacingFixture()
		{
			if (World && World->GetBegunPlay())
			{
				World->EndPlay(EEndPlayReason::Quit);
			}
			if (Instance)
			{
				Instance->Shutdown();
			}
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
			if (Instance)
			{
				Instance->RemoveFromRoot();
			}
			FCommandLine::Set(*PreviousCommand);
		}

		bool Prepare(FAutomationTestBase& Test)
		{
			FString EffectiveAuthMode;
			FParse::Value(FCommandLine::Get(), TEXT("MSAuthMode="), EffectiveAuthMode);
			if (!Test.TestTrue(TEXT("fixture's effective auth mode is OfflineLocal"),
				EffectiveAuthMode.Equals(TEXT("OfflineLocal"), ESearchCase::IgnoreCase)))
			{
				return false;
			}
			MPU01Data = LoadObject<UMSMobileSuitData>(nullptr, CrouchFacingMPU01DataPath);
			CrouchAction = LoadObject<UInputAction>(nullptr, CrouchFacingActionPath);
			PlayerContext = LoadObject<UInputMappingContext>(nullptr, PlayerContextPath);
			if (!Test.TestNotNull(TEXT("configured PlayerInput is UEnhancedPlayerInput"), Input)
				|| !Test.TestNotNull(TEXT("MPU01 playable data asset exists"), MPU01Data)
				|| !Test.TestNotNull(TEXT("real IA_Crouch asset exists"), CrouchAction)
				|| !Test.TestNotNull(TEXT("real IMC_MSPlayer asset exists"), PlayerContext))
			{
				return false;
			}

			const bool bMappedToLeftControl = PlayerContext->GetMappings().ContainsByPredicate(
				[this](const FEnhancedActionKeyMapping& Mapping)
				{
					return Mapping.Action == CrouchAction && Mapping.Key == EKeys::LeftControl;
				});
			if (!Test.TestTrue(TEXT("IA_Crouch is mapped to LeftControl"), bMappedToLeftControl))
			{
				return false;
			}

			UEnhancedInputLocalPlayerSubsystem* Subsystem =
				PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			UEnhancedInputComponent* Component = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
			if (!Test.TestNotNull(TEXT("real local Enhanced Input subsystem exists"), Subsystem)
				|| !Test.TestNotNull(TEXT("PawnClientRestart created EnhancedInputComponent"), Component))
			{
				return false;
			}
			FModifyContextOptions Options;
			Options.bForceImmediately = true;
			Subsystem->RequestRebuildControlMappings(Options, EInputMappingRebuildType::RebuildWithFlush);
			Test.TestTrue(TEXT("real Pawn lifecycle registered IMC_MSPlayer"),
				Subsystem->HasMappingContext(PlayerContext));
			Test.TestTrue(TEXT("rebuilt live mappings contain LeftControl for IA_Crouch"),
				Subsystem->QueryKeysMappedToAction(CrouchAction).Contains(EKeys::LeftControl));
			Test.TestTrue(TEXT("actual engine input stack contains Pawn bindings"),
				PC->HasPawnInputInEngineStack(Component));
			const bool bRealBinding = Component->GetActionEventBindings().ContainsByPredicate(
				[this](const auto& Binding)
				{
					return Binding->GetAction() == CrouchAction
						&& Binding->GetTriggerEvent() == ETriggerEvent::Started
						&& Binding->GetUObject() == Pawn;
				});
			if (!Test.TestTrue(TEXT("real Pawn binds IA_Crouch Started to its own handler"), bRealBinding))
			{
				return false;
			}

			// Initialize the actual imported MPU01 visual and its shared GAS state.
			Pawn->InitializeUnit(MPU01Data);
			Animator = Pawn->GetMechAnimatorComponent();
			Visual = Pawn->GetMeshVisualComponent();
			Rig = Pawn->GetMechRigComponent();
			Poseable = Visual ? Visual->GetPoseableMesh() : nullptr;
			if (!Test.TestNotNull(TEXT("MPU01 animator exists"), Animator)
				|| !Test.TestNotNull(TEXT("MPU01 mesh visual exists"), Visual)
				|| !Test.TestTrue(TEXT("MPU01 mesh visual is active"), Visual && Visual->IsMeshVisualActive())
				|| !Test.TestNotNull(TEXT("MPU01 poseable mesh exists"), Poseable)
				|| !Test.TestNotNull(TEXT("MPU01 runtime rig exists"), Rig)
				|| !Test.TestNotNull(TEXT("MPU01 left shin joint exists"), Rig ? Rig->GetJoint(TEXT("Shin_L")) : nullptr))
			{
				return false;
			}

			EvaluateInput();
			TickWorld(30);
			return Test.TestFalse(TEXT("fresh MPU01 has no State.Crouching tag"),
				Pawn->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Crouching));
		}

		void Send(const EInputEvent Event)
		{
			PC->InputKey(FInputKeyEventArgs(nullptr,
				FInputDeviceId::CreateFromInternalId(0), EKeys::LeftControl,
				Event, 1.0f, false, FPlatformTime::Cycles64()));
		}

		void EvaluateInput()
		{
			++GFrameCounter;
			PC->EvaluateDiscreteInputFrame(1.0f / 60.0f);
		}

		void TickWorld(const int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				World->Tick(LEVELTICK_All, 1.0f / 60.0f);
				++GFrameCounter;
			}
		}
	};
}

bool FMSCrouchInputAndPoseTest::RunTest(const FString&)
{
	FMSCrouchFacingFixture Fixture;
	if (!Fixture.Prepare(*this))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = Fixture.Pawn->GetAbilitySystemComponent();
	const FRotator StandingShin = Fixture.Poseable->GetBoneRotationByName(
		TEXT("shin_l"), EBoneSpaces::ComponentSpace);
	const FRotator StandingThigh = Fixture.Poseable->GetBoneRotationByName(
		TEXT("thigh_l"), EBoneSpaces::ComponentSpace);
	const float StandingRootZ = Fixture.Rig->GetRigRoot()->GetRelativeLocation().Z;

	// A real frame-separated key press must reach the production IA_Crouch handler.
	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	TestTrue(TEXT("LeftControl press enables State.Crouching on MPU01"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Fixture.TickWorld(90);

	const FRotator CrouchedShin = Fixture.Poseable->GetBoneRotationByName(
		TEXT("shin_l"), EBoneSpaces::ComponentSpace);
	const FRotator CrouchedThigh = Fixture.Poseable->GetBoneRotationByName(
		TEXT("thigh_l"), EBoneSpaces::ComponentSpace);
	const float CrouchedRootZ = Fixture.Rig->GetRigRoot()->GetRelativeLocation().Z;
	TestTrue(TEXT("crouching changes the imported MPU01 shin bone"),
		!StandingShin.Equals(CrouchedShin, 0.5f));
	TestTrue(TEXT("crouching changes the imported MPU01 thigh bone"),
		!StandingThigh.Equals(CrouchedThigh, 0.5f));
	TestTrue(TEXT("Animator lowers the MPU01 rig root while crouching"),
		CrouchedRootZ < StandingRootZ - 5.0f);
	TestTrue(TEXT("Animator reaches the crouch pose through the real tag"),
		Fixture.Animator->GetPoseState().CrouchPoseAlpha > 0.99f);

	// Release is a separate frame for the Boolean action; crouch itself is a
	// toggle, so the manual count remains until the next Ctrl press.
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	TestTrue(TEXT("LeftControl release frame keeps the manual crouch toggle"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	TestFalse(TEXT("the next LeftControl press clears the manual crouch toggle"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	Fixture.TickWorld(90);

	const FRotator StandingAgainShin = Fixture.Poseable->GetBoneRotationByName(
		TEXT("shin_l"), EBoneSpaces::ComponentSpace);
	const FRotator StandingAgainThigh = Fixture.Poseable->GetBoneRotationByName(
		TEXT("thigh_l"), EBoneSpaces::ComponentSpace);
	const float StandingAgainRootZ = Fixture.Rig->GetRigRoot()->GetRelativeLocation().Z;
	TestTrue(TEXT("standing returns the imported MPU01 shin bone"),
		StandingShin.Equals(StandingAgainShin, 1.0f));
	TestTrue(TEXT("standing returns the imported MPU01 thigh bone"),
		StandingThigh.Equals(StandingAgainThigh, 1.0f));
	TestTrue(TEXT("standing returns the MPU01 rig root"),
		FMath::IsNearlyEqual(StandingRootZ, StandingAgainRootZ, 1.0f));
	TestTrue(TEXT("Animator returns to the standing pose through the real tag"),
		Fixture.Animator->GetPoseState().CrouchPoseAlpha < 0.01f);
	return true;
}

bool FMSCrouchRecallOwnershipInputTest::RunTest(const FString&)
{
	FMSCrouchFacingFixture Fixture;
	if (!Fixture.Prepare(*this))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = Fixture.Pawn->GetAbilitySystemComponent();
	UMSEmergencyRecallComponent* Recall = Fixture.Pawn->GetEmergencyRecallComponent();
	if (!TestNotNull(TEXT("real emergency recall component exists"), Recall))
	{
		return false;
	}
	Fixture.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// Establish one manual crouch owner through the real Ctrl mapping, with the
	// press and release processed on separate frames.
	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	TestEqual(TEXT("Ctrl owns one crouch tag before recall"),
		ASC->GetTagCount(MSTags::State_Crouching), 1);

	TestTrue(TEXT("grounded MPU01 starts emergency recall"), Recall->TryStartRecall());
	TestEqual(TEXT("recall adds exactly one crouch tag count"),
		ASC->GetTagCount(MSTags::State_Crouching), 2);
	TestEqual(TEXT("recall owns one return tag count"),
		ASC->GetTagCount(MSTags::State_EmergencyRecall), 1);

	// Toggle the manual Ctrl owner off through the real input path. Recall's
	// crouch ownership must remain while the return transition is active.
	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	TestEqual(TEXT("Ctrl clear removes only its own crouch count"),
		ASC->GetTagCount(MSTags::State_Crouching), 1);
	TestTrue(TEXT("recall-owned crouch remains active after Ctrl clear"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	TestTrue(TEXT("recall remains active after Ctrl clear"), Recall->IsRecallActive());

	Recall->CancelRecall();
	TestEqual(TEXT("cancelling recall removes its remaining crouch count"),
		ASC->GetTagCount(MSTags::State_Crouching), 0);
	TestEqual(TEXT("cancelling recall removes the return tag"),
		ASC->GetTagCount(MSTags::State_EmergencyRecall), 0);
	return true;
}

bool FMSCrouchFacingRotationTest::RunTest(const FString&)
{
	FMSCrouchFacingFixture Fixture;
	if (!Fixture.Prepare(*this))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = Fixture.Pawn->GetAbilitySystemComponent();
	Fixture.Pawn->SetActorRotation(FRotator(0.0f, 0.0f, 0.0f));
	const float CrouchYaw = Fixture.Pawn->GetActorRotation().Yaw;

	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	if (!TestTrue(TEXT("facing test enters State.Crouching through LeftControl"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching)))
	{
		return false;
	}

	for (const float ControlYaw : {90.0f, 180.0f, -170.0f})
	{
		Fixture.PC->SetControlRotation(FRotator(0.0f, ControlYaw, 0.0f));
		Fixture.Pawn->FaceRotation(Fixture.PC->GetControlRotation(), 1.0f / 60.0f);
		TestTrue(FString::Printf(TEXT("crouching keeps actor yaw at %.1f while control yaw is %.1f"),
			CrouchYaw, ControlYaw),
			FMath::Abs(FMath::FindDeltaAngleDegrees(CrouchYaw,
				Fixture.Pawn->GetActorRotation().Yaw)) < 0.1f);
	}

	// The shared freeze branch also applies during the return tag. This does not
	// exercise Z input or respawn timing, which is covered by the recall tests.
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	ASC->AddLooseGameplayTag(MSTags::State_EmergencyRecall);
	Fixture.PC->SetControlRotation(FRotator(0.0f, 90.0f, 0.0f));
	Fixture.Pawn->FaceRotation(Fixture.PC->GetControlRotation(), 1.0f / 60.0f);
	TestTrue(TEXT("return tag also keeps actor yaw fixed"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(CrouchYaw,
			Fixture.Pawn->GetActorRotation().Yaw)) < 0.1f);
	ASC->RemoveLooseGameplayTag(MSTags::State_EmergencyRecall);
	Fixture.Send(IE_Pressed);
	Fixture.EvaluateInput();
	Fixture.Send(IE_Released);
	Fixture.EvaluateInput();
	TestFalse(TEXT("facing test leaves crouch before standing convergence"),
		ASC->HasMatchingGameplayTag(MSTags::State_Crouching));

	// Standing turns at a bounded rate and chooses the short route across +/-180.
	Fixture.Pawn->SetActorRotation(FRotator(0.0f, 170.0f, 0.0f));
	Fixture.PC->SetControlRotation(FRotator(0.0f, -170.0f, 0.0f));
	Fixture.Pawn->FaceRotation(Fixture.PC->GetControlRotation(), 0.05f);
	const float FirstStep = FMath::FindDeltaAngleDegrees(170.0f,
		Fixture.Pawn->GetActorRotation().Yaw);
	TestTrue(TEXT("standing starts a bounded shortest-path turn at the +/-180 wrap"),
		FirstStep > 0.0f && FirstStep <= 18.5f);

	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Fixture.Pawn->FaceRotation(Fixture.PC->GetControlRotation(), 1.0f / 60.0f);
	}
	TestTrue(TEXT("standing converges to current control yaw after the staged turn"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(-170.0f,
			Fixture.Pawn->GetActorRotation().Yaw)) < 0.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
