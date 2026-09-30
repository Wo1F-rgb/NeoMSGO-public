// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/MSDummyNameLibrary.h"
#include "Engine/Texture2D.h"
#include "UI/MSAvatarLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSAvatarLibraryTest, "NeoMSGO.UI.Avatar.NameResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSAvatarLibraryTest::RunTest(const FString& Parameters)
{
	// 空文字のみnullptr。マッピングに無い名前は名前ハッシュのフォールバックで必ず顔が出る
	TestNull(TEXT("empty name has no avatar"), MSAvatarLibrary::ResolveAvatarTexture(FString()));
	UTexture2D* UnmappedA = MSAvatarLibrary::ResolveAvatarTexture(MSDummyNames::MakeDummyName(1));
	UTexture2D* UnmappedB = MSAvatarLibrary::ResolveAvatarTexture(MSDummyNames::MakeDummyName(1));
	TestEqual(TEXT("unmapped pilot resolves deterministically to the same fallback"), UnmappedA, UnmappedB);
	if (MSAvatarLibrary::ResolveAvatarTexture(MSDummyNames::MakeDummyName(10)))
	{
		TestNotNull(TEXT("unmapped pilot receives a fallback portrait when assets are imported"), UnmappedA);
		TestNotNull(TEXT("the player speaker name also receives a portrait"),
			MSAvatarLibrary::ResolveAvatarTexture(TEXT("プレイヤー")));
	}

	// 前後空白は無視して同じ結果になる(ダミー名は「ガンオンN」に統一 — タスク#145)
	UTexture2D* Plain = MSAvatarLibrary::ResolveAvatarTexture(MSDummyNames::MakeDummyName(10));
	UTexture2D* Padded = MSAvatarLibrary::ResolveAvatarTexture(FString::Printf(TEXT("  %s "), *MSDummyNames::MakeDummyName(10)));
	TestEqual(TEXT("name lookup ignores surrounding whitespace"), Plain, Padded);

	// テクスチャアセットがインポート済みならガンオン10/ガンオン11のアバターが解決できる
	if (Plain)
	{
		TestNotNull(TEXT("second avatar dummy resolves when assets are imported"),
			MSAvatarLibrary::ResolveAvatarTexture(MSDummyNames::MakeDummyName(11)));
	}

	// WidgetTreeがnullでも安全にnullptrを返す
	TestNull(TEXT("null widget tree yields no widget"),
		MSAvatarLibrary::MakeAvatarWidget(nullptr, MSDummyNames::MakeDummyName(10), 32.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
