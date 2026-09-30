// Copyright NeoMSGO Project. All Rights Reserved.
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	float GetExplosionVisualInputRadiusCm(const UMSRangedWeaponData* Weapon)
	{
		if (!Weapon || Weapon->SplashRadius <= 0.0f)
			return UMSGameplayCue_Explosion::ExplosionVisualRadiusCm;
		// SpawnExplosion applies MSScale. This input preserves the actual
		// gameplay radius for either legacy or physical-metre weapon data.
		return Weapon->SplashRadius * 100.0f * Weapon->RangeWorldScale / MSGO::MSScale;
	}
}

UMSGameplayCue_Explosion::UMSGameplayCue_Explosion()
{
	GameplayCueTag = MSTags::Cue_Explosion;
	GameplayCueName = GameplayCueTag.GetTagName();
	static ConstructorHelpers::FObjectFinder<USoundBase> UnitFinder(
		TEXT("/Game/Audio/Imported/SFX_Explosion_Unit.SFX_Explosion_Unit"));
	static ConstructorHelpers::FObjectFinder<USoundBase> OrdnanceFinder(
		TEXT("/Game/Audio/Imported/SFX_Explosion_Ordnance.SFX_Explosion_Ordnance"));
	UnitExplosionSound = UnitFinder.Object;
	OrdnanceExplosionSound = OrdnanceFinder.Object;
}
bool UMSGameplayCue_Explosion::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	USoundBase* TypeASound = ResolveExplosionSound(Parameters.SourceObject.Get());
	MSAudio::SpawnMetaSoundAtLocation(
		MyTarget, MSAudioPreset::Resolve(TypeASound == OrdnanceExplosionSound
			? EMSAudioUsage::ExplosionSmall : EMSAudioUsage::ExplosionLarge, TypeASound),
		FVector(Parameters.Location), 1000.0f, 0.34f, 0.34f);
	if (UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr)
	{
		const FVector Center(Parameters.Location);
		const UMSRangedWeaponData* RangedWeapon = Cast<UMSRangedWeaponData>(Parameters.SourceObject.Get());
		const float VisualRadiusCm = GetExplosionVisualInputRadiusCm(RangedWeapon);
		// 恒久実装: 実行時生成StaticMeshComponent(発光Cube)による爆発。Shipping相当でも描画される
		// (AMSTransientVfxActor::SpawnExplosion)。膨張フェーズの寿命はB-7準拠で0.04〜0.06sに収める。
		AMSTransientVfxActor::SpawnExplosion(
			World, Center, VisualRadiusCm, ExplosionMaxDurationSeconds);
	}
	return MyTarget != nullptr;
}

USoundBase* UMSGameplayCue_Explosion::ResolveExplosionSound(const UObject* SourceObject) const
{
	// Projectile callers provide their weapon data. Unit-death callers provide the
	// destroyed actor, and legacy/no-source callers therefore safely default to unit.
	return Cast<UMSWeaponDataBase>(SourceObject) ? OrdnanceExplosionSound : UnitExplosionSound;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSExplosionVisualScaleTest,
	"NeoMSGO.Combat.WeaponGameplayCues.ExplosionVisualScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSExplosionVisualScaleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("explosion visual radius is enlarged from 55 cm to 90 cm"),
		FMath::IsNearlyEqual(UMSGameplayCue_Explosion::ExplosionVisualRadiusCm, 90.0f));
	TestTrue(TEXT("explosion outer shell initial dimensions follow MSScale"),
		AMSTransientVfxActor::CalculateExplosionOuterInitialScale(
			UMSGameplayCue_Explosion::ExplosionVisualRadiusCm).Equals(FVector(0.45f * MSGO::MSScale), 0.001f));
	TestTrue(TEXT("explosion outer shell maximum dimensions follow MSScale"),
		AMSTransientVfxActor::CalculateExplosionOuterMaximumScale(
			UMSGameplayCue_Explosion::ExplosionVisualRadiusCm).Equals(FVector(1.8f * MSGO::MSScale), 0.001f));
	UMSRangedWeaponData* Legacy = NewObject<UMSRangedWeaponData>();
	Legacy->SplashRadius = 3.0f;
	TestTrue(TEXT("legacy explosion visual remains the scaled gameplay radius"),
		FMath::IsNearlyEqual(GetExplosionVisualInputRadiusCm(Legacy) * MSGO::MSScale,
			Legacy->SplashRadius * 100.0f * Legacy->RangeWorldScale, 0.1f));
	UMSRangedWeaponData* Physical = NewObject<UMSRangedWeaponData>();
	Physical->SplashRadius = 30.0f;
	Physical->RangeWorldScale = 1.0f;
	TestTrue(TEXT("physical 30m explosion visual follows the 3000cm gameplay radius"),
		FMath::IsNearlyEqual(GetExplosionVisualInputRadiusCm(Physical) * MSGO::MSScale, 3000.0f, 0.1f));
	TestTrue(TEXT("B-7 minimum lifetime remains 0.04 seconds"),
		FMath::IsNearlyEqual(UMSGameplayCue_Explosion::ExplosionMinDurationSeconds, 0.04f));
	TestTrue(TEXT("B-7 maximum lifetime remains 0.06 seconds"),
		FMath::IsNearlyEqual(UMSGameplayCue_Explosion::ExplosionMaxDurationSeconds, 0.06f));
	return true;
}

#endif
