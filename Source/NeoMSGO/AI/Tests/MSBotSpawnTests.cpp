// Copyright NeoMSGO Project. All Rights Reserved.
// B2 (docs/BotDeployment_Design.md): BOT初期配備スポーンマネージャの受け入れテスト。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotCharacter.h"
#include "AI/MSBotAIController.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AI/MSBotSpawnPoint.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSHomeBase.h"
#include "Misc/CommandLine.h"
#include "Player/MSRespawnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotSpawnTierDistributionTest, "NeoMSGO.AI.BotSpawn.B2.TierDistribution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotTierWeaponVariationTest, "NeoMSGO.AI.BotSpawn.Review.TierWeaponVariation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotSpawnInitialDeploymentTest, "NeoMSGO.AI.BotSpawn.B2.InitialDeployment",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotSpawnDisabledTest, "NeoMSGO.AI.BotSpawn.B2.SpawnBotsFalseSpawnsNothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotSpawnHomeBaseFallbackTest, "NeoMSGO.AI.BotSpawn.B2.HomeBaseFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBotSpawnNoLocationTest, "NeoMSGO.AI.BotSpawn.B2.ZeroSpawnDoesNotCrash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaiganUndergroundBotRedeployTest,
	"NeoMSGO.AI.BotSpawn.TaiganUndergroundRedeploy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaiganMarkerLayerPlacementTest,
	"NeoMSGO.AI.BotSpawn.TaiganMarkerLayerPlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaiganHumanDropColumnTest,
	"NeoMSGO.AI.BotSpawn.TaiganHumanDropColumn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSBotSpawnTest
{
	struct FScopedOfflineAuth
	{
		FString Previous = FCommandLine::Get();

		FScopedOfflineAuth()
		{
			TArray<FString> Tokens;
			Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)) continue;
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			Effective += TEXT("-MSAuthMode=OfflineLocal");
			FCommandLine::Set(*Effective);
		}

		~FScopedOfflineAuth() { FCommandLine::Set(*Previous); }
	};

	struct FScopedWorld
	{
		UWorld* World = nullptr;
		AMSGameMode* GameMode = nullptr;

		explicit FScopedWorld(const FName WorldName = FName(TEXT("MSBotSpawnB2World")))
		{
			// UWorld::GetMapName reads the package basename, not the UWorld object's
			// name. Keep a unique transient package whose basename is the real map
			// name so the production Taigan gate is exercised by this fixture.
			static int32 PackageSerial = 0;
			const FString PackageName = FString::Printf(TEXT("/Temp/NeoMSGOTest/BotSpawn_%d/%s"),
				++PackageSerial, *WorldName.ToString());
			UPackage* WorldPackage = CreatePackage(*PackageName);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, WorldPackage);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			GameMode = World->SpawnActor<AMSGameMode>();
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
			World->EndPlay(EEndPlayReason::Quit);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSBotSpawnPoint* AddSpawnPoint(UWorld* World, const EMSCaptureOwner Team, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSBotSpawnPoint* Point = World->SpawnActor<AMSBotSpawnPoint>(
			AMSBotSpawnPoint::StaticClass(), Location, FRotator::ZeroRotator, Params);
		Point->SetSpawnTeam(Team);
		return Point;
	}

	AActor* AddSolidBox(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Center);
		return Actor;
	}

	UMSMobileSuitData* MakeBotSuitData()
	{
		UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(GetTransientPackage());
		Suit->UnitName = FText::FromString(TEXT("B2TestSuit"));
		Suit->MaxArmor = 3000;
		Suit->MaxDownPoint = 100;
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(Suit);
		Suit->EquippedWeapons.Add(TSoftObjectPtr<UMSWeaponDataBase>(Weapon));
		return Suit;
	}

	int32 CountBotsOnTeam(UWorld* World, const FGenericTeamId TeamId)
	{
		int32 Count = 0;
		for (TActorIterator<AMSBotCharacter> It(World); It; ++It)
		{
			if (It->GetGenericTeamId() == TeamId)
			{
				++Count;
			}
		}
		return Count;
	}

	void CountTiersOnTeam(UWorld* World, const FGenericTeamId TeamId,
		int32& OutWeak, int32& OutMedium, int32& OutStrong)
	{
		OutWeak = 0;
		OutMedium = 0;
		OutStrong = 0;
		for (TActorIterator<AMSBotCharacter> It(World); It; ++It)
		{
			if (It->GetGenericTeamId() != TeamId)
			{
				continue;
			}
			switch (It->GetEnemyTier())
			{
			case EMSEnemyTier::Weak: ++OutWeak; break;
			case EMSEnemyTier::Medium: ++OutMedium; break;
			case EMSEnemyTier::Strong: ++OutStrong; break;
			}
		}
	}
}

