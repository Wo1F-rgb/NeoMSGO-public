// Copyright NeoMSGO Project. All Rights Reserved.
//
// WeaponLibraryOpus (2026-09-24): 58 weapon/shield definitions display the 44 authored models.
// These are NullRHI logic checks of the saved assets and the attachment rules. They do not
// replace a rendered PIE check of how each model looks on a suit.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Combat/MSShieldComponent.h"
#include "Combat/MSVisualProjectile.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Data/MSWeaponLibraryOpusVisuals.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusAssetContractTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.AssetContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusRegenerationTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.RegenerationKeepsVisuals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusHeldVisualTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.HeldVisual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusPoseableVisualTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.PoseableVisual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusShieldAndHeadTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.NativeShieldAndHead",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponLibraryOpusProjectileTest,
	"NeoMSGO.Weapons.WeaponLibraryOpus.Projectiles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	using MSWeaponLibraryOpus::ERole;
	using MSWeaponLibraryOpus::FVisualRow;

	struct FOpusTestWorld
	{
		UWorld* World = nullptr;
		FOpusTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSWeaponLibraryOpusTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FOpusTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSPlayerCharacter* SpawnUnit(UWorld* World)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	}

	UMSWeaponDataBase* LoadWeapon(const TCHAR* AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/Data/Weapons/%s.%s"), AssetName, AssetName);
		return LoadObject<UMSWeaponDataBase>(nullptr, *Path);
	}

	UStaticMesh* LoadModel(const TCHAR* ModelId)
	{
		return Cast<UStaticMesh>(MSWeaponLibraryOpus::MeshPath(ModelId).TryLoad());
	}

	USceneComponent* FindNamedComponent(AActor* Owner, const FName Name)
	{
		TArray<USceneComponent*> Components;
		Owner->GetComponents(Components);
		for (USceneComponent* Component : Components)
		{
			if (Component && Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}

	bool IsFistRow(const FVisualRow& Row) { return Row.Mount == EMSWeaponMeshMount::Fist; }

	/** Hand grenades: the held model is also the flying body and its origin is the palm. */
	bool IsThrownRow(const FVisualRow& Row)
	{
		return Row.ProjectileModelId && FCString::Strcmp(Row.ProjectileModelId, Row.ModelId) == 0;
	}

	/** The socket that defines each model's working end. */
	FName KeySocketFor(const FVisualRow& Row, const UMSWeaponDataBase& Weapon)
	{
		if (Row.Role == ERole::NativeShield) return TEXT("ShieldMount");
		if (Row.Role == ERole::HeadModule) return TEXT("Muzzle");
		if (IsFistRow(Row)) return TEXT("Strike");
		if (IsThrownRow(Row)) return TEXT("Release");
		if (Weapon.IsA<UMSMeleeWeaponData>()) return TEXT("BladeTip");
		return TEXT("Muzzle");
	}

	FVector SocketDirection(const UStaticMeshSocket* Socket)
	{
		return Socket ? Socket->RelativeRotation.Vector().GetSafeNormal() : FVector::ZeroVector;
	}
}

bool FMSWeaponLibraryOpusAssetContractTest::RunTest(const FString& Parameters)
{
	TSet<FString> Models;
	int32 Shields = 0;
	int32 HeadModules = 0;
	for (const FVisualRow& Row : MSWeaponLibraryOpus::Rows)
	{
		Models.Add(Row.ModelId);
		if (Row.ProjectileModelId) Models.Add(Row.ProjectileModelId);
		Shields += Row.Role == ERole::NativeShield ? 1 : 0;
		HeadModules += Row.Role == ERole::HeadModule ? 1 : 0;

		const FString Label = Row.AssetName;
		UMSWeaponDataBase* Weapon = LoadWeapon(Row.AssetName);
		UStaticMesh* Mesh = LoadModel(Row.ModelId);
		if (!TestNotNull(*(Label + TEXT(" DA exists")), Weapon)
			|| !TestNotNull(*(Label + TEXT(" model SM_") + Row.ModelId + TEXT(" exists")), Mesh))
		{
			continue;
		}
		TestEqual(*(Label + TEXT(" WeaponMesh is its WeaponLibraryOpus model")),
			Weapon->WeaponMesh.ToSoftObjectPath().ToString(), MSWeaponLibraryOpus::MeshPath(Row.ModelId).ToString());
		TestTrue(*(Label + TEXT(" WeaponMeshTransform cancels MSScale once (193/1800)")),
			Weapon->WeaponMeshTransform.Equals(MSWeaponLibraryOpus::WeaponMeshTransform(), 1.e-5f)
			&& FMath::IsNearlyEqual(Weapon->WeaponMeshTransform.GetScale3D().X, 193.0f / 1800.0f, 1.e-5f));
		TestTrue(*(Label + TEXT(" mount mode")), Weapon->WeaponMeshMount == Row.Mount);
		TestFalse(*(Label + TEXT(" is not the TAU-sw dual heat-sword type")),
			Weapon->IsA<UMSDualHeatSwordWeaponData>());

		switch (Row.Role)
		{
		case ERole::NativeShield:
			TestTrue(*(Label + TEXT(" remains native shield data")), Weapon->IsA<UMSShieldWeaponData>()
				&& Weapon->CategoryTag == MSTags::Weapon_Category_Shield);
			break;
		case ERole::HeadModule:
			TestTrue(*(Label + TEXT(" remains a built-in machine gun")), Weapon->bIsBuiltIn
				&& Weapon->CategoryTag == MSTags::Weapon_Category_MachineGun);
			break;
		default:
			TestFalse(*(Label + TEXT(" is a selectable (non built-in) weapon")), Weapon->bIsBuiltIn);
			TestFalse(*(Label + TEXT(" is not shield data")), Weapon->IsA<UMSShieldWeaponData>());
			break;
		}

		// Imported geometry: grip/clamp/mount origin inside the bounds and the authored long axis.
		const FBox Box = Mesh->GetBoundingBox();
		TestTrue(*(Label + TEXT(" authored origin lies inside the mesh bounds")),
			Box.ExpandBy(1.0f).IsInside(FVector::ZeroVector));
		const FVector Size = Box.GetSize();
		if (Row.Role == ERole::NativeShield)
		{
			TestTrue(*(Label + TEXT(" shield is tallest along +X (top)")), Size.X > Size.Y && Size.X > Size.Z);
		}
		else if (Row.Role == ERole::HeadModule)
		{
			TestTrue(*(Label + TEXT(" head module is widest along Y")), Size.Y > Size.X && Size.Y > Size.Z);
		}
		else
		{
			TestTrue(*(Label + TEXT(" held model is longest along +Z")), Size.Z > Size.X && Size.Z > Size.Y);
		}
		TestTrue(*(Label + TEXT(" is in authored MS centimetres (not x100 again)")),
			Size.GetMax() > 100.0f && Size.GetMax() < 1500.0f);

		// Materials: every slot bound to its per-model material in the dedicated folder.
		const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
		TestTrue(*(Label + TEXT(" has 2-5 material slots")), Materials.Num() >= 2 && Materials.Num() <= 5);
		const FString MaterialFolder = FString(MSWeaponLibraryOpus::MeshFolder) + TEXT("Materials/") + Row.ModelId + TEXT("/");
		for (const FStaticMaterial& Slot : Materials)
		{
			TestTrue(*(Label + TEXT(" slot ") + Slot.MaterialSlotName.ToString() + TEXT(" uses its own material")),
				Slot.MaterialInterface && Slot.MaterialInterface->GetPathName().StartsWith(MaterialFolder));
		}

		// Working-end socket and direction.
		const FName KeySocket = KeySocketFor(Row, *Weapon);
		const UStaticMeshSocket* Socket = Mesh->FindSocket(KeySocket);
		if (!TestNotNull(*(Label + TEXT(" has socket ") + KeySocket.ToString()), Socket))
		{
			continue;
		}
		if (Row.Role == ERole::NativeShield)
		{
			TestTrue(*(Label + TEXT(" ShieldMount is the forearm clamp origin")),
				Socket->RelativeLocation.IsNearlyZero(0.5f));
			continue;
		}
		TestTrue(*(Label + TEXT(" ") + KeySocket.ToString() + TEXT(" points along authored +Z")),
			SocketDirection(Socket).Equals(FVector::UpVector, 0.001f));
		if (IsThrownRow(Row))
		{
			// The throw starts from the palm: Muzzle is the documented alias of Release.
			const UStaticMeshSocket* Muzzle = Mesh->FindSocket(TEXT("Muzzle"));
			TestTrue(*(Label + TEXT(" Release is the palm origin and Muzzle aliases it")),
				Socket->RelativeLocation.IsNearlyZero(0.5f) && Muzzle
				&& Muzzle->RelativeLocation.Equals(Socket->RelativeLocation, 0.01f)
				&& SocketDirection(Muzzle).Equals(FVector::UpVector, 0.001f));
			continue;
		}
		TestTrue(*(Label + TEXT(" ") + KeySocket.ToString() + TEXT(" is ahead of the origin")),
			Socket->RelativeLocation.Z > 0.0f);
		if (Row.Role == ERole::Weapon && !Weapon->IsA<UMSMeleeWeaponData>())
		{
			// Long-shroud exceptions (BL4 26cm, CB9 prongs) stay within 1.4m of the front bound.
			TestTrue(*(Label + TEXT(" Muzzle sits at the front of the model")),
				Socket->RelativeLocation.Z > Box.Max.Z - 140.0f);
		}
		if (Row.Role == ERole::HeadModule)
		{
			TestTrue(*(Label + TEXT(" has twin Muzzle_L/Muzzle_R and HeadMount")),
				Mesh->FindSocket(TEXT("Muzzle_L")) && Mesh->FindSocket(TEXT("Muzzle_R"))
				&& Mesh->FindSocket(TEXT("HeadMount")));
		}
		if (Weapon->IsA<UMSMeleeWeaponData>() && !IsFistRow(Row))
		{
			const UStaticMeshSocket* Root = Mesh->FindSocket(TEXT("BladeRoot"));
			TestTrue(*(Label + TEXT(" blade tip is beyond the blade root")),
				Root && Socket->RelativeLocation.Z > Root->RelativeLocation.Z);
		}
	}
	TestEqual(TEXT("58 definitions are assigned"), static_cast<int32>(UE_ARRAY_COUNT(MSWeaponLibraryOpus::Rows)),
		MSWeaponLibraryOpus::TargetDefinitionCount);
	TestEqual(TEXT("44 distinct authored models (incl. Shell_Projectile)"), Models.Num(), MSWeaponLibraryOpus::ModelCount);
	TestEqual(TEXT("three native shields"), Shields, 3);
	TestEqual(TEXT("one head module"), HeadModules, 1);

	// The 17 already detailed definitions keep their own models.
	const TPair<const TCHAR*, const TCHAR*> Excluded[] = {
		{TEXT("DA_Weapon_MidBazookaMB6"), TEXT("/Game/Meshes/Weapons/HeavyWeapons/")},
		{TEXT("DA_Weapon_ShoulderRocketPod"), TEXT("/Game/Meshes/Weapons/HeavyWeapons/")},
		{TEXT("DA_Weapon_MultiHomingMissile"), TEXT("/Game/Meshes/Weapons/HeavyWeapons/")},
		{TEXT("DA_Weapon_TAUsw_TwinHeatSword"), TEXT("/Game/Meshes/Weapons/TAUsw/")},
		{TEXT("DA_Weapon_TAUrus_ChargeBlasterLauncher"), TEXT("/Game/Meshes/Weapons/TAUrus/")}};
	for (const TPair<const TCHAR*, const TCHAR*>& Pair : Excluded)
	{
		TestFalse(*(FString(Pair.Key) + TEXT(" is not a WeaponLibraryOpus target")),
			MSWeaponLibraryOpus::Find(Pair.Key) != nullptr);
		const UMSWeaponDataBase* Weapon = LoadWeapon(Pair.Key);
		TestTrue(*(FString(Pair.Key) + TEXT(" keeps its existing model folder")), Weapon
			&& Weapon->WeaponMesh.ToSoftObjectPath().ToString().StartsWith(Pair.Value));
	}
	return true;
}

bool FMSWeaponLibraryOpusRegenerationTest::RunTest(const FString& Parameters)
{
	// MSWeaponDataGenCommandlet rebuilds each DA from a fresh object and then calls ApplyVisual.
	// Run that same step on fresh transient objects (nothing is saved) and compare with the assets.
	for (const FVisualRow& Row : MSWeaponLibraryOpus::Rows)
	{
		const FString Label = Row.AssetName;
		const UMSWeaponDataBase* Saved = LoadWeapon(Row.AssetName);
		if (!TestNotNull(*(Label + TEXT(" saved DA")), Saved)) continue;
		UMSWeaponDataBase* Fresh = NewObject<UMSWeaponDataBase>(GetTransientPackage(), Saved->GetClass());
		TestTrue(*(Label + TEXT(" fresh object starts without a model")), Fresh->WeaponMesh.IsNull());
		MSWeaponLibraryOpus::ApplyVisual(*Fresh, Row);
		TestEqual(*(Label + TEXT(" regenerated WeaponMesh")), Fresh->WeaponMesh.ToSoftObjectPath().ToString(),
			Saved->WeaponMesh.ToSoftObjectPath().ToString());
		TestTrue(*(Label + TEXT(" regenerated WeaponMeshTransform")),
			Fresh->WeaponMeshTransform.Equals(Saved->WeaponMeshTransform, 1.e-5f));
		TestTrue(*(Label + TEXT(" regenerated mount")), Fresh->WeaponMeshMount == Saved->WeaponMeshMount);
		const UMSRangedWeaponData* FreshRanged = Cast<UMSRangedWeaponData>(Fresh);
		const UMSRangedWeaponData* SavedRanged = Cast<UMSRangedWeaponData>(Saved);
		if (FreshRanged && SavedRanged)
		{
			TestEqual(*(Label + TEXT(" regenerated ProjectileMesh")),
				FreshRanged->ProjectileMesh.ToSoftObjectPath().ToString(),
				SavedRanged->ProjectileMesh.ToSoftObjectPath().ToString());
			TestTrue(*(Label + TEXT(" regenerated projectile transform/exhaust")),
				FreshRanged->ProjectileMeshTransform.Equals(SavedRanged->ProjectileMeshTransform, 1.e-4f)
				&& FreshRanged->bProjectileMeshExhaust == SavedRanged->bProjectileMeshExhaust);
		}
		// ApplyVisual must not touch balance values.
		const UMSWeaponDataBase* Defaults = GetDefault<UMSWeaponDataBase>(Saved->GetClass());
		TestTrue(*(Label + TEXT(" ApplyVisual leaves balance fields at their defaults")),
			Fresh->Power == Defaults->Power && Fresh->Range == Defaults->Range
			&& Fresh->MagazineSize == Defaults->MagazineSize && Fresh->Weight == Defaults->Weight
			&& Fresh->ReloadTime == Defaults->ReloadTime && Fresh->bIsBuiltIn == Defaults->bIsBuiltIn);
	}
	TestNull(TEXT("non-target DA has no WeaponLibraryOpus row"), MSWeaponLibraryOpus::Find(TEXT("DA_Weapon_MidBazookaMB6")));
	return true;
}

bool FMSWeaponLibraryOpusHeldVisualTest::RunTest(const FString& Parameters)
{
	FOpusTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
	if (!TestNotNull(TEXT("Cube-rig unit spawns"), Unit)) return false;
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	USceneComponent* Mount = Rig ? Rig->GetJoint(TEXT("WeaponMount")) : nullptr;
	USceneComponent* MountL = Rig ? Rig->GetJoint(TEXT("WeaponMount_L")) : nullptr;
	if (!TestNotNull(TEXT("weapon component"), Weapons) || !TestNotNull(TEXT("visual component"), Visual)
		|| !TestNotNull(TEXT("WeaponMount"), Mount) || !TestNotNull(TEXT("WeaponMount_L"), MountL))
	{
		return false;
	}
	TestNull(TEXT("Cube rig has no poseable mesh"), Unit->GetMeshVisualComponent()->GetPoseableMesh());
	UStaticMeshComponent* Held = Visual->GetImportedHeldMeshComponent();
	UStaticMeshComponent* HeldL = Visual->GetImportedLeftHeldMeshComponent();
	if (!TestNotNull(TEXT("imported held component"), Held) || !TestNotNull(TEXT("left held component"), HeldL))
	{
		return false;
	}
	const FName LegacyGroups[] = {TEXT("WeaponVisual_MachineGun"), TEXT("WeaponVisual_BeamRifle"),
		TEXT("WeaponVisual_Bazooka"), TEXT("WeaponVisual_Melee")};

	int32 Checked = 0;
	for (const FVisualRow& Row : MSWeaponLibraryOpus::Rows)
	{
		if (Row.Role != ERole::Weapon) continue;
		const FString Label = Row.AssetName;
		UMSWeaponDataBase* Weapon = LoadWeapon(Row.AssetName);
		if (!TestNotNull(*(Label + TEXT(" loads")), Weapon)) continue;
		Weapons->InitializeSlots({Weapon});
		Visual->RefreshFromWeaponComponent();
		++Checked;

		TestTrue(*(Label + TEXT(" shows its model in the hand")), Held->IsVisible()
			&& Held->GetStaticMesh() && Held->GetStaticMesh() == Weapon->WeaponMesh.Get());
		for (const FName GroupName : LegacyGroups)
		{
			const USceneComponent* Group = FindNamedComponent(Unit, GroupName);
			TestTrue(*(Label + TEXT(" hides procedural ") + GroupName.ToString()), Group && !Group->IsVisible());
		}
		TestTrue(*(Label + TEXT(" follows the Cube WeaponMount")), Held->GetAttachParent() == Mount);
		TestTrue(*(Label + TEXT(" shows authored size in world cm (MSScale applied once)")),
			Held->GetComponentScale().Equals(FVector::OneVector, 0.001f));

		const bool bDual = Row.Mount == EMSWeaponMeshMount::DualHands;
		TestTrue(*(Label + TEXT(" left-hand copy only for dual mount")), HeldL->IsVisible() == bDual);
		if (bDual)
		{
			TestTrue(*(Label + TEXT(" left copy is the same model on WeaponMount_L")),
				HeldL->GetStaticMesh() == Held->GetStaticMesh() && HeldL->GetAttachParent() == MountL
				&& HeldL->GetUpVector().Equals(MountL->GetForwardVector(), 0.001f)
				&& HeldL->GetComponentScale().Equals(FVector::OneVector, 0.001f));
		}
		if (IsFistRow(Row))
		{
			TestTrue(*(Label + TEXT(" strike axis continues the forearm (mount -Z)")),
				Held->GetUpVector().Equals(-Mount->GetUpVector(), 0.001f));
			TestTrue(*(Label + TEXT(" back of hand faces mount +X")),
				Held->GetForwardVector().Equals(Mount->GetForwardVector(), 0.001f));
			const FVector ForearmLocal = Rig->GetJoint(TEXT("Forearm_R"))->GetComponentTransform()
				.InverseTransformPosition(Held->GetComponentLocation());
			TestTrue(*(Label + TEXT(" sits on the forearm axis past its end")),
				FMath::Abs(ForearmLocal.X) < 0.5f && FMath::Abs(ForearmLocal.Y) < 0.5f && ForearmLocal.Z < -31.0f);
		}
		else
		{
			TestTrue(*(Label + TEXT(" authored +Z faces WeaponMount +X")),
				Held->GetUpVector().Equals(Mount->GetForwardVector(), 0.001f));
			TestTrue(*(Label + TEXT(" authored +X faces WeaponMount +Z")),
				Held->GetForwardVector().Equals(Mount->GetUpVector(), 0.001f));
			TestTrue(*(Label + TEXT(" grip origin stays on WeaponMount")),
				Held->GetComponentLocation().Equals(Mount->GetComponentLocation(), 0.01f));
		}
		if (Held->DoesSocketExist(TEXT("Muzzle")))
		{
			TestTrue(*(Label + TEXT(" fires from its Muzzle socket")),
				Visual->GetMuzzleWorldLocation().Equals(Held->GetSocketLocation(TEXT("Muzzle")), 0.01f));
			const FVector Direction = Visual->GetMuzzleWorldDirection(FVector::BackwardVector);
			TestTrue(*(Label + TEXT(" muzzle direction is the socket direction")),
				Direction.Equals(Held->GetSocketRotation(TEXT("Muzzle")).Vector().GetSafeNormal(), 0.001f));
			if (!IsFistRow(Row))
			{
				TestTrue(*(Label + TEXT(" muzzle points along the barrel (mount +X)")),
					Direction.Equals(Mount->GetForwardVector(), 0.001f));
			}
		}
	}
	TestEqual(TEXT("all 54 selectable targets were displayed"), Checked, 54);

	// Switching away from the twin dagger, and to a meshless weapon, leaves no ghost copies.
	UMSWeaponDataBase* Twin = LoadWeapon(TEXT("DA_Weapon_TwinDaggerTD1"));
	UMSRangedWeaponData* Meshless = NewObject<UMSRangedWeaponData>(Unit);
	Meshless->CategoryTag = MSTags::Weapon_Category_MachineGun;
	Weapons->InitializeSlots({Twin, Meshless});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("twin dagger shows both hands"), Held->IsVisible() && HeldL->IsVisible());
	TestTrue(TEXT("switch to meshless weapon is accepted"), Weapons->SwitchToSlot(1));
	TestTrue(TEXT("meshless switch hides both imported copies"), !Held->IsVisible() && !HeldL->IsVisible()
		&& HeldL->GetStaticMesh() == nullptr);
	const USceneComponent* LegacyMachineGun = FindNamedComponent(Unit, TEXT("WeaponVisual_MachineGun"));
	TestTrue(TEXT("meshless weapon keeps its procedural fallback"), LegacyMachineGun && LegacyMachineGun->IsVisible());
	TestTrue(TEXT("switch back to the twin dagger is accepted"), Weapons->SwitchToSlot(0));
	TestTrue(TEXT("twin dagger restores both hands"), Held->IsVisible() && HeldL->IsVisible());
	Weapons->InitializeSlots({LoadWeapon(TEXT("DA_Weapon_BR"))});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("single-hand weapon after twin dagger leaves no left ghost"), Held->IsVisible() && !HeldL->IsVisible());

	// Failure path: a legacy shield entry left in a weapon slot must not show a shield in the hand.
	UMSWeaponDataBase* LegacyShieldSlot = LoadWeapon(TEXT("DA_Weapon_DefenderShieldDS1"));
	Weapons->InitializeSlots({LegacyShieldSlot});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("legacy shield slot shows nothing in the hand"), LegacyShieldSlot
		&& !LegacyShieldSlot->WeaponMesh.IsNull() && !Held->IsVisible() && !HeldL->IsVisible());
	return true;
}

