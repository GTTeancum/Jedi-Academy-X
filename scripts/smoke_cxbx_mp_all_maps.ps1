param(
    [string]$Game = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X",
    [string]$CaptureRoot = "C:\Games\Emulators\CXBX-CodexCapture",
    [int]$HoldSeconds = 90,
    [int]$MapLoadTimeoutSeconds = 180,
    [int]$ScreenshotAtSeconds = 45,
    [int]$MapLimit = 0,
    [string[]]$MapNames = @(),
    [switch]$EnableSmokeInput,
    [int]$SmokeInputStart = 12000,
    [int]$SmokeInputEnd = 65000,
    [int]$SmokeAttackStart = 15000,
    [int]$SmokeAttackEnd = 60000,
    [int]$SmokeInputForward = 0,
    [int]$SmokeInputSide = 0,
    [int]$SmokeInputYaw = 0,
    [int]$SmokeViewPitch = 0,
    [int]$SmokeViewYaw = 9999,
    [switch]$CaptureLoadingScreens,
    [double]$LoadingScreenshotDelaySeconds = 1.0,
    [int]$LoadingScreenshotCount = 1,
    [double]$LoadingScreenshotIntervalSeconds = 0.35,
    [double]$MinFpsAverage = 30.0,
    [double]$MinFpsSample = 20.0,
    [int]$MaxLowFpsSamples = 4,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$maps = @(
    "hm_borg1", "hm_kln1", "hm_for1", "hm_noon", "hm_voy2", "hm_dn1",
    "hm_scav1", "hm_voy1", "hm_borg2", "hm_dn2", "hm_cam", "hm_borg3",
    "ctf_kln1", "ctf_kln2", "ctf_and1", "ctf_voy1", "ctf_voy2",
    "ctf_breach", "ctf_for1", "ctf_neptune", "ctf_oldwest", "ctf_reservoir",
    "ctf_singularity", "ctf_dn1", "ctf_spyglass2", "ctf_stasis", "hm_altar",
    "hm_blastradius", "hm_borgattack", "hm_for2", "hm_raven", "hm_temple", "hm_voy3"
)
if ($MapLimit -gt 0) {
    $maps = @($maps | Select-Object -First $MapLimit)
}
if ($MapNames.Count -gt 0) {
    $maps = @(
        $MapNames |
            ForEach-Object { $_ -split "," } |
            ForEach-Object { $_.Trim() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
}
if ($maps.Count -le 0) {
    throw "No Holomatch maps selected for smoke testing."
}

$fatalPattern = "FATAL|ERR_FATAL|KeBugCheck|Unhandled exception|Out of memory|Z_Malloc[^\r\n]*(failed|failure)|allocation failed|texture allocation failure"
$heartbeatPattern = "JA: FRAME_HEARTBEAT completedFrame=(\d+) realtime=(\d+) serverTime=(-?\d+) fd=(\d+) el=(\d+) fps=(\d+)\.(\d+) mem=(\d+)/(\d+)KB"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = [System.IO.Path]::GetFullPath($Game)
$captureRootFull = [System.IO.Path]::GetFullPath($CaptureRoot)
$xbe = Join-Path $gameRoot "efmp.xbe"
$startCapture = Join-Path $captureRootFull "Start-CodexCaptureSession.ps1"
$requestCapture = Join-Path $captureRootFull "Request-CodexScreenshot.ps1"
$commandInbox = Join-Path $gameRoot "ef_mp_runtime_commands.txt"
$commandTemp = Join-Path $gameRoot "ef_mp_runtime_commands.tmp"
$commandSlot = 0
$smokeHarnessMarker = Join-Path $gameRoot "ef_sp_smoke_harness.txt"
$smokePostMapCommands = Join-Path $gameRoot "ef_sp_postmap_commands.txt"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$sessionName = "stefx-all-mp-maps-$stamp"
$resultRoot = Join-Path $repoRoot "build\proofs\holomatch-map-sweep-$stamp"
$resultsJson = Join-Path $resultRoot "map-results.json"
$resultsCsv = Join-Path $resultRoot "map-results.csv"
$summaryJson = Join-Path $resultRoot "session-summary.json"
$finalLog = Join-Path $resultRoot "ef_mp_log.txt"
$results = New-Object System.Collections.Generic.List[object]
$ownedIds = New-Object System.Collections.Generic.List[int]
$launchTime = $null
$logPath = $null
$primaryPid = 0
$primaryProcessName = ""
$primaryProcessPath = ""
$primaryProcessStartTime = ""
$sessionFailure = $null
$sessionFailureContext = ""
$currentMapName = ""

function Get-CommandInboxPaths {
    @(
        (Join-Path $gameRoot "ef_mp_runtime_commands.txt"),
        (Join-Path $gameRoot "ef_mp_runtime_commands.0.txt"),
        (Join-Path $gameRoot "ef_mp_runtime_commands.1.txt"),
        (Join-Path $gameRoot "ef_mp_runtime_commands.2.txt"),
        (Join-Path $gameRoot "ef_mp_runtime_commands.3.txt"),
        (Join-Path $gameRoot "ef_mp_runtime_commands.tmp")
    )
}

function Get-CxbxProcesses {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match "^cxbx" -or $_.ProcessName -match "^cxbxr" })
}

function Get-ProcessPathSafe($Process) {
    try {
        return [string]$Process.Path
    } catch {
        return ""
    }
}

function Is-CaptureRootProcess($Process) {
    $path = Get-ProcessPathSafe $Process
    if ([string]::IsNullOrWhiteSpace($path)) {
        return $false
    }
    $full = [System.IO.Path]::GetFullPath($path)
    return $full.StartsWith($captureRootFull, [System.StringComparison]::OrdinalIgnoreCase)
}

function Resolve-OwnedCxbxProcess {
    $process = Get-Process -Id $script:primaryPid -ErrorAction SilentlyContinue
    if ($null -ne $process -and -not $process.HasExited) {
        return $process
    }

    $captureProcesses = @(Get-CxbxProcesses | Where-Object { Is-CaptureRootProcess $_ })
    if ($captureProcesses.Count -eq 1) {
        $script:primaryPid = [int]$captureProcesses[0].Id
        if (-not $ownedIds.Contains($script:primaryPid)) {
            $ownedIds.Add($script:primaryPid)
        }
        Write-Host "OWNED_PROCESS_SWITCHED pid=$script:primaryPid name=$($captureProcesses[0].ProcessName)"
        return $captureProcesses[0]
    }
    if ($captureProcesses.Count -gt 1) {
        $details = ($captureProcesses | ForEach-Object { "$($_.Id):$($_.ProcessName)" }) -join ", "
        throw "Multiple CodexCapture CXBX-R processes are running; refusing to guess ownership: $details"
    }
    return $null
}

function Get-LogCandidates {
    @(
        (Join-Path $gameRoot "ef_mp_log.txt"),
        (Join-Path $gameRoot "BaseEF\ef_mp_log.txt"),
        (Join-Path $gameRoot "..\EmuDisk\Partition1\ef_mp_log.txt"),
        (Join-Path $gameRoot "..\EmuDisk\Partition2\ef_mp_log.txt"),
        (Join-Path $gameRoot "..\EmuDisk\Partition3\ef_mp_log.txt"),
        (Join-Path $captureRootFull "EmuDisk\Partition1\ef_mp_log.txt"),
        (Join-Path $captureRootFull "EmuDisk\Partition2\ef_mp_log.txt"),
        (Join-Path $captureRootFull "EmuDisk\Partition3\ef_mp_log.txt"),
        (Join-Path $captureRootFull "ef_mp_log.txt")
    )
}

function Get-LogText {
    if ([string]::IsNullOrWhiteSpace($script:logPath) -or
        -not (Test-Path -LiteralPath $script:logPath -PathType Leaf)) {
        return ""
    }
    try {
        return [string]::Join("`n", @(Get-Content -LiteralPath $script:logPath -ErrorAction Stop))
    } catch {
        return ""
    }
}

function Test-TextContains([string]$Text, [string]$Needle) {
    if ([string]::IsNullOrWhiteSpace($Text) -or [string]::IsNullOrWhiteSpace($Needle)) {
        return $false
    }
    return ($Text.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
}

function Get-LastMatchingLine([string]$Text, [string]$Pattern) {
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ""
    }
    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -le 0) {
        return ""
    }
    return (($matches[$matches.Count - 1].Value) -replace "[\r\n]+", " ").Trim()
}