bool FMSTaiganUndergroundBotRedeployTest::RunTest(const FString&)
{
	MSBotSpawnTest::FScopedOfflineAuth OfflineAuth;
	MSBotSpawnTest::FScopedWorld Scope(FName(TEXT("Lvl_NewYork_Taigan")));
	TestTrue(TEXT("fixture uses the opt-in Taigan map name"),
		Scope.World->GetMapName().EndsWith(TEXT("Lvl_NewYork_Taigan")));
	MSBotSpawnTest::AddSolidBox(Scope.World, FVector(0, 0, -6500),
		FVector(30000, 30000, 100)); // floor top -6400
	MSBotSpawnTest::AddSolidBox(Scope.World, FVector(0, 0, -3250),
		FVector(30000, 30000, 150)); // ceiling underside -3400
	MSBotSpawnTest::AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer,
		FVector(0, 0, -5569));
	UMSMobileSuitData* Suit = MSBotSpawnTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
	Manager->SetTeamBotCounts(1, 0);
	Manager->SetBotMobileSuitDataOverride(Suit);
	Scope.GameMode->StartPlay();
	if (!TestEqual(TEXT("one Taigan underground bot initially spawns"),
		Manager->GetSpawnedBots().Num(), 1)) return false;
	AMSBotCharacter* Bot = Manager->GetSpawnedBots()[0].Get();
	if (!TestNotNull(TEXT("initial Taigan bot exists"), Bot)) return false;
	const float HalfHeight = Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Initial = Bot->GetActorLocation();
	TestTrue(TEXT("initial bot capsule is between underground floor and ceiling"),
		Initial.Z - HalfHeight > -6400.0f && Initial.Z + HalfHeight < -3400.0f);
	UMSRespawnComponent* Respawn = Bot->GetRespawnComponent();
	if (!TestNotNull(TEXT("underground bot has respawn component"), Respawn)) return false;
	const FVector SavedTarget = Respawn->GetDefaultSpawnPoint().GetLocation();
	TestTrue(TEXT("default target is the rear spawn on the underground side of the layer gate"),
		FVector::Dist2D(SavedTarget, Initial) < 1.0f && SavedTarget.Z < -4500.0f);
	TestTrue(TEXT("default target is a standing capsule centre above the authored floor"),
		FMath::IsNearlyEqual(SavedTarget.Z, -6400.0f + HalfHeight + 50.0f, 1.0f));
	Bot->SetActorLocation(Initial + FVector(5000.0f, 0.0f, 0.0f));
	Respawn->DeployAtDefaultSpawn(Suit);
	const FVector Redeployed = Bot->GetActorLocation();
	TestTrue(TEXT("default redeploy returns to its authored underground spawn XY"),
		FVector::Dist2D(Redeployed, Initial) < 1.0f);
	TestTrue(TEXT("default redeploy remains in the same underground layer"),
		Redeployed.Z - HalfHeight > -6400.0f && Redeployed.Z + HalfHeight < -3400.0f);
	TestTrue(TEXT("default redeploy leaves the authored 50 cm roof clearance"),
		Redeployed.Z + HalfHeight <= -3450.0f + 1.0f);
	return true;
}

