#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AI/MSBotSpawnPoint.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Data/MSMobileSuitData.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSDestructibleTarget.h"
#include "Gameplay/MSHomeBase.h"
#include "UI/MSDeploymentMapWidget.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSMapTypes.h"
#include "UI/MSMapBoundsActor.h"
#include "UI/MSMapCaptureSubsystem.h"
#include "UI/MSMapRadarRangeWidget.h"
#include "UI/MSMapWidgetBase.h"
#include "UI/MSFullscreenMapWidget.h"
#include "UI/MSHUDWidget.h"
#include "UI/MSHUDViewModel.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSBriefingWidget.h"
#include "UI/MSLobbyWidget.h"
#include "UI/MSSortieWidget.h"
#include "UI/MSSpawnSelectWidget.h"
#include "Misc/App.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapCoordinateTest, "NeoMSGO.UI.Map.Coordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapCaptureSubsystemTest, "NeoMSGO.UI.Map.OverviewCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapWaterProxyFiniteTest, "NeoMSGO.UI.Map.WaterProxyFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapRadarProjectionTest, "NeoMSGO.UI.Map.RadarRangeProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapGrid500mCaptureTest, "NeoMSGO.UI.Map.Grid500mRectangularCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapUIFitEntrancesTest, "NeoMSGO.UI.Map.FittedCanvasAndEntrances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRespawnGridRectTest, "NeoMSGO.UI.Map.RespawnGridRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapRectCaptureTest, "NeoMSGO.UI.Map.RectangularOverview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapCaptureUnlitOverviewTest, "NeoMSGO.UI.Map.OverviewUnlitOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapClickResolutionTest, "NeoMSGO.UI.Map.ClickResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapCaptureDeployPolicyTest, "NeoMSGO.UI.Map.CaptureDeployPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapCaptureMarkerPolicyTest, "NeoMSGO.UI.Map.CaptureMarkerPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeploymentSpawnPointTeamFilterTest, "NeoMSGO.UI.Deployment.SpawnPointTeamFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeploymentReferenceLayoutAndMSListTest, "NeoMSGO.UI.Deployment.ReferenceLayoutAndMSList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeploymentDeployClosesFrontendTest, "NeoMSGO.UI.Deployment.DeployClosesFrontend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MSMapWidgetTestP86
{
	struct FScopedMapWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMapWidgetTestP86World"));
		FScopedMapWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedMapWorld()
		{
			// EndPlay before CleanupWorld so the capture subsystem and its warmup
			// ticker are released at the same lifecycle boundary as a real map.
			if (World && World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			if (GEngine && World) GEngine->DestroyWorldContext(World);
			if (World) World->DestroyWorld(false);
		}
	};
}

bool FMSMapWaterProxyFiniteTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	UStaticMesh* WaterMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/NewYorkTaigan/Geometry/SM_NYT_Water.SM_NYT_Water"));
	if (!TestNotNull(TEXT("real Taigan water mesh is installed"), WaterMesh)) return false;
	AStaticMeshActor* Source = Scope.World->SpawnActor<AStaticMeshActor>();
	ASceneCapture2D* CaptureActor = Scope.World->SpawnActor<ASceneCapture2D>();
	UMSMapCaptureSubsystem* Subsystem = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	if (!TestNotNull(TEXT("water source actor"), Source) ||
		!TestNotNull(TEXT("capture actor"), CaptureActor) ||
		!TestNotNull(TEXT("map capture subsystem"), Subsystem)) return false;
	UStaticMeshComponent* SourceMesh = Source->GetStaticMeshComponent();
	SourceMesh->SetMobility(EComponentMobility::Movable);
	SourceMesh->SetStaticMesh(WaterMesh);
	Source->SetActorTransform(FTransform(FRotator(0.0f, 12.0f, 0.0f),
		FVector(800.0f, -1200.0f, 60.0f), FVector(1.5f, 0.8f, 1.0f)));
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	if (!TestNotNull(TEXT("capture component"), Capture)) return false;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		Subsystem->ConfigureOverviewWaterForTesting(Capture);
		int32 MeshActorCount = 0;
		int32 ProxyCount = 0;
		for (TActorIterator<AStaticMeshActor> It(Scope.World); It; ++It)
		{
			const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
			if (!Component || Component->GetStaticMesh() != WaterMesh) continue;
			++MeshActorCount;
			if (It->Tags.Contains(TEXT("MSMapOverviewWaterProxy")))
			{
				++ProxyCount;
				TestEqual(TEXT("proxy mobility allows actor transform"),
					Component->GetMobility(), EComponentMobility::Movable);
				TestTrue(TEXT("proxy matches original source transform"),
					It->GetActorTransform().Equals(Source->GetActorTransform(), 0.01f));
				TestEqual(TEXT("proxy has no gameplay collision"),
					Component->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			}
		}
		TestEqual(*FString::Printf(TEXT("attempt %d: actual water actor iterator stays finite"), Attempt),
			MeshActorCount, 2);
		TestEqual(*FString::Printf(TEXT("attempt %d: exactly one map-only proxy"), Attempt),
			ProxyCount, 1);
		TestEqual(TEXT("source-to-proxy cache remains one-to-one"),
			Subsystem->GetOverviewWaterProxyCountForTesting(), 1);
	}
	return true;
}

bool FMSMapRadarProjectionTest::RunTest(const FString&)
{
	const FVector2D MapSize(700.0f);
	const FVector2D HalfExtents(135550.0f, 210000.0f);
	const FVector WorldCenter(300.0f, -700.0f, 0.0f);
	const FVector RadarPosition = WorldCenter + FVector(-50000.0f, 80000.0f, 0.0f);
	const FVector2D Projected = UMSMapRadarRangeWidget::ProjectCenterUnclamped(
		RadarPosition, WorldCenter, HalfExtents, MapSize, false);
	TestTrue(TEXT("radar and map dots use the same rectangular projection"),
		Projected.Equals(UMSMapWidgetBase::WorldToMapLocalRectCentered(RadarPosition,
			WorldCenter, HalfExtents, MapSize, 0.0f), 0.01f));
	const float RadiusPx = 48000.0f * UMSMapRadarRangeWidget::OverviewPixelsPerCm(HalfExtents, MapSize);
	TestTrue(TEXT("480m radar radius is correctly scaled on 4.2km overview"),
		FMath::IsNearlyEqual(RadiusPx, 80.0f, 0.01f));
	const FVector2D Offscreen = UMSMapRadarRangeWidget::ProjectCenterUnclamped(
		WorldCenter + FVector(0.0f, 300000.0f, 0.0f), WorldCenter, HalfExtents, MapSize, false);
	TestTrue(TEXT("off-map radar centre is not clamped onto the image edge"), Offscreen.X < 0.0f);
	const FVector2D Content = UMSMapWidgetBase::GetRectContentSize(HalfExtents, MapSize, 0.0f);
	for (const TArray<FVector2D>& Path : UMSMapRadarRangeWidget::BuildClippedRingPaths(
		FVector2D(350.0f + Content.X * 0.5f - 30.0f, 350.0f), RadiusPx, MapSize, Content, false))
	for (const FVector2D& Point : Path)
	{
		TestTrue(TEXT("overview radar segments stay inside playable rectangle"),
			FMath::Abs(Point.X - 350.0f) <= Content.X * 0.5f + KINDA_SMALL_NUMBER &&
			FMath::Abs(Point.Y - 350.0f) <= Content.Y * 0.5f + KINDA_SMALL_NUMBER);
	}
	const FVector2D LocalSize(256.0f);
	const FVector2D LocalCenter = UMSMapRadarRangeWidget::ProjectCenterUnclamped(
		FVector(0.0f, 90000.0f, 0.0f), FVector::ZeroVector, FVector2D(100000.0f), LocalSize, true);
	TestTrue(TEXT("local radar matches north-up marker orientation"), LocalCenter.Equals(FVector2D(12.8f, 128.0f), 0.01f));
	const FVector2D LocalMarker = UMSMapWidgetBase::WorldToMapLocalRectCentered(
		FVector(0.0f, 90000.0f, 0.0f), FVector::ZeroVector,
		FVector2D(100000.0f), LocalSize, 0.0f);
	TestTrue(TEXT("local radar and map marker share a north-up position"), LocalCenter.Equals(LocalMarker, 0.01f));
	int32 LocalSegments = 0;
	for (const TArray<FVector2D>& Path : UMSMapRadarRangeWidget::BuildClippedRingPaths(
		LocalCenter, 48000.0f * 256.0f / 200000.0f, LocalSize, LocalSize, true))
	for (const FVector2D& Point : Path)
	{
		++LocalSegments;
		TestTrue(TEXT("local radar line remains under 1000m circular mask"),
			(Point - LocalSize * 0.5f).SizeSquared() <= FMath::Square(125.0f) + 0.01f);
	}
	TestTrue(TEXT("local radar intersects the circular viewport"), LocalSegments > 0);
	return true;
}

