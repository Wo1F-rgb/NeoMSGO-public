// Copyright NeoMSGO Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Data/MSMapDefinitionRow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapDefinitionTableTest, "NeoMSGO.Data.MapDefinition.TableAndLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMapDefinitionPackagesTest, "NeoMSGO.Data.MapDefinition.PackageAvailability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMSMapDefinitionTableTest::RunTest(const FString&)
{
	const TArray<FMSMapDefinitionRow>& Definitions = FMSMapDefinitionRow::GetMapDefinitions();
	TestEqual(TEXT("exactly ten map definitions exist"), Definitions.Num(), 10);

	struct FExpectedMap
	{
		const TCHAR* MapId;
		const TCHAR* DisplayName;
		const TCHAR* LevelPackagePath;
		EMSMapBiome Biome;
		bool bPlayable;
		bool bSpace;
		bool bVisibleInSelection;
	};
	const FExpectedMap Expected[] = {
		{TEXT("UnrealizerColony"), TEXT("アンリアライザコロニー"), TEXT("/Game/Maps/Lvl_GrandBattlefield"), EMSMapBiome::Colony, true, false, true},
		{TEXT("NewYark"), TEXT("ニューヤーク"), TEXT("/Game/Maps/Lvl_Biome_City"), EMSMapBiome::City, true, false, false},
		{TEXT("NewYorkTaigan"), TEXT("ニューヤーク -対岸の銃火-"), TEXT("/Game/Maps/Lvl_NewYork_Taigan"), EMSMapBiome::City, true, false, true},
		{TEXT("Daibao"), TEXT("ダイバオ"), TEXT("/Game/Maps/Lvl_Biome_Daibao"), EMSMapBiome::Coast, true, false, true},
		{TEXT("Taklamakan"), TEXT("タクラマカン砂漠"), TEXT(""), EMSMapBiome::Desert, false, false, true},
		{TEXT("TaklamakanRuinsPrototype"), TEXT("タクラマカン遺跡 -勇往邁進-（試作）"), TEXT("/Game/Maps/Lvl_Taklamakan_Ruins_Prototype"), EMSMapBiome::Desert, true, false, true},
		{TEXT("SoutheastAsia"), TEXT("東南アジア戦線"), TEXT(""), EMSMapBiome::Forest, false, false, true},
		{TEXT("OdessaCoast"), TEXT("オデッサ沿岸部"), TEXT(""), EMSMapBiome::Coast, false, false, true},
		{TEXT("TorringtonPort"), TEXT("トリントン港湾基地"), TEXT(""), EMSMapBiome::Port, false, false, true},
		{TEXT("Konpeitoh"), TEXT("コンペイトウ"), TEXT(""), EMSMapBiome::Space, false, true, true}
	};

	TSet<FName> MapIds;
	for (int32 Index = 0; Index < Definitions.Num() && Index < UE_ARRAY_COUNT(Expected); ++Index)
	{
		const FMSMapDefinitionRow& Definition = Definitions[Index];
		const FExpectedMap& ExpectedMap = Expected[Index];
		TestEqual(FString::Printf(TEXT("map %d id"), Index), Definition.MapId, FName(ExpectedMap.MapId));
		TestEqual(FString::Printf(TEXT("map %d display name"), Index), Definition.DisplayName.ToString(), FString(ExpectedMap.DisplayName));
		TestEqual(FString::Printf(TEXT("map %d level path"), Index), Definition.LevelPackagePath, FString(ExpectedMap.LevelPackagePath));
		TestEqual(FString::Printf(TEXT("map %d biome"), Index), Definition.Biome, ExpectedMap.Biome);
		TestEqual(FString::Printf(TEXT("map %d playable flag"), Index), Definition.bPlayable, ExpectedMap.bPlayable);
		TestEqual(FString::Printf(TEXT("map %d space flag"), Index), Definition.bSpace, ExpectedMap.bSpace);
		TestEqual(FString::Printf(TEXT("map %d selection visibility"), Index), Definition.bVisibleInSelection, ExpectedMap.bVisibleInSelection);
		TestFalse(FString::Printf(TEXT("map %d id is unique"), Index), MapIds.Contains(Definition.MapId));
		MapIds.Add(Definition.MapId);
		TestEqual(FString::Printf(TEXT("map %d id lookup"), Index), FMSMapDefinitionRow::FindMapById(Definition.MapId), &Definition);
	}

	const FMSMapDefinitionRow* Colony = FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_GrandBattlefield"));
	const FMSMapDefinitionRow* City = FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_Biome_City"));
	const FMSMapDefinitionRow* Taigan = FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_NewYork_Taigan"));
	const FMSMapDefinitionRow* Daibao = FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_Biome_Daibao"));
	const FMSMapDefinitionRow* TaklamakanDesert = FMSMapDefinitionRow::FindMapById(TEXT("Taklamakan"));
	const FMSMapDefinitionRow* TaklamakanRuins = FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_Taklamakan_Ruins_Prototype"));
	TestTrue(TEXT("GrandBattlefield resolves to UnrealizerColony"), Colony && Colony->MapId == TEXT("UnrealizerColony"));
	TestTrue(TEXT("Biome_City resolves to NewYark"), City && City->MapId == TEXT("NewYark"));
	TestTrue(TEXT("new Taigan level resolves to NewYorkTaigan"), Taigan && Taigan->MapId == TEXT("NewYorkTaigan"));
	TestTrue(TEXT("Biome_Daibao resolves to Daibao"), Daibao && Daibao->MapId == TEXT("Daibao"));
	TestTrue(TEXT("Taklamakan Ruins level resolves to its own prototype entry"),
		TaklamakanRuins && TaklamakanRuins->MapId == TEXT("TaklamakanRuinsPrototype"));
	TestTrue(TEXT("original Taklamakan desert entry remains unplayable"),
		TaklamakanDesert && !TaklamakanDesert->bPlayable && TaklamakanDesert->LevelPackagePath.IsEmpty());
	const TArray<FMSMapDefinitionRow>& Visible = FMSMapDefinitionRow::GetVisibleMapDefinitions();
	TestEqual(TEXT("selection has one fewer row than the complete archive"), Visible.Num(), Definitions.Num() - 1);
	TestFalse(TEXT("archived NewYark is absent from selection"), Visible.ContainsByPredicate(
		[](const FMSMapDefinitionRow& Definition) { return Definition.MapId == TEXT("NewYark"); }));
	TestTrue(TEXT("NewYorkTaigan is in selection"), Visible.ContainsByPredicate(
		[](const FMSMapDefinitionRow& Definition) { return Definition.MapId == TEXT("NewYorkTaigan"); }));
	TestTrue(TEXT("Taklamakan Ruins prototype is in selection"), Visible.ContainsByPredicate(
		[](const FMSMapDefinitionRow& Definition) { return Definition.MapId == TEXT("TaklamakanRuinsPrototype"); }));
	TestNull(TEXT("unknown level name does not resolve"), FMSMapDefinitionRow::FindMapByLevelName(TEXT("Lvl_Unknown")));
	return true;
}

bool FMSMapDefinitionPackagesTest::RunTest(const FString&)
{
	for (const FMSMapDefinitionRow& Definition : FMSMapDefinitionRow::GetMapDefinitions())
	{
		if (Definition.bPlayable)
		{
			TestFalse(FString::Printf(TEXT("%s has a level package path"), *Definition.MapId.ToString()), Definition.LevelPackagePath.IsEmpty());
			TestTrue(FString::Printf(TEXT("%s level package exists"), *Definition.MapId.ToString()),
				FPackageName::DoesPackageExist(Definition.LevelPackagePath));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s has no level package until playable"), *Definition.MapId.ToString()), Definition.LevelPackagePath.IsEmpty());
		}
	}
	return true;
}

#endif
