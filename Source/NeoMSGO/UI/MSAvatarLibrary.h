// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWidget;
class UWidgetTree;

/**
 * パイロット名 → アバター立ち絵(上半身クロップ)のマッピング。
 * ロビー/コミュニティUIのパイロット表示は、ここで解決できた場合のみ画像アバターを出し、
 * 解決できない名前は従来表示のまま(汎用の既定アバターは割り当てない)。
 *
 * ◆ 立ち絵を追加する手順(今後の拡張方法)
 *  1. charactor/ の立ち絵から上半身(頭頂〜胸)を正方形クロップし、512x512のPNGとして
 *     Content/UI/Avatars/Avatar_<Name>.png に保存する(顔が中央に来るよう調整)
 *  2. エディタで /Game/UI/Avatars にインポートする(アセット名は Avatar_<Name>)
 *  3. MSAvatarLibrary.cpp の GetAvatarAssetMap() に「名前(小文字) → アセットパス」を1行追加する
 */
namespace MSAvatarLibrary
{
	/**
	 * 名前(大文字小文字無視)に対応するアバターテクスチャを返す。
	 * 専用マッピングが無い名前は名前ハッシュで既存立ち絵から決定的に1枚を割り当てる
	 * (全パイロットに顔を出すため)。空文字のみnullptr。
	 */
	NEOMSGO_API UTexture2D* ResolveAvatarTexture(const FString& PilotName);

	/**
	 * アバターが解決できた場合のみ、Size x Size の画像ウィジェットを生成して返す。
	 * 解決できない場合はnullptr(呼び出し側は従来のプレースホルダー表示を維持する)。
	 */
	NEOMSGO_API UWidget* MakeAvatarWidget(UWidgetTree* WidgetTree, const FString& PilotName, float Size);
}
