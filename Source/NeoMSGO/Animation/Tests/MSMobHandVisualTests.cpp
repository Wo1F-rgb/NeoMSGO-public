// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMobHandVisuals.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/Image.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSScale.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "UObject/UObjectGlobals.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobHandPoseDecisionTest,
	"NeoMSGO.Animation.MobHandPoseDecision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobHandRuntimeAttachmentTest,
	"NeoMSGO.Animation.MobHandRuntimeAttachment",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobLightBmgArmFitTest,
	"NeoMSGO.Animation.MobHandBmgArmFit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobHandPreviewTest,
	"NeoMSGO.Animation.MobHandPreview",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobTriggerGunClearanceTest,
	"NeoMSGO.Animation.MobTriggerGunClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FHandTestWorld
	{
		UWorld* World = nullptr;
		FHandTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSMobHandTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FHandTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	template<typename T>
	T* MakeWeapon(UObject* Outer, const EMSWeaponMeshMount Mount = EMSWeaponMeshMount::Hand,
		const bool bBuiltIn = false)
	{
		T* Weapon = NewObject<T>(Outer);
		Weapon->WeaponMeshMount = Mount;
		Weapon->bIsBuiltIn = bBuiltIn;
		return Weapon;
	}

	FTransform ReferenceBoneTransform(const USkeletalMesh& Mesh, const FName BoneName)
	{
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		const int32 BoneIndex = Ref.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE) return FTransform::Identity;
		TArray<int32, TInlineAllocator<16>> Chain;
		for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index)) Chain.Add(Index);
		FTransform Result = FTransform::Identity;
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
			Result = Ref.GetRefBonePose()[Chain[Index]] * Result;
		return Result;
	}
}

bool FMSMobHandPoseDecisionTest::RunTest(const FString& Parameters)
{
	UMSRangedWeaponData* Gun = MakeWeapon<UMSRangedWeaponData>(GetTransientPackage());
	const FMSMobHandPosePair Free = UMSMobHandVisuals::ResolvePoseForWeapon(nullptr);
	TestEqual(TEXT("no active weapon leaves both hands open"), Free.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("no active weapon leaves right hand open"), Free.Right, EMSMobHandPose::Open);

	const FMSMobHandPosePair GunPose = UMSMobHandVisuals::ResolvePoseForWeapon(Gun);
	TestEqual(TEXT("single-hand gun leaves left hand open"), GunPose.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("single-hand gun uses trigger pose"), GunPose.Right, EMSMobHandPose::Trigger);

	UMSRangedWeaponData* DualGun = MakeWeapon<UMSRangedWeaponData>(GetTransientPackage(), EMSWeaponMeshMount::DualHands);
	const FMSMobHandPosePair DualPose = UMSMobHandVisuals::ResolvePoseForWeapon(DualGun);
	TestEqual(TEXT("dual-hand gun uses left grip"), DualPose.Left, EMSMobHandPose::Grip);
	TestEqual(TEXT("dual-hand gun uses right grip"), DualPose.Right, EMSMobHandPose::Grip);

	UMSMeleeWeaponData* Melee = MakeWeapon<UMSMeleeWeaponData>(GetTransientPackage());
	const FMSMobHandPosePair MeleePose = UMSMobHandVisuals::ResolvePoseForWeapon(Melee);
	TestEqual(TEXT("melee leaves left hand open"), MeleePose.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("melee uses right grip"), MeleePose.Right, EMSMobHandPose::Grip);

	UMSRangedWeaponData* Fist = MakeWeapon<UMSRangedWeaponData>(GetTransientPackage(), EMSWeaponMeshMount::Fist);
	const FMSMobHandPosePair FistPose = UMSMobHandVisuals::ResolvePoseForWeapon(Fist);
	TestEqual(TEXT("fist weapon leaves left hand open"), FistPose.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("fist weapon closes right hand"), FistPose.Right, EMSMobHandPose::Fist);

	UMSRangedWeaponData* BuiltIn = MakeWeapon<UMSRangedWeaponData>(GetTransientPackage(), EMSWeaponMeshMount::Hand, true);
	const FMSMobHandPosePair BuiltInPose = UMSMobHandVisuals::ResolvePoseForWeapon(BuiltIn);
	TestEqual(TEXT("built-in weapon keeps left hand relaxed"), BuiltInPose.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("built-in weapon keeps right hand relaxed"), BuiltInPose.Right, EMSMobHandPose::Open);

	UMSSupplyWeaponData* Supply = MakeWeapon<UMSSupplyWeaponData>(GetTransientPackage());
	const FMSMobHandPosePair SupplyPose = UMSMobHandVisuals::ResolvePoseForWeapon(Supply);
	TestEqual(TEXT("deployable supply leaves left hand open"), SupplyPose.Left, EMSMobHandPose::Open);
	TestEqual(TEXT("deployable supply uses right grip"), SupplyPose.Right, EMSMobHandPose::Grip);

	const TPair<const TCHAR*, const TCHAR*> Throwables[] = {
		{TEXT("DA_Weapon_Grenade"), TEXT("/Game/Data/Weapons/DA_Weapon_Grenade.DA_Weapon_Grenade")},
		{TEXT("DA_Weapon_SmokeGrenade"), TEXT("/Game/Data/Weapons/DA_Weapon_SmokeGrenade.DA_Weapon_SmokeGrenade")}};
	for (const auto& Throwable : Throwables)
	{
		UMSWeaponDataBase* Data = LoadObject<UMSWeaponDataBase>(nullptr, Throwable.Value);
		if (!TestNotNull(*FString::Printf(TEXT("%s actual data asset"), Throwable.Key), Data)) continue;
		const FMSMobHandPosePair Pose = UMSMobHandVisuals::ResolvePoseForWeapon(Data);
		TestEqual(*FString::Printf(TEXT("%s selects right grip"), Throwable.Key),
			Pose.Right, EMSMobHandPose::Grip);
	}
	return true;
}

bool FMSMobHandRuntimeAttachmentTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* MobLightHands = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/MobLightHands/SK_MobLightHands.SK_MobLightHands"));
	if (!TestNotNull(TEXT("SK_MobLightHands is imported"), MobLightHands)) return false;
	const TCHAR* const HandSides[] = {TEXT("L"), TEXT("R")};
	const TCHAR* const HandPoses[] = {TEXT("Open"), TEXT("Fist"), TEXT("Trigger"), TEXT("Grip")};
	for (const TCHAR* Side : HandSides)
	{
		for (const TCHAR* Pose : HandPoses)
		{
			const FString AssetName = FString::Printf(TEXT("SM_Hand_%s_%s"), Side, Pose);
			const FString AssetPath = FString::Printf(
				TEXT("/Game/Meshes/MobileSuits/MobLightHands/%s.%s"), *AssetName, *AssetName);
		TestNotNull(*FString::Printf(TEXT("%s actual static hand asset"), *AssetName),
				LoadObject<UStaticMesh>(nullptr, *AssetPath));
		}
	}
	TestTrue(TEXT("new MobLight body is the only hand-enabled body"),
		UMSMobHandVisuals::IsMobLightBody(MobLightHands));
	USkeletalMesh* LegacyMobLight = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/MobLight/SK_MobLight.SK_MobLight"));
	if (!TestNotNull(TEXT("legacy MobLight body is imported"), LegacyMobLight)) return false;
	TestFalse(TEXT("legacy MobLight body never receives static hand overlays"),
		UMSMobHandVisuals::IsMobLightBody(LegacyMobLight));

	FHandTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("test unit spawns"), Unit)) return false;
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Unit);
	Suit->VisualMesh = MobLightHands;
	Suit->VisualMeshTargetHeightCm = 1800.0f;
	Suit->VisualMeshYawOffsetDeg = -90.0f;
	Unit->InitializeUnit(Suit);
	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMobHandVisuals* Hands = Visual ? Visual->GetMobHandVisuals() : nullptr;
	if (!TestNotNull(TEXT("MobLight hand helper exists"), Hands)) return false;
	Hands->SetPoseOverride(EMSMobHandPose::Open, EMSMobHandPose::Open);
	Visual->TransferPoseForAutomationTests();
	TestEqual(TEXT("open preview pose renders both hands"), Hands->GetVisibleComponentCountForAutomationTests(), 2);
	const EMSMobHandPose PoseValues[] = {EMSMobHandPose::Open, EMSMobHandPose::Fist,
		EMSMobHandPose::Trigger, EMSMobHandPose::Grip};
	for (const EMSMobHandPose Pose : PoseValues)
	{
		Hands->SetPoseOverride(Pose, Pose);
		Visual->TransferPoseForAutomationTests();
		TestEqual(TEXT("every pose override renders exactly two hands"),
			Hands->GetVisibleComponentCountForAutomationTests(), 2);
		for (const bool bLeft : {true, false})
		{
			for (const EMSMobHandPose Candidate : PoseValues)
			{
				UStaticMeshComponent* Component =
					Hands->GetHandComponentForAutomationTests(bLeft, Candidate);
				TestNotNull(TEXT("every hand pose variant remains addressable"), Component);
				if (Component)
				{
					TestEqual(TEXT("only the selected hand pose is visible"),
						Component->IsVisible(), Candidate == Pose);
				}
			}
		}
	}
	Hands->ClearPoseOverride();

	UMSMobileSuitData* ProtectedMpu = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"));
	if (!TestNotNull(TEXT("protected MPU suit fixture exists"), ProtectedMpu)) return false;
	Unit->InitializeUnit(ProtectedMpu);
	TestFalse(TEXT("MPU mesh disables MobLight hand overlays"), Hands->IsUsingMobLightHands());
	TestEqual(TEXT("MPU mesh destroys every MobLight hand component"),
		Hands->GetVisibleComponentCountForAutomationTests(), 0);

	UMSMobileSuitData* LegacySuit = NewObject<UMSMobileSuitData>(Unit);
	LegacySuit->VisualMesh = LegacyMobLight;
	LegacySuit->VisualMeshTargetHeightCm = 1800.0f;
	LegacySuit->VisualMeshYawOffsetDeg = -90.0f;
	Unit->InitializeUnit(LegacySuit);
	TestFalse(TEXT("legacy MobLight mesh remains outside hand overlay gate"),
		Hands->IsUsingMobLightHands());
	TestEqual(TEXT("legacy MobLight destroys every hand component"),
		Hands->GetVisibleComponentCountForAutomationTests(), 0);

	Unit->InitializeUnit(Suit);
	Visual->TransferPoseForAutomationTests();
	TestTrue(TEXT("switching back recreates MobLight hand overlays"), Hands->IsUsingMobLightHands());
	TestEqual(TEXT("switching back restores two visible hands"),
		Hands->GetVisibleComponentCountForAutomationTests(), 2);

	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponDataBase* BattleRifle = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_BR.DA_Weapon_BR"));
	UMSWeaponDataBase* Gun = BattleRifle;
	UMSWeaponDataBase* DualGun = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_TwinDaggerTD1.DA_Weapon_TwinDaggerTD1"));
	UMSWeaponDataBase* Saber = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_HeatSwordHS1.DA_Weapon_HeatSwordHS1"));
	if (!TestNotNull(TEXT("actual rifle exists"), Gun)
		|| !TestNotNull(TEXT("actual twin daggers exist"), DualGun)
		|| !TestNotNull(TEXT("actual saber exists"), Saber)) return false;
	Weapons->InitializeSlots({Gun, DualGun, Saber});
	Visual->TransferPoseForAutomationTests();
	TestEqual(TEXT("slot change drives trigger pose"), Hands->GetCurrentPose().Right, EMSMobHandPose::Trigger);
	TestEqual(TEXT("single-hand slot leaves left hand open"), Hands->GetCurrentPose().Left, EMSMobHandPose::Open);
	TestEqual(TEXT("trigger pose renders exactly two hands"), Hands->GetVisibleComponentCountForAutomationTests(), 2);

	UPoseableMeshComponent* Poseable = Visual->GetPoseableMesh();
	UStaticMeshComponent* Trigger = Hands->GetHandComponentForAutomationTests(false, EMSMobHandPose::Trigger);
	if (!TestNotNull(TEXT("right trigger hand component exists"), Trigger)
		|| !TestNotNull(TEXT("poseable body exists"), Poseable)) return false;
	// The exported static hands use the same FBX-centimetre world coordinates as the
	// body. This weapon anchor must land on the live poseable mount, not the ghost
	// mount, after yaw and root scaling are applied.
	// Blender mount Y=-0.05m becomes UE Y=+5cm (FBX handedness conversion).
	const FVector TriggerAnchorSource(-395.0f, 5.0f, 745.0f);
	TestTrue(TEXT("authored grip anchor matches imported reference bone"),
		TriggerAnchorSource.Equals(ReferenceBoneTransform(*MobLightHands,
			TEXT("weapon_mount_r")).GetTranslation(), 0.1f));
	TestTrue(TEXT("trigger weapon anchor lands on live right weapon mount within 1cm"),
		Trigger->GetComponentTransform().TransformPosition(TriggerAnchorSource).Equals(
			Poseable->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace), 1.0f));

	Weapons->SwitchToSlot(1);
	Visual->TransferPoseForAutomationTests();
	TestEqual(TEXT("dual-hand slot drives left grip"), Hands->GetCurrentPose().Left, EMSMobHandPose::Grip);
	TestEqual(TEXT("dual-hand slot drives right grip"), Hands->GetCurrentPose().Right, EMSMobHandPose::Grip);
	TestEqual(TEXT("dual-hand slot renders exactly two hands"), Hands->GetVisibleComponentCountForAutomationTests(), 2);
	Weapons->SwitchToSlot(2);
	Visual->TransferPoseForAutomationTests();
	TestEqual(TEXT("saber slot drives right grip"), Hands->GetCurrentPose().Right, EMSMobHandPose::Grip);
	TestEqual(TEXT("saber slot leaves left hand open"), Hands->GetCurrentPose().Left, EMSMobHandPose::Open);
	Weapons->SwitchToSlot(0);
	Visual->TransferPoseForAutomationTests();
	TestEqual(TEXT("switching back restores right trigger"), Hands->GetCurrentPose().Right, EMSMobHandPose::Trigger);

	UMSWeaponVisualComponent* WeaponVisual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSWeaponDataBase* PN1 = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_PowerKnucklePN1.DA_Weapon_PowerKnucklePN1"));
	UMSWeaponDataBase* GK3 = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_GrappleKnuckleGK3.DA_Weapon_GrappleKnuckleGK3"));
	if (!TestNotNull(TEXT("actual PN1 data exists"), PN1)
		|| !TestNotNull(TEXT("actual GK3 data exists"), GK3)
		|| !TestNotNull(TEXT("weapon visual exists"), WeaponVisual)
		|| !TestNotNull(TEXT("MobLight rig exists"), Rig)) return false;

	// Exercise the measured fit after both an actor turn and a non-neutral upper-arm pose.
	// Keep the explicitly posed test arm from being replaced by idle locomotion
	// during the real world tick; MeshVisual and WeaponVisual still tick normally.
	Unit->SetActorTickEnabled(false);
	if (UMSMechAnimatorComponent* Animator = Unit->FindComponentByClass<UMSMechAnimatorComponent>())
	{
		Animator->SetComponentTickEnabled(false);
	}
	Unit->SetActorRotation(FRotator(0.0f, 37.0f, 0.0f));
	if (USceneComponent* UpperArm = Rig->GetJoint(TEXT("UpperArm_R")))
	{
		UpperArm->SetRelativeRotation(FRotator(19.0f, -23.0f, 11.0f));
	}
	for (UMSWeaponDataBase* Knuckle : {PN1, GK3})
	{
		const FString Label = Knuckle == PN1 ? TEXT("PN1") : TEXT("GK3");
		TestEqual(*(Label + TEXT(" uses fist mount")), Knuckle->WeaponMeshMount, EMSWeaponMeshMount::Fist);
		FTransform FistWorld;
		TestTrue(*(Label + TEXT(" resolves MobLight right fist fit")),
			Hands->TryGetFistWeaponWorldTransform(*Knuckle, false, FistWorld));
		FTransform Unused;
		TestFalse(*(Label + TEXT(" never resolves a left fist fit")),
			Hands->TryGetFistWeaponWorldTransform(*Knuckle, true, Unused));
		TestTrue(*(Label + TEXT(" fit transform is finite")), !FistWorld.ContainsNaN());

		const FTransform OriginalSkin = Knuckle->WeaponMeshTransform;
		const auto OriginalMesh = Knuckle->WeaponMesh;
		const auto OriginalMount = Knuckle->WeaponMeshMount;
		Weapons->InitializeSlots({Knuckle});
		Visual->TransferPoseForAutomationTests();
		WeaponVisual->RefreshFromWeaponComponent();
		Poseable = Visual->GetPoseableMesh();
		UStaticMeshComponent* Held = WeaponVisual->GetImportedHeldMeshComponent();
		UStaticMeshComponent* FistHand =
			Hands->GetHandComponentForAutomationTests(false, EMSMobHandPose::Fist);
		if (!TestNotNull(*(Label + TEXT(" imported held component")), Held)
			|| !TestNotNull(*(Label + TEXT(" right fist hand component")), FistHand)
			|| !TestNotNull(*(Label + TEXT(" poseable remains available")), Poseable)) return false;

		TestTrue(*(Label + TEXT(" held mesh is visible")), Held->IsVisible());
		TestTrue(*(Label + TEXT(" held mesh is the selected asset")), Held->GetStaticMesh() == Knuckle->WeaponMesh.Get());
		TestTrue(*(Label + TEXT(" attaches to live right forearm")),
			Held->GetAttachParent() == Poseable
			&& Held->GetAttachSocketName() == FName(TEXT("forearm_r")));
		TestTrue(*(Label + TEXT(" fit owns world location and rotation")),
			Held->IsUsingAbsoluteLocation() && Held->IsUsingAbsoluteRotation()
			&& Held->IsUsingAbsoluteScale());
		TestTrue(*(Label + TEXT(" fit uses the measured weapon scale once")),
			Held->GetComponentScale().Equals(FistWorld.GetScale3D()
				* (Knuckle->WeaponMeshTransform.GetScale3D() * MSGO::MSScale), 0.01f));

		// Compare against the hand's authored body frame rather than assuming the
		// imported forearm bone's local axes. This catches a source-axis mismatch
		// while remaining valid for the non-neutral arm pose above.
		const FVector ExpectedDorsal = FistHand->GetComponentTransform()
			.TransformVectorNoScale(FVector(-1.0f, 0.0f, 0.0f)).GetSafeNormal();
		const FVector ExpectedDistal = FistHand->GetComponentTransform()
			.TransformVectorNoScale(FVector(0.0f, 0.0f, -1.0f)).GetSafeNormal();
		TestTrue(*(Label + TEXT(" weapon +X faces the right dorsal side")),
			Held->GetForwardVector().Equals(ExpectedDorsal, 0.02f));
		TestTrue(*(Label + TEXT(" weapon +Z follows the forearm distal axis")),
			Held->GetUpVector().Equals(ExpectedDistal, 0.02f));
		// The authored hand anchor is the palm-side grip point. It should remain
		// close to the fitted weapon origin after the live pose and actor turn.
		const FVector FistPalmAnchor(-395.0f, 5.0f, 745.0f);
		TestTrue(*(Label + TEXT(" grip origin is near the fist palm")),
			(Held->GetComponentLocation()
				- FistHand->GetComponentTransform().TransformPosition(FistPalmAnchor)).Size() < 85.0f);

		// Re-evaluate the fit after another pose publication to catch a stale
		// world-absolute weapon transform in the update path.
		const FVector BeforeRefresh = Held->GetComponentLocation();
		if (USceneComponent* UpperArm = Rig->GetJoint(TEXT("UpperArm_R")))
		{
			UpperArm->SetRelativeRotation(Knuckle == PN1
				? FRotator(-14.0f, 31.0f, -17.0f)
				: FRotator(8.0f, -34.0f, 21.0f));
		}
		Visual->TransferPoseForAutomationTests();
		// The fit is world-absolute and must be re-evaluated by the scheduled
		// WeaponVisual tick after MeshVisual publishes this new pose.
		// Like Engine NetTestHelpers, advance the synthetic engine frame: tick tasks
		// execute at most once per GFrameCounter, even across multiple World::Ticks.
		++GFrameCounter;
		TestWorld.World->Tick(LEVELTICK_All, 0.016f);
		TestFalse(*(Label + TEXT(" fit follows a changed arm/actor pose")),
			BeforeRefresh.Equals(Held->GetComponentLocation(), 0.5f));
		TestTrue(*(Label + TEXT(" leaves authored weapon data unchanged")),
			Knuckle->WeaponMesh == OriginalMesh && Knuckle->WeaponMeshMount == OriginalMount
			&& Knuckle->WeaponMeshTransform.Equals(OriginalSkin, 0.0f));
	}

	UMSRangedWeaponData* Meshless = MakeWeapon<UMSRangedWeaponData>(Unit);
	Weapons->InitializeSlots({Meshless});
	WeaponVisual->RefreshFromWeaponComponent();
	UStaticMeshComponent* HiddenHeld = WeaponVisual->GetImportedHeldMeshComponent();
	TestTrue(TEXT("meshless weapon hides and clears fitted absolute pose"), HiddenHeld
		&& !HiddenHeld->IsVisible() && !HiddenHeld->IsUsingAbsoluteLocation()
		&& !HiddenHeld->IsUsingAbsoluteRotation() && !WeaponVisual->IsComponentTickEnabled());

	// A non-target BR immediately leaves the fitted forearm path and restores the
	// existing weapon_mount attachment contract.
	Weapons->InitializeSlots({Gun});
	Visual->TransferPoseForAutomationTests();
	WeaponVisual->RefreshFromWeaponComponent();
	UStaticMeshComponent* HeldAfterFist = WeaponVisual->GetImportedHeldMeshComponent();
	TestTrue(TEXT("BR remains visible after fist weapon"), HeldAfterFist && HeldAfterFist->IsVisible());
	FTransform NonFistTransform;
	TestFalse(TEXT("BR never enters the fitted fist resolver"),
		Hands->TryGetFistWeaponWorldTransform(*Gun, false, NonFistTransform));
	TestTrue(TEXT("BR returns to weapon_mount_r after fist weapon"), HeldAfterFist
		&& HeldAfterFist->GetAttachParent() == Poseable
		&& HeldAfterFist->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
	TestFalse(TEXT("BR clears fitted absolute location"), HeldAfterFist && HeldAfterFist->IsUsingAbsoluteLocation());
	TestFalse(TEXT("BR clears fitted absolute rotation"), HeldAfterFist && HeldAfterFist->IsUsingAbsoluteRotation());

	Hands->SetPoseOverride(EMSMobHandPose::Open, EMSMobHandPose::Open);
	Visual->TransferPoseForAutomationTests();
	UStaticMeshComponent* Open = Hands->GetHandComponentForAutomationTests(false, EMSMobHandPose::Open);
	if (!TestNotNull(TEXT("right open hand component exists"), Open)) return false;
	const FTransform RefForearm = ReferenceBoneTransform(*MobLightHands, TEXT("forearm_r"));
	const FTransform LiveForearm = Poseable->GetBoneTransformByName(TEXT("forearm_r"), EBoneSpaces::ComponentSpace);
	const FTransform OpenAssetToWorld = (RefForearm.Inverse() * LiveForearm)
		* Poseable->GetComponentTransform();
	const FVector WristSource(-395.0f, 0.0f, 798.0f);
	TestTrue(TEXT("open wrist anchor follows live forearm within 1cm"),
		Open->GetComponentTransform().TransformPosition(WristSource).Equals(
			OpenAssetToWorld.TransformPosition(WristSource), 1.0f));
	return true;
}

