// Copyright NeoMSGO Project. All Rights Reserved.
//
// TAURUS実メッシュ試験導入(docs/TAURUS_MeshTrial_Plan.md)の受け入れテスト。
// SK_TAUrusアセットが存在する環境でのみ実質検証し、無い場合はスキップ扱いで成功させる
// (メッシュはローカルFBX由来のためCI環境に無い可能性を考慮)。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSProceduralPoseSolver.h"
#include "Components/PoseableMeshComponent.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/MSPlayerCharacter.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualActivationTest,
	"NeoMSGO.Animation.MeshVisual.ActivationAndAutoFit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualNoMeshNoChangeTest,
	"NeoMSGO.Animation.MeshVisual.NoMeshMeansNoChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualPoseTransferTest,
	"NeoMSGO.Animation.MeshVisual.WalkPoseTransfersToBones",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualImportedMaterialsTest,
	"NeoMSGO.Animation.MeshVisual.ImportedMaterialsArePreserved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualMaterialFallbackTest,
	"NeoMSGO.Animation.MeshVisual.DefaultMaterialsUseTeamColorFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshThrusterDynamicBonesTest,
	"NeoMSGO.Animation.MeshVisual.Thrusters.DynamicBonePoints",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshThrusterFallbackTest,
	"NeoMSGO.Animation.MeshVisual.Thrusters.MeshWithoutBonesFallsBack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCubeThrusterLegacyTest,
	"NeoMSGO.Animation.MeshVisual.Thrusters.CubeRigKeepsLegacyPoints",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMeshVisualBotAppearanceDecorationTest,
	"NeoMSGO.Animation.MeshVisual.BotAppearanceReapplyKeepsDecorationHidden",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	constexpr const TCHAR* TaurusMeshPath =
		TEXT("/Game/Meshes/MobileSuits/TAUrus/SK_TAUrus.SK_TAUrus");

	struct FMSMeshVisualTestWorld
	{
		UWorld* World = nullptr;
		FMSMeshVisualTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMeshVisualTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FMSMeshVisualTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	AMSPlayerCharacter* SpawnMeshVisualTestUnit(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	UMSMobileSuitData* MakeTaurusData(USkeletalMesh* Mesh)
	{
		UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(GetTransientPackage());
		Data->VisualMesh = Mesh;
		Data->VisualMeshTargetHeightCm = 1800.0f;
		return Data;
	}

	USkeletalMesh* MakeTestMesh(const TArray<TPair<FName, FTransform>>& ThrusterBones)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
		{
			FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("test_root"), TEXT("test_root"), INDEX_NONE),
				FTransform::Identity, true);
			for (const TPair<FName, FTransform>& Thruster : ThrusterBones)
			{
				Modifier.Add(FMeshBoneInfo(Thruster.Key, Thruster.Key.ToString(), 0), Thruster.Value);
			}
		}
		// Modifier destruction rebuilds the finalized ref-pose arrays required by Skeleton merge.
		// SetSkinnedAssetAndUpdate/bone container setup asserts on a mesh with no Skeleton.
		// Runtime-constructed test meshes need one merged from the mesh's own RefSkeleton.
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
		Skeleton->MergeAllBonesToBoneTree(Mesh);
		Mesh->SetSkeleton(Skeleton);
		return Mesh;
	}

	void TestLegacyThrusterPoints(FAutomationTestBase& Test, AMSPlayerCharacter& Unit,
		UMSMechAnimatorComponent& Animator)
	{
		USceneComponent* Backpack = Unit.GetMechRigComponent()->GetJoint(TEXT("Backpack"));
		Test.TestNotNull(TEXT("legacy Backpack joint exists"), Backpack);
		if (!Backpack)
		{
			return;
		}
		// This headless test world never calls World->BeginPlay(), so the animator's own
		// BeginPlay (which populates ThrusterPointWorldTransforms via BuildThrusterVisuals)
		// never runs. EvaluatePose lazily triggers the same build on its first call.
		Animator.EvaluatePose(FMSProceduralPoseInputs(), 0.0f);
		Test.TestEqual(TEXT("legacy layout exposes two Backpack nozzles"),
			Animator.GetThrusterPointCountForAutomationTests(), 2);
		for (int32 Index = 0; Index < Animator.GetThrusterPointCountForAutomationTests(); ++Index)
		{
			const float SideY = Index == 0 ? -10.0f : 10.0f;
			const FVector ExpectedLocation = Backpack->GetComponentTransform().TransformPosition(
				FVector(-18.0f, SideY, -10.0f));
			const FTransform Actual = Animator.GetThrusterPointTransformForAutomationTests(Index);
			Test.TestTrue(FString::Printf(TEXT("legacy nozzle %d retains Backpack position"), Index),
				Actual.GetLocation().Equals(ExpectedLocation, KINDA_SMALL_NUMBER));
			Test.TestTrue(FString::Printf(TEXT("legacy nozzle %d exhaust points backward"), Index),
				Actual.GetUnitAxis(EAxis::X).Equals(-Unit.GetActorForwardVector(), KINDA_SMALL_NUMBER));
		}
	}

	UStaticMeshComponent* FindHeadDecoration(AMSPlayerCharacter& Unit)
	{
		const UMSMechRigComponent* Rig = Unit.GetMechRigComponent();
		if (!Rig)
		{
			return nullptr;
		}
		const FName DecorationName = Rig->GetHeadDecorationName();
		if (DecorationName.IsNone())
		{
			return nullptr;
		}
		TArray<UStaticMeshComponent*> StaticMeshes;
		Unit.GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* Component : StaticMeshes)
		{
			if (Component && Component->GetFName() == DecorationName)
			{
				return Component;
			}
		}
		return nullptr;
	}
}

