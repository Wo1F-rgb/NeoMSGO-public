#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSShopWidget.generated.h"

class UMSLobbyWidget;
class UMSUILockSubsystem;

/** Code-only skeleton of the MSGO shop menu. All six shop functions are intentionally disabled. */
UCLASS()
class NEOMSGO_API UMSShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="MS|Shop") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Shop") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Shop") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	void SetLobbyWidget(UMSLobbyWidget* InLobbyWidget) { LobbyWidget = InLobbyWidget; }

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void CloseImmediate();
	void BuildReferenceLayout();
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	UFUNCTION() void HandleBackClicked();

	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
};
