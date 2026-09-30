// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Components/CapsuleComponent.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"
#include "EngineUtils.h"
#include "Gameplay/MSGameState.h"
#include "Materials/MaterialInterface.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSUnitBase.h"
#include "HAL/IConsoleManager.h"
#include "NeoMSGO.h"

class FMSNetworkPredictionData_Client final : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	explicit FMSNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FMSCharacterSavedMove());
	}
};

namespace
{
	// Unity: gravity=-20 m/s^2 (1.8m機体基準)。18m世界では移動速度・距離がMSScale倍のため、
	// 重力もMSScale倍しないと降下だけが機体比~2m/s^2の月面のような鈍さになる。
	// (2026-07-19: 標準版≈2.04へ一度戻したが「重力が小さくなった」と再指摘があり、
	// 「機体比-20m/s^2」の落下感を優先してMSScale版へ再度戻した。ブースト自体が発動しない
	// 別問題は#185で追跡中)
	constexpr float MSGravityScale = 2000.0f * MSGO::MSScale / 980.0f; // ≈ 19.0

	// Unity: moveAcceleration=40 m/s^2 基準。速度はMSScale倍済みなので加速度にも同係数が必要。
	// さらに体感優先で機体比60 m/s^2へ引き上げ、左右切り返し(全ブースト速度からの逆入力停止)を
	// 0.35s→約0.23sに短縮してキビキビ感を出す(Unity値との厳密一致は不要)。
	constexpr float MSMoveAcceleration = 6000.0f * MSGO::MSScale;

	// ブースト未使用時の空中水平操作(WASD)が強すぎるという指摘により、非ブースト空中制御を半減。
	// ブースト中(IsBoostDashing)の空中制御には影響を与えない。
	constexpr float MSNonBoostAirControlMultiplier = 0.5f;

	constexpr float MSUnderwaterSpeedMultiplier = 0.8f;
	constexpr float MSWaterRippleMinInterval = 0.3f;
	constexpr float MSWaterRippleMaxInterval = 0.5f;
	TAutoConsoleVariable<int32> CVarMSBoostPredictionTrace(TEXT("ms.BoostPrediction.Trace"), 0,
		TEXT("Log move-time boost/OH/takeoff state (0 off, 1 on)."));

	bool FindRenderedWaterSurfaceZ(
		const UWorld& World, const FVector& FootLocation, const float ContactToleranceCm,
		float& OutSurfaceZ)
	{
		bool bFoundSurface = false;
		float ClosestVerticalDistance = TNumericLimits<float>::Max();

		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			if (!Mesh || !Mesh->IsVisible())
			{
				continue;
			}

			bool bUsesWaterMaterial = It->ActorHasTag(TEXT("MSWaterSurface"));
			for (const UMaterialInterface* Material : Mesh->GetMaterials())
			{
				// A folder called WaterRealism also contains land, rubble and VFX.
				// Only known surface materials (or an explicit surface tag) qualify.
				const FName MaterialName = Material ? Material->GetFName() : NAME_None;
				if (MaterialName == TEXT("M_River_P7") || MaterialName == TEXT("M_River_P7_Clear") ||
					MaterialName == TEXT("M_NYT_Water") || MaterialName == TEXT("M_NYT_ClearWater"))
				{
					bUsesWaterMaterial = true;
					break;
				}
			}
			if (!bUsesWaterMaterial)
			{
				continue;
			}

			const FBox SurfaceBounds = Mesh->Bounds.GetBox();
			if (!SurfaceBounds.IsValid ||
				FootLocation.X < SurfaceBounds.Min.X || FootLocation.X > SurfaceBounds.Max.X ||
				FootLocation.Y < SurfaceBounds.Min.Y || FootLocation.Y > SurfaceBounds.Max.Y)
			{
				continue;
			}

			const float VerticalDistance = FMath::Abs(SurfaceBounds.Max.Z - FootLocation.Z);
			if (VerticalDistance <= ContactToleranceCm && VerticalDistance < ClosestVerticalDistance)
			{
				ClosestVerticalDistance = VerticalDistance;
				OutSurfaceZ = SurfaceBounds.Max.Z;
				bFoundSurface = true;
			}
		}
		return bFoundSurface;
	}

}

UMSCharacterMovementComponent::UMSCharacterMovementComponent()
{
	SetNetworkMoveDataContainer(MSNetworkMoveData);
	SetMoveResponseDataContainer(MSResponseData);
	// 重力係数(Unity: gravity=-20f 相当)
	GravityScale = MSGravityScale;

	// 加速度・制動(Unity: moveAcceleration=40f。CharacterController.Move相当の即応感)
	MaxAcceleration = MSMoveAcceleration;
	BrakingDecelerationWalking = MSMoveAcceleration;
	// Preserve the last horizontal velocity after directional input is released in the air.
	// Acceleration still applies while a direction is held, so airborne steering/reversal remains
	// responsive; normal walking braking resumes automatically when MOVE_Walking is restored.
	BrakingDecelerationFalling = 0.0f;

	// Unity版は空中でも地上同様の水平操作が効く(CharacterController.Move直駆動)
	AirControl = 1.0f;

	// 速度の既定値(実戦ではSetMovementSpeedsで機体データから上書き)
	WalkSpeedCms *= MSGO::MSScale;
	BoostSpeedCms *= MSGO::MSScale;
	MaxWalkSpeed = WalkSpeedCms;

	// UE既定45cmを18m機体の身体比へ合わせる。
	MaxStepHeight = 45.0f * MSGO::MSScale;

	// 三人称MS: 機体の向きはコントローラYaw追従(カメラ構成はP3-5)。移動方向への自動旋回はしない
	bOrientRotationToMovement = false;

	// 通常ジャンプは使わない(ホバー式。Unity版にジャンプ跳躍は存在しない)
	JumpZVelocity = 0.0f;

	// 水域(bWaterVolume)でエンジンがMOVE_Swimmingへ自動遷移すると浮力で水面に浮いたまま
	// 川へ入れなくなる(#189)。本家仕様は「川底を歩く+水中減速」なのでSwimming自体を禁止する。
	// IsUnderwater()/水中0.8倍ペナルティ(#117)はボリューム境界で判定しており影響を受けない。
	NavAgentProps.bCanSwim = false;
}

