// Copyright NeoMSGO Project. All Rights Reserved.

#include "Combat/MSCombatStatics.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSBoostGameplayEffects.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSUnitBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "Gameplay/MSHomeBase.h"
#include "Player/MSCameraComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

namespace
{
	/**
	 * 線分(SegStart→SegEnd)が球(Center, Radius)と交差するか。
	 * Unity版のPhysics.RaycastAllがトレース線上の球コライダーを候補に含めるかの判定に相当する。
	 */
	bool DoesSegmentIntersectSphere(const FVector& SegStart, const FVector& SegEnd, const FVector& Center, const float Radius)
	{
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(Center, SegStart, SegEnd);
		return FVector::DistSquared(ClosestPoint, Center) <= FMath::Square(Radius);
	}

	/**
	 * 爆風中心から実ダメージコリジョンの最近傍表面までの距離を求める。
	 * ActorLocation/Boundsは機体の大きさを反映しないためfalloff用には使わない。
	 */
	bool TryGetSplashSurfaceDistance(const AActor* Actor, const FVector& Origin, float& OutDistance)
	{
		if (!Actor)
		{
			return false;
		}

		float ClosestDistance = TNumericLimits<float>::Max();
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (const UPrimitiveComponent* Component : Components)
		{
			const ECollisionEnabled::Type CollisionEnabled = Component ? Component->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
			if (!Component || (CollisionEnabled != ECollisionEnabled::QueryOnly &&
				CollisionEnabled != ECollisionEnabled::QueryAndPhysics) ||
				Component->GetCollisionResponseToChannel(MSCollision::WeaponTraceChannel) == ECR_Ignore)
			{
				continue;
			}

			FVector ClosestPoint = FVector::ZeroVector;
			const float Distance = Component->GetClosestPointOnCollision(Origin, ClosestPoint);
			if (Distance >= 0.0f && FMath::IsFinite(Distance))
			{
				ClosestDistance = FMath::Min(ClosestDistance, Distance);
			}
		}

		if (ClosestDistance == TNumericLimits<float>::Max())
		{
			return false;
		}

		OutDistance = ClosestDistance;
		return true;
	}

	bool HasCoreCollisionProfile(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (const UPrimitiveComponent* Component : Components)
		{
			if (Component && Component->GetCollisionProfileName() == MSCollision::CoreProfileName)
			{
				return true;
			}
		}
		return false;
	}
}

bool UMSCombatStatics::ShouldIgnoreWeaponTraceOccluder(const AActor* HitActor)
{
	const APhysicsVolume* HitVolume = Cast<APhysicsVolume>(HitActor);
	return HitVolume && HitVolume->bWaterVolume;
}

FMSHitscanResult UMSCombatStatics::ResolveHitscan(
	const UObject* WorldContextObject,
	const FVector& Origin,
	const FVector& Direction,
	const float Range,
	AActor* Shooter)
{
	FMSHitscanResult Result;
	const FVector TraceDirection = Direction.GetSafeNormal();
	Result.EndPoint = Origin + TraceDirection * FMath::Max(0.0f, Range);

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || TraceDirection.IsNearlyZero() || Range <= 0.0f)
	{
		return Result;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSResolveHitscan), false);
	QueryParams.bReturnPhysicalMaterial = true;
	if (Shooter)
	{
		// Ignoring an actor excludes all primitives owned by it, including capsule and HeadHitbox children (B-3).
		QueryParams.AddIgnoredActor(Shooter);
	}

	FHitResult Hit;
	while (World->LineTraceSingleByChannel(
		Hit, Origin, Result.EndPoint, MSCollision::WeaponTraceChannel, QueryParams))
	{
		if (!ShouldIgnoreWeaponTraceOccluder(Hit.GetActor()))
		{
			Result.Hit = Hit;
			Result.bBlockingHit = true;
			break;
		}

		// A water APhysicsVolume is a gameplay volume, not a weapon occluder.
		// Ignore that actor and trace the same segment again so a later enemy or
		// structure remains the first meaningful blocking hit.
		QueryParams.AddIgnoredActor(Hit.GetActor());
	}
	if (!Result.bBlockingHit)
	{
		return Result;
	}

	Result.EndPoint = Result.Hit.ImpactPoint;
	AActor* HitActor = Result.Hit.GetActor();
	Result.bCanDamage = HitActor && UMSTeamStatics::CanDamage(Shooter, HitActor);
	Result.bIsHeadshot = HitActor && IsHeadshotHit(Result.Hit, Origin, Origin + TraceDirection * Range);
	return Result;
}

