// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MSUITransitionSubsystem.generated.h"

class UBorder;
struct FWorldContext;

/**
 * Full-screen, code-only SF shutter used by the transition subsystem.
 * It also drives the shared rise/fade entrance treatment so native and Blueprint
 * screens can use the same timing without duplicating animation tracks.
 */
UCLASS(NotBlueprintable)
class NEOMSGO_API UMSUITransitionOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	void StartShutter(UUserWidget* InTarget, FSimpleDelegate InSwapScreen);
	void StartEntrance(UUserWidget* InTarget);
	/** Cancels without invoking a pending screen swap and restores the target. */
	void Cancel();
	bool IsAnimating() const { return Mode != EMode::Idle; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class EMode : uint8 { Idle, Shutter, Entrance };
	void BuildOverlay();
	void SetShutterAmount(float ClosedAmount, float ViewportHeight);
	void SetEntranceAmount(float Amount);
	void Finish();

	UPROPERTY(Transient) TObjectPtr<UBorder> InputBlocker;
	UPROPERTY(Transient) TObjectPtr<UBorder> TopShutter;
	UPROPERTY(Transient) TObjectPtr<UBorder> BottomShutter;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> TargetWidget;

	FSimpleDelegate SwapScreen;
	EMode Mode = EMode::Idle;
	float ElapsedSeconds = 0.0f;
	bool bScreenSwapped = false;
};

/** Central reusable entry point for all major menu-to-menu transitions. */
UCLASS()
class NEOMSGO_API UMSUITransitionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Closes the shutter, invokes SwapScreen while covered, then opens it.
	 * Returns false while another transition owns input; the new request is ignored.
	 * Headless/automation contexts execute SwapScreen immediately.
	 */
	static bool Transition(UUserWidget* Context, UUserWidget* Target, FSimpleDelegate SwapScreen);

	/** Plays the common 0.24 s upward rise + opacity entrance when no shutter is active. */
	static void PlayEntrance(UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category="MS|UI Transition")
	bool IsTransitioning() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool BeginTransition(UUserWidget* Context, UUserWidget* Target, FSimpleDelegate SwapScreen);
	void BeginEntrance(UUserWidget* Widget);
	UMSUITransitionOverlay* GetOrCreateOverlay(UUserWidget* Context);
	static bool CanAnimate(const UUserWidget* Context);
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void ResetOverlay();
	FDelegateHandle PreLoadMapHandle;

	UPROPERTY(Transient) TObjectPtr<UMSUITransitionOverlay> Overlay;
};
