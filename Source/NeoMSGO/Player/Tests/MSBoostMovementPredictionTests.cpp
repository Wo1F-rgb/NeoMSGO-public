// Copyright NeoMSGO Project. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSModuleGameplayEffects.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamAgentActor.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSBoostMovementPrediction.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostMovementClockTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.Boost.ClockAndAuthority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostMovementReplayTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.Boost.CombineAndReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostMovementResponseTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.Boost.ResponsesAndEpochs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostMovementSerializationTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.Boost.Serialization",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FBoostMovementTestWorld
	{
		UWorld* World = nullptr;
		FBoostMovementTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSBoostMovementTestWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}
		~FBoostMovementTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		AMSPlayerCharacter* Spawn(float X = 0.0f)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
				AMSPlayerCharacter::StaticClass(), FVector(X, 0, 2000), FRotator::ZeroRotator, Params);
			UMSMobileSuitData* Data = NewObject<UMSMobileSuitData>(Unit);
			Data->BoostCapacity = 100.0f;
			Data->BoostConsumptionRate = 20.0f;
			Data->BoostRechargeRate = 15.0f;
			Unit->InitializeUnit(Data);
			UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
			Movement->SetComponentTickEnabled(false);
			Movement->bRunPhysicsWithNoController = true;
			Movement->SetMovementMode(MOVE_Falling);
			return Unit;
		}
		void Tick(float Seconds)
		{
			World->Tick(LEVELTICK_All, Seconds);
			++GFrameCounter;
		}
		void Floor(float X, float TopZ)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(10000, 10000, 100));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Actor->SetActorLocation(FVector(X, 0, TopZ - 100));
		}
	};

	FSavedMovePtr RecordMove(AMSPlayerCharacter* Unit, float TimeStamp, float DeltaTime, bool bHover = true)
	{
		UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
		FNetworkPredictionData_Client_Character* Data =
			static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
		Data->CurrentTimeStamp = TimeStamp;
		Movement->SetHoverInputHeld(bHover);
		FSavedMovePtr Move = Data->CreateSavedMove();
		Move->SetMoveFor(Unit, DeltaTime, FVector::ZeroVector, *Data);
		Movement->PerformMovement(DeltaTime);
		Move->PostUpdate(Unit, FSavedMove_Character::PostUpdate_Record);
		return Move;
	}
	FMSCharacterSavedMove& MSMove(const FSavedMovePtr& Move)
	{
		return static_cast<FMSCharacterSavedMove&>(*Move);
	}
	FMSCharacterMoveResponseDataContainer MakeCorrection(const FSavedMovePtr& Move,
		const FMSBoostMovementState& State, const FVector& Location, uint32 Sequence)
	{
		FMSCharacterMoveResponseDataContainer Response;
		Response.bHasBoostState = true;
		Response.AuthoritativeState = State;
		Response.ClientSimulationEpoch = MSMove(Move).ClientSimulationEpoch;
		Response.ResponseSequence = Sequence;
		Response.ClientAdjustment.TimeStamp = Move->TimeStamp;
		Response.ClientAdjustment.NewLoc = Location;
		Response.ClientAdjustment.NewVel = FVector::ZeroVector;
		Response.ClientAdjustment.MovementMode = MOVE_Falling;
		return Response;
	}

	bool ReadBoostResponseThroughWire(UMSCharacterMovementComponent& Server,
		UMSCharacterMovementComponent& Client, FMSCharacterMoveResponseDataContainer& OutResponse)
	{
		FMSCharacterMoveResponseDataContainer Captured;
		Captured.ServerFillResponseData(Server, Server.GetPredictionData_Server_Character()->PendingAdjustment);
		if (!Captured.bHasBoostState) return false;
		FBitWriter Writer(4096, true);
		if (!Captured.Serialize(Server, Writer, nullptr)) return false;
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		return OutResponse.Serialize(Client, Reader, nullptr);
	}
}

