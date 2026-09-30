// Copyright NeoMSGO Project. All Rights Reserved.
//
// P2-4/P2-5 検証: UMSCombatStatics(爆風ユーティリティ/ヘッドショット判定)テスト。
// falloff(内側15%は1.0/外周でMinFalloff/半径外0)/ 二重適用防止(複数コライダー)/
// 本拠点コア遮蔽判定(壁越し爆風防止)/ 直撃+爆風加算(と除外オプション)を検証する。
// P2-5: 頭部ヒット/胴体ヒット(頭部候補なし)/頭部近傍ヒット(胴体コライダー越しでも頭部候補が
// トレース線分と交差していればHS扱い、のUnity版仕様)を検証する。
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Combat.CombatStatics"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Combat.CombatStatics"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "NeoMSGO.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCombatStaticsTest,
	"NeoMSGO.Combat.CombatStatics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** GE適用・空間クエリにはワールド登録済みASC/コリジョンが必要なため使い捨てGameワールドを作る */
	struct FCombatStaticsTestWorld
	{
		UWorld* World = nullptr;

		FCombatStaticsTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSCombatStaticsTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FCombatStaticsTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** 陣営+ASC+VitalSetを持つ最小テストユニット(AMSUnitBaseはAbstractのため代替構成) */
	struct FCombatStaticsTestUnit
	{
		AMSTeamAgentActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UMSAttributeSet_Vital* Vital = nullptr;
	};

	/** WeaponTraceチャンネルにヒットする球コライダーをアクターへ追加する */
	USphereComponent* AddWeaponTraceSphere(AActor* Owner, const FName Name, const float SphereRadius,
		const FName ProfileName = NAME_None)
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(Owner, Name);
		Sphere->SetupAttachment(Owner->GetRootComponent());
		Sphere->SetSphereRadius(SphereRadius);
		if (ProfileName != NAME_None)
		{
			// MSCore / MSStructure 等のプロファイル(DefaultEngine.ini P0-5)をそのまま使う
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

	/** WeaponTraceチャンネルにヒットするカプセル。実機体のcapsule falloffを検証する。 */
	UCapsuleComponent* AddWeaponTraceCapsule(AActor* Owner, const FName Name,
		const float CapsuleRadius, const float CapsuleHalfHeight)
	{
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Owner, Name);
		Capsule->SetupAttachment(Owner->GetRootComponent());
		Capsule->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(MSCollision::WeaponTraceChannel, ECR_Block);
		Capsule->RegisterComponent();
		return Capsule;
	}

	/** 実マップのWater Brushと同じく、WeaponTraceをBlockするテスト用PhysicsVolume。 */
	APhysicsVolume* SpawnTracePhysicsVolume(UWorld* World, const FVector& Location, const bool bWaterVolume)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>(
			APhysicsVolume::StaticClass(), Location, FRotator::ZeroRotator, Params);
		Volume->bWaterVolume = bWaterVolume;

		UBoxComponent* Bounds = NewObject<UBoxComponent>(Volume, TEXT("TraceVolumeBounds"));
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

	FCombatStaticsTestUnit SpawnUnit(UWorld* World, const FGenericTeamId TeamId, const FVector& Location,
		const float ColliderRadius = 50.0f, const FName ColliderProfile = NAME_None)
	{
		FCombatStaticsTestUnit Unit;

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
			AddWeaponTraceSphere(Unit.Actor, TEXT("TestCollider"), ColliderRadius, ColliderProfile);
		}

		Unit.ASC = NewObject<UAbilitySystemComponent>(Unit.Actor, TEXT("TestASC"));
		Unit.ASC->RegisterComponent();
		Unit.Vital = NewObject<UMSAttributeSet_Vital>(Unit.Actor, TEXT("TestVital"));
		Unit.ASC->AddSpawnedAttribute(Unit.Vital);
		Unit.ASC->InitAbilityActorInfo(Unit.Actor, Unit.Actor);

		return Unit;
	}

	/** コリジョンだけ持つ障害物(壁)アクター。ASC無し=ダメージ対象外 */
	AActor* SpawnWall(UWorld* World, const FVector& Location, const float SphereRadius)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Wall, TEXT("WallRoot"));
		Wall->SetRootComponent(Root);
		Root->RegisterComponent();
		Wall->SetActorLocation(Location);
		AddWeaponTraceSphere(Wall, TEXT("WallCollider"), SphereRadius, MSCollision::StructureProfileName);
		return Wall;
	}

	/**
	 * セクション間のテスト分離: 空間クエリ(SphereOverlap/LineTrace)を使うテストは
	 * 前セクションの残留アクターが混入するため、各セクション末尾で必ず破棄する。
	 */
	void DestroyAll(UWorld* World, std::initializer_list<AActor*> Actors)
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				World->DestroyActor(Actor);
			}
		}
	}

	/**
	 * P2-5用: 胴体コライダー(任意)+HeadHitboxコライダー(任意)を持つテスト機体アクター。
	 * AMSUnitBaseはAbstractのため、命中判定に必要な最小構成(WeaponTraceにBlockする球)のみで代替する。
	 */
	AActor* SpawnHeadshotTestActor(UWorld* World, const FVector& Location,
		const float TorsoRadius, const bool bWithHead, const FVector& HeadRelativeLocation, const float HeadRadius = 20.0f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Body = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Body, TEXT("BodyRoot"));
		Body->SetRootComponent(Root);
		Root->RegisterComponent();
		Body->SetActorLocation(Location);

		if (TorsoRadius > 0.0f)
		{
			AddWeaponTraceSphere(Body, TEXT("Torso"), TorsoRadius);
		}
		if (bWithHead)
		{
			// AMSUnitBase::HeadHitboxComponentName ("HeadHitbox") と同名のコンポーネントを用意する
			USphereComponent* Head = AddWeaponTraceSphere(Body, AMSUnitBase::HeadHitboxComponentName, HeadRadius);
			Head->SetRelativeLocation(HeadRelativeLocation);
		}

		return Body;
	}

	/** 直撃経路(GE_Damage)の適用ヘルパ(直撃+爆風加算テスト用) */
	void ApplyDirectDamage(const FCombatStaticsTestUnit& Source, const FCombatStaticsTestUnit& Target, const float Damage)
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Source.Actor, Source.Actor);
		const FGameplayEffectContextHandle ContextHandle(Context);

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), ContextHandle, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Damage);
		Source.ASC->ApplyGameplayEffectSpecToTarget(Spec, Target.ASC);
	}
}

