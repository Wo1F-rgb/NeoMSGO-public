#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MSHUDTypes.h"
#include "UI/MSHUDRingWidget.h"
#include "UI/MSHUDScopeWidget.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSMapTypes.h"
#include "UI/MSMapWidgetBase.h"
#include "UI/MSHUDWidget.h"
#include "UI/MSUIStyle.h"
#include "UI/MSMapCaptureSubsystem.h"
#include "UI/MSMapBoundsActor.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSShieldComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Player/MSRespawnComponent.h"
#include "UI/MSHUDLockOnMarkerWidget.h"
#include "UI/MSWeaponIconWidget.h"
#include "UI/MSWeaponPhotoSubsystem.h"
#include "AI/MSBotCharacter.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSGameInstance.h"
#include "Core/MSTeamTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerState.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSEmergencyRecallComponent.h"
#include "Combat/MSModuleGameplayEffects.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSModuleData.h"
#include "Data/MSWeaponData.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSLockOnComponent.h"
#include "Weapons/MSWeaponComponent.h"
#include "Voice/MSVoiceCutInSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDDPTest,"NeoMSGO.UI.HUD.DP",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDRespawnOverviewTest,"NeoMSGO.UI.HUD.RespawnOverviewAndLatePoints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDTeamGaugeTest,"NeoMSGO.UI.HUD.OwnerTeamGauges",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDHomingPresentationTest,"NeoMSGO.UI.HUD.HomingPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDHomingWidgetRingTest,"NeoMSGO.UI.HUD.HomingWidgetRings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDHomingCircleLockParityTest,"NeoMSGO.UI.HUD.HomingCircleLockParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDPresetVoiceCutInSnapshotTest,"NeoMSGO.UI.HUD.PresetVoiceCutInSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDSniperScopePresentationTest,"NeoMSGO.UI.HUD.SniperScopePresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDNativeShieldReadoutTest,"NeoMSGO.UI.HUD.NativeShieldReadout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponPhotoPresentationContractTest,"NeoMSGO.UI.WeaponPhoto.PresentationContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMSHUDNativeShieldReadoutTest::RunTest(const FString&)
{
	const MSUIStyle::EUIMode OriginalMode=MSUIStyle::GetSelectedMode();
	struct FRestoreMode { MSUIStyle::EUIMode Mode; ~FRestoreMode(){MSUIStyle::SetSelectedMode(Mode,false);} } Restore{OriginalMode};
	for (const MSUIStyle::EUIMode Mode : {MSUIStyle::EUIMode::TypeA,MSUIStyle::EUIMode::TypeB})
	{
		MSUIStyle::SetSelectedMode(Mode,false);
		UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDNativeShieldWorld"));
		FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
		FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
		APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
		UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
		UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(Controller);HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
		HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("suit without shield has no shield label"),HUD->GetShieldVisibilityForAutomationTests(),ESlateVisibility::Collapsed);
		TestEqual(TEXT("suit without shield has no shield bar"),HUD->ShieldBar->GetVisibility(),ESlateVisibility::Collapsed);
		TestFalse(TEXT("suit without shield cannot paint the video gauge"),HUD->bShieldInstrumentVisible);

		UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(Player);
		UMSShieldWeaponData* Data=NewObject<UMSShieldWeaponData>(Suit);Data->MaxDurability=1000.0f;Data->GuardFrontalAngleDegrees=65.0f;
		Suit->NativeShield=Data;Player->InitializeUnit(Suit);
		UMSShieldComponent* Shield=Player->GetShieldComponent();
		if(!TestNotNull(TEXT("equipped suit owns a shield component"),Shield)) {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
		HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("full number comes from the native shield without locale grouping"),HUD->GetShieldValueForAutomationTests().ToString(),FString(TEXT("1000")));
		TestEqual(TEXT("full shield ratio"),HUD->GetShieldPercentForAutomationTests(),1.0f);
		TestEqual(TEXT("shield label is beside its number"),HUD->GetShieldVisibilityForAutomationTests(),ESlateVisibility::HitTestInvisible);
		const FLinearColor HealthyFill=HUD->GetShieldFillColorForAutomationTests();
		TestEqual(TEXT("video uses vector gauge and classic uses native bar"),HUD->ShieldBar->GetVisibility(),
			Mode==MSUIStyle::EUIMode::TypeB?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);

		Shield->SetDeckShieldEnabled(false);HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("deck unequip hides shield"),HUD->GetShieldVisibilityForAutomationTests(),ESlateVisibility::Collapsed);
		TestFalse(TEXT("deck unequip removes the video gauge too"),HUD->bShieldInstrumentVisible);
		Shield->SetDeckShieldEnabled(true);
		float ArmorDamage=0.0f,ShieldDamage=0.0f;bool bGuarded=false;
		TestTrue(TEXT("frontal hit reduces the native shield"),Shield->TryAbsorbDamage(600.0f,false,FVector(1000,0,0),ArmorDamage,ShieldDamage,bGuarded));
		HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("partial shield number"),HUD->GetShieldValueForAutomationTests().ToString(),FString(TEXT("400")));
		TestTrue(TEXT("partial ratio uses the VM snapshot"),FMath::IsNearlyEqual(HUD->GetShieldPercentForAutomationTests(),0.4f));
		TestEqual(TEXT("40 percent stays cyan-green"),HUD->GetShieldFillColorForAutomationTests(),HealthyFill);
		TestTrue(TEXT("second hit reaches low durability"),Shield->TryAbsorbDamage(200.0f,false,FVector(1000,0,0),ArmorDamage,ShieldDamage,bGuarded));
		HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("20 percent changes to warning red"),HUD->GetShieldFillColorForAutomationTests(),MSHUDPalette::ArmorLow);
		TestTrue(TEXT("fatal hit breaks the native shield"),Shield->TryAbsorbDamage(200.0f,false,FVector(1000,0,0),ArmorDamage,ShieldDamage,bGuarded));
		HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("broken shield shows zero"),HUD->GetShieldValueForAutomationTests().ToString(),FString(TEXT("0")));
		TestEqual(TEXT("broken shield bar is empty"),HUD->GetShieldPercentForAutomationTests(),0.0f);
		TestEqual(TEXT("broken caption follows the original Japanese wording"),HUD->GetShieldBrokenTextForAutomationTests().ToString(),FString(TEXT("シールド大破")));
		TestEqual(TEXT("broken caption appears"),HUD->ShieldBrokenText->GetVisibility(),ESlateVisibility::HitTestInvisible);
		Shield->RepairToFull();HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("repair restores full gauge"),HUD->GetShieldPercentForAutomationTests(),1.0f);
		TestEqual(TEXT("repair removes broken caption"),HUD->ShieldBrokenText->GetVisibility(),ESlateVisibility::Collapsed);

		if(Mode==MSUIStyle::EUIMode::TypeB)
		{
			const FAnchors GaugeAnchors=CastChecked<UCanvasPanelSlot>(HUD->ShieldBar->Slot)->GetAnchors();
			const FVector2D GaugeMin(GaugeAnchors.Minimum.X*1920.0f,GaugeAnchors.Minimum.Y*1080.0f);
			const FVector2D GaugeMax(GaugeAnchors.Maximum.X*1920.0f,GaugeAnchors.Maximum.Y*1080.0f);
			const auto GetDesignBounds=[](UWidget* Text)
			{
				UWidget* Wrapper=Text;
				while(Wrapper&&!Cast<UCanvasPanelSlot>(Wrapper->Slot))Wrapper=Wrapper->GetParent();
				const FAnchors Anchors=CastChecked<UCanvasPanelSlot>(Wrapper->Slot)->GetAnchors();
				return FBox2d(FVector2d(Anchors.Minimum.X*1920.0f,Anchors.Minimum.Y*1080.0f),
					FVector2d(Anchors.Maximum.X*1920.0f,Anchors.Maximum.Y*1080.0f));
			};
			const FBox2d NumberBounds=GetDesignBounds(HUD->ShieldValueText);
			const FBox2d LabelBounds=GetDesignBounds(HUD->ShieldText);
			const FBox2d BrokenBounds=GetDesignBounds(HUD->ShieldBrokenText);
			TestTrue(TEXT("1920x1080 gauge sits at the reference ARMOR upper-right"),
				FVector2D::Distance(GaugeMin,FVector2D(1745,978))<0.1f&&FVector2D::Distance(GaugeMax,FVector2D(1916,988))<0.1f);
			TestTrue(TEXT("shield gauge clears weapon and ARMOR gauges"),GaugeMin.Y>748+2*66+56&&GaugeMax.Y<1018.0f);
			TestTrue(TEXT("number and SHIELD label sit above the narrow bar"),NumberBounds.Max.Y<=GaugeMin.Y&&LabelBounds.Max.Y<=GaugeMin.Y);
			TestTrue(TEXT("broken caption stays below shield bar and right of ARMOR numbers"),BrokenBounds.Min.Y>=GaugeMax.Y&&BrokenBounds.Min.X>1818.0);
		}
		Player->GetRespawnComponent()->SetAutoRespawnEnabled(false);
		Player->GetVitalSet()->OnDeath.Broadcast(Player,nullptr);HUD->RefreshGaugesForAutomationTests();
		TestEqual(TEXT("death hides shield label"),HUD->GetShieldVisibilityForAutomationTests(),ESlateVisibility::Collapsed);
		TestEqual(TEXT("death hides shield bar"),HUD->ShieldBar->GetVisibility(),ESlateVisibility::Collapsed);
		TestFalse(TEXT("death hides vector shield gauge"),HUD->bShieldInstrumentVisible);
		GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
	}
	return true;
}

