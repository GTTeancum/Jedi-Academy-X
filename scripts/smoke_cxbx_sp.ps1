param(
    [string]$Repo = "C:\Programming\GitHub\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Programming\GitHub\Jedi-Academy-X\CXBXR",
    [string]$Game = "C:\Programming\GitHub\Star-Trek-Elite-Force-X\build\release",
    [string]$Junction = "C:\Programming\GitHub\Star-Trek-Elite-Force-X\build\release",
    [string]$LoaderName = "cxbxr-ldr-project2.exe",
    [string]$Level = "",
    [string[]]$StartupCommand = @(),
    [string[]]$PostMapCommand = @(),
    [int]$WatchdogSeconds = 300,
    [int]$ActiveSeconds = 0,
    [int]$InitialQuietGraceSeconds = 180,
    [int]$QuietGraceSeconds = 20,
    [string]$ScreenshotPath = "",
    [int]$ScreenshotAfterActiveSeconds = 4,
    [int]$ScreenshotAfterServerTime = 72000,
    [int]$ScreenshotCount = 3,
    [int]$ScreenshotRandomWindowSeconds = 24,
    [string]$ScreenshotAfterLogPattern = "",
    [switch]$NoSmokeInput,
    [int]$SmokeInputStart = 71000,
    [int]$SmokeInputEnd = 112000,
    [int]$SmokeInputForward = 127,
    [int]$SmokeInputSide = 0,
    [int]$SmokeInputYaw = 0,
    [int]$SmokeInputAttackStart = 76000,
    [int]$SmokeInputAttackEnd = 100000,
    [switch]$RequireVerticalSlice,
    [switch]$AllowNoActive,
    [switch]$NoCopy,
    [switch]$Visible,
    [switch]$CxbxPresentThrottle
)

$ErrorActionPreference = "Stop"

function Get-LogPath {
    $candidates = @(
        (Join-Path $Game "ef_sp_log.txt"),
        (Join-Path $Junction "ef_sp_log.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_log.txt")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $candidates[0]
}

function Count-Matches([string]$Path, [string]$Pattern) {
    if (!(Test-Path $Path)) {
        return 0
    }

    return @((Select-String -Path $Path -Pattern $Pattern -ErrorAction SilentlyContinue)).Count
}

function Count-MatchesCaseSensitive([string]$Path, [string]$Pattern) {
    if (!(Test-Path $Path)) {
        return 0
    }

    return @((Select-String -Path $Path -Pattern $Pattern -CaseSensitive -ErrorAction SilentlyContinue)).Count
}

function Has-Match([string]$Path, [string]$Pattern) {
    if (!(Test-Path $Path)) {
        return $false
    }

    return [bool](Select-String -Path $Path -Pattern $Pattern -Quiet -ErrorAction SilentlyContinue)
}

function Get-NewestCompiledSourceInput {
    $sourceRoots = @(
        (Join-Path $Repo "code"),
        (Join-Path $Repo "SP-Mod-Source-Code-master")
    ) | Where-Object { Test-Path $_ }

    if (!$sourceRoots -or $sourceRoots.Count -eq 0) {
        return $null
    }

    $sourceExtensions = @(".c", ".cpp", ".h", ".hpp", ".asm", ".vcproj", ".sln")
    $excludedPathParts = @(
        "\Release\",
        "\Debug\",
        "\FinalBuild\",
        "\DemoDebug\",
        "\DemoRelease\",
        "\DemoFinal\",
        "\SHDebug\",
        "\build\",
        "\__pycache__\"
    )

    return Get-ChildItem -LiteralPath $sourceRoots -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $extension = $_.Extension.ToLowerInvariant()
            if ($sourceExtensions -notcontains $extension) {
                return $false
            }

            $fullName = $_.FullName
            foreach ($excludedPathPart in $excludedPathParts) {
                if ($fullName.IndexOf($excludedPathPart, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    return $false
                }
            }

            return $true
        } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Get-DebugFileCandidates {
    @(
        (Join-Path $Game "CxbxDebug.txt"),
        (Join-Path $Game "KrnlDebug.txt"),
        (Join-Path $Repo "build\release\CxbxDebug.txt"),
        (Join-Path $Repo "build\release\KrnlDebug.txt"),
        (Join-Path $Cxbx "CxbxDebug.txt"),
        (Join-Path $Cxbx "KrnlDebug.txt")
    )
}

function Copy-DebugFiles([string]$OutputDirectory, [string]$Stamp) {
    $copied = @()
    foreach ($candidate in Get-DebugFileCandidates) {
        if (Test-Path $candidate) {
            $leaf = Split-Path $candidate -Leaf
            $dest = Join-Path $OutputDirectory ("cxbx_sp_${Stamp}.${leaf}")
            Copy-Item $candidate $dest -Force
            $copied += $dest
        }
    }
    return $copied
}

function Test-BitmapEffectivelyBlank($Bitmap) {
    if (!$Bitmap -or $Bitmap.Width -le 0 -or $Bitmap.Height -le 0) {
        return $true
    }

    $sampleCount = 0
    $signalCount = 0
    $stepX = [Math]::Max(1, [int]($Bitmap.Width / 32))
    $stepY = [Math]::Max(1, [int]($Bitmap.Height / 24))
    for ($y = 0; $y -lt $Bitmap.Height; $y += $stepY) {
        for ($x = 0; $x -lt $Bitmap.Width; $x += $stepX) {
            $pixel = $Bitmap.GetPixel($x, $y)
            $luma = (0.2126 * $pixel.R) + (0.7152 * $pixel.G) + (0.0722 * $pixel.B)
            $sampleCount++
            if ($luma -gt 2) {
                $signalCount++
            }
        }
    }

    return ($sampleCount -eq 0 -or $signalCount -lt 2)
}

function Save-LogEncodedScreenshot([string]$Path) {
    $logPath = Get-LogPath
    if (!(Test-Path $logPath)) {
        return $null
    }

    $lines = Get-Content -Path $logPath -ErrorAction SilentlyContinue
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

        $dir = Split-Path -Parent $Path
        if ($dir) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }

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

        return "renderer log captured log='$logPath' output='$Path' source=${width}x${height} chunks=$chunks"
    } finally {
        $bmp.Dispose()
    }
}

