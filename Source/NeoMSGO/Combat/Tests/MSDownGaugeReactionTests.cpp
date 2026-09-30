// Copyright NeoMSGO Project. All Rights Reserved.
// Post-2019 down-gauge reaction contract through real player units and GE_Damage.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSHitReactionComponent.h"
#include "Components/BoxComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDownGaugeTierReactionTest,
	"NeoMSGO.Combat.DownGaugeReaction.TiersAndFireGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDownGaugeAirLandingTest,
	"NeoMSGO.Combat.DownGaugeReaction.AirLandingRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDownGaugeDeathResetTest,
	"NeoMSGO.Combat.DownGaugeReaction.DeathAndReset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSDownGaugeReactionTests
{
	struct FOfflineAuth
	{
		FString Previous = FCommandLine::Get();
		FOfflineAuth()
		{
			TArray<FString> Tokens;
			Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase)) continue;
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			FCommandLine::Set(*(Effective + TEXT("-MSAuthMode=OfflineLocal")));
		}
		~FOfflineAuth() { FCommandLine::Set(*Previous); }
	};

	struct FWorld
	{
		UWorld* World = nullptr;
		FWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSDownGaugeReactionWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}
		~FWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickDownGaugeWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.05f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSPlayerCharacter* SpawnPlayer(UWorld* World, const FVector& Location,
		const FGenericTeamId Team, const int32 DownGaugeLevel, UMSMobileSuitData*& OutData)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (!Player) return nullptr;
		Player->SetGenericTeamId(Team);
		OutData = NewObject<UMSMobileSuitData>(Player);
		OutData->MaxArmor = 10000;
		OutData->MaxDownPoint = 100; // Serialized prototype value maps to the level capacity.
		OutData->DownGaugeLevel = DownGaugeLevel;
		Player->InitializeUnit(OutData);
		Player->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		return Player;
	}

	void ApplyDamage(AMSPlayerCharacter* Source, AMSPlayerCharacter* Target,
		const float ArmorDamage, const float DownPoint, const bool bForceKnockdown = false)
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Source, Source);
		Context->SetAttackerLocation(Source->GetActorLocation());
		Context->SetForceKnockdown(bForceKnockdown);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), FGameplayEffectContextHandle(Context), 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, ArmorDamage);
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, DownPoint);
		Source->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(
			Spec, Target->GetAbilitySystemComponent());
	}

	AActor* AddSolidGround(UWorld* World)
	{
		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Ground);
		Ground->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(20000.0f, 20000.0f, 100.0f));
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0f, 0.0f, -100.0f)); // floor top at Z=0
		return Ground;
	}
}

