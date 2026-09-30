// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GenericTeamAgentInterface.h"
#include "Gameplay/MSCapturePoint.h"
#include "MSHomeBase.generated.h"

class AMSDestructibleTarget;
class AMSBotSpawnPoint;
class AMSHomeBase;
class UAbilitySystemComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMSHomeBaseType : uint8
{
	Fortress,
	Battleship,
	CommandPost,
	Reactor
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHomeBaseDestroyed, AMSHomeBase*, HomeBase,
	EMSCaptureOwner, HomeBaseFaction);

/** Fired every time the core loses Armor, before the base is fully destroyed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHomeBaseDamaged, AMSHomeBase*, HomeBase,
	EMSCaptureOwner, HomeBaseFaction);

/** Fixed-owner capture point containing a destructible GAS core. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSHomeBase : public AMSCapturePoint, public IAbilitySystemInterface,
	public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AMSHomeBase();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsCaptureLocked() const override { return true; }
	virtual bool CanDeployFor(FGenericTeamId TeamId) const override;
	virtual void UpdateVisuals_Implementation() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	bool IsHomeBaseDestroyed() const { return bDestroyed; }

	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	AMSDestructibleTarget* GetCoreTarget() const { return CoreTarget; }

	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	FVector GetSunkenTargetLocation() const { return SunkenTargetLocation; }

	/** Optional authored deployment marker; null preserves existing maps. */
	AMSBotSpawnPoint* GetDeploymentSpawnAnchor() const { return DeploymentSpawnAnchor; }
	void SetDeploymentSpawnAnchor(AMSBotSpawnPoint* InAnchor) { DeploymentSpawnAnchor = InAnchor; }

	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	int32 GetDestructionBroadcastCount() const { return DestructionBroadcastCount; }

	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	EMSHomeBaseType GetHomeBaseType() const { return HomeBaseType; }

	UFUNCTION(BlueprintCallable, Category="MS|HomeBase")
	void SetHomeBaseType(EMSHomeBaseType NewType);

	/** 0 while the core is untouched, 1 once it is fully destroyed. */
	UFUNCTION(BlueprintPure, Category="MS|HomeBase")
	float GetCoreArmorLostRatio() const;

	UPROPERTY(BlueprintAssignable, Category="MS|HomeBase")
	FOnHomeBaseDestroyed OnHomeBaseDestroyed;

	UPROPERTY(BlueprintAssignable, Category="MS|HomeBase")
	FOnHomeBaseDamaged OnHomeBaseDamaged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(ReplicatedUsing=OnRep_HomeBaseFaction, EditAnywhere, BlueprintReadOnly, Category="MS|HomeBase")
	EMSCaptureOwner HomeBaseFaction = EMSCaptureOwner::TeamPlayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|HomeBase")
	EMSHomeBaseType HomeBaseType = EMSHomeBaseType::Fortress;

	/** Use a separately authored level exterior and disable the built-in fortress pieces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|HomeBase|Authored Exterior")
	bool bUseAuthoredExterior = false;

	/** Local core position for an authored exterior; zero preserves all existing maps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|HomeBase|Authored Exterior", meta=(Units="cm"))
	FVector CoreLocalOffset = FVector::ZeroVector;

	/** C/Z may use a rear underground marker while this actor remains the selectable base. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MS|HomeBase|Deployment")
	TObjectPtr<AMSBotSpawnPoint> DeploymentSpawnAnchor;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="MS|HomeBase")
	bool bDestroyed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MS|HomeBase", meta=(ClampMin="1.0", Units="cm/s"))
	float SinkSpeed = 75.0f * MSGO::MSScale;

private:
	UFUNCTION()
	void OnRep_HomeBaseFaction();

	UFUNCTION()
	void HandleCoreDeath(AActor* DeadUnit, AActor* Killer);

	UFUNCTION()
	void HandleCoreDamaged(AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation);

	UStaticMeshComponent* CreateBuildingPart(const FName Name, const FVector& Location, const FVector& Size);
	void ApplyHomeBaseTypeVisuals();
	float GetDefaultMaxArmor() const;
	void ApplyBuildingColor(UStaticMeshComponent* Component, UMaterialInstanceDynamic*& Material,
		const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category="MS|HomeBase")
	TArray<TObjectPtr<UStaticMeshComponent>> BuildingParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BuildingMaterials;

	UPROPERTY(VisibleAnywhere, Category="MS|HomeBase")
	TObjectPtr<AMSDestructibleTarget> CoreTarget;

	FVector SunkenTargetLocation = FVector::ZeroVector;
	int32 DestructionBroadcastCount = 0;
};
