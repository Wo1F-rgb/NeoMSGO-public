// Copyright NeoMSGO Project. All Rights Reserved.
//
// P3-5 検証: 三人称カメラ/スコープ。
//  - 構造保証: SpringArmはbDoCollisionTest=true+ProbeChannel=ECC_Camera、自機カプセルは
//    MSPawnCapsuleプロファイル(P0-5)でCamera=Ignore → カメラが自機を障害物と誤認しない
//    (MIGRATION_NOTES 表#2再発防止をコリジョン設定の観測で検証)
//  - スコープFOV遷移: GetCameraView()内のFInterpTo補間でスコープFOVへ収束し、
//    解除で通常FOVへ戻る(カメラマネージャ更新の一部として適用=後段上書きなし。表#5対策)
//  - 感度: 通常時1.0、スコープ中はScopedSensitivityMultiplier(Look入力は
//    LookSensitivityScale=0.35×本倍率。AMSPlayerCharacter::Input_Look)
//  - SetScopedFromWeapon: スコープ武器(UMSRangedWeaponData, bIsScopeWeapon=true)の値を採用、
//    非スコープ武器/null武器はスコープ解除として扱う
// 実行方法:
//   エディタ: Session Frontend > Automation > "NeoMSGO.Player.Camera"
//   コマンドライン: -ExecCmds="Automation RunTests NeoMSGO.Player.Camera"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Core/MSScale.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "NeoMSGO.h"
#include "Player/MSCameraComponent.h"
#include "Player/MSPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSCameraTest,
	"NeoMSGO.Player.Camera",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** AMSPlayerCharacterのスポーンにワールドが必要(P2-6テストと同一パターン)。
	 *  注意: 無名namespaceでもUnityビルドでは他テストcppと同一TUに結合され得るため、
	 *  ヘルパーにはファイル固有の名前を付けること。 */
	struct FCameraTestWorld
	{
		UWorld* World = nullptr;

		FCameraTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSCameraTestWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			if (!World->GetBegunPlay())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
				World->GetWorldSettings()->NotifyMatchStarted();
			}
		}

		~FCameraTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** GetCameraView()をSeconds秒ぶん繰り返し呼び、FOV補間を進める(0.1s刻み。実行順はカメラマネージャ更新相当) */
	FMinimalViewInfo AdvanceCameraView(UMSCameraComponent* Camera, float Seconds)
	{
		FMinimalViewInfo View;
		constexpr float Step = 0.1f;
		while (Seconds > 0.0f)
		{
			Camera->GetCameraView(FMath::Min(Seconds, Step), View);
			Seconds -= Step;
		}
		return View;
	}
}

