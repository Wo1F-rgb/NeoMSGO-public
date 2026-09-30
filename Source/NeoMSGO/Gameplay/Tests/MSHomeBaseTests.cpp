// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSDestructibleTarget.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSHomeBase.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Player/MSPlayerCharacter.h"
#include "Data/MSWeaponData.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/MSWeaponComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseP72Test, "NeoMSGO.Gameplay.HomeBase.P7_2",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseWeaponDamageTest,
	"NeoMSGO.Gameplay.HomeBase.WeaponDamageRegression",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseGaugeDamageTest,
	"NeoMSGO.Gameplay.HomeBase.GaugeTracksArmorDamage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseTypeTest, "NeoMSGO.Gameplay.HomeBase.Types",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseAuthoredExteriorTest, "NeoMSGO.Gameplay.HomeBase.AuthoredExterior",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseLabelTest, "NeoMSGO.Gameplay.HomeBase.Labels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomeBaseFactionReplicationTest, "NeoMSGO.Gameplay.HomeBase.FactionReplication",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSHomeBaseTestP72
{
	struct FScopedHomeBaseWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHomeBaseTestP72World"));
		FScopedHomeBaseWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedHomeBaseWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	UStaticMeshComponent* FindBuildingPart(const AMSHomeBase* Base, const FName Name)
	{
		TArray<UStaticMeshComponent*> Components;
		Base->GetComponents(Components);
		for (UStaticMeshComponent* Component : Components)
		{
			if (Component && Component->GetFName() == Name) return Component;
		}
		return nullptr;
	}

	bool TraceWeapon(UWorld* World, const FVector& Start, const FVector& End, FHitResult& Hit)
	{
		return World->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel1);
	}

	void ApplyDamage(AMSDestructibleTarget* Attacker, AMSDestructibleTarget* Target, const float Amount)
	{
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Attacker->GetAbilitySystemComponent()->MakeEffectContext(), 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Amount);
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
		Attacker->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(Spec, Target->GetAbilitySystemComponent());
	}

	void ApplyLethalDamage(AMSDestructibleTarget* Attacker, AMSDestructibleTarget* Target)
	{
		ApplyDamage(Attacker, Target, 100000.0f);
	}

	AMSDestructibleTarget* SpawnAttacker(UWorld* World, const FVector& Location, const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSDestructibleTarget* Attacker = World->SpawnActor<AMSDestructibleTarget>(
			AMSDestructibleTarget::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Attacker) Attacker->SetGenericTeamId(Team);
		return Attacker;
	}

	struct FScopedHomeBaseBattleWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHomeBaseWeaponDamageWorld"));
		AMSGameMode* GameMode = nullptr;
		AMSHomeBase* PlayerBase = nullptr;
		AMSHomeBase* EnemyBase = nullptr;

		FScopedHomeBaseBattleWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			GameMode = World->SpawnActor<AMSGameMode>();
			PlayerBase = World->SpawnActor<AMSHomeBase>(FVector::ZeroVector, FRotator::ZeroRotator);
			EnemyBase = World->SpawnActor<AMSHomeBase>(FVector(30000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
			EnemyBase->SetGenericTeamId(MSTeam::EnemyTeamId());
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			GameMode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			GameMode->StartPlay();
		}

		~FScopedHomeBaseBattleWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSPlayerCharacter* SpawnShooter(UWorld* World, const FVector& Location, const FGenericTeamId Team,
		const int32 Power, UGA_Fire_Hitscan*& OutAbility)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Shooter = World->SpawnActor<AMSPlayerCharacter>(Location, FRotator::ZeroRotator, Params);
		Shooter->SetGenericTeamId(Team);
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Shooter);
		Weapon->MagazineSize = 4;
		Weapon->FireInterval = 0.0f;
		Weapon->Range = 30.0f;
		Weapon->Power = Power;
		Shooter->GetWeaponComponent()->InitializeSlots({ Weapon });
		OutAbility = NewObject<UGA_Fire_Hitscan>(Shooter);
		return Shooter;
	}

	AMSBotCharacter* SpawnBotShooter(UWorld* World, const FVector& Location, const FGenericTeamId Team,
		const int32 Power, UGA_Fire_Hitscan*& OutAbility)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSBotCharacter* Shooter = World->SpawnActor<AMSBotCharacter>(Location, FRotator::ZeroRotator, Params);
		Shooter->SetGenericTeamId(Team);
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Shooter);
		Weapon->MagazineSize = 1;
		Weapon->FireInterval = 0.0f;
		Weapon->Range = 30.0f;
		Weapon->Power = Power;
		Shooter->GetWeaponComponent()->InitializeSlots({ Weapon });
		OutAbility = NewObject<UGA_Fire_Hitscan>(Shooter);
		return Shooter;
	}

	AMSHomeBase* SpawnTypedBase(UWorld* World, const EMSHomeBaseType Type, const FVector& Location)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		AMSHomeBase* Base = World->SpawnActorDeferred<AMSHomeBase>(
			AMSHomeBase::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Base)
		{
			Base->SetHomeBaseType(Type);
			Base->FinishSpawning(Transform);
		}
		return Base;
	}
}

