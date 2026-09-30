// Copyright NeoMSGO Project. All Rights Reserved.
// P6-7: BOT death -> 3s explosion/hide -> 9s automatic respawn/restart.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/MSBotAIController.h"
#include "AI/MSBotCharacter.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayCueManager.h"
#include "Player/MSRespawnComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotRespawnP67Test, "NeoMSGO.AI.BotAI.P6_7.Respawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotRespawnTest
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBotRespawnP67World"));

		FScopedWorld()
		{
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

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.05f;
		while (Seconds > 0.0f)
		{
			const float DeltaSeconds = FMath::Min(Seconds, Step);
			World->Tick(LEVELTICK_All, DeltaSeconds);
			Seconds -= DeltaSeconds;
			++GFrameCounter;
		}
	}

	void KillBot(AMSTeamAgentActor* Attacker, AMSBotCharacter* Bot)
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Attacker, Attacker);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), FGameplayEffectContextHandle(Context), 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, Bot->GetVitalSet()->GetMaxArmor() * 10.0f);
		Bot->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(Spec);
	}
}

bool FMSBotRespawnP67Test::RunTest(const FString&)
{
	MSBotRespawnTest::FScopedWorld Scope;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMSTeamAgentActor* Attacker = Scope.World->SpawnActor<AMSTeamAgentActor>(
		AMSTeamAgentActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	Attacker->SetGenericTeamId(MSTeam::PlayerTeamId());

	const FVector SpawnPoint(1200.0f, -700.0f, 300.0f);
	AMSBotCharacter* Bot = Scope.World->SpawnActor<AMSBotCharacter>(
		AMSBotCharacter::StaticClass(), SpawnPoint, FRotator(0.0f, 45.0f, 0.0f), SpawnParams);
	if (!TestNotNull(TEXT("P6-7 BOT spawned"), Bot))
	{
		return false;
	}

	AMSBotAIController* Controller = Cast<AMSBotAIController>(Bot->GetController());
	if (!Controller)
	{
		Controller = Scope.World->SpawnActor<AMSBotAIController>();
		Controller->Possess(Bot);
	}
	if (!TestNotNull(TEXT("P6-7 BOT has AI controller"), Controller))
	{
		return false;
	}

	UMSMobileSuitData* Preset = NewObject<UMSMobileSuitData>(GetTransientPackage());
	Preset->MaxArmor = 4321;
	Preset->MaxDownPoint = 987;
	Preset->AddToRoot();
	Bot->InitializeUnit(Preset);

	UMSRespawnComponent* Respawn = Bot->GetRespawnComponent();
	if (!TestNotNull(TEXT("BOT owns UMSRespawnComponent"), Respawn))
	{
		Preset->RemoveFromRoot();
		return false;
	}
	TestEqual(TEXT("BOT explosion delay is 3 seconds"), Respawn->GetExplosionDelay(), 3.0f);
	TestEqual(TEXT("BOT respawn delay is 9 seconds"), Respawn->GetRespawnDelay(), 9.0f);
	TestTrue(TEXT("BOT automatic respawn is enabled"), Respawn->IsAutoRespawnEnabled());

	int32 ExplosionCueCount = 0;
	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	const FDelegateHandle CueHandle = CueManager->OnGameplayCueRouted().AddLambda(
		[&](AActor* Target, const FGameplayTag Tag, const EGameplayCueEvent::Type EventType,
			const FGameplayCueParameters&, EGameplayCueExecutionOptions)
		{
			if (Target == Bot && Tag == MSTags::Cue_Explosion && EventType == EGameplayCueEvent::Executed)
			{
				++ExplosionCueCount;
			}
		});

	MSBotRespawnTest::KillBot(Attacker, Bot);
	TestTrue(TEXT("Death applies State.Dead for the existing kneeling animation"),
		Bot->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestTrue(TEXT("Death immediately suspends StateTree AI"), Controller->IsAILogicSuspendedForDeath());
	TestFalse(TEXT("Death blocks patrol/combat movement requests"), Controller->CanIssueMovementRequest());
	TestFalse(TEXT("BOT remains visible during the 3 second kneeling window"), Bot->IsHidden());

	MSBotRespawnTest::TickWorld(Scope.World, 2.9f);
	TestEqual(TEXT("Explosion does not fire early"), ExplosionCueCount, 0);
	TestFalse(TEXT("BOT is still visible before 3 seconds"), Bot->IsHidden());

	MSBotRespawnTest::TickWorld(Scope.World, 0.2f);
	TestEqual(TEXT("Explosion Cue fires once at 3 seconds"), ExplosionCueCount, 1);
	TestTrue(TEXT("BOT is hidden after explosion"), Bot->IsHidden());
	TestFalse(TEXT("BOT collision is disabled after explosion"), Bot->GetActorEnableCollision());
	TestTrue(TEXT("BOT AI remains stopped between explosion and respawn"), Controller->IsAILogicSuspendedForDeath());

	MSBotRespawnTest::TickWorld(Scope.World, 5.7f);
	TestTrue(TEXT("BOT remains hidden before the 9 second respawn"), Bot->IsHidden());
	TestTrue(TEXT("State.Dead remains before the 9 second respawn"),
		Bot->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));

	MSBotRespawnTest::TickWorld(Scope.World, 0.4f);
	TestFalse(TEXT("Respawn removes State.Dead through InitializeUnit"),
		Bot->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	TestEqual(TEXT("Respawn fully restores Armor"), Bot->GetVitalSet()->GetArmor(), 4321.0f);
	TestEqual(TEXT("Respawn resets DownPoint"), Bot->GetVitalSet()->GetDownPoint(), 0.0f);
	TestFalse(TEXT("Respawn shows the BOT"), Bot->IsHidden());
	TestTrue(TEXT("Respawn restores BOT collision"), Bot->GetActorEnableCollision());
	TestEqual(TEXT("Respawn returns to the confirmed spawn X"), Bot->GetActorLocation().X, SpawnPoint.X);
	TestEqual(TEXT("Respawn returns to the confirmed spawn Y"), Bot->GetActorLocation().Y, SpawnPoint.Y);
	TestFalse(TEXT("Respawn releases the death AI suspension"), Controller->IsAILogicSuspendedForDeath());
	TestTrue(TEXT("Respawn permits a fresh patrol movement request"), Controller->CanIssueMovementRequest());
	TestEqual(TEXT("Patrol center remains the original spawn point for the fresh patrol"), Bot->GetPatrolCenter(), SpawnPoint);

	CueManager->OnGameplayCueRouted().Remove(CueHandle);
	Preset->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
