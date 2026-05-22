param(
    [string]$Repo = "C:\Programming\GitHub\Jedi-Academy-X",
    [string]$Cxbx = "C:\Programming\GitHub\Jedi-Academy-X\CXBXR",
    [string]$Game = "C:\Games\Emulators\CXBX\Jedi Academy rebuild",
    [string]$Junction = "C:\Games\Emulators\CXBX\Jedi Academy rebuild",
    [string]$LoaderName = "cxbxr-ldr-project2.exe",
    [string]$Level = "",
    [int]$WatchdogSeconds = 300,
    [int]$ActiveSeconds = 0,
    [int]$InitialQuietGraceSeconds = 180,
    [int]$QuietGraceSeconds = 20,
    [switch]$NoCopy
)

$ErrorActionPreference = "Stop"

function Get-CxbxProcesses {
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -in @("cxbx-project2.exe", "cxbxr-ldr-project2.exe")
        }
}

function Stop-CxbxProcesses {
    Get-CxbxProcesses | ForEach-Object {
        try {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        } catch {
        }
    }
}

function Get-LogPath {
    $candidates = @(
        (Join-Path $Game "ja_sp_log.txt"),
        (Join-Path $Junction "ja_sp_log.txt"),
        (Join-Path $Cxbx "EmuDisk\Partition1\ja_sp_log.txt")
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

function Has-Match([string]$Path, [string]$Pattern) {
    if (!(Test-Path $Path)) {
        return $false
    }

    return [bool](Select-String -Path $Path -Pattern $Pattern -Quiet -ErrorAction SilentlyContinue)
}

function Get-DebugFileCandidates {
    @(
        (Join-Path $Game "CxbxDebug.txt"),
        (Join-Path $Game "KrnlDebug.txt"),
        (Join-Path $Repo "code\x_exe\Release\CxbxDebug.txt"),
        (Join-Path $Repo "code\x_exe\Release\KrnlDebug.txt"),
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

$outDir = Join-Path $Repo "scripts\output"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutPath = Join-Path $outDir "cxbx_sp_${stamp}.stdout.txt"
$stderrPath = Join-Path $outDir "cxbx_sp_${stamp}.stderr.txt"
$summaryPath = Join-Path $outDir "cxbx_sp_${stamp}.summary.txt"

Stop-CxbxProcesses
Start-Sleep -Seconds 2

if (!$NoCopy) {
    Copy-Item (Join-Path $Repo "code\x_exe\Release\default.xbe") (Join-Path $Game "default.xbe") -Force
}

if ($Level) {
    Set-Content -Path (Join-Path $Game "ja_sp_level.txt") -Value $Level -Encoding ASCII
}

@(
    (Join-Path $Game "ja_sp_log.txt"),
    (Join-Path $Junction "ja_sp_log.txt"),
    (Join-Path $Cxbx "EmuDisk\Partition1\ja_sp_log.txt")
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

$p = Start-Process `
    -FilePath $loader `
    -ArgumentList $args `
    -WorkingDirectory $Cxbx `
    -PassThru `
    -WindowStyle Hidden `
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

    $cxbxAlive = [bool](Get-CxbxProcesses)
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

        $heartbeat = Get-HeartbeatInfo $log
        if ($heartbeat.LastCompletedFrame -ne $null) {
            if ($lastHeartbeatFrame -eq $null -or $heartbeat.LastCompletedFrame -ne $lastHeartbeatFrame) {
                $lastHeartbeatFrame = $heartbeat.LastCompletedFrame
                $lastHeartbeatChange = Get-Date
            }
            if ($ActiveSeconds -gt 0 -and $heartbeat.FirstRealtime -ne $null -and $heartbeat.LastRealtime -ne $null) {
                $activeElapsedSeconds = ($heartbeat.LastRealtime - $heartbeat.FirstRealtime) / 1000.0
                if ($activeElapsedSeconds -ge $ActiveSeconds) {
                    $activeSecondsReached = $true
                    break
                }
            }
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
        $quietLimit = if ($activeSeen) { $QuietGraceSeconds } else { $InitialQuietGraceSeconds }
        if (((Get-Date) - $lastOutputChange).TotalSeconds -ge $quietLimit) {
        $silentCrashSuspected = $true
        break
        }
    }

    if ($activeSeen -and $lastHeartbeatFrame -ne $null -and (((Get-Date) - $lastHeartbeatChange).TotalSeconds -ge $QuietGraceSeconds)) {
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

$aliveAtEnd = [bool](Get-CxbxProcesses)
$loaderExitCode = $null
if ($p.HasExited) {
    $loaderExitCode = $p.ExitCode
}
Stop-CxbxProcesses
Start-Sleep -Seconds 1

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

$active = Has-Match $logPath "cls.state = CA_ACTIVE - GAME IS RUNNING"
$returnedFrames = Count-Matches $logPath "CG_DRAW_ACTIVE_FRAME\) returned|CL_CGameRendering: VM_Call\(CG_DRAW_ACTIVE_FRAME\) returned"
$heartbeatCount = Count-Matches $logPath "JA: FRAME_HEARTBEAT completedFrame="
$finalHeartbeat = Get-HeartbeatInfo $logPath
$activeElapsedSecondsFinal = 0
if ($finalHeartbeat.FirstRealtime -ne $null -and $finalHeartbeat.LastRealtime -ne $null) {
    $activeElapsedSecondsFinal = [math]::Round(($finalHeartbeat.LastRealtime - $finalHeartbeat.FirstRealtime) / 1000.0, 1)
}
$failureCount = Count-Matches $logPath "texture allocation failures"
$fileFatalCount = Count-Matches $logPath "Out of memory|Received Exception|FATAL|Z_Malloc\(\): Out of memory|EIP"
$consoleFatalCount = @([regex]::Matches($consoleCombined, "Received Exception|FATAL: X86|EIP :=|unhandled exception")).Count
$fatalCount = $fileFatalCount + $consoleFatalCount

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
    $status = "FAIL_NOT_ACTIVE"
} elseif ($heartbeatCount -lt 5) {
    $status = "FAIL_HEARTBEAT_INSUFFICIENT"
} elseif ($ActiveSeconds -gt 0 -and !$activeSecondsReached) {
    $status = "FAIL_ACTIVE_SECONDS_INSUFFICIENT"
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
    "returnedFrames=$returnedFrames",
    "heartbeatCount=$heartbeatCount",
    "activeElapsedSeconds=$activeElapsedSecondsFinal",
    "targetActiveSeconds=$ActiveSeconds",
    "activeSecondsReached=$activeSecondsReached",
    "lastHeartbeatFrame=$($finalHeartbeat.LastCompletedFrame)",
    "lastHeartbeatRealtime=$($finalHeartbeat.LastRealtime)",
    "lastHeartbeatServerTime=$($finalHeartbeat.LastServerTime)",
    "failureCount=$failureCount",
    "fileFatalCount=$fileFatalCount",
    "consoleFatalCount=$consoleFatalCount",
    "fatalCount=$fatalCount",
    "logPath=$logPath",
    "stdoutPath=$stdoutPath",
    "stderrPath=$stderrPath",
    "debugCopies=$($debugCopies -join ';')",
    "",
    "=== file log tail ==="
) + $tail + @(
    "",
    "=== emulator console tail ==="
) + $consoleTail

$summary | Set-Content -Path $summaryPath -Encoding ASCII
$summary

if ($status -eq "PASS") {
    exit 0
}

exit 1