bool FMSWeaponPhotoPresentationContractTest::RunTest(const FString&)
{
	TArray<FColor> Pixels;
	Pixels.Init(FColor(0,0,0,0),8*6);
	Pixels[2*8+1]=FColor(255,255,255,255);
	Pixels[4*8+6]=FColor(255,255,255,32);
	const FIntRect Bounds=UMSWeaponPhotoSubsystem::FindOpaqueBounds(Pixels,8,6,8);
	TestEqual(TEXT("photo alpha bounds ignore transparent capture border"),Bounds,FIntRect(1,2,7,5));
	TArray<FColor> EdgePixels;
	EdgePixels.Init(FColor(0,0,0,0),8*6);
	EdgePixels[0]=FColor(255,255,255,255);
	EdgePixels[5*8+7]=FColor(255,255,255,32);
	TestEqual(TEXT("photo alpha bounds include content on both texture edges"),
		UMSWeaponPhotoSubsystem::FindOpaqueBounds(EdgePixels,8,6,8),FIntRect(0,0,8,6));
	TArray<FColor> EmptyPixels;
	EmptyPixels.Init(FColor(0,0,0,0),8*6);
	TestTrue(TEXT("empty alpha capture returns an empty bounds"),
		UMSWeaponPhotoSubsystem::FindOpaqueBounds(EmptyPixels,8,6,8).IsEmpty());
	TestTrue(TEXT("invalid alpha dimensions return an empty bounds"),
		UMSWeaponPhotoSubsystem::FindOpaqueBounds(Pixels,0,6,8).IsEmpty());
	const FLinearColor OliveHighlight=UMSWeaponPhotoSubsystem::ToneMapPhotoColor(FLinearColor(8.0f,4.0f,1.0f,0.6f));
	TestTrue(TEXT("HDR photo compression preserves the source color ratios"),
		FMath::IsNearlyEqual(OliveHighlight.R/OliveHighlight.G,2.0f,0.0001f) &&
		FMath::IsNearlyEqual(OliveHighlight.G/OliveHighlight.B,4.0f,0.0001f));
	TestTrue(TEXT("photo compression preserves alpha for transparent composition"),
		FMath::IsNearlyEqual(OliveHighlight.A,0.6f,0.0001f));
	const FLinearColor Neutral=UMSWeaponPhotoSubsystem::ToneMapPhotoColor(FLinearColor(8.0f,8.0f,8.0f,1.0f));
	TestTrue(TEXT("neutral exposure matches the old highlight brightness"),
		FMath::IsNearlyEqual(Neutral.R,8.0f/9.0f,0.0001f));
	const FBox2D CatalogPhoto=UMSWeaponIconWidget::CalculatePhotoCoverRect(FVector2D(100,35),FVector2D(240,100));
	TestTrue(TEXT("catalog photo cover fills the frame width"),FMath::IsNearlyEqual(CatalogPhoto.Max.X-CatalogPhoto.Min.X,100.0f));
	TestTrue(TEXT("catalog photo cover crops overflow vertically inside its clip"),CatalogPhoto.Min.Y<0.0f&&CatalogPhoto.Max.Y>35.0f);
	const FBox2D DetailPhoto=UMSWeaponIconWidget::CalculatePhotoCoverRect(FVector2D(140,53),FVector2D(300,110));
	TestTrue(TEXT("detail photo cover fills the frame height"),FMath::IsNearlyEqual(DetailPhoto.Max.Y-DetailPhoto.Min.Y,53.0f));
	TestTrue(TEXT("detail photo cover intentionally crops both horizontal edges"),DetailPhoto.Min.X<0.0f&&DetailPhoto.Max.X>140.0f);
	return true;
}

bool FMSHUDRespawnOverviewTest::RunTest(const FString&)
{
	struct FScope
	{
		UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDRespawnMapWorld"));
		FScope(){auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();}
		~FScope(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
	} Scope;
	AMSMapBoundsActor* Bounds=Scope.World->SpawnActor<AMSMapBoundsActor>(); Bounds->WorldHalfExtentCm=200000.0f;
	AMSPlayerCharacter* Player=Scope.World->SpawnActor<AMSPlayerCharacter>();
	// A real sortie always has a suit. Deploy rejects an uninitialised pawn before clearing
	// the death timer, so give this isolated world a valid transient preset as well.
	UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(Player);Suit->MaxArmor=4000;
	Player->InitializeUnit(Suit);
	TestEqual(TEXT("respawn fixture starts with an equipped suit"),Player->GetMobileSuitData(),Suit);
	APlayerController* Controller=Scope.World->SpawnActor<APlayerController>();
	Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));Controller->Possess(Player);
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	UMSHUDWidget* HUD=CreateWidget<UMSHUDWidget>(Controller);HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
	UMSMapCaptureSubsystem* Capture=Scope.World->GetSubsystem<UMSMapCaptureSubsystem>();
	HUD->RefreshRespawnPanel();
	TestEqual(TEXT("living HUD does not request overview capture"),Capture->GetCaptureRequestCountForTesting(),0);
	TestEqual(TEXT("HUD can start before points are replicated"),HUD->RespawnTacticalCaptureMarkers.Num(),0);
	AMSCapturePoint* Far=Scope.World->SpawnActor<AMSCapturePoint>(FVector(100000,0,0),FRotator::ZeroRotator);
	Player->GetRespawnComponent()->SetAutoRespawnEnabled(false);
	// Deliver the production death notification; the real component owns the timer.
	Player->GetVitalSet()->OnDeath.Broadcast(Player,nullptr);
	TestTrue(TEXT("death notification starts the actual respawn timer"),Player->GetRespawnComponent()->IsRespawnPending());
	HUD->RefreshRespawnPanel();
	TestEqual(TEXT("death HUD requests the level-wide overview"),Capture->GetCaptureRequestCountForTesting(),1);
	TestEqual(TEXT("late distant point is represented by the HUD"),HUD->RespawnTacticalCaptureMarkers.Num(),1);
	TestEqual(TEXT("HUD marker belongs to the actual distant point"),HUD->RespawnCapturePointOrder[0].Get(),Far);
	AMSCapturePoint* Late=Scope.World->SpawnActor<AMSCapturePoint>(FVector(-100000,0,0),FRotator::ZeroRotator);
	HUD->RefreshRespawnPanel();
	TestEqual(TEXT("another late point is added during death"),HUD->RespawnTacticalCaptureMarkers.Num(),2);
	TestEqual(TEXT("death refresh reuses the same overview"),Capture->GetCaptureRequestCountForTesting(),1);
	if (HUD->RespawnTacticalMapImage->GetBrush().GetResourceObject())
	{
		TestTrue(TEXT("rendering HUD uses the overview resource"),HUD->RespawnTacticalMapImage->GetBrush().GetResourceObject()==Capture->GetOverviewTexture());
		const auto MarkerDesignCenter=[HUD]()
		{
			const FAnchors Anchors=CastChecked<UCanvasPanelSlot>(HUD->RespawnTacticalPlayerMarker->Slot)->GetAnchors();
			return FVector2D((Anchors.Minimum.X+Anchors.Maximum.X)*960.0f,
				(Anchors.Minimum.Y+Anchors.Maximum.Y)*540.0f);
		};
		const FVector2D First=MarkerDesignCenter();
		const FVector FirstWorld=Player->GetActorLocation();
		TestTrue(TEXT("fixture moves the pawn to a second death position"),Player->SetActorLocation(FVector(100000,0,0)));
		HUD->RefreshRespawnPanel();
		const FVector2D Second=MarkerDesignCenter();
		const FVector2D ExpectedDelta=UMSMapWidgetBase::WorldToMapLocalRectCentered(
			Player->GetActorLocation(),Capture->GetMapWorldCenter(),Capture->GetMapWorldHalfExtents(),
			FVector2D(468.0f),0.0f)
			-UMSMapWidgetBase::WorldToMapLocalRectCentered(FirstWorld,
				Capture->GetMapWorldCenter(),Capture->GetMapWorldHalfExtents(),FVector2D(468.0f),0.0f);
		TestTrue(TEXT("southward death position moves marker down on the north-up image"),
			Second.Y>First.Y&&FVector2D::Distance(Second-First,ExpectedDelta)<0.05f);
	}
	Far->Destroy();HUD->RefreshRespawnPanel();
	TestEqual(TEXT("removed point disappears from death HUD"),HUD->RespawnTacticalCaptureMarkers.Num(),1);
	TestEqual(TEXT("remaining marker still belongs to the late point"),HUD->RespawnCapturePointOrder[0].Get(),Late);
	Player->GetRespawnComponent()->Deploy(nullptr,FTransform::Identity);HUD->RefreshRespawnPanel();
	TestFalse(TEXT("valid sortie consumes the death timer"),Player->GetRespawnComponent()->IsRespawnPending());
	TestEqual(TEXT("return to play collapses the death screen"),HUD->RespawnPanel->GetVisibility(),ESlateVisibility::Collapsed);
	TestEqual(TEXT("return to play does not request another capture"),Capture->GetCaptureRequestCountForTesting(),1);
	return true;
}

bool FMSHUDTeamGaugeTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDTeamGaugeWorld"));
	FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();
	AMSGameState* State=World->SpawnActor<AMSGameState>();World->SetGameState(State);
	State->MaxGauge=10000.0f;State->PlayerGauge=2500.0f;State->EnemyGauge=7500.0f;
	APlayerController* Controller=World->SpawnActor<APlayerController>();
	AMSPlayerState* PlayerState=World->SpawnActor<AMSPlayerState>();Controller->PlayerState=PlayerState;
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>();ViewModel->Initialize(Controller);
	PlayerState->SetSelectedFaction(MSTeam::PlayerTeamId());
	const FMSHUDBattleSnapshot First=ViewModel->GetBattle();
	TestEqual(TEXT("first team sees its depleted friendly gauge"),First.TeamARatio,0.25f);
	TestEqual(TEXT("first team sees opposing gauge"),First.TeamBRatio,0.75f);
	PlayerState->SetSelectedFaction(MSTeam::EnemyTeamId());
	const FMSHUDBattleSnapshot Second=ViewModel->GetBattle();
	TestEqual(TEXT("second team sees its own gauge as friendly"),Second.TeamARatio,0.75f);
	TestEqual(TEXT("second team sees first team as hostile"),Second.TeamBRatio,0.25f);
	TestEqual(TEXT("tactical amount follows the owning team"),Second.Tactical,7500);
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
	return true;
}

namespace
{
	/**
	 * タスク#220: 誘導捕捉サークルはHUDに描かれる円(MSHUDPalette::HomingCursorSize)と
	 * 同一の狭い円錐になった(1920x1080/FOV60°で全角16.09°=半角8.04°)。
	 * 旧実装の固定60°(半角30°)前提で置かれていた敵座標は円の外になるため、
	 * 実カメラ基準で「円の内側」へ置き直す。判定は対象の照準点(胸元)で行われるので、
	 * Actor位置ではなく照準点そのものを目標位置へ合わせる。
	 */
	void PlaceEnemyInHomingCircle(const AMSPlayerCharacter* Player,AMSPlayerCharacter* Enemy,
		const float ForwardCm,const float RightCm)
	{
		const UMSCameraComponent* Camera=Player->GetMSCamera();
		const FVector Desired=Camera->GetComponentLocation()
			+Camera->GetForwardVector()*ForwardCm+Camera->GetRightVector()*RightCm;
		Enemy->SetActorLocation(Enemy->GetActorLocation()+(Desired-Enemy->GetLockOnAimPoint()),
			false,nullptr,ETeleportType::TeleportPhysics);
	}
}

bool FMSHUDHomingCircleLockParityTest::RunTest(const FString&)
{
	const float DrawRadius = UMSHUDRingWidget::ComputeDrawRadius(
		MSHUDPalette::HomingCursorSize, MSHUDPalette::HomingCursorThickness);
	TestEqual(TEXT("HUD homing ring radius and lock-on radius share one value"),
		MSHUDPalette::HomingCursorDrawRadius, DrawRadius, 0.001f);
	return true;
}

bool FMSHUDSniperScopePresentationTest::RunTest(const FString&)
{
	TestTrue(TEXT("scope mask uses enough primitive bands for a smooth circular aperture"),
		UMSHUDScopeWidget::MaskSegments >= 96);
	TestTrue(TEXT("scope reticle has graduated ticks"),UMSHUDScopeWidget::ReticleTickCountPerSide >= 6);

	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDSniperScopeWorld"));
	FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSRangedWeaponData* Sniper=NewObject<UMSRangedWeaponData>(World);Sniper->bIsScopeWeapon=true;Sniper->CategoryTag=MSTags::Weapon_Category_BeamRifle;
	Player->GetWeaponComponent()->InitializeSlots({Sniper});
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);

	HUD->RefreshCombatForAutomationTests();
	TestEqual(TEXT("normal weapon view always shows the center cross"),HUD->GetReticleVisibilityForAutomationTests(),ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("scope overlay starts hidden"),HUD->GetScopeOverlayVisibilityForAutomationTests(),ESlateVisibility::Collapsed);
	Player->ToggleLockOnOrScope();HUD->RefreshCombatForAutomationTests();
	TestTrue(TEXT("right click route enters the existing camera scope state"),Player->GetMSCamera()->IsScoped());
	TestEqual(TEXT("normal center cross is replaced while scoped"),HUD->GetReticleVisibilityForAutomationTests(),ESlateVisibility::Collapsed);
	TestEqual(TEXT("scope overlay is visible only while scoped"),HUD->GetScopeOverlayVisibilityForAutomationTests(),ESlateVisibility::HitTestInvisible);
	Player->ToggleLockOnOrScope();HUD->RefreshCombatForAutomationTests();
	TestFalse(TEXT("second right click route exits scope"),Player->GetMSCamera()->IsScoped());
	TestEqual(TEXT("center cross returns after leaving scope"),HUD->GetReticleVisibilityForAutomationTests(),ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("scope overlay hides after leaving scope"),HUD->GetScopeOverlayVisibilityForAutomationTests(),ESlateVisibility::Collapsed);

	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

