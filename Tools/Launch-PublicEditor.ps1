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
$screenBounds = $display.Bounds
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

$windowApi = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PublicEditorWindowPlacement {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct WindowPlacement {
        public uint Length, Flags, ShowCmd;
        public Point MinPosition, MaxPosition;
        public Rect NormalPosition;
    }
    [DllImport("user32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowPlacement(IntPtr handle, ref WindowPlacement placement);
    [DllImport("user32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetWindowPlacement(IntPtr handle, ref WindowPlacement placement);
    [DllImport("user32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern int GetWindowTextLength(IntPtr handle);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern int GetWindowText(IntPtr handle, StringBuilder text, int maxCount);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);
}
'@
if ($null -eq ('PublicEditorWindowPlacement' -as [type])) {
    Add-Type -TypeDefinition $windowApi
}
$placementLayout = [PublicEditorWindowPlacement+WindowPlacement]::new()
if ([Runtime.InteropServices.Marshal]::SizeOf($placementLayout) -ne 44) {
    throw 'WINDOWPLACEMENTのWin32構造体サイズが一致しません。'
}

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

$process = Start-Process -FilePath $EditorExe -ArgumentList $arguments -WorkingDirectory $root -PassThru
$placed = $false
Write-Host 'Unreal Editorの初回初期化を考慮し、最大10分間メインウィンドウを待ちます。'
try {
    $deadline = (Get-Date).AddSeconds(600)
    $nextWaitNotice = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if ((Get-Date) -ge $nextWaitNotice) {
            Write-Host 'Unreal Editorの起動処理を待機中です。'
            $nextWaitNotice = (Get-Date).AddSeconds(30)
        }
        $process.Refresh()
        if ($process.HasExited) { break }
        $handle = $process.MainWindowHandle
        if ($handle -eq [IntPtr]::Zero) { continue }

        # MainWindowHandle may point at the startup splash. Act only after this
        # handle belongs to the launched PID and its title identifies the editor project.
        $ownerPid = [uint32]0
        [void][PublicEditorWindowPlacement]::GetWindowThreadProcessId($handle, [ref]$ownerPid)
        if ($ownerPid -ne [uint32]$process.Id -or -not [PublicEditorWindowPlacement]::IsWindowVisible($handle)) { continue }
        $titleLength = [PublicEditorWindowPlacement]::GetWindowTextLength($handle)
        if ($titleLength -le 0) { continue }
        $titleBuffer = [System.Text.StringBuilder]::new($titleLength + 1)
        [void][PublicEditorWindowPlacement]::GetWindowText($handle, $titleBuffer, $titleBuffer.Capacity)
        $title = $titleBuffer.ToString()
        $projectName = [IO.Path]::GetFileNameWithoutExtension($project)
        if ($title.IndexOf($projectName, [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
            $title.IndexOf('Unreal Editor', [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }
        $initialRect = [PublicEditorWindowPlacement+Rect]::new()
        if (-not [PublicEditorWindowPlacement]::GetWindowRect($handle, [ref]$initialRect) -or
            ($initialRect.Right - $initialRect.Left) -lt 800 -or
            ($initialRect.Bottom - $initialRect.Top) -lt 500) { continue }

        $placement = [PublicEditorWindowPlacement+WindowPlacement]::new()
        $placement.Length = [Runtime.InteropServices.Marshal]::SizeOf($placement)
        if (-not [PublicEditorWindowPlacement]::GetWindowPlacement($handle, [ref]$placement)) { continue }
        # WINDOWPLACEMENT uses workspace coordinates; SetWindowPos uses screen
        # coordinates. Offset the screen work-area origin against monitor Bounds
        # so rcNormalPosition restores to the selected monitor's usable area.
        $workOffsetX = $bounds.X - $screenBounds.X
        $workOffsetY = $bounds.Y - $screenBounds.Y
        $normalRect = [PublicEditorWindowPlacement+Rect]::new()
        $normalRect.Left = $x - $workOffsetX
        $normalRect.Top = $y - $workOffsetY
        $normalRect.Right = $normalRect.Left + $width
        $normalRect.Bottom = $normalRect.Top + $height
        $placement.Flags = 0
        $placement.ShowCmd = 4 # SW_SHOWNOACTIVATE: restore normal without activating.
        $placement.NormalPosition = $normalRect
        if (-not [PublicEditorWindowPlacement]::SetWindowPlacement($handle, [ref]$placement)) { continue }

        # Reapply exact screen bounds without changing focus or z-order.
        $ok = [PublicEditorWindowPlacement]::SetWindowPos($handle, [IntPtr]::Zero, $x, $y, $width, $height, 0x0014)
        $rect = [PublicEditorWindowPlacement+Rect]::new()
        $actualPlacement = [PublicEditorWindowPlacement+WindowPlacement]::new()
        $actualPlacement.Length = [Runtime.InteropServices.Marshal]::SizeOf($actualPlacement)
        $placementVerified = [PublicEditorWindowPlacement]::GetWindowPlacement($handle, [ref]$actualPlacement)
        $normalScreenLeft = $actualPlacement.NormalPosition.Left + $workOffsetX
        $normalScreenTop = $actualPlacement.NormalPosition.Top + $workOffsetY
        $normalScreenRight = $actualPlacement.NormalPosition.Right + $workOffsetX
        $normalScreenBottom = $actualPlacement.NormalPosition.Bottom + $workOffsetY
        $normalPlacementInside = $placementVerified -and $actualPlacement.ShowCmd -eq 1 -and
            $normalScreenLeft -ge $bounds.Left -and $normalScreenTop -ge $bounds.Top -and
            $normalScreenRight -le $bounds.Right -and $normalScreenBottom -le $bounds.Bottom
        $normalPlacementMatchesTarget = [Math]::Abs($normalScreenLeft - $x) -le 2 -and
            [Math]::Abs($normalScreenTop - $y) -le 2 -and
            [Math]::Abs($normalScreenRight - ($x + $width)) -le 2 -and
            [Math]::Abs($normalScreenBottom - ($y + $height)) -le 2
        if ($ok -and $normalPlacementInside -and $normalPlacementMatchesTarget -and
            [PublicEditorWindowPlacement]::GetWindowRect($handle, [ref]$rect) -and
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
