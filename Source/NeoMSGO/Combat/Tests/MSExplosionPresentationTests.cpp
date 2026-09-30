#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSExplosionPresentationTest,
 "NeoMSGO.Combat.WeaponGameplayCues.ExplosionPresentation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSExplosionPresentationTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ExplosionPresentationWorld"));
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 // Exercise the actual gameplay cue path, not a separate preview-only factory.
 AActor* Target = World->SpawnActor<AActor>();
 FGameplayCueParameters Parameters;
 Parameters.Location = FVector::ZeroVector;
 GetDefault<UMSGameplayCue_Explosion>()->OnExecute_Implementation(Target, Parameters);
 AMSTransientVfxActor* Tail = nullptr;
 AMSTransientVfxActor* Flash = nullptr;
 for (TActorIterator<AMSTransientVfxActor> It(World); It; ++It)
 {
  if (It->ActorHasTag(TEXT("MSVfx.ExplosionTail"))) Tail = *It;
  if (It->ActorHasTag(TEXT("MSVfx.ExplosionFlash"))) Flash = *It;
 }
 if (TestNotNull(TEXT("production cue creates fire/smoke tail"), Tail) &&
     TestNotNull(TEXT("production cue creates short textured flash"), Flash))
 {
  TInlineComponentArray<UStaticMeshComponent*> FlashMeshes(Flash);
  TestEqual(TEXT("impact flash never adds the old solid orange sphere"), FlashMeshes.Num(), 0);
  TestFalse(TEXT("cosmetic tail has no replication traffic"), Tail->GetIsReplicated());
  TestFalse(TEXT("cosmetic tail never enables actor collision"), Tail->GetActorEnableCollision());
  TInlineComponentArray<UMaterialBillboardComponent*> Billboards(Tail);
  UMaterialBillboardComponent* Fire = nullptr;
  UMaterialBillboardComponent* Smoke = nullptr;
  int32 FlameCount = 0, SmokeCount = 0, GroundCount = 0;
  for (auto* C : Billboards)
  {
   TestEqual(TEXT("all cosmetic billboards have no collision"), C->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
   if (C->GetFName() == TEXT("ExplosionFire")) Fire = C;
   if (C->GetName().StartsWith(TEXT("ExplosionFire"))) ++FlameCount;
   if (C->ComponentHasTag(TEXT("ExplosionSmoke"))) { ++SmokeCount; Smoke = C; }
   if (C->ComponentHasTag(TEXT("ExplosionGroundDust"))) ++GroundCount;
  }
  TestEqual(TEXT("near explosion uses three staggered authored flames"), FlameCount, 3);
  TestEqual(TEXT("near explosion has four smoke lobes"), SmokeCount, 4);
  TestEqual(TEXT("air burst never invents a floating ground layer"), GroundCount, 0);
  if (TestNotNull(TEXT("main fire billboard exists"), Fire) && TestNotNull(TEXT("smoke billboard exists"), Smoke))
  {
   auto* FireMaterial = Cast<UMaterialInstanceDynamic>(Fire->GetMaterial(0));
   auto* SmokeMaterial = Cast<UMaterialInstanceDynamic>(Smoke->GetMaterial(0));
   if (TestNotNull(TEXT("fire uses imported material instance"), FireMaterial) &&
       TestNotNull(TEXT("smoke has opacity animation"), SmokeMaterial))
   {
    TestEqual(TEXT("smoke waits for fire to develop"), SmokeMaterial->K2_GetScalarParameterValue(TEXT("SmokeOpacity")), 0.f);
    const float SmokeInitialSize = Smoke->Elements[0].BaseSizeX;
    Tail->Tick(.43f);
    TestTrue(TEXT("flame atlas advances in production tick"), FireMaterial->K2_GetScalarParameterValue(TEXT("FrameY")) >= 4.f);
    TestTrue(TEXT("smoke fades in after ignition"), SmokeMaterial->K2_GetScalarParameterValue(TEXT("SmokeOpacity")) > 0.1f);
    TestTrue(TEXT("world-size billboard grows, not only its ignored component scale"), Smoke->Elements[0].BaseSizeX > SmokeInitialSize);
    Flash->Tick(.07f);
    TestTrue(TEXT("flash ends within 60 ms"), Flash->IsHidden());
    TestFalse(TEXT("fire survives the short flash"), Tail->IsHidden());
    Tail->Tick(.5f);
    TestFalse(TEXT("flame is hidden when its animation finishes"), Fire->IsVisible());
    TestTrue(TEXT("smoke remains after the flame"), SmokeMaterial->K2_GetScalarParameterValue(TEXT("SmokeOpacity")) > 0.f);
    Tail->Tick(1.f);
    TestTrue(TEXT("tail stops drawing at its own lifetime"), Tail->IsHidden());
    TestFalse(TEXT("expired tail stops ticking"), Tail->IsActorTickEnabled());
   }
  }
 }
 GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSExplosionGroundDustTest,
 "NeoMSGO.Combat.WeaponGameplayCues.ExplosionGroundDust",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSExplosionGroundDustTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ExplosionGroundWorld"));
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 AActor* Ground = World->SpawnActor<AActor>();
 UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Ground);
 Ground->SetRootComponent(Mesh);
 Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
 Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Mesh->SetCollisionObjectType(ECC_WorldStatic);
 Mesh->SetCollisionResponseToAllChannels(ECR_Block);
 Mesh->SetWorldLocation(FVector(0, 0, -60));
 Mesh->SetWorldScale3D(FVector(20, 20, 1));
 Mesh->RegisterComponent();
 AMSTransientVfxActor::SpawnExplosion(World, FVector::ZeroVector, 90.f, .06f);
 int32 DustCount = 0;
 for (TActorIterator<AMSTransientVfxActor> It(World); It; ++It)
 {
  TInlineComponentArray<UMaterialBillboardComponent*> Components(*It);
  for (auto* C : Components)
   if (C->ComponentHasTag(TEXT("ExplosionGroundDust")))
   {
    ++DustCount;
    TestTrue(TEXT("dust has a flattened ground-hugging profile"), C->Elements[0].BaseSizeY < C->Elements[0].BaseSizeX * .3f);
   }
 }
 TestEqual(TEXT("actual world-static ground produces three dust lobes"), DustCount, 3);
 GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}
#endif