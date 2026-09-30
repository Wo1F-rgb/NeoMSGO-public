#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MSHUDTypes.h"
#include "UI/MSHUDViewModel.h"
#include "MSHUDWidget.generated.h"
class AMSCapturePoint; class AMSPlayerCharacter; class AMSUnitBase; class USceneCaptureComponent2D; class UTextureRenderTarget2D; class UBorder; class UButton; class UCanvasPanel; class UImage; class UProgressBar; class UTextBlock; class UVerticalBox; class UWidget; class USizeBox; class UScaleBox; class UMSHUDLockOnMarkerWidget; class UMSHUDRingWidget; class UMSHUDScopeWidget; class UMSHUDViewModel; class UMSUnitRegistrySubsystem; class UMSWeaponIconWidget;
UCLASS()
class NEOMSGO_API UMSHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public: void SetViewModel(UMSHUDViewModel* InViewModel);
	void ToggleMinimapMode();
	bool IsMinimapOverviewMode() const { return bMinimapOverviewMode; }
	/** Design-space playfield shared by the respawn image, grid, labels and markers. */
	static FBox2d GetRespawnMapContentRect(const FVector2D& WorldHalfExtentsCm);
	static FVector2D GetOverviewMinimapCanvasSize(const FVector2D& WorldHalfExtentsCm);
	/** Called by the lowest canvas child; parent NativePaint would cover its text. */
	int32 PaintVideoBackdrop(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
		const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMSHUDFactionPresentationTest;
	friend class FMSHUDRespawnOverviewTest;
	friend class FMSRespawnGridRectTest;
	friend class FMSHUDNativeShieldReadoutTest;
	void InitializeForAutomationTests();
	void RefreshGaugesForAutomationTests();
	void RefreshCaptureForAutomationTests();
	FLinearColor GetBoostFillColorForAutomationTests() const;
	float GetBoostPercentForAutomationTests() const;
	/** Commandlets have no LocalPlayer viewport; force only the projection result while retaining real VM state. */
	void RefreshCombatForAutomationTests(bool bForceHomingTargetProjected=false);
	const UMSHUDRingWidget* GetHomingRingForAutomationTests() const{return HomingRing;}
	const UMSHUDRingWidget* GetHomingTargetRingForAutomationTests() const{return HomingTargetRing;}
	const UMSHUDRingWidget* GetChargeRingForAutomationTests() const{return ChargeRing;}
	const UMSHUDLockOnMarkerWidget* GetLockOnMarkerForAutomationTests() const{return LockOnMarker;}
	FName GetLockOnDistanceTypefaceForAutomationTests() const;
	ESlateVisibility GetReticleVisibilityForAutomationTests() const;
	ESlateVisibility GetScopeOverlayVisibilityForAutomationTests() const;
	FText GetShieldTextForAutomationTests() const;
	FText GetShieldValueForAutomationTests() const;
	FText GetShieldBrokenTextForAutomationTests() const;
	float GetShieldPercentForAutomationTests() const;
	ESlateVisibility GetShieldVisibilityForAutomationTests() const;
	FLinearColor GetShieldFillColorForAutomationTests() const;
	int32 GetIncomingDirectionCountForAutomationTests() const;
	int32 GetIncomingDistanceCountForAutomationTests() const;
	bool IsIncomingMissileWarningVisibleForAutomationTests() const;
	bool IsIncomingLockEdgeFlashVisibleForAutomationTests() const;
	float GetIncomingDirectionAngleForAutomationTests(int32 Index) const;
	friend class FMSHUDEmergencyRecallIndicatorTest;
#endif
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
	UCanvasPanel* GetRootCanvas() const{return RootCanvas;}
private:
	void RebuildWeaponCards(int32 Count);
	void RefreshGaugeWidgets();
	void RefreshBattleWidgets();
	void RefreshCaptureWidgets();
	void RefreshCombatWidgets(bool bForceHomingTargetProjected=false);
	void RefreshIncomingLockWidgets(float DeltaTime=0.0f);
	void InitializeMinimapUI();
	void BuildTopBattlePanel();
	void BuildTaskPanel();
	void BuildRightBottomCluster();
	void BuildLeftBottomCluster();
	void RefreshTeamRoster();
	void BuildMissionIcon();
	void BuildModuleChargeGauge();
	void BuildEmergencyRecallIndicator();
	void RefreshEmergencyRecallIndicator();
	void BuildRespawnPanel();
	void RefreshRespawnPanel();
	void RebuildRespawnCaptureMarkers();
	void BuildVoiceCutIns();
	void RefreshVoiceCutIns();
	void EnsureMinimapCapture(AMSPlayerCharacter* Player);
	void UpdateMinimap(float DeltaTime);
	void UpdateMinimapLayout(const FVector2D& CanvasSize);
	void RefreshMinimapPickups();
	void RefreshMinimapCapturePoints();
	void SubscribeToUnit(AMSUnitBase* Unit);
	UFUNCTION() void HandleUnitDeath(AActor* DeadUnit, AActor* Killer);
	/** Debug convenience: reaches Settings without Esc, which the editor's embedded-PIE "Stop"
	 * shortcut intercepts before gameplay input ever sees the key press. */
	UFUNCTION() void HandleSettingsButtonClicked();
	UPROPERTY(Transient) TObjectPtr<UMSHUDViewModel> ViewModel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
	UPROPERTY(Transient) TObjectPtr<UWidget> SettingsButtonVideoFit;
	UPROPERTY(Transient) TObjectPtr<UWidget> TaskPanelVideoFit;
	UPROPERTY(Transient) TObjectPtr<UBorder> TaskPanel;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> BoostBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ArmorBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> DPBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DPStateText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> WeaponPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> TeamABar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> TeamBBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TacticalText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskMissionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ArmorValueText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ArmorMaxText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShieldText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShieldValueText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShieldBrokenText;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ShieldBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ScoreValueText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TeamRosterNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TeamRosterSuits;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LockOnDistanceText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModuleStatusText;
	UPROPERTY(Transient) TObjectPtr<UBorder> ModuleChargePanel;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ModuleChargeBar;
	UPROPERTY(Transient) TObjectPtr<UBorder> EmergencyRecallPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmergencyRecallText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmergencyRecallDetailText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> EmergencyRecallActivePanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmergencyRecallCountdownText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RespawnPanel;
	/** Death presentation keeps a read-only tactical overview behind the timer. */
	UPROPERTY(Transient) TObjectPtr<UImage> RespawnTacticalMapImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RespawnGridVerticalLines;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RespawnGridHorizontalLines;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RespawnGridColumnLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RespawnGridRowLabels;
	/** ScaleBox wrappers around the shared marker compound (shape + number/letter). */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RespawnTacticalCaptureMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RespawnTacticalCaptureMarkerShapes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RespawnTacticalUnitMarkers;
	UPROPERTY(Transient) TObjectPtr<UImage> RespawnTacticalPlayerMarker;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RespawnTacticalMissionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RespawnRecoveryText;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> RespawnMSListFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RespawnMSListNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RespawnMSListTimers;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RespawnTitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RespawnCountdownText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RespawnHintText;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> VoiceCutInPanels;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> VoiceCutInPortraitFills;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> VoiceCutInPortraitImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> VoiceCutInLetters;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> VoiceCutInNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> VoiceCutInCaptions;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> VoiceCutInBodies;
	UPROPERTY(Transient) TObjectPtr<UImage> MissionIconImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MissionIconText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MissionIconSubText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WeaponNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WeaponFireModes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> WeaponSelectionMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UMSWeaponIconWidget>> WeaponIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WeaponStatuses;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> WeaponReloads;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> WeaponCardFrames;
	/** Video-reference weapon rows are direct design-space children; the legacy box stays available for Mode A. */
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> VideoWeaponCards;
	TArray<bool> WeaponCardSelections;
	TArray<FLinearColor> VideoWeaponColors;
	TArray<bool> VideoWeaponSelections;
	TArray<bool> VideoWeaponReloading;
	bool bVideoHudArtPass = false;
	FMSHUDGaugeSnapshot VideoGaugeSnapshot;
	FMSHUDShieldSnapshot VideoShieldSnapshot;
	bool bShieldInstrumentVisible = false;
	FMSHUDBattleSnapshot VideoBattleSnapshot;
	/** 画像2枚(横棒+縦棒)による十字クロスヘア。テキスト"+"はグリフのベースライン分だけ
	 * 中心からずれ、誘導捕捉サークル等の中心と不一致になるため画像描画で厳密に画面中心へ置く。 */
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Reticle;
	UPROPERTY(Transient) TObjectPtr<UMSHUDScopeWidget> ScopeOverlay;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HitMarker;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KillPopupText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KillStreakText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DamageDirectionIndicator;
	/** Receiver-side incoming-lock arrows; data is already filtered by the owner-only component. */
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> IncomingLockIndicators;
	/** At most two of the incoming arrows receive a meter readout. */
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> IncomingLockDistanceTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IncomingMissileWarningText;
	/** The baseline lock warning is a pair of narrow edge flashes, separate from module arrows.
	 * Convention reference: https://ggame.jp/news/detail.php?id=3419 . Pixel/audio parity remains unverified. */
	UPROPERTY(Transient) TObjectPtr<UImage> IncomingLockEdgeLeft;
	UPROPERTY(Transient) TObjectPtr<UImage> IncomingLockEdgeRight;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> CenterReload;
	UPROPERTY(Transient) TObjectPtr<UMSHUDRingWidget> HomingRing;
	UPROPERTY(Transient) TObjectPtr<UMSHUDRingWidget> HomingTargetRing;
	UPROPERTY(Transient) TObjectPtr<UMSHUDRingWidget> ChargeRing;
	UPROPERTY(Transient) TObjectPtr<UMSHUDRingWidget> CaptureRing;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CaptureText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CaptureCompleteText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SupplyText;
	UPROPERTY(Transient) TObjectPtr<UMSHUDLockOnMarkerWidget> LockOnMarker;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> UnitBars;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> KillLogPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> KillLogBackground;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> KillLogLines;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MinimapCanvas;
	UPROPERTY(Transient) TObjectPtr<UBorder> MinimapFrame;
	UPROPERTY(Transient) TObjectPtr<USizeBox> MinimapVideoDesign;
	UPROPERTY(Transient) TObjectPtr<UScaleBox> MinimapVideoFit;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MinimapEntranceLegend;
	UPROPERTY(Transient) TObjectPtr<UImage> MinimapImage;
	UPROPERTY(Transient) TObjectPtr<class UMSMapRadarRangeWidget> MinimapRadarRangeLayer;
	UPROPERTY(Transient) TObjectPtr<UImage> MinimapCircleMask;
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> MinimapCircleMaskTexture;
	UPROPERTY(Transient) TObjectPtr<class UMSMapRadarRangeWidget> RespawnRadarRangeLayer;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MinimapPlayerMarker;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MinimapFOVRays;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MinimapCardinals;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MinimapUnitMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> MinimapEntranceMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MinimapEntranceShapes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MinimapPickupMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> MinimapPickupActors;
	UPROPERTY(Transient) TArray<TObjectPtr<AMSCapturePoint>> MinimapCapturePoints;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> MinimapCapturePointMarkers;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MinimapCapturePointShapes;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MinimapCaptureRoleBubbles;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SystemDownText;
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> MinimapCapture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget;
	TWeakObjectPtr<AMSPlayerCharacter> MinimapCapturePlayer;
	TArray<TWeakObjectPtr<AMSCapturePoint>> RespawnCapturePointOrder;
	float MinimapPickupPollAccumulator = MSHUDPalette::MinimapPickupPollInterval;
	bool bMinimapOverviewMode = false;
	bool bMinimapUndergroundLayer = false;
	float VideoMinimapFrameHeightPx = 315.0f;
	float SystemDownRemaining = 0.0f;
	float IncomingLockFlashTime = 0.0f;
	bool bIncomingMissileWarningWasVisible = false;
	TArray<FMSHUDKillLogEntry> KillLogEntries;
	TWeakObjectPtr<UMSUnitRegistrySubsystem> UnitRegistry;
};
