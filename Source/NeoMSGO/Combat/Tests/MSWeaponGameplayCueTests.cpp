// Copyright NeoMSGO Project. All Rights Reserved.
// P4-13: 共通射撃経路のCue分岐とHUD向け命中通知を実Actor/ASCで検証する。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemGlobals.h"
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSHomingProjectile.h"
#include "Combat/MSUnitBase.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayCueManager.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponGameplayCueTest,
	"NeoMSGO.Combat.WeaponGameplayCues.P4_13",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FWeaponCueTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSWeaponCueTestWorld"));
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);

		FWeaponCueTestWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FWeaponCueTestWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSUnitBase* SpawnCueUnit(UWorld* World, const FVector& Location, const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		check(Unit);
		Unit->SetGenericTeamId(Team);
		// The cue test advances World time; keep each authored trace height fixed.
		Unit->GetCharacterMovement()->DisableMovement();
		return Unit;
	}

	void AdvanceCueWorldPastCooldown(UWorld* World, const float Duration)
	{
		const double ReadyTime = static_cast<double>(World->GetTimeSeconds()) + Duration;
		const int32 MaxFrames = FMath::CeilToInt(Duration / 0.01f) + 2;
		for (int32 Frame = 0; Frame < MaxFrames && World->GetTimeSeconds() <= ReadyTime; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.01f);
		}
	}
}

