// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/GA_Fire_ContinuousBeam.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSContinuousBeamAbilityTest,
	"NeoMSGO.Weapons.ContinuousBeamAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FBeamTestWorld
	{
		UWorld* World = nullptr;
		FBeamTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBeamTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FBeamTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickBeamWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnBeamUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		// 2Tick目以降は「発射時点のカメラ位置・方向」で再取得される(実プレイ報告バグ修正)ため、
		// テストのカメラを意図した発射起点(機体位置+40cm)へ合わせる(MSPlayerWeaponInputTestsと同手法)
		if (UMSCameraComponent* Camera = Unit->FindComponentByClass<UMSCameraComponent>())
		{
			Camera->SetWorldLocation(Location + FVector(0.0f, 0.0f, 40.0f));
			Camera->SetWorldRotation(FRotator::ZeroRotator);
		}
		return Unit;
	}

	UMSContinuousBeamWeaponData* MakeBeamWeapon(UObject* Outer)
	{
		UMSContinuousBeamWeaponData* Weapon = NewObject<UMSContinuousBeamWeaponData>(Outer);
		Weapon->Range = 20.0f;
		Weapon->FireInterval = 0.0f;
		Weapon->BeamDamagePerSecond = 300;
		Weapon->BeamTickInterval = 0.1f;
		Weapon->BeamDownPointPerSecond = 20;
		return Weapon;
	}
}