bool FMSDownGaugeTierReactionTest::RunTest(const FString&)
{
	using namespace MSDownGaugeReactionTests;
	FOfflineAuth Auth;
	FWorld Scope;
	UMSMobileSuitData* SourceData = nullptr;
	AMSPlayerCharacter* Source = SpawnPlayer(Scope.World, FVector(0, -30000, 1000),
		MSTeam::EnemyTeamId(), 1, SourceData);
	if (!TestNotNull(TEXT("opposing real player attacker exists"), Source)) return false;

	for (int32 Level = 1; Level <= 3; ++Level)
	{
		UMSMobileSuitData* Data = nullptr;
		AMSPlayerCharacter* Target = SpawnPlayer(Scope.World, FVector(0, Level * 5000.0f, 1000),
			MSTeam::PlayerTeamId(), Level, Data);
		if (!TestNotNull(TEXT("real player target exists"), Target)) return false;
		Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		UAbilitySystemComponent* ASC = Target->GetAbilitySystemComponent();
		UMSAttributeSet_Vital* Vital = Target->GetVitalSet();
		UMSHitReactionComponent* Reaction = Target->GetHitReactionComponent();
		if (!TestNotNull(TEXT("player owns authority reaction component"), Reaction)) return false;
		const float Capacity = 1500.0f + 100.0f * Level;
		TestEqual(TEXT("suit level resolves to the real GAS DP capacity"), Vital->GetMaxDownPoint(), Capacity);
		TestEqual(TEXT("initial reaction is clear"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));

		UMSRangedWeaponData* Weapon = nullptr;
		if (Level == 1)
		{
			Weapon = NewObject<UMSRangedWeaponData>(Target);
			Weapon->MagazineSize = 1;
			Weapon->ReserveAmmo = 0;
			Weapon->FireInterval = 0.5f;
			Weapon->Power = 1;
			Weapon->Range = 10.0f;
			Target->GetWeaponComponent()->InitializeSlots({Weapon});
			TestTrue(TEXT("uninjured player can fire equipped hitscan weapon"),
				Target->GetWeaponComponent()->CanFire(0));
		}

		ApplyDamage(Source, Target, 1.0f, 799.0f);
		TestEqual(TEXT("799 DP stays below the shared flinch threshold"), Vital->GetDownPoint(), 799.0f);
		TestEqual(TEXT("799 DP has no reaction"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));
		ApplyDamage(Source, Target, 1.0f, 1.0f);
		TestEqual(TEXT("exactly 800 DP starts flinch"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::Flinch));
		TestEqual(TEXT("flinch retains the accumulated 800 DP"), Vital->GetDownPoint(), 800.0f);
		TestTrue(TEXT("flinch blocks combat while allowing subsequent DP"),
			UMSHitReactionComponent::BlocksCombat(ASC) && !UMSHitReactionComponent::ProtectsDownGauge(ASC));
		if (Level == 1)
		{
			const int32 AmmoBefore = Target->GetWeaponComponent()->GetMagazineAmmo(0);
			TestFalse(TEXT("flinch blocks the actual weapon component fire gate"),
				Target->GetWeaponComponent()->CanFire(0));
			TestFalse(TEXT("flinch rejects player fire input"), Target->StartCurrentWeaponFire());
			TestEqual(TEXT("rejected flinch shot spends no ammunition"),
				Target->GetWeaponComponent()->GetMagazineAmmo(0), AmmoBefore);
		}

		const float ArmorBeforeFollowUp = Vital->GetArmor();
		ApplyDamage(Source, Target, 1.0f, Capacity - 801.0f);
		TestEqual(TEXT("flinch follow-up below the level capacity accumulates DP"),
			Vital->GetDownPoint(), Capacity - 1.0f);
		TestEqual(TEXT("flinch follow-up below capacity does not cause heavy reaction"),
			static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::Flinch));
		TestTrue(TEXT("flinch follow-up still damages armor"), Vital->GetArmor() < ArmorBeforeFollowUp);
		ApplyDamage(Source, Target, 1.0f, 1.0f);
		TestEqual(TEXT("exact level capacity causes grounded stagger"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::Stagger));
		TestFalse(TEXT("ground stagger remains upright"), Reaction->IsProneReaction());
		TestEqual(TEXT("heavy reaction resets the DP gauge"), Vital->GetDownPoint(), 0.0f);
		TestTrue(TEXT("heavy reaction protects DP and blocks actions"),
			UMSHitReactionComponent::ProtectsDownGauge(ASC) && UMSHitReactionComponent::BlocksCombat(ASC));
		const float ArmorBeforeProtectedHit = Vital->GetArmor();
		ApplyDamage(Source, Target, 1.0f, 400.0f);
		TestTrue(TEXT("armor remains vulnerable during heavy reaction"), Vital->GetArmor() < ArmorBeforeProtectedHit);
		TestEqual(TEXT("DP does not accumulate during heavy reaction"), Vital->GetDownPoint(), 0.0f);
		TestEqual(TEXT("protected heavy reaction does not restart"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::Stagger));
		if (Level == 1)
		{
			TickDownGaugeWorld(Scope.World, UMSHitReactionComponent::StaggerSeconds + 0.10f);
			TestEqual(TEXT("ground stagger continues through a getting-up phase"),
				static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::GettingUp));
			TestFalse(TEXT("ground getting-up phase never uses prone pose"), Reaction->IsProneReaction());
			TestFalse(TEXT("getting up still blocks fire"), Target->GetWeaponComponent()->CanFire(0));
			TickDownGaugeWorld(Scope.World, UMSHitReactionComponent::GetUpSeconds + 0.10f);
			TestEqual(TEXT("ground reaction ends after getting up"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));
			TestTrue(TEXT("fire gate reopens after reaction"), Target->GetWeaponComponent()->CanFire(0));
			TestTrue(TEXT("player can actually fire after reaction clears"), Target->StartCurrentWeaponFire());
			TestEqual(TEXT("accepted shot spends exactly one round"),
				Target->GetWeaponComponent()->GetMagazineAmmo(0), 0);
			Target->StopCurrentWeaponFire();
		}
	}
	return true;
}

