// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemGlobals.h"
#include "Audio/MSAudioPlayback.h"
#include "Combat/Cues/MSGameplayCue_Fire.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayCueManager.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Fire_Homing.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOrdnanceFireAudioTest,
	"NeoMSGO.Weapons.OrdnanceFireAudio",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FOrdnanceAudioWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSOrdnanceAudioWorld"));

		FOrdnanceAudioWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FOrdnanceAudioWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSPlayerCharacter* SpawnShooter(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}
}

bool FMSOrdnanceFireAudioTest::RunTest(const FString& Parameters)
{
	FOrdnanceAudioWorld Scope;
	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	int32 BazookaFireCueCount = 0;
	int32 HomingFireCueCount = 0;
	const FDelegateHandle CueHandle = CueManager->OnGameplayCueRouted().AddLambda(
		[&](AActor*, const FGameplayTag Tag, EGameplayCueEvent::Type EventType,
			const FGameplayCueParameters& CueParameters, EGameplayCueExecutionOptions)
		{
			if (Tag != MSTags::Cue_Weapon_Fire || EventType != EGameplayCueEvent::Executed)
			{
				return;
			}
			const UMSWeaponDataBase* Weapon = Cast<UMSWeaponDataBase>(CueParameters.SourceObject.Get());
			BazookaFireCueCount += Weapon && Weapon->CategoryTag == MSTags::Weapon_Category_Bazooka;
			HomingFireCueCount += Weapon && Weapon->CategoryTag == MSTags::Weapon_Category_Homing;
		});

	const UMSGameplayCue_Fire* FireCue = GetDefault<UMSGameplayCue_Fire>();
	USoundBase* BazookaSound = LoadObject<USoundBase>(
		nullptr, TEXT("/Game/Audio/Imported/SFX_Fire_Bazooka.SFX_Fire_Bazooka"));
	if (!TestNotNull(TEXT("authored bazooka fire SoundWave loads"), BazookaSound))
	{
		CueManager->OnGameplayCueRouted().Remove(CueHandle);
		return false;
	}

	AMSPlayerCharacter* BazookaShooter = SpawnShooter(Scope.World, FVector::ZeroVector);
	UMSRangedWeaponData* Bazooka = NewObject<UMSRangedWeaponData>(BazookaShooter);
	Bazooka->CategoryTag = MSTags::Weapon_Category_Bazooka;
	Bazooka->MagazineSize = 1;
	Bazooka->Range = 20.0f;
	Bazooka->ProjectileSpeed = 40.0f;
	BazookaShooter->GetWeaponComponent()->InitializeSlots({Bazooka});
	TestEqual(TEXT("bazooka routes through standard ranged ability"),
		MSWeaponAbilityRouting::Resolve(Bazooka), EMSWeaponAbilityRoute::Hitscan);
	MSAudio::ResetRecordedMetaSoundRequest();
	TestTrue(TEXT("bazooka launch succeeds"),
		NewObject<UGA_Fire_Hitscan>(BazookaShooter)->Fire(
			BazookaShooter, FVector::ZeroVector, FVector::ForwardVector));
	TestEqual(TEXT("bazooka executes one fire Cue"), BazookaFireCueCount, 1);
	TestTrue(TEXT("bazooka Cue resolves the authored non-null sound"),
		FireCue->ResolveFireSound(Bazooka) == BazookaSound &&
		MSAudio::GetRecordedMetaSoundRequest().Sound == BazookaSound);

	AMSPlayerCharacter* HomingShooter = SpawnShooter(Scope.World, FVector(0.0f, 500.0f, 0.0f));
	UMSHomingWeaponData* Homing = NewObject<UMSHomingWeaponData>(HomingShooter);
	Homing->CategoryTag = MSTags::Weapon_Category_Homing;
	Homing->MagazineSize = 1;
	Homing->Range = 20.0f;
	Homing->LockOnRange = 20.0f;
	Homing->ProjectileSpeed = 40.0f;
	HomingShooter->GetWeaponComponent()->InitializeSlots({Homing});
	TestEqual(TEXT("homing data routes through homing ability"),
		MSWeaponAbilityRouting::Resolve(Homing), EMSWeaponAbilityRoute::Homing);
	MSAudio::ResetRecordedMetaSoundRequest();
	TestTrue(TEXT("homing missile launch succeeds"),
		NewObject<UGA_Fire_Homing>(HomingShooter)->FireHoming(
			HomingShooter, HomingShooter->GetActorLocation(), FVector::ForwardVector));
	TestEqual(TEXT("homing missile executes one fire Cue"), HomingFireCueCount, 1);
	TestTrue(TEXT("homing Cue resolves the authored non-null bazooka-family sound"),
		FireCue->ResolveFireSound(Homing) == BazookaSound &&
		MSAudio::GetRecordedMetaSoundRequest().Sound == BazookaSound);

	CueManager->OnGameplayCueRouted().Remove(CueHandle);
	return true;
}

#endif
