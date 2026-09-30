// Copyright NeoMSGO Project. All Rights Reserved.
//
// P4-12: 照射型ビーム(ContinuousBeam「ゲロビ」)用 GameplayCue(Looping/Actor)。
// 設計元: docs/UE5_MIGRATION_PLAN.md P4-12、docs/ArchitectureMap.md §4-6, §8
//         (Unity: ContinuousBeamVisual = 動的生成LineRenderer)
//
// 恒久実装: 実行時生成StaticMeshComponent(発光Cube、AMSVisualProjectile::ConfigureVisuals
//   と同じ /Engine/BasicShapes/Cube + UMaterialInstanceDynamic + UPointLightComponent
//   パターン)を始点/終点間に毎tick再配置・再スケールして描画する。DrawDebug系と異なり
//   Shipping相当でも描画される。UpdateBeamEndpointsのシグネチャ(Start/End更新のみ)は
//   変更していないため、内部実装のみの差し替えで完結している。
//
// データ駆動性(P4-12 AC1):
//   Weapon->TracerColor / TracerWidth を Parameters.SourceObject(UMSContinuousBeamWeaponData/
//   UMSRangedWeaponData)から読み取る。BeamRadiusが設定されていればTracerWidthより優先して
//   ビーム幅として使用する(ビーム武器はBeamRadiusが実際の判定半径のため)。
//
// 接続について(P4-12 AC2):
//   本CueクラスはGA_Fire_ContinuousBeamから発火できる設計(GameplayCueTag=
//   MSTags::Cue_Weapon_BeamLoop、UpdateBeamEndpointsで毎tick始点/終点を更新できるAPI)を
//   備えているが、GA_Fire_ContinuousBeam::FireTick からの実際の
//   ExecuteGameplayCue/AddGameplayCue呼び出し配線はP4-13で行う(今回はクラスの存在と
//   設計まで)。

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "MSGameplayCue_Beam.generated.h"

class UMaterialInterface;
class UMSRangedWeaponData;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 照射型ビームの見た目(始点〜終点の線分)をトリガーが押されている間描画し続けるCue。
 * GameplayCueTag: GameplayCue.MS.Weapon.BeamLoop (MSTags::Cue_Weapon_BeamLoop)
 *
 * AGameplayCueNotify_Actor派生(Looping)を選んだ理由: 照射ビームは発射者が入力を保持している間
 * 毎tick始点/終点が変化するため、状態(現在の始点/終点)を保持できるインスタンス化Actorが必要
 * (UGameplayCueNotify_Staticは状態を持てないBurst向け設計のため不適)。
 */
UCLASS()
class NEOMSGO_API AMSGameplayCue_Beam : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:
	AMSGameplayCue_Beam();

	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * ビームの現在の始点/終点を更新する。GA_Fire_ContinuousBeam::FireTick からBeamTickInterval
	 * 毎に呼び出される想定の公開API(実際の配線はP4-13。P4-12時点ではクラス設計のみ)。
	 */
	UFUNCTION(BlueprintCallable, Category = "MS|VFX")
	void UpdateBeamEndpoints(const FVector& InStart, const FVector& InEnd);

protected:
	/** Parameters.SourceObjectから武器データを解決する。取得できなければnullptr。 */
	static const UMSRangedWeaponData* ResolveWeaponData(const FGameplayCueParameters& Parameters);

	/** 武器データからビーム幅(m)を取り出す。BeamRadius(ContinuousBeamWeaponData)優先、無ければTracerWidth。 */
	static float ResolveBeamWidthMeters(const UMSRangedWeaponData* Weapon);

private:
	/** BeamBody/BeamCore/GlowLightを初回Tick時に遅延生成する(コンストラクタ時点ではまだ武器データが不明なため)。 */
	void EnsureVisualComponentsCreated();

	UPROPERTY()
	TWeakObjectPtr<const UMSRangedWeaponData> WeaponData;

	FVector BeamStart = FVector::ZeroVector;
	FVector BeamEnd = FVector::ZeroVector;
	bool bHasBeamPoints = false;

	/** AGameplayCueNotify_Actorはデフォルトでルートコンポーネントを持たないため、明示的に生成する。 */
	UPROPERTY()
	TObjectPtr<USceneComponent> VisualRoot;

	/** 太い色付きハロー(武器色)。 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BeamBody;

	/** 細い白コア。 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BeamCore;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> GlowLight;

	// ConstructorHelpers::FObjectFinderはコンストラクタ内でのみ使用可能なため、
	// コンストラクタでキャッシュしEnsureVisualComponentsCreated(Tick経由の遅延生成)で使い回す。
	UPROPERTY()
	TObjectPtr<UStaticMesh> CachedCubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CachedCubeMaterial;

	bool bVisualComponentsCreated = false;
};