function Get-ScreenshotOutputPath([string]$Path, [int]$Index, [int]$Count) {
    if ($Count -le 1) {
        return $Path
    }

    $dir = Split-Path -Parent $Path
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $ext = [System.IO.Path]::GetExtension($Path)
    if (!$ext) {
        $ext = ".png"
    }

    return Join-Path $dir ("{0}_{1:D2}{2}" -f $base, ($Index + 1), $ext)
}

function Save-RendererScreenshot([string]$Path) {
    $requestPaths = @(
        (Join-Path $Game "ef_sp_screenshot_request.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_screenshot_request.txt"),
        (Join-Path $Cxbx "ef_sp_screenshot_request.txt")
    )
    $bmpCandidates = @(
        (Join-Path $Game "ef_sp_backbuffer.bmp"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_backbuffer.bmp"),
        (Join-Path $Cxbx "ef_sp_backbuffer.bmp"),
        (Join-Path $Game "ef_sp_xgshot.bmp"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_xgshot.bmp"),
        (Join-Path $Cxbx "ef_sp_xgshot.bmp")
    )

    $bmpCandidates | ForEach-Object {
        Remove-Item $_ -Force -ErrorAction SilentlyContinue
    }
    $requestWriteCount = 0
    foreach ($requestPath in $requestPaths) {
        try {
            $requestDir = Split-Path -Parent $requestPath
            if ($requestDir) {
                New-Item -ItemType Directory -Path $requestDir -Force | Out-Null
            }
            Set-Content -Path $requestPath -Value "capture" -Encoding ASCII
            $requestWriteCount++
        } catch {
        }
    }
    if ($requestWriteCount -eq 0) {
        return "renderer screenshot request write failed requests='$($requestPaths -join ';')'"
    }

    $deadline = (Get-Date).AddSeconds(20)
    $bmpPath = $null
    while ((Get-Date) -lt $deadline) {
        foreach ($candidate in $bmpCandidates) {
            if (Test-Path $candidate) {
                $item = Get-Item $candidate
                if ($item.Length -gt 54) {
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
        $logCapture = Save-LogEncodedScreenshot $Path
        $requestPaths | ForEach-Object {
            Remove-Item $_ -Force -ErrorAction SilentlyContinue
        }
        if ($logCapture) {
            return $logCapture
        }
        return "renderer screenshot timeout requests='$($requestPaths -join ';')' candidates='$($bmpCandidates -join ';')'"
    }

    $bmpItem = Get-Item $bmpPath
    if ($bmpItem.Length -le 54) {
        $logCapture = Save-LogEncodedScreenshot $Path
        $requestPaths | ForEach-Object {
            Remove-Item $_ -Force -ErrorAction SilentlyContinue
        }
        if ($logCapture) {
            return $logCapture
        }
        return "renderer screenshot empty bmp='$bmpPath' length=$($bmpItem.Length)"
    }

    $dir = Split-Path -Parent $Path
    if ($dir) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $ext = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($ext -eq ".bmp") {
        Copy-Item $bmpPath $Path -Force
        $requestPaths | ForEach-Object {
            Remove-Item $_ -Force -ErrorAction SilentlyContinue
        }
        return "renderer captured bmp='$bmpPath' output='$Path' length=$($bmpItem.Length)"
    }

    Add-Type -AssemblyName System.Drawing
    $img = New-Object System.Drawing.Bitmap $bmpPath
    try {
        $format = [System.Drawing.Imaging.ImageFormat]::Png
        if ($ext -eq ".jpg" -or $ext -eq ".jpeg") {
            $format = [System.Drawing.Imaging.ImageFormat]::Jpeg
        }
        $img.Save($Path, $format)
        if (Test-BitmapEffectivelyBlank $img) {
            $logCapture = Save-LogEncodedScreenshot $Path
            if ($logCapture) {
                $requestPaths | ForEach-Object {
                    Remove-Item $_ -Force -ErrorAction SilentlyContinue
                }
                return "renderer log captured after blank bmp='$bmpPath'; $logCapture"
            }
        }
        $requestPaths | ForEach-Object {
            Remove-Item $_ -Force -ErrorAction SilentlyContinue
        }
        return "renderer captured bmp='$bmpPath' output='$Path' size=$($img.Width)x$($img.Height) length=$($bmpItem.Length)"
    } finally {
        $img.Dispose()
    }
}

function Get-HeartbeatInfo([string]$Path) {
    $result = @{
        Count = 0
        FirstRealtime = $null
        LastCompletedFrame = $null
        LastRealtime = $null
        LastServerTime = $null
    }

    if (!(Test-Path $Path)) {
        return $result
    }

    $matches = Select-String `
        -Path $Path `
        -Pattern "JA: FRAME_HEARTBEAT completedFrame=(\d+) realtime=(\d+) serverTime=(-?\d+)" `
        -AllMatches `
        -ErrorAction SilentlyContinue

    $result.Count = $matches.Count
    if ($matches.Count -gt 0) {
        $firstLine = $matches[0]
        $first = $firstLine.Matches[$firstLine.Matches.Count - 1]
        $result.FirstRealtime = [int]$first.Groups[2].Value

        $lastLine = $matches[$matches.Count - 1]
        $last = $lastLine.Matches[$lastLine.Matches.Count - 1]
        $result.LastCompletedFrame = [int]$last.Groups[1].Value
        $result.LastRealtime = [int]$last.Groups[2].Value
        $result.LastServerTime = [int]$last.Groups[3].Value
    }

    return $result
}

function Test-CinematicTailActive([string]$Path) {
    if (!(Test-Path $Path)) {
        return $false
    }

    $tail = Get-Content $Path -Tail 12 -ErrorAction SilentlyContinue
    foreach ($line in $tail) {
        if ($line -match "CIN_RunCinematic enter|BinkVideo::Run") {
            return $true
        }
    }

    return $false
}

$outDir = Join-Path $Repo "scripts\output"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutPath = Join-Path $outDir "cxbx_sp_${stamp}.stdout.txt"
$stderrPath = Join-Path $outDir "cxbx_sp_${stamp}.stderr.txt"
$summaryPath = Join-Path $outDir "cxbx_sp_${stamp}.summary.txt"
$screenshotResult = ""
$screenshotDone = $false
$screenshotTrigger = ""
$screenshotReadyAt = $null
$screenshotReadyTrigger = ""
$screenshotResults = @()
$screenshotTriggers = @()
$screenshotDueSeconds = @()
$screenshotNextIndex = 0
if ($ScreenshotPath) {
    $ScreenshotPath = [System.IO.Path]::GetFullPath($ScreenshotPath)
}
if ($RequireVerticalSlice -and $ActiveSeconds -le 0) {
    $ActiveSeconds = 190
}
if ($ScreenshotCount -lt 1) {
    $ScreenshotCount = 1
}
if ($ScreenshotPath -and !$ScreenshotAfterLogPattern) {
    $rng = New-Object System.Random
    for ($i = 0; $i -lt $ScreenshotCount; $i++) {
        $randomOffset = 0.0
        if ($ScreenshotRandomWindowSeconds -gt 0) {
            $randomOffset = $rng.NextDouble() * $ScreenshotRandomWindowSeconds
        } elseif ($ScreenshotCount -gt 1) {
            $randomOffset = [double]$i
        }
        $screenshotDueSeconds += [math]::Round(([double]$ScreenshotAfterActiveSeconds + $randomOffset), 3)
    }
    $screenshotDueSeconds = @($screenshotDueSeconds | Sort-Object)
}

New-Item -ItemType Directory -Path $Game -Force | Out-Null
if ($Junction -and $Junction -ne $Game) {
    New-Item -ItemType Directory -Path $Junction -Force | Out-Null
}
$emuRoot = Join-Path $Cxbx "EmuDisk\Partition1"
New-Item -ItemType Directory -Path $emuRoot -Force | Out-Null
$runtimeRoots = @($Game)
if ([System.IO.Path]::GetFullPath($emuRoot) -ne [System.IO.Path]::GetFullPath($Game)) {
    $runtimeRoots += $emuRoot
}
if ([System.IO.Path]::GetFullPath($Cxbx) -ne [System.IO.Path]::GetFullPath($Game) -and
    [System.IO.Path]::GetFullPath($Cxbx) -ne [System.IO.Path]::GetFullPath($emuRoot)) {
    $runtimeRoots += $Cxbx
}

if (!$NoCopy) {
    $sourceXbe = [System.IO.Path]::GetFullPath((Join-Path $Repo "build\release\default.xbe"))
    $destXbe = [System.IO.Path]::GetFullPath((Join-Path $Game "default.xbe"))
    if ($sourceXbe -ne $destXbe) {
        Copy-Item $sourceXbe $destXbe -Force
    }
}

@(
    (Join-Path $Game "ef_sp_level.txt"),
    (Join-Path $emuRoot "ef_sp_level.txt"),
    (Join-Path $Game "ja_sp_level.txt"),
    (Join-Path $emuRoot "ja_sp_level.txt"),
    (Join-Path $Game "ef_sp_screenshot_preopen.txt"),
    (Join-Path $Game "ef_sp_screenshot_request.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_screenshot_request.txt"),
    (Join-Path $Cxbx "ef_sp_screenshot_request.txt"),
    (Join-Path $Game "ef_sp_cxbx_present_throttle.txt"),
    (Join-Path $emuRoot "ef_sp_cxbx_present_throttle.txt"),
    (Join-Path $Game "ef_sp_commands.txt"),
    (Join-Path $emuRoot "ef_sp_commands.txt"),
    (Join-Path $Game "ef_sp_postmap_commands.txt"),
    (Join-Path $emuRoot "ef_sp_postmap_commands.txt"),
    (Join-Path $Game "ja_sp_commands.txt"),
    (Join-Path $emuRoot "ja_sp_commands.txt"),
    (Join-Path $Game "ef_sp_backbuffer.bmp"),
    (Join-Path $Game "ef_sp_xgshot.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_xgshot.bmp"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_backbuffer.bmp"),
    (Join-Path $Cxbx "ef_sp_xgshot.bmp"),
    (Join-Path $Cxbx "ef_sp_backbuffer.bmp")
) | ForEach-Object {
    Remove-Item $_ -Force -ErrorAction SilentlyContinue
}

if ($Level) {
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_level.txt") -Value $Level -Encoding ASCII
    }
}

if ($CxbxPresentThrottle) {
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_cxbx_present_throttle.txt") -Value "1" -Encoding ASCII
    }
}

$smokeCommand = ""
if (!$NoSmokeInput) {
    $smokeCommand = "set s_xbox_silentAudio 1;set stefx_smoke_input 1;set stefx_smoke_aim 1;set stefx_smoke_wake_ai 1;set stefx_smoke_unlock_player 1;set stefx_smoke_ready_weapon 1;set stefx_smoke_stage_enemy 1;set stefx_smoke_input_forward $SmokeInputForward;set stefx_smoke_input_side $SmokeInputSide;set stefx_smoke_input_yaw $SmokeInputYaw;set stefx_smoke_input_start $SmokeInputStart;set stefx_smoke_input_attack_start $SmokeInputAttackStart;set stefx_smoke_input_attack_end $SmokeInputAttackEnd;set stefx_smoke_input_end $SmokeInputEnd"
}

if ($StartupCommand -and $StartupCommand.Count -gt 0) {
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_commands.txt") -Value $StartupCommand -Encoding ASCII
    }
} elseif ($smokeCommand) {
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_commands.txt") -Value $smokeCommand -Encoding ASCII
    }
} else {
    foreach ($root in $runtimeRoots) {
        Remove-Item (Join-Path $root "ef_sp_commands.txt") -Force -ErrorAction SilentlyContinue
    }
}
if ($PostMapCommand -and $PostMapCommand.Count -gt 0) {
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_postmap_commands.txt") -Value $PostMapCommand -Encoding ASCII
    }
} elseif ($smokeCommand) {
    $postMapSmokeCommand = "set stefx_smoke_fasttime 1;set stefx_smoke_fasttime_msec 2000;set timescale 40;$smokeCommand"
    foreach ($root in $runtimeRoots) {
        Set-Content -Path (Join-Path $root "ef_sp_postmap_commands.txt") -Value $postMapSmokeCommand -Encoding ASCII
    }
} else {
    foreach ($root in $runtimeRoots) {
        Remove-Item (Join-Path $root "ef_sp_postmap_commands.txt") -Force -ErrorAction SilentlyContinue
    }
}

@(
    (Join-Path $Game "ef_sp_log.txt"),
    (Join-Path $Junction "ef_sp_log.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ef_sp_log.txt")
) | ForEach-Object {
    Remove-Item $_ -Force -ErrorAction SilentlyContinue
}

Get-DebugFileCandidates | ForEach-Object {
    Remove-Item $_ -Force -ErrorAction SilentlyContinue
}

Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

$loader = Join-Path $Cxbx $LoaderName
$xbe = Join-Path $Game "default.xbe"
$args = "/load `"$xbe`""
$windowStyle = if ($Visible) { "Normal" } else { "Hidden" }

$p = Start-Process `
    -FilePath $loader `
    -ArgumentList $args `
    -WorkingDirectory $Cxbx `
    -PassThru `
    -WindowStyle $windowStyle `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath

$deadline = (Get-Date).AddSeconds($WatchdogSeconds)
$lastOutputSignature = ""
$lastOutputChange = Get-Date
$silentCrashSuspected = $false
$frameHeartbeatStalled = $false
$activeSeen = $false
$lastHeartbeatFrame = $null
$lastHeartbeatChange = Get-Date
$activeSecondsReached = ($ActiveSeconds -le 0)

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2

    $cxbxAlive = !$p.HasExited
    if (!$cxbxAlive) {
        break
    }

    $log = Get-LogPath
    $signatureParts = @()
    if (Test-Path $log) {
        $item = Get-Item $log
        $signatureParts += "log={0}:{1}" -f $item.Length, $item.LastWriteTimeUtc.Ticks

        if (!$activeSeen -and (Has-Match $log "cls.state = CA_ACTIVE - GAME IS RUNNING")) {
            $activeSeen = $true
            $lastHeartbeatChange = Get-Date
        }

        if ($ScreenshotPath -and !$screenshotDone -and $ScreenshotAfterLogPattern -and (Has-Match $log $ScreenshotAfterLogPattern)) {
            $capturePath = Get-ScreenshotOutputPath $ScreenshotPath 0 1
            $screenshotResult = Save-RendererScreenshot $capturePath
            $screenshotResults += $screenshotResult
            $screenshotDone = $true
            $screenshotTrigger = "log:$ScreenshotAfterLogPattern"
            $screenshotTriggers += $screenshotTrigger
        }

        $heartbeat = Get-HeartbeatInfo $log
        if ($heartbeat.LastCompletedFrame -ne $null) {
            if ($lastHeartbeatFrame -eq $null -or $heartbeat.LastCompletedFrame -ne $lastHeartbeatFrame) {
                $lastHeartbeatFrame = $heartbeat.LastCompletedFrame
                $lastHeartbeatChange = Get-Date
            }
            if ($ScreenshotPath -and !$ScreenshotAfterLogPattern -and $screenshotReadyAt -eq $null) {
                if (($ScreenshotAfterServerTime -le 0 -or $heartbeat.LastServerTime -ge $ScreenshotAfterServerTime) -or
                    (Has-Match $log "STEFX: CG_AddViewWeapon added")) {
                    $screenshotReadyAt = Get-Date
                    $screenshotReadyTrigger = "serverTime:$($heartbeat.LastServerTime)"
                    if (Has-Match $log "STEFX: CG_AddViewWeapon added") {
                        $screenshotReadyTrigger += ";viewWeapon"
                    }
                }
            }
            if ($heartbeat.FirstRealtime -ne $null -and $heartbeat.LastRealtime -ne $null) {
                $activeElapsedSeconds = ($heartbeat.LastRealtime - $heartbeat.FirstRealtime) / 1000.0
                if ($ScreenshotPath -and !$ScreenshotAfterLogPattern -and $screenshotReadyAt -ne $null) {
                    $screenshotReadyElapsedSeconds = ((Get-Date) - $screenshotReadyAt).TotalSeconds
                    while ($screenshotNextIndex -lt $screenshotDueSeconds.Count -and $activeElapsedSeconds -ge $screenshotDueSeconds[$screenshotNextIndex]) {
                        if ($screenshotReadyElapsedSeconds -lt $screenshotDueSeconds[$screenshotNextIndex]) {
                            break
                        }
                        $capturePath = Get-ScreenshotOutputPath $ScreenshotPath $screenshotNextIndex $ScreenshotCount
                        $captureResult = Save-RendererScreenshot $capturePath
                        $captureTrigger = "readySeconds:$($screenshotDueSeconds[$screenshotNextIndex]);$screenshotReadyTrigger"
                        $screenshotResults += "$capturePath :: $captureResult"
                        $screenshotTriggers += $captureTrigger
                        $screenshotResult = $screenshotResults -join " || "
                        $screenshotTrigger = $screenshotTriggers -join ";"
                        $screenshotNextIndex++
                        if ($screenshotNextIndex -ge $ScreenshotCount) {
                            $screenshotDone = $true
                            break
                        }
                    }
                }
                if ($ActiveSeconds -gt 0 -and $activeElapsedSeconds -ge $ActiveSeconds) {
                    $activeSecondsReached = $true
                    break
                }
            }
        } elseif ($activeSeen -and (Test-CinematicTailActive $log)) {
            $lastHeartbeatChange = Get-Date
        } elseif ($activeSeen -and (((Get-Date) - $lastHeartbeatChange).TotalSeconds -ge $QuietGraceSeconds)) {
            $frameHeartbeatStalled = $true
            break
        }
    }
    if (Test-Path $stdoutPath) {
        $item = Get-Item $stdoutPath
        $signatureParts += "stdout={0}:{1}" -f $item.Length, $item.LastWriteTimeUtc.Ticks
    }
    if (Test-Path $stderrPath) {
        $item = Get-Item $stderrPath
        $signatureParts += "stderr={0}:{1}" -f $item.Length, $item.LastWriteTimeUtc.Ticks
    }

    $signature = $signatureParts -join "|"
    if ($signature -ne $lastOutputSignature) {
        $lastOutputSignature = $signature
        $lastOutputChange = Get-Date
    } else {
        $cinematicActive = $false
        if (Test-Path $log) {
            $cinematicActive = Test-CinematicTailActive $log
        }
        $quietLimit = if ($activeSeen -and !$cinematicActive) { $QuietGraceSeconds } else { $InitialQuietGraceSeconds }
        if (((Get-Date) - $lastOutputChange).TotalSeconds -ge $quietLimit) {
        $silentCrashSuspected = $true
        break
        }
    }

    if ($activeSeen -and $lastHeartbeatFrame -ne $null -and !(Test-CinematicTailActive $log) -and (((Get-Date) - $lastHeartbeatChange).TotalSeconds -ge $QuietGraceSeconds)) {
        $frameHeartbeatStalled = $true
        break
    }

    $consoleText = ""
    if (Test-Path $stdoutPath) {
        $consoleText += Get-Content $stdoutPath -Raw -ErrorAction SilentlyContinue
    }
    if (Test-Path $stderrPath) {
        $consoleText += Get-Content $stderrPath -Raw -ErrorAction SilentlyContinue
    }
    if ($consoleText -match "Received Exception|FATAL: X86|EIP :=|unhandled exception") {
        break
    }
}

$aliveAtEnd = !$p.HasExited
$loaderExitCode = $null
if ($p.HasExited) {
    $loaderExitCode = $p.ExitCode
}
if (!$p.HasExited) {
    try {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    } catch {
    }
}
Start-Sleep -Seconds 1
if (!$CxbxPresentThrottle) {
    Remove-Item (Join-Path $Game "ef_sp_cxbx_present_throttle.txt") -Force -ErrorAction SilentlyContinue
}

$logPath = Get-LogPath
$consoleCombined = ""
if (Test-Path $stdoutPath) {
    $consoleCombined += Get-Content $stdoutPath -Raw -ErrorAction SilentlyContinue
}
if (Test-Path $stderrPath) {
    $consoleCombined += Get-Content $stderrPath -Raw -ErrorAction SilentlyContinue
}
$debugCopies = Copy-DebugFiles $outDir $stamp
$debugCombined = ""
foreach ($debugCopy in $debugCopies) {
    $debugCombined += Get-Content $debugCopy -Raw -ErrorAction SilentlyContinue
}
$consoleCombined += $debugCombined

$xbePath = Join-Path $Game "default.xbe"
$xbeLastWriteTime = $null
$newestSourceInput = Get-NewestCompiledSourceInput
$newestSourceLastWriteTime = $null
$newestSourcePath = ""
if (Test-Path $xbePath) {
    $xbeLastWriteTime = (Get-Item -LiteralPath $xbePath).LastWriteTime
}
if ($newestSourceInput) {
    $newestSourceLastWriteTime = $newestSourceInput.LastWriteTime
    $newestSourcePath = $newestSourceInput.FullName
}
$xbeOlderThanSource = ($xbeLastWriteTime -ne $null -and $newestSourceLastWriteTime -ne $null -and $xbeLastWriteTime -lt $newestSourceLastWriteTime)

$active = Has-Match $logPath "cls.state = CA_ACTIVE - GAME IS RUNNING"
$uiInit = Has-Match $logPath "UI_Init|UI init|VM_Create.*ui|CL_InitUI"
$returnedFrames = Count-Matches $logPath "CG_DRAW_ACTIVE_FRAME\) returned|CL_CGameRendering: VM_Call\(CG_DRAW_ACTIVE_FRAME\) returned"
$heartbeatCount = Count-Matches $logPath "JA: FRAME_HEARTBEAT completedFrame="
$finalHeartbeat = Get-HeartbeatInfo $logPath
$activeElapsedSecondsFinal = 0
if ($finalHeartbeat.FirstRealtime -ne $null -and $finalHeartbeat.LastRealtime -ne $null) {
    $activeElapsedSecondsFinal = [math]::Round(($finalHeartbeat.LastRealtime - $finalHeartbeat.FirstRealtime) / 1000.0, 1)
}
$failureCount = Count-Matches $logPath "texture allocation failures"
$binkOpenCount = Count-Matches $logPath "BinkVideo::Start BinkOpen ok"
$binkFailCount = Count-Matches $logPath "BinkVideo::Start BinkOpen failed|CIN_RunCinematic BinkVideo::Start failed"
$missingMovieCount = Count-Matches $logPath "CIN_PlayCinematic not found"
$unknownFormatCount = @([regex]::Matches($consoleCombined, "Unknown Format")).Count + (Count-Matches $logPath "Unknown Format")
$yavinOverlaySkipCount = Count-Matches $logPath "XBOX_YAVIN_SKY_OVERLAY_SKIP"
$fileFatalCount = Count-MatchesCaseSensitive $logPath "Out of memory|Received Exception|FATAL|Z_Malloc\(\): Out of memory|EIP"
$consoleFatalCount = @([regex]::Matches($consoleCombined, "Received Exception|FATAL: X86|EIP :=|unhandled exception")).Count
$fatalCount = $fileFatalCount + $consoleFatalCount
$mapRawBspLoads = Count-Matches $logPath "EF: CM_LoadMap raw BSP 'maps/borg1\.bsp'"
$rawLightmapLoads = Count-Matches $logPath "EF: R_LoadRawLightmaps map='maps/borg1\.bsp'"
$rawLightmapStats = Count-Matches $logPath "EF: RAW_LIGHTMAP_STATS index="
$activeWorldMultitexture = Count-Matches $logPath "EF: ACTIVE_MTEXTURE shader='textures/"
$stage1LightmapApplies = Count-Matches $logPath "EF: TEX_STAGE_APPLY stage=1 texid=(?:1[1-9]|[2-9][0-9]+)\b"
$textureRebinds = Count-Matches $logPath "STEFX: FORCE_TEXTURE_REBIND"
$viewWeaponAdds = Count-Matches $logPath "STEFX: CG_AddViewWeapon added"
$smokeInputApplied = Count-Matches $logPath "STEFX: smoke input applied"
$smokeInputMoving = Count-Matches $logPath "STEFX: smoke input applied .*\bmove=(?!\(0,0,0\))\(-?\d+,-?\d+,-?\d+\)"
$smokeInputAttacking = Count-Matches $logPath "STEFX: smoke input applied .*attack=1"
$smokeAimTargets = Count-Matches $logPath "STEFX: smoke aim target"
$smokeStageEnemy = Count-Matches $logPath "STEFX: smoke stage enemy target="
$smokeUnlocks = Count-Matches $logPath "STEFX: smoke unlock player control"
$smokeReadyWeapon = Count-Matches $logPath "STEFX: smoke ready weapon"
$smokeAiWake = Count-Matches $logPath "STEFX: smoke wake enemy"
$controllerButtons = Count-Matches $logPath "STEFX: controller button"
$controllerAxes = Count-Matches $logPath "STEFX: controller axes"
$inputGateCleared = Count-Matches $logPath "STEFX: direct-map input gate cleared"
$xboxBindsInstalled = Count-Matches $logPath "STEFX: (installed|confirmed|replaced) Xbox bind"
$clientMoveResults = Count-Matches $logPath "STEFX: ClientThink PM state .* moved=1"
$playerAttackCmds = Count-Matches $logPath "STEFX: ClientThink player attack probe"
$playerPmoveFireEvents = Count-Matches $logPath "STEFX: PM_AddEvent fire"
$playerClientFireEvents = Count-Matches $logPath "STEFX: ClientEvents fire"
$playerCgameFireEvents = Count-Matches $logPath "STEFX: CG_FireWeapon ent=0"
$playerFireWeapon = Count-Matches $logPath "STEFX: FireWeapon enter ent=0"
$playerDamageHits = Count-Matches $logPath "STEFX: G_Damage player hit"
$npcPainEvents = Count-Matches $logPath "STEFX: NPC_Pain"
$npcEnemyAcquired = Count-Matches $logPath "STEFX: NPC_SetEnemy"
$npcSpawns = Count-Matches $logPath "STEFX: NPC_Begin"
$cgameCharacters = Count-Matches $logPath "STEFX: CG_Player valid ent="
$characterAnimSurfaces = Count-Matches $logPath "STEFX: R_AddAnimSurfaces"
$characterAnimSurfaceVisible = Count-Matches $logPath "STEFX: R_AddAnimSurfaces visible"
$characterAnimSurfaceCullOut = Count-Matches $logPath "STEFX: R_AddAnimSurfaces cull out"
$mdrPlaceholderSkips = Count-Matches $logPath "EF: skipping MDR placeholder render"

$missingRequirements = @()
if ($RequireVerticalSlice -and $xbeOlderThanSource) { $missingRequirements += "stale_xbe" }
if ($mapRawBspLoads -le 0) { $missingRequirements += "map" }
if ($rawLightmapLoads -le 0 -and $rawLightmapStats -le 0) { $missingRequirements += "lighting" }
if ($activeWorldMultitexture -le 0) { $missingRequirements += "textured_world" }
if ($stage1LightmapApplies -le 0) { $missingRequirements += "stage1_lightmap" }
if ($textureRebinds -le 0) { $missingRequirements += "texture_rebind" }
if ($viewWeaponAdds -le 0) { $missingRequirements += "visible_weapon" }
if ($inputGateCleared -le 0) { $missingRequirements += "input_gate" }
if ($xboxBindsInstalled -le 0) { $missingRequirements += "xbox_binds" }
if ($smokeInputMoving -le 0) { $missingRequirements += "smoke_input" }
if ($clientMoveResults -le 0) { $missingRequirements += "movement" }
if ($smokeInputAttacking -le 0 -and $playerAttackCmds -le 0) { $missingRequirements += "attack_cmd" }
if ($playerPmoveFireEvents -le 0 -and $playerClientFireEvents -le 0 -and $playerFireWeapon -le 0) {
    $missingRequirements += "server_fire"
}
if ($playerCgameFireEvents -le 0) { $missingRequirements += "client_fire" }
if ($npcSpawns -le 0 -and $cgameCharacters -le 0) {
    $missingRequirements += "characters_present"
}
if ($characterAnimSurfaceVisible -le 0) { $missingRequirements += "characters_visible" }
if ($mdrPlaceholderSkips -gt 0) { $missingRequirements += "mdr_placeholder_render" }
if ($npcEnemyAcquired -le 0 -and $npcPainEvents -le 0) { $missingRequirements += "ai_present" }
if ($playerDamageHits -le 0 -and $npcPainEvents -le 0) { $missingRequirements += "weapon_interaction" }

$rendererScreenshotsComplete = $false
$rendererScreenshotsNonblank = $false
if ($ScreenshotPath -and $screenshotDone -and $screenshotResults.Count -ge $ScreenshotCount) {
    $rendererScreenshotsComplete = $true
    $blankOrFailedShots = @($screenshotResults | Where-Object {
        $_ -match "timeout|failed|empty|after blank"
    }).Count
    $rendererScreenshotsNonblank = ($blankOrFailedShots -eq 0)
}
if (!$rendererScreenshotsComplete) { $missingRequirements += "renderer_screenshot" }
if (!$rendererScreenshotsNonblank) { $missingRequirements += "renderer_nonblank_screenshot" }

$verticalSlicePass = ($active -and $fatalCount -eq 0 -and $missingRequirements.Count -eq 0)
$missingRequirementsText = if ($missingRequirements.Count -gt 0) { $missingRequirements -join "," } else { "none" }

$status = "PASS"
if ($fatalCount -gt 0) {
    $status = "FAIL_EMULATOR_EXCEPTION"
} elseif ($frameHeartbeatStalled) {
    $status = "FAIL_FRAME_HEARTBEAT_STALLED"
} elseif ($silentCrashSuspected) {
    $status = "FAIL_LOG_STALLED_PROCESS_ALIVE"
} elseif (!$aliveAtEnd) {
    $status = "FAIL_EXITED_BEFORE_WATCHDOG"
} elseif (!$active) {
    if (!($AllowNoActive -and $uiInit)) {
        $status = "FAIL_NOT_ACTIVE"
    }
} elseif ($heartbeatCount -lt 5 -and !($ActiveSeconds -gt 0 -and $activeSecondsReached)) {
    $status = "FAIL_HEARTBEAT_INSUFFICIENT"
} elseif ($ActiveSeconds -gt 0 -and !$activeSecondsReached) {
    $status = "FAIL_ACTIVE_SECONDS_INSUFFICIENT"
} elseif ($RequireVerticalSlice -and !$verticalSlicePass) {
    $status = "FAIL_VERTICAL_SLICE_INCOMPLETE"
}

$tail = @()
if (Test-Path $logPath) {
    $tail = Get-Content $logPath -Tail 80
}

$consoleTail = @()
if ($consoleCombined.Length -gt 0) {
    $consoleTail = ($consoleCombined -split "`r?`n") | Select-Object -Last 80
}

$summary = @(
    "status=$status",
    "aliveAtEnd=$aliveAtEnd",
    "loaderHasExited=$($p.HasExited)",
    "loaderExitCode=$loaderExitCode",
    "silentCrashSuspected=$silentCrashSuspected",
    "frameHeartbeatStalled=$frameHeartbeatStalled",
    "active=$active",
    "uiInit=$uiInit",
    "returnedFrames=$returnedFrames",
    "heartbeatCount=$heartbeatCount",
    "activeElapsedSeconds=$activeElapsedSecondsFinal",
    "targetActiveSeconds=$ActiveSeconds",
    "activeSecondsReached=$activeSecondsReached",
    "lastHeartbeatFrame=$($finalHeartbeat.LastCompletedFrame)",
    "lastHeartbeatRealtime=$($finalHeartbeat.LastRealtime)",
    "lastHeartbeatServerTime=$($finalHeartbeat.LastServerTime)",
    "failureCount=$failureCount",
    "binkOpenCount=$binkOpenCount",
    "binkFailCount=$binkFailCount",
    "missingMovieCount=$missingMovieCount",
    "unknownFormatCount=$unknownFormatCount",
    "yavinOverlaySkipCount=$yavinOverlaySkipCount",
    "fileFatalCount=$fileFatalCount",
    "consoleFatalCount=$consoleFatalCount",
    "fatalCount=$fatalCount",
    "verticalSlicePass=$verticalSlicePass",
    "missingRequirements=$missingRequirementsText",
    "xbePath=$xbePath",
    "xbeLastWriteTime=$xbeLastWriteTime",
    "newestSourceInput=$newestSourcePath",
    "newestSourceLastWriteTime=$newestSourceLastWriteTime",
    "xbeOlderThanSource=$xbeOlderThanSource",
    "desktopCapture=disabled",
    "captureSource=xbe-renderer-request",
    "mapRawBspLoads=$mapRawBspLoads",
    "rawLightmapLoads=$rawLightmapLoads",
    "rawLightmapStats=$rawLightmapStats",
    "activeWorldMultitexture=$activeWorldMultitexture",
    "stage1LightmapApplies=$stage1LightmapApplies",
    "textureRebinds=$textureRebinds",
    "viewWeaponAdds=$viewWeaponAdds",
    "smokeInputApplied=$smokeInputApplied",
    "smokeInputMoving=$smokeInputMoving",
    "smokeInputAttacking=$smokeInputAttacking",
    "smokeAimTargets=$smokeAimTargets",
    "smokeStageEnemy=$smokeStageEnemy",
    "smokeUnlocks=$smokeUnlocks",
    "smokeReadyWeapon=$smokeReadyWeapon",
    "smokeAiWake=$smokeAiWake",
    "smokeHarnessEnabled=$(!$NoSmokeInput)",
    "smokeHarnessWindow=$SmokeInputStart..$SmokeInputEnd",
    "smokeHarnessAttackWindow=$SmokeInputAttackStart..$SmokeInputAttackEnd",
    "smokeHarnessMove=($SmokeInputForward,$SmokeInputSide,$SmokeInputYaw)",
    "controllerButtons=$controllerButtons",
    "controllerAxes=$controllerAxes",
    "inputGateCleared=$inputGateCleared",
    "xboxBindsInstalled=$xboxBindsInstalled",
    "clientMoveResults=$clientMoveResults",
    "playerAttackCmds=$playerAttackCmds",
    "playerPmoveFireEvents=$playerPmoveFireEvents",
    "playerClientFireEvents=$playerClientFireEvents",
    "playerCgameFireEvents=$playerCgameFireEvents",
    "playerFireWeapon=$playerFireWeapon",
    "playerDamageHits=$playerDamageHits",
    "npcPainEvents=$npcPainEvents",
    "npcEnemyAcquired=$npcEnemyAcquired",
    "npcSpawns=$npcSpawns",
    "cgameCharacters=$cgameCharacters",
    "characterAnimSurfaces=$characterAnimSurfaces",
    "characterAnimSurfaceVisible=$characterAnimSurfaceVisible",
    "characterAnimSurfaceCullOut=$characterAnimSurfaceCullOut",
    "mdrPlaceholderSkips=$mdrPlaceholderSkips",
    "logPath=$logPath",
    "stdoutPath=$stdoutPath",
    "stderrPath=$stderrPath",
    "debugCopies=$($debugCopies -join ';')",
    "screenshotPath=$ScreenshotPath",
    "screenshotCountRequested=$ScreenshotCount",
    "screenshotCountDone=$($screenshotResults.Count)",
    "rendererScreenshotsComplete=$rendererScreenshotsComplete",
    "rendererScreenshotsNonblank=$rendererScreenshotsNonblank",
    "screenshotDueSeconds=$($screenshotDueSeconds -join ',')",
    "screenshotAfterServerTime=$ScreenshotAfterServerTime",
    "screenshotReadyTrigger=$screenshotReadyTrigger",
    "screenshotDone=$screenshotDone",
    "screenshotTrigger=$screenshotTrigger",
    "screenshotResult=$screenshotResult",
    "",
    "=== file log tail ==="
) + $tail + @(
    "",
    "=== emulator console tail ==="
) + $consoleTail

$summary | Set-Content -Path $summaryPath -Encoding ASCII
$summary

$smokeInputFiles = @(
    "ef_sp_commands.txt",
    "ef_sp_postmap_commands.txt",
    "ef_sp_active_commands.txt",
    "ef_sp_active_command_time.txt",
    "ef_sp_cxbx_present_throttle.txt",
    "ef_sp_screenshot_request.txt",
    "ef_sp_screenshot_pending.txt"
)
foreach ($root in $runtimeRoots) {
    foreach ($name in $smokeInputFiles) {
        Remove-Item -LiteralPath (Join-Path $root $name) -Force -ErrorAction SilentlyContinue
    }
}

if ($status -eq "PASS") {
    exit 0
}

exit 1
