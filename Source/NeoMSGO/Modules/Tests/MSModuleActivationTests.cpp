#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Audio/MSAudioPlayback.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSBoostGameplayEffects.h"
#include "Combat/MSModuleGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Modules/GA_Module_Transzomm.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Sound/SoundBase.h"
#include "UI/MSDeckSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleGameplayEffectsTest,
	"NeoMSGO.Modules.Effects.NativeDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleActivationLifecycleTest,
	"NeoMSGO.Modules.Activation.DataDrivenLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleActivationGatesTest,
	"NeoMSGO.Modules.Activation.BlockedStatesAndDeathCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleForcedOverheatGuardTest,
	"NeoMSGO.Modules.Overheat.ForcedAndNormalReleaseCoexist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleInputAssetsTest,
	"NeoMSGO.Modules.Input.ActionAndDefaultKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModulePlayerDispatchTest,
	"NeoMSGO.Modules.Input.EquippedAndUnequippedDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSModuleDeckDispatchTest,
	"NeoMSGO.Modules.Input.DeployedSlotModuleSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSModuleTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSModuleTestWorld"));
		FScopedWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		void TickFor(float Seconds) const
		{
			for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += 0.01f)
			{
				World->Tick(LEVELTICK_All, FMath::Min(0.01f, Seconds - Elapsed));
				++GFrameCounter;
			}
		}
	};

	AMSPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	UMSModuleData* MakeShortModule()
	{
		UMSModuleData* Data = NewObject<UMSModuleData>(GetTransientPackage());
		Data->ModuleAbilityClass = UGA_Module_Transzomm::StaticClass();
		Data->EffectDurationSeconds = 0.08f;
		Data->SpeedMultiplier = 1.7f;
		Data->BoostConsumptionMultiplier = 1.8f;
		Data->ForcedOverheatSeconds = 0.12f;
		Data->CooldownSeconds = 0.30f;
		return Data;
	}

	bool ApplyDuration(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass,
		const FGameplayTag& DurationTag, float Duration)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.0f, ASC->MakeEffectContext());
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(DurationTag, Duration);
		return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).IsValid();
	}

	bool ApplyBoostDelta(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass,
		const FGameplayTag& DataTag, float Delta)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.0f, ASC->MakeEffectContext());
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(DataTag, Delta);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		return true;
	}
}

