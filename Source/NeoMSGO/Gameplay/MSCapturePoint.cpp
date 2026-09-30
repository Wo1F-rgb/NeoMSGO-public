// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSCapturePoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/MSScale.h"
#include "Core/MSTeamStatics.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSUnitRegistrySubsystem.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace MSCapturePointVisuals
{
	const FLinearColor NeutralColor(0.75f, 0.75f, 0.75f, 1.0f);
	const FLinearColor PlayerColor(0.25f, 0.55f, 1.0f, 1.0f);
	const FLinearColor EnemyColor(0.85f, 0.2f, 0.15f, 1.0f);
}

AMSCapturePoint::AMSCapturePoint()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Capture points are a small, map-wide set of gameplay metadata.  They must
	// remain available to deployment/map UI even when the pawn is far away from
	// the point (Daibao is much larger than the engine's default net cull range).
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FlagPole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagPole"));
	FlagPole->SetupAttachment(SceneRoot);
	FlagPole->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f) * MSGO::MSScale);
	FlagPole->SetRelativeScale3D(FVector(0.08f, 0.08f, 3.0f) * MSGO::MSScale);
	FlagPole->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Flag = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flag"));
	Flag->SetupAttachment(SceneRoot);
	Flag->SetRelativeLocation(FVector(55.0f, 0.0f, 275.0f) * MSGO::MSScale);
	Flag->SetRelativeScale3D(FVector(1.1f, 0.06f, 0.5f) * MSGO::MSScale);
	Flag->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RangeRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RangeRing"));
	RangeRing->SetupAttachment(SceneRoot);
	RangeRing->SetRelativeLocation(FVector(0.0f, 0.0f, 2.0f) * MSGO::MSScale);
	RangeRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RangeRing->SetVisibility(false);
	RangeRing->SetHiddenInGame(true);
	RangeOutline = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RangeOutline"));
	RangeOutline->SetupAttachment(SceneRoot);
	RangeOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RangeOutline->SetCanEverAffectNavigation(false);
	RangeOutline->SetCastShadow(false);
	PointLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PointLabel"));
	PointLabel->SetupAttachment(SceneRoot);
	PointLabel->SetRelativeLocation(FVector(0,0,325.f)*MSGO::MSScale);
	PointLabel->SetHorizontalAlignment(EHTA_Center);
	PointLabel->SetVerticalAlignment(EVRTA_TextCenter);
	PointLabel->SetWorldSize(42.f*MSGO::MSScale);
	PointLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PointLabel->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CylinderMesh.Succeeded())
	{
		FlagPole->SetStaticMesh(CylinderMesh.Object);
		RangeRing->SetStaticMesh(CylinderMesh.Object);
	}
	if (CubeMesh.Succeeded())
	{
		Flag->SetStaticMesh(CubeMesh.Object);
		RangeOutline->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OutlineMaterial(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	if (OutlineMaterial.Succeeded()) RangeOutline->SetMaterial(0, OutlineMaterial.Object);
}

void AMSCapturePoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateRangeRingGeometry();
}

void AMSCapturePoint::BeginPlay()
{
	Super::BeginPlay();
	UpdateRangeRingGeometry();
	// The point is a numbered beacon, not a fabric flag. Keep the legacy component
	// for saved instances but remove its solid billboard from the scene.
	Flag->SetVisibility(false);
	Flag->SetHiddenInGame(true);
	UpdateVisuals();
}

void AMSCapturePoint::UpdateRangeRingGeometry()
{
	if (!RangeRing) return;
	// Engine Cylinder diameter is 100 UU. The visual diameter is derived from the
	// gameplay radius so edited instances cannot drift away from capture judgement.
	const float DiameterScale = FMath::Max(0.0f, CaptureRadius) * 2.0f / 100.0f;
	RangeRing->SetRelativeScale3D(FVector(DiameterScale, DiameterScale, 0.01f * MSGO::MSScale));
	// Keep the radius contract for legacy callers, but never cover the ground with
	// an opaque cylinder. A narrow continuous perimeter denotes the playable area.
	RangeRing->SetVisibility(false);
	RangeRing->SetHiddenInGame(true);
	if (RangeOutline)
	{
		RangeOutline->SetVisibility(!IsCaptureLocked());
		RangeOutline->SetHiddenInGame(IsCaptureLocked());
		RangeOutline->ClearInstances();
		constexpr int32 Segments=96;
		const float HalfAngle=PI/Segments;
		const float Length=2.f*CaptureRadius*FMath::Sin(HalfAngle);
		for(int32 Index=0;Index<Segments;++Index)
		{
			const float Angle=(Index+.5f)*2.f*PI/Segments;
			const FVector Center(FMath::Cos(Angle)*CaptureRadius*FMath::Cos(HalfAngle),FMath::Sin(Angle)*CaptureRadius*FMath::Cos(HalfAngle),24.f);
			RangeOutline->AddInstance(FTransform(FRotator(0,FMath::RadiansToDegrees(Angle)+90.f,0),Center,FVector((Length+2.f)/100.f,0.45f,0.04f)));
		}
	}
}