bool FMSCombatStaticsTest::RunTest(const FString& Parameters)
{
	FCombatStaticsTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	const float MaxArmor = 5000.0f; // UMSAttributeSet_Vital コンストラクタ既定値
	const TArray<AActor*> NoIgnore;

	// ------------------------------------------------------------------
	// 0. WeaponTraceの水中回帰: water APhysicsVolumeだけを同一射線で透過し、
	//    通常PhysicsVolumeとMSStructure壁は後続の遮蔽物として残す。
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Shooter = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(-500, 0, 0), 0.0f);
		const FCombatStaticsTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(600, 0, 0));
		APhysicsVolume* Water = SpawnTracePhysicsVolume(World, FVector(250, 0, 0), /*bWaterVolume=*/true);

		const FMSHitscanResult WaterResult = UMSCombatStatics::ResolveHitscan(
			World, FVector::ZeroVector, FVector::ForwardVector, 1200.0f, Shooter.Actor);
		TestTrue(TEXT("水中射撃: water APhysicsVolumeはWeaponTraceの遮蔽物として無視される"),
			WaterResult.bBlockingHit);
		TestEqual(TEXT("水中射撃: water volume後の敵が最初の有効なHitActorになる"),
			WaterResult.Hit.GetActor(), static_cast<AActor*>(Enemy.Actor));
		TestTrue(TEXT("水中射撃: water volume後の敵へCanDamageが成立する"), WaterResult.bCanDamage);
		TestTrue(TEXT("水中射撃: water volumeだけが専用例外になる"),
			UMSCombatStatics::ShouldIgnoreWeaponTraceOccluder(Water));
		DestroyAll(World, { Shooter.Actor, Enemy.Actor, Water });

		const FCombatStaticsTestUnit AllyShooter = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(-500, 0, 0), 0.0f);
		const FCombatStaticsTestUnit Ally = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(600, 0, 0));
		APhysicsVolume* WaterBeforeAlly = SpawnTracePhysicsVolume(World, FVector(250, 0, 0), /*bWaterVolume=*/true);
		const FMSHitscanResult AllyResult = UMSCombatStatics::ResolveHitscan(
			World, FVector::ZeroVector, FVector::ForwardVector, 1200.0f, AllyShooter.Actor);
		TestEqual(TEXT("水中射撃: water volume後の味方も後続HitActorとして返る"),
			AllyResult.Hit.GetActor(), static_cast<AActor*>(Ally.Actor));
		TestFalse(TEXT("水中射撃: 味方へのCanDamage=falseルールを保持する"), AllyResult.bCanDamage);
		DestroyAll(World, { AllyShooter.Actor, Ally.Actor, WaterBeforeAlly });

		const FCombatStaticsTestUnit DryShooter = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(-500, 0, 0), 0.0f);
		const FCombatStaticsTestUnit DryEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(600, 0, 0));
		APhysicsVolume* DryVolume = SpawnTracePhysicsVolume(World, FVector(250, 0, 0), /*bWaterVolume=*/false);

		const FMSHitscanResult DryResult = UMSCombatStatics::ResolveHitscan(
			World, FVector::ZeroVector, FVector::ForwardVector, 1200.0f, DryShooter.Actor);
		TestTrue(TEXT("水中射撃: 通常PhysicsVolumeは引き続き遮蔽する"), DryResult.bBlockingHit);
		TestEqual(TEXT("水中射撃: 通常PhysicsVolumeが敵より先にHitActorになる"),
			DryResult.Hit.GetActor(), static_cast<AActor*>(DryVolume));
		TestFalse(TEXT("水中射撃: 通常PhysicsVolumeはwater例外にならない"),
			UMSCombatStatics::ShouldIgnoreWeaponTraceOccluder(DryVolume));
		DestroyAll(World, { DryShooter.Actor, DryEnemy.Actor, DryVolume });

		const FCombatStaticsTestUnit WallShooter = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(-500, 0, 0), 0.0f);
		const FCombatStaticsTestUnit WallEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(800, 0, 0));
		APhysicsVolume* WaterBeforeWall = SpawnTracePhysicsVolume(World, FVector(250, 0, 0), /*bWaterVolume=*/true);
		AActor* Wall = SpawnWall(World, FVector(600, 0, 0), 50.0f);

		const FMSHitscanResult WallResult = UMSCombatStatics::ResolveHitscan(
			World, FVector::ZeroVector, FVector::ForwardVector, 1400.0f, WallShooter.Actor);
		TestTrue(TEXT("水中射撃: water volume後の壁は遮蔽する"), WallResult.bBlockingHit);
		TestEqual(TEXT("水中射撃: water volume後は壁が敵より先にHitActorになる"),
			WallResult.Hit.GetActor(), Wall);
		// NoTeam actors remain eligible under CanDamage; HitActor above proves
		// occlusion. Whether a wall owns a damage receiver is a separate concern.
		TestTrue(TEXT("水中射撃: NoTeamの既存CanDamage契約を保持する"), WallResult.bCanDamage);
		DestroyAll(World, { WallShooter.Actor, WallEnemy.Actor, WaterBeforeWall, Wall });
	}

	// ------------------------------------------------------------------
	// 1. falloff計算(純関数): 内側15%は全量、外周で下限、半径外/不正半径は0
	// ------------------------------------------------------------------
	{
		TestEqual(TEXT("falloff: 中心(dist=0)は1.0"),
			UMSCombatStatics::CalcSplashFalloff(0.0f, 1000.0f), 1.0f);
		TestEqual(TEXT("falloff: 内側15%は全量1.0"),
			UMSCombatStatics::CalcSplashFalloff(100.0f, 1000.0f), 1.0f);
		TestEqual(TEXT("falloff: 内側境界(15%)も全量1.0"),
			UMSCombatStatics::CalcSplashFalloff(150.0f, 1000.0f), 1.0f);
		TestTrue(TEXT("falloff: 内側境界から外周まで線形(575cmは0.55)"),
			FMath::IsNearlyEqual(UMSCombatStatics::CalcSplashFalloff(575.0f, 1000.0f), 0.55f, KINDA_SMALL_NUMBER));
		TestEqual(TEXT("falloff: Radius到達で通常下限0.1"),
			UMSCombatStatics::CalcSplashFalloff(1000.0f, 1000.0f), 0.1f);
		TestEqual(TEXT("falloff: Radius以遠は0"),
			UMSCombatStatics::CalcSplashFalloff(1001.0f, 1000.0f), 0.0f);
		TestEqual(TEXT("falloff: 不正半径(0以下)は0"),
			UMSCombatStatics::CalcSplashFalloff(100.0f, 0.0f), 0.0f);
		TestEqual(TEXT("falloff: CBZ等の明示下限0.2を適用"),
			UMSCombatStatics::CalcSplashFalloff(1000.0f, 1000.0f, 0.2f), 0.2f);
		TestEqual(TEXT("falloff: DP用下限0.0なら外周0"),
			UMSCombatStatics::CalcSplashFalloff(1000.0f, 1000.0f, 0.0f), 0.0f);
	}

	// ------------------------------------------------------------------
	// 2. 爆風適用の数値検証(falloffがSetByCaller経由でExecutionに乗ること)
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Attacker = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), /*Collider=*/0.0f);
		const FCombatStaticsTestUnit NearEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(0, 0, 0));     // 表面50cm(内側15%で全量)
		const FCombatStaticsTestUnit MidEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(700, 0, 0), 200.0f); // 表面500cm
		const FCombatStaticsTestUnit CapsuleEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(900, 0, 0), 0.0f);
		AddWeaponTraceCapsule(CapsuleEnemy.Actor, TEXT("TestCapsule"), 150.0f, 250.0f); // 表面750cm
		const FCombatStaticsTestUnit FarEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(1250, 0, 0), 200.0f); // 表面1050cm(圏外)

		const int32 Applied = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, /*Radius=*/1000.0f,
			/*BaseDamage=*/1000.0f, NoIgnore, Attacker.Actor, /*DownPoint=*/50.0f);

		TestEqual(TEXT("爆風: 圏内3体のみ適用(圏外は対象外)"), Applied, 3);
		TestEqual(TEXT("爆風: 対象中心から表面まで50cmはfalloff1.0 → 1000ダメージ"), NearEnemy.Vital->GetArmor(), MaxArmor - 1000.0f);
		TestTrue(TEXT("爆風: Actor中心700cmではなく表面500cmでfalloff約0.629 → 約629ダメージ"),
			FMath::IsNearlyEqual(MidEnemy.Vital->GetArmor(), MaxArmor - 629.4118f, 1.0f));
		TestTrue(TEXT("爆風: DPは最低保証なしで距離減衰(50×0.588)"),
			FMath::IsNearlyEqual(MidEnemy.Vital->GetDownPoint(), 29.4118f, 1.0f));
		TestTrue(TEXT("爆風: capsule表面750cmでfalloff約0.365(中心900cmの過減衰を避ける)"),
			FMath::IsNearlyEqual(CapsuleEnemy.Vital->GetArmor(), MaxArmor - 364.7059f, 1.0f));
		TestEqual(TEXT("爆風: 圏外は無傷"), FarEnemy.Vital->GetArmor(), MaxArmor);

		DestroyAll(World, { Attacker.Actor, NearEnemy.Actor, MidEnemy.Actor, CapsuleEnemy.Actor, FarEnemy.Actor });
	}

	// ------------------------------------------------------------------
	// 3. 二重適用防止: 同一アクターの複数コライダー(カプセル+HeadHitbox等)でも適用1回
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Attacker = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FCombatStaticsTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(300, 0, 0));
		AddWeaponTraceSphere(Enemy.Actor, TEXT("HeadHitbox"), 30.0f);   // 2個目
		AddWeaponTraceSphere(Enemy.Actor, TEXT("ShieldHitbox"), 40.0f); // 3個目

		const int32 Applied = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, NoIgnore, Attacker.Actor);

		TestEqual(TEXT("二重適用防止: コライダー3個でも適用は1回"), Applied, 1);
		// 表面距離=250cm → falloff=1-(0.9×100/850)=0.8941176。GEは1回だけ。
		TestTrue(TEXT("二重適用防止: 表面距離falloffでダメージは1回分のみ"),
			FMath::IsNearlyEqual(Enemy.Vital->GetArmor(), MaxArmor - 894.1176f, 1.0f));

		DestroyAll(World, { Attacker.Actor, Enemy.Actor });
	}

	// ------------------------------------------------------------------
	// 4. 直撃+爆風の加算(既定)と DirectHitActor 除外オプション
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Attacker = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		const FCombatStaticsTestUnit Enemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector::ZeroVector, 0.0f);

		// 直撃1000 → その後、爆心=直撃点の爆風(falloff1.0で+1000)が加算される(Unity版仕様)
		ApplyDirectDamage(Attacker, Enemy, 1000.0f);
		const int32 AppliedDefault = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, NoIgnore, Attacker.Actor, 0.0f, /*DirectHitActor=*/Enemy.Actor, /*bExclude=*/false);
		TestEqual(TEXT("直撃+爆風: 既定は直撃対象にも爆風が乗る"), AppliedDefault, 1);
		TestEqual(TEXT("直撃+爆風: 1000+1000=2000の加算"), Enemy.Vital->GetArmor(), MaxArmor - 2000.0f);

		// 除外オプション有効時は爆風がスキップされる
		const int32 AppliedExcluded = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, NoIgnore, Attacker.Actor, 0.0f, Enemy.Actor, /*bExclude=*/true);
		TestEqual(TEXT("直撃除外: bExcludeDirectHitActor=trueで適用0"), AppliedExcluded, 0);
		TestEqual(TEXT("直撃除外: Armor変化なし"), Enemy.Vital->GetArmor(), MaxArmor - 2000.0f);

		// overlapに含まれない直撃対象でも、除外指定が無ければ距離0で追加適用される。
		TArray<AActor*> IgnoreDirect;
		IgnoreDirect.Add(Enemy.Actor);
		const int32 AppliedIgnored = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, IgnoreDirect, Attacker.Actor, 0.0f, Enemy.Actor, /*bExclude=*/false);
		TestEqual(TEXT("直撃対象: IgnoreActors指定時はoverlap補完されず適用0"), AppliedIgnored, 0);
		TestEqual(TEXT("直撃対象: IgnoreActors指定時はArmor不変"), Enemy.Vital->GetArmor(), MaxArmor - 2000.0f);

		// overlapに含まれる直撃対象は明示的な除外で確実にスキップされる。
		const FCombatStaticsTestUnit OverlapEnemy = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(300, 0, 0));
		ApplyDirectDamage(Attacker, OverlapEnemy, 1000.0f);
		const int32 AppliedExcludedOverlap = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector,
			1000.0f, 1000.0f, NoIgnore, Attacker.Actor, 0.0f, OverlapEnemy.Actor, /*bExclude=*/true);
		TestEqual(TEXT("直撃対象: overlap済みでもbExcludeDirectHitActor=trueで適用0"), AppliedExcludedOverlap, 0);
		TestEqual(TEXT("直撃対象: overlap済み除外時はArmor不変"), OverlapEnemy.Vital->GetArmor(), MaxArmor - 1000.0f);

		DestroyAll(World, { Attacker.Actor, Enemy.Actor, OverlapEnemy.Actor });
	}

	// ------------------------------------------------------------------
	// 5. 本拠点コア遮蔽判定: 壁(MSStructure)越しの爆風はコアに届かない
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Attacker = SpawnUnit(World, MSTeam::PlayerTeamId(), FVector(0, 0, 5000), 0.0f);
		// コア: MSCoreプロファイルのコライダーを持つ(遮蔽判定の対象になる条件)
		const FCombatStaticsTestUnit Core = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(800, 0, 0),
			/*ColliderRadius=*/100.0f, MSCollision::CoreProfileName);
		// 壁: 爆心→18m機体用コア照準高のトレース中点を塞ぐ。
		AActor* Wall = SpawnWall(World,
			FVector(400, 0, UMSCombatStatics::CoreOcclusionAimHeightOffset * 0.5f),
			/*SphereRadius=*/100.0f);

		// 遮蔽判定単体
		TestTrue(TEXT("遮蔽: 壁があるとコアは遮蔽される"),
			UMSCombatStatics::IsHomeBaseCoreOccluded(World, FVector::ZeroVector, Core.Actor->GetActorLocation(),
				Core.Actor, NoIgnore));

		// 遮蔽中の爆風はコアに届かない(壁自体はASC無しで対象外)
		const int32 AppliedOccluded = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, NoIgnore, Attacker.Actor);
		TestEqual(TEXT("遮蔽: 壁越し爆風はコアへ適用されない"), AppliedOccluded, 0);
		TestEqual(TEXT("遮蔽: コアは無傷"), Core.Vital->GetArmor(), MaxArmor);

		// 壁を除去すると遮蔽解除 → 爆風が通る
		Wall->SetActorEnableCollision(false);
		TestFalse(TEXT("遮蔽解除: 壁が無ければ遮蔽されない"),
			UMSCombatStatics::IsHomeBaseCoreOccluded(World, FVector::ZeroVector, Core.Actor->GetActorLocation(),
				Core.Actor, NoIgnore));

		const int32 AppliedClear = UMSCombatStatics::ApplySplashDamage(World, FVector::ZeroVector, 1000.0f,
			1000.0f, NoIgnore, Attacker.Actor);
		TestEqual(TEXT("遮蔽解除: コアへ適用される"), AppliedClear, 1);
		// 表面距離=700cm → falloff約0.4176 → 約418ダメージ
		TestTrue(TEXT("遮蔽解除: 表面距離falloff約0.4176で約418ダメージ"),
			FMath::IsNearlyEqual(Core.Vital->GetArmor(), MaxArmor - 417.647f, 1.0f));

		DestroyAll(World, { Attacker.Actor, Core.Actor, Wall });
	}

	// ------------------------------------------------------------------
	// 6. コア遮蔽の水中回帰: water単独は遮蔽せず、水の後ろの壁は遮蔽する。
	// ------------------------------------------------------------------
	{
		const FCombatStaticsTestUnit Core = SpawnUnit(World, MSTeam::EnemyTeamId(), FVector(800, 0, 0),
			/*ColliderRadius=*/100.0f, MSCollision::CoreProfileName);
		const FVector TraceStart = FVector::ZeroVector;
		const FVector CoreLocation = Core.Actor->GetActorLocation();
		const float TraceHeight = UMSCombatStatics::CoreOcclusionAimHeightOffset;

		APhysicsVolume* Water = SpawnTracePhysicsVolume(
			World, FVector(250, 0, TraceHeight * 0.3125f), /*bWaterVolume=*/true);
		TestFalse(TEXT("コア遮蔽: water volumeだけでは遮蔽しない"),
			UMSCombatStatics::IsHomeBaseCoreOccluded(World, TraceStart, CoreLocation, Core.Actor, NoIgnore));

		AActor* Wall = SpawnWall(World, FVector(600, 0, TraceHeight * 0.75f), 100.0f);
		TestTrue(TEXT("コア遮蔽: water volumeの後ろの壁は遮蔽する"),
			UMSCombatStatics::IsHomeBaseCoreOccluded(World, TraceStart, CoreLocation, Core.Actor, NoIgnore));

		DestroyAll(World, { Core.Actor, Water, Wall });
	}

	// ------------------------------------------------------------------
	// 7. ヘッドショット判定(P2-5): 頭部ヒット/胴体ヒット/頭部近傍ヒット
	// ------------------------------------------------------------------
	{
		// 6-1. 頭部ヒット: 命中コンポーネント自身がHeadHitbox → HS確定
		{
			AActor* HeadOnly = SpawnHeadshotTestActor(World, FVector(0, 0, 0), /*TorsoRadius=*/0.0f,
				/*bWithHead=*/true, FVector::ZeroVector, /*HeadRadius=*/20.0f);

			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(-1000, 0, 0), FVector(1000, 0, 0),
				MSCollision::WeaponTraceChannel, FCollisionQueryParams(SCENE_QUERY_STAT(MSHeadshotTest), false));
			TestTrue(TEXT("頭部ヒット: トレースがHeadHitboxに命中する"), bHit);
			TestTrue(TEXT("頭部ヒット: 命中コンポーネントがHeadHitboxならHS確定"),
				UMSCombatStatics::IsHeadshotHit(Hit, FVector(-1000, 0, 0), FVector(1000, 0, 0)));

			DestroyAll(World, { HeadOnly });
		}

		// 6-2. 胴体ヒット: HeadHitboxが存在しない(頭部候補なし)→ HSではない
		{
			AActor* TorsoOnly = SpawnHeadshotTestActor(World, FVector(0, 0, 0), /*TorsoRadius=*/100.0f,
				/*bWithHead=*/false, FVector::ZeroVector);

			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(-1000, 0, 0), FVector(1000, 0, 0),
				MSCollision::WeaponTraceChannel, FCollisionQueryParams(SCENE_QUERY_STAT(MSHeadshotTest), false));
			TestTrue(TEXT("胴体ヒット: トレースが胴体コライダーに命中する"), bHit);
			TestFalse(TEXT("胴体ヒット: HeadHitboxが存在しなければHSではない"),
				UMSCombatStatics::IsHeadshotHit(Hit, FVector(-1000, 0, 0), FVector(1000, 0, 0)));

			DestroyAll(World, { TorsoOnly });
		}

		// 6-3. 頭部近傍ヒット: 最近傍ヒットは胴体だが、同一アクター配下のHeadHitboxがトレース線分と
		//      交差している(=RaycastAllなら候補に挙がっていた)ためHS扱い(Unity版の特殊ロジック再現)
		{
			// 胴体(半径100、原点)がトレースを先にブロックするが、HeadHitboxは胴体の奥(同一直線上)に重なる
			AActor* TorsoWithHead = SpawnHeadshotTestActor(World, FVector(0, 0, 0), /*TorsoRadius=*/100.0f,
				/*bWithHead=*/true, /*HeadRelativeLocation=*/FVector(20, 0, 0), /*HeadRadius=*/20.0f);

			const FVector TraceStart(-1000, 0, 0);
			const FVector TraceEnd(1000, 0, 0);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd,
				MSCollision::WeaponTraceChannel, FCollisionQueryParams(SCENE_QUERY_STAT(MSHeadshotTest), false));
			TestTrue(TEXT("頭部近傍ヒット: トレースは胴体コライダーで停止する(最近傍ヒットは胴体)"), bHit);
			TestTrue(TEXT("頭部近傍ヒット: 命中コンポーネントは胴体自身(HeadHitboxではない)"),
				Hit.Component.IsValid() && Hit.Component->GetFName() != AMSUnitBase::HeadHitboxComponentName);
			TestTrue(TEXT("頭部近傍ヒット: 胴体コライダー越しでも頭部候補があればHS扱い(Unity版仕様)"),
				UMSCombatStatics::IsHeadshotHit(Hit, TraceStart, TraceEnd));

			DestroyAll(World, { TorsoWithHead });
		}

		// 6-4. 頭部が同アクター上にあってもトレース線分から外れていればHSにならない(誤判定防止)
		{
			AActor* TorsoWithOffHead = SpawnHeadshotTestActor(World, FVector(0, 0, 0), /*TorsoRadius=*/100.0f,
				/*bWithHead=*/true, /*HeadRelativeLocation=*/FVector(0, 500, 0), /*HeadRadius=*/20.0f);

			const FVector TraceStart(-1000, 0, 0);
			const FVector TraceEnd(1000, 0, 0);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd,
				MSCollision::WeaponTraceChannel, FCollisionQueryParams(SCENE_QUERY_STAT(MSHeadshotTest), false));
			TestTrue(TEXT("頭部オフセット: トレースは胴体コライダーに命中する"), bHit);
			TestFalse(TEXT("頭部オフセット: トレース線分から外れたHeadHitboxはHSにならない"),
				UMSCombatStatics::IsHeadshotHit(Hit, TraceStart, TraceEnd));

			DestroyAll(World, { TorsoWithOffHead });
		}
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P2-4/P2-5] UMSCombatStatics 爆風ユーティリティ/ヘッドショット判定テスト完了"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
