// Copyright NeoMSGO Project. All Rights Reserved.

#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMechAnimatorComponent.h"

#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	struct FMSRigPartDefinition
	{
		FName Name;
		FName Parent;
		FVector JointLocation;
		FVector CubeOffset;
		FVector CubeDimensions;
		bool bHasCube;
	};

	// MobileSuitRig.prefab: Unity (x right,y up,z forward)m -> UE (X forward,Y right,Z up)cm.
	const FMSRigPartDefinition RigDefinitions[] = {
		// Unity prefab dimensions expanded at the joints. The original exact cubes left visible
		// 8-14 cm gaps after UE's hierarchical rotations; small overlaps read as connected armor.
		{TEXT("Head"), NAME_None, {0,0,82}, {0,0,0}, {35,35,35}, true},
		{TEXT("Torso"), NAME_None, {0,0,35}, {0,0,0}, {36,60,58}, true},
		{TEXT("Hip"), NAME_None, {0,0,-5}, {0,0,0}, {39,54,32}, true},
		{TEXT("Backpack"), NAME_None, {-22,0,35}, {0,0,0}, {18,39,49}, true},
		{TEXT("Shoulder_L"), NAME_None, {0,-35,55}, {0,0,0}, {24,24,24}, true},
		{TEXT("Shoulder_R"), NAME_None, {0,35,55}, {0,0,0}, {24,24,24}, true},
		{TEXT("UpperArm_L"), NAME_None, {0,-42,45}, {0,0,-18}, {20,20,34}, true},
		{TEXT("UpperArm_R"), NAME_None, {0,42,45}, {0,0,-18}, {20,20,34}, true},
		{TEXT("Forearm_L"), TEXT("UpperArm_L"), {0,0,-32}, {0,0,-16}, {19,19,30}, true},
		{TEXT("Forearm_R"), TEXT("UpperArm_R"), {0,0,-32}, {0,0,-16}, {19,19,30}, true},
		{TEXT("Thigh_L"), NAME_None, {0,-16,-18}, {0,0,-21}, {27,27,36}, true},
		{TEXT("Thigh_R"), NAME_None, {0,16,-18}, {0,0,-21}, {27,27,36}, true},
		{TEXT("Shin_L"), TEXT("Thigh_L"), {0,0,-37}, {0,0,-15}, {23,23,30}, true},
		{TEXT("Shin_R"), TEXT("Thigh_R"), {0,0,-37}, {0,0,-15}, {23,23,30}, true},
		{TEXT("Foot_L"), TEXT("Shin_L"), {0,0,-29}, {8,0,-3}, {43,27,13}, true},
		{TEXT("Foot_R"), TEXT("Shin_R"), {0,0,-29}, {8,0,-3}, {43,27,13}, true},
		// TAU-sw v3 dual wield: mirror the right-hand attachment hierarchy on the left.
		{TEXT("WeaponMount_L"), TEXT("Forearm_L"), {15,0,-28}, {0,0,0}, {0,0,0}, false},
		// Held props must inherit the right forearm pose. This local offset preserves the former world-space rest pose.
		{TEXT("WeaponMount"), TEXT("Forearm_R"), {15,0,-28}, {0,0,0}, {0,0,0}, false},
	};

	const FName ColorParameter(TEXT("Color"));
	constexpr float RigRootToSoleCm = 93.5f;
	constexpr TCHAR MotionFormat[] = TEXT("msgo-mech-motion/1");
}

UMSMechRigComponent::UMSMechRigComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ArmorMaterial = MaterialFinder.Object;
}

FVector UMSMechRigComponent::GetJointRestRelativeLocation(const FName PartName) const
{
	for (const FMSRigPartDefinition& Definition : RigDefinitions)
	{
		if (Definition.Name == PartName) return Definition.JointLocation;
	}
	return FVector::ZeroVector;
}

void UMSMechRigComponent::OnRegister()
{
	Super::OnRegister();
	BuildRig();
}

