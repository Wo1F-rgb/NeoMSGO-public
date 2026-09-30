#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MSModuleCustomizeWidget.generated.h"

class UButton;
class UMSMobileSuitData;
class UMSDeckSubsystem;
class UMSModuleData;
class UMSSuitDetailWidget;
class UMSUILockSubsystem;
class UScrollBox;
class UTextBlock;

UCLASS()
class NEOMSGO_API UMSModuleCatalogEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetModule(UMSModuleData* InModule, class UMSModuleCustomizeWidget* InOwner);

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION() void HandleClicked();

	UPROPERTY(Transient) TObjectPtr<UMSModuleData> Module;
	UPROPERTY(Transient) TObjectPtr<UMSModuleCustomizeWidget> Owner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModuleNameText;
};

/** Per-slot offline module equipment and read-only online catalog. */
UCLASS()
class NEOMSGO_API UMSModuleCustomizeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MS|Module Customize") void Open(UMSMobileSuitData* Preset, int32 SlotIndex = -1);
	UFUNCTION(BlueprintCallable, Category = "MS|Module Customize") void Close();
	UFUNCTION(BlueprintPure, Category = "MS|Module Customize") bool IsOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	void SetReturnSuitDetailWidget(UMSSuitDetailWidget* InWidget) { ReturnSuitDetailWidget = InWidget; }
	void SetModuleCatalog(const TArray<UMSModuleData*>& Modules);
	void ChooseModule(UMSModuleData* Module);
	void SelectCategory(int32 CategoryIndex);
	/** Online module persistence is unsupported; offline changes use the captured deck slot. */
	bool IsModuleEquipmentSupported() const;
	bool IsModuleEquipped(const UMSModuleData* Module) const;
	int32 GetCatalogCount() const { return ModuleCatalog.Num(); }
	int32 GetFilteredCatalogCount() const { return FilteredModules.Num(); }
	int32 GetSelectedCategoryIndex() const { return SelectedCategoryIndex; }
	FText GetDisplayedModuleName() const;

#if WITH_DEV_AUTOMATION_TESTS
	void InitializeForAutomationTests(UMSMobileSuitData* Preset, const TArray<UMSModuleData*>& Modules);
	void SetDeckSubsystemForTesting(UMSDeckSubsystem* Deck) { TestDeckSubsystem = Deck; }
	void SetUILockSubsystemForTesting(UMSUILockSubsystem* InSubsystem) { TestUILockSubsystem = InSubsystem; }
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildReferenceLayout();
	void RefreshCatalogFromAssetManager();
	void RefreshCatalogRows();
	void RefreshDetails();
	void NotifyModuleChanged();
	UMSDeckSubsystem* ResolveDeckSubsystem() const;
	UMSModuleData* GetCurrentModule() const;
	void CommitModule(UMSModuleData* Module);
	UMSUILockSubsystem* ResolveUILockSubsystem() const;
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleCategoryEnhancementClicked();
	UFUNCTION() void HandleCategorySpecialClicked();
	UFUNCTION() void HandleCategorySupportClicked();
	UFUNCTION() void HandleCategoryOtherClicked();
	UFUNCTION() void HandleEquipClicked();
	UFUNCTION() void HandleRemoveClicked();

	UPROPERTY(Transient) TObjectPtr<UMSMobileSuitData> ActivePreset;
	UPROPERTY(Transient) TObjectPtr<UMSDeckSubsystem> TestDeckSubsystem;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NoticeText;
	UPROPERTY(Transient) TObjectPtr<UButton> RemoveButton;
	int32 ActiveSlotIndex = INDEX_NONE;
	bool bLastSaveFailed = false;
	UPROPERTY(Transient) TObjectPtr<UMSModuleData> SelectedModule;
	UPROPERTY(Transient) TObjectPtr<UMSSuitDetailWidget> ReturnSuitDetailWidget;
	UPROPERTY(Transient) TObjectPtr<UMSUILockSubsystem> TestUILockSubsystem;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSModuleData>> ModuleCatalog;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSModuleData>> FilteredModules;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CategoryButtons;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CatalogScrollBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModuleNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ParametersText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquippedStateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModuleSlotText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquippedIconText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EquipButtonText;
	UPROPERTY(Transient) TObjectPtr<UButton> EquipButton;
	int32 SelectedCategoryIndex = 0;
};
