param(
    [string]$Game = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Games\Emulators\CXBX",
    [string]$Output = "scripts\output\ef_mp_renderer_capture.png",
    [int]$TimeoutSec = 20
)

$ErrorActionPreference = "Stop"

function Ensure-ParentDirectory([string]$Path) {
    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Remove-Quietly([string[]]$Paths) {
    foreach ($path in $Paths) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
}

function Restore-TextFiles([hashtable]$Files) {
    foreach ($path in $Files.Keys) {
        try {
            Ensure-ParentDirectory $path
            Set-Content -LiteralPath $path -Value $Files[$path] -Encoding ASCII
        } catch {
        }
    }
}

function Get-LogPath {
    $candidates = @(
        (Join-Path $Game "ef_mp_log.txt"),
        (Join-Path $Game "BaseEF\ef_mp_log.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_log.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_log.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_log.txt"),
        (Join-Path $Cxbx "ef_mp_log.txt")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    return $candidates[0]
}

function Get-BitmapSignal([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $image = New-Object System.Drawing.Bitmap $Path
    try {
        $visibleSamples = 0
        $maxBrightness = 0
        for ($sy = 0; $sy -lt 12; $sy++) {
            $y = [int]([Math]::Round((($image.Height - 1) * $sy) / 11.0))
            for ($sx = 0; $sx -lt 16; $sx++) {
                $x = [int]([Math]::Round((($image.Width - 1) * $sx) / 15.0))
                $pixel = $image.GetPixel($x, $y)
                $brightness = [int]$pixel.R + [int]$pixel.G + [int]$pixel.B
                if ($brightness -gt $maxBrightness) {
                    $maxBrightness = $brightness
                }
                if ($brightness -gt 8) {
                    $visibleSamples++
                }
            }
        }

        return @{
            Width = $image.Width
            Height = $image.Height
            VisibleSamples = $visibleSamples
            MaxBrightness = $maxBrightness
        }
    } finally {
        $image.Dispose()
    }
}

function Save-LogEncodedScreenshot([string]$Path) {
    $logPath = Get-LogPath
    if (!(Test-Path -LiteralPath $logPath -PathType Leaf)) {
        return $null
    }

    $lines = Get-Content -LiteralPath $logPath -ErrorAction SilentlyContinue
    $beginIndex = -1
    $width = 0
    $height = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "STEFX: renderer screenshot log begin .* out=(\d+)x(\d+)") {
            $beginIndex = $i
            $width = [int]$Matches[1]
            $height = [int]$Matches[2]
        }
    }

    if ($beginIndex -lt 0 -or $width -le 0 -or $height -le 0) {
        return $null
    }

    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $chunks = 0
    $sawEnd = $false
    try {
        for ($i = $beginIndex + 1; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            if ($line -match "STEFX: renderer screenshot log end") {
                $sawEnd = $true
                break
            }
            if ($line -notmatch "STEFX: renderer screenshot log chunk row=(\d+) x=(\d+) pixels=(\d+) data=([0-9a-fA-F]+)") {
                continue
            }

            $row = [int]$Matches[1]
            $x = [int]$Matches[2]
            $pixels = [int]$Matches[3]
            $data = $Matches[4]
            if ($row -lt 0 -or $row -ge $height -or $x -lt 0 -or $x -ge $width) {
                continue
            }

            for ($n = 0; $n -lt $pixels; $n++) {
                $offset = $n * 6
                if ($offset + 6 -gt $data.Length -or $x + $n -ge $width) {
                    break
                }
                $r = [Convert]::ToInt32($data.Substring($offset, 2), 16)
                $g = [Convert]::ToInt32($data.Substring($offset + 2, 2), 16)
                $b = [Convert]::ToInt32($data.Substring($offset + 4, 2), 16)
                $bmp.SetPixel($x + $n, $row, [System.Drawing.Color]::FromArgb($r, $g, $b))
            }
            $chunks++
        }

        if (!$sawEnd -or $chunks -eq 0) {
            return $null
        }

        Ensure-ParentDirectory $Path

        $outBmp = New-Object System.Drawing.Bitmap 640, 480
        $gfx = [System.Drawing.Graphics]::FromImage($outBmp)
        try {
            $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
            $gfx.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
            $gfx.DrawImage($bmp, 0, 0, 640, 480)

            $ext = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
            $format = [System.Drawing.Imaging.ImageFormat]::Png
            if ($ext -eq ".jpg" -or $ext -eq ".jpeg") {
                $format = [System.Drawing.Imaging.ImageFormat]::Jpeg
            } elseif ($ext -eq ".bmp") {
                $format = [System.Drawing.Imaging.ImageFormat]::Bmp
            }
            $outBmp.Save($Path, $format)
        } finally {
            $gfx.Dispose()
            $outBmp.Dispose()
        }

        return @{
            Log = $logPath
            Output = (Resolve-Path $Path).Path
            Width = $width
            Height = $height
            Chunks = $chunks
        }
    } finally {
        $bmp.Dispose()
    }
}

$requestPaths = @(
    (Join-Path $Game "ef_mp_screenshot_request.txt"),
    (Join-Path $Game "BaseEF\ef_mp_screenshot_request.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_screenshot_request.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_screenshot_request.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_screenshot_request.txt"),
    (Join-Path $Cxbx "ef_mp_screenshot_request.txt")
)

$preopenPaths = @(
    (Join-Path $Game "ef_mp_screenshot_preopen.txt"),
    (Join-Path $Game "BaseEF\ef_mp_screenshot_preopen.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_screenshot_preopen.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_screenshot_preopen.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_screenshot_preopen.txt"),
    (Join-Path $Cxbx "ef_mp_screenshot_preopen.txt")
)

$bmpCandidates = @(
    (Join-Path $Game "ef_mp_backbuffer.bmp"),
    (Join-Path $Game "BaseEF\ef_mp_backbuffer.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_backbuffer.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_backbuffer.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_backbuffer.bmp"),
    (Join-Path $Cxbx "ef_mp_backbuffer.bmp"),
    (Join-Path $Game "ef_mp_xgshot.bmp"),
    (Join-Path $Game "BaseEF\ef_mp_xgshot.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_xgshot.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_xgshot.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_xgshot.bmp"),
    (Join-Path $Cxbx "ef_mp_xgshot.bmp")
)

$throttlePaths = @(
    (Join-Path $Game "ef_mp_cxbx_present_throttle.txt"),
    (Join-Path $Game "BaseEF\ef_mp_cxbx_present_throttle.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_mp_cxbx_present_throttle.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition2\ef_mp_cxbx_present_throttle.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition3\ef_mp_cxbx_present_throttle.txt"),
    (Join-Path $Cxbx "ef_mp_cxbx_present_throttle.txt")
)

$restoreThrottle = @{}
foreach ($throttlePath in $throttlePaths) {
    if (Test-Path -LiteralPath $throttlePath) {
        try {
            $restoreThrottle[$throttlePath] = Get-Content -LiteralPath $throttlePath -Raw -ErrorAction Stop
        } catch {
            $restoreThrottle[$throttlePath] = "1"
        }
    }
}

Remove-Quietly $throttlePaths
Remove-Quietly $bmpCandidates

$requestStartedUtc = [DateTime]::UtcNow
$requestWriteCount = 0
foreach ($preopenPath in $preopenPaths) {
    try {
        Ensure-ParentDirectory $preopenPath
        Set-Content -LiteralPath $preopenPath -Value "preopen" -Encoding ASCII
    } catch {
    }
}

foreach ($requestPath in $requestPaths) {
    try {
        Ensure-ParentDirectory $requestPath
        Set-Content -LiteralPath $requestPath -Value "capture" -Encoding ASCII
        $requestWriteCount++
    } catch {
    }
}

if ($requestWriteCount -eq 0) {
    Restore-TextFiles $restoreThrottle
    throw "Could not write any MP renderer capture request file."
}

Write-Host "REQUEST_WRITTEN count=$requestWriteCount paths='$($requestPaths -join ';')'"
if ($restoreThrottle.Count -gt 0) {
    Write-Host "PRESENT_THROTTLE_TEMPORARILY_REMOVED count=$($restoreThrottle.Count)"
}

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$bmpPath = $null
while ((Get-Date) -lt $deadline) {
    foreach ($candidate in $bmpCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            $item = Get-Item -LiteralPath $candidate
            if ($item.Length -gt 54 -and $item.LastWriteTimeUtc -ge $requestStartedUtc.AddSeconds(-1)) {
                $bmpPath = $candidate
                break
            }
        }
    }

    if ($bmpPath) {
        break
    }

    Start-Sleep -Milliseconds 250
}

