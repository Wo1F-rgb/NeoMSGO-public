// Copyright NeoMSGO Project. All Rights Reserved.
//
// 短命コンバットVFX用の実行時生成アクター。
// 基本エフェクトは /Engine/BasicShapes + UMaterialInstanceDynamic + PointLight、
// スラスター煙はCC0テクスチャのMaterial Billboardで構成する。
// DrawDebug系と異なり、いずれもShippingビルドで描画されるコンポーネントを使用する。
//
// 各エフェクトは EffectLifetime 秒でフェード(スケール/光量のLerp)し、
// SetLifeSpanで確実に破棄される(メモリリーク防止)。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MSTransientVfxActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialBillboardComponent;
class UNiagaraComponent;
class UPointLightComponent;
class UPoseableMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

enum class EMSImpactVisualStyle : uint8
{
	Kinetic,
	Beam,
	Explosive
};

/**
 * 発光プリミティブ、テクスチャBillboard、ポイントライトで構成される短命エフェクトアクター。
 * SpawnTracer / SpawnMuzzleFlash / SpawnImpactSparks / SpawnExplosion の
 * 静的ファクトリ経由で生成し、寿命経過後は自動破棄される。
 */
UCLASS(NotBlueprintable)
class NEOMSGO_API AMSTransientVfxActor : public AActor
{
	GENERATED_BODY()

public:
	AMSTransientVfxActor();

	virtual void Tick(float DeltaSeconds) override;

	/** 即着弾道トレーサー: Start→Endを結ぶ発光ライン(色付きハロー+白コア)。WidthCmはUnreal単位(cm)。 */
	static AMSTransientVfxActor* SpawnTracer(UWorld* World, const FVector& Start, const FVector& End,
		const FLinearColor& Color, float WidthCm, float LifetimeSeconds);

	/** マズルフラッシュ: 銃口位置に短時間表示される発光コア+前方ペタル。 */
	static AMSTransientVfxActor* SpawnMuzzleFlash(UWorld* World, const FVector& Location, const FVector& Forward);

	/** 着弾スパーク: 中心の発光Cube+放射状に飛散しながら縮む小Cube群。 */
	static AMSTransientVfxActor* SpawnImpactSparks(UWorld* World, const FVector& Location,
		const FLinearColor& Color, bool bStrong, float LifetimeSeconds,
		EMSImpactVisualStyle Style = EMSImpactVisualStyle::Kinetic);

	/** Requests the current water Niagara asset before the first splash. Never blocks the game thread. */
	static void PreloadWaterFootSplash(UWorld* World);

	/** Surface foam sheets and fine spray for entry or sustained partial immersion. */
	static AMSTransientVfxActor* SpawnWaterFootSplash(UWorld* World, const FVector& Location,
		float IntensityScale, bool bEntrySplash, float LifetimeSeconds);

	/** CC0炎アニメーション、遅延煙、接地粉塵、破片。RadiusCmはMSScale適用前。判定を持たない。 */
	static AMSTransientVfxActor* SpawnExplosion(UWorld* World, const FVector& Location,
		float RadiusCm, float LifetimeSeconds);
	/** Legacy radius conversion helpers, retained for existing callers. No sphere is rendered. */
	static FVector CalculateExplosionOuterInitialScale(float RadiusCm);
	static FVector CalculateExplosionOuterMaximumScale(float RadiusCm);

	/**
	 * ブースト残像(#72): SourceActorの可視StaticMesh群(Cubeリグ機体)のワールド姿勢を複製した
	 * ゴーストをその場に残し、寿命で縮みながら消える。ライトは持たない(多数スポーンのため)。
	 */
	static AMSTransientVfxActor* SpawnBoostAfterimage(UWorld* World, const AActor* SourceActor,
		const FLinearColor& Color, float LifetimeSeconds);

	/** Kenney CC0のアルファ付き煙を使う、カメラ正対Billboardパフ。 */
	static AMSTransientVfxActor* SpawnThrusterSmokePuff(UWorld* World, const FVector& Location,
		const FVector& Backward, const FVector& Up, int32 Variant, float LifetimeSeconds);

