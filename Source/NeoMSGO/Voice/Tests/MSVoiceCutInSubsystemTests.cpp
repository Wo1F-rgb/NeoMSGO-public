#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/MSAudioPlayback.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Core/MSGameInstance.h"
#include "Player/MSPlayerCharacter.h"
#include "Sound/SoundBase.h"
#include "Voice/MSVoiceCutInSubsystem.h"
#include "Voice/MSVoiceLibrary.h"
#include "Voice/MSVoiceSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVoiceCutInSubsystemRulesTest,
	"NeoMSGO.Voice.CutInSubsystem.RulesAndChannels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FScopedVoiceWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSVoiceCutInTestWorld"));
		FScopedVoiceWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedVoiceWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	FMSVoiceCutInRequest MakeRuleRequest(const EMSVoiceTrigger Trigger, const int32 Priority,
		const double Now)
	{
		FMSVoiceCutInRequest Request;
		Request.Trigger = Trigger;
		Request.Priority = Priority;
		Request.RequestTime = Now;
		Request.Line = FText::FromString(TEXT("test"));
		return Request;
	}
}

bool FMSVoiceCutInSubsystemRulesTest::RunTest(const FString&)
{
	const bool bPresetVoiceWasEnabled = MSVoiceSettings::IsEnabled(EMSVoiceCategory::Preset);
	MSVoiceSettings::SetEnabled(EMSVoiceCategory::Preset, true, false);
	FScopedVoiceWorld Scope;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AActor* Enemy = Scope.World->SpawnActor<AActor>(FVector(100.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Local player spawned"), Player) || !TestNotNull(TEXT("Enemy spawned"), Enemy))
	{
		return false;
	}
	UMSGameInstance* TestGameInstance = NewObject<UMSGameInstance>();

	UMSVoiceCutInSubsystem* KillSubsystem = NewObject<UMSVoiceCutInSubsystem>(TestGameInstance);
	MSAudio::ResetRecordedMetaSoundRequest();
	KillSubsystem->ProcessKillConfirmed(Player, Player, 1.0);
	FMSVoiceCutInRequest Request;
	TestTrue(TEXT("Self kill creates a left-channel request"),
		KillSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left, Request));
	TestEqual(TEXT("Self kill uses Kill trigger"), Request.Trigger, EMSVoiceTrigger::Kill);
	TestTrue(TEXT("Self kill selects a non-empty line"), !Request.Line.IsEmpty());
	TestTrue(TEXT("selected line retains the asset filename index"), Request.LineIndex >= 0);
	const FString ExpectedVoicePath = MSVoiceLibrary::GetPilotVoiceAssetPath(
		Request.VoiceType, Request.Trigger, Request.LineIndex);
	const USoundBase* RecordedVoice = MSAudio::GetRecordedMetaSoundRequest().Sound.Get();
	TestEqual(TEXT("preset cut-in submits its imported SoundWave"),
		RecordedVoice ? RecordedVoice->GetPathName() : FString(), ExpectedVoicePath);
	TestEqual(TEXT("imported voice preserves authored pitch"),
		MSAudio::GetRecordedMetaSoundRequest().Frequency, 1.0f);
	TestTrue(TEXT("preset voice uses the 2D playback path"),
		MSAudio::GetRecordedMetaSoundRequest().bIs2D);
	TestEqual(TEXT("Serious voice uses an 880 Hz blip"),
		MSVoiceCutInRules::GetBlipFrequency(EMSVoiceType::Serious), 880.0f);
	TestEqual(TEXT("Shy voice uses an 1100 Hz blip"),
		MSVoiceCutInRules::GetBlipFrequency(EMSVoiceType::Shy), 1100.0f);

	UMSVoiceCutInSubsystem* DeathSubsystem = NewObject<UMSVoiceCutInSubsystem>(TestGameInstance);
	DeathSubsystem->ProcessUnitDeath(Player, Enemy, Player, 10.0);
	FMSVoiceCutInRequest Left;
	FMSVoiceCutInRequest Right;
	TestTrue(TEXT("Self death creates the left request"),
		DeathSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left, Left));
	TestTrue(TEXT("Enemy killer creates the simultaneous right request"),
		DeathSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Right, Right));
	TestEqual(TEXT("Left request is Death"), Left.Trigger, EMSVoiceTrigger::Death);
	TestEqual(TEXT("Right request is enemy Kill"), Right.Trigger, EMSVoiceTrigger::Kill);
	TestEqual(TEXT("Enemy kill cut-in has top priority"), Right.Priority, 5);

	UMSVoiceCutInSubsystem* DamageSubsystem = NewObject<UMSVoiceCutInSubsystem>(TestGameInstance);
	DamageSubsystem->ProcessUnitDamaged(Player, Enemy, Player, 20.0);
	TestTrue(TEXT("First damage creates a request"),
		DamageSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left, Request));
	DamageSubsystem->ProcessUnitDamaged(Player, Enemy, Player, 24.0);
	TestFalse(TEXT("Second damage inside eight seconds is suppressed after the first display expires"),
		DamageSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left, Request));

	UMSVoiceCutInSubsystem* PrioritySubsystem = NewObject<UMSVoiceCutInSubsystem>(TestGameInstance);
	TestTrue(TEXT("Initial request is accepted"),
		PrioritySubsystem->SubmitRequest(MakeRuleRequest(EMSVoiceTrigger::Repair, 1, 30.0), 30.0));
	TestFalse(TEXT("Lower-priority request does not replace an active request"),
		PrioritySubsystem->SubmitRequest(MakeRuleRequest(EMSVoiceTrigger::Damaged, 0, 30.1), 30.1));
	TestTrue(TEXT("Higher-priority request replaces an active request"),
		PrioritySubsystem->SubmitRequest(MakeRuleRequest(EMSVoiceTrigger::Death, 5, 30.2), 30.2));
	PrioritySubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left, Request);
	TestEqual(TEXT("The higher-priority request remains active"), Request.Priority, 5);

	const EMSVoiceType FirstType = MSVoiceCutInRules::ResolveBotVoiceType(TEXT("AllyBot_07"));
	const EMSVoiceType SecondType = MSVoiceCutInRules::ResolveBotVoiceType(TEXT("AllyBot_07"));
	TestEqual(TEXT("Same bot name always resolves to the same voice type"), FirstType, SecondType);

	MSVoiceSettings::SetEnabled(EMSVoiceCategory::Preset, false, false);
	UMSVoiceCutInSubsystem* DisabledSubsystem = NewObject<UMSVoiceCutInSubsystem>(TestGameInstance);
	MSAudio::ResetRecordedMetaSoundRequest();
	DisabledSubsystem->ProcessKillConfirmed(Player, Player, 50.0);
	TestEqual(TEXT("preset voice setting disables playback immediately"),
		MSAudio::GetRecordedMetaSoundRequest().RequestCount, 0);
	MSVoiceSettings::SetEnabled(EMSVoiceCategory::Preset, bPresetVoiceWasEnabled, false);
	return true;
}

#endif
