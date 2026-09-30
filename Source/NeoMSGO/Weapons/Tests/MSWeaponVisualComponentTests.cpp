// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Combat/MSVisualProjectile.h"
#include "Combat/MSShieldComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponVisualComponentTest,
	"NeoMSGO.Weapons.WeaponVisualComponent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHeavyWeaponAssetContractTest,
	"NeoMSGO.Weapons.HeavyWeaponAssetContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSTAUswHeatSwordVisualTest,
	"NeoMSGO.Weapons.TAUswHeatSwordVisual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponMuzzleSocketTest,
	"NeoMSGO.Weapons.WeaponMuzzleSocket",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FWeaponVisualTestWorld
	{
		UWorld* World = nullptr;
		FWeaponVisualTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSWeaponVisualTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FWeaponVisualTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	template<typename T>
	T* MakeWeapon(UObject* Outer, const FGameplayTag& Category, const bool bBuiltIn = false)
	{
		T* Weapon = NewObject<T>(Outer);
		Weapon->CategoryTag = Category;
		Weapon->bIsBuiltIn = bBuiltIn;
		return Weapon;
	}

	USceneComponent* FindSceneComponent(AActor* Owner, const FName Name)
	{
		if (!Owner)
		{
			return nullptr;
		}
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetFName() == Name)
			{
				return Cast<USceneComponent>(Component);
			}
		}
		return nullptr;
	}
}

