#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Gameplay/MSCapturePoint.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Serialization/JsonSerializer.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "UI/MSTitleWidget.h"
#include "UI/MSLoginWidget.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineAuthorityDeckTest, "NeoMSGO.Online.Authority.RejectMalformedDeck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSOnlineAuthorityDeckTest::RunTest(const FString&)
{
	auto Parse = [](const FString& Text, FMSOnlineDeckSnapshot& Deck)
	{
		TSharedPtr<FJsonObject> Object;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && MSOnline::ParseDeck(Object, Deck);
	};
	const FString EmptySlot = TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null]}");
	const FString Valid = TEXT("{\"schema_version\":1,\"revision\":0,\"selected_slot\":0,\"slots\":[") +
		EmptySlot + TEXT(",") + EmptySlot + TEXT(",") + EmptySlot + TEXT(",") + EmptySlot + TEXT("]}");
	FMSOnlineDeckSnapshot Deck;
	TestTrue(TEXT("Explicit empty slots retain the 4 by 3 schema"), Parse(Valid, Deck));
	TestTrue(TEXT("An empty deck is valid storage, but has no deployable suit"), MSOnline::ValidateDeck(Deck));
	TestFalse(TEXT("Fractional revision is rejected"), Parse(Valid.Replace(TEXT("\"revision\":0"), TEXT("\"revision\":0.5")), Deck));
	TestFalse(TEXT("Fractional slot is rejected"), Parse(Valid.Replace(TEXT("\"selected_slot\":0"), TEXT("\"selected_slot\":0.5")), Deck));
	TestFalse(TEXT("Missing weapon positions are rejected"), Parse(Valid.Replace(TEXT("[null,null,null]"), TEXT("[null,null]")), Deck));
	TestFalse(TEXT("A path is not a primary asset ID"), Parse(Valid.Replace(TEXT("\"suit_id\":null"), TEXT("\"suit_id\":\"/Game/Injected.Asset\"")), Deck));
	const FString ValidV2 = FString(TEXT("{\"schema_version\":2,\"revision\":0,\"selected_slot\":0,\"slots\":[")) +
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},") +
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},") +
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false},") +
		TEXT("{\"suit_id\":null,\"weapon_ids\":[null,null,null],\"shield_equipped\":false}]}" );
	TestTrue(TEXT("schema2 requires and preserves explicit independent shield state"), Parse(ValidV2, Deck));
	TestFalse(TEXT("schema2 without independent shield state is rejected"), Parse(ValidV2.Replace(TEXT(",\"shield_equipped\":false"), TEXT("")), Deck));
	Deck = FMSOnlineDeckSnapshot(); Deck.Slots[0].WeaponIds[0] = FPrimaryAssetId(TEXT("MSWeaponData:Missing"));
	TestFalse(TEXT("Weapons without a suit are rejected"), MSOnline::ValidateDeck(Deck));
	Deck = FMSOnlineDeckSnapshot(); Deck.Slots[0].SuitId = FPrimaryAssetId(TEXT("MSWeaponData:WrongClass"));
	TestFalse(TEXT("A weapon asset cannot substitute a suit"), MSOnline::ValidateDeck(Deck));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlinePendingPawnTest, "NeoMSGO.Online.Authority.PendingCannotSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSOnlinePendingPawnTest::RunTest(const FString&)
{
	struct FCommandScope
	{
		FString Previous = FCommandLine::Get();
		// Preserve engine switches such as -nullrhi: FApp::CanEverRender caches
		// that flag on its first call in this module, even after scope teardown.
		FCommandScope() { FCommandLine::Append(TEXT(" -MSAuthMode=Online -MSServerId=automation")); }
		~FCommandScope() { FCommandLine::Set(*Previous); }
	} CommandScope;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSOnlinePendingTest"));
	if (!TestNotNull(TEXT("test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMSGameMode* Mode = World->SpawnActor<AMSGameMode>();
	AMSPlayerController* PC = World->SpawnActor<AMSPlayerController>();
	TestFalse(TEXT("New controller is never implicitly authenticated"), PC->IsOnlineAuthenticated());
	TestNull(TEXT("Default pawn spawn rejects a pending owner"), Mode->SpawnDefaultPawnAtTransform_Implementation(PC, FTransform::Identity));
	Mode->RestartPlayer(PC);
	TestNull(TEXT("RestartPlayer also rejects pending owner"), PC->GetPawn());
	TestFalse(TEXT("Pending owner cannot submit deployment"), PC->RequestOnlineDeployment(0, TEXT("InjectedPoint")));
	AMSPlayerState* State = World->SpawnActor<AMSPlayerState>();
	State->SetLoginUsername(TEXT("InjectedName"));
	TestTrue(TEXT("Online ignores the legacy self-asserted name setter"), State->GetLoginUsername().IsEmpty());
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineRetainedPresentationTest, "NeoMSGO.Online.Authority.RetainedPresentation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineRecallRedeployTest, "NeoMSGO.Online.Authority.RecallRedeploy",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSOnlineRetainedPresentationTest::RunTest(const FString&)
{
 const FString PreviousCommand = FCommandLine::Get();
 FCommandLine::Append(TEXT(" -MSAuthMode=Online"));
 UMSGameInstance* Instance = NewObject<UMSGameInstance>(GEngine);
 Instance->AddToRoot(); Instance->InitializeStandalone();
 UWorld* World = Instance->GetWorld();
 ON_SCOPE_EXIT
 {
  Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
  Instance->RemoveFromRoot(); FCommandLine::Set(*PreviousCommand);
 };
 World->InitializeActorsForPlay(FURL()); World->BeginPlay();
 if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
 ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
 Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
 auto* PC = World->SpawnActor<AMSPlayerController>();
 PC->PlayerState = World->SpawnActor<AMSPlayerState>(); PC->SetPlayer(LocalPlayer);
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Pawn = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), FVector(0,0,1000), FRotator::ZeroRotator, Params);
 Instance->bPendingSortie = true;
 // Authentication may arrive before the admission Pawn reference. This standalone
 // fixture must not impersonate the unauthed frontend, which correctly opens Title.
 PC->bOnlineAuthenticated = true;
 PC->Possess(Pawn); Pawn->PawnClientRestart();
 TestFalse(TEXT("Pawn restart alone waits for complete owner admission"), PC->bOnlineAdmissionPresented);
 TestTrue(TEXT("pending first-sortie intent survives the early Pawn restart"), Instance->bPendingSortie);
 // Explicitly inject owner replication fields; this is an arrival-order regression,
 // not proof of HTTP admission or a real socket reconnect.
 PC->bOnlineAuthenticated = true; PC->OnlineIdentity.AccountId = TEXT("fixture");
 PC->OnlineIdentity.RealmId = TEXT("fixture-realm"); PC->OnlineAssignedTeamId = MSTeam::PlayerTeamId().GetId();
 PC->bOnlineRestoredDeployment = true; PC->bOnlineDeployed = true;
 PC->OnRep_OnlineAdmission();
 TestFalse(TEXT("admission before the retained Pawn mapping still waits"), PC->bOnlineAdmissionPresented);
 PC->OnlineAdmissionPawn = Pawn; PC->OnRep_OnlineAdmission();
 TestFalse(TEXT("default initial respawn readiness is never treated as restored deployment"), PC->bOnlineAdmissionPresented);
 auto* Suit = NewObject<UMSMobileSuitData>(World); Suit->MaxArmor = 2000; Suit->ReuseDelaySeconds = 30;
 auto* Weapon = NewObject<UMSRangedWeaponData>(World);
 Weapon->MagazineSize = 10; Weapon->ReserveAmmo = 30; Weapon->ReloadTime = 8; Weapon->WeaponSwitchDuration = 2;
 FMSDeckLoadout Loadout; Loadout.EquippedWeapons[0] = Weapon; Loadout.EquippedWeapons[1] = Weapon;
 auto* Respawn = Pawn->GetRespawnComponent();
 TestTrue(TEXT("actual verified deploy creates the retained component snapshot"), Respawn->DeployVerifiedOnlineSlot(0, Suit, Loadout, FTransform(FVector(0,0,1000))));
 auto* Weapons = Pawn->GetWeaponComponent();
 Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Vital::GetArmorAttribute(), 1350);
 TestTrue(TEXT("real round consumption"), Weapons->ConsumeAmmoAndStartCooldown(0));
 TestTrue(TEXT("real reload begins"), Weapons->StartReload(0));
 TestTrue(TEXT("real slot switch begins while old slot reloads"), Weapons->SwitchToSlot(1));
 const float ReloadDeadline = Weapons->GetReloadEndServerTime(0), SwitchDeadline = Weapons->GetSwitchEndServerTime();
 auto* Briefing = CreateWidget<UMSBriefingWidget>(PC); Briefing->Open();
 auto* Spawn = CreateWidget<UMSSpawnSelectWidget>(PC); Spawn->OpenRespawn(Respawn);
 auto* Map = CreateWidget<UMSDeploymentMapWidget>(PC); Map->Open();
 TestTrue(TEXT("fixture has a real open briefing"), Briefing->IsOpen());
 TestTrue(TEXT("fixture has a real open deployment map"), Map->IsOpen());
 PC->OnRep_OnlineAdmission();
 TestTrue(TEXT("late snapshot completes restored admission"), PC->bOnlineAdmissionPresented);
 TestFalse(TEXT("restored live player consumes travel's initial-sortie intent"), Instance->bPendingSortie);
 TestFalse(TEXT("briefing is closed without returning to lobby"), Briefing->IsOpen());
 TestFalse(TEXT("spawn selection is closed"), Spawn->IsOpen());
 TestFalse(TEXT("deployment map is closed"), Map->IsOpen());
 TestTrue(TEXT("live owner has gameplay input after cleanup"), Instance->GetSubsystem<UMSUILockSubsystem>()->IsGameplayActive());
 TestEqual(TEXT("retained damaged armor is not healed"), Pawn->GetVitalSet()->GetArmor(), 1350.0f);
 TestEqual(TEXT("retained magazine is not replenished"), Weapons->GetMagazineAmmo(0), 9);
 TestEqual(TEXT("retained slot stays selected"), Weapons->GetActiveSlotIndex(), 1);
 TestEqual(TEXT("reload deadline is unchanged"), Weapons->GetReloadEndServerTime(0), ReloadDeadline);
 TestEqual(TEXT("switch deadline is unchanged"), Weapons->GetSwitchEndServerTime(), SwitchDeadline);
 // Repeated owner notifications cannot close UI subsequently opened by the player.
 Map->Open(); PC->OnRep_OnlineAdmission();
 TestTrue(TEXT("repeat admission notification cannot dismiss a later UI"), Map->IsOpen()); Map->Close();
 auto* Killer = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), FVector(10000,0,1000), FRotator::ZeroRotator, Params);
 Killer->SetGenericTeamId(MSTeam::EnemyTeamId()); Pawn->SetGenericTeamId(MSTeam::PlayerTeamId());
 auto Context = Killer->GetAbilitySystemComponent()->MakeEffectContext(); Context.AddInstigator(Killer, Killer);
 FGameplayEffectSpec Damage(GetDefault<UGE_Damage>(), Context, 1);
 Damage.SetSetByCallerMagnitude(MSTags::Data_Damage, 5000); Damage.SetSetByCallerMagnitude(MSTags::Data_DownPoint, 0);
 Pawn->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(Damage);
 TestTrue(TEXT("real hostile GE enters authoritative death"), Pawn->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
 const auto DeadSnapshot = Respawn->GetOnlineRespawnSnapshot();
 TestTrue(TEXT("real death starts the pending timer"), Respawn->IsRespawnPending());
 PC->UnPossess(); PC->bOnlineAdmissionPresented = false; Instance->bPendingSortie = true;
 PC->OnRep_OnlineAdmission();
 TestFalse(TEXT("owner state before re-possession cannot complete"), PC->bOnlineAdmissionPresented);
 PC->Possess(Pawn); Pawn->PawnClientRestart();
 TestTrue(TEXT("Pawn restart completes already-arrived dead admission"), PC->bOnlineAdmissionPresented);
 TestTrue(TEXT("dead reconnect shows death presentation"), Pawn->IsDeathPresentationActive());
 TestFalse(TEXT("dead reconnect does not open initial briefing"), Briefing->IsOpen());
 TestEqual(TEXT("death deadline survives reconnect"), Respawn->GetOnlineRespawnSnapshot().ReadyAt, DeadSnapshot.ReadyAt);
 TestEqual(TEXT("slot cooldown deadline survives reconnect"), Respawn->GetOnlineRespawnSnapshot().SlotReadyAt[0], DeadSnapshot.SlotReadyAt[0]);
 TestEqual(TEXT("dead reconnect never heals"), Pawn->GetVitalSet()->GetArmor(), 0.0f);
 // Advance ordinary frames: UWorld clamps a single 7.1-second delta, and a
 // not-yet-ticked TimerManager only promotes newly pending timers at tick end.
 // This standalone authority fixture has no GameState yet, so synchronized
 // respawn time uses World.TimeSeconds, the same clock as per-slot cooldowns.
 TestFalse(TEXT("restored death countdown world is not paused by stale frontend"), World->IsPaused());
 const double WaitStartedAt = World->GetTimeSeconds();
 const float RemainingWait = Respawn->GetRespawnTimeRemaining();
 const float SlotTimeBeforeWait = Respawn->GetDeckSlotTimeRemaining(0);
 TestTrue(TEXT("real death deadline still requires the normal wait"), RemainingWait >= Respawn->GetRespawnDelay() - 0.01f);
 constexpr float FrameStep = 0.05f;
 const int32 FramesToAdvance = FMath::CeilToInt((RemainingWait + 0.2f) / FrameStep);
 for (int32 Frame = 0; Frame < FramesToAdvance; ++Frame)
 {
  ++GFrameCounter; // TimerManager intentionally ticks at most once per engine frame.
  World->Tick(LEVELTICK_All, FrameStep);
 }
 const float WorldElapsed = static_cast<float>(World->GetTimeSeconds() - WaitStartedAt);
 TestTrue(TEXT("actual unpaused authority world time advanced through the death wait"), WorldElapsed >= RemainingWait);
 TestTrue(TEXT("slot cooldown loses only the elapsed world time"), FMath::IsNearlyEqual(
  Respawn->GetDeckSlotTimeRemaining(0), FMath::Max(0.0f, SlotTimeBeforeWait - WorldElapsed), 0.02f));
 TestTrue(TEXT("actual death wait expires"), Respawn->CanDeploy());
 PC->UnPossess(); PC->bOnlineAdmissionPresented = false; PC->Possess(Pawn); Pawn->PawnClientRestart();
 TestTrue(TEXT("ready dead reconnect opens the existing respawn screen"), Spawn->IsOpen());
 TestTrue(TEXT("slot cooldown is still positive after global death wait"), Respawn->GetDeckSlotTimeRemaining(0) > 0);
 TestEqual(TEXT("no reconnect increments deployment revision"), Respawn->GetOnlineRespawnSnapshot().DeployRevision, DeadSnapshot.DeployRevision);
 // A new, never-deployed admission still follows the normal briefing path.
 Spawn->Close();
 World->SetGameState(World->SpawnActor<AMSGameState>());
 PC->UnPossess(); PC->bOnlineAdmissionPresented = false; PC->bOnlineRestoredDeployment = false; PC->bOnlineDeployed = false;
 auto* NewPawn = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(), FVector(20000,0,1000), FRotator::ZeroRotator, Params);
 PC->OnlineAdmissionPawn = NewPawn; Instance->bPendingSortie = true; Instance->PendingRuleIndex = 0;
 PC->Possess(NewPawn); NewPawn->PawnClientRestart();
 TestTrue(TEXT("ordinary first admission opens its briefing"), Briefing->IsOpen());
 TestTrue(TEXT("ordinary first admission completes presentation"), PC->bOnlineAdmissionPresented);
 TestEqual(TEXT("first admission presentation does not deploy"), NewPawn->GetRespawnComponent()->GetOnlineRespawnSnapshot().DeployRevision, 0);
 Briefing->CloseForBattleEnd();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSOnlineStandaloneEntryTest, "NeoMSGO.Online.Authority.StandaloneTitleLoginEntry",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSOnlineStandaloneEntryTest::RunTest(const FString&)
{
 const FString PreviousCommand = FCommandLine::Get();
 FCommandLine::Append(TEXT(" -MSAuthMode=Online"));
 UMSGameInstance* Instance = NewObject<UMSGameInstance>(GEngine);
 Instance->AddToRoot(); Instance->InitializeStandalone();
 UWorld* World = Instance->GetWorld();
 ON_SCOPE_EXIT
 {
  Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
  Instance->RemoveFromRoot(); FCommandLine::Set(*PreviousCommand);
 };
 World->InitializeActorsForPlay(FURL()); World->BeginPlay();
 if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
 ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
 Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
 auto* PC = World->SpawnActor<AMSPlayerController>();
 PC->PlayerState = World->SpawnActor<AMSPlayerState>(); PC->SetPlayer(LocalPlayer);
 auto* Pawn = World->SpawnActor<AMSPlayerCharacter>();
 TestEqual(TEXT("normal online frontend is a standalone world"), World->GetNetMode(), NM_Standalone);
 TestFalse(TEXT("entry does not require a server-authenticated player"), PC->IsOnlineAuthenticated());
 TestFalse(TEXT("entry begins logged out"), Instance->IsLoggedIn());
 PC->Possess(Pawn); Pawn->PawnClientRestart();
 TArray<UUserWidget*> Found;
 UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UMSTitleWidget::StaticClass(), false);
 UMSTitleWidget* Title = nullptr;
 for (UUserWidget* Widget : Found) if (Widget->GetOwningPlayer() == PC) { Title = Cast<UMSTitleWidget>(Widget); break; }
 if (!TestNotNull(TEXT("real Pawn restart creates the normal title"), Title)) return false;
 TestTrue(TEXT("online standalone entry shows the title"), Title->IsOpen());
 if (!TestNotNull(TEXT("title contains the actual start button"), Title->GetStartButtonForTesting())) return false;
 Title->GetStartButtonForTesting()->OnClicked.Broadcast();
 Found.Reset(); UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UMSLoginWidget::StaticClass(), false);
 UMSLoginWidget* Login = nullptr;
 for (UUserWidget* Widget : Found) if (Widget->GetOwningPlayer() == PC) { Login = Cast<UMSLoginWidget>(Widget); break; }
 if (!TestNotNull(TEXT("start action reaches the login widget without server admission"), Login)) return false;
 TestTrue(TEXT("login is visible after the real start handler"), Login->IsOpen());
 TestFalse(TEXT("opening login never authenticates the game connection"), PC->IsOnlineAuthenticated());
 Login->Close(); Title->Close();
 return true;
}

