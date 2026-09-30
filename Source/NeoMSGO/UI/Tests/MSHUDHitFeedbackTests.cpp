// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSVitalGameplayEffects.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSHitFeedbackSubsystem.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSHUDViewModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDHitFeedbackTest,"NeoMSGO.UI.HUD.HitFeedback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDDamageDirectionDelegateTest,"NeoMSGO.UI.HUD.DamageDirectionDelegate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

namespace
{
	struct FHitFeedbackWorld
	{
		UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDHitFeedbackWorld"));
		UGameInstance* GameInstance=NewObject<UGameInstance>(GEngine);
		FHitFeedbackWorld()
		{
			FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.OwningGameInstance=GameInstance;Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);GameInstance->Init();World->InitializeActorsForPlay(FURL());World->BeginPlay();
			if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FHitFeedbackWorld(){GameInstance->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
		void Tick(float Seconds){while(Seconds>0.0f){const float Step=FMath::Min(Seconds,.1f);World->Tick(LEVELTICK_All,Step);Seconds-=Step;GFrameCounter++;}}
	};

	AMSPlayerCharacter* SpawnPlayer(UWorld* World,const FVector& Location,const FRotator& Rotation,const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),Location,Rotation,Params);Player->SetGenericTeamId(Team);return Player;
	}

	void ApplyDamage(AMSPlayerCharacter* Source,AMSPlayerCharacter* Target,float Damage,const FVector& AttackerLocation)
	{
		FMSGameplayEffectContext* Context=new FMSGameplayEffectContext();Context->AddInstigator(Source,Source);Context->SetAttackerLocation(AttackerLocation);
		FGameplayEffectSpec Spec(GetDefault<UGE_Damage>(),FGameplayEffectContextHandle(Context),1.0f);Spec.SetSetByCallerMagnitude(MSTags::Data_Damage,Damage);
		Source->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(Spec,Target->GetAbilitySystemComponent());
	}

	FGameplayEffectContextHandle MakeContext(AActor* Instigator,const FVector& Location)
	{
		FMSGameplayEffectContext* Context=new FMSGameplayEffectContext();Context->AddInstigator(Instigator,Instigator);Context->SetAttackerLocation(Location);return FGameplayEffectContextHandle(Context);
	}
}

bool FMSHUDHitFeedbackTest::RunTest(const FString&)
{
	FHitFeedbackWorld Scope;
	AMSPlayerCharacter* Player=SpawnPlayer(Scope.World,FVector::ZeroVector,FRotator::ZeroRotator,MSTeam::PlayerTeamId());
	APlayerController* Controller=Scope.World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>(Controller);ViewModel->Initialize(Controller);
	UMSHitFeedbackSubsystem* Feedback=Scope.GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>();
	if(!TestNotNull(TEXT("hit feedback subsystem exists"),Feedback))return false;

	Feedback->BroadcastHitConfirmed(Player,false);
	auto Hit=ViewModel->GetHitMarker();TestTrue(TEXT("normal hit requests a marker"),Hit.bVisible);TestFalse(TEXT("normal hit is not styled as HS"),Hit.bHeadshot);
	Feedback->BroadcastHitConfirmed(Player,true);
	Hit=ViewModel->GetHitMarker();TestTrue(TEXT("HS hit requests a marker"),Hit.bVisible);TestTrue(TEXT("HS request retains headshot style"),Hit.bHeadshot);

	Feedback->BroadcastKillConfirmed(Player);TestEqual(TEXT("first kill starts streak one"),ViewModel->GetKillPopup().KillStreak,1);
	Scope.Tick(1.2f);Feedback->BroadcastKillConfirmed(Player);TestEqual(TEXT("kill inside 1.3 seconds increments"),ViewModel->GetKillPopup().KillStreak,2);
	Scope.Tick(1.31f);Feedback->BroadcastKillConfirmed(Player);TestEqual(TEXT("streak persists until player death"),ViewModel->GetKillPopup().KillStreak,3);
	ViewModel->ProcessPlayerDeath();TestEqual(TEXT("player death resets streak"),ViewModel->GetKillPopup().KillStreak,0);
	TestEqual(TEXT("elapsed time does not reset streak"),UMSHUDViewModel::AdvanceKillStreak(2,10.0,99.0),3);

	TestTrue(TEXT("north-facing rear attacker is 180 degrees"),FMath::IsNearlyEqual(
		UMSHUDViewModel::CalculateRelativeDamageAngle(FVector::ZeroVector,FRotator(0,90,0),FVector(0,-100,0)),180.0f,.01f));
	TestTrue(TEXT("east-facing right attacker is +90 degrees"),FMath::IsNearlyEqual(
		UMSHUDViewModel::CalculateRelativeDamageAngle(FVector::ZeroVector,FRotator::ZeroRotator,FVector(0,100,0)),90.0f,.01f));
	return true;
}