void UMSMechRigComponent::BuildRig()
{
	AActor* Owner = GetOwner();
	if (bRigBuilt || !Owner || !Owner->GetRootComponent() || !CubeMesh)
	{
		return;
	}

	RigRoot = NewObject<USceneComponent>(Owner, TEXT("MSMechRigRoot"));
	Owner->AddInstanceComponent(RigRoot);
	RigRoot->SetupAttachment(Owner->GetRootComponent());
	const ACharacter* Character = Cast<ACharacter>(Owner);
	const float CapsuleHalfHeight = Character && Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
		: 88.0f * MSGO::MSScale;
	// RigRoot自身のRelativeLocationは自己スケールの影響を受けないためactor空間で算出する。
	RigRootRestRelativeZ = RigRootToSoleCm * MSGO::MSScale - CapsuleHalfHeight;
	RigRoot->SetRelativeLocation(FVector(0.0f, 0.0f, RigRootRestRelativeZ));
	RigRoot->SetRelativeScale3D(FVector(MSGO::MSScale));
	RigRoot->RegisterComponent();

	// Create every pivot first so definition order does not constrain parenting.
	for (const FMSRigPartDefinition& Definition : RigDefinitions)
	{
		const FName JointObjectName(*FString::Printf(TEXT("%s_Joint"), *Definition.Name.ToString()));
		USceneComponent* Joint = NewObject<USceneComponent>(Owner, JointObjectName);
		Owner->AddInstanceComponent(Joint);
		JointMap.Add(Definition.Name, Joint);
		OwnedJoints.Add(Joint);
	}

	for (const FMSRigPartDefinition& Definition : RigDefinitions)
	{
		USceneComponent* Joint = JointMap.FindRef(Definition.Name);
		USceneComponent* Parent = Definition.Parent.IsNone() ? RigRoot.Get() : JointMap.FindRef(Definition.Parent);
		Joint->SetupAttachment(Parent);
		Joint->SetRelativeLocation(Definition.JointLocation);
		Joint->RegisterComponent();

		if (!Definition.bHasCube)
		{
			continue;
		}

		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, Definition.Name);
		Owner->AddInstanceComponent(Part);
		Part->SetupAttachment(Joint);
		Part->SetStaticMesh(CubeMesh);
		Part->SetRelativeLocation(Definition.CubeOffset);
		Part->SetRelativeScale3D(Definition.CubeDimensions / 100.0f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetMobility(EComponentMobility::Movable);
		Part->RegisterComponent();

		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ArmorMaterial, this);
		Part->SetMaterial(0, Material);
		DynamicMaterials.Add(Material);
		PartMap.Add(Definition.Name, Part);
		OwnedParts.Add(Part);
	}

	bRigBuilt = true;
	ApplyAppearance(MSTeam::TeamPlayer, false, 0);
}

