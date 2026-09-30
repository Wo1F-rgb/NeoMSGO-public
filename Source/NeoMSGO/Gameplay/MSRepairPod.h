#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "AbilitySystemInterface.h"
#include "Core/MSTeamAgentActor.h"
#include "MSRepairPod.generated.h"

class AMSUnitBase;
class UAbilitySystemComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMSAttributeSet_Vital;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnPlayerRepaired, AMSUnitBase* /*PlayerUnit*/);

/** Team-filtered repair area. Enemy attacks can permanently disable its healing service. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSRepairPod : public AMSTeamAgentActor, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AMSRepairPod();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGenericTeamId GetGenericTeamId() const override { return Faction; }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual bool CanBeLockedOn() const override;
	virtual FVector GetLockOnAimPoint() const override;
	UFUNCTION(BlueprintPure, Category="MS|RepairPod")
	UMSAttributeSet_Vital* GetVitalSet() const { return VitalSet; }
	UFUNCTION(BlueprintPure, Category="MS|RepairPod")
	bool IsDestroyed() const { return bDestroyed; }
	UFUNCTION(BlueprintPure, Category="MS|RepairPod")
	UBoxComponent* GetDamageCollision() const { return DamageCollision; }
#if WITH_DEV_AUTOMATION_TESTS
	/** Exercises the same OnRep restoration path with a simulated received state. */
	void ApplyReplicatedDestroyedStateForTesting(bool bReceivedDestroyed)
	{
		bDestroyed = bReceivedDestroyed;
		OnRep_Destroyed();
	}
#endif

	UFUNCTION(BlueprintCallable, Category="MS|RepairPod", meta=(WorldContext="WorldContextObject"))
	static AMSRepairPod* SpawnRepairPod(const UObject* WorldContextObject, FVector Position,
		float InLifetime, FGenericTeamId InFaction);
	static AMSRepairPod* Spawn(UWorld* World, const FVector& Position, float InLifetime = 0.0f,
		FGenericTeamId InFaction = FGenericTeamId::NoTeam);

	UFUNCTION(BlueprintCallable, Category="MS|RepairPod")
	void RepairUnitsInRange();
	/** Audio is a later task; this hook fires only for repaired player characters. */
	UFUNCTION(BlueprintImplementableEvent, Category="MS|RepairPod")
	void OnPlayerRepairTick(AMSUnitBase* PlayerUnit);
	/** Native notification emitted after an actual repair/ammo tick targets a player unit. */
	FMSOnPlayerRepaired OnPlayerRepaired;

	UPROPERTY(ReplicatedUsing=OnRep_HealRadius, EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod",
		meta=(ClampMin="0.0", Units="cm"))
	float HealRadius = 600.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod", meta=(ClampMin="0.0"))
	float HealPerTick = 150.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AmmoFractionPerTick = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod", meta=(ClampMin="0.01", Units="s"))
	float TickInterval = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod", meta=(ClampMin="0.0", Units="s"))
	float Lifetime = 0.0f;
	UPROPERTY(ReplicatedUsing=OnRep_Faction, EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod")
	FGenericTeamId Faction = FGenericTeamId::NoTeam;
	/** Provisional durability until fixed-facility source data is confirmed; no auto-respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|RepairPod", meta=(ClampMin="1.0"))
	float MaxArmor = 2000.0f;

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* CreateTowerPart(const FName& Name, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Size, bool bAccent, USceneComponent* Parent = nullptr);
	void UpdateRangeRingGeometry();
	void InitializeVisualMaterials();
	void ApplyDestroyedState();
	FLinearColor GetTeamVisualColor() const;
	UFUNCTION()
	void HandlePodDeath(AActor* DeadActor, AActor* Killer);

	UFUNCTION()
	void OnRep_Faction();
	UFUNCTION()
	void OnRep_HealRadius();
	UFUNCTION()
	void OnRep_Destroyed();

	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category="MS|Collision")
	TObjectPtr<UBoxComponent> DamageCollision;
	UPROPERTY(VisibleAnywhere, Category="MS|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
	UPROPERTY(VisibleAnywhere, Category="MS|GAS")
	TObjectPtr<UMSAttributeSet_Vital> VitalSet;
	UPROPERTY(ReplicatedUsing=OnRep_Destroyed, VisibleAnywhere, Category="MS|RepairPod")
	bool bDestroyed = false;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<USceneComponent> BeaconRoot;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> StructureParts;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> AccentParts;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> RangeRingSegments;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<UPointLightComponent> BeaconLight;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual")
	TObjectPtr<UPointLightComponent> RangeLight;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VisualMaterials;

	float RepairTimeAccumulator = 0.0f;
};
