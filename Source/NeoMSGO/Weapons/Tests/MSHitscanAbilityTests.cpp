// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotCharacter.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHitscanAbilityTest,
	"NeoMSGO.Weapons.HitscanAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FHitscanTestWorld
	{
		UWorld* World;
		FHitscanTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHitscanTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FHitscanTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSUnitBase* SpawnHitscanUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		return Unit;
	}

	UBoxComponent* AddBlockingBox(AActor* Owner, const FVector& Location, FName Profile = TEXT("MSStructure"))
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
		Owner->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(25.0f));
		Box->SetCollisionProfileName(Profile);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Box;
	}

	void TickHitscanWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}
}

bool FMSHitscanAbilityTest::RunTest(const FString& Parameters)
{
	FHitscanTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSUnitBase* Shooter = SpawnHitscanUnit(World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Ally = SpawnHitscanUnit(World, FVector(400.0f, 0.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnHitscanUnit(World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());

	// A child primitive in front of the muzzle proves actor-level self exclusion covers the complete hierarchy.
	AddBlockingBox(Shooter, FVector(200.0f, 0.0f, 0.0f), TEXT("MSHitbox"));
	FMSHitscanResult Result = UMSCombatStatics::ResolveHitscan(World, FVector::ZeroVector, FVector::ForwardVector, 1200.0f, Shooter);
	TestTrue(TEXT("self hierarchy is skipped and the ally is the first blocking hit"), Result.Hit.GetActor() == Ally);
	TestTrue(TEXT("friendly fire blocks the shot"), Result.bBlockingHit);
	TestFalse(TEXT("friendly fire has zero damage permission"), Result.bCanDamage);

	Ally->SetActorEnableCollision(false);
	AActor* Structure = World->SpawnActor<AActor>();
	AddBlockingBox(Structure, FVector(400.0f, 0.0f, 0.0f));
	Result = UMSCombatStatics::ResolveHitscan(World, FVector::ZeroVector, FVector::ForwardVector, 1200.0f, Shooter);
	TestTrue(TEXT("MSStructure blocks before the enemy"), Result.Hit.GetActor() == Structure);
	Structure->Destroy();

	UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Shooter);
	Weapon->MagazineSize = 2;
	Weapon->FireInterval = 0.75f;
	Weapon->Range = 20.0f;
	Weapon->Power = 100;
	Shooter->GetWeaponComponent()->InitializeSlots({Weapon});

	UGA_Fire_Hitscan* Ability = NewObject<UGA_Fire_Hitscan>(Shooter);
	const FVector HeadOrigin(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("ability fires through the shared resolver"), Ability->Fire(Shooter, HeadOrigin, FVector::ForwardVector));
	TestTrue(TEXT("headshot uses IsHeadshotHit and context multiplier"), Enemy->GetVitalSet()->GetArmor() <= ArmorBefore - 150.0f);
	TestTrue(TEXT("fireInterval starts in MSWeaponComponent"), Shooter->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestFalse(TEXT("a second shot during fireInterval is rejected"), Ability->Fire(Shooter, HeadOrigin, FVector::ForwardVector));

	// Legacy assets retain their scaled gameplay-meter range until individually calibrated.
	// Keep visual origin deliberately off the camera ray to verify it cannot steer the hit.
	AMSUnitBase* SniperShooter = SpawnHitscanUnit(
		World, FVector(0.0f, -5000.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSUnitBase* SniperTarget = SpawnHitscanUnit(
		World, FVector(400.0f * 100.0f * MSGO::MSScale, -5000.0f, 0.0f),
		MSTeam::EnemyTeamId());
	UMSRangedWeaponData* SniperWeapon = NewObject<UMSRangedWeaponData>(SniperShooter);
	SniperWeapon->MagazineSize = 1;
	SniperWeapon->Range = 500.0f;
	SniperWeapon->Power = 100;
	SniperShooter->GetWeaponComponent()->InitializeSlots({SniperWeapon});
	UGA_Fire_Hitscan* SniperAbility = NewObject<UGA_Fire_Hitscan>(SniperShooter);
	const float SniperAimHeight =
		SniperTarget->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector SniperCameraOrigin(0.0f, -5000.0f, SniperAimHeight);
	const FVector SniperMuzzleOrigin(0.0f, -4300.0f, SniperAimHeight - 500.0f);
	const float SniperArmorBefore = SniperTarget->GetVitalSet()->GetArmor();
	TestTrue(TEXT("500 m sniper fires with a separate visual muzzle"),
		SniperAbility->FireFromOrigins(SniperShooter, SniperCameraOrigin,
			FVector::ForwardVector, SniperMuzzleOrigin));
	TestTrue(TEXT("camera/crosshair ray hits a target at 400 gameplay meters"),
		SniperTarget->GetVitalSet()->GetArmor() <= SniperArmorBefore - 100.0f);

	// A calibrated asset interprets the same authored 500 as 500 actual world meters.
	AMSUnitBase* CalibratedShooter = SpawnHitscanUnit(
		World, FVector(0.0f, -15000.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSUnitBase* CalibratedTarget = SpawnHitscanUnit(
		World, FVector(40000.0f, -15000.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* CalibratedWeapon = NewObject<UMSRangedWeaponData>(CalibratedShooter);
	CalibratedWeapon->MagazineSize = 1;
	CalibratedWeapon->Range = 500.0f;
	CalibratedWeapon->RangeWorldScale = 1.0f;
	CalibratedWeapon->Power = 100;
	CalibratedShooter->GetWeaponComponent()->InitializeSlots({CalibratedWeapon});
	UGA_Fire_Hitscan* CalibratedAbility = NewObject<UGA_Fire_Hitscan>(CalibratedShooter);
	const float CalibratedAimHeight = CalibratedTarget->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float CalibratedArmorBefore = CalibratedTarget->GetVitalSet()->GetArmor();
	TestTrue(TEXT("calibrated 500 m weapon fires"), CalibratedAbility->Fire(
		CalibratedShooter, FVector(0.0f, -15000.0f, CalibratedAimHeight), FVector::ForwardVector));
	TestTrue(TEXT("calibrated weapon hits a target 400 actual meters away"),
		CalibratedTarget->GetVitalSet()->GetArmor() <= CalibratedArmorBefore - 100.0f);
	AMSUnitBase* OutOfRangeShooter = SpawnHitscanUnit(
		World, FVector(0.f, -25000.f, 0.f), MSTeam::PlayerTeamId());
	AMSUnitBase* OutOfRangeTarget = SpawnHitscanUnit(
		World, FVector(60000.f, -25000.f, 0.f), MSTeam::EnemyTeamId());
	OutOfRangeShooter->GetWeaponComponent()->InitializeSlots({CalibratedWeapon});
	UGA_Fire_Hitscan* OutOfRangeAbility = NewObject<UGA_Fire_Hitscan>(OutOfRangeShooter);
	const float OutOfRangeArmor = OutOfRangeTarget->GetVitalSet()->GetArmor();
	TestTrue(TEXT("calibrated shot outside range still consumes a valid shot"), OutOfRangeAbility->Fire(
		OutOfRangeShooter, FVector(0.f, -25000.f, CalibratedAimHeight), FVector::ForwardVector));
	TestEqual(TEXT("500 actual metre weapon cannot hit a target 600 metres away"),
		OutOfRangeTarget->GetVitalSet()->GetArmor(), OutOfRangeArmor);

	// A standard ranged weapon with ProjectileSpeed>0 uses the same entry point, but defers
	// its confirmed damage until the spawned visual projectile finishes its flight.
	AMSUnitBase* ProjectileShooter = SpawnHitscanUnit(World, FVector(0.0f, 1000.0f, 0.0f), MSTeam::PlayerTeamId());
	// 世界距離×MSScale(WorldScale W2)。弾速も×MSScaleのため飛翔時間はスケール前と同等。
	AMSUnitBase* ProjectileEnemy = SpawnHitscanUnit(
		World, FVector(800.0f * MSGO::MSScale, 1000.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* ProjectileWeapon = NewObject<UMSRangedWeaponData>(ProjectileShooter);
	ProjectileWeapon->MagazineSize = 2;
	ProjectileWeapon->FireInterval = 0.75f;
	ProjectileWeapon->Range = 20.0f;
	ProjectileWeapon->Power = 100;
	ProjectileWeapon->ProjectileSpeed = 20.0f;
	ProjectileShooter->GetWeaponComponent()->InitializeSlots({ProjectileWeapon});
	UGA_Fire_Hitscan* ProjectileAbility = NewObject<UGA_Fire_Hitscan>(ProjectileShooter);
	const FVector ProjectileOrigin(0.0f, 1000.0f, 40.0f);
	const FVector ProjectileMuzzleOrigin(45.0f, 960.0f, 65.0f);
	const float ProjectileArmorBefore = ProjectileEnemy->GetVitalSet()->GetArmor();

	TestTrue(TEXT("visual-projectile weapon fires through the standard ability"),
		ProjectileAbility->FireFromOrigins(ProjectileShooter, ProjectileOrigin,
			FVector::ForwardVector, ProjectileMuzzleOrigin));
	TestEqual(TEXT("visual-projectile fire consumes ammunition immediately"),
		ProjectileShooter->GetWeaponComponent()->GetMagazineAmmo(0), 1);
	TestTrue(TEXT("visual-projectile fire starts cooldown immediately"),
		ProjectileShooter->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TArray<AActor*> SpawnedProjectiles;
	UGameplayStatics::GetAllActorsOfClass(World, AMSVisualProjectile::StaticClass(), SpawnedProjectiles);
	TestEqual(TEXT("standard ability spawns one AMSVisualProjectile"), SpawnedProjectiles.Num(), 1);
	if (SpawnedProjectiles.Num() == 1)
	{
		TestTrue(TEXT("visual projectile spawns at the muzzle rather than the trace/camera origin"),
			SpawnedProjectiles[0]->GetActorLocation().Equals(ProjectileMuzzleOrigin, 0.1f));
		TestFalse(TEXT("muzzle and camera origins are deliberately distinct in this regression test"),
			ProjectileMuzzleOrigin.Equals(ProjectileOrigin));
	}
	TestEqual(TEXT("visual projectile applies no immediate damage"),
		ProjectileEnemy->GetVitalSet()->GetArmor(), ProjectileArmorBefore);
	TickHitscanWorld(World, 0.2f);
	TestEqual(TEXT("visual projectile applies no damage while in flight"),
		ProjectileEnemy->GetVitalSet()->GetArmor(), ProjectileArmorBefore);
	TickHitscanWorld(World, 0.3f);
	TestTrue(TEXT("visual projectile applies damage after flight completes"),
		ProjectileEnemy->GetVitalSet()->GetArmor() <= ProjectileArmorBefore - 100.0f);

	// P6-2: BOT弱体化damageMultiplier(既定0.3)は即着経路(ResolveAndApplyDamage)だけでなく
	// 可視弾経路(LaunchVisualProjectile)にも適用されなければならない。
	//
	// 幾何条件(flaky対策): BOTはaimSpreadDegrees=4°のVRandConeで射線がランダムに傾くため、
	// 4°円錐内のどの方向でも「胴体カプセルに必ず命中し、HeadHitboxには決して交差しない」距離に
	// ターゲットを置く必要がある。射点Z=0・距離400cmなら最大偏差は400*tan4°≒28cm:
	//  - 横方向28cm < カプセル半径34cm → 必ず胴体にブロッキングヒット
	//  - 上方向28cm、HeadHitbox下端はZ=88-20=68cm → 40cmのクリアランスでHS(1.5倍)にならない
	// (IsHeadshotHitはブロッキングヒット点ではなくRange全長の線分で頭部球と交差判定するため、
	//  旧座標(射点Z=40・距離800cm)では最大Z≒96cmが頭部球Z68～108に入りダメージ45になり得た)
	FActorSpawnParameters BotSpawnParams;
	BotSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSBotCharacter* BotShooter = World->SpawnActor<AMSBotCharacter>(
		AMSBotCharacter::StaticClass(), FVector(0.0f, 2000.0f, 0.0f), FRotator::ZeroRotator, BotSpawnParams);
	check(BotShooter);
	BotShooter->SetGenericTeamId(MSTeam::EnemyTeamId());
	AMSUnitBase* BotTarget = SpawnHitscanUnit(World, FVector(800.0f, 2000.0f, 0.0f), MSTeam::PlayerTeamId());
	UMSRangedWeaponData* BotWeapon = NewObject<UMSRangedWeaponData>(BotShooter);
	BotWeapon->MagazineSize = 1;
	BotWeapon->FireInterval = 0.75f;
	BotWeapon->Range = 20.0f;
	BotWeapon->Power = 100;
	BotWeapon->ProjectileSpeed = 20.0f;
	BotShooter->GetWeaponComponent()->InitializeSlots({BotWeapon});
	UGA_Fire_Hitscan* BotAbility = NewObject<UGA_Fire_Hitscan>(BotShooter);
	const float BotTargetArmorBefore = BotTarget->GetVitalSet()->GetArmor();
	TestEqual(TEXT("BOT default weakening multiplier is 0.3"), BotShooter->GetCombatModifiers().DamageMultiplier, 0.3f);
	TestTrue(TEXT("BOT fires a visual-projectile weapon"),
		BotAbility->Fire(BotShooter, FVector(0.0f, 2000.0f, 0.0f), FVector::ForwardVector));
	TickHitscanWorld(World, 0.5f);
	const float BotProjectileDamage = BotTargetArmorBefore - BotTarget->GetVitalSet()->GetArmor();
	AddInfo(FString::Printf(TEXT("BOT visual-projectile observed damage: %.3f"), BotProjectileDamage));
	TestTrue(TEXT("BOT visual-projectile damage is scaled by the weakening multiplier (~30 not ~100)"),
		FMath::IsNearlyEqual(BotProjectileDamage, 30.0f, 1.0f));

	return true;
}

#endif
