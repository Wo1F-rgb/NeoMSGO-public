// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MSAttributeSet_Vital.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Burst.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBurstAbilityTest,
	"NeoMSGO.Weapons.BurstAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FBurstTestWorld
	{
		UWorld* World = nullptr;
		FBurstTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBurstTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FBurstTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickBurstWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnBurstUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		// 2発目以降は「発射時点のカメラ位置・方向」で再取得される(実プレイ報告バグ修正)ため、
		// テストのカメラを意図した発射起点(機体位置+40cm)へ合わせる(MSPlayerWeaponInputTestsと同手法)
		if (UMSCameraComponent* Camera = Unit->FindComponentByClass<UMSCameraComponent>())
		{
			Camera->SetWorldLocation(Location + FVector(0.0f, 0.0f, 40.0f));
			Camera->SetWorldRotation(FRotator::ZeroRotator);
		}
		return Unit;
	}

	UMSRangedWeaponData* MakeBurstWeapon(UObject* Outer, int32 Magazine, int32 Reserve)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->MagazineSize = Magazine;
		Weapon->ReserveAmmo = Reserve;
		Weapon->Power = 10;
		Weapon->Range = 20.0f;
		Weapon->FireInterval = 0.5f;
		Weapon->BurstCount = 3;
		Weapon->BurstInterval = 0.1f;
		return Weapon;
	}
}

