#include "UI/MSWeaponPhotoSubsystem.h"
#include "Data/MSWeaponData.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialRenderProxy.h"
#include "RenderingThread.h"

namespace
{
 TAutoConsoleVariable<int32> CVarWeaponPhotoDiagnostics(
  TEXT("ms.WeaponPhoto.Diagnostics"),0,
  TEXT("Opt-in photo capture diagnostics under Saved/WeaponPhotoDiagnostics. Change the positive serial to recapture cached photos."),ECVF_Default);

 // IsCompiling() only reports already queued jobs. A material can still have an
 // incomplete render-thread shader map (including an instance's parent) and draw
 // the default grey material. Ask the same proxy API used by the renderer; this
 // also submits outstanding on-demand shader jobs without blocking their compile.
 bool ArePhotoMaterialsReady(const UStaticMesh* Mesh,const ERHIFeatureLevel::Type FeatureLevel)
 {
  TArray<const FMaterialRenderProxy*> Proxies;
  for(int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
  {
   const UMaterialInterface* Material=Mesh->GetMaterial(Slot);
   const FMaterialRenderProxy* Proxy=Material?Material->GetRenderProxy():nullptr;
   if(!Proxy)return false;
   Proxies.Add(Proxy);
  }
  int32 FallbackSlot=INDEX_NONE;
  ENQUEUE_RENDER_COMMAND(MSCheckWeaponPhotoMaterials)(
   [Proxies=MoveTemp(Proxies),FeatureLevel,&FallbackSlot](FRHICommandListImmediate&)
   {
    for(int32 Slot=0;Slot<Proxies.Num();++Slot)
    {
     const FMaterialRenderProxy* Fallback=nullptr;
     Proxies[Slot]->GetMaterialWithFallback(FeatureLevel,Fallback);
     if(Fallback&&FallbackSlot==INDEX_NONE)FallbackSlot=Slot;
    }
   });
  // The photo readback already synchronizes rendering. This fence only checks
  // resource readiness; it never waits for shader compilation to finish.
  FlushRenderingCommands();
  if(FallbackSlot!=INDEX_NONE)
   UE_LOG(LogTemp,Verbose,TEXT("[WeaponPhoto] deferring render-material fallback: mesh=%s slot=%d"),*Mesh->GetPathName(),FallbackSlot);
  return FallbackSlot==INDEX_NONE;
 }

 TSharedRef<FJsonObject> SummarizePhotoPixels(const TArray<FLinearColor>& Values,const TArray<FLinearColor>& Scene)
 {
  TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
  TArray<float> Maxima;
  FVector Sum=FVector::ZeroVector;
  int32 Orange=0,Blue=0,Neutral=0,NonFinite=0;
  float Peak=0.0f;
  for(int32 Index=0;Index<Values.Num()&&Index<Scene.Num();++Index)
  {
   // Statistics exclude coverage edges and background; inverse opacity is
   // obtained from the same SceneColorHDR pass for every capture source.
   if(1.0f-Scene[Index].A<0.95f)continue;
   const FLinearColor& C=Values[Index];
   if(!FMath::IsFinite(C.R)||!FMath::IsFinite(C.G)||!FMath::IsFinite(C.B)){++NonFinite;continue;}
   const float Hi=FMath::Max3(C.R,C.G,C.B),Lo=FMath::Min3(C.R,C.G,C.B);
   Maxima.Add(Hi);Peak=FMath::Max(Peak,Hi);Sum+=FVector(C.R,C.G,C.B);
   if(C.R>0.03f&&C.R>C.G*1.25f&&C.R>C.B*1.6f)++Orange;
   if(C.B>0.003f&&C.B>C.R*1.25f&&C.B>C.G*1.05f)++Blue;
   if(Hi>0.001f&&(Hi-Lo)/Hi<0.12f)++Neutral;
  }
  Maxima.Sort();
  Result->SetNumberField(TEXT("interior_pixels"),Maxima.Num());
  Result->SetNumberField(TEXT("non_finite_pixels"),NonFinite);
  Result->SetNumberField(TEXT("orange_pixels"),Orange);
  Result->SetNumberField(TEXT("blue_pixels"),Blue);
  Result->SetNumberField(TEXT("near_neutral_pixels"),Neutral);
  Result->SetNumberField(TEXT("peak_linear_rgb"),Peak);
  if(!Maxima.IsEmpty())
  {
   const FVector Average=Sum/Maxima.Num();
   Result->SetNumberField(TEXT("mean_r"),Average.X);Result->SetNumberField(TEXT("mean_g"),Average.Y);Result->SetNumberField(TEXT("mean_b"),Average.Z);
   Result->SetNumberField(TEXT("median_max_rgb"),Maxima[Maxima.Num()/2]);
   Result->SetNumberField(TEXT("p95_max_rgb"),Maxima[FMath::Min(Maxima.Num()-1,FMath::FloorToInt(Maxima.Num()*0.95f))]);
  }
  return Result;
 }

 void SaveWeaponPhotoDiagnostics(const UStaticMesh* Mesh,const FString& Key,const int32 Serial,
  const TArray<FLinearColor>& Scene,const TArray<FLinearColor>& BaseColor,const TArray<FColor>& Final)
 {
  constexpr int32 Width=384,Height=192;
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("WeaponPhotoDiagnostics")/
   FString::Printf(TEXT("%s_%08x_%d"),*Mesh->GetName(),FCrc::StrCrc32(*Key),Serial);
  IFileManager::Get().MakeDirectory(*Directory,true);
  TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
  Report->SetStringField(TEXT("mesh"),Mesh->GetPathName());
  Report->SetStringField(TEXT("source"),TEXT("same registered mesh/slots, camera and inverse-opacity mask; no replacement materials"));
  Report->SetObjectField(TEXT("scene_hdr"),SummarizePhotoPixels(Scene,Scene));
  Report->SetBoolField(TEXT("scene_exr_saved"),FImageUtils::SaveImageByExtension(*(Directory/TEXT("SceneHDR.exr")),FImageView(Scene.GetData(),Width,Height)));
  Report->SetBoolField(TEXT("photo_png_saved"),FImageUtils::SaveImageByExtension(*(Directory/TEXT("Photo.png")),FImageView(Final.GetData(),Width,Height)));
  if(BaseColor.Num()==Scene.Num())
  {
   Report->SetObjectField(TEXT("base_color"),SummarizePhotoPixels(BaseColor,Scene));
   TArray<FColor> BasePixels;BasePixels.Reserve(BaseColor.Num());
   for(int32 Index=0;Index<BaseColor.Num();++Index)
   {
    FLinearColor C=BaseColor[Index];C.A=FMath::Clamp(1.0f-Scene[Index].A,0.0f,1.0f);
    BasePixels.Add(C.ToFColor(true));
   }
   Report->SetBoolField(TEXT("base_color_png_saved"),FImageUtils::SaveImageByExtension(*(Directory/TEXT("BaseColor.png")),FImageView(BasePixels.GetData(),Width,Height)));
   Report->SetBoolField(TEXT("base_color_exr_saved"),FImageUtils::SaveImageByExtension(*(Directory/TEXT("BaseColor.exr")),FImageView(BaseColor.GetData(),Width,Height)));
  }
  TArray<TSharedPtr<FJsonValue>> Slots;
  const FStaticMeshRenderData* RenderData=Mesh->GetRenderData();
  for(int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
  {
   TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
   Row->SetNumberField(TEXT("slot"),Slot);
   Row->SetStringField(TEXT("slot_name"),Mesh->GetStaticMaterials()[Slot].MaterialSlotName.ToString());
   const UMaterialInterface* Material=Mesh->GetMaterial(Slot);
   Row->SetStringField(TEXT("material"),GetPathNameSafe(Material));
   int32 Triangles=0;
   if(RenderData&&!RenderData->LODResources.IsEmpty())
    for(const FStaticMeshSection& Section:RenderData->LODResources[0].Sections)
     if(Section.MaterialIndex==Slot)Triangles+=Section.NumTriangles;
   Row->SetNumberField(TEXT("lod0_triangles"),Triangles);
   TArray<TSharedPtr<FJsonValue>> Textures;
   TArray<UTexture*> Used;if(Material)Material->GetUsedTextures(Used);
   for(const UTexture* UsedTexture:Used)
    if(const UTexture2D* Texture=Cast<UTexture2D>(UsedTexture))
    {
     TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>();
     Item->SetStringField(TEXT("path"),Texture->GetPathName());
     Item->SetNumberField(TEXT("size_x"),Texture->GetSizeX());Item->SetNumberField(TEXT("size_y"),Texture->GetSizeY());
     Item->SetNumberField(TEXT("resident_mips"),Texture->GetNumResidentMips());Item->SetNumberField(TEXT("total_mips"),Texture->GetNumMips());
     Item->SetBoolField(TEXT("srgb"),Texture->SRGB);Textures.Add(MakeShared<FJsonValueObject>(Item));
    }
   Row->SetArrayField(TEXT("textures"),Textures);Slots.Add(MakeShared<FJsonValueObject>(Row));
  }
  Report->SetArrayField(TEXT("material_slots"),Slots);
  FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Report,Writer);
  FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("Diagnostics.json")));
  UE_LOG(LogTemp,Display,TEXT("[WeaponPhoto] diagnostic capture saved: %s"),*Directory);
 }
}