function Get-LastLogLines([string]$Text, [int]$Count) {
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ""
    }
    $lines = @($Text -split "\r?\n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($lines.Count -le 0) {
        return ""
    }
    $skip = [Math]::Max(0, $lines.Count - $Count)
    return (($lines | Select-Object -Skip $skip) -join " | ").Trim()
}

function Get-OtherCxbxProcessSummary {
    $others = @(Get-CxbxProcesses | Where-Object { -not $ownedIds.Contains($_.Id) })
    if ($others.Count -le 0) {
        return ""
    }
    return (($others | ForEach-Object {
        $path = Get-ProcessPathSafe $_
        "$($_.Id):$($_.ProcessName):$path"
    }) -join ", ")
}

function Get-SweepFailureContext {
    $text = Get-LogText
    $ownedIdentity = "ownedPid=$script:primaryPid ownedName='$script:primaryProcessName' ownedPath='$script:primaryProcessPath' ownedStart='$script:primaryProcessStartTime'"
    $otherProcesses = Get-OtherCxbxProcessSummary
    if ([string]::IsNullOrWhiteSpace($otherProcesses)) {
        $otherProcesses = "none"
    }
    if ([string]::IsNullOrWhiteSpace($text)) {
        return "map=$script:currentMapName $ownedIdentity otherCxbx=[$otherProcesses] log=empty"
    }

    $lastMap = Get-LastMatchingLine $text "STEFX_HM_SWEEP: map active[^\r\n]*"
    $lastHeartbeat = Get-LastMatchingLine $text "JA: FRAME_HEARTBEAT[^\r\n]*"
    $lastAlive = Get-LastMatchingLine $text "STEFX_HM_ALIVE:[^\r\n]*"
    $lastMove = Get-LastMatchingLine $text "STEFX_HM_MOVE:[^\r\n]*"
    $lastTeleport = Get-LastMatchingLine $text "STEFX_HM_TELEPORT:[^\r\n]*"
    $lastFatal = Get-LastMatchingLine $text $fatalPattern
    $tail = Get-LastLogLines $text 8

    return "map=$script:currentMapName $ownedIdentity otherCxbx=[$otherProcesses] lastMap=[$lastMap] lastHeartbeat=[$lastHeartbeat] lastAlive=[$lastAlive] lastMove=[$lastMove] lastTeleport=[$lastTeleport] lastFatal=[$lastFatal] tail=[$tail]"
}