bool FMSMapGrid500mCaptureTest::RunTest(const FString&)
{
	// Grid500m importer: image width is world Y (7 km), height is world X (4.5 km).
	const FVector2D HalfExtents(225000.0f, 350000.0f);
	const FVector2D FullMapSize(700.0f);
	const FVector2D FullContent=UMSMapWidgetBase::GetRectContentSize(HalfExtents,FullMapSize,0.0f);
	TestTrue(TEXT("7km overview keeps a 700 by 450 pixel playfield"),
		FullContent.Equals(FVector2D(700.0f,450.0f),0.01f));
	const FVector2D North=UMSMapWidgetBase::WorldToMapLocalRectCentered(
		FVector(-225000,0,0),FVector::ZeroVector,HalfExtents,FullMapSize,0.0f);
	const FVector2D West=UMSMapWidgetBase::WorldToMapLocalRectCentered(
		FVector(0,350000,0),FVector::ZeroVector,HalfExtents,FullMapSize,0.0f);
	TestTrue(TEXT("7km map edge markers align with the rectangular image, not the square frame"),
		North.Equals(FVector2D(350.0f,125.0f),0.01f) &&
		West.Equals(FVector2D(0.0f,350.0f),0.01f));
	const FVector2D HUDContent=UMSMapWidgetBase::GetRectContentSize(HalfExtents,FVector2D(256.0f),0.0f);
	TestTrue(TEXT("N overview keeps 7 to 4.5 ratio inside the 256px HUD slot"),
		HUDContent.Equals(FVector2D(256.0f,256.0f*4.5f/7.0f),0.01f));
	TestTrue(TEXT("480m radar circle scales to 48px on a 700px-wide 7km map"),
		FMath::IsNearlyEqual(48000.0f*UMSMapRadarRangeWidget::OverviewPixelsPerCm(
			HalfExtents,FullMapSize),48.0f,0.01f));
	const FBox2d Respawn=UMSHUDWidget::GetRespawnMapContentRect(HalfExtents);
	TestTrue(TEXT("respawn content remains 14 by 9 equal 500m cells"),
		FMath::IsNearlyEqual(Respawn.GetSize().X/14.0,Respawn.GetSize().Y/9.0,0.01));
	const FVector2D RespawnNorth=UMSMapWidgetBase::WorldToMapLocalRectCentered(
		FVector(-225000,0,0),FVector::ZeroVector,HalfExtents,FVector2D(468.0f),0.0f);
	const FVector2D RadarNorth=UMSMapRadarRangeWidget::ProjectCenterUnclamped(
		FVector(-225000,0,0),FVector::ZeroVector,HalfExtents,FVector2D(468.0f),false);
	TestTrue(TEXT("respawn point and radar projection meet the image's north edge"),
		RespawnNorth.Equals(RadarNorth,0.01f) &&
		FMath::IsNearlyEqual(RespawnNorth.Y+309.0,Respawn.Min.Y,0.01));
	TestTrue(TEXT("480m radar circle shares the respawn content scale"),
		FMath::IsNearlyEqual(48000.0f*UMSMapRadarRangeWidget::OverviewPixelsPerCm(
			HalfExtents,FVector2D(468.0f)),468.0f*48000.0f/700000.0f,0.01f));
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSMapBoundsActor* Bounds=Scope.World->SpawnActor<AMSMapBoundsActor>();
	Bounds->WorldHalfExtentCm=HalfExtents.X;
	Bounds->WorldHalfExtentYCm=HalfExtents.Y;
	UMSMapCaptureSubsystem* Capture=Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	TestEqual(TEXT("Grid500m bounds preserve separate world axes"),
		Capture->GetMapWorldHalfExtents(),HalfExtents);
	if (UTextureRenderTarget2D* RT=Capture->GetOverviewTexture())
	{
		TestEqual(TEXT("7km world Y fills the capture width"),RT->SizeX,1024);
		TestTrue(TEXT("4.5km world X sets capture height"),
			FMath::Abs(RT->SizeY-FMath::RoundToInt(1024.0f*4.5f/7.0f))<=1);
		const ASceneCapture2D* Actor=Capture->GetCaptureActorForTesting();
		if (TestNotNull(TEXT("Grid500m overview capture exists"),Actor))
			TestTrue(TEXT("Grid500m camera spans the whole 7km width"),
				FMath::IsNearlyEqual(Actor->GetCaptureComponent2D()->OrthoWidth,700000.0f,0.1f));
	}
	else if(FApp::CanEverRender())AddError(TEXT("Grid500m overview render target missing"));
	return true;
}