UMSWeaponPhotoSubsystem::UMSWeaponPhotoSubsystem()
{
 // The metallic weapon materials need broad reflected light as well as the
 // studio point lights. Keep the same neutral cubemap used by suit previews in cooks.
 static ConstructorHelpers::FObjectFinder<UTextureCube> Ambient(
  TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
 StudioAmbientCubemap=Ambient.Object;
}

FLinearColor UMSWeaponPhotoSubsystem::ToneMapPhotoColor(const FLinearColor& LinearColor)
{
 const float Red=FMath::Max(0.0f,LinearColor.R);
 const float Green=FMath::Max(0.0f,LinearColor.G);
 const float Blue=FMath::Max(0.0f,LinearColor.B);
 // Per-channel x/(1+x) turns bright olive, gold and blue highlights into grey.
 const float Scale=1.0f/(1.0f+FMath::Max3(Red,Green,Blue));
 return FLinearColor(Red*Scale,Green*Scale,Blue*Scale,LinearColor.A);
}

FIntRect UMSWeaponPhotoSubsystem::FindOpaqueBounds(const TArray<FColor>& Pixels,const int32 Width,const int32 Height,const uint8 AlphaThreshold)
{
 if (Width<=0 || Height<=0 || Pixels.Num()!=Width*Height) return FIntRect(0,0,0,0);
 int32 MinX=Width,MinY=Height,MaxX=0,MaxY=0;
 for(int32 Y=0;Y<Height;++Y)
 {
  for(int32 X=0;X<Width;++X)
  {
   if(Pixels[Y*Width+X].A<=AlphaThreshold) continue;
   MinX=FMath::Min(MinX,X); MinY=FMath::Min(MinY,Y);
   MaxX=FMath::Max(MaxX,X+1); MaxY=FMath::Max(MaxY,Y+1);
  }
 }
 return MinX==Width ? FIntRect(0,0,0,0) : FIntRect(MinX,MinY,MaxX,MaxY);
}

UTexture2D* UMSWeaponPhotoSubsystem::GetPhoto(const UMSWeaponDataBase* Weapon)
{
 if (!Weapon || !GetWorld() || !FApp::CanEverRender() || GetWorld()->GetNetMode()==NM_DedicatedServer) return nullptr;
 UStaticMesh* Mesh=Weapon->WeaponMesh.LoadSynchronous();
 if (!Mesh) return nullptr;
 const int32 DiagnosticSerial=CVarWeaponPhotoDiagnostics.GetValueOnGameThread();
 const FString Key=Mesh->GetPathName()+Weapon->WeaponMeshTransform.ToString()+FString::Printf(TEXT("_diagnostic_%d"),DiagnosticSerial);
 if (const auto* Found=Photos.Find(Key)) return *Found;
 // A compiling source material can produce a grey fallback frame. Never cache it.
 if (Mesh->HasPendingInitOrStreaming()) return nullptr;
#if WITH_EDITOR
 if (Mesh->IsCompiling()) return nullptr;
#endif
 for (int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
 {
  const UMaterialInterface* Material=Mesh->GetMaterial(Slot);
  if (!Material || Material->IsCompiling() ||
   Material->GetPathName().Contains(TEXT("/Engine/EngineMaterials/DefaultMaterial")))
  {
   UE_LOG(LogTemp,Verbose,TEXT("[WeaponPhoto] waiting for source material: mesh=%s slot=%d material=%s"),
    *Mesh->GetPathName(),Slot,Material?*Material->GetPathName():TEXT("none"));
   return nullptr;
  }
  TArray<UTexture*> UsedTextures;
  Material->GetUsedTextures(UsedTextures);
  for (const UTexture* Texture : UsedTextures)
  {
   if (const UTexture2D* ColorTexture=Cast<UTexture2D>(Texture);
    ColorTexture && ColorTexture->HasPendingInitOrStreaming())
   {
    UE_LOG(LogTemp,Verbose,TEXT("[WeaponPhoto] waiting for texture: mesh=%s texture=%s"),
     *Mesh->GetPathName(),*ColorTexture->GetPathName());
    return nullptr;
   }
  }
 }
 if(!ArePhotoMaterialsReady(Mesh,GetWorld()->GetFeatureLevel()))return nullptr;
 FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient;
 Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 AActor* Studio=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FVector(0,0,-2000000),FRotator::ZeroRotator,Params);
 if (!Studio) return nullptr;
 USceneComponent* Root=NewObject<USceneComponent>(Studio); Studio->AddInstanceComponent(Root); Studio->SetRootComponent(Root); Root->RegisterComponent();
 UStaticMeshComponent* Subject=NewObject<UStaticMeshComponent>(Studio); Studio->AddInstanceComponent(Subject); Subject->SetupAttachment(Root);
 Subject->SetMobility(EComponentMobility::Movable);Subject->SetStaticMesh(Mesh); Subject->SetCollisionEnabled(ECollisionEnabled::NoCollision); Subject->SetCastShadow(false); Subject->SetLightingChannels(false,true,false);
 for (int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
  Subject->SetMaterial(Slot,Mesh->GetMaterial(Slot));
 const FQuat MeshRotation=FRotator(90,180,0).Quaternion()*Weapon->WeaponMeshTransform.GetRotation();
 const FVector MeshScale=Weapon->WeaponMeshTransform.GetScale3D();
 const FBoxSphereBounds Bounds=Mesh->GetBounds().TransformBy(FTransform(MeshRotation,FVector::ZeroVector,MeshScale));
 const double Scale=100.0/FMath::Max(Bounds.SphereRadius,1.0);
 Subject->SetRelativeRotation(MeshRotation);Subject->SetRelativeScale3D(MeshScale*Scale); Subject->SetRelativeLocation(-Bounds.Origin*Scale);
 // Request PSOs before creating a proxy, so it cannot retain a temporary default
 // material or be absent during its first capture. The widget retries nullptr.
 Subject->PrecachePSOs();
 if(Subject->CheckPSOPrecachingAndBoostPriority())
 {
  UE_LOG(LogTemp,Verbose,TEXT("[WeaponPhoto] deferring component PSO preparation: %s"),*Mesh->GetPathName());
  Studio->Destroy();return nullptr;
 }
 Subject->RegisterComponent();
 // Registration may request an additional permutation. Discard this studio if
 // so, even if the asynchronous completion races the later rendering fence.
 if(Subject->CheckPSOPrecachingAndBoostPriority()||!ArePhotoMaterialsReady(Mesh,GetWorld()->GetFeatureLevel()))
 {
  Studio->Destroy();return nullptr;
 }
 auto Light=[&](FVector Location,float Intensity,FLinearColor Color){
  auto* L=NewObject<UPointLightComponent>(Studio); Studio->AddInstanceComponent(L); L->SetupAttachment(Root); L->SetRelativeLocation(Location);
  L->SetMobility(EComponentMobility::Movable);L->SetIntensity(Intensity); L->SetAttenuationRadius(1500); L->SetLightColor(Color); L->SetCastShadows(false); L->SetLightingChannels(false,true,false); L->RegisterComponent();
 };
 Light({180,-180,260},45000,FLinearColor(1,.97f,.94f)); Light({-140,160,100},30000,FLinearColor(.85f,.93f,1));
 UTextureRenderTarget2D* RT=NewObject<UTextureRenderTarget2D>(this); RT->RenderTargetFormat=RTF_RGBA16f; RT->ClearColor=FLinearColor(0,0,0,1); RT->InitAutoFormat(384,192); RT->UpdateResourceImmediate(true);
 auto* Capture=NewObject<USceneCaptureComponent2D>(Studio); Studio->AddInstanceComponent(Capture); Capture->SetupAttachment(Root);
 Capture->TextureTarget=RT; Capture->CaptureSource=SCS_SceneColorHDR; Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList; Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
 Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;
 Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;
 Capture->PostProcessSettings.AutoExposureMinBrightness=1.0f;
 Capture->PostProcessSettings.AutoExposureMaxBrightness=1.0f;
 Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
 Capture->PostProcessSettings.AutoExposureBias=0.0f;
 Capture->PostProcessSettings.AmbientCubemap=StudioAmbientCubemap;
 Capture->PostProcessSettings.bOverride_AmbientCubemapIntensity=true;
 Capture->PostProcessSettings.AmbientCubemapIntensity=1.0f;
 Capture->PostProcessBlendWeight=1.0f;
 // Match the game's +Z-authored weapon correction before framing muzzle, side and top.
 const FVector Direction=FVector(.65,-1,.48).GetSafeNormal();
 const FRotator Rotation=FRotationMatrix::MakeFromX(-Direction).Rotator();
 const FVector Right=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y),Up=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
 const FVector Extent=Bounds.BoxExtent*Scale;
 const float HalfWidth=FVector::DotProduct(Right.GetAbs(),Extent),HalfHeight=FVector::DotProduct(Up.GetAbs(),Extent);
 Capture->ProjectionType=ECameraProjectionMode::Perspective;Capture->FOVAngle=35;
 const float Distance=FMath::Max(HalfWidth,HalfHeight*2)*1.16f/FMath::Tan(FMath::DegreesToRadians(17.5f))+100;
 Capture->SetRelativeLocation(Direction*Distance); Capture->SetRelativeRotation(Rotation);
 TArray<FEngineShowFlagsSetting> Flags;
 for(const TCHAR* Name:{TEXT("Fog"),TEXT("Atmosphere"),TEXT("Cloud"),TEXT("VolumetricFog"),TEXT("SkyLighting")}){FEngineShowFlagsSetting F;F.ShowFlagName=Name;F.Enabled=false;Flags.Add(F);}
 Capture->SetShowFlagSettings(Flags); Capture->RegisterComponent(); Capture->ShowOnlyComponent(Subject);
 // New components are registered and photographed in one call, before the world's
 // normal end-of-frame update. Submit their transforms before drawing the capture.
 GetWorld()->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
 TArray<FLinearColor> Pixels; FReadSurfaceDataFlags Read(RCM_MinMax); Read.SetLinearToGamma(false);
 const bool bRead=RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Read);
 TArray<FLinearColor> BaseColorPixels;
 if(DiagnosticSerial>0&&bRead&&Pixels.Num()==384*192)
 {
  // Separate material/UV/streaming errors from lighting and CPU tone mapping.
  // BaseColor alpha is undefined for masking; diagnostics reuse the HDR opacity.
  Capture->CaptureSource=SCS_BaseColor;Capture->CaptureScene();
  RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(BaseColorPixels,Read);
 }
 Studio->Destroy();
 if(!bRead || Pixels.Num()!=384*192) return nullptr;
 TArray<FColor> PhotoPixels; PhotoPixels.SetNumUninitialized(Pixels.Num());
 int32 VisiblePixels=0;
 for(int32 I=0;I<Pixels.Num();++I)
 {
  auto C=Pixels[I];
  C.A=FMath::Clamp(1.f-C.A,0.f,1.f);
  if(C.A>.01f) ++VisiblePixels;
  if(C.A>1.e-4f){C.R/=C.A;C.G/=C.A;C.B/=C.A;}
  C=ToneMapPhotoColor(C);
  PhotoPixels[I]=C.ToFColor(true);
 }
 UE_LOG(LogTemp,Display,TEXT("[WeaponPhoto] %s visible=%d / %d"),*Key,VisiblePixels,Pixels.Num());
 if(DiagnosticSerial>0)SaveWeaponPhotoDiagnostics(Mesh,Key,DiagnosticSerial,Pixels,BaseColorPixels,PhotoPixels);
 if(VisiblePixels<8)return nullptr; // never cache a blank capture as a finished photo
 const FIntRect Content=FindOpaqueBounds(PhotoPixels,384,192);
 if(Content.Width()<=0 || Content.Height()<=0) return nullptr;
 // Keep a small anti-aliased edge margin, then let each presentation frame
 // apply its own cover crop. This removes the capture's transparent border
 // without distorting weapons with different proportions.
 const int32 Padding=FMath::Clamp(FMath::Max(Content.Width(),Content.Height())/32,3,12);
 const FIntRect Crop(
  FMath::Max(0,Content.Min.X-Padding),FMath::Max(0,Content.Min.Y-Padding),
  FMath::Min(384,Content.Max.X+Padding),FMath::Min(192,Content.Max.Y+Padding));
 const int32 CropWidth=FMath::Max(1,Crop.Width()),CropHeight=FMath::Max(1,Crop.Height());
 UTexture2D* Photo=UTexture2D::CreateTransient(CropWidth,CropHeight,PF_B8G8R8A8); Photo->SRGB=true;Photo->Filter=TF_Bilinear;
 FColor* Out=static_cast<FColor*>(Photo->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
 for(int32 Y=0;Y<CropHeight;++Y)
  for(int32 X=0;X<CropWidth;++X)
   Out[Y*CropWidth+X]=PhotoPixels[(Crop.Min.Y+Y)*384+(Crop.Min.X+X)];
 Photo->GetPlatformData()->Mips[0].BulkData.Unlock();Photo->UpdateResource();
 UE_LOG(LogTemp,Display,TEXT("[WeaponPhoto] %s content=%dx%d crop=%dx%d"),*Key,Content.Width(),Content.Height(),CropWidth,CropHeight);
 Photos.Add(Key,Photo);return Photo;
}