bool FMSWeaponLibraryOpusPoseableVisualTest::RunTest(const FString& Parameters)
{
	FOpusTestWorld TestWorld;
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
	// TAU-sw has both weapon_mount_r and weapon_mount_l bones.
	UMSMobileSuitData* TaurusSw = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_TAUsw.DA_MS_TAUsw"));
	if (!TestNotNull(TEXT("unit spawns"), Unit) || !TestNotNull(TEXT("TAU-sw data"), TaurusSw)) return false;
	Unit->InitializeUnit(TaurusSw);
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
	UPoseableMeshComponent* Poseable = Unit->GetMeshVisualComponent()->GetPoseableMesh();
	if (!TestNotNull(TEXT("TAU-sw poseable mesh"), Poseable)
		|| !TestTrue(TEXT("TAU-sw has both hand bones"), Poseable->GetBoneIndex(TEXT("weapon_mount_r")) != INDEX_NONE
			&& Poseable->GetBoneIndex(TEXT("weapon_mount_l")) != INDEX_NONE)) return false;
	UStaticMeshComponent* Held = Visual->GetImportedHeldMeshComponent();
	UStaticMeshComponent* HeldL = Visual->GetImportedLeftHeldMeshComponent();
	const USceneComponent* Mount = Rig->GetJoint(TEXT("WeaponMount"));
	const USceneComponent* MountL = Rig->GetJoint(TEXT("WeaponMount_L"));

	const TCHAR* Cases[] = {TEXT("DA_Weapon_BR"), TEXT("DA_Weapon_SniperRifle"), TEXT("DA_Weapon_HeatSwordHS1"),
		TEXT("DA_Weapon_TwinDaggerTD1"), TEXT("DA_Weapon_PowerKnucklePN1"), TEXT("DA_Weapon_Grenade")};
	for (const TCHAR* AssetName : Cases)
	{
		const FString Label = AssetName;
		UMSWeaponDataBase* Weapon = LoadWeapon(AssetName);
		if (!TestNotNull(*(Label + TEXT(" loads")), Weapon)) continue;
		Weapons->InitializeSlots({Weapon});
		Visual->RefreshFromWeaponComponent();
		TestTrue(*(Label + TEXT(" follows weapon_mount_r")), Held->IsVisible()
			&& Held->GetAttachParent() == Poseable && Held->GetAttachSocketName() == FName(TEXT("weapon_mount_r")));
		TestTrue(*(Label + TEXT(" ignores the imported bone scale")), Held->IsUsingAbsoluteScale()
			&& Held->GetComponentScale().Equals(FVector::OneVector, 0.001f));
		if (Weapon->WeaponMeshMount == EMSWeaponMeshMount::Fist)
		{
			TestTrue(*(Label + TEXT(" strike axis continues the ghost forearm")),
				Held->GetUpVector().Equals(-Mount->GetUpVector(), 0.001f));
		}
		else
		{
			TestTrue(*(Label + TEXT(" authored +Z faces ghost mount +X")),
				Held->GetUpVector().Equals(Mount->GetForwardVector(), 0.001f));
		}
		const bool bDual = Weapon->WeaponMeshMount == EMSWeaponMeshMount::DualHands;
		TestTrue(*(Label + TEXT(" left copy only for the twin dagger")), HeldL->IsVisible() == bDual);
		if (bDual)
		{
			TestTrue(*(Label + TEXT(" left copy follows weapon_mount_l")),
				HeldL->GetAttachParent() == Poseable && HeldL->GetAttachSocketName() == FName(TEXT("weapon_mount_l"))
				&& HeldL->GetUpVector().Equals(MountL->GetForwardVector(), 0.001f)
				&& HeldL->GetComponentScale().Equals(FVector::OneVector, 0.001f));
		}
		TestFalse(*(Label + TEXT(" never uses the TAU-sw heat-sword components")),
			Visual->GetRightHeatSwordComponent()->IsVisible() || Visual->GetLeftHeatSwordComponent()->IsVisible());
	}
	return true;
}