bool FMSMeshVisualActivationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TaurusMeshPath);
	if (!Mesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping mesh visual test"));
		return true;
	}

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	TestNotNull(TEXT("unit spawns"), Unit);
	if (!Unit) return false;

	Unit->InitializeUnit(MakeTaurusData(Mesh));
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	TestNotNull(TEXT("mesh visual component exists"), Visual);
	if (!Visual) return false;
	TestTrue(TEXT("mesh visual becomes active with VisualMesh set"), Visual->IsMeshVisualActive());

	// 表示Cube16個+装飾が全て非表示
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	TestNotNull(TEXT("rig still exists (ghost rig)"), Rig);
	if (!Rig) return false;
	for (const FName& PartName : Rig->GetPartNames())
	{
		if (const UStaticMeshComponent* Part = Rig->GetPart(PartName))
		{
			TestFalse(FString::Printf(TEXT("cube part %s hidden"), *PartName.ToString()),
				Part->IsVisible());
		}
	}

	// 自動フィット: 親RigRootの倍率を含むワールドBounds高さが目標1800UU±5%
	UPoseableMeshComponent* Poseable = Visual->GetPoseableMesh();
	TestNotNull(TEXT("poseable mesh exists"), Poseable);
	if (!Poseable) return false;
	const float WorldHeight = Poseable->Bounds.BoxExtent.Z * 2.0f;
	TestTrue(FString::Printf(TEXT("auto-fit height %.1fUU within 1800UU +/-5%%"), WorldHeight),
		FMath::Abs(WorldHeight - 1800.0f) <= 1800.0f * 0.05f);

	// コリジョン無効
	TestEqual(TEXT("mesh has no collision"),
		Poseable->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// ゴースト関節は残っている(WeaponMount含む)
	TestNotNull(TEXT("ghost WeaponMount joint remains"), Rig->GetJoint(TEXT("WeaponMount")));
	return true;
}

