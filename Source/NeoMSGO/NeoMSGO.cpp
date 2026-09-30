// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoMSGO.h"
#include "Modules/ModuleManager.h"
#include "Core/MSTeamStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/NetworkVersion.h"
#include "Online/MSRoomRegistryServer.h"
#include "Online/MSRoomSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#include "FileHelpers.h"
#endif

/**
 * ゲームモジュール。
 * 起動時に陣営態度Solver(UMSTeamStatics::SolveAttitude)をエンジンへ登録し、
 * AIPerception を含む全経路の陣営判定規則を一元化する(計画書 B-2 / UE5_Architecture §4.2)。
 */
class FNeoMSGOModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		// Direct-IP connections also need to reject older packed movement payloads.
		FNetworkVersion::SetGameNetworkProtocolVersion(MSRoomProtocol::NetworkVersion);
		FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(MSRoomProtocol::NetworkVersion);
		FModuleManager::Get().LoadModule(TEXT("BinkAudioDecoder"));
		UMSTeamStatics::RegisterAttitudeSolver();

		if (FParse::Param(FCommandLine::Get(), TEXT("RoomRegistryServer")))
		{
			uint32 RegistryPort = 17788;
			FParse::Value(FCommandLine::Get(), TEXT("Port="), RegistryPort);
			RoomRegistryServer = MakeUnique<FMSRoomRegistryHttpServer>();
			if (!RoomRegistryServer->Start(RegistryPort))
			{
				UE_LOG(LogMSRoomRegistry, Error,
					TEXT("Room registry special mode failed to start on port %u"), RegistryPort);
			}

#if WITH_EDITOR
			EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
			{
				if (GIsEditor && GEditor)
				{
					UEditorLoadingAndSavingUtils::NewBlankMap(false);
					UE_LOG(LogMSRoomRegistry, Display,
						TEXT("Room registry mode loaded an empty transient map; no playable level is active"));
				}
			});
#endif
		}
	}

	virtual void ShutdownModule() override
	{
		if (EngineInitCompleteHandle.IsValid())
		{
			FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
			EngineInitCompleteHandle.Reset();
		}
		RoomRegistryServer.Reset();
		// モジュールアンロード/リロード時にダングリングポインタが残らないよう、
		// StartupModule で登録したSolverを解除する。
		UMSTeamStatics::UnregisterAttitudeSolver();
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	TUniquePtr<FMSRoomRegistryHttpServer> RoomRegistryServer;
	FDelegateHandle EngineInitCompleteHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE( FNeoMSGOModule, NeoMSGO, "NeoMSGO" );

DEFINE_LOG_CATEGORY(LogNeoMSGO)