float UMSCombatStatics::CalcSplashFalloff(
	const float Distance,
	const float Radius,
	const float MinFalloff,
	const float FullDamageRadiusRatio)
{
	if (!FMath::IsFinite(Distance) || !FMath::IsFinite(Radius) || Radius <= 0.0f || Distance > Radius)
	{
		return 0.0f;
	}

	const float ClampedDistance = FMath::Max(0.0f, Distance);
	const float ClampedMinFalloff = FMath::Clamp(FMath::IsFinite(MinFalloff) ? MinFalloff : 0.0f, 0.0f, 1.0f);
	const float ClampedFullDamageRatio = FMath::Clamp(
		FMath::IsFinite(FullDamageRadiusRatio) ? FullDamageRadiusRatio : 0.0f, 0.0f, 1.0f);
	const float FullDamageRadius = Radius * ClampedFullDamageRatio;
	if (ClampedDistance <= FullDamageRadius)
	{
		return 1.0f;
	}

	const float RampRadius = Radius - FullDamageRadius;
	if (RampRadius <= UE_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float RampAlpha = FMath::Clamp((ClampedDistance - FullDamageRadius) / RampRadius, 0.0f, 1.0f);
	return FMath::Lerp(1.0f, ClampedMinFalloff, RampAlpha);
}

int32 UMSCombatStatics::ApplySplashDamage(
	const UObject* WorldContextObject,
	const FVector& Origin,
	const float Radius,
	const float BaseDamage,
	const TArray<AActor*>& IgnoreActors,
	AActor* InstigatorActor,
	const float DownPoint,
	AActor* DirectHitActor,
	const bool bExcludeDirectHitActor,
	const float MinFalloff,
	const float FullDamageRadiusRatio, const float ShieldDamageScale,
	const float DirectHitDownPointMultiplier)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !FMath::IsFinite(Radius) || Radius <= 0.0f || !FMath::IsFinite(BaseDamage) || BaseDamage <= 0.0f)
	{
		return 0;
	}

	// ------------------------------------------------------------------
	// 1. 範囲収集: WeaponTraceチャンネルのSphereOverlap(§1.6)。
	//    壁(MSStructure)等の非ダメージ対象もヒットするが、ASC無しとして後段で除外される。
	// ------------------------------------------------------------------
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSSplashOverlap), /*bTraceComplex=*/false);
	if (InstigatorActor)
	{
		QueryParams.AddIgnoredActor(InstigatorActor);
	}
	QueryParams.AddIgnoredActors(IgnoreActors);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity,
		MSCollision::WeaponTraceChannel, FCollisionShape::MakeSphere(Radius), QueryParams);

	// ------------------------------------------------------------------
	// 2. アクター単位へ集約(二重適用防止。Unity: HashSet<Health>)。
	//    複数コライダー(カプセル+HeadHitbox等)が同時ヒットしても適用は1回。
	//    いずれかのコンポーネントがMSCoreプロファイルなら本拠点コアとして遮蔽判定対象。
	// ------------------------------------------------------------------
	TMap<AActor*, bool> Candidates; // Value = bIsHomeBaseCore
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor)
		{
			continue;
		}
		// AMSHomeBase forwards direct building hits to its core ASC. Splash damage must still
		// enter through the actual MSCore overlap below so IsHomeBaseCoreOccluded can preserve
		// wall shielding and the base/core pair cannot receive the same explosion twice.
		if (Actor->IsA<AMSHomeBase>())
		{
			continue;
		}
		const UPrimitiveComponent* Component = Overlap.GetComponent();
		const bool bIsCore = Component && Component->GetCollisionProfileName() == MSCollision::CoreProfileName;
		bool& bCoreFlag = Candidates.FindOrAdd(Actor, false);
		bCoreFlag |= bIsCore;
	}

	// 除外集合(QueryParamsで既に無視済みだが、直撃除外オプション含め明示的に固める)
	TSet<const AActor*> Excluded;
	if (InstigatorActor)
	{
		Excluded.Add(InstigatorActor);
	}
	for (const AActor* Ignore : IgnoreActors)
	{
		Excluded.Add(Ignore);
	}
	if (bExcludeDirectHitActor && DirectHitActor)
	{
		// 直撃は別経路(GE_Damage)で適用済み。呼び出し元が加算を望まない場合のみ除外(P2-4 要件3)
		Excluded.Add(DirectHitActor);
	}
	else if (DirectHitActor && !Excluded.Contains(DirectHitActor) && !DirectHitActor->IsA<AMSHomeBase>())
	{
		// Physics overlap can omit a confirmed direct-hit actor (for example, a narrow
		// hitbox at the sphere boundary). It still receives the same explosion at full
		// strength when the caller did not request explicit exclusion.
		Candidates.FindOrAdd(DirectHitActor, HasCoreCollisionProfile(DirectHitActor));
	}

	// 遮蔽トレースの無視リスト(攻撃者の機体が爆心付近にいても誤遮蔽しない)
	TArray<AActor*> OcclusionIgnore = IgnoreActors;
	if (InstigatorActor)
	{
		OcclusionIgnore.Add(InstigatorActor);
	}

	// ------------------------------------------------------------------
	// 3. 共通Context(falloffはSpec毎のSetByCallerで個別化するためContextは共有)。
	//    Instigator=攻撃者(Execution手順1のFF判定・キル帰属)、AttackerLocation=爆心
	//    (盾正面判定§1.6手順4・被弾方向HUD通知は爆心基準)。
	// ------------------------------------------------------------------
	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InstigatorActor);
	FGameplayEffectContextHandle ContextHandle = SourceASC
		? SourceASC->MakeEffectContext()
		: FGameplayEffectContextHandle(UAbilitySystemGlobals::Get().AllocGameplayEffectContext());
	ContextHandle.AddInstigator(InstigatorActor, InstigatorActor);
	if (FMSGameplayEffectContext* MSContext = FMSGameplayEffectContext::Get(ContextHandle))
	{
		MSContext->SetAttackerLocation(Origin);
  if (ShieldDamageScale >= 0.f) MSContext->SetShieldDamageScale(ShieldDamageScale);
	}

	// ------------------------------------------------------------------
	// 4. 対象ごとにfalloffを確定してGE_SplashDamage適用。
	//    FF判定はここでは行わない(UMSDamageExecution手順1が単一実装。B-2)。
	// ------------------------------------------------------------------
	int32 AppliedCount = 0;
	for (const TPair<AActor*, bool>& Candidate : Candidates)
	{
		AActor* Target = Candidate.Key;
		if (Excluded.Contains(Target))
		{
			continue;
		}
		// Count only actors that actually receive hostile damage. This makes the return
		// value suitable for explosion hit-marker feedback as well as diagnostics.
		if (InstigatorActor && !UMSTeamStatics::CanDamage(InstigatorActor, Target))
		{
			continue;
		}

		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!TargetASC)
		{
			continue; // 壁/建屋など非ダメージ対象
		}

		// 本拠点コアは壁越し爆風を受けない(Unity: IsHomeBaseCoreOccluded)
		if (Candidate.Value && IsHomeBaseCoreOccluded(World, Origin, Target->GetActorLocation(), Target, OcclusionIgnore))
		{
			continue;
		}

		float Distance = 0.0f;
		if (Target == DirectHitActor && !bExcludeDirectHitActor)
		{
			// Confirmed direct hits are full-strength even when overlap omitted the hitbox.
			Distance = 0.0f;
		}
		else if (!TryGetSplashSurfaceDistance(Target, Origin, Distance))
		{
			// Do not fall back to actor location or bounds: without query collision data
			// there is no safe surface distance. Confirmed direct hits were handled above.
			continue;
		}
		if (Distance > Radius)
		{
			continue;
		}
		const float Falloff = CalcSplashFalloff(Distance, Radius, MinFalloff, FullDamageRadiusRatio);
		const float DownPointFalloff = CalcSplashFalloff(Distance, Radius, 0.0f, FullDamageRadiusRatio);
		if (Falloff <= 0.0f)
		{
			continue;
		}

		FGameplayEffectSpec Spec(GetDefault<UGE_SplashDamage>(), ContextHandle, 1.0f);
		Spec.SetSetByCallerMagnitude(MSTags::Data_Damage, BaseDamage);
		Spec.SetSetByCallerMagnitude(MSTags::Data_SplashFalloff, Falloff);
		if (DownPoint > 0.0f)
		{
			// Apply spatial DP falloff exactly once here. DP has no minimum outer
			// guarantee; UMSDamageExecution applies SplashFalloff to Armor only.
			const float DirectScale = Target == DirectHitActor && FMath::IsFinite(DirectHitDownPointMultiplier)
				? FMath::Max(1.0f, DirectHitDownPointMultiplier) : 1.0f;
			Spec.SetSetByCallerMagnitude(MSTags::Data_DownPoint, DownPoint * DownPointFalloff * DirectScale);
		}

		if (SourceASC)
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
		}
		else
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
		}
		++AppliedCount;
	}

	return AppliedCount;
}

