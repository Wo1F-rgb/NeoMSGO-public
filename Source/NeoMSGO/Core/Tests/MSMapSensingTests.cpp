#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "AI/MSBotCharacter.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/MSMapSensingSubsystem.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSRadarPod.h"
#include "Gameplay/MSPlayerController.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSMapTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapSensingBoundsTest,
	"NeoMSGO.Map.Sensing.ContactAndRadarBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapSensingLineOfSightTest,
	"NeoMSGO.Map.Sensing.LineOfSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapSensingAuthorityPipelineTest,
	"NeoMSGO.Map.Sensing.AuthorityPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSMapSensingBoundsTest::RunTest(const FString&)
{
	TestFalse(TEXT("an unseen enemy has no held contact"),
		UMSMapSensingSubsystem::IsContactWithinHold(-1.0, 0.0));
	TestTrue(TEXT("a confirmed enemy is retained for exactly five seconds"),
		UMSMapSensingSubsystem::IsContactWithinHold(10.0, 15.0));
	TestFalse(TEXT("an old confirmation expires immediately beyond five seconds"),
		UMSMapSensingSubsystem::IsContactWithinHold(10.0, 15.001));
	TestFalse(TEXT("future timestamps cannot disclose a contact"),
		UMSMapSensingSubsystem::IsContactWithinHold(15.0, 10.0));
	TestFalse(TEXT("negative client receipt time rejects a stale snapshot"),
		AMSPlayerController::IsMapContactSnapshotTimeFresh(-1.0, 0.0));
	TestFalse(TEXT("future client receipt time rejects a snapshot"),
		AMSPlayerController::IsMapContactSnapshotTimeFresh(10.0, 9.0));
	TestTrue(TEXT("client snapshot remains fresh at two-second boundary"),
		AMSPlayerController::IsMapContactSnapshotTimeFresh(10.0, 12.0));
	TestFalse(TEXT("client snapshot expires after two seconds"),
		AMSPlayerController::IsMapContactSnapshotTimeFresh(10.0, 12.001));
	TestTrue(TEXT("human ally retains existing blue marker"),
		MSMapPalette::FriendlyContactColor(255).Equals(MSMapPalette::Friendly));
	TestFalse(TEXT("medium bot tier remains distinguishable from weak on the map"),
		MSMapPalette::FriendlyContactColor(0).Equals(MSMapPalette::FriendlyContactColor(1)));
	TestFalse(TEXT("strong bot tier remains distinguishable from medium on the map"),
		MSMapPalette::FriendlyContactColor(1).Equals(MSMapPalette::FriendlyContactColor(2)));

	const FVector Center(1234.0f, -5678.0f, 0.0f);
	const float NormalRadiusCm = AMSRadarPod::DefaultRangeStat * 10.0f;
	TestEqual(TEXT("listed 4800 radar stat yields 480 metre radius"), NormalRadiusCm, 48000.0f);
	TestTrue(TEXT("radar includes its exact 480 metre boundary"),
		AMSRadarPod::IsInsideRadarRange(Center, Center + FVector(NormalRadiusCm, 0, 0), NormalRadiusCm));
	TestFalse(TEXT("radar excludes a target one centimetre outside"),
		AMSRadarPod::IsInsideRadarRange(Center, Center + FVector(NormalRadiusCm + 1.0f, 0, 0), NormalRadiusCm));
	TestTrue(TEXT("listed 5280 maximum reaches 528 metres"),
		AMSRadarPod::IsInsideRadarRange(Center, Center + FVector(0, 52800.0f, 0), 5280.0f * 10.0f));
	return true;
}

bool FMSMapSensingLineOfSightTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMapSensingLOSTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	// This LOS-only world has no GameMode to dispatch BeginPlay. Ensure newly
	// spawned radar actors initialize their ASC and bind their death delegate.
	if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
	TestTrue(TEXT("fixture dispatches actor BeginPlay"), World->GetBegunPlay());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Friendly = World->SpawnActor<AMSPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AMSPlayerCharacter* Enemy = World->SpawnActor<AMSPlayerCharacter>(
		FVector(2000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("friendly test character"), Friendly) ||
		!TestNotNull(TEXT("enemy test character"), Enemy))
	{
		World->EndPlay(EEndPlayReason::Quit);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("clear friendly sight reveals the enemy"),
		UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Friendly, Enemy));
	AActor* Wall = World->SpawnActor<AActor>(FVector(1000.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator, Params);
	UBoxComponent* Barrier = NewObject<UBoxComponent>(Wall, TEXT("MapSightWall"));
	Wall->AddInstanceComponent(Barrier);
	Wall->SetRootComponent(Barrier);
	Barrier->SetBoxExtent(FVector(50.0f, 500.0f, 10000.0f));
	Barrier->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Barrier->SetCollisionResponseToAllChannels(ECR_Ignore);
	Barrier->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	Barrier->RegisterComponent();
	TestFalse(TEXT("opaque weapon-trace wall hides the enemy"),
		UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Friendly, Enemy));
	Wall->Destroy();
	Enemy->SetActorLocation(FVector(UMSMapSensingSubsystem::VisualRangeCm + 1.0f, 0.0f, 0.0f));
	TestFalse(TEXT("enemy outside visual range stays hidden"),
		UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Friendly, Enemy));
	Friendly->SetGenericTeamId(FGenericTeamId(1));
	Enemy->SetGenericTeamId(FGenericTeamId(2));
	AMSPlayerController* Viewer = World->SpawnActor<AMSPlayerController>(FVector::ZeroVector,
		FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("owner controller for position-only snapshot"), Viewer))
	{
		Viewer->Possess(Friendly);
		FMSMapContact Contact;
		Contact.ContactId = Enemy->GetUniqueID();
		Contact.LastKnownLocation = Enemy->GetActorLocation();
		Contact.bCurrentlyDetected = true;
		FMSMapRadarRange FriendlyRadar;
		FriendlyRadar.RadarId = 88;
		FriendlyRadar.Center = FVector(150.0f, -700.0f, 0.0f);
		FriendlyRadar.RadiusCm = 48000.0f;
		FMSMapFriendlyContact Ally;
		Ally.UnitId = 71;
		Ally.Location = FVector(-1200.0f, 4500.0f, 0.0f);
		Ally.BotTier = 2;
		Viewer->PublishMapContacts({Contact}, {Ally}, {FriendlyRadar}, 1);
		Enemy->SetActorLocation(FVector(3000.0f, 0.0f, 0.0f));
		TestTrue(TEXT("fresh team snapshot is visible to its owner"), Viewer->AreMapContactsFresh());
		if (TestEqual(TEXT("one owner-only contact was delivered"), Viewer->GetMapContacts().Num(), 1) &&
			TestEqual(TEXT("one remote ally is present without an actor reference"), Viewer->GetMapFriendlyContacts().Num(), 1) &&
			TestEqual(TEXT("one friendly radar was delivered"), Viewer->GetMapRadarRanges().Num(), 1))
		{
			TestEqual(TEXT("position-only DTO has stable opaque contact id"), Viewer->GetMapContacts()[0].ContactId,
				Enemy->GetUniqueID());
			TestTrue(TEXT("enemy movement never changes last-known contact position"),
				Viewer->GetMapContacts()[0].LastKnownLocation.Equals(Contact.LastKnownLocation));
			TestEqual(TEXT("friendly radar position/radius share the owner snapshot"),
				Viewer->GetMapRadarRanges()[0].RadiusCm, 48000.0f);
			TestEqual(TEXT("ally tier survives owner snapshot"), Viewer->GetMapFriendlyContacts()[0].BotTier, uint8(2));
			TestTrue(TEXT("ally position is independent of local actor relevancy"),
				Viewer->GetMapFriendlyContacts()[0].Location.Equals(Ally.Location));
		}
		Friendly->SetGenericTeamId(FGenericTeamId(2));
		TestFalse(TEXT("team change invalidates old enemy and radar snapshot"), Viewer->AreMapContactsFresh());
		Friendly->SetGenericTeamId(FGenericTeamId(1));
	}
	for (int32 Index = 0; Index < AMSRadarPod::MaxPerDeployer + 1; ++Index)
		AMSRadarPod::Spawn(World, FVector(0.0f, Index * 200.0f, 0.0f),
			Friendly, FGenericTeamId(1), AMSRadarPod::DefaultRangeStat);
	int32 ActiveRadars = 0;
	AMSRadarPod* DamageTarget = nullptr;
	for (TActorIterator<AMSRadarPod> It(World); It; ++It)
		if (IsValid(*It)) { ++ActiveRadars; DamageTarget = *It; }
	TestEqual(TEXT("the fourth deployment replaces the oldest radar"),
		ActiveRadars, AMSRadarPod::MaxPerDeployer);
	if (DamageTarget)
	{
		TestEqual(TEXT("radar BeginPlay initializes 300 armor"), DamageTarget->GetVitalSet()->GetArmor(), 300.0f);
		TestFalse(TEXT("friendly fire cannot damage the radar"),
			DamageTarget->ApplyWeaponDamage(Friendly, 500.0f));
		TestTrue(TEXT("friendly hit leaves radar sensing active"), DamageTarget->IsActive());
		TestTrue(TEXT("enemy direct hit damages the radar"),
			DamageTarget->ApplyWeaponDamage(Enemy, 500.0f));
		TestEqual(TEXT("fatal hit consumes radar armor"), DamageTarget->GetVitalSet()->GetArmor(), 0.0f);
		// Destroy() disables and removes the actor, but a UObject pointer may remain
		// IsValid until the world's destroy queue/GC is processed.
		TestFalse(TEXT("fatal enemy hit immediately disables radar sensing"), DamageTarget->IsActive());
		TestTrue(TEXT("fatal enemy hit schedules replicated radar destruction"),
			DamageTarget->IsActorBeingDestroyed());
	}
	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

