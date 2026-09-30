// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSChargeAbilityTest,
	"NeoMSGO.Weapons.ChargeAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FChargeTestWorld
	{
		UWorld* World = nullptr;
		FChargeTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSChargeTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FChargeTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickChargeWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnChargeUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		return Unit;
	}

	UMSRangedWeaponData* MakeChargeWeapon(UObject* Outer)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->MagazineSize = 3;
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->Range = 20.0f;
		Weapon->FireInterval = 0.5f;
		Weapon->bIsChargeable = true;
		Weapon->MaxChargeTime = 1.0f;
		return Weapon;
	}
}

bool FMSChargeAbilityTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld TestWorld;
	const FVector Origin(0.0f, 0.0f, 40.0f);

	AMSUnitBase* Shooter = SpawnChargeUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnChargeUnit(TestWorld.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* ChargeWeapon = MakeChargeWeapon(Shooter);
	Shooter->GetWeaponComponent()->InitializeSlots({ChargeWeapon});
	// Outer連鎖はGCのルートにならないため、テスト途中のGCで回収されないよう明示的にルート固定する
	// (実際に全体実行時のGCで回収され、AddDynamicがpending killでensureする間欠失敗が発生した)。
	UGA_Fire_Charge* Ability = NewObject<UGA_Fire_Charge>(Shooter);
	Ability->AddToRoot();
	ON_SCOPE_EXIT { Ability->RemoveFromRoot(); };

	TestTrue(TEXT("charge starts without consuming ammunition"),
		Ability->StartCharge(Shooter, Origin, FVector::ForwardVector));
	TestTrue(TEXT("charge start publishes State.Charging for HUD visibility"),
		Shooter->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Charging));
	TestEqual(TEXT("charge start leaves magazine unchanged"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);
	TickChargeWorld(TestWorld.World, 0.5f);
	TestTrue(TEXT("half charge progress follows held time"), FMath::IsNearlyEqual(Ability->GetChargeProgress01(), 0.5f, 0.03f));
	TestTrue(TEXT("multiplier is Lerp(1,2,progress)"), FMath::IsNearlyEqual(Ability->GetChargeMultiplier(), 1.5f, 0.03f));
	const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("release fires the charged shot"), Ability->ReleaseCharge());
	TestFalse(TEXT("release clears State.Charging"),
		Shooter->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Charging));
	TestTrue(TEXT("release applies the multiplier through GE_Damage SetByCaller"),
		FMath::IsNearlyEqual(ArmorBefore - Enemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));
	TestEqual(TEXT("release consumes exactly one round"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 2);
	TestTrue(TEXT("release starts fireInterval cooldown"), Shooter->GetWeaponComponent()->GetFireCooldownRemaining() > 0.0f);
	TestFalse(TEXT("cooldown rejects a new charge"), Ability->StartCharge(Shooter, Origin, FVector::ForwardVector));
	TickChargeWorld(TestWorld.World, 0.51f);
	TestTrue(TEXT("charge is allowed after fireInterval"), Ability->StartCharge(Shooter, Origin, FVector::ForwardVector));
	Ability->CancelCharge();

	// Full charge clamps at 2x even when held beyond MaxChargeTime, and the
	// clamped value must reach actual Armor damage rather than only the getter.
	TickChargeWorld(TestWorld.World, 0.01f);
	TestTrue(TEXT("full-charge scenario starts"), Ability->StartCharge(Shooter, Origin, FVector::ForwardVector));
	TickChargeWorld(TestWorld.World, 1.2f);
	TestEqual(TEXT("charge progress clamps at MaxChargeTime"), Ability->GetChargeProgress01(), 1.0f);
	TestEqual(TEXT("full charge clamps to 2x"), Ability->GetChargeMultiplier(), 2.0f);
	const float FullChargeArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("full charge releases"), Ability->ReleaseCharge());
	TestTrue(TEXT("full charge applies 2x actual Armor damage"), FMath::IsNearlyEqual(
		FullChargeArmorBefore - Enemy->GetVitalSet()->GetArmor(), 200.0f, 1.0f));

	// Switching broadcasts cancellation before a release can consume ammunition or fire.
	UMSRangedWeaponData* OtherWeapon = NewObject<UMSRangedWeaponData>(Shooter);
	OtherWeapon->MagazineSize = 5;
	OtherWeapon->Range = 20.0f;
	OtherWeapon->WeaponSwitchDuration = 0.0f;
	Shooter->GetWeaponComponent()->InitializeSlots({ChargeWeapon, OtherWeapon});
	TestTrue(TEXT("switch-cancellation scenario starts"), Ability->StartCharge(Shooter, Origin, FVector::ForwardVector));
	TickChargeWorld(TestWorld.World, 0.25f);
	TestTrue(TEXT("weapon switch succeeds while charging"), Shooter->GetWeaponComponent()->SwitchToSlot(1));
	TestFalse(TEXT("weapon switch discards charge state"), Ability->IsCharging());
	TestFalse(TEXT("release after switch is a dud"), Ability->ReleaseCharge());
	TestEqual(TEXT("discarded charge consumes no ammunition"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);
	TestEqual(TEXT("discarded charge applies no damage"), Enemy->GetVitalSet()->GetArmor(), ArmorBefore - 350.0f);

	// Charge uses ResolveAndApplyPellet, so its held-time multiplier must be captured by the
	// visual projectile and applied only when the projectile reaches the confirmed target.
	AMSUnitBase* ProjectileShooter = SpawnChargeUnit(TestWorld.World, FVector(0.0f, 1000.0f, 0.0f), MSTeam::PlayerTeamId());
	// 世界距離×MSScale(WorldScale W2)。弾速も×MSScaleのため飛翔時間はスケール前と同等。
	AMSUnitBase* ProjectileEnemy = SpawnChargeUnit(
		TestWorld.World, FVector(800.0f * MSGO::MSScale, 1000.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* ProjectileWeapon = MakeChargeWeapon(ProjectileShooter);
	ProjectileWeapon->ProjectileSpeed = 20.0f;
	ProjectileShooter->GetWeaponComponent()->InitializeSlots({ProjectileWeapon});
	UGA_Fire_Charge* ProjectileAbility = NewObject<UGA_Fire_Charge>(ProjectileShooter);
	ProjectileAbility->AddToRoot();
	ON_SCOPE_EXIT { ProjectileAbility->RemoveFromRoot(); };
	const float ProjectileArmorBefore = ProjectileEnemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("visual-projectile charge starts"), ProjectileAbility->StartCharge(
		ProjectileShooter, FVector(0.0f, 1000.0f, 40.0f), FVector::ForwardVector));
	TickChargeWorld(TestWorld.World, 0.5f);
	TestTrue(TEXT("visual-projectile charge releases"), ProjectileAbility->ReleaseCharge());
	TArray<AActor*> SpawnedProjectiles;
	UGameplayStatics::GetAllActorsOfClass(TestWorld.World, AMSVisualProjectile::StaticClass(), SpawnedProjectiles);
	TestEqual(TEXT("charged visual shot spawns one AMSVisualProjectile"), SpawnedProjectiles.Num(), 1);
	TestEqual(TEXT("charged visual shot deals no immediate damage"),
		ProjectileEnemy->GetVitalSet()->GetArmor(), ProjectileArmorBefore);
	TickChargeWorld(TestWorld.World, 0.2f);
	TestEqual(TEXT("charged visual shot deals no damage while in flight"),
		ProjectileEnemy->GetVitalSet()->GetArmor(), ProjectileArmorBefore);
	TickChargeWorld(TestWorld.World, 0.3f);
	TestTrue(TEXT("charged visual shot applies the 1.5x multiplier on impact"), FMath::IsNearlyEqual(
		ProjectileArmorBefore - ProjectileEnemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));

	// Explosive charge shots use a single splash GE instead of the direct-hit GE.
	// Check the actual armor change so a multiplier lost at the explosion boundary fails.
	{
		AMSUnitBase* ExplosiveShooter = SpawnChargeUnit(
			TestWorld.World, FVector(0.0f, 5000.0f, 0.0f), MSTeam::PlayerTeamId());
		AMSUnitBase* ExplosiveEnemy = SpawnChargeUnit(
			TestWorld.World, FVector(800.0f * MSGO::MSScale, 5000.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* ExplosiveWeapon = MakeChargeWeapon(ExplosiveShooter);
		ExplosiveWeapon->CategoryTag = MSTags::Weapon_Category_Bazooka;
		ExplosiveWeapon->ProjectileSpeed = 20.0f;
		ExplosiveWeapon->SplashRadius = 3.0f;
		ExplosiveWeapon->SplashDamage = 100;
		ExplosiveShooter->GetWeaponComponent()->InitializeSlots({ExplosiveWeapon});
		UGA_Fire_Charge* ExplosiveAbility = NewObject<UGA_Fire_Charge>(ExplosiveShooter);
		ExplosiveAbility->AddToRoot();
		ON_SCOPE_EXIT { ExplosiveAbility->RemoveFromRoot(); };

		const float ExplosiveArmorBefore = ExplosiveEnemy->GetVitalSet()->GetArmor();
		TestTrue(TEXT("explosive charge starts"), ExplosiveAbility->StartCharge(
			ExplosiveShooter, FVector(0.0f, 5000.0f, 40.0f), FVector::ForwardVector));
		TickChargeWorld(TestWorld.World, 0.5f);
		TestTrue(TEXT("explosive charge releases"), ExplosiveAbility->ReleaseCharge());
		TestEqual(TEXT("explosive charge deals no damage until impact"),
			ExplosiveEnemy->GetVitalSet()->GetArmor(), ExplosiveArmorBefore);
		TickChargeWorld(TestWorld.World, 0.5f);
		TestTrue(TEXT("explosive charge applies one 1.5x splash packet at direct hit"),
			FMath::IsNearlyEqual(ExplosiveArmorBefore - ExplosiveEnemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));
	}

	// Beam-style charge raises both real armor damage and actual DP. Legacy
	// charge fixtures above still use linear 1x..2x.
	{
		FChargeTestWorld ProfileWorld;
		AMSUnitBase* ProfileShooter = SpawnChargeUnit(
			ProfileWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
		AMSUnitBase* ProfileEnemy = SpawnChargeUnit(
			ProfileWorld.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* ProfileWeapon = MakeChargeWeapon(ProfileShooter);
		ProfileWeapon->bUseChargeProfile = true;
		ProfileWeapon->HalfChargeMultipliers.Damage = 1.5f;
		ProfileWeapon->HalfChargeMultipliers.DownPoint = 1.5f;
		ProfileWeapon->HalfChargeMultipliers.ProjectileSpeed = 1.2f;
		ProfileWeapon->FullChargeMultipliers.Damage = 2.0f;
		ProfileWeapon->FullChargeMultipliers.DownPoint = 2.0f;
		ProfileWeapon->FullChargeMultipliers.ProjectileSpeed = 1.5f;
		ProfileShooter->GetWeaponComponent()->InitializeSlots({ProfileWeapon});
		const FMSChargeShotMultipliers Quarter = ProfileWeapon->EvaluateChargeMultipliers(0.25f);
		TestTrue(TEXT("opt-in profile interpolates only between authored 0/half/full anchors"),
			FMath::IsNearlyEqual(Quarter.Damage, 1.25f) &&
			FMath::IsNearlyEqual(Quarter.DownPoint, 1.25f) &&
			FMath::IsNearlyEqual(Quarter.ProjectileSpeed, 1.1f));
		UGA_Fire_Charge* ProfileAbility = NewObject<UGA_Fire_Charge>(ProfileShooter);
		ProfileAbility->AddToRoot();
		ON_SCOPE_EXIT { ProfileAbility->RemoveFromRoot(); };

		TestTrue(TEXT("beam profile starts half-charge shot"), ProfileAbility->StartCharge(
			ProfileShooter, Origin, FVector::ForwardVector));
		TickChargeWorld(ProfileWorld.World, 0.5f);
		const float HalfArmor = ProfileEnemy->GetVitalSet()->GetArmor();
		const float HalfDP = ProfileEnemy->GetVitalSet()->GetDownPoint();
		TestTrue(TEXT("beam profile releases half-charge shot"), ProfileAbility->ReleaseCharge());
		TestTrue(TEXT("beam half-charge applies 1.5x actual armor damage"), FMath::IsNearlyEqual(
			HalfArmor - ProfileEnemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));
		TestTrue(TEXT("beam half-charge applies 1.5x actual DP"), FMath::IsNearlyEqual(
			ProfileEnemy->GetVitalSet()->GetDownPoint() - HalfDP, 15.0f, 0.1f));

		TickChargeWorld(ProfileWorld.World, 0.51f);
		TestTrue(TEXT("beam profile starts full-charge shot"), ProfileAbility->StartCharge(
			ProfileShooter, Origin, FVector::ForwardVector));
		TickChargeWorld(ProfileWorld.World, 1.01f);
		const float FullArmor = ProfileEnemy->GetVitalSet()->GetArmor();
		const float FullDP = ProfileEnemy->GetVitalSet()->GetDownPoint();
		TestTrue(TEXT("beam profile releases full-charge shot"), ProfileAbility->ReleaseCharge());
		TestTrue(TEXT("beam full-charge applies 2x actual armor damage"), FMath::IsNearlyEqual(
			FullArmor - ProfileEnemy->GetVitalSet()->GetArmor(), 200.0f, 1.0f));
		TestTrue(TEXT("beam full-charge applies 2x actual DP"), FMath::IsNearlyEqual(
			ProfileEnemy->GetVitalSet()->GetDownPoint() - FullDP, 20.0f, 0.1f));
	}

	// Bazooka-style charge snapshots all independent effects at launch. Check
	// the actual explosion packet and DP, plus its radius and flight duration.
	{
		FChargeTestWorld ProfileWorld;
		AMSUnitBase* ProfileShooter = SpawnChargeUnit(
			ProfileWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
		AMSUnitBase* ProfileEnemy = SpawnChargeUnit(
			ProfileWorld.World, FVector(800.0f * MSGO::MSScale, 0.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* ProfileWeapon = MakeChargeWeapon(ProfileShooter);
		ProfileWeapon->CategoryTag = MSTags::Weapon_Category_Bazooka;
		ProfileWeapon->ProjectileSpeed = 20.0f;
		ProfileWeapon->SplashRadius = 3.0f;
		ProfileWeapon->SplashDamage = 100;
		ProfileWeapon->bUseChargeProfile = true;
		ProfileWeapon->HalfChargeMultipliers.Damage = 1.2f;
		ProfileWeapon->HalfChargeMultipliers.DownPoint = 1.2f;
		ProfileWeapon->HalfChargeMultipliers.ProjectileSpeed = 1.1f;
		ProfileWeapon->HalfChargeMultipliers.SplashRadius = 1.2f;
		ProfileWeapon->FullChargeMultipliers.Damage = 1.5f;
		ProfileWeapon->FullChargeMultipliers.DownPoint = 1.5f;
		ProfileWeapon->FullChargeMultipliers.ProjectileSpeed = 1.2f;
		ProfileWeapon->FullChargeMultipliers.SplashRadius = 1.5f;
		ProfileShooter->GetWeaponComponent()->InitializeSlots({ProfileWeapon});
		UGA_Fire_Charge* ProfileAbility = NewObject<UGA_Fire_Charge>(ProfileShooter);
		ProfileAbility->AddToRoot();
		ON_SCOPE_EXIT { ProfileAbility->RemoveFromRoot(); };

		const float BaseRadiusCm = ProfileWeapon->SplashRadius * 100.0f * ProfileWeapon->RangeWorldScale;
		TestTrue(TEXT("bazooka profile starts half-charge shot"), ProfileAbility->StartCharge(
			ProfileShooter, Origin, FVector::ForwardVector));
		TickChargeWorld(ProfileWorld.World, 0.5f);
		const float HalfArmor = ProfileEnemy->GetVitalSet()->GetArmor();
		const float HalfDP = ProfileEnemy->GetVitalSet()->GetDownPoint();
		TestTrue(TEXT("bazooka profile releases half-charge shot"), ProfileAbility->ReleaseCharge());
		TArray<AActor*> Projectiles;
		UGameplayStatics::GetAllActorsOfClass(ProfileWorld.World, AMSVisualProjectile::StaticClass(), Projectiles);
		if (!TestEqual(TEXT("half-charge bazooka launches one projectile"), Projectiles.Num(), 1)) return false;
		const AMSVisualProjectile* HalfProjectile = CastChecked<AMSVisualProjectile>(Projectiles[0]);
		const float HalfFlightSeconds = HalfProjectile->GetFlightDuration();
		TestTrue(TEXT("half-charge bazooka scales its actual splash radius"), FMath::IsNearlyEqual(
			HalfProjectile->GetSplashRadiusCm(), BaseRadiusCm * 1.2f, 1.0f));
		TickChargeWorld(ProfileWorld.World, 0.5f);
		TestTrue(TEXT("half-charge bazooka applies 1.2x actual explosion damage"), FMath::IsNearlyEqual(
			HalfArmor - ProfileEnemy->GetVitalSet()->GetArmor(), 120.0f, 1.0f));
		TestTrue(TEXT("half-charge bazooka applies 1.2x actual DP"), FMath::IsNearlyEqual(
			ProfileEnemy->GetVitalSet()->GetDownPoint() - HalfDP, 12.0f, 0.1f));

		TickChargeWorld(ProfileWorld.World, 0.01f);
		TestTrue(TEXT("bazooka profile starts full-charge shot"), ProfileAbility->StartCharge(
			ProfileShooter, Origin, FVector::ForwardVector));
		TickChargeWorld(ProfileWorld.World, 1.01f);
		const float FullArmor = ProfileEnemy->GetVitalSet()->GetArmor();
		const float FullDP = ProfileEnemy->GetVitalSet()->GetDownPoint();
		TestTrue(TEXT("bazooka profile releases full-charge shot"), ProfileAbility->ReleaseCharge());
		Projectiles.Reset();
		UGameplayStatics::GetAllActorsOfClass(ProfileWorld.World, AMSVisualProjectile::StaticClass(), Projectiles);
		if (!TestEqual(TEXT("full-charge bazooka launches one projectile"), Projectiles.Num(), 1)) return false;
		const AMSVisualProjectile* FullProjectile = CastChecked<AMSVisualProjectile>(Projectiles[0]);
		TestTrue(TEXT("full-charge bazooka scales its actual splash radius"), FMath::IsNearlyEqual(
			FullProjectile->GetSplashRadiusCm(), BaseRadiusCm * 1.5f, 1.0f));
		TestTrue(TEXT("full-charge bazooka flies faster than half-charge"),
			FullProjectile->GetFlightDuration() < HalfFlightSeconds * 0.95f);
		TickChargeWorld(ProfileWorld.World, 0.5f);
		TestTrue(TEXT("full-charge bazooka applies 1.5x actual explosion damage"), FMath::IsNearlyEqual(
			FullArmor - ProfileEnemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));
		TestTrue(TEXT("full-charge bazooka applies 1.5x actual DP"), FMath::IsNearlyEqual(
			ProfileEnemy->GetVitalSet()->GetDownPoint() - FullDP, 15.0f, 0.1f));
	}

	// 実プレイ報告バグ回帰テスト: チャージ中に移動・旋回しても「リリース時点」の位置・方向から
	// 発射される(StartCharge時のOrigin/Directionの使い回し禁止)。
	// 開始時は+X方向(誰もいない)を向いてチャージし、リリース時に+Y方向の敵へ向き直す。
	{
		AMSUnitBase* MovingShooter = SpawnChargeUnit(TestWorld.World, FVector(0.0f, -3000.0f, 0.0f), MSTeam::PlayerTeamId());
		AMSUnitBase* SideEnemy = SpawnChargeUnit(TestWorld.World, FVector(0.0f, -2200.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* MovingWeapon = MakeChargeWeapon(MovingShooter);
		MovingShooter->GetWeaponComponent()->InitializeSlots({MovingWeapon});
		UGA_Fire_Charge* MovingAbility = NewObject<UGA_Fire_Charge>(MovingShooter);
		MovingAbility->AddToRoot();
		ON_SCOPE_EXIT { MovingAbility->RemoveFromRoot(); };
		const float SideEnemyArmorBefore = SideEnemy->GetVitalSet()->GetArmor();

		const FVector StartOrigin(0.0f, -3000.0f, 40.0f);
		TestTrue(TEXT("moving-shooter charge starts aimed at empty space (+X)"),
			MovingAbility->StartCharge(MovingShooter, StartOrigin, FVector::ForwardVector));
		TickChargeWorld(TestWorld.World, 0.5f);

		// チャージ中に移動+旋回した想定のリリース位置・方向(敵は+Y方向)
		const FVector ReleaseOrigin(0.0f, -2600.0f, 40.0f);
		TestTrue(TEXT("release fires from the release-time origin/direction"),
			MovingAbility->ReleaseChargeFromOrigins(ReleaseOrigin, FVector::RightVector, ReleaseOrigin));
		TestTrue(FString::Printf(TEXT("release-time aim hits the enemy on the new heading (damage=%.1f)"),
				SideEnemyArmorBefore - SideEnemy->GetVitalSet()->GetArmor()),
			SideEnemyArmorBefore - SideEnemy->GetVitalSet()->GetArmor() > 0.0f);
	}

	// Direct GAS activation must launch from the same live muzzle as the charge
	// glow, even after the player moves between press and release.
	{
		FChargeTestWorld GASWorld;
		AMSPlayerCharacter* GASShooter = CastChecked<AMSPlayerCharacter>(SpawnChargeUnit(
			GASWorld.World, FVector(0.0f, 9000.0f, 0.0f), MSTeam::PlayerTeamId()));
		UMSRangedWeaponData* GASWeapon = MakeChargeWeapon(GASShooter);
		GASWeapon->ProjectileSpeed = 2.0f;
		GASShooter->GetWeaponComponent()->InitializeSlots({GASWeapon});
		UMSWeaponVisualComponent* Visual = GASShooter->GetWeaponVisualComponent();
		if (!TestNotNull(TEXT("direct GAS shooter has a weapon visual"), Visual)) return false;
		Visual->RefreshFromWeaponComponent();
		const FVector StartMuzzle = Visual->GetMuzzleWorldLocation();
		TestTrue(TEXT("direct GAS test muzzle is distinct from actor origin"),
			FVector::Dist(StartMuzzle, GASShooter->GetActorLocation()) > 10.0f);

		UAbilitySystemComponent* GASASC = GASShooter->GetAbilitySystemComponent();
		constexpr int32 ChargeInputId = 77;
		const FGameplayAbilitySpecHandle ChargeHandle = GASASC->GiveAbility(
			FGameplayAbilitySpec(UGA_Fire_Charge::StaticClass(), 1, ChargeInputId));
		FGameplayAbilitySpec* ChargeSpec = GASASC->FindAbilitySpecFromHandle(ChargeHandle);
		if (!TestNotNull(TEXT("direct GAS charge spec is granted"), ChargeSpec)) return false;
		ChargeSpec->InputPressed = true;
		TestTrue(TEXT("direct GAS charge activates"), GASASC->TryActivateAbility(ChargeHandle));
		UGA_Fire_Charge* GASAbility = Cast<UGA_Fire_Charge>(ChargeSpec->GetPrimaryInstance());
		if (!TestNotNull(TEXT("direct GAS charge has an instance"), GASAbility)) return false;
		TestTrue(TEXT("direct GAS input press holds the charge"), GASAbility->IsCharging());

		GASShooter->SetActorLocation(FVector(350.0f, 9000.0f, 0.0f));
		TickChargeWorld(GASWorld.World, 0.25f);
		const FVector ReleaseMuzzle = Visual->GetMuzzleWorldLocation();
		TestTrue(TEXT("direct GAS release muzzle tracks movement"),
			FVector::Dist(StartMuzzle, ReleaseMuzzle) > 200.0f);
		GASASC->AbilityLocalInputReleased(ChargeInputId);
		TArray<AActor*> GASProjectiles;
		UGameplayStatics::GetAllActorsOfClass(GASWorld.World,
			AMSVisualProjectile::StaticClass(), GASProjectiles);
		TestEqual(TEXT("direct GAS input release launches one projectile"), GASProjectiles.Num(), 1);
		if (GASProjectiles.Num() == 1)
		{
			TestTrue(TEXT("direct GAS projectile launches from live charge-glow muzzle"),
				FVector::Dist(GASProjectiles[0]->GetActorLocation(), ReleaseMuzzle) < 1.0f);
		}
		TestEqual(TEXT("direct GAS release spends one round"),
			GASShooter->GetWeaponComponent()->GetMagazineAmmo(0), 2);
		TestFalse(TEXT("direct GAS release clears charging"), GASAbility->IsCharging());
	}

	// A directly held ability can outlive Character's cached fire pointer.  The
	// release path must still discard the charge after real GAS death.
	{
		FChargeTestWorld DeathWorld;
		AMSUnitBase* DeadShooter = SpawnChargeUnit(
			DeathWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
		UMSRangedWeaponData* DeathWeapon = MakeChargeWeapon(DeadShooter);
		DeadShooter->GetWeaponComponent()->InitializeSlots({DeathWeapon});
		UGA_Fire_Charge* DeathAbility = NewObject<UGA_Fire_Charge>(DeadShooter);
		DeathAbility->AddToRoot();
		ON_SCOPE_EXIT { DeathAbility->RemoveFromRoot(); };

		TestTrue(TEXT("death-guarded charge starts"), DeathAbility->StartCharge(
			DeadShooter, Origin, FVector::ForwardVector));
		UAbilitySystemComponent* DeathASC = DeadShooter->GetAbilitySystemComponent();
		AMSUnitBase* Killer = SpawnChargeUnit(
			DeathWorld.World, FVector(0.0f, 2000.0f, 0.0f), MSTeam::EnemyTeamId());
		FGameplayEffectContextHandle DamageContext =
			Killer->GetAbilitySystemComponent()->MakeEffectContext();
		DamageContext.AddInstigator(Killer, Killer);
		FGameplayEffectSpec Damage(GetDefault<UGE_Damage>(), DamageContext, 1.0f);
		Damage.SetSetByCallerMagnitude(
			MSTags::Data_Damage, DeadShooter->GetVitalSet()->GetArmor() + 100.0f);
		Damage.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
		DeathASC->ApplyGameplayEffectSpecToSelf(Damage);
		TestTrue(TEXT("charge fixture reached actual GAS death"),
			DeathASC->HasMatchingGameplayTag(MSTags::State_Dead));
		const int32 AmmoBeforeRelease = DeadShooter->GetWeaponComponent()->GetMagazineAmmo(0);
		TestFalse(TEXT("release after death discards the held charge"), DeathAbility->ReleaseCharge());
		TestFalse(TEXT("death clears the held charge state"), DeathAbility->IsCharging());
		TestEqual(TEXT("dead shooter does not spend ammunition on release"),
			DeadShooter->GetWeaponComponent()->GetMagazineAmmo(0), AmmoBeforeRelease);
		TestFalse(TEXT("dead shooter cannot begin another charge"), DeathAbility->StartCharge(
			DeadShooter, Origin, FVector::ForwardVector));
	}

	return true;
}

#endif