bool UMSCombatStatics::IsHomeBaseCoreOccluded(
	const UObject* WorldContextObject,
	const FVector& TraceStart,
	const FVector& CoreLocation,
	const AActor* CoreActor,
	const TArray<AActor*>& IgnoreActors)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return false;
	}

	// 爆心/射点 → コア中心(+150cm上方)。Unity: Physics.RaycastAll(center → target + up*1.5f)
	const FVector TraceEnd = CoreLocation + FVector(0.0f, 0.0f, CoreOcclusionAimHeightOffset);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MSCoreOcclusion), /*bTraceComplex=*/false);
	if (CoreActor)
	{
		QueryParams.AddIgnoredActor(CoreActor);
	}
	QueryParams.AddIgnoredActors(IgnoreActors);

	FHitResult Hit;
	while (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, MSCollision::WeaponTraceChannel, QueryParams))
	{
		if (!ShouldIgnoreWeaponTraceOccluder(Hit.GetActor()))
		{
			return true;
		}

		// Water is a gameplay volume and must not hide a real core occluder.
		// Continue the same segment so a wall behind the water still blocks.
		QueryParams.AddIgnoredActor(Hit.GetActor());
	}

	return false;
}

bool UMSCombatStatics::IsHeadshotHit(const FHitResult& Hit, const FVector& TraceStart, const FVector& TraceEnd)
{
	const AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return false;
	}

	// 1. 命中コンポーネント自身がHeadHitboxならHS確定
	if (Hit.Component.IsValid() && Hit.Component->GetFName() == AMSUnitBase::HeadHitboxComponentName)
	{
		return true;
	}

	// 2. 胴体コライダー越しでも、同一アクター配下のHeadHitboxがトレース線分と交差していればHS扱い
	//    (Unity: RaycastAllのヒット候補群にHeadHitboxが含まれていればHS、の再現。§0 ArchitectureMap L89)
	TInlineComponentArray<USphereComponent*> SphereComponents;
	HitActor->GetComponents(SphereComponents);
	for (const USphereComponent* Sphere : SphereComponents)
	{
		if (!Sphere || Sphere->GetFName() != AMSUnitBase::HeadHitboxComponentName)
		{
			continue;
		}

		if (DoesSegmentIntersectSphere(TraceStart, TraceEnd, Sphere->GetComponentLocation(), Sphere->GetScaledSphereRadius()))
		{
			return true;
		}
	}

	return false;
}

