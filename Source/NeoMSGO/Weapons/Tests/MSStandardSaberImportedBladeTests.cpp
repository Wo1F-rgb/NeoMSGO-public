// Copyright NeoMSGO Project. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberImportedBladeTest,
	"NeoMSGO.Weapons.StandardSaberImportedBlade",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSStandardSaberImportedBladeTest::RunTest(const FString& Parameters)
{
	UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr,
		TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"));
	UMSMeleeWeaponData* Weapon = LoadObject<UMSMeleeWeaponData>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_MPU01_BeamSaber.DA_Weapon_MPU01_BeamSaber"));
	if (!TestNotNull(TEXT("installed MPU01 suit exists"), Suit)
		|| !TestNotNull(TEXT("installed MPU01 saber exists"), Weapon)) return false;
	if (!TestTrue(TEXT("installed saber opts into standard motion"), Weapon->bUseStandardBeamSaberMotion))
		return false;

	struct FScopedWorld
	{
		UWorld* World;
		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SaberImportedBladeWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld()
		{
			World->BeginTearingDown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	} ScopedWorld;
	UWorld* World = ScopedWorld.World;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(10000, 20000, 3000), FRotator(0, 90, 0), Spawn);
	AMSPlayerCharacter* Target = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, -50000, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("attacker spawns"), Unit) || !TestNotNull(TEXT("target spawns"), Target)) return false;
	Unit->InitializeUnit(Suit);
	Unit->SetGenericTeamId(FGenericTeamId(1));
	Target->SetGenericTeamId(FGenericTeamId(2));
	Unit->GetCharacterMovement()->DisableMovement();
	Target->GetCharacterMovement()->DisableMovement();
	// Use the same visible weapon switch as deployment. InitializeSlots alone
	// only replaces inventory data; the suit initially displays its spray gun.
	TestTrue(TEXT("deployed MPU01 switches to its saber slot"), Unit->GetWeaponComponent()->SwitchToSlot(1));
	for (int32 I = 0; I < 200 && Unit->GetWeaponComponent()->IsSwitching(); ++I)
	{
		World->Tick(LEVELTICK_All, .01f);
		++GFrameCounter;
	}
	TestFalse(TEXT("real saber switch completes"), Unit->GetWeaponComponent()->IsSwitching());
	UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
	UMSMeshVisualComponent* MeshVisual = Unit->GetMeshVisualComponent();
	UStaticMeshComponent* Held = Unit->GetWeaponVisualComponent()->GetImportedHeldMeshComponent();
	if (!TestNotNull(TEXT("real imported saber component exists"), Held)
		|| !TestNotNull(TEXT("real imported saber mesh exists"), Held->GetStaticMesh().Get())) return false;
	if (!TestTrue(TEXT("visible weapon is the actual saber, not the previous spray gun"),
		Held->GetStaticMesh() == Weapon->WeaponMesh.LoadSynchronous())) return false;
	TestTrue(TEXT("imported saber is visible"), Held->IsVisible());
	TestEqual(TEXT("real saber attaches to the custom mesh hand"), Held->GetAttachSocketName(), FName(TEXT("weapon_mount_r")));
	AddInfo(FString::Printf(TEXT("Imported blade bounds=%s, component scale=%s, data scale=%s"),
		*Held->GetStaticMesh()->GetBoundingBox().ToString(), *Held->GetComponentScale().ToString(),
		*Weapon->WeaponMeshTransform.GetScale3D().ToString()));

	UGA_Melee* Ability = NewObject<UGA_Melee>(Unit);
	if (!TestTrue(TEXT("real-asset standard attack starts"), Ability->PerformMelee(
		Unit, Unit->GetActorLocation(), Unit->GetActorForwardVector(), false))) return false;

	// Compare the collision sampler against the visible component transform,
	// including the imported mesh's non-unit scale and its actual local endpoint.
	FVector HitPoint = FVector::ZeroVector;
	for (int32 Stage = 0; Stage <= 2; ++Stage)
	{
		Ability->StandardSaberAttackIndex = Stage;
		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(Stage);
		const float Time = (Timing.ActiveStart + Timing.ActiveEnd) * .5f;
		FVector Start, End;
		Ability->SampleStandardSaberBlade(Time, Start, End);
		Animator->PreviewStandardSaberPose(Stage, Time);
		MeshVisual->TransferPoseForAutomationTests();
		const FTransform Visible = Held->GetComponentTransform();
		const FVector VisibleTip = Visible.TransformPosition(
			FVector(0, 0, Held->GetStaticMesh()->GetBoundingBox().Max.Z));
		TestTrue(FString::Printf(TEXT("stage %d sampled grip matches real held component"), Stage),
			Start.Equals(Visible.GetLocation(), .05f));
		TestTrue(FString::Printf(TEXT("stage %d sampled tip matches real held component"), Stage),
			End.Equals(VisibleTip, .05f));
		TestTrue(FString::Printf(TEXT("stage %d blade length %.3f cm has game-scale length, without double FBX scaling"), Stage, FVector::Distance(Start, End)),
			FVector::Distance(Start, End) > 200 && FVector::Distance(Start, End) < 2000);
		if (Stage == 0)
		{
			const float LungeAlpha = FMath::Clamp((Time - Timing.LungeStart)
				/ (Timing.LungeEnd - Timing.LungeStart), 0.0f, 1.0f);
			HitPoint = (Start + End) * .5f + Unit->GetActorForwardVector()
				* Timing.LungeDistanceCm * MSGO::MSScale * LungeAlpha;
		}
	}
	Ability->StandardSaberAttackIndex = 0;
	Animator->PreviewStandardSaberPose(0, 0);
	MeshVisual->TransferPoseForAutomationTests();
	Target->SetActorLocation(HitPoint);
	const float ArmorBefore = Target->GetVitalSet()->GetArmor();
	for (int32 I = 0; I < 27; ++I)
	{
		World->Tick(LEVELTICK_All, .01f);
		++GFrameCounter;
	}
	TestTrue(TEXT("the real imported blade overlap applies damage through GAS"),
		Target->GetVitalSet()->GetArmor() < ArmorBefore);
	TestEqual(TEXT("one real blade stage only damages the target once"), Ability->GetLastHitCount(), 1);
	Ability->CancelMelee();
	return true;
}
#endif