bool FMSHUDDamageDirectionDelegateTest::RunTest(const FString&)
{
	FHitFeedbackWorld Scope;
	AMSPlayerCharacter* Player=SpawnPlayer(Scope.World,FVector::ZeroVector,FRotator(0,90,0),MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Attacker=SpawnPlayer(Scope.World,FVector(0,-1000,0),FRotator::ZeroRotator,MSTeam::EnemyTeamId());
	APlayerController* Controller=Scope.World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>(Controller);ViewModel->Initialize(Controller);

	ApplyDamage(Attacker,Player,100.0f,Attacker->GetActorLocation());
	auto Direction=ViewModel->GetDamageDirection();TestTrue(TEXT("actual armor damage broadcasts direction"),Direction.bVisible);
	TestTrue(TEXT("damage delegate carries rear attacker location"),FMath::IsNearlyEqual(Direction.RelativeAngleDegrees,180.0f,.01f));

	Scope.Tick(.1f);
	FGameplayEffectSpec Repair(GetDefault<UGE_RepairPod>(),MakeContext(Attacker,FVector(1000,0,0)),1.0f);Repair.SetSetByCallerMagnitude(MSTags::Data_RepairPod,50.0f);
	Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(Repair);
	Direction=ViewModel->GetDamageDirection();TestTrue(TEXT("positive additive repair does not broadcast"),FMath::IsNearlyEqual(Direction.RelativeAngleDegrees,180.0f,.01f));

	Scope.Tick(.1f);
	FGameplayEffectSpec Init(GetDefault<UGE_InitStats>(),MakeContext(Attacker,FVector(1000,0,0)),1.0f);
	Init.SetSetByCallerMagnitude(MSTags::Data_Init_MaxArmor,5000.0f);Init.SetSetByCallerMagnitude(MSTags::Data_Init_MaxDownPoint,100.0f);Init.SetSetByCallerMagnitude(MSTags::Data_Init_BoostCapacity,100.0f);
	Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(Init);
	Direction=ViewModel->GetDamageDirection();TestTrue(TEXT("override initialization does not broadcast"),FMath::IsNearlyEqual(Direction.RelativeAngleDegrees,180.0f,.01f));

	int32 KillNotifications=0;AActor* ConfirmedKiller=nullptr;
	UMSHitFeedbackSubsystem* Feedback=Scope.GameInstance->GetSubsystem<UMSHitFeedbackSubsystem>();
	const FDelegateHandle Handle=Feedback->OnKillConfirmed.AddLambda([&](AActor* Killer){++KillNotifications;ConfirmedKiller=Killer;});
	AMSPlayerCharacter* Victim=SpawnPlayer(Scope.World,FVector(1000,0,0),FRotator::ZeroRotator,MSTeam::EnemyTeamId());
	ApplyDamage(Player,Victim,Victim->GetVitalSet()->GetMaxArmor()*2.0f,Player->GetActorLocation());
	TestEqual(TEXT("death transition broadcasts one kill confirmation"),KillNotifications,1);TestEqual(TEXT("kill confirmation identifies killer"),ConfirmedKiller,static_cast<AActor*>(Player));
	Feedback->OnKillConfirmed.Remove(Handle);
	return true;
}

#endif