bool FMSTaiganMarkerLayerPlacementTest::RunTest(const FString&)
{
	using namespace MSBotSpawnTest;
	FScopedOfflineAuth OfflineAuth;
	const float HalfHeight = GetDefault<AMSBotCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestEqual(TEXT("Taigan scatter is limited to the authored marker neighbourhood"),
		UMSBotSpawnManagerComponent::GetTaiganSpawnScatterRadius(1500.0f * MSGO::MSScale), 3000.0f);
	TestTrue(TEXT("surface marker accepts its ground floor"),
		UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(HalfHeight + 10.0f, 10.0f, HalfHeight));
	TestFalse(TEXT("surface marker rejects a roof beyond the 500 cm layer tolerance"),
		UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(HalfHeight + 10.0f, 511.0f, HalfHeight));
	TestTrue(TEXT("underground marker accepts its authored floor"),
		UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(-6400.0f + HalfHeight, -6400.0f, HalfHeight));
	TestFalse(TEXT("underground marker rejects the surface floor"),
		UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(-6400.0f + HalfHeight, 10.0f, HalfHeight));

	// Three authored surface markers can supply a full 25-BOT team without
	// drifting onto higher NavMesh layers or invoking an unverified fallback.
	{
		FScopedWorld Scope(FName(TEXT("Lvl_NewYork_Taigan")));
		AddSolidBox(Scope.World, FVector(50000, 0, -90), FVector(100000, 10000, 100));
		const TArray<FVector> Markers = {
			FVector(0, 0, HalfHeight + 10.0f),
			FVector(50000, 0, HalfHeight + 10.0f),
			FVector(100000, 0, HalfHeight + 10.0f)};
		for (const FVector& Marker : Markers)
			AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer, Marker);
		TStrongObjectPtr<UMSMobileSuitData> Suit(MakeBotSuitData());
		UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
		Manager->SetTeamBotCounts(25, 0);
		Manager->SetBotMobileSuitDataOverride(Suit.Get());
		Scope.GameMode->StartPlay();
		TestEqual(TEXT("three valid marker neighbourhoods fill 25 team seats"),
			Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 25);
		for (const TWeakObjectPtr<AMSBotCharacter>& Entry : Manager->GetSpawnedBots())
		{
			const AMSBotCharacter* Bot = Entry.Get();
			if (!TestNotNull(TEXT("placed Taigan BOT exists"), Bot)) continue;
			const UMSRespawnComponent* Respawn = Bot->GetRespawnComponent();
			if (!TestNotNull(TEXT("placed Taigan BOT remembers its floor"), Respawn)) continue;
			const FVector Ground = Respawn->GetDefaultSpawnPoint().GetLocation();
			float NearestMarker = TNumericLimits<float>::Max();
			for (const FVector& Marker : Markers)
				NearestMarker = FMath::Min(NearestMarker, FVector::Dist2D(Ground, Marker));
			TestTrue(TEXT("Taigan BOT remains close to an authored marker"), NearestMarker <= 3000.0f + 1.0f);
			TestTrue(TEXT("Taigan BOT keeps marker's surface floor layer"),
				UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(HalfHeight + 10.0f,
					Ground.Z, HalfHeight));
		}
	}

	// C/Z are separate authored underground centres under the same covered
	// subway layer. Their saved redeploy targets must remain under the roof.
	{
		FScopedWorld Scope(FName(TEXT("Lvl_NewYork_Taigan")));
		AddSolidBox(Scope.World, FVector(0, 0, -6500), FVector(100000, 10000, 100));
		AddSolidBox(Scope.World, FVector(0, 0, -3250), FVector(100000, 10000, 150));
		AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer,
			FVector(-50000, 0, -6400.0f + HalfHeight));
		AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamEnemy,
			FVector(50000, 0, -6400.0f + HalfHeight));
		TStrongObjectPtr<UMSMobileSuitData> Suit(MakeBotSuitData());
		UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
		Manager->SetTeamBotCounts(1, 1);
		Manager->SetBotMobileSuitDataOverride(Suit.Get());
		Scope.GameMode->StartPlay();
		TestEqual(TEXT("C marker places one BOT on its underground layer"),
			Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 1);
		TestEqual(TEXT("Z marker places one BOT on its underground layer"),
			Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 1);
		for (const TWeakObjectPtr<AMSBotCharacter>& Entry : Manager->GetSpawnedBots())
		{
			const AMSBotCharacter* Bot = Entry.Get();
			if (!TestNotNull(TEXT("underground pair BOT exists"), Bot)) continue;
			const UMSRespawnComponent* Respawn = Bot->GetRespawnComponent();
			if (!TestNotNull(TEXT("underground pair BOT has respawn target"), Respawn)) continue;
			const FVector Target = Respawn->GetDefaultSpawnPoint().GetLocation();
			TestTrue(TEXT("C/Z default spawn keeps a standing centre below the roof"),
				Target.Z < -4500.0f && UMSBotSpawnManagerComponent::IsTaiganMarkerFloorLayer(
					-6400.0f + HalfHeight, Target.Z - HalfHeight - 50.0f, HalfHeight));
		}
	}

	// A roof spanning every candidate is not a substitute for the marker floor:
	// report a placement shortfall instead of spawning on the wrong layer.
	{
		FScopedWorld Scope(FName(TEXT("Lvl_NewYork_Taigan")));
		AddSolidBox(Scope.World, FVector(0, 0, -90), FVector(10000, 10000, 100));
		AddSolidBox(Scope.World, FVector(0, 0, 1900), FVector(10000, 10000, 100));
		AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer,
			FVector(0, 0, HalfHeight + 10.0f));
		TStrongObjectPtr<UMSMobileSuitData> Suit(MakeBotSuitData());
		UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
		Manager->SetTeamBotCounts(1, 0);
		Manager->SetBotMobileSuitDataOverride(Suit.Get());
		Scope.GameMode->StartPlay();
		TestEqual(TEXT("no surface-layer candidate means no forced rooftop deployment"),
			Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 0);
	}
	return true;
}