bool FMSMobLightBmgArmFitTest::RunTest(const FString& Parameters)
{
	const TCHAR* const MobLightBodyPath =
		TEXT("/Game/Meshes/MobileSuits/MobLightHands/SK_MobLightHands.SK_MobLightHands");
	const TCHAR* const BmgDataPath =
		TEXT("/Game/Data/Weapons/DA_Weapon_BeamMachineGun.DA_Weapon_BeamMachineGun");
	const TCHAR* const BmgMeshPath =
		TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/SM_BMG_BeamMachineGun.SM_BMG_BeamMachineGun");
	const TCHAR* const BrDataPath =
		TEXT("/Game/Data/Weapons/DA_Weapon_BR.DA_Weapon_BR");

	USkeletalMesh* MobLightBody = LoadObject<USkeletalMesh>(nullptr, MobLightBodyPath);
	UMSWeaponDataBase* Bmg = LoadObject<UMSWeaponDataBase>(nullptr, BmgDataPath);
	UMSWeaponDataBase* Br = LoadObject<UMSWeaponDataBase>(nullptr, BrDataPath);
	if (!TestNotNull(TEXT("BMG MobLight body exists"), MobLightBody)
		|| !TestNotNull(TEXT("BMG data exists"), Bmg)
		|| !TestNotNull(TEXT("BR switch target exists"), Br))
	{
		return false;
	}
	TestTrue(TEXT("BMG uses the exact fitted mesh"),
		Bmg->WeaponMesh.ToSoftObjectPath().ToString() == BmgMeshPath);
	TestEqual(TEXT("BMG fit is a right-hand mount"), Bmg->WeaponMeshMount, EMSWeaponMeshMount::Hand);

	FHandTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("BMG test unit exists"), Unit)) return false;
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Unit);
	Suit->VisualMesh = MobLightBody;
	Suit->VisualMeshTargetHeightCm = 1800.0f;
	Suit->VisualMeshYawOffsetDeg = -90.0f;
	Unit->InitializeUnit(Suit);

	UMSMeshVisualComponent* Visual = Unit->GetMeshVisualComponent();
	UMSMobHandVisuals* Hands = Visual ? Visual->GetMobHandVisuals() : nullptr;
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* WeaponVisual = Unit->GetWeaponVisualComponent();
	if (!TestNotNull(TEXT("BMG mesh visual exists"), Visual)
		|| !TestNotNull(TEXT("BMG hand helper exists"), Hands)
		|| !TestNotNull(TEXT("BMG animator exists"), Animator)
		|| !TestNotNull(TEXT("BMG rig exists"), Rig)
		|| !TestNotNull(TEXT("BMG weapon component exists"), Weapons)
		|| !TestNotNull(TEXT("BMG weapon visual exists"), WeaponVisual))
	{
		return false;
	}

	// Keep this deterministic test on the same direct EvaluatePose/TransferPose path
	// as the runtime animator, while avoiding an unrelated character tick changing the
	// active arm pose between assertions.
	Unit->SetActorTickEnabled(false);
	Animator->SetComponentTickEnabled(false);
	Unit->SetActorRotation(FRotator(8.0f, 37.0f, 0.0f));
	Weapons->InitializeSlots({Bmg, Br});
	Visual->TransferPoseForAutomationTests();
	WeaponVisual->RefreshFromWeaponComponent();
	UPoseableMeshComponent* Poseable = Visual->GetPoseableMesh();
	UStaticMeshComponent* Held = WeaponVisual->GetImportedHeldMeshComponent();
	USceneComponent* UpperArm = Rig->GetJoint(TEXT("UpperArm_R"));
	USceneComponent* Forearm = Rig->GetJoint(TEXT("Forearm_R"));
	USceneComponent* Mount = Rig->GetJoint(TEXT("WeaponMount"));
	UStaticMeshComponent* Trigger = Hands->GetHandComponentForAutomationTests(false, EMSMobHandPose::Trigger);
	if (!TestNotNull(TEXT("BMG poseable exists"), Poseable)
		|| !TestNotNull(TEXT("BMG held mesh exists"), Held)
		|| !TestNotNull(TEXT("BMG upper arm exists"), UpperArm)
		|| !TestNotNull(TEXT("BMG forearm exists"), Forearm)
		|| !TestNotNull(TEXT("BMG weapon mount exists"), Mount)
		|| !TestNotNull(TEXT("BMG trigger hand exists"), Trigger))
	{
		return false;
	}
	const FTransform OriginalSkin = Bmg->WeaponMeshTransform;
	const TSoftObjectPtr<UStaticMesh> OriginalMesh = Bmg->WeaponMesh;

	FMSProceduralPoseInputs Inputs;
	Inputs.bHasRangedWeapon = true;
	Inputs.bHasDirectionalFacing = true;
	Inputs.AimYaw = 0.0f;
	auto Publish = [&](const float DeltaSeconds)
	{
		Animator->EvaluatePose(Inputs, DeltaSeconds);
		Visual->TransferPoseForAutomationTests();
	};
	auto CheckHandAnchor = [&](const TCHAR* Label)
	{
		const FVector TriggerAnchorSource(-395.0f, 5.0f, 745.0f);
		TestTrue(FString::Printf(TEXT("%s trigger hand anchor follows live mount"), Label),
			Trigger->GetComponentTransform().TransformPosition(TriggerAnchorSource).Equals(
				Poseable->GetBoneLocation(TEXT("weapon_mount_r"), EBoneSpaces::WorldSpace), 1.0f));
	};
	auto CheckMuzzle = [&](const TCHAR* Label)
	{
		const FVector Muzzle = WeaponVisual->GetMuzzleWorldDirection(FVector::ZeroVector);
		TestTrue(FString::Printf(TEXT("%s BMG muzzle direction is valid"), Label), Muzzle.SizeSquared() > 0.99f);
		TestTrue(FString::Printf(TEXT("%s BMG muzzle follows the live mount"), Label),
			FVector::DotProduct(Muzzle, Mount->GetForwardVector()) > 0.98f);
		TestTrue(FString::Printf(TEXT("%s imported BMG muzzle follows its socket axis"), Label),
			FVector::DotProduct(Muzzle, Held->GetUpVector()) > 0.98f);
	};
	auto UpperLocalRotation = [&]()
	{
		return (FRotator(0.0f, -Animator->GetPoseState().UpperBodyYaw, 0.0f).Quaternion()
			* UpperArm->GetRelativeTransform().GetRotation()).Rotator();
	};

	Publish(1.0f);
	const FRotator RestUpper = UpperLocalRotation();
	const FRotator RestFore = Forearm->GetRelativeRotation();
	// A 100-degree elbow bend has an equivalent Euler decomposition with flipped
	// yaw/roll. Compare the intended orientation, not its serialization convention.
	const FQuat RestForeExpected = FRotator(100.f,0.f,-30.f).Quaternion();
	TestTrue(TEXT("BMG rest keeps the existing UE upper pitch 28"), FMath::IsNearlyEqual(RestUpper.Pitch, 28.0f, 0.75f));
	TestTrue(TEXT("BMG rest bends forearm under the stock"), RestFore.Quaternion().Equals(RestForeExpected,.01f));
	TestTrue(TEXT("BMG rest turns the upper arm roll outward"), FMath::IsNearlyEqual(RestUpper.Roll, 15.0f, 0.75f));
	TestTrue(TEXT("BMG rest turns the forearm roll inward"), RestFore.Quaternion().Equals(RestForeExpected,.01f));
	TestTrue(TEXT("BMG held mesh remains visible"), Held->IsVisible() && Held->GetStaticMesh() == Bmg->WeaponMesh.Get());
	CheckHandAnchor(TEXT("BMG rest"));
	CheckMuzzle(TEXT("BMG rest"));

	Inputs.bIsAiming = true;
	Inputs.AimYaw = 25.0f;
	Publish(1.0f);
	const FRotator AimUpper = UpperLocalRotation();
	const FRotator AimFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("BMG aim reaches UE upper pitch 50"), FMath::IsNearlyEqual(AimUpper.Pitch, 50.0f, 0.75f));
	TestTrue(TEXT("BMG aim bends forearm under the stock"), FMath::IsNearlyEqual(AimFore.Pitch, 85.0f, 0.75f));
	TestTrue(TEXT("BMG aim preserves upper roll"), FMath::IsNearlyEqual(AimUpper.Roll, 15.0f, 0.75f));
	TestTrue(TEXT("BMG aim preserves forearm roll"), FMath::IsNearlyEqual(AimFore.Roll, -30.0f, 0.75f));
	const FVector ExpectedMountForward = Unit->GetActorTransform().TransformVectorNoScale(
		FRotator(0.0f, 25.0f, 0.0f).RotateVector(FVector::ForwardVector)).GetSafeNormal();
	TestTrue(TEXT("BMG mount keeps the upper-body aim direction"),
		Mount->GetForwardVector().Equals(ExpectedMountForward, 0.02f));
	CheckHandAnchor(TEXT("BMG aim"));
	CheckMuzzle(TEXT("BMG aim"));

	// Other trigger-grip guns now share the stock-clearing elbow posture. Switching
	// between them must retain grip registration instead of returning to arm/stock overlap.
	Inputs.bIsAiming = false;
	Inputs.bHasDirectionalFacing = false;
	Inputs.AimYaw = 0.0f;
	Weapons->SwitchToSlot(1);
	Visual->TransferPoseForAutomationTests();
	WeaponVisual->RefreshFromWeaponComponent();
	for (int32 Frame = 0; Frame < 4; ++Frame) Publish(0.25f);
	const FRotator BrUpper = UpperLocalRotation();
	const FRotator BrFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("BR retains stock clearance at upper arm"), FMath::IsNearlyEqual(BrUpper.Roll,15.f,.75f));
	TestTrue(TEXT("BR retains stock clearance at elbow"), BrFore.Quaternion().Equals(RestForeExpected,.01f));
	TestTrue(TEXT("switching away leaves the other weapon visible"), Held->IsVisible() && Held->GetStaticMesh() == Br->WeaponMesh.Get());

	Weapons->SwitchToSlot(0);
	Visual->TransferPoseForAutomationTests();
	WeaponVisual->RefreshFromWeaponComponent();
	for (int32 Frame = 0; Frame < 4; ++Frame) Publish(0.25f);
	const FRotator ReturnUpper = UpperLocalRotation();
	const FRotator ReturnFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("switching back restores BMG upper roll"), FMath::IsNearlyEqual(ReturnUpper.Roll, 15.0f, 0.75f));
	TestTrue(TEXT("switching back restores BMG forearm roll"), ReturnFore.Quaternion().Equals(RestForeExpected,.01f));
	TestTrue(TEXT("switching back restores the exact BMG mesh"), Held->IsVisible() && Held->GetStaticMesh() == Bmg->WeaponMesh.Get());

	// Full-body states own the arm pose.  The BMG fit must leave the normal path
	// immediately, and must be rebuilt after each state clears rather than leaving
	// a stale sideways elbow behind.
	auto CheckFullBodyDisabled = [&](const TCHAR* Label, bool FMSProceduralPoseInputs::* StateFlag)
	{
		Inputs.bIsAiming = false;
		Inputs.bHasDirectionalFacing = false;
		Inputs.*StateFlag = true;
		Publish(1.0f);
		const FRotator DisabledUpper = UpperLocalRotation();
		const FRotator DisabledFore = Forearm->GetRelativeRotation();
		TestTrue(FString::Printf(TEXT("%s clears BMG upper roll"), Label), FMath::Abs(DisabledUpper.Roll) < 0.75f);
		TestTrue(FString::Printf(TEXT("%s clears BMG forearm roll"), Label), FMath::Abs(DisabledFore.Roll) < 0.75f);
		Inputs.*StateFlag = false;
		Publish(1.0f);
		const FRotator RestoredUpper = UpperLocalRotation();
		const FRotator RestoredFore = Forearm->GetRelativeRotation();
		TestTrue(FString::Printf(TEXT("%s restores BMG upper roll"), Label), FMath::IsNearlyEqual(RestoredUpper.Roll, 15.0f, 0.75f));
		TestTrue(FString::Printf(TEXT("%s restores BMG forearm pose"), Label), RestoredFore.Quaternion().Equals(RestForeExpected,.01f));
	};
	CheckFullBodyDisabled(TEXT("death"), &FMSProceduralPoseInputs::bIsDead);
	CheckFullBodyDisabled(TEXT("knockdown"), &FMSProceduralPoseInputs::bIsKnockedDown);
	CheckFullBodyDisabled(TEXT("stagger"), &FMSProceduralPoseInputs::bIsStaggered);

	// Authored melee motion owns the arm while active; once it ends the BMG pose must
	// return without retaining the motion's zero-roll arm state.
	Animator->StartMeleeMotion(EMSMeleeMotionType::Punch, 100, false);
	Publish(0.02f);
	const FRotator ActiveMotionUpper = UpperLocalRotation();
	const FRotator ActiveMotionFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("active authored motion clears BMG upper roll"),
		FMath::Abs(FRotator::NormalizeAxis(ActiveMotionUpper.Roll - 15.0f)) > 1.0f);
	TestTrue(TEXT("active authored motion clears BMG forearm roll"),
		!ActiveMotionFore.Quaternion().Equals(RestForeExpected,.01f));
	for (int32 Frame = 0; Frame < 20; ++Frame) Publish(0.02f);
	const FRotator MotionReturnUpper = UpperLocalRotation();
	const FRotator MotionReturnFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("BMG upper roll returns after authored motion"), FMath::IsNearlyEqual(MotionReturnUpper.Roll, 15.0f, 1.0f));
	TestTrue(TEXT("BMG forearm pose returns after authored motion"), MotionReturnFore.Quaternion().Equals(RestForeExpected,.015f));

	// The fitted pose is right-arm-only.  Reparenting the live mount to the left
	// forearm must take the BMG branch out, and putting it back on the new body must
	// restore the fit without changing the weapon data.
	Mount->AttachToComponent(Rig->GetJoint(TEXT("Forearm_L")),
		FAttachmentTransformRules::KeepRelativeTransform);
	Publish(1.0f);
	const FRotator LeftMountUpper = UpperLocalRotation();
	const FRotator LeftMountFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("left weapon mount excludes BMG upper roll"), FMath::Abs(LeftMountUpper.Roll) < 0.75f);
	TestTrue(TEXT("left weapon mount excludes BMG forearm roll"), FMath::Abs(LeftMountFore.Roll) < 0.75f);
	Mount->AttachToComponent(Rig->GetJoint(TEXT("Forearm_R")),
		FAttachmentTransformRules::KeepRelativeTransform);
	Publish(1.0f);
	const FRotator RightMountUpper = UpperLocalRotation();
	const FRotator RightMountFore = Forearm->GetRelativeRotation();
	TestTrue(TEXT("restored right mount restores BMG upper roll"),
		FMath::IsNearlyEqual(RightMountUpper.Roll, 15.0f, 0.75f));
	TestTrue(TEXT("restored right mount restores BMG forearm roll"),
		RightMountFore.Quaternion().Equals(RestForeExpected,.01f));

	// The exact mesh path alone must never enable the fit on a legacy or protected
	// body.  Use the same active BMG data on each body so this exercises the runtime
	// body gate rather than only the hand asset resolver.
	auto CheckBmgDisabledForBody = [&](const TCHAR* Label, USkeletalMesh* Body)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s body exists"), Label), Body)) return;
		AMSPlayerCharacter* OtherUnit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
		if (!TestNotNull(*FString::Printf(TEXT("%s test unit exists"), Label), OtherUnit)) return;
		UMSMobileSuitData* OtherSuit = NewObject<UMSMobileSuitData>(OtherUnit);
		OtherSuit->VisualMesh = Body;
		OtherSuit->VisualMeshTargetHeightCm = 1800.0f;
		OtherSuit->VisualMeshYawOffsetDeg = -90.0f;
		OtherUnit->InitializeUnit(OtherSuit);
		UMSWeaponComponent* OtherWeapons = OtherUnit->GetWeaponComponent();
		UMSMechAnimatorComponent* OtherAnimator = OtherUnit->GetMechAnimatorComponent();
		UMSMechRigComponent* OtherRig = OtherUnit->GetMechRigComponent();
		if (!TestNotNull(*FString::Printf(TEXT("%s weapon component exists"), Label), OtherWeapons)
			|| !TestNotNull(*FString::Printf(TEXT("%s animator exists"), Label), OtherAnimator)
			|| !TestNotNull(*FString::Printf(TEXT("%s rig exists"), Label), OtherRig)) return;
		OtherUnit->SetActorTickEnabled(false);
		OtherAnimator->SetComponentTickEnabled(false);
		OtherWeapons->InitializeSlots({Bmg});
		FMSProceduralPoseInputs OtherInputs;
		OtherInputs.bHasRangedWeapon = true;
		OtherInputs.bHasDirectionalFacing = true;
		OtherInputs.bIsAiming = true;
		OtherAnimator->EvaluatePose(OtherInputs, 1.0f);
		const USceneComponent* OtherUpper = OtherRig->GetJoint(TEXT("UpperArm_R"));
		const USceneComponent* OtherFore = OtherRig->GetJoint(TEXT("Forearm_R"));
		if (!TestNotNull(*FString::Printf(TEXT("%s upper arm exists"), Label), OtherUpper)
			|| !TestNotNull(*FString::Printf(TEXT("%s forearm exists"), Label), OtherFore)) return;
		TestTrue(FString::Printf(TEXT("%s excludes BMG upper roll"), Label),
			FMath::Abs(OtherUpper->GetRelativeRotation().Roll) < 0.75f);
		TestTrue(FString::Printf(TEXT("%s excludes BMG forearm roll"), Label),
			FMath::Abs(OtherFore->GetRelativeRotation().Roll) < 0.75f);
	};
	USkeletalMesh* LegacyMobLight = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/MobLight/SK_MobLight.SK_MobLight"));
	CheckBmgDisabledForBody(TEXT("legacy MobLight"), LegacyMobLight);
	UMSMobileSuitData* ProtectedMpu = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"));
	if (TestNotNull(TEXT("BMG protected MPU fixture exists"), ProtectedMpu))
	{
		CheckBmgDisabledForBody(TEXT("protected MPU"), ProtectedMpu->VisualMesh.LoadSynchronous());
	}
	TestTrue(TEXT("BMG data mesh and skin transform remain unchanged"),
		Bmg->WeaponMesh == OriginalMesh && Bmg->WeaponMeshTransform.Equals(OriginalSkin, 0.0f));
	return true;
}

