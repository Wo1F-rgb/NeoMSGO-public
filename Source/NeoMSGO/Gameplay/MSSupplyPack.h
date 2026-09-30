#pragma once

#include "CoreMinimal.h"
#include "Core/MSScale.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Actor.h"
#include "MSSupplyPack.generated.h"

class AMSPlayerCharacter;
class AMSUnitBase;
class UMaterialInstanceDynamic;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FMSOnSupplyPickedUp, AMSUnitBase* /*Consumer*/);

UENUM(BlueprintType)
enum class EMSSupplyPackType : uint8
{
	Repair,
	Ammo
};

/** An overlap-discovered, explicitly consumed supply pickup. */
UCLASS(Blueprintable)
class NEOMSGO_API AMSSupplyPack : public AActor
{
	GENERATED_BODY()

public:
	AMSSupplyPack();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category="MS|SupplyPack")
	bool TryConsume(AMSUnitBase* Consumer);
	/** Native notification emitted only after a successful pickup. */
	FMSOnSupplyPickedUp OnSupplyPickedUp;

	UFUNCTION(BlueprintCallable, Category="MS|SupplyPack", meta=(WorldContext="WorldContextObject"))
	static AMSSupplyPack* SpawnSupplyPack(const UObject* WorldContextObject, FVector Position,
		EMSSupplyPackType InType, FGenericTeamId InFaction);
	static AMSSupplyPack* Spawn(UWorld* World, const FVector& Position, EMSSupplyPackType InType,
		FGenericTeamId InFaction = FGenericTeamId::NoTeam);

	UFUNCTION(BlueprintPure, Category="MS|SupplyPack")
	USphereComponent* GetPickupSphere() const { return PickupSphere; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|SupplyPack", meta=(ClampMin="0.0", Units="cm"))
	float PickupRadius = 350.0f * MSGO::MSScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|SupplyPack", meta=(ClampMin="0.0"))
	float HealAmount = 1500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|SupplyPack")
	EMSSupplyPackType SupplyPackType = EMSSupplyPackType::Repair;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MS|SupplyPack")
	FGenericTeamId Faction = FGenericTeamId::NoTeam;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnPickupBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION() void OnPickupEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);
	void ConfigureVisuals();
	void ApplyVisualColor(UStaticMeshComponent* Component, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category="MS|SupplyPack") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category="MS|SupplyPack") TObjectPtr<USphereComponent> PickupSphere;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual") TObjectPtr<USceneComponent> FloatingVisualRoot;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual") TObjectPtr<UStaticMeshComponent> BoxMesh;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual") TObjectPtr<UStaticMeshComponent> AccentA;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual") TObjectPtr<UStaticMeshComponent> AccentB;
	UPROPERTY(VisibleAnywhere, Category="MS|Visual") TArray<TObjectPtr<UStaticMeshComponent>> AmmoRounds;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> VisualMaterials;
	float AnimationTime = 0.0f;
};
