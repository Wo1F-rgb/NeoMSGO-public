// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallCompletionTest,
	"NeoMSGO.Player.EmergencyRecall.Completion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallDamageTest,
	"NeoMSGO.Player.EmergencyRecall.DamageInterrupt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallCooldownTest,
	"NeoMSGO.Player.EmergencyRecall.Cooldown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallCrouchOwnershipTest,
	"NeoMSGO.Player.EmergencyRecall.CrouchTagOwnership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallInputAssetsTest,
	"NeoMSGO.Player.EmergencyRecall.InputAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallRejectionTest,
	"NeoMSGO.Player.EmergencyRecall.RejectionReasons",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallNoticeTest,
	"NeoMSGO.Player.EmergencyRecall.InterruptionNotices",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallOwnerNotificationTest,
	"NeoMSGO.Player.EmergencyRecall.OwnerNotificationTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSEmergencyRecallRepeatInputTest,
	"NeoMSGO.Player.EmergencyRecall.RepeatInputDoesNotCancel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** These standalone fixtures intentionally exercise the offline gameplay path.
	 * Strip the configured Online override so the first parsed mode is deterministic. */
	struct FScopedEmergencyRecallOfflineAuth
	{
		FString Previous = FCommandLine::Get();

		FScopedEmergencyRecallOfflineAuth()
		{
			TArray<FString> Tokens;
			Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			Effective += TEXT("-MSAuthMode=OfflineLocal");
			FCommandLine::Set(*Effective);
		}

		~FScopedEmergencyRecallOfflineAuth()
		{
			FCommandLine::Set(*Previous);
		}
	};

	struct FRecallTestWorld
	{
		FScopedEmergencyRecallOfflineAuth OfflineAuth;
		UWorld* World = nullptr;

		FRecallTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
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

		~FRecallTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	void TickWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Seconds, Step));
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	AMSPlayerCharacter* SpawnGroundedPlayer(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector(0.0f, 0.0f, 100.0f), FRotator::ZeroRotator, Params);
		if (!Player)
		{
			return nullptr;
		}
		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Player);
		Data->EmergencyRecallPerformance = 1000; // Five-second countdown keeps the test quick.
		Player->InitializeUnit(Data);
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		return Player;
	}

	void ApplyDamage(UWorld* World, AMSPlayerCharacter* Player)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSTeamAgentActor* Attacker = World->SpawnActor<AMSTeamAgentActor>(
			AMSTeamAgentActor::StaticClass(), FVector(500.0f, 0.0f, 100.0f), FRotator::ZeroRotator, Params);
		Attacker->SetGenericTeamId(MSTeam::EnemyTeamId());

		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Attacker, Attacker);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), FGameplayEffectContextHandle(Context), 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, 100.0f);
		Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(Spec);
	}
}

bool FMSEmergencyRecallCompletionTest::RunTest(const FString& Parameters)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	if (!TestNotNull(TEXT("Recall component is player-only and present"), Recall)) return false;

	bool bCompleted = false;
	bool bStarted = false;
	Recall->OnRecallStarted.AddLambda([&bStarted]() { bStarted = true; });
	Recall->OnRecallCompleted.AddLambda([&bCompleted]() { bCompleted = true; });
	TestTrue(TEXT("Grounded living player starts recall"), Recall->TryStartRecall());
	TestTrue(TEXT("Successful TryStartRecall emits its start notification"), bStarted);
	TestEqual(TEXT("Recall tag count while active"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_EmergencyRecall), 1);
	TestEqual(TEXT("Recall owns one crouch tag count"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_Crouching), 1);

	TickWorld(TestWorld.World, 5.2f);
	TestTrue(TEXT("Completion delegate fired"), bCompleted);
	TestFalse(TEXT("Recall inactive after completion"), Recall->IsRecallActive());
	TestEqual(TEXT("Recall tag removed"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_EmergencyRecall), 0);
	TestEqual(TEXT("Recall crouch count removed"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_Crouching), 0);
	TestTrue(TEXT("Player hidden for resortie"), Player->IsHidden());
	TestFalse(TEXT("Player collision disabled for resortie"), Player->GetActorEnableCollision());
	TestTrue(TEXT("Completion leaves the player awaiting a new sortie"), Recall->IsAwaitingDeploy());
	TestEqual(TEXT("completion has its own notice"), Recall->GetNotice(), EMSRecallNotice::Completed);
	TestFalse(TEXT("completed recall cannot start a second countdown"), Recall->TryStartRecall());
	TestEqual(TEXT("completed state rejection identifies deployment selection"), Recall->GetNoticeReason(), EMSRecallReason::AwaitingDeployment);
	TestFalse(TEXT("Recall never marks player dead"),
		Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
	return true;
}