bool UMSMechRigComponent::LoadMotionSetFromJson(const FString& JsonText, FString& OutError)
{
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Invalid JSON document");
		return false;
	}

	FString Format;
	if (!Root->TryGetStringField(TEXT("format"), Format) || Format != MotionFormat)
	{
		OutError = FString::Printf(TEXT("Unsupported motion format: %s"), *Format);
		return false;
	}

	double JsonFps = 0.0;
	if (!Root->TryGetNumberField(TEXT("fps"), JsonFps) || JsonFps <= 0.0)
	{
		OutError = TEXT("fps must be greater than zero");
		return false;
	}

	const TSharedPtr<FJsonObject>* MotionObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("motions"), MotionObject) || !MotionObject || !MotionObject->IsValid())
	{
		OutError = TEXT("motions object is missing");
		return false;
	}

	TMap<FName, FMSMechMotion> ParsedMotions;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& MotionPair : (*MotionObject)->Values)
	{
		const TSharedPtr<FJsonObject> MotionJson = MotionPair.Value.IsValid()
			? MotionPair.Value->AsObject() : nullptr;
		if (!MotionJson.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not a motion object"), *MotionPair.Key);
			return false;
		}

		FMSMechMotion Motion;
		if (!MotionJson->TryGetBoolField(TEXT("loop"), Motion.bLoop)
			|| !MotionJson->TryGetNumberField(TEXT("frameCount"), Motion.FrameCount)
			|| !MotionJson->TryGetNumberField(TEXT("durationSec"), Motion.DurationSeconds)
			|| Motion.FrameCount <= 0 || Motion.DurationSeconds <= 0.0f)
		{
			OutError = FString::Printf(TEXT("%s has invalid metadata"), *MotionPair.Key);
			return false;
		}

		const TSharedPtr<FJsonObject>* JointsObject = nullptr;
		if (!MotionJson->TryGetObjectField(TEXT("joints"), JointsObject)
			|| !JointsObject || !JointsObject->IsValid())
		{
			OutError = FString::Printf(TEXT("%s has no joints object"), *MotionPair.Key);
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& JointPair : (*JointsObject)->Values)
		{
			if (!JointMap.Contains(FName(*JointPair.Key)))
			{
				OutError = FString::Printf(TEXT("%s references unknown joint %s"),
					*MotionPair.Key, *JointPair.Key);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>& Frames = JointPair.Value->AsArray();
			if (Frames.Num() != Motion.FrameCount)
			{
				OutError = FString::Printf(TEXT("%s.%s has %d frames; expected %d"),
					*MotionPair.Key, *JointPair.Key, Frames.Num(), Motion.FrameCount);
				return false;
			}

			TArray<FQuat>& Quaternions = Motion.JointFrames.Add(FName(*JointPair.Key));
			Quaternions.Reserve(Frames.Num());
			for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
			{
				const TArray<TSharedPtr<FJsonValue>>& Values = Frames[FrameIndex]->AsArray();
				if (Values.Num() != 4)
				{
					OutError = FString::Printf(TEXT("%s.%s[%d] is not [w,x,y,z]"),
						*MotionPair.Key, *JointPair.Key, FrameIndex);
					return false;
				}
				FQuat Quat(
					Values[1]->AsNumber(), Values[2]->AsNumber(),
					Values[3]->AsNumber(), Values[0]->AsNumber());
				if (Quat.SizeSquared() <= UE_SMALL_NUMBER)
				{
					OutError = FString::Printf(TEXT("%s.%s[%d] is a zero quaternion"),
						*MotionPair.Key, *JointPair.Key, FrameIndex);
					return false;
				}
				Quat.Normalize();
				Quaternions.Add(Quat);
			}
		}

		ParsedMotions.Add(FName(*MotionPair.Key), MoveTemp(Motion));
	}

	if (ParsedMotions.IsEmpty())
	{
		OutError = TEXT("motions object is empty");
		return false;
	}

	StopMotion(false);
	Motions = MoveTemp(ParsedMotions);
	MotionFramesPerSecond = static_cast<float>(JsonFps);
	UE_LOG(LogTemp, Display, TEXT("[Task222] Loaded %d mech motions at %.1f fps"),
		Motions.Num(), MotionFramesPerSecond);
	return true;
}

bool UMSMechRigComponent::LoadMotionSetFromFile(const FString& FilePath)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("[Task222] Could not read motion JSON: %s"), *FilePath);
		return false;
	}
	FString Error;
	const bool bLoaded = LoadMotionSetFromJson(JsonText, Error);
	if (!bLoaded)
	{
		UE_LOG(LogTemp, Error, TEXT("[Task222] Motion JSON rejected (%s): %s"), *FilePath, *Error);
	}
	return bLoaded;
}

bool UMSMechRigComponent::LoadDefaultTAUswMotionSet()
{
	return LoadMotionSetFromFile(FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("Motions/TAUsw/TAUsw_HeatSword_Motions.json")));
}

TArray<FName> UMSMechRigComponent::GetAvailableMotionNames() const
{
	TArray<FName> Names;
	Motions.GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	return Names;
}

int32 UMSMechRigComponent::GetMotionFrameCount(const FName MotionName) const
{
	const FMSMechMotion* Motion = Motions.Find(MotionName);
	return Motion ? Motion->FrameCount : 0;
}

bool UMSMechRigComponent::PlayMotion(const FName MotionName, const bool bRestart)
{
	if (!Motions.Contains(MotionName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Task222] Unknown mech motion: %s"), *MotionName.ToString());
		return false;
	}
	if (bRestart || ActiveMotionName != MotionName)
	{
		MotionPlaybackSeconds = 0.0f;
	}
	ActiveMotionName = MotionName;
	bMotionPlaying = true;
	return EvaluateActiveMotion();
}

void UMSMechRigComponent::StopMotion(const bool bResetPose)
{
	if (bResetPose)
	{
		if (const FMSMechMotion* Motion = Motions.Find(ActiveMotionName))
		{
			ResetMotionJoints(*Motion);
		}
	}
	bMotionPlaying = false;
}

bool UMSMechRigComponent::SetMotionPlaybackPosition(const float TimeSeconds)
{
	const FMSMechMotion* Motion = Motions.Find(ActiveMotionName);
	if (!Motion)
	{
		return false;
	}
	MotionPlaybackSeconds = Motion->bLoop
		? FMath::Fmod(FMath::Max(0.0f, TimeSeconds), Motion->DurationSeconds)
		: FMath::Clamp(TimeSeconds, 0.0f, Motion->DurationSeconds);
	return EvaluateActiveMotion();
}

