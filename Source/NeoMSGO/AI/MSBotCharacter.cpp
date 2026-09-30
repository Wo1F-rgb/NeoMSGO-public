#include "AI/MSBotCharacter.h"
#include "AI/MSBotAIController.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AI/GA_BotKick.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/GA_Fire_Burst.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/GA_Fire_ContinuousBeam.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Fire_Homing.h"
#include "Weapons/GA_Fire_Shotgun.h"
#include "Weapons/MSWeaponComponent.h"
#include "Data/MSWeaponData.h"
#include "Animation/MSMechRigComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSCharacterMovementComponent.h"

AMSBotCharacter::AMSBotCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMSCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	TeamId = MSTeam::EnemyTeamId();
	AIControllerClass = AMSBotAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// StateTree destinations are intentionally shared (the same target/capture route), so use
	// CharacterMovement RVO as the last local layer after the AI's wider separation steering.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bUseRVOAvoidance = true;
		Movement->AvoidanceConsiderationRadius = AIConfig.RVOAvoidanceRadius;
		Movement->AvoidanceWeight = 0.65f;
	}

	RespawnComponent = CreateDefaultSubobject<UMSRespawnComponent>(TEXT("RespawnComponent"));
	RespawnComponent->SetRespawnTimings(/*ExplosionDelay=*/3.0f, /*RespawnDelay=*/9.0f);
	RespawnComponent->SetAutoRespawnEnabled(true);
	LockOnComponent = CreateDefaultSubobject<UMSLockOnComponent>(TEXT("BotHomingLockOn"));
	LockOnComponent->ConfigureAIHomingOnly();
}

void AMSBotCharacter::InitializeUnit(UMSMobileSuitData* Data)
{
	Super::InitializeUnit(Data);
	if (LockOnComponent) LockOnComponent->ResetForDeployment();
}

void AMSBotCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnemyTierVariant = CalculateEnemyTierVariant(EnemyTier);
	if (UMSMechRigComponent* Rig = GetMechRigComponent())
	{
		Rig->ApplyAppearance(GetGenericTeamId().GetId(), true, EnemyTierVariant);
	}
}

void AMSBotCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// Reapply instance/BP config after construction; designers can tune the radius per BOT class.
		Movement->bUseRVOAvoidance = true;
		Movement->AvoidanceConsiderationRadius = AIConfig.RVOAvoidanceRadius;
		if (GetWorld() && AMSBotAIController::IsTaiganNavigationMap(GetWorld()->GetMapName()))
		{
			const float CapsuleRadius = GetCapsuleComponent()
				? GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
			Movement->AvoidanceConsiderationRadius =
				AMSBotAIController::CalculateTaiganRVOConsiderationRadius(
					CapsuleRadius, Movement->MaxWalkSpeed, AIConfig.RVOAvoidanceRadius);
		}
	}
	PatrolCenter = GetActorLocation();
	EnemyTierVariant = CalculateEnemyTierVariant(EnemyTier);
	if (UMSMechRigComponent* Rig = GetMechRigComponent())
	{
		Rig->ApplyAppearance(GetGenericTeamId().GetId(), true, EnemyTierVariant);
	}
	RespawnComponent->OnExplosionDue.AddUniqueDynamic(this, &AMSBotCharacter::HandleExplosionDue);
	RespawnComponent->OnDeployed.AddUniqueDynamic(this, &AMSBotCharacter::HandleDeployed);
	if (HasAuthority() && AbilitySystemComponent)
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Melee::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_BotKick::StaticClass()));
		// AI activates ranged weapons through Ability.Fire tags. Grant every native route once;
		// MSWeaponAbilityRouting selects the class matching the equipped weapon data at runtime.
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_Hitscan::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_Shotgun::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_Burst::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_Homing::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_Charge::StaticClass()));
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fire_ContinuousBeam::StaticClass()));
	}
}

void AMSBotCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RespawnComponent)
	{
		RespawnComponent->OnExplosionDue.RemoveDynamic(this, &AMSBotCharacter::HandleExplosionDue);
		RespawnComponent->OnDeployed.RemoveDynamic(this, &AMSBotCharacter::HandleDeployed);
	}
	Super::EndPlay(EndPlayReason);
}