bool FMSEmergencyRecallRepeatInputTest::RunTest(const FString&)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	if (!TestNotNull(TEXT("Recall component exists"), Recall)) return false;

	Player->PressRecallInputForTesting();
	TestTrue(TEXT("first Z press starts recall"), Recall->IsRecallActive());
	Player->PressRecallInputForTesting();
	TestTrue(TEXT("second Z press leaves countdown active"), Recall->IsRecallActive());
	TestEqual(TEXT("second Z press does not publish a cancellation"),
		Recall->GetNotice(), EMSRecallNotice::None);
	TestEqual(TEXT("second Z press does not start cooldown"),
		Recall->GetRecallCooldownRemaining(), 0.0f);

	AMSPlayerCharacter* PendingPlayer = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Pending-request player spawned"), PendingPlayer)) return false;
	UMSEmergencyRecallComponent* PendingRecall = PendingPlayer->GetEmergencyRecallComponent();
	PendingRecall->SetRequestPendingForTesting();
	PendingPlayer->PressRecallInputForTesting();
	TestTrue(TEXT("Z press leaves a pending online request intact"), PendingRecall->IsRequestPending());
	TestFalse(TEXT("pending Z press does not start a separate countdown"), PendingRecall->IsRecallActive());
	return true;
}

bool FMSEmergencyRecallCrouchOwnershipTest::RunTest(const FString& Parameters)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	if (!TestNotNull(TEXT("Recall component is present"), Recall)) return false;

	// Manual crouch owns one loose-tag count; recall must add and later remove only
	// its own count so Ctrl-held state survives the transition.
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	ASC->AddLooseGameplayTag(MSTags::State_Crouching);
	TestTrue(TEXT("Recall starts with an existing crouch tag"), Recall->TryStartRecall());
	TestEqual(TEXT("Recall adds exactly one crouch count"), ASC->GetTagCount(MSTags::State_Crouching), 2);
	Recall->CancelRecall();
	TestEqual(TEXT("Cancelling recall preserves manual crouch count"), ASC->GetTagCount(MSTags::State_Crouching), 1);
	return true;
}

bool FMSEmergencyRecallDamageTest::RunTest(const FString& Parameters)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	TestTrue(TEXT("Recall starts"), Recall && Recall->TryStartRecall());
	ApplyDamage(TestWorld.World, Player);
	TestFalse(TEXT("Armor damage interrupts recall"), Recall->IsRecallActive());
	TestEqual(TEXT("Recall tag removed after damage"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_EmergencyRecall), 0);
	TestEqual(TEXT("Recall crouch count removed after damage"),
		Player->GetAbilitySystemComponent()->GetTagCount(MSTags::State_Crouching), 0);
	TestTrue(TEXT("Damage interruption begins cooldown"), Recall->GetRecallCooldownRemaining() > 4.0f);
	return true;
}

bool FMSEmergencyRecallCooldownTest::RunTest(const FString& Parameters)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	TestTrue(TEXT("Recall starts"), Recall && Recall->TryStartRecall());
	Recall->CancelRecall();
	TestEqual(TEXT("explicit cancel has manual reason"), Recall->GetNoticeReason(), EMSRecallReason::ManualCancel);
	TestFalse(TEXT("Immediate restart is rejected"), Recall->TryStartRecall());
	TickWorld(TestWorld.World, 5.2f);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("Restart succeeds after five-second cooldown"), Recall->TryStartRecall());
	return true;
}

bool FMSEmergencyRecallInputAssetsTest::RunTest(const FString& Parameters)
{
	UInputAction* RecallAction = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/Input/Actions/MS/IA_Recall.IA_Recall"));
	UInputMappingContext* PlayerContext = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
	if (!TestNotNull(TEXT("IA_Recall asset exists"), RecallAction) ||
		!TestNotNull(TEXT("IMC_MSPlayer asset exists"), PlayerContext))
	{
		return false;
	}

	const bool bMappedToZ = PlayerContext->GetMappings().ContainsByPredicate(
		[RecallAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == RecallAction && Mapping.Key == EKeys::Z;
		});
	TestTrue(TEXT("IA_Recall is mapped to Z in IMC_MSPlayer"), bMappedToZ);
	return true;
}

