// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MSNavigatorTypes.generated.h"

/** デフォルト2名のナビゲーター。連邦(Federation)側/ジオン系(Rebellion)側それぞれの案内役。 */
UENUM(BlueprintType)
enum class EMSNavigator : uint8
{
	NoelAnderson,
	YuukiNakasato
};

/** ナビゲーターボイスの発話タイミング(5トリガー)。 */
UENUM(BlueprintType)
enum class EMSNavigatorTrigger : uint8
{
	Login,
	PreDeployment,
	FirstSortie,
	VictoryReturn,
	DefeatReturn
};
