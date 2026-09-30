#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "UObject/Object.h"
#include "MSHUDTypes.h"
#include "Voice/MSVoiceTypes.h"
#include "MSHUDViewModel.generated.h"

class AMSPlayerCharacter;
class UMSAttributeSet_Vital;
class UMSAttributeSet_Boost;
class UMSHitFeedbackSubsystem;
class UMSVoiceCutInSubsystem;

USTRUCT() struct FMSHUDGaugeSnapshot { GENERATED_BODY() float Boost=0, Armor=0, DP=0; bool bStaggered=false,bKnockedDown=false,bFlinched=false,bGettingUp=false; FLinearColor BoostColor=MSHUDPalette::BoostFill, DPColor=MSHUDPalette::DPNormal; };
USTRUCT() struct FMSHUDWeaponSlotSnapshot { GENERATED_BODY() FText Name; FText FireMode; FText Status; FText Ammo; FMSHUDIconStyle Icon; bool bHasWeapon=false; bool bSelected=false; bool bReloading=false; float ReloadRemaining=0.0f; float ReloadDuration=0.0f; float ReloadProgress=0.0f; };
USTRUCT() struct FMSHUDCombatSnapshot { GENERATED_BODY() bool bSwitching=false,bHoming=false,bLocked=false,bHomingTargetInCircle=false,bHomingCaptured=false,bCharging=false,bReloading=false,bAmmoArcVisible=false,bScoped=false; float ReloadRemaining=0.0f,ReloadDuration=0.0f,ReloadProgress=0,DwellProgress=0,ChargeProgress=0,AmmoArcProgress=0; };
USTRUCT() struct FMSHUDCaptureSnapshot { GENERATED_BODY() bool bVisible=false,bContested=false,bHostileProgress=false,bCompleted=false; float Progress=0; FText Name; FText State; };
USTRUCT() struct FMSHUDSupplySnapshot { GENERATED_BODY() bool bVisible=false; FText Prompt; };
USTRUCT() struct FMSHUDLockOnMarkerSnapshot { GENERATED_BODY() bool bVisible=false; bool bRedLocked=false; FVector2D ScreenPosition=FVector2D::ZeroVector; float DistanceMeters=0; };
USTRUCT() struct FMSHUDHomingTargetSnapshot { GENERATED_BODY() bool bVisible=false,bCaptured=false; FVector2D ScreenPosition=FVector2D::ZeroVector; float Progress=0; };
USTRUCT() struct FMSHUDBattleSnapshot { GENERATED_BODY() bool bValid=false; FText TimeText; float TeamARatio=0, TeamBRatio=0; FGenericTeamId TeamAId=FGenericTeamId::NoTeam, TeamBId=FGenericTeamId::NoTeam; int32 Tactical=0; };
USTRUCT() struct FMSHUDArmorSnapshot { GENERATED_BODY() int32 Armor=0, MaxArmor=0; };
/** Compact native-shield state for the lower-right combat instrument. */
USTRUCT() struct FMSHUDShieldSnapshot
{
	GENERATED_BODY()
	bool bSupported=false;
	bool bEquipped=false;
	bool bBroken=false;
	bool bGuarding=false;
	float Durability=0.0f;
	float MaxDurability=0.0f;
	float Ratio=0.0f;
};
USTRUCT() struct FMSHUDTaskSnapshot { GENERATED_BODY() bool bDefense=true; FText MissionType; FText MissionTypeEnglish; FText TaskText; };
USTRUCT() struct FMSHUDScoreSnapshot { GENERATED_BODY() int32 Score=0; };
USTRUCT() struct FMSHUDUnitBarSnapshot { GENERATED_BODY() FVector2D ScreenPosition=FVector2D::ZeroVector; float ArmorRatio=0; FLinearColor FillColor=MSHUDPalette::EnemyUnitBar; };
USTRUCT() struct FMSHUDHitMarkerSnapshot { GENERATED_BODY() bool bVisible=false,bHeadshot=false; float Elapsed=0; };
USTRUCT() struct FMSHUDKillPopupSnapshot { GENERATED_BODY() bool bVisible=false; FText Text; int32 KillStreak=0; float Elapsed=0; };
USTRUCT() struct FMSHUDDamageDirectionSnapshot { GENERATED_BODY() bool bVisible=false; float RelativeAngleDegrees=0,Elapsed=0; };
/** Receiver-filtered incoming threats. Angles are relative to the active camera (0 = forward, +90 = right). */
USTRUCT() struct FMSHUDIncomingLockIndicatorSnapshot
{
	GENERATED_BODY()
	float RelativeAngleDegrees=0.0f;
	bool bHasDistance=false;
	float DistanceMeters=-1.0f;
};
USTRUCT() struct FMSHUDIncomingLockSnapshot
{
	GENERATED_BODY()
	bool bIncomingLock=false;
	bool bVisible=false;
	bool bMissilePrelockWarning=false;
	TArray<FMSHUDIncomingLockIndicatorSnapshot> DirectionIndicators;
	TArray<FMSHUDIncomingLockIndicatorSnapshot> DistanceIndicators;
};
USTRUCT() struct FMSHUDModuleSnapshot { GENERATED_BODY() bool bVisible=false; FText ModuleName; FText StateText; float RemainingSeconds=0.0f; };
USTRUCT() struct FMSHUDModuleChargeSnapshot { GENERATED_BODY() bool bVisible=false; float Progress=0.0f; };
USTRUCT() struct FMSHUDEmergencyRecallSnapshot
{
	GENERATED_BODY()
	bool bVisible=false, bActive=false, bPending=false, bAwaitingDeployment=false;
	float RemainingSeconds=0.0f, DurationSeconds=0.0f, Progress=0.0f, CooldownSeconds=0.0f;
	FText StatusText;
	FText DetailText;
};
USTRUCT() struct FMSHUDRespawnSnapshot { GENERATED_BODY() bool bVisible=false; float RemainingSeconds=0.0f; };
USTRUCT() struct FMSHUDCutInSnapshot { GENERATED_BODY() bool bVisible=false; FText SpeakerName; FText Line; FLinearColor PortraitColor=FLinearColor::White; FText VoiceTypeLetter; float Elapsed=0.0f; };
USTRUCT() struct FMSHUDCutInsSnapshot { GENERATED_BODY() FMSHUDCutInSnapshot Left; FMSHUDCutInSnapshot Right; };

