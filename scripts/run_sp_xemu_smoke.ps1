param(
    [string]$Map = "yavin1b",
    [string[]]$Maps = @(),
    [string[]]$Command = @(),
    [string[]]$SoakCommand = @(),
    [string[]]$Button = @(),
    [switch]$DefaultStartButtons,
    [switch]$NormalBoot,
    [switch]$Headless,
    [switch]$Repack,
    [switch]$RepackOnly,
    [switch]$Build,
    [int]$Duration = 90,
    [int]$Interval = 10,
    [string]$Name = "",
    [string]$Iso = "",
    [string]$Hdd = "C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2",
    [string]$XemuExe = "",
    [string]$ConfigPath = "",
    [string]$Port = "4460",
    [switch]$ExplicitXboxMachine,
    [string[]]$DumpMem = @(),
    [string[]]$DumpPhys = @(),
    [string]$WatchCr2 = "",
    [switch]$PollXBlog,
    [switch]$PollXBlogLite,
    [string]$PollXBlogAddr = "",
    [string]$PollXBlogPhysDelta = "",
    [switch]$VideoDebug,
    [switch]$MonitorScreenshots,
    [switch]$XemuNativeScreenshots,
    [int]$ScreenshotStartDelay = 0,
    [switch]$NoWindowFallback,
    [switch]$NoScreenshots,
    [switch]$SampleRegisters,
    [switch]$FailOnFrozen,
    [switch]$LeaveRunningOnFrozen,
    [switch]$SkipFinalDumps,
    [switch]$KeepIso,
    [switch]$SharedSnapshotHdd,
    [string]$XemuDebug = "",
    [string]$XemuDebugLog = "",
    [string[]]$XemuArg = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputDir = Join-Path $repoRoot "scripts\output"
$defaultIso = Join-Path $repoRoot "build\xemu\JediAcademyX_SP_direct.iso"
$stageDir = Join-Path $repoRoot "build\xemu\sp_direct_stage"
if ($NormalBoot) {
    $defaultIso = Join-Path $repoRoot "build\xemu\JediAcademyX_SP_normal.iso"
    $stageDir = Join-Path $repoRoot "build\xemu\sp_normal_stage"
}
$stageXbe = Join-Path $stageDir "default.xbe"
$builtXbe = Join-Path $repoRoot "code\x_exe\Release\default.xbe"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

function Remove-GeneratedIso {
    param([string]$Path)

    if ($KeepIso -or [string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    $buildXemuRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build\xemu"))
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($buildXemuRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return
    }

    Remove-Item -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue
}

function Initialize-StageFromSource {
    if (-not $Repack) {
        return
    }

    $sourceDir = Join-Path $repoRoot "Star Wars Jedi Academy game"
    if (-not (Test-Path $sourceDir)) {
        throw "Normal boot asset source not found: $sourceDir"
    }

    $buildXemuRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build\xemu"))
    $fullStageDir = [System.IO.Path]::GetFullPath($stageDir)
    if (-not $fullStageDir.StartsWith($buildXemuRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to recreate stage outside build\\xemu: $fullStageDir"
    }

    if (Test-Path $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

    $sourceRoot = [System.IO.Path]::GetFullPath($sourceDir)
    $sourceDefaultXbe = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot "default.xbe"))

    Get-ChildItem -LiteralPath $sourceRoot -Directory -Recurse -Force | ForEach-Object {
        $relativePath = $_.FullName.Substring($sourceRoot.Length).TrimStart('\')
        if (($_.Name -ne "__pycache__") -and ($relativePath -notlike "*\__pycache__*")) {
            New-Item -ItemType Directory -Path (Join-Path $stageDir $relativePath) -Force | Out-Null
        }
    }

    Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force | ForEach-Object {
        $sourcePath = [System.IO.Path]::GetFullPath($_.FullName)
        $relativePath = $sourcePath.Substring($sourceRoot.Length).TrimStart('\')
        if (($relativePath -notlike "*\__pycache__*") -and
            (-not $sourcePath.Equals($sourceDefaultXbe, [System.StringComparison]::OrdinalIgnoreCase))) {
            $destPath = Join-Path $stageDir $relativePath
            $destParent = Split-Path -Parent $destPath
            if (-not (Test-Path $destParent)) {
                New-Item -ItemType Directory -Path $destParent -Force | Out-Null
            }
            New-Item -ItemType HardLink -Path $destPath -Target $sourcePath | Out-Null
        }
    }
}

if ([string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = $defaultIso
}

$normalizedMaps = @()
if ($NormalBoot) {
    $Maps = @("__normal_boot__")
}
elseif ($Maps.Count -eq 0) {
    $Maps = @($Map)
}
foreach ($entry in $Maps) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedMaps += $trimmed
        }
    }
}
$Maps = $normalizedMaps

$normalizedDumps = @()
foreach ($entry in $DumpMem) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedDumps += $trimmed
        }
    }
}
$DumpMem = $normalizedDumps