bool FMSMobHandPreviewTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* MobLightHands = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Meshes/MobileSuits/MobLightHands/SK_MobLightHands.SK_MobLightHands"));
	if (!TestNotNull(TEXT("preview MobLight body is imported"), MobLightHands)) return false;
	for (const TCHAR* Side : {TEXT("L"), TEXT("R")})
	{
		for (const TCHAR* Pose : {TEXT("Fist"), TEXT("Open"), TEXT("Trigger"), TEXT("Grip")})
		{
			const FString AssetName = FString::Printf(TEXT("SM_Hand_%s_%s"), Side, Pose);
			const FString AssetPath = FString::Printf(
				TEXT("/Game/Meshes/MobileSuits/MobLightHands/%s.%s"), *AssetName, *AssetName);
			TestNotNull(*FString::Printf(TEXT("preview %s static hand asset exists"), *AssetName),
				LoadObject<UStaticMesh>(nullptr, *AssetPath));
		}
	}
	UMSMobileSuitData* ProtectedMpu = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"));
	if (!TestNotNull(TEXT("preview protected MPU suit fixture exists"), ProtectedMpu)) return false;

	FHandTestWorld TestWorld;
	UMSMobileSuitPreviewRenderer* Renderer = NewObject<UMSMobileSuitPreviewRenderer>();
	UImage* Image = NewObject<UImage>(Renderer);
	if (!TestTrue(TEXT("preview renderer initializes"), Renderer->Initialize(TestWorld.World, Image)))
	{
		return false;
	}
	UMSMobileSuitData* MobLightSuit = NewObject<UMSMobileSuitData>(Renderer);
	MobLightSuit->VisualMesh = MobLightHands;
	MobLightSuit->VisualMeshTargetHeightCm = 1800.0f;
	MobLightSuit->VisualMeshYawOffsetDeg = -90.0f;

	Renderer->SetMobileSuit(MobLightSuit);
	UMSMobHandVisuals* Hands = Renderer->GetMobHandVisuals();
	if (!TestNotNull(TEXT("preview hand helper exists"), Hands))
	{
		Renderer->Shutdown();
		return false;
	}
	TestTrue(TEXT("preview uses the new MobLight body"), Hands->IsUsingMobLightHands());
	TestEqual(TEXT("preview creates exactly two open hands"),
		Hands->GetVisibleComponentCountForAutomationTests(), 2);
	TestEqual(TEXT("preview leaves the left hand open"),
		Hands->GetCurrentPose().Left, EMSMobHandPose::Open);
	TestEqual(TEXT("preview leaves the right hand open"),
		Hands->GetCurrentPose().Right, EMSMobHandPose::Open);

	Renderer->SetMobileSuit(ProtectedMpu);
	TestFalse(TEXT("preview MPU switch disables MobLight hands"), Hands->IsUsingMobLightHands());
	TestEqual(TEXT("preview MPU switch removes every hand component"),
		Hands->GetVisibleComponentCountForAutomationTests(), 0);

	Renderer->SetMobileSuit(MobLightSuit);
	TestTrue(TEXT("preview MobLight switch recreates hand components"), Hands->IsUsingMobLightHands());
	TestEqual(TEXT("preview MobLight switch restores two open hands"),
		Hands->GetVisibleComponentCountForAutomationTests(), 2);
	TestEqual(TEXT("preview reset restores left open pose"),
		Hands->GetCurrentPose().Left, EMSMobHandPose::Open);
	TestEqual(TEXT("preview reset restores right open pose"),
		Hands->GetCurrentPose().Right, EMSMobHandPose::Open);

	Renderer->Shutdown();
	return true;
}

