#pragma once
#include "CoreMinimal.h"
#include "Math/IntRect.h"
#include "Subsystems/WorldSubsystem.h"
#include "MSWeaponPhotoSubsystem.generated.h"
class UMSWeaponDataBase;
class UTexture2D;
class UTextureCube;

/** One real-mesh photograph per mesh per world; never used by the combat HUD. */
UCLASS()
class NEOMSGO_API UMSWeaponPhotoSubsystem : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 UMSWeaponPhotoSubsystem();
 UTexture2D* GetPhoto(const UMSWeaponDataBase* Weapon);

 /** Returns the exclusive pixel bounds of non-transparent content. */
 static FIntRect FindOpaqueBounds(const TArray<FColor>& Pixels,int32 Width,int32 Height,uint8 AlphaThreshold=8);
 /** Compresses HDR brightness with one common scale, preserving the source RGB ratios. */
 static FLinearColor ToneMapPhotoColor(const FLinearColor& LinearColor);
private:
 UPROPERTY() TObjectPtr<UTextureCube> StudioAmbientCubemap;
 UPROPERTY(Transient) TMap<FString,TObjectPtr<UTexture2D>> Photos;
};
