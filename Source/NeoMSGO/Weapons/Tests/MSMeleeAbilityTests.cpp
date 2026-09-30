// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-7 GA_Melee 受け入れ条件テスト:
//  AC1: 通常格闘 判定半径1.5m・リーチ=weapon.range(D格のみ1.6倍)
//  AC2: D格 判定半径2.8m・リーチ1.6倍・ダメージ3倍×meleeCorrection×チャージ倍率
//  AC3: D格でブースト30消費。残量不足は強制OH(ConsumeBoostOrOverheat)でも突進・攻撃は実行
//  AC6: ダメージ/DPは通常のGE_Damage経路(UMSDamageExecution。FF最終防衛線が機能する)
//  AC7: しゃがみ中(State.Crouching)はD格不可=通常格闘へ降格
//  (補助: AC5 攻撃後硬直0.45sのState.MeleeRecoveryが次の攻撃をブロックすること)

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeleeAbilityTest,
	"NeoMSGO.Weapons.MeleeAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeleeGASRecoveryLifecycleTest,
	"NeoMSGO.Weapons.MeleeGASRecoveryLifecycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeleePerWeaponDashBoostCostTest,
	"NeoMSGO.Weapons.MeleePerWeaponDashBoostCost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FMeleeTestWorld
	{
		UWorld* World = nullptr;
		FMeleeTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMeleeTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FMeleeTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickMeleeWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnMeleeUnit(UWorld* World, const FVector& Location, FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		// 重力落下で判定球と相対位置がずれないよう固定(突進はAddActorWorldOffsetのため影響なし)
		Unit->GetCharacterMovement()->DisableMovement();
		return Unit;
	}

	UMSMeleeWeaponData* MakeMeleeWeapon(UObject* Outer)
	{
		UMSMeleeWeaponData* Weapon = NewObject<UMSMeleeWeaponData>(Outer);
		Weapon->Range = 3.0f;        // リーチ3m: 通常=300cm / D格=480cm
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->FireInterval = 0.0f;
		Weapon->MagazineSize = 0;    // 無限弾
		return Weapon;
	}
}

