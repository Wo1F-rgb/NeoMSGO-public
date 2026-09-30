#pragma once

#include "Core/MSTeamAgentActor.h"
#include "AbilitySystemInterface.h"
#include "MSRadarPod.generated.h"

class UAbilitySystemComponent;
class UMSAttributeSet_Vital;
class UBoxComponent;
class UStaticMeshComponent;

/** Team radar placed by a supply weapon. The sensing subsystem reads its authority state. */
UCLASS()
class NEOMSGO_API AMSRadarPod : public AMSTeamAgentActor, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AMSRadarPod();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMSAttributeSet_Vital* GetVitalSet() const { return VitalSet; }
	bool IsActive() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	static AMSRadarPod* Spawn(UWorld* World, const FVector& Location, AActor* Deployer,
		FGenericTeamId Team, float RangeStat);
	static constexpr int32 MaxPerDeployer = 3;
	static constexpr float LifetimeSeconds = 150.0f;
	static constexpr float DefaultRangeStat = 4800.0f;
	float GetRadarRadiusCm() const { return RangeStat * 10.0f; }
	AActor* GetDeployer() const { return Deployer.Get(); }
	bool ApplyWeaponDamage(const AActor* Attacker, float Damage);
	static bool IsInsideRadarRange(const FVector& RadarPosition, const FVector& TargetPosition,
		float RadiusCm);
protected:
	virtual void BeginPlay() override;
private:
	UFUNCTION() void HandlePodDeath(AActor* DeadActor, AActor* Killer);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UMSAttributeSet_Vital> VitalSet;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> HitBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(Replicated) uint8 ReplicatedTeamId = 255;
	UPROPERTY(Replicated) float RangeStat = DefaultRangeStat;
	UPROPERTY(Transient) TWeakObjectPtr<AActor> Deployer;
	// PlayerState survives pawn replacement; AI falls back to its controller.
	UPROPERTY(Transient) TWeakObjectPtr<AActor> DeploymentIdentity;
	static constexpr float MaxArmor = 300.0f;
};
