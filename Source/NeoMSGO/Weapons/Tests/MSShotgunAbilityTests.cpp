// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/GA_Fire_Shotgun.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShotgunAbilityTest,
	"NeoMSGO.Weapons.ShotgunAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FShotgunTestWorld
	{
		UWorld* World;
		FShotgunTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSShotgunTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FShotgunTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSUnitBase* SpawnShotgunUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		// Keep the trace's fixed head-height geometry while the real world clock advances.
		Unit->GetCharacterMovement()->DisableMovement();
		return Unit;
	}

	void AdvanceShotgunWorldPastCooldown(UWorld* World, const float Duration)
	{
		const double ReadyTime = static_cast<double>(World->GetTimeSeconds()) + Duration;
		const int32 MaxFrames = FMath::CeilToInt(Duration / 0.01f) + 2;
		for (int32 Frame = 0; Frame < MaxFrames && World->GetTimeSeconds() <= ReadyTime; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.01f);
		}
	}
}

bool FMSShotgunAbilityTest::RunTest(const FString& Parameters)
{
	// Fixed seed proves every generated pellet is independent and remains inside the configured cone.
	FRandomStream Random(0x53474E);
	TArray<FVector> Directions;
	constexpr int32 DirectionCount = 32;
	constexpr float SpreadDegrees = 12.0f;
	for (int32 Index = 0; Index < DirectionCount; ++Index)
	{
		const FVector Direction = UGA_Fire_Shotgun::MakePelletDirection(
			FVector::ForwardVector, SpreadDegrees, Random);
		Directions.Add(Direction);
		TestTrue(TEXT("pellet direction is normalized"), Direction.IsNormalized());
		TestTrue(TEXT("pellet direction remains inside pelletSpreadDegrees"),
			FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(FVector::ForwardVector, Direction))) <= SpreadDegrees + KINDA_SMALL_NUMBER);
	}
	bool bFoundIndependentDirection = false;
	for (int32 Index = 1; Index < Directions.Num(); ++Index)
	{
		bFoundIndependentDirection |= !Directions[Index].Equals(Directions[0], KINDA_SMALL_NUMBER);
	}
	TestTrue(TEXT("seeded pellets use independent random directions"), bFoundIndependentDirection);

	FShotgunTestWorld TestWorld;
	AMSUnitBase* Shooter = SpawnShotgunUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Ally = SpawnShotgunUnit(TestWorld.World, FVector(400.0f, 0.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnShotgunUnit(TestWorld.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());

	UMSShotgunWeaponData* Weapon = NewObject<UMSShotgunWeaponData>(Shooter);
	Weapon->MagazineSize = 3;
	Weapon->FireInterval = 0.75f;
	Weapon->Range = 20.0f;
	Weapon->Power = 10;
	Weapon->PelletCount = 6;
	Weapon->PelletSpreadDegrees = 0.0f; // Exact rays make FF/HS damage assertions deterministic.
	Shooter->GetWeaponComponent()->InitializeSlots({Weapon});

	TStrongObjectPtr<UGA_Fire_Shotgun> Ability(NewObject<UGA_Fire_Shotgun>(Shooter));
	const FVector HeadOrigin(0.0f, 0.0f, Enemy->GetSimpleCollisionHalfHeight());
	const float AllyArmorBefore = Ally->GetVitalSet()->GetArmor();
	const float EnemyArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("one trigger fires the shotgun"), Ability->FireShotgun(Shooter, HeadOrigin, FVector::ForwardVector));
	TestEqual(TEXT("one trigger consumes exactly one round, not one per pellet"),
		Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 2);
	TestEqual(TEXT("friendly pellets are stopped without damaging the ally"), Ally->GetVitalSet()->GetArmor(), AllyArmorBefore);
	TestEqual(TEXT("friendly pellets do not pass through to the enemy"), Enemy->GetVitalSet()->GetArmor(), EnemyArmorBefore);
	TestTrue(TEXT("fireInterval cooldown starts"), Shooter->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestFalse(TEXT("second trigger during fireInterval is rejected"),
		Ability->FireShotgun(Shooter, HeadOrigin, FVector::ForwardVector));
	TestEqual(TEXT("rejected trigger consumes no ammunition"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 2);

	Ally->SetActorEnableCollision(false);
	const FVector ShooterLocation = Shooter->GetActorLocation();
	const FVector EnemyLocation = Enemy->GetActorLocation();
	AdvanceShotgunWorldPastCooldown(TestWorld.World, Weapon->FireInterval);
	TestEqual(TEXT("world clock completes shotgun fireInterval"),
		Shooter->GetWeaponComponent()->GetFireCooldownRemaining(), 0.0f);
	TestTrue(TEXT("shooter stays at the same trace origin while time advances"),
		Shooter->GetActorLocation().Equals(ShooterLocation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("enemy stays at the same headshot location while time advances"),
		Enemy->GetActorLocation().Equals(EnemyLocation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("shotgun fires again after fireInterval"), Ability->FireShotgun(Shooter, HeadOrigin, FVector::ForwardVector));
	const float ExpectedHeadshotDamage = Weapon->Power * 1.5f * Weapon->PelletCount;
	TestTrue(TEXT("every pellet independently applies the shared headshot damage path"),
		Enemy->GetVitalSet()->GetArmor() <= EnemyArmorBefore - ExpectedHeadshotDamage);

	return true;
}

#endif
