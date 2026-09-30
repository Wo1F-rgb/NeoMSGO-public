// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSWeaponDataGenCommandlet.generated.h"

/**
 * MSWeaponDataGenCommandlet (P1-3)
 *
 * MCP/エディタの対話操作なしで Content/Data/Weapons/ 配下に
 * UMSWeaponDataBase 派生(7種)の71武器分DataAssetを生成・保存するEditor限定コマンドレット。
 * Unity版71武器の実データ解析結果(weapon_import_plan.json)相当のデータをコードで直書きし、
 * 一度限りの資産生成手段として使用する(再実行は既存アセットを上書き)。
 * P1-2のMSMobileSuitDataGenCommandletと同一パターンを踏襲する。
 *
 * 実行方法:
 *   UnrealEditor-Cmd.exe <uproject> -run=MSWeaponDataGen -unattended -nopause -nosplash -nullrhi
 */
UCLASS()
class NEOMSGO_API UMSWeaponDataGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
