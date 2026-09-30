#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotCharacter.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSShieldComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSRepairPod.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRepairPodP75Test, "NeoMSGO.Gameplay.RepairPod.P7_5",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSRepairPodTestP75
{
	struct FScopedRepairWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSRepairPodTestP75World"));
		FScopedRepairWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedRepairWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	void AdvanceRepairWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.05f);
			World->Tick(LEVELTICK_All, Step); Seconds -= Step; ++GFrameCounter;
		}
	}

	AMSBotCharacter* SpawnRepairBot(UWorld* World, const FVector& Location, const FGenericTeamId Team, float Armor)
	{
		// This world has no floor. Keep the repair fixtures stationary so the test measures the
		// pod radius rather than the bot movement component's gravity/AI behaviour.
		AMSBotCharacter* Unit = World->SpawnActorDeferred<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
			FTransform(FRotator::ZeroRotator, Location), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->AutoPossessAI = EAutoPossessAI::Disabled;
		Unit->SetGenericTeamId(Team);
		Unit->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
		Unit->GetCharacterMovement()->DisableMovement();
		Unit->GetVitalSet()->SetMaxArmor(500.0f); Unit->GetVitalSet()->SetArmor(Armor);
		return Unit;
	}
}

bool FMSRepairPodP75Test::RunTest(const FString&)
{
	using namespace MSRepairPodTestP75;
	FScopedRepairWorld Scope;
	AMSBotCharacter* Ally = SpawnRepairBot(Scope.World, FVector(100, 0, 0) * MSGO::MSScale, MSTeam::PlayerTeamId(), 200.0f);
	AMSBotCharacter* Enemy = SpawnRepairBot(Scope.World, FVector(200, 0, 0) * MSGO::MSScale, MSTeam::EnemyTeamId(), 200.0f);
	AMSBotCharacter* Outside = SpawnRepairBot(Scope.World, FVector(601, 0, 0) * MSGO::MSScale, MSTeam::PlayerTeamId(), 200.0f);
	AMSBotCharacter* Dead = SpawnRepairBot(Scope.World, FVector(300, 0, 0) * MSGO::MSScale, MSTeam::PlayerTeamId(), 200.0f);
	Dead->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	UMSMobileSuitData* ShieldSuit = NewObject<UMSMobileSuitData>(Ally, TEXT("RepairShieldSuit"));
	UMSShieldWeaponData* ShieldData = NewObject<UMSShieldWeaponData>(ShieldSuit, TEXT("RepairShieldData"));
	ShieldData->MaxDurability = 300.0f;
	ShieldSuit->NativeShield = ShieldData;
	Ally->InitializeUnit(ShieldSuit);
	Ally->GetVitalSet()->SetMaxArmor(500.0f); Ally->GetVitalSet()->SetArmor(200.0f);

	AMSRepairPod* NeutralPod = AMSRepairPod::Spawn(Scope.World, FVector::ZeroVector);
	float TowerTopCm = 0.0f;
	TArray<UStaticMeshComponent*> PodMeshes;
	NeutralPod->GetComponents(PodMeshes);
	for (const UStaticMeshComponent* Mesh : PodMeshes)
	{
		if (Mesh && !Mesh->GetName().StartsWith(TEXT("HealRangeSegment")))
		{
			TowerTopCm = FMath::Max(TowerTopCm, Mesh->Bounds.GetBox().Max.Z - NeutralPod->GetActorLocation().Z);
		}
	}
	const float CharacterHeightCm = Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f;
	TestTrue(TEXT("tower top preserves the approved 190-220 cm ratio at 18 m scale"),
		TowerTopCm >= 190.0f * MSGO::MSScale && TowerTopCm <= 220.0f * MSGO::MSScale);
	TestTrue(TEXT("tower top does not exceed twice the character capsule height"),
		TowerTopCm <= CharacterHeightCm * 2.0f);
	AdvanceRepairWorld(Scope.World, 1.05f);
	TestEqual(TEXT("living in-range unit receives 150 Armor after one second"), Ally->GetVitalSet()->GetArmor(), 350.0f);
	TestEqual(TEXT("neutral pod repairs enemy too"), Enemy->GetVitalSet()->GetArmor(), 350.0f);
	TestEqual(TEXT("outside unit is not repaired"), Outside->GetVitalSet()->GetArmor(), 200.0f);
	TestEqual(TEXT("dead unit is excluded"), Dead->GetVitalSet()->GetArmor(), 200.0f);
	UMSShieldComponent* AllyShield = Ally->GetShieldComponent();
	TestNotNull(TEXT("repair target has a native shield"), AllyShield);
	float ArmorDamage = 0.0f, ShieldDamage = 0.0f; bool bGuarded = false;
	TestTrue(TEXT("repair test breaks the native shield first"), AllyShield && AllyShield->TryAbsorbDamage(
		300.0f, false, FVector(1000.0f, 0.0f, 0.0f), ArmorDamage, ShieldDamage, bGuarded));
	TestTrue(TEXT("repair pod sees the broken shield"), AllyShield && AllyShield->IsShieldBroken());
	NeutralPod->RepairUnitsInRange();
	TestTrue(TEXT("partial repair leaves a broken shield unavailable"), AllyShield && AllyShield->IsShieldBroken());
	NeutralPod->RepairUnitsInRange();
	TestFalse(TEXT("repair pod reactivates shield at full durability"), AllyShield && AllyShield->IsShieldBroken());
	TestEqual(TEXT("repair pod restores shield durability"), AllyShield ? AllyShield->GetCurrentDurability() : 0.0f, 300.0f);
	NeutralPod->RepairUnitsInRange();
	TestEqual(TEXT("Armor recovery clamps at MaxArmor"), Ally->GetVitalSet()->GetArmor(), 500.0f);
	NeutralPod->Destroy();

	Ally->GetVitalSet()->SetArmor(100.0f);
	Enemy->GetVitalSet()->SetArmor(100.0f);
	Outside->GetVitalSet()->SetArmor(100.0f);
	AMSRepairPod* TeamPod = AMSRepairPod::Spawn(Scope.World, FVector::ZeroVector, 0.0f, MSTeam::PlayerTeamId());
	TeamPod->RepairUnitsInRange();
	TestEqual(TEXT("in-range allied unit gains actual Armor"), Ally->GetVitalSet()->GetArmor(), 250.0f);
	TestEqual(TEXT("in-range enemy unit gains no Armor"), Enemy->GetVitalSet()->GetArmor(), 100.0f);
	TestEqual(TEXT("out-of-range allied unit gains no Armor"), Outside->GetVitalSet()->GetArmor(), 100.0f);
	TeamPod->Destroy();

	UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Ally);
	Weapon->MagazineSize = 10; Weapon->ReserveAmmo = 100; Weapon->FireInterval = 0.0f; Weapon->ReloadTime = 0.01f;
	UMSWeaponComponent* Weapons = Ally->GetWeaponComponent(); Weapons->InitializeSlots({Weapon});
	for (int32 Cycle = 0; Cycle < 2; ++Cycle)
	{
		for (int32 Round = 0; Round < 10; ++Round) TestTrue(TEXT("ammo setup consumes magazine"), Weapons->ConsumeAmmoDuringBurst(0));
		TestTrue(TEXT("magazine depletion auto-starts reload"), Weapons->IsReloading(0)); AdvanceRepairWorld(Scope.World, 0.02f);
	}
	TestEqual(TEXT("ammo setup reserve"), Weapons->GetReserveAmmo(0), 80);
	AMSRepairPod* AmmoPod = AMSRepairPod::Spawn(Scope.World, FVector::ZeroVector, 0.0f, MSTeam::PlayerTeamId());
	AmmoPod->RepairUnitsInRange();
	TestEqual(TEXT("ammo recovers 15 percent of maximum reserve"), Weapons->GetReserveAmmo(0), 95);
	AmmoPod->Destroy();

	AMSRepairPod* TimedPod = AMSRepairPod::Spawn(Scope.World, FVector(2000, 0, 0) * MSGO::MSScale, 0.1f);
	TWeakObjectPtr<AMSRepairPod> TimedPodWeak = TimedPod;
	AdvanceRepairWorld(Scope.World, 0.15f);
	TestFalse(TEXT("positive Lifetime destroys pod"), TimedPodWeak.IsValid());
	return true;
}

#endif