FNetworkPredictionData_Client* UMSCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UMSCharacterMovementComponent* MutableThis = const_cast<UMSCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FMSNetworkPredictionData_Client(*this);
	}

	return ClientPredictionData;
}

UMSBoostComponent* UMSCharacterMovementComponent::ResolveBoostDriver()
{
	if (!CachedBoostDriver.IsValid() && CharacterOwner)
	{
		CachedBoostDriver = CharacterOwner->FindComponentByClass<UMSBoostComponent>();
	}
	if (CachedBoostDriver.IsValid()) CachedBoostDriver->RefreshMovementDriver();
	return CachedBoostDriver.Get();
}

void UMSCharacterMovementComponent::ResetBoostPrediction()
{
	BoostMovementState = FMSBoostMovementState();
	bHasTakeoffBase = false;
	TakeoffBaseZ = 0.0f;
	++ClientSimulationEpoch;
	bRequestBoostCorrection = false;
	// Do not reset response ordering here: packets from before a respawn may still arrive.
	PendingResponseTimeStamp = -1.0f;
	if (CharacterOwner && CharacterOwner->HasAuthority() && ServerPredictionData)
	{
		// A deferred adjustment from the previous deployment must not move the
		// respawned owner back. The next accepted move receives a fresh snapshot.
		FNetworkPredictionData_Server_Character* Server = static_cast<FNetworkPredictionData_Server_Character*>(ServerPredictionData);
		Server->PendingAdjustment = FClientAdjustment();
		Server->bForceClientUpdate = true;
	}
}

FMSBoostMovementState UMSCharacterMovementComponent::CaptureBoostMovementState()
{
	if (UMSBoostComponent* Driver = ResolveBoostDriver())
	{
		if (!BoostMovementState.bInitialized || (CharacterOwner && CharacterOwner->HasAuthority()))
		{
			Driver->ReadMovementState(BoostMovementState);
		}
	}
	BoostMovementState.TakeoffBaseZ = TakeoffBaseZ;
	BoostMovementState.bHasTakeoffBase = bHasTakeoffBase;
	return BoostMovementState;
}

void UMSCharacterMovementComponent::RestoreBoostMovementState(const FMSBoostMovementState& State)
{
	BoostMovementState = State;
	TakeoffBaseZ = State.TakeoffBaseZ;
	bHasTakeoffBase = State.bHasTakeoffBase;
}

void UMSCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	UMSBoostComponent* Driver = ResolveBoostDriver();
	if (!Driver || !Driver->UsesMovementClock() || !HasValidData() || DeltaSeconds <= 0.0f ||
		MovementMode == MOVE_None || UpdatedComponent->Mobility != EComponentMobility::Movable ||
		UpdatedComponent->IsSimulatingPhysics())
	{
		Super::PerformMovement(DeltaSeconds);
		return;
	}
	const FMSBoostMovementState Start = CaptureBoostMovementState();
	const bool bConsuming = IsConsumingBoost();
	const bool bGroundedAtStart = IsMovingOnGround();
	Super::PerformMovement(DeltaSeconds);
	const bool bGroundedAtEnd = IsMovingOnGround();
	if (CharacterOwner->HasAuthority()) Driver->AdvanceMovementClock(DeltaSeconds, bConsuming, bGroundedAtStart, bGroundedAtEnd);
	else BoostMovementState.Advance(DeltaSeconds, bConsuming, bGroundedAtStart, bGroundedAtEnd);
	const FMSBoostMovementState End = CaptureBoostMovementState();
	float TimeStamp = ActiveServerMoveTimeStamp;
	if (const FSavedMove_Character* ReplayedMove = GetCurrentReplayedSavedMove()) TimeStamp = ReplayedMove->TimeStamp;
	else if (bForcedBoostUpdate) TimeStamp = GetPredictionData_Server_Character()->CurrentClientTimeStamp;
	else if (TimeStamp < 0.0f && ClientPredictionData) TimeStamp = GetPredictionData_Client_Character()->CurrentTimeStamp;
	TraceBoostMove(bForcedBoostUpdate ? TEXT("forced") : GetCurrentReplayedSavedMove() ? TEXT("replay") :
		bCombinedBoostMove ? TEXT("combined") : TEXT("move"), TimeStamp, DeltaSeconds, Start, End, bConsuming ? 1 : 0);
	bCombinedBoostMove = false;
}

bool UMSCharacterMovementComponent::ForcePositionUpdate(float DeltaTime)
{
	TGuardValue<bool> ForcedGuard(bForcedBoostUpdate, true);
	return Super::ForcePositionUpdate(DeltaTime);
}

void UMSCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	TGuardValue<const FMSCharacterNetworkMoveData*> MoveGuard(ActiveBoostNetworkMove,
		static_cast<const FMSCharacterNetworkMoveData*>(&MoveData));
	TGuardValue<float> TimeGuard(ActiveServerMoveTimeStamp, MoveData.TimeStamp);
	// The engine alone validates timestamps and derives dt. Rejected/duplicate moves
	// never enter PerformMovement, so they cannot debit/recharge boost.
	Super::ServerMove_PerformMovement(MoveData);
}

void UMSCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime,
	const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	const FMSBoostMovementState State = CaptureBoostMovementState();
	const bool bNetworkBoost = CachedBoostDriver.IsValid() && CachedBoostDriver->UsesMovementClock();
	if (bNetworkBoost && ActiveBoostNetworkMove &&
		(ActiveBoostNetworkMove->bRequestBoostCorrection || !State.Matches(ActiveBoostNetworkMove->ReportedEndState)))
	{
		// Requests the standard authoritative correction, including at zero position
		// error. No position tolerance or client-authority setting is changed.
		GetPredictionData_Server_Character()->bForceClientUpdate = true;
	}
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
	if (bNetworkBoost && GetPredictionData_Server_Character()->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		PendingResponseBoostState = State;
		PendingResponseTimeStamp = ClientTimeStamp;
		PendingResponseClientEpoch = ActiveBoostNetworkMove ? ActiveBoostNetworkMove->ClientSimulationEpoch : 0;
		++ResponseSequence;
		TraceBoostMove(GetPredictionData_Server_Character()->PendingAdjustment.bAckGoodMove ? TEXT("ack-snapshot") :
			TEXT("correction-snapshot"), ClientTimeStamp, DeltaTime,
			ActiveBoostNetworkMove ? ActiveBoostNetworkMove->ReportedEndState : State, State);
	}
}

void UMSCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	const FMSCharacterMoveResponseDataContainer& Response = static_cast<const FMSCharacterMoveResponseDataContainer&>(MoveResponse);
	if (!Response.bHasBoostState)
	{
		Super::ClientHandleMoveResponse(MoveResponse);
		return;
	}
	// Match the engine's saved-move timestamp/reset handling, plus the initialization
	// epoch and server sequence. A timestamp alone is reused after the engine resets it.
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	const int32 Index = ClientData->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp);
	if (Index == INDEX_NONE || Response.ClientSimulationEpoch != ClientSimulationEpoch ||
		static_cast<int32>(Response.ResponseSequence - LastAcceptedResponseSequence) <= 0) return;
	const FMSCharacterSavedMove& Saved = static_cast<const FMSCharacterSavedMove&>(*ClientData->SavedMoves[Index]);
	if (Saved.bOldTimeStampBeforeReset || Saved.ClientSimulationEpoch != ClientSimulationEpoch ||
		(BoostMovementState.bInitialized && Response.AuthoritativeState.Generation < BoostMovementState.Generation)) return;
	if (Response.IsGoodMove())
	{
		if (!Saved.EndBoostState.Matches(Response.AuthoritativeState))
		{
			// ACK contains no authoritative transform. Retain this move and request a
			// normal correction on the next accepted move instead of inventing a pose.
			bRequestBoostCorrection = true;
			return;
		}
		Super::ClientHandleMoveResponse(MoveResponse);
		LastAcceptedResponseSequence = Response.ResponseSequence;
		return; // Never rewind the owner's unacknowledged prediction on a good ACK.
	}
	if (Response.bHasBase && !Response.ClientAdjustment.NewBase && Response.ClientAdjustment.bBaseRelativePosition) return;
	const FMSBoostMovementState Before = BoostMovementState;
	{
		TGuardValue<bool> CorrectionGuard(bApplyingBoostCorrection, true);
		Super::ClientHandleMoveResponse(MoveResponse);
	}
	if (ClientData->LastAckedMove.IsValid() && ClientData->LastAckedMove->TimeStamp == Response.ClientAdjustment.TimeStamp)
	{
		// Standard position/velocity/mode has been applied. Seed once even if no moves
		// remain to replay. Subsequent PrepMoveFor preserves this corrected anchor.
		RestoreBoostMovementState(Response.AuthoritativeState);
		LastAcceptedResponseSequence = Response.ResponseSequence;
		bRequestBoostCorrection = false;
		TraceBoostMove(TEXT("correction-applied"), Response.ClientAdjustment.TimeStamp, 0.0f, Before, BoostMovementState);
	}
}

void UMSCharacterMovementComponent::TraceBoostMove(const TCHAR* Phase, float TimeStamp, float DeltaSeconds,
	const FMSBoostMovementState& Start, const FMSBoostMovementState& End, int32 ConsumingAtStart) const
{
#if !UE_BUILD_SHIPPING
	if (CVarMSBoostPredictionTrace.GetValueOnGameThread() == 0) return;
	UE_LOG(LogNeoMSGO, Log, TEXT("[BoostPrediction] owner=%s role=%d phase=%s ts=%.6f dt=%.6f inputs=%d%d%d%d gen=%u epoch=%u sequence=%u start=%.6f/%d/%.3f/%d end=%.6f/%d/%.3f/%d rate=%.6f/%.6f consuming=%d z=%.3f vz=%.3f"),
		*GetNameSafe(CharacterOwner), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : -1,
		Phase, TimeStamp, DeltaSeconds, bBoostInputHeld, bHoverInputHeld, bZeroGravityAscendInputHeld,
		bZeroGravityDescendInputHeld, End.Generation, ClientSimulationEpoch, ResponseSequence,
		Start.Boost, Start.bOverheated, Start.TakeoffBaseZ, Start.bHasTakeoffBase,
		End.Boost, End.bOverheated, End.TakeoffBaseZ, End.bHasTakeoffBase,
		End.ConsumptionPerSecond, End.RechargePerSecond, ConsumingAtStart,
		UpdatedComponent ? UpdatedComponent->GetComponentLocation().Z : 0.0, Velocity.Z);
#endif
}

