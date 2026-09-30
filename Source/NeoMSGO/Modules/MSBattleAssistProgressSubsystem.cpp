#include "Modules/MSBattleAssistProgressSubsystem.h"
#include "Modules/MSBattleAssistPersistence.h"
#include "Online/MSOnlineAccountSubsystem.h"
#include "Data/MSBattleAssistRules.h"
#include "Data/MSBattleAssistModuleData.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	bool CanDevelopDefinition(const UMSBattleAssistModuleData* Module)
	{
		if (!Module || Module->Support != EMSModuleSupport::Implemented || Module->Evidence != EMSModuleEvidence::Confirmed
			|| !Module->bRecipeKnown || Module->FamilyId.IsNone() || Module->SlotCost < 1 || Module->SlotCost > 10 || Module->Effects.IsEmpty()) return false;
		for (const FMSBattleAssistEffect& Effect : Module->Effects)
			if (!FMath::IsFinite(Effect.Value) || Effect.Value < 0.f || Effect.Unit.IsNone()) return false;
		return true;
	}
	bool ValidProgress(const FMSBattleAssistProgress& P)
	{
		if (P.GP < 0 || P.GP > 9007199254740991LL || P.Revision < 0 || P.Revision > 9007199254740991LL || P.DevelopedModuleIds.Num() > 1024 || P.Materials.Num() > 256) return false;
		TSet<FPrimaryAssetId> Seen;
		for (const auto& Id : P.DevelopedModuleIds)
		{
			if (!Id.IsValid() || Id.PrimaryAssetType != UMSBattleAssistModuleData::PrimaryAssetType || Seen.Contains(Id)) return false;
			Seen.Add(Id);
		}
		for (const auto& Item : P.Materials) if (Item.Key.IsNone() || Item.Value < 0) return false;
		return true;
	}
	bool Integer(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int64& Out)
	{
		const auto Field = Object->TryGetField(Key);
		if (!Field.IsValid() || Field->Type != EJson::Number) return false;
		double Value = 0;
		if (!Object->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value) || Value < 0 || Value > 9007199254740991.0 || FMath::FloorToDouble(Value) != Value) return false;
		Out = static_cast<int64>(Value); return true;
	}
}

void UMSBattleAssistProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMSOnlineAccountSubsystem>();
	if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>())
		Account->OnAccountChanged.AddUniqueDynamic(this, &UMSBattleAssistProgressSubsystem::HandleAccountChanged);
	ReloadProgress();
}

void UMSBattleAssistProgressSubsystem::Deinitialize()
{
	++OperationGeneration;
	bShuttingDown = true; bPending = false;
	auto Completion = MoveTemp(DevelopmentCompletion);
	if (Completion) Completion(false, FText::FromString(TEXT("開発処理を終了しました。結果は再読込して確認してください。")));
	if (GetGameInstance()) if (auto* Account = GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>())
		Account->OnAccountChanged.RemoveDynamic(this, &UMSBattleAssistProgressSubsystem::HandleAccountChanged);
	Super::Deinitialize();
}

void UMSBattleAssistProgressSubsystem::HandleAccountChanged(FMSOnlineIdentity Identity, int64 Generation)
{
	const bool bWasPending = bPending;
	auto Completion = MoveTemp(DevelopmentCompletion);
	++OperationGeneration; bPending = false; bRejectedSave = false;
	Progress = FMSBattleAssistProgress(); LastError = FText::GetEmpty();
	OnProgressChanged.Broadcast();
	const FText Cancelled = FText::FromString(TEXT("アカウントが変わったため開発処理を終了しました。結果は再読込して確認してください。"));
	if (bWasPending) OnOperationCompleted.Broadcast(false, Cancelled);
	if (Completion) Completion(false, Cancelled);
	ReloadProgress();
}

bool UMSBattleAssistProgressSubsystem::Finish(bool bSuccess, const FText& Message)
{
	bPending = false;
	LastError = bSuccess ? FText::GetEmpty() : Message;
	// Detach before delegates: a consumer may start another operation from a callback.
	auto Completion = MoveTemp(DevelopmentCompletion);
	if (bSuccess) OnProgressChanged.Broadcast();
	OnOperationCompleted.Broadcast(bSuccess, Message);
	if (Completion) Completion(bSuccess, Message);
	return bSuccess;
}