bool UMSCombatStatics::ConsumeBoostOrOverheat(UAbilitySystemComponent* ASC, float Cost)
{
	if (!ASC || Cost <= 0.0f)
	{
		return true;
	}

	// BoostSet未保持(AMSDestructibleTarget等)は消費対象なし=コスト制約なしで続行
	const UMSAttributeSet_Boost* BoostSet = ASC->GetSet<UMSAttributeSet_Boost>();
	if (!BoostSet)
	{
		return true;
	}

	// 全額(-Cost)を請求する。残量不足時はBoostSetの0クランプで「残り全消費」となり、
	// 0到達でUMSAttributeSet_Boost::PostGameplayEffectExecuteが強制OHを発動する
	// (Unity: ConsumeBoost #150「残量不足なら残り全部消費+強制オーバーヒート」)。
	const FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGE_BoostConsume::StaticClass(), 1.0f, Context);
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(MSTags::Data_BoostConsume, -Cost);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	// 残量ちょうど=Costの0到達もOH(Unity: currentBoost<=0規則)。false=強制OHが発動した
	return BoostSet->GetBoost() > 0.0f;
}

void UMSCombatStatics::ResolveCurrentFireOrigins(const AActor* Shooter, const FTransform& StartTransform,
	const FVector& StartTraceOrigin, const FVector& StartDirection, const FVector& StartVisualOrigin,
	FVector& OutTraceOrigin, FVector& OutDirection, FVector& OutVisualOrigin)
{
	OutTraceOrigin = StartTraceOrigin;
	OutDirection = StartDirection.GetSafeNormal();
	OutVisualOrigin = StartVisualOrigin;
	if (!Shooter)
	{
		return;
	}

	const FTransform CurrentTransform = Shooter->GetActorTransform();
	if (const UMSCameraComponent* Camera = Shooter->FindComponentByClass<UMSCameraComponent>())
	{
		// プレイヤー: 発射時点の実カメラ位置・実カメラ方向(TryFireCurrentWeaponの初弾と同一基準)
		OutTraceOrigin = Camera->GetComponentLocation();
		OutDirection = Camera->GetForwardVector().GetSafeNormal();
	}
	else
	{
		// BOT等: 開始時の機体Transform基準の相対値を現在Transformへ再適用(機体へ追従)
		OutTraceOrigin = CurrentTransform.TransformPosition(
			StartTransform.InverseTransformPosition(StartTraceOrigin));
		OutDirection = CurrentTransform.TransformVectorNoScale(
			StartTransform.InverseTransformVectorNoScale(StartDirection)).GetSafeNormal();
	}

	if (const UMSWeaponVisualComponent* WeaponVisual = Shooter->FindComponentByClass<UMSWeaponVisualComponent>())
	{
		OutVisualOrigin = WeaponVisual->GetMuzzleWorldLocation();
	}
	else
	{
		OutVisualOrigin = CurrentTransform.TransformPosition(
			StartTransform.InverseTransformPosition(StartVisualOrigin));
	}
}
