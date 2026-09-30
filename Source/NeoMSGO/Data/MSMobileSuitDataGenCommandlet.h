// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSMobileSuitDataGenCommandlet.generated.h"

/**
 * MSMobileSuitDataGenCommandlet (P1-2)
 *
 * MCP/エディタの対話操作なしで Content/Data/MobileSuits/ 配下に
 * UMSMobileSuitData の8機体分DataAssetを生成・保存するEditor限定コマンドレット。
 * MobileSuitCatalog.Definitions(ArchitectureMap.md §13)相当のデータをコードで直書きし、
 * 一度限りの資産生成手段として使用する(再実行は既存アセットを上書き)。
 *
 * 実行方法:
 *   UnrealEditor-Cmd.exe <uproject> -run=MSMobileSuitDataGen -unattended -nopause -nosplash
 */
UCLASS()
class NEOMSGO_API UMSMobileSuitDataGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
