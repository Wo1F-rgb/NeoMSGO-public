// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-10 検証: AMSHomingProjectile(誘導ミサイルActor)テスト。
// 受け入れ条件:
//  1. 旋回率90°/秒でターゲット方向へ機首を向ける(RotateTowards相当の滑らかな追尾)
//  2. 寿命6秒(タイムアウトで自爆=爆風を発生させて消滅)
//  3. 障害物(WeaponTraceでBlockする壁)に着弾したら爆発
//  4. 追尾対象が消失(破棄)した場合は誘導を諦めて直進を続ける
//  6. 着弾時に ApplySplashDamage(P2-4)で爆風ダメージを与える
// 追加: 味方ターゲットは受理しない(#161 味方誤追尾バグの再発防止)
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Combat.HomingProjectile"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Combat.HomingProjectile"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSHomingProjectile.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "NeoMSGO.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingProjectileTest,
	"NeoMSGO.Combat.HomingProjectile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Actor Tick+空間クエリ+GE適用が必要なため使い捨てGameワールドを作る(P4-10テスト専用) */
	struct FHomingProjTestWorld
	{
		UWorld* World = nullptr;

		FHomingProjTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHomingProjectileTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			// GameMode不在のベアワールドでは UWorld::BeginPlay が BegunPlay を立てないため、
			// 明示的に通知してスポーンActorのBeginPlay/Tickを有効化する(Actor Tick駆動テスト必須)
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FHomingProjTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** 0.1s刻みでワールドをTickする(AMSHomingProjectile::Tick駆動用) */
	void HomingProjTickWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Seconds, Step));
			Seconds -= Step;
			GFrameCounter++;
		}
	}

	/** 陣営+ASC+VitalSetを持つ最小テストユニット(AMSUnitBaseはAbstractのため代替構成) */
	struct FHomingProjTestUnit
	{
		AMSTeamAgentActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UMSAttributeSet_Vital* Vital = nullptr;
	};

	/** WeaponTraceチャンネルにヒットする球コライダーをアクターへ追加する */
	USphereComponent* HomingProjAddTraceSphere(AActor* Owner, const FName Name, const float SphereRadius,
		const FName ProfileName = NAME_None)
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(Owner, Name);
		Sphere->SetupAttachment(Owner->GetRootComponent());
		Sphere->SetSphereRadius(SphereRadius);
		if (ProfileName != NAME_None)
		{
			Sphere->SetCollisionProfileName(ProfileName);
		}
		else
		{
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
			Sphere->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
		}
		Sphere->RegisterComponent();
		return Sphere;
	}

	/**
	 * テストユニット生成。bLockOnTargetable=true をリフレクションで設定し、
	 * UMSTeamStatics::CanLockOn(追尾維持の単一API)を通過できるようにする。
	 */
	FHomingProjTestUnit SpawnHomingProjTestUnit(UWorld* World, const FGenericTeamId TeamId, const FVector& Location,
		const float ColliderRadius = 50.0f, const bool bLockOnTargetable = true)
	{
		FHomingProjTestUnit Unit;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Unit.Actor = World->SpawnActor<AMSTeamAgentActor>(AMSTeamAgentActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		Unit.Actor->SetGenericTeamId(TeamId);

		USceneComponent* Root = NewObject<USceneComponent>(Unit.Actor, TEXT("TestRoot"));
		Unit.Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Unit.Actor->SetActorLocation(Location);

		if (ColliderRadius > 0.0f)
		{
			HomingProjAddTraceSphere(Unit.Actor, TEXT("TestCollider"), ColliderRadius);
		}

		if (bLockOnTargetable)
		{
			// AMSTeamAgentActor::bLockOnTargetable は protected のためリフレクションで設定
			if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(AMSTeamAgentActor::StaticClass(), TEXT("bLockOnTargetable")))
			{
				Prop->SetPropertyValue_InContainer(Unit.Actor, true);
			}
		}

		Unit.ASC = NewObject<UAbilitySystemComponent>(Unit.Actor, TEXT("TestASC"));
		Unit.ASC->RegisterComponent();
		Unit.Vital = NewObject<UMSAttributeSet_Vital>(Unit.Actor, TEXT("TestVital"));
		Unit.ASC->AddSpawnedAttribute(Unit.Vital);
		Unit.ASC->InitAbilityActorInfo(Unit.Actor, Unit.Actor);

		return Unit;
	}

	/** コリジョンだけ持つ障害物(壁)アクター。MSStructureプロファイル(WeaponTrace=Block) */
	AActor* SpawnHomingProjWall(UWorld* World, const FVector& Location, const float SphereRadius)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Wall, TEXT("WallRoot"));
		Wall->SetRootComponent(Root);
		Root->RegisterComponent();
		Wall->SetActorLocation(Location);
		HomingProjAddTraceSphere(Wall, TEXT("WallCollider"), SphereRadius, MSCollision::StructureProfileName);
		return Wall;
	}

	/** 実マップの水Brush同様、WeaponTraceをBlockするテスト用water PhysicsVolume。 */
	APhysicsVolume* SpawnHomingProjWaterVolume(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>(
			APhysicsVolume::StaticClass(), Location, FRotator::ZeroRotator, Params);
		Volume->bWaterVolume = true;

		UBoxComponent* Bounds = NewObject<UBoxComponent>(Volume, TEXT("WaterTraceBounds"));
		Volume->AddInstanceComponent(Bounds);
		Bounds->SetupAttachment(Volume->GetRootComponent());
		Bounds->SetBoxExtent(FVector(100.0f));
		Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Bounds->SetCollisionObjectType(ECC_WorldStatic);
		Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
		Bounds->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
		Bounds->RegisterComponent();
		return Volume;
	}

	/** 誘導弾スポーン(向き指定)。InitHomingは呼び出し側で行う */
	AMSHomingProjectile* SpawnHomingProjectileActor(UWorld* World, const FVector& Location, const FRotator& Rotation)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSHomingProjectile>(AMSHomingProjectile::StaticClass(), Location, Rotation, Params);
	}

	/** セクション間のテスト分離: 空間クエリへ残留アクターが混入しないよう各セクション末尾で破棄 */
	void HomingProjDestroyAll(UWorld* World, std::initializer_list<AActor*> Actors)
	{
		for (AActor* Actor : Actors)
		{
			if (IsValid(Actor))
			{
				World->DestroyActor(Actor);
			}
		}
	}

	/** Expected profile kept local so homing tests do not call production falloff code. */
	float ExpectedExplosionFalloff(const float Distance, const float Radius,
		const float MinFalloff = 0.1f, const float FullDamageRadiusRatio = 0.15f)
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

