#include "UI/MSHUDTypes.h"

FMSHUDKillLogEntry FMSHUDKillLog::MakeEntry(const FString& UnitName, const bool bPlayerDeath)
{
	FMSHUDKillLogEntry Entry;
	Entry.Text = FText::FromString(UnitName + (bPlayerDeath ? TEXT("は撃破された!") : TEXT("を撃破!")));
	Entry.Color = bPlayerDeath ? MSHUDPalette::KillLogPlayerDeath : MSHUDPalette::KillLogEnemyDeath;
	Entry.RemainingLifetime = MSHUDPalette::KillLogLifetime;
	return Entry;
}

FMSHUDKillLogEntry FMSHUDKillLog::MakeTimedEntry(const FText& EventTime, const FString& UnitName,
	const bool bPlayerDeath)
{
	FMSHUDKillLogEntry Entry = MakeEntry(UnitName, bPlayerDeath);
	if (!EventTime.IsEmpty())
	{
		Entry.Text = FText::Format(FText::FromString(TEXT("{0}  {1}")), EventTime, Entry.Text);
	}
	return Entry;
}

void FMSHUDKillLog::AddEntry(TArray<FMSHUDKillLogEntry>& Entries, const FMSHUDKillLogEntry& Entry,
	const int32 MaxLines)
{
	if (MaxLines <= 0) { Entries.Reset(); return; }
	Entries.Add(Entry);
	if (Entries.Num() > MaxLines) Entries.RemoveAt(0, Entries.Num() - MaxLines);
}

void FMSHUDKillLog::TickEntries(TArray<FMSHUDKillLogEntry>& Entries, const float DeltaTime)
{
	for (FMSHUDKillLogEntry& Entry : Entries) Entry.RemainingLifetime -= FMath::Max(0.0f, DeltaTime);
	Entries.RemoveAll([](const FMSHUDKillLogEntry& Entry) { return Entry.RemainingLifetime <= 0.0f; });
}
#include "Core/MSGameplayTags.h"

FMSHUDIconStyle FMSHUDTypes::GetIconStyle(const FGameplayTag& T)
{
	// カテゴリ色は従来のまま維持し、表示はシルエット(UMSWeaponIconWidget::GetSilhouetteBoxes)。
	if (T == MSTags::Weapon_Category_MachineGun) return {FLinearColor(.20f,.85f,.35f,1), TEXT("MG"), T};
	if (T == MSTags::Weapon_Category_BeamRifle) return {FLinearColor(.20f,.70f,1,.99f), TEXT("BR"), T};
	if (T == MSTags::Weapon_Category_Bazooka) return {FLinearColor(1,.45f,.12f,.98f), TEXT("BZ"), T};
	if (T == MSTags::Weapon_Category_Melee) return {FLinearColor(1,.15f,.65f,.97f), TEXT("ML"), T};
	if (T == MSTags::Weapon_Category_Shield) return {FLinearColor(.25f,.40f,1,.96f), TEXT("SH"), T};
	if (T == MSTags::Weapon_Category_Supply) return {FLinearColor(.15f,1,.70f,.95f), TEXT("SP"), T};
	if (T == MSTags::Weapon_Category_ContinuousBeam) return {FLinearColor(.75f,.20f,1,.94f), TEXT("CB"), T};
	if (T == MSTags::Weapon_Category_Shotgun) return {FLinearColor(1,.85f,.10f,.93f), TEXT("SG"), T};
	if (T == MSTags::Weapon_Category_Homing) return {FLinearColor(1,.10f,.10f,.92f), TEXT("HM"), T};
	return {FLinearColor::Gray, TEXT("??"), T};
}

EMSHUDFireMode FMSHUDTypes::GetFireMode(const FGameplayTag& T)
{
	// Captured HUD semantics: continuous-trigger guns are FA, trigger-once ranged weapons are
	// SA, and equipment without a firing cadence is ALL. Never infer this from a localized name.
	if (T == MSTags::Weapon_Category_MachineGun || T == MSTags::Weapon_Category_ContinuousBeam)
	{
		return EMSHUDFireMode::FullAutomatic;
	}
	if (T == MSTags::Weapon_Category_Melee || T == MSTags::Weapon_Category_Shield ||
		T == MSTags::Weapon_Category_Supply)
	{
		return EMSHUDFireMode::AllPurpose;
	}
	return EMSHUDFireMode::SemiAutomatic;
}

FText FMSHUDTypes::GetFireModeLabel(const FGameplayTag& T)
{
	switch (GetFireMode(T))
	{
	case EMSHUDFireMode::FullAutomatic: return FText::FromString(TEXT("FA"));
	case EMSHUDFireMode::AllPurpose: return FText::FromString(TEXT("ALL"));
	default: return FText::FromString(TEXT("SA"));
	}
}
