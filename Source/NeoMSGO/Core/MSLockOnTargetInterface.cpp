// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSLockOnTargetInterface.h"
#include "GameFramework/Actor.h"

FVector IMSLockOnTargetInterface::GetLockOnAimPoint() const
{
	// ネイティブインターフェースの実装オブジェクトはUObject。Actorならその位置を返す。
	if (const AActor* AsActor = Cast<AActor>(const_cast<IMSLockOnTargetInterface*>(this)))
	{
		return AsActor->GetActorLocation();
	}
	return FVector::ZeroVector;
}