$normalizedPhysDumps = @()
foreach ($entry in $DumpPhys) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedPhysDumps += $trimmed
        }
    }
}
$DumpPhys = $normalizedPhysDumps

$normalizedSoakCommands = @()
foreach ($entry in $SoakCommand) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedSoakCommands += $trimmed
        }
    }
}
$SoakCommand = $normalizedSoakCommands

$normalizedButtons = @()
foreach ($entry in $Button) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedButtons += $trimmed
        }
    }
}
$Button = $normalizedButtons

if ($DefaultStartButtons -and $Button.Count -eq 0) {
    $Button = @(
        "ui+500 a 220 0",
        "ui+2500 a 220 0",
        "ui+4500 a 220 0",
        "ui+6500 a 220 0",
        "ui+8500 a 220 0",
        "ui+10500 a 220 0"
    )
}

if ($Build) {
    $stamp = Get-Date -Format yyyyMMdd_HHmmss
    $buildLog = Join-Path $outputDir "build_sp_smoke_$stamp.log"
    Push-Location $repoRoot
    try {
        cmd /c "powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp > `"$buildLog`" 2>&1"
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog -Tail 100
            throw "SP build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    Write-Host "SP build log: $buildLog"
    $Repack = $true
}
if ($RepackOnly) {
    $Repack = $true
}

if (-not (Test-Path $extractXiso)) {
    throw "extract-xiso not found: $extractXiso"
}
Initialize-StageFromSource
$stageAvailable = Test-Path $stageDir
$stageRequired = $Repack -or (-not $NormalBoot) -or ($Command.Count -gt 0) -or ($SoakCommand.Count -gt 0) -or ($Button.Count -gt 0)
if (-not $stageAvailable -and $stageRequired) {
    throw "SP stage directory not found: $stageDir"
}

$results = @()
foreach ($mapName in $Maps) {
    if ([string]::IsNullOrWhiteSpace($mapName)) {
        continue
    }

    $isNormalRun = $NormalBoot -or $mapName -eq "__normal_boot__"
    $safeMap = if ($isNormalRun) { "normal" } else { ($mapName -replace '[^A-Za-z0-9_.-]', '_') }
    $runName = if ([string]::IsNullOrWhiteSpace($Name)) { "ja_sp_$safeMap" } else { "${Name}_$safeMap" }

    if ($stageAvailable) {
        $levelPath = Join-Path $stageDir "ja_sp_level.txt"
        $buttonPath = Join-Path $stageDir "ja_sp_buttons.txt"
        $autosmokePath = Join-Path $stageDir "ja_sp_autosmoke.txt"
        Remove-Item -LiteralPath $autosmokePath -Force -ErrorAction SilentlyContinue
        if ($DefaultStartButtons) {
            Set-Content -LiteralPath $autosmokePath -Value "1" -Encoding ASCII
        }
        if ($isNormalRun) {
            Remove-Item -LiteralPath $levelPath -Force -ErrorAction SilentlyContinue
        }
        else {
            Set-Content -LiteralPath $levelPath -Value $mapName -Encoding ASCII
        }
        if ($Command.Count -gt 0) {
            Set-Content -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Value $Command -Encoding ASCII
        }
        else {
            Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Force -ErrorAction SilentlyContinue
        }
        if ($SoakCommand.Count -gt 0) {
            Set-Content -LiteralPath (Join-Path $stageDir "ja_sp_soak_commands.txt") -Value $SoakCommand -Encoding ASCII
        }
        else {
            Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_soak_commands.txt") -Force -ErrorAction SilentlyContinue
        }
        if ($Button.Count -gt 0) {
            Set-Content -LiteralPath $buttonPath -Value $Button -Encoding ASCII
        }
        else {
            Remove-Item -LiteralPath $buttonPath -Force -ErrorAction SilentlyContinue
        }
    }

    if ($Repack) {
        if (-not (Test-Path $builtXbe)) {
            throw "Built XBE not found: $builtXbe"
        }
        Remove-Item -LiteralPath $stageXbe -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $builtXbe -Destination $stageXbe -Force

        $stamp = Get-Date -Format yyyyMMdd_HHmmss
        $repackLog = Join-Path $outputDir "repack_sp_${safeMap}_$stamp.log"
        $repackComplete = $false
        Push-Location (Join-Path $repoRoot "build\xemu")
        try {
            $isoName = [System.IO.Path]::GetFileName($Iso)
            $stageName = [System.IO.Path]::GetFileName($stageDir)
            Remove-Item -LiteralPath $isoName -Force -ErrorAction SilentlyContinue
            & $extractXiso -c $stageName $isoName *> $repackLog
            if ($LASTEXITCODE -ne 0) {
                Get-Content -LiteralPath $repackLog -Tail 80
                throw "extract-xiso failed with exit code $LASTEXITCODE"
            }
            $repackComplete = $true
        }
        finally {
            Pop-Location
            if (-not $repackComplete) {
                Remove-GeneratedIso $Iso
            }
        }
        Write-Host "Repacked SP ISO for $safeMap"
        Write-Host "Repack log: $repackLog"
    }

    if ($RepackOnly) {
        $results += [pscustomobject]@{ Map = $mapName; ExitCode = 0; Name = $runName }
        continue
    }

    if (-not (Test-Path $Iso)) {
        throw "ISO not found: $Iso"
    }

    $argsList = @(
        (Join-Path $repoRoot "scripts\ja_xemu_smoke.py"),
        "--iso", $Iso,
        "--hdd", $Hdd,
        "--name", $runName,
        "--port", $Port,
        "--duration", "$Duration",
        "--interval", "$Interval",
        "--smoke-keymap"
    )

    if (-not [string]::IsNullOrWhiteSpace($XemuExe)) {
        $argsList += @("--xemu-exe", $XemuExe)
    }
    if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
        $argsList += @("--config-path", $ConfigPath)
    }
    if ($Headless) {
        $argsList += "--headless"
    }
    if ($ExplicitXboxMachine) {
        $argsList += "--explicit-xbox-machine"
    }
    if ($SharedSnapshotHdd) {
        $argsList += "--shared-snapshot-hdd"
    }
    if ($MonitorScreenshots) {
        $argsList += "--monitor-screenshots"
    }
    if ($XemuNativeScreenshots) {
        $argsList += "--xemu-native-screenshots"
    }
    if ($ScreenshotStartDelay -gt 0) {
        $argsList += @("--screenshot-start-delay", "$ScreenshotStartDelay")
    }
    if ($NoWindowFallback) {
        $argsList += "--no-window-fallback"
    }
    if ($NoScreenshots) {
        $argsList += "--no-screenshots"
    }
    if ($SampleRegisters) {
        $argsList += "--sample-registers"
    }
    if ($FailOnFrozen) {
        $argsList += "--fail-on-frozen"
    }
    if ($LeaveRunningOnFrozen) {
        $argsList += "--leave-running-on-frozen"
    }
    if ($SkipFinalDumps) {
        $argsList += "--skip-final-dumps"
    }
    if (-not [string]::IsNullOrWhiteSpace($WatchCr2)) {
        $argsList += @("--watch-cr2", $WatchCr2)
    }
    if ($PollXBlog -or $PollXBlogLite) {
        $argsList += "--poll-xblog"
        if ($PollXBlogLite) {
            $argsList += "--poll-xblog-lite"
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogAddr)) {
            $argsList += @("--poll-xblog-addr", $PollXBlogAddr)
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogPhysDelta)) {
            $argsList += @("--poll-xblog-phys-delta", $PollXBlogPhysDelta)
        }
    }
    if ($VideoDebug) {
        $argsList += "--video-debug"
    }
    if (-not [string]::IsNullOrWhiteSpace($XemuDebug)) {
        $argsList += "--xemu-arg=-d"
        $argsList += @("--xemu-arg", $XemuDebug)
    }
    if (-not [string]::IsNullOrWhiteSpace($XemuDebugLog)) {
        $argsList += "--xemu-arg=-D"
        $argsList += @("--xemu-arg", $XemuDebugLog)
    }
    foreach ($spec in $DumpMem) {
        if (-not [string]::IsNullOrWhiteSpace($spec)) {
            $argsList += @("--dump-mem", $spec)
        }
    }
    foreach ($spec in $DumpPhys) {
        if (-not [string]::IsNullOrWhiteSpace($spec)) {
            $argsList += @("--dump-phys", $spec)
        }
    }
    foreach ($arg in $XemuArg) {
        if (-not [string]::IsNullOrWhiteSpace($arg)) {
            if ($arg.StartsWith("-")) {
                $argsList += "--xemu-arg=$arg"
            }
            else {
                $argsList += @("--xemu-arg", $arg)
            }
        }
    }

    Push-Location $repoRoot
    try {
        python @argsList
        $results += [pscustomobject]@{ Map = $mapName; ExitCode = $LASTEXITCODE; Name = $runName }
    }
    finally {
        Pop-Location
        if ($Repack) {
            Remove-GeneratedIso $Iso
        }
    }

    if ($results[-1].ExitCode -ne 0) {
        break
    }
}

$results | Format-Table -AutoSize
$failures = @($results | Where-Object { $_.ExitCode -ne 0 })
if ($failures.Count -gt 0) {
    exit 1
}
