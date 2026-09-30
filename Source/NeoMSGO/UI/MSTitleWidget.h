// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSTitleWidget.generated.h"

class UButton;
class UImage;
class UMSLoginWidget;
class UMSUILockSubsystem;

/**
 * Code-only title screen. Shown at startup while the local player is logged out;
 * the start button leads into the login screen (MSLoginWidget).
 */
UCLASS()
class NEOMSGO_API UMSTitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static const FName TitleScreenId;
	/** Imported from Content/UI/Title/T_TitleBackground_Source.png. */
	static const TCHAR* BackgroundTexturePath;

	UFUNCTION(BlueprintCallable, Category="MS|Title") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Title") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Title") bool IsOpen() const { return bIsOpen; }

	/** Finds or creates the shared title widget for this player and opens it. */
	static UMSTitleWidget* OpenTitleScreen(APlayerController* PlayerController);

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	UButton* GetStartButtonForTesting() const { return StartButton; }
	UImage* GetBackgroundImageForTesting() const { return BackgroundImage; }
	void SetLoginWidgetForTesting(UMSLoginWidget* InWidget) { LoginWidget = InWidget; }
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
#endif

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildReferenceLayout();
	bool EnsureLoginWidget();
	UMSUILockSubsystem* ResolveUILockSubsystem() const;

	UFUNCTION(BlueprintCallable, Category="MS|Title", meta=(AllowPrivateAccess="true"))
	void HandleStartClicked();

	UPROPERTY(Transient) TObjectPtr<UImage> BackgroundImage;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	UPROPERTY(Transient) TObjectPtr<UMSLoginWidget> LoginWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	bool bIsOpen = false;
};
