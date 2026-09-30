// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-7 検証: リスポーンタイマー・爆発遅延・上空8m降下・Deploy APIの目標確定一貫性。
//  - respawnDelay=10s: 死亡から10s(±Tick境界)で自動リスポーン(State.Dead除去+Armor全快)
//  - explosionDelay=5s: 死亡から5sで爆発通知(OnExplosionDue)発火の土台
//  - 上空降下方式: リスポーン位置 = 目標XY一致+目標Z上空8m(800cm)、MOVE_Fallingで降下開始
//  - B-11 目標1回確定: 死亡待機中にSetDefaultSpawnPointを変えても確定済み目標へリスポーン
//  - Deploy(preset, spawnOverride): 機体切替(MaxArmor/データ差し替え)+指定地点上空へテレポート、
//    進行中の死亡フロー(自動リスポーン・爆発タイマー)をキャンセル
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Player.Respawn"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Player.Respawn"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AI/MSBotSpawnPoint.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSHomeBase.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Misc/CommandLine.h"
#include "NeoMSGO.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "UObject/Package.h"
#include "UI/MSDeckSubsystem.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRespawnTest,
	"NeoMSGO.Player.Respawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaiganUndergroundRespawnTest,
	"NeoMSGO.Player.Respawn.TaiganUnderground",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaiganSurfaceRespawnTest,
	"NeoMSGO.Player.Respawn.TaiganSurfaceLayer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTaklamakanCoveredRespawnTest,
	"NeoMSGO.Player.Respawn.TaklamakanCoveredBases",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHostedRoomFirstSortieTest,
	"NeoMSGO.Player.Respawn.HostedRoomFirstSortie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Respawn/deck fixtures exercise the local sortie path without online admission. */
	struct FScopedRespawnOfflineAuth
	{
		FString Previous = FCommandLine::Get();

		FScopedRespawnOfflineAuth()
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

		~FScopedRespawnOfflineAuth()
		{
			FCommandLine::Set(*Previous);
		}
	};

	/** タイマー駆動・重力降下にワールドが必要なため、テスト専用の使い捨てGameワールドを作る(P2-6テストと同一パターン) */
	struct FRespawnTestWorld
	{
		FScopedRespawnOfflineAuth OfflineAuth;
		UWorld* World = nullptr;

		explicit FRespawnTestWorld(const FName WorldName = FName(TEXT("MSRespawnTestWorld")))
		{
			// GetMapName uses the package basename. A named package is necessary
			// to test the same map-specific branch as /Game/Maps/Lvl_NewYork_Taigan.
			static int32 PackageSerial = 0;
			const FString PackageName = FString::Printf(TEXT("/Temp/NeoMSGOTest/Respawn_%d/%s"),
				++PackageSerial, *WorldName.ToString());
			UPackage* WorldPackage = CreatePackage(*PackageName);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, WorldPackage);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FRespawnTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FHostedRoomRespawnWorld
	{
		FScopedRespawnOfflineAuth OfflineAuth;
		UWorld* World = nullptr;
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		AMSGameMode* Mode = nullptr;

		FHostedRoomRespawnWorld()
		{
			static int32 Serial = 0;
			UPackage* Package = CreatePackage(*FString::Printf(
				TEXT("/Temp/NeoMSGOTest/HostedRoomRespawn_%d/Lvl_NewYork_Taigan"), ++Serial));
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("Lvl_NewYork_Taigan"), Package);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			FURL URL;
			URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
			URL.AddOption(TEXT("RoomHost=1"));
			URL.AddOption(TEXT("MapId=NewYorkTaigan"));
			World->SetGameMode(URL);
			Mode = World->GetAuthGameMode<AMSGameMode>();
			if (Mode) Mode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			if (Mode) Mode->StartPlay();
		}

		~FHostedRoomRespawnWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** タイマー満了・落下を進める。
	 *  注意: 無名namespaceでもUnityビルドでは他テストcppと同一TUに結合され得るため、
	 *  TickWorld等の汎用名はファイル間で再定義衝突する。ファイル固有の名前を付けること。 */
	void TickRespawnWorld(UWorld* World, float Seconds)
	{
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Seconds, Step));
			Seconds -= Step;
			GFrameCounter++;
		}
	}

	/** 機体プリセット(GC防止のためAddToRootし、デストラクタで解除) */
	struct FScopedPreset
	{
		UMSMobileSuitData* Data = nullptr;

		explicit FScopedPreset(const int32 MaxArmor)
		{
			// 名前は自動生成(テスト再実行時のTransientパッケージ内での名前衝突を回避)
			Data = NewObject<UMSMobileSuitData>(GetTransientPackage());
			Data->MaxArmor = MaxArmor;
			Data->AddToRoot();
		}

		~FScopedPreset()
		{
			if (Data)
			{
				Data->RemoveFromRoot();
			}
		}
	};

	/** 敵陣営の攻撃者(Player陣営の被弾側へCanDamage可) */
	AMSTeamAgentActor* SpawnRespawnTestAttacker(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSTeamAgentActor* Attacker = World->SpawnActor<AMSTeamAgentActor>(
			AMSTeamAgentActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Attacker->SetGenericTeamId(MSTeam::EnemyTeamId());
		return Attacker;
	}

	AActor* SpawnRespawnTestSolidBox(UWorld* World, const FVector& Centre, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Centre);
		return Actor;
	}

	/** 実戦経路(GE_Damage+SetByCaller)で撃破する */
	void Kill(AMSTeamAgentActor* Attacker, AMSPlayerCharacter* Player)
	{
		FMSGameplayEffectContext* Context = new FMSGameplayEffectContext();
		Context->AddInstigator(Attacker, Attacker);
		const FGameplayEffectContextHandle ContextHandle(Context);

		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(), ContextHandle, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, 10.0f * Player->GetVitalSet()->GetMaxArmor());
		Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(Spec, Player->GetAbilitySystemComponent());
	}
}

