// Copyright NeoMSGO Project. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Net/UnrealNetwork.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapons/GA_Fire_Charge.h"
#include "Weapons/MSChargeVisualComponent.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSChargeVisualComponentTest,
	"NeoMSGO.Weapons.ChargeVisualLifecycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FChargeVisualTestWorld
	{
		UWorld* World = nullptr;
		FChargeVisualTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSChargeVisualTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FChargeVisualTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickWorld(UWorld* World, float Seconds)
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

bool FMSChargeVisualComponentTest::RunTest(const FString&)
{
	FChargeVisualTestWorld TestWorld;
	AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
	if (!TestNotNull(TEXT("Unit spawns"), Unit)) return false;
	UMSChargeVisualComponent* Glow = Unit->FindComponentByClass<UMSChargeVisualComponent>();
	if (!TestNotNull(TEXT("weapon visual creates charge presentation"), Glow)
		|| !TestNotNull(TEXT("charge billboard uses the authored glow material"),
			Glow->GetGlowBillboard())) return false;
	UMaterialBillboardComponent* Billboard = Glow->GetGlowBillboard();
	if (!TestEqual(TEXT("halo and core are separate rendered layers"), Billboard->Elements.Num(), 2)) return false;
	TestTrue(TEXT("layers have independent material parameters"),
		Billboard->Elements[0].Material != Billboard->Elements[1].Material);
	TInlineComponentArray<UMSChargeVisualComponent*> ChargeComponents(Unit);
	TestEqual(TEXT("charge presentation is created only once"), ChargeComponents.Num(), 1);
	TestFalse(TEXT("uncharged weapon has no muzzle glow"), Glow->IsGlowVisible());
	UMSWeaponComponent::StaticClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> ReplicatedProps;
	Unit->GetWeaponComponent()->GetLifetimeReplicatedProps(ReplicatedProps);
	const FProperty* ChargeProperty = FindFProperty<FProperty>(
		UMSWeaponComponent::StaticClass(), TEXT("ChargeSnapshot"));
	bool bObserverReplicated = false;
	if (ChargeProperty)
	{
		for (const FLifetimeProperty& Prop : ReplicatedProps)
		{
			if (Prop.RepIndex == ChargeProperty->RepIndex)
			{
				bObserverReplicated = Prop.Condition == COND_None;
				break;
			}
		}
	}
	TestTrue(TEXT("charge snapshot is visible to observer clients"), bObserverReplicated);

	UMSRangedWeaponData* ChargeWeapon = NewObject<UMSRangedWeaponData>(Unit);
	ChargeWeapon->CategoryTag = MSTags::Weapon_Category_BeamRifle;
	ChargeWeapon->bIsChargeable = true;
	ChargeWeapon->MaxChargeTime = 1.0f;
	ChargeWeapon->FireInterval = 0.0f;
	ChargeWeapon->MagazineSize = 5;
	ChargeWeapon->Range = 20.0f;
	UMSRangedWeaponData* OtherWeapon = NewObject<UMSRangedWeaponData>(Unit);
	OtherWeapon->CategoryTag = MSTags::Weapon_Category_MachineGun;
	OtherWeapon->MagazineSize = 5;
	OtherWeapon->WeaponSwitchDuration = 0.0f;
	ChargeWeapon->WeaponSwitchDuration = 0.0f;
	UMSWeaponComponent* Weapons = Unit->GetWeaponComponent();
	Weapons->InitializeSlots({ChargeWeapon, OtherWeapon});
	Unit->GetWeaponVisualComponent()->RefreshFromWeaponComponent();
	UGA_Fire_Charge* Ability = NewObject<UGA_Fire_Charge>(Unit);
	Ability->AddToRoot();
	ON_SCOPE_EXIT { Ability->RemoveFromRoot(); };

	const FVector Origin = Unit->GetActorLocation();
	TestTrue(TEXT("charge starts"), Ability->StartCharge(Unit, Origin, FVector::ForwardVector));
	TickWorld(TestWorld.World, 0.25f);
	TestTrue(TEXT("replicated charge snapshot makes glow visible"), Glow->IsGlowVisible());
	TestTrue(TEXT("glow follows actual muzzle position"), Glow->GetGlowWorldLocation().Equals(
		Unit->GetWeaponVisualComponent()->GetMuzzleWorldLocation(), 1.0f));
	const float QuarterScale = Glow->GetGlowScale();
	const float QuarterHaloSize = Billboard->Elements[0].BaseSizeX;
	const float QuarterCoreSize = Billboard->Elements[1].BaseSizeX;
	TickWorld(TestWorld.World, 0.5f);
	TestTrue(TEXT("glow grows as charge progresses"), Glow->GetGlowScale() > QuarterScale);
	TestTrue(TEXT("actual rendered halo grows, not just component scale"),
		Billboard->Elements[0].BaseSizeX > QuarterHaloSize);
	TestTrue(TEXT("actual rendered core grows with charge"),
		Billboard->Elements[1].BaseSizeX > QuarterCoreSize);
	TestTrue(TEXT("billboard uses world size without inherited suit scaling"),
		!Billboard->Elements[0].bSizeIsInScreenSpace && !Billboard->Elements[1].bSizeIsInScreenSpace
		&& Billboard->GetComponentScale().Equals(FVector::OneVector));
	Unit->SetActorLocation(Unit->GetActorLocation() + FVector(200.0f, 70.0f, 0.0f));
	TickWorld(TestWorld.World, 0.01f);
	TestTrue(TEXT("glow tracks a moving muzzle"), Glow->GetGlowWorldLocation().Equals(
		Unit->GetWeaponVisualComponent()->GetMuzzleWorldLocation(), 1.0f));
	TestTrue(TEXT("charge releases"), Ability->ReleaseCharge());
	TickWorld(TestWorld.World, 0.01f);
	TestFalse(TEXT("release hides muzzle glow"), Glow->IsGlowVisible());
	TestFalse(TEXT("release hides the component containing both layers"), Billboard->IsVisible());

	TestTrue(TEXT("second charge starts"), Ability->StartCharge(Unit,
		Unit->GetActorLocation(), FVector::ForwardVector));
	TickWorld(TestWorld.World, 0.01f);
	TestTrue(TEXT("second charge shows glow"), Glow->IsGlowVisible());
	TestTrue(TEXT("weapon switch succeeds"), Weapons->SwitchToSlot(1));
	TickWorld(TestWorld.World, 0.01f);
	TestFalse(TEXT("weapon switch hides muzzle glow"), Glow->IsGlowVisible());

	TestTrue(TEXT("charge weapon can be reselected"), Weapons->SwitchToSlot(0));
	TestTrue(TEXT("third charge starts"), Ability->StartCharge(Unit,
		Unit->GetActorLocation(), FVector::ForwardVector));
	TickWorld(TestWorld.World, 0.01f);
	TestTrue(TEXT("third charge shows glow"), Glow->IsGlowVisible());
	Unit->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Dead);
	TickWorld(TestWorld.World, 0.01f);
	TestFalse(TEXT("death hides muzzle glow"), Glow->IsGlowVisible());
	TestFalse(TEXT("death hides both rendered layers"), Billboard->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSChargeVisualAuthoredWeaponsTest,
	"NeoMSGO.Weapons.ChargeVisualAuthoredWeapons",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSChargeVisualAuthoredWeaponsTest::RunTest(const FString&)
{
	// This verifies authored equipment and render inputs, not gameplay visibility.
	// Normal third-person PIE captures remain required for visual acceptance.
	FChargeVisualTestWorld TestWorld;
	struct FEquipmentCase { const TCHAR* Suit; const TCHAR* Weapon; };
	const FEquipmentCase Cases[] = {
		{TEXT("/Game/Data/MobileSuits/DA_MS_MPU01_ThreeJS.DA_MS_MPU01_ThreeJS"),
		 TEXT("/Game/Data/Weapons/DA_Weapon_ChargeBazookaCB3.DA_Weapon_ChargeBazookaCB3")},
		{TEXT("/Game/Data/MobileSuits/DA_MS_TAUrus.DA_MS_TAUrus"),
		 TEXT("/Game/Data/Weapons/DA_Weapon_TAUrus_ChargeBlasterLauncher.DA_Weapon_TAUrus_ChargeBlasterLauncher")}
	};
	for (const FEquipmentCase& Equipment : Cases)
	{
		UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr, Equipment.Suit);
		UMSRangedWeaponData* Weapon = LoadObject<UMSRangedWeaponData>(nullptr, Equipment.Weapon);
		if (!TestNotNull(Equipment.Suit, Suit) || !TestNotNull(Equipment.Weapon, Weapon)) return false;
		AMSPlayerCharacter* Unit = TestWorld.World->SpawnActor<AMSPlayerCharacter>();
		if (!TestNotNull(TEXT("authored equipment unit spawns"), Unit)) return false;
		Unit->InitializeUnit(Suit);
		Unit->GetWeaponComponent()->InitializeSlots({Weapon});
		Unit->GetWeaponVisualComponent()->RefreshFromWeaponComponent();
		UMSChargeVisualComponent* Glow = Unit->FindComponentByClass<UMSChargeVisualComponent>();
		if (!TestNotNull(TEXT("authored unit has charge presentation"), Glow)) return false;
		UMaterialBillboardComponent* Billboard = Glow->GetGlowBillboard();
		if (!TestNotNull(TEXT("authored unit has billboard"), Billboard)
			|| !TestEqual(TEXT("authored unit has both charge layers"), Billboard->Elements.Num(), 2)) return false;
		UGA_Fire_Charge* Ability = NewObject<UGA_Fire_Charge>(Unit);
		Ability->AddToRoot();
		ON_SCOPE_EXIT { Ability->CancelCharge(); Ability->RemoveFromRoot(); };
		if (!TestTrue(TEXT("authored weapon can start charge"), Ability->StartCharge(Unit,
			Unit->GetActorLocation(), FVector::ForwardVector))) return false;
		TickWorld(TestWorld.World, Weapon->MaxChargeTime * 0.25f);
		TestTrue(TEXT("authored charge enables both layers at actual muzzle"), Billboard->IsVisible()
			&& Glow->GetGlowWorldLocation().Equals(Unit->GetWeaponVisualComponent()->GetMuzzleWorldLocation(), 1.0f));
		const float QuarterHalo = Billboard->Elements[0].BaseSizeX;
		const float QuarterCore = Billboard->Elements[1].BaseSizeX;
		UMaterialInstanceDynamic* HaloMaterial = Cast<UMaterialInstanceDynamic>(Billboard->Elements[0].Material);
		UMaterialInstanceDynamic* CoreMaterial = Cast<UMaterialInstanceDynamic>(Billboard->Elements[1].Material);
		if (!TestNotNull(TEXT("halo has dynamic intensity"), HaloMaterial)
			|| !TestNotNull(TEXT("core has dynamic intensity"), CoreMaterial)) return false;
		const float QuarterIntensity = CoreMaterial->K2_GetScalarParameterValue(TEXT("Intensity"));
		TickWorld(TestWorld.World, Weapon->MaxChargeTime);
		TestEqual(TEXT("authored charge reaches full"), Unit->GetWeaponComponent()->GetChargeProgress01(), 1.0f);
		TestTrue(TEXT("full charge enlarges both real sprite dimensions"),
			Billboard->Elements[0].BaseSizeX > QuarterHalo && Billboard->Elements[1].BaseSizeX > QuarterCore);
		TestTrue(TEXT("full charge brightens its core above the surrounding halo"),
			CoreMaterial->K2_GetScalarParameterValue(TEXT("Intensity")) > QuarterIntensity
			&& CoreMaterial->K2_GetScalarParameterValue(TEXT("Intensity"))
				> HaloMaterial->K2_GetScalarParameterValue(TEXT("Intensity")));
		TestTrue(TEXT("culling bounds follow the enlarged halo"),
			Billboard->Bounds.BoxExtent.GetMin() >= Billboard->Elements[0].BaseSizeX - 0.1f);
		float Smallest = Billboard->Elements[0].BaseSizeX;
		float Largest = Smallest;
		for (int32 Sample = 0; Sample < 8; ++Sample)
		{
			TickWorld(TestWorld.World, 0.05f);
			Smallest = FMath::Min(Smallest, Billboard->Elements[0].BaseSizeX);
			Largest = FMath::Max(Largest, Billboard->Elements[0].BaseSizeX);
		}
		TestTrue(TEXT("holding full charge pulses rendered size"), Largest - Smallest > 5.0f);
		Ability->CancelCharge();
		TickWorld(TestWorld.World, 0.01f);
		TestFalse(TEXT("stopping authored charge hides both layers"), Billboard->IsVisible());
		Unit->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