bool FMSMeleeAbilityTest::RunTest(const FString& Parameters)
{
	FMeleeTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	// 攻撃者(原点) / 近距離敵(通常・D格両方の判定内) / 遠距離敵(D格のみ判定内) /
	// 味方(通常格闘の判定球内。FF最終防衛線=UMSDamageExecution手順1の検証用)
	AMSUnitBase* Attacker = SpawnMeleeUnit(World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* NearEnemy = SpawnMeleeUnit(World, FVector(2500.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	AMSUnitBase* FarEnemy = SpawnMeleeUnit(World, FVector(6000.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	AMSUnitBase* Ally = SpawnMeleeUnit(World, FVector(2500.0f, -500.0f, 0.0f), MSTeam::PlayerTeamId());

	UAbilitySystemComponent* ASC = Attacker->GetAbilitySystemComponent();
	UMSMeleeWeaponData* Weapon = MakeMeleeWeapon(Attacker);
	Attacker->GetWeaponComponent()->InitializeSlots({Weapon});
	// Production owns this object in AMSPlayerCharacter::MeleeAbility (UPROPERTY).
	// Outer and TimerManager's CreateUObject delegates do not keep it alive: every
	// virtual World tick can invoke engine-wide ConditionalCollectGarbage.
	TStrongObjectPtr<UGA_Melee> Ability(NewObject<UGA_Melee>(Attacker));

	// ---- AC1/AC2: 判定半径・リーチ・ダメージ倍率の定数/式 ----
	TestEqual(TEXT("AC1: normal melee radius follows MSScale"), UGA_Melee::GetMeleeRadiusCm(false), 150.0f * MSGO::MSScale);
	TestEqual(TEXT("AC2: dash melee radius follows MSScale"), UGA_Melee::GetMeleeRadiusCm(true), 280.0f * MSGO::MSScale);
	TestEqual(TEXT("AC1: normal melee reach follows MSScale"), UGA_Melee::GetMeleeReachCm(3.0f, false), 300.0f * MSGO::MSScale);
	TestEqual(TEXT("AC2: dash melee reach follows MSScale"), UGA_Melee::GetMeleeReachCm(3.0f, true), 480.0f * MSGO::MSScale);
	TestTrue(TEXT("legacy weapon runtime reach remains world-scaled within 0.001cm float precision"),
		FMath::IsNearlyEqual(UGA_Melee::GetMeleeReachCmForWeapon(Weapon, false),
			Weapon->Range * 100.0f * MSGO::MSScale, 0.001f));
	UMSMeleeWeaponData* PhysicalWeapon = NewObject<UMSMeleeWeaponData>(Attacker);
	PhysicalWeapon->Range = 6.0f;
	PhysicalWeapon->RangeWorldScale = 1.0f;
	TestEqual(TEXT("physical-metre melee normal reach uses six metres"),
		UGA_Melee::GetMeleeReachCmForWeapon(PhysicalWeapon, false), 600.0f);
	TestEqual(TEXT("physical-metre melee dash reach scales the hit center only"),
		UGA_Melee::GetMeleeReachCmForWeapon(PhysicalWeapon, true), 960.0f);
	TestEqual(TEXT("AC1: normal multiplier is 1x"), UGA_Melee::CalcDamageMultiplier(false, 1.0f, 1.0f), 1.0f);
	TestEqual(TEXT("AC2: dash multiplier is 3x"), UGA_Melee::CalcDamageMultiplier(true, 1.0f, 1.0f), 3.0f);
	TestTrue(TEXT("AC2: dash multiplier = 3 * meleeCorrection * charge"),
		FMath::IsNearlyEqual(UGA_Melee::CalcDamageMultiplier(true, 1.2f, 1.5f), 5.4f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("D-melee DP keeps melee correction and charge without the HP-only 3x"),
		FMath::IsNearlyEqual(UGA_Melee::CalcDownPointMultiplier(1.2f, 1.5f), 1.8f, KINDA_SMALL_NUMBER));

	// ---- Phase 1: 通常格闘(AC1 + AC6) ----
	{
		const float NearArmor = NearEnemy->GetVitalSet()->GetArmor();
		const float NearDP = NearEnemy->GetVitalSet()->GetDownPoint();
		const float FarArmor = FarEnemy->GetVitalSet()->GetArmor();
		const float AllyArmor = Ally->GetVitalSet()->GetArmor();
		const float BoostBefore = Attacker->GetBoostSet()->GetBoost();

		TestTrue(TEXT("AC1: normal melee swings"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false, 1.0f));
		TestFalse(TEXT("AC1: normal swing is not a dash melee"), Ability->WasLastAttackDashMelee());
		TestEqual(TEXT("normal melee does not block boost recovery"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
		TestEqual(TEXT("AC6: GE_Damage applied per unique actor in the sphere (near enemy + ally)"),
			Ability->GetLastHitCount(), 2);
		TestTrue(TEXT("AC1/AC6: near enemy takes weapon.Power * 1x = 100 via GE_Damage"),
			FMath::IsNearlyEqual(NearArmor - NearEnemy->GetVitalSet()->GetArmor(), 100.0f, 0.5f));
		TestTrue(TEXT("AC6: near enemy DP rises by weapon.DownPoint * 1x = 10 via the same GE"),
			FMath::IsNearlyEqual(NearEnemy->GetVitalSet()->GetDownPoint() - NearDP, 10.0f, 0.5f));
		TestEqual(TEXT("AC1: far enemy (60m) is outside the scaled normal swing"),
			FarEnemy->GetVitalSet()->GetArmor(), FarArmor);
		TestEqual(TEXT("AC6: friendly in the sphere takes zero damage (UMSDamageExecution FF gate)"),
			Ally->GetVitalSet()->GetArmor(), AllyArmor);
		TestTrue(TEXT("AC1: normal melee consumes no boost"),
			FMath::IsNearlyEqual(Attacker->GetBoostSet()->GetBoost(), BoostBefore, 0.01f));

		// 補助AC5: 硬直0.45s中は次の攻撃不可
		TestEqual(TEXT("AC5: State.MeleeRecovery is granted after the swing"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TestFalse(TEXT("AC5: a dash swing requested during normal recovery is rejected"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, true, 1.0f));
		TestEqual(TEXT("rejected D-melee never applies a boost regen block"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
		TestTrue(TEXT("rejected D-melee does not consume boost"),
			FMath::IsNearlyEqual(Attacker->GetBoostSet()->GetBoost(), BoostBefore, 0.01f));
		TickMeleeWorld(World, 0.6f);
		TestEqual(TEXT("AC5: State.MeleeRecovery expires after 0.45s"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
	}

	// ---- Phase 2: しゃがみ中はD格不可=通常格闘へ降格(AC7) ----
	{
		ASC->SetLooseGameplayTagCount(MSTags::State_Crouching, 1);
		const float NearArmor = NearEnemy->GetVitalSet()->GetArmor();
		const float FarArmor = FarEnemy->GetVitalSet()->GetArmor();
		const float BoostBefore = Attacker->GetBoostSet()->GetBoost();
		const FVector PosBefore = Attacker->GetActorLocation();

		TestTrue(TEXT("AC7: melee while crouching still swings (as a normal melee)"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, true, 1.0f));
		TestFalse(TEXT("AC7: dash request is demoted while State.Crouching"), Ability->WasLastAttackDashMelee());
		TestTrue(TEXT("AC7: demoted swing deals normal 1x damage"),
			FMath::IsNearlyEqual(NearArmor - NearEnemy->GetVitalSet()->GetArmor(), 100.0f, 0.5f));
		TestEqual(TEXT("AC7: demoted swing has no extended dash reach/radius"),
			FarEnemy->GetVitalSet()->GetArmor(), FarArmor);
		TestTrue(TEXT("AC7: demoted swing consumes no boost"),
			FMath::IsNearlyEqual(Attacker->GetBoostSet()->GetBoost(), BoostBefore, 0.01f));
		TickMeleeWorld(World, 0.6f);
		TestTrue(TEXT("AC7: demoted swing performs no lunge"),
			Attacker->GetActorLocation().Equals(PosBefore, 1.0f));
		ASC->SetLooseGameplayTagCount(MSTags::State_Crouching, 0);
	}

	// ---- Phase 3: D格(AC2 + AC3前半) ----
	{
		const float NearArmor = NearEnemy->GetVitalSet()->GetArmor();
		const float FarArmor = FarEnemy->GetVitalSet()->GetArmor();
		const float BoostBefore = Attacker->GetBoostSet()->GetBoost();
		TestTrue(TEXT("AC3: attacker starts with enough boost"), BoostBefore >= 100.0f - 0.01f);

		// チャージ倍率2.0を明示指定: damage = 100 * 3(D格) * 1.0(meleeCorrection) * 2.0(charge) = 600
		TestTrue(TEXT("AC2: dash melee swings"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, true, 2.0f));
		TestTrue(TEXT("AC2: swing executed as a dash melee"), Ability->WasLastAttackDashMelee());
		TestEqual(TEXT("dash melee blocks boost recovery from the attack onward"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);
		FMSBoostMovementState DashState;
		TestTrue(TEXT("boost driver reads the D-melee action tag"),
			Attacker->FindComponentByClass<UMSBoostComponent>()->ReadMovementState(DashState));
		TestTrue(TEXT("boost movement snapshot blocks recovery during D-melee"), DashState.bRegenBlocked);
		TestTrue(TEXT("AC2: far enemy (60m) is inside the scaled dash swing"),
			FMath::IsNearlyEqual(FarArmor - FarEnemy->GetVitalSet()->GetArmor(), 600.0f, 0.5f));
		TestTrue(TEXT("AC2: near enemy takes the same 3x * charge damage"),
			FMath::IsNearlyEqual(NearArmor - NearEnemy->GetVitalSet()->GetArmor(), 600.0f, 0.5f));
		// D格の実DPは強制転倒時に0へリセットされる。係数は上のruntime helperで検証する。
		TestTrue(TEXT("D-melee hit forces knockdown independently of its DP magnitude"),
			FarEnemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_KnockedDown));
		TestEqual(TEXT("forced knockdown resets the target DP gauge"), FarEnemy->GetVitalSet()->GetDownPoint(), 0.0f);
		TestTrue(TEXT("AC3: dash melee consumes 30 boost (100 -> 70)"),
			FMath::IsNearlyEqual(Attacker->GetBoostSet()->GetBoost(), BoostBefore - 30.0f, 0.01f));
		TestFalse(TEXT("AC3: no overheat while boost remains"),
			ASC->HasMatchingGameplayTag(MSTags::State_Overheated));
		// Deliberately exercise GC during active lunge/recovery instead of depending
		// on the full suite's FApp delta / GFrameCounter-driven GC schedule.
		TWeakObjectPtr<UGA_Melee> AbilityBeforeGC(Ability.Get());
		TWeakObjectPtr<UMSMeleeWeaponData> WeaponBeforeGC(Weapon);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		if (!TestTrue(TEXT("fixture retains the active melee ability across GC"), AbilityBeforeGC.IsValid())) return false;
		TestTrue(TEXT("weapon component retains its equipped weapon across GC"), WeaponBeforeGC.IsValid());
		TestEqual(TEXT("GC does not prematurely end dash recovery"), ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TestEqual(TEXT("GC does not prematurely release D-melee boost block"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);
		TickMeleeWorld(World, 0.9f); // 突進0.35s+硬直0.45sを消化
		TestEqual(TEXT("dash melee recovery has expired"), ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestEqual(TEXT("completed D-melee releases its boost block"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
	}

	// ---- Phase 4: ブースト残量不足のD格=強制OHでも突進・攻撃は実行(AC3後半) ----
	{
		// 障害物のないレーンへ移して突進距離を計測する
		Attacker->SetActorLocation(FVector(0.0f, -20000.0f, 0.0f));
		Attacker->GetBoostSet()->SetBoost(10.0f);

		const FVector PosBefore = Attacker->GetActorLocation();
		TestTrue(TEXT("AC3: dash melee still executes with insufficient boost"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), -FVector::ForwardVector, true, 1.0f));
		TestTrue(TEXT("AC3: executed as a dash melee"), Ability->WasLastAttackDashMelee());
		TestTrue(TEXT("AC3: remaining boost is fully consumed (10 -> 0)"),
			Attacker->GetBoostSet()->GetBoost() <= KINDA_SMALL_NUMBER);
		TestTrue(TEXT("AC3: forced overheat is triggered (State.Overheated)"),
			ASC->HasMatchingGameplayTag(MSTags::State_Overheated));

		// AC4補助: 突進26m/s×0.35s=9.1mの直線移動がOH下でも実行される
		TickMeleeWorld(World, 0.5f);
		const float LungeDistance = -(Attacker->GetActorLocation().X - PosBefore.X);
		TestTrue(FString::Printf(TEXT("AC3: lunge still runs while overheated (26m/s x MSScale x 0.35s = %.1fcm, got %.1f)"),
			910.0f * MSGO::MSScale, LungeDistance),
			FMath::IsNearlyEqual(LungeDistance, 910.0f * MSGO::MSScale, 30.0f * MSGO::MSScale));
		TestEqual(TEXT("OH D-melee still blocks regen after the lunge ends"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);
		ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
		Ability->CancelMelee();
		TestEqual(TEXT("death during post-lunge recovery clears D-melee regen block"),
			ASC->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
		TestEqual(TEXT("death during post-lunge recovery clears the melee action"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
		ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 0);
	}

	return true;
}

bool FMSMeleePerWeaponDashBoostCostTest::RunTest(const FString& Parameters)
{
	FMeleeTestWorld TestWorld;
	AMSUnitBase* CustomUnit = SpawnMeleeUnit(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* DefaultUnit = SpawnMeleeUnit(TestWorld.World, FVector(50000.0f, 0.0f, 0.0f), MSTeam::PlayerTeamId());
	UMSMeleeWeaponData* CustomWeapon = MakeMeleeWeapon(CustomUnit);
	UMSMeleeWeaponData* DefaultWeapon = MakeMeleeWeapon(DefaultUnit);
	TestEqual(TEXT("unconfigured melee weapon retains the original 30 boost cost"),
		DefaultWeapon->DashMeleeBoostCost, 30.0f);
	CustomWeapon->DashMeleeBoostCost = 17.0f;
	TestFalse(TEXT("custom-cost weapon uses the generic melee path"),
		CustomWeapon->bUseStandardBeamSaberMotion || CustomWeapon->bUseGundamDualSaberMotion);
	TestFalse(TEXT("default-cost weapon uses the generic melee path"),
		DefaultWeapon->bUseStandardBeamSaberMotion || DefaultWeapon->bUseGundamDualSaberMotion);
	CustomUnit->GetWeaponComponent()->InitializeSlots({CustomWeapon});
	DefaultUnit->GetWeaponComponent()->InitializeSlots({DefaultWeapon});
	TStrongObjectPtr<UGA_Melee> CustomAbility(NewObject<UGA_Melee>(CustomUnit));
	TStrongObjectPtr<UGA_Melee> DefaultAbility(NewObject<UGA_Melee>(DefaultUnit));
	const float CustomBefore = CustomUnit->GetBoostSet()->GetBoost();
	const float DefaultBefore = DefaultUnit->GetBoostSet()->GetBoost();
	if (!TestTrue(TEXT("both fixtures have enough boost for an exact debit"),
		CustomBefore >= 30.0f && DefaultBefore >= 30.0f)) return false;
	TestTrue(TEXT("generic D melee with a custom-cost weapon executes"),
		CustomAbility->PerformMelee(CustomUnit, CustomUnit->GetActorLocation(), FVector::ForwardVector, true, 1.0f));
	TestTrue(TEXT("generic D melee consumes the authored 17 boost through GAS"),
		FMath::IsNearlyEqual(CustomUnit->GetBoostSet()->GetBoost(), CustomBefore - 17.0f, 0.01f));
	TestTrue(TEXT("generic D melee with a default-cost weapon executes"),
		DefaultAbility->PerformMelee(DefaultUnit, DefaultUnit->GetActorLocation(), FVector::ForwardVector, true, 1.0f));
	TestTrue(TEXT("another generic weapon still consumes the default 30 boost through GAS"),
		FMath::IsNearlyEqual(DefaultUnit->GetBoostSet()->GetBoost(), DefaultBefore - 30.0f, 0.01f));
	CustomAbility->CancelMelee();
	DefaultAbility->CancelMelee();
	return true;
}

bool FMSMeleeGASRecoveryLifecycleTest::RunTest(const FString& Parameters)
{
	FMeleeTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSUnitBase* Attacker = SpawnMeleeUnit(World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	UAbilitySystemComponent* ASC = Attacker->GetAbilitySystemComponent();
	UMSMeleeWeaponData* LegacyWeapon = MakeMeleeWeapon(Attacker);
	Attacker->GetWeaponComponent()->InitializeSlots({LegacyWeapon});
	ASC->GiveAbility(FGameplayAbilitySpec(UGA_Melee::StaticClass()));
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UGA_Melee::StaticClass());
	UGA_Melee* Ability = Spec ? Cast<UGA_Melee>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("GAS owns a per-actor melee instance"), Ability)) return false;

	TestTrue(TEXT("GAS legacy swing activates"), ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
	TestTrue(TEXT("legacy activation stays alive for its recovery timer"), Ability->IsActive());
	TestEqual(TEXT("legacy swing grants exactly one recovery tag"),
		ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
	TestFalse(TEXT("GAS cannot retrigger during recovery"),
		ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
	TickMeleeWorld(World, 0.30f);
	TestEqual(TEXT("legacy 0.45-second lock still applies"),
		ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
	TickMeleeWorld(World, 0.20f);
	TestEqual(TEXT("GAS legacy timer releases recovery"),
		ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
	TestFalse(TEXT("legacy GAS activation ends after recovery"), Ability->IsActive());
	TestTrue(TEXT("GAS legacy swing can activate again"),
		ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
	TestEqual(TEXT("second legacy activation owns one recovery tag"),
		ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
	TickMeleeWorld(World, 0.50f);
	TestEqual(TEXT("second legacy recovery also releases"),
		ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);

	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		const bool bDual = Variant == 1;
		UMSMeleeWeaponData* TimedWeapon = MakeMeleeWeapon(Attacker);
		TimedWeapon->bUseStandardBeamSaberMotion = !bDual;
		TimedWeapon->bUseGundamDualSaberMotion = bDual;
		Attacker->GetWeaponComponent()->InitializeSlots({TimedWeapon});
		const FString Label = bDual ? TEXT("dual") : TEXT("standard");
		TestTrue(Label + TEXT(" timed GAS swing activates"),
			ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
		TestTrue(Label + TEXT(" stage timer keeps GAS activation alive"), Ability->IsActive());
		TestEqual(Label + TEXT(" startup has one recovery tag"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TickMeleeWorld(World, 0.35f);
		// This is the same-instance follow-up path used while GAS blocks retriggering.
		TestTrue(Label + TEXT(" buffered follow-up remains available"), Ability->PerformMelee(
			Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
		TickMeleeWorld(World, 0.55f);
		TestEqual(Label + TEXT(" follow-up stage executes"),
			Ability->GetLastStandardSaberAttackIndex(), bDual ? 11 : 1);
		ASC->CancelAbilityHandle(Spec->Handle);
		TestEqual(Label + TEXT(" GAS cancel clears recovery exactly once"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestFalse(Label + TEXT(" GAS cancel ends activation"), Ability->IsActive());
		TestTrue(Label + TEXT(" can activate after GAS cancel"),
			ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
		Ability->CancelMelee();
		TestEqual(Label + TEXT(" direct cancel also clears recovery"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestFalse(Label + TEXT(" direct cancel ends activation"), Ability->IsActive());
		TestTrue(Label + TEXT(" can activate after direct cancel"),
			ASC->TryActivateAbilityByClass(UGA_Melee::StaticClass()));
		TickMeleeWorld(World, 1.50f);
		TestEqual(Label + TEXT(" stage and combo-window timers release recovery"),
			ASC->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestFalse(Label + TEXT(" completed GAS activation ends"), Ability->IsActive());
	}
	return true;
}

#endif