bool FMSWeaponLibraryOpusShieldAndHeadTest::RunTest(const FString& Parameters)
{
	FOpusTestWorld TestWorld;
	const TPair<const TCHAR*, const TCHAR*> ShieldSuits[] = {
		{TEXT("DA_MS_StrikerMk1"), TEXT("DS1_DefenderShield")},
		{TEXT("DA_MS_GuardianDefender"), TEXT("DS1_DefenderShield")},
		{TEXT("DA_MS_BreakerAssault"), TEXT("LS2_LightShield")},
		{TEXT("DA_MS_VanguardRX"), TEXT("LS2_LightShield")},
		{TEXT("DA_MS_DreadnoughtHeavy"), TEXT("FS3_FortressShield")}};
	for (const TPair<const TCHAR*, const TCHAR*>& Pair : ShieldSuits)
	{
		const FString Label = Pair.Key;
		const FString SuitPath = FString::Printf(TEXT("/Game/Data/MobileSuits/%s.%s"), Pair.Key, Pair.Key);
		UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr, *SuitPath);
		AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
		if (!TestNotNull(*(Label + TEXT(" data")), Suit) || !TestNotNull(*(Label + TEXT(" unit")), Unit)) continue;
		Unit->SetGenericTeamId(MSTeam::EnemyTeamId());
		Unit->InitializeUnit(Suit);
		UMSShieldComponent* Shield = Unit->GetShieldComponent();
		UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
		UStaticMeshComponent* ShieldMesh = Visual->GetImportedShieldMeshComponent();
		USceneComponent* Forearm = Unit->GetMechRigComponent()->GetJoint(TEXT("Forearm_L"));
		const USceneComponent* Procedural = FindNamedComponent(Unit, TEXT("WeaponVisual_Shield"));
		UStaticMesh* Expected = LoadModel(Pair.Value);
		if (!TestNotNull(*(Label + TEXT(" shield component")), Shield) || !TestNotNull(*(Label + TEXT(" imported shield component")), ShieldMesh)
			|| !TestNotNull(*(Label + TEXT(" model")), Expected))
		{
			continue;
		}
		const float MaxBefore = Shield->GetMaxDurability();
		TestTrue(*(Label + TEXT(" native shield keeps its durability data")), Shield->IsNativeShieldEquipped()
			&& (MaxBefore == 990.0f || MaxBefore == 1380.0f || MaxBefore == 1820.0f));
		// Cube suits use the Forearm_L joint; imported suits (MobLight since 2026-09-24) the forearm_l bone.
		UPoseableMeshComponent* SuitMesh = Unit->GetMeshVisualComponent()->GetPoseableMesh();
		const bool bOnForearm = SuitMesh
			? ShieldMesh->GetAttachParent() == SuitMesh && ShieldMesh->GetAttachSocketName() == FName(TEXT("forearm_l"))
			: ShieldMesh->GetAttachParent() == Forearm;
		TestTrue(*(Label + TEXT(" shows its authored shield on the left forearm")), ShieldMesh->IsVisible()
			&& ShieldMesh->GetStaticMesh() == Expected && bOnForearm && Visual->IsShieldVisible());
		TestTrue(*(Label + TEXT(" procedural shield cubes are hidden")), Procedural && !Procedural->IsVisible());
		TestTrue(*(Label + TEXT(" shield is authored size in world cm")),
			ShieldMesh->GetComponentScale().Equals(FVector::OneVector, 0.001f));
		// The face points away from the body on the side where the shield actually is (imported
		// TAUsw-contract meshes carry forearm_l on the suit's right).
		const float ShieldSide = FMath::Sign(Unit->GetActorTransform().InverseTransformPosition(
			ShieldMesh->GetComponentLocation()).Y);
		TestTrue(*(Label + TEXT(" shield top faces the elbow and its face points outward")),
			ShieldSide != 0.0f && ShieldMesh->GetForwardVector().Equals(Forearm->GetUpVector(), 0.001f)
			&& ShieldMesh->GetUpVector().Equals(Forearm->GetRightVector() * ShieldSide, 0.001f));
		TestFalse(*(Label + TEXT(" shield is never in a weapon slot")),
			Unit->GetWeaponComponent()->GetWeaponData(0) == Shield->GetShieldData()
			|| Unit->GetWeaponComponent()->GetWeaponData(1) == Shield->GetShieldData()
			|| Unit->GetWeaponComponent()->GetWeaponData(2) == Shield->GetShieldData());

		float Armor = 0.0f;
		float ShieldDamage = 0.0f;
		bool bGuarded = false;
		Shield->TryAbsorbDamage(MaxBefore + 100.0f, false,
			Unit->GetActorLocation() + Unit->GetActorForwardVector() * 1000.0f, Armor, ShieldDamage, bGuarded);
		TestTrue(*(Label + TEXT(" frontal packet breaks the shield")), Shield->IsShieldBroken());
		TestTrue(*(Label + TEXT(" broken shield keeps a damaged forearm remnant")),
			!ShieldMesh->IsVisible() && ShieldMesh->GetStaticMesh() == Expected
			&& Visual->IsBrokenShieldVisible() && Visual->IsShieldVisible());
		TestFalse(*(Label + TEXT(" broken shield cannot guard another packet")), Shield->CanProvideGuard());
		Shield->Repair(MaxBefore - 1.0f);
		TestTrue(*(Label + TEXT(" partial repair keeps the damaged remnant")),
			Shield->IsShieldBroken() && Visual->IsBrokenShieldVisible() && !ShieldMesh->IsVisible());
		Shield->RepairToFull();
		TestTrue(*(Label + TEXT(" full repair restores the intact shield")),
			ShieldMesh->IsVisible() && ShieldMesh->GetStaticMesh() == Expected
			&& Visual->IsShieldVisible() && !Visual->IsBrokenShieldVisible());
		Shield->SetDeckShieldEnabled(false);
		Visual->RefreshFromWeaponComponent();
		TestFalse(*(Label + TEXT(" deck-removed shield leaves no ghost")),
			ShieldMesh->IsVisible() || Visual->IsShieldVisible() || Visual->IsBrokenShieldVisible());
		Shield->SetDeckShieldEnabled(true);
		Shield->ResetForRespawn();
		Visual->RefreshFromWeaponComponent();
		TestTrue(*(Label + TEXT(" respawned shield reappears")), ShieldMesh->IsVisible());
		TestEqual(*(Label + TEXT(" durability max unchanged by the visual")), Shield->GetMaxDurability(), MaxBefore);
	}

	// Built-in head vulcan: head module, never the hand.
	AMSPlayerCharacter* Unit = SpawnUnit(TestWorld.World);
	if (!TestNotNull(TEXT("head-vulcan unit"), Unit)) return false;
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
	USceneComponent* Head = Unit->GetMechRigComponent()->GetJoint(TEXT("Head"));
	UMSWeaponDataBase* HeadVulcan = LoadWeapon(TEXT("DA_Weapon_HeadVulcan"));
	UMSWeaponDataBase* Rifle = LoadWeapon(TEXT("DA_Weapon_BR"));
	if (!TestNotNull(TEXT("head vulcan DA"), HeadVulcan) || !TestNotNull(TEXT("BR DA"), Rifle)) return false;
	UStaticMeshComponent* HeadMesh = Visual->GetImportedHeadVulcanMeshComponent();
	UStaticMeshComponent* Held = Visual->GetImportedHeldMeshComponent();
	const USceneComponent* ProceduralHead = FindNamedComponent(Unit, TEXT("WeaponVisual_HeadVulcan"));
	Weapons->InitializeSlots({Rifle, HeadVulcan});
	Visual->RefreshFromWeaponComponent();
	TestTrue(TEXT("head module is on the Head joint"), HeadMesh && HeadMesh->IsVisible()
		&& HeadMesh->GetStaticMesh() == LoadModel(TEXT("HV_HeadVulcanModule")) && HeadMesh->GetAttachParent() == Head);
	TestTrue(TEXT("procedural head cubes are hidden"), ProceduralHead && !ProceduralHead->IsVisible()
		&& Visual->IsHeadVulcanVisible());
	TestTrue(TEXT("head module is authored size and faces forward"),
		HeadMesh->GetComponentScale().Equals(FVector::OneVector, 0.001f)
		&& HeadMesh->GetUpVector().Equals(Head->GetForwardVector(), 0.001f));
	TestTrue(TEXT("rifle in the hand while the head module stays"), Held->IsVisible() && Held->GetStaticMesh() == Rifle->WeaponMesh.Get());
	TestFalse(TEXT("inactive head vulcan does not own the muzzle"),
		Visual->GetMuzzleWorldLocation().Equals(HeadMesh->GetSocketLocation(TEXT("Muzzle")), 0.01f));
	TestTrue(TEXT("switch to head vulcan"), Weapons->SwitchToSlot(1));
	TestFalse(TEXT("head vulcan is never held"), Held->IsVisible());
	TestTrue(TEXT("active head vulcan fires from its Muzzle socket"),
		Visual->GetMuzzleWorldLocation().Equals(HeadMesh->GetSocketLocation(TEXT("Muzzle")), 0.01f)
		&& Visual->GetMuzzleWorldDirection(FVector::BackwardVector).Equals(Head->GetForwardVector(), 0.001f));
	Weapons->InitializeSlots({Rifle});
	Visual->RefreshFromWeaponComponent();
	TestFalse(TEXT("removing the head vulcan leaves no ghost"), HeadMesh->IsVisible() || Visual->IsHeadVulcanVisible());
	return true;
}

