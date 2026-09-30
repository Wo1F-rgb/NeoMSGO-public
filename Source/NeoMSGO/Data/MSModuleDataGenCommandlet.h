#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MSModuleDataGenCommandlet.generated.h"

/** Generates /Game/Data/Modules/DA_Module_Transzomm after the new reflected class has been built. */
UCLASS()
class NEOMSGO_API UMSModuleDataGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
	#else
	virtual int32 Main(const FString&) override { return 0; }
	#endif
};
