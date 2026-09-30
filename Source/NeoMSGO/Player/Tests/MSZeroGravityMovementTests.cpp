// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameState.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSZeroGravityNoFallTest,
	"NeoMSGO.Player.Movement.ZeroGravity.NoGravityFall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSZeroGravityVerticalInputTest,
	"NeoMSGO.Player.Movement.ZeroGravity.VerticalInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSZeroGravityGroundCompatibilityTest,
	"NeoMSGO.Player.Movement.ZeroGravity.GroundMapCompatibility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSZeroGravityBoostParityTest,
	"NeoMSGO.Player.Movement.ZeroGravity.BoostSpeedParity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSZeroGravityMovementTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSZeroGravityMovementTestWorld"));
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

		~FTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnUnit(UWorld* World, const FVector& Location)
	{
		// 18mカプセルが床フィクスチャと重ならない安全高(MSPlayerWeaponInputTestsと同方式)。
		const FVector SafeLocation = Location + FVector(0.0f, 0.0f, 900.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), SafeLocation, FRotator::ZeroRotator, Params);
		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Unit);
		Data->WalkSpeed = 7.5f;
		Data->BoostSpeed = 15.0f;
		Unit->InitializeUnit(Data);
		Unit->SetActorEnableCollision(false);
		Unit->GetMSCharacterMovement()->bRunPhysicsWithNoController = true;
		return Unit;
	}

	void TickMovement(UMSCharacterMovementComponent* Movement, const float DeltaSeconds)
	{
		Movement->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}
}

bool FMSZeroGravityNoFallTest::RunTest(const FString& Parameters)
{
	using namespace MSZeroGravityMovementTests;
	FTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World, FVector(0.0f, 0.0f, 1000.0f));
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	Movement->SetZeroGravityEnabled(true);
	Movement->Velocity = FVector(300.0f, 0.0f, 0.0f);
	const float StartZ = Unit->GetActorLocation().Z;
	TickMovement(Movement, 0.1f);
	TestTrue(TEXT("zero-gravity map uses MOVE_Flying"), Movement->IsZeroGravityFlying());
	TestEqual(TEXT("zero-gravity acceleration is exactly 0 cm/s^2"), Movement->GetGravityZ(), 0.0f);
	TestEqual(TEXT("zero gravity does not change altitude without vertical input"),
		static_cast<float>(Unit->GetActorLocation().Z), StartZ, 0.01f);
	TestTrue(TEXT("low flying braking preserves horizontal inertia after 0.1 seconds"), Movement->Velocity.X > 250.0f);
	return true;
}

bool FMSZeroGravityVerticalInputTest::RunTest(const FString& Parameters)
{
	using namespace MSZeroGravityMovementTests;
	FTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World, FVector(0.0f, 0.0f, 1000.0f));
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	Movement->SetZeroGravityEnabled(true);
	Movement->StopMovementImmediately();
	Movement->SetZeroGravityAscendInputHeld(true);
	TestEqual(TEXT("Space supplies a +1 vertical input"), Movement->GetZeroGravityVerticalInput(), 1.0f);
	const float AscendStartZ = Unit->GetActorLocation().Z;
	TickMovement(Movement, 0.1f);
	TestTrue(TEXT("Space supplies positive flying acceleration"), Movement->GetCurrentAcceleration().Z > 0.0f);
	TestTrue(TEXT("Space produces positive vertical velocity"), Movement->Velocity.Z > 0.0f);
	TestTrue(TEXT("Space increases actual world Z"), Unit->GetActorLocation().Z > AscendStartZ);

	Movement->StopMovementImmediately();
	Movement->SetZeroGravityAscendInputHeld(false);
	Movement->SetZeroGravityDescendInputHeld(true);
	TestEqual(TEXT("Ctrl supplies a -1 vertical input"), Movement->GetZeroGravityVerticalInput(), -1.0f);
	const float DescendStartZ = Unit->GetActorLocation().Z;
	TickMovement(Movement, 0.1f);
	TestTrue(TEXT("Ctrl supplies negative flying acceleration"), Movement->GetCurrentAcceleration().Z < 0.0f);
	TestTrue(TEXT("Ctrl produces negative vertical velocity"), Movement->Velocity.Z < 0.0f);
	TestTrue(TEXT("Ctrl decreases actual world Z"), Unit->GetActorLocation().Z < DescendStartZ);
	return true;
}

bool FMSZeroGravityGroundCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace MSZeroGravityMovementTests;
	FTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World, FVector(0.0f, 0.0f, 1000.0f));
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();

	TestFalse(TEXT("AMSGameState defaults every map to ground movement"), GetDefault<AMSGameState>()->IsZeroGravityMap());
	TestFalse(TEXT("a unit spawned without the map flag remains a ground unit"), Movement->IsZeroGravityEnabled());
	Movement->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("ground maps retain MOVE_Falling"), Movement->IsFlying());
	// 体感優先: 18m世界では重力もMSScale倍(機体比-20 m/s^2)。標準版へ一度戻したが
	// 「重力が小さくなった」と再指摘(2026-07-19)があり、MSScale版へ再度戻した。
	TestEqual(TEXT("ground maps retain body-relative -20 m/s^2 suit gravity"),
		Movement->GetGravityZ(), -2000.0f * MSGO::MSScale, 0.5f);
	return true;
}

bool FMSZeroGravityBoostParityTest::RunTest(const FString& Parameters)
{
	using namespace MSZeroGravityMovementTests;
	FTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World, FVector(0.0f, 0.0f, 1000.0f));
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->SetBoostInputHeld(true);
	const float GroundBoostSpeed = Movement->GetMaxSpeed();
	Movement->SetZeroGravityEnabled(true);
	const float SpaceBoostSpeed = Movement->GetMaxSpeed();

	TestEqual(TEXT("ground boost uses configured speed times MSScale"), GroundBoostSpeed,
		1500.0f * MSGO::MSScale, 0.01f);
	TestEqual(TEXT("space boost uses the same scaled speed"), SpaceBoostSpeed,
		1500.0f * MSGO::MSScale, 0.01f);
	TestEqual(TEXT("space and ground boost speed multipliers are identical"), SpaceBoostSpeed, GroundBoostSpeed);
	TestEqual(TEXT("walk speed uses configured m/s times 100 times MSScale"), Movement->MaxWalkSpeed,
		750.0f * MSGO::MSScale, 0.01f);
	TestEqual(TEXT("MaxStepHeight follows the 45cm baseline times MSScale"), Movement->MaxStepHeight,
		45.0f * MSGO::MSScale, 0.01f);
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	for (const FGameplayTag& ReactionTag : {MSTags::State_Flinched.GetTag(), MSTags::State_GettingUp.GetTag()})
	{
		ASC->SetLooseGameplayTagCount(ReactionTag, 1);
		TestEqual(TEXT("hit reaction prevents input speed in zero-gravity flight"), Movement->GetMaxSpeed(), 0.0f);
		TestFalse(TEXT("hit reaction prevents boost in zero-gravity flight"), Movement->CanUseBoost());
		ASC->SetLooseGameplayTagCount(ReactionTag, 0);
		TestEqual(TEXT("flight speed resumes when the reaction clears"), Movement->GetMaxSpeed(), SpaceBoostSpeed);
	}
	Movement->SetZeroGravityEnabled(false);
	Movement->SetMovementMode(MOVE_Flying);
	ASC->SetLooseGameplayTagCount(MSTags::State_Flinched, 1);
	TestEqual(TEXT("hit reaction also gates the regular flying fallback"), Movement->GetMaxSpeed(), 0.0f);
	ASC->SetLooseGameplayTagCount(MSTags::State_Flinched, 0);
	TestTrue(TEXT("regular flying speed resumes after hit reaction"), Movement->GetMaxSpeed() > 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