function Assert-OwnedProcessAlive {
    $process = Resolve-OwnedCxbxProcess
    if ($null -eq $process -or $process.HasExited) {
        throw "Owned CXBX-R process $($script:primaryPid) exited during the continuous sweep. $(Get-SweepFailureContext)"
    }
}

function Wait-ForFreshLog {
    $deadline = (Get-Date).AddSeconds($MapLoadTimeoutSeconds)
    do {
        Assert-OwnedProcessAlive
        foreach ($candidate in Get-LogCandidates) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $item = Get-Item -LiteralPath $candidate
                if ($item.LastWriteTime -ge $launchTime.AddSeconds(-1)) {
                    $script:logPath = $item.FullName
                    return
                }
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for a fresh ef_mp_log.txt."
}

function Wait-ForMapActive([string]$MapName) {
    $marker = "STEFX_HM_SWEEP: map active name='$MapName' active=1"
    $deadline = (Get-Date).AddSeconds($MapLoadTimeoutSeconds)
    do {
        Assert-OwnedProcessAlive
        $text = Get-LogText
        if (Select-String -LiteralPath $script:logPath -SimpleMatch $marker -Quiet -ErrorAction SilentlyContinue) {
            return
        }
        if ($text -match $fatalPattern) {
            throw "Fatal marker appeared while loading $MapName."
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for map-active marker for $MapName."
}

function Wait-ForMapTransitionStart([string]$MapName) {
    $markers = @(
        "JA: SV_Map_: requested '$MapName'",
        "EF: SV_SpawnServer before CM_LoadMap map='$MapName'",
        "EF: CM_LoadMap raw probe begin name='maps/$MapName.bsp'"
    )
    $deadline = (Get-Date).AddSeconds($MapLoadTimeoutSeconds)
    do {
        Assert-OwnedProcessAlive
        $text = Get-LogText
        foreach ($marker in $markers) {
            if (Test-TextContains $text $marker) {
                return
            }
        }
        if ($text -match $fatalPattern) {
            throw "Fatal marker appeared while waiting for map transition start for $MapName."
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for map-transition-start marker for $MapName."
}

function Get-MapLogText([string]$MapName) {
    $text = Get-LogText
    $marker = "STEFX_HM_SWEEP: map active name='$MapName' active=1"
    $markerIndex = $text.LastIndexOf($marker, [System.StringComparison]::OrdinalIgnoreCase)
    if ($markerIndex -lt 0) {
        return ""
    }
    return $text.Substring($markerIndex)
}

function Get-MapTransitionLogText([string]$MapName) {
    $text = Get-LogText
    $commandMarker = "STEFX_HM_SWEEP: execute command 'map $MapName'"
    $commandIndex = $text.LastIndexOf($commandMarker, [System.StringComparison]::OrdinalIgnoreCase)
    if ($commandIndex -ge 0) {
        return $text.Substring($commandIndex)
    }

    $loadMarker = "EF: CM_LoadMap raw BSP complete clientload=0 name='maps/$MapName.bsp'"
    $loadIndex = $text.LastIndexOf($loadMarker, [System.StringComparison]::OrdinalIgnoreCase)
    if ($loadIndex -ge 0) {
        return $text.Substring($loadIndex)
    }

    return Get-MapLogText $MapName
}

function Test-MapBspLoaded([string]$Text, [string]$MapName) {
    $escapedMap = [regex]::Escape($MapName)
    return ($Text -match "EF: CM_LoadMap raw BSP complete[^\r\n]*name='maps/$escapedMap\.bsp'") -or
        ($Text -match "STEFX: CM_LoadMap reusing loaded raw BSP 'maps/$escapedMap\.bsp'")
}

function Set-NextMap([string]$MapName) {
    $gametype = if ($MapName.StartsWith("ctf_", [System.StringComparison]::OrdinalIgnoreCase)) { 4 } else { 0 }
    $slotPath = Join-Path $gameRoot ("ef_mp_runtime_commands.{0}.txt" -f ($script:commandSlot % 4))
    $script:commandSlot++
    Remove-Item -LiteralPath (Get-CommandInboxPaths) -Force -ErrorAction SilentlyContinue
    @(
        "set g_gametype $gametype",
        "map $MapName"
    ) | Set-Content -LiteralPath $commandTemp -Encoding ASCII
    Move-Item -LiteralPath $commandTemp -Destination $slotPath -Force
}

function Wait-ForMapPlayable([string]$MapName) {
    $baselineHeartbeatKeys = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($match in [regex]::Matches((Get-LogText), $heartbeatPattern)) {
        [void]$baselineHeartbeatKeys.Add(($match.Groups[1].Value + ":" + $match.Groups[2].Value))
    }

    $deadline = (Get-Date).AddSeconds($MapLoadTimeoutSeconds)
    do {
        Assert-OwnedProcessAlive
        $wholeLog = Get-LogText
        $mapText = Get-MapLogText $MapName
        $transitionText = Get-MapTransitionLogText $MapName
        if ($wholeLog -match $fatalPattern) {
            throw "Fatal marker appeared while waiting for playable map $MapName."
        }

        $hasTargetBsp = Test-MapBspLoaded $transitionText $MapName
        $hasBot = ($mapText -match "STEFX_HM_SWEEP: bot active client=[1-9][0-9]*")
        $hasFreshHeartbeat = $false
        foreach ($match in [regex]::Matches($wholeLog, $heartbeatPattern)) {
            $heartbeatKey = $match.Groups[1].Value + ":" + $match.Groups[2].Value
            if (-not $baselineHeartbeatKeys.Contains($heartbeatKey)) {
                $hasFreshHeartbeat = $true
                break
            }
        }

        if ($hasTargetBsp -and $hasBot -and $hasFreshHeartbeat) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for playable marker for $MapName."
}

function Save-Results {
    $results.ToArray() | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultsJson -Encoding ASCII
    $results.ToArray() | Export-Csv -LiteralPath $resultsCsv -NoTypeInformation -Encoding ASCII
}

function Get-HeartbeatStats([string]$Text) {
    $matches = [regex]::Matches($Text, $heartbeatPattern)
    $fpsValues = New-Object System.Collections.Generic.List[double]
    $memoryValues = New-Object System.Collections.Generic.List[int]
    foreach ($match in $matches) {
        $fpsValues.Add(([double]$match.Groups[6].Value + ([double]$match.Groups[7].Value / 10.0)))
        $memoryValues.Add([int]$match.Groups[8].Value)
    }
    $perfValues = @($fpsValues.ToArray() | Select-Object -Skip 10)
    if ($perfValues.Count -eq 0) {
        $perfValues = @($fpsValues.ToArray())
    }
    $fpsAverage = if ($perfValues.Count -gt 0) {
        [Math]::Round((($perfValues | Measure-Object -Average).Average), 1)
    } else { 0.0 }
    $fpsMinimum = if ($perfValues.Count -gt 0) {
        [Math]::Round((($perfValues | Measure-Object -Minimum).Minimum), 1)
    } else { 0.0 }
    $lowFpsSamples = @($perfValues | Where-Object { $_ -lt $script:MinFpsSample }).Count
    [PSCustomObject]@{
        Count = $matches.Count
        FpsAverage = $fpsAverage
        FpsMinimum = $fpsMinimum
        LowFpsSamples = $lowFpsSamples
        MemoryStartKb = if ($memoryValues.Count -gt 0) { $memoryValues[0] } else { 0 }
        MemoryEndKb = if ($memoryValues.Count -gt 0) { $memoryValues[$memoryValues.Count - 1] } else { 0 }
        MemoryMinimumKb = if ($memoryValues.Count -gt 0) { ($memoryValues | Measure-Object -Minimum).Minimum } else { 0 }
    }
}

function Get-PatternCount([string]$Text, [string]$Pattern) {
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return 0
    }
    return [regex]::Matches($Text, $Pattern).Count
}

if (-not (Test-Path -LiteralPath $xbe -PathType Leaf)) {
    throw "Missing staged efmp.xbe: $xbe"
}
if (-not (Test-Path -LiteralPath $startCapture -PathType Leaf) -or
    -not (Test-Path -LiteralPath $requestCapture -PathType Leaf)) {
    throw "CXBX-CodexCapture scripts are missing from $captureRootFull"
}

$existing = @(Get-CxbxProcesses)
if ($DryRun) {
    [PSCustomObject][ordered]@{
        dryRun = $true
        gameRoot = $gameRoot
        captureRoot = $captureRootFull
        xbe = $xbe
        mapCount = $maps.Count
        maps = $maps
        holdSeconds = $HoldSeconds
        screenshotAtSeconds = $ScreenshotAtSeconds
        captureLoadingScreens = [bool]$CaptureLoadingScreens
        loadingScreenshotDelaySeconds = $LoadingScreenshotDelaySeconds
        loadingScreenshotCount = $LoadingScreenshotCount
        loadingScreenshotIntervalSeconds = $LoadingScreenshotIntervalSeconds
        minFpsAverage = $MinFpsAverage
        minFpsSample = $MinFpsSample
        maxLowFpsSamples = $MaxLowFpsSamples
        existingCxbxProcesses = $existing.Count
        existingCxbxProcessDetails = @($existing | ForEach-Object {
            [PSCustomObject][ordered]@{
                id = $_.Id
                processName = $_.ProcessName
                path = Get-ProcessPathSafe $_
                startTime = try { $_.StartTime.ToString("o") } catch { "" }
            }
        })
    } | ConvertTo-Json -Depth 5
    return
}

if ($existing.Count -gt 0) {
    $details = ($existing | ForEach-Object { "$($_.Id):$($_.ProcessName)" }) -join ", "
    throw "CXBX-R is already running; refusing to step on it: $details"
}

New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
foreach ($candidate in Get-LogCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $archiveName = ((Split-Path -Leaf $candidate) + ".before-$stamp")
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $resultRoot $archiveName) -Force
        Remove-Item -LiteralPath $candidate -Force
    }
}
Remove-Item -LiteralPath (Get-CommandInboxPaths) -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $smokeHarnessMarker, $smokePostMapCommands -Force -ErrorAction SilentlyContinue
if ($EnableSmokeInput) {
    Set-Content -LiteralPath $smokeHarnessMarker -Value "1" -Encoding ASCII
    @(
        "set stefx_smoke_input 1",
        "set stefx_smoke_input_start $SmokeInputStart",
        "set stefx_smoke_input_end $SmokeInputEnd",
        "set stefx_smoke_input_attack_start $SmokeAttackStart",
        "set stefx_smoke_input_attack_end $SmokeAttackEnd",
        "set stefx_smoke_input_forward $SmokeInputForward",
        "set stefx_smoke_input_side $SmokeInputSide",
        "set stefx_smoke_input_yaw $SmokeInputYaw",
        "set stefx_smoke_view_pitch $SmokeViewPitch",
        "set stefx_smoke_view_yaw $SmokeViewYaw",
        "set stefx_hm_smoke_combat_proof 1"
    ) | Set-Content -LiteralPath $smokePostMapCommands -Encoding ASCII
}

try {
    $launchTime = Get-Date
    $captureMetadata = & $startCapture -Xbe $xbe -Session $sessionName
    $primaryPid = [int]$captureMetadata.process_id
    $ownedIds.Add($primaryPid)
    $ownedProcessAtLaunch = Get-Process -Id $primaryPid -ErrorAction SilentlyContinue
    if ($null -ne $ownedProcessAtLaunch) {
        $primaryProcessName = [string]$ownedProcessAtLaunch.ProcessName
        $primaryProcessPath = Get-ProcessPathSafe $ownedProcessAtLaunch
        try {
            $primaryProcessStartTime = $ownedProcessAtLaunch.StartTime.ToString("o")
        } catch {
            $primaryProcessStartTime = ""
        }
    }
    Write-Host "CONTINUOUS_SESSION_STARTED pid=$primaryPid session=$sessionName maps=$($maps.Count) hold=${HoldSeconds}s"

    Start-Sleep -Seconds 2
    foreach ($process in Get-CxbxProcesses) {
        if (-not $ownedIds.Contains($process.Id) -and -not (Is-CaptureRootProcess $process)) {
            throw "A separate CXBX-R process appeared after launch; refusing to adopt it: $($process.Id):$($process.ProcessName)"
        }
    }
    [void](Resolve-OwnedCxbxProcess)

    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class StefxWindow {
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
}
"@
    $ownedProcess = Get-Process -Id $primaryPid -ErrorAction SilentlyContinue
    if ($null -ne $ownedProcess -and $ownedProcess.MainWindowHandle -ne 0) {
        [void][StefxWindow]::ShowWindowAsync($ownedProcess.MainWindowHandle, 6)
    }

    Wait-ForFreshLog
    Write-Host "LOG_READY path=$logPath"

    for ($mapIndex = 0; $mapIndex -lt $maps.Count; $mapIndex++) {
        $mapName = $maps[$mapIndex]
        $currentMapName = $mapName
        $loadingCapturePath = ""
        $loadingCaptureColors = 0
        $loadingCaptureError = ""
        $loadingCapturePaths = New-Object 'System.Collections.Generic.List[string]'
        $loadingCaptureColorValues = New-Object 'System.Collections.Generic.List[int]'
        $loadingCaptureErrors = New-Object 'System.Collections.Generic.List[string]'
        Set-NextMap $mapName

        if ($CaptureLoadingScreens) {
            Wait-ForMapTransitionStart $mapName
            Start-Sleep -Milliseconds ([Math]::Max(0, [int]($LoadingScreenshotDelaySeconds * 1000.0)))
            $loadingAttempts = [Math]::Max(1, $LoadingScreenshotCount)
            for ($loadingIndex = 1; $loadingIndex -le $loadingAttempts; $loadingIndex++) {
                try {
                    $loadingCapture = & $requestCapture -Session $sessionName -Name ("{0}-loading-{1:D2}" -f $mapName, $loadingIndex) -TimeoutSeconds 30
                    $thisLoadingPath = [string]$loadingCapture.ProofPath
                    $thisLoadingColors = [int]$loadingCapture.UniqueSampledColors
                    if ([string]::IsNullOrWhiteSpace($loadingCapturePath)) {
                        $loadingCapturePath = $thisLoadingPath
                        $loadingCaptureColors = $thisLoadingColors
                    }
                    $loadingCapturePaths.Add($thisLoadingPath)
                    $loadingCaptureColorValues.Add($thisLoadingColors)
                } catch {
                    $loadingCaptureErrors.Add(("capture {0}: {1}" -f $loadingIndex, $_.Exception.Message))
                }
                if ($loadingIndex -lt $loadingAttempts) {
                    Start-Sleep -Milliseconds ([Math]::Max(0, [int]($LoadingScreenshotIntervalSeconds * 1000.0)))
                }
            }
            if ($loadingCaptureErrors.Count -gt 0) {
                $loadingCaptureError = ($loadingCaptureErrors.ToArray() -join "; ")
            }
        }

        Wait-ForMapActive $mapName
        Wait-ForMapPlayable $mapName
        $mapStart = Get-Date
        $baselineHeartbeatKeys = New-Object 'System.Collections.Generic.HashSet[string]'
        $seenHeartbeatKeys = New-Object 'System.Collections.Generic.HashSet[string]'
        $heartbeatLines = New-Object 'System.Collections.Generic.List[string]'
        foreach ($match in [regex]::Matches((Get-LogText), $heartbeatPattern)) {
            [void]$baselineHeartbeatKeys.Add(($match.Groups[1].Value + ":" + $match.Groups[2].Value))
        }
        $lastHeartbeatCount = 0
        $lastHeartbeatAdvance = $mapStart
        $botActive = $false
        $fatal = $false
        $capturePath = ""
        $captureColors = 0
        $captureError = ""
        $captureRequested = $false
        Write-Host ("MAP_START {0}/{1} name={2} pid={3}" -f ($mapIndex + 1), $maps.Count, $mapName, $primaryPid)

        while (((Get-Date) - $mapStart).TotalSeconds -lt $HoldSeconds) {
            Assert-OwnedProcessAlive
            $wholeLog = Get-LogText
            $mapText = Get-MapLogText $mapName
            if ($wholeLog -match $fatalPattern) {
                $fatal = $true
                throw "Fatal/OOM marker appeared during ${mapName}: $($Matches[0])"
            }

            if ($mapText -match "STEFX_HM_SWEEP: bot active client=[1-9][0-9]*") {
                $botActive = $true
            }

            foreach ($match in [regex]::Matches($wholeLog, $heartbeatPattern)) {
                $heartbeatKey = $match.Groups[1].Value + ":" + $match.Groups[2].Value
                if (-not $baselineHeartbeatKeys.Contains($heartbeatKey) -and
                    $seenHeartbeatKeys.Add($heartbeatKey)) {
                    $heartbeatLines.Add($match.Value)
                }
            }

            $heartbeatCount = $seenHeartbeatKeys.Count
            if ($heartbeatCount -gt $lastHeartbeatCount) {
                $lastHeartbeatCount = $heartbeatCount
                $lastHeartbeatAdvance = Get-Date
            } elseif (((Get-Date) - $lastHeartbeatAdvance).TotalSeconds -gt 12) {
                throw "Heartbeat stalled for more than 12 seconds during $mapName."
            }

            $elapsed = ((Get-Date) - $mapStart).TotalSeconds
            if (-not $captureRequested -and $elapsed -ge $ScreenshotAtSeconds) {
                $captureRequested = $true
                try {
                    $capture = & $requestCapture -Session $sessionName -Name $mapName -TimeoutSeconds 30
                    $capturePath = [string]$capture.ProofPath
                    $captureColors = [int]$capture.UniqueSampledColors
                } catch {
                    $captureError = $_.Exception.Message
                }
            }
            Start-Sleep -Seconds 2
        }

        $stats = Get-HeartbeatStats ($heartbeatLines.ToArray() -join "`n")
        $mapText = Get-MapLogText $mapName
        $transitionText = Get-MapTransitionLogText $mapName
        $targetBspLoaded = Test-MapBspLoaded $transitionText $mapName
        $engineTransitionStarted = (
            (Test-TextContains $transitionText "JA: SV_Map_: requested '$mapName'") -or
            (Test-TextContains $transitionText "EF: SV_SpawnServer before CM_LoadMap map='$mapName'") -or
            (Test-TextContains $transitionText "EF: CM_LoadMap raw probe begin name='maps/$mapName.bsp'")
        )
        $ammoPickupCount = Get-PatternCount $mapText "STEFX_HM_AMMO: (pickup-ammo|weapon-pickup-post)|STEFX_HM_ITEM: weapon pickup"
        $fireWeaponCount = Get-PatternCount $mapText "STEFX_WEAPON: FireWeapon"
        $damageCount = Get-PatternCount $mapText "STEFX_HM_DAMAGE:"
        $localDamageCount = Get-PatternCount $mapText "STEFX_HM_DAMAGE: attacker=0 target=[1-9][0-9]*"
        $botDamageCount = Get-PatternCount $mapText "STEFX_HM_DAMAGE: attacker=[1-9][0-9]* target=0"
        $phaserClientTargetCount = Get-PatternCount $mapText "STEFX_WEAPON: PhaserTarget[^\r\n]*targetClient=1"
        $teleportResetCount = Get-PatternCount $mapText "STEFX_HM_PREDICTION: hosted teleport reset|STEFX_HM_TELEPORT:"
        $stepSlideCount = Get-PatternCount $mapText "STEFX_HM_STEP|STEFX_HM_SP: stepslide|accept-step|reject-steep-step|reject-down-trace-solid"
        $hostedFootstepSkipCount = Get-PatternCount $mapText "STEFX_HM_SOUND: skip hosted footstep"
        $frameRateIssue = ($stats.FpsAverage -lt $MinFpsAverage -or $stats.LowFpsSamples -gt $MaxLowFpsSamples)
        $memoryIssue = ($stats.MemoryMinimumKb -gt 0 -and $stats.MemoryMinimumKb -lt 4096)
        $captureIssue = ([string]::IsNullOrWhiteSpace($capturePath) -or $captureColors -le 8)
        $status = if ($fatal -or $stats.Count -lt [Math]::Max(30, $HoldSeconds - 15) -or
            -not $botActive -or $frameRateIssue -or $memoryIssue -or $captureIssue) { "FAIL" } else { "PASS" }

        $result = [PSCustomObject][ordered]@{
            order = $mapIndex + 1
            map = $mapName
            status = $status
            processId = $primaryPid
            startedAt = $mapStart.ToString("o")
            heldSeconds = [Math]::Round(((Get-Date) - $mapStart).TotalSeconds, 1)
            heartbeatCount = $stats.Count
            fpsAverage = $stats.FpsAverage
            fpsMinimum = $stats.FpsMinimum
            lowFpsSamples = $stats.LowFpsSamples
            memoryStartKb = $stats.MemoryStartKb
            memoryEndKb = $stats.MemoryEndKb
            memoryMinimumKb = $stats.MemoryMinimumKb
            engineTransitionStarted = $engineTransitionStarted
            targetBspLoaded = $targetBspLoaded
            botActive = $botActive
            fatalMarker = $fatal
            frameRateIssue = $frameRateIssue
            memoryIssue = $memoryIssue
            captureIssue = $captureIssue
            captureColors = $captureColors
            capturePath = $capturePath
            captureError = $captureError
            loadingCaptureColors = $loadingCaptureColors
            loadingCapturePath = $loadingCapturePath
            loadingCaptureError = $loadingCaptureError
            loadingCaptureCount = $loadingCapturePaths.Count
            loadingCapturePaths = ($loadingCapturePaths.ToArray() -join ";")
            loadingCaptureColorValues = (($loadingCaptureColorValues.ToArray() | ForEach-Object { [string]$_ }) -join ";")
            ammoPickupCount = $ammoPickupCount
            fireWeaponCount = $fireWeaponCount
            damageCount = $damageCount
            localDamageCount = $localDamageCount
            botDamageCount = $botDamageCount
            phaserClientTargetCount = $phaserClientTargetCount
            teleportResetCount = $teleportResetCount
            stepSlideCount = $stepSlideCount
            hostedFootstepSkipCount = $hostedFootstepSkipCount
        }
        $results.Add($result)
        Save-Results
        Write-Host ("MAP_RESULT {0}/{1} name={2} status={3} hb={4} fpsAvg={5} fpsMin={6} mem={7}->{8}KB bot={9} colors={10} fire={11} damage={12} localDamage={13} botDamage={14} phaserClient={15} ammo={16} teleport={17} step={18} footskip={19}" -f `
            ($mapIndex + 1), $maps.Count, $mapName, $status, $stats.Count, $stats.FpsAverage,
            $stats.FpsMinimum, $stats.MemoryStartKb, $stats.MemoryEndKb, $botActive, $captureColors,
            $fireWeaponCount, $damageCount, $localDamageCount, $botDamageCount, $phaserClientTargetCount,
            $ammoPickupCount, $teleportResetCount, $stepSlideCount,
            $hostedFootstepSkipCount)
    }
} catch {
    $sessionFailure = $_.Exception.Message
    $sessionFailureContext = Get-SweepFailureContext
    Write-Host "SESSION_FAILURE $sessionFailure"
} finally {
    if (-not [string]::IsNullOrWhiteSpace($logPath) -and (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        Copy-Item -LiteralPath $logPath -Destination $finalLog -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $commandInbox, $commandTemp -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $smokeHarnessMarker, $smokePostMapCommands -Force -ErrorAction SilentlyContinue

    foreach ($process in Get-CxbxProcesses) {
        if ($ownedIds.Contains($process.Id)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }

    $sessionEnd = Get-Date
    $summary = [PSCustomObject][ordered]@{
        session = $sessionName
        processId = $primaryPid
        continuousSession = $true
        requestedMaps = $maps.Count
        completedMaps = $results.Count
        passedMaps = @($results | Where-Object { $_.status -eq "PASS" }).Count
        failedMaps = @($results | Where-Object { $_.status -ne "PASS" }).Count
        holdSecondsPerMap = $HoldSeconds
        launchedAt = if ($null -ne $launchTime) { $launchTime.ToString("o") } else { "" }
        endedAt = $sessionEnd.ToString("o")
        elapsedSeconds = if ($null -ne $launchTime) { [Math]::Round(($sessionEnd - $launchTime).TotalSeconds, 1) } else { 0 }
        sessionFailure = $sessionFailure
        sessionFailureContext = $sessionFailureContext
        ownedProcessName = $primaryProcessName
        ownedProcessPath = $primaryProcessPath
        ownedProcessStartTime = $primaryProcessStartTime
        logPath = $finalLog
        resultPath = $resultsJson
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryJson -Encoding ASCII
    Save-Results
    Write-Host "CONTINUOUS_SESSION_ENDED completed=$($results.Count)/$($maps.Count) passed=$(@($results | Where-Object { $_.status -eq 'PASS' }).Count) failure='$sessionFailure' results=$resultRoot"
}

if (-not [string]::IsNullOrWhiteSpace($sessionFailure) -or $results.Count -ne $maps.Count -or
    @($results | Where-Object { $_.status -ne "PASS" }).Count -gt 0) {
    exit 1
}
exit 0
