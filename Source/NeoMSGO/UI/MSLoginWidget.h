// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSLoginWidget.generated.h"

class AMSPlayerCharacter;
class UButton;
class UEditableTextBox;
class UMSAuthSubsystem;
class UMSGameInstance;
class UMSOnlineAccountSubsystem;
class UMSTitleWidget;
class UMSUILockSubsystem;
class UTextBlock;

/**
 * Code-only login screen backed by UMSAuthSubsystem. OfflineLocal keeps the legacy
 * username-only flow; explicit Online mode adds masked password, registration, guest
 * resume, reconnect and server status without storing a password.
 */
UCLASS()
class NEOMSGO_API UMSLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static const FName LoginScreenId;

	UFUNCTION(BlueprintCallable, Category="MS|Login") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Login") void Close();
	UFUNCTION(BlueprintPure, Category="MS|Login") bool IsOpen() const { return bIsOpen; }

	/** Finds or creates the shared login widget for this player and opens it. */
	static UMSLoginWidget* OpenLoginScreen(APlayerController* PlayerController);

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	UEditableTextBox* GetUsernameBoxForTesting() const { return UsernameBox; }
	UEditableTextBox* GetPasswordBoxForTesting() const { return PasswordBox; }
	UButton* GetLoginButtonForTesting() const { return LoginButton; }
	UButton* GetRegisterButtonForTesting() const { return RegisterButton; }
	UButton* GetGuestButtonForTesting() const { return GuestButton; }
	UButton* GetReconnectButtonForTesting() const { return ReconnectButton; }
	UButton* GetLogoutButtonForTesting() const { return LogoutButton; }
	UButton* GetBackButtonForTesting() const { return BackButton; }
	UButton* GetStartServerButtonForTesting() const { return StartServerButton; }
	FString GetErrorTextForTesting() const;
	void SetAuthSubsystemForTesting(UMSAuthSubsystem* InSubsystem) { TestAuthSubsystem = InSubsystem; }
	void SetTitleWidgetForTesting(UMSTitleWidget* InWidget) { TitleWidget = InWidget; }
	void SetLobbyPlayerForTesting(AMSPlayerCharacter* InPlayer) { TestLobbyPlayer = InPlayer; }
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void ShowError(const FText& Message);
	void EnterLobby();
	void BeginOnlineLogin(bool bRegister);
	void BeginOnlineGuestResume();
	void FinishOnlineRequest(bool bSucceeded, const FString& Message);
	void SetOnlineControlsVisible(bool bVisible);
	bool EnsureTitleWidget();
	UMSAuthSubsystem* ResolveAuthSubsystem() const;
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	AMSPlayerCharacter* ResolveLobbyPlayer() const;

	UFUNCTION() void HandleLoginClicked();
	UFUNCTION() void HandleRegisterClicked();
	UFUNCTION(BlueprintCallable, Category="MS|Login", meta=(AllowPrivateAccess="true"))
	void HandleGuestClicked();
	UFUNCTION() void HandleReconnectClicked();
	UFUNCTION(BlueprintCallable, Category="MS|Login", meta=(AllowPrivateAccess="true")) void HandleStartServerClicked();
	UFUNCTION() void HandleLogoutClicked();
	UFUNCTION() void HandleContinueClicked();
	UFUNCTION() void HandleBackClicked();

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> UsernameBox;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> PasswordBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ServerUrlText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PasswordLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ErrorText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UButton> LoginButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RegisterButton;
	UPROPERTY(Transient) TObjectPtr<UButton> GuestButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ReconnectButton;
	UPROPERTY(Transient) TObjectPtr<UButton> StartServerButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LogoutButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ContinueButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
	UPROPERTY(Transient) TObjectPtr<UMSTitleWidget> TitleWidget;
	UPROPERTY(Transient) TObjectPtr<UMSAuthSubsystem> TestAuthSubsystem;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	UPROPERTY(Transient) TObjectPtr<AMSPlayerCharacter> TestLobbyPlayer;
	bool bIsOpen = false;
	bool bOnlineRequestPending = false;
	bool bLocalServerStarting = false;
};
