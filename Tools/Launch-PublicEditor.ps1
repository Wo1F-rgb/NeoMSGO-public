[CmdletBinding()]
param(
    [string]$EditorExe = (Join-Path $env:ProgramFiles 'Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe'),
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd([char[]]'\/')
$project = Join-Path $root 'NeoMSGO.uproject'
$manifestPath = Join-Path $root 'PublicSourceManifest.json'
$userDir = Join-Path $root '.local/Editor'
foreach ($path in @($root, (Join-Path $root '.local'), $userDir)) {
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
            throw '公開root/UserDirにリンクまたはjunctionを使用できません。'
        }
    }
}
if (-not (Test-Path -LiteralPath $project -PathType Leaf) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw '公開ソースexport済みのproject/manifestがありません。'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.kind -ne 'neomsgo-public-source-export' -or $manifest.postprocess.status -ne 'passed' -or
    $manifest.postprocess.auth_mode -ne 'OfflineLocal') {
    throw '公開コピーのpostprocess証跡を確認できません。'
}
if (-not (Test-Path -LiteralPath $EditorExe -PathType Leaf)) { throw 'UE5.7 UnrealEditor.exeがありません。-EditorExeで指定してください。' }
$gameConfig = Get-Content -LiteralPath (Join-Path $root 'Config/DefaultGame.ini') -Raw
$engineConfig = Get-Content -LiteralPath (Join-Path $root 'Config/DefaultEngine.ini') -Raw
if ($gameConfig -notmatch '(?m)^AuthMode=OfflineLocal\s*$' -or
    $gameConfig -notmatch '(?m)^AuthBaseUrl="?http://127\.0\.0\.1:17980"?\s*$' -or
    $engineConfig -notmatch '(?m)^RegistryBaseUrl=http://127\.0\.0\.1:17788\s*$' -or
    $engineConfig -notmatch '(?m)^AdvertisedRoomAddress=127\.0\.0\.1:7787\s*$') {
    throw '公開コピーの独立した認証/ルーム設定を確認できません。'
}

Add-Type -AssemblyName System.Windows.Forms
$screens = @([System.Windows.Forms.Screen]::AllScreens)
$secondary = @($screens | Where-Object { -not $_.Primary } |
    Sort-Object DeviceName | Select-Object -First 1)
$display = $null
$displayMode = $null
if ($secondary.Count -gt 0) {
    $display = $secondary[0]
    $displayMode = 'secondary'
} else {
    $primary = @($screens | Where-Object { $_.Primary } | Select-Object -First 1)
    if ($primary.Count -gt 0) {
        $display = $primary[0]
    } elseif ($screens.Count -gt 0) {
        $display = $screens[0]
    } else {
        throw '表示画面を検出できません。Editorを起動しません。'
    }
    $displayMode = 'primary_single_monitor_fallback'
}
$bounds = $display.WorkingArea
if ($bounds.Width -lt 900 -or $bounds.Height -lt 600) { throw '表示画面の作業領域が小さすぎます。' }

foreach ($port in @(17980, 17788, 7787)) {
    if (@(Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue).Count -gt 0) {
        throw "TCP port $port は使用中です。既存listenerを停止せず起動を中止しました。"
    }
}
if (@(Get-NetUDPEndpoint -LocalPort 7787 -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'UDP port 7787 は使用中です。既存listenerを停止せず起動を中止しました。'
}
$running = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction Stop | Where-Object {
    ([string]$_.CommandLine).IndexOf($project, [StringComparison]::OrdinalIgnoreCase) -ge 0
})
if ($running.Count -gt 0) { throw 'この公開projectのEditorは既に起動中です。' }

if ($CheckOnly) {
    [ordered]@{ status = 'preflight_ok'; display_monitor = $display.DeviceName; display_mode = $displayMode;
        monitor_working_area = @($bounds.X, $bounds.Y, $bounds.Width, $bounds.Height);
        project = $project; user_dir = $userDir; game_port = 7787; registry_port = 17788;
        auth_port = 17980; editor_exe = $EditorExe } | ConvertTo-Json -Depth 4
    return
}

New-Item -ItemType Directory -Path $userDir -Force | Out-Null
$x = $bounds.X + 20
$y = $bounds.Y + 20
$width = $bounds.Width - 40
$height = $bounds.Height - 40
$arguments = @("`"$project`"", '-nosplash', '-MSAuthMode=OfflineLocal', '-MSAuthBaseUrl=http://127.0.0.1:17980',
    '-Port=7787', "-UserDir=`"$userDir`"", "-WinX=$x", "-WinY=$y", "-ResX=$width", "-ResY=$height")

$windowApi = @'
using System;
using System.Runtime.InteropServices;
public static class PublicEditorWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
}
'@
Add-Type -TypeDefinition $windowApi
$process = Start-Process -FilePath $EditorExe -ArgumentList $arguments -WorkingDirectory $root -PassThru
$placed = $false
try {
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) { break }
        $handle = $process.MainWindowHandle
        if ($handle -eq [IntPtr]::Zero) { continue }
        $ok = [PublicEditorWindow]::SetWindowPos($handle, [IntPtr]::Zero, $x, $y, $width, $height, 0x0014)
        $rect = [PublicEditorWindow+Rect]::new()
        if ($ok -and [PublicEditorWindow]::GetWindowRect($handle, [ref]$rect) -and
            $rect.Left -ge $bounds.Left -and $rect.Top -ge $bounds.Top -and
            $rect.Right -le $bounds.Right -and $rect.Bottom -le $bounds.Bottom) {
            $placed = $true
            break
        }
    }
} finally {
    if (-not $placed -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    }
}
if (-not $placed) { throw '起動した公開Editorを選択画面内に配置・確認できず、起動したプロセスを終了しました。' }
[ordered]@{ status = 'launched_with_verified_display'; pid = $process.Id; display_monitor = $display.DeviceName;
    display_mode = $displayMode;
    user_dir = $userDir; game_port = 7787; registry_port = 17788; auth_port = 17980 } | ConvertTo-Json -Depth 3