bool FMSTaiganHumanDropColumnTest::RunTest(const FString&)
{
	using namespace MSBotSpawnTest;
	FScopedOfflineAuth OfflineAuth;
	const float HumanRadius = GetDefault<AMSPlayerCharacter>()->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float BotRadius = GetDefault<AMSBotCharacter>()->GetCapsuleComponent()->GetScaledCapsuleRadius();
	FMSProtectedHumanDropColumn Drop;
	Drop.GroundLocation = FVector(0.0f, 0.0f, -11400.0f);
	Drop.CapsuleRadius = HumanRadius;
	const float GuardDistance = HumanRadius + BotRadius + 200.0f;
	TestTrue(TEXT("same-floor overlapping landing capsules reserve the human drop column"),
		UMSBotSpawnManagerComponent::ConflictsWithHumanDropColumn(
			FVector(GuardDistance - 1.0f, 0.0f, -11400.0f), BotRadius, Drop));
	TestFalse(TEXT("a separate horizontal landing keeps Taigan BOT scatter available"),
		UMSBotSpawnManagerComponent::ConflictsWithHumanDropColumn(
			FVector(GuardDistance + 1.0f, 0.0f, -11400.0f), BotRadius, Drop));
	TestFalse(TEXT("the same XY on another authored floor is not blocked"),
		UMSBotSpawnManagerComponent::ConflictsWithHumanDropColumn(
			FVector(0.0f, 0.0f, 0.0f), BotRadius, Drop));

	for (const bool bProvideAlternateMarker : {false, true})
	{
		FScopedWorld Scope(FName(TEXT("Lvl_NewYork_Taigan")));
		AddSolidBox(Scope.World, FVector(0, 0, -11500), FVector(30000, 30000, 100));
		AddSolidBox(Scope.World, FVector(0, 0, -5250), FVector(30000, 30000, 150));
		const float BotHalfHeight = GetDefault<AMSBotCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer,
			FVector(0, 0, -11400.0f + BotHalfHeight));
		if (bProvideAlternateMarker)
			AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer,
				FVector(5000.0f, 0, -11400.0f + BotHalfHeight));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Human = Scope.World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector(0, 0, -6270), FRotator::ZeroRotator, Params);
		APlayerController* Controller = Scope.World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("human drop fixture has a pawn"), Human)
			|| !TestNotNull(TEXT("human drop fixture has a controller"), Controller)) return false;
		Controller->Possess(Human);
		Human->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		TestTrue(TEXT("possessed human is descending when BOT candidates are resolved"),
			Controller->GetPawn() == Human && Human->GetCharacterMovement()->IsFalling());
		TStrongObjectPtr<UMSMobileSuitData> Suit(MakeBotSuitData());
		UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
		Manager->SetTeamBotCounts(1, 0);
		Manager->SetBotMobileSuitDataOverride(Suit.Get());
		// Every candidate around the first marker intersects the protected column.
		// With an alternate marker, existing fallback must still supply the seat.
		Manager->SetSpawnSearchRadiusForTests(100.0f);
		Scope.GameMode->StartPlay();
		TestEqual(bProvideAlternateMarker
			? TEXT("alternate marker fills the seat outside the human drop column")
			: TEXT("only overlapping marker cannot place a BOT into the human drop column"),
			Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), bProvideAlternateMarker ? 1 : 0);
		if (bProvideAlternateMarker && Manager->GetSpawnedBots().Num() == 1)
		{
			const AMSBotCharacter* Bot = Manager->GetSpawnedBots()[0].Get();
			if (!TestNotNull(TEXT("alternate marker BOT exists"), Bot)) return false;
			TestTrue(TEXT("alternate BOT landing remains outside reserved capsule clearance"),
				FVector::Dist2D(Bot->GetActorLocation(), Human->GetActorLocation()) > GuardDistance);
		}
	}
	return true;
}