bool FMSWeaponVisualComponentTest::RunTest(const FString& Parameters)
{
	FWeaponVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("Unit spawns"), Unit)) return false;
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	if (!TestNotNull(TEXT("Weapon component exists"), Weapons)
		|| !TestNotNull(TEXT("Visual component exists"), Visual)
		|| !TestNotNull(TEXT("Rig exists"), Rig)) return false;

	TestTrue(TEXT("WeaponMount is parented to Forearm_R"),
		Rig->GetJoint(TEXT("WeaponMount"))->GetAttachParent() == Rig->GetJoint(TEXT("Forearm_R")));
	TestEqual(TEXT("machine-gun category maps to distinct prop"),
		UMSWeaponVisualComponent::ResolveVisualKind(MSTags::Weapon_Category_MachineGun), EMSWeaponVisualKind::MachineGun);
	TestEqual(TEXT("beam-rifle category maps to distinct prop"),
		UMSWeaponVisualComponent::ResolveVisualKind(MSTags::Weapon_Category_BeamRifle), EMSWeaponVisualKind::BeamRifle);
	TestEqual(TEXT("bazooka category maps to distinct prop"),
		UMSWeaponVisualComponent::ResolveVisualKind(MSTags::Weapon_Category_Bazooka), EMSWeaponVisualKind::Bazooka);
	TestEqual(TEXT("melee category maps to distinct prop"),
		UMSWeaponVisualComponent::ResolveVisualKind(MSTags::Weapon_Category_Melee), EMSWeaponVisualKind::Melee);

	UMSRangedWeaponData* MachineGun = MakeWeapon<UMSRangedWeaponData>(Unit, MSTags::Weapon_Category_MachineGun);
	UMSRangedWeaponData* BeamRifle = MakeWeapon<UMSRangedWeaponData>(Unit, MSTags::Weapon_Category_BeamRifle);
	UMSRangedWeaponData* Bazooka = MakeWeapon<UMSRangedWeaponData>(Unit, MSTags::Weapon_Category_Bazooka);
	Weapons->InitializeSlots({MachineGun, BeamRifle, Bazooka});
	Visual->RefreshFromWeaponComponent();
	TestEqual(TEXT("initial active slot displays machine gun"), Visual->GetDisplayedWeaponKind(), EMSWeaponVisualKind::MachineGun);
	const USceneComponent* WeaponMount = Rig->GetJoint(TEXT("WeaponMount"));
	TestTrue(TEXT("legacy machine-gun muzzle location remains category based"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(FVector(96.0f, 0.0f, 0.0f)), 0.01f));
	TestTrue(TEXT("legacy weapon keeps the supplied firing direction"),
		Visual->GetMuzzleWorldDirection(FVector::RightVector).Equals(FVector::RightVector, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("switch to beam slot is accepted"), Weapons->SwitchToSlot(1));
	TestEqual(TEXT("switch delegate updates beam prop immediately"), Visual->GetDisplayedWeaponKind(), EMSWeaponVisualKind::BeamRifle);
	BeamRifle->bOverrideMuzzleTransform = true;
	BeamRifle->MuzzleOffsetOverride = FVector(123.0f, 4.0f, 5.0f);
	BeamRifle->MuzzleDirectionOverride = FVector::RightVector;
	TestTrue(TEXT("muzzle override position is transformed from WeaponMount local space"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(BeamRifle->MuzzleOffsetOverride), 0.01f));
	TestTrue(TEXT("muzzle override direction is transformed from WeaponMount local space"),
		Visual->GetMuzzleWorldDirection(FVector::BackwardVector).Equals(
			WeaponMount->GetComponentTransform().TransformVectorNoScale(FVector::RightVector).GetSafeNormal(), 0.001f));
	BeamRifle->bOverrideMuzzleTransform = false;
	TestTrue(TEXT("disabling override restores the legacy beam-rifle muzzle location"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(FVector(95.0f, 0.0f, 0.0f)), 0.01f));
	TestTrue(TEXT("switch to bazooka slot is accepted"), Weapons->SwitchToSlot(2));
	TestEqual(TEXT("switch delegate updates bazooka prop immediately"), Visual->GetDisplayedWeaponKind(), EMSWeaponVisualKind::Bazooka);

	UMSMeleeWeaponData* Melee = MakeWeapon<UMSMeleeWeaponData>(Unit, MSTags::Weapon_Category_Melee);
	UMSRangedWeaponData* BuiltInVulcan = MakeWeapon<UMSRangedWeaponData>(Unit, MSTags::Weapon_Category_MachineGun, true);
	UMSShieldWeaponData* Shield = MakeWeapon<UMSShieldWeaponData>(Unit, MSTags::Weapon_Category_Shield);
	UMSMobileSuitData* NativeShieldSuit = NewObject<UMSMobileSuitData>(Unit, TEXT("NativeShieldSuit"));
	NativeShieldSuit->NativeShield = Shield;
	Unit->InitializeUnit(NativeShieldSuit);
	Weapons->InitializeSlots({Melee, BuiltInVulcan, Shield});
	Visual->RefreshFromWeaponComponent();
	TestEqual(TEXT("melee loadout displays blade prop"), Visual->GetDisplayedWeaponKind(), EMSWeaponVisualKind::Melee);
	TestTrue(TEXT("built-in machine gun creates permanent head vulcan"), Visual->IsHeadVulcanVisible());
	TestTrue(TEXT("equipped shield creates permanent left-forearm shield"), Visual->IsShieldVisible());
	TestTrue(TEXT("switch to built-in slot is accepted"), Weapons->SwitchToSlot(1));
	TestEqual(TEXT("built-in selection hides handheld prop"), Visual->GetDisplayedWeaponKind(), EMSWeaponVisualKind::None);
	TestTrue(TEXT("head vulcan remains visible while selected"), Visual->IsHeadVulcanVisible());

	UMSRangedWeaponData* BuiltInPod = MakeWeapon<UMSRangedWeaponData>(Unit, MSTags::Weapon_Category_Bazooka, true);
	Weapons->InitializeSlots({BuiltInPod});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("built-in bazooka creates permanent shoulder pod"), Visual->IsShoulderPodVisible());
	TestTrue(TEXT("native shield remains visible when removed from weapon loadout"), Visual->IsShieldVisible());
	Unit->GetShieldComponent()->SetDeckShieldEnabled(false);
	Visual->RefreshFromWeaponComponent();
	TestFalse(TEXT("deck unequip hides the native shield"), Visual->IsShieldVisible());
	Unit->GetShieldComponent()->SetDeckShieldEnabled(true);
	Visual->RefreshFromWeaponComponent();

	// A built-in shoulder pod may own its imported mesh. It stays visible while a
	// handheld weapon is selected, but its muzzle socket is only authoritative when
	// the built-in pod is the active weapon.
	UStaticMesh* ShoulderMesh = NewObject<UStaticMesh>(Unit, TEXT("ImportedShoulderPodMesh"));
	UStaticMeshSocket* ShoulderMuzzle = NewObject<UStaticMeshSocket>(ShoulderMesh, TEXT("ShoulderMuzzleSocket"));
	ShoulderMuzzle->SocketName = FName(TEXT("Muzzle"));
	ShoulderMuzzle->RelativeLocation = FVector(80.0f, 2.0f, 6.0f);
	ShoulderMuzzle->RelativeRotation = FRotator::ZeroRotator;
	ShoulderMuzzle->RelativeScale = FVector::OneVector;
	ShoulderMesh->AddSocket(ShoulderMuzzle);
	BuiltInPod->WeaponMesh = ShoulderMesh;
	BuiltInPod->WeaponMeshTransform = FTransform::Identity;
	Visual->RefreshFromWeaponComponent();
	UStaticMeshComponent* ImportedShoulder = Visual->GetImportedShoulderPodMeshComponent();
	USceneComponent* ShoulderGroup = FindSceneComponent(Unit, TEXT("WeaponVisual_ShoulderPod"));
	TestTrue(TEXT("built-in shoulder pod imported mesh is visible"),
		ImportedShoulder && ImportedShoulder->IsVisible()
		&& Visual->IsShoulderPodVisible());
	TestTrue(TEXT("shoulder pod ignores the scaled rig root for world-cm mesh dimensions"),
		ImportedShoulder && ImportedShoulder->IsUsingAbsoluteScale());
	TestTrue(TEXT("imported shoulder pod replaces the procedural shoulder cubes"),
		ImportedShoulder && ShoulderGroup && !ShoulderGroup->IsVisible());
	TestTrue(TEXT("active built-in shoulder muzzle prefers its Muzzle socket"),
		ImportedShoulder && Visual->GetMuzzleWorldLocation().Equals(
			ImportedShoulder->GetSocketLocation(FName(TEXT("Muzzle"))), 0.01f));

	UMSRangedWeaponData* HandheldBazooka = MakeWeapon<UMSRangedWeaponData>(
		Unit, MSTags::Weapon_Category_Bazooka);
	Weapons->InitializeSlots({HandheldBazooka, BuiltInPod});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("shoulder pod remains visible when a handheld weapon is active"),
		Visual->IsShoulderPodVisible() && ImportedShoulder && ImportedShoulder->IsVisible());
	TestTrue(TEXT("handheld selection does not use the inactive shoulder muzzle"),
		!Visual->GetMuzzleWorldLocation().Equals(
			ImportedShoulder->GetSocketLocation(FName(TEXT("Muzzle"))), 0.01f));
	TestTrue(TEXT("switching back to the built-in pod restores its socket muzzle"),
		Weapons->SwitchToSlot(1)
		&& Visual->GetMuzzleWorldLocation().Equals(
			ImportedShoulder->GetSocketLocation(FName(TEXT("Muzzle"))), 0.01f));

	// TAU-rus replaces only its primary BeamRifle prop and follows the imported
	// skeletal weapon mount. Its firing cue resolves from the displayed muzzle.
	UMSMobileSuitData* Taurus = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUrus.DA_MS_TAUrus"));
	if (TestNotNull(TEXT("TAU-rus data asset exists"), Taurus))
	{
		Unit->InitializeUnit(Taurus);
		// デッキ永続化がEquippedWeaponsをユーザーカスタムに置き換えている環境でも
		// このテストの主題(チャージブラスターの表示/マズル)を検証できるよう、
		// スロット構成は明示的にチャージブラスターへ差し替える。
		UMSRangedWeaponData* TaurusBlaster = LoadObject<UMSRangedWeaponData>(nullptr, TEXT(
			"/Game/Data/Weapons/DA_Weapon_TAUrus_ChargeBlasterLauncher.DA_Weapon_TAUrus_ChargeBlasterLauncher"));
		if (TestNotNull(TEXT("TAU-rus charge blaster asset exists"), TaurusBlaster))
		{
			UMSMeleeWeaponData* SecondSlot = MakeWeapon<UMSMeleeWeaponData>(Unit, MSTags::Weapon_Category_Melee);
			Weapons->InitializeSlots({TaurusBlaster, SecondSlot});
			Visual->RefreshFromWeaponComponent();
		}
		UPoseableMeshComponent* Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
		UStaticMeshComponent* ImportedWeapon = Visual->GetImportedHeldMeshComponent();
		TestNotNull(TEXT("TAU-rus poseable mesh is active"), Poseable);
		TestNotNull(TEXT("charge blaster imported mesh component exists"), ImportedWeapon);
		TestTrue(TEXT("charge blaster imported mesh is visible"), Visual->IsImportedHeldMeshVisible());
		if (Poseable && ImportedWeapon)
		{
			TestTrue(TEXT("charge blaster attaches to the TAU-rus poseable mesh"),
				ImportedWeapon->GetAttachParent() == Poseable);
			TestEqual(TEXT("charge blaster follows weapon_mount_r"),
				ImportedWeapon->GetAttachSocketName(), FName(TEXT("weapon_mount_r")));
			TestTrue(TEXT("charge blaster grip origin stays at the mount"),
				ImportedWeapon->GetRelativeLocation().IsNearlyZero());
			TestTrue(TEXT("charge blaster restores common MS scale below the auto-fit mesh"),
				ImportedWeapon->GetRelativeScale3D().Equals(FVector(MSGO::MSScale)));
			TestTrue(TEXT("charge blaster authored +Z barrel faces ghost mount +X"),
				ImportedWeapon->GetUpVector().Equals(WeaponMount->GetForwardVector(), 0.001f));
			TestTrue(TEXT("charge blaster ignores imported bone scale"),
				ImportedWeapon->IsUsingAbsoluteScale());
		}
		UMSWeaponDataBase* ChargeBlaster = Weapons->GetWeaponData(0);
		if (Poseable && ImportedWeapon && ChargeBlaster)
		{
			TestFalse(TEXT("displayed TAU-rus charge blaster has no authored Muzzle socket"),
				ImportedWeapon->DoesSocketExist(FName(TEXT("Muzzle"))));
			const FVector GhostMuzzle = WeaponMount->GetComponentTransform().TransformPosition(
				ChargeBlaster->MuzzleOffsetOverride);
			const FVector DisplayedMuzzle = ImportedWeapon->GetComponentTransform().TransformPosition(
				FVector::UpVector * ChargeBlaster->MuzzleOffsetOverride.X);
			TestTrue(TEXT("charge blaster muzzle is resolved from displayed mesh +Z"),
				Visual->GetMuzzleWorldLocation().Equals(DisplayedMuzzle, 0.01f));
			TestTrue(TEXT("TAU-rus pose mount differs from ghost WeaponMount"),
				!DisplayedMuzzle.Equals(GhostMuzzle, 1.0f));
			TestTrue(TEXT("charge blaster firing direction follows displayed barrel +Z"),
				Visual->GetMuzzleWorldDirection(FVector::BackwardVector).Equals(
					ImportedWeapon->GetUpVector(), 0.001f));

			// Move the actual poseable weapon bone without touching the Cube rig. The
			// firing cue must follow the visible barrel rather than the ghost mount.
			const FName MountBone(TEXT("weapon_mount_r"));
			const FVector OriginalBoneWorld = Poseable->GetBoneLocationByName(
				MountBone, EBoneSpaces::WorldSpace);
			Poseable->SetBoneLocationByName(MountBone,
				OriginalBoneWorld + FVector(0.0f, 80.0f, 0.0f), EBoneSpaces::WorldSpace);
			Poseable->RefreshBoneTransforms();
			const FVector MovedDisplayedMuzzle = ImportedWeapon->GetComponentTransform().TransformPosition(
				FVector::UpVector * ChargeBlaster->MuzzleOffsetOverride.X);
			TestTrue(TEXT("charge blaster mesh follows the moved pose bone"),
				MovedDisplayedMuzzle.Equals(DisplayedMuzzle + FVector(0.0f, 80.0f, 0.0f), 0.1f));
			TestTrue(TEXT("muzzle follows the moved displayed barrel"),
				Visual->GetMuzzleWorldLocation().Equals(MovedDisplayedMuzzle, 0.1f));
			TestTrue(TEXT("moving the pose bone does not move the ghost muzzle"),
				WeaponMount->GetComponentTransform().TransformPosition(
					ChargeBlaster->MuzzleOffsetOverride).Equals(GhostMuzzle, 0.01f));
			Poseable->SetBoneLocationByName(MountBone, OriginalBoneWorld, EBoneSpaces::WorldSpace);
			Poseable->RefreshBoneTransforms();
		}

		// A skin belongs to the weapon, so every suit keeps the charge-blaster mesh.
		UMSMobileSuitData* TaurusAssault = LoadObject<UMSMobileSuitData>(nullptr,
			TEXT("/Game/Data/MobileSuits/DA_MS_TAUrusAssault.DA_MS_TAUrusAssault"));
		if (TestNotNull(TEXT("TAU-rus Assault data asset exists"), TaurusAssault))
		{
			Unit->InitializeUnit(TaurusAssault);
			Weapons->InitializeSlots({ChargeBlaster, Melee});
			Visual->RefreshFromWeaponComponent();
			Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
			ImportedWeapon = Visual->GetImportedHeldMeshComponent();
			TestTrue(TEXT("Assault keeps the charge-blaster weapon skin"), ImportedWeapon
				&& ImportedWeapon->GetStaticMesh()
				&& ImportedWeapon->GetStaticMesh()->GetFName()
					== FName(TEXT("SM_TAUrus_ChargeBlasterLauncher")));
			TestTrue(TEXT("Assault weapon remains attached to weapon_mount_r"), Poseable && ImportedWeapon
				&& ImportedWeapon->GetAttachParent() == Poseable
				&& ImportedWeapon->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
			if (Poseable && ImportedWeapon)
			{
				TestTrue(TEXT("Assault charge-blaster +Z barrel faces ghost mount +X"),
					ImportedWeapon->GetUpVector().Equals(WeaponMount->GetForwardVector(), 0.001f));
			}
		}

		// Regression for #239/#235-3: TAU-sw must not replace the weapon-owned skin.
		UMSMobileSuitData* TaurusSw = LoadObject<UMSMobileSuitData>(nullptr,
			TEXT("/Game/Data/MobileSuits/DA_MS_TAUsw.DA_MS_TAUsw"));
		if (TestNotNull(TEXT("TAU-sw data asset exists"), TaurusSw))
		{
			Unit->InitializeUnit(TaurusSw);
			Weapons->InitializeSlots({ChargeBlaster, Melee});
			Visual->RefreshFromWeaponComponent();
			Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
			ImportedWeapon = Visual->GetImportedHeldMeshComponent();
			TestTrue(TEXT("TAU-sw keeps the TAU-rus charge-blaster skin"), ImportedWeapon
				&& ImportedWeapon->GetStaticMesh()
				&& ImportedWeapon->GetStaticMesh()->GetFName()
					== FName(TEXT("SM_TAUrus_ChargeBlasterLauncher")));
			TestTrue(TEXT("TAU-sw weapon remains attached to weapon_mount_r"), Poseable && ImportedWeapon
				&& ImportedWeapon->GetAttachParent() == Poseable
				&& ImportedWeapon->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
			if (Poseable && ImportedWeapon)
			{
				TestTrue(TEXT("TAU-sw weapon +Z barrel faces ghost mount +X"),
					ImportedWeapon->GetUpVector().Equals(WeaponMount->GetForwardVector(), 0.001f));
			}
		}
		TestTrue(TEXT("switching away from charge blaster is accepted"), Weapons->SwitchToSlot(1));
		TestFalse(TEXT("switching away hides imported charge blaster mesh"), Visual->IsImportedHeldMeshVisible());
		UStaticMeshComponent* RightSword = Visual->GetRightHeatSwordComponent();
		UStaticMeshComponent* LeftSword = Visual->GetLeftHeatSwordComponent();
		TestTrue(TEXT("meshless melee falls back instead of borrowing a suit skin"),
			RightSword && LeftSword && !RightSword->IsVisible() && !LeftSword->IsVisible());

		AMSPlayerCharacter* CubeUnit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
		if (TestNotNull(TEXT("Cube-rig regression unit spawns"), CubeUnit))
		{
			UMSWeaponComponent* CubeWeapons = CubeUnit->GetWeaponComponent();
			UMSWeaponVisualComponent* CubeVisual = CubeUnit->GetWeaponVisualComponent();
			CubeWeapons->InitializeSlots({ChargeBlaster});
			CubeVisual->RefreshFromWeaponComponent();
			UStaticMeshComponent* CubeImportedWeapon = CubeVisual->GetImportedHeldMeshComponent();
			TestTrue(TEXT("Cube rig also displays the imported charge blaster"),
				CubeVisual->IsImportedHeldMeshVisible());
			TestTrue(TEXT("Cube rig keeps the ghost WeaponMount attachment basis"),
				CubeImportedWeapon && CubeImportedWeapon->GetAttachParent()
					== CubeUnit->GetMechRigComponent()->GetJoint(TEXT("WeaponMount")));
			TestTrue(TEXT("Cube rig keeps authored weapon scale under its scaled WeaponMount"),
				CubeImportedWeapon && CubeImportedWeapon->GetRelativeScale3D().Equals(
					FVector::OneVector, KINDA_SMALL_NUMBER));
			TestTrue(TEXT("Cube rig keeps ordinary inherited scale behavior"),
				CubeImportedWeapon && !CubeImportedWeapon->IsUsingAbsoluteScale());
		}
	}

	MachineGun->Range = BeamRifle->Range = Bazooka->Range = 5.0f;
	MachineGun->ProjectileSpeed = BeamRifle->ProjectileSpeed = Bazooka->ProjectileSpeed = 10.0f;
	AMSVisualProjectile* Tracer = AMSVisualProjectile::LaunchVisualProjectile(
		Unit, Unit->GetActorLocation(), FVector::ForwardVector, MachineGun);
	AMSVisualProjectile* BeamBolt = AMSVisualProjectile::LaunchVisualProjectile(
		Unit, Unit->GetActorLocation(), FVector::ForwardVector, BeamRifle);
	AMSVisualProjectile* Shell = AMSVisualProjectile::LaunchVisualProjectile(
		Unit, Unit->GetActorLocation(), FVector::ForwardVector, Bazooka);
	if (!TestNotNull(TEXT("machine-gun tracer spawns"), Tracer)
		|| !TestNotNull(TEXT("beam bolt spawns"), BeamBolt)
		|| !TestNotNull(TEXT("bazooka shell spawns"), Shell)) return false;
	TestEqual(TEXT("machine gun uses short tracer style"), Tracer->GetVisualStyle(), EMSProjectileVisualStyle::Tracer);
	TestEqual(TEXT("beam rifle uses beam-bolt style"), BeamBolt->GetVisualStyle(), EMSProjectileVisualStyle::BeamBolt);
	TestEqual(TEXT("bazooka uses luminous shell/trail style"), Shell->GetVisualStyle(), EMSProjectileVisualStyle::BazookaShell);
	TestNotNull(TEXT("projectile has runtime Cube body"), Shell->GetVisualBody());
	TestNotNull(TEXT("projectile has runtime Cube trail"), Shell->GetVisualTrail());
	TestTrue(TEXT("projectile visuals are NoCollision"),
		Shell->GetVisualBody()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
		&& Shell->GetVisualTrail()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("machine-gun projectile body dimensions follow MSScale"),
		Tracer->GetVisualBody()->GetRelativeScale3D().Equals(
			FVector(0.42f, 0.025f, 0.025f) * MSGO::MSScale, 0.001f));
	TestTrue(TEXT("beam projectile trail dimensions follow MSScale"),
		BeamBolt->GetVisualTrail()->GetRelativeScale3D().Equals(
			FVector(1.1f, 0.025f, 0.025f) * MSGO::MSScale, 0.001f));
	TestTrue(TEXT("bazooka projectile body dimensions follow MSScale"),
		Shell->GetVisualBody()->GetRelativeScale3D().Equals(
			FVector(0.34f, 0.13f, 0.13f) * MSGO::MSScale, 0.001f));
	return true;
}

bool FMSHeavyWeaponAssetContractTest::RunTest(const FString& Parameters)
{
	FWeaponVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("heavy weapon asset test unit spawns"), Unit))
	{
		return false;
	}

	UMSRangedWeaponData* ShoulderWeapon = LoadObject<UMSRangedWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_ShoulderRocketPod.DA_Weapon_ShoulderRocketPod"));
	UStaticMesh* ShoulderMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Meshes/Weapons/HeavyWeapons/SM_SP6_ShoulderPod.SM_SP6_ShoulderPod"));
	const bool bShoulderWeaponLoaded = TestNotNull(
		TEXT("DA_Weapon_ShoulderRocketPod asset exists"), ShoulderWeapon);
	const bool bShoulderMeshLoaded = TestNotNull(
		TEXT("SM_SP6_ShoulderPod asset exists"), ShoulderMesh);
	if (!bShoulderWeaponLoaded || !bShoulderMeshLoaded)
	{
		return false;
	}
	if (!TestTrue(TEXT("shoulder rocket pod is a built-in weapon"), ShoulderWeapon->bIsBuiltIn)
		|| !TestEqual(TEXT("shoulder rocket pod points at SM_SP6_ShoulderPod"),
			ShoulderWeapon->WeaponMesh.LoadSynchronous(), ShoulderMesh))
	{
		return false;
	}
	TestTrue(TEXT("shoulder pod uses local +Z as its longest authored axis"),
		ShoulderMesh->GetBounds().BoxExtent.Z > ShoulderMesh->GetBounds().BoxExtent.X
		&& ShoulderMesh->GetBounds().BoxExtent.Z > ShoulderMesh->GetBounds().BoxExtent.Y);
	TestTrue(TEXT("shoulder pod has the authored Muzzle socket"),
		ShoulderMesh->FindSocket(FName(TEXT("Muzzle"))) != nullptr);

	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	USceneComponent* ShoulderJoint = Rig ? Rig->GetJoint(TEXT("Shoulder_L")) : nullptr;
	if (!TestNotNull(TEXT("heavy weapon component exists"), Weapons)
		|| !TestNotNull(TEXT("heavy weapon visual exists"), Visual)
		|| !TestNotNull(TEXT("heavy weapon rig exists"), Rig)
		|| !TestNotNull(TEXT("Shoulder_L joint exists"), ShoulderJoint))
	{
		return false;
	}

	Weapons->InitializeSlots({ShoulderWeapon});
	Visual->RefreshFromWeaponComponent();
	UStaticMeshComponent* ImportedShoulder = Visual->GetImportedShoulderPodMeshComponent();
	USceneComponent* ProceduralShoulder = FindSceneComponent(Unit, TEXT("WeaponVisual_ShoulderPod"));
	if (!TestNotNull(TEXT("imported shoulder pod component exists"), ImportedShoulder)
		|| !TestNotNull(TEXT("procedural shoulder pod group exists"), ProceduralShoulder))
	{
		return false;
	}
	TestTrue(TEXT("production shoulder pod is visible while equipped"),
		ImportedShoulder->IsVisible() && Visual->IsShoulderPodVisible());
	TestTrue(TEXT("production shoulder pod uses the Shoulder_L parent"),
		ImportedShoulder->GetAttachParent() == ShoulderJoint);
	TestTrue(TEXT("production shoulder pod ignores inherited rig scale"),
		ImportedShoulder->IsUsingAbsoluteScale());
	TestFalse(TEXT("production shoulder pod replaces the procedural cubes"),
		ProceduralShoulder->IsVisible());
	const FVector ExpectedScale = ShoulderWeapon->WeaponMeshTransform.GetScale3D() * MSGO::MSScale;
	TestTrue(TEXT("production shoulder pod restores its authored world-cm scale"),
		ImportedShoulder->GetRelativeScale3D().Equals(ExpectedScale, 0.001f));
	const float ShoulderLengthCm = ShoulderMesh->GetBounds().BoxExtent.Z * 2.0f
		* FMath::Abs(ImportedShoulder->GetRelativeScale3D().Z);
	TestTrue(TEXT("production shoulder pod is approximately 4.5m long"),
		FMath::IsNearlyEqual(ShoulderLengthCm, 445.6f, 6.0f));
	TestTrue(TEXT("shoulder pod local +Z barrel faces rig +X"),
		ImportedShoulder->GetUpVector().Equals(ShoulderJoint->GetForwardVector(), 0.01f));
	TestTrue(TEXT("shoulder pod local +X up faces rig +Z"),
		ImportedShoulder->GetForwardVector().Equals(ShoulderJoint->GetUpVector(), 0.01f));
	TestTrue(TEXT("active shoulder pod muzzle uses the authored socket"),
		Visual->GetMuzzleWorldLocation().Equals(
			ImportedShoulder->GetSocketLocation(FName(TEXT("Muzzle"))), 0.01f));

	UMSRangedWeaponData* Handheld = MakeWeapon<UMSRangedWeaponData>(
		Unit, MSTags::Weapon_Category_MachineGun);
	Weapons->InitializeSlots({Handheld, ShoulderWeapon});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("shoulder pod remains visible with a handheld weapon active"),
		ImportedShoulder->IsVisible() && Visual->IsShoulderPodVisible());
	TestTrue(TEXT("switching back to shoulder pod restores its authored muzzle socket"),
		Weapons->SwitchToSlot(1)
		&& Visual->GetMuzzleWorldLocation().Equals(
			ImportedShoulder->GetSocketLocation(FName(TEXT("Muzzle"))), 0.01f));
	return true;
}

