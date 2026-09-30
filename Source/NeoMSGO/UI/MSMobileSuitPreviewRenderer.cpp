#include "UI/MSMobileSuitPreviewRenderer.h"

#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSMobHandVisuals.h"
#include "Core/MSTeamTypes.h"

#include "Animation/MSMechRigComponent.h"
#include "Components/Image.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSShieldPresentation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float RigRootToSoleCm = -93.5f;
}

UMSMobileSuitPreviewRenderer::UMSMobileSuitPreviewRenderer()
{
	// Keep a hard CDO reference so the neutral environment is included in cooks.
	static ConstructorHelpers::FObjectFinder<UTextureCube> Ambient(
		TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
	StudioAmbientCubemap = Ambient.Object;
}

UStaticMeshComponent* UMSMobileSuitPreviewRenderer::AddStudioCube(
	USceneComponent* Root, UStaticMesh* CubeMesh, const FVector& DesignLocation,
	const FVector& DesignDimensions, const FRotator& Rotation, const FLinearColor& Color)
{
	if (!PreviewActor || !Root || !CubeMesh)
	{
		return nullptr;
	}

	const float Scale = MSGO::MSScale;
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(PreviewActor);
	PreviewActor->AddInstanceComponent(Component);
	Component->SetupAttachment(Root);
	Component->SetStaticMesh(CubeMesh);
	Component->SetRelativeLocation(DesignLocation * Scale);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D((DesignDimensions * Scale) / 100.0f);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCastShadow(false);
	Component->SetLightingChannels(false, true, false);

	UMaterialInterface* BaseMaterial = Cast<UMaterialInterface>(
		StaticLoadObject(UMaterialInterface::StaticClass(), nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Material);
		StudioMaterials.Add(Material);
	}
	Component->RegisterComponent();
	StudioBackdrop.Add(Component);
	return Component;
}

UPointLightComponent* UMSMobileSuitPreviewRenderer::AddStudioLight(
	USceneComponent* Root, const FVector& DesignLocation, const float DesignIntensity,
	const float AttenuationRadius, const FLinearColor& Color)
{
	if (!PreviewActor || !Root)
	{
		return nullptr;
	}

	const float Scale = MSGO::MSScale;
	UPointLightComponent* Light = NewObject<UPointLightComponent>(PreviewActor);
	PreviewActor->AddInstanceComponent(Light);
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(DesignLocation * Scale);
	Light->SetIntensity(DesignIntensity * Scale * Scale);
	Light->SetAttenuationRadius(AttenuationRadius * Scale);
	Light->SetLightColor(Color, false);
	// Broad fill lights complement the neutral ambient reflection in this
	// isolated render target, without introducing overlapping shadow maps.
	Light->SetCastShadows(false);
	Light->SetLightingChannels(false, true, false);
	Light->RegisterComponent();
	StudioLights.Add(Light);
	return Light;
}

void UMSMobileSuitPreviewRenderer::BuildSortieStudio(USceneComponent* Root)
{
	if (!Root)
	{
		return;
	}

	UStaticMesh* CubeMesh = Cast<UStaticMesh>(StaticLoadObject(
		UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	if (!CubeMesh)
	{
		return;
	}

	// The original respawn screen keeps a dim hangar visible behind the cards and
	// suit.  The preview is isolated from the live map, so build a small, stable
	// studio from UE primitives instead of replacing the background with black.
	AddStudioCube(Root, CubeMesh, FVector(-180.0f, 0.0f, 112.0f),
		FVector(18.0f, 1600.0f, 450.0f), FRotator::ZeroRotator,
		FLinearColor(0.12f, 0.125f, 0.13f, 1.0f));
	AddStudioCube(Root, CubeMesh, FVector(0.0f, 0.0f, RigRootToSoleCm - 9.0f),
		FVector(820.0f, 760.0f, 18.0f), FRotator::ZeroRotator,
		FLinearColor(0.07f, 0.075f, 0.08f, 1.0f));
	AddStudioCube(Root, CubeMesh, FVector(-146.0f, 200.0f, 60.0f),
		FVector(24.0f, 24.0f, 300.0f), FRotator::ZeroRotator,
		FLinearColor(0.145f, 0.15f, 0.155f, 1.0f));
	AddStudioCube(Root, CubeMesh, FVector(-146.0f, -200.0f, 60.0f),
		FVector(24.0f, 24.0f, 300.0f), FRotator::ZeroRotator,
		FLinearColor(0.125f, 0.13f, 0.135f, 1.0f));

	// Horizontal hangar crossbars keep the expanded wall from reading as a
	// featureless plane while staying behind the mobile suit silhouette.
	AddStudioCube(Root, CubeMesh, FVector(-150.0f, 0.0f, 120.0f),
		FVector(24.0f, 1500.0f, 18.0f), FRotator::ZeroRotator,
		FLinearColor(0.15f, 0.155f, 0.16f, 1.0f));
	AddStudioCube(Root, CubeMesh, FVector(-150.0f, 0.0f, -35.0f),
		FVector(20.0f, 1400.0f, 12.0f), FRotator::ZeroRotator,
		FLinearColor(0.105f, 0.11f, 0.115f, 1.0f));

	// Floor rails provide the short perspective lines visible below the feet.
	AddStudioCube(Root, CubeMesh, FVector(0.0f, -130.0f, -92.5f),
		FVector(780.0f, 5.0f, 2.0f), FRotator::ZeroRotator,
		FLinearColor(0.105f, 0.11f, 0.115f, 1.0f));
	AddStudioCube(Root, CubeMesh, FVector(0.0f, 130.0f, -92.5f),
		FVector(780.0f, 5.0f, 2.0f), FRotator::ZeroRotator,
		FLinearColor(0.105f, 0.11f, 0.115f, 1.0f));

	// Three lights give the armor a readable front plane, a soft fill, and a
	// narrow rim against the hangar wall without changing the map's lighting.
	AddStudioLight(Root, FVector(230.0f, 150.0f, 275.0f), 33600.0f, 720.0f,
		FLinearColor(1.0f, 0.97f, 0.93f));
	AddStudioLight(Root, FVector(80.0f, -250.0f, 210.0f), 14800.0f, 620.0f,
		FLinearColor(0.90f, 0.94f, 1.0f));
	AddStudioLight(Root, FVector(-210.0f, 40.0f, 250.0f), 19200.0f, 580.0f,
		FLinearColor(0.93f, 0.97f, 1.0f));
}

void UMSMobileSuitPreviewRenderer::SetStudioBackdropVisible(const bool bVisible)
{
	for (UStaticMeshComponent* Component : StudioBackdrop)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}

void UMSMobileSuitPreviewRenderer::BuildHangarStudio(USceneComponent* Root)
{
	if (!Root || !HangarBackdrop.IsEmpty()) return;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return;

	// MSGO9, frame 2180 (65.314 s): recessed service bay, supported galleries,
	// roof trusses, dock rails and a shutter. All geometry below is in real cm;
	// only the established suit sole datum comes from the small authoring rig.
	const float Floor = RigRootToSoleCm * MSGO::MSScale;
	const FLinearColor Wall(.027f,.043f,.058f), Steel(.065f,.092f,.115f);
	const FLinearColor Edge(.11f,.145f,.17f), Dark(.018f,.026f,.032f);
	const FLinearColor Yellow(.48f,.32f,.065f), Light(.55f,.72f,.78f);
	TMap<FLinearColor, UMaterialInstanceDynamic*> Materials;
	auto Box = [&](const TCHAR* Role, FVector P, FVector D, FLinearColor Color,
		FRotator R = FRotator::ZeroRotator) -> UStaticMeshComponent*
	{
		// Reuse the preview component setup without scaling environment dimensions.
		UStaticMeshComponent* Part = AddStudioCube(Root, Cube, P / MSGO::MSScale,
			D / MSGO::MSScale, R, Color);
		if (!Part) return nullptr;
		StudioBackdrop.Remove(Part);
		HangarBackdrop.Add(Part);
		Part->ComponentTags.Add(FName(Role));
		Part->SetCastShadow(D.GetMin() >= 20.f);
		// Many structural members share one material rather than one MID per bolt.
		if (UMaterialInstanceDynamic** Existing = Materials.Find(Color))
		{
			StudioMaterials.Remove(Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)));
			Part->SetMaterial(0, *Existing);
		}
		else Materials.Add(Color, Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)));
		return Part;
	};
	auto Beam = [&](const TCHAR* Role, FVector A, FVector B, float Width, float Depth)
	{
		Box(Role, (A+B)*.5f, FVector((B-A).Size(),Width,Depth), Steel,
			FRotationMatrix::MakeFromX(B-A).Rotator());
	};

	// Continuous slab supports the feet, galleries and columns; shallow seams
	// divide its surface without a separate floating display plinth.
	Box(TEXT("HangarFloor"), FVector(1000,0,Floor-18), FVector(13000,12000,36),
		FLinearColor(.060f,.078f,.09f));
	Box(TEXT("HangarWall"), FVector(-3700,0,Floor+2400), FVector(80,12000,4800), Wall);
	for (float Side : {-1.f,1.f})
		Box(TEXT("HangarWall"), FVector(1000,Side*5960,Floor+2400), FVector(13000,80,4800), Wall);
	for (int32 I=-5; I<=7; ++I)
		Box(TEXT("HangarFloorJoint"), FVector(I*900.f,0,Floor+.35f), FVector(3,12000,.7f), Dark);
	for (int32 I=-6; I<=6; ++I)
		Box(TEXT("HangarFloorJoint"), FVector(1000,I*900.f,Floor+.35f), FVector(13000,3,.7f), Dark);
	for (float Side : {-1.f,1.f})
	{
		Box(TEXT("HangarDockRail"), FVector(500,Side*1150,Floor+1.5f), FVector(10000,34,3), Dark);
		Box(TEXT("HangarDockRail"), FVector(500,Side*1150,Floor+3), FVector(10000,8,3), Edge);
		Box(TEXT("HangarSafetyLine"), FVector(0,Side*1450,Floor+1), FVector(4800,11,2), Yellow);
	}

	// Rear columns bear the gallery and header; each has a foot plate on the slab.
	for (float Y : {-5400.f,-3000.f,3000.f,5400.f})
	{
		Box(TEXT("HangarColumn"), FVector(-3390,Y,Floor+2100), FVector(130,140,4200), Steel);
		Box(TEXT("HangarColumnFoot"), FVector(-3390,Y,Floor+22), FVector(230,240,44), Edge);
		for (float Z : {900.f,2100.f,3300.f})
			Box(TEXT("HangarColumnCollar"), FVector(-3390,Y,Floor+Z), FVector(150,165,35), Edge);
	}
	Box(TEXT("HangarGallery"), FVector(-3320,0,Floor+2300), FVector(680,11200,40), Steel);
	Box(TEXT("HangarGalleryFascia"), FVector(-2970,0,Floor+2270), FVector(25,11200,110), Dark);
	for (int32 I=-11; I<=11; ++I)
		Box(TEXT("HangarRailingPost"), FVector(-2980,I*500.f,Floor+2380), FVector(12,12,120), Edge);
	for (float Z : {2360.f,2440.f})
		Box(TEXT("HangarRailing"), FVector(-2980,0,Floor+Z), FVector(12,11200,12), Edge);
	for (float Y : {-4500.f,4500.f})
		Beam(TEXT("HangarGalleryBrace"), FVector(-3360,Y,Floor+1650), FVector(-2980,Y,Floor+2280), 35,35);

	// Deep shutter ribs read as a maintenance door, framed independently of the
	// structural columns. Equipment stays outside the central suit silhouette.
	Box(TEXT("HangarShutter"), FVector(-3635,0,Floor+1080), FVector(45,4450,2160), Dark);
	for (int32 I=0; I<15; ++I)
		Box(TEXT("HangarShutterRib"), FVector(-3604,0,Floor+75+I*140.f), FVector(20,4400,20), Steel);
	for (float Y : {-2300.f,2300.f})
		Box(TEXT("HangarDoorFrame"), FVector(-3570,Y,Floor+1100), FVector(110,95,2200), Edge);
	Box(TEXT("HangarDoorHeader"), FVector(-3540,0,Floor+2190), FVector(150,4700,100), Steel);
	for (int32 I=-5; I<=5; ++I)
		Box(TEXT("HangarHazardMark"), FVector(-3455,I*400.f,Floor+2190), FVector(10,150,65), Yellow,
			FRotator(0,0,-25));

	// Side galleries and roof crane rails are carried by paired portal columns.
	for (float Side : {-1.f,1.f})
	{
		Box(TEXT("HangarGallery"), FVector(-450,Side*5270,Floor+2300), FVector(5800,680,40), Steel);
		for (float X : {-3300.f,-600.f,2300.f})
		{
			Box(TEXT("HangarColumn"), FVector(X,Side*5500,Floor+2100), FVector(150,150,4200), Steel);
			Box(TEXT("HangarColumnFoot"), FVector(X,Side*5500,Floor+22), FVector(260,260,44), Edge);
		}
		Box(TEXT("HangarCraneRail"), FVector(-450,Side*5500,Floor+4100), FVector(5800,190,180), Edge);
		Box(TEXT("HangarRailing"), FVector(-450,Side*4930,Floor+2440), FVector(5800,12,12), Edge);
		for (int32 I=0; I<12; ++I)
			Box(TEXT("HangarRailingPost"), FVector(-3200+I*500.f,Side*4930,Floor+2380), FVector(12,12,120), Edge);
		for (float X : {-1900.f,900.f})
		{
			Beam(TEXT("HangarTruss"), FVector(X-1300,Side*5500,Floor+3200), FVector(X+1300,Side*5500,Floor+4000), 45,45);
			Beam(TEXT("HangarTruss"), FVector(X-1300,Side*5500,Floor+4000), FVector(X+1300,Side*5500,Floor+3200), 45,45);
		}
		// Utility cabinets stand on the floor, with door seams, vents and status lamps.
		for (int32 I=0; I<3; ++I)
		{
			const float Y=Side*(3600+I*430.f);
			Box(TEXT("HangarCabinet"), FVector(-3130,Y,Floor+410), FVector(360,360,820), Steel);
			Box(TEXT("HangarCabinetDoor"), FVector(-2940,Y,Floor+415), FVector(16,315,740), Wall);
			Box(TEXT("HangarCabinetLamp"), FVector(-2928,Y-80,Floor+670), FVector(10,55,20), Light);
			for (int32 J=0; J<4; ++J)
				Box(TEXT("HangarCabinetVent"), FVector(-2928,Y,Floor+210+J*45.f), FVector(10,260,12), Dark);
		}
	}
	// Two connected headers with diagonal web members; no unsupported beams.
	for (float Z : {3250.f,3950.f})
		Box(TEXT("HangarRoofHeader"), FVector(-3390,0,Floor+Z), FVector(140,11200,100), Steel);
	for (int32 I=0; I<8; ++I)
		Beam(TEXT("HangarRoofTruss"), FVector(-3390,-5400+I*1350.f,Floor+3300),
			FVector(-3390,-4050+I*1350.f,Floor+3900), 55,55);
	Box(TEXT("HangarCraneBridge"), FVector(-600,0,Floor+4090), FVector(230,11000,220), Steel);
	Box(TEXT("HangarCraneCarriage"), FVector(-600,-3450,Floor+3930), FVector(400,520,150), Edge);
	Box(TEXT("HangarCeiling"), FVector(-450,0,Floor+4300), FVector(6600,12000,60), Dark);
	for (float Y : {-3500.f,3500.f})
	{
		Box(TEXT("HangarLightHousing"), FVector(-3320,Y,Floor+2820), FVector(130,900,120), Dark);
		Box(TEXT("HangarLightLens"), FVector(-3245,Y,Floor+2820), FVector(15,760,55), Light);
	}
	// Floor-standing service lifts form a middle-distance layer, as in the video.
	// Their platforms sit on four braced posts; ladders terminate at those decks.
	for (float Side : {-1.f,1.f})
	{
		const float Y = Side * 2350.f;
		Box(TEXT("HangarServiceLiftBase"), FVector(-1800,Y,Floor+25), FVector(650,620,50), Dark);
		for (float XOffset : {-210.f,210.f})
		{
			for (float YOffset : {-230.f,230.f})
				Box(TEXT("HangarServiceLiftPost"), FVector(-1800+XOffset,Y+YOffset,Floor+1200), FVector(36,36,2400), Edge);
			for (int32 I=0; I<4; ++I)
			{
				Beam(TEXT("HangarServiceLiftBrace"), FVector(-1800+XOffset,Y-230,Floor+60+I*550.f),
					FVector(-1800+XOffset,Y+230,Floor+610+I*550.f), 18,18);
			}
		}
		Box(TEXT("HangarServiceLiftDeck"), FVector(-1800,Y,Floor+1820), FVector(650,620,35), Steel);
		Box(TEXT("HangarServiceLiftHeader"), FVector(-1800,Y,Floor+2400), FVector(520,540,60), Steel);
		for (float Offset : {-100.f,100.f})
			Box(TEXT("HangarLadderStile"), FVector(-2130,Y+Offset,Floor+915), FVector(20,20,1830), Edge);
		for (int32 I=0; I<15; ++I)
			Box(TEXT("HangarLadderRung"), FVector(-2130,Y,Floor+80+I*120.f), FVector(20,200,14), Edge);
	}
}

