#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSIncomingLockComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSHUDWidget.h"
#include "Weapons/MSWeaponComponent.h"
#include "Data/MSWeaponData.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDIncomingLockPresentationTest,
	"NeoMSGO.UI.HUD.IncomingLockPresentation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDIncomingLockAngleTest,
	"NeoMSGO.UI.HUD.IncomingLockCameraRelativeAngle",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

namespace
{
	using K=EMSBattleAssistEffectKind;

	struct FIncomingLockFixture
	{
		UWorld* World=nullptr;
		TStrongObjectPtr<UMSBattleAssistRulesData> Rules;
		TStrongObjectPtr<UMSMobileSuitData> Suit;
		TStrongObjectPtr<UMSBattleAssistModuleData> Module;

		FIncomingLockFixture()
		{
			World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDIncomingLockWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());World->BeginPlay();
			if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
			Rules=TStrongObjectPtr<UMSBattleAssistRulesData>(NewObject<UMSBattleAssistRulesData>());
			Suit=TStrongObjectPtr<UMSMobileSuitData>(NewObject<UMSMobileSuitData>());
			Module=TStrongObjectPtr<UMSBattleAssistModuleData>(NewObject<UMSBattleAssistModuleData>());
			Suit->Cost=200;Suit->MaxArmor=5000.0f;
			Module->FamilyId=TEXT("IncomingLockHUD");Module->SlotCost=3;Module->Support=EMSModuleSupport::Implemented;Module->Evidence=EMSModuleEvidence::Confirmed;
			for(const TPair<K,float>& Value : {
				TPair<K,float>(K::IncomingLockDirection,10.0f),
				TPair<K,float>(K::IncomingLockDirectionDistance,2.0f),
				TPair<K,float>(K::IncomingMissilePrelockWarning,1.0f)})
			{
				FMSBattleAssistEffect& Effect=Module->Effects.AddDefaulted_GetRef();Effect.Kind=Value.Key;Effect.Value=Value.Value;Effect.Unit=TEXT("fixture");
			}
			Rules->Modules.Add(Module.Get());
			FMSBattleAssistSuitProfile& Profile=Rules->SuitProfiles.AddDefaulted_GetRef();Profile.SuitId=Suit->GetPrimaryAssetId();Profile.BattleAssistSlotCapacity=3;
			FMSBattleAssistTimeRow& Time=Rules->TimeRows.AddDefaulted_GetRef();Time.Cost=200;Time.TotalSeconds.Init(60.0f,11);
			MSBattleAssist::SetRulesDataForTests(Rules.Get());
		}

		~FIncomingLockFixture()
		{
			MSBattleAssist::SetRulesDataForTests(nullptr);
			if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
		}

		AMSPlayerCharacter* Spawn(const uint8 Team,const FVector& Location)
		{
			FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AMSPlayerCharacter* Unit=World->SpawnActor<AMSPlayerCharacter>(Location,FRotator::ZeroRotator,Params);
			Unit->SetGenericTeamId(FGenericTeamId(Team));Unit->InitializeUnit(Suit.Get());return Unit;
		}

		bool EquipTarget(AMSPlayerCharacter* Target)
		{
			FMSBattleAssistLoadout Loadout;Loadout.ModuleIds.Add(Module->GetPrimaryAssetId());
			return Target&&Target->InitializeUnitWithBattleAssist(Suit.Get(),Loadout);
		}

		bool ArmManualSource(AMSPlayerCharacter* Source,AMSPlayerCharacter* Target)
		{
			UMSRangedWeaponData* Weapon=NewObject<UMSRangedWeaponData>(Source);
			Weapon->CategoryTag=MSTags::Weapon_Category_MachineGun;
			Source->GetWeaponComponent()->InitializeSlots({Weapon});
			const FRotator AimRotation=(Target->GetActorLocation()-Source->GetActorLocation()).Rotation();
			// The production camera is attached through a spring arm.  Give that
			// attachment one real world update before asking the authority lock path
			// to validate the camera-origin LOS/cone, as gameplay does every frame.
			Source->SetActorRotation(AimRotation);
			Source->GetMSCamera()->SetWorldRotation(AimRotation);
			World->Tick(LEVELTICK_All,0.01f);++GFrameCounter;
			return Source->GetLockOnComponent()->ApplyManualLockIntent(Target,Source->GetBattleAssistEpoch(),1,Target->GetBattleAssistEpoch());
		}
	};
}

