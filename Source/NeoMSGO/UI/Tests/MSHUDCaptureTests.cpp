#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "Components/TextBlock.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSHUDWidget.h"
#include "UI/MSHUDRingWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDCapturePresentationTest,"NeoMSGO.UI.HUD.CapturePresentation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMSHUDCapturePresentationTest::RunTest(const FString&)
{
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSCapturePresentationWorld"));
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());World->BeginPlay();
 if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Point=World->SpawnActor<AMSCapturePoint>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
 Point->SetPointName(FText::FromString(TEXT("3")));
 auto* Player=World->SpawnActor<AMSPlayerCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
 UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(World);Suit->MaxArmor=4000;
 Player->InitializeUnit(Suit);Player->SetGenericTeamId(MSTeam::PlayerTeamId());
 auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
 auto* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
 auto* HUD=NewObject<UMSHUDWidget>();HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
 auto* Ring=CastChecked<UMSHUDRingWidget>(HUD->GetWidgetFromName(TEXT("CaptureProgressFrame")));
 auto* Label=CastChecked<UTextBlock>(HUD->GetWidgetFromName(TEXT("CaptureStateText")));
 TestTrue(TEXT("neutral point is visible in range"),VM->GetCapture().bVisible);
 TestEqual(TEXT("neutral point starts empty, not half full"),VM->GetCapture().Progress,0.0f);
 TestEqual(TEXT("public point name replaces generated actor id"),VM->GetCapture().Name.ToString(),FString(TEXT("3")));
 Point->SetFixedOwner(EMSCaptureOwner::TeamEnemy);Point->TickCaptureLogic(1,0,2.0f);
 HUD->RefreshCaptureForAutomationTests();
 TestTrue(TEXT("hostile phase shrinks from enemy control"),FMath::IsNearlyEqual(Ring->GetProgress(),.7f));
 TestTrue(TEXT("enemy ownership uses the blue reference fill"),Ring->GetColor().Equals(MSHUDPalette::CaptureHostileFill));
 TestEqual(TEXT("reference label stays Japanese"),Label->GetText().ToString(),FString(TEXT("制圧中")));
 Point->SetFixedOwner(EMSCaptureOwner::Neutral);Point->TickCaptureLogic(1,0,2.0f);
 HUD->RefreshCaptureForAutomationTests();
 TestTrue(TEXT("friendly phase grows from neutral"),FMath::IsNearlyEqual(Ring->GetProgress(),.3f));
 TestTrue(TEXT("friendly control uses orange reference fill"),Ring->GetColor().Equals(MSHUDPalette::CaptureFill));
 const float BeforeContest=Point->GetProgress();Point->TickCaptureLogic(1,1,2.0f);
 TestEqual(TEXT("contested actual capture logic does not advance UI progress"),VM->GetCapture().Progress,BeforeContest);
	Point->SetFixedOwner(EMSCaptureOwner::TeamPlayer);HUD->RefreshCaptureForAutomationTests();
 TestTrue(TEXT("completion keeps full gauge and completion text"),VM->GetCapture().bCompleted && VM->GetCapture().Progress==1.f);
 TestEqual(TEXT("completion label"),VM->GetCapture().State.ToString(),FString(TEXT("制圧完了")));
 for(int32 Frame=0;Frame<40;++Frame)World->Tick(LEVELTICK_TimeOnly,.05f);HUD->RefreshCaptureForAutomationTests();
 TestFalse(TEXT("completed friendly point hides after presentation interval"),VM->GetCapture().bVisible);
 TestEqual(TEXT("widget hides completed gauge"),Ring->GetVisibility(),ESlateVisibility::Collapsed);
 Player->SetGenericTeamId(MSTeam::EnemyTeamId());
 TestTrue(TEXT("opposite faction sees the same point as hostile"),VM->GetCapture().bHostileProgress);
 Point->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
 TestTrue(TEXT("opposite faction also receives completion transition"),VM->GetCapture().bCompleted);
 for(int32 Frame=0;Frame<40;++Frame)World->Tick(LEVELTICK_TimeOnly,.05f);
 TestFalse(TEXT("opposite faction hides completed point after interval"),VM->GetCapture().bVisible);
 Point->SetFixedOwner(EMSCaptureOwner::Neutral);Point->TickCaptureLogic(0,1,2.0f);
 TestFalse(TEXT("enemy-team player's own negative progress is friendly"),VM->GetCapture().bHostileProgress);
 TestTrue(TEXT("negative team capture has the same fill magnitude"),FMath::IsNearlyEqual(VM->GetCapture().Progress,.3f));
 Player->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Dead);
 TestFalse(TEXT("dead pawn cannot show capture gauge"),VM->GetCapture().bVisible);
 Player->GetAbilitySystemComponent()->RemoveLooseGameplayTag(MSTags::State_Dead);
 Player->SetActorLocation(FVector(Point->GetCaptureRadius()+100,0,0));
 TestFalse(TEXT("out of range hides capture gauge"),VM->GetCapture().bVisible);
 Player->SetActorLocation(FVector::ZeroVector);Point->Destroy();
 World->SpawnActor<AMSHomeBase>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
 TestFalse(TEXT("home base never creates numeric capture UI"),VM->GetCapture().bVisible);
 VM->Initialize(nullptr);World->BeginTearingDown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return true;
}
#endif