bool FMSCameraTest::RunTest(const FString& Parameters)
{
	FCameraTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Player = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector(0, 0, 300.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("AMSPlayerCharacterのスポーン"), Player))
	{
		return false;
	}

	UMSCameraComponent* Camera = Player->GetMSCamera();
	USpringArmComponent* Boom = Player->GetCameraBoom();
	if (!TestNotNull(TEXT("UMSCameraComponentが搭載されている"), Camera) ||
		!TestNotNull(TEXT("SpringArmが搭載されている"), Boom))
	{
		return false;
	}
	Camera->SetViewPreset(EMSCameraViewPreset::RightShoulder);
	TestTrue(TEXT("default camera arm is scaled with the 18m mobile suit"),
		FMath::IsNearlyEqual(Boom->TargetArmLength, 450.0f * MSGO::MSScale));
	TestTrue(TEXT("camera pivot remains at scaled chest height"),
		FMath::IsNearlyEqual(Boom->GetRelativeLocation().Z, 50.0f * MSGO::MSScale, 0.1f));
	TestTrue(TEXT("default shoulder offset is scaled with the 18m mobile suit"),
		Boom->SocketOffset.Equals(FVector(0.0f, 55.0f, 20.0f) * MSGO::MSScale, 0.1f));
	TestTrue(TEXT("camera collision probe remains enabled with a finite wall-clearance radius"),
		Boom->ProbeSize > 0.0f && Boom->ProbeSize < 100.0f);

	// --- 1. 構造保証: カメラが自機を障害物と誤認しない(チャンネル設計。MIGRATION_NOTES 表#2) ---
	{
		TestTrue(TEXT("SpringArm: bDoCollisionTest=true(障害物回避は有効)"), Boom->bDoCollisionTest);
		TestEqual(TEXT("SpringArm: ProbeChannel=ECC_Camera"),
			static_cast<int32>(Boom->ProbeChannel.GetValue()), static_cast<int32>(ECC_Camera));
		// 自機カプセル(MSPawnCapsuleプロファイル。P0-5)はCameraチャンネルをIgnore
		// → ProbeがECC_Cameraである限り、自機カプセルは構造的にヒットし得ない
		TestEqual(TEXT("自機カプセル: Cameraチャンネル応答=Ignore(MSPawnCapsule/P0-5)"),
			static_cast<int32>(Player->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera)),
			static_cast<int32>(ECR_Ignore));
		TestEqual(TEXT("装飾メッシュ: Cameraチャンネル応答=Ignore(MSNoCollision)"),
			static_cast<int32>(Player->GetMesh()->GetCollisionResponseToChannel(ECC_Camera)),
			static_cast<int32>(ECR_Ignore));
	}

	Camera->SetViewPreset(EMSCameraViewPreset::HighOverhead);
	TestEqual(TEXT("high overhead preset is retained"), Camera->GetViewPreset(), EMSCameraViewPreset::HighOverhead);
	TestEqual(TEXT("high overhead centers lateral offset"), Boom->SocketOffset.Y, 0.0);
	TestTrue(TEXT("high overhead arm uses the common scale"),
		FMath::IsNearlyEqual(Boom->TargetArmLength, 380.0f * MSGO::MSScale));
	TestTrue(TEXT("high overhead raises camera"), Boom->SocketOffset.Z > 100.0f * MSGO::MSScale);
	Camera->SetViewPreset(EMSCameraViewPreset::RightShoulder);
	TestTrue(TEXT("right shoulder restores scaled lateral offset"),
		FMath::IsNearlyEqual(Boom->SocketOffset.Y, 55.0f * MSGO::MSScale, 0.1f));

	// IA_ViewToggle is the P-key input path, and its bound handler must toggle in both directions.
	UInputAction* ViewToggleAction = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/Input/Actions/MS/IA_ViewToggle.IA_ViewToggle"));
	UInputMappingContext* PlayerContext = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Input/IMC_MSPlayer.IMC_MSPlayer"));
	if (!TestNotNull(TEXT("IA_ViewToggle asset exists"), ViewToggleAction) ||
		!TestNotNull(TEXT("IMC_MSPlayer asset exists"), PlayerContext))
	{
		return false;
	}
	const bool bMappedToP = PlayerContext->GetMappings().ContainsByPredicate(
		[ViewToggleAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == ViewToggleAction && Mapping.Key == EKeys::P;
		});
	TestTrue(TEXT("IA_ViewToggle is mapped to P in IMC_MSPlayer"), bMappedToP);
	Player->SetCameraViewPreset(EMSCameraViewPreset::RightShoulder, false);
	Player->ToggleCameraViewPreset();
	TestEqual(TEXT("P-equivalent trigger toggles RightShoulder to HighOverhead"),
		Player->GetCameraViewPreset(), EMSCameraViewPreset::HighOverhead);
	Player->ToggleCameraViewPreset();
	TestEqual(TEXT("second P-equivalent trigger toggles HighOverhead to RightShoulder"),
		Player->GetCameraViewPreset(), EMSCameraViewPreset::RightShoulder);

	// --- 2. スコープFOV遷移(GetCameraView内補間=カメラマネージャ更新の一部として安定適用) ---
	{
		const float DefaultFOV = Camera->FieldOfView; // コンストラクタでDefaultFieldOfView(60)から開始
		TestEqual(TEXT("初期状態: 非スコープ"), Camera->IsScoped(), false);
		TestEqual(TEXT("初期状態: 通常FOV=60"), DefaultFOV, 60.0f);

		// スコープON(Unity: scopedFieldOfView=12相当の値を武器データから受領する想定)
		Camera->SetScoped(true, 12.0f, 0.35f);
		TestTrue(TEXT("スコープON: IsScoped=true"), Camera->IsScoped());
		TestEqual(TEXT("スコープON直後: FOVは即変化しない(補間はGetCameraView内)"),
			Camera->FieldOfView, DefaultFOV);

		// 補間途中: 目標へ単調接近している(実フレーム相当のdt=16msで確認。
		// FInterpToは alpha=dt×速度12 のため、dt>=1/12s では1回で目標に到達する点に注意)
		FMinimalViewInfo View;
		Camera->GetCameraView(0.016f, View);
		TestTrue(FString::Printf(TEXT("遷移中: FOVが目標へ向かって減少(%.1f)"), View.FOV),
			View.FOV < DefaultFOV && View.FOV > 12.0f);

		// 収束: FInterpTo(速度12)で2秒あれば十分収束する
		View = AdvanceCameraView(Camera, 2.0f);
		TestTrue(FString::Printf(TEXT("収束: スコープFOV=12へ収束(%.2f)"), View.FOV),
			FMath::IsNearlyEqual(View.FOV, 12.0f, 0.5f));

		// スコープOFF → 通常FOVへ復帰
		Camera->SetScoped(false, 0.0f, 0.0f);
		TestFalse(TEXT("スコープOFF: IsScoped=false"), Camera->IsScoped());
		View = AdvanceCameraView(Camera, 2.0f);
		TestTrue(FString::Printf(TEXT("復帰: 通常FOV=60へ収束(%.2f)"), View.FOV),
			FMath::IsNearlyEqual(View.FOV, DefaultFOV, 0.5f));
	}

	// --- 3. 感度倍率(Look入力はLookSensitivityScale=0.35×本倍率。Input_Look) ---
	{
		TestEqual(TEXT("通常時: 感度倍率=1.0"), Camera->GetLookSensitivityMultiplier(), 1.0f);
		Camera->SetScoped(true, 12.0f, 0.35f);
		TestEqual(TEXT("スコープ中: 感度倍率=ScopedSensitivityMultiplier(0.35)"),
			Camera->GetLookSensitivityMultiplier(), 0.35f);
		Camera->SetScoped(false, 0.0f, 0.0f);
		TestEqual(TEXT("解除後: 感度倍率=1.0へ復帰"), Camera->GetLookSensitivityMultiplier(), 1.0f);
	}

	// --- 4. SetScopedFromWeapon(P4-1接続用。スコープ関連フィールドはUMSRangedWeaponDataが持つ) ---
	{
		// 名前は自動生成(テスト再実行時のTransientパッケージ内での名前衝突を回避)
		UMSRangedWeaponData* ScopeWeapon = NewObject<UMSRangedWeaponData>(GetTransientPackage());
		ScopeWeapon->bIsScopeWeapon = true;
		ScopeWeapon->ScopedFieldOfView = 20.0f;
		ScopeWeapon->ScopedSensitivityMultiplier = 0.5f;

		UMSRangedWeaponData* NonScopeWeapon = NewObject<UMSRangedWeaponData>(GetTransientPackage());
		NonScopeWeapon->bIsScopeWeapon = false;

		Camera->SetScopedFromWeapon(ScopeWeapon, true);
		TestTrue(TEXT("スコープ武器: IsScoped=true"), Camera->IsScoped());
		TestEqual(TEXT("スコープ武器: 感度倍率=武器データ値(0.5)"), Camera->GetLookSensitivityMultiplier(), 0.5f);
		const FMinimalViewInfo View = AdvanceCameraView(Camera, 2.0f);
		TestTrue(FString::Printf(TEXT("スコープ武器: FOV=武器データ値20へ収束(%.2f)"), View.FOV),
			FMath::IsNearlyEqual(View.FOV, 20.0f, 0.5f));

		Camera->SetScopedFromWeapon(NonScopeWeapon, true);
		TestFalse(TEXT("非スコープ武器への持ち替え: スコープ解除"), Camera->IsScoped());

		Camera->SetScopedFromWeapon(ScopeWeapon, true);
		Camera->SetScopedFromWeapon(nullptr, true);
		TestFalse(TEXT("null武器: スコープ解除"), Camera->IsScoped());
	}

	UE_LOG(LogNeoMSGO, Log, TEXT("[P3-5] カメラ構造保証/スコープFOV遷移/感度倍率テスト完了"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