bool FMSBotSpawnTierDistributionTest::RunTest(const FString&)
{
	// Weak60%/Medium30%/Strong10%。丸めは Strong→Medium→Weak の順に最低1体保証。
	int32 Weak = 0, Medium = 0, Strong = 0;

	UMSBotSpawnManagerComponent::ComputeTierCounts(12, Weak, Medium, Strong);
	TestEqual(TEXT("12体構成: Strong = 1 (round(1.2))"), Strong, 1);
	TestEqual(TEXT("12体構成: Medium = 4 (round(3.6))"), Medium, 4);
	TestEqual(TEXT("12体構成: Weak = 7 (残数)"), Weak, 7);

	UMSBotSpawnManagerComponent::ComputeTierCounts(0, Weak, Medium, Strong);
	TestEqual(TEXT("0体構成: Strongも0"), Strong, 0);
	TestEqual(TEXT("0体構成: Mediumも0"), Medium, 0);
	TestEqual(TEXT("0体構成: Weakも0"), Weak, 0);

	UMSBotSpawnManagerComponent::ComputeTierCounts(1, Weak, Medium, Strong);
	TestEqual(TEXT("1体構成: 最低保証はStrongが先"), Strong, 1);
	TestEqual(TEXT("1体構成: Mediumは0"), Medium, 0);
	TestEqual(TEXT("1体構成: Weakは0"), Weak, 0);

	UMSBotSpawnManagerComponent::ComputeTierCounts(2, Weak, Medium, Strong);
	TestEqual(TEXT("2体構成: Strong=1"), Strong, 1);
	TestEqual(TEXT("2体構成: Medium=1 (Strongの次に保証)"), Medium, 1);
	TestEqual(TEXT("2体構成: Weak=0"), Weak, 0);

	UMSBotSpawnManagerComponent::ComputeTierCounts(3, Weak, Medium, Strong);
	TestEqual(TEXT("3体構成: Strong=1"), Strong, 1);
	TestEqual(TEXT("3体構成: Medium=1"), Medium, 1);
	TestEqual(TEXT("3体構成: Weak=1"), Weak, 1);

	const TArray<EMSEnemyTier> Assignments = UMSBotSpawnManagerComponent::BuildTierAssignments(12);
	TestEqual(TEXT("BuildTierAssignmentsの総数は要求数と一致"), Assignments.Num(), 12);
	int32 AssignedStrong = 0;
	for (const EMSEnemyTier Tier : Assignments)
	{
		if (Tier == EMSEnemyTier::Strong) ++AssignedStrong;
	}
	TestEqual(TEXT("BuildTierAssignmentsのStrong数はComputeTierCountsと一致"), AssignedStrong, 1);
	return true;
}

