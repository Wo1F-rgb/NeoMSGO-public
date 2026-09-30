#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UI/MSMapBoundsActor.h"
#include "UI/MSMapCaptureSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSUndergroundMapSelectionTest,
    "NeoMSGO.UI.Map.UndergroundLayerSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSUndergroundMapTest
{
    struct FWorldScope
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSUndergroundLayerWorld"));
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
            if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

bool FMSUndergroundMapSelectionTest::RunTest(const FString&)
{
    using namespace MSUndergroundMapTest;
    {
        FWorldScope Scope;
        AMSMapBoundsActor* Bounds = Scope.World->SpawnActor<AMSMapBoundsActor>();
        UMSMapCaptureSubsystem* Capture = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
        if (!TestNotNull(TEXT("bounds"), Bounds) || !TestNotNull(TEXT("capture"), Capture)) return false;
        Bounds->UndergroundRegions.Add(FBox(FVector(-1000, -1000, -6900), FVector(1000, 1000, -3000)));
        Bounds->UndergroundRegions.Add(FBox(FVector(2000, -1000, -10100), FVector(5000, 1000, -3000)));
        Bounds->UndergroundOverviewTexture = UTexture2D::CreateTransient(14, 9);
        TestFalse(TEXT("ordinary map does not opt in by altitude"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -5569), false));
        TestNull(TEXT("ordinary map has no exposed underground texture"), Capture->GetUndergroundOverviewTexture());
        Bounds->bEnableUndergroundLayer = true;
        TestTrue(TEXT("source corridor selects underground"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -5569), false));
        TestTrue(TEXT("deep hall selects underground"), Capture->ShouldUseUndergroundLayer(FVector(3000, 0, -8769), false));
        TestFalse(TEXT("surface over same tunnel remains surface"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, 831), false));
        TestFalse(TEXT("low point outside tunnel XY remains surface"), Capture->ShouldUseUndergroundLayer(FVector(10000, 0, -5569), false));
        TestFalse(TEXT("point below authored floor volume remains surface"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -12000), false));
        for (const bool Previous : {false, true})
            TestFalse(TEXT("river bed cannot select underground from either state"),
                Capture->ShouldUseUndergroundLayer(FVector(0, 0, -2700), Previous));
        TestFalse(TEXT("entrance hysteresis band does not enter early"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -3250), false));
        TestTrue(TEXT("entrance hysteresis band retains underground"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -3250), true));
        TestFalse(TEXT("ascent above exit threshold returns surface"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -2999), true));
        TestFalse(TEXT("outside XY does not enter"), Capture->ShouldUseUndergroundLayer(FVector(1150, 0, -5569), false));
        TestTrue(TEXT("small XY edge motion retains active layer"), Capture->ShouldUseUndergroundLayer(FVector(1150, 0, -5569), true));
        TestFalse(TEXT("outside XY hysteresis margin returns surface"), Capture->ShouldUseUndergroundLayer(FVector(1301, 0, -5569), true));
        TestTrue(TEXT("API returns the authored hard referenced texture"), Capture->GetUndergroundOverviewTexture() == Bounds->UndergroundOverviewTexture);
        for (int32 Index = 0; Index < 100; ++Index)
            Capture->ShouldUseUndergroundLayer(FVector(0, 0, -5569), true);
        TestEqual(TEXT("layer decisions request no scene captures"), Capture->GetCaptureRequestCountForTesting(), 0);
        Bounds->UndergroundOverviewTexture = nullptr;
        TestFalse(TEXT("missing asset falls back to surface"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -5569), true));
        Bounds->Destroy();
        TestNull(TEXT("removed bounds cannot leak old texture"), Capture->GetUndergroundOverviewTexture());
    }
    {
        FWorldScope NextMap;
        UMSMapCaptureSubsystem* Capture = NextMap.World->GetSubsystem<UMSMapCaptureSubsystem>();
        TestFalse(TEXT("next world does not inherit last map layer"), Capture->ShouldUseUndergroundLayer(FVector(0, 0, -5569), true));
        TestNull(TEXT("next world has no old layer texture"), Capture->GetUndergroundOverviewTexture());
    }
    return true;
}
#endif