void UMSMobileSuitPreviewRenderer::SetHangarBackdropVisible(const bool bVisible)
{
	for (UStaticMeshComponent* Part : HangarBackdrop)
		if (Part) Part->SetVisibility(bVisible);
}

void UMSMobileSuitPreviewRenderer::SetSortieLightingEnabled(const bool bEnabled)
{
	for (UPointLightComponent* Light : StudioLights)
	{
		if (Light)
		{
			Light->SetVisibility(bEnabled, true);
		}
	}
}

void UMSMobileSuitPreviewRenderer::SetPreviewLightingChannels(const bool bSortieProfile)
{
	if (!PreviewActor)
	{
		return;
	}

	TInlineComponentArray<UPrimitiveComponent*> Primitives(PreviewActor);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive)
		{
			Primitive->SetLightingChannels(!bSortieProfile, bSortieProfile, false);
		}
	}
}

void UMSMobileSuitPreviewRenderer::SetSortieViewFlags(const bool bSortieProfile)
{
	if (!Capture)
	{
		return;
	}

	TArray<FEngineShowFlagsSetting> ShowFlagSettings;
	const TCHAR* Flags[] = { TEXT("Fog"), TEXT("Atmosphere"), TEXT("Cloud"),
		TEXT("VolumetricFog"), TEXT("SkyLighting") };
	for (const TCHAR* FlagName : Flags)
	{
		FEngineShowFlagsSetting Setting;
		Setting.ShowFlagName = FlagName;
		Setting.Enabled = !bSortieProfile;
		ShowFlagSettings.Add(Setting);
	}
	Capture->SetShowFlagSettings(ShowFlagSettings);
}