bool FMSEmergencyRecallRejectionTest::RunTest(const FString&)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("player exists"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("airborne start is rejected"), Recall->TryStartRecall());
	TestEqual(TEXT("airborne reason retained"), Recall->GetNoticeReason(), EMSRecallReason::Airborne);
	TestEqual(TEXT("rejected notification available"), Recall->GetNotice(), EMSRecallNotice::Rejected);
	TestFalse(TEXT("rejection cannot start a timer"), Recall->IsRecallActive());
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	ASC->AddLooseGameplayTag(MSTags::State_Staggered);
	TestFalse(TEXT("incapacitated start is rejected"), Recall->TryStartRecall());
	TestEqual(TEXT("incapacitated reason retained"), Recall->GetNoticeReason(), EMSRecallReason::Incapacitated);
	ASC->RemoveLooseGameplayTag(MSTags::State_Staggered);
	TestTrue(TEXT("valid start succeeds"), Recall->TryStartRecall());
	TestEqual(TEXT("successful start clears old notice"), Recall->GetNotice(), EMSRecallNotice::None);
	TestFalse(TEXT("duplicate start rejected"), Recall->TryStartRecall());
	TestEqual(TEXT("duplicate reason retained"), Recall->GetNoticeReason(), EMSRecallReason::AlreadyActive);
	Recall->CancelRecall();
	TestFalse(TEXT("cooldown start is rejected"), Recall->TryStartRecall());
	TestEqual(TEXT("cooldown reason retained"), Recall->GetNoticeReason(), EMSRecallReason::Cooldown);
	TestTrue(TEXT("cooldown still runs after rejection"), Recall->GetRecallCooldownRemaining() > 4.9f);
	ASC->AddLooseGameplayTag(MSTags::State_Dead);
	TestFalse(TEXT("dead start is rejected"), Recall->TryStartRecall());
	TestEqual(TEXT("death takes priority over cooldown"), Recall->GetNoticeReason(), EMSRecallReason::Dead);
	return true;
}

bool FMSEmergencyRecallNoticeTest::RunTest(const FString&)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("player exists"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	TestTrue(TEXT("recall starts"), Recall->TryStartRecall());
	Player->ApplyMovementInput(FVector2D(1.0f, 0.0f));
	TestEqual(TEXT("movement cancellation reason"), Recall->GetNoticeReason(), EMSRecallReason::Movement);
	TestEqual(TEXT("cancellation notice visible"), Recall->GetNotice(), EMSRecallNotice::Cancelled);
	TickWorld(TestWorld.World, 3.2f);
	TestEqual(TEXT("notice expires after three seconds"), Recall->GetNotice(), EMSRecallNotice::None);
	TestTrue(TEXT("cooldown remains after notification expires"), Recall->GetRecallCooldownRemaining() > 1.0f);
	TickWorld(TestWorld.World, 2.0f);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("recall restarts"), Recall->TryStartRecall());
	ApplyDamage(TestWorld.World, Player);
	TestEqual(TEXT("real armor damage produces damage reason"), Recall->GetNoticeReason(), EMSRecallReason::Damage);
	TickWorld(TestWorld.World, 5.2f);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("third recall starts"), Recall->TryStartRecall());
	Player->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Dead);
	TestFalse(TEXT("death interrupts countdown"), Recall->IsRecallActive());
	TestEqual(TEXT("death has its own reason"), Recall->GetNoticeReason(), EMSRecallReason::Dead);
	return true;
}

bool FMSEmergencyRecallOwnerNotificationTest::RunTest(const FString&)
{
	FRecallTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnGroundedPlayer(TestWorld.World);
	if (!TestNotNull(TEXT("player exists"), Player)) return false;
	UMSEmergencyRecallComponent* Recall = Player->GetEmergencyRecallComponent();
	int32 Starts = 0;
	Recall->OnRecallStarted.AddLambda([&Starts]() { ++Starts; });
	Recall->SetRequestPendingForTesting();
	TestFalse(TEXT("pending request does not claim success"), Recall->IsRecallActive());
	Recall->ApplyAuthorityNoticeForTesting(EMSRecallNotice::Rejected, EMSRecallReason::Airborne);
	TestFalse(TEXT("authority rejection clears pending request"), Recall->IsRequestPending());
	TestEqual(TEXT("owner gets rejection reason"), Recall->GetNoticeReason(), EMSRecallReason::Airborne);
	TestEqual(TEXT("rejection cannot emit a start"), Starts, 0);
	Recall->SetRequestPendingForTesting();
	Recall->ApplyReplicatedStateForTesting(true, false, 1);
	Recall->ApplyReplicatedStateForTesting(true, false, 1);
	TestEqual(TEXT("repeated RepNotify emits start once"), Starts, 1);
	TestFalse(TEXT("authority active state clears pending request"), Recall->IsRequestPending());
	Recall->ApplyAuthorityNoticeForTesting(EMSRecallNotice::Cancelled, EMSRecallReason::Movement);
	Recall->ApplyReplicatedStateForTesting(true, false, 1);
	TestFalse(TEXT("stale active snapshot cannot resurrect cancelled generation"), Recall->IsRecallActive());
	TestEqual(TEXT("stale active state preserves interruption notice"), Recall->GetNoticeReason(), EMSRecallReason::Movement);
	TestEqual(TEXT("stale snapshot cannot repeat start"), Starts, 1);
	Recall->ApplyReplicatedStateForTesting(false, false, 1);
	Recall->ApplyReplicatedStateForTesting(true, false, 2);
	TestEqual(TEXT("next generation notifies once"), Starts, 2);
	Recall->ApplyReplicatedStateForTesting(false, false, 2);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("authority starts its next recall"), Recall->TryStartRecall());
	Recall->ApplyReplicatedStateForTesting(true, false, 3);
	TestEqual(TEXT("listen authority plus presentation does not double notify"), Starts, 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
