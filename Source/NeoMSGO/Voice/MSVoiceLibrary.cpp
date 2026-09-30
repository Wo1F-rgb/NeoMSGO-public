// Copyright NeoMSGO Project. All Rights Reserved.

#include "Voice/MSVoiceLibrary.h"

#define LOCTEXT_NAMESPACE "MSVoiceLibrary"

namespace
{
#define VOICE_LINES(Name, ...) \
	const TArray<FText>& Name() { static const TArray<FText> Lines{ __VA_ARGS__ }; return Lines; }

	VOICE_LINES(HotbloodSortie, LOCTEXT("HotbloodSortie0", "了解！出撃だ！！"), LOCTEXT("HotbloodSortie1", "行くぜー！！"), LOCTEXT("HotbloodSortie2", "準備万端いつでもいいぜ！"))
	VOICE_LINES(SeriousSortie, LOCTEXT("SeriousSortie0", "了解！出撃します！"), LOCTEXT("SeriousSortie1", "準備完了、作戦行動に移ります!"))
	VOICE_LINES(ShySortie, LOCTEXT("ShySortie0", "了解。これより、行動を開始します。"), LOCTEXT("ShySortie1", "出撃ですね。分かりました。行きます！"))

	VOICE_LINES(HotbloodDeployed, LOCTEXT("HotbloodDeployed0", "俺の機体は不死身だぜ！！"))
	VOICE_LINES(SeriousDeployed, LOCTEXT("SeriousDeployed0", "動いた！これで戦える！"), LOCTEXT("SeriousDeployed1", "まだまだ！！ここからが本番だ！！"))
	VOICE_LINES(ShyDeployed, LOCTEXT("ShyDeployed0", "機体が、目を覚ました！"), LOCTEXT("ShyDeployed1", "修復確認！行動を再開します！"))

	VOICE_LINES(HotbloodKill, LOCTEXT("HotbloodKill0", "やったぜ！撃破だぜ！"), LOCTEXT("HotbloodKill1", "ここを通すわけには、いかないぜ！"))
	VOICE_LINES(SeriousKill, LOCTEXT("SeriousKill0", "や、やったぞ、撃破したぞ！"), LOCTEXT("SeriousKill1", "これ以上好きにさせない！"))
	VOICE_LINES(ShyKill, LOCTEXT("ShyKill0", "ハァハァ、敵機撃墜…"), LOCTEXT("ShyKill1", "敵なら倒します！降伏してください！"))

	VOICE_LINES(HotbloodKillStreak, LOCTEXT("HotbloodKillStreak0", "まだまだ、続くぜ！"), LOCTEXT("HotbloodKillStreak1", "ここからが、腕の見せ所だ！"))
	VOICE_LINES(SeriousKillStreak, LOCTEXT("SeriousKillStreak0", "いいぞ、このまま続けー！"), LOCTEXT("SeriousKillStreak1", "見える！敵が止まって見える！"))
	VOICE_LINES(ShyKillStreak, LOCTEXT("ShyKillStreak0", "まだ、まだ行けます！このまま続けます！"), LOCTEXT("ShyKillStreak1", "敵の動きが読めてきた。これなら！"))

	VOICE_LINES(HotbloodCaptureBase, LOCTEXT("HotbloodCaptureBase0", "拠点をいただいたぜ！"))
	VOICE_LINES(SeriousCaptureBase, LOCTEXT("SeriousCaptureBase0", "拠点を占領した！"))
	VOICE_LINES(ShyCaptureBase, LOCTEXT("ShyCaptureBase0", "拠点を奪いました。"))

	VOICE_LINES(HotbloodDamaged, LOCTEXT("HotbloodDamaged0", "こ、これくらい！"), LOCTEXT("HotbloodDamaged1", "まだまだ！"), LOCTEXT("HotbloodDamaged2", "いいのを貰ったぜ！"))
	VOICE_LINES(SeriousDamaged, LOCTEXT("SeriousDamaged0", "クッ、被弾したか！"), LOCTEXT("SeriousDamaged1", "ま、まだまだーっ！"))
	VOICE_LINES(ShyDamaged, LOCTEXT("ShyDamaged0", "だ、大丈夫、大丈夫だ。"), LOCTEXT("ShyDamaged1", "予想以上のダメージか。"))

	VOICE_LINES(HotbloodDeath, LOCTEXT("HotbloodDeath0", "クッ、これからって時に！"), LOCTEXT("HotbloodDeath1", "機体が、俺の機体がぁー！"), LOCTEXT("HotbloodDeath2", "出直してくる！"))
	VOICE_LINES(SeriousDeath, LOCTEXT("SeriousDeath0", "クソーッ！ここまでか！"), LOCTEXT("SeriousDeath1", "避け切れなかった。"))
	VOICE_LINES(ShyDeath, LOCTEXT("ShyDeath0", "墜とされたのか！？"), LOCTEXT("ShyDeath1", "やられた。ここで終わりなのか？"))

	VOICE_LINES(HotbloodRepair, LOCTEXT("HotbloodRepair0", "大丈夫だ、修理は任せろ！"), LOCTEXT("HotbloodRepair1", "機体は、まだまだ再生可能だ！"))
	VOICE_LINES(SeriousRepair, LOCTEXT("SeriousRepair0", "機体は、まだ戦うと言っています！"))
	VOICE_LINES(ShyRepair, LOCTEXT("ShyRepair0", "修理…しましょうか？"))