FLinearColor UMSMobileSuitPreviewRenderer::GetCategoryColor(const UMSMobileSuitData* MobileSuit)
{
	if (!MobileSuit) return FLinearColor(0.35f, 0.9f, 1.0f);
	switch (MobileSuit->UnitType)
	{
	case EMSMobileSuitUnitType::Assault: return FLinearColor(0.25f, 0.95f, 0.85f);
	case EMSMobileSuitUnitType::Heavy: return FLinearColor(1.0f, 0.65f, 0.18f);
	case EMSMobileSuitUnitType::Support: return FLinearColor(0.35f, 1.0f, 0.42f);
	case EMSMobileSuitUnitType::Bombardment: return FLinearColor(1.0f, 0.35f, 0.22f);
	case EMSMobileSuitUnitType::Sniper: return FLinearColor(0.72f, 0.48f, 1.0f);
	default: return FLinearColor(0.35f, 0.9f, 1.0f);
	}
}

bool UMSMobileSuitPreviewRenderer::Initialize(UWorld* World, UImage* TargetImage)
{
	if (!World || !TargetImage) return false;
	Shutdown();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	PreviewActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(0, 0, -100000.0f), FRotator::ZeroRotator, Params);
	if (!PreviewActor) return false;

	USceneComponent* Root = NewObject<USceneComponent>(PreviewActor, TEXT("PreviewRoot"));
	PreviewActor->AddInstanceComponent(Root);
	PreviewActor->SetRootComponent(Root);
	Root->RegisterComponent();
	BuildSortieStudio(Root);
	// The primitive hangar is a SpawnSelect-only backdrop. Keep the renderer's
	// construction/default capture compatible with the existing transparent
	// customize and deck-card previews until an explicit sortie profile is set.
	SetStudioBackdropVisible(false);
	SetSortieLightingEnabled(false);

	Rig = NewObject<UMSMechRigComponent>(PreviewActor, TEXT("PreviewMechRig"));
	PreviewActor->AddInstanceComponent(Rig);
	Rig->RegisterComponent();

	VisualMesh = NewObject<UPoseableMeshComponent>(PreviewActor, TEXT("PreviewVisualMesh"));
	PreviewActor->AddInstanceComponent(VisualMesh);
	VisualMesh->SetupAttachment(Rig->GetRigRoot());
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->SetVisibility(false, true);
	VisualMesh->RegisterComponent();
	MobHandVisuals = NewObject<UMSMobHandVisuals>(PreviewActor, TEXT("PreviewMobHandVisuals"));

	// Hangar uses the exact native shield asset also used by the combat visual.
	if (USceneComponent* Forearm = Rig->GetJoint(TEXT("Forearm_L")))
	{
		NativeShieldMesh = NewObject<UStaticMeshComponent>(PreviewActor, TEXT("PreviewNativeShield"));
		PreviewActor->AddInstanceComponent(NativeShieldMesh);
		NativeShieldMesh->SetupAttachment(Forearm);
		NativeShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NativeShieldMesh->SetGenerateOverlapEvents(false);
		NativeShieldMesh->SetCanEverAffectNavigation(false);
		NativeShieldMesh->SetVisibility(false);
		NativeShieldMesh->RegisterComponent();
	}

	PreviewKeyLight = NewObject<UPointLightComponent>(PreviewActor, TEXT("PreviewKeyLight"));
	PreviewActor->AddInstanceComponent(PreviewKeyLight);
	PreviewKeyLight->SetupAttachment(Root);
	PreviewKeyLight->SetRelativeLocation(FVector(220.0f, 180.0f, 260.0f) * MSGO::MSScale);
	PreviewKeyLight->SetIntensity(6500.0f);
	PreviewKeyLight->SetAttenuationRadius(1100.0f * MSGO::MSScale);
	PreviewKeyLight->RegisterComponent();

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MobileSuitPreviewRT"), RF_Transient);
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->ClearColor = FLinearColor(0.008f, 0.015f, 0.025f, 1.0f);
	RenderTarget->InitAutoFormat(512, 512);
	RenderTarget->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(PreviewActor, TEXT("PreviewCapture"));
	PreviewActor->AddInstanceComponent(Capture);
	Capture->SetupAttachment(Root);
	Capture->TextureTarget = RenderTarget;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->FOVAngle = 32.0f;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// Scale the entire approved 193 cm composition with the 18 m rig. FOV remains
	// unchanged, so hangar/customize/deployment previews retain the same framing.
	// Keep the renderer's construction default compatible with the existing
	// renderer smoke test; the SpawnSelect profile below applies the authored
	// front three-quarter camera before its first visible capture.
	Capture->SetRelativeLocation(FVector(330.0f, 270.0f, 125.0f) * MSGO::MSScale);
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-330.0f, -270.0f, -95.0f)).Rotator());
	Capture->RegisterComponent();
	Capture->ShowOnlyActorComponents(PreviewActor);

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.SetResourceObject(RenderTarget);
	Brush.ImageSize = FVector2D(512.0f);
	TargetImage->SetBrush(Brush);
	OutputImage = TargetImage;
	SetMobileSuit(nullptr);
	return true;
}