bool FMSModuleGameplayEffectsTest::RunTest(const FString&)
{
	const UGameplayEffect* Effect = GetDefault<UGE_ModuleEffect>();
	const UGameplayEffect* Forced = GetDefault<UGE_ModuleForcedOverheat>();
	const UGameplayEffect* Cooldown = GetDefault<UGE_ModuleCooldown>();
	TestEqual(TEXT("effect uses duration policy"), Effect->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	TestTrue(TEXT("effect grants active tag"), Effect->GetGrantedTags().HasTagExact(MSTags::State_Module_EffectActive));
	TestTrue(TEXT("forced OH grants generic tag"), Forced->GetGrantedTags().HasTagExact(MSTags::State_Overheated));
	TestTrue(TEXT("forced OH grants guard tag"), Forced->GetGrantedTags().HasTagExact(MSTags::State_Module_ForcedOverheat));
	TestTrue(TEXT("cooldown grants cooldown tag"), Cooldown->GetGrantedTags().HasTagExact(MSTags::State_Module_Cooldown));
	return true;
}

bool FMSModuleActivationLifecycleTest::RunTest(const FString&)
{
	MSModuleTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = MSModuleTests::SpawnPlayer(Scope.World);
	if (!TestNotNull(TEXT("player spawned"), Player)) return false;
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	UMSAttributeSet_Boost* BoostSet = Player->GetBoostSet();
	UMSBoostComponent* Boost = Player->FindComponentByClass<UMSBoostComponent>();
	UMSCharacterMovementComponent* Movement = Player->GetMSCharacterMovement();
	UMSMechAnimatorComponent* Animator = Player->GetMechAnimatorComponent();
	UMSModuleData* Data = MSModuleTests::MakeShortModule();
	UGA_Module_Transzomm* Ability = NewObject<UGA_Module_Transzomm>(Player);
	Boost->SetRates(20.0f, 0.0f); // Keep the completion Boost=0 assertion observable.
	BoostSet->SetMaxBoost(100.0f);
	BoostSet->SetBoost(65.0f);
	Movement->SetMovementMode(MOVE_Walking);
	const float BaseWalkSpeed = Movement->GetMaxSpeed();
	const float BaseConsumption = Boost->GetEffectiveConsumptionPerSecond();

	MSAudio::ResetRecordedMetaSoundRequest();
	TestTrue(TEXT("activation succeeds"), Ability->TryActivateModule(Player, Data));
	const MSAudio::FRecordedMetaSoundRequest& ActivationAudio =
		MSAudio::GetRecordedMetaSoundRequest();
	TestTrue(TEXT("successful activation plays the authored module sound"),
		ActivationAudio.Sound == Ability->GetModuleActivateSound());
	TestEqual(TEXT("module activation is a 2D one-shot"), ActivationAudio.bIs2D, true);
	TestEqual(TEXT("module activation sound path"),
		Ability->GetModuleActivateSound()->GetPathName(),
		FString(TEXT("/Game/Audio/Imported/SFX_ModuleActivate.SFX_ModuleActivate")));

	MSAudio::ResetRecordedMetaSoundRequest();
	UGA_Module_Transzomm::PlaySortieTransition(Player);
	const MSAudio::FRecordedMetaSoundRequest& SortieAudio =
		MSAudio::GetRecordedMetaSoundRequest();
	TestTrue(TEXT("sortie hook plays the authored transition sound"),
		SortieAudio.Sound == Ability->GetSortieTransitionSound());
	TestEqual(TEXT("sortie transition is a 2D one-shot"), SortieAudio.bIs2D, true);
	TestTrue(TEXT("active tag applied immediately"), ASC->HasMatchingGameplayTag(MSTags::State_Module_EffectActive));
	TestTrue(TEXT("cooldown tag applied at the same time"), ASC->HasMatchingGameplayTag(MSTags::State_Module_Cooldown));
	TestEqual(TEXT("walk speed uses DataAsset multiplier"), Movement->GetMaxSpeed(), BaseWalkSpeed * Data->SpeedMultiplier);
	TestEqual(TEXT("consumption uses DataAsset multiplier"), Boost->GetEffectiveConsumptionPerSecond(),
		BaseConsumption * Data->BoostConsumptionMultiplier);
	TestTrue(TEXT("activation enables the module afterimage override"),
		Animator->IsModuleAfterimageOverrideEnabled());
	TestTrue(TEXT("activation forwards the DataAsset afterimage color"),
		Animator->GetModuleAfterimageOverrideColor().Equals(Data->AfterimageColor, 0.001f));

	Movement->SetBoostInputHeld(true);
	const float BoostedModuleSpeed = Movement->GetMaxSpeed();
	Movement->SetModuleSpeedMultiplier(1.0f);
	const float BoostedBaseSpeed = Movement->GetMaxSpeed();
	Movement->SetModuleSpeedMultiplier(Data->SpeedMultiplier);
	TestEqual(TEXT("boost speed uses the same DataAsset multiplier"), BoostedModuleSpeed,
		BoostedBaseSpeed * Data->SpeedMultiplier);
	Movement->SetBoostInputHeld(false);

	Scope.TickFor(Data->EffectDurationSeconds + 0.03f);
	TestFalse(TEXT("effect expires at data-driven duration"), ASC->HasMatchingGameplayTag(MSTags::State_Module_EffectActive));
	TestEqual(TEXT("speed resets after tag removal"), Movement->GetModuleSpeedMultiplier(), 1.0f);
	TestEqual(TEXT("consumption resets after tag removal"), Boost->GetModuleConsumptionMultiplier(), 1.0f);
	TestFalse(TEXT("effect expiry disables the module afterimage override"),
		Animator->IsModuleAfterimageOverrideEnabled());
	TestEqual(TEXT("normal completion empties boost"), BoostSet->GetBoost(), 0.0f);
	TestTrue(TEXT("normal completion starts overheat"), ASC->HasMatchingGameplayTag(MSTags::State_Overheated));
	TestTrue(TEXT("normal completion starts forced guard"), ASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat));

	Scope.TickFor(Data->ForcedOverheatSeconds + 0.03f);
	TestFalse(TEXT("forced overheat expires by its DataAsset duration"), ASC->HasMatchingGameplayTag(MSTags::State_Overheated));
	TestFalse(TEXT("forced guard expires with overheat"), ASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat));
	TestTrue(TEXT("cooldown still covers the post-OH gap"), ASC->HasMatchingGameplayTag(MSTags::State_Module_Cooldown));
	Scope.TickFor(Data->CooldownSeconds + 0.03f);
	TestFalse(TEXT("cooldown eventually expires"), ASC->HasMatchingGameplayTag(MSTags::State_Module_Cooldown));
	TestTrue(TEXT("ability can reactivate after cooldown"), Ability->TryActivateModule(Player, Data));
	return true;
}