bool FMSWeaponLibraryOpusProjectileTest::RunTest(const FString& Parameters)
{
	FOpusTestWorld TestWorld;
	AMSPlayerCharacter* Shooter = SpawnUnit(TestWorld.World);
	if (!TestNotNull(TEXT("shooter"), Shooter)) return false;
	int32 Checked = 0;
	for (const FVisualRow& Row : MSWeaponLibraryOpus::Rows)
	{
		if (!Row.ProjectileModelId) continue;
		const FString Label = Row.AssetName;
		UMSRangedWeaponData* Weapon = Cast<UMSRangedWeaponData>(LoadWeapon(Row.AssetName));
		UStaticMesh* Body = LoadModel(Row.ProjectileModelId);
		if (!TestNotNull(*(Label + TEXT(" ranged DA")), Weapon) || !TestNotNull(*(Label + TEXT(" projectile model")), Body)) continue;
		++Checked;
		TestEqual(*(Label + TEXT(" ProjectileMesh")), Weapon->ProjectileMesh.ToSoftObjectPath().ToString(),
			MSWeaponLibraryOpus::MeshPath(Row.ProjectileModelId).ToString());
		TestFalse(*(Label + TEXT(" unpowered body has no rocket exhaust")), Weapon->bProjectileMeshExhaust);
		TestTrue(*(Label + TEXT(" keeps a visible projectile speed")), Weapon->ProjectileSpeed > 0.0f);
		const FTransform& BodyTransform = Weapon->ProjectileMeshTransform;
		TestTrue(*(Label + TEXT(" ProjectileMeshTransform is the table value")),
			BodyTransform.Equals(MSWeaponLibraryOpus::ProjectileMeshTransform(Row.ProjectileModelId), 1.e-4f));
		TestTrue(*(Label + TEXT(" body stays in world cm (no MSScale)")),
			BodyTransform.GetScale3D().Equals(FVector::OneVector, 1.e-5f));
		// Authored forward: Shell_Projectile +X (flight axes); thrown grenades reuse the held +Z.
		const bool bShell = FCString::Strcmp(Row.ProjectileModelId, TEXT("Shell_Projectile")) == 0;
		const FVector Size = Body->GetBoundingBox().GetSize();
		const FVector AuthoredForward = bShell ? FVector::ForwardVector : FVector::UpVector;
		TestTrue(*(Label + TEXT(" body long axis is the authored forward")), bShell
			? (Size.X > Size.Y && Size.X > Size.Z) : (Size.Z > Size.X && Size.Z > Size.Y));
		TestTrue(*(Label + TEXT(" authored forward flies along actor +X")),
			BodyTransform.GetRotation().RotateVector(AuthoredForward).Equals(FVector::ForwardVector, 0.001f));

		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
			Shooter, FVector(0.0f, 150.0f * Checked, 0.0f), FVector::ForwardVector, Weapon);
		if (!TestNotNull(*(Label + TEXT(" visual projectile spawns")), Projectile)) continue;
		TestTrue(*(Label + TEXT(" flies with its authored body")), Projectile->IsUsingAuthoredProjectileMesh()
			&& Projectile->GetVisualBody()->GetStaticMesh() == Body);
		TestFalse(*(Label + TEXT(" hides the rocket flame")), Projectile->GetVisualTrail()->IsVisible());
		TestTrue(*(Label + TEXT(" keeps authored paint slots")), Body->GetStaticMaterials().Num() >= 2
			&& Projectile->GetVisualBody()->GetMaterial(0) == Body->GetMaterial(0));
	}
	TestEqual(TEXT("eight shell/grenade definitions checked"), Checked, 8);

	// The heavy-weapon rocket keeps its flame (field default and saved asset unchanged).
	UMSRangedWeaponData* Rocket = Cast<UMSRangedWeaponData>(LoadWeapon(TEXT("DA_Weapon_MidBazookaMB6")));
	if (TestNotNull(TEXT("MB-6 loads"), Rocket))
	{
		TestTrue(TEXT("MB-6 rocket keeps its exhaust"), Rocket->bProjectileMeshExhaust);
		AMSVisualProjectile* Projectile = AMSVisualProjectile::LaunchVisualProjectile(
			Shooter, FVector(0.0f, -300.0f, 0.0f), FVector::ForwardVector, Rocket);
		TestTrue(TEXT("MB-6 rocket flame is visible"), Projectile && Projectile->GetVisualTrail()->IsVisible());
	}
	// Failure path: an unset projectile mesh keeps the Cube shell and its trail.
	UMSRangedWeaponData* Plain = NewObject<UMSRangedWeaponData>(Shooter);
	Plain->CategoryTag = MSTags::Weapon_Category_Bazooka;
	Plain->ProjectileSpeed = 10.0f;
	Plain->Range = 5.0f;
	Plain->bProjectileMeshExhaust = false;
	AMSVisualProjectile* Fallback = AMSVisualProjectile::LaunchVisualProjectile(
		Shooter, FVector(0.0f, -600.0f, 0.0f), FVector::ForwardVector, Plain);
	TestTrue(TEXT("meshless shell keeps the Cube body and visible trail"), Fallback
		&& !Fallback->IsUsingAuthoredProjectileMesh() && Fallback->GetVisualTrail()->IsVisible());
	return true;
}

#endif
