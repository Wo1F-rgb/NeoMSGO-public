#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Sound/SoundBase.h"

#include "Voice/MSVoiceLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSVoiceLibraryAllLinesTest,
	"NeoMSGO.Voice.Library.AllTypeTriggerLines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FExpectedVoiceLines
	{
		EMSVoiceType Type;
		EMSVoiceTrigger Trigger;
		TArray<FString> Lines;
	};
}

bool FMSVoiceLibraryAllLinesTest::RunTest(const FString&)
{
	const TArray<FExpectedVoiceLines> Expected = {
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Sortie, {TEXT("了解！出撃だ！！"), TEXT("行くぜー！！"), TEXT("準備万端いつでもいいぜ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Sortie, {TEXT("了解！出撃します！"), TEXT("準備完了、作戦行動に移ります!")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Sortie, {TEXT("了解。これより、行動を開始します。"), TEXT("出撃ですね。分かりました。行きます！")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Deployed, {TEXT("俺の機体は不死身だぜ！！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Deployed, {TEXT("動いた！これで戦える！"), TEXT("まだまだ！！ここからが本番だ！！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Deployed, {TEXT("機体が、目を覚ました！"), TEXT("修復確認！行動を再開します！")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Kill, {TEXT("やったぜ！撃破だぜ！"), TEXT("ここを通すわけには、いかないぜ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Kill, {TEXT("や、やったぞ、撃破したぞ！"), TEXT("これ以上好きにさせない！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Kill, {TEXT("ハァハァ、敵機撃墜…"), TEXT("敵なら倒します！降伏してください！")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::KillStreak, {TEXT("まだまだ、続くぜ！"), TEXT("ここからが、腕の見せ所だ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::KillStreak, {TEXT("いいぞ、このまま続けー！"), TEXT("見える！敵が止まって見える！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::KillStreak, {TEXT("まだ、まだ行けます！このまま続けます！"), TEXT("敵の動きが読めてきた。これなら！")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::CaptureBase, {TEXT("拠点をいただいたぜ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::CaptureBase, {TEXT("拠点を占領した！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::CaptureBase, {TEXT("拠点を奪いました。")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Damaged, {TEXT("こ、これくらい！"), TEXT("まだまだ！"), TEXT("いいのを貰ったぜ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Damaged, {TEXT("クッ、被弾したか！"), TEXT("ま、まだまだーっ！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Damaged, {TEXT("だ、大丈夫、大丈夫だ。"), TEXT("予想以上のダメージか。")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Death, {TEXT("クッ、これからって時に！"), TEXT("機体が、俺の機体がぁー！"), TEXT("出直してくる！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Death, {TEXT("クソーッ！ここまでか！"), TEXT("避け切れなかった。")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Death, {TEXT("墜とされたのか！？"), TEXT("やられた。ここで終わりなのか？")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::Repair, {TEXT("大丈夫だ、修理は任せろ！"), TEXT("機体は、まだまだ再生可能だ！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::Repair, {TEXT("機体は、まだ戦うと言っています！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::Repair, {TEXT("修理…しましょうか？")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::SupplyPickup, {TEXT("いただき！")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::SupplyPickup, {TEXT("やった！良いものが手に入った！")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::SupplyPickup, {TEXT("順調に、任務遂行中です！")}},
		{EMSVoiceType::Hotblood, EMSVoiceTrigger::EmergencyRecall, {TEXT("すまない、退避させてもらう。")}},
		{EMSVoiceType::Serious, EMSVoiceTrigger::EmergencyRecall, {TEXT("あとは頼みました、一時退避します。")}},
		{EMSVoiceType::Shy, EMSVoiceTrigger::EmergencyRecall, {TEXT("悔しいけど…退避します。")}}
	};

	TestEqual(TEXT("All 3 x 10 combinations are specified"), Expected.Num(), 30);
	for (const FExpectedVoiceLines& Entry : Expected)
	{
		const TArrayView<const FText> Actual = MSVoiceLibrary::GetLines(Entry.Type, Entry.Trigger);
		TestTrue(TEXT("Every type/trigger pair has at least one line"), !Actual.IsEmpty());
		TestEqual(TEXT("Line count matches the design table"), Actual.Num(), Entry.Lines.Num());
		for (int32 Index = 0; Index < Actual.Num() && Index < Entry.Lines.Num(); ++Index)
		{
			TestEqual(TEXT("Line text matches the design table"), Actual[Index].ToString(), Entry.Lines[Index]);
			const FString AssetPath = MSVoiceLibrary::GetPilotVoiceAssetPath(Entry.Type, Entry.Trigger, Index);
			TestNotNull(*FString::Printf(TEXT("Pilot voice asset loads: %s"), *AssetPath),
				LoadObject<USoundBase>(nullptr, *AssetPath));
		}
	}
	return true;
}

#endif