bool FMSContinuousBeamAbilityTest::RunTest(const FString& Parameters)
{
	FBeamTestWorld TestWorld;
	const FVector Origin(0.0f, 0.0f, 40.0f);

	AMSUnitBase* Shooter = SpawnBeamUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Enemy = SpawnBeamUnit(TestWorld.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	UMSContinuousBeamWeaponData* Weapon = MakeBeamWeapon(Shooter);
	Shooter->GetWeaponComponent()->InitializeSlots({Weapon});
	TStrongObjectPtr<UGA_Fire_ContinuousBeam> Ability(NewObject<UGA_Fire_ContinuousBeam>(Shooter));

	// AC4: trace/visual origin is Origin + Direction*1.2m — verified via the pure helper (no visual
	// asset is spawned, only the position calculation, as required by the acceptance criteria).
	const FVector ExpectedVisualOrigin = Origin + FVector::ForwardVector * 120.0f;
	TestEqual(TEXT("beam visual/trace origin is offset 1.2m forward"),
		UGA_Fire_ContinuousBeam::GetBeamVisualOrigin(Origin, FVector::ForwardVector), ExpectedVisualOrigin);

	const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();
	const float DownPointBefore = Enemy->GetVitalSet()->GetDownPoint();

	// AC1/AC2/AC3: StartBeam fires the first tick synchronously through the shared hitscan path
	// (UMSCombatStatics::ResolveHitscan — no bespoke trace exists in GA_Fire_ContinuousBeam),
	// applying tickDamage = BeamDamagePerSecond * BeamTickInterval and the matching DP formula.
	TestTrue(TEXT("beam starts and applies its first tick"),
		Ability->StartBeam(Shooter, Origin, FVector::ForwardVector));
	TestTrue(TEXT("beam is active immediately after start"), Ability->IsBeamActive());
	TestTrue(TEXT("beam loop GameplayCue is active while firing"),
		Shooter->GetAbilitySystemComponent()->IsGameplayCueActive(MSTags::Cue_Weapon_BeamLoop));
	TestEqual(TEXT("first tick applied"), Ability->GetTickCount(), 1);
	TestTrue(TEXT("first tick damage matches BeamDamagePerSecond * BeamTickInterval (30)"),
		FMath::IsNearlyEqual(ArmorBefore - Enemy->GetVitalSet()->GetArmor(), 30.0f, 0.5f));
	TestTrue(TEXT("first tick DP matches BeamDownPointPerSecond * BeamTickInterval (2)"),
		FMath::IsNearlyEqual(Enemy->GetVitalSet()->GetDownPoint() - DownPointBefore, 2.0f, 0.5f));

	// AC2/AC3: further ticks accrue at BeamTickInterval while the trigger stays held.
	TickBeamWorld(TestWorld.World, 0.05f);
	TestEqual(TEXT("no early second tick before BeamTickInterval elapses"), Ability->GetTickCount(), 1);
	TickBeamWorld(TestWorld.World, 0.11f);
	TestEqual(TEXT("second tick fires at BeamTickInterval"), Ability->GetTickCount(), 2);
	TestTrue(TEXT("cumulative damage after two ticks is ~60"),
		FMath::IsNearlyEqual(ArmorBefore - Enemy->GetVitalSet()->GetArmor(), 60.0f, 1.0f));
	TestTrue(TEXT("cumulative DP after two ticks is ~4"),
		FMath::IsNearlyEqual(Enemy->GetVitalSet()->GetDownPoint() - DownPointBefore, 4.0f, 1.0f));

	// AC5: releasing the trigger stops the beam immediately — no further ticks accrue.
	Ability->StopBeam();
	TestFalse(TEXT("beam is inactive after release"), Ability->IsBeamActive());
	TestFalse(TEXT("beam loop GameplayCue is removed on release"),
		Shooter->GetAbilitySystemComponent()->IsGameplayCueActive(MSTags::Cue_Weapon_BeamLoop));
	const float ArmorAfterStop = Enemy->GetVitalSet()->GetArmor();
	TickBeamWorld(TestWorld.World, 0.5f);
	TestEqual(TEXT("no further ticks after release"), Ability->GetTickCount(), 2);
	TestEqual(TEXT("no further damage after release"), Enemy->GetVitalSet()->GetArmor(), ArmorAfterStop);

	// A stopped beam can be restarted (fresh trigger press).
	TestTrue(TEXT("beam can restart after being stopped"),
		Ability->StartBeam(Shooter, Origin, FVector::ForwardVector));
	TestEqual(TEXT("restart begins a fresh tick count"), Ability->GetTickCount(), 1);
	Ability->StopBeam();

	// 実プレイ報告バグ回帰テスト(バーストと同類): 照射中に機体が移動しても、以降のTickの
	// トレース起点が機体(カメラ)へ追従する(開始地点から照射され続けない)。
	{
		AMSUnitBase* MoveShooter = SpawnBeamUnit(TestWorld.World, FVector(0.0f, 900.0f, 0.0f), MSTeam::PlayerTeamId());
		AMSUnitBase* StartLaneEnemy = SpawnBeamUnit(TestWorld.World, FVector(800.0f, 900.0f, 0.0f), MSTeam::EnemyTeamId());
		AMSUnitBase* MovedLaneEnemy = SpawnBeamUnit(TestWorld.World, FVector(800.0f, 1500.0f, 0.0f), MSTeam::EnemyTeamId());
		UMSContinuousBeamWeaponData* MoveWeapon = MakeBeamWeapon(MoveShooter);
		MoveShooter->GetWeaponComponent()->InitializeSlots({MoveWeapon});
		TStrongObjectPtr<UGA_Fire_ContinuousBeam> MoveAbility(
			NewObject<UGA_Fire_ContinuousBeam>(MoveShooter));

		const float StartLaneArmorBefore = StartLaneEnemy->GetVitalSet()->GetArmor();
		const float MovedLaneArmorBefore = MovedLaneEnemy->GetVitalSet()->GetArmor();
		TestTrue(TEXT("moving-shooter beam starts on the initial lane"), MoveAbility->StartBeam(
			MoveShooter, FVector(0.0f, 900.0f, 40.0f), FVector::ForwardVector));
		TestTrue(TEXT("first beam tick hits the initial-lane enemy"),
			StartLaneArmorBefore - StartLaneEnemy->GetVitalSet()->GetArmor() >= 25.0f);

		MoveShooter->SetActorLocation(FVector(0.0f, 1500.0f, 0.0f));
		if (UMSCameraComponent* MoveCamera = MoveShooter->FindComponentByClass<UMSCameraComponent>())
		{
			MoveCamera->SetWorldLocation(FVector(0.0f, 1500.0f, 40.0f));
			MoveCamera->SetWorldRotation(FRotator::ZeroRotator);
		}
		const float StartLaneArmorAfterMove = StartLaneEnemy->GetVitalSet()->GetArmor();
		TickBeamWorld(TestWorld.World, 0.25f);
		MoveAbility->StopBeam();
		TestTrue(FString::Printf(TEXT("beam ticks after the move originate from the new position (moved-lane damage=%.1f)"),
				MovedLaneArmorBefore - MovedLaneEnemy->GetVitalSet()->GetArmor()),
			MovedLaneArmorBefore - MovedLaneEnemy->GetVitalSet()->GetArmor() >= 25.0f);
		TestEqual(TEXT("the stale start position takes no further beam damage after the move"),
			StartLaneEnemy->GetVitalSet()->GetArmor(), StartLaneArmorAfterMove);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSContinuousBeamDeathCancellationTest,
	"NeoMSGO.Weapons.ContinuousBeamStopsOnAuthoritativeDeath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSContinuousBeamDeathCancellationTest::RunTest(const FString& Parameters)
{
	FBeamTestWorld TestWorld;
	AMSUnitBase* Shooter = SpawnBeamUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* Killer = SpawnBeamUnit(TestWorld.World, FVector(0, 3000, 0), MSTeam::EnemyTeamId());
	UMSContinuousBeamWeaponData* Beam = MakeBeamWeapon(Shooter);
	Beam->MagazineSize = 10;
	Beam->ReserveAmmo = 0;
	Shooter->GetWeaponComponent()->InitializeSlots({Beam});
	// Deliberately outside Character's cached ability pointer: FireTick must
	// independently guard death for server/remote actors as well as local ones.
	TStrongObjectPtr<UGA_Fire_ContinuousBeam> Ability(NewObject<UGA_Fire_ContinuousBeam>(Shooter));
	TestFalse(TEXT("death fixture is not a locally controlled pawn"), Shooter->IsLocallyControlled());
	TestTrue(TEXT("beam fires first tick before death"), Ability->StartBeam(Shooter, FVector(0, 0, 40), FVector::ForwardVector));
	const int32 AmmoAfterFirstTick = Shooter->GetWeaponComponent()->GetMagazineAmmo(0);
	const int32 TicksBeforeDeath = Ability->GetTickCount();
	UAbilitySystemComponent* ASC = Shooter->GetAbilitySystemComponent();
	FGameplayEffectContextHandle DamageContext = Killer->GetAbilitySystemComponent()->MakeEffectContext();
	DamageContext.AddInstigator(Killer, Killer);
	FGameplayEffectSpec Damage(GetDefault<UGE_Damage>(), DamageContext, 1.0f);
	Damage.SetSetByCallerMagnitude(MSTags::Data_Damage, Shooter->GetVitalSet()->GetArmor() + 100.0f);
	Damage.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
	ASC->ApplyGameplayEffectSpecToSelf(Damage);
	TestTrue(TEXT("real hostile damage reaches GAS death"), ASC->HasMatchingGameplayTag(MSTags::State_Dead));
	TickBeamWorld(TestWorld.World, 0.4f);
	TestFalse(TEXT("non-local death stops the beam timer"), Ability->IsBeamActive());
	TestEqual(TEXT("death emits no subsequent damage ticks"), Ability->GetTickCount(), TicksBeforeDeath);
	TestEqual(TEXT("death preserves remaining beam ammunition"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), AmmoAfterFirstTick);
	TestFalse(TEXT("a dead shooter cannot restart an uncached beam"), Ability->StartBeam(Shooter, FVector(0, 0, 40), FVector::ForwardVector));
	return true;
}

#endif