struct NEOMSGO_API FMSHUDMinimapProjection
{
	FVector2D Position = FVector2D::ZeroVector;
	bool bInBounds = false;
};

struct NEOMSGO_API FMSHUDMinimapUnitMarker
{
	bool bVisible = false;
	bool bFriendly = false;
	FLinearColor Color = FLinearColor::Transparent;
};

UCLASS()
class NEOMSGO_API UMSHUDViewModel : public UObject
{
	GENERATED_BODY()
public:
	void Initialize(APlayerController* InOwner);
	FMSHUDGaugeSnapshot GetGauges();
	TArray<FMSHUDWeaponSlotSnapshot> GetWeaponSlots();
	FMSHUDCombatSnapshot GetCombat();
	FMSHUDCaptureSnapshot GetCapture();
	FMSHUDSupplySnapshot GetSupply();
	FMSHUDLockOnMarkerSnapshot GetLockOnMarker();
	FMSHUDHomingTargetSnapshot GetHomingTargetMarker();
	TArray<FMSHUDUnitBarSnapshot> GetUnitBars();
	FMSHUDBattleSnapshot GetBattle();
	FMSHUDArmorSnapshot GetArmorNumbers();
	FMSHUDShieldSnapshot GetShield();
	FMSHUDTaskSnapshot GetTask();
	FMSHUDScoreSnapshot GetScore() const;
	FMSHUDHitMarkerSnapshot GetHitMarker() const;
	FMSHUDKillPopupSnapshot GetKillPopup() const;
	FMSHUDDamageDirectionSnapshot GetDamageDirection() const;
	FMSHUDIncomingLockSnapshot GetIncomingLock();
	FMSHUDModuleSnapshot GetModule();
	FMSHUDModuleChargeSnapshot GetModuleCharge();
	FMSHUDEmergencyRecallSnapshot GetEmergencyRecall();
	FMSHUDRespawnSnapshot GetRespawn();
	FMSHUDCutInsSnapshot GetCutIns();
	static constexpr float CutInFadeInSeconds=0.2f;
	static constexpr float CutInHoldSeconds=3.0f;
	static constexpr float CutInFadeOutSeconds=0.3f;
	static float SafeRatio(float Value,float Maximum);
	static void EvaluateDP(float Ratio,bool bStaggered,bool bKnockedDown,float& OutPercent,FLinearColor& OutColor);
	static void EvaluateDownGauge(float AccumulatedDP,float Capacity,bool bHeavyReaction,float& OutPercent,FLinearColor& OutColor);
	static FLinearColor EvaluateBoostColor(float Ratio,bool bOverheated,bool& bInOutRecovering);
	static FText ComposeSlotStatus(bool bReloading,bool bEmpty,const FText& Ammo,bool bSelected,bool bLocked,const FText& TargetName);
	static float CalculateAmmoArcProgress(int32 CurrentAmmo,int32 MaximumAmmo,bool bReloading,float ReloadProgress);
	static FLinearColor SelectUnitBarColor(bool bAlly);
	static FMSHUDLockOnMarkerSnapshot MakeLockOnMarkerSnapshot(const AActor* LockTarget,bool bProjected,const FVector2D& ScreenPosition,float DistanceMeters=0.0f,bool bRedLocked=false);
	/** UE world units are absolute centimetres even after the MS body-scale migration. */
	static float WorldDistanceToMeters(float DistanceUnrealUnits) { return DistanceUnrealUnits / 100.0f; }
	static FLinearColor SelectHomingCursorColor(bool bTargetInCircle);
	/** 未制圧拠点(非本拠点かつ自軍所有でない)が残っていれば「攻撃」、全て自軍なら「防衛」任務。 */
	static FMSHUDTaskSnapshot MakeTaskSnapshot(int32 UncapturedPointCount);
	/** North-up projection: world +X maps up, world +Y maps right. Out-of-range positions are edge-clamped. */
	static FMSHUDMinimapProjection ProjectWorldToMinimap(const FVector& WorldPosition, const FVector& CenterPosition,
		float OrthoWidth, const FVector2D& MapSize);
	static bool IsInsideLocalMinimapCircle(const FMSHUDMinimapProjection& Projection,
		const FVector2D& MapSize, float EdgePaddingPx = 5.0f);
	/** Pure presentation policy; callers obtain both team ids through UMSTeamStatics. */
	static FMSHUDMinimapUnitMarker MakeMinimapUnitMarker(FGenericTeamId ViewerTeam, FGenericTeamId UnitTeam);
	static float CalculateRelativeDamageAngle(const FVector& UnitLocation, const FRotator& UnitRotation, const FVector& AttackerLocation);
	/** Converts the receiver snapshot's world bearing to the camera-relative HUD bearing. */
	static float CalculateIncomingLockRelativeAngle(float DirectionYawDegrees, float ViewYawDegrees);
	static int32 AdvanceKillStreak(int32 CurrentStreak, double PreviousKillTime, double CurrentKillTime);
	/** Deterministic reducers used by delegate handlers and automation tests. */
	void ProcessHitConfirmed(bool bIsHeadshot, double NowSeconds);
	void ProcessKillConfirmed(double NowSeconds);
	void ProcessPlayerDeath();
	void ProcessDamageDirection(const FVector& AttackerLocation, double NowSeconds);
#if WITH_DEV_AUTOMATION_TESTS
	void SetVoiceCutInSubsystemForTesting(UMSVoiceCutInSubsystem* InSubsystem);
#endif
	virtual void BeginDestroy() override;
private:
	AMSPlayerCharacter* ResolvePawn();
	double GetCurrentTime() const;
	bool IsLocalCombatActor(const AActor* Actor) const;
	void RefreshVitalSubscription(AMSPlayerCharacter* CurrentPawn);
	void HandleHitConfirmed(AActor* Shooter, bool bIsHeadshot);
	void HandleKillConfirmed(AActor* Killer);
	UFUNCTION() void HandleDamagedFrom(AActor* DamagedUnit, AActor* Attacker, FVector AttackerLocation);
	UFUNCTION() void HandlePlayerDeath(AActor* DeadUnit, AActor* Killer);
	UFUNCTION() void HandlePlayerOverheat(AActor* Unit);
	void BindVoiceCutInSubsystem(UMSVoiceCutInSubsystem* InSubsystem);
	void HandleVoiceCutInRequested(const struct FMSVoiceCutInRequest& Request);
	void HandleVoiceCutInCleared(EMSVoiceCutInChannel Channel);
	TWeakObjectPtr<APlayerController> Owner;
	TWeakObjectPtr<AMSPlayerCharacter> Pawn;
	TWeakObjectPtr<UMSAttributeSet_Vital> SubscribedVital;
	TWeakObjectPtr<UMSAttributeSet_Boost> SubscribedBoost;
	TWeakObjectPtr<UMSHitFeedbackSubsystem> HitFeedbackSubsystem;
	TWeakObjectPtr<UMSVoiceCutInSubsystem> VoiceCutInSubsystem;
	TOptional<struct FMSVoiceCutInRequest> LastCutInRequests[2];
	bool bHitMarkerActive=false,bHitMarkerHeadshot=false,bKillPopupActive=false,bDamageDirectionActive=false;
	double HitMarkerStartTime=0,KillPopupStartTime=0,LastKillTime=-1.0e9,DamageDirectionStartTime=0;
	int32 KillStreak=0;
	int32 TotalScore=0;
	float DamageDirectionAngle=0;
	bool bBoostRecoveringFromOverheat=false;
	TWeakObjectPtr<AMSPlayerCharacter> BoostRecoveryPawn;
	TWeakObjectPtr<class AMSCapturePoint> LastCapturePoint;
	TWeakObjectPtr<AMSPlayerCharacter> CapturePawn;
	uint8 CaptureViewerTeam=255;
	float LastCaptureProgress=1.f;
	double CaptureCompleteUntil=0;
};