bool FMSBoostMovementClockTest::RunTest(const FString&)
{
	FBoostMovementTestWorld Scope;
	AMSPlayerCharacter* Immediate = Scope.Spawn(0);
	AMSPlayerCharacter* Batched = Scope.Spawn(50000);
	for (AMSPlayerCharacter* Unit : {Immediate, Batched})
	{
		// Only route selection is injected. dt validation, physics, GAS and callbacks
		// below are the real production paths; the world has no sockets/NetDriver.
		Unit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		Unit->GetMSCharacterMovement()->CaptureBoostMovementState();
	}
	TArray<FMSBoostMovementState> Expected;
	bool bSawExhaustion = false, bSawRelease = false;
	for (int32 Step = 1; Step <= 150; ++Step)
	{
		Scope.Tick(0.05f);
		FMSCharacterNetworkMoveData Move;
		Move.TimeStamp = Step * 0.05f;
		Move.CompressedMoveFlags = FSavedMove_Character::FLAG_Custom_1;
		Move.MovementMode = MOVE_Falling;
		Move.Location = Immediate->GetActorLocation();
		Immediate->GetMSCharacterMovement()->ServerMove_PerformMovement(Move);
		const FMSBoostMovementState State = Immediate->GetMSCharacterMovement()->CaptureBoostMovementState();
		bSawExhaustion |= State.bOverheated;
		bSawRelease |= bSawExhaustion && !State.bOverheated;
		Expected.Add(State);
		// Hold a 100ms batch before delivery, then process each accepted timestamp.
		if (Step % 3 == 0)
		{
			for (int32 Offset = 2; Offset >= 0; --Offset)
			{
				Move.TimeStamp = (Step - Offset) * 0.05f;
				Move.Location = Batched->GetActorLocation();
				Batched->GetMSCharacterMovement()->ServerMove_PerformMovement(Move);
				const FMSBoostMovementState Actual = Batched->GetMSCharacterMovement()->CaptureBoostMovementState();
				TestTrue(TEXT("same accepted timestamp has identical energy/OH despite delayed batch"),
					FMath::IsNearlyEqual(Actual.Boost, Expected[Step - Offset - 1].Boost, 0.001f) &&
					Actual.bOverheated == Expected[Step - Offset - 1].bOverheated);
			}
		}
	}
	TestTrue(TEXT("real consume GE reached OH"), bSawExhaustion);
	TestTrue(TEXT("real regen GE released OH at 80 percent under held hover"), bSawRelease);
	UMSCharacterMovementComponent* Movement = Immediate->GetMSCharacterMovement();
	const float BeforeDuplicate = Immediate->GetBoostSet()->GetBoost();
	FMSCharacterNetworkMoveData Duplicate;
	Duplicate.TimeStamp = 7.5f;
	Duplicate.CompressedMoveFlags = FSavedMove_Character::FLAG_Custom_1;
	Movement->ServerMove_PerformMovement(Duplicate);
	Duplicate.TimeStamp = 7.45f;
	Movement->ServerMove_PerformMovement(Duplicate);
	TestEqual(TEXT("duplicate and expired engine moves cannot consume boost"), Immediate->GetBoostSet()->GetBoost(), BeforeDuplicate);
	Scope.Tick(0.2f);
	TestEqual(TEXT("network clock disables timer debit/recovery"), Immediate->GetBoostSet()->GetBoost(), BeforeDuplicate);
	TestEqual(TEXT("configured recovery already contains exactly one x3 multiplier"), Movement->CaptureBoostMovementState().RechargePerSecond, 45.0f);
	Immediate->FindComponentByClass<UMSBoostComponent>()->SetModuleConsumptionMultiplier(0.5f);
	Immediate->GetAbilitySystemComponent()->AddLooseGameplayTag(MSTags::State_Module_EffectActive);
	TestEqual(TEXT("module consumption multiplier is captured from the existing driver"), Movement->CaptureBoostMovementState().ConsumptionPerSecond, 10.0f);
	Immediate->GetAbilitySystemComponent()->RemoveLooseGameplayTag(MSTags::State_Module_EffectActive);

	// Snapshot is immutable even when an ability spends energy before serialization.
	const FClientAdjustment Adjustment = Movement->GetPredictionData_Server_Character()->PendingAdjustment;
	FMSCharacterMoveResponseDataContainer ResponseBefore;
	ResponseBefore.ServerFillResponseData(*Movement, Adjustment);
	TestTrue(TEXT("accepted move captured response state"), ResponseBefore.bHasBoostState);
	UMSCombatStatics::ConsumeBoostOrOverheat(Immediate->GetAbilitySystemComponent(), 17.0f);
	FMSCharacterMoveResponseDataContainer ResponseAfter;
	ResponseAfter.ServerFillResponseData(*Movement, Adjustment);
	TestTrue(TEXT("later D-melee GE cannot relabel an older response timestamp"), ResponseBefore.AuthoritativeState.Matches(ResponseAfter.AuthoritativeState));
	TestFalse(TEXT("D-melee spend enters next authority state"), Movement->CaptureBoostMovementState().Matches(ResponseBefore.AuthoritativeState));

	UAbilitySystemComponent* ASC = Immediate->GetAbilitySystemComponent();
	FGameplayEffectSpecHandle Forced = ASC->MakeOutgoingSpec(UGE_ModuleForcedOverheat::StaticClass(), 1.0f, ASC->MakeEffectContext());
	Forced.Data->SetSetByCallerMagnitude(MSTags::Data_Module_OverheatDuration, 90.0f);
	ASC->ApplyGameplayEffectSpecToSelf(*Forced.Data);
	TestTrue(TEXT("module forced OH enters authority state"), Movement->CaptureBoostMovementState().bOverheated);
	ASC->AddLooseGameplayTag(MSTags::State_Dead);
	const FMSBoostMovementState Dead = Movement->CaptureBoostMovementState();
	Movement->PerformMovement(0.1f);
	TestTrue(TEXT("death enters correction state"), Dead.bDead);
	TestEqual(TEXT("death cannot regenerate boost"), Immediate->GetBoostSet()->GetBoost(), Dead.Boost);
	ASC->RemoveLooseGameplayTag(MSTags::State_Dead);
	const uint32 OldGeneration = Dead.Generation;
	UMSMobileSuitData* RespawnData = NewObject<UMSMobileSuitData>(Immediate);
	Immediate->InitializeUnit(RespawnData);
	TestTrue(TEXT("unit reinitialization advances authority generation"), Movement->CaptureBoostMovementState().Generation > OldGeneration);
	TestFalse(TEXT("respawn clears forced overheat"), Movement->CaptureBoostMovementState().bOverheated);

	// Stock forced movement uses the same common integration hook once.
	Immediate->SetAutonomousProxy(true);
	Movement->SetHoverInputHeld(true);
	const float BeforeForce = Immediate->GetBoostSet()->GetBoost();
	TestTrue(TEXT("stock ForcePositionUpdate executes"), Movement->ForcePositionUpdate(0.05f));
	TestTrue(TEXT("forced movement charges exactly its dt"), FMath::IsNearlyEqual(Immediate->GetBoostSet()->GetBoost(),
		BeforeForce - Immediate->FindComponentByClass<UMSBoostComponent>()->GetEffectiveConsumptionPerSecond() * 0.05f, 0.001f));
	FMSCharacterNetworkMoveData BadPosition;
	BadPosition.TimeStamp = 7.6f;
	BadPosition.CompressedMoveFlags = FSavedMove_Character::FLAG_Custom_1;
	BadPosition.MovementMode = MOVE_Falling;
	BadPosition.Location = Immediate->GetActorLocation() + FVector(100000, 0, 0);
	BadPosition.ReportedEndState = Movement->CaptureBoostMovementState();
	BadPosition.ReportedEndState.Advance(0.05f, true, false, false);
	// Isolate normal position validation from the earlier respawn's forced refresh.
	Movement->GetPredictionData_Server_Character()->bForceClientUpdate = false;
	Movement->ServerMove_PerformMovement(BadPosition);
	TestFalse(TEXT("bad position still produces standard server correction"), Movement->GetPredictionData_Server_Character()->PendingAdjustment.bAckGoodMove);
	TestTrue(TEXT("server never adopts a false client location"), FVector::Dist(Immediate->GetActorLocation(), BadPosition.Location) > 50000);
	const float AuthorityBase = Movement->TakeoffBaseZ;
	BadPosition.TimeStamp = 7.65f;
	BadPosition.ReportedEndState.bHasTakeoffBase = true;
	BadPosition.ReportedEndState.TakeoffBaseZ = 999999;
	Movement->ServerMove_PerformMovement(BadPosition);
	TestEqual(TEXT("client takeoff declaration cannot write the server's anchor"), Movement->TakeoffBaseZ, AuthorityBase);

	// Resume a deterministic checkpoint just before the production 240-second
	// timestamp boundary. The engine performs the reset/flags/validation itself;
	// neither a hand-written reset flag nor a shortened reset interval is used.
	{
		FBoostMovementTestWorld ResetServerScope;
		FBoostMovementTestWorld ResetOwnerScope;
		const float ResetInterval = GetDefault<UMSCharacterMovementComponent>()->MinTimeBetweenTimeStampResets;
		ResetServerScope.World->TimeSeconds = ResetInterval - 0.05f;
		ResetOwnerScope.World->TimeSeconds = ResetInterval - 0.05f;
		AMSPlayerCharacter* ResetServerUnit = ResetServerScope.Spawn();
		AMSPlayerCharacter* ResetOwnerUnit = ResetOwnerScope.Spawn();
		UMSCharacterMovementComponent* ResetServer = ResetServerUnit->GetMSCharacterMovement();
		UMSCharacterMovementComponent* ResetOwner = ResetOwnerUnit->GetMSCharacterMovement();
		ResetServerUnit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		ResetOwnerUnit->SetRole(ROLE_AutonomousProxy);
		ResetOwnerUnit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		ResetServer->CaptureBoostMovementState();
		ResetOwner->CaptureBoostMovementState();
		auto* ResetClient = static_cast<FNetworkPredictionData_Client_Character*>(ResetOwner->GetPredictionData_Client());
		auto* ResetServerData = ResetServer->GetPredictionData_Server_Character();
		ResetClient->CurrentTimeStamp = ResetInterval - 0.05f;
		ResetServerData->CurrentClientTimeStamp = ResetInterval - 0.05f;
		FSavedMovePtr BeforeResetPending;
		FMSCharacterNetworkMoveData BeforeResetRequest;
		FMSCharacterMoveResponseDataContainer BeforeResetResponse;
		for (int32 BeforeResetIndex = 0; BeforeResetIndex < 2; ++BeforeResetIndex)
		{
			ResetServerScope.Tick(0.05f);
			ResetOwnerScope.Tick(0.05f);
			const float AcceptedDelta = ResetClient->UpdateTimeStampAndDeltaTime(0.05f, *ResetOwnerUnit, *ResetOwner);
			FSavedMovePtr Accepted = RecordMove(ResetOwnerUnit, ResetClient->CurrentTimeStamp, AcceptedDelta);
			ResetClient->SavedMoves.Add(Accepted);
			FMSCharacterNetworkMoveData Request;
			Request.ClientFillNetworkMoveData(*Accepted, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
			ResetServer->ServerMove_PerformMovement(Request);
			FMSCharacterMoveResponseDataContainer Reply;
			if (!TestTrue(TEXT("pre-reset move obtains a real server response"), ReadBoostResponseThroughWire(*ResetServer, *ResetOwner, Reply))) return false;
			if (BeforeResetIndex == 0)
			{
				ResetOwner->ClientHandleMoveResponse(Reply);
				ResetOwner->ClientUpdatePositionAfterServerUpdate();
			}
			else
			{
				BeforeResetPending = Accepted;
				BeforeResetRequest = Request;
				BeforeResetResponse = Reply; // delayed across the engine reset
			}
		}
		const uint32 EpochBeforeReset = MSMove(BeforeResetPending).ClientSimulationEpoch;
		const float ServerBoostBeforeReset = ResetServerUnit->GetBoostSet()->GetBoost();
		ResetServerScope.Tick(0.05f);
		ResetOwnerScope.Tick(0.05f);
		const float ResetDelta = ResetClient->UpdateTimeStampAndDeltaTime(0.05f, *ResetOwnerUnit, *ResetOwner);
		TestTrue(TEXT("engine reset marked old buffered move"), BeforeResetPending->bOldTimeStampBeforeReset);
		TestTrue(TEXT("engine reset marked last acknowledged move"), ResetClient->LastAckedMove.IsValid() && ResetClient->LastAckedMove->bOldTimeStampBeforeReset);
		TestTrue(TEXT("engine itself reduced timestamp at the production reset interval"),
			ResetClient->CurrentTimeStamp > 0.0f && ResetClient->CurrentTimeStamp < 1.0f);
		const FSavedMovePtr AfterReset = RecordMove(ResetOwnerUnit, ResetClient->CurrentTimeStamp, ResetDelta);
		ResetClient->SavedMoves.Add(AfterReset);
		TestFalse(TEXT("new move after engine reset uses a fresh timestamp"), AfterReset->bOldTimeStampBeforeReset);
		TestEqual(TEXT("clock reset is not a unit reinitialization"), MSMove(AfterReset).ClientSimulationEpoch, EpochBeforeReset);
		const float OwnerBeforeOldReply = ResetOwner->GetPredictedBoostMovementState().Boost;
		ResetOwner->ClientHandleMoveResponse(BeforeResetResponse);
		TestEqual(TEXT("real pre-reset reply cannot overwrite current owner state"), ResetOwner->GetPredictedBoostMovementState().Boost, OwnerBeforeOldReply);
		TestTrue(TEXT("real pre-reset reply cannot ACK an engine-expired saved move"), ResetClient->SavedMoves.Contains(BeforeResetPending));
		FMSCharacterNetworkMoveData AfterResetRequest;
		AfterResetRequest.ClientFillNetworkMoveData(*AfterReset, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
		ResetServer->ServerMove_PerformMovement(AfterResetRequest);
		TestEqual(TEXT("ServerMove's real VerifyClientTimeStamp accepted and reset its timestamp"), ResetServerData->CurrentClientTimeStamp, AfterReset->TimeStamp);
		TestTrue(TEXT("first post-reset move charged only its accepted dt"), FMath::IsNearlyEqual(
			ResetServerUnit->GetBoostSet()->GetBoost(), ServerBoostBeforeReset - 20.0f * ResetDelta, 0.001f));
		FMSCharacterMoveResponseDataContainer AfterResetResponse;
		if (!TestTrue(TEXT("post-reset snapshot remains paired with accepted move"), ReadBoostResponseThroughWire(*ResetServer, *ResetOwner, AfterResetResponse))) return false;
		const float BeforeExpired = ResetServerUnit->GetBoostSet()->GetBoost();
		ResetServer->ServerMove_PerformMovement(BeforeResetRequest);
		ResetServer->ServerMove_PerformMovement(AfterResetRequest);
		TestEqual(TEXT("server rejects pre-reset old move and duplicate without extra energy debit"), ResetServerUnit->GetBoostSet()->GetBoost(), BeforeExpired);
		TestEqual(TEXT("rejected old request cannot roll server timestamp back"), ResetServerData->CurrentClientTimeStamp, AfterReset->TimeStamp);
		ResetOwner->ClientHandleMoveResponse(AfterResetResponse);
		ResetOwner->ClientUpdatePositionAfterServerUpdate();
		TestTrue(TEXT("real post-reset ACK/correction reaches normal engine acknowledgment"), ResetClient->LastAckedMove == AfterReset);
		TestTrue(TEXT("client/server boost agree after real engine timestamp reset"), FMath::IsNearlyEqual(
			ResetOwner->GetPredictedBoostMovementState().Boost, ResetServerUnit->GetBoostSet()->GetBoost(), 0.001f));
	}
	return true;
}

bool FMSBoostMovementReplayTest::RunTest(const FString&)
{
	FBoostMovementTestWorld Scope;
	Scope.Floor(0, 0);
	Scope.Floor(50000, 500);
	AMSPlayerCharacter* Unit = Scope.Spawn();
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	UMSBoostComponent* Driver = Unit->FindComponentByClass<UMSBoostComponent>();
	Unit->SetRole(ROLE_AutonomousProxy);
	Driver->MovementClockOverrideForTest = true;
	FMSBoostMovementState Anchor = Movement->CaptureBoostMovementState();
	Anchor.Generation = 40;
	Anchor.TakeoffBaseZ = 900;
	Anchor.bHasTakeoffBase = true;
	Movement->RestoreBoostMovementState(Anchor);
	Movement->Velocity = FVector(0, 0, 100); // stable nonzero velocity satisfies the stock combine rule
	FNetworkPredictionData_Client_Character* Client = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	const FSavedMovePtr First = RecordMove(Unit, 0.05f, 0.05f);
	Client->CurrentTimeStamp = 0.1f;
	FSavedMovePtr Combined = Client->CreateSavedMove();
	Combined->SetMoveFor(Unit, 0.05f, FVector::ZeroVector, *Client);
	TestTrue(TEXT("unchanged flight/input can still combine"), First->CanCombineWith(Combined, Unit, 0.125f));
	Combined->CombineWith(First.Get(), Unit, nullptr, First->GetRevertedLocation());
	TestEqual(TEXT("CombineWith rewinds energy before total dt"), Movement->BoostMovementState.Boost, Anchor.Boost);
	Movement->PerformMovement(Combined->DeltaTime);
	Combined->PostUpdate(Unit, FSavedMove_Character::PostUpdate_Record);
	TestTrue(TEXT("combined duration charged once"), FMath::IsNearlyEqual(Movement->BoostMovementState.Boost, 98.0f, 0.001f));
	TestEqual(TEXT("owner prediction never applied an authoritative GE"), Unit->GetBoostSet()->GetBoost(), 100.0f);
	TestEqual(TEXT("combined flight retained ceiling anchor"), Movement->TakeoffBaseZ, Anchor.TakeoffBaseZ);
	FMSBoostMovementState NearEmpty = Anchor;
	NearEmpty.Boost = 0.1f;
	Movement->RestoreBoostMovementState(NearEmpty);
	const FSavedMovePtr Exhaust = RecordMove(Unit, 0.2f, 0.05f);
	const FSavedMovePtr Recovery = RecordMove(Unit, 0.25f, 0.05f);
	TestFalse(TEXT("OH entry boundary cannot combine"), Exhaust->CanCombineWith(Recovery, Unit, 0.125f));
	FMSBoostMovementState NearRelease = Anchor;
	NearRelease.Boost = 79.0f;
	NearRelease.bOverheated = true;
	Movement->RestoreBoostMovementState(NearRelease);
	const FSavedMovePtr Release = RecordMove(Unit, 0.3f, 0.05f);
	const FSavedMovePtr Reuse = RecordMove(Unit, 0.35f, 0.05f);
	TestFalse(TEXT("80-percent release boundary cannot combine"), Release->CanCombineWith(Reuse, Unit, 0.125f));
	TestTrue(TEXT("held hover reuses boost after release"), MSMove(Reuse).EndBoostState.Boost < MSMove(Reuse).StartBoostState.Boost);

	// First reproduce a real takeoff from floor A and then a later takeoff at B.
	Movement->RestoreBoostMovementState(Anchor);
	const float HalfHeight = Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Unit->SetActorLocation(FVector(0, 0, HalfHeight + 2.2f));
	Movement->SetMovementMode(MOVE_Walking);
	const FSavedMovePtr FloorAMove = RecordMove(Unit, 1.0f, 0.016f);
	const float BaseA = MSMove(FloorAMove).EndBoostState.TakeoffBaseZ;
	Unit->SetActorLocation(FVector(50000, 0, HalfHeight + 502.2f));
	Movement->SetMovementMode(MOVE_Walking);
	const FSavedMovePtr FloorBMove = RecordMove(Unit, 1.016f, 0.016f);
	TestTrue(TEXT("actual takeoffs from differently elevated floors have different bases"),
		MSMove(FloorBMove).EndBoostState.TakeoffBaseZ > BaseA + 400.0f);
	Client->SavedMoves = {FloorAMove, FloorBMove};
	FMSBoostMovementState ServerAnchor = MSMove(FloorAMove).EndBoostState;
	const FVector AirOverA(0, 0, BaseA + 1000);
	Movement->SetMovementMode(MOVE_Walking); // correction itself must not invent a takeoff here
	FMSCharacterMoveResponseDataContainer Correction = MakeCorrection(FloorAMove, ServerAnchor, AirOverA, 1);
	Movement->ClientHandleMoveResponse(Correction);
	TestEqual(TEXT("walking-to-air correction uses authority takeoff, not corrected air Z"), Movement->TakeoffBaseZ, BaseA);
	TestTrue(TEXT("stock correction replay executes pending floor-B saved move"), Movement->ClientUpdatePositionAfterServerUpdate());
	TestEqual(TEXT("old future floor-B base cannot overwrite corrected floor-A flight"), Movement->TakeoffBaseZ, BaseA);
	TestEqual(TEXT("replay rewrites next saved start base"), MSMove(FloorBMove).StartBoostState.TakeoffBaseZ, BaseA);

	// A second real correction lands on B during replay, then genuinely takes off.
	const FSavedMovePtr Landing = RecordMove(Unit, 1.066f, 0.05f, false);
	const FSavedMovePtr Takeoff = RecordMove(Unit, 1.116f, 0.05f, true);
	const FSavedMovePtr NextFlight = RecordMove(Unit, 1.166f, 0.05f, true);
	Client->SavedMoves = {FloorBMove, Landing, Takeoff, NextFlight};
	Correction = MakeCorrection(FloorBMove, ServerAnchor, FVector(50000, 0, HalfHeight + 520), 2);
	Correction.ClientAdjustment.NewVel = FVector(0, 0, -1000);
	Movement->ClientHandleMoveResponse(Correction);
	Movement->ClientUpdatePositionAfterServerUpdate();
	TestEqual(TEXT("replay landed on the actual B floor"), Landing->EndPackedMovementMode, uint8(MOVE_Walking));
	const float ReplayedBaseB = MSMove(Takeoff).EndBoostState.TakeoffBaseZ;
	TestTrue(TEXT("replay takeoff calculates B from its actual corrected position"), ReplayedBaseB > BaseA + 400.0f);
	TestTrue(TEXT("following replay preserves the newly calculated B ceiling within 1 uu"),
		FMath::IsNearlyEqual(MSMove(NextFlight).StartBoostState.TakeoffBaseZ, ReplayedBaseB, 1.0f));
	FMSBoostMovementState Expected = ServerAnchor;
	Expected.Advance(0.05f, false, false, true);
	Expected.Advance(0.05f, true, false, false);
	Expected.Advance(0.05f, true, false, false);
	TestTrue(TEXT("multiple correction replays debit only unacknowledged intervals once"),
		FMath::IsNearlyEqual(Movement->BoostMovementState.Boost, Expected.Boost, 0.001f));
	TestEqual(TEXT("two replays still do not mutate GAS"), Unit->GetBoostSet()->GetBoost(), 100.0f);

	// Compare actual reached Z, not just the stored base. Start from the genuine
	// floor-B takeoff state above, and keep production speed/height/boost settings.
	{
		FBoostMovementTestWorld CeilingCombinedScope;
		FBoostMovementTestWorld CeilingReplayScope;
		AMSPlayerCharacter* CombinedUnit = CeilingCombinedScope.Spawn();
		AMSPlayerCharacter* ReplayUnit = CeilingReplayScope.Spawn();
		UMSCharacterMovementComponent* CombinedMovement = CombinedUnit->GetMSCharacterMovement();
		UMSCharacterMovementComponent* ReplayMovement = ReplayUnit->GetMSCharacterMovement();
		for (AMSPlayerCharacter* CeilingUnit : {CombinedUnit, ReplayUnit})
		{
			CeilingUnit->SetRole(ROLE_AutonomousProxy);
			CeilingUnit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
			CeilingUnit->GetMSCharacterMovement()->CaptureBoostMovementState();
			CeilingUnit->GetMSCharacterMovement()->Velocity = FVector(0, 0, 100);
		}
		const FMSBoostMovementState CeilingAnchor = MSMove(Takeoff).EndBoostState;
		CombinedMovement->RestoreBoostMovementState(CeilingAnchor);
		FMSBoostMovementState StaleFutureAnchor = CeilingAnchor;
		StaleFutureAnchor.TakeoffBaseZ += 500.0f;
		ReplayMovement->RestoreBoostMovementState(StaleFutureAnchor);
		auto* CombinedData = static_cast<FNetworkPredictionData_Client_Character*>(CombinedMovement->GetPredictionData_Client());
		auto* ReplayData = static_cast<FNetworkPredictionData_Client_Character*>(ReplayMovement->GetPredictionData_Client());
		TArray<FMSBoostMovementState> BoundaryStates;
		TArray<FVector> BoundaryPositions;
		TArray<FVector> BoundaryVelocities;
		TArray<FSavedMovePtr> ReplayMoves;
		int32 ActualCombineCount = 0;
		for (int32 Step = 0; Step < 60; ++Step)
		{
			const float EndTime = (Step + 1) * 0.05f;
			const FSavedMovePtr HalfMove = RecordMove(CombinedUnit, EndTime - 0.025f, 0.025f);
			CombinedData->CurrentTimeStamp = EndTime;
			FSavedMovePtr WholeMove = CombinedData->CreateSavedMove();
			WholeMove->SetMoveFor(CombinedUnit, 0.025f, FVector::ZeroVector, *CombinedData);
			if (HalfMove->CanCombineWith(WholeMove, CombinedUnit, 0.125f))
			{
				WholeMove->CombineWith(HalfMove.Get(), CombinedUnit, nullptr, HalfMove->GetRevertedLocation());
				WholeMove->SetInitialPosition(CombinedUnit); // stock combine caller's final step
				++ActualCombineCount;
			}
			CombinedMovement->PerformMovement(WholeMove->DeltaTime);
			WholeMove->PostUpdate(CombinedUnit, FSavedMove_Character::PostUpdate_Record);
			BoundaryStates.Add(MSMove(WholeMove).EndBoostState);
			BoundaryPositions.Add(CombinedUnit->GetActorLocation());
			BoundaryVelocities.Add(CombinedMovement->Velocity);
			const FSavedMovePtr Predicted = RecordMove(ReplayUnit, EndTime, 0.05f);
			ReplayMoves.Add(Predicted);
			ReplayData->SavedMoves.Add(Predicted);
		}
		TestTrue(TEXT("ceiling run actually used ordinary saved-move combination"), ActualCombineCount > 0);
		const double ExpectedCeilingZ = CeilingAnchor.TakeoffBaseZ + CombinedMovement->MaxHoverHeight;
		const double CombinedCeilingZ = CombinedUnit->GetActorLocation().Z;
		TestTrue(TEXT("combined real movement reached the configured hover ceiling within 1 uu"), FMath::Abs(CombinedCeilingZ - ExpectedCeilingZ) <= 1.0);
		TestTrue(TEXT("ceiling test stayed below exhaustion instead of finishing on a falling path"),
			CombinedMovement->IsHoverAscending() && CombinedMovement->GetPredictedBoostMovementState().Boost > 0.0f);
		for (int32 CorrectionIndex = 0; CorrectionIndex < 2; ++CorrectionIndex)
		{
			FMSCharacterMoveResponseDataContainer CeilingCorrection = MakeCorrection(ReplayMoves[CorrectionIndex],
				BoundaryStates[CorrectionIndex], BoundaryPositions[CorrectionIndex], CorrectionIndex + 1);
			CeilingCorrection.ClientAdjustment.NewVel = BoundaryVelocities[CorrectionIndex];
			ReplayMovement->ClientHandleMoveResponse(CeilingCorrection);
			TestTrue(TEXT("ceiling correction entered actual pending-move replay"), ReplayMovement->ClientUpdatePositionAfterServerUpdate());
			const double ReplayCeilingZ = ReplayUnit->GetActorLocation().Z;
			TestTrue(FString::Printf(TEXT("actual replay %d ceiling Z differs from combined Z by at most 1 uu (%.3f vs %.3f)"),
				CorrectionIndex + 1, ReplayCeilingZ, CombinedCeilingZ), FMath::Abs(ReplayCeilingZ - CombinedCeilingZ) <= 1.0);
			TestTrue(TEXT("repeated correction did not accumulate extra hover height"), FMath::Abs(ReplayCeilingZ - ExpectedCeilingZ) <= 1.0);
			TestTrue(TEXT("ceiling replay retained one-time periodic energy accounting"), FMath::IsNearlyEqual(
				ReplayMovement->GetPredictedBoostMovementState().Boost, CombinedMovement->GetPredictedBoostMovementState().Boost, 0.001f));
		}
	}
	return true;
}

bool FMSBoostMovementResponseTest::RunTest(const FString&)
{
	FBoostMovementTestWorld Scope;
	AMSPlayerCharacter* Unit = Scope.Spawn();
	UMSCharacterMovementComponent* Movement = Unit->GetMSCharacterMovement();
	UMSBoostComponent* Driver = Unit->FindComponentByClass<UMSBoostComponent>();
	Unit->SetRole(ROLE_AutonomousProxy);
	Driver->MovementClockOverrideForTest = true;
	Movement->CaptureBoostMovementState();
	FNetworkPredictionData_Client_Character* Client = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	const FSavedMovePtr Old = RecordMove(Unit, 10.0f, 0.05f);
	const FSavedMovePtr New = RecordMove(Unit, 10.05f, 0.05f);
	Client->SavedMoves = {Old, New};
	FMSCharacterMoveResponseDataContainer Response = MakeCorrection(Old, MSMove(Old).EndBoostState, Unit->GetActorLocation(), 3);
	Response.ClientAdjustment.bAckGoodMove = true;
	const float AheadBoost = Movement->BoostMovementState.Boost;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("good ACK does not rewind the unacknowledged owner state"), Movement->BoostMovementState.Boost, AheadBoost);
	TestTrue(TEXT("good ACK passes to normal CMC acknowledgment"), Client->LastAckedMove == Old);
	Response = MakeCorrection(New, MSMove(New).EndBoostState, Unit->GetActorLocation(), 2);
	Response.AuthoritativeState.Boost = 1;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("reordered response cannot overwrite current state"), Movement->BoostMovementState.Boost, AheadBoost);
	Response.ResponseSequence = 4;
	Response.ClientAdjustment.bAckGoodMove = true;
	Movement->ClientHandleMoveResponse(Response);
	TestTrue(TEXT("mismatched ACK requests standard authority correction"), Movement->bRequestBoostCorrection);
	TestTrue(TEXT("mismatched ACK retains pending moves for replay"), Client->SavedMoves.Contains(New));
	Response.ClientAdjustment.bAckGoodMove = false;
	Response.AuthoritativeState.bOverheated = true;
	Response.AuthoritativeState.Generation = 2;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("correction with no remaining replay still sets energy"), Movement->BoostMovementState.Boost, 1.0f);
	TestTrue(TEXT("external forced OH correction sets movement gate"), Movement->BoostMovementState.bOverheated);
	TestFalse(TEXT("accepted correction clears request"), Movement->bRequestBoostCorrection);
	TestFalse(TEXT("no extra replay was fabricated"), Movement->ClientUpdatePositionAfterServerUpdate());

	// Simulate the exact stock timestamp-reset flags, retaining its real lookup/ACK handling.
	Client->LastAckedMove->bOldTimeStampBeforeReset = true;
	const FSavedMovePtr ResetMove = RecordMove(Unit, 0.05f, 0.05f);
	Client->SavedMoves = {ResetMove};
	Response = MakeCorrection(ResetMove, MSMove(ResetMove).EndBoostState, Unit->GetActorLocation(), 5);
	Response.AuthoritativeState.Boost = 42;
	Response.AuthoritativeState.Generation = 1;
	const float BeforeOldGeneration = Movement->BoostMovementState.Boost;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("older authority generation is rejected even with a fresh timestamp"), Movement->BoostMovementState.Boost, BeforeOldGeneration);
	Response.AuthoritativeState.Generation = 2;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("fresh response after engine timestamp reset is accepted"), Movement->BoostMovementState.Boost, 42.0f);
	const FSavedMovePtr BeforeRespawn = RecordMove(Unit, 0.10f, 0.05f);
	Client->SavedMoves = {BeforeRespawn};
	Response = MakeCorrection(BeforeRespawn, MSMove(BeforeRespawn).EndBoostState, Unit->GetActorLocation(), 6);
	Movement->ResetBoostPrediction();
	Movement->CaptureBoostMovementState();
	const float FreshBoost = Movement->BoostMovementState.Boost;
	Movement->ClientHandleMoveResponse(Response);
	TestEqual(TEXT("old local initialization epoch cannot reset respawn state"), Movement->BoostMovementState.Boost, FreshBoost);

	Driver->MovementClockOverrideForTest.Reset();
	Unit->SetRole(ROLE_SimulatedProxy);
	const float ProxyBefore = Unit->GetBoostSet()->GetBoost();
	Movement->PerformMovement(0.1f);
	Scope.Tick(0.1f);
	TestEqual(TEXT("simulated proxy does not integrate or apply periodic energy GE"), Unit->GetBoostSet()->GetBoost(), ProxyBefore);

	// Each case starts with a real server response, then an authority-only gameplay
	// event while the client has no knowledge of it. Keep two unacknowledged moves
	// so two successive server corrections actually enter the engine replay loop.
	for (int32 EventCase = 0; EventCase < 3; ++EventCase)
	{
		const TCHAR* EventName = EventCase == 0 ? TEXT("D-melee spend") :
			EventCase == 1 ? TEXT("module forced OH") : TEXT("lethal damage / GE_Dead");
		FBoostMovementTestWorld ServerScope;
		FBoostMovementTestWorld OwnerScope;
		AMSPlayerCharacter* ServerUnit = ServerScope.Spawn();
		AMSPlayerCharacter* OwnerUnit = OwnerScope.Spawn();
		UMSCharacterMovementComponent* ServerMovement = ServerUnit->GetMSCharacterMovement();
		UMSCharacterMovementComponent* OwnerMovement = OwnerUnit->GetMSCharacterMovement();
		ServerUnit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		OwnerUnit->SetRole(ROLE_AutonomousProxy);
		OwnerUnit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		ServerMovement->CaptureBoostMovementState();
		OwnerMovement->CaptureBoostMovementState();
		auto* OwnerData = static_cast<FNetworkPredictionData_Client_Character*>(OwnerMovement->GetPredictionData_Client());
		const FSavedMovePtr Initial = RecordMove(OwnerUnit, 0.05f, 0.05f);
		OwnerData->SavedMoves.Add(Initial);
		FMSCharacterNetworkMoveData InitialRequest;
		InitialRequest.ClientFillNetworkMoveData(*Initial, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
		ServerMovement->ServerMove_PerformMovement(InitialRequest);
		FMSCharacterMoveResponseDataContainer InitialResponse;
		if (!TestTrue(TEXT("initial authority snapshot crosses the real response wire format"),
			ReadBoostResponseThroughWire(*ServerMovement, *OwnerMovement, InitialResponse))) return false;
		OwnerMovement->ClientHandleMoveResponse(InitialResponse);
		OwnerMovement->ClientUpdatePositionAfterServerUpdate();

		UAbilitySystemComponent* ServerASC = ServerUnit->GetAbilitySystemComponent();
		if (EventCase == 0)
		{
			TestTrue(TEXT("D-melee uses the production immediate consume GE"), UMSCombatStatics::ConsumeBoostOrOverheat(ServerASC, 25.0f));
			ServerASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 1,
				EGameplayTagReplicationState::TagOnly);
			TestTrue(TEXT("authority D-melee regen block enters the movement snapshot"),
				ServerMovement->CaptureBoostMovementState().bRegenBlocked);
		}
		else if (EventCase == 1)
		{
			// A forced OH below the release threshold must survive the pending moves.
			UMSCombatStatics::ConsumeBoostOrOverheat(ServerASC, 70.0f);
			FGameplayEffectSpecHandle Spec = ServerASC->MakeOutgoingSpec(UGE_ModuleForcedOverheat::StaticClass(), 1.0f, ServerASC->MakeEffectContext());
			Spec.Data->SetSetByCallerMagnitude(MSTags::Data_Module_OverheatDuration, 90.0f);
			ServerASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			TestTrue(TEXT("real module GE granted forced overheat"), ServerASC->HasMatchingGameplayTag(MSTags::State_Module_ForcedOverheat));
		}
		else
		{
			AMSTeamAgentActor* Attacker = ServerScope.World->SpawnActor<AMSTeamAgentActor>();
			Attacker->SetGenericTeamId(MSTeam::EnemyTeamId());
			FMSGameplayEffectContext* DamageContext = new FMSGameplayEffectContext();
			DamageContext->AddInstigator(Attacker, Attacker);
			FGameplayEffectSpec Damage(GetDefault<UGE_Damage>(), FGameplayEffectContextHandle(DamageContext), 1.0f);
			Damage.SetSetByCallerMagnitude(MSTags::Data_Damage, 10.0f * ServerUnit->GetVitalSet()->GetMaxArmor());
			ServerASC->ApplyGameplayEffectSpecToTarget(Damage, ServerASC);
			TestEqual(TEXT("real damage execution reduced armor to zero"), ServerUnit->GetVitalSet()->GetArmor(), 0.0f);
			TestTrue(TEXT("real death transition granted GE_Dead"), ServerASC->HasMatchingGameplayTag(MSTags::State_Dead));
		}
		TestFalse(TEXT("owner has not received an external OH tag before correction"), OwnerUnit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Overheated));
		TestFalse(TEXT("owner has not received a dead tag before correction"), OwnerUnit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_Dead));
		if (EventCase == 0)
			TestFalse(TEXT("owner initially predicts without the authority D-melee block"),
				OwnerMovement->GetPredictedBoostMovementState().bRegenBlocked);

		TArray<FSavedMovePtr> PendingMoves;
		FMSCharacterNetworkMoveData Requests[3];
		for (int32 MoveIndex = 0; MoveIndex < 3; ++MoveIndex)
		{
			const FSavedMovePtr Pending = RecordMove(OwnerUnit, 0.10f + MoveIndex * 0.05f, 0.05f);
			PendingMoves.Add(Pending);
			OwnerData->SavedMoves.Add(Pending);
			// Preserve what was actually sent before any correction/replay rewrites it.
			Requests[MoveIndex].ClientFillNetworkMoveData(*Pending, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
		}
		const float UncorrectedBoost = OwnerMovement->GetPredictedBoostMovementState().Boost;
		for (int32 CorrectionIndex = 0; CorrectionIndex < 2; ++CorrectionIndex)
		{
			ServerMovement->ServerMove_PerformMovement(Requests[CorrectionIndex]);
			FMSCharacterMoveResponseDataContainer ActualResponse;
			if (!TestTrue(FString::Printf(TEXT("%s correction %d serializes an actual move snapshot"), EventName, CorrectionIndex + 1),
				ReadBoostResponseThroughWire(*ServerMovement, *OwnerMovement, ActualResponse))) return false;
			TestTrue(TEXT("external event divergence uses a real standard correction"), ActualResponse.IsCorrection());
			TestTrue(TEXT("snapshot represents current authority gameplay state at this move timestamp"),
				ActualResponse.AuthoritativeState.Matches(ServerMovement->CaptureBoostMovementState()));
			if (EventCase == 0)
				TestTrue(TEXT("D-melee block crosses the real correction wire"),
					ActualResponse.AuthoritativeState.bRegenBlocked);
			OwnerMovement->ClientHandleMoveResponse(ActualResponse);
			TestTrue(FString::Printf(TEXT("%s enters stock unacknowledged replay %d"), EventName, CorrectionIndex + 1),
				OwnerMovement->ClientUpdatePositionAfterServerUpdate());
			FMSBoostMovementState ExpectedAfterReplay = ActualResponse.AuthoritativeState;
			for (int32 ReplayIndex = CorrectionIndex + 1; ReplayIndex < PendingMoves.Num(); ++ReplayIndex)
			{
				ExpectedAfterReplay.Advance(PendingMoves[ReplayIndex]->DeltaTime, ExpectedAfterReplay.CanUseEnergy(), false, false);
			}
			const FMSBoostMovementState Replayed = OwnerMovement->GetPredictedBoostMovementState();
			TestTrue(FString::Printf(TEXT("%s survives replay %d without old energy/OH/death overwriting it"), EventName, CorrectionIndex + 1),
				FMath::IsNearlyEqual(Replayed.Boost, ExpectedAfterReplay.Boost, 0.001f) &&
				Replayed.bOverheated == ExpectedAfterReplay.bOverheated && Replayed.bDead == ExpectedAfterReplay.bDead &&
				Replayed.bRegenBlocked == ExpectedAfterReplay.bRegenBlocked);
		}
		ServerMovement->ServerMove_PerformMovement(Requests[2]);
		const FMSBoostMovementState ServerEnd = ServerMovement->CaptureBoostMovementState();
		const FMSBoostMovementState OwnerEnd = OwnerMovement->GetPredictedBoostMovementState();
		TestTrue(FString::Printf(TEXT("%s client and authority agree at the same final move timestamp"), EventName),
			FMath::IsNearlyEqual(OwnerEnd.Boost, ServerEnd.Boost, 0.001f) &&
			OwnerEnd.bOverheated == ServerEnd.bOverheated && OwnerEnd.bDead == ServerEnd.bDead &&
			OwnerEnd.bRegenBlocked == ServerEnd.bRegenBlocked);
		TestFalse(TEXT("external event did not disappear into the original prediction"), FMath::IsNearlyEqual(OwnerEnd.Boost, UncorrectedBoost, 0.001f));
		TestEqual(TEXT("two replays still left the owner's replicated GAS energy untouched"), OwnerUnit->GetBoostSet()->GetBoost(), 100.0f);
	}
	return true;
}