bool FMSTaklamakanCoveredRespawnTest::RunTest(const FString&)
{
	FRespawnTestWorld Scope(TEXT("Lvl_Taklamakan_Ruins_Prototype"));
	UWorld* World = Scope.World;
	TestTrue(TEXT("fixture uses the opt-in Taklamakan prototype map name"),
		World->GetMapName().EndsWith(TEXT("Lvl_Taklamakan_Ruins_Prototype")));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Taklamakan player exists"), Player)) return false;
	FScopedPreset Preset(5000);
	Player->InitializeUnit(Preset.Data);
	// This disposable world has no controller; keep gravity/collision physics
	// active so the landing assertion exercises the actual fall onto the floor.
	Player->GetCharacterMovement()->bRunPhysicsWithNoController = true;
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("Taklamakan player has respawn component"), Respawn)) return false;
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	struct FAuthoredBase
	{
		const TCHAR* Id;
		FVector Location;
		EMSCaptureOwner Team;
	};
	const FAuthoredBase Authored[] = {
		{TEXT("A"), FVector(-28051.95f, 30974.03f, 4100.0f), EMSCaptureOwner::TeamPlayer},
		{TEXT("B"), FVector(25129.87f, 26298.70f, 3100.0f), EMSCaptureOwner::TeamPlayer},
		{TEXT("X"), FVector(-42662.34f, -37402.60f, 4100.0f), EMSCaptureOwner::TeamEnemy},
		{TEXT("Y"), FVector(15194.81f, -31558.44f, 3100.0f), EMSCaptureOwner::TeamEnemy}
	};
	AMSHomeBase* FirstBase = nullptr;
	AMSBotSpawnPoint* FirstMarker = nullptr;
	AActor* FirstFloor = nullptr;
	AActor* FirstRoof = nullptr;
	for (const FAuthoredBase& Row : Authored)
	{
		const FVector MarkerLocation = Row.Location + FVector(2922.08f, 0.0f, 1000.0f);
		AActor* Floor = SpawnRespawnTestSolidBox(World,
			FVector(MarkerLocation.X, MarkerLocation.Y, Row.Location.Z - 100.0f), FVector(1600, 1600, 100));
		AActor* Roof = SpawnRespawnTestSolidBox(World,
			FVector(MarkerLocation.X, MarkerLocation.Y, Row.Location.Z + 3400.0f), FVector(1600, 1600, 300));
		Floor->Tags.AddUnique(FName(TEXT("MapFidelity0929Taklamakan")));
		Roof->Tags.AddUnique(FName(TEXT("MapFidelity0929Taklamakan")));
		AMSHomeBase* Base = World->SpawnActor<AMSHomeBase>(
			AMSHomeBase::StaticClass(), Row.Location, FRotator::ZeroRotator, Params);
		AMSBotSpawnPoint* Marker = World->SpawnActor<AMSBotSpawnPoint>(
			AMSBotSpawnPoint::StaticClass(), MarkerLocation, FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("authored base exists"), Base) ||
			!TestNotNull(TEXT("authored spawn marker exists"), Marker)) return false;
		// The real level sets UseAuthoredExterior. Suppress the fixture's default
		// fortress collision so only the tagged authored floor/roof boxes define
		// covered space, including the missing-roof failure case.
		Base->SetActorEnableCollision(false);
		Base->Tags.AddUnique(FName(TEXT("MapFidelity0929Taklamakan")));
		Marker->Tags.AddUnique(FName(TEXT("MapFidelity0929Taklamakan")));
		Base->SetPointName(FText::FromString(Row.Id));
		Base->SetGenericTeamId(Row.Team == EMSCaptureOwner::TeamPlayer
			? MSTeam::PlayerTeamId() : MSTeam::EnemyTeamId());
		Marker->SetSpawnTeam(Row.Team);
		Base->SetDeploymentSpawnAnchor(Marker);
		Respawn->Deploy(Preset.Data, Base->GetActorTransform());
		const FVector Actual = Player->GetActorLocation();
		TestTrue(FString::Printf(TEXT("%s uses its authored marker XY"), Row.Id),
			FVector::Dist2D(Actual, MarkerLocation) < 1.0f);
		TestTrue(FString::Printf(TEXT("%s capsule starts above the authored floor"), Row.Id),
			Actual.Z - HalfHeight > Row.Location.Z);
		TestTrue(FString::Printf(TEXT("%s capsule stays under the authored roof"), Row.Id),
			Actual.Z + HalfHeight < Row.Location.Z + 3100.0f);
		if (!FirstBase) { FirstBase = Base; FirstMarker = Marker; FirstFloor = Floor; FirstRoof = Roof; }
	}
	Respawn->Deploy(Preset.Data, FirstMarker->GetActorTransform());
	const FVector DirectDrop = Player->GetActorLocation();
	TestTrue(TEXT("direct authored marker selection also stays under its base roof"),
		FVector::Dist2D(DirectDrop, FirstMarker->GetActorLocation()) < 1.0f &&
		DirectDrop.Z - HalfHeight > FirstBase->GetActorLocation().Z &&
		DirectDrop.Z + HalfHeight < FirstBase->GetActorLocation().Z + 3100.0f);
	TickRespawnWorld(World, 4.0f);
	const FVector SafeLocation = Player->GetActorLocation();
	TestTrue(TEXT("covered drop lands on its reviewed base floor"),
		Player->GetCharacterMovement()->IsMovingOnGround() &&
		FMath::Abs(SafeLocation.Z - HalfHeight - FirstBase->GetActorLocation().Z) < 100.0f);
	FirstBase->SetDeploymentSpawnAnchor(nullptr);
	AddExpectedMessage(TEXT("Taklamakan base has no valid explicit deployment anchor"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("missing anchor rejects sortie without teleport"), Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	FirstBase->SetDeploymentSpawnAnchor(FirstMarker);
	FirstMarker->Tags.Remove(FName(TEXT("MapFidelity0929Taklamakan")));
	AddExpectedMessage(TEXT("Taklamakan base has no valid explicit deployment anchor"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("foreign anchor rejects sortie without teleport"), Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	FirstMarker->Tags.AddUnique(FName(TEXT("MapFidelity0929Taklamakan")));
	const FVector ReviewedMarkerLocation = FirstMarker->GetActorLocation();
	FirstMarker->SetActorLocation(ReviewedMarkerLocation + FVector(0.0f, 5000.0f, 0.0f));
	AddExpectedMessage(TEXT("Taklamakan covered deploy rejected; authored floor missing"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("anchor outside the authored floor rejects without teleport"),
		Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	FirstMarker->SetActorLocation(ReviewedMarkerLocation);
	FirstRoof->Destroy();
	AddExpectedMessage(TEXT("Taklamakan covered deploy rejected; no safe capsule space"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("missing roof rejects covered drop without teleport"), Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	SpawnRespawnTestSolidBox(World,
		FVector(FirstMarker->GetActorLocation().X, FirstMarker->GetActorLocation().Y,
			FirstBase->GetActorLocation().Z + 1800.0f), FVector(1600, 1600, 200));
	AddExpectedMessage(TEXT("Taklamakan covered deploy rejected; no safe capsule space"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("low roof rejects capsule without teleport"), Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	FirstFloor->Destroy();
	AddExpectedMessage(TEXT("Taklamakan covered deploy rejected; authored floor missing"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FirstBase->GetActorTransform());
	TestTrue(TEXT("missing authored floor rejects without Nav fallback"), Player->GetActorLocation().Equals(SafeLocation, 1.0f));
	return true;
}

bool FMSTaiganUndergroundRespawnTest::RunTest(const FString&)
{
	FRespawnTestWorld Scope(TEXT("Lvl_NewYork_Taigan"));
	UWorld* World = Scope.World;
	TestTrue(TEXT("fixture uses the opt-in Taigan map name"),
		World->GetMapName().EndsWith(TEXT("Lvl_NewYork_Taigan")));
	const FVector RearMarker(20000.0f, 0.0f, -5569.0f);
	SpawnRespawnTestSolidBox(World, FVector(RearMarker.X, RearMarker.Y, -6500.0f),
		FVector(6000.0f, 6000.0f, 100.0f)); // top -6400
	SpawnRespawnTestSolidBox(World, FVector(RearMarker.X, RearMarker.Y, -3250.0f),
		FVector(6000.0f, 6000.0f, 150.0f)); // underside -3400

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSHomeBase* Base = World->SpawnActor<AMSHomeBase>(
		AMSHomeBase::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AMSBotSpawnPoint* Marker = World->SpawnActor<AMSBotSpawnPoint>(
		AMSBotSpawnPoint::StaticClass(), RearMarker, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Taigan home base exists"), Base) ||
		!TestNotNull(TEXT("Taigan rear marker exists"), Marker)) return false;
	Base->Tags.AddUnique(FName(TEXT("NewYorkTaiganGenerated")));
	Base->SetPointName(FText::FromString(TEXT("C")));
	Marker->Tags.AddUnique(FName(TEXT("NewYorkTaiganGenerated")));
	Marker->SetSpawnTeam(EMSCaptureOwner::TeamPlayer);
	Base->SetDeploymentSpawnAnchor(Marker);

	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Taigan player exists"), Player)) return false;
	FScopedPreset Preset(5000);
	Player->InitializeUnit(Preset.Data);
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("Taigan player has respawn component"), Respawn)) return false;
	Respawn->Deploy(Preset.Data, Base->GetActorTransform());
	const FVector Actual = Player->GetActorLocation();
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestEqual(TEXT("C base selection deploys at explicitly linked rear marker X"), Actual.X, RearMarker.X, 1.0);
	TestEqual(TEXT("C base selection deploys at explicitly linked rear marker Y"), Actual.Y, RearMarker.Y, 1.0);
	TestTrue(TEXT("underground capsule clears the floor"), Actual.Z - HalfHeight > -6400.0f);
	TestTrue(TEXT("underground capsule remains below the roof"), Actual.Z + HalfHeight < -3400.0f);
	TestTrue(TEXT("underground drop remains above standing centre"), Actual.Z > -6400.0f + HalfHeight);
	const FVector CaptureFour(RearMarker.X + 4000.0f, RearMarker.Y, -6400.0f);
	Respawn->Deploy(Preset.Data, FTransform(CaptureFour));
	const FVector CaptureDrop = Player->GetActorLocation();
	TestEqual(TEXT("underground capture point keeps its own X"), CaptureDrop.X, CaptureFour.X, 1.0);
	TestTrue(TEXT("floor-height capture target resolves floor below the roof"),
		CaptureDrop.Z - HalfHeight > -6400.0f && CaptureDrop.Z + HalfHeight < -3400.0f);
	Base->SetDeploymentSpawnAnchor(nullptr);
	AddExpectedMessage(TEXT("Taigan base has no valid explicit deployment anchor"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, Base->GetActorTransform());
	TestTrue(TEXT("C base without explicit marker cannot deploy into the wrong layer"),
		Player->GetActorLocation().Equals(CaptureDrop, 1.0f));
	return true;
}

bool FMSTaiganSurfaceRespawnTest::RunTest(const FString&)
{
	FRespawnTestWorld Scope(TEXT("Lvl_NewYork_Taigan"));
	UWorld* World = Scope.World;
	SpawnRespawnTestSolidBox(World, FVector(0, 0, -100), FVector(15000, 10000, 100));
	// A roof above another valid ground target must not become a second floor
	// through wide Nav projection or through the high drop trajectory.
	SpawnRespawnTestSolidBox(World, FVector(5000, 0, 2200), FVector(1500, 1500, 200));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("surface sortie player exists"), Player)) return false;
	FScopedPreset Preset(5000);
	Player->InitializeUnit(Preset.Data);
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("surface sortie has respawn component"), Respawn)) return false;
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	AMSBotSpawnPoint* Marker = World->SpawnActor<AMSBotSpawnPoint>(
		AMSBotSpawnPoint::StaticClass(), FVector(-5000, 0, HalfHeight), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("surface authored marker exists"), Marker)) return false;
	Marker->Tags.AddUnique(FName(TEXT("NewYorkTaiganGenerated")));
	Respawn->Deploy(Preset.Data, Marker->GetActorTransform());
	const FVector MarkerDrop = Player->GetActorLocation();
	TestEqual(TEXT("authored marker centre keeps its selected XY"), MarkerDrop.X, -5000.0, 1.0);
	TestEqual(TEXT("authored marker centre resolves its floor, not the marker Z"),
		MarkerDrop.Z, static_cast<double>(Respawn->GetDropSpawnHeight()), 1.0);

	Respawn->Deploy(Preset.Data, FTransform(FVector(0, 0, 10)));
	const FVector GroundDrop = Player->GetActorLocation();
	TestEqual(TEXT("saved ground target keeps its XY without broad Nav projection"), GroundDrop.X, 0.0, 1.0);
	TestEqual(TEXT("saved ground target keeps the surface layer"),
		GroundDrop.Z, static_cast<double>(Respawn->GetDropSpawnHeight()), 1.0);

	AddExpectedMessage(TEXT("Taigan surface deploy rejected"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 2);
	Respawn->Deploy(Preset.Data, FTransform(FVector(5000, 0, 10)));
	TestTrue(TEXT("roof above the selected floor rejects the drop without teleport"),
		Player->GetActorLocation().Equals(GroundDrop, 1.0f));
	Respawn->Deploy(Preset.Data, FTransform(FVector(20000, 0, 10)));
	TestTrue(TEXT("missing authored floor rejects deployment without teleport"),
		Player->GetActorLocation().Equals(GroundDrop, 1.0f));
	return true;
}

bool FMSHostedRoomFirstSortieTest::RunTest(const FString&)
{
	FHostedRoomRespawnWorld Scope;
	if (!TestNotNull(TEXT("hosted room mode exists"), Scope.Mode)) return false;
	AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
	if (!TestNotNull(TEXT("hosted room state exists"), State)) return false;
	TestTrue(TEXT("hosted room initially waits for a human sortie"),
		Scope.Mode->IsWaitingForRoomDeployment());
	SpawnRespawnTestSolidBox(Scope.World, FVector(0, 0, -100), FVector(5000, 5000, 100));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	APlayerController* Controller = Scope.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("human pawn exists"), Player) ||
		!TestNotNull(TEXT("human controller exists"), Controller)) return false;
	Controller->Possess(Player);
	FScopedPreset Preset(5000);
	Player->InitializeUnit(Preset.Data);
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("human respawn component exists"), Respawn)) return false;
	const FVector Before = Player->GetActorLocation();
	AddExpectedMessage(TEXT("Taigan surface deploy rejected"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 1);
	Respawn->Deploy(Preset.Data, FTransform(FVector(20000, 0, 10)));
	TestTrue(TEXT("invalid floor does not move the pawn"), Player->GetActorLocation().Equals(Before, 1.0));
	TestTrue(TEXT("failed sortie cannot start the room"), Scope.Mode->IsWaitingForRoomDeployment());
	TestEqual(TEXT("failed sortie keeps the gauge full"), State->PlayerGauge, State->MaxGauge);

	Respawn->Deploy(Preset.Data, FTransform(FVector(0, 0, 10)));
	TestFalse(TEXT("successful human sortie starts the room once"), Scope.Mode->IsWaitingForRoomDeployment());
	const FVector FirstDrop = Player->GetActorLocation();
	TestEqual(TEXT("valid sortie uses its authored floor X"), FirstDrop.X, 0.0, 1.0);
	Scope.Mode->ApplyGaugeDamage(MSTeam::PlayerTeamId(), 400.0f);
	const float GaugeAfterStart = State->PlayerGauge;
	Respawn->Deploy(Preset.Data, FTransform(FVector(0, 0, 10)));
	TestFalse(TEXT("redeployment never reopens the waiting gate"), Scope.Mode->IsWaitingForRoomDeployment());
	TestEqual(TEXT("redeployment does not refill the gauge"), State->PlayerGauge, GaugeAfterStart);
	return true;
}

bool FMSRespawnTest::RunTest(const FString& Parameters)
{
	FRespawnTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSTeamAgentActor* Attacker = SpawnRespawnTestAttacker(World);

	const FScopedPreset PresetA(5000);
	const FScopedPreset PresetB(8000);

	// プレイヤー機体をスポーン(初期配置=既定スポーン地点としてRespawnComponentが記録する)
	const FVector StartLocation(0.0f, 0.0f, 300.0f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), StartLocation, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("AMSPlayerCharacterのスポーン"), Player))
	{
		return false;
	}
	Player->InitializeUnit(PresetA.Data);

	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("RespawnComponentが搭載されている"), Respawn))
	{
		return false;
	}
	// This legacy timer section explicitly exercises automatic respawn; P8-8 player default is manual selection.
	Respawn->SetAutoRespawnEnabled(true);
	Respawn->SetRespawnTimings(5.0f, 10.0f);

	// --- 1. 死亡 → respawnDelay=10s / explosionDelay=5s の独立タイマー ---
	{
		TestEqual(TEXT("初期化: Armor=PresetA.MaxArmor(5000)"), Player->GetVitalSet()->GetArmor(), 5000.0f);

		Kill(Attacker, Player);
		TestEqual(TEXT("撃破: State.Dead付与"), ASC->GetTagCount(MSTags::State_Dead), 1);
		TestTrue(TEXT("撃破: リスポーン待機開始"), Respawn->IsRespawnPending());
		TestTrue(TEXT("撃破: 残り時間はほぼ10s"), Respawn->GetRespawnTimeRemaining() > 9.0f);
		TestFalse(TEXT("撃破直後: まだ再出撃不可(CanDeploy=false)"), Respawn->CanDeploy());

		// B-11: 死亡待機中に既定スポーン地点を動かしても、確定済みの目標はブレない
		Respawn->SetDefaultSpawnPoint(FTransform(FVector(50000.0f, 50000.0f, 300.0f)));

		TickRespawnWorld(World, 4.5f);
		TestFalse(TEXT("爆発: 4.5s時点では未発火"), Respawn->HasExplosionFired());
		TickRespawnWorld(World, 1.0f); // 計5.5s > explosionDelay 5s
		TestTrue(TEXT("爆発: 5s経過で発火(explosionDelay=5s)"), Respawn->HasExplosionFired());
		TestEqual(TEXT("爆発時点: まだ死亡中"), ASC->GetTagCount(MSTags::State_Dead), 1);

		TickRespawnWorld(World, 4.0f); // 計9.5s < respawnDelay 10s
		TestEqual(TEXT("リスポーン: 9.5s時点ではまだ死亡中"), ASC->GetTagCount(MSTags::State_Dead), 1);

		TickRespawnWorld(World, 0.7f); // 計10.2s > 10s
		TestEqual(TEXT("リスポーン: 10s経過でState.Dead除去(自動リスポーン)"), ASC->GetTagCount(MSTags::State_Dead), 0);
		TestEqual(TEXT("リスポーン: Armor全快"), Player->GetVitalSet()->GetArmor(), 5000.0f);
		TestFalse(TEXT("リスポーン: 待機解除"), Respawn->IsRespawnPending());

		// 上空降下方式 + B-11目標1回確定:
		// 位置 = 死亡時に確定した目標(初期配置)のXY、Z = 目標+DropSpawnHeight
		// (タイマー発火後の落下1〜2Tick分を許容)。高さはコンポーネントの実値を参照するので、
		// DropSpawnHeight を変更してもテストが追随する。
		const float ExpectedDrop = Respawn->GetDropSpawnHeight();
		const FVector Location = Player->GetActorLocation();
		TestEqual(TEXT("降下: X=確定目標のX(後から動かした既定地点ではない)"), Location.X, StartLocation.X);
		TestEqual(TEXT("降下: Y=確定目標のY"), Location.Y, StartLocation.Y);
		TestTrue(FString::Printf(TEXT("降下: Zは目標上空DropSpawnHeight付近から降下開始(Z=%.1f, 期待~%.1f)"),
					 Location.Z, StartLocation.Z + ExpectedDrop),
			Location.Z > StartLocation.Z + ExpectedDrop * 0.875f
				&& Location.Z <= StartLocation.Z + ExpectedDrop + 0.5f);
		TestEqual(TEXT("降下: MOVE_Fallingで重力降下中"),
			Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Falling);
	}

	// --- 2. Deploy(preset, spawnOverride): 機体切替+指定地点上空8mへ ---
	{
		const FVector OverrideLocation(2000.0f, -1000.0f, 500.0f);
		Player->GetRespawnComponent()->Deploy(PresetB.Data, FTransform(FRotator(0, 90.0f, 0), OverrideLocation));

		TestEqual(TEXT("Deploy: 機体データがPresetBへ切替"), Player->GetMobileSuitData(),
			static_cast<UMSMobileSuitData*>(PresetB.Data));
		TestEqual(TEXT("Deploy: MaxArmorがPresetB値(8000)"), Player->GetVitalSet()->GetMaxArmor(), 8000.0f);
		TestEqual(TEXT("Deploy: Armor全快(8000)"), Player->GetVitalSet()->GetArmor(), 8000.0f);

		const FVector Location = Player->GetActorLocation();
		TestEqual(TEXT("Deploy: X=spawnOverride.X"), Location.X, OverrideLocation.X);
		TestEqual(TEXT("Deploy: Y=spawnOverride.Y"), Location.Y, OverrideLocation.Y);
		TestEqual(TEXT("Deploy: Z=scaled drop height"), Location.Z,
			OverrideLocation.Z + Player->GetRespawnComponent()->GetDropSpawnHeight(), 0.01);
		TestEqual(TEXT("Deploy: Yaw=spawnOverrideの向き"), Player->GetActorRotation().Yaw, 90.0);
		TestEqual(TEXT("Deploy: MOVE_Fallingで重力降下開始"),
			Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Falling);
	}

	// --- 3. 死亡待機中の手動Deployは死亡フロー(自動リスポーン・爆発タイマー)をキャンセルする ---
	{
		Kill(Attacker, Player);
		TestTrue(TEXT("再撃破: リスポーン待機開始"), Respawn->IsRespawnPending());
		TickRespawnWorld(World, 1.0f); // 爆発(5s)前に手動Deploy

		const FVector Override2(-3000.0f, 4000.0f, 200.0f);
		Respawn->Deploy(nullptr, FTransform(Override2)); // Preset=null=現在機体(PresetB)のまま

		TestEqual(TEXT("手動Deploy: State.Dead除去"), ASC->GetTagCount(MSTags::State_Dead), 0);
		TestEqual(TEXT("手動Deploy: 機体はPresetBのまま"), Player->GetMobileSuitData(),
			static_cast<UMSMobileSuitData*>(PresetB.Data));
		TestFalse(TEXT("手動Deploy: 自動リスポーンはキャンセル"), Respawn->IsRespawnPending());

		TickRespawnWorld(World, 12.0f); // 旧タイマーが生きていれば爆発5s/リスポーン10sが発火するはずの時間
		TestFalse(TEXT("キャンセル後: 旧爆発タイマーは発火しない"), Respawn->HasExplosionFired());
		TestEqual(TEXT("キャンセル後: 二重リスポーンなし(生存のまま)"), ASC->GetTagCount(MSTags::State_Dead), 0);
	}

	// --- 4. マーカーが地中でも、長距離トレースで地表を解決してから降下する ---
	{
		const FVector MarkerLocation(30000.0f, 30000.0f, 26000.0f);
		const float GroundTopZ = 190000.0f;
		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* GroundCollision = NewObject<UBoxComponent>(Ground);
		Ground->SetRootComponent(GroundCollision);
		GroundCollision->SetBoxExtent(FVector(5000.0f, 5000.0f, 100.0f));
		GroundCollision->SetCollisionObjectType(ECC_WorldStatic);
		GroundCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		GroundCollision->SetCollisionResponseToAllChannels(ECR_Block);
		GroundCollision->RegisterComponent();
		Ground->SetActorLocation(FVector(MarkerLocation.X, MarkerLocation.Y, GroundTopZ - 100.0f));

		Respawn->Deploy(nullptr, FTransform(MarkerLocation));
		const FVector ResolvedDeployLocation = Player->GetActorLocation();
		TestEqual(TEXT("Ground resolve preserves marker X"), ResolvedDeployLocation.X, MarkerLocation.X, 0.01);
		TestEqual(TEXT("Ground resolve preserves marker Y"), ResolvedDeployLocation.Y, MarkerLocation.Y, 0.01);
		TestEqual(TEXT("Ground resolve uses traced surface Z before adding drop height"),
			ResolvedDeployLocation.Z,
			static_cast<double>(GroundTopZ + Respawn->GetDropSpawnHeight()), 0.1);
		TestTrue(TEXT("Ground resolve corrects a marker more than 1km below the surface"),
			ResolvedDeployLocation.Z - MarkerLocation.Z > 100000.0f);
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P3-7] リスポーン(10s)/爆発(5s)/上空8m降下/Deploy目標確定一貫性テスト完了"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRespawnDeckSlotFlowTest,
	"NeoMSGO.Player.Respawn.DeckSlotFlow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSRespawnDeckSlotFlowTest::RunTest(const FString&)
{
	FRespawnTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSTeamAgentActor* Attacker = SpawnRespawnTestAttacker(World);

	const FScopedPreset Preset(5000);
	Preset.Data->ReuseDelaySeconds = 20.0f;
	Preset.Data->EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);

	TArray<UObject*> RootedWeapons;
	auto MakeWeapon = [World, &RootedWeapons](const int32 Magazine, const int32 Reserve)
	{
		UMSRangedWeaponData* Weapon = NewObject<UMSRangedWeaponData>(World);
		Weapon->MagazineSize = Magazine;
		Weapon->ReserveAmmo = Reserve;
		Weapon->ReloadTime = 1.0f;
		Weapon->WeaponSwitchDuration = 0.0f;
		Weapon->AddToRoot();
		RootedWeapons.Add(Weapon);
		return Weapon;
	};
	UMSRangedWeaponData* Slot0Weapon = MakeWeapon(2, 6);
	UMSRangedWeaponData* Slot2Weapon = MakeWeapon(1, 3);
	UMSRangedWeaponData* Slot1Weapon = MakeWeapon(4, 8);
	Preset.Data->EquippedWeapons[0] = Slot0Weapon;
	Preset.Data->EquippedWeapons[1] = Slot1Weapon;
	Preset.Data->EquippedWeapons[2] = Slot2Weapon;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("deck flow player spawned"), Player))
	{
		for (UObject* Weapon : RootedWeapons) Weapon->RemoveFromRoot();
		return false;
	}
	Player->InitializeUnit(Preset.Data);
	UMSRespawnComponent* Respawn = Player->GetRespawnComponent();
	if (!TestNotNull(TEXT("deck flow respawn component exists"), Respawn))
	{
		for (UObject* Weapon : RootedWeapons) Weapon->RemoveFromRoot();
		return false;
	}
	Respawn->SetAutoRespawnEnabled(false);

	// Player defaults are the manual sortie contract: a real GE_Damage kill must
	// expose the seven-second global wait before CanDeploy becomes true.
	TestEqual(TEXT("player default respawn wait is seven seconds"), Respawn->GetRespawnDelay(), 7.0f);
	Kill(Attacker, Player);
	TestTrue(TEXT("real GE_Damage kill starts the global wait"), Respawn->IsRespawnPending());
	TestFalse(TEXT("global wait blocks deployment before seven seconds"), Respawn->CanDeploy());
	TestTrue(TEXT("global wait begins near seven seconds"), Respawn->GetRespawnTimeRemaining() > 6.5f);
	TickRespawnWorld(World, 6.9f);
	TestTrue(TEXT("global wait remains pending just before seven seconds"), Respawn->IsRespawnPending());
	TestFalse(TEXT("global wait is not ready at 6.9 seconds"), Respawn->CanDeploy());
	TickRespawnWorld(World, 0.3f);
	TestFalse(TEXT("global wait becomes ready after seven seconds"), Respawn->IsRespawnPending());
	TestTrue(TEXT("manual sortie becomes deployable after global wait"), Respawn->CanDeploy());

	// UGameInstanceSubsystems must be owned by a UGameInstance even in a
	// commandlet-style test world. Keep both temporary objects rooted for the
	// whole flow so the deck and its soft references cannot be collected.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	UMSDeckSubsystem* Deck = NewObject<UMSDeckSubsystem>(GameInstance);
	Deck->AddToRoot();
	TestTrue(TEXT("same preset can occupy deck slot 0"), Deck->AssignPreset(0, Preset.Data));
	TestTrue(TEXT("same preset can occupy deck slot 1"), Deck->AssignPreset(1, Preset.Data));
	UMSDeckSaveGame* Save = Deck->CreateSaveGameForAutomationTests();
	if (!TestNotNull(TEXT("temporary deck save object created"), Save))
	{
		Deck->RemoveFromRoot();
		GameInstance->RemoveFromRoot();
		for (UObject* Weapon : RootedWeapons) Weapon->RemoveFromRoot();
		return false;
	}
	Save->AddToRoot();
	Save->Slots[0].EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	Save->Slots[1].EquippedWeapons.SetNum(UMSMobileSuitData::MaxWeaponSlots);
	const FSoftObjectPath EmptyWeapon;
	Save->Slots[0].EquippedWeapons[0] = FSoftObjectPath(Slot0Weapon->GetPathName());
	Save->Slots[0].EquippedWeapons[1] = EmptyWeapon;
	Save->Slots[0].EquippedWeapons[2] = FSoftObjectPath(Slot2Weapon->GetPathName());
	Save->Slots[1].EquippedWeapons[0] = EmptyWeapon;
	Save->Slots[1].EquippedWeapons[1] = FSoftObjectPath(Slot1Weapon->GetPathName());
	Save->Slots[1].EquippedWeapons[2] = EmptyWeapon;
	TestTrue(TEXT("temporary save restores two distinct loadouts without disk IO"),
		Deck->RestoreSaveGameForAutomationTests(Save));

	const FMSDeckLoadout LoadedSlot0 = Deck->GetLoadoutInSlot(0);
	const FMSDeckLoadout LoadedSlot1 = Deck->GetLoadoutInSlot(1);
	TestTrue(TEXT("slot 0 loadout keeps weapon at index 0"), LoadedSlot0.EquippedWeapons[0].Get() == Slot0Weapon);
	TestTrue(TEXT("slot 0 loadout keeps an empty index 1"), LoadedSlot0.EquippedWeapons[1].IsNull());
	TestTrue(TEXT("slot 0 loadout keeps weapon at index 2"), LoadedSlot0.EquippedWeapons[2].Get() == Slot2Weapon);
	TestTrue(TEXT("slot 1 loadout keeps an empty index 0"), LoadedSlot1.EquippedWeapons[0].IsNull());
	TestTrue(TEXT("slot 1 loadout keeps weapon at index 1"), LoadedSlot1.EquippedWeapons[1].Get() == Slot1Weapon);
	TestTrue(TEXT("slot 1 loadout keeps an empty index 2"), LoadedSlot1.EquippedWeapons[2].IsNull());

	TestTrue(TEXT("slot 0 deploy applies its loadout"), Respawn->DeployDeckSlot(Deck, 0, FTransform(FVector(1000, 0, 0))));
	UMSWeaponComponent* Weapons = Player->GetWeaponComponent();
	TestEqual(TEXT("slot 0 deploy keeps all three weapon rows"), Weapons->GetSlotCount(), 3);
	TestEqual(TEXT("slot 0 deploy uses weapon 0"), Weapons->GetWeaponData(0), static_cast<UMSWeaponDataBase*>(Slot0Weapon));
	TestNull(TEXT("slot 0 deploy keeps weapon row 1 empty"), Weapons->GetWeaponData(1));
	TestEqual(TEXT("slot 0 deploy uses weapon 2"), Weapons->GetWeaponData(2), static_cast<UMSWeaponDataBase*>(Slot2Weapon));

	Kill(Attacker, Player);
	const float Slot0CooldownAtDeath = Respawn->GetDeckSlotTimeRemaining(0);
	TestTrue(TEXT("death records reuse CT for the active deck slot"), Slot0CooldownAtDeath > 19.0f);
	TestEqual(TEXT("a different deck slot has no reuse CT"), Respawn->GetDeckSlotTimeRemaining(1), 0.0f);
	TestTrue(TEXT("early emergency eject ends only the global wait"), Respawn->RequestEmergencyEject());
	const float Slot0CooldownAfterEject = Respawn->GetDeckSlotTimeRemaining(0);
	TestTrue(TEXT("early emergency eject does not clear the dead slot CT"), Slot0CooldownAfterEject > 18.0f);
	TickRespawnWorld(World, 1.0f);
	TestTrue(TEXT("dead slot CT advances with world time"), Respawn->GetDeckSlotTimeRemaining(0) < Slot0CooldownAfterEject);
	TestEqual(TEXT("other deck slot remains immediately available"), Respawn->GetDeckSlotTimeRemaining(1), 0.0f);
	TestTrue(TEXT("other deck slot can deploy while dead slot is cooling down"),
		Respawn->CanDeployDeckSlot(1) && Respawn->DeployDeckSlot(Deck, 1, FTransform(FVector(0, 1000, 0))));
	TestEqual(TEXT("slot 1 deploy keeps all three weapon rows"), Weapons->GetSlotCount(), 3);
	TestNull(TEXT("slot 1 deploy keeps weapon row 0 empty"), Weapons->GetWeaponData(0));
	TestEqual(TEXT("slot 1 deploy uses weapon 1"), Weapons->GetWeaponData(1), static_cast<UMSWeaponDataBase*>(Slot1Weapon));
	TestNull(TEXT("slot 1 deploy keeps weapon row 2 empty"), Weapons->GetWeaponData(2));

	Save->RemoveFromRoot();
	Deck->RemoveFromRoot();
	GameInstance->RemoveFromRoot();
	for (UObject* Weapon : RootedWeapons) Weapon->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
