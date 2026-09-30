# 独立ワークスペース

公開source copyは既存projectから独立した通常ファイルで、別projectへのlinkはありません。元projectのpath・設定・Backend dataへfallbackしない構成を保ちます。欠落したfileは公開copy側で解消し、元環境から補いません。同じUE 5.7.4を使うことはできますが、project状態やuser dataは共有しません。

## 起動と保存先

Tools/Launch-PublicEditor.batまたはTools/Launch-PublicEditor.ps1を使います。CheckOnlyはWindows PowerShell 5.1で通過済みです。Editor用UserDirは公開repo内の.local/Editorへ分けられ、この場所はgitignore対象です。サブモニターが必要です。直接NeoMSGO.uprojectを開くとUserDirの分離を経由しないため推奨しません。

fresh-assets-final UE commandletはexit 0で完了し、D側Savedの独立を再確認しました。Editor実起動・描画・game開始・interactive playからの保存は未確認です。追加のPlay processやclientにはそれぞれ異なるUserDirが必要で、その起動分離はまだ受入していません。

## モードとport

OfflineLocalは公開copyの既定です。単独BOT利用ではBackend不要ですが、公開copyでの実プレイは未確認です。

| 用途 | 公開copyの設定port |
|---|---:|
| account/API | 17980 |
| admin | 17981 |
| room registry | 17788 |
| game host | 7787 |

これらは設定値であり、service起動や通信成功を示しません。同じPCで複数Backendを起動する場合はAPI/admin listenerが衝突しないよう分けます。同一PCの複数game hostもlisten portを分けます。別PCから参加させるhostは、clientから到達できるaddressを広告します。loopbackは別PCからの接続先になりません。

## Backend data

OfflineLocalではBackend起動は不要です。自分のBackendを使う場合、SQLite DB・生成key・runtime config・稼働情報をsource repo外の専用data directoryに置き、他環境と共有しません。実DBやkeyを公開copyへ持ち込まないでください。

Backend test 28件はNode.js 24.16.0で成功しました。単体testの結果であり、オンラインlogin、友人PC接続、実roomの作成・退出を検証したものではありません。account方式は未決で、招待制が実装済みとは扱いません。

## 確認済みと未確認

確認済み: 公開copyは独立した通常fileでlinkなし、launcherのCheckOnly、Editor用UserDirとD側Savedの分離、Editor buildとmetadata sanitizer後の最終build、native sanitizer 97/97・cleaned failure 0件、headless UE Automation 25件、AssetRegistry 1,605 package/5 map/dependency欠落0件、Backend test 28件、binary asset path scanでmachine-specific path 0件。Editor buildはVS2026/MSVC 14.51で、UE preferred MSVC 14.44のwarningを記録しています。これらは実画面・実プレイ・cook/packageの確認ではありません。

未確認: 公開copyのEditor描画、OfflineLocal BOT戦、cook/packageと生成物の起動、複数Play processのUserDir分離、Backend service起動後の保存先、友人PC、オンラインaccount、room作成・検索・join/leave。過去の別buildのオフライン二process直接接続は、これらを代替しません。