void UMSMobileSuitPreviewRenderer::ConfigureSortieView(const bool bPortrait)
{
	if (!Capture || !RenderTarget) return;
	CloseupProfile = ECloseupProfile::None;
	SetHangarBackdropVisible(false);
	Capture->ShowFlags.SetTemporalAA(true);
	// Leave the hangar's single-frame lighting overrides when changing profiles.
	Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = false;
	Capture->PostProcessSettings.bOverride_ReflectionMethod = false;
	if (CustomizePlatform) CustomizePlatform->SetVisibility(false);
	bCustomizeView = false;
	CustomizeTexture = nullptr;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// The main sortie image follows the 1920:1080 viewport. Card captures use
	// the authored 160:116 slot ratio so UMG does not stretch the render target.
	const uint32 Width = bPortrait ? 160u : 1920u;
	const uint32 Height = bPortrait ? 116u : 1080u;
	RenderTarget->ClearColor = FLinearColor(0.006f, 0.012f, 0.020f, 1.0f);
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->bForceLinearGamma = true;
	RenderTarget->TargetGamma = 0.f;
	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.SetResourceObject(RenderTarget);
	Brush.ImageSize = FVector2D(static_cast<float>(Width), static_cast<float>(Height));
	if (OutputImage) OutputImage->SetBrush(Brush);

	// ShowOnlyActorComponents keeps the temporary studio independent of the live
	// map. Atmospheric passes are disabled only for the sortie composition.
	SetSortieViewFlags(true);

	// Lighting must not accumulate from the four card captures or other menus.
	static uint32 SortieStudioIndex = 0;
	PreviewActor->SetActorLocation(FVector(++SortieStudioIndex * 50000.0, 0, -1000000.0));
	SetPreviewLightingChannels(true);
	if (PreviewKeyLight) PreviewKeyLight->SetVisibility(false);
	SetSortieLightingEnabled(true);
	Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Capture->PostProcessSettings.AutoExposureMinBrightness = 1.0f;
	Capture->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 2.0f;
	Capture->PostProcessSettings.AmbientCubemap = StudioAmbientCubemap;
	Capture->PostProcessSettings.bOverride_AmbientCubemapIntensity = true;
	Capture->PostProcessSettings.AmbientCubemapIntensity = 1.0f;
	// FOVAngle is horizontal: preserve a 27 degree vertical view at each RT
	// aspect, instead of squeezing a square capture into the full-screen image.
	const float Aspect = static_cast<float>(Width) / Height;
	Capture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(27.0f * 0.5f)) * Aspect));
	// Keep the card centered; offset the full-screen suit toward the reference's
	// right-of-center position without moving the mesh or its authored pivots.
	Capture->SetRelativeLocation(FVector(430.0f, bPortrait ? -140.0f : -110.0f, 75.0f) * MSGO::MSScale);
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-430.0f, 140.0f, -75.0f)).Rotator());
	SetStudioBackdropVisible(true);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureScene();
}