bool FMSDownGaugeAirLandingTest::RunTest(const FString&)
{
	using namespace MSDownGaugeReactionTests;
	FOfflineAuth Auth;
	FWorld Scope;
	if (!TestNotNull(TEXT("collision floor exists"), AddSolidGround(Scope.World))) return false;
	UMSMobileSuitData* SourceData = nullptr;
	UMSMobileSuitData* TargetData = nullptr;
	AMSPlayerCharacter* Source = SpawnPlayer(Scope.World, FVector(0, -30000, 1000),
		MSTeam::EnemyTeamId(), 1, SourceData);
	AMSPlayerCharacter* Target = SpawnPlayer(Scope.World, FVector(0, 0, 9000),
		MSTeam::PlayerTeamId(), 2, TargetData);
	if (!TestNotNull(TEXT("air attacker exists"), Source)
		|| !TestNotNull(TEXT("air target exists"), Target)) return false;
	UCharacterMovementComponent* Movement = Target->GetCharacterMovement();
	UMSHitReactionComponent* Reaction = Target->GetHitReactionComponent();
	Movement->SetMovementMode(MOVE_Falling);
	const double InitialZ = Target->GetActorLocation().Z;
	ApplyDamage(Source, Target, 1.0f, Target->GetVitalSet()->GetMaxDownPoint());
	TestEqual(TEXT("airborne capacity hit starts FallingDown"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::FallingDown));
	TestTrue(TEXT("airborne heavy reaction uses prone pose"), Reaction->IsProneReaction());
	TestEqual(TEXT("airborne heavy reaction resets DP"), Target->GetVitalSet()->GetDownPoint(), 0.0f);
	TickDownGaugeWorld(Scope.World, UMSHitReactionComponent::FallBlendSeconds + 0.10f);
	TestEqual(TEXT("falling phase does not advance to Downed before actual contact"),
		static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::FallingDown));
	bool bLanded = false;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		TickDownGaugeWorld(Scope.World, 0.05f);
		if (Movement->IsMovingOnGround() && Reaction->GetPhase() == EMSHitReactionPhase::Downed)
		{
			bLanded = true;
			break;
		}
	}
	TestTrue(TEXT("real character fall reaches the blocking floor and enters Downed"), bLanded);
	TestTrue(TEXT("fall traverses world distance instead of teleporting"), Target->GetActorLocation().Z < InitialZ - 100.0);
	if (!bLanded) return false;
	TickDownGaugeWorld(Scope.World, UMSHitReactionComponent::DownedSeconds + 0.10f);
	TestEqual(TEXT("Downed advances to GettingUp after its own duration"),
		static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::GettingUp));
	TestTrue(TEXT("prone reaction remains marked through getting up"), Reaction->IsProneReaction());
	TestTrue(TEXT("getting up still blocks combat"),
		UMSHitReactionComponent::BlocksCombat(Target->GetAbilitySystemComponent()));
	TickDownGaugeWorld(Scope.World, UMSHitReactionComponent::GetUpSeconds + 0.10f);
	TestEqual(TEXT("landing recovery reaches the playable state"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));
	TestFalse(TEXT("playable state no longer blocks combat"),
		UMSHitReactionComponent::BlocksCombat(Target->GetAbilitySystemComponent()));
	return true;
}

bool FMSDownGaugeDeathResetTest::RunTest(const FString&)
{
	using namespace MSDownGaugeReactionTests;
	FOfflineAuth Auth;
	FWorld Scope;
	UMSMobileSuitData* SourceData = nullptr;
	UMSMobileSuitData* TargetData = nullptr;
	AMSPlayerCharacter* Source = SpawnPlayer(Scope.World, FVector(0, -30000, 1000),
		MSTeam::EnemyTeamId(), 1, SourceData);
	AMSPlayerCharacter* Target = SpawnPlayer(Scope.World, FVector(0, 0, 1000),
		MSTeam::PlayerTeamId(), 3, TargetData);
	if (!TestNotNull(TEXT("death attacker exists"), Source)
		|| !TestNotNull(TEXT("death target exists"), Target)) return false;
	UAbilitySystemComponent* ASC = Target->GetAbilitySystemComponent();
	UMSHitReactionComponent* Reaction = Target->GetHitReactionComponent();
	Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	ApplyDamage(Source, Target, 1.0f, 800.0f);
	TestEqual(TEXT("death fixture starts in flinch"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::Flinch));
	ApplyDamage(Source, Target, Target->GetVitalSet()->GetMaxArmor() * 2.0f, 0.0f);
	TestTrue(TEXT("lethal real damage GE grants State.Dead"), ASC->HasMatchingGameplayTag(MSTags::State_Dead));
	TickDownGaugeWorld(Scope.World, 0.10f);
	TestEqual(TEXT("death clears reaction on authority tick"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));
	TestEqual(TEXT("death clears flinch tag"), ASC->GetTagCount(MSTags::State_Flinched), 0);
	Target->InitializeUnit(TargetData);
	TestFalse(TEXT("unit initialization removes death state"), ASC->HasMatchingGameplayTag(MSTags::State_Dead));
	TestEqual(TEXT("unit initialization restores full armor"),
		Target->GetVitalSet()->GetArmor(), Target->GetVitalSet()->GetMaxArmor());
	TestEqual(TEXT("unit initialization restores empty down gauge"), Target->GetVitalSet()->GetDownPoint(), 0.0f);
	TestEqual(TEXT("unit initialization preserves level-three capacity"), Target->GetVitalSet()->GetMaxDownPoint(), 1800.0f);
	TestEqual(TEXT("unit initialization leaves no reaction phase"), static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::None));

	// A D-melee context forces a fall from low DP independently of the 800/1800 thresholds.
	ApplyDamage(Source, Target, 1.0f, 1.0f, true);
	TestEqual(TEXT("forced knockdown context starts FallingDown below threshold"),
		static_cast<uint8>(Reaction->GetPhase()), static_cast<uint8>(EMSHitReactionPhase::FallingDown));
	TestEqual(TEXT("forced knockdown resets the small DP packet"), Target->GetVitalSet()->GetDownPoint(), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