namespace MSMapSensingAuthorityTest
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMapSensingAuthorityWorld"));
		FWorldScope()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		void Advance(const float Seconds)
		{
			float Remaining = Seconds;
			while (Remaining > 0.0f)
			{
				const float Step = FMath::Min(0.1f, Remaining);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
				Remaining -= Step;
			}
		}
	};
}

bool FMSMapSensingAuthorityPipelineTest::RunTest(const FString&)
{
	using namespace MSMapSensingAuthorityTest;
	FWorldScope Scope;
	UWorld* World = Scope.World;
	UMSMapSensingSubsystem* Sensing = World->GetSubsystem<UMSMapSensingSubsystem>();
	UMSUnitRegistrySubsystem* Registry = World->GetSubsystem<UMSUnitRegistrySubsystem>();
	if (!TestNotNull(TEXT("real world sensing subsystem"), Sensing) ||
		!TestNotNull(TEXT("real world unit registry"), Registry)) return false;
	TestTrue(TEXT("fixture is authoritative"), World->GetNetMode() != NM_Client);
	TestTrue(TEXT("fixture dispatches BeginPlay"), World->GetBegunPlay());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto SpawnPlayer = [&](const FVector& Location, uint8 Team)
	{
		AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(Location, FRotator::ZeroRotator, Params);
		if (Unit)
		{
			Unit->SetGenericTeamId(FGenericTeamId(Team));
			Unit->GetCharacterMovement()->DisableMovement();
		}
		return Unit;
	};
	AMSPlayerCharacter* PawnA = SpawnPlayer(FVector(-300000, -300000, 0), 1);
	AMSPlayerCharacter* PawnB = SpawnPlayer(FVector(300000, -300000, 0), 1);
	AMSPlayerCharacter* Enemy = SpawnPlayer(FVector(10000, 0, 0), 2);
	// A real bot supplies the shared observation and bot-tier DTO. Disable its AI
	// before BeginPlay so combat decisions cannot move/kill the sensing fixtures.
	AMSBotCharacter* Scout = World->SpawnActorDeferred<AMSBotCharacter>(AMSBotCharacter::StaticClass(),
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("viewer A pawn"), PawnA) || !TestNotNull(TEXT("viewer B pawn"), PawnB)
		|| !TestNotNull(TEXT("enemy"), Enemy) || !TestNotNull(TEXT("allied scout bot"), Scout)) return false;
	Scout->AutoPossessAI = EAutoPossessAI::Disabled;
	Scout->SetGenericTeamId(FGenericTeamId(1));
	Scout->FinishSpawning(FTransform::Identity);
	Scout->GetCharacterMovement()->DisableMovement();
	Scout->SetEnemyTier(EMSEnemyTier::Strong);
	AMSPlayerController* ViewerA = World->SpawnActor<AMSPlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AMSPlayerController* ViewerB = World->SpawnActor<AMSPlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("same-team viewer A"), ViewerA) || !TestNotNull(TEXT("same-team viewer B"), ViewerB)) return false;
	ViewerA->Possess(PawnA);
	ViewerB->Possess(PawnB);
	TestTrue(TEXT("all fixtures register through normal actor BeginPlay"),
		Registry->IsRegistered(PawnA) && Registry->IsRegistered(PawnB)
		&& Registry->IsRegistered(Scout) && Registry->IsRegistered(Enemy));
	TestFalse(TEXT("viewer A cannot personally see the enemy"), UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, PawnA, Enemy));
	TestFalse(TEXT("viewer B cannot personally see the enemy"), UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, PawnB, Enemy));
	TestTrue(TEXT("allied scout has the only friendly visual sightline"), UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Scout, Enemy));
	AMSRadarPod* FriendlyRadar = AMSRadarPod::Spawn(World, FVector(-500000, 0, 0), PawnA,
		FGenericTeamId(1), AMSRadarPod::DefaultRangeStat);
	AMSRadarPod* EnemyRadar = AMSRadarPod::Spawn(World, FVector(10000, 1000, 0), Enemy,
		FGenericTeamId(2), AMSRadarPod::DefaultRangeStat);
	if (!TestNotNull(TEXT("friendly radar"), FriendlyRadar) || !TestNotNull(TEXT("opposing radar"), EnemyRadar)) return false;
	const uint32 FriendlyRadarId = FriendlyRadar->GetUniqueID();
	TestFalse(TEXT("remote friendly radar cannot explain the visual detection"),
		AMSRadarPod::IsInsideRadarRange(FriendlyRadar->GetActorLocation(), Enemy->GetActorLocation(), FriendlyRadar->GetRadarRadiusCm()));

	// Public production Tick -> private UpdateTeamContacts -> PublishMapContacts.
	// No synthetic contact arrays, direct RPC implementation or private sighting
	// timestamps are injected here. These are local standalone owners, not remote clients.
	const auto Refresh = [&]() { Sensing->Tick(UMSMapSensingSubsystem::UpdateIntervalSeconds); };
	const auto CheckSharedContact = [&](const TCHAR* Phase, bool Detected, const FVector& Expected)
	{
		for (AMSPlayerController* Viewer : {ViewerA, ViewerB})
		{
			const FString Prefix = FString::Printf(TEXT("%s %s"), Phase, *Viewer->GetName());
			TestTrue(Prefix + TEXT(" has a fresh producer snapshot"), Viewer->AreMapContactsFresh());
			if (TestEqual(Prefix + TEXT(" receives one enemy contact"), Viewer->GetMapContacts().Num(), 1))
			{
				const FMSMapContact& Contact = Viewer->GetMapContacts()[0];
				TestEqual(Prefix + TEXT(" preserves enemy identity"), Contact.ContactId, Enemy->GetUniqueID());
				TestEqual(Prefix + TEXT(" detection state"), Contact.bCurrentlyDetected, Detected);
				TestTrue(Prefix + TEXT(" observed position only"), Contact.LastKnownLocation.Equals(Expected, 0.1f));
			}
		}
	};
	const auto CheckFriendlyDTOs = [&](int32 RadarCount)
	{
		for (AMSPlayerController* Viewer : {ViewerA, ViewerB})
		{
			const auto& Friendlies = Viewer->GetMapFriendlyContacts();
			TestEqual(TEXT("producer includes other owner pawn and allied bot"), Friendlies.Num(), 2);
			TestFalse(TEXT("producer omits viewer's own pawn from friendlies"), Friendlies.ContainsByPredicate(
				[&](const FMSMapFriendlyContact& Item) { return Item.UnitId == Viewer->GetPawn()->GetUniqueID(); }));
			TestFalse(TEXT("enemy cannot enter friendly DTO array"), Friendlies.ContainsByPredicate(
				[&](const FMSMapFriendlyContact& Item) { return Item.UnitId == Enemy->GetUniqueID(); }));
			const FMSMapFriendlyContact* ScoutDTO = Friendlies.FindByPredicate(
				[&](const FMSMapFriendlyContact& Item) { return Item.UnitId == Scout->GetUniqueID(); });
			if (TestNotNull(TEXT("producer contains shared scout DTO"), ScoutDTO))
			{
				TestEqual(TEXT("producer copies actual bot tier"), ScoutDTO->BotTier, uint8(EMSEnemyTier::Strong));
				TestTrue(TEXT("producer copies scout world position"), ScoutDTO->Location.Equals(Scout->GetActorLocation(), 0.1f));
			}
			const auto& Ranges = Viewer->GetMapRadarRanges();
			if (TestEqual(TEXT("only active allied radars enter DTO"), Ranges.Num(), RadarCount) && RadarCount == 1)
			{
				TestEqual(TEXT("producer copies friendly radar identity"), Ranges[0].RadarId, FriendlyRadarId);
				TestTrue(TEXT("producer copies actual radar position"), Ranges[0].Center.Equals(FriendlyRadar->GetActorLocation(), 0.1f));
				TestEqual(TEXT("producer copies 480m range"), Ranges[0].RadiusCm, 48000.0f);
			}
		}
	};
	Refresh();
	const FVector VisualPosition = Enemy->GetActorLocation();
	CheckSharedContact(TEXT("shared ally vision"), true, VisualPosition);
	CheckFriendlyDTOs(1);

	AActor* Wall = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("sight occluder"), Wall)) return false;
	UBoxComponent* Barrier = NewObject<UBoxComponent>(Wall, TEXT("SharedSightBarrier"));
	Wall->AddInstanceComponent(Barrier);
	Wall->SetRootComponent(Barrier);
	Barrier->SetBoxExtent(FVector(100, 20000, 20000));
	Barrier->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Barrier->SetCollisionResponseToAllChannels(ECR_Ignore);
	Barrier->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	Barrier->RegisterComponent();
	Wall->SetActorLocation(FVector(5000, 0, 0));
	Enemy->SetActorLocation(FVector(12000, 4000, 0));
	TestFalse(TEXT("wall removes the only shared visual sightline"), UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Scout, Enemy));
	Refresh();
	CheckSharedContact(TEXT("hidden enemy movement holds old observed position"), false, VisualPosition);
	Scope.Advance(4.8f);
	Refresh();
	CheckSharedContact(TEXT("held before five-second expiry"), false, VisualPosition);
	Scope.Advance(0.4f);
	Refresh();
	for (AMSPlayerController* Viewer : {ViewerA, ViewerB})
	{
		TestTrue(TEXT("snapshot remains fresh after contact expiry"), Viewer->AreMapContactsFresh());
		TestEqual(TEXT("producer removes occluded contact after five seconds"), Viewer->GetMapContacts().Num(), 0);
	}
	CheckFriendlyDTOs(1);

	FriendlyRadar->SetActorLocation(Enemy->GetActorLocation() + FVector(0, 1500, 0));
	TestFalse(TEXT("radar acquisition does not reopen visual LOS"), UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Scout, Enemy));
	Refresh();
	CheckSharedContact(TEXT("friendly radar shares through occlusion"), true, Enemy->GetActorLocation());
	Enemy->SetActorLocation(FVector(16000, 4000, 0));
	Refresh();
	const FVector RadarPosition = Enemy->GetActorLocation();
	CheckSharedContact(TEXT("active radar updates enemy position"), true, RadarPosition);
	CheckFriendlyDTOs(1);
	TestTrue(TEXT("hostile hit destroys friendly radar through real damage entry"), FriendlyRadar->ApplyWeaponDamage(Enemy, 500));
	TestFalse(TEXT("destroyed radar is inactive"), FriendlyRadar->IsActive());
	Enemy->SetActorLocation(FVector(18000, 4000, 0));
	Refresh();
	CheckSharedContact(TEXT("destroyed radar retains only last confirmed position"), false, RadarPosition);
	CheckFriendlyDTOs(0);
	TestTrue(TEXT("enemy radar still exists but cannot reveal enemies to the wrong team"), EnemyRadar->IsActive());
	Scope.Advance(5.2f);
	Refresh();
	for (AMSPlayerController* Viewer : {ViewerA, ViewerB})
		TestEqual(TEXT("loss of radar detection expires held contacts"), Viewer->GetMapContacts().Num(), 0);
	CheckFriendlyDTOs(0);
	// Reacquire through actual visual sensing, then change the same registered
	// actor's team without advancing time. A held enemy entry must disappear at
	// the next producer update, not survive beside its new friendly DTO for 5s.
	Enemy->SetActorLocation(FVector(1000, 1000, 0));
	TestTrue(TEXT("enemy returns to a clear scout sightline before team change"),
		UMSMapSensingSubsystem::HasFriendlyLineOfSight(World, Scout, Enemy));
	Refresh();
	CheckSharedContact(TEXT("real enemy sighting before same-actor team change"), true, Enemy->GetActorLocation());
	const double TeamChangeTime = World->GetTimeSeconds();
	const uint32 ChangedUnitId = Enemy->GetUniqueID();
	Enemy->SetGenericTeamId(FGenericTeamId(1));
	Refresh();
	TestEqual(TEXT("team change removal does not wait for hold expiry"),
		static_cast<double>(World->GetTimeSeconds()), TeamChangeTime);
	for (AMSPlayerController* Viewer : {ViewerA, ViewerB})
	{
		TestTrue(TEXT("same-team owner receives a fresh team-change snapshot"), Viewer->AreMapContactsFresh());
		TestEqual(TEXT("former enemy immediately disappears from enemy DTOs"), Viewer->GetMapContacts().Num(), 0);
		const auto& Friendlies = Viewer->GetMapFriendlyContacts();
		TestEqual(TEXT("former enemy joins the other pawn and scout in friendly DTOs"), Friendlies.Num(), 3);
		const FMSMapFriendlyContact* ChangedDTO = Friendlies.FindByPredicate(
			[&](const FMSMapFriendlyContact& Item) { return Item.UnitId == ChangedUnitId; });
		if (TestNotNull(TEXT("same actor now has a friendly DTO"), ChangedDTO))
		{
			TestTrue(TEXT("team-changed actor keeps its current position"),
				ChangedDTO->Location.Equals(Enemy->GetActorLocation(), 0.1f));
			TestEqual(TEXT("team-changed human has the player tier"), ChangedDTO->BotTier, uint8(255));
		}
	}
	AddInfo(TEXT("Authority producer and two local controller consumers tested; remote two-client transport/relevancy remains unverified."));
	return true;
}

#endif
