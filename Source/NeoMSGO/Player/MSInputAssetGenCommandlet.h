// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSInputAssetGenCommandlet.generated.h"

/**
 * MSInputAssetGenCommandlet (P3-3)
 *
 * MCP/エディタの対話操作なしで Content/Input/ 配下に Enhanced Input の
 * InputAction(IA_*)/InputMappingContext(IMC_MSPlayer) 一式を生成・保存する
 * Editor限定コマンドレット。P1-2/P1-3(MSMobileSuitDataGen/MSWeaponDataGen)と
 * 同一パターンを踏襲する。
 *
 * 生成先は Content/Input/Actions/MS/ 以下(既存の UE5テンプレート由来の
 * IA_Move 等 Content/Input/Actions/ 直下のアセットは ThirdPersonテンプレート
 * (BP_ThirdPersonCharacter)が直接参照しているため上書きせず、衝突を避ける)。
 *
 * 実行方法:
 *   UnrealEditor-Cmd.exe <uproject> -run=MSInputAssetGen -unattended -nopause -nosplash -nullrhi
 */
UCLASS()
class NEOMSGO_API UMSInputAssetGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
