// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Online/MSRoomDescriptor.h"
#include "MSRoomListEntryWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnRoomSelected, FMSRoomDescriptor, Room);

/** One code-only row in the room browser. */
UCLASS()
class NEOMSGO_API UMSRoomListEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRoomDescriptor(const FMSRoomDescriptor& InDescriptor);
	const FMSRoomDescriptor& GetRoomDescriptor() const { return RoomDescriptor; }
	UButton* GetSelectButton() const { return SelectButton; }

	UPROPERTY(BlueprintAssignable, Category = "MS|Room")
	FOnRoomSelected OnRoomSelected;

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
#endif

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildLayout();
	void RefreshText();
	UFUNCTION() void HandleClicked();

	UPROPERTY(Transient) TObjectPtr<UButton> SelectButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoomNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PlayerCountText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BotCountText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HostText;
	UPROPERTY(Transient) FMSRoomDescriptor RoomDescriptor;
};
