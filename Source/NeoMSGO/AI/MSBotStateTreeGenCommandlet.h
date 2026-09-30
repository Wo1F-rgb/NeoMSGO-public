#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSBotStateTreeGenCommandlet.generated.h"

/** Builds and compiles the native-node StateTree used by every MSGO bot. */
UCLASS()
class NEOMSGO_API UMSBotStateTreeGenCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