void UMSMobileSuitPreviewRenderer::SetMobileSuit(const UMSMobileSuitData* MobileSuit, const bool bShieldEquipped)
{
	if (!Rig || !VisualMesh || !Capture) return;
	if (CurrentMobileSuit.Get() != MobileSuit)
	{
		CurrentMobileSuit = MobileSuit;
		PreviewYaw = 0.0f;
		Rig->GetRigRoot()->SetRelativeRotation(FRotator::ZeroRotator);
	}
	USkeletalMesh* Mesh = MobileSuit && !MobileSuit->VisualMesh.IsNull()
		? MobileSuit->VisualMesh.LoadSynchronous() : nullptr;
	const bool bUseVisualMesh = Mesh != nullptr;
	for (const FName PartName : Rig->GetPartNames())
	{
		if (UStaticMeshComponent* Part = Rig->GetPart(PartName)) Part->SetVisibility(!bUseVisualMesh, true);
	}
	if (UStaticMeshComponent* Decoration = Rig->GetPart(Rig->GetHeadDecorationName()))
		Decoration->SetVisibility(!bUseVisualMesh, true);
	// Apply the independent equipment flag after recursive rig visibility changes.
	const UMSShieldWeaponData* Shield = MobileSuit && bShieldEquipped ? MobileSuit->GetNativeShield() : nullptr;
	if (NativeShieldMesh)
	{
		UStaticMesh* ShieldMesh = Shield ? Shield->WeaponMesh.LoadSynchronous() : nullptr;
		if (NativeShieldMesh->GetStaticMesh() != ShieldMesh) NativeShieldMesh->EmptyOverrideMaterials();
		NativeShieldMesh->SetStaticMesh(ShieldMesh);
		MSShieldPresentation::ApplyTeamColor(NativeShieldMesh, UMSMeshVisualComponent::GetRoleAccentColor(MobileSuit), true);
		NativeShieldMesh->SetVisibility(NativeShieldMesh->GetStaticMesh() != nullptr);
	}
	auto AttachShield = [&]()
	{
		if (Shield && NativeShieldMesh)
		{
			NativeShieldMesh->SetVisibility(NativeShieldMesh->GetStaticMesh() != nullptr);
			MSShieldPresentation::Attach(NativeShieldMesh, Rig->GetJoint(TEXT("Forearm_L")),
				bUseVisualMesh ? VisualMesh.Get() : nullptr, Rig->GetRigRoot()->GetComponentTransform(),
				Shield->WeaponMeshTransform);
		}
	};

	if (bUseVisualMesh)
	{
		// Poseable meshes only allocate the reference pose when the bone count
		// changes. Our suits share 23 bones but use different bind transforms and
		// units: retaining the old pose can put the new suit outside the camera.
		if (VisualMesh->GetSkinnedAsset() != Mesh)
		{
			VisualMesh->SetSkinnedAssetAndUpdate(nullptr, true);
		}
		VisualMesh->SetSkinnedAssetAndUpdate(Mesh, true);
		// Do not propagate visibility to the hand children: the helper owns the
		// one-visible-variant rule below.
		VisualMesh->SetVisibility(true, false);
		// Same team/role tint as in battle: player-team armor with the role accent. Meshes whose
		// materials lack the MSTeamPrimary/MSRoleAccent parameters keep their imported look.
		VisualMesh->EmptyOverrideMaterials();
		Rig->ApplyAppearance(MSTeam::TeamPlayer, false, 0);
		UMSMeshVisualComponent::ApplyAppearanceParameters(*VisualMesh, this,
			Rig->GetPartColor(TEXT("Torso")), UMSMeshVisualComponent::GetRoleAccentColor(MobileSuit));
		if (MobHandVisuals)
		{
			MobHandVisuals->Initialize(PreviewActor, VisualMesh, Rig, nullptr, Mesh);
			MobHandVisuals->SetPoseOverride(EMSMobHandPose::Open, EMSMobHandPose::Open);
			MobHandVisuals->SetAppearance(Rig->GetPartColor(TEXT("Torso")),
				UMSMeshVisualComponent::GetRoleAccentColor(MobileSuit));
			// Components created after Initialize are not automatically added to a
			// capture's ShowOnly list.
			Capture->ShowOnlyActorComponents(PreviewActor);
		}
		const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
		const float MeshHeight = Bounds.BoxExtent.Z * 2.0f;
		const float RigWorldScaleZ = FMath::Abs(Rig->GetRigRoot()->GetComponentScale().Z);
		const float Scale = MeshHeight > KINDA_SMALL_NUMBER
			? MobileSuit->VisualMeshTargetHeightCm /
				(MeshHeight * FMath::Max(RigWorldScaleZ, UE_SMALL_NUMBER)) : 1.0f;
		const float BottomLocal = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
		VisualMesh->SetRelativeScale3D(FVector(Scale));
		VisualMesh->SetRelativeLocationAndRotation(
			FVector(0.0f, 0.0f, RigRootToSoleCm - BottomLocal),
			FRotator(0.0f, MobileSuit->VisualMeshYawOffsetDeg, 0.0f));
		VisualMesh->RefreshBoneTransforms();
		if (MobHandVisuals)
		{
			MobHandVisuals->Update();
		}
		VisualMesh->SetVisibility(true, false);
		AttachShield();
		CapturePreview();
		return;
	}

	VisualMesh->SetVisibility(false, true);
	if (MobHandVisuals)
	{
		MobHandVisuals->Reset();
	}
	AttachShield();
	const FLinearColor Primary = GetCategoryColor(MobileSuit);
	const FLinearColor Accent = Primary * 0.38f;
	const FLinearColor Head = FLinearColor::LerpUsingHSV(Primary, FLinearColor::White, 0.45f);
	for (const FName PartName : Rig->GetPartNames())
	{
		Rig->SetPartColor(PartName, PartName == TEXT("Head") ? Head
			: (PartName == TEXT("Backpack") || PartName == TEXT("Foot_L") || PartName == TEXT("Foot_R")) ? Accent : Primary);
	}
	CapturePreview();
}

