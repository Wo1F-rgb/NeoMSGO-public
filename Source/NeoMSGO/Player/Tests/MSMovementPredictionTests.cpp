// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Data/MSMobileSuitData.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMovementPredictionFlagsTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.InputFlags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMovementPredictionTestWorld
	{
		UWorld* World = nullptr;

		FMovementPredictionTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMovementPredictionTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FMovementPredictionTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnUnit(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector(0.0f, 0.0f, 1000.0f),
			FRotator::ZeroRotator, Params);
		Unit->GetMSCharacterMovement()->bRunPhysicsWithNoController = true;
		Unit->GetMSCharacterMovement()->SetMovementMode(MOVE_Falling);

		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Unit);
		Data->BoostCapacity = 100.0f;
		Data->BoostConsumptionRate = 20.0f;
		Data->BoostRechargeRate = 15.0f;
		Unit->InitializeUnit(Data);
		return Unit;
	}
}

bool FMSMovementPredictionFlagsTest::RunTest(const FString& Parameters)
{
	FMovementPredictionTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	FNetworkPredictionData_Client_Character* ClientData =
		static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	TestNotNull(TEXT("custom client prediction data is allocated"), ClientData);
	if (!ClientData)
	{
		return false;
	}

	ClientData->CurrentTimeStamp = 1.0f;
	const auto MakeMove = [&]() -> FSavedMovePtr
	{
		FSavedMovePtr Move = ClientData->CreateSavedMove();
		Move->SetMoveFor(Unit, 0.016f, FVector::ForwardVector, *ClientData);
		Move->PostUpdate(Unit, FSavedMove_Character::PostUpdate_Record);
		ClientData->CurrentTimeStamp += 0.016f;
		return Move;
	};

	// All custom states are captured into the same byte that stock ServerMove
	// serializes.  Ascend and descend are intentionally mutually exclusive here.
	Movement->SetBoostInputHeld(true);
	Movement->SetHoverInputHeld(true);
	Movement->SetZeroGravityAscendInputHeld(true);
	Movement->SetZeroGravityDescendInputHeld(false);
	FSavedMovePtr HeldMove = MakeMove();
	const uint8 HeldFlags = HeldMove->GetCompressedFlags();
	TestTrue(TEXT("boost held uses Custom_0"),
		(HeldFlags & FSavedMove_Character::FLAG_Custom_0) != 0);
	TestTrue(TEXT("hover held uses Custom_1"),
		(HeldFlags & FSavedMove_Character::FLAG_Custom_1) != 0);
	TestTrue(TEXT("zero-G ascend held uses Custom_2"),
		(HeldFlags & FSavedMove_Character::FLAG_Custom_2) != 0);
	TestTrue(TEXT("zero-G descend clear leaves Custom_3 clear"),
		(HeldFlags & FSavedMove_Character::FLAG_Custom_3) == 0);

	// PrepMoveFor must restore the intent before a correction replay reaches
	// MoveAutonomous.  The explicit decode below verifies the server path too.
	Movement->SetBoostInputHeld(false);
	Movement->SetHoverInputHeld(false);
	Movement->SetZeroGravityAscendInputHeld(false);
	Movement->SetZeroGravityDescendInputHeld(true);
	HeldMove->PrepMoveFor(Unit);
	TestTrue(TEXT("replay restores boost held"), Movement->IsBoostDashing());
	TestTrue(TEXT("replay restores hover held while falling"), Movement->IsHoverAscending());
	TestEqual(TEXT("replay restores zero-G ascend intent"), Movement->GetZeroGravityVerticalInput(), 1.0f);

	Movement->UpdateFromCompressedFlags(HeldFlags);
	TestTrue(TEXT("compressed flags restore boost on authority/replay"), Movement->IsBoostDashing());
	TestTrue(TEXT("compressed flags restore hover on authority/replay"), Movement->IsHoverAscending());
	TestEqual(TEXT("compressed flags restore zero-G ascend"), Movement->GetZeroGravityVerticalInput(), 1.0f);

	// A release edge must be retained as a separate saved move.  Otherwise a
	// combined packet can leave the server consuming energy after key release.
	Movement->SetBoostInputHeld(false);
	Movement->SetHoverInputHeld(false);
	Movement->SetZeroGravityAscendInputHeld(false);
	Movement->SetZeroGravityDescendInputHeld(false);
	FSavedMovePtr ReleasedMove = MakeMove();
	const uint8 ReleasedFlags = ReleasedMove->GetCompressedFlags();
	const uint8 CustomMask =
		FSavedMove_Character::FLAG_Custom_0 |
		FSavedMove_Character::FLAG_Custom_1 |
		FSavedMove_Character::FLAG_Custom_2 |
		FSavedMove_Character::FLAG_Custom_3;
	TestEqual(TEXT("release clears all custom input flags"), ReleasedFlags & CustomMask, uint8(0));
	TestFalse(TEXT("held and released moves cannot combine"),
		HeldMove->CanCombineWith(ReleasedMove, Unit, 0.125f));

	// The same held state remains combinable when the stock movement rules permit
	// it, proving the custom boundary is state-based rather than always forcing
	// one packet per frame.
	Movement->SetBoostInputHeld(true);
	Movement->SetHoverInputHeld(true);
	Movement->SetZeroGravityAscendInputHeld(true);
	Movement->SetZeroGravityDescendInputHeld(false);
	FSavedMovePtr SameHeldMove = MakeMove();
	TestTrue(TEXT("unchanged held states can combine"),
		HeldMove->CanCombineWith(SameHeldMove, Unit, 0.125f));

	Movement->UpdateFromCompressedFlags(ReleasedFlags);
	TestFalse(TEXT("server decode clears boost after release"), Movement->IsBoostDashing());
	TestFalse(TEXT("server decode clears hover after release"), Movement->IsHoverAscending());
	TestEqual(TEXT("server decode clears zero-G vertical intent"), Movement->GetZeroGravityVerticalInput(), 0.0f);

	// Decoding an ascend press while grounded must retain the setter's zero-G
	// movement-mode transition; this is the path that previously diverged on the
	// authority even when the input byte itself arrived intact.
	Movement->SetZeroGravityEnabled(true);
	Movement->SetMovementMode(MOVE_Walking);
	Movement->UpdateFromCompressedFlags(HeldFlags);
	TestTrue(TEXT("zero-G ascend decode enters Flying from ground"), Movement->IsFlying());
	Movement->SetZeroGravityEnabled(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