void AMSCapturePoint::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bProgressChangedThisFrame = false;
	if(PointLabel)
	{
		if(const APlayerController* PC=GetWorld()->GetFirstPlayerController())
			if(PC->PlayerCameraManager)
				PointLabel->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-PointLabel->GetComponentLocation()).Rotation());
	}

	if (HasAuthority())
	{
		if (const UMSUnitRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UMSUnitRegistrySubsystem>())
		{
			LastAllyCount = Registry->CountUnitsOfTeamInRange(MSTeam::PlayerTeamId(), GetActorLocation(), CaptureRadius);
			LastEnemyCount = Registry->CountUnitsOfTeamInRange(MSTeam::EnemyTeamId(), GetActorLocation(), CaptureRadius);
			TickCaptureLogic(LastAllyCount, LastEnemyCount, DeltaSeconds);
		}
	}

	if (bProgressChangedThisFrame && Flag)
	{
		Flag->AddLocalRotation(FRotator(0.0f, 20.0f * DeltaSeconds, 0.0f));
	}
}

float AMSCapturePoint::CalculateCaptureSpeedMultiplier(const int32 CapturerCount, const int32 EffectiveCapturerLimit)
{
	const int32 Effective = FMath::Clamp(CapturerCount, 1, FMath::Max(1, EffectiveCapturerLimit));
	return 1.0f + 0.5f * static_cast<float>(Effective - 1);
}

