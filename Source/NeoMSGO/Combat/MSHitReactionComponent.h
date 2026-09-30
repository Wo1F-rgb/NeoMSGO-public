#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MSHitReactionComponent.generated.h"

class UAbilitySystemComponent;
class UMSMechRigComponent;
class USceneComponent;
class UCapsuleComponent;

UENUM(BlueprintType)
enum class EMSHitReactionPhase : uint8 { None, Flinch, Stagger, FallingDown, Downed, GettingUp };

USTRUCT()
struct FMSHitReactionState
{
	GENERATED_BODY()
	UPROPERTY() EMSHitReactionPhase Phase = EMSHitReactionPhase::None;
	UPROPERTY() double StartServerTime = 0;
	UPROPERTY() uint32 Sequence = 0;
	UPROPERTY() bool bProne = false;
};

/** Authority owns reaction transitions; clients evaluate the same presentation timeline. */
UCLASS(ClassGroup=(MS), meta=(BlueprintSpawnableComponent))
class NEOMSGO_API UMSHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMSHitReactionComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	void StartFlinch();
	void StartHeavyReaction(bool bForceKnockdown = false);
	void ResetReaction();
	UFUNCTION(BlueprintPure, Category="MS|HitReaction")
	EMSHitReactionPhase GetPhase() const { return State.Phase; }
	UFUNCTION(BlueprintPure, Category="MS|HitReaction")
	bool IsProneReaction() const { return State.bProne; }
	UFUNCTION(BlueprintPure, Category="MS|HitReaction")
	float GetPhaseElapsed() const;
	static bool BlocksCombat(const UAbilitySystemComponent* ASC);
	static bool ProtectsDownGauge(const UAbilitySystemComponent* ASC);
	// Tunable production timings, NOT measured original-game timings.
	static constexpr float FlinchSeconds=.3f;
	static constexpr float StaggerSeconds=1.2f;
	static constexpr float FallBlendSeconds=.35f;
	static constexpr float DownedSeconds=1.7f;
	static constexpr float GetUpSeconds=.8f;
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FMSHitReactionState State;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> ReactionRoot;
	UPROPERTY(Transient) TObjectPtr<UCapsuleComponent> ReactionBodyHitbox;
	UFUNCTION() void OnRep_State();
	void SetPhase(EMSHitReactionPhase Phase);
	void ApplyTags();
	void ApplyPresentation();
	double ServerTime() const;
};