bool FMSBurstAbilityTest::RunTest(const FString& Parameters)
{
	FBurstTestWorld TestWorld;
	AMSUnitBase* Shooter = SpawnBurstUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnBurstUnit(TestWorld.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* Weapon = MakeBurstWeapon(Shooter, 3, 0);
	Shooter->GetWeaponComponent()->InitializeSlots({Weapon});
	UGA_Fire_Burst* Ability = NewObject<UGA_Fire_Burst>(Shooter);

	const FVector Origin(0.0f, 0.0f, 40.0f);
	const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("burst starts and fires its first round immediately"),
		Ability->StartBurst(Shooter, Origin, FVector::ForwardVector));
	TestEqual(TEXT("only one round has fired before BurstInterval"), Ability->GetRoundsFired(), 1);
	TestEqual(TEXT("first round consumes one magazine round"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 2);
	TickBurstWorld(TestWorld.World, 0.05f);
	TestEqual(TEXT("no early second round"), Ability->GetRoundsFired(), 1);
	// TimerManager schedules relative to the world's current timer time; allow one test tick of tolerance.
	TickBurstWorld(TestWorld.World, 0.11f);
	TestEqual(TEXT("second round fires at BurstInterval"), Ability->GetRoundsFired(), 2);
	TickBurstWorld(TestWorld.World, 0.10f);
	TestEqual(TEXT("BurstCount rounds complete"), Ability->GetRoundsFired(), 3);
	TestFalse(TEXT("timer ends after the complete burst"), Ability->IsBurstActive());
	TestTrue(TEXT("each round applied damage through the hitscan damage path"),
		Enemy->GetVitalSet()->GetArmor() <= ArmorBefore - 30.0f);
	TestTrue(TEXT("fireInterval remains active after a shorter burst ends"),
		Shooter->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestFalse(TEXT("cooldown rejects a new burst"), Ability->StartBurst(Shooter, Origin, FVector::ForwardVector));

	// A magazine that cannot satisfy BurstCount must stop the timer without drawing reserve ammo directly.
	AMSUnitBase* ShortShooter = SpawnBurstUnit(TestWorld.World, FVector(0.0f, 1000.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSUnitBase* ShortEnemy = SpawnBurstUnit(TestWorld.World, FVector(800.0f, 1000.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* ShortWeapon = MakeBurstWeapon(ShortShooter, 2, 5);
	ShortWeapon->BurstCount = 4;
	ShortShooter->GetWeaponComponent()->InitializeSlots({ShortWeapon});
	UGA_Fire_Burst* ShortAbility = NewObject<UGA_Fire_Burst>(ShortShooter);
	const float ShortArmorBefore = ShortEnemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("short-magazine burst starts"), ShortAbility->StartBurst(
		ShortShooter, FVector(0.0f, 1000.0f, 40.0f), FVector::ForwardVector));
	TickBurstWorld(TestWorld.World, 0.35f);
	TestEqual(TEXT("burst stops when magazine ammunition is exhausted"), ShortAbility->GetRoundsFired(), 2);
	TestFalse(TEXT("ammo exhaustion clears the repeating timer"), ShortAbility->IsBurstActive());
	TestEqual(TEXT("reserve is not consumed without reload"), ShortShooter->GetWeaponComponent()->GetReserveAmmo(0), 5);
	TestTrue(TEXT("both available rounds used the shared damage path"),
		ShortEnemy->GetVitalSet()->GetArmor() <= ShortArmorBefore - 20.0f);

	// Projectile bursts launch one delayed visual actor per round while retaining the normal
	// burst ammunition and timer semantics.
	AMSUnitBase* ProjectileShooter = SpawnBurstUnit(TestWorld.World, FVector(0.0f, 2000.0f, 0.0f), MSTeam::PlayerTeamId());
	// 世界距離×MSScale(WorldScale W2)。弾速も×MSScaleのため飛翔時間はスケール前と同等。
	AMSUnitBase* ProjectileEnemy = SpawnBurstUnit(
		TestWorld.World, FVector(800.0f * MSGO::MSScale, 2000.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* ProjectileWeapon = MakeBurstWeapon(ProjectileShooter, 3, 0);
	ProjectileWeapon->ProjectileSpeed = 10.0f;
	ProjectileShooter->GetWeaponComponent()->InitializeSlots({ProjectileWeapon});
	UGA_Fire_Burst* ProjectileAbility = NewObject<UGA_Fire_Burst>(ProjectileShooter);
	const float ProjectileArmorBefore = ProjectileEnemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("visual-projectile burst starts"), ProjectileAbility->StartBurst(
		ProjectileShooter, FVector(0.0f, 2000.0f, 40.0f), FVector::ForwardVector));
	TickBurstWorld(TestWorld.World, 0.25f);
	TArray<AActor*> SpawnedProjectiles;
	UGameplayStatics::GetAllActorsOfClass(TestWorld.World, AMSVisualProjectile::StaticClass(), SpawnedProjectiles);
	TestEqual(TEXT("visual-projectile burst launches one actor per round"), SpawnedProjectiles.Num(), 3);
	TestEqual(TEXT("visual-projectile burst deals no damage before flight completes"),
		ProjectileEnemy->GetVitalSet()->GetArmor(), ProjectileArmorBefore);
	TickBurstWorld(TestWorld.World, 0.8f);
	TestTrue(TEXT("all visual-projectile burst rounds damage after impact"),
		ProjectileEnemy->GetVitalSet()->GetArmor() <= ProjectileArmorBefore - 30.0f);

	// 実プレイ報告バグ回帰テスト: バースト連射中に機体が移動しても、2発目以降の発射位置が
	// 機体(カメラ)へ追従する(開始時点の位置から弾が出続けない)。
	// 開始レーン(Y=900)で1発目→レーンY=1500へ移動→2/3発目は移動後レーンの敵に命中する。
	{
		AMSUnitBase* MoveShooter = SpawnBurstUnit(TestWorld.World, FVector(0.0f, 3000.0f, 0.0f), MSTeam::PlayerTeamId());
		AMSUnitBase* StartLaneEnemy = SpawnBurstUnit(TestWorld.World, FVector(800.0f, 3000.0f, 0.0f), MSTeam::EnemyTeamId());
		AMSUnitBase* MovedLaneEnemy = SpawnBurstUnit(TestWorld.World, FVector(800.0f, 4000.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* MoveWeapon = MakeBurstWeapon(MoveShooter, 3, 0);
		MoveShooter->GetWeaponComponent()->InitializeSlots({MoveWeapon});
		UGA_Fire_Burst* MoveAbility = NewObject<UGA_Fire_Burst>(MoveShooter);

		const float StartLaneArmorBefore = StartLaneEnemy->GetVitalSet()->GetArmor();
		const float MovedLaneArmorBefore = MovedLaneEnemy->GetVitalSet()->GetArmor();
		TestTrue(TEXT("moving-shooter burst starts on the initial lane"), MoveAbility->StartBurst(
			MoveShooter, FVector(0.0f, 3000.0f, 40.0f), FVector::ForwardVector));
		TestTrue(TEXT("first round hits the initial-lane enemy"),
			StartLaneArmorBefore - StartLaneEnemy->GetVitalSet()->GetArmor() >= 10.0f);

		// バースト中に機体を移動(カメラは機体へ追従して新レーンから前方を向く)
		MoveShooter->SetActorLocation(FVector(0.0f, 4000.0f, 0.0f));
		if (UMSCameraComponent* MoveCamera = MoveShooter->FindComponentByClass<UMSCameraComponent>())
		{
			MoveCamera->SetWorldLocation(FVector(0.0f, 4000.0f, 40.0f));
			MoveCamera->SetWorldRotation(FRotator::ZeroRotator);
		}
		TickBurstWorld(TestWorld.World, 0.3f);
		TestEqual(TEXT("all burst rounds complete after the move"), MoveAbility->GetRoundsFired(), 3);
		TestTrue(FString::Printf(TEXT("rounds after the move originate from the new position (moved-lane damage=%.1f)"),
				MovedLaneArmorBefore - MovedLaneEnemy->GetVitalSet()->GetArmor()),
			MovedLaneArmorBefore - MovedLaneEnemy->GetVitalSet()->GetArmor() >= 20.0f);
		TestTrue(TEXT("the stale start position no longer receives rounds after the move"),
			StartLaneArmorBefore - StartLaneEnemy->GetVitalSet()->GetArmor() <= 10.0f);
	}

 // Canceling a held connection/weapon must not emit any future rounds.
 {
  FBurstTestWorld CancelWorld;
  AMSUnitBase* CancelShooter = SpawnBurstUnit(CancelWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
  UMSRangedWeaponData* CancelWeapon = MakeBurstWeapon(CancelShooter, 6, 0);
  CancelShooter->GetWeaponComponent()->InitializeSlots({CancelWeapon});
  UGA_Fire_Burst* CancelAbility = NewObject<UGA_Fire_Burst>(CancelShooter);
  TestTrue(TEXT("cancellable burst starts"), CancelAbility->StartBurst(CancelShooter, FVector(0,0,40), FVector::ForwardVector));
  CancelAbility->CancelBurst();
  TickBurstWorld(CancelWorld.World, 0.4f);
  TestFalse(TEXT("explicit cancellation clears the active timer"), CancelAbility->IsBurstActive());
  TestEqual(TEXT("cancellation emits no follow-up rounds"), CancelAbility->GetRoundsFired(), 1);
  TestEqual(TEXT("cancellation preserves remaining ammunition"), CancelShooter->GetWeaponComponent()->GetMagazineAmmo(0), 5);
 }
 // This ability was created outside Character's cached pointer: the follow-up
 // guard must still reject real GAS death rather than trusting that cache alone.
 {
  FBurstTestWorld DeathWorld;
  AMSUnitBase* DeadShooter = SpawnBurstUnit(DeathWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
  UMSRangedWeaponData* DeathWeapon = MakeBurstWeapon(DeadShooter, 6, 0);
  DeadShooter->GetWeaponComponent()->InitializeSlots({DeathWeapon});
  UGA_Fire_Burst* DeathAbility = NewObject<UGA_Fire_Burst>(DeadShooter);
  TestTrue(TEXT("death-guarded burst starts"), DeathAbility->StartBurst(DeadShooter, FVector(0,0,40), FVector::ForwardVector));
  UAbilitySystemComponent* ASC = DeadShooter->GetAbilitySystemComponent();
  AMSUnitBase* Killer = SpawnBurstUnit(DeathWorld.World, FVector(0, 2000, 0), MSTeam::EnemyTeamId());
  FGameplayEffectContextHandle DamageContext = Killer->GetAbilitySystemComponent()->MakeEffectContext();
  DamageContext.AddInstigator(Killer, Killer);
  FGameplayEffectSpec Damage(GetDefault<UGE_Damage>(), DamageContext, 1.0f);
  Damage.SetSetByCallerMagnitude(MSTags::Data_Damage, DeadShooter->GetVitalSet()->GetArmor() + 100.0f);
  Damage.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
  ASC->ApplyGameplayEffectSpecToSelf(Damage);
  TestTrue(TEXT("fixture reached actual GAS death"), ASC->HasMatchingGameplayTag(MSTags::State_Dead));
  TickBurstWorld(DeathWorld.World, 0.4f);
  TestFalse(TEXT("death stops remaining burst rounds"), DeathAbility->IsBurstActive());
  TestEqual(TEXT("dead shooter did not spend follow-up ammunition"), DeadShooter->GetWeaponComponent()->GetMagazineAmmo(0), 5);
 }
 return true;
}

#endif
