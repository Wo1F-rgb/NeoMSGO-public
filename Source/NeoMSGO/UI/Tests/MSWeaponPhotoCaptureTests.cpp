#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/MSWeaponPhotoSubsystem.h"
#include "Data/MSWeaponData.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponPhotoFirstReadyColorTest,
 "NeoMSGO.UI.WeaponPhoto.FirstReadySourceColor",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

namespace
{
 // Use the production GetPhoto path, actual BSG asset, fresh per-world cache,
 // and normal diagnostic mode. Never accept a grey first photo and recapture it.
 class FMSWaitForFirstWeaponPhoto final : public IAutomationLatentCommand
 {
 public:
  explicit FMSWaitForFirstWeaponPhoto(FAutomationTestBase* InTest)
   : Test(InTest),Started(FPlatformTime::Seconds())
  {
   World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSFirstWeaponPhotoWorld"));
   FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
   Context.SetCurrentWorld(World);
   World->InitializeActorsForPlay(FURL());World->BeginPlay();
   if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
   Weapon.Reset(LoadObject<UMSWeaponDataBase>(nullptr,
    TEXT("/Game/Data/Weapons/DA_Weapon_MPU01_BeamSprayGun.DA_Weapon_MPU01_BeamSprayGun")));
  }
  virtual ~FMSWaitForFirstWeaponPhoto() override
  {
   if(World->GetBegunPlay())World->EndPlay(EEndPlayReason::Quit);
   GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
  }
  virtual bool Update() override
  {
   if(!Weapon.IsValid()){Test->AddError(TEXT("Actual BSG data asset missing"));return true;}
   const double Now=FPlatformTime::Seconds();
   if(Now-Started>30.0){Test->AddError(TEXT("Photo did not become ready within the widget's 30-second retry budget"));return true;}
   if(Now<NextAttempt)return false;
   NextAttempt=Now+0.25;
   UMSWeaponPhotoSubsystem* Subsystem=World->GetSubsystem<UMSWeaponPhotoSubsystem>();
   if(!Subsystem){Test->AddError(TEXT("Photo subsystem missing"));return true;}
   UTexture2D* Photo=Subsystem->GetPhoto(Weapon.Get());
   ++Attempts;
   // Every incomplete attempt must release its temporary actor/components too.
   int32 Studios=0;
   for(TActorIterator<AActor> It(World);It;++It)
   {
    if(It->IsActorBeingDestroyed())continue;
    TArray<UStaticMeshComponent*> Components;It->GetComponents(Components);
    for(const UStaticMeshComponent* Component:Components)
     if(Component->GetStaticMesh()==Weapon->WeaponMesh.Get())++Studios;
   }
   Test->TestEqual(TEXT("Photo attempts leave no live studio mesh"),Studios,0);
   if(!Photo)return false;
   Test->TestTrue(TEXT("First completed photo is reused from the normal cache"),Subsystem->GetPhoto(Weapon.Get())==Photo);
   FTexturePlatformData* Data=Photo->GetPlatformData();
   if(!Data||Data->Mips.IsEmpty()){Test->AddError(TEXT("Photo has no inspectable pixels"));return true;}
   const int32 Count=Photo->GetSizeX()*Photo->GetSizeY();
   const FColor* Pixels=static_cast<const FColor*>(Data->Mips[0].BulkData.LockReadOnly());
   if(!Pixels){Data->Mips[0].BulkData.Unlock();Test->AddError(TEXT("Photo pixel readback missing"));return true;}
   int32 Orange=0,Blue=0,Opaque=0;
   for(int32 Index=0;Index<Count;++Index)
   {
    if(Pixels[Index].A<242)continue;
    ++Opaque;const FLinearColor C(Pixels[Index]);
    if(C.R>0.03f&&C.R>C.G*1.25f&&C.R>C.B*1.6f)++Orange;
    if(C.B>0.003f&&C.B>C.R*1.25f&&C.B>C.G*1.05f)++Blue;
   }
   Test->TestTrue(TEXT("First published photo contains the source orange muzzle"),Orange>=8);
   Test->TestTrue(TEXT("First published photo contains the source navy paint"),Blue>=100);
   Test->TestTrue(TEXT("Photo contains an opaque weapon silhouette"),Opaque>=1000);
   const FString ImagePath=FPaths::ProjectSavedDir()/TEXT("Automation/WeaponPhotoFirstReady.png");
   FImageUtils::SaveImageByExtension(*ImagePath,FImageView(Pixels,Photo->GetSizeX(),Photo->GetSizeY()));
   Data->Mips[0].BulkData.Unlock();
   Test->AddInfo(FString::Printf(TEXT("Normal-mode FIRST photo: attempts=%d orange=%d blue=%d opaque=%d; %s. A fresh-process hangar check is still required to exercise cold shader resources."),Attempts,Orange,Blue,Opaque,*ImagePath));
   return true;
  }
 private:
  FAutomationTestBase* Test;
  UWorld* World=nullptr;
  TStrongObjectPtr<UMSWeaponDataBase> Weapon;
  double Started=0.0,NextAttempt=0.0;
  int32 Attempts=0;
 };
}

bool FMSWeaponPhotoFirstReadyColorTest::RunTest(const FString&)
{
 if(!FApp::CanEverRender()||GUsingNullRHI)
 {
  AddWarning(TEXT("FirstReadySourceColor requires an actual renderer; no colour validation was performed."));
  return true;
 }
 IConsoleVariable* Diagnostics=IConsoleManager::Get().FindConsoleVariable(TEXT("ms.WeaponPhoto.Diagnostics"));
 if(!TestNotNull(TEXT("Photo diagnostic cvar exists"),Diagnostics))return false;
 if(!TestEqual(TEXT("Run this regression in ordinary mode, with no diagnostic extra capture"),Diagnostics->GetInt(),0))return false;
 ADD_LATENT_AUTOMATION_COMMAND(FMSWaitForFirstWeaponPhoto(this));
 return true;
}
#endif