void UMSMechRigComponent::UpdateMotion(const float DeltaSeconds)
{
	FMSMechMotion* Motion = Motions.Find(ActiveMotionName);
	if (!bMotionPlaying || !Motion || DeltaSeconds < 0.0f)
	{
		return;
	}
	MotionPlaybackSeconds += DeltaSeconds;
	if (Motion->bLoop)
	{
		MotionPlaybackSeconds = FMath::Fmod(MotionPlaybackSeconds, Motion->DurationSeconds);
	}
	else if (MotionPlaybackSeconds >= Motion->DurationSeconds)
	{
		MotionPlaybackSeconds = Motion->DurationSeconds;
		bMotionPlaying = false;
	}
	EvaluateActiveMotion();
}

bool UMSMechRigComponent::EvaluateActiveMotion()
{
	const FMSMechMotion* Motion = Motions.Find(ActiveMotionName);
	if (!Motion || Motion->FrameCount <= 0)
	{
		return false;
	}

	const float FramePosition = MotionPlaybackSeconds * MotionFramesPerSecond;
	int32 FrameA = FMath::FloorToInt(FramePosition);
	float Alpha = FramePosition - static_cast<float>(FrameA);
	int32 FrameB = FrameA + 1;
	if (Motion->bLoop)
	{
		FrameA %= Motion->FrameCount;
		FrameB %= Motion->FrameCount;
	}
	else
	{
		FrameA = FMath::Clamp(FrameA, 0, Motion->FrameCount - 1);
		FrameB = FMath::Clamp(FrameB, 0, Motion->FrameCount - 1);
		if (FrameA == FrameB)
		{
			Alpha = 0.0f;
		}
	}

	if (UMSMechAnimatorComponent* Animator = GetOwner()->FindComponentByClass<UMSMechAnimatorComponent>())
	{
		Animator->ResetDirectionalFacingForAuthoredMotion();
	}
	for (const TPair<FName, TArray<FQuat>>& JointFrames : Motion->JointFrames)
	{
		USceneComponent* Joint = GetJoint(JointFrames.Key);
		if (Joint && JointFrames.Value.IsValidIndex(FrameA) && JointFrames.Value.IsValidIndex(FrameB))
		{
			const FQuat Interpolated = FQuat::Slerp(
				JointFrames.Value[FrameA], JointFrames.Value[FrameB], Alpha).GetNormalized();
			Joint->SetRelativeRotation(Interpolated);
		}
	}
	return true;
}

void UMSMechRigComponent::ResetMotionJoints(const FMSMechMotion& Motion)
{
	for (const TPair<FName, TArray<FQuat>>& JointFrames : Motion.JointFrames)
	{
		if (USceneComponent* Joint = GetJoint(JointFrames.Key))
		{
			Joint->SetRelativeRotation(FQuat::Identity);
		}
	}
}

USceneComponent* UMSMechRigComponent::GetJoint(const FName PartName) const
{
	return JointMap.FindRef(PartName);
}

UStaticMeshComponent* UMSMechRigComponent::GetPart(const FName PartName) const
{
	return PartMap.FindRef(PartName);
}

FTransform UMSMechRigComponent::GetJointTransform(const FName PartName) const
{
	const USceneComponent* Joint = GetJoint(PartName);
	return Joint ? Joint->GetComponentTransform() : FTransform::Identity;
}

bool UMSMechRigComponent::SetPartColor(const FName PartName, const FLinearColor Color)
{
	UStaticMeshComponent* Part = GetPart(PartName);
	if (!Part)
	{
		return false;
	}
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0));
	if (!Material)
	{
		Material = UMaterialInstanceDynamic::Create(ArmorMaterial, this);
		Part->SetMaterial(0, Material);
		DynamicMaterials.Add(Material);
	}
	Material->SetVectorParameterValue(ColorParameter, Color);
	PartColors.Add(PartName, Color);
	return true;
}

FLinearColor UMSMechRigComponent::GetPartColor(const FName PartName) const
{
	if (const FLinearColor* Color = PartColors.Find(PartName))
	{
		return *Color;
	}
	return FLinearColor::Transparent;
}

TArray<FName> UMSMechRigComponent::GetPartNames() const
{
	TArray<FName> Names;
	PartMap.GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	return Names;
}

