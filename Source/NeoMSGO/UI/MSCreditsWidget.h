// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSCreditsWidget.generated.h"

class UButton;
class UMultiLineEditableTextBox;

/** #236: code-only credits screen backed by the maintained markdown credit documents. */
UCLASS()
class NEOMSGO_API UMSCreditsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MS|Credits") void Open();
	UFUNCTION(BlueprintCallable, Category = "MS|Credits") void Close();
	UFUNCTION(BlueprintPure, Category = "MS|Credits") bool IsOpen() const { return bIsOpen; }

	/** Loads both authoritative documents. Returns a diagnostic message if either is unavailable. */
	static FString LoadCreditsText();
	/** Minimal markdown-to-readable-text conversion; attribution text itself is never duplicated. */
	static FString MakeDisplayText(FString Markdown);

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	UButton* GetCloseButtonForTesting() const { return CloseButton; }
	FString GetDisplayedTextForTesting() const;
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleCloseClicked();
	void BuildLayout();

	UPROPERTY(Transient) TObjectPtr<UMultiLineEditableTextBox> CreditsTextBox;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	bool bIsOpen = false;
};
