#include "Core/MSUnitRegistrySubsystem.h"

#include "AbilitySystemComponent.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamStatics.h"

void UMSUnitRegistrySubsystem::RegisterUnit(AMSUnitBase* Unit)
{
	if (IsValid(Unit) && !IsRegistered(Unit))
	{
		Units.Add(Unit);
		OnUnitRegistered.Broadcast(Unit);
	}
}

void UMSUnitRegistrySubsystem::UnregisterUnit(AMSUnitBase* Unit)
{
	Units.RemoveAll([Unit](const TWeakObjectPtr<AMSUnitBase>& Entry) { return !Entry.IsValid() || Entry.Get() == Unit; });
}

AMSUnitBase* UMSUnitRegistrySubsystem::FindNearestHostileUnit(const AActor* Querier, const float MaxDistance) const
{
	if (!IsValid(Querier) || MaxDistance < 0.0f) return nullptr;
	const float MaxDistanceSq = FMath::Square(MaxDistance);
	float BestDistanceSq = MaxDistanceSq;
	AMSUnitBase* Best = nullptr;
	for (const TWeakObjectPtr<AMSUnitBase>& Entry : Units)
	{
		AMSUnitBase* Candidate = Entry.Get();
		if (!IsValid(Candidate) || Candidate == Querier ||
			!UMSTeamStatics::CanDamage(Querier, Candidate)) continue;
		const UAbilitySystemComponent* ASC = Candidate->GetAbilitySystemComponent();
		if (!ASC || ASC->HasMatchingGameplayTag(MSTags::State_Dead)) continue;
		const float DistanceSq = FVector::DistSquared(Querier->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSq <= BestDistanceSq) { BestDistanceSq = DistanceSq; Best = Candidate; }
	}
	return Best;
}

int32 UMSUnitRegistrySubsystem::CountUnitsOfTeam(const FGenericTeamId TeamId, const bool bExcludeDead) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AMSUnitBase>& Entry : Units)
	{
		const AMSUnitBase* Unit = Entry.Get();
		if (!IsValid(Unit) || !UMSTeamStatics::IsOnTeam(Unit, TeamId)) continue;
		const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
		if (bExcludeDead && ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead)) continue;
		++Count;
	}
	return Count;
}

int32 UMSUnitRegistrySubsystem::CountUnitsOfTeamInRange(const FGenericTeamId TeamId, const FVector& Origin,
	const float RadiusCm, const bool bExcludeDead) const
{
	if (RadiusCm < 0.0f)
	{
		return 0;
	}

	const float RadiusSquared = FMath::Square(RadiusCm);
	int32 Count = 0;
	for (const TWeakObjectPtr<AMSUnitBase>& Entry : Units)
	{
		const AMSUnitBase* Unit = Entry.Get();
		if (!IsValid(Unit) || !UMSTeamStatics::IsOnTeam(Unit, TeamId) ||
			FVector::DistSquared2D(Origin, Unit->GetActorLocation()) > RadiusSquared)
		{
			continue;
		}

		const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
		if (bExcludeDead && ASC && ASC->HasMatchingGameplayTag(MSTags::State_Dead))
		{
			continue;
		}
		++Count;
	}
	return Count;
}

bool UMSUnitRegistrySubsystem::IsRegistered(const AMSUnitBase* Unit) const
{
	return Units.ContainsByPredicate([Unit](const TWeakObjectPtr<AMSUnitBase>& Entry) { return Entry.Get() == Unit; });
}

int32 UMSUnitRegistrySubsystem::GetRegisteredUnitCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AMSUnitBase>& Entry : Units) if (Entry.IsValid()) ++Count;
	return Count;
}

void UMSUnitRegistrySubsystem::ForEachRegisteredUnit(TFunctionRef<void(AMSUnitBase*)> Visitor) const
{
	for (const TWeakObjectPtr<AMSUnitBase>& Entry : Units)
	{
		if (AMSUnitBase* Unit = Entry.Get(); IsValid(Unit))
		{
			Visitor(Unit);
		}
	}
}
