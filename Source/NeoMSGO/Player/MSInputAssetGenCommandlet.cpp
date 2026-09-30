// Copyright NeoMSGO Project. All Rights Reserved.

#include "Player/MSInputAssetGenCommandlet.h"

#if WITH_EDITOR

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "InputCoreTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/Linker.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace
{
	/** IA_*(Content/Input/Actions/MS/)の識別・軸種定義 */
	struct FMSInputActionDef
	{
		FString AssetName;
		EInputActionValueType ValueType = EInputActionValueType::Boolean;
	};

	/**
	 * 生成先パッケージルート。ThirdPersonテンプレート由来の Content/Input/Actions/ 直下
	 * (IA_Move / IA_Look / IA_Jump 等、BP_ThirdPersonCharacterが直接参照)とは衝突させない
	 * ため、専用サブフォルダに分離する。
	 */
	const FString ActionsPackageRoot = TEXT("/Game/Input/Actions/MS/");
	const FString ContextPackageName = TEXT("/Game/Input/IMC_MSPlayer");

	TArray<FMSInputActionDef> BuildActionDefinitions()
	{
		return {
			{ TEXT("IA_Move"),    EInputActionValueType::Axis2D },
			{ TEXT("IA_Look"),    EInputActionValueType::Axis2D },
			{ TEXT("IA_Dash"),    EInputActionValueType::Boolean },
			{ TEXT("IA_Jump"),    EInputActionValueType::Boolean }, // Unity版Jump=ホバー上昇トリガー(§21)
			{ TEXT("IA_Crouch"),  EInputActionValueType::Boolean },
			{ TEXT("IA_Fire"),    EInputActionValueType::Boolean },
			{ TEXT("IA_Reload"),  EInputActionValueType::Boolean },
			{ TEXT("IA_LockOn"),  EInputActionValueType::Boolean },
			{ TEXT("IA_Action"),  EInputActionValueType::Boolean }, // 汎用インタラクト(補給回収等。§SupplyPickup)
			{ TEXT("IA_Weapon1"), EInputActionValueType::Boolean },
			{ TEXT("IA_Weapon2"), EInputActionValueType::Boolean },
			{ TEXT("IA_Weapon3"), EInputActionValueType::Boolean },
			{ TEXT("IA_Module"),  EInputActionValueType::Boolean },
			{ TEXT("IA_ViewToggle"), EInputActionValueType::Boolean },
			{ TEXT("IA_Recall"),  EInputActionValueType::Boolean },
			{ TEXT("IA_EmergencyDodge"), EInputActionValueType::Boolean },
			{ TEXT("IA_Tab"),     EInputActionValueType::Boolean },
			{ TEXT("IA_Esc"),     EInputActionValueType::Boolean },
		};
	}

	/** 新規パッケージにUObjectを生成する共通ヘルパ(P1-3 MSWeaponDataGenCommandletと同一パターン) */
	template <typename T>
	T* CreateAssetInPackage(const FString& PackageName, const FString& AssetName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		if (T* Existing = FindObject<T>(Package, *AssetName))
		{
			return Existing;
		}
		return NewObject<T>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	bool SavePackageToDisk(UPackage* Package, UObject* Asset, const FString& PackageName, const bool bNewAsset)
	{
		Asset->MarkPackageDirty();
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Asset);
		}

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		// IMC_MSPlayer is also referenced by AMSPlayerCharacter's CDO during commandlet startup.
		// Release that package loader explicitly so Windows does not retain a read handle while
		// SavePackage atomically replaces the existing uasset.
		ResetLoadersForSave(Package, *PackageFileName);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
	}
}

