// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-11: 視覚弾+遅延ダメージ適用のAutomationテスト。
// 検証する受け入れ条件:
//  1. 命中判定(誰に当たるか/HSか/FFで無効か)は発射時に確定する
//  2. ダメージ適用(GE_Damage)は見た目の着弾時のみ。飛翔中はASCへのGE適用が発生しない
//  3. 爆風はチャージ倍率(DamageMultiplier)を反映して ApplySplashDamage に渡される
//  4. 発射時トレース確定 → 飛翔 → 飛翔完了時にのみダメージ適用、の時系列

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSUnitBase.h"
#include "Combat/MSVisualProjectile.h"
#include "AbilitySystemGlobals.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameplayCueManager.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVisualProjectileTest,
	"NeoMSGO.Combat.VisualProjectile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVisualProjectileMeshTest,
	"NeoMSGO.Combat.VisualProjectileMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVisualProjectilePhysicalUnitsTest,
	"NeoMSGO.Combat.VisualProjectilePhysicalUnits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FVisualProjectileTestWorld
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
		FVisualProjectileTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSVisualProjectileTestWorld"));
			GameInstance = NewObject<UGameInstance>(GEngine);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FVisualProjectileTestWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickVisualProjectileWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnVisualProjectileUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		return Unit;
	}

	UMSRangedWeaponData* MakeVisualProjectileWeapon(UObject* Outer, float ProjectileSpeed,
		float SplashRadius = 0.0f, int32 SplashDamage = 0)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Outer);
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->Range = 20.0f;
		Weapon->ProjectileSpeed = ProjectileSpeed; // m/s。>0 で可視弾(B-6)
		Weapon->SplashRadius = SplashRadius;
		Weapon->SplashDamage = SplashDamage;
		return Weapon;
	}

	/** Expected profile kept local so this test does not merely call the production falloff helper. */
	float ExpectedVisualExplosionFalloff(const float Distance, const float Radius,
		const float MinFalloff, const float FullDamageRadiusRatio)
	{
		if (Radius <= 0.0f || Distance >= Radius)
		{
			return MinFalloff;
		}
		const float NormalizedDistance = FMath::Max(0.0f, Distance / Radius);
		if (NormalizedDistance <= FullDamageRadiusRatio)
		{
			return 1.0f;
		}
		const float Denominator = FMath::Max(KINDA_SMALL_NUMBER, 1.0f - FullDamageRadiusRatio);
		return FMath::Lerp(1.0f, MinFalloff,
			FMath::Clamp((NormalizedDistance - FullDamageRadiusRatio) / Denominator, 0.0f, 1.0f));
	}
}

