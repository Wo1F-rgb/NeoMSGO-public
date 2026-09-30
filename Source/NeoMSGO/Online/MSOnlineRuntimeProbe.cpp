#include "Online/MSOnlineRuntimeProbe.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Gameplay/MSPlayerController.h"
#include "Gameplay/MSPlayerState.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSCapturePoint.h"
#include "Gameplay/MSHomeBase.h"
#include "Core/MSTeamTypes.h"
#include "Core/MSTeamStatics.h"
#include "Player/MSPlayerCharacter.h"
#include "Player/MSRespawnComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Online/MSRoomSubsystem.h"
#include "AI/MSBotCharacter.h"
#include "Engine/NetDriver.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Weapons/MSWeaponComponent.h"
#include "UI/MSUILockSubsystem.h"
#include "UI/MSDeckSubsystem.h"
#include "UI/MSHangarWidget.h"
#include "UI/MSSuitDetailWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

bool UMSOnlineRuntimeProbe::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	FString Directory;
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE)
		&& FParse::Value(FCommandLine::Get(), TEXT("MSOnlineProbeDir="), Directory) && !Directory.IsEmpty();
#endif
}
void UMSOnlineRuntimeProbe::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	FString Directory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MSOnlineProbeDir="), Directory)) return;
	Directory = FPaths::ConvertRelativePathToFull(Directory);
	IFileManager::Get().MakeDirectory(*Directory, true);
	OutputPath = Directory / FString::Printf(TEXT("runtime-%u-%d.json"), FPlatformProcess::GetCurrentProcessId(), World.GetUniqueID());
	// Frontend worlds are intentionally paused. Read diagnostics on the process
	// ticker so login/viewport evidence remains available without unpausing play.
	ProbeTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMSOnlineRuntimeProbe::TickSnapshot), 0.2f);
}
void UMSOnlineRuntimeProbe::Deinitialize()
{
	FTSTicker::RemoveTicker(ProbeTicker);
	Super::Deinitialize();
}
bool UMSOnlineRuntimeProbe::TickSnapshot(float DeltaTime)
{
	WriteSnapshot();
	return GetWorld() != nullptr;
}
void UMSOnlineRuntimeProbe::WriteSnapshot()
{
	UWorld* World = GetWorld();
	if (!World || OutputPath.IsEmpty()) return;
	auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("captured_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("world"), World->GetPathName());
	Root->SetNumberField(TEXT("net_mode"), World->GetNetMode());
	Root->SetNumberField(TEXT("world_seconds"), World->GetTimeSeconds());
	Root->SetBoolField(TEXT("world_paused"), World->IsPaused());
	if (const AMSGameState* State = World->GetGameState<AMSGameState>())
	{
		Root->SetNumberField(TEXT("server_seconds"), State->GetServerWorldTimeSeconds());
		Root->SetNumberField(TEXT("match_remaining"), State->RemainingTime);
		Root->SetBoolField(TEXT("battle_ended"), State->bBattleEnded);
		Root->SetNumberField(TEXT("player_states"), State->PlayerArray.Num());
		Root->SetBoolField(TEXT("player_hosted_room"), State->bPlayerHostedRoom);
		if (State->bPlayerHostedRoom)
		{
			Root->SetStringField(TEXT("room_map_id"), State->HostedRoomSettings.MapId.ToString());
			Root->SetNumberField(TEXT("room_bot_count"), State->HostedRoomSettings.BotCount);
			Root->SetBoolField(TEXT("room_fill_teams_with_bots"), State->HostedRoomSettings.bFillTeamsWithBots);
			Root->SetNumberField(TEXT("room_capacity"), State->HostedRoomSettings.GetMaxCombatants());
			Root->SetNumberField(TEXT("room_rule"), State->HostedRoomSettings.RuleIndex);
			Root->SetNumberField(TEXT("room_difficulty"), static_cast<int32>(State->HostedRoomSettings.BotDifficulty));
		}
	}
	int32 AlliedBots = 0, EnemyBots = 0;
	for (TActorIterator<AMSBotCharacter> It(World); It; ++It)
	{
		if (!IsValid(*It)) continue;
		if (UMSTeamStatics::IsOnTeam(*It, MSTeam::PlayerTeamId())) ++AlliedBots;
		else if (UMSTeamStatics::IsOnTeam(*It, MSTeam::EnemyTeamId())) ++EnemyBots;
	}
	Root->SetNumberField(TEXT("allied_bot_seats"), AlliedBots);
	Root->SetNumberField(TEXT("enemy_bot_seats"), EnemyBots);
	if (UGameInstance* Instance = World->GetGameInstance())
	{
		if (auto* Rooms = Instance->GetSubsystem<UMSRoomSubsystem>())
		{
			Root->SetBoolField(TEXT("room_operation_pending"), Rooms->IsRoomOperationPending());
			Root->SetStringField(TEXT("room_status"), Rooms->GetRoomStatusText().ToString());
			Root->SetStringField(TEXT("hosted_room_id"), Rooms->GetHostedSession().RoomId);
			Root->SetStringField(TEXT("hosted_instance_id"), Rooms->GetHostedSession().InstanceId);
		}
		if (auto* Lock = Instance->GetSubsystem<UMSUILockSubsystem>())
		{
			Root->SetBoolField(TEXT("ui_screen_open"), Lock->IsAnyScreenOpen());
			Root->SetBoolField(TEXT("ui_gameplay_active"), Lock->IsGameplayActive());
		}
		if (auto* Account = Instance->GetSubsystem<UMSOnlineAccountSubsystem>())
		{
			Root->SetNumberField(TEXT("login_state"), static_cast<int32>(Account->GetLoginState()));
			Root->SetStringField(TEXT("local_account"), Account->GetIdentity().AccountId);
			Root->SetNumberField(TEXT("deck_revision"), Account->GetCommittedDeck().Revision);
			TArray<TSharedPtr<FJsonValue>> AccountSlots;
			for (const FMSOnlineDeckSlot& Slot : Account->GetCommittedDeck().Slots)
			{
				auto Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("suit"), Slot.SuitId.ToString());
				TArray<TSharedPtr<FJsonValue>> Weapons;
				for (const FPrimaryAssetId& Id : Slot.WeaponIds) Weapons.Add(MakeShared<FJsonValueString>(Id.ToString()));
				Row->SetArrayField(TEXT("weapons"), Weapons);
				AccountSlots.Add(MakeShared<FJsonValueObject>(Row));
			}
			Root->SetArrayField(TEXT("account_deck"), AccountSlots);
		}
		if (auto* Deck = Instance->GetSubsystem<UMSDeckSubsystem>())
		{
			Root->SetStringField(TEXT("deck_instance"), Deck->GetPathName());
			Root->SetNumberField(TEXT("deck_selected_slot"), Deck->GetSelectedSlotIndex());
			TArray<TSharedPtr<FJsonValue>> DeckSlots;
			for (int32 Index = 0; Index < Deck->GetSlotCount(); ++Index)
			{
				auto Row = MakeShared<FJsonObject>();
				const UMSMobileSuitData* Suit = Deck->GetPresetInSlot(Index);
				Row->SetStringField(TEXT("suit"), Suit ? Suit->GetPrimaryAssetId().ToString() : FString());
				TArray<TSharedPtr<FJsonValue>> Weapons;
				// Do not resolve/load assets here: diagnostics must not repair a stale view.
				for (const auto& Weapon : Deck->GetLoadoutInSlot(Index).EquippedWeapons)
					Weapons.Add(MakeShared<FJsonValueString>(Weapon.Get() ? Weapon.Get()->GetPrimaryAssetId().ToString() : Weapon.ToSoftObjectPath().ToString()));
				Row->SetArrayField(TEXT("weapons"), Weapons);
				DeckSlots.Add(MakeShared<FJsonValueObject>(Row));
			}
			Root->SetArrayField(TEXT("resolved_deck"), DeckSlots);
		}
		if (APlayerController* PC = Instance->GetFirstLocalPlayerController(World))
		{
			int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
			Root->SetNumberField(TEXT("viewport_width"), Width); Root->SetNumberField(TEXT("viewport_height"), Height);
			Root->SetBoolField(TEXT("cursor_visible"), PC->bShowMouseCursor);
			Root->SetBoolField(TEXT("ignore_move_input"), PC->IsMoveInputIgnored());
		}
	}
	TArray<TSharedPtr<FJsonValue>> Units;
	for (TActorIterator<AMSPlayerCharacter> It(World); It; ++It)
	{
		AMSPlayerCharacter* Pawn = *It;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("pawn"), Pawn->GetName());
		Row->SetStringField(TEXT("location"), Pawn->GetActorLocation().ToCompactString());
		Row->SetNumberField(TEXT("role"), static_cast<int32>(Pawn->GetLocalRole()));
		Row->SetNumberField(TEXT("x"), Pawn->GetActorLocation().X);
		Row->SetNumberField(TEXT("y"), Pawn->GetActorLocation().Y);
		Row->SetNumberField(TEXT("z"), Pawn->GetActorLocation().Z);
		if (const auto* Movement = Cast<UMSCharacterMovementComponent>(Pawn->GetCharacterMovement()))
		{
			Row->SetStringField(TEXT("velocity"), Movement->Velocity.ToCompactString());
			Row->SetNumberField(TEXT("speed"), Movement->Velocity.Size());
			Row->SetNumberField(TEXT("movement_mode"), static_cast<int32>(Movement->MovementMode));
			Row->SetNumberField(TEXT("max_speed"), Movement->GetMaxSpeed());
			Row->SetBoolField(TEXT("boost_dashing"), Movement->IsBoostDashing());
			Row->SetBoolField(TEXT("hover_ascending"), Movement->IsHoverAscending());
			Row->SetBoolField(TEXT("boost_input"), Movement->IsBoostInputHeld());
			Row->SetBoolField(TEXT("hover_input"), Movement->IsHoverInputHeld());
			Row->SetBoolField(TEXT("zero_g_ascend_input"), Movement->IsZeroGravityAscendInputHeld());
			Row->SetBoolField(TEXT("zero_g_descend_input"), Movement->IsZeroGravityDescendInputHeld());
		}
		if (const auto* Boost = Pawn->GetBoostSet()) Row->SetNumberField(TEXT("boost"), Boost->GetBoost());
		Row->SetBoolField(TEXT("local"), Pawn->IsLocallyControlled());
		Row->SetNumberField(TEXT("team"), Pawn->GetGenericTeamId().GetId());
		Row->SetBoolField(TEXT("hidden"), Pawn->IsHidden());
		Row->SetBoolField(TEXT("gameplay_input_allowed"), Pawn->IsGameplayInputAllowed());
		if (const auto* PC = Cast<AMSPlayerController>(Pawn->GetController()))
		{ Row->SetBoolField(TEXT("authenticated"), PC->IsOnlineAuthenticated()); Row->SetBoolField(TEXT("deployed"), PC->IsOnlineDeployed()); Row->SetNumberField(TEXT("assigned_team"), PC->GetOnlineAssignedTeamId().GetId()); }
		if (const auto* PS = Pawn->GetPlayerState<AMSPlayerState>()) Row->SetStringField(TEXT("player"), PS->GetPlayerName());
		if (const auto* Vital = Pawn->GetVitalSet())
		{ Row->SetNumberField(TEXT("armor"), Vital->GetArmor()); Row->SetNumberField(TEXT("max_armor"), Vital->GetMaxArmor()); }
		if (const auto* Suit = Pawn->GetMobileSuitData()) Row->SetStringField(TEXT("suit"), Suit->GetPrimaryAssetId().ToString());
		if (const auto* Weapon = Pawn->GetWeaponComponent())
		{
			Row->SetNumberField(TEXT("active_slot"), Weapon->GetActiveSlotIndex());
			Row->SetNumberField(TEXT("switch_end"), Weapon->GetSwitchEndServerTime());
			TArray<TSharedPtr<FJsonValue>> Slots;
			for (int32 Index = 0; Index < Weapon->GetSlotCount(); ++Index)
			{
				auto Slot = MakeShared<FJsonObject>();
				Slot->SetStringField(TEXT("weapon"), Weapon->GetWeaponData(Index) ? Weapon->GetWeaponData(Index)->GetPrimaryAssetId().ToString() : FString());
				Slot->SetNumberField(TEXT("magazine"), Weapon->GetMagazineAmmo(Index)); Slot->SetNumberField(TEXT("reserve"), Weapon->GetReserveAmmo(Index));
				Slot->SetNumberField(TEXT("reload_end"), Weapon->GetReloadEndServerTime(Index)); Slot->SetNumberField(TEXT("reload_remaining"), Weapon->GetReloadRemaining(Index));
				Slots.Add(MakeShared<FJsonValueObject>(Slot));
			}
			Row->SetArrayField(TEXT("weapons"), Slots);
		}
		if (const auto* Respawn = Pawn->GetRespawnComponent())
		{ Row->SetNumberField(TEXT("respawn_remaining"), Respawn->GetRespawnTimeRemaining()); Row->SetBoolField(TEXT("can_deploy"), Respawn->CanDeploy()); }
		Units.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("players"), Units);
	// Read both ends of the actual replication path when diagnosing deployment.
	// This opt-in probe never changes ownership or bypasses deployment policy.
	TArray<TSharedPtr<FJsonValue>> Points;
	for (TActorIterator<AMSCapturePoint> It(World); It; ++It)
	{
		const AMSCapturePoint* Point = *It;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actor"), Point->GetName());
		Row->SetStringField(TEXT("label"), Point->GetPointName().ToString());
		Row->SetStringField(TEXT("location"), Point->GetActorLocation().ToCompactString());
		Row->SetNumberField(TEXT("owner_team"), Point->GetOwnerTeamId().GetId());
		Row->SetBoolField(TEXT("team1_can_deploy"), Point->CanDeployFor(MSTeam::PlayerTeamId()));
		Row->SetBoolField(TEXT("team2_can_deploy"), Point->CanDeployFor(MSTeam::EnemyTeamId()));
		Row->SetBoolField(TEXT("always_relevant"), Point->bAlwaysRelevant);
		if (const auto* Home = Cast<AMSHomeBase>(Point))
		{
			Row->SetBoolField(TEXT("destroyed"), Home->IsHomeBaseDestroyed());
			Row->SetNumberField(TEXT("unit_team"), Home->GetGenericTeamId().GetId());
		}
		Points.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("capture_points"), Points);
	TArray<TSharedPtr<FJsonValue>> Screens;
	TArray<TSharedPtr<FJsonValue>> ScreenDetails;
	TArray<TSharedPtr<FJsonValue>> EquipmentViews;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		if (It->GetWorld() != World || !It->IsInViewport()) continue;
		if (It->IsVisible()) Screens.Add(MakeShared<FJsonValueString>(It->GetClass()->GetName()));
		if (It->IsVisible())
		{
			auto Screen = MakeShared<FJsonObject>();
			Screen->SetStringField(TEXT("class"), It->GetClass()->GetName());
			Screen->SetStringField(TEXT("widget"), It->GetPathName());
			Screen->SetStringField(TEXT("owner"), GetPathNameSafe(It->GetOwningPlayer()));
			Screen->SetNumberField(TEXT("opacity"), It->GetRenderOpacity());
			Screen->SetStringField(TEXT("size"), It->GetCachedGeometry().GetLocalSize().ToString());
			ScreenDetails.Add(MakeShared<FJsonValueObject>(Screen));
		}
		const auto* Detail = Cast<UMSSuitDetailWidget>(*It);
		const auto* Hangar = Cast<UMSHangarWidget>(*It);
		if (!Detail && !Hangar) continue;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("widget"), It->GetPathName());
		Row->SetBoolField(TEXT("visible"), It->IsVisible());
		if (const auto* Property = FindFProperty<FObjectPropertyBase>(It->GetClass(), TEXT("DeckSubsystem")))
		{
			const UObject* BoundDeck = Property->GetObjectPropertyValue_InContainer(*It);
			Row->SetStringField(TEXT("bound_deck"), BoundDeck ? BoundDeck->GetPathName() : FString());
		}
		if (Detail) Row->SetNumberField(TEXT("displayed_deck_slot"), Detail->GetDisplayedSlotIndex());
		if (Hangar)
		{
			Row->SetNumberField(TEXT("weapon_slot"), Hangar->GetSelectedSlotIndex());
			Row->SetNumberField(TEXT("current_load"), Hangar->GetCurrentLoad());
		}
		// Only equipment widgets, never login fields or credentials.
		TArray<TSharedPtr<FJsonValue>> Texts;
		if (It->WidgetTree)
		{
			TArray<UWidget*> Children; It->WidgetTree->GetAllWidgets(Children);
			for (const UWidget* Child : Children)
				if (const auto* Label = Cast<UTextBlock>(Child); Label && Label->IsVisible() && !Label->GetText().IsEmpty())
					Texts.Add(MakeShared<FJsonValueString>(Label->GetText().ToString()));
		}
		Row->SetArrayField(TEXT("labels"), Texts);
		EquipmentViews.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("visible_screens"), Screens);
	Root->SetArrayField(TEXT("screen_details"), ScreenDetails);
	Root->SetArrayField(TEXT("equipment_views"), EquipmentViews);
	FString Body; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Body));
	const FString Temporary = OutputPath + TEXT(".tmp");
	if (FFileHelper::SaveStringToFile(Body, *Temporary)) IFileManager::Get().Move(*OutputPath, *Temporary, true, true);
}