bool FMSWeaponGameplayCueTest::RunTest(const FString& Parameters)
{
	FWeaponCueTestWorld Scope;
	AMSUnitBase* Shooter = SpawnCueUnit(Scope.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	AMSUnitBase* NormalEnemy = SpawnCueUnit(Scope.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());

	UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Shooter);
	Weapon->MagazineSize = 4;
	Weapon->FireInterval = 0.1f;
	Weapon->Range = 20.0f;
	Weapon->Power = 100;
	Shooter->GetWeaponComponent()->InitializeSlots({Weapon});
	TStrongObjectPtr<UGA_Fire_Hitscan> Ability(NewObject<UGA_Fire_Hitscan>(Shooter));

	TArray<FGameplayTag> RoutedTags;
	TArray<bool> ImpactContextHeadshots;
	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	const FDelegateHandle CueRouteHandle = CueManager->OnGameplayCueRouted().AddLambda(
		[&](AActor* Target, const FGameplayTag Tag, EGameplayCueEvent::Type EventType,
			const FGameplayCueParameters& CueParameters, EGameplayCueExecutionOptions)
		{
			if (Target != Shooter || EventType != EGameplayCueEvent::Executed)
			{
				return;
			}
			RoutedTags.Add(Tag);
			if (Tag == MSTags::Cue_Impact_Normal || Tag == MSTags::Cue_Impact_Headshot)
			{
				const FMSGameplayEffectContext* Context = FMSGameplayEffectContext::Get(CueParameters.EffectContext);
				ImpactContextHeadshots.Add(Context && Context->IsHeadshot());
			}
		});

	UMSHitFeedbackSubsystem* HitFeedback = Scope.GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>();
	if (!TestNotNull(TEXT("GameInstance creates the shared hit-feedback subsystem"), HitFeedback))
	{
		CueManager->OnGameplayCueRouted().Remove(CueRouteHandle);
		return false;
	}
	TArray<bool> BroadcastHeadshots;
	TArray<TWeakObjectPtr<AActor>> BroadcastShooters;
	const FDelegateHandle HitHandle = HitFeedback->OnHitConfirmed.AddLambda(
		[&](AActor* ConfirmedShooter, const bool bIsHeadshot)
		{
			BroadcastShooters.Add(ConfirmedShooter);
			BroadcastHeadshots.Add(bIsHeadshot);
		});
	const auto CountRouted = [&RoutedTags](const FGameplayTag Tag)
	{
		int32 Count = 0;
		for (const FGameplayTag RoutedTag : RoutedTags)
		{
			Count += RoutedTag == Tag ? 1 : 0;
		}
		return Count;
	};

	// 胴体命中: Normal Impact + false通知。
	TestTrue(TEXT("body shot executes the common hitscan damage path"),
		Ability->Fire(Shooter, FVector::ZeroVector, FVector::ForwardVector));
	NormalEnemy->SetActorEnableCollision(false);
	const FVector ShooterLocation = Shooter->GetActorLocation();
	AdvanceCueWorldPastCooldown(Scope.World, Weapon->FireInterval);
	TestEqual(TEXT("world clock completes first hitscan fireInterval"),
		Shooter->GetWeaponComponent()->GetFireCooldownRemaining(), 0.0f);
	TestTrue(TEXT("first cooldown does not move the shooter or trace origin"),
		Shooter->GetActorLocation().Equals(ShooterLocation, KINDA_SMALL_NUMBER));

	// 頭部命中: Headshot Impact + true通知。
	AMSUnitBase* HeadEnemy = SpawnCueUnit(Scope.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	const FVector HeadOrigin(0.0f, 0.0f, HeadEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	TestTrue(TEXT("head shot executes the same common hitscan damage path"),
		Ability->Fire(Shooter, HeadOrigin, FVector::ForwardVector));

	TestEqual(TEXT("fire Cue executes once per accepted shot"), CountRouted(MSTags::Cue_Weapon_Fire), 2);
	TestEqual(TEXT("existing tracer Cue is wired once per accepted shot"), CountRouted(MSTags::Cue_Weapon_Tracer), 2);
	TestEqual(TEXT("normal impact Cue executes exactly once"), CountRouted(MSTags::Cue_Impact_Normal), 1);
	TestEqual(TEXT("headshot impact Cue executes exactly once"), CountRouted(MSTags::Cue_Impact_Headshot), 1);
	TestEqual(TEXT("hit-marker data Cue executes for both damaging hits"), CountRouted(MSTags::Cue_HitMarker), 2);
	TestEqual(TEXT("both impact Cues retain their shared EffectContext"), ImpactContextHeadshots.Num(), 2);
	if (ImpactContextHeadshots.Num() == 2)
	{
		TestFalse(TEXT("normal impact context is not a headshot"), ImpactContextHeadshots[0]);
		TestTrue(TEXT("headshot impact context is marked headshot"), ImpactContextHeadshots[1]);
	}
	TestEqual(TEXT("hit-feedback subsystem broadcasts once per damaging hit"), BroadcastHeadshots.Num(), 2);
	if (BroadcastHeadshots.Num() == 2)
	{
		TestFalse(TEXT("body hit broadcasts false"), BroadcastHeadshots[0]);
		TestTrue(TEXT("head hit broadcasts true"), BroadcastHeadshots[1]);
		TestTrue(TEXT("both broadcasts identify the source shooter"),
			BroadcastShooters[0] == Shooter && BroadcastShooters[1] == Shooter);
	}

	// 死亡遷移からKiller側ASCへKill Confirmが返ることも同じルート監視で確認する。
	HeadEnemy->SetActorEnableCollision(false);
	AdvanceCueWorldPastCooldown(Scope.World, Weapon->FireInterval);
	TestEqual(TEXT("world clock completes second hitscan fireInterval"),
		Shooter->GetWeaponComponent()->GetFireCooldownRemaining(), 0.0f);
	TestTrue(TEXT("second cooldown does not move the shooter or trace origin"),
		Shooter->GetActorLocation().Equals(ShooterLocation, KINDA_SMALL_NUMBER));
	AMSUnitBase* LethalEnemy = SpawnCueUnit(Scope.World, FVector(800.0f, 0.0f, 0.0f), MSTeam::EnemyTeamId());
	Weapon->Power = 10000;
	TestTrue(TEXT("lethal shot executes"), Ability->Fire(Shooter, FVector::ZeroVector, FVector::ForwardVector));
	TestEqual(TEXT("death transition executes one kill-confirm Cue on the killer ASC"),
		CountRouted(MSTags::Cue_Kill_Confirm), 1);
	// 機体死亡の爆発Cueは即時発火しない(#65の3秒しゃがみ演出後にUMSRespawnComponent経由で発火するため)。
	TestEqual(TEXT("unit death does not fire an explosion Cue immediately (delayed by respawn kneel timer)"),
		CountRouted(MSTags::Cue_Explosion), 0);

	// 誘導弾の寿命切れ自爆もExplodeを通り、射手ASCから爆発Cueを発火する。
	AMSHomingProjectile* Projectile = Scope.World->SpawnActor<AMSHomingProjectile>();
	Projectile->InitHoming(Shooter, nullptr, 0.0f);
	float RemainingLifetime = AMSHomingProjectile::MaxLifetimeSeconds + 0.1f;
	while (RemainingLifetime > 0.0f)
	{
		const float Step = FMath::Min(RemainingLifetime, 0.1f);
		++GFrameCounter;
		Scope.World->Tick(LEVELTICK_All, Step);
		RemainingLifetime -= Step;
	}
	TestEqual(TEXT("homing projectile adds one ordnance explosion Cue"),
		CountRouted(MSTags::Cue_Explosion), 1);
	TestTrue(TEXT("B-7 explosion duration lower bound is 0.04s"),
		FMath::IsNearlyEqual(UMSGameplayCue_Explosion::ExplosionMinDurationSeconds, 0.04f));
	TestTrue(TEXT("B-7 explosion duration upper bound is 0.06s"),
		FMath::IsNearlyEqual(UMSGameplayCue_Explosion::ExplosionMaxDurationSeconds, 0.06f));

	HitFeedback->OnHitConfirmed.Remove(HitHandle);
	CueManager->OnGameplayCueRouted().Remove(CueRouteHandle);
	return true;
}

#endif
