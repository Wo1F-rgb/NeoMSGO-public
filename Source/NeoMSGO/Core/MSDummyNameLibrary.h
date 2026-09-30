// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 架空(ダミー)キャラクター名の単一生成点(タスク#145)。
 * フレンド一覧・小隊・BOTなどで表示する架空キャラ名は必ずここを通して
 * 「ガンオン1」「ガンオン2」... の形式に統一する。
 * 実データ(本物のユーザーアカウント名)はこの関数を通さない。
 */
namespace MSDummyNames
{
	/** 「ガンオン{Index}」を返す(Indexは1以上にクランプ) */
	NEOMSGO_API FString MakeDummyName(int32 Index);

	/** 「ガンオン+数字」形式のダミー名かどうか(大文字小文字/前後空白は無視) */
	NEOMSGO_API bool IsDummyName(const FString& Name);
}
