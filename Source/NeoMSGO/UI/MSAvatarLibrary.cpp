// Copyright NeoMSGO Project. All Rights Reserved.

#include "UI/MSAvatarLibrary.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Core/MSDummyNameLibrary.h"
#include "Engine/Texture2D.h"

namespace
{
	/**
	 * 名前(小文字) → テクスチャアセットのオブジェクトパス。
	 * 立ち絵を追加したらここに1行足すだけでロビー/コミュニティの全パイロット表示に反映される。
	 * ダミーパイロット名は「ガンオンN」に統一(タスク#145。旧: Kitano/Satou)。
	 */
	const TMap<FString, FString>& GetAvatarAssetMap()
	{
		static const TMap<FString, FString> Map = {
			{MSDummyNames::MakeDummyName(10).ToLower(), TEXT("/Game/UI/Avatars/Avatar_Kitano.Avatar_Kitano")},
			{MSDummyNames::MakeDummyName(11).ToLower(), TEXT("/Game/UI/Avatars/Avatar_Satou.Avatar_Satou")},
		};
		return Map;
	}
}

UTexture2D* MSAvatarLibrary::ResolveAvatarTexture(const FString& PilotName)
{
	const FString Key = PilotName.TrimStartAndEnd().ToLower();
	if (Key.IsEmpty()) return nullptr;
	if (const FString* AssetPath = GetAvatarAssetMap().Find(Key))
	{
		return LoadObject<UTexture2D>(nullptr, **AssetPath);
	}
	// 専用立ち絵が無いパイロットにも必ず顔を出す: 名前ハッシュで既存立ち絵から
	// 決定的に1枚選ぶ(同じ名前は毎回同じ顔になる)。
	TArray<FString> Fallbacks;
	GetAvatarAssetMap().GenerateValueArray(Fallbacks);
	if (Fallbacks.IsEmpty()) return nullptr;
	return LoadObject<UTexture2D>(nullptr, *Fallbacks[GetTypeHash(Key) % Fallbacks.Num()]);
}

UWidget* MSAvatarLibrary::MakeAvatarWidget(UWidgetTree* WidgetTree, const FString& PilotName, const float Size)
{
	UTexture2D* Texture = WidgetTree ? ResolveAvatarTexture(PilotName) : nullptr;
	if (!Texture) return nullptr;

	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrushFromTexture(Texture, false);

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(Size);
	Box->SetHeightOverride(Size);
	Box->SetContent(Image);
	return Box;
}
