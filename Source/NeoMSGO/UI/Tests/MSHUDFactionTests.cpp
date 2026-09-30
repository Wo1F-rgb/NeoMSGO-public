#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "UI/MSFactionPresentation.h"
#include "UI/MSHUDWidget.h"
#include "UI/MSHUDViewModel.h"
#include "UI/MSMapTypes.h"
#include "UI/MSUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDFactionPresentationTest,
	"NeoMSGO.UI.HUD.FactionPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHUDFactionPresentationTest::RunTest(const FString&)
{
	struct FRestoreMode
	{
		MSUIStyle::EUIMode Mode=MSUIStyle::GetSelectedMode();
		~FRestoreMode(){MSUIStyle::SetSelectedMode(Mode,false);}
	} RestoreMode;
	for(const MSUIStyle::EUIMode Mode : {MSUIStyle::EUIMode::TypeA,MSUIStyle::EUIMode::TypeB})
	{
		MSUIStyle::SetSelectedMode(Mode,false);
		UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDFactionWorld"));
		if(!TestNotNull(TEXT("world"),World))return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());World->BeginPlay();
		if(!World->GetBegunPlay())World->GetWorldSettings()->NotifyBeginPlay();
		AMSGameState* State=World->SpawnActor<AMSGameState>();World->SetGameState(State);
		State->MaxGauge=10000.0f;State->PlayerGauge=2500.0f;State->EnemyGauge=7500.0f;
		APlayerController* Controller=World->SpawnActor<APlayerController>();
		AMSPlayerState* PlayerState=World->SpawnActor<AMSPlayerState>();Controller->PlayerState=PlayerState;
		FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSPlayerCharacter* Player=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
		Controller->Possess(Player);
		UMSMobileSuitData* Suit=NewObject<UMSMobileSuitData>(Player);Player->InitializeUnit(Suit);
		UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
		UMSHUDWidget* HUD=NewObject<UMSHUDWidget>(Controller);HUD->Initialize();HUD->InitializeForAutomationTests();HUD->SetViewModel(VM);
		UTextBlock* Top=Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("PlayerFactionLabel")));
		UTextBlock* Bottom=Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("EnemyFactionLabel")));
		if(!TestNotNull(TEXT("top label"),Top)||!TestNotNull(TEXT("bottom label"),Bottom))
		{
			GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;
		}
		for(const bool bOrder : {false,true})
		{
			const FGenericTeamId Team=bOrder?MSTeam::EnemyTeamId():MSTeam::PlayerTeamId();
			PlayerState->SetSelectedFaction(Team);Player->SetGenericTeamId(Team);
			const FLinearColor ExpectedTop=bOrder?MSFactionPresentation::OrderColor:MSFactionPresentation::RebellionColor;
			const FLinearColor ExpectedBottom=bOrder?MSFactionPresentation::RebellionColor:MSFactionPresentation::OrderColor;
			for(const EMSMobileSuitFaction Provenance : {EMSMobileSuitFaction::Federation,EMSMobileSuitFaction::Principality})
			{
				Suit->Faction=Provenance;
				HUD->RefreshBattleWidgets();
				TestEqual(TEXT("changing suit provenance never changes the assigned top faction"),Top->GetText().ToString(),FString(bOrder?TEXT("ORDER"):TEXT("REBELLION")));
				TestEqual(TEXT("opposing faction is in the second row"),Bottom->GetText().ToString(),FString(bOrder?TEXT("REBELLION"):TEXT("ORDER")));
				TestEqual(TEXT("top gauge carries its own team's depleted amount"),HUD->TeamABar->GetPercent(),bOrder?0.75f:0.25f);
				TestEqual(TEXT("bottom gauge carries the opponent amount"),HUD->TeamBBar->GetPercent(),bOrder?0.25f:0.75f);
				TestEqual(TEXT("top native bar uses faction color in both modes"),HUD->TeamABar->GetFillColorAndOpacity(),ExpectedTop);
				TestEqual(TEXT("bottom native bar uses faction color in both modes"),HUD->TeamBBar->GetFillColorAndOpacity(),ExpectedBottom);
				TestEqual(TEXT("top text matches faction color"),Top->GetColorAndOpacity().GetSpecifiedColor(),ExpectedTop);
				TestEqual(TEXT("bottom text matches faction color"),Bottom->GetColorAndOpacity().GetSpecifiedColor(),ExpectedBottom);
				TestEqual(TEXT("vector top row receives the same assigned faction"),HUD->VideoBattleSnapshot.TeamAId.GetId(),Team.GetId());
				TestEqual(TEXT("vector bottom color follows opposing faction"),MSFactionPresentation::GaugeColor(HUD->VideoBattleSnapshot.TeamBId),ExpectedBottom);
			}
		}
		Controller->UnPossess();HUD->RefreshBattleWidgets();
		TestEqual(TEXT("Order remains selected through death/recall without a pawn"),Top->GetText().ToString(),FString(TEXT("ORDER")));
		TestEqual(TEXT("ally HP remains green independently of faction"),UMSHUDViewModel::SelectUnitBarColor(true),MSHUDPalette::AllyUnitBar);
		TestTrue(TEXT("ally HP has dominant green"),MSHUDPalette::AllyUnitBar.G>MSHUDPalette::AllyUnitBar.R&&MSHUDPalette::AllyUnitBar.G>MSHUDPalette::AllyUnitBar.B);
		TestEqual(TEXT("enemy HP remains red independently of faction"),UMSHUDViewModel::SelectUnitBarColor(false),MSHUDPalette::EnemyUnitBar);
		TestTrue(TEXT("map ally stays blue"),MSMapPalette::Friendly.B>MSMapPalette::Friendly.R&&MSMapPalette::Friendly.B>MSMapPalette::Friendly.G);
		GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSHUDFactionAuthorityTest,
	"NeoMSGO.UI.HUD.FactionAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSHUDFactionAuthorityTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("MSHUDFactionAuthorityWorld"));
	if(!TestNotNull(TEXT("world"),World))return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	// Do not begin/tick this world: the fixture never starts authentication or network requests.
	AMSPlayerController* Controller=World->SpawnActor<AMSPlayerController>();
	AMSPlayerState* PlayerState=World->SpawnActor<AMSPlayerState>();Controller->PlayerState=PlayerState;
	PlayerState->SetSelectedFaction(MSTeam::PlayerTeamId());
	AMSGameState* State=World->SpawnActor<AMSGameState>();World->SetGameState(State);
	State->MaxGauge=10000.0f;State->PlayerGauge=2500.0f;State->EnemyGauge=7500.0f;
	UMSHUDViewModel* VM=NewObject<UMSHUDViewModel>(Controller);VM->Initialize(Controller);
	FBoolProperty* Auth=FindFProperty<FBoolProperty>(Controller->GetClass(),TEXT("bOnlineAuthenticated"));
	FByteProperty* Team=FindFProperty<FByteProperty>(Controller->GetClass(),TEXT("OnlineAssignedTeamId"));
	if(!TestNotNull(TEXT("replicated authentication field"),Auth)||!TestNotNull(TEXT("replicated assigned team field"),Team))
	{
		GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;
	}
	{
		struct FCommandScope
		{
			FString Previous=FCommandLine::Get();
			FCommandScope(){FCommandLine::Set(*(FString(TEXT("-MSAuthMode=Online "))+Previous));}
			~FCommandScope(){FCommandLine::Set(*Previous);}
		} CommandScope;
		Team->SetPropertyValue_InContainer(Controller,MSTeam::EnemyTeamId().GetId());
		TestEqual(TEXT("pending admission never falls back to default Rebellion PlayerState"),MSFactionPresentation::ResolveMatchTeam(Controller).GetId(),FGenericTeamId::NoTeam.GetId());
		TestFalse(TEXT("pending admission has no valid faction gauge"),VM->GetBattle().bValid);
		Auth->SetPropertyValue_InContainer(Controller,true);
		TestEqual(TEXT("server assigned Order wins over stale Rebellion PlayerState"),MSFactionPresentation::ResolveMatchTeam(Controller).GetId(),MSTeam::EnemyTeamId().GetId());
		const FMSHUDBattleSnapshot Battle=VM->GetBattle();
		TestEqual(TEXT("authoritative Order owns the top gauge"),Battle.TeamAId.GetId(),MSTeam::EnemyTeamId().GetId());
		TestEqual(TEXT("authoritative Order receives its 75 percent gauge"),Battle.TeamARatio,0.75f);
		TestEqual(TEXT("authoritative Order receives its tactical amount"),Battle.Tactical,7500);
		Auth->SetPropertyValue_InContainer(Controller,false);
		TestFalse(TEXT("loss of admission clears previously assigned gauge"),VM->GetBattle().bValid);
	}
	GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
	return true;
}
#endif
