#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHangarStudioTest,
	"NeoMSGO.UI.MobileSuitPreview.HangarStructureAndPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHangarStudioTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSHangarStudioTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	UMSMobileSuitPreviewRenderer* Renderer = NewObject<UMSMobileSuitPreviewRenderer>();
	UImage* Image = NewObject<UImage>();
	if (!TestTrue(TEXT("renderer initializes"), Renderer->Initialize(World, Image)))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Renderer->ConfigureHangarView();
	UTextureRenderTarget2D* Target = Renderer->GetRenderTarget();
	USceneCaptureComponent2D* Capture = Renderer->GetCaptureComponent();
	TestEqual(TEXT("full HD width"), Target->SizeX, 1920);
	TestEqual(TEXT("full HD height"), Target->SizeY, 1080);
	TestTrue(TEXT("dark room retains sub-LSB precision without linear 8-bit dither"), Target->RenderTargetFormat == RTF_RGBA16f);
	TestFalse(TEXT("tone-mapped linear room is not sampled as sRGB"), Target->IsSRGB());
	TestEqual(TEXT("room leaves display encoding to Slate"), Target->GetDisplayGamma(), 1.f);
	TestTrue(TEXT("UMG uses GPU render target directly"), Image->GetBrush().GetResourceObject() == Target);
	TestTrue(TEXT("capture includes AA and tone curve with linear output, not raw scene HDR"), Capture->CaptureSource == SCS_FinalToneCurveHDR);
	TestFalse(TEXT("no temporal history dependency on first open"), Capture->ShowFlags.TemporalAA);
	TestTrue(TEXT("first hangar frame does not depend on converged GI or reflection history"),
		Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod
		&& Capture->PostProcessSettings.DynamicGlobalIlluminationMethod==EDynamicGlobalIlluminationMethod::None
		&& Capture->PostProcessSettings.bOverride_ReflectionMethod
		&& Capture->PostProcessSettings.ReflectionMethod==EReflectionMethod::None);
	TestTrue(TEXT("spatial AA enabled"), Capture->ShowFlags.AntiAliasing);
	TestFalse(TEXT("no continual hidden-menu render"), Capture->bCaptureEveryFrame);
	TestFalse(TEXT("no movement-triggered duplicate capture"), Capture->bCaptureOnMovement);
	TestTrue(TEXT("customize camera looks up from the suit's lower right"),
		Capture->GetRelativeLocation().Y < 0.f && Capture->GetRelativeLocation().Z < 0.f
		&& Capture->GetRelativeRotation().Pitch > 2.f && Capture->GetRelativeRotation().Pitch < 5.f);
	TestTrue(TEXT("low customize camera stays above the continuous floor"),
		Capture->GetRelativeLocation().Z > -93.5f*MSGO::MSScale);
	TestTrue(TEXT("camera preserves live-tested caption and rotation-button clearance"),
		Capture->GetRelativeLocation().Equals(FVector(500,-148,-65)*MSGO::MSScale,.1f));
	// Check the visible direction, not merely the camera-coordinate sign.
	const FVector CameraRight = Capture->GetRelativeRotation().RotateVector(FVector::RightVector);
	TestTrue(TEXT("suit forward projects toward screen left in the initial hangar view"),
		FVector::DotProduct(FVector::ForwardVector, CameraRight) < -.2f);

	TInlineComponentArray<UStaticMeshComponent*> Parts(Renderer->GetPreviewActor());
	UStaticMeshComponent* Floor = nullptr;
	int32 Columns = 0, Galleries = 0, Trusses = 0;
	const float Sole = -93.5f * MSGO::MSScale;
	for (UStaticMeshComponent* Part : Parts)
	{
		const FVector P = Part->GetRelativeLocation();
		const FVector D = Part->GetRelativeScale3D() * 100.f;
		if (Part->ComponentHasTag(TEXT("HangarFloor")))
		{
			Floor = Part;
			TestTrue(TEXT("continuous floor top is the suit sole datum"), FMath::IsNearlyEqual(P.Z+D.Z*.5f,Sole,.1f));
			TestTrue(TEXT("floor dimensions are real centimetres"), D.Equals(FVector(13000,12000,36),.1f));
		}
		if (Part->ComponentHasTag(TEXT("HangarColumn")))
		{
			++Columns;
			TestTrue(TEXT("columns rest on continuous slab"), FMath::IsNearlyEqual(P.Z-D.Z*.5f,Sole,.1f));
		}
		if (Part->ComponentHasTag(TEXT("HangarGallery")))
		{
			++Galleries;
			// A gallery must overlap a load-bearing column in plan and height.
			bool bSupported = false;
			for (UStaticMeshComponent* Support : Parts)
			{
				if (!Support->ComponentHasTag(TEXT("HangarColumn"))) continue;
				const FVector Q=Support->GetRelativeLocation(), S=Support->GetRelativeScale3D()*100.f;
				bSupported |= FMath::Abs(P.X-Q.X) <= (D.X+S.X)*.5f
					&& FMath::Abs(P.Y-Q.Y) <= (D.Y+S.Y)*.5f && Q.Z+S.Z*.5f >= P.Z;
			}
			TestTrue(TEXT("gallery has a physical bearing column"), bSupported);
		}
		if (Part->ComponentHasTag(TEXT("HangarRoofTruss"))) ++Trusses;
	}
	TestNotNull(TEXT("single connected floor exists"), Floor);
	TestTrue(TEXT("room contains supported structural hierarchy"), Columns >= 8 && Galleries == 3 && Trusses >= 6);
	const FTransform FloorTransform = Floor ? Floor->GetRelativeTransform() : FTransform::Identity;
	const int32 PartCount = Parts.Num();
	Renderer->RotatePreview(75.f);
	TestTrue(TEXT("rotation retains GPU brush without readback"), Image->GetBrush().GetResourceObject() == Target);
	if (Floor) TestTrue(TEXT("suit rotation does not rotate its room"), Floor->GetRelativeTransform().Equals(FloorTransform));
	Renderer->ConfigureHangarView();
	TInlineComponentArray<UStaticMeshComponent*> RepeatedParts(Renderer->GetPreviewActor());
	TestEqual(TEXT("reopening reuses geometry"), RepeatedParts.Num(), PartCount);
	Renderer->RotatePreview(-Renderer->GetPreviewYaw());
	Renderer->ConfigureDeckCardView();
	TestEqual(TEXT("deck keeps its own capture dimensions"), Target->SizeX, 440);
	TestEqual(TEXT("deck capture matches the 220 by 135 image slot"), Target->SizeY, 270);
	auto TestLowScreenLeftCamera = [this, Capture](const TCHAR* Context)
	{
		TestTrue(FString::Printf(TEXT("%s looks slightly upward"), Context),
			Capture->GetRelativeRotation().Pitch > 2.f && Capture->GetRelativeRotation().Pitch < 5.f);
		const FVector Right = Capture->GetRelativeRotation().RotateVector(FVector::RightVector);
		TestTrue(FString::Printf(TEXT("%s projects suit forward toward screen left"), Context),
			FVector::DotProduct(FVector::ForwardVector, Right) < -.2f);
	};
	TestLowScreenLeftCamera(TEXT("deck card"));
	auto ProjectVertical = [Capture, Target](const FVector& DesignPoint)
	{
		const FVector CameraPoint = Capture->GetRelativeTransform().InverseTransformPosition(DesignPoint*MSGO::MSScale);
		const float TanVerticalHalfFOV = FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle*.5f))
			* static_cast<float>(Target->SizeY)/Target->SizeX;
		return CameraPoint.Z/(CameraPoint.X*TanVerticalHalfFOV);
	};
	TestTrue(TEXT("deck retains the head top inside the upper-body frame"),
		FMath::Abs(ProjectVertical(FVector(0,0,100))) < 1.f);
	TestTrue(TEXT("deck keeps transparent HDR path"), Capture->CaptureSource == SCS_SceneColorHDR);
	TestTrue(TEXT("deck keeps its previous half-float storage"), Target->RenderTargetFormat == RTF_RGBA16f);
	TestEqual(TEXT("deck restores its previous linear display gamma"), Target->GetDisplayGamma(), 1.f);
	TestFalse(TEXT("deck does not inherit hangar GI override"), Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod);
	TestFalse(TEXT("deck does not inherit hangar reflection override"), Capture->PostProcessSettings.bOverride_ReflectionMethod);
	if (Floor) TestFalse(TEXT("room hidden from transparent deck"), Floor->IsVisible());
	Renderer->ConfigureDeploymentListView();
	TestEqual(TEXT("deployment list uses twice its visible image width"), Target->SizeX, 280);
	TestEqual(TEXT("deployment list uses twice its visible image height"), Target->SizeY, 64);
	TestLowScreenLeftCamera(TEXT("deployment MS LIST"));
	TestTrue(TEXT("deployment targets the raised upper torso, excluding waist and feet"),
		FMath::Abs(ProjectVertical(FVector(0,0,68))) < .1f
		&& ProjectVertical(FVector(0,0,35)) < -1.f
		&& FMath::Abs(ProjectVertical(FVector(0,0,-93.5f))) > 1.f);
	TestTrue(TEXT("deployment list uses its render target directly"), Image->GetBrush().GetResourceObject() == Target);
	TestFalse(TEXT("deployment thumbnails remain capture-on-selection"), Capture->bCaptureEveryFrame);
	Renderer->ConfigureCustomizeView();
	TestTrue(TEXT("thumbnail profiles do not alter the existing customize composition"),
		Capture->GetRelativeLocation().Equals(FVector(430,70,45)*MSGO::MSScale,.1f));
	Renderer->ConfigureSortieView(false);
	TestTrue(TEXT("sortie restores its previous LDR storage and capture"),
		Target->RenderTargetFormat == RTF_RGBA8 && Capture->CaptureSource == SCS_FinalColorLDR);
	TestEqual(TEXT("sortie restores its previous linear display gamma"), Target->GetDisplayGamma(), 1.f);
	Renderer->ConfigureHangarView();
	TestTrue(TEXT("return from deck restores direct full-HD brush"), Image->GetBrush().GetResourceObject() == Target && Target->SizeX == 1920);
	TestTrue(TEXT("return from another profile restores linear tone-mapped room"),
		Target->RenderTargetFormat == RTF_RGBA16f && Capture->CaptureSource == SCS_FinalToneCurveHDR);
	if (Floor) TestTrue(TEXT("return from deck restores floor"), Floor->IsVisible());
	Renderer->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