int32 UMSInputAssetGenCommandlet::Main(const FString& Params)
{
	int32 NumSaved = 0;
	int32 NumFailed = 0;

	// ---- 1. InputAction一式(IA_*) ----
	TMap<FString, UInputAction*> Actions;
	for (const FMSInputActionDef& Def : BuildActionDefinitions())
	{
		const FString PackageName = ActionsPackageRoot + Def.AssetName;
		const bool bNewAsset = !FPackageName::DoesPackageExist(PackageName);
		UInputAction* Action = CreateAssetInPackage<UInputAction>(PackageName, Def.AssetName);
		if (!Action)
		{
			UE_LOG(LogTemp, Error, TEXT("MSInputAssetGen: failed to create %s"), *Def.AssetName);
			++NumFailed;
			continue;
		}

		Action->ValueType = Def.ValueType;
		const bool bIsModuleAction = Def.AssetName == TEXT("IA_Module");
		if (bIsModuleAction)
		{
			Action->Triggers.Reset();
			UInputTriggerHold* Hold = NewObject<UInputTriggerHold>(Action);
			Hold->HoldTimeThreshold = 1.0f;
			Hold->bIsOneShot = true;
			Action->Triggers.Add(Hold);
		}
		Actions.Add(Def.AssetName, Action);

		if (!bNewAsset && !bIsModuleAction)
		{
			continue;
		}
		if (SavePackageToDisk(Action->GetOutermost(), Action, PackageName, bNewAsset))
		{
			UE_LOG(LogTemp, Display, TEXT("MSInputAssetGen: saved %s"), *PackageName);
			++NumSaved;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MSInputAssetGen: failed to save %s"), *PackageName);
			++NumFailed;
		}
	}

	// ---- 2. InputMappingContext(IMC_MSPlayer) ----
	const bool bNewContext = !FPackageName::DoesPackageExist(ContextPackageName);
	UInputMappingContext* Context = CreateAssetInPackage<UInputMappingContext>(ContextPackageName, TEXT("IMC_MSPlayer"));
	if (!Context)
	{
		UE_LOG(LogTemp, Error, TEXT("MSInputAssetGen: failed to create IMC_MSPlayer"));
		return 1;
	}
	Context->UnmapAll();

	auto GetAction = [&Actions](const TCHAR* Name) -> UInputAction*
	{
		UInputAction* const* Found = Actions.Find(Name);
		return Found ? *Found : nullptr;
	};

	// Move(WASD, Axis2D)。PlayerMappable=false(保護。PlayerMappableKeySettings未設定のまま=リバインド不可)
	if (UInputAction* Move = GetAction(TEXT("IA_Move")))
	{
		// W: 前方(+Y)。Swizzle(YXZ)でキーボードのスカラー入力(X)をY軸へ回す
		FEnhancedActionKeyMapping& W = Context->MapKey(Move, EKeys::W);
		W.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));

		// S: 後方(-Y)。Swizzle(YXZ)+Negate
		FEnhancedActionKeyMapping& S = Context->MapKey(Move, EKeys::S);
		S.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		S.Modifiers.Add(NewObject<UInputModifierNegate>(Context));

		// A: 左(-X)。Negateのみ
		FEnhancedActionKeyMapping& A = Context->MapKey(Move, EKeys::A);
		A.Modifiers.Add(NewObject<UInputModifierNegate>(Context));

		// D: 右(+X)。無加工
		Context->MapKey(Move, EKeys::D);
	}

	// Look(マウスXY, Axis2D)。PlayerMappable=false(保護)。
	// 生のMouse2D DeltaはY成分がスクリーン座標系(下方向が正)のため、Negate(Y軸のみ)を掛けないと
	// AddControllerPitchInputの符号と逆転し、マウスを上に動かすとカメラが下を向く(上下反転)。
	if (UInputAction* Look = GetAction(TEXT("IA_Look")))
	{
		FEnhancedActionKeyMapping& LookMapping = Context->MapKey(Look, EKeys::Mouse2D);
		UInputModifierNegate* NegateY = NewObject<UInputModifierNegate>(Context);
		NegateY->bX = false; NegateY->bZ = false;
		LookMapping.Modifiers.Add(NegateY);
	}

	// Dash/Boost(Space押下保持。ユーザー確定仕様 2026-07-19: Space=ブースト, Shift=ジャンプ)
	if (UInputAction* Dash = GetAction(TEXT("IA_Dash")))
	{
		Context->MapKey(Dash, EKeys::SpaceBar);
	}

	// Jump(=ホバー上昇トリガー。Shift)
	if (UInputAction* Jump = GetAction(TEXT("IA_Jump")))
	{
		Context->MapKey(Jump, EKeys::LeftShift);
	}

	// Crouch(トグル。Ctrl。仕様書の既定キー割当に合わせる)
	if (UInputAction* Crouch = GetAction(TEXT("IA_Crouch")))
	{
		Context->MapKey(Crouch, EKeys::LeftControl);
	}

	// Fire(左クリック)
	if (UInputAction* Fire = GetAction(TEXT("IA_Fire")))
	{
		Context->MapKey(Fire, EKeys::LeftMouseButton);
	}

	// Reload(E。仕様書の既定キー割当に合わせる)
	if (UInputAction* Reload = GetAction(TEXT("IA_Reload")))
	{
		Context->MapKey(Reload, EKeys::E);
	}

	// LockOn(右クリック、トグル式。仕様書の既定キー割当に合わせる)
	if (UInputAction* LockOn = GetAction(TEXT("IA_LockOn")))
	{
		Context->MapKey(LockOn, EKeys::RightMouseButton);
	}

	// Action(汎用インタラクト。Fは固有モジュール専用になったためGへ移動)
	if (UInputAction* Action = GetAction(TEXT("IA_Action")))
	{
		Context->MapKey(Action, EKeys::G);
	}

	// Weapon1/2/3(数字キー)
	if (UInputAction* Weapon1 = GetAction(TEXT("IA_Weapon1")))
	{
		Context->MapKey(Weapon1, EKeys::One);
	}
	if (UInputAction* Weapon2 = GetAction(TEXT("IA_Weapon2")))
	{
		Context->MapKey(Weapon2, EKeys::Two);
	}
	if (UInputAction* Weapon3 = GetAction(TEXT("IA_Weapon3")))
	{
		Context->MapKey(Weapon3, EKeys::Three);
	}
	if (UInputAction* Module = GetAction(TEXT("IA_Module")))
	{
		Context->MapKey(Module, EKeys::F);
	}
	if (UInputAction* ViewToggle = GetAction(TEXT("IA_ViewToggle")))
	{
		Context->MapKey(ViewToggle, EKeys::P);
	}
	if (UInputAction* Recall = GetAction(TEXT("IA_Recall")))
	{
		Context->MapKey(Recall, EKeys::Z);
	}
	if (UInputAction* EmergencyDodge = GetAction(TEXT("IA_EmergencyDodge")))
	{
		Context->MapKey(EmergencyDodge, EKeys::X);
	}

	// Tab(マップ表示)
	if (UInputAction* Tab = GetAction(TEXT("IA_Tab")))
	{
		Context->MapKey(Tab, EKeys::Tab);
	}

	// Esc(メニュー/ポーズ)
	if (UInputAction* Esc = GetAction(TEXT("IA_Esc")))
	{
		Context->MapKey(Esc, EKeys::Escape);
	}

	if (SavePackageToDisk(Context->GetOutermost(), Context, ContextPackageName, bNewContext))
	{
		UE_LOG(LogTemp, Display, TEXT("MSInputAssetGen: saved %s (mappings=%d)"), *ContextPackageName, Context->GetMappings().Num());
		++NumSaved;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MSInputAssetGen: failed to save %s"), *ContextPackageName);
		++NumFailed;
	}

	UE_LOG(LogTemp, Display, TEXT("MSInputAssetGen: done. Saved=%d Failed=%d"), NumSaved, NumFailed);

	return NumFailed > 0 ? 1 : 0;
}

#endif // WITH_EDITOR
