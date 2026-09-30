// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MSModuleGameplayEffects.generated.h"

/** Timed module effect; duration is supplied through Data.Module.Duration. */
UCLASS()
class NEOMSGO_API UGE_ModuleEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UGE_ModuleEffect();
};

/** Fixed module overheat; grants both the generic and module-specific state tags. */
UCLASS()
class NEOMSGO_API UGE_ModuleForcedOverheat : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UGE_ModuleForcedOverheat();
};

/** Cooldown measured from activation time. */
UCLASS()
class NEOMSGO_API UGE_ModuleCooldown : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UGE_ModuleCooldown();
};