bool FMSHUDIncomingLockAngleTest::RunTest(const FString&)
{
	TestEqual(TEXT("world bearing straight ahead is camera-relative zero"),UMSHUDViewModel::CalculateIncomingLockRelativeAngle(0.0f,0.0f),0.0f);
	TestEqual(TEXT("world bearing right of a 90 degree camera is zero"),UMSHUDViewModel::CalculateIncomingLockRelativeAngle(90.0f,90.0f),0.0f);
	TestEqual(TEXT("camera-relative left wraps to negative ninety"),UMSHUDViewModel::CalculateIncomingLockRelativeAngle(0.0f,90.0f),-90.0f);
	TestEqual(TEXT("camera-relative angle takes the shortest signed path"),UMSHUDViewModel::CalculateIncomingLockRelativeAngle(179.0f,-179.0f),-2.0f);
	return true;
}

bool FMSHUDIncomingLockPresentationTest::RunTest(const FString&)
{
	FIncomingLockFixture Fixture;
	AMSPlayerCharacter* Target=Fixture.Spawn(1,FVector::ZeroVector);
	TestTrue(TEXT("target receives the real incoming-lock module loadout"),Fixture.EquipTarget(Target));
	APlayerController* Controller=Fixture.World->SpawnActor<APlayerController>();Controller->Possess(Target);Controller->SetControlRotation(FRotator::ZeroRotator);
	TArray<FVector> SourceLocations;
	for(int32 Index=0;Index<11;++Index)
	{
		const float Angle=FMath::DegreesToRadians(Index*30.0f);
		SourceLocations.Add(FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.0f)*400.0f);
	}
	for(const FVector& Location:SourceLocations)
	{
		AMSPlayerCharacter* Source=Fixture.Spawn(2,Location);
		TestTrue(TEXT("real source lock is accepted by the authority path"),Fixture.ArmManualSource(Source,Target));
	}

	const FMSIncomingLockSnapshot& ReceiverSnapshot=Target->GetIncomingLockComponent()->GetSnapshot();
	TestTrue(TEXT("receiver snapshot publishes the real incoming lock"),ReceiverSnapshot.bIncomingLock);
	TestTrue(TEXT("receiver snapshot contains no source actor pointers"),ReceiverSnapshot.Entries.Num()<=10);
	TestEqual(TEXT("component caps direction entries at ten"),ReceiverSnapshot.Entries.Num(),10);
	int32 DistanceCount=0;for(const FMSIncomingLockEntry& Entry:ReceiverSnapshot.Entries)DistanceCount+=Entry.bHasDistance?1:0;
	TestEqual(TEXT("component caps distance disclosure at two"),DistanceCount,2);

	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>(Controller);ViewModel->Initialize(Controller);
	const FMSHUDIncomingLockSnapshot View=ViewModel->GetIncomingLock();
	TestEqual(TEXT("view model preserves the component direction cap"),View.DirectionIndicators.Num(),10);
	TestEqual(TEXT("view model preserves the component distance cap"),View.DistanceIndicators.Num(),2);
	TestTrue(TEXT("manual lock does not claim the missile prelock warning"),!View.bMissilePrelockWarning);
	TestTrue(TEXT("first source bearing is camera-relative and finite"),FMath::IsFinite(View.DirectionIndicators[0].RelativeAngleDegrees));

	UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(ViewModel);HUD->RefreshCombatForAutomationTests();
	TestEqual(TEXT("widget consumes all ten receiver direction indicators"),HUD->GetIncomingDirectionCountForAutomationTests(),10);
	TestEqual(TEXT("widget consumes only two receiver distance indicators"),HUD->GetIncomingDistanceCountForAutomationTests(),2);
	TestTrue(TEXT("baseline incoming lock flashes both HUD edges"),HUD->IsIncomingLockEdgeFlashVisibleForAutomationTests());
	TestFalse(TEXT("manual lock does not show missile prelock text"),HUD->IsIncomingMissileWarningVisibleForAutomationTests());

	Target->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Dead);
	TestFalse(TEXT("dead receiver hides incoming lock data through the component contract"),ViewModel->GetIncomingLock().bVisible);
	HUD->RefreshCombatForAutomationTests();
	TestEqual(TEXT("dead receiver clears direction arrows"),HUD->GetIncomingDirectionCountForAutomationTests(),0);
	TestFalse(TEXT("dead receiver clears baseline edge warning"),HUD->IsIncomingLockEdgeFlashVisibleForAutomationTests());
	return true;
}

#endif