void AMSCapturePoint::TickCaptureLogic(const int32 AllyCount, const int32 EnemyCount, const float DeltaSeconds)
{
	if (IsCaptureLocked() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float PreviousProgress = Progress;
	if (AllyCount > 0 && EnemyCount == 0)
	{
		Progress += CaptureRate * CalculateCaptureSpeedMultiplier(AllyCount, MaxEffectiveCapturers) * DeltaSeconds;
	}
	else if (EnemyCount > 0 && AllyCount == 0)
	{
		Progress -= CaptureRate * CalculateCaptureSpeedMultiplier(EnemyCount, MaxEffectiveCapturers) * DeltaSeconds;
	}
	else if (AllyCount == 0 && EnemyCount == 0)
	{
		float RestingProgress = 0.0f;
		switch (CurrentOwner)
		{
		case EMSCaptureOwner::TeamPlayer: RestingProgress = 1.0f; break;
		case EMSCaptureOwner::TeamEnemy: RestingProgress = -1.0f; break;
		case EMSCaptureOwner::Neutral: default: break;
		}
		Progress = FMath::FInterpConstantTo(Progress, RestingProgress, DeltaSeconds, DecayRate);
	}

	Progress = FMath::Clamp(Progress, -1.0f, 1.0f);
	bProgressChangedThisFrame = !FMath::IsNearlyEqual(PreviousProgress, Progress);

	if (Progress >= 1.0f)
	{
		SetOwnerAndNotify(EMSCaptureOwner::TeamPlayer);
	}
	else if (Progress <= -1.0f)
	{
		SetOwnerAndNotify(EMSCaptureOwner::TeamEnemy);
	}
	else if ((CurrentOwner == EMSCaptureOwner::TeamPlayer && Progress <= 0.0f) ||
		(CurrentOwner == EMSCaptureOwner::TeamEnemy && Progress >= 0.0f))
	{
		SetOwnerAndNotify(EMSCaptureOwner::Neutral);
	}

	if (bProgressChangedThisFrame)
	{
		UpdateVisuals();
	}
}

void AMSCapturePoint::SetFixedOwner(const EMSCaptureOwner NewOwner)
{
	Progress = NewOwner == EMSCaptureOwner::TeamPlayer ? 1.0f :
		(NewOwner == EMSCaptureOwner::TeamEnemy ? -1.0f : 0.0f);
	SetOwnerAndNotify(NewOwner);
	UpdateVisuals();
}

FGenericTeamId AMSCapturePoint::GetOwnerTeamId() const
{
	return UMSTeamStatics::CaptureOwnerToTeamId(CurrentOwner);
}

bool AMSCapturePoint::CanDeployFor(const FGenericTeamId TeamId) const
{
	return UMSTeamStatics::CaptureOwnerToTeamId(CurrentOwner) == TeamId;
}

AMSCapturePoint* AMSCapturePoint::FindNearestDeployablePoint(UWorld* World, const FGenericTeamId TeamId,
	const FVector& WorldLocation, float& OutDistance2D)
{
	OutDistance2D = TNumericLimits<float>::Max();
	if (!World) return nullptr;
	AMSCapturePoint* Nearest = nullptr;
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
	{
		AMSCapturePoint* Point = *It;
		if (!IsValid(Point) || !Point->CanDeployFor(TeamId)) continue;
		const float Distance = FVector::Dist2D(WorldLocation, Point->GetActorLocation());
		if (Distance < OutDistance2D) { OutDistance2D = Distance; Nearest = Point; }
	}
	return Nearest;
}

void AMSCapturePoint::SetOwnerAndNotify(const EMSCaptureOwner NewOwner)
{
	if (CurrentOwner == NewOwner)
	{
		return;
	}
	CurrentOwner = NewOwner;
	OnCaptureOwnerChanged.Broadcast(this, NewOwner);
	UpdateVisuals();
}

void AMSCapturePoint::OnRep_Progress()
{
	Progress = FMath::Clamp(Progress, -1.0f, 1.0f);
	UpdateVisuals();
}

void AMSCapturePoint::OnRep_CurrentOwner(const EMSCaptureOwner PreviousOwner)
{
	if (PreviousOwner != CurrentOwner)
	{
		OnCaptureOwnerChanged.Broadcast(this, CurrentOwner);
	}
	UpdateVisuals();
}

void AMSCapturePoint::ApplyVisualColor(UStaticMeshComponent* Component, UMaterialInstanceDynamic*& Material,
	const FLinearColor& Color)
{
	if (!Component)
	{
		return;
	}
	if (!Material)
	{
		Material = Component->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}
}

void AMSCapturePoint::UpdateVisuals_Implementation()
{
	FLinearColor TeamColor = MSCapturePointVisuals::NeutralColor;
	switch (CurrentOwner)
	{
	case EMSCaptureOwner::TeamPlayer: TeamColor = MSCapturePointVisuals::PlayerColor; break;
	case EMSCaptureOwner::TeamEnemy: TeamColor = MSCapturePointVisuals::EnemyColor; break;
	case EMSCaptureOwner::Neutral: default: break;
	}

	UMaterialInstanceDynamic* FlagMID = FlagMaterial;
	ApplyVisualColor(Flag, FlagMID, TeamColor);
	FlagMaterial = FlagMID;
	if(PointLabel)
	{
		PointLabel->SetVisibility(!IsCaptureLocked());
		PointLabel->SetHiddenInGame(IsCaptureLocked());
		PointLabel->SetText(PointName);
		PointLabel->SetTextRenderColor(TeamColor.ToFColor(true));
	}
	FLinearColor RingColor = TeamColor;
	RingColor.A = CurrentOwner == EMSCaptureOwner::Neutral ? 0.18f : 0.4f;
	UMaterialInstanceDynamic* RingMID = RingMaterial;
	ApplyVisualColor(RangeRing, RingMID, RingColor);
	RingMaterial = RingMID;
	if (RangeOutline)
	{
		RangeOutline->SetCustomPrimitiveDataFloat(0,1.f);
		RangeOutline->SetCustomPrimitiveDataVector4(1,FVector4(TeamColor.R,TeamColor.G,TeamColor.B,1.f));
	}
}

void AMSCapturePoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSCapturePoint, Progress);
	DOREPLIFETIME(AMSCapturePoint, CurrentOwner);
}