bool FMSHomeBaseP72Test::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseWorld Scope;
	UWorld* World = Scope.World;
	AMSHomeBase* Base = World->SpawnActor<AMSHomeBase>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("Home base spawns"), Base);
	AMSDestructibleTarget* Core = Base ? Base->GetCoreTarget() : nullptr;
	TestNotNull(TEXT("Authority home base spawns its core"), Core);
	if (!Base || !Core) return false;

	TestTrue(TEXT("Capture is structurally locked"), Base->IsCaptureLocked());
	TestEqual(TEXT("Fixed owner is Player"), Base->GetCurrentOwner(), EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Fixed progress is +1"), Base->GetProgress(), 1.0f);
	TestTrue(TEXT("Living owned home base allows deployment"), Base->CanDeployFor(MSTeam::PlayerTeamId()));
	Base->TickCaptureLogic(0, 20, 100.0f);
	TestEqual(TEXT("Enemy presence cannot change owner"), Base->GetCurrentOwner(), EMSCaptureOwner::TeamPlayer);
	TestEqual(TEXT("Enemy presence cannot change progress"), Base->GetProgress(), 1.0f);

	TestNotNull(TEXT("Core exposes a valid ASC"), Core->GetAbilitySystemComponent());
	TestNotNull(TEXT("Core owns VitalSet"), Core->GetVitalSet());
	TestEqual(TEXT("Fortress Armor initializes through GE_InitStats"), Core->GetVitalSet()->GetArmor(), 100000.0f);
	TestEqual(TEXT("Fortress MaxArmor initializes through GE_InitStats"), Core->GetVitalSet()->GetMaxArmor(), 100000.0f);
	TestEqual(TEXT("Core extent follows MSScale"), Core->GetCoreCollision()->GetUnscaledBoxExtent(), FVector(150.0f * MSGO::MSScale));
	TestEqual(TEXT("Core collision center follows MSScale"), Core->GetCoreCollision()->GetRelativeLocation(), FVector(0, 0, 150.0f * MSGO::MSScale));
	TestEqual(TEXT("Core uses MSCore profile"), Core->GetCoreCollision()->GetCollisionProfileName(), FName(TEXT("MSCore")));
	TestEqual(TEXT("Core team is converted from CaptureOwner"), Core->GetGenericTeamId(),
		UMSTeamStatics::CaptureOwnerToTeamId(EMSCaptureOwner::TeamPlayer));

	AMSDestructibleTarget* EnemyViewer = World->SpawnActor<AMSDestructibleTarget>(FVector(3000, 0, 0) * MSGO::MSScale, FRotator::ZeroRotator);
	EnemyViewer->SetGenericTeamId(MSTeam::EnemyTeamId());
	AMSDestructibleTarget* FriendlyViewer = World->SpawnActor<AMSDestructibleTarget>(FVector(3000, 500, 0) * MSGO::MSScale, FRotator::ZeroRotator);
	FriendlyViewer->SetGenericTeamId(MSTeam::PlayerTeamId());
	TestTrue(TEXT("Living enemy core can be locked"), UMSTeamStatics::CanLockOn(EnemyViewer, Core));
	TestFalse(TEXT("Friendly core can never be locked"), UMSTeamStatics::CanLockOn(FriendlyViewer, Core));

	const TArray<FName> PartNames = { TEXT("Wall_Back"), TEXT("Wall_Left"), TEXT("Wall_Right"),
		TEXT("Wall_FrontLeft"), TEXT("Wall_FrontRight"), TEXT("Lintel"), TEXT("Roof"), TEXT("Wall_Screen") };
	for (const FName Name : PartNames)
	{
		UStaticMeshComponent* Part = MSHomeBaseTestP72::FindBuildingPart(Base, Name);
		TestNotNull(*FString::Printf(TEXT("Building part exists: %s"), *Name.ToString()), Part);
		if (Part) TestEqual(*FString::Printf(TEXT("%s uses MSStructure"), *Name.ToString()),
			Part->GetCollisionProfileName(), FName(TEXT("MSStructure")));
	}
	TestEqual(TEXT("Front segment width follows MSScale"), MSHomeBaseTestP72::FindBuildingPart(Base, TEXT("Wall_FrontLeft"))->GetRelativeScale3D().Y * 100.0, 500.0 * MSGO::MSScale);
	TestEqual(TEXT("Screen dimensions follow MSScale"), MSHomeBaseTestP72::FindBuildingPart(Base, TEXT("Wall_Screen"))->GetRelativeScale3D(), FVector(0.6, 7.0, 3.5) * MSGO::MSScale);
	const UStaticMeshComponent* Lintel = MSHomeBaseTestP72::FindBuildingPart(Base, TEXT("Lintel"));
	const float EntranceClearance = Lintel->GetRelativeLocation().Z - Lintel->GetRelativeScale3D().Z * 50.0f;
	TestTrue(TEXT("Scaled entrance clears the 1650 UU navigation agent"), EntranceClearance > 1650.0f);

	FHitResult Hit;
	MSHomeBaseTestP72::TraceWeapon(World, FVector(-2000, 0, 150) * MSGO::MSScale, Core->GetLockOnAimPoint(), Hit);
	TestEqual(TEXT("Back outdoor shot is blocked by structure"), Hit.GetActor(), static_cast<AActor*>(Base));
	MSHomeBaseTestP72::TraceWeapon(World, FVector(0, 2000, 150) * MSGO::MSScale, Core->GetLockOnAimPoint(), Hit);
	TestEqual(TEXT("Side outdoor shot is blocked by structure"), Hit.GetActor(), static_cast<AActor*>(Base));
	MSHomeBaseTestP72::TraceWeapon(World, FVector(0, 0, 2000) * MSGO::MSScale, Core->GetLockOnAimPoint(), Hit);
	TestEqual(TEXT("Top outdoor shot is blocked by roof"), Hit.GetActor(), static_cast<AActor*>(Base));
	MSHomeBaseTestP72::TraceWeapon(World, FVector(2000, 0, 150) * MSGO::MSScale, Core->GetLockOnAimPoint(), Hit);
	TestEqual(TEXT("Entrance straight shot is blocked by screen"), Hit.GetActor(), static_cast<AActor*>(Base));
	MSHomeBaseTestP72::TraceWeapon(World, FVector(0, 500, 150) * MSGO::MSScale, Core->GetLockOnAimPoint(), Hit);
	TestEqual(TEXT("Inside around screen can hit core"), Hit.GetActor(), static_cast<AActor*>(Core));

	const FVector DestructionLocation = Base->GetActorLocation();
	MSHomeBaseTestP72::ApplyLethalDamage(EnemyViewer, Core);
	TestTrue(TEXT("Damage execution destroys core"), Base->IsHomeBaseDestroyed());
	TestFalse(TEXT("Destroyed home base rejects deployment"), Base->CanDeployFor(MSTeam::PlayerTeamId()));
	TestFalse(TEXT("Destroyed core cannot be locked"), UMSTeamStatics::CanLockOn(EnemyViewer, Core));
	TestEqual(TEXT("Sink target follows MSScale"), Base->GetSunkenTargetLocation(), DestructionLocation + FVector(0, 0, -200.0f * MSGO::MSScale));
	TestEqual(TEXT("Destruction event broadcasts once"), Base->GetDestructionBroadcastCount(), 1);
	MSHomeBaseTestP72::ApplyLethalDamage(EnemyViewer, Core);
	TestEqual(TEXT("Repeated lethal damage does not move sink target"), Base->GetSunkenTargetLocation(), DestructionLocation + FVector(0, 0, -200.0f * MSGO::MSScale));
	TestEqual(TEXT("Repeated lethal damage does not rebroadcast"), Base->GetDestructionBroadcastCount(), 1);

	for (int32 Index = 0; Index < 100; ++Index) Base->Tick(0.1f);
	TestEqual(TEXT("Base reaches exact sink target"), Base->GetActorLocation(), Base->GetSunkenTargetLocation());
	const double SettledZ = Base->GetActorLocation().Z;
	for (int32 Index = 0; Index < 100; ++Index) Base->Tick(0.1f);
	TestEqual(TEXT("Base Z remains fixed after settling"), Base->GetActorLocation().Z, SettledZ);
	return true;
}

bool FMSHomeBaseWeaponDamageTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseBattleWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	TestNotNull(TEXT("Battle uses AMSGameState"), State);
	TestNotNull(TEXT("Player base has a GAS core"), Scope.PlayerBase->GetCoreTarget());
	TestNotNull(TEXT("Enemy base has a GAS core"), Scope.EnemyBase->GetCoreTarget());
	if (!State || !Scope.PlayerBase->GetCoreTarget() || !Scope.EnemyBase->GetCoreTarget()) return false;

	TestEqual(TEXT("Battle starts with the documented 10000 player gauge"), State->PlayerGauge, 10000.0f);
	TestEqual(TEXT("Battle starts with the documented 10000 enemy gauge"), State->EnemyGauge, 10000.0f);
	TestEqual(TEXT("Enemy base exposes the configured enemy team"), Scope.EnemyBase->GetGenericTeamId(),
		MSTeam::EnemyTeamId());
	TestEqual(TEXT("Building hits forward to the same ASC as the core"),
		Scope.EnemyBase->GetAbilitySystemComponent(),
		Scope.EnemyBase->GetCoreTarget()->GetAbilitySystemComponent());

	// Symmetry regression: an enemy-controlled shooter can damage the player base exterior.
	UGA_Fire_Hitscan* EnemyAbility = nullptr;
	AMSBotCharacter* EnemyShooter = MSHomeBaseTestP72::SpawnBotShooter(
		Scope.World, FVector(8500.0f, 550.0f * MSGO::MSScale, 0.0f), MSTeam::EnemyTeamId(), 1000, EnemyAbility);
	const FVector PlayerWallShotOrigin(8000.0f, 550.0f * MSGO::MSScale, 400.0f * MSGO::MSScale);
	const FMSHitscanResult PlayerWallHit = UMSCombatStatics::ResolveHitscan(
		Scope.World, PlayerWallShotOrigin, FVector::BackwardVector, 3000.0f, EnemyShooter);
	TestEqual(TEXT("WeaponTrace hits the player home-base exterior actor"), PlayerWallHit.Hit.GetActor(),
		static_cast<AActor*>(Scope.PlayerBase));
	TestTrue(TEXT("Enemy is permitted to damage the player home base"), PlayerWallHit.bCanDamage);
	TestTrue(TEXT("Enemy BOT weapon fires into the player base exterior"),
		EnemyAbility->Fire(EnemyShooter, PlayerWallShotOrigin, FVector::BackwardVector));
	TestEqual(TEXT("Enemy BOT exterior hit removes 300 Armor with its 0.3 damage multiplier"),
		Scope.PlayerBase->GetCoreTarget()->GetVitalSet()->GetArmor(), 99700.0f);

	// Player attacks the opposing exterior twice: the first shot proves Armor loss and the
	// second proves the real GAS death delegate -> base destruction -> battle-gauge chain.
	UGA_Fire_Hitscan* PlayerAbility = nullptr;
	AMSPlayerCharacter* PlayerShooter = MSHomeBaseTestP72::SpawnShooter(
		Scope.World, FVector(38500.0f, 550.0f * MSGO::MSScale, 0.0f), MSTeam::PlayerTeamId(), 50000, PlayerAbility);
	const FVector EnemyWallShotOrigin(38000.0f, 550.0f * MSGO::MSScale, 400.0f * MSGO::MSScale);
	TestTrue(TEXT("First player weapon attack hits the enemy base"),
		PlayerAbility->Fire(PlayerShooter, EnemyWallShotOrigin, FVector::BackwardVector));
	TestEqual(TEXT("First weapon attack reduces enemy Fortress Armor from 100000 to 50000"),
		Scope.EnemyBase->GetCoreTarget()->GetVitalSet()->GetArmor(), 50000.0f);
	TestFalse(TEXT("Enemy base remains operational above zero Armor"), Scope.EnemyBase->IsHomeBaseDestroyed());

	TestTrue(TEXT("Second player weapon attack hits the enemy base"),
		PlayerAbility->Fire(PlayerShooter, EnemyWallShotOrigin, FVector::BackwardVector));
	TestEqual(TEXT("Second weapon attack reduces enemy base Armor to zero"),
		Scope.EnemyBase->GetCoreTarget()->GetVitalSet()->GetArmor(), 0.0f);
	TestTrue(TEXT("Zero Armor transitions the enemy base to destroyed"), Scope.EnemyBase->IsHomeBaseDestroyed());
	TestEqual(TEXT("Destroying the enemy base removes the documented 2500 gauge"),
		State->EnemyGauge, 7500.0f);
	// 300 of 100000 Armor = 0.3% of the base's 2500 gauge share = 7.5 gauge (task #226).
	TestEqual(TEXT("The earlier enemy exterior hit already drained the player gauge"),
		State->PlayerGauge, 9992.5f);
	TestNull(TEXT("Destroyed base no longer accepts damage or hit confirmation"),
		Scope.EnemyBase->GetAbilitySystemComponent());
	return true;
}

bool FMSHomeBaseGaugeDamageTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseBattleWorld Scope;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	AMSDestructibleTarget* EnemyCore = Scope.EnemyBase ? Scope.EnemyBase->GetCoreTarget() : nullptr;
	AMSDestructibleTarget* PlayerCore = Scope.PlayerBase ? Scope.PlayerBase->GetCoreTarget() : nullptr;
	TestNotNull(TEXT("Gauge battle uses AMSGameState"), State);
	TestNotNull(TEXT("Enemy base has a core"), EnemyCore);
	TestNotNull(TEXT("Player base has a core"), PlayerCore);
	if (!State || !EnemyCore || !PlayerCore) return false;

	AMSDestructibleTarget* PlayerAttacker = MSHomeBaseTestP72::SpawnAttacker(
		Scope.World, FVector(60000.0f, 0.0f, 0.0f), MSTeam::PlayerTeamId());
	AMSDestructibleTarget* EnemyAttacker = MSHomeBaseTestP72::SpawnAttacker(
		Scope.World, FVector(-30000.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	if (!PlayerAttacker || !EnemyAttacker) return false;

	// 拠点制圧戦 / 本拠点強襲戦: HomeBaseDamage = 2500 per base.
	Scope.GameMode->ApplyRules(400.0f, 2500.0f, 10000.0f);
	TestEqual(TEXT("Untouched base reports no armor loss"), Scope.EnemyBase->GetCoreArmorLostRatio(), 0.0f);

	MSHomeBaseTestP72::ApplyDamage(PlayerAttacker, EnemyCore, 25000.0f);
	TestEqual(TEXT("Quarter damage removes a quarter of Fortress Armor"),
		EnemyCore->GetVitalSet()->GetArmor(), 75000.0f);
	TestEqual(TEXT("Quarter damage reports a 0.25 armor-loss ratio"),
		Scope.EnemyBase->GetCoreArmorLostRatio(), 0.25f);
	TestEqual(TEXT("Quarter damage drains a quarter of the base gauge share"),
		State->EnemyGauge, 9375.0f);
	TestFalse(TEXT("Partial damage does not destroy the base"), Scope.EnemyBase->IsHomeBaseDestroyed());

	MSHomeBaseTestP72::ApplyDamage(PlayerAttacker, EnemyCore, 25000.0f);
	TestEqual(TEXT("Accumulated half damage drains half the base gauge share"),
		State->EnemyGauge, 8750.0f);

	// Healing (or any non-damage attribute change) must never refund gauge.
	EnemyCore->GetVitalSet()->SetArmor(75000.0f);
	MSHomeBaseTestP72::ApplyDamage(PlayerAttacker, EnemyCore, 1.0f);
	TestEqual(TEXT("Repaired armor does not refund already drained gauge"),
		State->EnemyGauge, 8750.0f);

	MSHomeBaseTestP72::ApplyLethalDamage(PlayerAttacker, EnemyCore);
	TestTrue(TEXT("Lethal damage destroys the enemy base"), Scope.EnemyBase->IsHomeBaseDestroyed());
	TestEqual(TEXT("A destroyed base costs exactly the rule's 2500 in total"),
		State->EnemyGauge, 7500.0f);
	Scope.EnemyBase->OnHomeBaseDestroyed.Broadcast(Scope.EnemyBase, EMSCaptureOwner::TeamEnemy);
	TestEqual(TEXT("A repeated destruction event cannot drain the gauge twice"),
		State->EnemyGauge, 7500.0f);
	TestEqual(TEXT("Enemy base damage never touches the player gauge"), State->PlayerGauge, 10000.0f);

	// 殲滅戦: HomeBaseDamage = 0, so base damage is specified to leave the gauge alone.
	Scope.GameMode->ApplyRules(500.0f, 0.0f, 10000.0f);
	MSHomeBaseTestP72::ApplyDamage(EnemyAttacker, PlayerCore, 40000.0f);
	TestEqual(TEXT("Annihilation rule keeps armor damage off the gauge"), State->PlayerGauge, 10000.0f);
	TestTrue(TEXT("Annihilation rule still applies armor damage"),
		PlayerCore->GetVitalSet()->GetArmor() < 100000.0f);
	return true;
}

bool FMSHomeBaseTypeTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseWorld Scope;
	const EMSHomeBaseType Types[] = {
		EMSHomeBaseType::Fortress, EMSHomeBaseType::Battleship,
		EMSHomeBaseType::CommandPost, EMSHomeBaseType::Reactor
	};
	TArray<AMSHomeBase*> Bases;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
	{
		Bases.Add(MSHomeBaseTestP72::SpawnTypedBase(
			Scope.World, Types[Index], FVector(Index * 30000.0f, 0.0f, 0.0f)));
		TestNotNull(FString::Printf(TEXT("Type %d spawns"), Index), Bases.Last());
		if (Bases.Last())
		{
			TestEqual(FString::Printf(TEXT("Type %d is retained"), Index),
				Bases.Last()->GetHomeBaseType(), Types[Index]);
		}
	}
	if (Bases.Contains(nullptr)) return false;

	TestEqual(TEXT("Fortress uses 100000 MaxArmor"),
		Bases[0]->GetCoreTarget()->GetVitalSet()->GetMaxArmor(), 100000.0f);
	TestEqual(TEXT("Battleship uses 75000 MaxArmor"),
		Bases[1]->GetCoreTarget()->GetVitalSet()->GetMaxArmor(), 75000.0f);
	TestEqual(TEXT("CommandPost uses 75000 MaxArmor"),
		Bases[2]->GetCoreTarget()->GetVitalSet()->GetMaxArmor(), 75000.0f);
	TestEqual(TEXT("Reactor uses 75000 MaxArmor"),
		Bases[3]->GetCoreTarget()->GetVitalSet()->GetMaxArmor(), 75000.0f);

	const FVector FortressRoof = MSHomeBaseTestP72::FindBuildingPart(Bases[0], TEXT("Roof"))->GetRelativeScale3D();
	for (int32 Index = 1; Index < Bases.Num(); ++Index)
	{
		const FVector VariantRoof = MSHomeBaseTestP72::FindBuildingPart(Bases[Index], TEXT("Roof"))->GetRelativeScale3D();
		TestFalse(FString::Printf(TEXT("Type %d has a distinct proportion"), Index), VariantRoof.Equals(FortressRoof));
	}
	return true;
}

bool FMSHomeBaseAuthoredExteriorTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseWorld Scope;
	FBoolProperty* Authored = FindFProperty<FBoolProperty>(AMSHomeBase::StaticClass(), TEXT("bUseAuthoredExterior"));
	FStructProperty* Offset = FindFProperty<FStructProperty>(AMSHomeBase::StaticClass(), TEXT("CoreLocalOffset"));
	if (!TestNotNull(TEXT("authored exterior is editable"), Authored)
		|| !TestNotNull(TEXT("core local offset is editable"), Offset)) return false;
	const AMSHomeBase* Defaults = AMSHomeBase::StaticClass()->GetDefaultObject<AMSHomeBase>();
	TestFalse(TEXT("existing maps retain the built-in exterior by default"), Authored->GetPropertyValue_InContainer(Defaults));
	TestTrue(TEXT("existing maps retain an unshifted core by default"),
		Offset->ContainerPtrToValuePtr<FVector>(Defaults)->IsZero());

	AMSHomeBase* Legacy = Scope.World->SpawnActor<AMSHomeBase>(FVector(30000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("legacy base spawns"), Legacy) ||
		!TestNotNull(TEXT("legacy core spawns"), Legacy->GetCoreTarget())) return false;
	UStaticMeshComponent* LegacyRoof = MSHomeBaseTestP72::FindBuildingPart(Legacy, TEXT("Roof"));
	if (!TestNotNull(TEXT("legacy roof exists"), LegacyRoof)) return false;
	TestTrue(TEXT("default roof remains visible"), LegacyRoof->IsVisible());
	TestTrue(TEXT("default roof retains collision"), LegacyRoof->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	TestTrue(TEXT("default core remains at the base origin"),
		Legacy->GetCoreTarget()->GetActorLocation().Equals(Legacy->GetActorLocation(), 0.1f));

	const FTransform Transform(FRotator(0.0f, 90.0f, 0.0f), FVector(70000.0f, -45000.0f, -12000.0f),
		FVector(1.5f, 1.0f, 1.0f));
	AMSHomeBase* Base = Scope.World->SpawnActorDeferred<AMSHomeBase>(
		AMSHomeBase::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("authored base deferred spawn succeeds"), Base)) return false;
	Authored->SetPropertyValue_InContainer(Base, true);
	const FVector LocalOffset(1000.0f, -500.0f, -1800.0f);
	*Offset->ContainerPtrToValuePtr<FVector>(Base) = LocalOffset;
	Base->SetHomeBaseType(EMSHomeBaseType::Battleship);
	Base->FinishSpawning(Transform);
	AMSDestructibleTarget* Core = Base->GetCoreTarget();
	if (!TestNotNull(TEXT("authored base still spawns its GAS core"), Core)) return false;
	TestTrue(TEXT("local offset is transformed by rotation and base scale"),
		Core->GetActorLocation().Equals(Base->GetActorTransform().TransformPosition(LocalOffset), 0.1f));
	TestTrue(TEXT("core uses base rotation"), Core->GetActorRotation().Equals(Base->GetActorRotation(), 0.1f));
	TestTrue(TEXT("core keeps the existing unit scale"), Core->GetActorScale3D().Equals(FVector::OneVector, 0.01f));
	TestEqual(TEXT("core stays attached in world space"), Core->GetAttachParentActor(), static_cast<AActor*>(Base));
	TestEqual(TEXT("GAS forwarding still reaches the core"), Base->GetAbilitySystemComponent(), Core->GetAbilitySystemComponent());

	Base->UpdateVisuals();
	Base->SetHomeBaseType(EMSHomeBaseType::CommandPost);
	for (const FName PartName : { FName(TEXT("Wall_Back")), FName(TEXT("Wall_Left")), FName(TEXT("Wall_Right")),
		FName(TEXT("Wall_FrontLeft")), FName(TEXT("Wall_FrontRight")), FName(TEXT("Lintel")),
		FName(TEXT("Roof")), FName(TEXT("Wall_Screen")) })
	{
		UStaticMeshComponent* Part = MSHomeBaseTestP72::FindBuildingPart(Base, PartName);
		if (!TestNotNull(*FString::Printf(TEXT("%s built-in part exists"), *PartName.ToString()), Part)) continue;
		TestFalse(*FString::Printf(TEXT("%s does not reappear"), *PartName.ToString()), Part->IsVisible());
		TestEqual(*FString::Printf(TEXT("%s does not block collision"), *PartName.ToString()),
			Part->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(*FString::Printf(TEXT("%s does not affect navigation"), *PartName.ToString()),
			Part->CanEverAffectNavigation());
	}
	return true;
}

bool FMSHomeBaseLabelTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseWorld Scope;
	const TCHAR* Labels[] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("X"), TEXT("Y"), TEXT("Z") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
	{
		AMSHomeBase* Base = Scope.World->SpawnActor<AMSHomeBase>(
			FVector(Index * 30000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		TestNotNull(FString::Printf(TEXT("Label base %s spawns"), Labels[Index]), Base);
		if (!Base) continue;
		Base->SetPointName(FText::FromString(Labels[Index]));
		TestEqual(FString::Printf(TEXT("GetPointName returns %s"), Labels[Index]),
			Base->GetPointName().ToString(), FString(Labels[Index]));
	}
	return true;
}

bool FMSHomeBaseFactionReplicationTest::RunTest(const FString&)
{
	MSHomeBaseTestP72::FScopedHomeBaseWorld Scope;
	AMSHomeBase* ServerBase = Scope.World->SpawnActor<AMSHomeBase>();
	AMSHomeBase* ReceivingBase = Scope.World->SpawnActor<AMSHomeBase>(FVector(40000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("authority fixture base exists"), ServerBase)
		|| !TestNotNull(TEXT("notification fixture base exists"), ReceivingBase)) return false;
	FProperty* Faction = FindFProperty<FProperty>(AMSHomeBase::StaticClass(), TEXT("HomeBaseFaction"));
	if (!TestNotNull(TEXT("faction reflection metadata exists"), Faction)) return false;
	TestTrue(TEXT("runtime-normalized faction is a replicated property"), Faction->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("faction uses a visual-only replication notification"), Faction->RepNotifyFunc, FName(TEXT("OnRep_HomeBaseFaction")));
	// Match FRepLayout::InitFromClass: reflection RepIndex values are assigned
	// lazily, before GetLifetimeReplicatedProps. A standalone test world has no
	// NetDriver to do this for us; querying first can collide with AActor's FAST
	// push-model indices even though the production replication contract is valid.
	UClass* ReplicatedClass = ServerBase->GetClass();
	ReplicatedClass->SetUpRuntimeReplicationData();
	if (!TestTrue(TEXT("runtime replication schema assigns faction its own property index"),
		ReplicatedClass->ClassReps.IsValidIndex(Faction->RepIndex)
		&& ReplicatedClass->ClassReps[Faction->RepIndex].Property == Faction)) return false;
	TArray<FLifetimeProperty> Lifetime;
	ServerBase->GetLifetimeReplicatedProps(Lifetime);
	TestTrue(TEXT("faction is registered for every relevant connection"), Lifetime.ContainsByPredicate(
		[Faction](const FLifetimeProperty& Property) { return Property.RepIndex == Faction->RepIndex && Property.Condition == COND_None; }));
	UFunction* Notify = ReceivingBase->FindFunction(Faction->RepNotifyFunc);
	if (!TestNotNull(TEXT("faction notification is callable by replication"), Notify)) return false;
	UStaticMeshComponent* Flag = MSHomeBaseTestP72::FindBuildingPart(ReceivingBase, TEXT("FlagPole"));
	if (!TestNotNull(TEXT("base visual component exists"), Flag)) return false;
	// SetGenericTeamId is also the GameMode's runtime normalization entry point.
	ServerBase->SetGenericTeamId(MSTeam::EnemyTeamId());
	// This verifies metadata and notification behavior, not transport. Simulate
	// both arrival orders without calling the setter on the receiving object.
	const EMSCaptureOwner Owners[] = { EMSCaptureOwner::Neutral, EMSCaptureOwner::TeamPlayer };
	for (const EMSCaptureOwner OwnerBefore : Owners)
	{
		ReceivingBase->SetFixedOwner(OwnerBefore);
		const float ProgressBefore = ReceivingBase->GetProgress();
		Flag->SetVisibility(true); // A visual refresh must restore the hidden home-base flag.
		Faction->CopyCompleteValue(Faction->ContainerPtrToValuePtr<void>(ReceivingBase),
			Faction->ContainerPtrToValuePtr<void>(ServerBase));
		ReceivingBase->ProcessEvent(Notify, nullptr);
		TestEqual(TEXT("received faction exposes the authority's generic team"), ReceivingBase->GetGenericTeamId(), ServerBase->GetGenericTeamId());
		TestFalse(TEXT("faction notification refreshes home-base visuals"), Flag->IsVisible());
		TestEqual(TEXT("faction notification never overwrites separately replicated ownership"), ReceivingBase->GetCurrentOwner(), OwnerBefore);
		TestEqual(TEXT("faction notification never overwrites capture progress"), ReceivingBase->GetProgress(), ProgressBefore);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
