// Copyright NeoMSGO Project. All Rights Reserved.
//
// MobLight (2026-09-24): the eight Cube-only suits use one light 22-bone humanoid. NullRHI
// logic checks of the saved assets and the runtime contract (height, ground contact, facing,
// hand mounts, team/role tint). They do not replace a rendered PIE check.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MSBotCharacter.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobLightSuitVisualTest,
	"NeoMSGO.Animation.MobLight.SuitVisual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobLightTeamTintTest,
	"NeoMSGO.Animation.MobLight.TeamAndRoleTint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	const TCHAR* const MobLightMeshPath = TEXT("/Game/Meshes/MobileSuits/MobLightHands/SK_MobLightHands.SK_MobLightHands");
	const TCHAR* const MobSuits[] = {TEXT("DA_MS_StrikerMk1"), TEXT("DA_MS_AssaultCommander"),
		TEXT("DA_MS_BreakerAssault"), TEXT("DA_MS_DreadnoughtHeavy"), TEXT("DA_MS_FieldKeeperSupport"),
		TEXT("DA_MS_GuardianDefender"), TEXT("DA_MS_RaptorRecon"), TEXT("DA_MS_VanguardRX")};
	const TPair<const TCHAR*, const TCHAR*> ProtectedSuits[] = {
		{TEXT("DA_MS_TAUsw"), TEXT("SK_TAUsw")}, {TEXT("DA_MS_TAUrus"), TEXT("SK_TAUrus")},
		{TEXT("DA_MS_TAUrusAssault"), TEXT("SK_TAUrusAssault")}, {TEXT("DA_MS_MPU01_ThreeJS"), TEXT("SK_MPU01_ThreeJS")},
		{TEXT("DA_MS_MPU01_BlockPaint"), TEXT("SK_MPU01_BlockPaint")},
		{TEXT("DA_MS_MPU01_GeneratedMesh"), TEXT("SK_MPU01_BlockPaint")}};

	UMSMobileSuitData* LoadSuit(const TCHAR* Name)
	{
		return LoadObject<UMSMobileSuitData>(nullptr, *FString::Printf(TEXT("/Game/Data/MobileSuits/%s.%s"), Name, Name));
	}

	struct FMobTestWorld
	{
		UWorld* World = nullptr;
		FMobTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMobLightTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FMobTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		template<typename T>
		T* Spawn(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<T>(T::StaticClass(), Location, FRotator::ZeroRotator, Params);
		}
	};

	/** The tint MID whose parent is the named MobLight material (slot order is not assumed). */
	UMaterialInstanceDynamic* FindTint(const UMSMeshVisualComponent* Visual, const TCHAR* ParentName)
	{
		for (int32 Index = 0; Index < Visual->GetAppearanceMaterialCountForAutomationTests(); ++Index)
		{
			UMaterialInstanceDynamic* Mid = Visual->GetAppearanceMaterialForAutomationTests(Index);
			if (Mid && Mid->Parent && Mid->Parent->GetName() == ParentName) return Mid;
		}
		return nullptr;
	}

	FLinearColor Param(UMaterialInstanceDynamic* Mid, const FName Name)
	{
		return Mid ? Mid->K2_GetVectorParameterValue(Name) : FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);
	}

	FVector BoneAxisX(UPoseableMeshComponent* Mesh, const FName Bone)
	{
		return Mesh->GetBoneQuaternion(Bone, EBoneSpaces::WorldSpace).GetAxisX();
	}
}