void UMSMechRigComponent::SetRigGeometryVisible(const bool bVisible)
{
	bRigGeometryVisible = bVisible;
	for (const TObjectPtr<UStaticMeshComponent>& Part : OwnedParts)
	{
		if (Part)
		{
			Part->SetVisibility(bVisible);
		}
	}
	if (HeadDecoration)
	{
		HeadDecoration->SetVisibility(bVisible);
	}
}

void UMSMechRigComponent::ApplyAppearance(const uint8 InTeamId, const bool bIsBot, const int32 EnemyTierVariant)
{
	if (!bRigBuilt)
	{
		return;
	}

	FLinearColor Primary(0.94f, 0.96f, 1.0f);
	FLinearColor Accent(0.15f, 0.35f, 0.75f);
	FLinearColor Head = FLinearColor::White;
	const bool bEnemy = InTeamId == MSTeam::TeamEnemy;
	if (bEnemy)
	{
		switch (FMath::Clamp(EnemyTierVariant, 0, 2))
		{
		// Authored sRGB swatches: Zaku olive/sage, Gouf cobalt/navy, Char dusty coral/oxblood.
		// Keep two distinct armor colors instead of multiplying one hue everywhere.
		case 1: Primary = FLinearColor(FColor(63, 110, 168)); Accent = FLinearColor(FColor(31, 52, 89)); break;
		case 2: Primary = FLinearColor(FColor(180, 104, 98)); Accent = FLinearColor(FColor(98, 44, 50)); break;
		default: Primary = FLinearColor(FColor(110, 139, 100)); Accent = FLinearColor(FColor(49, 73, 48)); break;
		}
		Head = Primary;
	}
	else if (bIsBot)
	{
		Primary = FLinearColor(1.0f, 0.82f, 0.12f);
		Accent = FLinearColor(0.42f, 0.36f, 0.08f);
		Head = FLinearColor(0.60f, 0.55f, 0.18f);
	}

	for (const FName Name : GetPartNames())
	{
		SetPartColor(Name, (Name == TEXT("Head")) ? Head
			: (Name == TEXT("Backpack") || Name == TEXT("Foot_L") || Name == TEXT("Foot_R")) ? Accent : Primary);
	}

	RemoveHeadDecoration();
	if (bEnemy && bIsBot)
	{
		if (EnemyTierVariant >= 1)
		{
			AddHeadDecoration(TEXT("CommandHorn"), FVector(0, 0, 25), FRotator(-25, 0, 0), FVector(7, 7, 50), FLinearColor(0.85f, 0.78f, 0.55f));
		}
		else
		{
			AddHeadDecoration(TEXT("SensorSpike"), FVector(0, 0, 23), FRotator::ZeroRotator, FVector(25, 25, 55), FLinearColor(0.85f, 0.15f, 0.10f));
		}
	}
}

void UMSMechRigComponent::AddHeadDecoration(const FName DecorationName, const FVector& RelativeLocation,
	const FRotator& RelativeRotation, const FVector& CubeDimensionsCm, const FLinearColor& Color)
{
	AActor* Owner = GetOwner();
	USceneComponent* HeadJoint = GetJoint(TEXT("Head"));
	if (!Owner || !HeadJoint || !CubeMesh)
	{
		return;
	}
	HeadDecoration = NewObject<UStaticMeshComponent>(Owner, DecorationName);
	Owner->AddInstanceComponent(HeadDecoration);
	HeadDecoration->SetupAttachment(HeadJoint);
	HeadDecoration->SetStaticMesh(CubeMesh);
	HeadDecoration->SetRelativeLocation(RelativeLocation);
	HeadDecoration->SetRelativeRotation(RelativeRotation);
	HeadDecoration->SetRelativeScale3D(CubeDimensionsCm / 100.0f);
	HeadDecoration->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadDecoration->SetCanEverAffectNavigation(false);
	HeadDecoration->RegisterComponent();
	HeadDecoration->SetVisibility(bRigGeometryVisible);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ArmorMaterial, this);
	Material->SetVectorParameterValue(ColorParameter, Color);
	HeadDecoration->SetMaterial(0, Material);
	DynamicMaterials.Add(Material);
}

void UMSMechRigComponent::RemoveHeadDecoration()
{
	if (HeadDecoration)
	{
		HeadDecoration->DestroyComponent();
		HeadDecoration = nullptr;
	}
}

FName UMSMechRigComponent::GetHeadDecorationName() const
{
	return HeadDecoration ? HeadDecoration->GetFName() : NAME_None;
}
