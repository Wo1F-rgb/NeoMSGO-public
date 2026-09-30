// Copyright NeoMSGO Project. All Rights Reserved.

#include "Data/MSMapDefinitionRow.h"

#include "Misc/PackageName.h"

const TArray<FMSMapDefinitionRow>& FMSMapDefinitionRow::GetMapDefinitions()
{
	static const TArray<FMSMapDefinitionRow> Definitions = []
	{
		TArray<FMSMapDefinitionRow> Result;
		auto Add = [&Result](const TCHAR* MapId, const TCHAR* DisplayName, const TCHAR* LevelPackagePath,
			const EMSMapBiome Biome, const FLinearColor& ThumbnailColor, const bool bPlayable, const bool bSpace,
			const bool bVisibleInSelection = true)
		{
			FMSMapDefinitionRow& Row = Result.AddDefaulted_GetRef();
			Row.MapId = FName(MapId);
			Row.DisplayName = FText::FromString(DisplayName);
			Row.LevelPackagePath = LevelPackagePath;
			Row.Biome = Biome;
			Row.ThumbnailColor = ThumbnailColor;
			Row.bPlayable = bPlayable;
			Row.bSpace = bSpace;
			Row.bVisibleInSelection = bVisibleInSelection;
		};

		Add(TEXT("UnrealizerColony"), TEXT("アンリアライザコロニー"), TEXT("/Game/Maps/Lvl_GrandBattlefield"),
			EMSMapBiome::Colony, FLinearColor(0.24f, 0.32f, 0.22f, 1.0f), true, false);
		Add(TEXT("NewYark"), TEXT("ニューヤーク"), TEXT("/Game/Maps/Lvl_Biome_City"),
			EMSMapBiome::City, FLinearColor(0.18f, 0.28f, 0.36f, 1.0f), true, false, false);
		Add(TEXT("NewYorkTaigan"), TEXT("ニューヤーク -対岸の銃火-"), TEXT("/Game/Maps/Lvl_NewYork_Taigan"),
			EMSMapBiome::City, FLinearColor(0.18f, 0.28f, 0.36f, 1.0f), true, false);
		// 実在お台場(地理院DEM+OSM)ベースの実寸マップ。名称はユーザー決定(2026-07-19)。
		Add(TEXT("Daibao"), TEXT("ダイバオ"), TEXT("/Game/Maps/Lvl_Biome_Daibao"),
			EMSMapBiome::Coast, FLinearColor(0.16f, 0.30f, 0.44f, 1.0f), true, false);
		Add(TEXT("Taklamakan"), TEXT("タクラマカン砂漠"), TEXT(""),
			EMSMapBiome::Desert, FLinearColor(0.48f, 0.36f, 0.18f, 1.0f), false, false);
		Add(TEXT("TaklamakanRuinsPrototype"), TEXT("タクラマカン遺跡 -勇往邁進-（試作）"), TEXT("/Game/Maps/Lvl_Taklamakan_Ruins_Prototype"),
			EMSMapBiome::Desert, FLinearColor(0.48f, 0.36f, 0.18f, 1.0f), true, false);
		Add(TEXT("SoutheastAsia"), TEXT("東南アジア戦線"), TEXT(""),
			EMSMapBiome::Forest, FLinearColor(0.16f, 0.31f, 0.20f, 1.0f), false, false);
		Add(TEXT("OdessaCoast"), TEXT("オデッサ沿岸部"), TEXT(""),
			EMSMapBiome::Coast, FLinearColor(0.14f, 0.34f, 0.42f, 1.0f), false, false);
		Add(TEXT("TorringtonPort"), TEXT("トリントン港湾基地"), TEXT(""),
			EMSMapBiome::Port, FLinearColor(0.30f, 0.33f, 0.36f, 1.0f), false, false);
		Add(TEXT("Konpeitoh"), TEXT("コンペイトウ"), TEXT(""),
			EMSMapBiome::Space, FLinearColor(0.12f, 0.12f, 0.24f, 1.0f), false, true);
		return Result;
	}();
	return Definitions;
}

const TArray<FMSMapDefinitionRow>& FMSMapDefinitionRow::GetVisibleMapDefinitions()
{
	static const TArray<FMSMapDefinitionRow> Definitions = GetMapDefinitions().FilterByPredicate(
		[](const FMSMapDefinitionRow& Definition) { return Definition.bVisibleInSelection; });
	return Definitions;
}

const FMSMapDefinitionRow* FMSMapDefinitionRow::FindMapById(const FName MapId)
{
	return GetMapDefinitions().FindByPredicate(
		[MapId](const FMSMapDefinitionRow& Definition) { return Definition.MapId == MapId; });
}

const FMSMapDefinitionRow* FMSMapDefinitionRow::FindMapByLevelName(const FString& ShortLevelName)
{
	return GetMapDefinitions().FindByPredicate([&ShortLevelName](const FMSMapDefinitionRow& Definition)
	{
		return !Definition.LevelPackagePath.IsEmpty()
			&& FPackageName::GetShortName(Definition.LevelPackagePath).Equals(ShortLevelName, ESearchCase::IgnoreCase);
	});
}
