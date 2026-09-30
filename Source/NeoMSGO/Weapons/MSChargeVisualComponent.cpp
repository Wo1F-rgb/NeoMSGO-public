// Copyright NeoMSGO Project. All Rights Reserved.

#include "Weapons/MSChargeVisualComponent.h"

#include "Combat/MSUnitBase.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

namespace
{
	// World centimetres: the standard shoulder camera sits about 42 m behind an
	// 18 m suit. The soft radial material needs a broad halo plus a bright core.
	constexpr float HaloStartHalfSizeCm = 140.0f;
	constexpr float HaloFullHalfSizeCm = 360.0f;
	constexpr float CoreStartHalfSizeCm = 35.0f;
	constexpr float CoreFullHalfSizeCm = 90.0f;
	const FName ColorParameter(TEXT("Color"));
	const FName IntensityParameter(TEXT("Intensity"));
	constexpr const TCHAR* GlowMaterialPath =
		TEXT("/Game/Textures/VFX/Charge/M_ChargeGlow.M_ChargeGlow");
}

UMSChargeVisualComponent::UMSChargeVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UMSChargeVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer || !Owner->GetRootComponent())
	{
		SetComponentTickEnabled(false);
		return;
	}

	const AMSUnitBase* Unit = Cast<AMSUnitBase>(Owner);
	WeaponComponent = Unit ? Unit->GetWeaponComponent() : nullptr;
	WeaponVisual = Unit ? Unit->GetWeaponVisualComponent() : nullptr;
	if (!WeaponComponent || !WeaponVisual)
	{
		SetComponentTickEnabled(false);
		return;
	}
	AddTickPrerequisiteComponent(WeaponVisual);

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath);
	if (!BaseMaterial)
	{
		SetComponentTickEnabled(false);
		return;
	}
	GlowMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	CoreMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (!GlowMaterial || !CoreMaterial)
	{
		SetComponentTickEnabled(false);
		return;
	}

	GlowBillboard = NewObject<UMaterialBillboardComponent>(Owner,
		TEXT("WeaponCharge_MuzzleGlow"), RF_Transient);
	Owner->AddInstanceComponent(GlowBillboard);
	GlowBillboard->SetupAttachment(Owner->GetRootComponent());
	GlowBillboard->SetAbsolute(true, true, true);
	GlowBillboard->SetMobility(EComponentMobility::Movable);
	GlowBillboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlowBillboard->SetGenerateOverlapEvents(false);
	GlowBillboard->SetCastShadow(false);
	GlowBillboard->AddElement(GlowMaterial, nullptr, false,
		HaloStartHalfSizeCm, HaloStartHalfSizeCm, nullptr);
	GlowBillboard->AddElement(CoreMaterial, nullptr, false,
		CoreStartHalfSizeCm, CoreStartHalfSizeCm, nullptr);
	GlowBillboard->SetVisibility(false);
	GlowBillboard->RegisterComponent();
	UpdateGlow();
}

void UMSChargeVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideGlow();
	Super::EndPlay(EndPlayReason);
}

void UMSChargeVisualComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateGlow();
}

FVector UMSChargeVisualComponent::GetGlowWorldLocation() const
{
	return GlowBillboard ? GlowBillboard->GetComponentLocation() : FVector::ZeroVector;
}

void UMSChargeVisualComponent::HideGlow()
{
	bGlowVisible = false;
	GlowScale = 0.0f;
	DisplayedWeapon.Reset();
	if (GlowBillboard)
	{
		GlowBillboard->SetVisibility(false);
	}
}

FLinearColor UMSChargeVisualComponent::ResolveChargeColor(const UMSRangedWeaponData* Weapon) const
{
	if (Weapon->CategoryTag.MatchesTagExact(MSTags::Weapon_Category_Bazooka))
	{
		return FLinearColor(1.0f, 0.55f, 0.13f);
	}
	const FLinearColor Authored = Weapon->TracerColor;
	return Authored.IsAlmostBlack() || Authored.Equals(FLinearColor::White, 0.01f)
		? FLinearColor(0.22f, 0.75f, 1.0f) : Authored;
}

void UMSChargeVisualComponent::UpdateGlow()
{
	if (!GlowBillboard || !GlowMaterial || !CoreMaterial || !WeaponComponent || !WeaponVisual
		|| !WeaponComponent->IsChargeSnapshotActive())
	{
		HideGlow();
		return;
	}
	const int32 SlotIndex = WeaponComponent->GetActiveSlotIndex();
	const UMSRangedWeaponData* Weapon = Cast<UMSRangedWeaponData>(
		WeaponComponent->GetWeaponData(SlotIndex));
	if (!Weapon || !Weapon->bIsChargeable || Weapon->MaxChargeTime <= 0.0f)
	{
		HideGlow();
		return;
	}
	if (DisplayedWeapon.Get() != Weapon)
	{
		const FLinearColor ChargeColor = ResolveChargeColor(Weapon);
		GlowMaterial->SetVectorParameterValue(ColorParameter, ChargeColor);
		CoreMaterial->SetVectorParameterValue(ColorParameter,
			FMath::Lerp(ChargeColor, FLinearColor::White, 0.75f));
		DisplayedWeapon = Weapon;
	}

	const float Progress = WeaponComponent->GetChargeProgress01();
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const bool bFullyCharged = Progress >= 1.0f;
	const float Pulse = bFullyCharged ? 1.0f + 0.12f * FMath::Sin(Time * 9.0f) : 1.0f;
	const float HaloHalfSize = FMath::Lerp(HaloStartHalfSizeCm, HaloFullHalfSizeCm, Progress) * Pulse;
	const float CoreHalfSize = FMath::Lerp(CoreStartHalfSizeCm, CoreFullHalfSizeCm, Progress) * Pulse;
	GlowScale = HaloHalfSize / HaloStartHalfSizeCm;
	GlowMaterial->SetScalarParameterValue(IntensityParameter,
		FMath::Lerp(1.5f, 5.0f, Progress) * Pulse);
	CoreMaterial->SetScalarParameterValue(IntensityParameter,
		FMath::Lerp(5.0f, 14.0f, Progress) * (bFullyCharged ? 1.35f : 1.0f) * Pulse);
	// MaterialBillboard's inverse-transformed camera basis cancels component
	// scale. Change the actual sprite dimensions, including its culling bounds.
	FMaterialSpriteElement& Halo = GlowBillboard->Elements[0];
	FMaterialSpriteElement& Core = GlowBillboard->Elements[1];
	if (!FMath::IsNearlyEqual(Halo.BaseSizeX, HaloHalfSize)
		|| !FMath::IsNearlyEqual(Core.BaseSizeX, CoreHalfSize))
	{
		Halo.BaseSizeX = Halo.BaseSizeY = HaloHalfSize;
		Core.BaseSizeX = Core.BaseSizeY = CoreHalfSize;
		GlowBillboard->UpdateBounds();
		GlowBillboard->MarkRenderStateDirty();
	}
	GlowBillboard->SetWorldLocation(WeaponVisual->GetMuzzleWorldLocation());
	GlowBillboard->SetVisibility(true);
	bGlowVisible = true;
}