bool FMSMobLightSuitVisualTest::RunTest(const FString&)
{
	USkeletalMesh* MobLight = LoadObject<USkeletalMesh>(nullptr, MobLightMeshPath);
	if (!TestNotNull(TEXT("SK_MobLight exists"), MobLight)) return false;
	const FReferenceSkeleton& Ref = MobLight->GetRefSkeleton();
	const TCHAR* const Bones[] = {TEXT("root"), TEXT("pelvis"), TEXT("chest"), TEXT("head"), TEXT("shoulder_l"),
		TEXT("upperarm_l"), TEXT("forearm_l"), TEXT("weapon_mount_l"), TEXT("shoulder_r"), TEXT("upperarm_r"),
		TEXT("forearm_r"), TEXT("weapon_mount_r"), TEXT("thruster_back_l"), TEXT("thruster_back_r"), TEXT("thigh_l"),
		TEXT("shin_l"), TEXT("foot_l"), TEXT("thruster_foot_l"), TEXT("thigh_r"), TEXT("shin_r"), TEXT("foot_r"),
		TEXT("thruster_foot_r")};
	for (const TCHAR* Bone : Bones)
	{
		TestTrue(FString::Printf(TEXT("SK_MobLight has bone %s"), Bone), Ref.FindBoneIndex(Bone) != INDEX_NONE);
	}
	TestEqual(TEXT("22 contract bones plus the armature root"), Ref.GetNum(), 23);
	TestTrue(TEXT("weapon mounts follow the forearms"),
		Ref.GetBoneName(Ref.GetParentIndex(Ref.FindBoneIndex(TEXT("weapon_mount_r")))) == FName(TEXT("forearm_r"))
		&& Ref.GetBoneName(Ref.GetParentIndex(Ref.FindBoneIndex(TEXT("weapon_mount_l")))) == FName(TEXT("forearm_l")));
	TestTrue(TEXT("MobLight owns a new skeleton"), MobLight->GetSkeleton()
		&& MobLight->GetSkeleton()->GetPathName().StartsWith(TEXT("/Game/Meshes/MobileSuits/MobLightHands/")));
	TestEqual(TEXT("four material slots"), MobLight->GetMaterials().Num(), 4);

	FMobTestWorld TestWorld;
	int32 Checked = 0;
	for (const TCHAR* SuitName : MobSuits)
	{
		const FString Label = SuitName;
		UMSMobileSuitData* Suit = LoadSuit(SuitName);
		AMSPlayerCharacter* Unit = TestWorld.Spawn<AMSPlayerCharacter>(FVector(1000.0f * Checked, 0.0f, 500.0f));
		if (!TestNotNull(*(Label + TEXT(" loads")), Suit) || !TestNotNull(*(Label + TEXT(" unit")), Unit)) continue;
		++Checked;
		TestTrue(*(Label + TEXT(" uses SK_MobLight at 1800cm")), Suit->VisualMesh.Get() == MobLight
			|| Suit->VisualMesh.LoadSynchronous() == MobLight);
		TestEqual(*(Label + TEXT(" target height")), Suit->VisualMeshTargetHeightCm, 1800.0f);
		Unit->InitializeUnit(Suit);
		UMSMeshVisualComponent* MeshVisual = Unit->GetMeshVisualComponent();
		UPoseableMeshComponent* Poseable = MeshVisual ? MeshVisual->GetPoseableMesh() : nullptr;
		if (!TestNotNull(*(Label + TEXT(" builds the poseable mesh")), Poseable)) continue;
		TestTrue(*(Label + TEXT(" shows MobLight")), Poseable->GetSkinnedAsset() == MobLight && Poseable->IsVisible());
		MeshVisual->TransferPoseForAutomationTests();
		if (Label == TEXT("DA_MS_StrikerMk1"))
		{
			// Exercise the shipped profile through InitializeUnit, not a hand-built mesh fixture.
			TestTrue(TEXT("Striker loads the SK_MobLightHands crouch contact profile"),
				MeshVisual->HasCrouchContactProfile());
			TestTrue(TEXT("Striker crouch contacts resolve to a finite world height"),
				FMath::IsFinite(MeshVisual->GetCrouchContactMinWorldZ()));
		}

		const FBoxSphereBounds Bounds = MobLight->GetImportedBounds();
		const float WorldHeight = Bounds.BoxExtent.Z * 2.0f * Poseable->GetComponentScale().Z;
		TestTrue(*(Label + TEXT(" is 1800cm tall in the world")), FMath::IsNearlyEqual(WorldHeight, 1800.0f, 1.0f));
		const float SoleZ = Poseable->GetComponentLocation().Z
			+ (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Poseable->GetComponentScale().Z;
		const float CapsuleBottom = Unit->GetActorLocation().Z - Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		TestTrue(*(Label + FString::Printf(TEXT(" soles touch the capsule bottom (%.1f vs %.1f)"), SoleZ, CapsuleBottom)),
			FMath::IsNearlyEqual(SoleZ, CapsuleBottom, 2.0f));

		// Facing: back thrusters exhaust behind the suit, foot thrusters point down (bone local +X).
		const FVector Forward = Unit->GetActorForwardVector();
		TestTrue(*(Label + TEXT(" back thrusters exhaust backwards (yaw offset is right)")),
			FVector::DotProduct(BoneAxisX(Poseable, TEXT("thruster_back_l")), Forward) < -0.9f
			&& FVector::DotProduct(BoneAxisX(Poseable, TEXT("thruster_back_r")), Forward) < -0.9f);
		TestTrue(*(Label + TEXT(" foot thrusters exhaust downwards")),
			BoneAxisX(Poseable, TEXT("thruster_foot_l")).Z < -0.9f && BoneAxisX(Poseable, TEXT("thruster_foot_r")).Z < -0.9f);
		const FVector HeadLocal = Unit->GetActorTransform().InverseTransformPosition(
			Poseable->GetBoneLocation(TEXT("head"), EBoneSpaces::WorldSpace));
		const FVector LeftHandLocal = Unit->GetActorTransform().InverseTransformPosition(
			Poseable->GetBoneLocation(TEXT("weapon_mount_l"), EBoneSpaces::WorldSpace));
		const FVector RightHandLocal = Unit->GetActorTransform().InverseTransformPosition(
			Poseable->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace));
		AddInfo(FString::Printf(TEXT("%s actor-local head %s, weapon_mount_l %s, weapon_mount_r %s"), *Label,
			*HeadLocal.ToString(), *LeftHandLocal.ToString(), *RightHandLocal.ToString()));
		TestTrue(*(Label + TEXT(" head is on top")), HeadLocal.Z > 400.0f);
		// Actor forward is +X: anatomical left is -Y, right is +Y.
		TestTrue(*(Label + TEXT(" weapon mounts are on the anatomical left and right")),
			LeftHandLocal.Y < -100.0f && RightHandLocal.Y > 100.0f
			&& FMath::IsNearlyEqual(LeftHandLocal.Y, -RightHandLocal.Y, 1.0f));
		// A one-sided pose must move the same anatomical arm, not its mirror.
		if (USceneComponent* LeftArm = Unit->GetMechRigComponent()->GetJoint(TEXT("UpperArm_L")))
		{
			const FRotator OriginalRotation = LeftArm->GetRelativeRotation();
			const FVector LeftBefore = Poseable->GetBoneLocation(TEXT("weapon_mount_l"), EBoneSpaces::WorldSpace);
			const FVector RightBefore = Poseable->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace);
			LeftArm->SetRelativeRotation(OriginalRotation + FRotator(35.0f, 0.0f, 0.0f));
			MeshVisual->TransferPoseForAutomationTests();
			TestTrue(*(Label + TEXT(" left arm pose moves the left hand only")),
				FVector::Dist(LeftBefore, Poseable->GetBoneLocation(TEXT("weapon_mount_l"), EBoneSpaces::WorldSpace)) > 20.0f
				&& FVector::Dist(RightBefore, Poseable->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace)) < 1.0f);
			LeftArm->SetRelativeRotation(OriginalRotation);
			MeshVisual->TransferPoseForAutomationTests();
		}

		// Right-hand weapon follows weapon_mount_r; the twin dagger also uses weapon_mount_l.
		UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
		UMSWeaponVisualComponent* WeaponVisual = Unit->GetWeaponVisualComponent();
		UMSWeaponDataBase* Twin = LoadObject<UMSWeaponDataBase>(nullptr,
			TEXT("/Game/Data/Weapons/DA_Weapon_TwinDaggerTD1.DA_Weapon_TwinDaggerTD1"));
		if (Weapons && WeaponVisual && Twin)
		{
			Weapons->InitializeSlots({Twin});
			WeaponVisual->RefreshFromWeaponComponent();
			UStaticMeshComponent* Right = WeaponVisual->GetImportedHeldMeshComponent();
			UStaticMeshComponent* Left = WeaponVisual->GetImportedLeftHeldMeshComponent();
			TestTrue(*(Label + TEXT(" held weapons attach to the MobLight hand bones")),
				Right && Left && Right->IsVisible() && Left->IsVisible()
				&& Right->GetAttachParent() == Poseable && Right->GetAttachSocketName() == FName(TEXT("weapon_mount_r"))
				&& Left->GetAttachParent() == Poseable && Left->GetAttachSocketName() == FName(TEXT("weapon_mount_l"))
				&& Right->GetComponentScale().Equals(FVector::OneVector, 0.001f));
		}
	}
	TestEqual(TEXT("eight Cube-only suits checked"), Checked, 8);

	// Protected suits keep their own meshes and receive no tint.
	for (const TPair<const TCHAR*, const TCHAR*>& Pair : ProtectedSuits)
	{
		UMSMobileSuitData* Suit = LoadSuit(Pair.Key);
		USkeletalMesh* Mesh = Suit ? Suit->VisualMesh.LoadSynchronous() : nullptr;
		TestTrue(*(FString(Pair.Key) + TEXT(" keeps its own mesh")), Mesh && Mesh->GetName() == Pair.Value);
	}
	AMSPlayerCharacter* TauUnit = TestWorld.Spawn<AMSPlayerCharacter>(FVector(0.0f, 5000.0f, 500.0f));
	if (TauUnit)
	{
		TauUnit->InitializeUnit(LoadSuit(TEXT("DA_MS_TAUsw")));
		if (UPoseableMeshComponent* TauMesh = TauUnit->GetMeshVisualComponent()->GetPoseableMesh())
		{
			TauUnit->GetMeshVisualComponent()->TransferPoseForAutomationTests();
			const FTransform TauActor = TauUnit->GetActorTransform();
			const FVector TauLeft = TauActor.InverseTransformPosition(TauMesh->GetBoneLocation(TEXT("weapon_mount_l"), EBoneSpaces::WorldSpace));
			const FVector TauRight = TauActor.InverseTransformPosition(TauMesh->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace));
			AddInfo(FString::Printf(TEXT("TAUsw reference: weapon_mount_l %s, weapon_mount_r %s"), *TauLeft.ToString(), *TauRight.ToString()));
			const USceneComponent* GhostLeft = TauUnit->GetMechRigComponent()->GetJoint(TEXT("WeaponMount_L"));
			AddInfo(FString::Printf(TEXT("Cube ghost WeaponMount_L %s"),
				*TauActor.InverseTransformPosition(GhostLeft->GetComponentLocation()).ToString()));
			TestTrue(TEXT("TAUsw reference uses the same mirrored side convention"), TauLeft.Y > 100.0f && TauRight.Y < -100.0f);
		}
		TestEqual(TEXT("TAUsw materials are not tinted"),
			TauUnit->GetMeshVisualComponent()->GetAppearanceMaterialCountForAutomationTests(), 0);
	}

	// Diagnostic only: MPU01 (not a 22-bone TAUsw-contract mesh) side placement for the report.
	if (AMSPlayerCharacter* MpuUnit = TestWorld.Spawn<AMSPlayerCharacter>(FVector(0.0f, 9000.0f, 500.0f)))
	{
		MpuUnit->InitializeUnit(LoadSuit(TEXT("DA_MS_MPU01_ThreeJS")));
		UMSMeshVisualComponent* MpuVisual = MpuUnit->GetMeshVisualComponent();
		if (UPoseableMeshComponent* MpuMesh = MpuVisual ? MpuVisual->GetPoseableMesh() : nullptr)
		{
			MpuVisual->TransferPoseForAutomationTests();
			const FTransform MpuActor = MpuUnit->GetActorTransform();
			for (const TCHAR* Bone : {TEXT("weapon_mount_l"), TEXT("weapon_mount_r")})
			{
				if (MpuMesh->GetBoneIndex(Bone) != INDEX_NONE)
				{
					AddInfo(FString::Printf(TEXT("MPU01_ThreeJS %s %s"), Bone, *MpuActor.InverseTransformPosition(
						MpuMesh->GetBoneLocation(Bone, EBoneSpaces::WorldSpace)).ToString()));
				}
			}
		}
	}

	// The dynamic BOT default suit is the MobLight Striker.
	// BotMobileSuitData is protected; read the class default through reflection.
	const FSoftObjectProperty* BotSuitProperty = FindFProperty<FSoftObjectProperty>(
		UMSBotSpawnManagerComponent::StaticClass(), TEXT("BotMobileSuitData"));
	UMSMobileSuitData* BotSuit = BotSuitProperty ? Cast<UMSMobileSuitData>(BotSuitProperty->GetPropertyValue_InContainer(
		GetDefault<UMSBotSpawnManagerComponent>()).LoadSynchronous()) : nullptr;
	TestTrue(TEXT("dynamic BOT default suit is StrikerMk1 with MobLight"), BotSuit
		&& BotSuit->GetName() == TEXT("DA_MS_StrikerMk1") && BotSuit->VisualMesh.LoadSynchronous() == MobLight);
	return true;
}

