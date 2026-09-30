# NeoMSGO 公開ソースの起動案内

GitHub公開先: [Wo1F-rgb/NeoMSGO-public](https://github.com/Wo1F-rgb/NeoMSGO-public)。ソースは通常のGitファイルです。Git LFSは不要です。

## ソースを入手する

- GitHubの **Code → Download ZIP** から取得して展開するか、通常の `git clone` を使います。
- cloneする場合はPowerShellで `git clone https://github.com/Wo1F-rgb/NeoMSGO-public.git` を実行し、作成された `NeoMSGO-public` フォルダへ移動します。
- GitHub Releaseに配布用ソースZIPが掲載された場合も使えます。公開copyの設定・素材を別の場所から補わないでください。

## 必要なものと初回ビルド

- Unreal Engine **5.7.4**。
- Unreal Engine 5.7.4に対応するVisual StudioのC++ toolchainとWindows SDK。
- 初回ビルドは `Tools/Build-PublicEditor.bat` を起動します。Engineを標準場所以外に入れた場合は `UE57_ROOT` にEngineフォルダを指定してください。
- UE5.7.4のBuild.batで `NeoMSGOEditor Win64 Development` をビルドします。下記はUEの標準インストール場所を使うPowerShell例です。Engineを別の場所に入れた場合は `$BuildBat` を変更してください。

```powershell
$BuildBat = Join-Path $env:ProgramFiles 'Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat'
$Project = (Resolve-Path .\NeoMSGO.uproject).Path
& $BuildBat NeoMSGOEditor Win64 Development "-Project=$Project"
if ($LASTEXITCODE -ne 0) { throw "NeoMSGOEditor build failed: $LASTEXITCODE" }
```

公開copyのEditor buildはUE5.7.4とMSVC 14.51で一度成功し、UEからpreferred MSVC 14.44についてwarningが出ています。初回ビルドに成功してからEditorを起動してください。

## OfflineLocalで試す

1. `Tools/Launch-PublicEditor.bat` を起動します。PowerShell版は `Tools/Launch-PublicEditor.ps1` です。2台目のモニターがあればそちらへ開き、1台だけならメインモニターを使います。
2. Editorで `/Game/Maps/Lvl_GrandBattlefield` を開き、Playを押します。
3. タイトルから「ゲスト」で入り、ロビーでルームを作成します。マップにニューヤークを選び、BOTの自動補充を有効にして、作戦開始 → 機体選択 → 出撃地点の選択へ進みます。OfflineLocalが既定なので、Backendやaccount serviceは起動不要です。

事前確認だけ行う場合:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Tools\Launch-PublicEditor.ps1 -CheckOnly
```

LauncherはEditorの保存先をこのrepo内の `.local/Editor` に分離し、公開copy用のportを使います。元の開発環境とはUserDirやBackend dataを共有しません。直接 `NeoMSGO.uproject` を開くとLauncherのUserDir分離を通らないため、Launcherから起動してください。

OfflineLocalではBackendは不要です。portは設定値で、通信成功を示すものではありません。

| 用途 | port |
|---|---:|
| account/API | 17980 |
| admin | 17981 |
| room registry | 17788 |
| game host | 7787 |

GitHubから新しく取得したコピーで、Editorの新規ビルド、1920×1080の描画、ゲストログイン、ニューヤークの25対25 BOT戦、移動・射撃・緊急帰投・再出撃を確認しました。[実画面と確認範囲](docs/PUBLIC_PLAYTEST.md)。友人PC、別PC同士のオンライン対戦、cook/package、全機体・全マップの確認はまだです。

## ライセンス

MITは承認された自作codeのみを対象とします。Content内の第三者model・texture・画像・音声等は個別の条件に従います。第三者素材の来歴・条件の追加確認は保留中であり、MITがContent全体へ適用されるとは案内しません。
