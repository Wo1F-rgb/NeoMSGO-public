// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSResultWidget.generated.h"

class UButton;
class UMSLobbyWidget;
class UMSUnitRegistrySubsystem;
class UTextBlock;
class UVerticalBox;
class APlayerState;

/** Code-only battle result screen shown between battle cleanup and the lobby. */
UCLASS()
class NEOMSGO_API UMSResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="MS|Result") void Open();
	UFUNCTION(BlueprintCallable, Category="MS|Result") void Close();
	UFUNCTION(BlueprintCallable, Category="MS|Result") void SetLobbyWidget(UMSLobbyWidget* InLobbyWidget);
	UFUNCTION(BlueprintPure, Category="MS|Result") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	/** Legacy registry-backed name order for callers that still need unit presence data. */
	static TArray<FString> BuildRankingNames(const UMSUnitRegistrySubsystem* Registry);

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests();
	void RefreshResultDataForAutomationTests() { RefreshResultData(); }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void CloseImmediate();
	void BuildReferenceLayout();
	void RefreshResultData();
	void RebuildRankingRows(const TArray<const APlayerState*>& PlayerStates, const APlayerState* ViewerPlayerState);

	UFUNCTION() void HandleNextClicked();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> OutcomeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GaugeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DurationText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PilotCardsBox;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RankingRowsBox;
	UPROPERTY(Transient) TObjectPtr<UButton> NextButton;
	UPROPERTY(Transient) TObjectPtr<UMSLobbyWidget> LobbyWidget;
};