void UMSCharacterMovementComponent::UpdateFromCompressedFlags(const uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// The four custom bits are part of the same packed ServerMove as the
	// acceleration and movement mode.  Reconstructing them before
	// PerformMovement keeps hover gravity/boost speed/zero-G vertical input
	// identical on authority and on client replay.
	bBoostInputHeld = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bHoverInputHeld = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	// Keep the setter side effect in the server decode path as well.  Without
	// this, a zero-G ascend received while grounded would remain in Walking on
	// the authority even though the autonomous client had already entered Flying.
	SetZeroGravityAscendInputHeld((Flags & FSavedMove_Character::FLAG_Custom_2) != 0);
	SetZeroGravityDescendInputHeld((Flags & FSavedMove_Character::FLAG_Custom_3) != 0);
}

void UMSCharacterMovementComponent::SetMovementSpeeds(float InWalkSpeedMps, float InBoostSpeedMps)
{
	// Unity(m/s) → UE(cm/s)、さらに193cm機体時代と同じ体格比の移動感へ合わせる。
	WalkSpeedCms = FMath::Max(0.0f, InWalkSpeedMps) * 100.0f * MSGO::MSScale;
	BoostSpeedCms = FMath::Max(0.0f, InBoostSpeedMps) * 100.0f * MSGO::MSScale;

	// エンジン既定経路(PathFollowing等)がMaxWalkSpeedを参照するケースに備えて同期
	MaxWalkSpeed = WalkSpeedCms;
}

bool UMSCharacterMovementComponent::IsBoostDashing() const
{
	// Unity: IsDashing = 非しゃがみ・Dash押下・非オーバーヒート・残量>0
	return bBoostInputHeld && CanUseBoost();
}

bool UMSCharacterMovementComponent::IsHoverAscending() const
{
	// ホバーキー保持+ブースト使用可+空中(接地中の離陸はUpdateCharacterStateBeforeMovementで遷移)
	return bHoverInputHeld && CanUseBoost() && IsFalling();
}

bool UMSCharacterMovementComponent::IsGliding() const
{
	// Unity: ブーストダッシュ空中滑空時は verticalVelocity=0f で高度維持。
	// ホバー上昇が優先され、それ以外の空中ブーストダッシュ中が滑空。
	return IsFalling() && IsBoostDashing() && !IsHoverAscending();
}

bool UMSCharacterMovementComponent::IsConsumingBoost() const
{
	// Unity: HandleBoostState — (wantsDash || wantsHover) かつ 使用可能条件成立で消費。
	// しゃがみ中・よろけ中はEN消費停止(回復継続)= CanUseBoost() が false。
	return (bBoostInputHeld || bHoverInputHeld) && CanUseBoost();
}

bool UMSCharacterMovementComponent::IsUnderwater() const
{
	const APhysicsVolume* PhysicsVolume = GetPhysicsVolume();
	if (IsSwimming() && (!PhysicsVolume || !PhysicsVolume->bWaterVolume))
	{
		// テスト/明示的なSwimming移動モードは水中として扱う。
		return true;
	}
	if (!PhysicsVolume || !PhysicsVolume->bWaterVolume || !UpdatedComponent)
	{
		return false;
	}

	const FBox VolumeBounds = PhysicsVolume->GetComponentsBoundingBox(/*bNonColliding=*/true);
	return !VolumeBounds.IsValid ||
		VolumeBounds.Max.Z - UpdatedComponent->GetComponentLocation().Z >= UnderwaterDepthThresholdCm;
}

float UMSCharacterMovementComponent::ComputeWaterRippleInterval(
	const float HorizontalSpeedCms, const float ReferenceMaxSpeedCms)
{
	const float SpeedAlpha = FMath::Clamp(
		HorizontalSpeedCms / FMath::Max(ReferenceMaxSpeedCms, 1.0f), 0.0f, 1.0f);
	return FMath::Lerp(MSWaterRippleMaxInterval, MSWaterRippleMinInterval, SpeedAlpha);
}