bool FMSBoostMovementSerializationTest::RunTest(const FString&)
{
	FBoostMovementTestWorld Scope;
	UMSCharacterMovementComponent* Movement = Scope.Spawn()->GetMSCharacterMovement();
	FMSBoostMovementState State;
	State.bInitialized = true;
	State.Boost = 37.125f;
	State.MaxBoost = 120;
	State.ConsumptionPerSecond = 7.5f;
	State.RechargePerSecond = 45;
	State.NormalRechargePerSecond = 84;
	State.RecoveryDelaySeconds = 0.25f;
	State.RecoveryDelayRemaining = 0.125f;
	State.bGrounded = true;
	State.Generation = 17;
	State.bOverheated = true;
	State.bRegenBlocked = true;
	State.bHasTakeoffBase = true;
	State.TakeoffBaseZ = 748.118f;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("finite prediction state serializes"), State.Serialize(Writer));
	FMemoryReader Reader(Bytes);
	FMSBoostMovementState Decoded;
	TestTrue(TEXT("finite prediction state deserializes"), Decoded.Serialize(Reader));
	TestTrue(TEXT("state wire round trip retains energy/rates/OH/D-melee block/generation/altitude/grounding/recovery wait"), Decoded.Matches(State));
	FMSBoostMovementState WithoutMeleeBlock = State;
	WithoutMeleeBlock.bRegenBlocked = false;
	TestFalse(TEXT("D-melee gate changes the predicted simulation contract"), WithoutMeleeBlock.HasSameSimulationGate(State));
	FMSCharacterNetworkMoveData Request;
	Request.TimeStamp = 5.25f;
	Request.CompressedMoveFlags = FSavedMove_Character::FLAG_Custom_1 | FSavedMove_Character::FLAG_Custom_3;
	Request.ReportedEndState = State;
	Request.ClientSimulationEpoch = 9;
	Request.bRequestBoostCorrection = true;
	FBitWriter RequestWriter(4096, true);
	TestTrue(TEXT("extended C2S packed move serializes"), Request.Serialize(*Movement, RequestWriter, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
	FBitReader RequestReader(RequestWriter.GetData(), RequestWriter.GetNumBits());
	FMSCharacterNetworkMoveData RequestDecoded;
	TestTrue(TEXT("extended C2S packed move deserializes"), RequestDecoded.Serialize(*Movement, RequestReader, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
	TestTrue(TEXT("C2S retains the numeric declaration, epoch and correction request"),
		RequestDecoded.ReportedEndState.Matches(State) && RequestDecoded.ClientSimulationEpoch == 9 && RequestDecoded.bRequestBoostCorrection);
	TestEqual(TEXT("C2S retains all standard compressed input bits"), RequestDecoded.CompressedMoveFlags, Request.CompressedMoveFlags);
	FMSCharacterMoveResponseDataContainer Response;
	Response.bHasBoostState = true;
	Response.AuthoritativeState = State;
	Response.ClientSimulationEpoch = 9;
	Response.ResponseSequence = 25;
	Response.ClientAdjustment.TimeStamp = 5.25f;
	Response.ClientAdjustment.bAckGoodMove = true;
	FBitWriter ResponseWriter(4096, true);
	TestTrue(TEXT("good ACK includes the authoritative state in packed wire data"), Response.Serialize(*Movement, ResponseWriter, nullptr));
	FBitReader ResponseReader(ResponseWriter.GetData(), ResponseWriter.GetNumBits());
	FMSCharacterMoveResponseDataContainer ResponseDecoded;
	TestTrue(TEXT("extended good ACK deserializes"), ResponseDecoded.Serialize(*Movement, ResponseReader, nullptr));
	TestTrue(TEXT("S2C state is paired with the same move timestamp, epoch and sequence"),
		ResponseDecoded.AuthoritativeState.Matches(State) && ResponseDecoded.ClientAdjustment.TimeStamp == 5.25f &&
		ResponseDecoded.ClientSimulationEpoch == 9 && ResponseDecoded.ResponseSequence == 25 && ResponseDecoded.IsGoodMove());
	Bytes.Reset();
	State.ConsumptionPerSecond = -1;
	FMemoryWriter InvalidWriter(Bytes);
	State.Serialize(InvalidWriter);
	FMemoryReader InvalidReader(Bytes);
	TestFalse(TEXT("invalid network energy rate is rejected before simulation"), Decoded.Serialize(InvalidReader));
	for (float InvalidWait : {-0.01f, 0.26f, std::numeric_limits<float>::infinity()})
	{
		Bytes.Reset();
		State.ConsumptionPerSecond = 20.0f;
		State.RecoveryDelayRemaining = InvalidWait;
		FMemoryWriter BadWaitWriter(Bytes);
		State.Serialize(BadWaitWriter);
		FMemoryReader BadWaitReader(Bytes);
		TestFalse(TEXT("invalid recovery countdown rejected before simulation"), Decoded.Serialize(BadWaitReader));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSBoostRecoveryTest,
	"NeoMSGO.Player.Movement.NetworkPrediction.Boost.GroundedRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSBoostRecoveryTest::RunTest(const FString&)
{
	FMSBoostMovementState State;
	State.bInitialized = true;
	State.Boost = 20.0f;
	State.MaxBoost = 100.0f;
	State.ConsumptionPerSecond = 20.0f;
	State.RechargePerSecond = 45.0f;
	State.NormalRechargePerSecond = 70.0f;
	State.Advance(2.0f, false, false, false);
	TestEqual(TEXT("falling without boost cannot recover"), State.Boost, 20.0f);
	State.Advance(0.2f, false, false, true);
	TestEqual(TEXT("landing does not credit the preceding airborne dt"), State.RecoveryDelayRemaining, 0.25f);
	State.Advance(0.2f, false, true, true);
	TestEqual(TEXT("grounded recovery waits before adding energy"), State.Boost, 20.0f);
	State.Advance(0.1f, false, true, true);
	TestTrue(TEXT("only time beyond delay recovers at 70 percent capacity/s"), FMath::IsNearlyEqual(State.Boost, 23.5f, .001f));
	State.Advance(0.1f, true, true, true);
	TestTrue(TEXT("spending resumes immediately and resets full wait"), FMath::IsNearlyEqual(State.Boost, 21.5f, .001f) && State.RecoveryDelayRemaining == .25f);
	State.Advance(.2f, false, true, true);
	State.Advance(.01f, false, true, false);
	TestEqual(TEXT("leaving ground just before recovery resets wait"), State.RecoveryDelayRemaining, .25f);
	State.Advance(.01f, false, false, true);
	State.Advance(.25f, false, true, true);
	TestTrue(TEXT("repeated landing cannot bypass its full delay"), FMath::IsNearlyEqual(State.Boost, 21.5f, .001f));
	State.Advance(.1f, false, true, true);
	TestTrue(TEXT("ordinary grounded movement need not be stationary to recover"), FMath::IsNearlyEqual(State.Boost, 28.5f, .001f));
	State.Boost = 30.0f;
	State.RecoveryDelayRemaining = 0.0f;
	State.bRegenBlocked = true;
	State.Advance(1.0f, false, true, true);
	TestTrue(TEXT("D-melee blocks grounded normal recovery beyond the entire old wait"),
		FMath::IsNearlyEqual(State.Boost, 30.0f, .001f) && State.RecoveryDelayRemaining == .25f);
	State.bRegenBlocked = false;
	State.Advance(.2f, false, true, true);
	TestEqual(TEXT("D-melee release still observes the existing recovery wait"), State.Boost, 30.0f);
	State.Advance(.1f, false, true, true);
	TestTrue(TEXT("boost resumes only after the remaining .05s wait"), FMath::IsNearlyEqual(State.Boost, 33.5f, .001f));
	State.Boost = 0;
	State.bOverheated = true;
	State.bRegenBlocked = true;
	State.Advance(1.0f, false, false, false);
	TestEqual(TEXT("D-melee also blocks OH recovery while the action is active"), State.Boost, 0.0f);
	State.bRegenBlocked = false;
	State.Advance(1.0f, false, false, false);
	TestEqual(TEXT("OH keeps existing airborne 45/s, without normal delay"), State.Boost, 45.0f);
	State.Advance(1.0f, false, false, false);
	TestFalse(TEXT("OH still releases at 80 percent"), State.bOverheated);
	State.Advance(.5f, false, false, false);
	TestEqual(TEXT("after OH release ordinary airborne recovery is blocked"), State.Boost, 90.0f);

	FBoostMovementTestWorld ServerScope;
	FBoostMovementTestWorld OwnerScope;
	ServerScope.Floor(0, 0);
	OwnerScope.Floor(0, 0);
	AMSPlayerCharacter* ServerUnit = ServerScope.Spawn();
	AMSPlayerCharacter* OwnerUnit = OwnerScope.Spawn();
	OwnerUnit->SetRole(ROLE_AutonomousProxy);
	for (AMSPlayerCharacter* Unit : {ServerUnit, OwnerUnit})
	{
		Unit->FindComponentByClass<UMSBoostComponent>()->MovementClockOverrideForTest = true;
		Unit->SetActorLocation(FVector(0, 0, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.2f));
		Unit->GetMSCharacterMovement()->SetMovementMode(MOVE_Walking);
		Unit->GetMSCharacterMovement()->CaptureBoostMovementState();
	}
	UMSCharacterMovementComponent* Server = ServerUnit->GetMSCharacterMovement();
	UMSCharacterMovementComponent* Owner = OwnerUnit->GetMSCharacterMovement();
	UMSCombatStatics::ConsumeBoostOrOverheat(ServerUnit->GetAbilitySystemComponent(), 50.0f);
	const FMSBoostMovementState Start = Server->CaptureBoostMovementState();
	TestEqual(TEXT("standard actual charge 15 x3 produces normal rate70"), Start.NormalRechargePerSecond, 70.0f);
	TestEqual(TEXT("OH keeps actual charge45"), Start.RechargePerSecond, 45.0f);
	Owner->RestoreBoostMovementState(Start);
	auto* Client = static_cast<FNetworkPredictionData_Client_Character*>(Owner->GetPredictionData_Client());
	TArray<FSavedMovePtr> Moves;
	TArray<FMSBoostMovementState> ServerStates;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector WalkingAccel(Server->GetMaxAcceleration(), 0, 0);
		Server->MoveAutonomous((Index + 1) * .05f, .05f, 0, WalkingAccel);
		// MoveAutonomous is protected: this test is an existing friend of the MS CMC.
		Client->CurrentTimeStamp = (Index + 1) * .05f;
		Owner->SetHoverInputHeld(false);
		const FSavedMovePtr Move = Client->CreateSavedMove();
		Move->SetMoveFor(OwnerUnit, .05f, WalkingAccel, *Client);
		Owner->MoveAutonomous(Client->CurrentTimeStamp, .05f, Move->GetCompressedFlags(), WalkingAccel);
		Move->PostUpdate(OwnerUnit, FSavedMove_Character::PostUpdate_Record);
		Moves.Add(Move);
		Client->SavedMoves.Add(Move);
		ServerStates.Add(Server->CaptureBoostMovementState());
		TestTrue(TEXT("actual grounded authority GE and client prediction agree at every move"),
			ServerStates.Last().Matches(Owner->GetPredictedBoostMovementState()));
	}
	TestTrue(TEXT("walking displacement does not prevent recovery"), ServerUnit->GetActorLocation().X > 1.0f && OwnerUnit->GetActorLocation().X > 1.0f);
	TestTrue(TEXT("fixture really stays on collision floor"), Server->IsMovingOnGround() && Owner->IsMovingOnGround());
	TestTrue(TEXT("GAS recovers only .15s after .25s delay"), FMath::IsNearlyEqual(ServerUnit->GetBoostSet()->GetBoost(), 60.5f, .001f));
	TestFalse(TEXT("saved moves cannot combine across countdown changes"), Moves[0]->CanCombineWith(Moves[1], OwnerUnit, .125f));
	FMSBoostMovementState Corrected = ServerStates[4];
	Corrected.Boost = 40.0f;
	Corrected.RecoveryDelayRemaining = .15f;
	FMSCharacterMoveResponseDataContainer Correction = MakeCorrection(Moves[4], Corrected, OwnerUnit->GetActorLocation(), 1);
	Correction.ClientAdjustment.MovementMode = MOVE_Walking;
	Owner->ClientHandleMoveResponse(Correction);
	TestTrue(TEXT("grounded correction runs stock pending-move replay"), Owner->ClientUpdatePositionAfterServerUpdate());
	TestTrue(TEXT("replay preserves authority wait instead of stale zero-delay future"),
		FMath::IsNearlyEqual(Owner->GetPredictedBoostMovementState().Boost, 40.0f, .001f) &&
		FMath::IsNearlyZero(Owner->GetPredictedBoostMovementState().RecoveryDelayRemaining, .0001f));
	TestEqual(TEXT("client replay never applies GAS recovery"), OwnerUnit->GetBoostSet()->GetBoost(), 100.0f);
	UMSCombatStatics::ConsumeBoostOrOverheat(ServerUnit->GetAbilitySystemComponent(), 10.0f);
	TestEqual(TEXT("instant D-melee cost restarts exhausted delay"), Server->CaptureBoostMovementState().RecoveryDelayRemaining, .25f);
	Server->PerformMovement(.1f);
	TestTrue(TEXT("instant cost cannot immediately regenerate"), FMath::IsNearlyEqual(ServerUnit->GetBoostSet()->GetBoost(), 50.5f, .001f));
	UMSBoostComponent* Driver = ServerUnit->FindComponentByClass<UMSBoostComponent>();
	Driver->SetRates(20, 30); // raw charge10 has already received InitializeUnit's multiplier.
	TestTrue(TEXT("existing suit charge ratio is preserved, not multiplied twice"),
		FMath::IsNearlyEqual(Server->CaptureBoostMovementState().NormalRechargePerSecond, 70.0f * 2.0f / 3.0f, .001f));
	Driver->SetRates(20, 0);
	TestEqual(TEXT("explicit zero rate still disables normal recharge"), Server->CaptureBoostMovementState().NormalRechargePerSecond, 0.0f);
	Driver->SetRates(20, 45);
	ServerUnit->GetBoostSet()->SetBoost(50.0f);
	UAbilitySystemComponent* ServerASC = ServerUnit->GetAbilitySystemComponent();
	ServerASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 1,
		EGameplayTagReplicationState::TagOnly);
	TestTrue(TEXT("authority movement snapshot includes active D-melee regen block"),
		Server->CaptureBoostMovementState().bRegenBlocked);
	Server->PerformMovement(.5f);
	TestEqual(TEXT("grounded authority applies no regen GE during D-melee recovery"),
		ServerUnit->GetBoostSet()->GetBoost(), 50.0f);
	ServerASC->SetLooseGameplayTagCount(MSTags::State_DashMeleeRegenBlock, 0,
		EGameplayTagReplicationState::TagOnly);
	TestFalse(TEXT("authority movement snapshot clears D-melee regen block"),
		Server->CaptureBoostMovementState().bRegenBlocked);
	Server->PerformMovement(.2f);
	TestEqual(TEXT("release still waits the pre-existing .25s before regen GE"),
		ServerUnit->GetBoostSet()->GetBoost(), 50.0f);
	Server->PerformMovement(.1f);
	TestTrue(TEXT("authority regen GE resumes after D-melee wait"),
		ServerUnit->GetBoostSet()->GetBoost() > 50.0f);
	return true;
}

#endif
