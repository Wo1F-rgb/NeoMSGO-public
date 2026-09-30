// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Guard.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGuardAbilityTest,
	"NeoMSGO.Weapons.Guard.ToggleAndWeaponSwitch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSGuardAbilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSGuardAbilityTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();

	UMSRangedWeaponData* W0 = NewObject<UMSRangedWeaponData>(Unit);
	UMSRangedWeaponData* W1 = NewObject<UMSRangedWeaponData>(Unit);
	Weapons->InitializeSlots({W0, W1});

	const FGameplayAbilitySpecHandle GuardHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Guard::StaticClass()));
	TestFalse(TEXT("direct GA_Guard toggle is rejected for native shields"),
		NewObject<UGA_Guard>(Unit)->ToggleGuard(Unit));
	TestTrue(TEXT("legacy ability activation returns without granting guard"), ASC->TryActivateAbility(GuardHandle));
	TestEqual(TEXT("manual press cannot grant State.Guarding"), ASC->GetTagCount(MSTags::State_Guarding), 0);
	TestTrue(TEXT("weapon switch remains available"), Weapons->SwitchToSlot(1));
	TestEqual(TEXT("switch leaves manual guard absent"), ASC->GetTagCount(MSTags::State_Guarding), 0);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
