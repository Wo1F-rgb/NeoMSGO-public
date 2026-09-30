// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PublicAssetImportSanitizeCommandlet.generated.h"

/** Removes machine-specific reimport metadata from the exported public project's assets. */
UCLASS()
class NEOMSGO_API UPublicAssetImportSanitizeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual int32 Main(const FString& Params) override;
#else
	virtual int32 Main(const FString&) override { return 1; }
#endif
};