	VOICE_LINES(HotbloodSupplyPickup, LOCTEXT("HotbloodSupplyPickup0", "いただき！"))
	VOICE_LINES(SeriousSupplyPickup, LOCTEXT("SeriousSupplyPickup0", "やった！良いものが手に入った！"))
	VOICE_LINES(ShySupplyPickup, LOCTEXT("ShySupplyPickup0", "順調に、任務遂行中です！"))

	VOICE_LINES(HotbloodEmergencyRecall, LOCTEXT("HotbloodEmergencyRecall0", "すまない、退避させてもらう。"))
	VOICE_LINES(SeriousEmergencyRecall, LOCTEXT("SeriousEmergencyRecall0", "あとは頼みました、一時退避します。"))
	VOICE_LINES(ShyEmergencyRecall, LOCTEXT("ShyEmergencyRecall0", "悔しいけど…退避します。"))

#undef VOICE_LINES

	TArrayView<const FText> SelectLines(const EMSVoiceType Type, const EMSVoiceTrigger Trigger)
	{
#define SELECT_TRIGGER(TriggerName) \
		case EMSVoiceTrigger::TriggerName: \
			switch (Type) { \
			case EMSVoiceType::Hotblood: return MakeArrayView(Hotblood##TriggerName()); \
			case EMSVoiceType::Serious: return MakeArrayView(Serious##TriggerName()); \
			case EMSVoiceType::Shy: return MakeArrayView(Shy##TriggerName()); \
			default: return {}; }
		switch (Trigger)
		{
			SELECT_TRIGGER(Sortie)
			SELECT_TRIGGER(Deployed)
			SELECT_TRIGGER(Kill)
			SELECT_TRIGGER(KillStreak)
			SELECT_TRIGGER(CaptureBase)
			SELECT_TRIGGER(Damaged)
			SELECT_TRIGGER(Death)
			SELECT_TRIGGER(Repair)
			SELECT_TRIGGER(SupplyPickup)
			SELECT_TRIGGER(EmergencyRecall)
			default: return {};
		}
#undef SELECT_TRIGGER
	}
}

TArrayView<const FText> MSVoiceLibrary::GetLines(const EMSVoiceType Type, const EMSVoiceTrigger Trigger)
{
	return SelectLines(Type, Trigger);
}

FText MSVoiceLibrary::PickLine(const EMSVoiceType Type, const EMSVoiceTrigger Trigger, const int32 RandomSeed)
{
	int32 UnusedLineIndex = INDEX_NONE;
	return PickLine(Type, Trigger, RandomSeed, UnusedLineIndex);
}

FText MSVoiceLibrary::PickLine(const EMSVoiceType Type, const EMSVoiceTrigger Trigger, const int32 RandomSeed,
	int32& OutLineIndex)
{
	const TArrayView<const FText> Lines = GetLines(Type, Trigger);
	if (Lines.IsEmpty())
	{
		OutLineIndex = INDEX_NONE;
		return FText::GetEmpty();
	}
	FRandomStream Random(RandomSeed);
	OutLineIndex = Random.RandRange(0, Lines.Num() - 1);
	return Lines[OutLineIndex];
}

const TCHAR* MSVoiceLibrary::GetVoiceTypeId(const EMSVoiceType Type)
{
	switch (Type)
	{
	case EMSVoiceType::Serious: return TEXT("Serious");
	case EMSVoiceType::Shy: return TEXT("Shy");
	case EMSVoiceType::Hotblood:
	default: return TEXT("Hotblood");
	}
}

const TCHAR* MSVoiceLibrary::GetTriggerId(const EMSVoiceTrigger Trigger)
{
	switch (Trigger)
	{
	case EMSVoiceTrigger::Sortie: return TEXT("Sortie");
	case EMSVoiceTrigger::Deployed: return TEXT("Deployed");
	case EMSVoiceTrigger::Kill: return TEXT("Kill");
	case EMSVoiceTrigger::KillStreak: return TEXT("KillStreak");
	case EMSVoiceTrigger::CaptureBase: return TEXT("CaptureBase");
	case EMSVoiceTrigger::Damaged: return TEXT("Damaged");
	case EMSVoiceTrigger::Death: return TEXT("Death");
	case EMSVoiceTrigger::Repair: return TEXT("Repair");
	case EMSVoiceTrigger::SupplyPickup: return TEXT("SupplyPickup");
	case EMSVoiceTrigger::EmergencyRecall: return TEXT("EmergencyRecall");
	default: return TEXT("");
	}
}

FString MSVoiceLibrary::GetPilotVoiceAssetPath(const EMSVoiceType Type, const EMSVoiceTrigger Trigger,
	const int32 LineIndex)
{
	if (LineIndex < 0)
	{
		return FString();
	}
	const FString AssetName = FString::Printf(TEXT("%s_%s_%d"), GetVoiceTypeId(Type),
		GetTriggerId(Trigger), LineIndex);
	return FString::Printf(TEXT("/Game/Audio/Imported/PilotVoice/%s.%s"), *AssetName, *AssetName);
}

#undef LOCTEXT_NAMESPACE