bool FMSModuleActivationGatesTest::RunTest(const FString&)
{
	MSModuleTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = MSModuleTests::SpawnPlayer(Scope.World);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	UMSModuleData* Data = MSModuleTests::MakeShortModule();
	auto IsBlockedBy = [this, Player, ASC, Data](const FGameplayTag& Tag)
	{
		ASC->SetLooseGameplayTagCount(Tag, 1);
		UGA_Module_Transzomm* Ability = NewObject<UGA_Module_Transzomm>(Player);
		const bool bBlocked = !Ability->TryActivateModule(Player, Data);
		ASC->SetLooseGameplayTagCount(Tag, 0);
		return bBlocked;
	};
	TestTrue(TEXT("active state blocks activation"), IsBlockedBy(MSTags::State_Module_EffectActive));
	TestTrue(TEXT("cooldown blocks activation"), IsBlockedBy(MSTags::State_Module_Cooldown));
	TestTrue(TEXT("overheat blocks activation"), IsBlockedBy(MSTags::State_Overheated));
	TestTrue(TEXT("death blocks activation"), IsBlockedBy(MSTags::State_Dead));

	UGA_Module_Transzomm* ActiveAbility = NewObject<UGA_Module_Transzomm>(Player);
	TestTrue(TEXT("precondition activation succeeds"), ActiveAbility->TryActivateModule(Player, Data));
	ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TestFalse(TEXT("death removes active module effect"), ASC->HasMatchingGameplayTag(MSTags::State_Module_EffectActive));
	TestFalse(TEXT("death removes module cooldown"), ASC->HasMatchingGameplayTag(MSTags::State_Module_Cooldown));
	TestFalse(TEXT("death cleanup does not start forced overheat"), ASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat));
	ASC->SetLooseGameplayTagCount(MSTags::State_Dead, 0);
	return true;
}

bool FMSModuleForcedOverheatGuardTest::RunTest(const FString&)
{
	MSModuleTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = MSModuleTests::SpawnPlayer(Scope.World);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	UMSAttributeSet_Boost* Boost = Player->GetBoostSet();
	Boost->SetMaxBoost(100.0f);

	TestTrue(TEXT("forced OH applied"), MSModuleTests::ApplyDuration(ASC,
		UGE_ModuleForcedOverheat::StaticClass(), MSTags::Data_Module_OverheatDuration, 0.12f));
	Boost->SetBoost(79.0f);
	TestTrue(TEXT("forced OH recovery GE applied"), MSModuleTests::ApplyBoostDelta(ASC,
		UGE_BoostRegen::StaticClass(), MSTags::Data_BoostRegen, 1.0f));
	TestEqual(TEXT("boost reaches normal release threshold"), Boost->GetBoost(), 80.0f);
	TestFalse(TEXT("forced OH releases at 80 percent recovery"), ASC->HasMatchingGameplayTag(MSTags::State_Overheated));
	TestFalse(TEXT("80 percent recovery removes forced guard with its effect"), ASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat));

	TestTrue(TEXT("normal OH applied"), MSModuleTests::ApplyDuration(ASC,
		UGE_Overheat::StaticClass(), MSTags::Data_OverheatDuration, 1.0f));
	Boost->SetBoost(79.0f);
	TestTrue(TEXT("normal recovery GE applied"), MSModuleTests::ApplyBoostDelta(ASC,
		UGE_BoostRegen::StaticClass(), MSTags::Data_BoostRegen, 1.0f));
	TestFalse(TEXT("normal OH still releases at 80 percent"), ASC->HasMatchingGameplayTag(MSTags::State_Overheated));
	return true;
}