bool FMSBotTierWeaponVariationTest::RunTest(const FString&)
{
	const TArray<FSoftObjectPath> Weak = AMSBotCharacter::GetTierWeaponAssetPaths(EMSEnemyTier::Weak);
	const TArray<FSoftObjectPath> Medium = AMSBotCharacter::GetTierWeaponAssetPaths(EMSEnemyTier::Medium);
	const TArray<FSoftObjectPath> Strong = AMSBotCharacter::GetTierWeaponAssetPaths(EMSEnemyTier::Strong);
	TestEqual(TEXT("Weak has gun only"), Weak.Num(), 1);
	TestTrue(TEXT("Weak weapon is a machine gun asset"), Weak[0].ToString().Contains(TEXT("MachineGun")));
	TestEqual(TEXT("blue Medium has melee only"), Medium.Num(), 1);
	TestTrue(TEXT("Medium weapon is the heat sword"), Medium[0].ToString().Contains(TEXT("HeatSword")));
	TestEqual(TEXT("Strong has gun and bazooka"), Strong.Num(), 2);
	TestTrue(TEXT("Strong includes a beam rifle"), Strong[0].ToString().Contains(TEXT("DA_Weapon_BR")));
	TestTrue(TEXT("Strong includes a bazooka"), Strong[1].ToString().Contains(TEXT("Bazooka")));

	const TArray<FGameplayTag> Categories = {
		MSTags::Weapon_Category_BeamRifle, MSTags::Weapon_Category_Bazooka};
	TestEqual(TEXT("Strong selects gun below preferred distance"),
		AMSBotAIController::ChooseStrongTierWeaponSlot(1000.0f, 1500.0f, Categories, 1), 0);
	TestEqual(TEXT("Strong selects bazooka at preferred distance"),
		AMSBotAIController::ChooseStrongTierWeaponSlot(1500.0f, 1500.0f, Categories, 0), 1);
	return true;
}