	/** Persistent orange ring used for the full emergency-recall countdown. */
	static AMSTransientVfxActor* SpawnRecallRing(UWorld* World, const FVector& Location,
		float LifetimeSeconds);

protected:
	/**
	 * 発光Cube要素を1つ追加する。スケールはEffectLifetime経過でStartScale→EndScaleへLerpし、
	 * VelocityCmPerSecが非ゼロなら相対位置が毎秒その分だけ移動する(スパーク飛散用)。
	 */
	UStaticMeshComponent* AddCubeElement(const FVector& RelLocation, const FRotator& RelRotation,
		const FVector& StartScale, const FVector& EndScale, const FLinearColor& Color,
		const FVector& VelocityCmPerSec = FVector::ZeroVector,
		const FVector& AccelerationCmPerSecSq = FVector::ZeroVector);
	UStaticMeshComponent* AddSphereElement(const FVector& RelLocation, const FVector& StartScale,
		const FVector& EndScale, const FLinearColor& Color,
		const FVector& VelocityCmPerSec = FVector::ZeroVector,
		const FVector& AccelerationCmPerSecSq = FVector::ZeroVector,
		const FRotator& RelRotation = FRotator::ZeroRotator);
	UMaterialBillboardComponent* AddSmokeBillboard(const FVector& RelLocation,
		float StartSizeCm, float EndSizeCm, const FLinearColor& Color, float Opacity,
		const FVector& VelocityCmPerSec, int32 TextureVariant,
		float DelaySeconds = 0.0f, float LifetimeSeconds = 0.0f);
	UStaticMeshComponent* AddWaterSheet(const FVector& RelLocation, const FRotator& Rotation,
		const FVector& StartScale, const FVector& EndScale, UMaterialInterface* Material,
		float Opacity, const FVector& Velocity, const FVector& Acceleration);
	void AddExplosionFlame(FName Name, const FVector& Location, float SizeCm,
		float DelaySeconds, float DurationSeconds, float Opacity, int32 FirstFrame = 0);
	bool AddPoseableMeshAfterimage(UPoseableMeshComponent& SourceMesh,
		const FTransform& SourceActorTransform, const FLinearColor& Color);

	/** GlowLightの色/強度/減衰半径を設定する。強度はEffectLifetime経過で0へフェードする。 */
	void ConfigureLight(const FLinearColor& Color, float Intensity, float AttenuationRadius);

	/** エフェクト寿命を設定し、SetLifeSpanで破棄を予約する。 */
	void SetEffectLifetime(float Seconds);

private:
	static AMSTransientVfxActor* SpawnAt(UWorld* World, const FVector& Location, const FRotator& Rotation);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> GlowLight;

	/** #230: Niagara detail pass for the water splash (mesh foam stays as the fallback). */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> WaterNiagara;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> WaterPlaneMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterFoamMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterSprayMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ExplosionSheetMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ExplosionSheetInstances;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialBillboardComponent>> ExplosionFlames;
	TArray<FVector> ExplosionFlameOrigins;
	TArray<float> ExplosionFlameDelays;
	TArray<float> ExplosionFlameDurations;
	TArray<float> ExplosionFlameOpacities;
	TArray<int32> ExplosionFlameFirstFrames;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> SmokeTextures;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CubeElements;

	TArray<FVector> ElementBaseLocations;
	TArray<FVector> ElementVelocities;
	TArray<FVector> ElementAccelerations;
	TArray<FVector> ElementStartScales;
	TArray<FVector> ElementEndScales;
	TArray<FLinearColor> ElementColors;
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ElementMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UPoseableMeshComponent>> PoseableMeshElements;
	TArray<FVector> PoseableMeshStartScales;
	TArray<FLinearColor> PoseableMeshColors;
	TArray<TArray<TObjectPtr<UMaterialInstanceDynamic>>> PoseableMeshMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialBillboardComponent>> SmokeBillboards;
	TArray<FVector> SmokeBaseLocations;
	TArray<FVector> SmokeVelocities;
	TArray<float> SmokeStartSizes;
	TArray<float> SmokeEndSizes;
	TArray<float> SmokeOpacities;
	TArray<float> SmokeDelays;
	TArray<float> SmokeLifetimes;
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SmokeMaterials;

	float EffectLifetime = 0.15f;
	float ElapsedSeconds = 0.0f;
	float InitialLightIntensity = 0.0f;
	bool bFadeOut = true;
};
