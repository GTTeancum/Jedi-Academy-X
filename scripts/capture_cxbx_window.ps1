param(
    [string]$Repo = "C:\Programming\GitHub\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Programming\GitHub\Jedi-Academy-X\CXBXR",
    [string]$Game = "C:\Programming\GitHub\Star-Trek-Elite-Force-X\build\release",
    [string]$LoaderName = "cxbxr-ldr-project2.exe",
    [string]$Level = "borg1",
    [int]$WaitServerTime = 54000,
    [string]$WaitLogPattern = "",
    [int]$FastTimeMsec = 200,
    [switch]$NoBorg1SliceWarp,
    [switch]$NoSmokeInput,
    [switch]$SplitScreenCoop,
    [switch]$RenderProbe,
    [int]$WatchdogSeconds = 180,
    [int]$Count = 3,
    [int]$IntervalSeconds = 2,
    [string]$OutputPrefix = "",
    [switch]$AllowExternalRuntimeRoots
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CxbxWindowCaptureNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindowDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int width, int height, IntPtr hdcSrc, int xSrc, int ySrc, int rop);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
"@

function Get-RuntimeRoots {
    $roots = @($Game)
    if ($AllowExternalRuntimeRoots) {
        $emuPartition = Join-Path $Cxbx "EmuDisk\Partition1"
        foreach ($candidate in @($emuPartition, $Cxbx)) {
            if ($roots -notcontains $candidate) {
                $roots += $candidate
            }
        }
    }
    return $roots
}

function Remove-RuntimeFile([string]$Name) {
    foreach ($root in (Get-RuntimeRoots)) {
        Remove-Item -LiteralPath (Join-Path $root $Name) -Force -ErrorAction SilentlyContinue
    }
}

function Write-RuntimeFile([string]$Name, [string]$Text) {
    foreach ($root in (Get-RuntimeRoots)) {
        try {
            New-Item -ItemType Directory -Force -Path $root -ErrorAction Stop | Out-Null
            Set-Content -LiteralPath (Join-Path $root $Name) -Value $Text -Encoding ASCII -ErrorAction Stop
        } catch {
            Write-Verbose ("Could not write runtime file '{0}' under '{1}': {2}" -f $Name, $root, $_.Exception.Message)
        }
    }
}

