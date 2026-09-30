#pragma once

#include "CoreMinimal.h"
#include "Combat/MSUnitBase.h"
#include "AI/MSBotAITypes.h"
#include "MSBotCharacter.generated.h"

class UMSRespawnComponent;
class UMSWeaponDataBase;
class UMSLockOnComponent;

UCLASS()
class NEOMSGO_API AMSBotCharacter : public AMSUnitBase
{
	GENERATED_BODY()
public:
	AMSBotCharacter(const FObjectInitializer& ObjectInitializer);
	const FVector& GetPatrolCenter() const { return PatrolCenter; }
	const FMSBotAIConfig& GetAIConfig() const { return AIConfig; }
	EMSEnemyTier GetEnemyTier() const { return EnemyTier; }
	void SetEnemyTier(EMSEnemyTier NewTier);
	/** Replaces suit-default slots with the Unity EnemyTier loadout. */
	bool ApplyTierWeaponLoadout();
	static TArray<FSoftObjectPath> GetTierWeaponAssetPaths(EMSEnemyTier Tier);
	const FMSBotWeakeningParams& GetWeakeningParams() const { return WeakeningParams; }
	UMSRespawnComponent* GetRespawnComponent() const { return RespawnComponent; }
	UMSLockOnComponent* GetLockOnComponent() const { return LockOnComponent; }
	virtual void InitializeUnit(UMSMobileSuitData* Data) override;

	/** 外見差分連携用(P6-2 AC6)。0=Weak/1=Medium/2=Strong。実際のメッシュ/マテリアル切替はP6-3以降 */
	UFUNCTION(BlueprintPure, Category="MS|AI")
	int32 GetEnemyTierVariant() const { return EnemyTierVariant; }

	/** True when this BOT uses the player-team (ally) color and ally head shape. TeamId remains the sole faction state. */
	UFUNCTION(BlueprintPure, Category="MS|AI|Visual")
	bool IsAllyVisualVariant() const;

	/** Blueprint visual key: enemy Weak/Medium/Strong = 0/1/2, ally Weak/Medium/Strong = 3/4/5. */
	UFUNCTION(BlueprintPure, Category="MS|AI|Visual")
	int32 GetVisualVariantId() const;

	static int32 CalculateEnemyTierVariant(EMSEnemyTier Tier);
	static int32 CalculateVisualVariantId(EMSEnemyTier Tier, bool bAllyVisualVariant);

	//~ Begin AMSUnitBase Interface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FMSCombatModifiers GetCombatModifiers() const override;
	//~ End AMSUnitBase Interface

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleExplosionDue(AMSUnitBase* Unit, FVector DeathLocation);

	UFUNCTION()
	void HandleDeployed(AMSUnitBase* Unit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|Respawn")
	TObjectPtr<UMSRespawnComponent> RespawnComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MS|LockOn")
	TObjectPtr<UMSLockOnComponent> LockOnComponent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|AI") FMSBotAIConfig AIConfig;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|AI") EMSEnemyTier EnemyTier = EMSEnemyTier::Weak;
	/** 全階級共通の弱体化パラメータ(P6-2 AC3)。damageMultiplier=0.3/aimSpreadDegrees=4°/fireIntervalMultiplier=5倍 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|AI") FMSBotWeakeningParams WeakeningParams;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="MS|AI") FVector PatrolCenter = FVector::ZeroVector;
	/** EnemyTierから算出したキャッシュ値(P6-2 AC6)。BeginPlayおよびEnemyTier変更時に更新 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="MS|AI") int32 EnemyTierVariant = 0;
};