bool FMSHUDPresetVoiceCutInSnapshotTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDPresetVoiceCutInWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	APlayerController* Controller=World->SpawnActor<APlayerController>();
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>(Controller);ViewModel->AddToRoot();ViewModel->Initialize(Controller);
	UMSGameInstance* GameInstance=NewObject<UMSGameInstance>();GameInstance->AddToRoot();
	UMSVoiceCutInSubsystem* VoiceSubsystem=NewObject<UMSVoiceCutInSubsystem>(GameInstance);VoiceSubsystem->AddToRoot();
	ViewModel->SetVoiceCutInSubsystemForTesting(VoiceSubsystem);

	const double RequestTime=World->GetTimeSeconds();
	FMSVoiceCutInRequest Request;Request.Channel=EMSVoiceCutInChannel::Left;Request.SpeakerName=FText::FromString(TEXT("ALLY-01"));Request.Line=FText::FromString(TEXT("ここは任せろ！"));Request.VoiceType=EMSVoiceType::Serious;Request.PortraitColor=FLinearColor(0.15f,0.55f,1.0f,1.0f);Request.Trigger=EMSVoiceTrigger::Kill;Request.Priority=3;Request.RequestTime=RequestTime;
	TestTrue(TEXT("cut-in request is accepted"),VoiceSubsystem->SubmitRequest(Request,RequestTime));
	FMSHUDCutInsSnapshot Snapshot=ViewModel->GetCutIns();
	TestTrue(TEXT("GetCutIns reflects the active left request"),Snapshot.Left.bVisible);
	TestFalse(TEXT("left request does not leak into the independent right channel"),Snapshot.Right.bVisible);
	TestEqual(TEXT("speaker name is copied exactly"),Snapshot.Left.SpeakerName.ToString(),FString(TEXT("ALLY-01")));
	TestEqual(TEXT("line is copied exactly"),Snapshot.Left.Line.ToString(),FString(TEXT("ここは任せろ！")));
	TestEqual(TEXT("serious voice letter is 真"),Snapshot.Left.VoiceTypeLetter.ToString(),FString(TEXT("真")));
	TestEqual(TEXT("portrait color is copied exactly"),Snapshot.Left.PortraitColor,Request.PortraitColor);

	for(int32 I=0;I<32;++I){World->Tick(LEVELTICK_All,0.1f);++GFrameCounter;}
	VoiceSubsystem->Tick(0.0f);
	FMSVoiceCutInRequest ExpiredRequest;
	TestFalse(TEXT("subsystem has cleared its active request after the 3.0 second display"),VoiceSubsystem->GetActiveRequest(EMSVoiceCutInChannel::Left,ExpiredRequest));
	Snapshot=ViewModel->GetCutIns();
	TestTrue(TEXT("cut-in remains visible during the 0.3 second fade after the 3.0 second hold"),Snapshot.Left.bVisible);
	TestTrue(TEXT("elapsed time has passed the 3.0 second hold"),Snapshot.Left.Elapsed>3.0f);
	for(int32 I=0;I<5;++I){World->Tick(LEVELTICK_All,0.1f);++GFrameCounter;}
	Snapshot=ViewModel->GetCutIns();
	TestFalse(TEXT("cut-in is hidden after 3.0 seconds plus the 0.3 second fade"),Snapshot.Left.bVisible);

	VoiceSubsystem->RemoveFromRoot();GameInstance->RemoveFromRoot();ViewModel->RemoveFromRoot();
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
bool FMSHUDDPTest::RunTest(const FString&){float P;FLinearColor C;UMSHUDViewModel::EvaluateDP(.69f,false,false,P,C);TestEqual(TEXT("0.69 normal"),C,MSHUDPalette::DPNormal);UMSHUDViewModel::EvaluateDP(.70f,false,false,P,C);TestEqual(TEXT("0.70 warning"),C,MSHUDPalette::DPWarning);UMSHUDViewModel::EvaluateDP(.1f,true,false,P,C);TestEqual(TEXT("stagger full"),P,1.f);TestEqual(TEXT("stagger warning"),C,MSHUDPalette::DPWarning);return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDBoostColorTest,"NeoMSGO.UI.HUD.BoostColorRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
// 4回目の仕様修正: OH中(State.Overheated=80%回復まで継続)は赤、タグ解除で即通常色。黄色フェーズ廃止
bool FMSHUDBoostColorTest::RunTest(const FString&){bool R=false;TestEqual(TEXT("normal boost uses existing color"),UMSHUDViewModel::EvaluateBoostColor(1.f,false,R),MSHUDPalette::BoostFill);TestEqual(TEXT("overheat is red"),UMSHUDViewModel::EvaluateBoostColor(0.f,true,R),MSHUDPalette::BoostOverheated);TestEqual(TEXT("overheat stays red while recovering (tag persists to 80 percent)"),UMSHUDViewModel::EvaluateBoostColor(.5f,true,R),MSHUDPalette::BoostOverheated);TestEqual(TEXT("tag release at 80 percent restores normal color immediately"),UMSHUDViewModel::EvaluateBoostColor(.8f,false,R),MSHUDPalette::BoostFill);TestFalse(TEXT("recovery latch clears"),R);return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDBoostNativePathTest,"NeoMSGO.UI.HUD.BoostColorNativeTickPath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDBoostNativePathTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDBoostNativePathWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
	UMSAttributeSet_Boost* Boost=Player->GetBoostSet();UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent();Boost->SetBoost(0.0f);Boost->TriggerOverheat();
	TestTrue(TEXT("real player ASC receives State.Overheated"),ASC->HasMatchingGameplayTag(MSTags::State_Overheated));HUD->RefreshGaugesForAutomationTests();TestEqual(TEXT("NativeTick gauge updater applies OH red"),HUD->GetBoostFillColorForAutomationTests(),MSHUDPalette::BoostOverheated);
	// OH 中も実回復量を表示する。0 から回復する赤バーで、満量固定にはしない。
	TestEqual(TEXT("OH shows actual empty boost before recovery"),HUD->GetBoostPercentForAutomationTests(),0.0f);
	// 回復途中(タグ保持中)は実残量を赤で表示(4回目の仕様修正: 黄色フェーズ廃止・80%までタグ継続)
	Boost->SetBoost(50.0f);HUD->RefreshGaugesForAutomationTests();TestEqual(TEXT("recovering below 80 percent stays red while tag persists"),HUD->GetBoostFillColorForAutomationTests(),MSHUDPalette::BoostOverheated);
	TestEqual(TEXT("recovering bar shows actual amount"),HUD->GetBoostPercentForAutomationTests(),0.5f);
	// 80%到達=State.Overheated解除(実プレイでは回復GE経由でMSAttributeSet_Boostが自動除去)で通常色へ
	FGameplayTagContainer Tags;Tags.AddTag(MSTags::State_Overheated);ASC->RemoveActiveEffectsWithGrantedTags(Tags);
	Boost->SetBoost(80.0f);HUD->RefreshGaugesForAutomationTests();TestEqual(TEXT("80 percent restores normal through widget path"),HUD->GetBoostFillColorForAutomationTests(),MSHUDPalette::BoostFill);
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDSlotTextTest,"NeoMSGO.UI.HUD.SlotText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDSlotTextTest::RunTest(const FString&){const FText Ammo=FText::FromString(TEXT("12/60")),Target=FText::FromString(TEXT("Enemy"));TestEqual(TEXT("reload"),UMSHUDViewModel::ComposeSlotStatus(true,false,Ammo,false,false,Target).ToString(),FString(TEXT("RELOAD")));TestEqual(TEXT("empty"),UMSHUDViewModel::ComposeSlotStatus(false,true,Ammo,false,false,Target).ToString(),FString(TEXT("EMPTY")));TestEqual(TEXT("normal"),UMSHUDViewModel::ComposeSlotStatus(false,false,Ammo,false,false,Target).ToString(),FString(TEXT("12/60")));TestEqual(TEXT("lock"),UMSHUDViewModel::ComposeSlotStatus(false,false,Ammo,true,true,Target).ToString(),FString(TEXT("12/60  LOCK:Enemy")));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDIconsTest,"NeoMSGO.UI.HUD.Icons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDIconsTest::RunTest(const FString&)
{
	const FGameplayTag Tags[]={MSTags::Weapon_Category_MachineGun,MSTags::Weapon_Category_BeamRifle,MSTags::Weapon_Category_Bazooka,MSTags::Weapon_Category_Melee,MSTags::Weapon_Category_Shield,MSTags::Weapon_Category_Supply,MSTags::Weapon_Category_ContinuousBeam,MSTags::Weapon_Category_Shotgun,MSTags::Weapon_Category_Homing};
	TSet<FString> Names,Colors,Silhouettes;
	for(const auto& T:Tags)
	{
		auto S=FMSHUDTypes::GetIconStyle(T);
		Names.Add(S.Abbreviation);Colors.Add(S.Color.ToString());
		TestNotEqual(TEXT("mapped"),S.Abbreviation,FString(TEXT("??")));
		TestEqual(TEXT("icon style carries the category tag for the silhouette widget"),S.Category,T);
		// 実プレイ報告対応: 各カテゴリは略号ではなく固有のシルエット(矩形集合)を持つ
		const TArray<FBox2D> Boxes=UMSWeaponIconWidget::GetSilhouetteBoxes(T);
		TestTrue(TEXT("silhouette has at least two shapes"),Boxes.Num()>=2);
		FString Serialized;
		for(const FBox2D& Box:Boxes)Serialized+=FString::Printf(TEXT("(%.2f,%.2f,%.2f,%.2f)"),Box.Min.X,Box.Min.Y,Box.Max.X,Box.Max.Y);
		Silhouettes.Add(Serialized);
		for(const FBox2D& Box:Boxes)
			TestTrue(TEXT("silhouette boxes stay inside the normalized icon area"),
				Box.Min.X>=0.0f&&Box.Min.Y>=0.0f&&Box.Max.X<=1.0f&&Box.Max.Y<=1.0f&&Box.Min.X<Box.Max.X&&Box.Min.Y<Box.Max.Y);
	}
	TestEqual(TEXT("nine abbreviations"),Names.Num(),9);TestEqual(TEXT("nine colors"),Colors.Num(),9);
	TestEqual(TEXT("nine distinct silhouettes"),Silhouettes.Num(),9);
	TestTrue(TEXT("unknown category still yields a fallback silhouette"),
		UMSWeaponIconWidget::GetSilhouetteBoxes(FGameplayTag()).Num()>=1);
	TestEqual(TEXT("machine gun uses reference FA badge"),FMSHUDTypes::GetFireModeLabel(MSTags::Weapon_Category_MachineGun).ToString(),FString(TEXT("FA")));
	TestEqual(TEXT("beam rifle uses reference SA badge"),FMSHUDTypes::GetFireModeLabel(MSTags::Weapon_Category_BeamRifle).ToString(),FString(TEXT("SA")));
	TestEqual(TEXT("melee equipment uses reference ALL badge"),FMSHUDTypes::GetFireModeLabel(MSTags::Weapon_Category_Melee).ToString(),FString(TEXT("ALL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDOverlayColorTest,"NeoMSGO.UI.HUD.OverlayColors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDOverlayColorTest::RunTest(const FString&){TestEqual(TEXT("enemy fill"),UMSHUDViewModel::SelectUnitBarColor(false),FLinearColor(.9f,.15f,.15f,1));TestEqual(TEXT("ally BOT fill"),UMSHUDViewModel::SelectUnitBarColor(true),FLinearColor(.2f,.85f,.35f,1));return true;}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDLockMarkerSnapshotTest,"NeoMSGO.UI.HUD.LockMarkerSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDLockMarkerSnapshotTest::RunTest(const FString&){const FVector2D Position(123,456);TestFalse(TEXT("no target hides marker"),UMSHUDViewModel::MakeLockOnMarkerSnapshot(nullptr,true,Position).bVisible);AActor* Target=NewObject<AActor>();const auto Visible=UMSHUDViewModel::MakeLockOnMarkerSnapshot(Target,true,Position);TestTrue(TEXT("projected target shows marker"),Visible.bVisible);TestFalse(TEXT("normal lock snapshot is not red"),Visible.bRedLocked);TestEqual(TEXT("projected position retained"),Visible.ScreenPosition,Position);TestFalse(TEXT("projection failure hides marker"),UMSHUDViewModel::MakeLockOnMarkerSnapshot(Target,false,Position).bVisible);const auto Red=UMSHUDViewModel::MakeLockOnMarkerSnapshot(Target,true,Position,0.0f,true);TestTrue(TEXT("red lock snapshot carries red state"),Red.bRedLocked);UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();UMSHUDLockOnMarkerWidget* Marker=const_cast<UMSHUDLockOnMarkerWidget*>(HUD->GetLockOnMarkerForAutomationTests());TestNotNull(TEXT("lock marker widget exists"),Marker);if(Marker){TestTrue(TEXT("reference lock outline remains thin"),Marker->GetOuterThickness()<=3.0f);TestTrue(TEXT("coloured stroke is narrower than its contrast edge"),Marker->GetInnerThickness()>0 && Marker->GetInnerThickness()<Marker->GetOuterThickness());TestEqual(TEXT("normal lock uses subtle dark contrast edge"),Marker->GetOuterColor(),MSHUDPalette::LockOnMarkerOuter);TestEqual(TEXT("normal lock marker inner edge is yellow"),Marker->GetInnerColor(),MSHUDPalette::LockOnMarkerNormal);Marker->SetRedLocked(true);TestEqual(TEXT("red lock keeps its subtle contrast edge"),Marker->GetOuterColor(),MSHUDPalette::LockOnMarkerOuter);TestEqual(TEXT("red lock marker inner edge turns red"),Marker->GetInnerColor(),MSHUDPalette::LockOnMarker);}const UMSHUDRingWidget* Charge=HUD->GetChargeRingForAutomationTests();TestNotNull(TEXT("charge ring exists"),Charge);if(Charge){TestTrue(TEXT("charge progress starts at 12 o'clock"),Charge->IsProgressOriginTop());}return true;}

bool FMSHUDHomingPresentationTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDHomingPresentationWorld"));
	FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,Params);
	AMSPlayerCharacter* Enemy=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(1000,200,100),FRotator::ZeroRotator,Params);
	Player->SetGenericTeamId(MSTeam::PlayerTeamId());Enemy->SetGenericTeamId(MSTeam::EnemyTeamId());
	APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	Player->GetMSCamera()->SetWorldLocation(FVector(0,0,140));Player->GetMSCamera()->SetWorldRotation(FRotator::ZeroRotator);
	UMSHomingWeaponData* Weapon=NewObject<UMSHomingWeaponData>(World);Weapon->CategoryTag=MSTags::Weapon_Category_Homing;Weapon->MagazineSize=2;Weapon->Range=30.0f;Weapon->LockOnRange=30.0f;
	Player->GetWeaponComponent()->InitializeSlots({Weapon});
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->AddToRoot();VM->Initialize(Controller);
	auto Tick=[World](float Seconds){while(Seconds>0.0f){const float Step=FMath::Min(Seconds,0.01f);World->Tick(LEVELTICK_All,Step);Seconds-=Step;++GFrameCounter;}};

	// #220: 捕捉サークル(半角8.04°)の内側 約4.3° へ置く。
	PlaceEnemyInHomingCircle(Player,Enemy,4000.0f,300.0f);
	Tick(0.01f);
	const FMSHUDCombatSnapshot Entered=VM->GetCombat();
	TestTrue(TEXT("enemy in the homing circle is exposed through the ViewModel"),Entered.bHomingTargetInCircle);
	TestEqual(TEXT("cursor becomes the specified red as soon as an enemy enters"),UMSHUDViewModel::SelectHomingCursorColor(Entered.bHomingTargetInCircle),MSHUDPalette::HomingCursorTracking);
	TestEqual(TEXT("dwell starts at zero through the ViewModel"),Entered.DwellProgress,0.0f);
	Tick(0.9f);
	const FMSHUDCombatSnapshot Captured=VM->GetCombat();
	TestTrue(TEXT("target is captured after the real dwell interval"),Captured.bHomingCaptured);
	TestEqual(TEXT("dwell reaches one through the ViewModel"),Captured.DwellProgress,1.0f);
	Enemy->SetActorLocation(FVector(-3000,0,100));Tick(0.02f);
	const FMSHUDCombatSnapshot Empty=VM->GetCombat();
	TestFalse(TEXT("cursor state clears after the enemy leaves the circle"),Empty.bHomingTargetInCircle);
	TestEqual(TEXT("empty homing circle uses the specified pale color"),UMSHUDViewModel::SelectHomingCursorColor(Empty.bHomingTargetInCircle),MSHUDPalette::HomingCursorIdle);

	VM->RemoveFromRoot();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

// FMSHUDHomingPresentationTest verifies the ViewModel snapshot; this test drives the same scenario
// through the actual UMSHUDWidget so the two on-screen rings (wide tracking cursor + captured-target
// gauge) are exercised end to end, matching the FMSHUDBoostNativePathTest widget-level pattern.
bool FMSHUDHomingWidgetRingTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDHomingWidgetRingWorld"));
	FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,Params);
	AMSPlayerCharacter* Enemy=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(1000,200,100),FRotator::ZeroRotator,Params);
	Player->SetGenericTeamId(MSTeam::PlayerTeamId());Enemy->SetGenericTeamId(MSTeam::EnemyTeamId());
	APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	Player->GetMSCamera()->SetWorldLocation(FVector(0,0,140));Player->GetMSCamera()->SetWorldRotation(FRotator::ZeroRotator);
	UMSHomingWeaponData* Weapon=NewObject<UMSHomingWeaponData>(World);Weapon->CategoryTag=MSTags::Weapon_Category_Homing;Weapon->MagazineSize=2;Weapon->Range=30.0f;Weapon->LockOnRange=30.0f;
	Player->GetWeaponComponent()->InitializeSlots({Weapon});
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->AddToRoot();HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
	auto Tick=[World](float Seconds){while(Seconds>0.0f){const float Step=FMath::Min(Seconds,0.01f);World->Tick(LEVELTICK_All,Step);Seconds-=Step;++GFrameCounter;}};

	// UnrealEditor-Cmd has no LocalPlayer viewport, so only the projection success is supplied;
	// candidate/color/dwell values still come from the real lock-on component through the ViewModel.
	// #220: 捕捉サークル(半角8.04°)の内側 約4.3° へ置く。
	PlaceEnemyInHomingCircle(Player,Enemy,4000.0f,300.0f);
	Tick(0.01f);HUD->RefreshCombatForAutomationTests(true);
	const UMSHUDRingWidget* HomingRing=HUD->GetHomingRingForAutomationTests();
	const UMSHUDRingWidget* TargetRing=HUD->GetHomingTargetRingForAutomationTests();
	TestNotNull(TEXT("wide tracking cursor ring exists"),HomingRing);
	TestNotNull(TEXT("captured-target gauge ring exists"),TargetRing);
	if(!HomingRing||!TargetRing)
	{
		HUD->RemoveFromRoot();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;
	}
	TestEqual(TEXT("tracking cursor turns the specified red once an enemy enters the circle"),
		HomingRing->GetColor(),MSHUDPalette::HomingCursorTracking);
	TestEqual(TEXT("tracking cursor renders at the wide, thick-lined size"),
		HomingRing->GetArcThickness(),MSHUDPalette::HomingCursorThickness);
	TestEqual(TEXT("captured-target gauge starts at zero dwell"),TargetRing->GetProgress(),0.0f);
	TestEqual(TEXT("target ring is visible while a candidate is on screen"),
		TargetRing->GetVisibility(),ESlateVisibility::HitTestInvisible);

	Tick(0.9f);HUD->RefreshCombatForAutomationTests(true);
	TestEqual(TEXT("captured-target gauge fills to one at capture"),TargetRing->GetProgress(),1.0f);
	TestEqual(TEXT("captured-target gauge uses the specified yellow progress color"),
		TargetRing->GetColor(),MSHUDPalette::HomingTargetProgress);

	Enemy->SetActorLocation(FVector(-3000,0,100));Tick(0.02f);HUD->RefreshCombatForAutomationTests(true);
	TestEqual(TEXT("tracking cursor returns to the pale idle color once the enemy leaves"),
		HomingRing->GetColor(),MSHUDPalette::HomingCursorIdle);
	TestEqual(TEXT("target gauge hides once there is no candidate to project"),
		TargetRing->GetVisibility(),ESlateVisibility::Collapsed);

	HUD->RemoveFromRoot();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDMinimapProjectionTest,"NeoMSGO.UI.HUD.MinimapProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDMinimapProjectionTest::RunTest(const FString&)
{
	const FVector Center(1000,2000,300);const FVector2D Size(256,256);
	const float DiameterCm=MSHUDPalette::MinimapOrthoWidth;
	const float RadiusCm=DiameterCm*.5f;
	TestEqual(TEXT("local minimap nominal radius is 1000 metres"),RadiusCm,100000.0f);
	const auto AtCenter=UMSHUDViewModel::ProjectWorldToMinimap(Center,Center,DiameterCm,Size);
	TestTrue(TEXT("local circle contains its centre"),UMSHUDViewModel::IsInsideLocalMinimapCircle(AtCenter,Size));
	TestTrue(TEXT("player center is in bounds"),AtCenter.bInBounds);TestEqual(TEXT("player maps to UI center"),AtCenter.Position,FVector2D(128,128));
	const auto NorthEdge=UMSHUDViewModel::ProjectWorldToMinimap(Center+FVector(RadiusCm,0,900),Center,DiameterCm,Size);
	TestTrue(TEXT("positive X boundary is in bounds"),NorthEdge.bInBounds);TestTrue(TEXT("positive X maps to bottom when world north is yaw 180"),NorthEdge.Position.Equals(FVector2D(128,256),0.01f));
	TestTrue(TEXT("1000m point lies on the exact circle rim"),UMSHUDViewModel::IsInsideLocalMinimapCircle(NorthEdge,Size,0.0f));
	const auto EastEdge=UMSHUDViewModel::ProjectWorldToMinimap(Center+FVector(0,RadiusCm,-900),Center,DiameterCm,Size);
	const auto Corner=UMSHUDViewModel::ProjectWorldToMinimap(Center+FVector(RadiusCm,RadiusCm,0),Center,DiameterCm,Size);
	TestTrue(TEXT("square projector reaches the corner"),Corner.bInBounds);
	TestFalse(TEXT("circular local map clips the corner marker"),UMSHUDViewModel::IsInsideLocalMinimapCircle(Corner,Size));
	TestTrue(TEXT("positive Y boundary is in bounds"),EastEdge.bInBounds);TestTrue(TEXT("positive Y maps to left when world north is yaw 180"),EastEdge.Position.Equals(FVector2D(0,128),0.01f));
	const auto Outside=UMSHUDViewModel::ProjectWorldToMinimap(Center+FVector(-RadiusCm-1.0f,0,0),Center,DiameterCm,Size);
	TestFalse(TEXT("past boundary is out of bounds"),Outside.bInBounds);TestTrue(TEXT("out-of-range position clamps to top edge"),Outside.Position.Equals(FVector2D(128,0),0.01f));
	TestFalse(TEXT("point beyond 1000m is outside the local circle"),UMSHUDViewModel::IsInsideLocalMinimapCircle(Outside,Size,0.0f));
	const FVector2D FullMapSize(256,256);
	const FVector2D FullMapHalfExtents(RadiusCm);
	const FVector2D LocalDelta=NorthEdge.Position-AtCenter.Position;
	const FVector2D FullMapDelta=UMSMapWidgetBase::WorldToMapLocalRectCentered(
		Center+FVector(RadiusCm,0,0),Center,FullMapHalfExtents,FullMapSize,0.0f)
		-UMSMapWidgetBase::WorldToMapLocalRectCentered(Center,Center,FullMapHalfExtents,FullMapSize,0.0f);
	TestTrue(TEXT("local minimap marker and full map use the same north-up axes"),LocalDelta.Equals(FullMapDelta,0.01f));
	TestEqual(TEXT("player minimap icon stays screen-space sized"),MSHUDPalette::MinimapPlayerIconSize,22.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDMinimapTeamMarkerTest,"NeoMSGO.UI.HUD.MinimapTeamMarker",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDMinimapTeamMarkerTest::RunTest(const FString&)
{
	const auto Enemy=UMSHUDViewModel::MakeMinimapUnitMarker(MSTeam::PlayerTeamId(),MSTeam::EnemyTeamId());
	TestTrue(TEXT("hostile unit gets a minimap marker"),Enemy.bVisible);TestEqual(TEXT("enemy marker is red"),Enemy.Color,MSMapPalette::Enemy);
	const auto Ally=UMSHUDViewModel::MakeMinimapUnitMarker(MSTeam::PlayerTeamId(),MSTeam::PlayerTeamId());
	TestTrue(TEXT("friendly unit gets a minimap marker"),Ally.bVisible);TestTrue(TEXT("friendly marker carries distinct shape state"),Ally.bFriendly);TestEqual(TEXT("friendly marker is blue"),Ally.Color,MSMapPalette::Friendly);
	TestFalse(TEXT("enemy marker is not tagged as friendly"),Enemy.bFriendly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDKillLogTest,"NeoMSGO.UI.HUD.KillLog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDKillLogTest::RunTest(const FString&)
{
	const FMSHUDKillLogEntry PlayerDeath=FMSHUDKillLog::MakeEntry(TEXT("PlayerMS"),true);
	TestEqual(TEXT("player death text"),PlayerDeath.Text.ToString(),FString(TEXT("PlayerMSは撃破された!")));
	TestEqual(TEXT("player death red"),PlayerDeath.Color,MSHUDPalette::KillLogPlayerDeath);
	const FMSHUDKillLogEntry EnemyDeath=FMSHUDKillLog::MakeEntry(TEXT("EnemyMS"),false);
	TestEqual(TEXT("enemy death text"),EnemyDeath.Text.ToString(),FString(TEXT("EnemyMSを撃破!")));
	TestEqual(TEXT("enemy death green"),EnemyDeath.Color,MSHUDPalette::KillLogEnemyDeath);
	const FMSHUDKillLogEntry Timed=FMSHUDKillLog::MakeTimedEntry(FText::FromString(TEXT("14'31")),TEXT("EnemyMS"),false);
	TestEqual(TEXT("actual event time prefixes the battle log"),Timed.Text.ToString(),FString(TEXT("14'31  EnemyMSを撃破!")));

	TArray<FMSHUDKillLogEntry> Entries;
	for(int32 I=0;I<6;++I)FMSHUDKillLog::AddEntry(Entries,FMSHUDKillLog::MakeEntry(FString::Printf(TEXT("Unit%d"),I),false));
	TestEqual(TEXT("line count is capped"),Entries.Num(),MSHUDPalette::MaxKillLogLines);
	TestEqual(TEXT("oldest line is removed"),Entries[0].Text.ToString(),FString(TEXT("Unit1を撃破!")));
	FMSHUDKillLog::TickEntries(Entries,3.99f);TestEqual(TEXT("entries survive before lifetime"),Entries.Num(),5);
	FMSHUDKillLog::TickEntries(Entries,0.01f);TestEqual(TEXT("entries expire at four seconds"),Entries.Num(),0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDBattleLayoutTest,"NeoMSGO.UI.HUD.BattleLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDBattleLayoutTest::RunTest(const FString&)
{
	// R4-1: mission type derives from uncaptured (non-home, non-player-owned) point count.
	const auto Defense=UMSHUDViewModel::MakeTaskSnapshot(0);
	TestTrue(TEXT("all points owned means defense"),Defense.bDefense);
	TestEqual(TEXT("defense mission label"),Defense.MissionType.ToString(),FString(TEXT("防衛")));
	TestEqual(TEXT("defense mission english"),Defense.MissionTypeEnglish.ToString(),FString(TEXT("DEFENCE")));
	TestEqual(TEXT("defense task text"),Defense.TaskText.ToString(),FString(TEXT("本拠点を防衛せよ!")));
	const auto Attack=UMSHUDViewModel::MakeTaskSnapshot(2);
	TestFalse(TEXT("remaining points mean attack"),Attack.bDefense);
	TestEqual(TEXT("attack mission label"),Attack.MissionType.ToString(),FString(TEXT("攻撃")));
	TestEqual(TEXT("attack task text"),Attack.TaskText.ToString(),FString(TEXT("拠点を制圧せよ!")));

	// R4-1: lock-on snapshot carries the target distance in meters for the reticle readout.
	AActor* Target=NewObject<AActor>();
	const auto Locked=UMSHUDViewModel::MakeLockOnMarkerSnapshot(Target,true,FVector2D(10,20),305.0f);
	TestTrue(TEXT("locked marker visible"),Locked.bVisible);
	TestEqual(TEXT("distance meters retained"),Locked.DistanceMeters,305.0f);
	TestTrue(TEXT("known 123.45m world distance is displayed within five percent"),
		FMath::IsNearlyEqual(UMSHUDViewModel::WorldDistanceToMeters(12345.0f),123.45f,123.45f*0.05f));

	// R4-1: each confirmed kill adds KillScorePoints to the bottom-left SCORE readout.
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>();
	TestEqual(TEXT("score starts at zero"),ViewModel->GetScore().Score,0);
	ViewModel->ProcessKillConfirmed(1.0);ViewModel->ProcessKillConfirmed(2.0);
	TestEqual(TEXT("two kills accumulate score"),ViewModel->GetScore().Score,2*MSHUDPalette::KillScorePoints);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDCapturePlayerRangeTest,"NeoMSGO.UI.HUD.CapturePlayerRange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDCapturePlayerRangeTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDCapturePlayerRangeWorld"));
	FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSCapturePoint* Point=World->SpawnActor<AMSCapturePoint>(AMSCapturePoint::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	AMSBotCharacter* AllyBot=World->SpawnActor<AMSBotCharacter>(AMSBotCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	AllyBot->SetGenericTeamId(MSTeam::PlayerTeamId());
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(Point->GetCaptureRadius()+100.0f,0.0f,0.0f),FRotator::ZeroRotator,Params);
	APlayerController* Controller=World->SpawnActor<APlayerController>(APlayerController::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	Controller->Possess(Player);
	Point->Tick(0.01f);

	TestTrue(TEXT("Precondition: an allied unit is inside the capture radius"),Point->IsLocalPlayerInRange());
	UMSHUDViewModel* ViewModel=NewObject<UMSHUDViewModel>();
	ViewModel->Initialize(Controller);
	TestFalse(TEXT("Capture ring is hidden when only an allied bot is in range"),ViewModel->GetCapture().bVisible);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDModuleIndicatorTest,"NeoMSGO.UI.HUD.ModuleIndicator",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDModuleIndicatorTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDModuleWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	TestFalse(TEXT("module indicator hidden without equipment"),VM->GetModule().bVisible);

	UMSModuleData* Module=NewObject<UMSModuleData>(GetTransientPackage());Module->ModuleName=FText::FromString(TEXT("TWIN DRIVE SYSTEM"));
	UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(GetTransientPackage());Suit->EquippedModule=Module;Player->InitializeUnit(Suit);
	auto Ready=VM->GetModule();TestTrue(TEXT("equipped module indicator visible"),Ready.bVisible);TestEqual(TEXT("module name comes from equipped data"),Ready.ModuleName.ToString(),FString(TEXT("TWIN DRIVE SYSTEM")));TestTrue(TEXT("idle module displays data name and READY"),Ready.StateText.ToString().Contains(TEXT("TWIN DRIVE SYSTEM  READY")));
	UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent();
	auto Apply=[ASC](TSubclassOf<UGameplayEffect> Effect,const FGameplayTag& DurationTag,const FGameplayTag& StateTag)
	{
		FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(Effect,1.0f,ASC->MakeEffectContext());Spec.Data->SetSetByCallerMagnitude(DurationTag,5.0f);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		return ASC->HasMatchingGameplayTag(StateTag);
	};
	TestTrue(TEXT("active GE applied"),Apply(UGE_ModuleEffect::StaticClass(),MSTags::Data_Module_Duration,MSTags::State_Module_EffectActive));auto Active=VM->GetModule();TestTrue(TEXT("active text"),Active.StateText.ToString().Contains(TEXT("ACTIVE")));TestTrue(TEXT("active has remaining time"),Active.RemainingSeconds>0.0f);
	FGameplayTagContainer Remove;Remove.AddTag(MSTags::State_Module_EffectActive);ASC->RemoveActiveEffectsWithGrantedTags(Remove);
	TestTrue(TEXT("forced GE applied"),Apply(UGE_ModuleForcedOverheat::StaticClass(),MSTags::Data_Module_OverheatDuration,MSTags::State_Module_ForcedOverheat));auto Overheat=VM->GetModule();TestTrue(TEXT("overheat text"),Overheat.StateText.ToString().Contains(TEXT("OVERHEAT")));TestTrue(TEXT("overheat has remaining time"),Overheat.RemainingSeconds>0.0f);
	Remove.Reset();Remove.AddTag(MSTags::State_Module_ForcedOverheat);ASC->RemoveActiveEffectsWithGrantedTags(Remove);
	TestTrue(TEXT("cooldown GE applied"),Apply(UGE_ModuleCooldown::StaticClass(),MSTags::Data_Module_Cooldown,MSTags::State_Module_Cooldown));auto Cooldown=VM->GetModule();TestTrue(TEXT("cooldown text"),Cooldown.StateText.ToString().Contains(TEXT("COOLDOWN")));TestTrue(TEXT("cooldown has remaining time"),Cooldown.RemainingSeconds>0.0f);
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDEmergencyRecallIndicatorTest,"NeoMSGO.UI.HUD.EmergencyRecallIndicator",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDEmergencyRecallIndicatorTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDEmergencyRecallWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
	APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(Player);Suit->EmergencyRecallPerformance=1000;Player->InitializeUnit(Suit);
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(GetTransientPackage());HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
	TestFalse(TEXT("recall indicator hidden while inactive"),VM->GetEmergencyRecall().bVisible);

	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UMSEmergencyRecallComponent* Recall=Player->GetEmergencyRecallComponent();
	Recall->SetRequestPendingForTesting();
	TestTrue(TEXT("unacknowledged request is shown as pending"),VM->GetEmergencyRecall().bPending);
	TestFalse(TEXT("unacknowledged request is not a running countdown"),VM->GetEmergencyRecall().bActive);
	HUD->RefreshEmergencyRecallIndicator();
	TestTrue(TEXT("pending recall does not advertise a cancel key"),HUD->EmergencyRecallDetailText->GetText().IsEmpty());
	Recall->ApplyAuthorityNoticeForTesting(EMSRecallNotice::Rejected,EMSRecallReason::Airborne);
	TestTrue(TEXT("authority rejection is visible"),VM->GetEmergencyRecall().bVisible);
	TestTrue(TEXT("rejection text explains the reason"),VM->GetEmergencyRecall().StatusText.ToString().Contains(TEXT("着地")));
	TestTrue(TEXT("recall starts for grounded player"),Recall&&Recall->TryStartRecall());
	const FMSHUDEmergencyRecallSnapshot Active=VM->GetEmergencyRecall();
	TestTrue(TEXT("recall indicator visible while active"),Active.bActive);
	TestTrue(TEXT("active recall exposes positive remaining seconds"),Active.RemainingSeconds>0.0f);
	TestFalse(TEXT("active recall VM never promises manual cancellation"),Active.DetailText.ToString().Contains(TEXT("[Z]"))||Active.DetailText.ToString().Contains(TEXT("中断")));
	TestFalse(TEXT("living recall does not become a death respawn"),VM->GetRespawn().bVisible);
	TestEqual(TEXT("active snapshot has full duration"),Active.DurationSeconds,5.0f);
	TestTrue(TEXT("fresh recall progress starts at zero"),FMath::IsNearlyZero(Active.Progress));
	// A newly-created world's first TimerManager tick activates pending timers;
	// subsequent frames advance the countdown. Exercise real frames, not a VM override.
	for (int32 Frame=0; Frame<3; ++Frame) { World->Tick(LEVELTICK_All,.1f);++GFrameCounter; }
	const FMSHUDEmergencyRecallSnapshot Advanced=VM->GetEmergencyRecall();
	TestTrue(TEXT("authority timer remaining time decreases"),Advanced.RemainingSeconds<Active.RemainingSeconds);
	TestTrue(TEXT("progress follows authoritative timer"),Advanced.Progress>0.0f);
	TestTrue(TEXT("progress matches elapsed fraction"),FMath::IsNearlyEqual(Advanced.Progress,1.0f-Advanced.RemainingSeconds/Advanced.DurationSeconds));
	HUD->RefreshEmergencyRecallIndicator();
	TestEqual(TEXT("active countdown is the upper RESPAWN panel"),HUD->EmergencyRecallActivePanel->GetVisibility(),ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("active recall does not duplicate the lower notice"),HUD->EmergencyRecallPanel->GetVisibility(),ESlateVisibility::Collapsed);
	TestTrue(TEXT("countdown uses reference hundredths separator"),HUD->EmergencyRecallCountdownText->GetText().ToString().Contains(TEXT("’")));
	auto PanelText=[](const UCanvasPanel* Panel)
	{
		FString Text;
		if(Panel)for(int32 Child=0;Child<Panel->GetChildrenCount();++Child)
			if(const UTextBlock* Label=Cast<UTextBlock>(Panel->GetChildAt(Child)))Text+=Label->GetText().ToString()+TEXT("\n");
		return Text;
	};
	const FString RecallLabels=PanelText(HUD->EmergencyRecallActivePanel);
	TestFalse(TEXT("actual active recall widgets have no misleading cancel prompt"),RecallLabels.Contains(TEXT("[Z]"))||RecallLabels.Contains(TEXT("中断")));
	// MSGO9 136s: living full-HP recall itself reads RESPAWN / 再出撃まで.
	TestTrue(TEXT("reference recall title is preserved"),RecallLabels.Contains(TEXT("R E S P A W N")));
	TestTrue(TEXT("reference recall caption is preserved"),RecallLabels.Contains(TEXT("再出撃まで")));
	if(HUD->bVideoHudArtPass)
	{
		const FString DeathLabels=PanelText(HUD->RespawnPanel);
		TestTrue(TEXT("death respawn title remains unchanged"),DeathLabels.Contains(TEXT("R E S P A W N")));
		TestTrue(TEXT("death respawn caption remains unchanged"),DeathLabels.Contains(TEXT("再出撃まで")));
		TestEqual(TEXT("living recall does not show the death panel"),HUD->RespawnPanel->GetVisibility(),ESlateVisibility::Collapsed);
	}
	// Regression: the actual cut-in child rectangles must clear the recall frame,
	// including either channel, then return to the ordinary position after cancel.
	HUD->RefreshVoiceCutIns();
	if (TestTrue(TEXT("recall collision regression uses the reference HUD"),HUD->bVideoHudArtPass))
	{
		const FVector2D DesignSize(1920.0,1080.0);
		const FAnchors RecallBounds=CastChecked<UCanvasPanelSlot>(HUD->EmergencyRecallActivePanel->GetChildAt(0)->Slot)->GetAnchors();
		const FVector2D RecallMin=RecallBounds.Minimum*DesignSize,RecallMax=RecallBounds.Maximum*DesignSize;
		for (UCanvasPanel* VoicePanel : HUD->VoiceCutInPanels)
		{
			const FAnchors VoiceBounds=CastChecked<UCanvasPanelSlot>(VoicePanel->Slot)->GetAnchors();
			const FVector2D VoiceOrigin=VoiceBounds.Minimum*DesignSize;
			const FVector2D VoiceSize=(VoiceBounds.Maximum-VoiceBounds.Minimum)*DesignSize;
			for (int32 Child=0;Child<VoicePanel->GetChildrenCount();++Child)
			{
				const FAnchors ChildBounds=CastChecked<UCanvasPanelSlot>(VoicePanel->GetChildAt(Child)->Slot)->GetAnchors();
				const FVector2D Min=VoiceOrigin+ChildBounds.Minimum*VoiceSize,Max=VoiceOrigin+ChildBounds.Maximum*VoiceSize;
				TestTrue(TEXT("voice portrait and dialogue do not cover recall timer"),
					Max.X<=RecallMin.X || Min.X>=RecallMax.X || Max.Y<=RecallMin.Y || Min.Y>=RecallMax.Y);
			}
		}
	}

	Recall->CancelRecall(EMSRecallReason::Movement);
	const FMSHUDEmergencyRecallSnapshot Cancelled=VM->GetEmergencyRecall();
	TestFalse(TEXT("active countdown ends after cancellation"),Cancelled.bActive);
	TestTrue(TEXT("cancellation shows one non-intrusive notice"),Cancelled.bVisible);
	TestTrue(TEXT("notice identifies movement interruption"),Cancelled.StatusText.ToString().Contains(TEXT("移動")));
	TestTrue(TEXT("notice includes retry cooldown"),Cancelled.CooldownSeconds>4.9f);
	TestEqual(TEXT("inactive recall has no remaining seconds"),Cancelled.RemainingSeconds,0.0f);
	HUD->RefreshEmergencyRecallIndicator();
	TestEqual(TEXT("cancelled countdown panel disappears"),HUD->EmergencyRecallActivePanel->GetVisibility(),ESlateVisibility::Collapsed);
	TestEqual(TEXT("cancellation uses lower notice"),HUD->EmergencyRecallPanel->GetVisibility(),ESlateVisibility::HitTestInvisible);
	HUD->RefreshVoiceCutIns();
	for (UCanvasPanel* VoicePanel : HUD->VoiceCutInPanels)
		TestTrue(TEXT("cancel restores normal voice placement"),CastChecked<UCanvasPanelSlot>(VoicePanel->Slot)->GetAnchors().Minimum.IsNearlyZero());
	Player->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Dead);
	TestFalse(TEXT("death suppresses recall notice and cooldown"),VM->GetEmergencyRecall().bVisible);
	HUD->RefreshEmergencyRecallIndicator();
	TestEqual(TEXT("death hides recall notice"),HUD->EmergencyRecallPanel->GetVisibility(),ESlateVisibility::Collapsed);
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDReloadAuthoritativeSnapshotTest,"NeoMSGO.UI.HUD.ReloadAuthoritativeSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMSHUDReloadAuthoritativeSnapshotTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDReloadAuthoritativeWorld"));FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());World->BeginPlay();if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);APlayerController* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
	UMSRangedWeaponData* Primary=NewObject<UMSRangedWeaponData>(World);Primary->MagazineSize=2;Primary->ReserveAmmo=4;Primary->ReloadTime=2.0f;Primary->WeaponSwitchDuration=0.0f;Primary->FireInterval=0.0f;
	UMSRangedWeaponData* Sidearm=NewObject<UMSRangedWeaponData>(World);Sidearm->MagazineSize=1;Sidearm->ReserveAmmo=2;Sidearm->ReloadTime=1.0f;Sidearm->WeaponSwitchDuration=0.0f;Sidearm->FireInterval=0.0f;
	UMSWeaponComponent* Weapons=Player->GetWeaponComponent();Weapons->InitializeSlots({Primary,Sidearm,nullptr});
	TestTrue(TEXT("one shot leaves a partial magazine"),Weapons->ConsumeAmmoAndStartCooldown(0));
	TestTrue(TEXT("explicit reload starts from the partial magazine"),Weapons->StartReload(0));
	TestEqual(TEXT("component owns the configured reload duration"),Weapons->GetReloadDuration(0),2.0f);
	// UWorld clamps a single large delta. Drive five real 0.1s ticks instead.
	for(int32 Tick=0;Tick<5;++Tick) { World->Tick(LEVELTICK_All,0.1f);++GFrameCounter; }
	UMSHUDViewModel* First=NewObject<UMSHUDViewModel>(Controller);First->Initialize(Controller);
	const TArray<FMSHUDWeaponSlotSnapshot> FirstSlots=First->GetWeaponSlots();
	TestEqual(TEXT("HUD sees the authoritative remaining time"),FirstSlots[0].ReloadRemaining,1.5f);
	TestEqual(TEXT("HUD sees the authoritative start duration"),FirstSlots[0].ReloadDuration,2.0f);
	TestTrue(TEXT("HUD progress is derived from the component clock"),FirstSlots[0].ReloadProgress>.24f&&FirstSlots[0].ReloadProgress<.26f);
	UMSHUDViewModel* Recreated=NewObject<UMSHUDViewModel>(Controller);Recreated->Initialize(Controller);
	const TArray<FMSHUDWeaponSlotSnapshot> RecreatedSlots=Recreated->GetWeaponSlots();
	TestEqual(TEXT("recreated VM retains reload progress instead of restarting it"),RecreatedSlots[0].ReloadProgress,FirstSlots[0].ReloadProgress);
	TestTrue(TEXT("switching keeps a nonselected reload progressing"),Weapons->SwitchToSlot(1));
	for(int32 Tick=0;Tick<5;++Tick) { World->Tick(LEVELTICK_All,0.1f);++GFrameCounter; }
	const TArray<FMSHUDWeaponSlotSnapshot> BackgroundSlots=Recreated->GetWeaponSlots();
	TestTrue(TEXT("nonselected slot still reports reload progress"),BackgroundSlots[0].ReloadProgress>.49f&&BackgroundSlots[0].ReloadProgress<.51f);
	TestEqual(TEXT("null slot keeps its visible position"),BackgroundSlots.Num(),3);
	TestEqual(TEXT("null slot remains an empty row"),BackgroundSlots[2].Status.ToString(),FString(TEXT("EMPTY")));
	TestFalse(TEXT("null slot has no weapon icon"),BackgroundSlots[2].bHasWeapon);
	TestFalse(TEXT("null slot is never selected"),BackgroundSlots[2].bSelected);
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
