// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSVisualProjectile.h"
#include "Components/BoxComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Gameplay/MSRepairPod.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSPlayerWeaponInputTest,
	"NeoMSGO.Player.WeaponInputDispatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSPlayerChargeDamageTest,
	"NeoMSGO.Player.ChargeInputActualDamage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingWeaponFireTest,
	"NeoMSGO.Player.HomingWeaponFire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingSalvoTargetPersistenceTest,
	"NeoMSGO.Player.HomingSalvoTargetPersistence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSFullAutoWeaponInputTest,
	"NeoMSGO.Player.FullAutoWeaponInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCrouchReleaseInputTest,
	"NeoMSGO.Player.CrouchReleaseInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHomingSalvoFireTest,
	"NeoMSGO.Player.Review68.HomingSalvoOneTriggerFullMagazine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSSniperScopeInputTest,
	"NeoMSGO.Player.Review71.SniperRightClickScope",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Weapon input fixtures are standalone offline gameplay tests. */
	struct FScopedWeaponInputOfflineAuth
	{
		FString Previous = FCommandLine::Get();

		FScopedWeaponInputOfflineAuth()
		{
			TArray<FString> Tokens;
			Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			Effective += TEXT("-MSAuthMode=OfflineLocal");
			FCommandLine::Set(*Effective);
		}

		~FScopedWeaponInputOfflineAuth()
		{
			FCommandLine::Set(*Previous);
		}
	};

	struct FPlayerWeaponTestWorld
	{
		FScopedWeaponInputOfflineAuth OfflineAuth;
		UWorld* World = nullptr;
		FPlayerWeaponTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSPlayerWeaponInputTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FPlayerWeaponTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	AMSPlayerCharacter* SpawnPlayer(UWorld* World, const FVector& Location, const FGenericTeamId Team)
	{
		const FVector SafeLocation = Location + FVector(0.0f, 0.0f, 900.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), SafeLocation, FRotator::ZeroRotator, Params);
		check(Player);
		Player->SetGenericTeamId(Team);
		// Lock-on tests use an explicit camera transform; keep it level with the scaled chest aim point.
		Player->GetMSCamera()->SetWorldLocation(FVector(
			SafeLocation.X, SafeLocation.Y, Player->GetLockOnAimPoint().Z));
		Player->GetMSCamera()->SetWorldRotation(FRotator::ZeroRotator);
		return Player;
	}

	template <typename T>
	T* MakeWeapon(UObject* Outer, const FGameplayTag& Category)
	{
		T* Weapon = NewObject<T>(Outer);
		Weapon->CategoryTag = Category;
		Weapon->MagazineSize = 5;
		Weapon->ReserveAmmo = 5;
		Weapon->Range = 20.0f;
		Weapon->FireInterval = 0.0f;
		Weapon->WeaponSwitchDuration = 0.0f;
		return Weapon;
	}

	void Equip(AMSPlayerCharacter* Player, UMSWeaponDataBase* Weapon)
	{
		Player->GetWeaponComponent()->InitializeSlots({Weapon});
	}

	AActor* SpawnGround(UWorld* World)
	{
		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* Collision = NewObject<UBoxComponent>(Ground, TEXT("PlayerInputGround"));
		Ground->SetRootComponent(Collision);
		Collision->SetBoxExtent(FVector(10000.0f, 10000.0f, 10.0f));
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Collision->SetCollisionObjectType(ECC_WorldStatic);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Collision->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
		return Ground;
	}

	/**
	 * タスク#220: 誘導捕捉サークルはHUDに描かれる円と同一の狭い円錐になった
	 * (1920x1080/FOV60°で全角約16.09°=半角8.04°)。旧実装の固定60°を前提に置かれていた
	 * 敵位置は円の外になるため、実カメラ基準で「円内」に置き直すためのヘルパー。
	 * 判定は対象の照準点(胸元)で行われるので、照準点そのものを目標位置へ合わせる。
	 * @param ForwardCm 視線方向の距離 cm
	 * @param RightCm   視線右方向のオフセット cm(atan(RightCm/ForwardCm) < 8.04° に収めること)
	 */
	void PlaceAimPointInHomingCircle(const AMSPlayerCharacter* Shooter, AMSPlayerCharacter* Target,
		const float ForwardCm, const float RightCm)
	{
		const UMSCameraComponent* Camera = Shooter->GetMSCamera();
		const FVector Desired = Camera->GetComponentLocation()
			+ Camera->GetForwardVector() * ForwardCm + Camera->GetRightVector() * RightCm;
		Target->SetActorLocation(
			Target->GetActorLocation() + (Desired - Target->GetLockOnAimPoint()),
			false, nullptr, ETeleportType::TeleportPhysics);
	}

	void TickWeaponInputTestWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}
}