if (!$bmpPath) {
    $logCapture = Save-LogEncodedScreenshot $Output
    Remove-Quietly ($requestPaths + $preopenPaths)
    Restore-TextFiles $restoreThrottle
    if ($logCapture) {
        $signal = Get-BitmapSignal $logCapture.Output
        Write-Host "CAPTURE_LOG_OK log='$($logCapture.Log)' output='$($logCapture.Output)' source=$($logCapture.Width)x$($logCapture.Height) chunks=$($logCapture.Chunks)"
        Write-Host "CAPTURE_ANALYSIS visibleSamples=$($signal.VisibleSamples) maxBrightness=$($signal.MaxBrightness) size=$($signal.Width)x$($signal.Height)"
        if ($signal.VisibleSamples -lt 8 -and $signal.MaxBrightness -lt 96) {
            throw "Renderer log capture appears blank. visibleSamples=$($signal.VisibleSamples) maxBrightness=$($signal.MaxBrightness) output='$($logCapture.Output)'"
        }
        exit 0
    }
    throw "Renderer capture timed out. candidates='$($bmpCandidates -join ';')'"
}

Ensure-ParentDirectory $Output

$outputExt = [System.IO.Path]::GetExtension($Output).ToLowerInvariant()
if ($outputExt -eq ".bmp") {
    Copy-Item -LiteralPath $bmpPath -Destination $Output -Force
    $outputPath = (Resolve-Path $Output).Path
} else {
    Add-Type -AssemblyName System.Drawing
    $image = New-Object System.Drawing.Bitmap $bmpPath
    try {
        $format = [System.Drawing.Imaging.ImageFormat]::Png
        if ($outputExt -eq ".jpg" -or $outputExt -eq ".jpeg") {
            $format = [System.Drawing.Imaging.ImageFormat]::Jpeg
        }
        $image.Save($Output, $format)
    } finally {
        $image.Dispose()
    }
    $outputPath = (Resolve-Path $Output).Path
}