void UMSMobileSuitPreviewRenderer::ConfigureCustomizeView()
{
	if (!Capture || !RenderTarget) return;
	// Reuse the existing isolated lighting setup; only this profile changes the camera/alpha.
	ConfigureSortieView(true);
	// Preserve the pre-existing customize/deck lighting and transparent alpha.
	// These profiles used the original isolated key light and fixed exposure,
	// not the renderer's construction defaults or the new sortie studio lights.
	SetStudioBackdropVisible(false);
	SetSortieLightingEnabled(true);
	SetPreviewLightingChannels(true);
	SetSortieViewFlags(true);
	if (PreviewKeyLight)
	{
		PreviewKeyLight->SetLightingChannels(false, true, false);
		PreviewKeyLight->SetIntensity(18000.0f * MSGO::MSScale * MSGO::MSScale);
		PreviewKeyLight->SetVisibility(true);
	}
	Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Capture->PostProcessSettings.AutoExposureMinBrightness = 4.0f;
	Capture->PostProcessSettings.AutoExposureMaxBrightness = 4.0f;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = false;
	Capture->PostProcessSettings.AmbientCubemap = nullptr;
	Capture->PostProcessSettings.bOverride_AmbientCubemapIntensity = false;
	bCustomizeView = true;
	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor(0,0,0,1);
	RenderTarget->InitAutoFormat(512,640);
	RenderTarget->UpdateResourceImmediate(true);
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; // inverse opacity in A
	Capture->SetRelativeLocation(FVector(430,70,45)*MSGO::MSScale);
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-430,-70,-36.5f)).Rotator());
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(32.f*.5f))*.8f));
	// A transparent suit composited over a distant painted floor appears airborne.
	// Render its support in the same camera, at the exact sole plane used by every mesh.
	if (!CustomizePlatform)
	{
		CustomizePlatform=AddStudioCube(PreviewActor->GetRootComponent(),
			LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")),
			FVector(0,0,RigRootToSoleCm-7),FVector(185,175,14),FRotator::ZeroRotator,FLinearColor(.19f,.23f,.27f));
		StudioBackdrop.Remove(CustomizePlatform);
	}
	if (CustomizePlatform) CustomizePlatform->SetVisibility(true);
	Capture->ShowOnlyActorComponents(PreviewActor);
	CapturePreview();
}