void UMSCharacterMovementComponent::UpdateWaterFootRipple(const float DeltaSeconds)
{
	ENetMode WaterRippleNetMode = GetNetMode();
#if WITH_DEV_AUTOMATION_TESTS
	if (WaterRippleNetModeOverrideForTest.IsSet())
	{
		WaterRippleNetMode = WaterRippleNetModeOverrideForTest.GetValue();
	}
#endif

	FVector SurfaceLocation = FVector::ZeroVector;
	if (!UpdatedComponent || WaterRippleNetMode == NM_DedicatedServer ||
		!GetOwner() || GetOwner()->IsActorBeingDestroyed() || OwnerHasTag(MSTags::State_Dead) ||
		!FindIntersectingWaterSurface(SurfaceLocation))
	{
		bWasTouchingWater = false;
		WaterRippleElapsed = 0.0f;
		return;
	}

	const float HorizontalSpeed = Velocity.Size2D();
	if (!bWasTouchingWater)
	{
		SpawnWaterFootRipple(SurfaceLocation, HorizontalSpeed, /*bEntrySplash=*/true);
		bWasTouchingWater = true;
		WaterRippleElapsed = 0.0f;
		return;
	}

	const float Interval = ComputeWaterRippleInterval(HorizontalSpeed, FMath::Max(GetMaxSpeed(), 1.0f));
	// A stationary, partly emerged MS still displaces the surface. Saturating the
	// timer emits at most one transient per tick after a hitch or replay gap.
	WaterRippleElapsed = FMath::Min(
		WaterRippleElapsed + FMath::Max(DeltaSeconds, 0.0f), Interval);
	if (WaterRippleElapsed >= Interval)
	{
		WaterRippleElapsed = 0.0f;
		SpawnWaterFootRipple(SurfaceLocation, HorizontalSpeed, /*bEntrySplash=*/false);
	}
}

#if WITH_EDITOR
void UMSCharacterMovementComponent::UpdateWaterFootRippleForEditorAcceptance(
	const float DeltaSeconds)
{
	UpdateWaterFootRipple(DeltaSeconds);
}
#endif

bool UMSCharacterMovementComponent::FindIntersectingWaterSurface(FVector& OutSurfaceLocation) const
{
	if (!UpdatedComponent || !GetWorld())
	{
		return false;
	}

	const FVector CapsuleCenter = UpdatedComponent->GetComponentLocation();
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	const float FootZ = CapsuleCenter.Z - CapsuleHalfHeight;
	const float TopZ = CapsuleCenter.Z + CapsuleHalfHeight;

	auto TryVolume = [&](const APhysicsVolume* Volume) -> bool
	{
		if (!Volume || !Volume->bWaterVolume)
		{
			return false;
		}
		const FBox Bounds = Volume->GetComponentsBoundingBox(/*bNonColliding=*/true);
		if (!Bounds.IsValid ||
			CapsuleCenter.X < Bounds.Min.X || CapsuleCenter.X > Bounds.Max.X ||
			CapsuleCenter.Y < Bounds.Min.Y || CapsuleCenter.Y > Bounds.Max.Y ||
			TopZ <= Bounds.Min.Z)
		{
			return false;
		}
		// A slanted or curved brush can have dry land inside its AABB. Probe the
		// actual footprint where a brush exists (Taigan always needs this check).
		// Box-only test volumes retain their existing bounds-based behaviour.
		const UBrushComponent* Brush = Volume->GetBrushComponent();
		if (Volume->ActorHasTag(TEXT("NewYorkTaiganGenerated")) || (Brush && Brush->Brush))
		{
			const float ProbeZ = FMath::Clamp(
				FMath::Min(TopZ, Bounds.Max.Z), Bounds.Min.Z + 1.0f, Bounds.Max.Z - 1.0f);
			if (!Volume->EncompassesPoint(FVector(CapsuleCenter.X, CapsuleCenter.Y, ProbeZ)))
			{
				return false;
			}
		}
		// The penalty volume top is only a detection boundary. Daibao's rendered sea surface is
		// currently 30 cm while the shared volume top is 0 cm; using Bounds.Max.Z produced the
		// observed all-sites Z=2.00 and buried the horizontal foam ring. Resolve the visible
		// water mesh covering this XY so rivers and inland water at other elevations also work.
		float SurfaceZ = Bounds.Max.Z;
		const bool bHasRenderedSurface = FindRenderedWaterSurfaceZ(
			*GetWorld(), FVector(CapsuleCenter.X, CapsuleCenter.Y, Bounds.Max.Z),
			RenderedWaterSurfaceSearchToleranceCm, SurfaceZ);
		// Some detection volumes cover more land than their visible water mesh.
		// An AABB-only candidate needs a visible surface. For the selected water
		// volume, retain shallow legacy contact; deeper immersion needs the mesh.
		if (!bHasRenderedSurface &&
			(Volume != GetPhysicsVolume() ||
				FootZ < Bounds.Max.Z - WaterSurfaceContactToleranceCm))
		{
			return false;
		}
		// The lower edge may just graze the water, but the upper edge must remain
		// above it. Once the capsule is fully below the surface, stop the wake.
		if (FootZ > SurfaceZ + WaterSurfaceContactToleranceCm || TopZ <= SurfaceZ)
		{
			return false;
		}
		OutSurfaceLocation = FVector(CapsuleCenter.X, CapsuleCenter.Y, SurfaceZ + 2.0f);
		return true;
	};

	// Fast path where CharacterMovement already selected the water volume.
	if (TryVolume(GetPhysicsVolume()))
	{
		return true;
	}

	// APhysicsVolume selection is based on the character centre. Check water volumes
	// against the whole capsule so shallow and partially immersed contact both work.
	for (TActorIterator<APhysicsVolume> It(GetWorld()); It; ++It)
	{
		if (TryVolume(*It))
		{
			return true;
		}
	}
	return false;
}

