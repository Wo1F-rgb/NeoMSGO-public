// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSMobileSuitWeaponLinkCommandlet.generated.h"

/**
 * MSMobileSuitWeaponLinkCommandlet (P1-4)
 *
 * MCP/エディタの対話操作なしで、P1-2で生成済みの Content/Data/MobileSuits/ 配下 8機体DataAsset
 * それぞれの EquippedWeapons に、P1-3で生成済みの Content/Data/Weapons/ 配下71武器DataAssetから
 * 選んだ2〜3個をソフト参照として設定・保存するEditor限定コマンドレット。
 * MSMobileSuitDataGenCommandlet(P1-2)/MSWeaponDataGenCommandlet(P1-3)と同一パターンを踏襲する。
 *
 * 武器の割り当ては各機体のアーキタイプ(UnitType。P1-2完了報告のコンセプト参照)に沿った
 * 「らしさ」を優先し、厳密なバランス調整は行わない(数値精度方針: 非中核パラメータはUnity版と
 * 厳密一致不要、ガンダムオンライン再現度優先)。
 *
 * 実行方法:
 *   UnrealEditor-Cmd.exe <uproject> -run=MSMobileSuitWeaponLink -unattended -nopause -nosplash -nullrhi
 */
UCLASS()
class NEOMSGO_API UMSMobileSuitWeaponLinkCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