void UMSMobileSuitPreviewRenderer::ConfigureHangarView()
{
	if (!Capture || !RenderTarget || !PreviewActor) return;
	ConfigureSortieView(false);
	BuildHangarStudio(PreviewActor->GetRootComponent());
	if (CustomizePlatform) CustomizePlatform->SetVisibility(false);
	SetStudioBackdropVisible(false);
	SetHangarBackdropVisible(true);
	// Store tone-mapped linear color at half precision, then let Slate encode it
	// once for the display. Linear 8-bit LDR amplified its one-LSB tone-map dither
	// into visible 0/13 sRGB speckles in dark walls. FinalToneCurveHDR also disables
	// backbuffer-quantization dither; no temporal warm-up or CPU readback is needed.
	bCustomizeView = false;
	CustomizeTexture = nullptr;
	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->bForceLinearGamma = true;
	RenderTarget->TargetGamma = 0.f;
	RenderTarget->InitAutoFormat(1920,1080);
	RenderTarget->UpdateResourceImmediate(true);
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	Capture->PostProcessSettings.AutoExposureBias = .5f;
	Capture->PostProcessSettings.AmbientCubemapIntensity = .25f;
	// Static menu captures have no converged Lumen/SSR history. Use the existing
	// key/fill lights and ambient cubemap instead of freezing noisy first samples.
	Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Capture->PostProcessSettings.bOverride_ReflectionMethod = true;
	Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	if (PreviewKeyLight)
	{
		PreviewKeyLight->SetLightingChannels(false, true, false);
		PreviewKeyLight->SetIntensity(18000.f * MSGO::MSScale * MSGO::MSScale);
		PreviewKeyLight->SetCastShadows(true);
		PreviewKeyLight->SetVisibility(true);
	}
	Capture->ShowFlags.SetAntiAliasing(true);
	// UE 5.7 SceneView::SetupAntiAliasingMethod falls back from TSR/TAA to FXAA
	// when TemporalAA is disabled. Selection/rotation-only capture has no stable
	// temporal history; spatial AA avoids first-frame ghosting and warm-up passes.
	Capture->ShowFlags.SetTemporalAA(false);
	// MSGO9 frames 2180/2190: the suit faces SCREEN LEFT from a low view.
	// With model forward +X, the camera belongs on -Y; +Y reverses that framing.
	// Low right-side view, with ceiling undersides
	// visible. These are inferred framing values, not recovered original camera data.
	// Live 1920x1080 comparison D keeps the antenna below the caption and the
	// soles above the rotation buttons, with a light upward view from above the floor.
	Capture->SetRelativeLocation(FVector(500,-148,-65)*MSGO::MSScale);
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-500,148,30)).Rotator());
	Capture->FOVAngle=FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(38.f*.5f))*(1920.f/1080.f)));
	Capture->ShowOnlyActorComponents(PreviewActor);
	CapturePreview();
}

void UMSMobileSuitPreviewRenderer::ConfigureDeckCardView()
{
	ConfigureCustomizeView();
	if (!Capture || !RenderTarget) return;
	if (CustomizePlatform) CustomizePlatform->SetVisibility(false);
	RenderTarget->InitAutoFormat(440,270);
	RenderTarget->UpdateResourceImmediate(true);
	CustomizeTexture = nullptr;
	CloseupProfile = ECloseupProfile::DeckCard;
	Capture->FOVAngle=FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(31.f*.5f))*(440.f/270.f)));
	CapturePreview();
}

void UMSMobileSuitPreviewRenderer::ConfigureDeploymentListView()
{
	ConfigureSortieView(true);
	if (!Capture || !RenderTarget) return;
	// The widget reserves 140 x 32 px, not the sortie selector's 160 x 116.
	// Capture at 2x that resolution without shrinking a full-body portrait into it.
	RenderTarget->InitAutoFormat(280,64);
	RenderTarget->UpdateResourceImmediate(true);
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.SetResourceObject(RenderTarget);
	Brush.ImageSize = FVector2D(280,64);
	if (OutputImage) OutputImage->SetBrush(Brush);
	CloseupProfile = ECloseupProfile::DeploymentList;
	Capture->FOVAngle=FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(16.f*.5f))*(280.f/64.f)));
	CapturePreview();
}