bool FMSPlayerChargeDamageTest::RunTest(const FString&)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World,
		FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Enemy = SpawnPlayer(TestWorld.World,
		FVector(800.0f, 0.0f, 100.0f), MSTeam::EnemyTeamId());
	UMSRangedWeaponData* Weapon = MakeWeapon<UMSRangedWeaponData>(
		TestWorld.World, MSTags::Weapon_Category_BeamRifle);
	Weapon->bIsChargeable = true;
	Weapon->MaxChargeTime = 1.0f;
	Weapon->Power = 100;
	Weapon->DownPoint = 0;
	Equip(Shooter, Weapon);

	const float ArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("player fire press starts charge route"), Shooter->StartCurrentWeaponFire());
	TestTrue(TEXT("player holds a charge snapshot"), Shooter->IsChargeFireActive());
	TickWeaponInputTestWorld(TestWorld.World, 0.5f);
	Shooter->StopCurrentWeaponFire();
	TestFalse(TEXT("player fire release ends charge"), Shooter->IsChargeFireActive());
	TestTrue(TEXT("player input route applies 1.5x actual Armor damage"), FMath::IsNearlyEqual(
		ArmorBefore - Enemy->GetVitalSet()->GetArmor(), 150.0f, 1.0f));
	return true;
}

bool FMSPlayerWeaponInputTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);

	// Real subclasses plus all nine category tags prove the shared player/BOT resolver contract.
	UMSRangedWeaponData* MachineGun = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	UMSRangedWeaponData* BeamRifle = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_BeamRifle);
	BeamRifle->bIsChargeable = true;
	UMSRangedWeaponData* Bazooka = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_Bazooka);
	Bazooka->BurstCount = 3;
	UMSMeleeWeaponData* Melee = MakeWeapon<UMSMeleeWeaponData>(TestWorld.World, MSTags::Weapon_Category_Melee);
	UMSShieldWeaponData* Shield = MakeWeapon<UMSShieldWeaponData>(TestWorld.World, MSTags::Weapon_Category_Shield);
	UMSSupplyWeaponData* Supply = MakeWeapon<UMSSupplyWeaponData>(TestWorld.World, MSTags::Weapon_Category_Supply);
	UMSContinuousBeamWeaponData* Beam = MakeWeapon<UMSContinuousBeamWeaponData>(TestWorld.World, MSTags::Weapon_Category_ContinuousBeam);
	UMSShotgunWeaponData* Shotgun = MakeWeapon<UMSShotgunWeaponData>(TestWorld.World, MSTags::Weapon_Category_Shotgun);
	Shotgun->PelletSpreadDegrees = 0.0f;
	UMSHomingWeaponData* Homing = MakeWeapon<UMSHomingWeaponData>(TestWorld.World, MSTags::Weapon_Category_Homing);
	Homing->ProjectileSpeed = 1000.0f;
	UMSRangedWeaponData* VisualMachineGun = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	VisualMachineGun->ProjectileSpeed = 110.0f;
	UMSRangedWeaponData* VisualBeamRifle = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_BeamRifle);
	VisualBeamRifle->ProjectileSpeed = 42.0f;
	VisualBeamRifle->bIsChargeable = true;
	UMSRangedWeaponData* VisualBazooka = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_Bazooka);
	VisualBazooka->ProjectileSpeed = 12.0f;
	VisualBazooka->BurstCount = 3;

	TestEqual(TEXT("MachineGun routes to hitscan"), MSWeaponAbilityRouting::Resolve(MachineGun), EMSWeaponAbilityRoute::Hitscan);
	TestEqual(TEXT("chargeable BeamRifle routes to charge"), MSWeaponAbilityRouting::Resolve(BeamRifle), EMSWeaponAbilityRoute::Charge);
	TestEqual(TEXT("burst Bazooka routes to burst"), MSWeaponAbilityRouting::Resolve(Bazooka), EMSWeaponAbilityRoute::Burst);
	TestEqual(TEXT("Melee routes to melee"), MSWeaponAbilityRouting::Resolve(Melee), EMSWeaponAbilityRoute::Melee);
	TestEqual(TEXT("Shield is native equipment and has no three-slot route"),
		MSWeaponAbilityRouting::Resolve(Shield), EMSWeaponAbilityRoute::Unsupported);
	TestEqual(TEXT("Supply routes to deploy"), MSWeaponAbilityRouting::Resolve(Supply), EMSWeaponAbilityRoute::Deploy);
	TestEqual(TEXT("ContinuousBeam routes to held beam"), MSWeaponAbilityRouting::Resolve(Beam), EMSWeaponAbilityRoute::ContinuousBeam);
	TestEqual(TEXT("Shotgun routes to pellet fire"), MSWeaponAbilityRouting::Resolve(Shotgun), EMSWeaponAbilityRoute::Shotgun);
	TestEqual(TEXT("projectile Homing routes to its target-tracking GA"),
		MSWeaponAbilityRouting::Resolve(Homing), EMSWeaponAbilityRoute::Homing);
	TestEqual(TEXT("visual-projectile MachineGun routes to standard fire"),
		MSWeaponAbilityRouting::Resolve(VisualMachineGun), EMSWeaponAbilityRoute::Hitscan);
	TestEqual(TEXT("visual-projectile charge weapon routes to charge"),
		MSWeaponAbilityRouting::Resolve(VisualBeamRifle), EMSWeaponAbilityRoute::Charge);
	TestEqual(TEXT("visual-projectile burst weapon routes to burst"),
		MSWeaponAbilityRouting::Resolve(VisualBazooka), EMSWeaponAbilityRoute::Burst);

	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World, FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Enemy = SpawnPlayer(TestWorld.World, FVector(800.0f, 0.0f, 100.0f), MSTeam::EnemyTeamId());

	Equip(Shooter, MachineGun);
	const float HitscanArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("IA_Fire equivalent invokes hitscan Fire"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("hitscan route is observable"), Shooter->GetLastWeaponAbilityRoute(), EMSWeaponAbilityRoute::Hitscan);
	TestEqual(TEXT("hitscan consumes one round"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 4);
	TestTrue(TEXT("hitscan applies damage"), Enemy->GetVitalSet()->GetArmor() < HitscanArmorBefore);

	Equip(Shooter, Shotgun);
	const float ShotgunArmorBefore = Enemy->GetVitalSet()->GetArmor();
	TestTrue(TEXT("IA_Fire equivalent invokes FireShotgun"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("shotgun route is observable"), Shooter->GetLastWeaponAbilityRoute(), EMSWeaponAbilityRoute::Shotgun);
	TestTrue(TEXT("shotgun applies pellet damage"), Enemy->GetVitalSet()->GetArmor() < ShotgunArmorBefore);

	Equip(Shooter, Bazooka);
	Bazooka->BurstInterval = 0.2f;
	TestTrue(TEXT("IA_Fire equivalent invokes StartBurst"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("burst remains active after its synchronous first round"), Shooter->IsBurstFireActive());

	Equip(Shooter, BeamRifle);
	TestTrue(TEXT("IA_Fire equivalent invokes StartCharge"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("charge state is active while held"), Shooter->IsChargeFireActive());
	Shooter->CompleteCurrentWeaponFire();
	TestFalse(TEXT("release invokes ReleaseCharge"), Shooter->IsChargeFireActive());
	TestEqual(TEXT("charge release consumes one round"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 4);

	Equip(Shooter, Beam);
	Beam->BeamTickInterval = 0.2f;
	TestTrue(TEXT("IA_Fire equivalent invokes StartBeam"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("beam state is active while held"), Shooter->IsContinuousBeamFireActive());
	Shooter->CompleteCurrentWeaponFire();
	TestFalse(TEXT("release invokes StopBeam"), Shooter->IsContinuousBeamFireActive());

	Equip(Shooter, Melee);
	TestTrue(TEXT("IA_Fire equivalent invokes PerformMelee"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("melee route is observable"), Shooter->GetLastWeaponAbilityRoute(), EMSWeaponAbilityRoute::Melee);

	Equip(Shooter, Shield);
	TestFalse(TEXT("legacy shield slot cannot invoke manual guard"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("legacy shield slot is unsupported by the three-slot route"),
		Shooter->GetLastWeaponAbilityRoute(), EMSWeaponAbilityRoute::Unsupported);
	TestFalse(TEXT("manual guard tag remains absent"),
		Shooter->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Guarding));

	Equip(Shooter, Supply);
	Supply->DeployedItemType = EMSDeployedItemType::RepairPod;
	TestTrue(TEXT("IA_Fire equivalent invokes DeployItem"), Shooter->TryFireCurrentWeapon());
	TArray<AActor*> DeployedPods;
	UGameplayStatics::GetAllActorsOfClass(TestWorld.World, AMSRepairPod::StaticClass(), DeployedPods);
	TestEqual(TEXT("Supply fire creates a deployable"), DeployedPods.Num(), 1);

	Equip(Shooter, Homing);
	TestTrue(TEXT("Homing invokes its fire GA"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("Homing consumes one round"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 4);

	// Reload and slot input wrappers must mutate the real weapon component.
	UMSRangedWeaponData* ReloadWeapon = MakeWeapon<UMSRangedWeaponData>(Shooter, MSTags::Weapon_Category_MachineGun);
	ReloadWeapon->MagazineSize = 2;
	ReloadWeapon->ReserveAmmo = 3;
	UMSRangedWeaponData* Slot1 = MakeWeapon<UMSRangedWeaponData>(Shooter, MSTags::Weapon_Category_MachineGun);
	UMSRangedWeaponData* Slot2 = MakeWeapon<UMSRangedWeaponData>(Shooter, MSTags::Weapon_Category_MachineGun);
	Shooter->GetWeaponComponent()->InitializeSlots({ReloadWeapon, Slot1, Slot2});
	TestTrue(TEXT("precondition shot creates reload capacity"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("IA_Reload equivalent starts active-slot reload"), Shooter->TryReloadCurrentWeapon());
	TestTrue(TEXT("weapon component reports reloading"), Shooter->GetWeaponComponent()->IsReloading(0));
	TestTrue(TEXT("IA_Weapon2 equivalent switches to slot 1"), Shooter->TrySwitchWeaponSlot(1));
	TestEqual(TEXT("active slot changed to 1"), Shooter->GetWeaponComponent()->GetActiveSlotIndex(), 1);
	TestTrue(TEXT("IA_Weapon3 equivalent switches to slot 2"), Shooter->TrySwitchWeaponSlot(2));
	TestEqual(TEXT("active slot changed to 2"), Shooter->GetWeaponComponent()->GetActiveSlotIndex(), 2);

	// Switching while held must cancel before UMSWeaponComponent changes the slot.
	Shooter->GetWeaponComponent()->InitializeSlots({BeamRifle, Slot1});
	TestTrue(TEXT("switch-cancel charge starts"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("switch away from charge succeeds"), Shooter->TrySwitchWeaponSlot(1));
	TestFalse(TEXT("switch cancels charge"), Shooter->IsChargeFireActive());
	Shooter->GetWeaponComponent()->InitializeSlots({Beam, Slot1});
	TestTrue(TEXT("switch-cancel beam starts"), Shooter->TryFireCurrentWeapon());
	TestTrue(TEXT("switch away from beam succeeds"), Shooter->TrySwitchWeaponSlot(1));
	TestFalse(TEXT("switch stops beam"), Shooter->IsContinuousBeamFireActive());

	return true;
}

bool FMSHomingWeaponFireTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World, FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Target = SpawnPlayer(TestWorld.World, FVector(1000.0f, 400.0f, 100.0f), MSTeam::EnemyTeamId());

	UMSHomingWeaponData* Homing = MakeWeapon<UMSHomingWeaponData>(TestWorld.World, MSTags::Weapon_Category_Homing);
	Homing->MagazineSize = 2;
	Homing->Range = 30.0f;
	Homing->LockOnRange = 30.0f;
	Homing->ProjectileSpeed = 5.0f;
	Homing->HomingTurnRateDegPerSec = 90.0f;
	Equip(Shooter, Homing);
	TickWeaponInputTestWorld(TestWorld.World, 0.02f);
	// #220: 捕捉サークル(半角8.04°)の内側 7.0° へ配置。横方向のズレは残すので
	// 「ミサイルが横へ曲がる」検証はそのまま成立する。
	PlaceAimPointInHomingCircle(Shooter, Target, 4000.0f, 490.0f);
	TickWeaponInputTestWorld(TestWorld.World, 0.02f);

	TestEqual(TEXT("Homing is never routed to Unsupported"),
		MSWeaponAbilityRouting::Resolve(Homing), EMSWeaponAbilityRoute::Homing);

	// 実プレイ仕様: 円内に入っただけでは即時ロックしない。dwell(0.8s)の継続捕捉で完了する。
	UMSLockOnComponent* LockOn = Shooter->GetLockOnComponent();
	TestNull(TEXT("entering the homing circle does not capture instantly"),
		LockOn->GetHomingCapturedTarget());
	TickWeaponInputTestWorld(TestWorld.World, 0.4f);
	TestNull(TEXT("mid-dwell (0.4s) has not captured yet"), LockOn->GetHomingCapturedTarget());
	TestTrue(FString::Printf(TEXT("mid-dwell ring progress is partial (%.2f)"), LockOn->GetHomingDwellProgress()),
		LockOn->GetHomingDwellProgress() > 0.2f && LockOn->GetHomingDwellProgress() < 0.9f);
	TickWeaponInputTestWorld(TestWorld.World, 0.5f);
	TestEqual(TEXT("enemy tracked through the full dwell is captured without right click"),
		LockOn->GetHomingCapturedTarget(), static_cast<AActor*>(Target));
	TestEqual(TEXT("captured target reports full ring progress"), LockOn->GetHomingDwellProgress(), 1.0f);
	TestFalse(TEXT("homing auto-capture does not activate normal camera lock"),
		LockOn->IsLockedOn());

	// ロック完了後に円(捕捉サークル)から外れたら捕捉解除、再度円内に入れてもdwellをやり直す
	const FVector TargetOriginalLocation = Target->GetActorLocation();
	Target->SetActorLocation(Shooter->GetActorLocation() - FVector(3000.0f, 0.0f, 0.0f)); // 背後=円外
	TickWeaponInputTestWorld(TestWorld.World, 0.1f);
	TestNull(TEXT("captured target leaving the circle releases the capture"),
		LockOn->GetHomingCapturedTarget());
	Target->SetActorLocation(TargetOriginalLocation);
	TickWeaponInputTestWorld(TestWorld.World, 0.15f);
	TestNull(TEXT("re-entering the circle restarts the dwell instead of instantly re-capturing"),
		LockOn->GetHomingCapturedTarget());
	TickWeaponInputTestWorld(TestWorld.World, 0.8f);
	TestEqual(TEXT("full dwell after re-entry captures again"),
		LockOn->GetHomingCapturedTarget(), static_cast<AActor*>(Target));

	TestTrue(TEXT("Homing weapon fires"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("Homing shot consumes ammo"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 1);

	AMSVisualProjectile* Projectile = nullptr;
	for (TActorIterator<AMSVisualProjectile> It(TestWorld.World); It; ++It)
	{
		if (It->GetOwner() == Shooter && It->IsHomingFlight())
		{
			Projectile = *It;
			break;
		}
	}
	TestNotNull(TEXT("Homing spawns an AMSVisualProjectile"), Projectile);
	if (Projectile)
	{
		TestEqual(TEXT("Aim-cone enemy becomes the homing target"), Projectile->GetHomingTarget(), static_cast<AActor*>(Target));
		const FVector Start = Projectile->GetActorLocation();
		// #220で対象が捕捉サークル内(7.0°)へ移った分、必要な旋回量が小さい。
		// 「直線補間ではなく曲線」を見るための観測窓は旋回完了前(7°/90°/s≒0.078s)に取る。
		TickWeaponInputTestWorld(TestWorld.World, 0.06f);
		const FVector Travel = Projectile->GetActorLocation() - Start;
		const FVector ToTarget = Target->GetActorLocation() - Start;
		TestTrue(TEXT("Missile turns laterally toward the target"), Travel.Y > 1.0f && Projectile->GetActorForwardVector().Y > 0.0f);
		TestTrue(TEXT("Missile follows a curved path rather than direct linear interpolation"),
			Travel.X > 1.0f && Travel.Y / Travel.X < (ToTarget.Y / ToTarget.X) * 0.75f);
	}
	return true;
}

bool FMSHomingSalvoTargetPersistenceTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter=SpawnPlayer(TestWorld.World,FVector(0,0,100),MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Target=SpawnPlayer(TestWorld.World,FVector(1000,200,100),MSTeam::EnemyTeamId());
	UMSHomingWeaponData* Homing=MakeWeapon<UMSHomingWeaponData>(TestWorld.World,MSTags::Weapon_Category_Homing);
	Homing->MagazineSize=3;Homing->ReserveAmmo=0;Homing->Range=30.0f;Homing->LockOnRange=30.0f;
	Homing->ProjectileSpeed=5.0f;Homing->HomingTurnRateDegPerSec=90.0f;Homing->BurstInterval=0.05f;Homing->ReloadTime=5.0f;
	Equip(Shooter,Homing);
	// #220: 捕捉サークル(半角8.04°)の内側へ配置(約4.3°)。
	PlaceAimPointInHomingCircle(Shooter,Target,4000.0f,300.0f);
	TickWeaponInputTestWorld(TestWorld.World,0.9f);
	TestEqual(TEXT("precondition: target is captured before the salvo"),Shooter->GetLockOnComponent()->GetHomingCapturedTarget(),static_cast<AActor*>(Target));
	TestTrue(TEXT("first homing round starts the salvo"),Shooter->TryFireCurrentWeapon());

	Target->SetActorLocation(FVector(-3000,0,100));
	TickWeaponInputTestWorld(TestWorld.World,0.2f);
	TestNull(TEXT("HUD capture is released after the target leaves the large circle"),Shooter->GetLockOnComponent()->GetHomingCapturedTarget());
	int32 SalvoProjectileCount=0;
	for(TActorIterator<AMSVisualProjectile> It(TestWorld.World);It;++It)
	{
		if(It->GetOwner()!=Shooter)continue;
		++SalvoProjectileCount;
		TestTrue(TEXT("every salvo round remains a homing projectile"),It->IsHomingFlight());
		TestEqual(TEXT("every salvo round keeps the first round's target outside the circle"),It->GetHomingTarget(),static_cast<AActor*>(Target));
	}
	TestEqual(TEXT("all three delayed rounds were spawned"),SalvoProjectileCount,3);
	return true;
}

bool FMSFullAutoWeaponInputTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World, FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());
	SpawnPlayer(TestWorld.World, FVector(800.0f, 0.0f, 100.0f), MSTeam::EnemyTeamId());

	UMSRangedWeaponData* MachineGun = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	MachineGun->MagazineSize = 3;
	MachineGun->FireInterval = 0.1f;
	Equip(Shooter, MachineGun);
	TestTrue(TEXT("Fire press fires the first machine-gun round"), Shooter->StartCurrentWeaponFire());
	TestTrue(TEXT("Machine gun enters automatic-fire state while held"), Shooter->IsAutomaticFireActive());
	TickWeaponInputTestWorld(TestWorld.World, 0.35f);
	TestEqual(TEXT("FireRate interval produces multiple shots while held"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 0);
	TestFalse(TEXT("Automatic fire stops itself when the magazine is empty"), Shooter->IsAutomaticFireActive());

	UMSRangedWeaponData* SecondSlot = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	MachineGun->MagazineSize = 5;
	Shooter->GetWeaponComponent()->InitializeSlots({MachineGun, SecondSlot});
	TestTrue(TEXT("Automatic fire restarts after re-equip"), Shooter->StartCurrentWeaponFire());
	TestTrue(TEXT("Switching weapon succeeds while fire is held"), Shooter->TrySwitchWeaponSlot(1));
	TestFalse(TEXT("Weapon switching clears the automatic-fire timer"), Shooter->IsAutomaticFireActive());

	UMSRangedWeaponData* Charge = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_BeamRifle);
	Charge->bIsChargeable = true;
	Equip(Shooter, Charge);
	TestTrue(TEXT("Charge still starts on press"), Shooter->StartCurrentWeaponFire());
	TestFalse(TEXT("Charge does not use the automatic-fire timer"), Shooter->IsAutomaticFireActive());
	Shooter->StopCurrentWeaponFire();

	UMSContinuousBeamWeaponData* Beam = MakeWeapon<UMSContinuousBeamWeaponData>(TestWorld.World, MSTags::Weapon_Category_ContinuousBeam);
	Equip(Shooter, Beam);
	TestTrue(TEXT("Continuous beam still starts on press"), Shooter->StartCurrentWeaponFire());
	TestFalse(TEXT("Continuous beam keeps its dedicated held-fire control"), Shooter->IsAutomaticFireActive());
	Shooter->StopCurrentWeaponFire();
	return true;
}

// ユーザーレビュー#68: 連装(Homing)武器は1トリガーでマガジン内全弾が
// 発射間隔ごとに自動連続発射される(押しっぱなし不要)。
bool FMSHomingSalvoFireTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World, FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());

	auto MakeSalvoWeapon = [&TestWorld]()
	{
		UMSHomingWeaponData* Weapon = MakeWeapon<UMSHomingWeaponData>(TestWorld.World, MSTags::Weapon_Category_Homing);
		Weapon->MagazineSize = 4;
		Weapon->ReserveAmmo = 4;
		Weapon->ProjectileSpeed = 25.0f;
		Weapon->BurstInterval = 0.05f;
		Weapon->ReloadTime = 5.0f;
		return Weapon;
	};

	// --- 1トリガーで全弾発射(時間差) ---
	Equip(Shooter, MakeSalvoWeapon());
	TestTrue(TEXT("1クリック目で初弾が出る"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("初弾直後は残弾3(全弾同時発射ではない)"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);
	TestFalse(TEXT("サルボ中の再トリガーは割り込めない"), Shooter->TryFireCurrentWeapon());
	// TimerManagerはワールドのタイマー時刻基準でスケジュールされるため1tick分の許容を持たせる
	// (MSBurstAbilityTestsと同方針)。0.12s時点=2〜3発目まで発射済み・全弾は未発射。
	TickWeaponInputTestWorld(TestWorld.World, 0.12f);
	const int32 MidSalvoAmmo = Shooter->GetWeaponComponent()->GetMagazineAmmo(0);
	TestTrue(FString::Printf(TEXT("BurstInterval経過で追加弾が時間差発射される(残弾%d)"), MidSalvoAmmo),
		MidSalvoAmmo < 3 && MidSalvoAmmo > 0);
	TickWeaponInputTestWorld(TestWorld.World, 0.40f);
	TestEqual(TEXT("押しっぱなし無しでマガジン全弾が発射される"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 0);
	TestTrue(TEXT("撃ち切り後は自動リロードが始まる"), Shooter->GetWeaponComponent()->IsReloading(0));
	int32 ProjectileCount = 0;
	for (TActorIterator<AMSVisualProjectile> It(TestWorld.World); It; ++It)
	{
		if (It->GetOwner() == Shooter) ++ProjectileCount;
	}
	TestEqual(TEXT("4発ぶんの誘導弾がスポーンされている"), ProjectileCount, 4);

	// --- 武器切替でサルボ中断 ---
	UMSRangedWeaponData* SideArm = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	Shooter->GetWeaponComponent()->InitializeSlots({MakeSalvoWeapon(), SideArm});
	TestTrue(TEXT("切替テスト: 初弾発射"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("切替テスト: 初弾後残弾3"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);
	TestTrue(TEXT("サルボ中でも武器切替できる"), Shooter->TrySwitchWeaponSlot(1));
	TickWeaponInputTestWorld(TestWorld.World, 0.5f);
	TestEqual(TEXT("武器切替でサルボが中断され残弾は減らない"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);

	// --- 死亡でサルボ中断 ---
	Shooter->GetWeaponComponent()->InitializeSlots({MakeSalvoWeapon()});
	TestTrue(TEXT("死亡テスト: 初弾発射"), Shooter->TryFireCurrentWeapon());
	Shooter->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TickWeaponInputTestWorld(TestWorld.World, 0.5f);
	TestEqual(TEXT("死亡でサルボが中断され残弾は減らない"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 3);
	Shooter->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Dead, 0);

	// --- 単発(マガジン1)の武器は従来どおり1発で終わる ---
	UMSHomingWeaponData* Single = MakeWeapon<UMSHomingWeaponData>(TestWorld.World, MSTags::Weapon_Category_Homing);
	Single->MagazineSize = 1;
	Single->ReserveAmmo = 2;
	Single->ProjectileSpeed = 25.0f;
	Single->ReloadTime = 5.0f;
	Equip(Shooter, Single);
	TestTrue(TEXT("単発武器も発射できる"), Shooter->TryFireCurrentWeapon());
	TestEqual(TEXT("単発武器はサルボ化しない"), Shooter->GetWeaponComponent()->GetMagazineAmmo(0), 0);
	return true;
}

// ユーザーレビュー#71: スコープ武器(スナイパー系)装備中の右クリックは
// ロックオントグルではなくスコープズームのトグル。他武器は従来どおり。
bool FMSSniperScopeInputTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	AMSPlayerCharacter* Shooter = SpawnPlayer(TestWorld.World, FVector(0.0f, 0.0f, 100.0f), MSTeam::PlayerTeamId());
	AMSPlayerCharacter* OtherPlayer = SpawnPlayer(TestWorld.World, FVector(800.0f, 0.0f, 100.0f), MSTeam::EnemyTeamId());
	APlayerController* LocalController = TestWorld.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("scope owner controller"), LocalController)) return false;
	AddExpectedError(TEXT("CreateWidget"), EAutomationExpectedErrorFlags::Contains, 1);
	LocalController->SetAsLocalPlayerController();
	LocalController->Possess(Shooter);
	TestTrue(TEXT("scope fixture has a locally controlled pawn"), Shooter->IsLocallyControlled());
	OtherPlayer->GetMSCamera()->SetScoped(true, 20.0f, 0.3f);


	UMSRangedWeaponData* Sniper = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	Sniper->bIsScopeWeapon = true;
	Sniper->ScopedFieldOfView = 20.0f;
	Sniper->ScopedSensitivityMultiplier = 0.3f;
	UMSRangedWeaponData* Rifle = MakeWeapon<UMSRangedWeaponData>(TestWorld.World, MSTags::Weapon_Category_MachineGun);
	Shooter->GetWeaponComponent()->InitializeSlots({Sniper, Rifle});

	UMSCameraComponent* Camera = Shooter->GetMSCamera();
	UMSLockOnComponent* LockOn = Shooter->GetLockOnComponent();

	// スナイパー装備中: 右クリック=スコープON(ロックオンは発動しない)
	Shooter->ToggleLockOnOrScope();
	TestTrue(TEXT("スコープ武器の右クリックでIsScoped=true"), Camera->IsScoped());
	TestFalse(TEXT("スコープ武器の右クリックはロックオンを発動しない"), LockOn->IsLockedOn());
	TestEqual(TEXT("スコープ中は感度が武器データの倍率へ低下"), Camera->GetLookSensitivityMultiplier(), 0.3f);

	// 再押下でスコープ解除
	Shooter->ToggleLockOnOrScope();
	TestFalse(TEXT("再押下でスコープ解除"), Camera->IsScoped());
	TestEqual(TEXT("解除で感度が通常へ戻る"), Camera->GetLookSensitivityMultiplier(), 1.0f);

	// スコープ中に武器を持ち替えたらスコープ解除
	Shooter->ToggleLockOnOrScope();
	TestTrue(TEXT("持ち替えテスト: スコープON"), Camera->IsScoped());
	TestTrue(TEXT("スコープ中でも武器切替できる"), Shooter->TrySwitchWeaponSlot(1));
	TestFalse(TEXT("非スコープ武器への持ち替えでスコープ解除"), Camera->IsScoped());
	TestTrue(TEXT("local switch does not alter another pawn's camera"), OtherPlayer->GetMSCamera()->IsScoped());

	// スナイパー以外の武器: 右クリック=従来どおりロックオントグル
	Shooter->ToggleLockOnOrScope();
	TestFalse(TEXT("通常武器の右クリックではスコープしない"), Camera->IsScoped());
	TestTrue(TEXT("通常武器の右クリックはロックオンをトグルする"), LockOn->IsLockedOn());
	Shooter->ToggleLockOnOrScope();
	TestFalse(TEXT("再押下でロックオン解除"), LockOn->IsLockedOn());
	return true;
}

bool FMSCrouchReleaseInputTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	AMSPlayerCharacter* Player = SpawnPlayer(TestWorld.World, FVector::ZeroVector, MSTeam::PlayerTeamId());
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();

	Player->ToggleCrouch();
	TestTrue(TEXT("Crouch toggle adds State.Crouching"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Player->ApplyMovementInput(FVector2D::ZeroVector);
	TestTrue(TEXT("Zero movement input does not release crouch"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
	TestFalse(TEXT("Non-zero movement input releases State.Crouching"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));

	Player->ToggleCrouch();
	TestTrue(TEXT("Crouch can still be toggled on again"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Player->BeginHoverInput();
	TestFalse(TEXT("Jump/hover input releases State.Crouching"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	Player->ToggleCrouch();
	Player->ToggleCrouch();
	TestFalse(TEXT("Crouch key re-press still toggles crouch off"), ASC->HasMatchingGameplayTag(MSTags::State_Crouching));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRetainedPawnFireCancellationTest,
	"NeoMSGO.Player.RetainedPawnFireCancellation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSRetainedPawnFireCancellationTest::RunTest(const FString& Parameters)
{
	FPlayerWeaponTestWorld TestWorld;
	SpawnGround(TestWorld.World);
	// Invoke the same suspension hook used by online PawnLeavingGame while the
	// pawn stays alive. This tests teardown, not a synthetic successful connection.
	AMSPlayerCharacter* SalvoShooter = SpawnPlayer(TestWorld.World, FVector(0, 0, 100), MSTeam::PlayerTeamId());
	UMSHomingWeaponData* Homing = MakeWeapon<UMSHomingWeaponData>(TestWorld.World, MSTags::Weapon_Category_Homing);
	Homing->MagazineSize = 4;
	Homing->ReserveAmmo = 0;
	Homing->BurstInterval = 0.05f;
	Homing->ProjectileSpeed = 25.0f;
	Equip(SalvoShooter, Homing);
	TestTrue(TEXT("salvo starts before connection suspension"), SalvoShooter->TryFireCurrentWeapon());
	const int32 SalvoAmmo = SalvoShooter->GetWeaponComponent()->GetMagazineAmmo(0);
	SalvoShooter->SuspendOnlineInputs();
	TickWeaponInputTestWorld(TestWorld.World, 0.4f);
	TestFalse(TEXT("retained pawn was not destroyed to hide the timer bug"), SalvoShooter->IsActorBeingDestroyed());
	TestEqual(TEXT("suspended retained pawn emits no further salvo rounds"), SalvoShooter->GetWeaponComponent()->GetMagazineAmmo(0), SalvoAmmo);

	AMSPlayerCharacter* BeamShooter = SpawnPlayer(TestWorld.World, FVector(0, 4000, 100), MSTeam::PlayerTeamId());
	UMSContinuousBeamWeaponData* Beam = MakeWeapon<UMSContinuousBeamWeaponData>(TestWorld.World, MSTags::Weapon_Category_ContinuousBeam);
	Beam->MagazineSize = 10;
	Beam->ReserveAmmo = 0;
	Beam->BeamTickInterval = 0.05f;
	Equip(BeamShooter, Beam);
	TestTrue(TEXT("beam starts before connection suspension"), BeamShooter->TryFireCurrentWeapon());
	TestTrue(TEXT("fixture has a running held beam"), BeamShooter->IsContinuousBeamFireActive());
	const int32 BeamAmmo = BeamShooter->GetWeaponComponent()->GetMagazineAmmo(0);
	BeamShooter->SuspendOnlineInputs();
	TestFalse(TEXT("suspension immediately clears beam state"), BeamShooter->IsContinuousBeamFireActive());
	TickWeaponInputTestWorld(TestWorld.World, 0.4f);
	TestEqual(TEXT("suspended retained pawn spends no further beam ammunition"), BeamShooter->GetWeaponComponent()->GetMagazineAmmo(0), BeamAmmo);
	return true;
}

#endif