void UMSCharacterMovementComponent::SpawnWaterFootRipple(
	const FVector& SurfaceLocation, const float HorizontalSpeedCms, const bool bEntrySplash)
{
	if (!GetWorld())
	{
		return;
	}

	const float SpeedScale = FMath::GetMappedRangeValueClamped(
		FVector2D(0.0f, FMath::Max(GetMaxSpeed(), 1.0f)), FVector2D(0.85f, 1.35f), HorizontalSpeedCms);
	const float EntryScale = bEntrySplash ? 1.2f : 1.0f;
#if WITH_DEV_AUTOMATION_TESTS
	// Count the spawn request so branch tests also work under -nullrhi, where the VFX actor
	// may fail to spawn.
	++WaterRippleSpawnCountForTest;
#endif
	// The mesh silhouette remains the deterministic fallback while the optional
	// Niagara detail pass is streamed independently.
	AMSTransientVfxActor::SpawnWaterFootSplash(
		GetWorld(), SurfaceLocation, SpeedScale * EntryScale, bEntrySplash, 0.9f);
	UE_LOG(LogTemp, Log,
		TEXT("MSWaterFootRipple: Spawned water foot splash at %s speed=%.1f entry=%s"),
		*SurfaceLocation.ToCompactString(), HorizontalSpeedCms,
		bEntrySplash ? TEXT("true") : TEXT("false"));
}

void UMSCharacterMovementComponent::SetZeroGravityEnabled(const bool bEnabled)
{
	if (bZeroGravityEnabled == bEnabled) return;
	bZeroGravityEnabled = bEnabled;
	bZeroGravityAscendInputHeld = false;
	bZeroGravityDescendInputHeld = false;

	if (bZeroGravityEnabled)
	{
		PreZeroGravityBrakingDecelerationFlying = BrakingDecelerationFlying;
		BrakingDecelerationFlying = ZeroGravityBrakingDeceleration;
		// Start in free flight. PhysFlying will settle onto a nearby walkable surface naturally.
		SetMovementMode(MOVE_Flying);
	}
	else
	{
		BrakingDecelerationFlying = PreZeroGravityBrakingDecelerationFlying;
		if (IsFlying())
		{
			SetMovementMode(MOVE_Falling);
		}
	}
}

void UMSCharacterMovementComponent::RefreshZeroGravityModeFromWorld()
{
	const UWorld* World = GetWorld();
	const AMSGameState* GameState = World ? World->GetGameState<AMSGameState>() : nullptr;
	SetZeroGravityEnabled(GameState && GameState->IsZeroGravityMap());
}

float UMSCharacterMovementComponent::GetZeroGravityVerticalInput() const
{
	return static_cast<float>(bZeroGravityAscendInputHeld) -
		static_cast<float>(bZeroGravityDescendInputHeld);
}

void UMSCharacterMovementComponent::SetZeroGravityAscendInputHeld(const bool bHeld)
{
	bZeroGravityAscendInputHeld = bHeld;
	if (bHeld && bZeroGravityEnabled && IsMovingOnGround())
	{
		SetMovementMode(MOVE_Flying);
	}
}

void UMSCharacterMovementComponent::SetZeroGravityDescendInputHeld(const bool bHeld)
{
	bZeroGravityDescendInputHeld = bHeld;
}

float UMSCharacterMovementComponent::GetMaxSpeed() const
{
	// This gate must precede the non-zero-gravity flying fallback as well as the
	// walking/boost switch: hit reactions suspend player input in either map mode.
	if (IsIncapacitated() || IsCrouchingByTag())
	{
		return 0.0f;
	}
	const float ModuleMultiplier = OwnerHasTag(MSTags::State_Module_EffectActive)
		? ModuleSpeedMultiplier : 1.0f;
	const auto* AssistUnit = Cast<AMSUnitBase>(GetOwner());
 const float WaterRatio = AssistUnit ? AssistUnit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::WaterInaptitudeSpeedRatio, 80.f) * .01f : MSUnderwaterSpeedMultiplier;
 const float UnderwaterMultiplier = IsUnderwater() && !bAmphibious ? WaterRatio : 1.f;
 const float WalkMultiplier = 1.f + (AssistUnit ? AssistUnit->GetBattleAssistEffectValue(EMSBattleAssistEffectKind::WalkSpeedIncrease) * .01f : 0.f);
	if (MovementMode == MOVE_Flying && !bZeroGravityEnabled)
	{
		return Super::GetMaxSpeed() * UnderwaterMultiplier;
	}
	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
	case MOVE_Flying:
	{
		if (IsBoostDashing())
		{
			// 非前進方向へのブーストは88%へ低減(Unity: nonForwardBoostRatio=0.88f)
			return ApplyNonForwardBoostRatio(BoostSpeedCms) * ModuleMultiplier * UnderwaterMultiplier;
		}
		// MaxWalkSpeed is kept in sync by SetMovementSpeeds and may be temporarily adjusted by BOT AI.
		return MaxWalkSpeed * WalkMultiplier * ModuleMultiplier * UnderwaterMultiplier;
	}
	default:
		return Super::GetMaxSpeed() * UnderwaterMultiplier;
	}
}

float UMSCharacterMovementComponent::GetGravityZ() const
{
	if (bZeroGravityEnabled)
	{
		return 0.0f;
	}
	// ホバー上昇/滑空中は重力を無効化し、PhysFallingで垂直速度を直接制御する
	// (Unity: ホバー中は verticalVelocity を直接設定、滑空中は 0f 固定)
	if (IsHoverAscending() || IsGliding())
	{
		return 0.0f;
	}
	return Super::GetGravityZ();
}

