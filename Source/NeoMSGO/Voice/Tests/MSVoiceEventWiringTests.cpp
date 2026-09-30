#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MSAttributeSet_Vital.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSRepairPod.h"
#include "Gameplay/MSSupplyPack.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVoiceGameplayEventWiringTest,
	"NeoMSGO.Voice.EventWiring.RepairAndSupply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FScopedVoiceEventWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSVoiceEventTestWorld"));
		FScopedVoiceEventWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedVoiceEventWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

bool FMSVoiceGameplayEventWiringTest::RunTest(const FString&)
{
	FScopedVoiceEventWorld Scope;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Player spawned"), Player)) return false;
	Player->SetGenericTeamId(MSTeam::PlayerTeamId());
	Player->GetVitalSet()->SetMaxArmor(2000.0f);
	Player->GetVitalSet()->SetArmor(100.0f);

	AMSRepairPod* Pod = AMSRepairPod::Spawn(Scope.World, FVector::ZeroVector, 0.0f,
		MSTeam::PlayerTeamId());
	int32 RepairNotifications = 0;
	AMSUnitBase* RepairedUnit = nullptr;
	Pod->OnPlayerRepaired.AddLambda([&](AMSUnitBase* Unit)
	{
		++RepairNotifications;
		RepairedUnit = Unit;
	});
	Pod->RepairUnitsInRange();
	TestEqual(TEXT("Repair pod emits one native player-repair notification"), RepairNotifications, 1);
	TestEqual(TEXT("Repair notification identifies the player target"), RepairedUnit, static_cast<AMSUnitBase*>(Player));

	Player->GetVitalSet()->SetArmor(100.0f);
	AMSSupplyPack* Pack = AMSSupplyPack::Spawn(Scope.World, FVector::ZeroVector,
		EMSSupplyPackType::Repair, MSTeam::PlayerTeamId());
	int32 PickupNotifications = 0;
	AMSUnitBase* PickupConsumer = nullptr;
	Pack->OnSupplyPickedUp.AddLambda([&](AMSUnitBase* Unit)
	{
		++PickupNotifications;
		PickupConsumer = Unit;
	});
	TestTrue(TEXT("Supply pack is consumed successfully"), Pack->TryConsume(Player));
	TestEqual(TEXT("Successful pickup emits one native notification"), PickupNotifications, 1);
	TestEqual(TEXT("Pickup notification identifies the consumer"), PickupConsumer, static_cast<AMSUnitBase*>(Player));
	return true;
}

#endif