bool FMSMeshVisualNoMeshNoChangeTest::RunTest(const FString& Parameters)
{
	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	TestNotNull(TEXT("unit spawns"), Unit);
	if (!Unit) return false;

	// VisualMesh未設定のデフォルト初期化(BeginPlay時のInitializeUnit済み)
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	TestNotNull(TEXT("mesh visual component exists"), Visual);
	if (!Visual) return false;
	TestFalse(TEXT("mesh visual stays inactive without VisualMesh"), Visual->IsMeshVisualActive());
	TestNull(TEXT("no poseable mesh is created"), Visual->GetPoseableMesh());

	// 表示Cubeは従来どおり可視
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	if (!Rig) return false;
	bool bAnyVisible = false;
	for (const FName& PartName : Rig->GetPartNames())
	{
		if (const UStaticMeshComponent* Part = Rig->GetPart(PartName))
		{
			bAnyVisible |= Part->IsVisible();
		}
	}
	TestTrue(TEXT("cube parts remain visible without VisualMesh"), bAnyVisible);
	return true;
}

bool FMSMeshVisualPoseTransferTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TaurusMeshPath);
	if (!Mesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping pose transfer test"));
		return true;
	}

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	Unit->InitializeUnit(MakeTaurusData(Mesh));
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	if (!Visual || !Animator || !Visual->IsMeshVisualActive()) return false;
	UPoseableMeshComponent* Poseable = Visual->GetPoseableMesh();

	// 歩行ポーズを数フレーム進めて転写: thigh_l / thigh_r が時間変化し、かつ逆位相であること
	// (2026-07-16更新: 肘・膝分割FBXにより leg_l/r は thigh_l/r + shin_l/r へ分離)
	FMSProceduralPoseInputs Inputs;
	Inputs.GroundSpeed = 600.0f;
	auto StepAndTransfer = [&](const float Seconds)
	{
		constexpr float Step = 1.0f / 30.0f;
		for (float T = 0.0f; T < Seconds; T += Step)
		{
			Animator->EvaluatePose(Inputs, Step);
			Visual->TransferPoseForAutomationTests();
		}
	};

	StepAndTransfer(0.25f);
	const FRotator LegL_A = Poseable->GetBoneRotationByName(TEXT("thigh_l"), EBoneSpaces::ComponentSpace);
	const FRotator LegR_A = Poseable->GetBoneRotationByName(TEXT("thigh_r"), EBoneSpaces::ComponentSpace);
	StepAndTransfer(0.25f);
	const FRotator LegL_B = Poseable->GetBoneRotationByName(TEXT("thigh_l"), EBoneSpaces::ComponentSpace);

	TestTrue(TEXT("thigh_l bone rotation changes over walk time"),
		!LegL_A.Equals(LegL_B, 0.5f));
	TestTrue(TEXT("thigh_l and thigh_r are out of phase while walking"),
		!LegL_A.Equals(LegR_A, 0.5f));

	// しゃがみでRigRootが降下する(メッシュはRigRootの子なので追従)
	const float RootZBefore = Unit->GetMechRigComponent()->GetRigRoot()->GetRelativeLocation().Z;
	FMSProceduralPoseInputs CrouchInputs;
	CrouchInputs.bIsCrouching = true;
	for (int32 I = 0; I < 60; ++I)
	{
		Animator->EvaluatePose(CrouchInputs, 1.0f / 30.0f);
		Visual->TransferPoseForAutomationTests();
	}
	const float RootZAfter = Unit->GetMechRigComponent()->GetRigRoot()->GetRelativeLocation().Z;
	TestTrue(TEXT("crouch lowers rig root (mesh follows as child)"), RootZAfter < RootZBefore - 5.0f);
	return true;
}

bool FMSMeshVisualImportedMaterialsTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TaurusMeshPath);
	if (!Mesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping imported material test"));
		return true;
	}

	const TArray<FSkeletalMaterial>& SourceMaterials = Mesh->GetMaterials();
	TestEqual(TEXT("TAURUS has one imported material per part"), SourceMaterials.Num(), 13);
	for (int32 SlotIndex = 0; SlotIndex < SourceMaterials.Num(); ++SlotIndex)
	{
		const UMaterialInterface* Material = SourceMaterials[SlotIndex].MaterialInterface;
		TestNotNull(FString::Printf(TEXT("slot %d has an imported material"), SlotIndex), Material);
		if (Material)
		{
			TestTrue(FString::Printf(TEXT("slot %d material is stored in TAURUS content"), SlotIndex),
				Material->GetPathName().StartsWith(TEXT("/Game/Meshes/MobileSuits/TAUrus/")));
		}
	}

	// コア転写ボーンの相対順序を固定しつつ、機体ごとの可変thruster_<slot>追加を許可する。
	// 2026-07-16更新: 肘・膝分割FBXへの再インポートにより、arm_l/r・leg_l/rはそれぞれ
	// upperarm/forearm・thigh/shinへ分離し、weapon_mount_rが追加された(docs/TAURUS_MeshTrial_Design.md参照)。
	const FName ExpectedBones[] = {
		TEXT("TAUrus_rig"), TEXT("root"), TEXT("pelvis"), TEXT("chest"), TEXT("head"),
		TEXT("shoulder_l"), TEXT("upperarm_l"), TEXT("forearm_l"),
		TEXT("shoulder_r"), TEXT("upperarm_r"), TEXT("forearm_r"), TEXT("weapon_mount_r"),
		TEXT("thigh_l"), TEXT("shin_l"), TEXT("foot_l"),
		TEXT("thigh_r"), TEXT("shin_r"), TEXT("foot_r")};
	constexpr int32 ExpectedBoneCount = static_cast<int32>(UE_ARRAY_COUNT(ExpectedBones));
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const TSet<FName> AllowedThrusterBones = {
		TEXT("thruster_foot_l"), TEXT("thruster_foot_r"),
		TEXT("thruster_back_l"), TEXT("thruster_back_r"),
		TEXT("thruster_shoulder_l"), TEXT("thruster_shoulder_r"),
		TEXT("thruster_hip_l"), TEXT("thruster_hip_r")};
	int32 CoreBoneIndex = 0;
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetRawBoneNum(); ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		if (CoreBoneIndex < ExpectedBoneCount && BoneName == ExpectedBones[CoreBoneIndex])
		{
			++CoreBoneIndex;
			continue;
		}
		TestTrue(FString::Printf(TEXT("additional bone %s follows thruster_<slot> convention"),
			*BoneName.ToString()), AllowedThrusterBones.Contains(BoneName));
	}
	TestEqual(TEXT("all core bones exist in their established relative order"),
		CoreBoneIndex, ExpectedBoneCount);

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	Unit->InitializeUnit(MakeTaurusData(Mesh));
	UPoseableMeshComponent* Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
	if (!Poseable) return false;
	for (int32 SlotIndex = 0; SlotIndex < SourceMaterials.Num(); ++SlotIndex)
	{
		TestEqual(FString::Printf(TEXT("slot %d is not overridden by a team-color MID"), SlotIndex),
			Poseable->GetMaterial(SlotIndex), SourceMaterials[SlotIndex].MaterialInterface.Get());
	}
	return true;
}

bool FMSMeshVisualMaterialFallbackTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* SourceMesh = LoadObject<USkeletalMesh>(nullptr, TaurusMeshPath);
	if (!SourceMesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping material fallback test"));
		return true;
	}

	// SK_TAUrusはテクスチャ付きになったため、複製メッシュの全スロットを既定マテリアルへ
	// 差し替えて、従来の単色MID動作を「マテリアル無し相当」の前提で検証する。
	USkeletalMesh* FallbackMesh = DuplicateObject<USkeletalMesh>(SourceMesh, GetTransientPackage());
	UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	for (FSkeletalMaterial& Slot : FallbackMesh->GetMaterials())
	{
		Slot.MaterialInterface = DefaultMaterial;
	}

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	Unit->InitializeUnit(MakeTaurusData(FallbackMesh));
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UPoseableMeshComponent* Poseable = Visual ? Visual->GetPoseableMesh() : nullptr;
	if (!Visual || !Poseable) return false;

	for (int32 SlotIndex = 0; SlotIndex < FallbackMesh->GetMaterials().Num(); ++SlotIndex)
	{
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Poseable->GetMaterial(SlotIndex));
		TestNotNull(FString::Printf(TEXT("slot %d receives fallback MID"), SlotIndex), Mid);
		if (Mid)
		{
			TestTrue(FString::Printf(TEXT("slot %d fallback uses AppliedColor"), SlotIndex),
				Mid->K2_GetVectorParameterValue(TEXT("Color")).Equals(
					Visual->GetAppliedColorForAutomationTests(), KINDA_SMALL_NUMBER));
		}
	}
	return true;
}

bool FMSMeshThrusterDynamicBonesTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FName, FTransform>> AuthoredThrusters = {
		{TEXT("thruster_foot_l"), FTransform(FRotator(0.0f, 20.0f, 0.0f), FVector(11.0f, -22.0f, 33.0f))},
		{TEXT("thruster_back_r"), FTransform(FRotator(15.0f, 70.0f, 5.0f), FVector(-44.0f, 55.0f, 66.0f))},
		{TEXT("thruster_hip_l"), FTransform(FRotator(-25.0f, 130.0f, 10.0f), FVector(77.0f, 88.0f, -99.0f))}};

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	Unit->InitializeUnit(MakeTaurusData(MakeTestMesh(AuthoredThrusters)));
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UPoseableMeshComponent* Poseable = Visual ? Visual->GetPoseableMesh() : nullptr;
	if (!Animator || !Poseable) return false;

	// This headless test world never calls World->BeginPlay(), so BuildThrusterVisuals (normally
	// triggered by the animator's own BeginPlay) hasn't run yet. EvaluatePose triggers it lazily.
	Animator->EvaluatePose(FMSProceduralPoseInputs(), 0.0f);

	TestTrue(TEXT("mesh thruster bones override the legacy Backpack layout"),
		Animator->UsesMeshThrusterBonesForAutomationTests());
	TestEqual(TEXT("detected nozzle count equals the authored thruster bone count"),
		Animator->GetThrusterPointCountForAutomationTests(), AuthoredThrusters.Num());
	TestEqual(TEXT("all detected names are retained"),
		Animator->GetThrusterBoneNamesForAutomationTests().Num(), AuthoredThrusters.Num());

	for (const TPair<FName, FTransform>& Authored : AuthoredThrusters)
	{
		const FTransform WorldTransform(Authored.Value.GetRotation(),
			Unit->GetActorTransform().TransformPosition(Authored.Value.GetLocation()));
		Poseable->SetBoneTransformByName(Authored.Key, WorldTransform, EBoneSpaces::WorldSpace);
	}
	Animator->UpdateThrusterEffectsAfterMeshPose(0.0f);
	for (int32 Index = 0; Index < AuthoredThrusters.Num(); ++Index)
	{
		const FTransform Actual = Animator->GetThrusterPointTransformForAutomationTests(Index);
		const FTransform BoneWorld = Poseable->GetBoneTransformByName(
			AuthoredThrusters[Index].Key, EBoneSpaces::WorldSpace);
		TestTrue(FString::Printf(TEXT("nozzle %s follows its world position"),
			*AuthoredThrusters[Index].Key.ToString()),
			Actual.GetLocation().Equals(BoneWorld.GetLocation(), KINDA_SMALL_NUMBER));
		TestTrue(FString::Printf(TEXT("nozzle %s uses bone local +X as exhaust direction"),
			*AuthoredThrusters[Index].Key.ToString()),
			Actual.GetUnitAxis(EAxis::X).Equals(BoneWorld.GetUnitAxis(EAxis::X), KINDA_SMALL_NUMBER));
	}
	return true;
}

