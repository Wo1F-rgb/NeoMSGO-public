// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/Cues/MSGameplayCue_Tracer.h"

#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSWeaponVisualComponent.h"

UMSGameplayCue_Tracer::UMSGameplayCue_Tracer()
{
	GameplayCueTag = MSTags::Cue_Weapon_Tracer;
	GameplayCueName = GameplayCueTag.GetTagName();
}

const UMSRangedWeaponData* UMSGameplayCue_Tracer::ResolveWeaponData(const FGameplayCueParameters& Parameters)
{
	return Cast<UMSRangedWeaponData>(Parameters.SourceObject.Get());
}

bool UMSGameplayCue_Tracer::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// データ駆動(P4-12 AC1): 武器データが取得できればそこから、できなければ
	// UMSRangedWeaponDataのデフォルト値(White/0.04m/0.12s)にフォールバックする。
	const UMSRangedWeaponData* Weapon = ResolveWeaponData(Parameters);
	const UMSRangedWeaponData* DefaultsSource = Weapon ? Weapon : GetDefault<UMSRangedWeaponData>();
	const FLinearColor Color = DefaultsSource->TracerColor;
	const float WidthMeters = DefaultsSource->TracerWidth;
	const float Lifetime = DefaultsSource->TracerLifetime;

	// 始点: 攻撃発生位置(GA_Fire_HitscanがContextに設定済み)。未設定ならInstigator位置にフォールバック。
	FVector Start = FVector::ZeroVector;
	bool bHasStart = false;
	if (const FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(Parameters.EffectContext))
	{
		if (MSContext->HasAttackerLocation())
		{
			Start = MSContext->GetAttackerLocation();
			bHasStart = true;
		}
	}
	if (!bHasStart)
	{
		if (const AActor* InstigatorActor = Parameters.Instigator.Get())
		{
			Start = InstigatorActor->GetActorLocation();
			bHasStart = true;
		}
		else if (MyTarget)
		{
			Start = MyTarget->GetActorLocation();
			bHasStart = true;
		}
	}
	if (const AMSUnitBase* Unit = Cast<AMSUnitBase>(Parameters.Instigator.Get()))
	{
		if (const UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent())
		{
			Start = Visual->GetMuzzleWorldLocation();
			bHasStart = true;
		}
	}

	// 終点: 命中点(Parameters.Location)。P4-13でCue発火経路を配線する際にHit位置が渡される想定。
	const FVector End = FVector(Parameters.Location);

	if (!bHasStart || Start.Equals(End))
	{
		return false;
	}

	// 恒久実装: 実行時生成StaticMeshComponent(発光Cube)+PointLightによる弾道トレーサー。
	// Widthはメートル単位のデータをUnreal単位(cm)へ変換して渡す。Shipping相当でも描画される。
	AMSTransientVfxActor::SpawnTracer(const_cast<UWorld*>(World), Start, End, Color,
		WidthMeters * 100.0f * MSGO::MSScale, FMath::Max(0.02f, Lifetime));

	return true;
}