FVector UMSCharacterMovementComponent::GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	// ブースト中はUnity版どおりAirControl=1.0のフル挙動を維持し、非ブースト時のみ半減する
	// (WASDでの空中水平操作が強すぎるという指摘対応)。
	if (IsBoostDashing())
	{
		return Super::GetAirControl(DeltaTime, TickAirControl, FallAcceleration);
	}
	return Super::GetAirControl(DeltaTime, TickAirControl, FallAcceleration) * MSNonBoostAirControlMultiplier;
}

void UMSCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	// Start the cosmetic Niagara request before the first contact. This is
	// intentionally independent of movement simulation and is a no-op on a
	// dedicated server.
	AMSTransientVfxActor::PreloadWaterFootSplash(GetWorld());
}

void UMSCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bWasTouchingWater = false;
	WaterRippleElapsed = 0.0f;
	Super::EndPlay(EndPlayReason);
}

void UMSCharacterMovementComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	// The base tick may run autonomous prediction, correction, and replay. Do
	// not emit water VFX from those hooks; run the cadence once after the real
	// movement tick has completed instead.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || GetNetMode() == NM_DedicatedServer ||
		TickType == LEVELTICK_TimeOnly || DeltaTime <= 0.0f)
	{
		return;
	}
	UpdateWaterFootRipple(DeltaTime);
}

void UMSCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (bZeroGravityEnabled && IsFlying() && GetZeroGravityVerticalInput() <= 0.0f &&
		Acceleration.Z <= 0.0f && Velocity.Z <= 0.0f && UpdatedComponent)
	{
		FFindFloorResult Floor;
		FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
		if (Floor.IsWalkableFloor() && Floor.FloorDist <= ZeroGravityFloorSnapDistance)
		{
			SetMovementMode(MOVE_Walking);
		}
	}

	// 接地中にホバー開始 → 離陸(Falling遷移+初速付与)。TakeoffBaseZはOnMovementModeChangedで記録
	if (bHoverInputHeld && CanUseBoost() && IsMovingOnGround())
	{
		SetMovementMode(MOVE_Falling);
		Velocity.Z = ComputeHoverAscendSpeed();
	}

	// State.Boosting LooseTag をアニメ/VFX(P5-1/P5-7)向けに同期
	UpdateBoostingLooseTag();
}

void UMSCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	const bool bWasGrounded = PreviousMovementMode == MOVE_Walking || PreviousMovementMode == MOVE_NavWalking;
	if (!bApplyingBoostCorrection && bWasGrounded != IsMovingOnGround())
	{
		BoostMovementState.RecoveryDelayRemaining = BoostMovementState.RecoveryDelaySeconds;
		BoostMovementState.bGrounded = IsMovingOnGround();
		if (UMSBoostComponent* Driver = ResolveBoostDriver()) Driver->NotifyGroundingChanged(IsMovingOnGround());
	}
	if (!bApplyingBoostCorrection && IsMovingOnGround()) bHasTakeoffBase = false;

	// Walking uses normal floor handling. Losing that floor must enter free flight, never gravity fall.
	if (bZeroGravityEnabled && MovementMode == MOVE_Falling)
	{
		SetMovementMode(MOVE_Flying);
		return;
	}

	// 接地→空中の遷移で離陸地点を記録(Unity: maxJumpHeightは「離陸地点からの」最大上昇高度)。
	// ホバー離陸・歩き落下・ブースト滑空開始のいずれもここを通る。
	if (!bApplyingBoostCorrection && MovementMode == MOVE_Falling &&
		(PreviousMovementMode == MOVE_Walking || PreviousMovementMode == MOVE_NavWalking))
	{
		TakeoffBaseZ = UpdatedComponent ? static_cast<float>(UpdatedComponent->GetComponentLocation().Z) : 0.0f;
		bHasTakeoffBase = UpdatedComponent != nullptr;
	}
}

void UMSCharacterMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	if (IsHoverAscending())
	{
		// ホバー上昇: 上限(離陸+4m)手前0.6mの減速帯で線形に減速、上限で0=その場ホバリング
		Velocity.Z = ComputeHoverAscendSpeed();
	}
	else if (IsGliding())
	{
		// 滑空高度維持(Unity: verticalVelocity=0f)
		Velocity.Z = 0.0f;
	}

	Super::PhysFalling(deltaTime, Iterations);
}

FVector UMSCharacterMovementComponent::NewFallVelocity(
	const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	// UCharacterMovementComponent::NewFallVelocity と同じ手順だが、終端速度に
	// PhysicsVolume::TerminalVelocity(UE既定4000cm/s=18m機体では機体比~4m/sの鈍い降下)
	// ではなく MSScale 倍の TerminalFallSpeedCms を使う。ボリューム側がより大きい値を
	// 明示設定している場合はそちらを尊重する。
	// (2026-07-19: 一度エンジン既定へ戻したが「重力が軽い」と再指摘があり、原因はここの
	// クランプが低すぎたことと判明。MSScale版へ再度戻した)
	FVector Result = InitialVelocity;
	if (DeltaTime > 0.0f)
	{
		Result += Gravity * DeltaTime;

		const float TerminalLimit = FMath::Max(
			FMath::Abs(GetPhysicsVolume()->TerminalVelocity), TerminalFallSpeedCms);
		if (Result.SizeSquared() > FMath::Square(TerminalLimit))
		{
			const FVector GravityDir = Gravity.GetSafeNormal();
			if ((Result | GravityDir) > TerminalLimit)
			{
				Result = FVector::PointPlaneProject(Result, FVector::ZeroVector, GravityDir)
					+ GravityDir * TerminalLimit;
			}
		}
	}
	return Result;
}

