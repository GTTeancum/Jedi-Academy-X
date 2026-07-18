param(
    [string]$Game = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Games\Emulators\CXBX",
    [string]$Output = "scripts\output\holomatch_window_capture.png",
    [int]$WatchdogSeconds = 120,
    [ValidateSet("PrintWindow", "Screen")]
    [string]$CaptureMode = "PrintWindow",
    [switch]$Visible,
    [switch]$BringToFront
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-CxbxProcesses([string]$Root) {
    $fullRoot = Resolve-FullPath $Root
    @(Get-Process cxbx* -ErrorAction SilentlyContinue | Where-Object {
        try {
            $_.Path -and (Resolve-FullPath $_.Path).StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
        } catch {
            $false
        }
    })
}

function Get-ImageSignal([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $colors = New-Object 'System.Collections.Generic.HashSet[string]'
        $maxBrightness = 0
        for ($yi = 0; $yi -le 16; $yi++) {
            $y = [Math]::Min($bitmap.Height - 1, [int][Math]::Round(($bitmap.Height - 1) * $yi / 16))
            for ($xi = 0; $xi -le 16; $xi++) {
                $x = [Math]::Min($bitmap.Width - 1, [int][Math]::Round(($bitmap.Width - 1) * $xi / 16))
                $c = $bitmap.GetPixel($x, $y)
                [void]$colors.Add(('{0:X2}{1:X2}{2:X2}{3:X2}' -f $c.A, $c.R, $c.G, $c.B))
                $brightness = [Math]::Max($c.R, [Math]::Max($c.G, $c.B))
                if ($brightness -gt $maxBrightness) {
                    $maxBrightness = $brightness
                }
            }
        }
        [PSCustomObject]@{
            Width = $bitmap.Width
            Height = $bitmap.Height
            UniqueSampledColors = $colors.Count
            MaxBrightness = $maxBrightness
        }
    } finally {
        $bitmap.Dispose()
    }
}

$repoRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..")
if (![System.IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $repoRoot $Output
}
$Output = Resolve-FullPath $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$gameRoot = Resolve-FullPath $Game
$cxbxRoot = Resolve-FullPath $Cxbx
$xbe = Join-Path $gameRoot "efmp.xbe"
$loader = Join-Path $cxbxRoot "cxbxr-ldr.exe"
$logPath = Join-Path $gameRoot "ef_mp_log.txt"
$phasePath = Join-Path $gameRoot "ef_mp_phase.txt"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$stdoutPath = Join-Path (Split-Path -Parent $Output) "capture_cxbx_window_$stamp.stdout.txt"
$stderrPath = Join-Path (Split-Path -Parent $Output) "capture_cxbx_window_$stamp.stderr.txt"

if (!(Test-Path -LiteralPath $xbe -PathType Leaf)) {
    throw "Missing XBE: $xbe"
}
if (!(Test-Path -LiteralPath $loader -PathType Leaf)) {
    throw "Missing CXBX-R loader: $loader"
}

$existing = Get-CxbxProcesses $cxbxRoot
if ($existing.Count -gt 0) {
    $details = ($existing | ForEach-Object { "$($_.Id):$($_.ProcessName)" }) -join ", "
    throw "CXBX-R is already running in $cxbxRoot; refusing to close it. Processes: $details"
}

Remove-Item -LiteralPath $logPath, $phasePath, $Output -Force -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class CxbxWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_SHOWWINDOW = 0x0040;

    public static IntPtr[] GetTopLevelWindowsForPids(int[] pids) {
        HashSet<int> wanted = new HashSet<int>(pids);
        List<IntPtr> windows = new List<IntPtr>();
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (wanted.Contains((int)pid)) {
                windows.Add(hWnd);
            }
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }

    public static string GetTitle(IntPtr hWnd) {
        int length = GetWindowTextLength(hWnd);
        System.Text.StringBuilder builder = new System.Text.StringBuilder(Math.Max(1, length + 1));
        GetWindowText(hWnd, builder, builder.Capacity);
        return builder.ToString();
    }

    public static int GetProcessId(IntPtr hWnd) {
        uint pid;
        GetWindowThreadProcessId(hWnd, out pid);
        return (int)pid;
    }
}
"@

$windowStyle = if ($Visible) { "Normal" } else { "Hidden" }
$args = "/load `"$xbe`""
$process = Start-Process `
    -FilePath $loader `
    -ArgumentList $args `
    -WorkingDirectory $cxbxRoot `
    -PassThru `
    -WindowStyle $windowStyle `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath

$patterns = @(
    "STEFX_HM: direct Holomatch startup bypasses menus",
    "STEFX_HM: EF SP interface HUD draw active",
    "STEFX_HM: score update client="
)
$seen = @{}
foreach ($pattern in $patterns) {
    $seen[$pattern] = $false
}

try {
    $deadline = (Get-Date).AddSeconds($WatchdogSeconds)
    do {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $logPath -PathType Leaf) {
            $text = Get-Content -LiteralPath $logPath -Raw -ErrorAction SilentlyContinue
            foreach ($pattern in $patterns) {
                if ($text -like "*$pattern*") {
                    $seen[$pattern] = $true
                }
            }
            if ((@($seen.Values | Where-Object { -not $_ })).Count -eq 0) {
                break
            }
        }
        if ((Get-CxbxProcesses $cxbxRoot).Count -eq 0) {
            throw "CXBX-R exited before capture markers were reached"
        }
    } while ((Get-Date) -lt $deadline)

    $missing = @($seen.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
    if ($missing.Count -gt 0) {
        throw "Timed out waiting for capture marker(s): $($missing -join ', ')"
    }

    $ownedProcesses = @(Get-CxbxProcesses $cxbxRoot)
    $ownedIds = [int[]]($ownedProcesses | Select-Object -ExpandProperty Id)
    $windowHandles = @()
    if ($ownedIds.Count -gt 0) {
        $windowHandles = @([CxbxWindowCapture]::GetTopLevelWindowsForPids($ownedIds))
    }
    $windowHandle = $windowHandles |
        Where-Object { [CxbxWindowCapture]::IsWindowVisible($_) -and [CxbxWindowCapture]::GetTitle($_) } |
        Select-Object -First 1
    if (!$windowHandle) {
        $windowHandle = $windowHandles | Select-Object -First 1
    }
    if (!$windowHandle) {
        $processDetails = ($ownedProcesses | ForEach-Object { "$($_.Id):$($_.ProcessName):main=0x$('{0:x}' -f $_.MainWindowHandle)" }) -join ", "
        throw "No CXBX-R top-level window handle found. Processes: $processDetails"
    }

    if ($BringToFront) {
        [CxbxWindowCapture]::ShowWindow($windowHandle, 5) | Out-Null
        [CxbxWindowCapture]::SetForegroundWindow($windowHandle) | Out-Null
        Start-Sleep -Milliseconds 250
    }

    $rect = New-Object CxbxWindowCapture+RECT
    [CxbxWindowCapture]::GetWindowRect($windowHandle, [ref]$rect) | Out-Null
    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    if ($CaptureMode -eq "Screen" -and !$BringToFront) {
        [CxbxWindowCapture]::ShowWindow($windowHandle, 5) | Out-Null
        [CxbxWindowCapture]::SetWindowPos(
            $windowHandle,
            [CxbxWindowCapture]::HWND_TOPMOST,
            0,
            0,
            0,
            0,
            [CxbxWindowCapture]::SWP_NOMOVE -bor [CxbxWindowCapture]::SWP_NOSIZE -bor [CxbxWindowCapture]::SWP_SHOWWINDOW
        ) | Out-Null
        [CxbxWindowCapture]::SetForegroundWindow($windowHandle) | Out-Null
        Start-Sleep -Milliseconds 1000
    }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        if ($CaptureMode -eq "Screen") {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, [System.Drawing.Size]::new($width, $height))
            $printOk = $true
        } else {
            $hdc = $graphics.GetHdc()
            try {
                $printOk = [CxbxWindowCapture]::PrintWindow($windowHandle, $hdc, 2)
            } finally {
                $graphics.ReleaseHdc($hdc)
            }
        }
        $bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        if ($CaptureMode -eq "Screen") {
            [CxbxWindowCapture]::SetWindowPos(
                $windowHandle,
                [CxbxWindowCapture]::HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                [CxbxWindowCapture]::SWP_NOMOVE -bor [CxbxWindowCapture]::SWP_NOSIZE
            ) | Out-Null
        }
    }

    $signal = Get-ImageSignal $Output
    [PSCustomObject]@{
        Output = $Output
        Width = $signal.Width
        Height = $signal.Height
        CaptureMode = $CaptureMode
        PrintWindowOk = $printOk
        UniqueSampledColors = $signal.UniqueSampledColors
        MaxBrightness = $signal.MaxBrightness
        WindowTitle = [CxbxWindowCapture]::GetTitle($windowHandle)
        WindowPid = [CxbxWindowCapture]::GetProcessId($windowHandle)
        WindowHandle = ("0x{0:x}" -f $windowHandle.ToInt64())
        Log = $logPath
        Stdout = $stdoutPath
        Stderr = $stderrPath
        SeenDirectBoot = $seen[$patterns[0]]
        SeenHud = $seen[$patterns[1]]
        SeenScore = $seen[$patterns[2]]
    }

    if (!$printOk -or $signal.UniqueSampledColors -le 1 -or $signal.MaxBrightness -eq 0) {
        exit 88
    }
} finally {
    Get-CxbxProcesses $cxbxRoot | Stop-Process -Force -ErrorAction SilentlyContinue
}