bool FMSMapUIFitEntrancesTest::RunTest(const FString&)
{
	const FVector2D HalfExtents(225000.0f,350000.0f);
	const FVector2D HUDSize=UMSHUDWidget::GetOverviewMinimapCanvasSize(HalfExtents);
	TestTrue(TEXT("HUD overview frame matches the 14:9 atlas instead of retaining a square gap"),
		HUDSize.Equals(FVector2D(256.0f,256.0f*9.0f/14.0f),0.01f));
	const FVector2D FullHD=UMSFullscreenMapWidget::GetLayoutMapSize(FVector2D(1920.0f,1080.0f));
	const FVector2D HD=UMSFullscreenMapWidget::GetLayoutMapSize(FVector2D(1280.0f,720.0f));
	TestTrue(TEXT("1920x1080 full map occupies a large 14:9 playfield"),FullHD.Equals(FVector2D(1260.0f,810.0f),0.01f));
	TestTrue(TEXT("1280x720 full map scales inside the viewport"),HD.Equals(FVector2D(840.0f,540.0f),0.01f));
	TestTrue(TEXT("full map image fills its map canvas without distortion or padding"),
		UMSMapWidgetBase::GetRectContentSize(HalfExtents,FullHD,0.0f).Equals(FullHD,0.01f));
	const FVector2D North=UMSMapWidgetBase::WorldToMapLocalRectCentered(
		FVector(-225000.0f,0.0f,0.0f),FVector::ZeroVector,HalfExtents,FullHD,0.0f);
	const FVector2D RadarNorth=UMSMapRadarRangeWidget::ProjectCenterUnclamped(
		FVector(-225000.0f,0.0f,0.0f),FVector::ZeroVector,HalfExtents,FullHD,false);
	TestTrue(TEXT("fullscreen marker and radar agree on the new top edge"),
		North.Equals(FVector2D(FullHD.X*.5f,0.0f),0.01f)&&North.Equals(RadarNorth,0.01f));
	const FVector2D BriefingContent=UMSMapWidgetBase::GetRectContentSize(HalfExtents,FVector2D(208.0f,204.0f),0.0f);
	const float SmallHome=UMSMapWidgetBase::GetCaptureMarkerSizeForCanvas(MSMapPalette::HomeBaseMarkerSizePx,BriefingContent);
	const float SmallCapture=UMSMapWidgetBase::GetCaptureMarkerSizeForCanvas(MSMapPalette::CapturePointMarkerSizePx,BriefingContent);
	TestTrue(TEXT("briefing's six base/point symbols shrink with its visible 208px map"),
		SmallHome>=18.0f&&SmallHome<=21.0f&&SmallCapture>=14.0f&&SmallCapture<=17.0f);
	for(uint8 Tier=0;Tier<=2;++Tier)
	{
		const FLinearColor Blue=UMSMapWidgetBase::FriendlyDotColor(Tier);
		TestTrue(TEXT("all allied tier dots remain blue"),Blue.B>Blue.G&&Blue.B>Blue.R);
	}
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	for(int32 Number=1;Number<=4;++Number)
	{
		for(const bool bBottom:{false,true})
		{
			ATargetPoint* Point=Scope.World->SpawnActor<ATargetPoint>();
			Point->Tags.Add(FName(TEXT("NYT_NavEntrance")));
			Point->Tags.Add(FName(*FString::Printf(TEXT("NYT_NavEntry_%02d"),Number)));
			Point->Tags.Add(bBottom?FName(TEXT("NYT_NavBottom")):FName(TEXT("NYT_NavTop")));
			Point->SetActorLocation(FVector(Number*1000.0f,Number*2000.0f,bBottom?-6390.0f:10.0f));
		}
	}
	const auto Surface=UMSMapWidgetBase::GatherEntranceLocations(Scope.World,false);
	const auto Underground=UMSMapWidgetBase::GatherEntranceLocations(Scope.World,true);
	TestEqual(TEXT("only the four tagged surface entrances are drawn"),Surface.Num(),4);
	TestEqual(TEXT("only the four tagged underground exits are drawn"),Underground.Num(),4);
	if(Surface.Num()==4&&Underground.Num()==4)
	{
		TestEqual(TEXT("entry numbering follows saved tags, not actor iteration order"),Surface[3].Number,4);
		TestEqual(TEXT("surface uses top target point"),Surface[0].Location.Z,10.0);
		TestEqual(TEXT("underground uses bottom target point"),Underground[0].Location.Z,-6390.0);
	}
	ATargetPoint* Untagged=Scope.World->SpawnActor<ATargetPoint>();
	Untagged->Tags.Add(FName(TEXT("NYT_NavEntrance")));
	Untagged->Tags.Add(FName(TEXT("NYT_NavTop")));
	Untagged->Tags.Add(FName(TEXT("NYT_NavEntry_05")));
	TestEqual(TEXT("no fifth entrance is inferred"),UMSMapWidgetBase::GatherEntranceLocations(Scope.World,false).Num(),4);
	return true;
}

bool FMSRespawnGridRectTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSMapBoundsActor* Bounds=Scope.World->SpawnActor<AMSMapBoundsActor>();
	Bounds->WorldHalfExtentCm=225000.0f;
	Bounds->WorldHalfExtentYCm=350000.0f;
	AMSPlayerCharacter* Player=Scope.World->SpawnActor<AMSPlayerCharacter>();
	APlayerController* Controller=Scope.World->SpawnActor<APlayerController>();
	Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	Controller->Possess(Player);
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);
	VM->Initialize(Controller);
	UMSHUDWidget* HUD=CreateWidget<UMSHUDWidget>(Controller);
	if(!TestNotNull(TEXT("respawn HUD exists"),HUD))return false;
	HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
	if(!TestNotNull(TEXT("video respawn panel exists"),HUD->RespawnPanel.Get()))return false;
	Player->GetRespawnComponent()->SetAutoRespawnEnabled(false);
	Player->GetVitalSet()->OnDeath.Broadcast(Player,nullptr);
	if(!TestTrue(TEXT("death fixture starts the production respawn timer"),
		Player->GetRespawnComponent()->IsRespawnPending()))return false;
	HUD->RefreshRespawnPanel();
	TestEqual(TEXT("respawn HUD refresh actually became visible"),
		HUD->RespawnPanel->GetVisibility(),ESlateVisibility::HitTestInvisible);
	// SetDesignSlot stores design coordinates in canvas anchors, leaving Position,
	// Size and Offsets at zero. Resolve them at the authored 1920x1080 viewport.
	auto DesignRect=[](const UCanvasPanelSlot* Slot)
	{
		const FAnchors Anchors=Slot->GetAnchors();
		return FBox2d(FVector2d(Anchors.Minimum.X*1920.0,Anchors.Minimum.Y*1080.0),
			FVector2d(Anchors.Maximum.X*1920.0,Anchors.Maximum.Y*1080.0));
	};
	auto LogSlot=[this,&DesignRect](const TCHAR* Name,const UCanvasPanelSlot* Slot)
	{
		const FAnchors Anchors=Slot->GetAnchors();
		const FMargin Offsets=Slot->GetOffsets();
		const FBox2d Actual=DesignRect(Slot);
		AddInfo(FString::Printf(TEXT("%s anchors=(%.6f,%.6f)-(%.6f,%.6f) offsets=(%.2f,%.2f,%.2f,%.2f) design=(%.2f,%.2f)-(%.2f,%.2f)"),
			Name,Anchors.Minimum.X,Anchors.Minimum.Y,Anchors.Maximum.X,Anchors.Maximum.Y,
			Offsets.Left,Offsets.Top,Offsets.Right,Offsets.Bottom,
			Actual.Min.X,Actual.Min.Y,Actual.Max.X,Actual.Max.Y));
	};
	const FBox2d Rect=UMSHUDWidget::GetRespawnMapContentRect(FVector2D(225000.0f,350000.0f));
	const FVector2D Min=Rect.Min,Size=Rect.GetSize();
	const UCanvasPanelSlot* ImageSlot=CastChecked<UCanvasPanelSlot>(HUD->RespawnTacticalMapImage->Slot);
	LogSlot(TEXT("image"),ImageSlot);
	AddInfo(FString::Printf(TEXT("expected content design=(%.2f,%.2f)-(%.2f,%.2f)"),
		Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y));
	const FBox2d ActualImage=DesignRect(ImageSlot);
	TestTrue(TEXT("rendered respawn image starts at shared content origin"),
		ActualImage.Min.Equals(Min,0.01f) && ActualImage.GetSize().Equals(Size,0.01f));
	if(!TestEqual(TEXT("14 columns have 13 internal lines"),HUD->RespawnGridVerticalLines.Num(),13) ||
		!TestEqual(TEXT("9 rows have 8 internal lines"),HUD->RespawnGridHorizontalLines.Num(),8) ||
		!TestEqual(TEXT("14 column labels exist"),HUD->RespawnGridColumnLabels.Num(),14) ||
		!TestEqual(TEXT("9 row labels exist"),HUD->RespawnGridRowLabels.Num(),9))return false;
	const UCanvasPanelSlot* FirstColumn=CastChecked<UCanvasPanelSlot>(HUD->RespawnGridVerticalLines[0]->Slot);
	const UCanvasPanelSlot* LastRow=CastChecked<UCanvasPanelSlot>(HUD->RespawnGridHorizontalLines.Last()->Slot);
	LogSlot(TEXT("vertical-first"),FirstColumn);
	LogSlot(TEXT("horizontal-last"),LastRow);
	const FBox2d ActualColumn=DesignRect(FirstColumn);
	const FBox2d ActualRow=DesignRect(LastRow);
	TestTrue(TEXT("vertical grid line spans content height only"),
		ActualColumn.Min.Equals(Min+FVector2D(Size.X/14.0f,0),0.01f) &&
		FMath::IsNearlyEqual(ActualColumn.GetSize().Y,Size.Y,0.01f));
	TestTrue(TEXT("horizontal grid line spans content width only"),
		ActualRow.Min.Equals(Min+FVector2D(0,Size.Y*8.0f/9.0f),0.01f) &&
		FMath::IsNearlyEqual(ActualRow.GetSize().X,Size.X,0.01f));
	const UCanvasPanelSlot* FirstColumnLabel=CastChecked<UCanvasPanelSlot>(HUD->RespawnGridColumnLabels[0]->Slot);
	const UCanvasPanelSlot* LastRowLabel=CastChecked<UCanvasPanelSlot>(HUD->RespawnGridRowLabels.Last()->Slot);
	LogSlot(TEXT("column-label-first"),FirstColumnLabel);
	LogSlot(TEXT("row-label-last"),LastRowLabel);
	const FBox2d ActualColumnLabel=DesignRect(FirstColumnLabel);
	const FBox2d ActualRowLabel=DesignRect(LastRowLabel);
	TestTrue(TEXT("column labels track the content top"),
		ActualColumnLabel.Min.Equals(Min+FVector2D(0,-18.0f),0.01f) &&
		FMath::IsNearlyEqual(ActualColumnLabel.GetSize().X,Size.X/14.0f,0.01f));
	TestTrue(TEXT("row labels track the centre of their content cell"),
		ActualRowLabel.Min.Equals(Min+FVector2D(-24.0f,Size.Y*8.5f/9.0f-12.0f),0.01f));
	return true;
}