void UMSCharacterMovementComponent::PhysFlying(float deltaTime, int32 Iterations)
{
	if (!bZeroGravityEnabled)
	{
		Super::PhysFlying(deltaTime, Iterations);
		return;
	}

	const bool bInputBlocked = IsIncapacitated() || IsCrouchingByTag();
	if (bInputBlocked) Acceleration.Z = 0.0f;
	const float VerticalInput = bInputBlocked ? 0.0f : GetZeroGravityVerticalInput();
	if (!FMath::IsNearlyZero(VerticalInput))
	{
		Acceleration.Z = VerticalInput * MaxAcceleration;
		// Vertical input is tracked outside the pawn's horizontal input vector. Without
		// this, CalcVelocity sees a zero analog modifier and clamps vertical-only motion
		// back to zero even though Acceleration.Z is valid.
		AnalogInputModifier = FMath::Max(AnalogInputModifier, FMath::Abs(VerticalInput));
	}
	BrakingDecelerationFlying = ZeroGravityBrakingDeceleration;
	Super::PhysFlying(deltaTime, Iterations);
}

bool UMSCharacterMovementComponent::IsIncapacitated() const
{
	return OwnerHasTag(MSTags::State_Dead)
		|| OwnerHasTag(MSTags::State_Flinched)
		|| OwnerHasTag(MSTags::State_Staggered)
		|| OwnerHasTag(MSTags::State_KnockedDown)
		|| OwnerHasTag(MSTags::State_GettingUp)
		// P4-7: 格闘の攻撃後硬直(0.45s)中は移動キャンセル不可=全入力ブロック
		// (Unity: dashMeleeRecoveryTime。State.MeleeRecoveryタグはGA_Meleeが付与/除去)
		|| OwnerHasTag(MSTags::State_MeleeRecovery);
}

bool UMSCharacterMovementComponent::IsCrouchingByTag() const
{
	// しゃがみトグル(P3-4)はState.Crouching LooseTagで表現(Architecture §1.3/§2.2)
	return OwnerHasTag(MSTags::State_Crouching);
}

bool UMSCharacterMovementComponent::CanUseBoost() const
{
	// Unity: 非しゃがみ・非よろけ・非オーバーヒート・残量あり(#131: しゃがみ中のEN消費バグ再発防止)
	if (IsIncapacitated() || IsCrouchingByTag())
	{
		return false;
	}
	// The owning proxy uses the move-time gate; delayed generic OH replication must
	// not override the corrected/replayed energy state. Other incapacitation stays above.
	if (CachedBoostDriver.IsValid() && CachedBoostDriver->UsesMovementClock() &&
		CharacterOwner && !CharacterOwner->HasAuthority() && BoostMovementState.bInitialized)
	{
		return BoostMovementState.CanUseEnergy();
	}
	return !OwnerHasTag(MSTags::State_Overheated) &&
		(!BoostAvailableDelegate.IsBound() || BoostAvailableDelegate.Execute());
}

float UMSCharacterMovementComponent::ComputeHoverAscendSpeed() const
{
	if (!UpdatedComponent)
	{
		return 0.0f;
	}

	// 上限までの残り高度に応じた減速係数(残り>=BrakeZoneで1、上限到達で0。Unity: jumpCeilingBrakeZone)
	const float CeilingZ = TakeoffBaseZ + MaxHoverHeight;
	const float Remaining = CeilingZ - static_cast<float>(UpdatedComponent->GetComponentLocation().Z);
	const float BrakeFactor = FMath::Clamp(Remaining / HoverCeilingBrakeZone, 0.0f, 1.0f);
	return HoverAscendSpeed * BrakeFactor;
}

float UMSCharacterMovementComponent::ApplyNonForwardBoostRatio(float Speed) const
{
	if (!UpdatedComponent)
	{
		return Speed;
	}

	const FVector AccelDir2D = Acceleration.GetSafeNormal2D();
	if (AccelDir2D.IsNearlyZero())
	{
		return Speed;
	}

	// 入力方向と機体前方の水平内積(0〜1)で 0.88〜1.0 をLerp。
	// 純前進=1.0 / 横・後退=0.88(Unity: nonForwardBoostRatio の連続近似)
	const FVector Forward2D = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float ForwardDot = FMath::Clamp(static_cast<float>(FVector::DotProduct(AccelDir2D, Forward2D)), 0.0f, 1.0f);
	return Speed * FMath::Lerp(NonForwardBoostRatio, 1.0f, ForwardDot);
}

bool UMSCharacterMovementComponent::OwnerHasTag(const FGameplayTag& Tag) const
{
	if (!CachedASC.IsValid())
	{
		CachedASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	}
	return CachedASC.IsValid() && CachedASC->HasMatchingGameplayTag(Tag);
}

void UMSCharacterMovementComponent::UpdateBoostingLooseTag()
{
	// ダッシュ/ホバー/滑空のいずれかでブースト使用中(§2.2 State.Boosting: アニメ・VFX参照用)
	const bool bActive = IsBoostDashing() || IsHoverAscending();
	if (bActive == bBoostingTagApplied)
	{
		return;
	}

	if (!CachedASC.IsValid())
	{
		CachedASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	}
	if (!CachedASC.IsValid())
	{
		return;
	}

	CachedASC->SetLooseGameplayTagCount(MSTags::State_Boosting, bActive ? 1 : 0);
	bBoostingTagApplied = bActive;
}
