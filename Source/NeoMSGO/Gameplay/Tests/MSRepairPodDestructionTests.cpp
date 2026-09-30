#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSRepairPod.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRepairPodDestructionTest,
	"NeoMSGO.Gameplay.RepairPod.EnemyDestruction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSRepairPodDestructionTest
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSRepairPodDestructionWorld"));
		FWorldScope()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FWorldScope() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	};

	AMSPlayerCharacter* SpawnShooter(UWorld* World, const FVector& Location, const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Shooter = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Shooter)
		{
			Shooter->SetGenericTeamId(Team);
			Shooter->GetCharacterMovement()->DisableMovement();
		}
		return Shooter;
	}

	void ApplyDirectDamage(AMSPlayerCharacter* Shooter, AMSRepairPod* Pod, const float Damage)
	{
		FGameplayEffectContextHandle Context = Shooter->GetAbilitySystemComponent()->MakeEffectContext();
		Context.AddInstigator(Shooter, Shooter);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), Context, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Damage);
		Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0.0f);
		Shooter->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(
			Spec, Pod->GetAbilitySystemComponent());
	}
}

bool FMSRepairPodDestructionTest::RunTest(const FString&)
{
	using namespace MSRepairPodDestructionTest;
	FWorldScope Scope;
	AMSRepairPod* Pod = AMSRepairPod::Spawn(Scope.World, FVector::ZeroVector, 0.0f,
		MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Enemy = SpawnShooter(Scope.World, FVector(-1500, 0, 0), MSTeam::EnemyTeamId());
	AMSPlayerCharacter* Ally = SpawnShooter(Scope.World, FVector(1500, 0, 0), MSTeam::PlayerTeamId());
	if (!TestNotNull(TEXT("team pod spawned"), Pod) || !TestNotNull(TEXT("enemy spawned"), Enemy)
		|| !TestNotNull(TEXT("ally spawned"), Ally)) return false;
	TestTrue(TEXT("repair pod damage hitbox answers WeaponTrace"),
		Pod->GetDamageCollision()->GetCollisionResponseToChannel(MSCollision::WeaponTraceChannel) == ECR_Block);
	TestEqual(TEXT("provisional durability is configurable and initialized through GAS"),
		Pod->GetVitalSet()->GetMaxArmor(), 2000.0f);
	const FProperty* DestroyedProperty = FindFProperty<FProperty>(AMSRepairPod::StaticClass(), TEXT("bDestroyed"));
	TestTrue(TEXT("destroyed state is replicated"),
		DestroyedProperty && DestroyedProperty->HasAnyPropertyFlags(CPF_Net));

	const FVector EnemyRayStart(-1500.0f, 0.0f, 880.0f);
	const FMSHitscanResult EnemyTrace = UMSCombatStatics::ResolveHitscan(
		Scope.World, EnemyRayStart, FVector::ForwardVector, 3000.0f, Enemy);
	TestTrue(TEXT("enemy bullet reaches the pod, not decorative meshes"),
		EnemyTrace.bBlockingHit && EnemyTrace.Hit.GetActor() == Pod && EnemyTrace.bCanDamage);
	const FMSHitscanResult AllyTrace = UMSCombatStatics::ResolveHitscan(
		Scope.World, FVector(1500.0f, 0.0f, 880.0f), -FVector::ForwardVector, 3000.0f, Ally);
	TestTrue(TEXT("friendly bullet still collides with the installation"),
		AllyTrace.bBlockingHit && AllyTrace.Hit.GetActor() == Pod);
	TestFalse(TEXT("friendly bullet has no damage permission"), AllyTrace.bCanDamage);

	UMSRangedWeaponData* AllyGun = NewObject<UMSRangedWeaponData>(Ally);
	AllyGun->MagazineSize = 1; AllyGun->FireInterval = 0.0f; AllyGun->Range = 20.0f;
	AllyGun->Power = 500;
	Ally->GetWeaponComponent()->InitializeSlots({AllyGun});
	TStrongObjectPtr<UGA_Fire_Hitscan> AllyAbility(NewObject<UGA_Fire_Hitscan>(Ally));
	TestTrue(TEXT("friendly gun fires into the pod"), AllyAbility->Fire(
		Ally, FVector(1500.0f, 0.0f, 880.0f), -FVector::ForwardVector));
	TestEqual(TEXT("friendly direct hit leaves Armor unchanged"), Pod->GetVitalSet()->GetArmor(), 2000.0f);
	UMSCombatStatics::ApplySplashDamage(Scope.World, FVector(0, 0, 880), 1000.0f,
		500.0f, {}, Ally);
	TestEqual(TEXT("friendly blast leaves Armor unchanged"), Pod->GetVitalSet()->GetArmor(), 2000.0f);
	AMSPlayerCharacter* Neutral = SpawnShooter(Scope.World, FVector(-2500, 0, 0), FGenericTeamId::NoTeam);
	if (!TestNotNull(TEXT("neutral source spawned"), Neutral)) return false;
	ApplyDirectDamage(Neutral, Pod, 500.0f);
	TestEqual(TEXT("neutral source is not an enemy and cannot damage a team pod"),
		Pod->GetVitalSet()->GetArmor(), 2000.0f);

	UMSRangedWeaponData* EnemyGun = NewObject<UMSRangedWeaponData>(Enemy);
	EnemyGun->MagazineSize = 1; EnemyGun->FireInterval = 0.0f; EnemyGun->Range = 20.0f;
	EnemyGun->Power = 400;
	Enemy->GetWeaponComponent()->InitializeSlots({EnemyGun});
	TStrongObjectPtr<UGA_Fire_Hitscan> EnemyAbility(NewObject<UGA_Fire_Hitscan>(Enemy));
	TestTrue(TEXT("enemy gun fires into the pod"), EnemyAbility->Fire(
		Enemy, EnemyRayStart, FVector::ForwardVector));
	TestTrue(TEXT("hostile hitscan follows common GAS damage path"),
		Pod->GetVitalSet()->GetArmor() < 2000.0f);
	const float AfterBullet = Pod->GetVitalSet()->GetArmor();
	UMSCombatStatics::ApplySplashDamage(Scope.World, FVector(0, 0, 880), 1000.0f,
		300.0f, {}, Enemy);
	TestTrue(TEXT("hostile explosion damages the same pod ASC"),
		Pod->GetVitalSet()->GetArmor() < AfterBullet);

	UMSMeleeWeaponData* EnemySword = NewObject<UMSMeleeWeaponData>(Enemy);
	EnemySword->Range = 3.0f; EnemySword->Power = 200; EnemySword->DownPoint = 0.0f;
	EnemySword->MagazineSize = 0; EnemySword->FireInterval = 0.0f;
	Enemy->GetWeaponComponent()->InitializeSlots({EnemySword});
	TStrongObjectPtr<UGA_Melee> Melee(NewObject<UGA_Melee>(Enemy));
	const float BeforeMelee = Pod->GetVitalSet()->GetArmor();
	TestTrue(TEXT("enemy melee swing executes"), Melee->PerformMelee(
		Enemy, EnemyRayStart, FVector::ForwardVector, false));
	TestTrue(TEXT("hostile melee overlap damages the pod"),
		Pod->GetVitalSet()->GetArmor() < BeforeMelee);

	ApplyDirectDamage(Enemy, Pod, 5000.0f);
	TestTrue(TEXT("zero Armor disables repairs"), Pod->IsDestroyed());
	TestEqual(TEXT("destroyed pod has zero Armor"), Pod->GetVitalSet()->GetArmor(), 0.0f);
	TestTrue(TEXT("destroyed state carries the GAS dead tag"),
		Pod->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestTrue(TEXT("destroyed pod no longer intercepts fire or melee"),
		Pod->GetDamageCollision()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestFalse(TEXT("destroyed pod cannot be locked on"), Pod->CanBeLockedOn());
	Ally->GetVitalSet()->SetArmor(500.0f);
	Pod->RepairUnitsInRange();
	TestEqual(TEXT("destroyed pod cannot heal an in-range ally"),
		Ally->GetVitalSet()->GetArmor(), 500.0f);
	const float ArmorAfterDeath = Pod->GetVitalSet()->GetArmor();
	ApplyDirectDamage(Enemy, Pod, 5000.0f);
	TestEqual(TEXT("repeat death packet cannot reduce Armor or re-enable the pod"),
		Pod->GetVitalSet()->GetArmor(), ArmorAfterDeath);
	TestTrue(TEXT("repeat packet leaves the pod disabled"), Pod->IsDestroyed());

	// Simulate a late-joining proxy receiving the replicated property. The same
	// rep-notify restores collision, lights and range presentation from the bit.
	AMSRepairPod* ProxyPod = AMSRepairPod::Spawn(Scope.World, FVector(10000, 0, 0), 0.0f,
		MSTeam::PlayerTeamId());
	if (!TestNotNull(TEXT("late proxy fixture spawned"), ProxyPod)) return false;
	ProxyPod->ApplyReplicatedDestroyedStateForTesting(true);
	TestTrue(TEXT("late proxy receives disabled collision"),
		ProxyPod->GetDamageCollision()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	ProxyPod->ApplyReplicatedDestroyedStateForTesting(false);
	TestTrue(TEXT("rep-notify can restore intact collision"),
		ProxyPod->GetDamageCollision()->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
	ProxyPod->ApplyReplicatedDestroyedStateForTesting(true);
	TestTrue(TEXT("rep-notify re-applies destroyed collision state"),
		ProxyPod->GetDamageCollision()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TArray<UPointLightComponent*> Lights;
	ProxyPod->GetComponents(Lights);
	for (const UPointLightComponent* Light : Lights)
		TestFalse(TEXT("replicated destruction extinguishes service lights"), Light->IsVisible());
	return true;
}

#endif