bool FMSTAUswHeatSwordVisualTest::RunTest(const FString& Parameters)
{
	FWeaponVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("TAU-sw heat-sword unit spawns"), Unit))
	{
		return false;
	}
	UMSMobileSuitData* TaurusSw = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUsw.DA_MS_TAUsw"));
	UMSMeleeWeaponData* SingleAsset = LoadObject<UMSMeleeWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_HeatSwordHS1.DA_Weapon_HeatSwordHS1"));
	UMSDualHeatSwordWeaponData* DualAsset = LoadObject<UMSDualHeatSwordWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_TAUsw_TwinHeatSword.DA_Weapon_TAUsw_TwinHeatSword"));
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	if (!TestNotNull(TEXT("TAU-sw v3 data exists"), TaurusSw)
		|| !TestNotNull(TEXT("single heat-sword data asset exists"), SingleAsset)
		|| !TestNotNull(TEXT("twin heat-sword data asset exists"), DualAsset)
		|| !TestNotNull(TEXT("weapon visual exists"), Visual)
		|| !TestNotNull(TEXT("WeaponMount_L exists"), Rig ? Rig->GetJoint(TEXT("WeaponMount_L")) : nullptr))
	{
		return false;
	}
	UStaticMesh* HeatSwordMesh = DualAsset->WeaponMesh.LoadSynchronous();
	if (!TestNotNull(TEXT("twin heat-sword owns the imported heat-sword mesh"), HeatSwordMesh))
	{
		return false;
	}
	TestEqual(TEXT("twin heat-sword uses SM_TAUsw_HeatSword"),
		HeatSwordMesh->GetFName(), FName(TEXT("SM_TAUsw_HeatSword")));

	Unit->InitializeUnit(TaurusSw);
	Visual->RefreshFromWeaponComponent();
	UPoseableMeshComponent* Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
	UStaticMeshComponent* ImportedWeapon = Visual->GetImportedHeldMeshComponent();
	UStaticMeshComponent* RightSword = Visual->GetRightHeatSwordComponent();
	UStaticMeshComponent* LeftSword = Visual->GetLeftHeatSwordComponent();
	USceneComponent* GenericMelee = FindSceneComponent(Unit, TEXT("WeaponVisual_Melee"));
	if (!TestNotNull(TEXT("TAU-sw poseable mesh is active"), Poseable)
		|| !TestNotNull(TEXT("imported held mesh component exists"), ImportedWeapon)
		|| !TestNotNull(TEXT("right heat-sword component exists"), RightSword)
		|| !TestNotNull(TEXT("left heat-sword component exists"), LeftSword)
		|| !TestNotNull(TEXT("generic melee fallback exists"), GenericMelee))
	{
		return false;
	}

	// A meshless single melee weapon must keep the procedural generic melee prop and must
	// never borrow TAU-sw's weapon-specific heat-sword mesh.
	UMSMeleeWeaponData* MeshlessMelee = MakeWeapon<UMSMeleeWeaponData>(Unit, MSTags::Weapon_Category_Melee);
	Weapons->InitializeSlots({MeshlessMelee});
	Visual->RefreshFromWeaponComponent();
	TestEqual(TEXT("meshless melee selects the single weapon mode"),
		Visual->GetHeatSwordMode(), EMSHeatSwordMode::Single);
	TestTrue(TEXT("meshless melee keeps one generic melee prop visible"),
		Visual->GetDisplayedWeaponKind() == EMSWeaponVisualKind::Melee && GenericMelee->IsVisible());
	TestFalse(TEXT("meshless melee has no imported held mesh"), Visual->IsImportedHeldMeshVisible());
	TestTrue(TEXT("meshless melee does not borrow either TAU-sw heat sword"),
		!RightSword->IsVisible() && !LeftSword->IsVisible()
		&& !RightSword->GetStaticMesh() && !LeftSword->GetStaticMesh());

	// HS-1 owns its WeaponLibraryOpus mesh (2026-09-24). It is a single held weapon and still
	// never uses the TAU-sw dual heat-sword components or their 9m auto-fit.
	UStaticMesh* HS1Mesh = SingleAsset->WeaponMesh.LoadSynchronous();
	TestTrue(TEXT("HS-1 owns SM_HS1_HeatSword"),
		HS1Mesh && HS1Mesh->GetFName() == FName(TEXT("SM_HS1_HeatSword")));
	Weapons->InitializeSlots({SingleAsset});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("HS-1 uses one imported held mesh on weapon_mount_r"),
		HS1Mesh && ImportedWeapon->IsVisible() && ImportedWeapon->GetStaticMesh() == HS1Mesh
		&& ImportedWeapon->GetAttachParent() == Poseable
		&& ImportedWeapon->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
	TestTrue(TEXT("HS-1 does not borrow either TAU-sw heat sword"),
		!GenericMelee->IsVisible() && !RightSword->IsVisible() && !LeftSword->IsVisible());

	// A single weapon-owned mesh uses the generic imported held component. The
	// dedicated heat-sword components stay hidden so the same mesh is not doubled.
	UMSMeleeWeaponData* SingleOwnedMesh = MakeWeapon<UMSMeleeWeaponData>(
		Unit, MSTags::Weapon_Category_Melee);
	SingleOwnedMesh->WeaponMesh = DualAsset->WeaponMesh;
	Weapons->InitializeSlots({SingleOwnedMesh});
	Visual->RefreshFromWeaponComponent();
	TestEqual(TEXT("single weapon-owned mesh selects the single mode"),
		Visual->GetHeatSwordMode(), EMSHeatSwordMode::Single);
	TestTrue(TEXT("single weapon-owned mesh uses one imported held mesh"),
		Visual->GetDisplayedWeaponKind() == EMSWeaponVisualKind::Melee
		&& ImportedWeapon->IsVisible() && ImportedWeapon->GetStaticMesh() == HeatSwordMesh
		&& !GenericMelee->IsVisible() && !RightSword->IsVisible() && !LeftSword->IsVisible());
	TestTrue(TEXT("single weapon-owned mesh attaches to weapon_mount_r"),
		ImportedWeapon->GetAttachParent() == Poseable
		&& ImportedWeapon->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));

	Weapons->InitializeSlots({DualAsset});
	Visual->RefreshFromWeaponComponent();
	TestEqual(TEXT("dual data-asset type selects dual visual mode"),
		Visual->GetHeatSwordMode(), EMSHeatSwordMode::Dual);
	TestTrue(TEXT("dual mode displays the weapon-owned mesh in both hands"),
		RightSword->IsVisible() && LeftSword->IsVisible()
		&& RightSword->GetStaticMesh() == HeatSwordMesh
		&& LeftSword->GetStaticMesh() == HeatSwordMesh
		&& RightSword->GetStaticMesh() == LeftSword->GetStaticMesh());
	TestTrue(TEXT("dual mode keeps the generic held prop hidden"),
		!GenericMelee->IsVisible() && !Visual->IsImportedHeldMeshVisible());
	TestTrue(TEXT("right sword is attached to weapon_mount_r"),
		RightSword->GetAttachParent() == Poseable
		&& RightSword->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
	TestTrue(TEXT("left sword is attached to weapon_mount_l"),
		LeftSword->GetAttachParent() == Poseable
		&& LeftSword->GetAttachSocketName() == FName(TEXT("weapon_mount_l")));
	TestTrue(TEXT("both swords ignore imported bone scale"),
		RightSword->IsUsingAbsoluteScale() && LeftSword->IsUsingAbsoluteScale());
	const float SwordLengthCm = HeatSwordMesh->GetBounds().BoxExtent.Z * 2.0f;
	const float ExpectedSwordScale = SwordLengthCm > KINDA_SMALL_NUMBER
		? 900.5f / SwordLengthCm : 0.0f;
	TestTrue(TEXT("both swords use the runtime's 900.5cm design length"),
		SwordLengthCm > KINDA_SMALL_NUMBER
		&& FMath::IsNearlyEqual(RightSword->GetRelativeScale3D().X, ExpectedSwordScale, 0.1f)
		&& FMath::IsNearlyEqual(LeftSword->GetRelativeScale3D().X, ExpectedSwordScale, 0.1f)
		&& FMath::IsNearlyEqual(
			SwordLengthCm * RightSword->GetRelativeScale3D().Z, 900.5f, 0.1f)
		&& FMath::IsNearlyEqual(
			SwordLengthCm * LeftSword->GetRelativeScale3D().Z, 900.5f, 0.1f));
	return true;
}

