param(
    [string]$Repo = "C:\Programming\GitHub\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Games\Emulators\CXBX",
    [string]$Game = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X",
    [string]$LoaderName = "cxbxr-ldr.exe",
    [int]$BootWaitSeconds = 8,
    [int]$AfterInputMilliseconds = 900,
    [int]$WindowWidth = 0,
    [int]$WindowHeight = 0,
    [string]$StartupCommand = "",
    [string]$StartupScreenName = "direct",
    [string]$OutputPrefix = ""
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class EfMenuCaptureNative {
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
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindowDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int width, int height, IntPtr hdcSrc, int xSrc, int ySrc, int rop);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }
}
"@

function Find-CxbxWindow([int[]]$ProcessIds) {
    $script:found = [IntPtr]::Zero
    $script:foundArea = 0
    [EfMenuCaptureNative+EnumWindowsProc]$callback = {
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (![EfMenuCaptureNative]::IsWindowVisible($hWnd)) {
            return $true
        }
        [uint32]$windowProcessId = 0
        [void][EfMenuCaptureNative]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessIds -notcontains [int]$windowProcessId) {
            return $true
        }
        $title = New-Object System.Text.StringBuilder 256
        [void][EfMenuCaptureNative]::GetWindowTextW($hWnd, $title, $title.Capacity)
        if ($title.ToString() -notmatch "Cxbx|Reloaded|Star Trek|Elite Force") {
            return $true
        }
        $rect = New-Object EfMenuCaptureNative+RECT
        if (![EfMenuCaptureNative]::GetWindowRect($hWnd, [ref]$rect)) {
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
    [void][EfMenuCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
    $result = $script:found
    $script:found = [IntPtr]::Zero
    $script:foundArea = 0
    return $result
}

function Capture-Window([IntPtr]$Hwnd, [string]$Path) {
    [void][EfMenuCaptureNative]::ShowWindow($Hwnd, 5)
    [void][EfMenuCaptureNative]::SetWindowPos($Hwnd, [IntPtr]::new(-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0040)
    [void][EfMenuCaptureNative]::SetForegroundWindow($Hwnd)
    Start-Sleep -Milliseconds 200

    $client = New-Object EfMenuCaptureNative+RECT
    if (![EfMenuCaptureNative]::GetClientRect($Hwnd, [ref]$client)) {
        throw "GetClientRect failed"
    }
    $origin = New-Object EfMenuCaptureNative+POINT
    $origin.X = 0
    $origin.Y = 0
    if (![EfMenuCaptureNative]::ClientToScreen($Hwnd, [ref]$origin)) {
        throw "ClientToScreen failed"
    }
    $width = $client.Right - $client.Left
    $height = $client.Bottom - $client.Top
    if ($width -le 0 -or $height -le 0) {
        throw "CXBX client has invalid size ${width}x${height}"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $method = "CopyFromScreen"
    try {
        $gfx.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size $width, $height))
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $gfx.Dispose()
        [void][EfMenuCaptureNative]::SetWindowPos($Hwnd, [IntPtr]::new(-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0040)
        $bmp.Dispose()
    }
    return "captured path=$Path method=$method size=${width}x${height}"
}

function Send-MenuKeys([IntPtr]$Hwnd, [string[]]$Keys) {
    [void][EfMenuCaptureNative]::SetForegroundWindow($Hwnd)
    Start-Sleep -Milliseconds 150
    foreach ($key in $Keys) {
        $vk = switch ($key) {
            "{ENTER}" { 0x0D }
            "{ESC}" { 0x1B }
            "{UP}" { 0x26 }
            "{DOWN}" { 0x28 }
            "{LEFT}" { 0x25 }
            "{RIGHT}" { 0x27 }
            default { 0 }
        }
        if ($vk -ne 0) {
            [EfMenuCaptureNative]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 60
            [EfMenuCaptureNative]::keybd_event([byte]$vk, 0, 0x0002, [UIntPtr]::Zero)
        } else {
            [System.Windows.Forms.SendKeys]::SendWait($key)
        }
        Start-Sleep -Milliseconds $AfterInputMilliseconds
    }
}

foreach ($name in @(
    "ef_sp_level.txt",
    "ef_sp_commands.txt",
    "ef_sp_postmap_commands.txt",
    "ef_sp_smoke_harness.txt",
    "ef_sp_screenshot_request.txt"
)) {
    Remove-Item -LiteralPath (Join-Path $Game $name) -Force -ErrorAction SilentlyContinue
}

if ($StartupCommand) {
    Set-Content -LiteralPath (Join-Path $Game "ef_sp_commands.txt") -Value ($StartupCommand + "`n") -Encoding ASCII
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
if (!$OutputPrefix) {
    $OutputPrefix = Join-Path $Repo "scripts\output\ef_ps2_menu_runtime_$stamp"
}

$loader = Join-Path $Cxbx $LoaderName
$xbe = Join-Path $Game "default.xbe"
$beforeIds = @(Get-Process cxbx* -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$proc = Start-Process -FilePath $loader -ArgumentList "/load `"$xbe`"" -WorkingDirectory $Cxbx -PassThru -WindowStyle Normal

$newIds = @($proc.Id)
$hwnd = [IntPtr]::Zero
for ($attempt = 0; $attempt -lt 120 -and $hwnd -eq [IntPtr]::Zero; $attempt++) {
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

try {
    if ($hwnd -eq [IntPtr]::Zero) {
        throw "No CXBX window found for process ids: $($newIds -join ',')"
    }

    if ($WindowWidth -gt 0 -and $WindowHeight -gt 0) {
        [void][EfMenuCaptureNative]::ShowWindow($hwnd, 5)
        [void][EfMenuCaptureNative]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, $WindowWidth, $WindowHeight, 0x0040)
        Start-Sleep -Milliseconds 500
    }

    Start-Sleep -Seconds $BootWaitSeconds
    $results = @("processIds=$($newIds -join ',')")

    if ($StartupCommand) {
        $screens = @(
            @{ Name = $StartupScreenName; Keys = @() }
        )
    } else {
        $screens = @(
            @{ Name = "main";       Keys = @() },
            @{ Name = "newgame";    Keys = @("{ENTER}") },
            @{ Name = "main_after_newgame"; Keys = @("{ESC}") },
            @{ Name = "loadgame";   Keys = @("{DOWN}", "{ENTER}") },
            @{ Name = "main_after_loadgame"; Keys = @("{ESC}") },
            @{ Name = "configure";  Keys = @("{DOWN}", "{DOWN}", "{DOWN}", "{ENTER}") },
            @{ Name = "audio";      Keys = @("{ENTER}") },
            @{ Name = "configure_after_audio"; Keys = @("{ESC}") },
            @{ Name = "video";      Keys = @("{DOWN}", "{ENTER}") },
            @{ Name = "configure_after_video"; Keys = @("{ESC}") },
            @{ Name = "controller"; Keys = @("{DOWN}", "{DOWN}", "{ENTER}") }
        )
    }

    foreach ($screen in $screens) {
        Send-MenuKeys $hwnd $screen.Keys
        $path = "${OutputPrefix}_$($screen.Name).png"
        $results += Capture-Window $hwnd $path
    }

    $summaryPath = "${OutputPrefix}.summary.txt"
    $results | Set-Content -LiteralPath $summaryPath -Encoding ASCII
    $results
} finally {
    $after = @(Get-Process cxbx* -ErrorAction SilentlyContinue)
    foreach ($p in $after) {
        if ($beforeIds -notcontains $p.Id) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