bool FMSBotSpawnInitialDeploymentTest::RunTest(const FString&)
{
	MSBotSpawnTest::FScopedWorld Scope;
	if (!TestNotNull(TEXT("GameMode spawned"), Scope.GameMode))
	{
		return false;
	}

	// 18mカプセルの高密度配置可否ではなく24体初期化を検証するため、1体ごとに十分離れたマーカーを用意。
	TArray<FVector> AllyMarkers;
	TArray<FVector> EnemyMarkers;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		AllyMarkers.Add(FVector(-20000.0f, -22000.0f + Index * 4000.0f, 0.0f));
		EnemyMarkers.Add(FVector(20000.0f, -22000.0f + Index * 4000.0f, 0.0f));
		MSBotSpawnTest::AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer, AllyMarkers.Last());
		MSBotSpawnTest::AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamEnemy, EnemyMarkers.Last());
	}

	UMSMobileSuitData* Suit = MSBotSpawnTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);

	UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
	if (!TestNotNull(TEXT("AMSGameMode carries a UMSBotSpawnManagerComponent"), Manager))
	{
		return false;
	}
	TestTrue(TEXT("bSpawnBots defaults to enabled"), Manager->IsSpawningEnabled());
	TestEqual(TEXT("default ally deployment count is 12"), Manager->GetAllyBotCount(), 12);
	TestEqual(TEXT("default enemy deployment count is 12"), Manager->GetEnemyBotCount(), 12);
	Manager->SetBotMobileSuitDataOverride(Suit);

	TestEqual(TEXT("no bots exist before StartPlay"),
		MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::PlayerTeamId())
		+ MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::EnemyTeamId()), 0);

	Scope.GameMode->StartPlay();

	// 1. 既定数(敵12/味方12)がスポーンされる
	int32 TotalBots = 0;
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		++TotalBots;
	}
	TestEqual(TEXT("StartPlay deploys the default 24 bots"), TotalBots, 24);

	// 2. チームIDが正しく設定される
	TestEqual(TEXT("12 bots carry the player (ally) team id"),
		MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::PlayerTeamId()), 12);
	TestEqual(TEXT("12 bots carry the enemy team id"),
		MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::EnemyTeamId()), 12);

	// 3. Tier配分(12体: Weak7/Medium4/Strong1)が両チームで成立
	int32 Weak = 0, Medium = 0, Strong = 0;
	MSBotSpawnTest::CountTiersOnTeam(Scope.World, MSTeam::EnemyTeamId(), Weak, Medium, Strong);
	TestEqual(TEXT("enemy tier split: Weak 7"), Weak, 7);
	TestEqual(TEXT("enemy tier split: Medium 4"), Medium, 4);
	TestEqual(TEXT("enemy tier split: Strong 1"), Strong, 1);
	MSBotSpawnTest::CountTiersOnTeam(Scope.World, MSTeam::PlayerTeamId(), Weak, Medium, Strong);
	TestEqual(TEXT("ally tier split: Weak 7"), Weak, 7);
	TestEqual(TEXT("ally tier split: Medium 4"), Medium, 4);
	TestEqual(TEXT("ally tier split: Strong 1"), Strong, 1);

	// 4-5. 各BOT: 確定位置で静的ジオメトリと非オーバーラップ、InitializeUnit経由(Armor>0+武器適用)
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSBotSpawnB2Test), /*bTraceComplex=*/false);
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It); // 検査対象は静的ジオメトリ(BOT同士は対象外)
	}
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		AMSBotCharacter* Bot = *It;
		const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
		const bool bBlocked = Scope.World->OverlapBlockingTestByChannel(
			Bot->GetActorLocation(), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
			QueryParams);
		TestFalse(TEXT("spawned bot capsule does not blocking-overlap static geometry"), bBlocked);

		TestEqual(TEXT("bot was initialized through InitializeUnit with the manager suit data"),
			Bot->GetMobileSuitData(), Suit);
		TestTrue(TEXT("bot armor is above zero after InitializeUnit"),
			Bot->GetVitalSet()->GetArmor() > 0.0f);
		TestEqual(TEXT("bot armor equals the suit data MaxArmor"), Bot->GetVitalSet()->GetArmor(), 3000.0f);
		TestTrue(TEXT("bot weapon slots were built from the suit data"),
			Bot->GetWeaponComponent()->GetSlotCount() > 0);
		TestNotNull(TEXT("bot slot 0 carries applied weapon data"),
			Bot->GetWeaponComponent()->GetWeaponData(0));

		// マーカー半径内(探索半径1500cm)に配置されている
		const FVector Location2D(Bot->GetActorLocation().X, Bot->GetActorLocation().Y, 0.0f);
		const bool bAlly = Bot->GetGenericTeamId() == MSTeam::PlayerTeamId();
		float DistToNearestMarker = TNumericLimits<float>::Max();
		for (const FVector& Marker : bAlly ? AllyMarkers : EnemyMarkers)
		{
			DistToNearestMarker = FMath::Min(DistToNearestMarker, FVector::Dist(Location2D, Marker));
		}
		// +200cm: AdjustIfPossibleButAlwaysSpawn のカプセル押し出し分の許容
		TestTrue(TEXT("bot landed inside a team marker search radius"),
			DistToNearestMarker <= Manager->GetSpawnSearchRadius() + 200.0f);
	}

	// マネージャは弱参照リストで全スポーンBOTを保持する(B3人数維持の土台)
	TestEqual(TEXT("manager tracks every spawned bot"), Manager->GetSpawnedBots().Num(), 24);
	TestEqual(TEXT("manager per-team ally count"), Manager->CountSpawnedBots(MSTeam::PlayerTeamId()), 12);
	TestEqual(TEXT("manager per-team enemy count"), Manager->CountSpawnedBots(MSTeam::EnemyTeamId()), 12);

	// StartPlayの多重呼び出しでも増殖しない
	Scope.GameMode->StartPlay();
	TotalBots = 0;
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		++TotalBots;
	}
	TestEqual(TEXT("a second StartPlay does not duplicate the deployment"), TotalBots, 24);
	return true;
}