bool FMSMobLightTeamTintTest::RunTest(const FString&)
{
	FMobTestWorld TestWorld;
	UMSMobileSuitData* Striker = LoadSuit(TEXT("DA_MS_StrikerMk1"));
	UMSMobileSuitData* Dreadnought = LoadSuit(TEXT("DA_MS_DreadnoughtHeavy"));
	AMSPlayerCharacter* Player = TestWorld.Spawn<AMSPlayerCharacter>(FVector(0.0f, 0.0f, 500.0f));
	if (!TestNotNull(TEXT("Striker"), Striker) || !TestNotNull(TEXT("Dreadnought"), Dreadnought)
		|| !TestNotNull(TEXT("player unit"), Player)) return false;
	Player->SetGenericTeamId(MSTeam::PlayerTeamId());
	Player->InitializeUnit(Striker);
	UMSMeshVisualComponent* Visual = Player->GetMeshVisualComponent();
	UMSMechRigComponent* Rig = Player->GetMechRigComponent();
	TestEqual(TEXT("armor_main, armor_light and sensor have independent instance parameters"),
		Visual->GetAppearanceMaterialCountForAutomationTests(), 3);
	TestNotNull(TEXT("armor_main tint exists"), FindTint(Visual, TEXT("M_MobLight_ArmorMain")));
	TestNotNull(TEXT("armor_light tint exists"), FindTint(Visual, TEXT("M_MobLight_ArmorLight")));
	const FLinearColor PlayerColor = Rig->GetPartColor(TEXT("Torso"));
	TestTrue(TEXT("main armor uses the player team color"), Param(FindTint(Visual, TEXT("M_MobLight_ArmorMain")),
		UMSMeshVisualComponent::TeamPrimaryParameter).Equals(PlayerColor, 0.001f));

	// Team changes after the mesh is built still reach the armor (bots set team after InitializeUnit).
	Player->SetGenericTeamId(MSTeam::EnemyTeamId());
	Visual->RefreshAppearanceForAutomationTests();
	const FLinearColor EnemyColor = Rig->GetPartColor(TEXT("Torso"));
	TestFalse(TEXT("enemy team color differs from the player color"), EnemyColor.Equals(PlayerColor, 0.05f));
	TestTrue(TEXT("main armor follows the enemy team color"), Param(FindTint(Visual, TEXT("M_MobLight_ArmorMain")),
		UMSMeshVisualComponent::TeamPrimaryParameter).Equals(EnemyColor, 0.001f));

	// Enemy BOT tiers keep distinct colors on the same model.
	TArray<FLinearColor> TierColors;
	for (const EMSEnemyTier Tier : {EMSEnemyTier::Weak, EMSEnemyTier::Medium, EMSEnemyTier::Strong})
	{
		AMSBotCharacter* Bot = TestWorld.Spawn<AMSBotCharacter>(FVector(2000.0f * (TierColors.Num() + 1), 0.0f, 500.0f));
		if (!TestNotNull(TEXT("enemy bot spawns"), Bot)) continue;
		Bot->SetEnemyTier(Tier);
		Bot->SetGenericTeamId(MSTeam::EnemyTeamId());
		Bot->InitializeUnit(Striker);
		UMSMeshVisualComponent* BotVisual = Bot->GetMeshVisualComponent();
		BotVisual->RefreshAppearanceForAutomationTests();
		TestTrue(TEXT("bot shows MobLight"), BotVisual->IsMeshVisualActive());
		const FLinearColor Color = Param(FindTint(BotVisual, TEXT("M_MobLight_ArmorMain")),
			UMSMeshVisualComponent::TeamPrimaryParameter);
		TestTrue(TEXT("bot armor uses its rig team/tier color"), Color.Equals(Bot->GetMechRigComponent()->GetPartColor(TEXT("Torso")), 0.001f));
		TierColors.Add(Color);
		TestTrue(TEXT("enemy sensors are pink without recoloring the allied shared material"),
			Param(FindTint(BotVisual, TEXT("M_MobLight_SensorEmissive")), UMSMeshVisualComponent::SensorColorParameter)
			.Equals(FLinearColor(1.0f, 0.025f, 0.22f), 0.001f));
	}
	TestTrue(TEXT("three enemy tiers stay distinguishable"), TierColors.Num() == 3
		&& !TierColors[0].Equals(TierColors[1], 0.05f) && !TierColors[1].Equals(TierColors[2], 0.05f)
		&& !TierColors[0].Equals(TierColors[2], 0.05f));

	// Role accent: Assault (Striker) and Heavy (Dreadnought) differ; it follows the hangar palette.
	AMSPlayerCharacter* Heavy = TestWorld.Spawn<AMSPlayerCharacter>(FVector(0.0f, 3000.0f, 500.0f));
	Heavy->InitializeUnit(Dreadnought);
	Heavy->GetMeshVisualComponent()->RefreshAppearanceForAutomationTests();
	TestTrue(TEXT("allied sensors retain original green"),
		Param(FindTint(Heavy->GetMeshVisualComponent(), TEXT("M_MobLight_SensorEmissive")), UMSMeshVisualComponent::SensorColorParameter)
		.Equals(FLinearColor(0.05f, 0.9f, 0.28f), 0.001f));
	const FLinearColor AssaultAccent = Visual->GetAppliedRoleAccentForAutomationTests();
	const FLinearColor HeavyAccent = Heavy->GetMeshVisualComponent()->GetAppliedRoleAccentForAutomationTests();
	TestTrue(TEXT("Assault and Heavy role accents differ"), !AssaultAccent.Equals(HeavyAccent, 0.05f));
	TestTrue(TEXT("role accent derives from the hangar role color"),
		HeavyAccent.Equals(UMSMeshVisualComponent::GetRoleAccentColor(Dreadnought), 0.001f));
	TestTrue(TEXT("armor_light carries the role accent"), Param(FindTint(Heavy->GetMeshVisualComponent(),
		TEXT("M_MobLight_ArmorLight")), UMSMeshVisualComponent::RoleAccentParameter).Equals(HeavyAccent, 0.001f));
	return true;
}

#endif