bool FMSVisualProjectilePhysicalUnitsTest::RunTest(const FString& Parameters)
{
	FVisualProjectileTestWorld TestWorld;
	AMSUnitBase* Shooter = SpawnVisualProjectileUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnVisualProjectileUnit(TestWorld.World, FVector(50000.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* Weapon = MakeVisualProjectileWeapon(Shooter, 100.0f, 30.0f, 100);
	Weapon->Range = 600.0f;
	Weapon->RangeWorldScale = 1.0f;
	const FVector Origin(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
		Shooter, Origin, FVector::ForwardVector, Weapon);
	if (!TestNotNull(TEXT("physical-unit projectile launches"), Projectile)) return false;
	TestTrue(TEXT("physical-unit trace reaches the 500m enemy"), Projectile->GetConfirmedHit().Hit.GetActor() == Enemy);
	TestTrue(TEXT("100m/s projectile takes about five seconds to reach 500m"),
		FMath::IsNearlyEqual(Projectile->GetFlightDuration(), 5.0f, 0.5f));
	TestTrue(TEXT("30m authored splash is 3000 world centimetres"),
		FMath::IsNearlyEqual(Projectile->GetSplashRadiusCm(), 3000.0f, 1.0f));
	return true;
}

bool FMSVisualProjectileTest::RunTest(const FString& Parameters)
{
	FVisualProjectileTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSUnitBase* Shooter = SpawnVisualProjectileUnit(World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	int32 HitFeedbackCount = 0;
	UMSHitFeedbackSubsystem* Feedback = TestWorld.GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>();
	const FDelegateHandle FeedbackHandle = Feedback->OnHitConfirmed.AddLambda(
		[&](AActor* Source, bool) { if (Source == Shooter) ++HitFeedbackCount; });
	TArray<FGameplayTag> RoutedCueTags;
	TArray<TWeakObjectPtr<AActor>> RoutedCueInstigators;
	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	const FDelegateHandle CueRouteHandle = CueManager->OnGameplayCueRouted().AddLambda(
		[&](AActor* Target, const FGameplayTag Tag, EGameplayCueEvent::Type EventType,
			const FGameplayCueParameters& CueParameters, EGameplayCueExecutionOptions)
		{
			if (Target == Shooter && EventType == EGameplayCueEvent::Executed)
			{
				RoutedCueTags.Add(Tag);
				RoutedCueInstigators.Add(CueParameters.Instigator.Get());
			}
		});

	// ------------------------------------------------------------------
	// シナリオA: 発射時命中確定(HS含む)→ 飛翔中GE適用ゼロ → 着弾時にのみGE_Damage適用
	// ------------------------------------------------------------------
	{
		AMSUnitBase* Enemy = SpawnVisualProjectileUnit(
			World, FVector(800.0f * MSGO::MSScale, 0.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* Weapon = MakeVisualProjectileWeapon(Shooter, /*ProjectileSpeed=*/8.0f);
		const FVector HeadOrigin(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();

		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
			Shooter, HeadOrigin, FVector::ForwardVector, Weapon);
		if (!TestNotNull(TEXT("A: 視覚弾がスポーンされる"), Projectile))
		{
			return false;
		}

		// 受け入れ条件1: 命中は発射時に確定
		const FMSHitscanResult& Hit = Projectile->GetConfirmedHit();
		TestTrue(TEXT("A: 発射時点でブロッキングヒットが確定している"), Hit.bBlockingHit);
		TestTrue(TEXT("A: 誰に当たるかが発射時に確定している"), Hit.Hit.GetActor() == Enemy);
		TestTrue(TEXT("A: ヘッドショット判定が発射時に確定している"), Hit.bIsHeadshot);
		TestTrue(TEXT("A: 敵対関係(bCanDamage)が発射時に確定している"), Hit.bCanDamage);

		// 世界距離と弾速を同じMSScaleで拡大するため、最大射程の飛翔時間は変更前と同等。
		const float ExpectedDuration = FVector::Dist(HeadOrigin, Hit.EndPoint)
			/ (800.0f * MSGO::MSScale);
		TestTrue(TEXT("A: 飛翔時間は距離/弾速で計算される"),
			FMath::IsNearlyEqual(Projectile->GetFlightDuration(), ExpectedDuration, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("A: MSScale後も同等の最大射程到達時間を維持"),
			Projectile->GetFlightDuration() > 0.5f && Projectile->GetFlightDuration() < 1.0f);

		// 受け入れ条件2/4: 発射時点ではダメージゼロ
		TestEqual(TEXT("A: 発射時点ではASCへのGE適用が発生しない"), Enemy->GetVitalSet()->GetArmor(), ArmorBefore);

		// 飛翔中(0.5秒経過)もダメージゼロ、弾は中間地点付近
		TickVisualProjectileWorld(World, 0.5f);
		TestTrue(TEXT("A: 0.5秒後はまだ飛翔中"), Projectile->IsInFlight());
		TestFalse(TEXT("A: 0.5秒後はまだ着弾解決していない"), Projectile->HasResolvedImpact());
		TestEqual(TEXT("A: 飛翔中はASCへのGE適用が発生しない(撃破表示が出ない)"),
			Enemy->GetVitalSet()->GetArmor(), ArmorBefore);
		TestTrue(TEXT("A: 見た目の弾が発射点と着弾点の間を移動している"),
			Projectile->GetActorLocation().X > 100.0f && Projectile->GetActorLocation().X < Hit.EndPoint.X);

		// 受け入れ条件1の駄目押し: 発射後にターゲットが移動しても命中結果は変わらない(発射時確定)
		Enemy->SetActorLocation(FVector(0.0f, 3000.0f * MSGO::MSScale, 0.0f));

		// 飛翔完了 → 着弾時にのみGE_Damage適用(HS 1.5倍 = 150)
		TickVisualProjectileWorld(World, ExpectedDuration - 0.5f + 0.05f);
		TestFalse(TEXT("A: 着弾後は飛翔中ではない"), Projectile->IsInFlight());
		TestTrue(TEXT("A: 着弾解決済み"), Projectile->HasResolvedImpact());
		TestTrue(TEXT("A: 着弾時に発射時確定のターゲットへHS倍率込みでGE_Damageが適用される"),
			FMath::IsNearlyEqual(ArmorBefore - Enemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));

		Enemy->Destroy();
	}

	// ------------------------------------------------------------------
	// シナリオD: 誘導ミサイルの敵命中は爆発Cueに加えて射手用Impact Cueも発火する。
	// #184の回帰防止: 旧実装はBazookaShell分岐でCue_Explosionだけを発火していた。
	// ------------------------------------------------------------------
	{
		const int32 CueCountBefore = RoutedCueTags.Num();
		AMSUnitBase* Enemy = SpawnVisualProjectileUnit(
			World, FVector(800.0f * MSGO::MSScale, 0.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSHomingWeaponData* Weapon = NewObject<UMSHomingWeaponData>(Shooter);
		Weapon->CategoryTag = MSTags::Weapon_Category_Homing;
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->Range = 20.0f;
		Weapon->ProjectileSpeed = 20.0f;
		Weapon->SplashRadius = 5.0f;
		Weapon->SplashDamage = 40;
		Weapon->HomingTurnRateDegPerSec = 180.0f;

		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchHomingVisualProjectile(
			Shooter, FVector::ZeroVector, FVector::ForwardVector, Weapon, Enemy);
		if (!TestNotNull(TEXT("D: 誘導ミサイルがスポーンされる"), Projectile))
		{
			CueManager->OnGameplayCueRouted().Remove(CueRouteHandle);
			return false;
		}
		TickVisualProjectileWorld(World, Projectile->GetFlightDuration() + 0.1f);
		TestTrue(TEXT("D: 誘導ミサイルが着弾解決される"), Projectile->HasResolvedImpact());

		int32 ExplosionCount = 0;
		int32 ImpactCount = 0;
		bool bImpactKeptShooterInstigator = false;
		for (int32 Index = CueCountBefore; Index < RoutedCueTags.Num(); ++Index)
		{
			ExplosionCount += RoutedCueTags[Index] == MSTags::Cue_Explosion ? 1 : 0;
			const bool bImpact = RoutedCueTags[Index] == MSTags::Cue_Impact_Normal
				|| RoutedCueTags[Index] == MSTags::Cue_Impact_Headshot;
			ImpactCount += bImpact ? 1 : 0;
			bImpactKeptShooterInstigator |= bImpact && RoutedCueInstigators[Index] == Shooter;
		}
		TestEqual(TEXT("D: ミサイル命中は爆発Cueを1回発火する"), ExplosionCount, 1);
		TestEqual(TEXT("D: ミサイル命中はImpact Cueも1回発火する"), ImpactCount, 1);
		TestTrue(TEXT("D: Impact CueのInstigatorは射手を維持する"), bImpactKeptShooterInstigator);
		Enemy->Destroy();
	}

	// ------------------------------------------------------------------
	// シナリオB: FF(味方直撃)は発射時に bCanDamage=false が確定し、着弾してもダメージゼロ
	// ------------------------------------------------------------------
	{
		AMSUnitBase* Ally = SpawnVisualProjectileUnit(
			World, FVector(600.0f * MSGO::MSScale, 0.0f, 0.0f), MSTeam::PlayerTeamId());
		UMSRangedWeaponData* Weapon = MakeVisualProjectileWeapon(Shooter, /*ProjectileSpeed=*/20.0f);
		const float AllyArmorBefore = Ally->GetVitalSet()->GetArmor();

		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
			Shooter, FVector::ZeroVector, FVector::ForwardVector, Weapon);
		if (!TestNotNull(TEXT("B: 視覚弾がスポーンされる"), Projectile))
		{
			return false;
		}
		TestTrue(TEXT("B: 味方への直撃は発射時に確定する"), Projectile->GetConfirmedHit().Hit.GetActor() == Ally);
		TestFalse(TEXT("B: FF無効(bCanDamage=false)は発射時に確定する"), Projectile->GetConfirmedHit().bCanDamage);

		TickVisualProjectileWorld(World, Projectile->GetFlightDuration() + 0.05f);
		TestTrue(TEXT("B: 味方直撃弾も着弾解決はされる"), Projectile->HasResolvedImpact());
		TestEqual(TEXT("B: FFは着弾してもダメージゼロ"), Ally->GetVitalSet()->GetArmor(), AllyArmorBefore);

		Ally->Destroy();
	}

	// ------------------------------------------------------------------
	// シナリオC: 爆風武器+チャージ倍率2.0。正の爆発は直撃を含む1 packetだけを
	//            着弾時に適用し、Powerと旧SplashDamageの大きい方を基礎値にする。
	// ------------------------------------------------------------------
	{
		AMSUnitBase* Enemy = SpawnVisualProjectileUnit(
			World, FVector(800.0f * MSGO::MSScale, 0.0f, 0.0f), MSTeam::EnemyTeamId());
		AMSUnitBase* Bystander = SpawnVisualProjectileUnit(World,
			FVector(800.0f * MSGO::MSScale, 300.0f * MSGO::MSScale, 0.0f), MSTeam::EnemyTeamId());
		UMSRangedWeaponData* Weapon = MakeVisualProjectileWeapon(Shooter, /*ProjectileSpeed=*/20.0f,
			/*SplashRadius=*/5.0f, /*SplashDamage=*/80);
		Weapon->DirectHitDownPointMultiplier = 2.0f;
		TestTrue(TEXT("C: 正の爆発packet基礎値はPowerと旧SplashDamageの大きい方"),
			FMath::IsNearlyEqual(Weapon->GetExplosionBaseDamage(), 100.0f));
		TestTrue(TEXT("C: 通常爆発の外周falloff下限は0.1"),
			FMath::IsNearlyEqual(Weapon->GetExplosionMinFalloff(), 0.1f));
		TestTrue(TEXT("C: 通常爆発は半径15%までfull damage"),
			FMath::IsNearlyEqual(Weapon->GetExplosionFullDamageRadiusRatio(), 0.15f));
		UMSRangedWeaponData* SmokeProfile = NewObject<UMSRangedWeaponData>(Shooter);
		SmokeProfile->Power = 0;
		SmokeProfile->SplashRadius = 5.0f;
		SmokeProfile->SplashDamage = 80;
		SmokeProfile->bIsChargeable = true;
		TestTrue(TEXT("C: Power=0煙のhelper基礎値は旧SplashDamageを返す"),
			FMath::IsNearlyEqual(SmokeProfile->GetExplosionBaseDamage(), 80.0f));
		TestTrue(TEXT("C: Power=0煙の外周falloffは旧0.3を維持"),
			FMath::IsNearlyEqual(SmokeProfile->GetExplosionMinFalloff(), 0.3f));
		TestTrue(TEXT("C: Power=0煙に新しい内側full半径を追加しない"),
			FMath::IsNearlyZero(SmokeProfile->GetExplosionFullDamageRadiusRatio()));
		const float EnemyArmorBefore = Enemy->GetVitalSet()->GetArmor();
		const float BystanderArmorBefore = Bystander->GetVitalSet()->GetArmor();

		// カプセル中心高さ(z=0)を狙い、HS無しの胴体直撃にする
		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
			Shooter, FVector::ZeroVector, FVector::ForwardVector, Weapon, /*DamageMultiplier=*/2.0f);
		if (!TestNotNull(TEXT("C: 視覚弾がスポーンされる"), Projectile))
		{
			return false;
		}
		TestTrue(TEXT("C: 直撃対象は発射時に確定する"), Projectile->GetConfirmedHit().Hit.GetActor() == Enemy);
		TestFalse(TEXT("C: 胴体直撃はHSではない"), Projectile->GetConfirmedHit().bIsHeadshot);

		// 飛翔中(約0.4秒の飛翔の半分)は直撃・爆風とも未適用
		TickVisualProjectileWorld(World, Projectile->GetFlightDuration() * 0.5f);
		TestEqual(TEXT("C: 飛翔中は直撃対象にGE適用が発生しない"), Enemy->GetVitalSet()->GetArmor(), EnemyArmorBefore);
		TestEqual(TEXT("C: 飛翔中は爆風対象にもGE適用が発生しない"), Bystander->GetVitalSet()->GetArmor(), BystanderArmorBefore);

		TickVisualProjectileWorld(World, Projectile->GetFlightDuration() * 0.5f + 0.05f);
		TestTrue(TEXT("C: 着弾解決済み"), Projectile->HasResolvedImpact());

		// 期待値: BaseDamage = max(100,80) * 2.0 = 200。直撃対象はDirectHitActor
		// として中心扱い、near missだけprofile(.1/.15)で距離減衰する。
		const FVector Impact = Projectile->GetConfirmedHit().EndPoint;
		// AMSUnitBase's WeaponTrace fixture is a capsule with radius 34m*MSScale. The
		// production resolver measures the nearest collision surface, so derive that distance
		// from the known capsule geometry rather than calling GetClosestPointOnCollision.
		const float BystanderCenterDistance = FVector::Dist(Impact, Bystander->GetActorLocation());
		const float BystanderSurfaceDistance = FMath::Max(
			0.0f, BystanderCenterDistance - 34.0f * MSGO::MSScale);
		const float BystanderFalloff = ExpectedVisualExplosionFalloff(
			BystanderSurfaceDistance, 500.0f * MSGO::MSScale, 0.1f, 0.15f);
		const float ExpectedBystanderDamage = FMath::RoundToFloat(200.0f * BystanderFalloff);
		const float ExpectedBystanderDP = 20.0f * ExpectedVisualExplosionFalloff(
			BystanderSurfaceDistance, 500.0f * MSGO::MSScale, 0.0f, 0.15f);
		TestTrue(TEXT("C: charge doubles base DP and confirmed direct impact doubles it once more"),
			FMath::IsNearlyEqual(Enemy->GetVitalSet()->GetDownPoint(), 40.0f, 0.1f));
		TestTrue(TEXT("C: nearby splash DP receives charge and falloff but not the direct-hit bonus"),
			FMath::IsNearlyEqual(Bystander->GetVitalSet()->GetDownPoint(), ExpectedBystanderDP, 0.1f));

		// DirectHitActorは中心扱いなので、直撃対象は200の1 packetだけ受ける。
		TestTrue(TEXT("C: 直撃対象は単一爆発packetの200だけで二重適用されない"),
			FMath::IsNearlyEqual(EnemyArmorBefore - Enemy->GetVitalSet()->GetArmor(), 200.0f, 1.0f));
		// near missは同じ200をprofile(.1/.15)で距離減衰する。
		TestTrue(TEXT("C: near missは単一爆発BaseDamage(200)へ新profileを適用"),
			FMath::IsNearlyEqual(BystanderArmorBefore - Bystander->GetVitalSet()->GetArmor(), ExpectedBystanderDamage, 1.0f));
		TestTrue(TEXT("C: 爆風対象に実際にダメージが入っている"),
			BystanderArmorBefore - Bystander->GetVitalSet()->GetArmor() > 0.0f);

		Enemy->Destroy();
		Bystander->Destroy();
	}

	// Scenario A is a direct hit, B is friendly fire, and C applies one explosion packet.
	// Each hostile shot produces exactly one HUD notification; friendly fire produces none.
	TestEqual(TEXT("direct, missile, and splash scenarios each broadcast one hit marker"), HitFeedbackCount, 3);
	CueManager->OnGameplayCueRouted().Remove(CueRouteHandle);
	Feedback->OnHitConfirmed.Remove(FeedbackHandle);
	return true;
}

bool FMSVisualProjectileMeshTest::RunTest(const FString& Parameters)
{
	FVisualProjectileTestWorld TestWorld;
	AMSUnitBase* Shooter = SpawnVisualProjectileUnit(
		TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	if (!TestNotNull(TEXT("authored projectile mesh test shooter spawns"), Shooter))
	{
		return false;
	}

	UStaticMesh* RocketMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Meshes/Weapons/HeavyWeapons/SM_Rocket_Projectile.SM_Rocket_Projectile"));
	UStaticMesh* MissileMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Meshes/Weapons/HeavyWeapons/SM_Missile_Projectile.SM_Missile_Projectile"));
	UMSRangedWeaponData* RocketWeapon = LoadObject<UMSRangedWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_MidBazookaMB6.DA_Weapon_MidBazookaMB6"));
	UMSHomingWeaponData* MissileWeapon = LoadObject<UMSHomingWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_HomingMissile.DA_Weapon_HomingMissile"));
	UMaterialInterface* ExhaustMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Meshes/Weapons/HeavyWeapons/M_HeavyWeaponExhaust.M_HeavyWeaponExhaust"));
	const bool bRocketMeshLoaded = TestNotNull(TEXT("production Rocket_Projectile mesh asset loads"), RocketMesh);
	const bool bMissileMeshLoaded = TestNotNull(TEXT("production Missile_Projectile mesh asset loads"), MissileMesh);
	const bool bRocketWeaponLoaded = TestNotNull(TEXT("MidBazookaMB6 data asset loads"), RocketWeapon);
	const bool bMissileWeaponLoaded = TestNotNull(TEXT("HomingMissile data asset loads"), MissileWeapon);
	const bool bExhaustMaterialLoaded = TestNotNull(TEXT("heavy weapon exhaust material loads"), ExhaustMaterial);
	if (!bRocketMeshLoaded || !bMissileMeshLoaded || !bRocketWeaponLoaded
		|| !bMissileWeaponLoaded || !bExhaustMaterialLoaded)
	{
		return false;
	}

	TestEqual(TEXT("MidBazookaMB6 selects the production rocket mesh"),
		RocketWeapon->ProjectileMesh.LoadSynchronous(), RocketMesh);
	TestEqual(TEXT("HomingMissile selects the production missile mesh"),
		MissileWeapon->ProjectileMesh.LoadSynchronous(), MissileMesh);
	TestTrue(TEXT("production rocket mesh is authored along local +X"),
		RocketMesh->GetBounds().BoxExtent.X > RocketMesh->GetBounds().BoxExtent.Y * 2.0f
		&& RocketMesh->GetBounds().BoxExtent.X > RocketMesh->GetBounds().BoxExtent.Z * 2.0f);
	TestTrue(TEXT("production missile mesh is authored along local +X"),
		MissileMesh->GetBounds().BoxExtent.X > MissileMesh->GetBounds().BoxExtent.Y * 2.0f
		&& MissileMesh->GetBounds().BoxExtent.X > MissileMesh->GetBounds().BoxExtent.Z * 2.0f);
	const float RocketLengthCm = RocketMesh->GetBounds().BoxExtent.X * 2.0f
		* FMath::Abs(RocketWeapon->ProjectileMeshTransform.GetScale3D().X);
	const float MissileLengthCm = MissileMesh->GetBounds().BoxExtent.X * 2.0f
		* FMath::Abs(MissileWeapon->ProjectileMeshTransform.GetScale3D().X);
	TestTrue(TEXT("Rocket_Projectile keeps its authored approximately 2.8m length"),
		FMath::IsNearlyEqual(RocketLengthCm, 282.8f, 3.0f));
	TestTrue(TEXT("Missile_Projectile keeps its authored approximately 3.2m length"),
		FMath::IsNearlyEqual(MissileLengthCm, 324.5f, 3.0f));
	TestTrue(TEXT("rocket projectile transform is direct actor-centimetre identity"),
		RocketWeapon->ProjectileMeshTransform.Equals(FTransform::Identity, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("missile projectile transform is direct actor-centimetre identity"),
		MissileWeapon->ProjectileMeshTransform.Equals(FTransform::Identity, KINDA_SMALL_NUMBER));

	AMSVisualProjectile* RocketProjectile = AMSVisualProjectile::LaunchVisualProjectile(
		Shooter, FVector::ZeroVector, FVector::ForwardVector, RocketWeapon);
	AMSVisualProjectile* MissileProjectile = AMSVisualProjectile::LaunchVisualProjectile(
		Shooter, FVector(0.0f, 120.0f, 0.0f), FVector::ForwardVector, MissileWeapon);
	if (!TestNotNull(TEXT("authored rocket projectile visual spawns"), RocketProjectile)
		|| !TestNotNull(TEXT("authored missile projectile visual spawns"), MissileProjectile))
	{
		return false;
	}

	for (const TPair<AMSVisualProjectile*, UStaticMesh*>& Pair : {
		TPair<AMSVisualProjectile*, UStaticMesh*>(RocketProjectile, RocketMesh),
		TPair<AMSVisualProjectile*, UStaticMesh*>(MissileProjectile, MissileMesh)})
	{
		AMSVisualProjectile* Projectile = Pair.Key;
		UStaticMesh* AuthoredMesh = Pair.Value;
		TestTrue(TEXT("production projectile mesh replaces the Cube body"),
			Projectile->IsUsingAuthoredProjectileMesh()
			&& Projectile->GetVisualBody()->GetStaticMesh() == AuthoredMesh);
		TestTrue(TEXT("production projectile body exposes every authored paint slot"),
			AuthoredMesh->GetStaticMaterials().Num() > 0);
		for (int32 MaterialIndex = 0; MaterialIndex < AuthoredMesh->GetStaticMaterials().Num(); ++MaterialIndex)
		{
			TestNotNull(TEXT("authored projectile paint material asset exists"),
				AuthoredMesh->GetMaterial(MaterialIndex));
			TestEqual(TEXT("authored projectile paint material remains on its slot"),
				Projectile->GetVisualBody()->GetMaterial(MaterialIndex),
				AuthoredMesh->GetMaterial(MaterialIndex));
		}
		TestTrue(TEXT("production rocket/missile exhaust stays visible"),
			Projectile->GetVisualTrail()->IsVisible());
		TestTrue(TEXT("production projectile keeps a separate cone exhaust visual"),
			Projectile->GetVisualTrail()->GetStaticMesh()
				&& Projectile->GetVisualTrail()->GetStaticMesh() != AuthoredMesh);
		UMaterialInstanceDynamic* TrailMaterial = Cast<UMaterialInstanceDynamic>(
			Projectile->GetVisualTrail()->GetMaterial(0));
		TestNotNull(TEXT("production projectile exhaust uses a dynamic paint instance"), TrailMaterial);
		if (TrailMaterial)
		{
			TestTrue(TEXT("production projectile exhaust uses M_HeavyWeaponExhaust"),
				TrailMaterial->GetMaterial() == ExhaustMaterial->GetMaterial());
			TestTrue(TEXT("production projectile exhaust is explicitly orange"),
				TrailMaterial->K2_GetVectorParameterValue(TEXT("Color"))
					.Equals(FLinearColor(1.0f, 0.38f, 0.05f, 1.0f), 0.01f));
		}
		TestTrue(TEXT("exhaust cone uses its long local +Z scale axis"),
			Projectile->GetVisualTrail()->GetRelativeScale3D().Z
				> Projectile->GetVisualTrail()->GetRelativeScale3D().X * 4.0f
				&& Projectile->GetVisualTrail()->GetRelativeScale3D().Z
				> Projectile->GetVisualTrail()->GetRelativeScale3D().Y * 4.0f);
		TestTrue(TEXT("exhaust cone apex points back along actor -X"),
			Projectile->GetVisualTrail()->GetRelativeRotation().RotateVector(FVector::UpVector)
				.Equals(FVector::BackwardVector, 0.01f));
		const float NozzleRearX = AuthoredMesh->GetBounds().Origin.X
			- AuthoredMesh->GetBounds().BoxExtent.X - 6.0f;
		TestTrue(TEXT("exhaust cone base is attached behind the projectile rear bound"),
			Projectile->GetVisualTrail()->GetRelativeLocation().X < NozzleRearX);
	}
	return true;
}

#endif