bool FMSMobTriggerGunClearanceTest::RunTest(const FString&)
{
	FHandTestWorld TestWorld;
	AMSPlayerCharacter* Unit=TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	UMSMobileSuitData* Suit=LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_StrikerMk1.DA_MS_StrikerMk1"));
	if (!TestNotNull(TEXT("production Striker fixture"),Suit) || !Unit) return false;
	Unit->InitializeUnit(Suit);
	Unit->SetActorTickEnabled(false);
	Unit->SetActorRotation(FRotator(0,37,0));
	auto* Animator=Unit->GetMechAnimatorComponent();
	auto* Visual=Unit->GetMeshVisualComponent();
	auto* Hands=Visual->GetMobHandVisuals();
	auto* Rig=Unit->GetMechRigComponent();
	auto* Weapons=Unit->GetWeaponComponent();
	auto* WeaponVisual=Unit->GetWeaponVisualComponent();
	Animator->SetComponentTickEnabled(false);
	if (!TestTrue(TEXT("fixture uses actual std2 MobHands"),Hands && Hands->IsUsingMobLightHands())) return false;
	auto* Posed=Visual->GetPoseableMesh();
	FMSProceduralPoseInputs Inputs;
	Inputs.bHasRangedWeapon=true;
	Inputs.bHasDirectionalFacing=true;
	auto Publish=[&]()
	{
		for (int32 I=0; I<45; ++I)
		{
			Animator->EvaluatePose(Inputs,1.f/30.f);
			Visual->TransferPoseForAutomationTests();
		}
	};
	// Different receiver lengths/widths and stock layouts use the same authored
	// trigger datum. No per-weapon translation may slide a grip out of the fingers.
	for (const TCHAR* Name : {TEXT("BurstCarbineBC4"),TEXT("BR"),TEXT("BeamMachineGun"),
		TEXT("Shotgun"),TEXT("GatlingGun"),TEXT("BeamLauncherBL4")})
	{
		const FString Path=FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"),Name,Name);
		auto* Weapon=LoadObject<UMSWeaponDataBase>(nullptr,*Path);
		if (!TestNotNull(*FString::Printf(TEXT("actual %s data"),Name),Weapon)) continue;
		Weapons->InitializeSlots({Weapon});
		Visual->TransferPoseForAutomationTests();
		WeaponVisual->RefreshFromWeaponComponent();
		auto* Held=WeaponVisual->GetImportedHeldMeshComponent();
		auto* Hand=Hands->GetHandComponentForAutomationTests(false,EMSMobHandPose::Trigger);
		if (!TestNotNull(TEXT("actual imported gun"),Held) || !TestNotNull(TEXT("actual trigger hand"),Hand)) continue;
		for (const bool bAim : {false,true}) for (const float Yaw : {-60.f,0.f,60.f})
		{
			Inputs.bIsAiming=bAim; Inputs.AimYaw=Yaw;
			Publish(); // Do not refresh/re-attach the gun to hide drift during a pose change.
			const FString Label=FString::Printf(TEXT("%s %s yaw %.0f"),Name,bAim?TEXT("aim"):TEXT("carry"),Yaw);
			const FVector Mount=Posed->GetBoneLocation(TEXT("weapon_mount_r"),EBoneSpaces::WorldSpace);
			const FVector HandGrip=Hand->GetComponentTransform().TransformPosition(FVector(-395,5,745));
			TestTrue(Label+TEXT(" keeps gun origin registered inside finger grip"),
				Held->GetComponentLocation().Equals(HandGrip,1.f) && Mount.Equals(HandGrip,1.f));
			TestTrue(Label+TEXT(" maintains muzzle aim while elbow moves sideways"),
				FVector::DotProduct(WeaponVisual->GetMuzzleWorldDirection(FVector::ZeroVector),
					Rig->GetJoint(TEXT("WeaponMount"))->GetForwardVector())>.98f);
			const FVector Elbow=Posed->GetBoneLocation(TEXT("forearm_r"),EBoneSpaces::WorldSpace);
			const FVector GunForward=WeaponVisual->GetMuzzleWorldDirection(FVector::ZeroVector).GetSafeNormal();
			const FVector BodyUp=Unit->GetActorUpVector();
			const FVector GunRight=FVector::CrossProduct(BodyUp,GunForward).GetSafeNormal();
			const FVector GripToElbow=(Elbow-Mount).GetSafeNormal();
			// Imported gun local +Z is its muzzle axis; local +Y is not a stable
			// definition of horizontal clearance. Use the actual socket muzzle frame.
			TestTrue(Label+TEXT(" moves elbow out of receiver's vertical plane"),
				FMath::Abs(FVector::DotProduct(GripToElbow,GunRight))>.35f);
			// A lateral-only fit used to pass while the elbow was 110 cm above the
			// grip and the cuff cut through BC4's stock. This approach-cone proxy
			// requires the forearm to rise into the grip from underneath. It is not
			// a substitute for surface-intersection/render checks on the actual mesh.
			TestTrue(Label+TEXT(" approaches the grip from below the rear receiver"),
				FVector::DotProduct(GripToElbow,BodyUp)<-.4f);
			TestTrue(Label+TEXT(" keeps elbow behind the trigger"),
				FVector::DotProduct(GripToElbow,GunForward)<-.5f);
		}
	}
	// Grip/throwable/fist weapons must not inherit the firearm elbow posture.
	for (const TCHAR* Name : {TEXT("HeatSwordHS1"),TEXT("Grenade"),TEXT("PowerKnucklePN1")})
	{
		const FString Path=FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"),Name,Name);
		auto* Weapon=LoadObject<UMSWeaponDataBase>(nullptr,*Path);
		if (!TestNotNull(TEXT("non-trigger control asset"),Weapon)) continue;
		Weapons->InitializeSlots({Weapon});
		Visual->TransferPoseForAutomationTests();
		WeaponVisual->RefreshFromWeaponComponent();
		Inputs.bIsAiming=false; Inputs.AimYaw=0;
		Publish();
		TestTrue(FString(Name)+TEXT(" clears firearm elbow clearance pose"),
			FMath::Abs(Rig->GetJoint(TEXT("Forearm_R"))->GetRelativeRotation().Roll)<.75f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