bool FMSModuleInputAssetsTest::RunTest(const FString&)
{
	UInputAction* Action = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/Input/Actions/MS/IA_Module.IA_Module"));
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
	if (!TestNotNull(TEXT("IA_Module exists"), Action) ||
		!TestNotNull(TEXT("IMC_MSPlayer exists"), Context))
	{
		return false;
	}
	bool bFoundF = false;
	bool bFoundFour = false;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		bFoundF |= Mapping.Action == Action && Mapping.Key == EKeys::F;
		bFoundFour |= Mapping.Action == Action && Mapping.Key == EKeys::Four;
	}
	TestTrue(TEXT("IA_Module is mapped to F"), bFoundF);
	TestFalse(TEXT("IA_Module is no longer mapped to 4"), bFoundFour);
	const UInputTriggerHold* HoldTrigger = nullptr;
	for (const UInputTrigger* Trigger : Action->Triggers)
	{
		if (const UInputTriggerHold* Hold = Cast<UInputTriggerHold>(Trigger))
		{
			HoldTrigger = Hold;
			break;
		}
	}
	if (TestNotNull(TEXT("IA_Module uses a hold trigger"), HoldTrigger))
	{
		TestEqual(TEXT("module hold threshold is one second"), HoldTrigger->HoldTimeThreshold, 1.0f);
		TestTrue(TEXT("module hold triggers once"), HoldTrigger->bIsOneShot);
	}

	UMSModuleData* Asset = LoadObject<UMSModuleData>(nullptr,
		TEXT("/Game/Data/Modules/DA_Module_Transzomm.DA_Module_Transzomm"));
	if (!TestNotNull(TEXT("DA_Module_Transzomm exists"), Asset)) return false;
	TestEqual(TEXT("asset points at the native module GA"), Asset->ModuleAbilityClass.Get(),
		UGA_Module_Transzomm::StaticClass());
	return true;
}

bool FMSModulePlayerDispatchTest::RunTest(const FString&)
{
	MSModuleTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = MSModuleTests::SpawnPlayer(Scope.World);
	TestFalse(TEXT("unequipped player ignores module input"), Player->TryActivateEquippedModule());
	UMSModuleData* Module = LoadObject<UMSModuleData>(nullptr,
		TEXT("/Game/Data/Modules/DA_Module_Transzomm.DA_Module_Transzomm"));
	if (!TestNotNull(TEXT("module asset loaded"), Module)) return false;
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(GetTransientPackage());
	Suit->EquippedModule = Module;
	Player->InitializeUnit(Suit);
	TestTrue(TEXT("equipped player dispatches module ability"), Player->TryActivateEquippedModule());
	TestTrue(TEXT("dispatch applies active tag"), Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(
		MSTags::State_Module_EffectActive));
	return true;
}

bool FMSModuleDeckDispatchTest::RunTest(const FString&)
{
	MSModuleTests::FScopedWorld Scope;
	AMSPlayerCharacter* Player = MSModuleTests::SpawnPlayer(Scope.World);
	UMSModuleData* Module = MSModuleTests::MakeShortModule();
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(GetTransientPackage());
	Suit->EquippedModule = Module;
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(Instance.Get()));
	TStrongObjectPtr<UMSDeckSaveGame> Save(NewObject<UMSDeckSaveGame>());
	Save->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	for (int32 Slot : {0, 1})
	{
		Save->Slots[Slot].MobileSuit = FSoftObjectPath(Suit);
		Save->Slots[Slot].EquippedWeapons.SetNum(3);
	}
	Save->Slots[1].EquippedModule = FSoftObjectPath(Module);
	TestTrue(TEXT("v3 fixture restores distinct modules on the same catalog suit"), Deck->RestoreSaveGameForAutomationTests(Save.Get()));
	Player->InitializeUnit(Suit);
	TestEqual(TEXT("legacy InitializeUnit still uses preset default"), Player->GetEquippedModule(), Module);
	TestTrue(TEXT("real sortie deploys the explicitly unequipped slot"), Player->GetRespawnComponent()->DeployDeckSlot(Deck.Get(), 0, FTransform(FVector(1000, 0, 0))));
	TestNull(TEXT("explicit slot removal overrides preset default"), Player->GetEquippedModule());
	TestFalse(TEXT("unequipped slot cannot activate the preset default"), Player->TryActivateEquippedModule());
	TestTrue(TEXT("real sortie deploys the equipped slot"), Player->GetRespawnComponent()->DeployDeckSlot(Deck.Get(), 1, FTransform(FVector(2000, 0, 0))));
	Deck->SetSelectedSlotIndex(0);
	TestEqual(TEXT("later deck selection does not alter deployed snapshot"), Player->GetEquippedModule(), Module);
	TestTrue(TEXT("equipped offline slot activates"), Player->TryActivateEquippedModule());
	TestTrue(TEXT("slot module activation reaches the real GAS effect"), Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Module_EffectActive));
	TestEqual(TEXT("dispatch does not modify the shared preset"), Suit->EquippedModule.Get(), Module);
	return true;
}

#endif