void UMSMobileSuitPreviewRenderer::ApplyCloseupFraming()
{
	if (!Capture || CloseupProfile == ECloseupProfile::None) return;
	const bool bDeck = CloseupProfile == ECloseupProfile::DeckCard;
	// Keep the same -Y viewing side and light upward pitch as the detail hangar.
	// Deck cards retain the head/upper body; the short MS LIST strip intentionally
	// crops to the chest and shoulders. Do not fit to shield/backpack bounds:
	// their size would turn a large-shield suit back into a tiny full-body image.
	// K's first live MS LIST capture put imported suits' abdomen/waist in the
	// strip. Raise both camera and target equally to retain the upward pitch.
	FVector Camera = bDeck ? FVector(245,-72.5f,35) : FVector(140,-41.5f,59.5f);
	FVector Target(0,0,bDeck ? 50.f : 68.f);
	const UMSMobileSuitData* Suit = CurrentMobileSuit.Get();
	const float HeightScale = IsUsingVisualMesh() && Suit
		? FMath::Max(Suit->VisualMeshTargetHeightCm,1.f) / (193.f * MSGO::MSScale) : 1.f;
	const FVector Sole(0,0,RigRootToSoleCm);
	Camera = Sole + (Camera-Sole)*HeightScale;
	Target = Sole + (Target-Sole)*HeightScale;
	Capture->SetRelativeLocation(Camera*MSGO::MSScale);
	Capture->SetRelativeRotation(FRotationMatrix::MakeFromX(Target-Camera).Rotator());
}

void UMSMobileSuitPreviewRenderer::CapturePreview()
{
	if (!Capture) return;
	ApplyCloseupFraming();
	Capture->CaptureScene();
	if (!bCustomizeView || !RenderTarget || !OutputImage || !FApp::CanEverRender()) return;
	// Captures happen on selection changes, never every frame. Read the real scene alpha,
	// invert the capture's background coverage and keep the vector hangar visible behind it.
	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	TArray<FLinearColor> ScenePixels;
	FReadSurfaceDataFlags ReadFlags(RCM_MinMax); ReadFlags.SetLinearToGamma(false);
	const int32 Width=RenderTarget->SizeX, Height=RenderTarget->SizeY;
	if (!Resource || !Resource->ReadLinearColorPixels(ScenePixels,ReadFlags) || ScenePixels.Num()!=Width*Height) return;
	if (!CustomizeTexture || CustomizeTexture->GetSizeX()!=Width || CustomizeTexture->GetSizeY()!=Height)
	{
		CustomizeTexture=UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8);
		CustomizeTexture->SRGB=true;
		CustomizeTexture->Filter=TF_Bilinear;
	}
	FColor* Destination=static_cast<FColor*>(CustomizeTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 I=0;I<ScenePixels.Num();++I)
	{
		FLinearColor C=ScenePixels[I];
		C.A=FMath::Clamp(1.f-C.A,0.f,1.f);
		if(C.A>1.e-4f){C.R/=C.A;C.G/=C.A;C.B/=C.A;}
		C.R=FMath::Max(0.f,C.R);C.G=FMath::Max(0.f,C.G);C.B=FMath::Max(0.f,C.B);
		C.R=C.R/(1.f+C.R);C.G=C.G/(1.f+C.G);C.B=C.B/(1.f+C.B);
		Destination[I]=C.ToFColor(true);
	}
	CustomizeTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	CustomizeTexture->UpdateResource();
	OutputImage->SetBrushFromTexture(CustomizeTexture,true);
}

bool UMSMobileSuitPreviewRenderer::IsUsingVisualMesh() const
{
	return VisualMesh && VisualMesh->IsVisible() && VisualMesh->GetSkinnedAsset() != nullptr;
}

bool UMSMobileSuitPreviewRenderer::IsInitialized() const
{
	return IsValid(PreviewActor) && !PreviewActor->IsActorBeingDestroyed()
		&& IsValid(Rig) && IsValid(VisualMesh) && IsValid(Capture) && IsValid(RenderTarget);
}

void UMSMobileSuitPreviewRenderer::RotatePreview(const float DeltaYawDegrees)
{
	if (!IsInitialized() || !Rig->GetRigRoot() || !FMath::IsFinite(DeltaYawDegrees)) return;
	PreviewYaw = FMath::UnwindDegrees(PreviewYaw + DeltaYawDegrees);
	// Rotate the suit and its attached hands/shield, not the studio camera.
	Rig->GetRigRoot()->SetRelativeRotation(FRotator(0.0f, PreviewYaw, 0.0f));
	CapturePreview();
}

bool UMSMobileSuitPreviewRenderer::IsNativeShieldVisible() const
{
	return NativeShieldMesh && NativeShieldMesh->IsVisible();
}

bool UMSMobileSuitPreviewRenderer::AreRigPartsVisible() const
{
	if (!Rig) return false;
	for (const FName PartName : Rig->GetPartNames())
	{
		if (const UStaticMeshComponent* Part = Rig->GetPart(PartName); Part && Part->IsVisible()) return true;
	}
	return false;
}

void UMSMobileSuitPreviewRenderer::Shutdown()
{
	CurrentMobileSuit.Reset();
	PreviewYaw = 0.0f;
	bCustomizeView = false;
	CloseupProfile = ECloseupProfile::None;
	CustomizeTexture = nullptr;
	StudioLights.Reset();
	StudioBackdrop.Reset();
	HangarBackdrop.Reset();
	NativeShieldMesh = nullptr;
	StudioMaterials.Reset();
	PreviewKeyLight = nullptr;
	CustomizePlatform = nullptr;
	OutputImage = nullptr;
	Capture = nullptr;
	if (MobHandVisuals)
	{
		MobHandVisuals->Reset();
	}
	MobHandVisuals = nullptr;
	VisualMesh = nullptr;
	Rig = nullptr;
	if (PreviewActor)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}
	RenderTarget = nullptr;
}

void UMSMobileSuitPreviewRenderer::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}
