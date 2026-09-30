#include "Online/MSLocalServerLauncher.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"

namespace
{
    void CheckHealth(FString Url, TFunction<void(bool, const FString&)> Completion)
    {
        auto Request = FHttpModule::Get().CreateRequest();
        Url.RemoveFromEnd(TEXT("/"));
        Request->SetURL(Url + TEXT("/health")); Request->SetVerb(TEXT("GET")); Request->SetTimeout(5.0f);
        auto Finish = MakeShared<TFunction<void(bool, const FString&)>>(MoveTemp(Completion));
        auto Completed = MakeShared<bool>(false);
        auto CompleteOnce = [Finish, Completed](bool bHealthy, const FString& Realm)
        {
            if (*Completed) return;
            *Completed = true;
            (*Finish)(bHealthy, Realm);
        };
        Request->OnProcessRequestComplete().BindLambda([CompleteOnce](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            TSharedPtr<FJsonObject> Json; bool bOk = false; FString Realm; double Protocol = 0;
            const bool bHealthy = bConnected && Response.IsValid() && Response->GetResponseCode() == 200
                && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json)
                && Json.IsValid() && Json->TryGetBoolField(TEXT("ok"), bOk) && bOk
                && Json->TryGetStringField(TEXT("realm_id"), Realm) && !Realm.IsEmpty()
                && Json->TryGetNumberField(TEXT("protocol"), Protocol) && Protocol == 1;
            CompleteOnce(bHealthy, Realm);
        });
        if (!Request->ProcessRequest()) CompleteOnce(false, FString());
    }
}

bool MSLocalServerLauncher::IsAvailable()
{
#if PLATFORM_WINDOWS
    const FString Root = FPaths::ProjectDir();
    return IFileManager::Get().FileExists(*FPaths::Combine(Root, TEXT("StartLoginServer.bat")))
        && IFileManager::Get().FileExists(*FPaths::Combine(Root, TEXT("Tools/Online/StartAccountServer.ps1")))
        && IFileManager::Get().FileExists(*FPaths::Combine(Root, TEXT("Backend/package.json")));
#else
    return false;
#endif
}

void MSLocalServerLauncher::Start(const FString& AuthBaseUrl, TFunction<void(bool, const FString&)> Completion)
{
    const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("StartLoginServer.bat")));
    if (!IsAvailable())
    {
        Completion(false, TEXT("この配布版には共通サーバーを同梱していません。配布者にサーバーの起動と接続先を確認してください。"));
        return;
    }
#if PLATFORM_WINDOWS
    // The URL is never passed to the shell. Only the fixed, local launcher is run.
    Async(EAsyncExecution::Thread, [Script, AuthBaseUrl, Completion = MoveTemp(Completion)]() mutable
    {
        const FString Command = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
        const FString Arguments = FString::Printf(TEXT("/d /s /c \"\"%s\" --no-pause\""), *Script);
        FProcHandle Process = FPlatformProcess::CreateProc(*Command, *Arguments, true, true, true, nullptr, 0, *FPaths::GetPath(Script), nullptr);
        int32 Code = -1;
        bool bStarted = Process.IsValid();
        bool bStillStarting = false;
        if (bStarted)
        {
            const double Deadline = FPlatformTime::Seconds() + 60.0;
            while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline) FPlatformProcess::Sleep(0.1f);
            bStillStarting = FPlatformProcess::IsProcRunning(Process);
            bStarted = FPlatformProcess::GetProcReturnCode(Process, &Code) && Code == 0;
            FPlatformProcess::CloseProc(Process);
        }
        AsyncTask(ENamedThreads::GameThread, [bStarted, bStillStarting, AuthBaseUrl, Completion = MoveTemp(Completion)]() mutable
        {
            if (!bStarted)
            {
                Completion(false, bStillStarting
                    ? TEXT("起動処理がまだ続いています。少し待ってからデスクトップの「NeoMSGO ログインサーバー」で状態を確認してください。")
                    : TEXT("サーバーを起動できませんでした。デスクトップの「NeoMSGO ログインサーバー」を開いてエラーを確認してください。"));
                return;
            }
            // Verify the local service first, then the game's configured endpoint.
            CheckHealth(TEXT("http://127.0.0.1:17980"), [AuthBaseUrl, Completion = MoveTemp(Completion)](bool bLocalHealthy, const FString& LocalRealm) mutable
            {
                if (!bLocalHealthy) { Completion(false, TEXT("このPCのサーバーに接続できません。デスクトップの起動アイコンで状態を確認してください。")); return; }
                CheckHealth(AuthBaseUrl, [LocalRealm, Completion = MoveTemp(Completion)](bool bHealthy, const FString& Realm) mutable
                {
                    const bool bReady = bHealthy && Realm == LocalRealm;
                    Completion(bReady, bReady
                        ? TEXT("サーバーを起動しました。「ログイン」または「ゲストで再開」を選んでください。対戦ルームは入場後に作成できます。")
                        : TEXT("このPCのサーバーは起動済みですが、表示中の接続先に届かないか別のサーバーです。接続先とTailscaleを確認してください。"));
                });
            });
        });
    });
#else
    Completion(false, TEXT("この起動ボタンはWindows版に対応しています。"));
#endif
}