Add-Type -AssemblyName System.Drawing
$analysisImage = New-Object System.Drawing.Bitmap $bmpPath
try {
    $visibleSamples = 0
    $maxBrightness = 0
    for ($sy = 0; $sy -lt 12; $sy++) {
        $y = [int]([Math]::Round((($analysisImage.Height - 1) * $sy) / 11.0))
        for ($sx = 0; $sx -lt 16; $sx++) {
            $x = [int]([Math]::Round((($analysisImage.Width - 1) * $sx) / 15.0))
            $pixel = $analysisImage.GetPixel($x, $y)
            $brightness = [int]$pixel.R + [int]$pixel.G + [int]$pixel.B
            if ($brightness -gt $maxBrightness) {
                $maxBrightness = $brightness
            }
            if ($brightness -gt 8) {
                $visibleSamples++
            }
        }
    }
} finally {
    $analysisImage.Dispose()
}

Remove-Quietly ($requestPaths + $preopenPaths)
Restore-TextFiles $restoreThrottle

$bmpItem = Get-Item -LiteralPath $bmpPath
Write-Host "CAPTURE_OK source='$bmpPath' output='$outputPath' bytes=$($bmpItem.Length)"
Write-Host "CAPTURE_ANALYSIS visibleSamples=$visibleSamples maxBrightness=$maxBrightness"

if ($visibleSamples -lt 8 -and $maxBrightness -lt 96) {
    $logCapture = Save-LogEncodedScreenshot $Output
    if ($logCapture) {
        $signal = Get-BitmapSignal $logCapture.Output
        Write-Host "CAPTURE_LOG_OK log='$($logCapture.Log)' output='$($logCapture.Output)' source=$($logCapture.Width)x$($logCapture.Height) chunks=$($logCapture.Chunks)"
        Write-Host "CAPTURE_LOG_ANALYSIS visibleSamples=$($signal.VisibleSamples) maxBrightness=$($signal.MaxBrightness) size=$($signal.Width)x$($signal.Height)"
        if ($signal.VisibleSamples -ge 8 -or $signal.MaxBrightness -ge 96) {
            exit 0
        }
    }
    throw "Renderer capture appears blank. visibleSamples=$visibleSamples maxBrightness=$maxBrightness output='$outputPath'"
}