bool UMSBattleAssistProgressSubsystem::ParseProgress(const FString& Body, FMSBattleAssistProgress& Out)
{
	TSharedPtr<FJsonObject> Object;
	if (Body.Len() > 262144 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object || Object->Values.Num() != 4) return false;
	FMSBattleAssistProgress Candidate;
	const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
	const TSharedPtr<FJsonObject>* Materials = nullptr;
	if (!Integer(Object, TEXT("gp"), Candidate.GP) || !Integer(Object, TEXT("revision"), Candidate.Revision)
		|| !Object->TryGetArrayField(TEXT("developed_module_ids"), Ids) || !Ids || Ids->Num() > 1024
		|| !Object->TryGetObjectField(TEXT("materials"), Materials) || !Materials || !Materials->IsValid() || (*Materials)->Values.Num() > 256) return false;
	for (const auto& Value : *Ids)
	{
		FString Id;
		if (!Value || !Value->TryGetString(Id) || Id.Len() > 256) return false;
		const FPrimaryAssetId Parsed(Id);
		if (Parsed.ToString() != Id) return false;
		Candidate.DevelopedModuleIds.Add(Parsed);
	}
	for (const auto& Material : (*Materials)->Values)
	{
		int64 Quantity;
		if (Material.Key.IsEmpty() || Material.Key.Len() > 128 || !Integer(*Materials, *Material.Key, Quantity) || Quantity > MAX_int32) return false;
		const FName Name(*Material.Key);
		if (Candidate.Materials.Contains(Name)) return false;
		Candidate.Materials.Add(Name, static_cast<int32>(Quantity));
	}
	if (!ValidProgress(Candidate)) return false;
	Out = MoveTemp(Candidate); return true;
}

bool UMSBattleAssistProgressSubsystem::SendProgressRequest(const FString& Verb, const FString& Path, const FString& Body)
{
	auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (!Account || !Account->HasOnlineSession()) return Finish(false, FText::FromString(TEXT("ログイン後に開発状況を読み込んでください。")));
	bPending = true;
	const uint64 Generation = ++OperationGeneration;
	const int64 AuthGeneration = Account->GetAuthGeneration();
	TWeakObjectPtr<UMSBattleAssistProgressSubsystem> WeakThis(this);
	TWeakObjectPtr<UMSOnlineAccountSubsystem> WeakAccount(Account);
	const bool bAccepted = Account->SendBattleAssistRequest(Verb, Path, Body,
		[WeakThis, WeakAccount, Generation, AuthGeneration](bool bSucceeded, int32 Code, const FString& Response)
		{
			if (!WeakThis.IsValid() || !WeakAccount.IsValid() || WeakThis->OperationGeneration != Generation
				|| WeakAccount->GetAuthGeneration() != AuthGeneration) return;
			FMSBattleAssistProgress Candidate;
			if (bSucceeded && Code >= 200 && Code < 300 && ParseProgress(Response, Candidate))
			{
				WeakThis->Progress = MoveTemp(Candidate);
				WeakThis->Finish(true, FText::FromString(TEXT("開発状況を更新しました。")));
			}
			else
			{
				FString Error = Code == 409 ? TEXT("別の接続で開発状況が変わりました。再読込してください。") : TEXT("開発状況を更新できませんでした。残高は再読込して確認してください。");
				TSharedPtr<FJsonObject> Envelope; const TSharedPtr<FJsonObject>* Detail = nullptr;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response), Envelope) && Envelope
					&& Envelope->TryGetObjectField(TEXT("error"), Detail) && Detail && Detail->IsValid())
				{
					FString SafeMessage;
					if ((*Detail)->TryGetStringField(TEXT("message"), SafeMessage) && SafeMessage.Len() <= 512) Error = SafeMessage;
				}
				WeakThis->Finish(false, FText::FromString(Error));
			}
		});
	if (!bAccepted && bPending && Generation == OperationGeneration)
		Finish(false, FText::FromString(TEXT("サーバーへ接続できません。")));
	return bAccepted;
}

