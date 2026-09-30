// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NeoMSGO : ModuleRules
{
	public NeoMSGO(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"BinkAudioDecoder",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Niagara",
			"NavigationSystem",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"AssetRegistry"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "RenderCore", "RHI" });
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Crypt32.lib");
		}

		// P1-2: DataAsset生成コマンドレット(Editor限定)がAsset作成/保存APIを使うためUnrealEdをEditorビルドのみ追加
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"StateTreeEditorModule",
				"PropertyBindingUtils"
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"NeoMSGO"
		});

		// #236: the in-game credits read the maintained source documents instead of a copied
		// string/data asset. Stage those same files beside packaged binaries; editor runs read
		// them directly from ProjectDir/docs.
		string CreditsSourceDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "docs"));
		RuntimeDependencies.Add("$(TargetOutputDir)/Credits/AUDIO_ASSET_CREDITS.md",
			Path.Combine(CreditsSourceDir, "AUDIO_ASSET_CREDITS.md"));
		RuntimeDependencies.Add("$(TargetOutputDir)/Credits/ENVIRONMENT_ASSET_CREDITS.md",
			Path.Combine(CreditsSourceDir, "ENVIRONMENT_ASSET_CREDITS.md"));

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