function Get-LogText {
    $paths = @((Join-Path $Game "ef_sp_log.txt"))
    if ($AllowExternalRuntimeRoots) {
        $paths += @(
            (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_log.txt"),
            (Join-Path $Cxbx "ef_sp_log.txt")
        )
    }
    foreach ($path in $paths) {
        if (Test-Path $path) {
            return Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
        }
    }
    return ""
}

function Get-LastServerTime([string]$LogText) {
    $matches = [regex]::Matches($LogText, "serverTime=(\d+)")
    if ($matches.Count -eq 0) {
        return 0
    }
    return [int]$matches[$matches.Count - 1].Groups[1].Value
}

function Find-CxbxWindow([int[]]$ProcessIds) {
    $script:found = [IntPtr]::Zero
    $script:foundArea = 0
    [CxbxWindowCaptureNative+EnumWindowsProc]$callback = {
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (![CxbxWindowCaptureNative]::IsWindowVisible($hWnd)) {
            return $true
        }
        [uint32]$windowProcessId = 0
        [void][CxbxWindowCaptureNative]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessIds -notcontains [int]$windowProcessId) {
            return $true
        }
        $title = New-Object System.Text.StringBuilder 256
        [void][CxbxWindowCaptureNative]::GetWindowTextW($hWnd, $title, $title.Capacity)
        if ($title.ToString() -notmatch "Cxbx|Reloaded|project2|Star Trek|Elite Force") {
            return $true
        }
        $rect = New-Object CxbxWindowCaptureNative+RECT
        if (![CxbxWindowCaptureNative]::GetWindowRect($hWnd, [ref]$rect)) {
            return $true
        }
        $width = [Math]::Max(0, $rect.Right - $rect.Left)
        $height = [Math]::Max(0, $rect.Bottom - $rect.Top)
        $area = $width * $height
        if ($area -gt $script:foundArea) {
            $script:foundArea = $area
            $script:found = $hWnd
        }
        return $true
    }
    [void][CxbxWindowCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
    $result = $script:found
    $script:found = [IntPtr]::Zero
    $script:foundArea = 0
    return $result
}

function Test-BitmapSignal([System.Drawing.Bitmap]$Bitmap) {
    $nonBlack = 0
    # Ignore the title/menu band; PrintWindow can capture chrome while the D3D client is black.
    $topSkip = 0
    if ($Bitmap.Height -gt 120) {
        $topSkip = [Math]::Min($Bitmap.Height - 1, 48)
    }
    $sampleHeight = [Math]::Max(1, $Bitmap.Height - $topSkip)
    for ($y = 0; $y -lt 12; $y++) {
        $py = [Math]::Min($Bitmap.Height - 1, $topSkip + [int](($sampleHeight - 1) * $y / 11))
        for ($x = 0; $x -lt 16; $x++) {
            $px = [Math]::Min($Bitmap.Width - 1, [int](($Bitmap.Width - 1) * $x / 15))
            $c = $Bitmap.GetPixel($px, $py)
            if (($c.R + $c.G + $c.B) -gt 16) {
                $nonBlack++
            }
        }
    }
    return $nonBlack
}

function Capture-Window([IntPtr]$Hwnd, [string]$Path) {
    $rect = New-Object CxbxWindowCaptureNative+RECT
    if (![CxbxWindowCaptureNative]::GetWindowRect($Hwnd, [ref]$rect)) {
        throw "GetWindowRect failed"
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "CXBX window has invalid size ${width}x${height}"
    }

    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $gfx.GetHdc()
    $printed = [CxbxWindowCaptureNative]::PrintWindow($Hwnd, $hdc, 2)
    $gfx.ReleaseHdc($hdc)
    $gfx.Dispose()

    $signal = Test-BitmapSignal $bmp
    if (!$printed -or $signal -lt 4) {
        $bmp.Dispose()
        $bmp = New-Object System.Drawing.Bitmap $width, $height
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $dst = $gfx.GetHdc()
        $src = [CxbxWindowCaptureNative]::GetWindowDC($Hwnd)
        if ($src -eq [IntPtr]::Zero) {
            $gfx.ReleaseHdc($dst)
            $gfx.Dispose()
            $bmp.Dispose()
            throw "GetWindowDC failed"
        }
        [void][CxbxWindowCaptureNative]::BitBlt($dst, 0, 0, $width, $height, $src, 0, 0, 0x00CC0020)
        [void][CxbxWindowCaptureNative]::ReleaseDC($Hwnd, $src)
        $gfx.ReleaseHdc($dst)
        $gfx.Dispose()
        $signal = Test-BitmapSignal $bmp
        $method = "BitBlt"
    } else {
        $method = "PrintWindow"
    }

    if ($signal -lt 4) {
        $bmp.Dispose()
        $bmp = New-Object System.Drawing.Bitmap $width, $height
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size $width, $height))
        $gfx.Dispose()
        $signal = Test-BitmapSignal $bmp
        $method = "CopyFromScreen"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return "path=$Path method=$method signal=$signal size=${width}x${height}"
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $Repo "scripts\output"
if (!$OutputPrefix) {
    $OutputPrefix = Join-Path $outDir ("borg1_cxbx_window_current_$stamp")
}

$beforeIds = @(Get-Process cxbx* -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)

foreach ($name in @(
    "ef_sp_log.txt",
    "ef_sp_level.txt",
    "ef_sp_commands.txt",
    "ef_sp_postmap_commands.txt",
    "ef_sp_smoke_harness.txt",
    "ef_sp_active_commands.txt",
    "ef_sp_active_command_time.txt",
    "ef_sp_screenshot_request.txt",
    "ef_sp_cxbx_present_throttle.txt",
    "ef_sp_renderprobe.txt"
)) {
    Remove-RuntimeFile $name
}

Write-RuntimeFile "ef_sp_level.txt" ($Level + "`n")
Write-RuntimeFile "ef_sp_smoke_harness.txt" "1`n"
if ($RenderProbe) {
    Write-RuntimeFile "ef_sp_renderprobe.txt" "1`n"
}
$useBorg1SliceWarp = (!$NoBorg1SliceWarp -and $Level -ieq "borg1")
$splitCommand = ""
if ($SplitScreenCoop) {
    $splitCommand = "set stefx_splitScreen 1;set stefx_splitScreenPlayers 2;set stefx_splitScreenMode coop;set stefx_splitScreenP2Entity -1;"
}
if ($NoSmokeInput) {
    $smokeCommand = $splitCommand + "set s_xbox_smokeSilentAudio 1;set stefx_smoke_input 0;set stefx_smoke_aim 0;set stefx_smoke_wake_ai 0;set stefx_smoke_unlock_player 1;set stefx_smoke_ready_weapon 1;set stefx_smoke_stage_enemy 0"
    Write-RuntimeFile "ef_sp_commands.txt" ($smokeCommand + "`n")
    Write-RuntimeFile "ef_sp_postmap_commands.txt" ($smokeCommand + "`n")
} elseif ($useBorg1SliceWarp) {
    $sliceCommand = $splitCommand + "set s_xbox_smokeSilentAudio 1;set stefx_borg1_slice_warp 1;+attack"
    Write-RuntimeFile "ef_sp_commands.txt" ($sliceCommand + "`n")
    Write-RuntimeFile "ef_sp_postmap_commands.txt" ("set stefx_smoke_fasttime 1;set stefx_smoke_fasttime_msec $FastTimeMsec;set timescale 40;" + $sliceCommand + "`n")
} else {
    $smokeCommand = $splitCommand + "set s_xbox_smokeSilentAudio 1;set stefx_smoke_input 1;set stefx_smoke_aim 1;set stefx_smoke_wake_ai 1;set stefx_smoke_unlock_player 1;set stefx_smoke_ready_weapon 1;set stefx_smoke_stage_enemy 1;set stefx_smoke_input_forward 127;set stefx_smoke_input_side 0;set stefx_smoke_input_yaw 0;set stefx_smoke_input_start 6000;set stefx_smoke_input_attack_start 6500;set stefx_smoke_input_attack_end 22000;set stefx_smoke_input_end 36000"
    Write-RuntimeFile "ef_sp_commands.txt" ($smokeCommand + "`n")
    Write-RuntimeFile "ef_sp_postmap_commands.txt" ("set stefx_smoke_fasttime 1;set stefx_smoke_fasttime_msec $FastTimeMsec;set timescale 40;" + $smokeCommand + "`n")
}

$loader = Join-Path $Cxbx $LoaderName
$xbe = Join-Path $Game "default.xbe"
$stdoutPath = "$OutputPrefix.stdout.txt"
$stderrPath = "$OutputPrefix.stderr.txt"
Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath $loader -ArgumentList "/load `"$xbe`"" -WorkingDirectory $Cxbx -PassThru -WindowStyle Normal -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

$newIds = @($proc.Id)
$hwnd = [IntPtr]::Zero
for ($attempt = 0; $attempt -lt 80 -and $hwnd -eq [IntPtr]::Zero; $attempt++) {
    $after = @(Get-Process cxbx* -ErrorAction SilentlyContinue)
    $candidateIds = @($after | Where-Object { $beforeIds -notcontains $_.Id } | Select-Object -ExpandProperty Id)
    if ($candidateIds.Count -gt 0) {
        $newIds = $candidateIds
    }
    $hwnd = Find-CxbxWindow $newIds
    if ($hwnd -eq [IntPtr]::Zero) {
        if ($proc.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
}

$deadline = (Get-Date).AddSeconds($WatchdogSeconds)
$ready = $false
try {
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $log = Get-LogText
        $serverTime = Get-LastServerTime $log
        if ($WaitLogPattern -and $log -match $WaitLogPattern) {
            $ready = $true
            break
        }
        if (!$WaitLogPattern -and $serverTime -ge $WaitServerTime) {
            $ready = $true
            break
        }
        if ($proc.HasExited) {
            break
        }
    }

    if ($hwnd -eq [IntPtr]::Zero) {
        throw "No CXBX window found for process ids: $($newIds -join ',')"
    }

    $results = @(
        "ready=$ready",
        "serverTime=$(Get-LastServerTime (Get-LogText))",
        "waitLogPattern=$WaitLogPattern",
        "fastTimeMsec=$FastTimeMsec",
        "borg1SliceWarp=$useBorg1SliceWarp",
        "splitScreenCoop=$SplitScreenCoop",
        "noSmokeInput=$NoSmokeInput",
        "processIds=$($newIds -join ',')"
    )
    for ($i = 0; $i -lt $Count; $i++) {
        if ($i -gt 0) {
            Start-Sleep -Seconds $IntervalSeconds
        }
        $path = "{0}_{1:00}.png" -f $OutputPrefix, ($i + 1)
        $results += Capture-Window $hwnd $path
    }
    $summaryPath = "$OutputPrefix.summary.txt"
    $results | Set-Content -LiteralPath $summaryPath -Encoding ASCII
    $results
} finally {
    $after = @(Get-Process cxbx* -ErrorAction SilentlyContinue)
    foreach ($p in $after) {
        if ($beforeIds -notcontains $p.Id) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($name in @(
        "ef_sp_commands.txt",
        "ef_sp_postmap_commands.txt",
        "ef_sp_smoke_harness.txt",
        "ef_sp_active_commands.txt",
        "ef_sp_active_command_time.txt",
        "ef_sp_screenshot_request.txt",
        "ef_sp_cxbx_present_throttle.txt",
        "ef_sp_renderprobe.txt"
    )) {
        Remove-RuntimeFile $name
    }
}