void AMSBotCharacter::HandleExplosionDue(AMSUnitBase* Unit, const FVector DeathLocation)
{
	if (Unit != this)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		FGameplayCueParameters Parameters;
		Parameters.Location = DeathLocation;
		AbilitySystemComponent->ExecuteGameplayCue(MSTags::Cue_Explosion, Parameters);
	}

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AMSBotCharacter::HandleDeployed(AMSUnitBase* Unit)
{
	if (Unit != this)
	{
		return;
	}

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AMSBotCharacter::SetEnemyTier(const EMSEnemyTier NewTier)
{
	EnemyTier = NewTier;
	EnemyTierVariant = CalculateEnemyTierVariant(EnemyTier);
	if (UMSMechRigComponent* Rig = GetMechRigComponent())
	{
		Rig->ApplyAppearance(GetGenericTeamId().GetId(), true, EnemyTierVariant);
	}
}

TArray<FSoftObjectPath> AMSBotCharacter::GetTierWeaponAssetPaths(const EMSEnemyTier Tier)
{
	switch (Tier)
	{
	case EMSEnemyTier::Medium:
		// Blue close-assault BOT: preserve the existing melee identity.
		return {FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_HeatSwordHS1.DA_Weapon_HeatSwordHS1"))};
	case EMSEnemyTier::Strong:
		// Gun first for spawn/engage range; controller changes to bazooka at long distance.
		return {
			FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_BR.DA_Weapon_BR")),
			FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_MidBazookaMB6.DA_Weapon_MidBazookaMB6"))};
	case EMSEnemyTier::Weak:
	default:
		return {FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_LightMachineGunLM6.DA_Weapon_LightMachineGunLM6"))};
	}
}

bool AMSBotCharacter::ApplyTierWeaponLoadout()
{
	UMSWeaponComponent* Weapons = GetWeaponComponent();
	if (!Weapons)
	{
		return false;
	}
	TArray<UMSWeaponDataBase*> LoadedWeapons;
	for (const FSoftObjectPath& Path : GetTierWeaponAssetPaths(EnemyTier))
	{
		if (UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(Path.TryLoad()))
		{
			LoadedWeapons.Add(Weapon);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSBotCharacter: tier weapon asset is missing: %s"), *Path.ToString());
		}
	}
	if (LoadedWeapons.IsEmpty())
	{
		return false;
	}
	Weapons->InitializeSlots(LoadedWeapons);
	return true;
}

void AMSBotCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	Super::SetGenericTeamId(NewTeamId);
	if (UMSMechRigComponent* Rig = GetMechRigComponent())
	{
		Rig->ApplyAppearance(NewTeamId.GetId(), true, EnemyTierVariant);
	}
}

int32 AMSBotCharacter::CalculateEnemyTierVariant(const EMSEnemyTier Tier)
{
	switch (Tier)
	{
	case EMSEnemyTier::Weak: return 0;
	case EMSEnemyTier::Medium: return 1;
	case EMSEnemyTier::Strong: return 2;
	default: return 0;
	}
}

bool AMSBotCharacter::IsAllyVisualVariant() const
{
	return UMSTeamStatics::IsOnTeam(this, MSTeam::PlayerTeamId());
}

int32 AMSBotCharacter::GetVisualVariantId() const
{
	return CalculateVisualVariantId(EnemyTier, IsAllyVisualVariant());
}

int32 AMSBotCharacter::CalculateVisualVariantId(const EMSEnemyTier Tier, const bool bAllyVisualVariant)
{
	constexpr int32 AllyVariantOffset = 3;
	return CalculateEnemyTierVariant(Tier) + (bAllyVisualVariant ? AllyVariantOffset : 0);
}

FMSCombatModifiers AMSBotCharacter::GetCombatModifiers() const
{
	FMSCombatModifiers Modifiers;
	Modifiers.DamageMultiplier = WeakeningParams.DamageMultiplier;
	Modifiers.AimSpreadDegrees = WeakeningParams.AimSpreadDegrees;
	Modifiers.FireIntervalMultiplier = WeakeningParams.FireIntervalMultiplier;
	Modifiers.MeleeFireIntervalMultiplier = WeakeningParams.GetMeleeFireIntervalMultiplier();
	return Modifiers;
}