bool FMSMeshThrusterFallbackTest::RunTest(const FString& Parameters)
{
	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	Unit->InitializeUnit(MakeTaurusData(MakeTestMesh({})));
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	if (!Animator) return false;
	TestFalse(TEXT("mesh without thruster_* bones selects the legacy layout"),
		Animator->UsesMeshThrusterBonesForAutomationTests());
	TestLegacyThrusterPoints(*this, *Unit, *Animator);
	return true;
}

bool FMSCubeThrusterLegacyTest::RunTest(const FString& Parameters)
{
	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	if (!Unit) return false;
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	if (!Animator) return false;
	TestFalse(TEXT("Cube rig does not use mesh thruster bones"),
		Animator->UsesMeshThrusterBonesForAutomationTests());
	TestLegacyThrusterPoints(*this, *Unit, *Animator);
	return true;
}

bool FMSMeshVisualBotAppearanceDecorationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TaurusMeshPath);
	if (!Mesh)
	{
		AddInfo(TEXT("SK_TAUrus not present in this environment - skipping decoration visibility test"));
		return true;
	}

	FMSMeshVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnMeshVisualTestUnit(TestWorld.World);
	TestNotNull(TEXT("unit spawns"), Unit);
	if (!Unit) return false;

	Unit->InitializeUnit(MakeTaurusData(Mesh));
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	TestNotNull(TEXT("mesh visual exists"), Visual);
	TestNotNull(TEXT("rig exists"), Rig);
	if (!Visual || !Rig) return false;
	TestTrue(TEXT("real mesh is active before bot appearance reapply"), Visual->IsMeshVisualActive());

	// ApplyAppearance removes and recreates the head decoration. Both passes
	// must inherit the hidden state established by the real mesh visual.
	Rig->ApplyAppearance(MSTeam::TeamEnemy, true, 0);
	TestEqual(TEXT("enemy bot receives SensorSpike"), Rig->GetHeadDecorationName(), FName(TEXT("SensorSpike")));
	UStaticMeshComponent* Decoration = FindHeadDecoration(*Unit);
	TestNotNull(TEXT("SensorSpike component can be found"), Decoration);
	if (!Decoration) return false;
	TestFalse(TEXT("SensorSpike stays hidden while real mesh is visible"), Decoration->IsVisible());

	Rig->ApplyAppearance(MSTeam::TeamEnemy, true, 0);
	Decoration = FindHeadDecoration(*Unit);
	TestNotNull(TEXT("recreated SensorSpike component can be found"), Decoration);
	if (!Decoration) return false;
	TestFalse(TEXT("recreated SensorSpike stays hidden while real mesh is visible"), Decoration->IsVisible());

	Visual->InitializeFromMobileSuit(nullptr);
	TestFalse(TEXT("real mesh is inactive after removal"), Visual->IsMeshVisualActive());
	Decoration = FindHeadDecoration(*Unit);
	TestNotNull(TEXT("SensorSpike remains after real mesh removal"), Decoration);
	if (!Decoration) return false;
	TestTrue(TEXT("SensorSpike becomes visible with the Cube rig"), Decoration->IsVisible());

	bool bAnyCubeVisible = false;
	for (const FName& PartName : Rig->GetPartNames())
	{
		if (const UStaticMeshComponent* Part = Rig->GetPart(PartName))
		{
			bAnyCubeVisible |= Part->IsVisible();
		}
	}
	TestTrue(TEXT("Cube rig parts become visible after real mesh removal"), bAnyCubeVisible);

	Rig->SetRigGeometryVisible(false);
	TestFalse(TEXT("explicit rig hide also hides SensorSpike"), Decoration->IsVisible());
	Rig->SetRigGeometryVisible(true);
	TestTrue(TEXT("explicit rig show restores SensorSpike"), Decoration->IsVisible());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
