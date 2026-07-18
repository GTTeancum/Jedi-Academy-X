param(
    [string]$Game = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X",
    [string]$Cxbx = "C:\Games\Emulators\CXBX",
    [string]$LoaderName = "cxbxr-ldr.exe",
    [int]$WatchdogSeconds = 75,
    [int]$MinimumRunSeconds = 35,
    [int]$MaxLogStaleSeconds = 45,
    [switch]$KeepAlive,
    [switch]$Visible,
    [switch]$NoSmokeProof,
    [string]$CaptureOutput = "",
    [int]$CaptureTimeoutSec = 45
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Ensure-Directory([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Container)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Get-CxbxProcesses {
    @(Get-Process cxbx* -ErrorAction SilentlyContinue)
}

function Get-ProcessPathSafe($Process) {
    try {
        return $Process.Path
    } catch {
        return ""
    }
}

function Get-NewCxbxProcesses([int[]]$BeforeIds, [string]$CxbxRoot) {
    $root = Resolve-FullPath $CxbxRoot
    Get-CxbxProcesses | Where-Object {
        $BeforeIds -notcontains $_.Id -and
        ((Get-ProcessPathSafe $_) -eq "" -or (Resolve-FullPath (Get-ProcessPathSafe $_)).StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase))
    }
}

function Stop-NewCxbxProcesses([int[]]$BeforeIds, [string]$CxbxRoot) {
    Get-NewCxbxProcesses $BeforeIds $CxbxRoot | ForEach-Object {
        try {
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        } catch {
        }
    }
}

function Get-LogCandidates([string]$GameRoot, [string]$CxbxRoot) {
    @(
        (Join-Path $GameRoot "ef_mp_log.txt"),
        (Join-Path $GameRoot "BaseEF\ef_mp_log.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition1\ef_mp_log.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition2\ef_mp_log.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition3\ef_mp_log.txt"),
        (Join-Path $CxbxRoot "ef_mp_log.txt")
    )
}

function Get-PhaseCandidates([string]$GameRoot, [string]$CxbxRoot) {
    @(
        (Join-Path $GameRoot "ef_mp_phase.txt"),
        (Join-Path $GameRoot "BaseEF\ef_mp_phase.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition1\ef_mp_phase.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition2\ef_mp_phase.txt"),
        (Join-Path $CxbxRoot "EmuDisk\Partition3\ef_mp_phase.txt"),
        (Join-Path $CxbxRoot "ef_mp_phase.txt")
    )
}

function Get-FreshLog([string[]]$Candidates, [datetime]$StartTime) {
    $Candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { Get-Item -LiteralPath $_ } |
        Where-Object { $_.LastWriteTime -ge $StartTime.AddSeconds(-1) } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Set-RuntimeFlag([string[]]$Roots, [string]$Leaf, [string]$Value) {
    foreach ($root in $Roots) {
        try {
            Ensure-Directory $root
            Set-Content -LiteralPath (Join-Path $root $Leaf) -Value $Value -Encoding ASCII
        } catch {
        }
    }
}

function Remove-RuntimeFlag([string[]]$Roots, [string]$Leaf) {
    foreach ($root in $Roots) {
        Remove-Item -LiteralPath (Join-Path $root $Leaf) -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-RendererCapture(
    [string]$ScriptRoot,
    [string]$GameRoot,
    [string]$CxbxRoot,
    [string]$CaptureOutputPath,
    [int]$TimeoutSec
) {
    $captureScript = Join-Path $ScriptRoot "capture_cxbx_mp_renderer.ps1"
    $captureFullPath = Resolve-FullPath $CaptureOutputPath
    try {
        $lines = @(
            & powershell -ExecutionPolicy Bypass -File $captureScript `
                -Game $GameRoot `
                -Cxbx $CxbxRoot `
                -Output $captureFullPath `
                -TimeoutSec $TimeoutSec 2>&1
        )
        return @{
            ExitCode = $LASTEXITCODE
            Lines = $lines
        }
    } catch {
        return @{
            ExitCode = 99
            Lines = @($_.Exception.Message)
        }
    }
}

$repoRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..")
$gameRoot = Resolve-FullPath $Game
$cxbxRoot = Resolve-FullPath $Cxbx
$loader = Join-Path $cxbxRoot $LoaderName
$xbe = Join-Path $gameRoot "efmp.xbe"
$outputDir = Join-Path $repoRoot "scripts\output"
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutPath = Join-Path $outputDir "cxbx_mp_log_$stamp.stdout.txt"
$stderrPath = Join-Path $outputDir "cxbx_mp_log_$stamp.stderr.txt"
$summaryPath = Join-Path $outputDir "cxbx_mp_log_$stamp.summary.txt"

Ensure-Directory $outputDir

if (!(Test-Path -LiteralPath $loader -PathType Leaf)) {
    throw "Missing CXBX-R loader: $loader"
}
if (!(Test-Path -LiteralPath $xbe -PathType Leaf)) {
    throw "Missing staged efmp.xbe: $xbe"
}

$existing = Get-CxbxProcesses
if ($existing.Count -gt 0) {
    $details = ($existing | ForEach-Object { "$($_.Id):$($_.ProcessName)" }) -join ", "
    throw "CXBX-R is already running; refusing to launch or close someone else's instance. Processes: $details"
}

$runtimeRoots = @(
    $gameRoot,
    (Join-Path $gameRoot "BaseEF"),
    (Join-Path $cxbxRoot "EmuDisk\Partition1"),
    (Join-Path $cxbxRoot "EmuDisk\Partition2"),
    (Join-Path $cxbxRoot "EmuDisk\Partition3"),
    $cxbxRoot
)

$logCandidates = Get-LogCandidates $gameRoot $cxbxRoot
$phaseCandidates = Get-PhaseCandidates $gameRoot $cxbxRoot
foreach ($log in @($logCandidates + $phaseCandidates)) {
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        $safeLeaf = ($log -replace "[:\\]", "_")
        Copy-Item -LiteralPath $log -Destination (Join-Path $outputDir "cxbx_mp_log_${stamp}_before_$safeLeaf") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
    }
}

Remove-RuntimeFlag $runtimeRoots "ef_mp_cxbx_present_throttle.txt"
if (!$NoSmokeProof) {
    Set-RuntimeFlag $runtimeRoots "ef_mp_smoke_proof.txt" "1"
} else {
    Remove-RuntimeFlag $runtimeRoots "ef_mp_smoke_proof.txt"
}

$beforeIds = @(Get-CxbxProcesses | Select-Object -ExpandProperty Id)
$launchTime = Get-Date
$windowStyle = if ($Visible) { "Normal" } else { "Hidden" }
$args = "/load `"$xbe`""

$process = $null
$logStale = $false
$captureExitCode = $null
$captureLines = @()
$captureStarted = $false
$lastLogProgressTime = $null
$lastLogWriteTime = $null
$lastLogLength = -1
$lastPhaseWriteTime = $null
$lastPhaseLength = -1
$patterns = @(
    @{ Name = "Runtime marker"; Pattern = "STEFX_HM: efmp\.xbe runtime log sink path=" },
    @{ Name = "hm_borg1 running"; Pattern = "STEFX_HM: direct Holomatch map is running map='hm_borg1'" },
    @{ Name = "Shader manifest"; Pattern = "STEFX_HM: renderer loaded shader manifest" },
    @{ Name = "SP screen texture"; Pattern = "STEFX_HM: renderer using SP-style GL_RGBA screen texture; legacy MP GL_LIN_RGBA8 path disabled" },
    @{ Name = "Stretch prime skipped"; Pattern = "STEFX_HM: renderer skipped inherited zero-size registration StretchPic prime" },
    @{ Name = "Local client active"; Pattern = "STEFX_HM: direct Holomatch local client is active" },
    @{ Name = "Borg bot 1"; Pattern = "STEFX_HM: addbot accepted name='1_of_12'" },
    @{ Name = "Borg bot 2"; Pattern = "STEFX_HM: addbot accepted name='2_of_3'" },
    @{ Name = "Direct combat spawn override"; Pattern = "STEFX_HM: direct Holomatch combat spawn override client=[12]" },
    @{ Name = "Direct combat bot command"; Pattern = "STEFX_HM: direct Holomatch combat bot command client=[12]" },
    @{ Name = "HUD draw active"; Pattern = "STEFX_HM: EF SP interface HUD draw active" },
    @{ Name = "Match heartbeat"; Pattern = "STEFX_HM: match heartbeat .*active=[1-9]" },
    @{ Name = "Combat weapon path"; Pattern = "(STEFX_HM: server EF Phaser applied damage attacker=|STEFX_HM: server EF fire bridge weapon=|STEFX_HM: server emitted EF trace impact event weapon=|STEFX_HM: cgame EF missile impact feedback end event=|STEFX_HM: server Holomatch missile impact used EF normal damage weapon=)" },
    @{ Name = "Score update"; Pattern = "STEFX_HM: score update client=" },
    @{ Name = "Death scored"; Pattern = "STEFX_HM: player death scored" },
    @{ Name = "Respawn loop"; Pattern = "STEFX_HM: respawn used EF direct path" }
)
$requiredNames = @(
    "Runtime marker",
    "hm_borg1 running",
    "Shader manifest",
    "SP screen texture",
    "Stretch prime skipped",
    "Local client active",
    "Borg bot 1",
    "Borg bot 2",
    "Direct combat spawn override",
    "Direct combat bot command",
    "HUD draw active",
    "Match heartbeat",
    "Combat weapon path",
    "Score update",
    "Death scored",
    "Respawn loop"
)
$seen = @{}
foreach ($pattern in $patterns) {
    $seen[$pattern.Name] = $false
}

try {
    $process = Start-Process `
        -FilePath $loader `
        -ArgumentList $args `
        -WorkingDirectory $cxbxRoot `
        -PassThru `
        -WindowStyle $windowStyle `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath

    $deadline = (Get-Date).AddSeconds($WatchdogSeconds)
    $minimumDeadline = (Get-Date).AddSeconds($MinimumRunSeconds)
    $freshLog = $null
    $allCoreSeen = $false

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1

        $freshLog = Get-FreshLog $logCandidates $launchTime
        if ($freshLog) {
            if ($lastLogWriteTime -ne $freshLog.LastWriteTime -or $lastLogLength -ne $freshLog.Length) {
                $lastLogProgressTime = Get-Date
                $lastLogWriteTime = $freshLog.LastWriteTime
                $lastLogLength = $freshLog.Length
            }

            $text = Get-Content -LiteralPath $freshLog.FullName -Raw -ErrorAction SilentlyContinue
            foreach ($pattern in $patterns) {
                if (!$seen[$pattern.Name] -and $text -match $pattern.Pattern) {
                    $seen[$pattern.Name] = $true
                }
            }

            if ($CaptureOutput -and !$captureStarted -and $seen["HUD draw active"]) {
                $captureStarted = $true
                $captureResult = Invoke-RendererCapture $PSScriptRoot $gameRoot $cxbxRoot $CaptureOutput $CaptureTimeoutSec
                $captureExitCode = $captureResult.ExitCode
                $captureLines = @($captureResult.Lines)
            }

            $allCoreSeen = $true
            foreach ($requiredName in $requiredNames) {
                if (!$seen[$requiredName]) {
                    $allCoreSeen = $false
                    break
                }
            }

            if ($allCoreSeen -and (Get-Date) -ge $minimumDeadline) {
                break
            }
        }

        $freshPhase = Get-FreshLog $phaseCandidates $launchTime
        if ($freshPhase) {
            if ($lastPhaseWriteTime -ne $freshPhase.LastWriteTime -or $lastPhaseLength -ne $freshPhase.Length) {
                $lastLogProgressTime = Get-Date
                $lastPhaseWriteTime = $freshPhase.LastWriteTime
                $lastPhaseLength = $freshPhase.Length
            }
        }

        if ($MaxLogStaleSeconds -gt 0 -and $lastLogProgressTime -and (Get-Date) -ge $launchTime.AddSeconds($MaxLogStaleSeconds)) {
            $staleFor = ((Get-Date) - $lastLogProgressTime).TotalSeconds
            if ($staleFor -ge $MaxLogStaleSeconds) {
                $logStale = $true
                break
            }
        }

        $newProcesses = @(Get-NewCxbxProcesses $beforeIds $cxbxRoot)
        if ($process.HasExited -and $newProcesses.Count -eq 0) {
            break
        }
    }

    if ($CaptureOutput -and !$captureStarted) {
        $captureStarted = $true
        $captureResult = Invoke-RendererCapture $PSScriptRoot $gameRoot $cxbxRoot $CaptureOutput $CaptureTimeoutSec
        $captureExitCode = $captureResult.ExitCode
        $captureLines = @($captureResult.Lines)
    }
} finally {
    if (!$KeepAlive) {
        Stop-NewCxbxProcesses $beforeIds $cxbxRoot
        Start-Sleep -Seconds 1
    }
    if (!$KeepAlive) {
        Remove-RuntimeFlag $runtimeRoots "ef_mp_cxbx_present_throttle.txt"
        Remove-RuntimeFlag $runtimeRoots "ef_mp_smoke_proof.txt"
    }
}

$freshLog = Get-FreshLog $logCandidates $launchTime
$freshPhase = Get-FreshLog $phaseCandidates $launchTime
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("CXBX-R MP Holomatch log smoke")
$lines.Add("started=$launchTime")
$lines.Add("xbe=$xbe")
$lines.Add("loader=$loader")
$lines.Add("windowStyle=$windowStyle")
$lines.Add("smokeProof=$($(if ($NoSmokeProof) { 'disabled' } else { 'enabled' }))")
$lines.Add("maxLogStaleSeconds=$MaxLogStaleSeconds")
if ($CaptureOutput) {
    $lines.Add("captureOutput=$(Resolve-FullPath $CaptureOutput)")
    $lines.Add("captureExitCode=$captureExitCode")
    foreach ($captureLine in $captureLines) {
        $lines.Add("capture: $captureLine")
    }
}
if ($freshLog) {
    $lines.Add("log=$($freshLog.FullName)")
    $lines.Add("logTimestamp=$($freshLog.LastWriteTime)")
    $lines.Add("logBytes=$($freshLog.Length)")
} else {
    $lines.Add("log=<missing fresh log>")
}
if ($freshPhase) {
    $lines.Add("phase=$($freshPhase.FullName)")
    $lines.Add("phaseTimestamp=$($freshPhase.LastWriteTime)")
    $phaseText = Get-Content -LiteralPath $freshPhase.FullName -Raw -ErrorAction SilentlyContinue
    if ($phaseText) {
        $lines.Add("phaseLast=$($phaseText.Trim())")
    }
} else {
    $lines.Add("phase=<missing fresh phase>")
}
foreach ($pattern in $patterns) {
    $state = if ($seen[$pattern.Name]) { "OK" } else { "MISS" }
    $lines.Add("$state $($pattern.Name)")
}

$missing = @($requiredNames | Where-Object { !$seen[$_] })
$optionalMissing = @($patterns | Where-Object { !$seen[$_.Name] -and $requiredNames -notcontains $_.Name } | ForEach-Object { $_.Name })
if ($missing.Count -gt 0) {
    $lines.Add("missing=$($missing -join ', ')")
}
if ($optionalMissing.Count -gt 0) {
    $lines.Add("optionalMissing=$($optionalMissing -join ', ')")
}
if ($logStale) {
    $staleSeconds = if ($lastLogProgressTime) { [int](((Get-Date) - $lastLogProgressTime).TotalSeconds) } else { -1 }
    $lines.Add("logStale=true staleSeconds=$staleSeconds")
}
if ($process -and $process.HasExited) {
    $lines.Add("loaderExitCode=$($process.ExitCode)")
}

Set-Content -LiteralPath $summaryPath -Value $lines -Encoding ASCII
$lines | ForEach-Object { Write-Host $_ }

if (!$freshLog) {
    exit 2
}
if ($logStale) {
    exit 3
}
if ($missing.Count -gt 0) {
    exit 1
}
if ($CaptureOutput -and $captureExitCode -ne 0) {
    exit 4
}
exit 0