bool FMSOnlineRecallRedeployTest::RunTest(const FString&)
{
 const FString PreviousCommand = FCommandLine::Get();
 FCommandLine::Append(TEXT(" -MSAuthMode=Online"));
 UMSGameInstance* Instance = NewObject<UMSGameInstance>(GEngine);
 Instance->AddToRoot(); Instance->InitializeStandalone();
 UWorld* World = Instance->GetWorld();
 ON_SCOPE_EXIT
 {
  Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
  Instance->RemoveFromRoot(); FCommandLine::Set(*PreviousCommand);
 };
 World->InitializeActorsForPlay(FURL()); World->BeginPlay();
 if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();

 ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
 Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
 AMSPlayerController* PC = World->SpawnActor<AMSPlayerController>();
 PC->PlayerState = World->SpawnActor<AMSPlayerState>(); PC->SetPlayer(LocalPlayer);
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 AMSPlayerCharacter* Pawn = World->SpawnActor<AMSPlayerCharacter>(
  AMSPlayerCharacter::StaticClass(), FVector(0, 0, 1000), FRotator::ZeroRotator, Params);
 PC->Possess(Pawn); Pawn->PawnClientRestart();

 // This is an authenticated, already deployed owner fixture; no HTTP/auth result
 // is faked and the actual deployment path below still resolves the catalog slot.
 PC->bOnlineAuthenticated = true;
 PC->bOnlineDeployed = true;
 PC->OnlineIdentity.AccountId = TEXT("recall-fixture");
 PC->OnlineIdentity.RealmId = TEXT("recall-fixture-realm");
 PC->OnlineAssignedTeamId = MSTeam::PlayerTeamId().GetId();
 PC->OnlineAdmissionPawn = Pawn;
 PC->bOnlineRestoredDeployment = true;
 auto* State = CastChecked<AMSPlayerState>(PC->PlayerState);
 State->SetSelectedFaction(MSTeam::PlayerTeamId());
 Pawn->SetGenericTeamId(MSTeam::PlayerTeamId());

 UMSMobileSuitData* CatalogSuit = nullptr;
 FPrimaryAssetId CatalogSuitId;
 TArray<FPrimaryAssetId> SuitIds;
 UAssetManager::Get().GetPrimaryAssetIdList(UMSMobileSuitData::PrimaryAssetType, SuitIds);
 for (const FPrimaryAssetId& Id : SuitIds)
 {
  UMSMobileSuitData* Candidate = Cast<UMSMobileSuitData>(
   UAssetManager::Get().GetPrimaryAssetPath(Id).TryLoad());
  if (Candidate && Candidate->GetPrimaryAssetId() == Id && Candidate->MaxArmor > 0.0f)
  {
   CatalogSuit = Candidate; CatalogSuitId = Id; break;
  }
 }
 if (!TestNotNull(TEXT("a real catalog mobile-suit resolves"), CatalogSuit)) return false;

 FMSOnlineDeckSnapshot Deck;
 Deck.SchemaVersion = 1; Deck.Revision = 1; Deck.SelectedSlot = 0; Deck.Slots.SetNum(4);
 for (FMSOnlineDeckSlot& Slot : Deck.Slots) Slot.WeaponIds.SetNum(3);
 Deck.Slots[0].SuitId = CatalogSuitId;
 PC->OnlineDeck = Deck;

 // Use a transient current suit only to make the recall countdown deterministic;
 // RequestOnlineDeployment still resolves and deploys the real catalog suit.
 UMSMobileSuitData* CurrentSuit = NewObject<UMSMobileSuitData>(World);
 CurrentSuit->MaxArmor = 2000.0f;
 CurrentSuit->EmergencyRecallPerformance = 1000;
 Pawn->InitializeUnit(CurrentSuit);
 Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

 AMSCapturePoint* SpawnPoint = World->SpawnActor<AMSCapturePoint>(
  AMSCapturePoint::StaticClass(), FVector(0, 0, 0), FRotator::ZeroRotator, Params);
 SpawnPoint->SetFixedOwner(EMSCaptureOwner::TeamPlayer);
 UMSRespawnComponent* Respawn = Pawn->GetRespawnComponent();
 UMSEmergencyRecallComponent* Recall = Pawn->GetEmergencyRecallComponent();
 if (!TestNotNull(TEXT("respawn component exists"), Respawn) ||
  !TestNotNull(TEXT("emergency recall component exists"), Recall)) return false;

 // Establish a real deployed revision before testing the return-to-sortie path.
 FMSDeckLoadout EmptyLoadout;
 TestTrue(TEXT("initial verified deployment succeeds"), Respawn->DeployVerifiedOnlineSlot(
  0, CatalogSuit, EmptyLoadout, FTransform(FVector(0, 0, 1000))));
 PC->bOnlineDeployed = true;
 Pawn->InitializeUnit(CurrentSuit);
 Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
 const int32 DeployedRevision = Respawn->GetOnlineRespawnSnapshot().DeployRevision;
 TestTrue(TEXT("initial deploy publishes a live revision"), DeployedRevision > 0);

 // A living pawn may submit the transport request, but the server must reject it
 // without changing revision, recall state, armor, or visibility.
 const float ArmorBeforeReject = Pawn->GetVitalSet()->GetArmor();
 TestTrue(TEXT("living request reaches the normal deployment RPC path"),
  PC->RequestOnlineDeployment(0, SpawnPoint->GetFName()));
 TestEqual(TEXT("living request does not increment deployment revision"),
  Respawn->GetOnlineRespawnSnapshot().DeployRevision, DeployedRevision);
 TestFalse(TEXT("living request does not enter recall-awaiting state"), Recall->IsAwaitingDeploy());
 TestEqual(TEXT("living request does not replenish armor"), Pawn->GetVitalSet()->GetArmor(), ArmorBeforeReject);
 // Standalone automation has no client transport; clear only an unconsumed request
 // marker so the following public request can exercise the successful path.
 if (PC->bDeploymentPending) PC->ClientOnlineDeploymentResult_Implementation(false, FText::GetEmpty());

 TestTrue(TEXT("grounded deployed owner starts authoritative recall"), Recall->TryStartRecall());
 TestTrue(TEXT("recall countdown is active"), Recall->IsRecallActive());
 const float RecallRemaining = Recall->GetRecallTimeRemaining();
 TestTrue(TEXT("recall uses the configured short fixture countdown"), RecallRemaining > 4.0f && RecallRemaining < 6.0f);
 constexpr float FrameStep = 0.1f;
 for (float Elapsed = 0.0f; Elapsed < RecallRemaining + 0.25f; Elapsed += FrameStep)
 {
  ++GFrameCounter;
  World->Tick(LEVELTICK_All, FrameStep);
 }
 TestFalse(TEXT("authoritative recall countdown completed"), Recall->IsRecallActive());
 TestTrue(TEXT("completed recall awaits deployment"), Recall->IsAwaitingDeploy());
 const FMSOnlineRespawnSnapshot Awaiting = Respawn->GetOnlineRespawnSnapshot();
 TestTrue(TEXT("recall snapshot marks awaiting deployment"), Awaiting.bRecallAwaitingDeploy);
 TestFalse(TEXT("recall snapshot is not alive"), Awaiting.bAlive);
 TestTrue(TEXT("completed recall is immediately ready to select a slot"), Awaiting.bReady);
 TestTrue(TEXT("completed recall hides the pawn"), Pawn->IsHidden());
 TestFalse(TEXT("completed recall disables collision"), Pawn->GetActorEnableCollision());
 TestTrue(TEXT("completed recall disables movement"), Pawn->GetCharacterMovement()->MovementMode == MOVE_None);
 const float RecallZ = Pawn->GetActorLocation().Z;
 ++GFrameCounter; World->Tick(LEVELTICK_All, 1.0f);
 TestTrue(TEXT("hidden pawn stays at a stable height while awaiting deployment"),
  FMath::IsNearlyEqual(Pawn->GetActorLocation().Z, RecallZ, 0.01f));

 // Exercise the retained-pawn admission branch against the real snapshot state.
 PC->bOnlineAdmissionPresented = false;
 PC->OnRep_OnlineAdmission();
 TestTrue(TEXT("reconnect restores awaiting recall presentation"), PC->bOnlineAdmissionPresented);
 TestTrue(TEXT("reconnect keeps the pawn hidden"), Pawn->IsHidden());
 TestFalse(TEXT("reconnect keeps collision disabled"), Pawn->GetActorEnableCollision());

 TestTrue(TEXT("completed recall allows the normal public deployment request"),
  PC->RequestOnlineDeployment(0, SpawnPoint->GetFName()));
 const FMSOnlineRespawnSnapshot Redeployed = Respawn->GetOnlineRespawnSnapshot();
 TestFalse(TEXT("successful sortie consumes recall-awaiting state"), Redeployed.bRecallAwaitingDeploy);
 TestTrue(TEXT("successful sortie publishes alive state"), Redeployed.bAlive);
 TestTrue(TEXT("successful sortie increments deployment revision"), Redeployed.DeployRevision > DeployedRevision);
 TestFalse(TEXT("successful sortie clears recall component state"), Recall->IsAwaitingDeploy());
 TestFalse(TEXT("successful sortie reveals the pawn"), Pawn->IsHidden());
 TestTrue(TEXT("successful sortie restores collision"), Pawn->GetActorEnableCollision());
 TestTrue(TEXT("successful sortie remains deployed"), PC->IsOnlineDeployed());
 return true;
}
#endif
