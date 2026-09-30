// Copyright NeoMSGO Project. All Rights Reserved.
// 実プレイ報告バグ「弾頭(トレーサー/マズルフラッシュ/着弾等)が全く表示されない」回帰テスト。
// 根本原因: ネイティブC++のGameplayCueNotifyクラスはGameplayCueManagerのアセットレジストリ走査
// (Blueprint GeneratedClassタグ)では発見されず、どのCueSetにも未登録だったため、
// ExecuteGameplayCueがルーティングされても処理するNotifyが存在しなかった。
// 修正: UMSAbilitySystemGlobals::GetGameplayCueManagerでランタイムCueSetへ明示登録する。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "AbilitySystemGlobals.h"
#include "Audio/MSAudioPlayback.h"
#include "Combat/Cues/MSGameplayCue_Explosion.h"
#include "Combat/Cues/MSGameplayCue_Fire.h"
#include "Combat/Cues/MSGameplayCue_Impact.h"
#include "Combat/Cues/MSTransientVfxActor.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSNativeGameplayCueRegistrationTest,
	"NeoMSGO.Combat.NativeGameplayCueRegistration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FNativeCueTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSNativeCueTestWorld"));
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);

		FNativeCueTestWorld()
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(World);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FNativeCueTestWorld()
		{
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	int32 CountTransientVfxActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AMSTransientVfxActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

bool FMSNativeGameplayCueRegistrationTest::RunTest(const FString& Parameters)
{
	// ------------------------------------------------------------------
	// 1. ランタイムCueSetに全ネイティブCueタグが登録されていること
	// ------------------------------------------------------------------
	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	if (!TestNotNull(TEXT("GameplayCueManagerを取得できる"), CueManager))
	{
		return false;
	}
	UGameplayCueSet* RuntimeCueSet = CueManager->GetRuntimeCueSet();
	if (!TestNotNull(TEXT("ランタイムCueSetを取得できる"), RuntimeCueSet))
	{
		return false;
	}

	const auto HasCue = [RuntimeCueSet](const FGameplayTag& Tag)
	{
		for (const FGameplayCueNotifyData& Data : RuntimeCueSet->GameplayCueData)
		{
			if (Data.GameplayCueTag == Tag)
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("Cue_Weapon_Fire(マズルフラッシュ/発射音)が登録済み"), HasCue(MSTags::Cue_Weapon_Fire));
	TestTrue(TEXT("Cue_Weapon_Tracer(弾道トレーサー)が登録済み"), HasCue(MSTags::Cue_Weapon_Tracer));
	TestTrue(TEXT("Cue_Weapon_BeamLoop(連続ビーム)が登録済み"), HasCue(MSTags::Cue_Weapon_BeamLoop));
	TestTrue(TEXT("Cue_Impact_Normal(着弾)が登録済み"), HasCue(MSTags::Cue_Impact_Normal));
	TestTrue(TEXT("Cue_Impact_Headshot(HS着弾)が登録済み"), HasCue(MSTags::Cue_Impact_Headshot));
	TestTrue(TEXT("Cue_Explosion(爆発)が登録済み"), HasCue(MSTags::Cue_Explosion));
	TestTrue(TEXT("Cue_Kill_Confirm(キル確認)が登録済み"), HasCue(MSTags::Cue_Kill_Confirm));

	// ------------------------------------------------------------------
	// 2. ExecuteGameplayCue → ネイティブNotify実行 → VFXアクター実スポーンまでの端到端検証
	// ------------------------------------------------------------------
	FNativeCueTestWorld Scope;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSUnitBase* Shooter = Scope.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("射手ユニットのスポーン"), Shooter))
	{
		return false;
	}
	APlayerController* LocalController = Scope.World->SpawnActor<APlayerController>(
		APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("local player controller"), LocalController))
	{
		return false;
	}
	AddExpectedError(TEXT("CreateWidget"), EAutomationExpectedErrorFlags::Contains, 1);
	LocalController->SetAsLocalPlayerController();
	LocalController->Possess(CastChecked<APawn>(Shooter));
	TestTrue(TEXT("standalone shooter is locally controlled"),
		CastChecked<APawn>(Shooter)->IsLocallyControlled());
	UAbilitySystemComponent* ASC = Shooter->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("射手のASC"), ASC))
	{
		return false;
	}
	AMSUnitBase* ImpactTarget = Scope.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(800.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("impact target"), ImpactTarget))
	{
		return false;
	}
	UAbilitySystemComponent* ImpactTargetASC = ImpactTarget->GetAbilitySystemComponent();

	const int32 VfxBefore = CountTransientVfxActors(Scope.World);

	// トレーサーCue: 射手位置(銃口)→前方800cmの弾道を要求する
	FGameplayCueParameters TracerParameters(ASC->MakeEffectContext());
	TracerParameters.Location = FVector(800.0f, 0.0f, 100.0f);
	TracerParameters.Instigator = Shooter;
	ASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Tracer, TracerParameters);

	const int32 VfxAfterTracer = CountTransientVfxActors(Scope.World);
	TestTrue(FString::Printf(TEXT("トレーサーCue実行でVFXアクターがスポーンされる(%d -> %d)"),
		VfxBefore, VfxAfterTracer), VfxAfterTracer > VfxBefore);

	// 着弾Cue: Cue target is deliberately the victim. Private feedback must follow
	// Parameters.Instigator (the attacker), never MyTarget/the victim.
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters ImpactParameters(ASC->MakeEffectContext());
	ImpactParameters.Location = FVector(800.0f, 0.0f, 100.0f);
	ImpactParameters.Instigator = Shooter;
	ImpactTargetASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, ImpactParameters);

	const int32 VfxAfterImpact = CountTransientVfxActors(Scope.World);
	TestTrue(FString::Printf(TEXT("着弾Cue実行でVFXアクターが追加スポーンされる(%d -> %d)"),
		VfxAfterTracer, VfxAfterImpact), VfxAfterImpact > VfxAfterTracer);
	const MSAudio::FRecordedMetaSoundRequest& ImpactAudio = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("local player attacking a remote victim plays hit confirm"), ImpactAudio.RequestCount, 1);
	TestTrue(TEXT("着弾確認音は距離非依存の2D"), ImpactAudio.bIs2D);
	TestTrue(TEXT("着弾確認音は有限寿命"), ImpactAudio.OneShotDuration > 0.0f);

	// 発射Cue(マズルフラッシュ): 同様にスポーンされる
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters FireParameters(ASC->MakeEffectContext());
	FireParameters.Location = Shooter->GetActorLocation();
	FireParameters.Instigator = Shooter;
	ASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, FireParameters);

	const int32 VfxAfterFire = CountTransientVfxActors(Scope.World);
	TestTrue(FString::Printf(TEXT("発射Cue実行でマズルフラッシュVFXがスポーンされる(%d -> %d)"),
		VfxAfterImpact, VfxAfterFire), VfxAfterFire > VfxAfterImpact);
	const MSAudio::FRecordedMetaSoundRequest& FireAudio = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("発射Cueから音声ヘルパーまで到達する"), FireAudio.RequestCount, 1);
	TestFalse(TEXT("発射音は銃口位置の3D"), FireAudio.bIs2D);

	// 撃破Cue: Killer ASC -> native notify -> 2D bounded confirmation audio.
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters KillParameters(ASC->MakeEffectContext());
	KillParameters.Instigator = Shooter;
	ASC->ExecuteGameplayCue(MSTags::Cue_Kill_Confirm, KillParameters);
	const MSAudio::FRecordedMetaSoundRequest& KillAudio = MSAudio::GetRecordedMetaSoundRequest();
	TestEqual(TEXT("撃破Cueから音声ヘルパーまで到達する"), KillAudio.RequestCount, 1);
	TestTrue(TEXT("撃破確認音は距離非依存の2D"), KillAudio.bIs2D);
	TestTrue(TEXT("撃破確認音は有限寿命"), KillAudio.OneShotDuration > 0.0f);

	// A standalone/listen-server AIController is local too; it must still never qualify as
	// the local human attacker. This covers BOT -> player and BOT -> BOT with the victim
	// intentionally used as MyTarget in both cases.
	AMSUnitBase* RemoteShooter = Scope.World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("remote shooter avatar"), RemoteShooter))
	{
		return false;
	}
	AAIController* BotController = Scope.World->SpawnActor<AAIController>(
		AAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	BotController->Possess(CastChecked<APawn>(RemoteShooter));
	TestTrue(TEXT("standalone BOT is locally simulated (regression precondition)"),
		CastChecked<APawn>(RemoteShooter)->IsLocallyControlled());
	UAbilitySystemComponent* RemoteASC = RemoteShooter->GetAbilitySystemComponent();
	const int32 VfxBeforeRemoteImpact = CountTransientVfxActors(Scope.World);
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters RemoteImpactParameters(RemoteASC->MakeEffectContext());
	RemoteImpactParameters.Location = FVector(600.0f, 0.0f, 100.0f);
	RemoteImpactParameters.Instigator = RemoteShooter;
	ASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, RemoteImpactParameters);
	TestEqual(TEXT("BOT attacking the local player does not play hit confirm"),
		MSAudio::GetRecordedMetaSoundRequest().RequestCount, 0);
	TestTrue(TEXT("remote impact still spawns VFX"),
		CountTransientVfxActors(Scope.World) > VfxBeforeRemoteImpact);
	MSAudio::ResetRecordedMetaSoundRequest();
	ImpactTargetASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, RemoteImpactParameters);
	TestEqual(TEXT("BOT attacking another BOT does not play hit confirm"),
		MSAudio::GetRecordedMetaSoundRequest().RequestCount, 0);
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters RemoteKillParameters(RemoteASC->MakeEffectContext());
	RemoteKillParameters.Instigator = RemoteShooter;
	RemoteASC->ExecuteGameplayCue(MSTags::Cue_Kill_Confirm, RemoteKillParameters);
	TestEqual(TEXT("remote kill confirm does not play private audio"),
		MSAudio::GetRecordedMetaSoundRequest().RequestCount, 0);

	// Authored-sound routing is recorded at the native Cue boundary, not just tested as a CDO reference.
	UMSRangedWeaponData* BeamWeapon = NewObject<UMSRangedWeaponData>(Shooter);
	BeamWeapon->CategoryTag = MSTags::Weapon_Category_BeamRifle;
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters BeamFireParameters(ASC->MakeEffectContext());
	BeamFireParameters.Location = Shooter->GetActorLocation();
	BeamFireParameters.SourceObject = BeamWeapon;
	ASC->ExecuteGameplayCue(MSTags::Cue_Weapon_Fire, BeamFireParameters);
	TestTrue(TEXT("BeamRifle Fire Cue records the beam source"),
		MSAudio::GetRecordedMetaSoundRequest().Sound ==
		GetDefault<UMSGameplayCue_Fire>()->ResolveFireSound(BeamWeapon));

	UMSMeleeWeaponData* MeleeWeapon = NewObject<UMSMeleeWeaponData>(Shooter);
	MeleeWeapon->CategoryTag = MSTags::Weapon_Category_Melee;
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters MeleeImpactParameters(ASC->MakeEffectContext());
	MeleeImpactParameters.Location = FVector(700.0f, 0.0f, 80.0f);
	MeleeImpactParameters.Instigator = Shooter;
	MeleeImpactParameters.SourceObject = MeleeWeapon;
	ASC->ExecuteGameplayCue(MSTags::Cue_Impact_Normal, MeleeImpactParameters);
	TestTrue(TEXT("melee Impact Cue records the melee source"),
		MSAudio::GetRecordedMetaSoundRequest().Sound ==
		GetDefault<UMSGameplayCue_Impact_Normal>()->GetMeleeImpactSound());

	UMSRangedWeaponData* BazookaWeapon = NewObject<UMSRangedWeaponData>(Shooter);
	BazookaWeapon->CategoryTag = MSTags::Weapon_Category_Bazooka;
	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters OrdnanceParameters(ASC->MakeEffectContext());
	OrdnanceParameters.Location = FVector(900.0f, 0.0f, 50.0f);
	OrdnanceParameters.SourceObject = BazookaWeapon;
	ASC->ExecuteGameplayCue(MSTags::Cue_Explosion, OrdnanceParameters);
	TestTrue(TEXT("weapon-sourced Explosion Cue records ordnance audio"),
		MSAudio::GetRecordedMetaSoundRequest().Sound ==
		GetDefault<UMSGameplayCue_Explosion>()->GetOrdnanceExplosionSound());

	MSAudio::ResetRecordedMetaSoundRequest();
	FGameplayCueParameters UnitExplosionParameters(ASC->MakeEffectContext());
	UnitExplosionParameters.Location = Shooter->GetActorLocation();
	UnitExplosionParameters.SourceObject = Shooter;
	ASC->ExecuteGameplayCue(MSTags::Cue_Explosion, UnitExplosionParameters);
	TestTrue(TEXT("unit-sourced Explosion Cue records unit audio"),
		MSAudio::GetRecordedMetaSoundRequest().Sound ==
		GetDefault<UMSGameplayCue_Explosion>()->GetUnitExplosionSound());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