bool UMSBattleAssistProgressSubsystem::ReloadProgress()
{
	if (bPending || bShuttingDown) return false;
	const auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (Account && Account->IsOnlineMode()) return SendProgressRequest(TEXT("GET"), TEXT("/v1/me/battle-assist"), FString());
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		Progress = FMSBattleAssistProgress(); bRejectedSave = false;
		return Finish(true, FText::GetEmpty());
	}
	const auto* Saved = Cast<UMSBattleAssistProgressSave>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!Saved || Saved->Version != 1 || !ValidProgress(Saved->Progress))
	{
		bRejectedSave = true;
		return Finish(false, FText::FromString(TEXT("開発保存が破損または未対応です。元ファイルを保護しています。")));
	}
	Progress = Saved->Progress; bRejectedSave = false;
	return Finish(true, FText::GetEmpty());
}

bool UMSBattleAssistProgressSubsystem::SaveProgress(const FMSBattleAssistProgress& Candidate) const
{
	if (bRejectedSave || !ValidProgress(Candidate)) return false;
	auto* Saved = NewObject<UMSBattleAssistProgressSave>(); Saved->Progress = Candidate;
	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(Saved, Bytes)) return false;
	return MSBattleAssistPersistence::SaveBytes(SaveSlotName, Bytes);
}

bool UMSBattleAssistProgressSubsystem::TryDevelopModule(FPrimaryAssetId ModuleId)
{
	return TryDevelopModule(ModuleId, Progress.Revision, nullptr);
}

bool UMSBattleAssistProgressSubsystem::TryDevelopModule(FPrimaryAssetId ModuleId, int64 ExpectedRevision,
	TFunction<void(bool, FText)> Completion)
{
	if (bPending || bShuttingDown)
	{
		if (Completion) Completion(false, FText::FromString(TEXT("開発処理中または終了処理中です。")));
		return false;
	}
	DevelopmentCompletion = MoveTemp(Completion);
	if (ExpectedRevision < 0 || ExpectedRevision > 9007199254740991LL)
		return Finish(false, FText::FromString(TEXT("開発状況の版番号が不正です。")));
	const auto* Module = MSBattleAssist::FindModule(ModuleId);
	if (!CanDevelopDefinition(Module))
		return Finish(false, FText::FromString(TEXT("このモジュールは開発に未対応です。")));
	const auto* Account = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSOnlineAccountSubsystem>() : nullptr;
	if (Account && Account->IsOnlineMode())
	{
		const auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("module_id"), ModuleId.ToString());
		Object->SetNumberField(TEXT("expected_revision"), static_cast<double>(ExpectedRevision));
		FString Body; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Body));
		return SendProgressRequest(TEXT("POST"), TEXT("/v1/me/battle-assist/develop"), Body);
	}
	if (Progress.DevelopedModuleIds.Contains(ModuleId)) return Finish(true, FText::FromString(TEXT("開発済みです。")));
	if (Progress.Revision != ExpectedRevision)
		return Finish(false, FText::FromString(TEXT("別の操作で開発状況が変わりました。再読込してください。")));
	if (Module->DevelopmentCostGP < 0 || Progress.GP < Module->DevelopmentCostGP)
		return Finish(false, FText::FromString(TEXT("GPが不足しています。")));
	FMSBattleAssistProgress Candidate = Progress;
	for (const auto& Material : Module->DevelopmentMaterials)
	{
		if (Material.Value < 0 || Candidate.Materials.FindRef(Material.Key) < Material.Value)
			return Finish(false, FText::FromString(TEXT("開発素材が不足しています。")));
		Candidate.Materials.FindOrAdd(Material.Key) -= Material.Value;
	}
	Candidate.GP -= Module->DevelopmentCostGP;
	Candidate.DevelopedModuleIds.Add(ModuleId); ++Candidate.Revision;
	if (!SaveProgress(Candidate)) return Finish(false, FText::FromString(TEXT("開発結果を保存できません。GP・素材を維持しました。")));
	Progress = MoveTemp(Candidate);
	return Finish(true, FText::FromString(TEXT("開発が完了しました。全機体で繰り返し装備できます。")));
}