bool FMSHomingProjectileTest::RunTest(const FString& Parameters)
{
	FHomingProjTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	const float MaxArmor = 5000.0f; // UMSAttributeSet_Vital コンストラクタ既定値

	// ------------------------------------------------------------------
	// 1. 旋回率90°/秒: +X向きの弾が+Y方向(90°)のターゲットへ、0.5sで約45°だけ旋回する
	//    (Speed=0でその場旋回させ、幾何を固定して角度だけを検証する)
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FHomingProjTestUnit Target = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(0, 1000, 0));

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		Missile->InitHoming(Shooter.Actor, Target.Actor, /*Speed=*/0.0f, /*TurnRate=*/90.0f);
		TestTrue(TEXT("旋回: 敵ターゲットは追尾対象として受理される"), Missile->IsHomingActive());

		HomingProjTickWorld(World, 0.5f);
		const float YawAfterHalf = Missile->GetActorRotation().Yaw;
		TestTrue(FString::Printf(TEXT("旋回: 0.5sで約45°(90°/s制限。実測%.1f°)"), YawAfterHalf),
			YawAfterHalf >= 40.0f && YawAfterHalf <= 50.0f);
		TestTrue(TEXT("旋回: 0.5s時点ではまだターゲット方向(90°)に到達していない(瞬間旋回でない)"),
			YawAfterHalf < 85.0f);

		HomingProjTickWorld(World, 1.0f);
		const float YawAfterFull = Missile->GetActorRotation().Yaw;
		TestTrue(FString::Printf(TEXT("旋回: 1.5s後はターゲット方向90°へ収束(実測%.1f°)"), YawAfterFull),
			FMath::Abs(YawAfterFull - 90.0f) <= 2.0f);

		HomingProjDestroyAll(World, { Shooter.Actor, Target.Actor, Missile });
	}

	// ------------------------------------------------------------------
	// 2. 寿命6秒: タイムアウトで自爆(爆風を発生させて消滅)
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FHomingProjTestUnit Bystander = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(300, 0, 0));

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		// ターゲット無し・速度0でその場に留まらせ、寿命切れだけを発生させる
		Missile->InitHoming(Shooter.Actor, nullptr, /*Speed=*/0.0f, /*TurnRate=*/90.0f,
			/*SplashRadius=*/1000.0f, /*SplashDamage=*/1000.0f);
		const TWeakObjectPtr<AMSHomingProjectile> MissilePtr = Missile;

		HomingProjTickWorld(World, 5.5f);
		TestTrue(TEXT("寿命: 5.5s時点ではまだ生存"), MissilePtr.IsValid() && !MissilePtr->HasExploded());
		TestEqual(TEXT("寿命: 自爆前は周囲無傷"), Bystander.Vital->GetArmor(), MaxArmor);

		HomingProjTickWorld(World, 1.0f);
		TestFalse(TEXT("寿命: 6s超過で消滅(自爆)"), MissilePtr.IsValid());
		// Fixtureは半径50cmの球なので、中心距離300cmでは表面距離250cm。
		// 数値InitHomingは新profile(.1/.15): 250/1000の中間falloffは0.894117…→894。
		const float ExpectedDamage = FMath::RoundToFloat(
			1000.0f * ExpectedExplosionFalloff(300.0f - 50.0f, 1000.0f));
		TestEqual(TEXT("寿命: 自爆の爆風が表面距離と新profileで適用される(894)"),
			Bystander.Vital->GetArmor(), MaxArmor - ExpectedDamage);

		HomingProjDestroyAll(World, { Shooter.Actor, Bystander.Actor });
	}

	// ------------------------------------------------------------------
	// 3. 水中回帰: water APhysicsVolumeを通過して後続の敵へ着弾する。
	//    水volumeで止まる旧実装では、爆心が敵から離れすぎてダメージが入らない。
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(-500, 0, 5000), 0.0f);
		const FHomingProjTestUnit Enemy = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(800, 0, 0));
		APhysicsVolume* Water = SpawnHomingProjWaterVolume(World, FVector(300, 0, 0));

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		Missile->InitHoming(Shooter.Actor, nullptr, /*Speed=*/2000.0f, /*TurnRate=*/90.0f,
			/*SplashRadius=*/300.0f, /*SplashDamage=*/1000.0f);
		const TWeakObjectPtr<AMSHomingProjectile> MissilePtr = Missile;

		HomingProjTickWorld(World, 0.5f); // 水volume(200..400)を越え、敵の手前(x=750)へ着弾
		TestFalse(TEXT("水中射撃: water volume通過後の敵Hitで誘導弾が爆発・消滅する"), MissilePtr.IsValid());
		TestEqual(TEXT("水中射撃: water volumeで止まらず後続敵へ直撃爆風が入る"),
			Enemy.Vital->GetArmor(), MaxArmor - 1000.0f);

		HomingProjDestroyAll(World, { Shooter.Actor, Enemy.Actor, Water });
	}

	// ------------------------------------------------------------------
	// 4+7. 障害物着弾: WeaponTraceでBlockする壁(MSStructure)に当たると爆発し、
	//      ApplySplashDamage(P2-4)の爆風が周囲の敵へ適用される
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		// 壁: 中心(500,0,0)半径100 → +X直進の弾はx=400で着弾する
		AActor* Wall = SpawnHomingProjWall(World, FVector(500, 0, 0), /*SphereRadius=*/100.0f);
		// 爆心(400,0,0)から300cmの位置の敵(コライダーは細く、弾道x軸には触れない)
		const FHomingProjTestUnit Enemy = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(400, 300, 0), 50.0f);

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		// ターゲット無し直進弾(壁着弾のみを検証)
		Missile->InitHoming(Shooter.Actor, nullptr, /*Speed=*/2000.0f, /*TurnRate=*/90.0f,
			/*SplashRadius=*/1000.0f, /*SplashDamage=*/1000.0f);
		const TWeakObjectPtr<AMSHomingProjectile> MissilePtr = Missile;

		HomingProjTickWorld(World, 0.5f); // 2000cm/sなら0.2s強で壁面(x=400)へ到達
		TestFalse(TEXT("障害物: 壁着弾で爆発・消滅する"), MissilePtr.IsValid());
		TestTrue(TEXT("障害物: 壁は破壊されず残る(ASC無し=ダメージ対象外)"), IsValid(Wall));
		// fixture球半径50cmを引いた表面距離250cm: 新profile(.1/.15)で894ダメージ。
		const float ExpectedDamage = FMath::RoundToFloat(
			1000.0f * ExpectedExplosionFalloff(300.0f - 50.0f, 1000.0f));
		TestEqual(TEXT("着弾爆風: 表面距離と新profileで周囲の敵へ894ダメージ"),
			Enemy.Vital->GetArmor(), MaxArmor - ExpectedDamage);

		HomingProjDestroyAll(World, { Shooter.Actor, Wall, Enemy.Actor });
	}

	// ------------------------------------------------------------------
	// 4. 対象消失: 追尾中にターゲットが破棄されたら誘導を諦め、向きを変えず直進を続ける
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FHomingProjTestUnit Target = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(0, 3000, 0));

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		Missile->InitHoming(Shooter.Actor, Target.Actor, /*Speed=*/500.0f, /*TurnRate=*/90.0f);

		HomingProjTickWorld(World, 0.3f);
		TestTrue(TEXT("消失: 破棄前は追尾中"), Missile->IsHomingActive());
		const float YawWhileHoming = Missile->GetActorRotation().Yaw;
		TestTrue(FString::Printf(TEXT("消失: 破棄前に旋回が進行している(実測%.1f°)"), YawWhileHoming),
			YawWhileHoming > 10.0f);

		// ターゲット破棄(死亡時もCanLockOn(単一API)がfalseになり同経路で誘導解除される)
		World->DestroyActor(Target.Actor);

		HomingProjTickWorld(World, 0.1f); // 消失検知Tick
		TestFalse(TEXT("消失: 対象破棄で誘導解除(再取得しない)"), Missile->IsHomingActive());
		const FRotator FrozenRotation = Missile->GetActorRotation();
		const FVector LocationAtLoss = Missile->GetActorLocation();

		HomingProjTickWorld(World, 0.5f);
		TestTrue(TEXT("消失: 以後は向きを変えない(直進)"),
			Missile->GetActorRotation().Equals(FrozenRotation, 0.1f));
		const FVector Moved = Missile->GetActorLocation() - LocationAtLoss;
		TestTrue(FString::Printf(TEXT("消失: 凍結した機首方向へ直進を続ける(移動量%.1fcm)"), Moved.Size()),
			Moved.Size() > 200.0f &&
			(Moved.GetSafeNormal() | FrozenRotation.Vector()) > 0.999f);
		TestTrue(TEXT("消失: 直進中は爆発しない(寿命内)"), IsValid(Missile) && !Missile->HasExploded());

		HomingProjDestroyAll(World, { Shooter.Actor, Missile });
	}

	// ------------------------------------------------------------------
	// 追加(条件5の防御実装): 味方ターゲットはInitHomingで受理されず直進弾になる(#161対策)
	// ------------------------------------------------------------------
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FHomingProjTestUnit Ally = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 1000, 0));

		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		Missile->InitHoming(Shooter.Actor, Ally.Actor, /*Speed=*/0.0f, /*TurnRate=*/90.0f);
		TestFalse(TEXT("味方除外: 味方はCanLockOn(B-2)で弾かれ追尾対象にならない"), Missile->IsHomingActive());

		HomingProjTickWorld(World, 0.3f);
		TestTrue(TEXT("味方除外: 味方方向へは旋回しない(Yaw=0のまま)"),
			FMath::IsNearlyZero(Missile->GetActorRotation().Yaw, 0.1f));

		HomingProjDestroyAll(World, { Shooter.Actor, Ally.Actor, Missile });
	}

	// Physical-metre weapon data must use the same flight/blast unit contract
	// as the visual homing path. 100m/s receives the established 1.75x pass.
	{
		const FHomingProjTestUnit Shooter = SpawnHomingProjTestUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		AActor* Wall = SpawnHomingProjWall(World, FVector(5000, 0, 0), 100.0f);
		const FHomingProjTestUnit Inside = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(4900, 2500, 0), 50.0f);
		const FHomingProjTestUnit Outside = SpawnHomingProjTestUnit(World, MSTeam::EnemyTeamId(), FVector(4900, 4000, 0), 50.0f);
		UMSHomingWeaponData* Physical = NewObject<UMSHomingWeaponData>(Shooter.Actor);
		Physical->RangeWorldScale = 1.0f;
		Physical->ProjectileSpeed = 100.0f;
		Physical->SplashRadius = 30.0f;
		Physical->Power = 100;
		Physical->SplashDamage = 100;
		AMSHomingProjectile* Missile = SpawnHomingProjectileActor(World, FVector::ZeroVector, FRotator::ZeroRotator);
		Missile->InitFromWeaponData(Physical, Shooter.Actor, nullptr);
		const TWeakObjectPtr<AMSHomingProjectile> MissilePtr = Missile;
		HomingProjTickWorld(World, 0.1f);
		TestTrue(TEXT("physical 100m/s homing projectile travels 17.5m in 0.1s after explosive speed pass"),
			FMath::IsNearlyEqual(Missile->GetActorLocation().X, 1750.0f, 20.0f));
		HomingProjTickWorld(World, 0.3f);
		TestFalse(TEXT("physical-unit homing projectile impacts the wall"), MissilePtr.IsValid());
		TestTrue(TEXT("25m bystander receives the 30m physical splash"), Inside.Vital->GetArmor() < MaxArmor);
		TestEqual(TEXT("40m bystander remains outside the 30m physical splash"), Outside.Vital->GetArmor(), MaxArmor);
		HomingProjDestroyAll(World, { Shooter.Actor, Wall, Inside.Actor, Outside.Actor });
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P4-10] AMSHomingProjectile 誘導ミサイルActorテスト完了"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
