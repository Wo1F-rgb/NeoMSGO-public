// Copyright NeoMSGO Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Voice/MSNavigatorTypes.h"
#include "MSNavigatorSubsystem.generated.h"

/**
 * デフォルト2名(ノエル・アンダーソン/ユウキ・ナカサト)のナビゲーターボイス選択と再生を担う。
 * 選択状態はMSAudioPreset.cppと同じくGConfig(GGameUserSettingsIni)へ永続化する。
 */
UCLASS()
class NEOMSGO_API UMSNavigatorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category="MS|Navigator")
	EMSNavigator GetSelectedNavigator() const;

	/** 即座に反映し、bSaveがtrueならGameUserSettings.iniへ書き込む。 */
	UFUNCTION(BlueprintCallable, Category="MS|Navigator")
	void SetSelectedNavigator(EMSNavigator Navigator, bool bSave = true);

	/**
	 * 選択中ナビゲーターの該当トリガー音声を解決して再生する。
	 * アセット未インポート等でLoadObjectがnullptrを返しても安全にno-opする。
	 * FirstSortieは1戦闘(1回のデプロイ)につき1回だけ発火するようbFirstSortiePlayedThisSessionで抑制する。
	 */
	UFUNCTION(BlueprintCallable, Category="MS|Navigator")
	void PlayLine(EMSNavigatorTrigger Trigger);

	/** 新しい出撃セッションの開始時にFirstSortieガードをリセットする(必要になれば呼び出し側から)。 */
	UFUNCTION(BlueprintCallable, Category="MS|Navigator")
	void ResetFirstSortieGuard() { bFirstSortiePlayedThisSession = false; }

	/** "ノエル・アンダーソン" / "ユウキ・ナカサト" を返す表示名ヘルパー。 */
	static FText GetNavigatorDisplayName(EMSNavigator Navigator);

private:
	/** アセットパス組み立てに使うNavigatorId(例: "NoelAnderson")。 */
	static const TCHAR* GetNavigatorId(EMSNavigator Navigator);

	/** アセットパス組み立てに使うTriggerId(例: "Login")。 */
	static const TCHAR* GetTriggerId(EMSNavigatorTrigger Trigger);

	bool bSelectedNavigatorLoaded = false;
	EMSNavigator SelectedNavigator = EMSNavigator::NoelAnderson;

	/** 1戦闘(1回のデプロイ)につき1回だけFirstSortieを鳴らすためのガード。 */
	bool bFirstSortiePlayedThisSession = false;
};
