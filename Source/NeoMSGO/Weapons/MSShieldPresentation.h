#pragma once

#include "CoreMinimal.h"
#include "Animation/MSMechRigComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

/** One authored-shield attachment contract for hangar and combat. No materials are replaced. */
namespace MSShieldPresentation
{
	inline UStaticMeshComponent* Part(UStaticMeshComponent* Shield, const FName Name,
		const FLinearColor& Color)
	{
		if (!Shield || !Shield->GetOwner()) return nullptr;
		UStaticMeshComponent* Result = nullptr;
		for (USceneComponent* Child : Shield->GetAttachChildren())
			if (Child->GetFName() == Name) Result = Cast<UStaticMeshComponent>(Child);
		if (!Result)
		{
			// Use the same explicit Color-parameter material as MSMechRig, rather
			// than relying on whichever default material the engine cube contains.
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			FLinearColor ExistingColor;
			if (!ensureMsgf(Base && Base->GetVectorParameterValue(
				FHashedMaterialParameterInfo(TEXT("Color")), ExistingColor),
				TEXT("Shield support/damage material must expose the Color vector parameter"))) return nullptr;
			Result = NewObject<UStaticMeshComponent>(Shield->GetOwner(), Name, RF_Transient);
			Shield->GetOwner()->AddInstanceComponent(Result);
			Result->SetupAttachment(Shield);
			Result->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Result->SetMaterial(0, Base);
			Result->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Result->SetGenerateOverlapEvents(false);
			Result->SetCanEverAffectNavigation(false);
			Result->RegisterComponent();
		}
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Result->GetMaterial(0));
		if (!Material) Material = Result->CreateAndSetMaterialInstanceDynamic(0);
		if (Material) Material->SetVectorParameterValue(TEXT("Color"), Color);
		return Result;
	}

	inline void SetGuardBrace(UStaticMeshComponent* Shield, const FVector& ForearmCenter,
		const FQuat& ForearmRotation, const FVector& Up, const float RigScale, const bool bShow)
	{
		if (!bShow)
		{
			for (USceneComponent* Child : Shield->GetAttachChildren())
				if (Child->GetName().StartsWith(TEXT("NativeShieldGuard"))) Child->SetVisibility(false, true);
			return;
		}
		const FLinearColor Metal(0.045f, 0.055f, 0.060f);
		if (UStaticMeshComponent* Cuff = Part(Shield, TEXT("NativeShieldGuardCuff"), Metal))
		{
			Cuff->SetWorldTransform(FTransform(ForearmRotation, ForearmCenter,
				FVector(22.0f, 22.0f, 9.0f) * RigScale / 100.0f));
			Cuff->SetVisibility(true);
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Offset = Up * (Side ? 4.0f : -4.0f) * RigScale;
			const FVector A = ForearmCenter + Offset;
			const FVector B = Shield->GetComponentLocation() + Offset;
			const FVector Span = B - A;
			if (UStaticMeshComponent* Rail = Part(Shield,
				Side ? TEXT("NativeShieldGuardRailUpper") : TEXT("NativeShieldGuardRailLower"), Metal))
			{
				Rail->SetWorldTransform(FTransform(FRotationMatrix::MakeFromX(Span.GetSafeNormal()).ToQuat(),
					(A + B) * 0.5f, FVector(FMath::Max(Span.Size(), 1.0), 2.5f * RigScale, 2.5f * RigScale) / 100.0f));
				Rail->SetVisibility(true);
			}
		}
	}

	inline bool IsBrokenAppearanceVisible(const UStaticMeshComponent* Shield)
	{
		if (!Shield) return false;
		for (const USceneComponent* Child : Shield->GetAttachChildren())
			if (Child->GetName().StartsWith(TEXT("NativeShieldBroken")) && Child->IsVisible()) return true;
		return false;
	}

	/** Cosmetic wreckage only: authority still owns the shield's existing broken state. */
	inline void UpdateBrokenAppearance(UStaticMeshComponent* Shield, const bool bBroken)
	{
		if (!Shield) return;
		const bool bShowBroken = bBroken && Shield->GetStaticMesh();
		Shield->SetVisibility(!bBroken && Shield->GetStaticMesh(), false);
		for (USceneComponent* Child : Shield->GetAttachChildren())
		{
			if (Child->GetName().StartsWith(TEXT("NativeShieldBroken"))) Child->SetVisibility(bShowBroken, true);
			if ((bBroken || !Shield->GetStaticMesh()) && Child->GetName().StartsWith(TEXT("NativeShieldGuard"))) Child->SetVisibility(false, true);
		}
		if (!bShowBroken) return;
		FLinearColor Paint(0.10f, 0.12f, 0.11f);
		for (int32 Slot = 0; Slot < Shield->GetNumMaterials(); ++Slot)
			if (const UMaterialInterface* Material = Shield->GetMaterial(Slot))
			{
				FLinearColor Tint;
				if (Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("MSTeamPrimary")), Tint))
				{
					Paint = Tint * 0.60f;
					break;
				}
			}
		const FVector Size = Shield->GetStaticMesh()->GetBoundingBox().GetSize();
		const float Height = Size.X, Width = Size.Y;
		const float PlateZ = Shield->GetStaticMesh()->GetBoundingBox().Max.Z * 0.55f;
		auto Fragment = [Shield](const FName Name, const FVector& Center, const FVector& Dimensions,
			const float Twist, const FLinearColor& Color)
		{
			if (UStaticMeshComponent* Piece = Part(Shield, Name, Color))
			{
				Piece->SetRelativeTransform(FTransform(FRotator(0, Twist, 0), Center, Dimensions / 100.0f));
				Piece->SetVisibility(true);
			}
		};
		// Uneven overlapping shards preserve the mounting stump while most of the
		// plate is visibly missing. Dark fractured edges and exposed rear ribs make
		// this distinct from an intact shield that was merely recolored or shrunk.
		Fragment(TEXT("NativeShieldBrokenPlate"), {-Height*.12f, 0, PlateZ},
			{Height*.30f, Width*.66f, 16}, -6, Paint);
		Fragment(TEXT("NativeShieldBrokenShardLeft"), {Height*.045f, -Width*.23f, PlateZ},
			{Height*.20f, Width*.20f, 14}, 19, Paint*.70f);
		Fragment(TEXT("NativeShieldBrokenShardRight"), {-Height*.14f, Width*.29f, PlateZ},
			{Height*.22f, Width*.17f, 18}, -23, Paint*.80f);
		Fragment(TEXT("NativeShieldBrokenSpine"), {-Height*.09f, 0, 12},
			{Height*.46f, 24, 28}, 0, FLinearColor(.045f,.05f,.055f));
		Fragment(TEXT("NativeShieldBrokenRib"), {-Height*.14f, 0, 20},
			{22, Width*.80f, 24}, 0, FLinearColor(.045f,.05f,.055f));
		Fragment(TEXT("NativeShieldBrokenScorch"), {Height*.03f, -Width*.025f, PlateZ+10},
			{25, Width*.45f, 6}, -12, FLinearColor(.008f,.006f,.005f));
	}

	inline void ApplyTeamColor(UStaticMeshComponent* Shield, const FLinearColor& Color, const bool bTint)
	{
		if (!Shield) return;
		for (int32 Slot = 0; Slot < Shield->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Shield->GetMaterial(Slot);
			float Enabled = 0.0f;
			if (!Material || !Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(TEXT("MSShieldTeamTint")), Enabled)) continue;
			UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
			if (!Mid) Mid = Shield->CreateAndSetMaterialInstanceDynamic(Slot);
			if (!Mid) continue;
			Mid->SetScalarParameterValue(TEXT("MSShieldTeamTint"), bTint ? 1.0f : 0.0f);
			Mid->SetVectorParameterValue(TEXT("MSTeamPrimary"), Color);
		}
	}
	inline void Attach(UStaticMeshComponent* Shield, USceneComponent* Forearm,
		UPoseableMeshComponent* SuitMesh, const FTransform& RigFrame,
		const FTransform& Skin, const float GuardAlpha = 0.0f, const float UpperBodyYaw = 0.0f)
	{
		if (!Shield || !Forearm) return;
		const FName Bone(TEXT("forearm_l"));
		const bool bBone = SuitMesh && SuitMesh->IsVisible() && SuitMesh->GetBoneIndex(Bone) != INDEX_NONE;
		const FVector Anchor = bBone ? SuitMesh->GetBoneLocation(Bone, EBoneSpaces::WorldSpace)
			: Forearm->GetComponentLocation();
		const bool bRight = RigFrame.InverseTransformPosition(Anchor).Y > 0.0f;
		const FQuat RestAxes = FRotationMatrix::MakeFromZX(
			FVector(0, bRight ? 1 : -1, 0), FVector::UpVector).ToQuat();
		FTransform World = FTransform(RestAxes * Skin.GetRotation(),
			FVector(0, 0, -16) + Skin.GetLocation(), Skin.GetScale3D()) * Forearm->GetComponentTransform();
		World.SetLocation(Anchor + Forearm->GetComponentTransform().TransformVector(
			FVector(0, 0, -16) + Skin.GetLocation()));
		// The clamp follows the bent forearm, while the grip rotates the broad shield face
		// across the chest. Authored +X stays up and +Z faces the threat, not the sky.
		const FVector Up = RigFrame.GetRotation().GetUpVector();
		FVector Forward = (RigFrame.GetRotation() * FRotator(0, UpperBodyYaw, 0).Quaternion()).GetForwardVector();
		if (const AActor* Owner = Forearm->GetOwner())
		{
			if (const UMSMechRigComponent* Rig = Owner->FindComponentByClass<UMSMechRigComponent>())
			{
				// Use the pose that was actually applied to the torso. Reconstructing this
				// from solver yaw can oppose the visible torso during turn/pose updates.
				// Torso already includes actor and rig facing: do not compose them twice.
				if (const USceneComponent* Torso = Rig->GetJoint(TEXT("Torso")))
					Forward = Torso->GetForwardVector();
			}
		}
		Forward = FVector::VectorPlaneProject(Forward, Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = RigFrame.GetRotation().GetForwardVector();
		const FQuat Guard = FRotationMatrix::MakeFromZX(Forward, Up).ToQuat() * Skin.GetRotation();
		World.SetRotation(FQuat::Slerp(World.GetRotation(), Guard,
			FMath::Clamp(GuardAlpha, 0.0f, 1.0f)).GetNormalized());
		if (GuardAlpha > 0.0f)
		{
			// Rotating a forearm-mounted plate upright must also move its grip in
			// front of the bent arm. Otherwise the unchanged midpoint mount leaves
			// the fingers sticking through the painted front. Find the smallest
			// outward standoff from the actual visible hand OBB and forearm envelope.
			const FVector Normal = World.GetRotation().GetUpVector();
			float ArmFront = -TNumericLimits<float>::Max();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector ArmLocal((Corner & 1) ? 10.0f : -10.0f,
					(Corner & 2) ? 10.0f : -10.0f, (Corner & 4) ? 0.0f : -32.0f);
				ArmFront = FMath::Max(ArmFront, float(FVector::DotProduct(Normal,
					Anchor + Forearm->GetComponentTransform().TransformVector(ArmLocal))));
			}
			if (SuitMesh)
			{
				for (USceneComponent* Child : SuitMesh->GetAttachChildren())
				{
					const UStaticMeshComponent* Hand = Cast<UStaticMeshComponent>(Child);
					if (!Hand || !Hand->IsVisible() || !Hand->GetStaticMesh()
						|| !Hand->GetName().StartsWith(TEXT("MobHand_L_"))) continue;
					const FBox Bounds = Hand->GetStaticMesh()->GetBoundingBox();
					for (int32 Corner = 0; Corner < 8; ++Corner)
					{
						const FVector Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
							(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
							(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
						ArmFront = FMath::Max(ArmFront, float(FVector::DotProduct(Normal,
							Hand->GetComponentTransform().TransformPosition(Point))));
					}
				}
			}
			// ShieldMount (local origin) is behind the painted plate on all three
			// authored shields. Keep the glove behind that plane, with a small gap.
			const float Clearance = 1.0f * Forearm->GetComponentScale().GetAbsMax();
			const float Standoff = FMath::Max(0.0f, ArmFront + Clearance
				- float(FVector::DotProduct(Normal, World.GetLocation())));
			World.AddToTranslation(Normal * Standoff * FMath::Clamp(GuardAlpha, 0.0f, 1.0f));
		}
		Shield->SetAbsolute(false, false, true);
		Shield->AttachToComponent(bBone ? static_cast<USceneComponent*>(SuitMesh) : Forearm,
			FAttachmentTransformRules::KeepWorldTransform, bBone ? Bone : NAME_None);
		Shield->SetWorldTransform(World);
		SetGuardBrace(Shield, Anchor + Forearm->GetComponentTransform().TransformVector(FVector(0, 0, -16)),
			Forearm->GetComponentQuat(), Up, Forearm->GetComponentScale().GetAbsMax(), GuardAlpha > 0.01f);
	}
}