bool FMSWeaponMuzzleSocketTest::RunTest(const FString& Parameters)
{
	FWeaponVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("socket test unit spawns"), Unit))
	{
		return false;
	}

	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UStaticMeshComponent* ImportedMesh = Visual ? Visual->GetImportedHeldMeshComponent() : nullptr;
	const USceneComponent* WeaponMount = Rig ? Rig->GetJoint(TEXT("WeaponMount")) : nullptr;
	if (!TestNotNull(TEXT("socket test weapon component exists"), Weapons)
		|| !TestNotNull(TEXT("socket test visual component exists"), Visual)
		|| !TestNotNull(TEXT("socket test imported mesh component exists"), ImportedMesh)
		|| !TestNotNull(TEXT("socket test WeaponMount exists"), WeaponMount))
	{
		return false;
	}

	const FName MuzzleSocketName(TEXT("Muzzle"));
	UStaticMesh* SocketedMesh = NewObject<UStaticMesh>(Unit, TEXT("SocketedTestWeaponMesh"));
	UStaticMeshSocket* MuzzleSocket = NewObject<UStaticMeshSocket>(SocketedMesh, TEXT("MuzzleSocket"));
	MuzzleSocket->SocketName = MuzzleSocketName;
	MuzzleSocket->RelativeLocation = FVector(75.0f, 8.0f, 12.0f);
	MuzzleSocket->RelativeRotation = FRotator(0.0f, 90.0f, 0.0f);
	MuzzleSocket->RelativeScale = FVector::OneVector;
	SocketedMesh->AddSocket(MuzzleSocket);

	UMSShotgunWeaponData* SocketWeapon = MakeWeapon<UMSShotgunWeaponData>(
		Unit, MSTags::Weapon_Category_Shotgun);
	SocketWeapon->WeaponMesh = SocketedMesh;
	SocketWeapon->bOverrideMuzzleTransform = true;
	SocketWeapon->MuzzleOffsetOverride = FVector(2.0f, 3.0f, 4.0f);
	SocketWeapon->MuzzleDirectionOverride = FVector::BackwardVector;
	Weapons->InitializeSlots({SocketWeapon});
	Visual->RefreshFromWeaponComponent();

	TestTrue(TEXT("socketed weapon mesh is displayed"), Visual->IsImportedHeldMeshVisible());
	TestTrue(TEXT("displayed weapon exposes the Muzzle socket"), ImportedMesh->DoesSocketExist(MuzzleSocketName));
	TestTrue(TEXT("socket location is preferred over WeaponMount override"),
		!Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(SocketWeapon->MuzzleOffsetOverride), 0.01f));
	TestTrue(TEXT("socket location resolves in world space"),
		Visual->GetMuzzleWorldLocation().Equals(ImportedMesh->GetSocketLocation(MuzzleSocketName), 0.01f));
	TestTrue(TEXT("socket rotation supplies the world firing direction"),
		Visual->GetMuzzleWorldDirection(FVector::BackwardVector).Equals(
			ImportedMesh->GetSocketRotation(MuzzleSocketName).Vector().GetSafeNormal(), 0.001f));

	ImportedMesh->SetRelativeLocation(FVector(19.0f, -11.0f, 7.0f));
	TestTrue(TEXT("socket follows imported mesh movement"),
		Visual->GetMuzzleWorldLocation().Equals(ImportedMesh->GetSocketLocation(MuzzleSocketName), 0.01f));

	ImportedMesh->SetRelativeRotation(FRotator(12.0f, 35.0f, -18.0f));
	TestTrue(TEXT("socket follows imported mesh rotation"),
		Visual->GetMuzzleWorldDirection(FVector::BackwardVector).Equals(
			ImportedMesh->GetSocketRotation(MuzzleSocketName).Vector().GetSafeNormal(), 0.001f));

	ImportedMesh->SetRelativeScale3D(FVector(1.7f, 0.65f, 1.25f));
	TestTrue(TEXT("socket follows imported mesh scale"),
		Visual->GetMuzzleWorldLocation().Equals(ImportedMesh->GetSocketLocation(MuzzleSocketName), 0.01f));

	ImportedMesh->SetVisibility(false, true);
	TestTrue(TEXT("hidden imported mesh restores muzzle override location"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(SocketWeapon->MuzzleOffsetOverride), 0.01f));
	TestTrue(TEXT("hidden imported mesh restores muzzle override direction"),
		Visual->GetMuzzleWorldDirection(FVector::ForwardVector).Equals(
			WeaponMount->GetComponentTransform().TransformVectorNoScale(
				SocketWeapon->MuzzleDirectionOverride.GetSafeNormal()).GetSafeNormal(), 0.001f));

	ImportedMesh->SetVisibility(true, true);
	UStaticMesh* SocketlessMesh = NewObject<UStaticMesh>(Unit, TEXT("SocketlessTestWeaponMesh"));
	SocketWeapon->WeaponMesh = SocketlessMesh;
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("socketless imported mesh stays displayed for fallback coverage"),
		Visual->IsImportedHeldMeshVisible());
	TestTrue(TEXT("socketless mesh restores muzzle override location"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(SocketWeapon->MuzzleOffsetOverride), 0.01f));
	TestTrue(TEXT("socketless mesh restores muzzle override direction"),
		Visual->GetMuzzleWorldDirection(FVector::ForwardVector).Equals(
			WeaponMount->GetComponentTransform().TransformVectorNoScale(
				SocketWeapon->MuzzleDirectionOverride.GetSafeNormal()).GetSafeNormal(), 0.001f));

	SocketWeapon->WeaponMesh = nullptr;
	Visual->RefreshFromWeaponComponent();
	TestFalse(TEXT("unmeshed weapon hides imported mesh"), Visual->IsImportedHeldMeshVisible());
	TestTrue(TEXT("unmeshed weapon keeps muzzle override location"),
		Visual->GetMuzzleWorldLocation().Equals(
			WeaponMount->GetComponentTransform().TransformPosition(SocketWeapon->MuzzleOffsetOverride), 0.01f));
	return true;
}

#endif