bool FMSMapCoordinateTest::RunTest(const FString&)
{
	const FVector2D Size(MSMapPalette::DeploymentMapSizePx);
	TestTrue(TEXT("origin maps to center"), UMSMapWidgetBase::WorldToMapLocal(FVector::ZeroVector,
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(Size * 0.5f, KINDA_SMALL_NUMBER));
	// WorldScale 10x (W6): 境界での正規化は WorldHalfExtentCm 自体を座標として渡すため、
	// 定数値がMSScale適用でいくつになっても結果(マージンまでの位置)は不変。
	// #229: 地図は北上表示。Tools/RealWorld の生成規約でワールド +X = 地理的な南、+Y = 西なので、
	// 画面上に来るのは -X(北)、画面右に来るのは -Y(東)になる。
	TestTrue(TEXT("-X (geographic north) maps up to the margin"), UMSMapWidgetBase::WorldToMapLocal(FVector(-MSMapPalette::WorldHalfExtentCm, 0, 0),
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(FVector2D(380, 24), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("+X (geographic south) maps down to the margin"), UMSMapWidgetBase::WorldToMapLocal(FVector(MSMapPalette::WorldHalfExtentCm, 0, 0),
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(FVector2D(380, 736), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("-Y (geographic east) maps right to the margin"), UMSMapWidgetBase::WorldToMapLocal(FVector(0, -MSMapPalette::WorldHalfExtentCm, 0),
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(FVector2D(736, 380), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("+Y (geographic west) maps left to the margin"), UMSMapWidgetBase::WorldToMapLocal(FVector(0, MSMapPalette::WorldHalfExtentCm, 0),
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(FVector2D(24, 380), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("out of range clamps to both edges"), UMSMapWidgetBase::WorldToMapLocal(FVector(9999999, -9999999, 0),
		MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx).Equals(FVector2D(736, 736), KINDA_SMALL_NUMBER));
	for (const FVector Sample : { FVector(-(MSMapPalette::WorldHalfExtentCm - 1.0f), -17321, 0), FVector(1234, 9876, 0),
		FVector(MSMapPalette::WorldHalfExtentCm, MSMapPalette::WorldHalfExtentCm, 0) })
	{
		const FVector RoundTrip = UMSMapWidgetBase::MapLocalToWorld(UMSMapWidgetBase::WorldToMapLocal(Sample,
			MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx), MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
		TestTrue(*FString::Printf(TEXT("round trip is under 1cm: %s"), *Sample.ToString()), FVector::Dist2D(Sample, RoundTrip) < 1.0f);
	}
	// The Taigan playfield is wider along world Y. Terrain pixels and markers
	// must share this rectangular projection inside the map's square frame.
	const FVector2D RectHalfExtents(135550.0f, 210000.0f);
	const FVector2D RectMapSize(700.0f);
	const FVector RectCenter(1400.0f, -2800.0f, 0.0f);
	const FVector2D RectTop = UMSMapWidgetBase::WorldToMapLocalRectCentered(
		RectCenter + FVector(-RectHalfExtents.X, 0.0f, 0.0f), RectCenter,
		RectHalfExtents, RectMapSize, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("rectangular north edge is inset to the real playfield height"),
		FMath::IsNearlyEqual(RectTop.X, 350.0f, 0.01f) && RectTop.Y > MSMapPalette::EdgeMarginPx);
	const FVector2D RectWest = UMSMapWidgetBase::WorldToMapLocalRectCentered(
		RectCenter + FVector(0.0f, RectHalfExtents.Y, 0.0f), RectCenter,
		RectHalfExtents, RectMapSize, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("rectangular west edge reaches the horizontal margin"),
		RectWest.Equals(FVector2D(MSMapPalette::EdgeMarginPx, 350.0f), 0.01f));
	const FVector RectSample = RectCenter + FVector(-56000.0f, 147000.0f, 0.0f);
	const FVector RectRoundTrip = UMSMapWidgetBase::MapLocalToWorldRectCentered(
		UMSMapWidgetBase::WorldToMapLocalRectCentered(RectSample, RectCenter,
			RectHalfExtents, RectMapSize, MSMapPalette::EdgeMarginPx),
		RectCenter, RectHalfExtents, RectMapSize, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("rectangular marker and map click agree within 1cm"),
		FVector::Dist2D(RectSample, RectRoundTrip) < 1.0f);
	const FVector2D WideMapSize(960.0f, 480.0f);
	const FVector2D WideContent = UMSMapWidgetBase::GetRectContentSize(
		RectHalfExtents, WideMapSize, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("wide deployment image fits its height without stretching"),
		FMath::IsNearlyEqual(WideContent.Y, 432.0f, 0.01f) &&
		FMath::IsNearlyEqual(WideContent.X / WideContent.Y,
			RectHalfExtents.Y / RectHalfExtents.X, 0.0001f));
	const FVector2D WideMarker = UMSMapWidgetBase::WorldToMapLocalRectCentered(
		RectSample, RectCenter, RectHalfExtents, WideMapSize, MSMapPalette::EdgeMarginPx);
	const FVector WideRoundTrip = UMSMapWidgetBase::MapLocalToWorldRectCentered(
		WideMarker, RectCenter, RectHalfExtents, WideMapSize, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("wide deployment marker and click agree within 1cm"),
		FVector::Dist2D(RectSample, WideRoundTrip) < 1.0f);
	const FVector LocalCenter = RectCenter + FVector(10000.0f, 20000.0f, 0.0f);
	const FBox2d LocalUV = UMSMapWidgetBase::GetLocalAtlasUVRegion(LocalCenter, RectCenter,
		RectHalfExtents, 200000.0f);
	const FVector2d LocalUVMid = (LocalUV.Min + LocalUV.Max) * 0.5;
	TestTrue(TEXT("underground crop is centred at the same world coordinate as its map marker"),
		FMath::IsNearlyEqual(LocalUVMid.X, 0.5 - 20000.0 / 420000.0, 0.00001) &&
		FMath::IsNearlyEqual(LocalUVMid.Y, 0.5 + 10000.0 / 271100.0, 0.00001));
	TestTrue(TEXT("underground local crop spans exactly the 1000 metre radial field"),
		FMath::IsNearlyEqual(LocalUV.Max.X - LocalUV.Min.X, 200000.0 / 420000.0, 0.00001) &&
		FMath::IsNearlyEqual(LocalUV.Max.Y - LocalUV.Min.Y, 200000.0 / 271100.0, 0.00001));
	const FBox2d ZeroExtentUV = UMSMapWidgetBase::GetLocalAtlasUVRegion(LocalCenter, RectCenter,
		FVector2D::ZeroVector, 200000.0f);
	TestTrue(TEXT("invalid map extents fall back to the uncropped atlas"),
		ZeroExtentUV.Min.Equals(FVector2d(0, 0)) && ZeroExtentUV.Max.Equals(FVector2d(1, 1)));

	// #229: SceneCapture が使う回転定数そのものが北上であることを、RHI に依存せず検証する。
	{
		const FRotationMatrix CaptureRotation(FRotator(-90.0f, MSMapPalette::WorldNorthYawDeg, 0.0f));
		const FVector CaptureUp = CaptureRotation.GetUnitAxis(EAxis::Z);
		const FVector CaptureRight = CaptureRotation.GetUnitAxis(EAxis::Y);
		TestTrue(TEXT("capture rotation looks down"), CaptureRotation.GetUnitAxis(EAxis::X).Equals(FVector(0, 0, -1), 0.001f));
		const FVector2D Middle = UMSMapWidgetBase::WorldToMapLocal(FVector::ZeroVector,
			MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
		const FVector2D Up = UMSMapWidgetBase::WorldToMapLocal(CaptureUp * (MSMapPalette::WorldHalfExtentCm * 0.5f),
			MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
		const FVector2D Right = UMSMapWidgetBase::WorldToMapLocal(CaptureRight * (MSMapPalette::WorldHalfExtentCm * 0.5f),
			MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
		TestTrue(TEXT("capture up matches widget up"), Up.Y < Middle.Y - 1.0f && FMath::IsNearlyEqual(Up.X, Middle.X, 0.01f));
		TestTrue(TEXT("capture right matches widget right"), Right.X > Middle.X + 1.0f && FMath::IsNearlyEqual(Right.Y, Middle.Y, 0.01f));
	}
	return true;
}

bool FMSMapRectCaptureTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSMapBoundsActor* Bounds = Scope.World->SpawnActor<AMSMapBoundsActor>();
	Bounds->WorldHalfExtentCm = 135550.0f;
	Bounds->WorldHalfExtentYCm = 210000.0f;
	UMSMapCaptureSubsystem* Subsystem = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	TestEqual(TEXT("authored X/Y playfield extents are kept separately"),
		Subsystem->GetMapWorldHalfExtents(), FVector2D(135550.0f, 210000.0f));
	TestEqual(TEXT("legacy scalar extent remains the maximum axis"),
		Subsystem->GetMapWorldHalfExtent(), 210000.0f);
	UTextureRenderTarget2D* Overview = Subsystem->GetOverviewTexture();
	if (!FApp::CanEverRender()) return true;
	if (!TestNotNull(TEXT("rectangular overview texture exists"), Overview)) return false;
	TestEqual(TEXT("long world Y axis fills image width"), Overview->SizeX, 1024);
	TestTrue(TEXT("short world X axis trims the image height"),
		FMath::Abs(Overview->SizeY - FMath::RoundToInt(1024.0f * 135550.0f / 210000.0f)) <= 1);
	const ASceneCapture2D* Actor = Subsystem->GetCaptureActorForTesting();
	const USceneCaptureComponent2D* Capture = Actor ? Actor->GetCaptureComponent2D() : nullptr;
	if (!TestNotNull(TEXT("rectangular scene capture exists"), Capture)) return false;
	TestTrue(TEXT("camera horizontal span matches world Y playfield"),
		FMath::IsNearlyEqual(Capture->OrthoWidth, 420000.0f, 0.1f));
	return true;
}

bool FMSMapCaptureMarkerPolicyTest::RunTest(const FString&)
{
	TestEqual(TEXT("fallback point number is one based"), UMSMapWidgetBase::ResolveCapturePointNumber(nullptr, 2).ToString(), FString(TEXT("3")));
	TestTrue(TEXT("home-base marker is larger than a normal point"), MSMapPalette::HomeBaseMarkerSizePx > MSMapPalette::CapturePointMarkerSizePx);
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSCapturePoint* Friendly = Scope.World->SpawnActor<AMSCapturePoint>(); Friendly->SetFixedOwner(EMSCaptureOwner::TeamPlayer);
	AMSCapturePoint* Hostile = Scope.World->SpawnActor<AMSCapturePoint>(); Hostile->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
	AMSCapturePoint* Neutral = Scope.World->SpawnActor<AMSCapturePoint>(); Neutral->SetFixedOwner(EMSCaptureOwner::Neutral);
	TestEqual(TEXT("friendly point is blue"), UMSMapWidgetBase::ResolveCapturePointColor(Friendly, MSTeam::PlayerTeamId()), MSMapPalette::Friendly);
	TestEqual(TEXT("hostile point is red"), UMSMapWidgetBase::ResolveCapturePointColor(Hostile, MSTeam::PlayerTeamId()), MSMapPalette::Enemy);
	TestEqual(TEXT("neutral point is grey"), UMSMapWidgetBase::ResolveCapturePointColor(Neutral, MSTeam::PlayerTeamId()), MSMapPalette::Neutral);
	AMSHomeBase* Home = Scope.World->SpawnActor<AMSHomeBase>();
	if (AMSDestructibleTarget* Core = Home ? Home->GetCoreTarget() : nullptr)
	{
		Core->GetVitalSet()->OnDeath.Broadcast(Core, nullptr);
		TestEqual(TEXT("destroyed home base is dark grey"), UMSMapWidgetBase::ResolveCapturePointColor(Home, MSTeam::PlayerTeamId()), MSMapPalette::Destroyed);
	}
	else AddError(TEXT("home base did not create its destructible core"));
	return true;
}

bool FMSMapCaptureSubsystemTest::RunTest(const FString&)
{
	{
		MSMapWidgetTestP86::FScopedMapWorld Scope;
		UMSMapCaptureSubsystem* Subsystem = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
		TestNotNull(TEXT("map capture subsystem exists"), Subsystem);
		if (Subsystem)
		{
			TestEqual(TEXT("missing bounds actor falls back to origin"), Subsystem->GetMapWorldCenter(), FVector::ZeroVector);
			TestEqual(TEXT("missing bounds actor falls back to MSMapPalette::WorldHalfExtentCm (20500cm x MSScale)"),
				Subsystem->GetMapWorldHalfExtent(), MSMapPalette::WorldHalfExtentCm);
		}
	}

	{
		// #233: without an authored actor, loaded world geometry determines the map
		// range. These meshes model opposite terrain edges of a 10 km map.
		MSMapWidgetTestP86::FScopedMapWorld Scope;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		TestNotNull(TEXT("engine cube exists for measured-bounds test"), Cube);
		if (Cube)
		{
			for (const float X : { -450000.0f, 450000.0f })
			{
				AStaticMeshActor* Tile = Scope.World->SpawnActor<AStaticMeshActor>(
					FVector(X, 0.0f, 0.0f), FRotator::ZeroRotator);
				Tile->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Tile->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Tile->SetActorScale3D(FVector(1000.0f, 1000.0f, 1.0f));
				Tile->GetStaticMeshComponent()->UpdateBounds();
			}

			UMSMapCaptureSubsystem* Measured =
				Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
			TestNotNull(TEXT("map capture subsystem exists for geometry measurement"), Measured);
			if (Measured)
			{
				TestTrue(TEXT("measured geometry covers the full 10 km map plus margin"),
					Measured->GetMapWorldHalfExtent() > 500000.0f);
				TestTrue(TEXT("symmetric terrain keeps the measured centre at origin"),
					Measured->GetMapWorldCenter().Equals(FVector::ZeroVector, 1.0f));
			}
		}
	}

	{
		// #233: 境界アクタが無いレベル(ダイバオ)ではフォールバック定数が実マップより
		// 遥かに狭く、遠方の拠点が縁にクランプされていた。境界アクタが無い場合は
		// 実コンテンツから実測した範囲を使い、全拠点が収まることを保証する。
		MSMapWidgetTestP86::FScopedMapWorld Scope;
		Scope.World->SpawnActor<AMSHomeBase>(FVector(450000.0f, -450000.0f, 600.0f), FRotator::ZeroRotator);
		Scope.World->SpawnActor<AMSHomeBase>(FVector(-120000.0f, 90000.0f, 600.0f), FRotator::ZeroRotator);
		UMSMapCaptureSubsystem* Measured = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
		TestNotNull(TEXT("map capture subsystem exists without a bounds actor"), Measured);
		if (Measured)
		{
			const float HalfExtent = Measured->GetMapWorldHalfExtent();
			const FVector Center = Measured->GetMapWorldCenter();
			TestTrue(TEXT("measured fallback is wider than the conservative constant"),
				HalfExtent > MSMapPalette::WorldHalfExtentCm);
			for (const FVector Point : { FVector(450000.0f, -450000.0f, 600.0f), FVector(-120000.0f, 90000.0f, 600.0f) })
			{
				TestTrue(*FString::Printf(TEXT("home base fits inside the measured extent: %s"), *Point.ToString()),
					FMath::Abs(Point.X - Center.X) <= HalfExtent && FMath::Abs(Point.Y - Center.Y) <= HalfExtent);
			}
		}
	}

	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSMapBoundsActor* Bounds = Scope.World->SpawnActor<AMSMapBoundsActor>(FVector(1200.0f, -3400.0f, 500.0f), FRotator::ZeroRotator);
	Bounds->WorldHalfExtentCm = 32100.0f;
	Bounds->CaptureHeightCm = 70000.0f;
	TestFalse(TEXT("authored overview capture remains lit by default"), Bounds->UsesUnlitOverview());
	UMSMapCaptureSubsystem* Subsystem = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	TestNotNull(TEXT("map capture subsystem exists with authored bounds"), Subsystem);
	if (!Subsystem) return false;
	TestEqual(TEXT("authored map centre is returned"), Subsystem->GetMapWorldCenter(), Bounds->GetActorLocation());
	TestEqual(TEXT("authored map half extent is returned"), Subsystem->GetMapWorldHalfExtent(), 32100.0f);

	UTextureRenderTarget2D* First = Subsystem->GetOverviewTexture();
	UTextureRenderTarget2D* Second = Subsystem->GetOverviewTexture();
	TestEqual(TEXT("overview render target is cached"), Second, First);
	TestEqual(TEXT("capture is requested only once"), Subsystem->GetCaptureRequestCountForTesting(), 1);
	if (!FApp::CanEverRender())
	{
		// -NullRHI intentionally has no render resources. The subsystem must cache the
		// failed attempt and let map widgets use their solid-colour fallback.
		TestNull(TEXT("NullRHI returns no overview render target"), First);
		TestNull(TEXT("NullRHI does not spawn a capture actor"), Subsystem->GetCaptureActorForTesting());
		return true;
	}

	TestNotNull(TEXT("overview capture creates a render target"), First);
	if (First)
	{
		TestEqual(TEXT("overview width is 1024"), First->SizeX, 1024);
		TestEqual(TEXT("overview height is 1024"), First->SizeY, 1024);
	}
	const ASceneCapture2D* CaptureActor = Subsystem->GetCaptureActorForTesting();
	const USceneCaptureComponent2D* Capture = CaptureActor ? CaptureActor->GetCaptureComponent2D() : nullptr;
	TestNotNull(TEXT("one-shot scene capture actor exists"), Capture);
	if (Capture)
	{
		TestFalse(TEXT("overview capture does not capture every frame"), Capture->bCaptureEveryFrame);
		TestFalse(TEXT("overview capture does not capture on movement"), Capture->bCaptureOnMovement);
		TestEqual(TEXT("overview capture uses final LDR colour"), Capture->CaptureSource, ESceneCaptureSource::SCS_FinalColorLDR);
		TestTrue(TEXT("overview capture covers authored bounds"), FMath::IsNearlyEqual(Capture->OrthoWidth, 64200.0f));
		TestTrue(TEXT("default overview capture keeps scene lighting"), Capture->ShowFlags.Lighting);
		TestTrue(TEXT("default overview capture keeps the legacy height-based view distance"),
			FMath::IsNearlyEqual(Capture->MaxViewDistanceOverride, Bounds->GetCaptureHeight() * 2.0f));
	}
	if (CaptureActor)
	{
		// #229: 撮影画像もマーカーも「北が画面上」でなければならない。真下向きカメラの
		// Up ベクトルが北(=ワールド -X)、Right が東(=ワールド -Y)であることを検証し、
		// 同じ方向がウィジェット投影でも上/右になることを突き合わせる。
		const FTransform CaptureTransform = CaptureActor->GetActorTransform();
		const FVector CaptureUp = CaptureTransform.GetUnitAxis(EAxis::Z);
		const FVector CaptureRight = CaptureTransform.GetUnitAxis(EAxis::Y);
		const FVector CaptureForward = CaptureTransform.GetUnitAxis(EAxis::X);
		TestTrue(TEXT("overview capture looks straight down"), CaptureForward.Equals(FVector(0, 0, -1), 0.001f));
		TestTrue(TEXT("overview capture up axis is geographic north (world -X)"), CaptureUp.Equals(FVector(-1, 0, 0), 0.001f));
		TestTrue(TEXT("overview capture right axis is geographic east (world -Y)"), CaptureRight.Equals(FVector(0, -1, 0), 0.001f));

		const FVector Center = Subsystem->GetMapWorldCenter();
		const float HalfExtent = Subsystem->GetMapWorldHalfExtent();
		const FVector2D Size(MSMapPalette::FullscreenMapSizePx);
		const FVector2D Middle = UMSMapWidgetBase::WorldToMapLocalCentered(Center, Center, HalfExtent, Size, MSMapPalette::EdgeMarginPx);
		const FVector2D AlongCaptureUp = UMSMapWidgetBase::WorldToMapLocalCentered(
			Center + CaptureUp * (HalfExtent * 0.5f), Center, HalfExtent, Size, MSMapPalette::EdgeMarginPx);
		const FVector2D AlongCaptureRight = UMSMapWidgetBase::WorldToMapLocalCentered(
			Center + CaptureRight * (HalfExtent * 0.5f), Center, HalfExtent, Size, MSMapPalette::EdgeMarginPx);
		TestTrue(TEXT("capture up direction projects towards the top of the widget"), AlongCaptureUp.Y < Middle.Y - 1.0f);
		TestTrue(TEXT("capture up direction does not drift horizontally"), FMath::IsNearlyEqual(AlongCaptureUp.X, Middle.X, 0.01f));
		TestTrue(TEXT("capture right direction projects towards the right of the widget"), AlongCaptureRight.X > Middle.X + 1.0f);
		TestTrue(TEXT("capture right direction does not drift vertically"), FMath::IsNearlyEqual(AlongCaptureRight.Y, Middle.Y, 0.01f));
	}
	return true;
}

bool FMSMapCaptureUnlitOverviewTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSMapBoundsActor* Bounds = Scope.World->SpawnActor<AMSMapBoundsActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("authored bounds actor exists for unlit overview"), Bounds);
	if (!Bounds) return false;

	Bounds->WorldHalfExtentCm = 300000.0f;
	Bounds->CaptureHeightCm = 50000.0f;
	Bounds->bUseUnlitOverview = true;
	TestTrue(TEXT("unlit overview is opt-in on the authored bounds"), Bounds->UsesUnlitOverview());

	UMSMapCaptureSubsystem* Subsystem = Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	TestNotNull(TEXT("map capture subsystem exists for unlit overview"), Subsystem);
	if (!Subsystem) return false;

	UTextureRenderTarget2D* Overview = Subsystem->GetOverviewTexture();
	if (!FApp::CanEverRender())
	{
		TestNull(TEXT("NullRHI does not create an overview capture"), Overview);
		TestNull(TEXT("NullRHI does not create a capture actor"), Subsystem->GetCaptureActorForTesting());
		AddInfo(TEXT("Render-specific unlit show-flag and view-distance assertions require a rendering device."));
		return true;
	}

	TestNotNull(TEXT("unlit overview capture creates a render target"), Overview);
	const ASceneCapture2D* CaptureActor = Subsystem->GetCaptureActorForTesting();
	const USceneCaptureComponent2D* Capture = CaptureActor ? CaptureActor->GetCaptureComponent2D() : nullptr;
	TestNotNull(TEXT("unlit overview capture component exists"), Capture);
	if (Capture)
	{
		TestFalse(TEXT("opt-in overview disables scene lighting"), Capture->ShowFlags.Lighting);

		const float CaptureHeight = Bounds->GetCaptureHeight();
		const float RequiredCornerDistance = FVector(
			Bounds->GetMapWorldHalfExtent(), Bounds->GetMapWorldHalfExtent(), CaptureHeight).Size() + 5000.0f;
		TestTrue(TEXT("opt-in view distance reaches the far map corner"),
			Capture->MaxViewDistanceOverride >= RequiredCornerDistance);
		TestTrue(TEXT("opt-in view distance exceeds the legacy height-only range"),
			Capture->MaxViewDistanceOverride > CaptureHeight * 2.0f);
	}
	return true;
}

bool FMSMapClickResolutionTest::RunTest(const FString&)
{
	const FVector2D Size(MSMapPalette::DeploymentMapSizePx);
	FMSDeployPointSnapshot Point; Point.MapLocal = FVector2D(100, 100); Point.WorldLocation = FVector(500, 600, 70);
	Point.MarkerSizePx = MSMapPalette::CapturePointMarkerSizePx; Point.Label = FText::FromString(TEXT("A")); Point.bCanDeploy = true;
	FMSMapClickResolution Result = UMSDeploymentMapWidget::ResolveMapClick(FVector2D(105, 100), { Point }, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("deployable marker click resolves"), Result.bResolved); TestTrue(TEXT("marker click is direct"), Result.bDirectPoint);
	TestEqual(TEXT("direct click keeps capture point Z"), Result.WorldLocation, Point.WorldLocation);
	const FMSMapCapturePointInteractionBounds LetterBounds = UMSMapWidgetBase::GetCapturePointInteractionBounds(
		Point.MapLocal, Point.MarkerSizePx, Point.Label, Point.bDiamondMarker);
	TestTrue(TEXT("the right-side label midpoint is inside the drawn marker rectangle"),
		LetterBounds.Contains(Point.MapLocal + FVector2D(LetterBounds.HalfExtents.X * 0.5f, 0.0f)));
	TestTrue(TEXT("the right-side label edge is inclusive"),
		LetterBounds.Contains(Point.MapLocal + FVector2D(LetterBounds.HalfExtents.X - 0.01f, 0.0f)));
	TestFalse(TEXT("the left outer margin is outside the drawn marker rectangle"),
		LetterBounds.Contains(Point.MapLocal - FVector2D(LetterBounds.HalfExtents.X + 0.01f, 0.0f)));
	Result = UMSDeploymentMapWidget::ResolveMapClick(Point.MapLocal + FVector2D(LetterBounds.HalfExtents.X - 0.01f, 0), { Point }, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("the visible label edge resolves through the production click resolver"), Result.bResolved && Result.bDirectPoint);
	Result = UMSDeploymentMapWidget::ResolveMapClick(Point.MapLocal - FVector2D(LetterBounds.HalfExtents.X + 0.01f, 0), { Point }, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestFalse(TEXT("the left outer margin does not resolve as a marker"), Result.bDirectPoint || Result.bResolved);
	FMSDeployPointSnapshot Overlap = Point; Overlap.MapLocal += FVector2D(20.0f, 0.0f); Overlap.WorldLocation += FVector(20.0f, 0.0f, 0.0f);
	Result = UMSDeploymentMapWidget::ResolveMapClick(Point.MapLocal + FVector2D(8.0f, 0.0f), { Point, Overlap }, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("overlapping marker rectangles choose the nearest marker"), Result.bDirectPoint);
	TestEqual(TEXT("nearest overlapping marker preserves its world location"), Result.WorldLocation, Point.WorldLocation);
	FMSDeployPointSnapshot Home = Point; Home.MarkerSizePx = MSMapPalette::HomeBaseMarkerSizePx; Home.bDiamondMarker = true;
	Home.MapLocal += FVector2D(100.0f, 0.0f); Home.WorldLocation += FVector(100.0f, 0.0f, 0.0f);
	const FMSMapCapturePointInteractionBounds HomeBounds = UMSMapWidgetBase::GetCapturePointInteractionBounds(
		Home.MapLocal, Home.MarkerSizePx, Home.Label, Home.bDiamondMarker);
	Result = UMSDeploymentMapWidget::ResolveMapClick(Home.MapLocal + FVector2D(HomeBounds.HalfExtents.X - 0.01f, 0.0f), { Home }, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("the rotated home-base diamond label edge resolves"), Result.bResolved && Result.bDirectPoint);
	Point.bCanDeploy = false;
	const FVector2D EnemyLabelEdge = Point.MapLocal + FVector2D(LetterBounds.HalfExtents.X - 0.01f, 0.0f);
	Result = UMSDeploymentMapWidget::ResolveMapClick(EnemyLabelEdge, { Point }, true, 0.0f,
		MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestFalse(TEXT("an enemy label edge remains a direct rejected marker"), Result.bResolved);
	Result = UMSDeploymentMapWidget::ResolveMapClick(FVector2D(100, 100), { Point }, true, 0.0f,
		MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestFalse(TEXT("enemy/destroyed direct marker is rejected without free-point fallthrough"), Result.bResolved);
	Result = UMSDeploymentMapWidget::ResolveMapClick(Size * 0.5f, {}, true, MSMapPalette::FreeDeployRangeCm - 0.1f,
		MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("free point inside the free-deploy range resolves"), Result.bResolved); TestFalse(TEXT("free point is not direct"), Result.bDirectPoint);
	Result = UMSDeploymentMapWidget::ResolveMapClick(Size * 0.5f, {}, true, MSMapPalette::FreeDeployRangeCm + 0.1f,
		MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, Size, MSMapPalette::EdgeMarginPx);
	TestFalse(TEXT("free point beyond the free-deploy range is rejected"), Result.bResolved);
	return true;
}

bool FMSMapCaptureDeployPolicyTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSCapturePoint* PlayerPoint = Scope.World->SpawnActor<AMSCapturePoint>(FVector(1000, 0, 0), FRotator::ZeroRotator);
	AMSCapturePoint* EnemyPoint = Scope.World->SpawnActor<AMSCapturePoint>(FVector(100, 0, 0), FRotator::ZeroRotator);
	PlayerPoint->SetFixedOwner(EMSCaptureOwner::TeamPlayer); EnemyPoint->SetFixedOwner(EMSCaptureOwner::TeamEnemy);
	TestTrue(TEXT("owned point allows deployment"), PlayerPoint->CanDeployFor(MSTeam::PlayerTeamId()));
	TestFalse(TEXT("enemy point rejects deployment"), EnemyPoint->CanDeployFor(MSTeam::PlayerTeamId()));
	float Distance = 0.0f;
	AMSCapturePoint* Nearest = AMSCapturePoint::FindNearestDeployablePoint(Scope.World, MSTeam::PlayerTeamId(), FVector::ZeroVector, Distance);
	TestEqual(TEXT("nearest search filters a closer enemy point"), Nearest, PlayerPoint);
	TestTrue(TEXT("nearest search reports 2D distance"), FMath::IsNearlyEqual(Distance, 1000.0f));
	AMSHomeBase* Home = Scope.World->SpawnActor<AMSHomeBase>(FVector(2000, 0, 0), FRotator::ZeroRotator);
	TestTrue(TEXT("living owned home base delegates to base policy"), Home->CanDeployFor(MSTeam::PlayerTeamId()));
	return true;
}

bool FMSDeploymentSpawnPointTeamFilterTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	// WorldScale 10x (W6): WorldHalfExtentCm×MSScaleに合わせて配置座標も×MSScaleし、
	// マップ上のピクセル間隔(マーカー判定半径との関係)をスケール前と同一に保つ。
	AMSBotSpawnPoint* Friendly = Scope.World->SpawnActor<AMSBotSpawnPoint>(
		FVector(1400.0f * MSGO::MSScale, 2300.0f * MSGO::MSScale, 90), FRotator::ZeroRotator);
	AMSBotSpawnPoint* Enemy = Scope.World->SpawnActor<AMSBotSpawnPoint>(
		FVector(-3200.0f * MSGO::MSScale, 800.0f * MSGO::MSScale, 70), FRotator::ZeroRotator);
	Friendly->SetSpawnTeam(EMSCaptureOwner::TeamPlayer); Enemy->SetSpawnTeam(EMSCaptureOwner::TeamEnemy);
	TArray<FMSDeployPointSnapshot> Snapshots;
	UMSDeploymentMapWidget::AppendTeamSpawnPointSnapshots(Scope.World, MSTeam::PlayerTeamId(), Snapshots, FVector2D(MSMapPalette::DeploymentMapSizePx));
	TestEqual(TEXT("only the friendly spawn point is exposed"), Snapshots.Num(), 1);
	TestEqual(TEXT("friendly snapshot keeps its transform"), Snapshots[0].WorldLocation, Friendly->GetActorLocation());
	FMSMapClickResolution Result = UMSDeploymentMapWidget::ResolveMapClick(Snapshots[0].MapLocal, Snapshots, false,
		TNumericLimits<float>::Max(), MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm,
		FVector2D(MSMapPalette::DeploymentMapSizePx), MSMapPalette::EdgeMarginPx);
	TestTrue(TEXT("friendly AMSBotSpawnPoint resolves as a direct deployment marker"), Result.bResolved && Result.bDirectPoint);
	const FVector2D EnemyLocal = UMSMapWidgetBase::WorldToMapLocal(Enemy->GetActorLocation(), MSMapPalette::WorldHalfExtentCm,
		FVector2D(MSMapPalette::DeploymentMapSizePx), MSMapPalette::EdgeMarginPx);
	Result = UMSDeploymentMapWidget::ResolveMapClick(EnemyLocal, Snapshots, false, TNumericLimits<float>::Max(),
		MSMapPalette::FreeDeployRangeCm, MSMapPalette::WorldHalfExtentCm, FVector2D(MSMapPalette::DeploymentMapSizePx), MSMapPalette::EdgeMarginPx);
	TestFalse(TEXT("enemy-only AMSBotSpawnPoint cannot resolve"), Result.bResolved);
	return true;
}

bool FMSDeploymentReferenceLayoutAndMSListTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	TStrongObjectPtr<UMSDeckSubsystem> Deck(NewObject<UMSDeckSubsystem>(NewObject<UGameInstance>(GEngine)));
	UMSMobileSuitData* Alpha = NewObject<UMSMobileSuitData>(Deck.Get()); Alpha->UnitName = FText::FromString(TEXT("ALPHA"));
	UMSMobileSuitData* Bravo = NewObject<UMSMobileSuitData>(Deck.Get()); Bravo->UnitName = FText::FromString(TEXT("BRAVO"));
	Deck->AssignPreset(0, Alpha); Deck->AssignPreset(1, Bravo);
	TStrongObjectPtr<UMSDeploymentMapWidget> Widget(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Widget->Initialize(); Widget->InitializeForAutomationTests(); Widget->SetDeckSubsystemForTesting(Deck.Get());
	Widget->SetMissionRuleName(FText::FromString(TEXT("拠点制圧戦"))); Widget->SetDeployPreset(Alpha);
	TestNotNull(TEXT("reference title is present"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DeploymentTitleText"))));
	TestEqual(TEXT("selected mission rule is shown in the mission band"), Widget->GetMissionRuleName().ToString(), FString(TEXT("拠点制圧戦")));
	TestNotNull(TEXT("MS LIST is present"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DeploymentMSListTitle"))));
	TestNotNull(TEXT("return-to-suit button is present"), Cast<UButton>(Widget->GetWidgetFromName(TEXT("ReturnToSuitSelectionButton"))));
	CastChecked<UButton>(Widget->GetWidgetFromName(TEXT("DeploymentMSListSlot1Button")))->OnClicked.Broadcast();
	TestEqual(TEXT("MS LIST card click selects another preset"), Widget->GetDeployPreset(), Bravo);
	TestEqual(TEXT("MS LIST selection tracks the clicked deck slot"), Widget->GetSelectedDeckSlotIndex(), 1);
	return true;
}

bool FMSDeploymentDeployClosesFrontendTest::RunTest(const FString&)
{
	MSMapWidgetTestP86::FScopedMapWorld Scope;
	AMSPlayerCharacter* Player = Scope.World->SpawnActor<AMSPlayerCharacter>();
	UMSMobileSuitData* Preset = NewObject<UMSMobileSuitData>(Scope.World); Preset->UnitName = FText::FromString(TEXT("DEPLOY TEST")); Preset->MaxArmor = 2500;
	TStrongObjectPtr<UMSDeploymentMapWidget> Deployment(NewObject<UMSDeploymentMapWidget>(Scope.World));
	Deployment->Initialize(); Deployment->InitializeForAutomationTests(); Deployment->SetRespawnComponentForTesting(Player->GetRespawnComponent()); Deployment->SetDeployPreset(Preset); Deployment->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSSpawnSelectWidget> SpawnSelect(NewObject<UMSSpawnSelectWidget>(Scope.World)); SpawnSelect->Initialize(); SpawnSelect->InitializeForAutomationTests(); SpawnSelect->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSBriefingWidget> Briefing(NewObject<UMSBriefingWidget>(Scope.World)); Briefing->Initialize(); Briefing->InitializeForAutomationTests(); Briefing->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSSortieWidget> Sortie(NewObject<UMSSortieWidget>(Scope.World)); Sortie->Initialize(); Sortie->InitializeForAutomationTests(); Sortie->SetVisibility(ESlateVisibility::Visible);
	TStrongObjectPtr<UMSLobbyWidget> Lobby(NewObject<UMSLobbyWidget>(Scope.World)); Lobby->Initialize(); Lobby->InitializeForAutomationTests(); Lobby->SetVisibility(ESlateVisibility::Visible);
	APlayerState* Pauser = Scope.World->SpawnActor<APlayerState>(); Scope.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	const FVector Target(1230, -4560, 175);
	Deployment->ConfirmSelectionForAutomationTests(Target);
	TestEqual(TEXT("Deploy receives the selected preset"), Player->GetMobileSuitData(), Preset);
	TestTrue(TEXT("Deploy teleports to scaled DropSpawnHeight above selected point"),
		Player->GetActorLocation().Equals(
			Target + FVector(0, 0, Player->GetRespawnComponent()->GetDropSpawnHeight()), 0.1f));
	TestFalse(TEXT("deployment map closes after deploy"), Deployment->IsOpen());
	TestFalse(TEXT("spawn selection closes after deploy"), SpawnSelect->IsOpen());
	TestFalse(TEXT("briefing closes after deploy"), Briefing->IsOpen());
	TestFalse(TEXT("sortie closes after deploy"), Sortie->IsOpen());
	TestFalse(TEXT("lobby closes after deploy"), Lobby->IsOpen());
	TestFalse(TEXT("gameplay is unpaused after deploy"), Scope.World->IsPaused());
	return true;
}

#endif