bool FMSBotSpawnDisabledTest::RunTest(const FString&)
{
	MSBotSpawnTest::FScopedWorld Scope;
	MSBotSpawnTest::AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamPlayer, FVector(-5000.0f, 0.0f, 0.0f));
	MSBotSpawnTest::AddSpawnPoint(Scope.World, EMSCaptureOwner::TeamEnemy, FVector(5000.0f, 0.0f, 0.0f));

	UMSMobileSuitData* Suit = MSBotSpawnTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
	Manager->SetBotMobileSuitDataOverride(Suit);
	Manager->SetSpawnBotsEnabled(false);

	Scope.GameMode->StartPlay();

	int32 TotalBots = 0;
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		++TotalBots;
	}
	TestEqual(TEXT("bSpawnBots=false deploys no bot at all"), TotalBots, 0);
	TestEqual(TEXT("manager tracks nothing while disabled"), Manager->GetSpawnedBots().Num(), 0);
	return true;
}

bool FMSBotSpawnHomeBaseFallbackTest::RunTest(const FString&)
{
	// AMSBotSpawnPoint がひとつも無いレベルでは AMSHomeBase 位置がフォールバックになる。
	MSBotSpawnTest::FScopedWorld Scope;
	const TArray<FVector> HomeBaseLocations = {
		FVector(4000.0f, -5000.0f, 0.0f), FVector(4000.0f, 5000.0f, 0.0f)};
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FVector& HomeBaseLocation : HomeBaseLocations)
	{
		Scope.World->SpawnActor<AMSHomeBase>(
			AMSHomeBase::StaticClass(), HomeBaseLocation, FRotator::ZeroRotator, Params);
	}

	UMSMobileSuitData* Suit = MSBotSpawnTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	UMSBotSpawnManagerComponent* Manager = Scope.GameMode->GetBotSpawnManager();
	Manager->SetBotMobileSuitDataOverride(Suit);
	// This case verifies fallback selection; high-density deployment remains covered by InitialDeployment.
	Manager->SetTeamBotCounts(1, 1);

	Scope.GameMode->StartPlay();

	TestEqual(TEXT("ally bots spawn from the home-base fallback"),
		MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::PlayerTeamId()), 1);
	TestEqual(TEXT("enemy bots spawn from the home-base fallback"),
		MSBotSpawnTest::CountBotsOnTeam(Scope.World, MSTeam::EnemyTeamId()), 1);
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		const FVector Location2D(It->GetActorLocation().X, It->GetActorLocation().Y, 0.0f);
		float DistanceToNearestBase = TNumericLimits<float>::Max();
		for (const FVector& HomeBaseLocation : HomeBaseLocations)
		{
			DistanceToNearestBase = FMath::Min(DistanceToNearestBase,
				FVector::Dist(Location2D, FVector(HomeBaseLocation.X, HomeBaseLocation.Y, 0.0f)));
		}
		TestTrue(TEXT("fallback bot landed inside the home-base search radius"),
			DistanceToNearestBase <= Manager->GetSpawnSearchRadius() + 200.0f);
	}
	return true;
}

bool FMSBotSpawnNoLocationTest::RunTest(const FString&)
{
	// マーカーもAMSHomeBaseもPlayerStartも無い: 0体スポーン+Warningでクラッシュしない。
	AddExpectedMessage(TEXT("BotSpawnManager"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2);

	MSBotSpawnTest::FScopedWorld Scope;
	UMSMobileSuitData* Suit = MSBotSpawnTest::MakeBotSuitData();
	TStrongObjectPtr<UMSMobileSuitData> SuitGuard(Suit);
	Scope.GameMode->GetBotSpawnManager()->SetBotMobileSuitDataOverride(Suit);

	Scope.GameMode->StartPlay();

	int32 TotalBots = 0;
	for (TActorIterator<AMSBotCharacter> It(Scope.World); It; ++It)
	{
		++TotalBots;
	}
	TestEqual(TEXT("no spawnable location leaves the world empty without crashing"), TotalBots, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
