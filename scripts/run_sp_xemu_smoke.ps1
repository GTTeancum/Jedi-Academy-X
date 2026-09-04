param(
    [string]$Map = "borg6",
    [string[]]$Maps = @(),
    [string[]]$Command = @(),
    [string[]]$PostMapCommand = @(),
    [string[]]$ActiveCommand = @(),
    [int]$ActiveCommandServerTime = 72000,
    [switch]$NormalBoot,
    [switch]$MiniSoak,
    [switch]$DirectCoop,
    [switch]$DirectHolomatch,
    [switch]$HolomatchPhaserProof,
    [switch]$HolomatchOverlayProof,
    [switch]$HolomatchShaderTrace,
    [switch]$HolomatchDisableFog,
    [int]$HolomatchGameType = 0,
    [ValidateRange(1, 4)]
    [int]$HolomatchPlayers = 4,
    [ValidateRange(0, 4)]
    [int]$HolomatchHumanPlayers = 0,
    [switch]$HolomatchVirtualControls,
    [switch]$EnableSmokeInput,
    [switch]$SyntheticCombatHarness,
    [int]$SmokeInputStart = 12000,
    [int]$SmokeInputEnd = 70000,
    [int]$SmokeAttackStart = 18000,
    [int]$SmokeAttackEnd = 60000,
    [int]$SmokeAltAttackStart = 0,
    [int]$SmokeAltAttackEnd = 0,
    [int]$SmokeInputForward = 90,
    [int]$SmokeInputSide = 0,
    [int]$SmokeInputYaw = 0,
    [int]$SmokeViewPitch = 9999,
    [int]$SmokeViewYaw = 9999,
    [ValidateSet(-1, 0, 1, 2)]
    [int]$NativeDrawPath = -1,
    [switch]$Headless,
    [switch]$Repack,
    [switch]$Build,
    [int]$Duration = 90,
    [int]$Interval = 10,
    [int]$FirstShotDelay = 20,
    [ValidateRange(0, 100)]
    [int]$ScreenshotCount = 0,
    [string]$Name = "",
    [string]$Iso = "",
    [switch]$ImmutableIso,
    [string]$DefaultXbe = "",
    [string]$SpPk3 = "",
    [string]$MpPk3 = "",
    [string]$StageSource = "",
    [string]$Port = "4460",
    [string[]]$DumpMem = @(),
    [string[]]$DumpBinMem = @(),
    [string[]]$DumpPhys = @(),
    [string]$WatchCr2 = "",
    [double]$SampleEipInterval = 0.0,
    [string]$PollWordAddr = "",
    [int]$PollWordCount = 1,
    [double]$PollWordInterval = 1.0,
    [string]$PollWordLabel = "counter",
    [switch]$PollXBlog,
    [switch]$PollXBlogPerfOnly,
    [int]$PollXBlogStartDelay = 0,
    [double]$PollXBlogInterval = 1.0,
    [switch]$XBlogAutoDumps,
    [switch]$ExtractXBlogProfile,
    [switch]$VisualCheck,
    [string]$PollXBlogAddr = "",
    [string]$PollXBlogPhysAddr = "",
    [string]$PollXBlogMap = "",
    [string]$PollXBlogPhysDelta = "0x284000",
    [switch]$VideoDebug,
    [switch]$NoScreenshots,
    [switch]$Visible,
    [string]$MonitorKeys = "",
    [switch]$HostWindowKeys,
    [ValidateRange(0, 4)]
    [int]$KeyboardControllerPort = 0,
    [string[]]$XemuArg = @(),
    [string]$Hdd = "C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2",
    [string]$XemuExe = "C:\Games\Emulators\Xemu\JACodex\xemu.exe",
    [string]$ConfigPath = "C:\Games\Emulators\Xemu\JACodex\xemu.toml",
    [switch]$RepackOnly,
    [switch]$KeepIso,
    [switch]$KeepStage,
    [switch]$CleanReleaseIso,
    [switch]$CleanStageData
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

# Perf-only telemetry is itself a request to poll the guest heartbeat.  Keep a
# short startup delay so monitor traffic does not compete with boot/loading.
if ($PollXBlogPerfOnly -and -not $PollXBlog) {
    $PollXBlog = $true
    if ($PollXBlogStartDelay -le 0) {
        $PollXBlogStartDelay = 15
    }
}
if ($PollXBlogPerfOnly -and $PollXBlogInterval -lt 5.0) {
    $PollXBlogInterval = 5.0
}
if ($SyntheticCombatHarness -and -not $EnableSmokeInput) {
    throw "SyntheticCombatHarness requires EnableSmokeInput."
}
# XLaunchNewImage places the SP-hosted efmp telemetry block above its linked
# virtual address in the current XEMU/LLE mapping. Prefer that proven mapping
# so direct-Holomatch proof does not begin with a disruptive 64 MiB RAM scan.
if ($DirectHolomatch -and $PollXBlog -and $PollXBlogPhysDelta -eq "0x284000") {
    $PollXBlogPhysDelta = "-0x12c000"
}

try {
    $harnessProcess = [System.Diagnostics.Process]::GetCurrentProcess()
    $harnessProcess.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
$harnessProcess.ProcessorAffinity = [IntPtr]0x555
}
catch {
    Write-Warning "Could not constrain XEMU harness resources: $($_.Exception.Message)"
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputDir = Join-Path $repoRoot "scripts\output"
$defaultIso = Join-Path $repoRoot "build\xemu\StarTrekEliteForceX_unified_minisoak.iso"
$stageDir = Join-Path $repoRoot "build\xemu\shared_stage"
$canonicalStageSource = "C:\Games\Emulators\stefx_iso_seed_complete"
$stageXbe = Join-Path $stageDir "default.xbe"
$stageMpXbe = Join-Path $stageDir "efmp.xbe"
$builtXbe = Join-Path $repoRoot "build\release\default.xbe"
$builtMpXbe = Join-Path $repoRoot "build\release\efmp.xbe"
$builtSpPk3 = if ([string]::IsNullOrWhiteSpace($SpPk3)) {
    Join-Path $repoRoot "build\release\BaseEF\xbox0.pk3"
}
elseif ([System.IO.Path]::IsPathRooted($SpPk3)) {
    [System.IO.Path]::GetFullPath($SpPk3)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $SpPk3))
}
$builtMpPk3 = if ([string]::IsNullOrWhiteSpace($MpPk3)) {
    Join-Path $repoRoot "build\release\BaseEF\xbox1.pk3"
}
elseif ([System.IO.Path]::IsPathRooted($MpPk3)) {
    [System.IO.Path]::GetFullPath($MpPk3)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $MpPk3))
}

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-XbeRuntimeBuildId {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    [byte[]]$data = [System.IO.File]::ReadAllBytes($Path)
    [byte[]]$marker = [System.Text.Encoding]::ASCII.GetBytes("STEFX_RUNTIME_BUILD_ID ")
    for ($i = 0; $i -le $data.Length - $marker.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $marker.Length; $j++) {
            if ($data[$i + $j] -ne $marker[$j]) {
                $matched = $false
                break
            }
        }
        if (-not $matched) {
            continue
        }
        $end = $i
        $limit = [Math]::Min($data.Length, $i + 256)
        while ($end -lt $limit -and $data[$end] -ne 0 -and $data[$end] -ne 10 -and $data[$end] -ne 13) {
            $end++
        }
        return [System.Text.Encoding]::ASCII.GetString($data, $i, $end - $i)
    }
    return $null
}

function Assert-XbeRuntimeBuildId {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    $runtimeBuildId = Get-XbeRuntimeBuildId -Path $Path
    if (-not $runtimeBuildId) {
        throw "$Label is missing STEFX_RUNTIME_BUILD_ID; rebuild scripts\build_xbox.ps1 -Target spmp before XEMU proof."
    }
    $fileName = [System.IO.Path]::GetFileName($Path).ToLowerInvariant()
    $expectedFragments = switch ($fileName) {
        "default.xbe" { @("personality=default", "log=ef_sp_log.txt") }
        "efmp.xbe" { @("personality=efmp", "log=ef_mp_log.txt") }
        default { @() }
    }
    foreach ($fragment in $expectedFragments) {
        if ($runtimeBuildId -notlike "*$fragment*") {
            throw "$Label has wrong STEFX_RUNTIME_BUILD_ID identity: expected fragment '$fragment' in '$runtimeBuildId'"
        }
    }
    Write-Host "Runtime build id: $Label -> $runtimeBuildId"
}

function Test-StagePayloadMatchesSource {
    param(
        [string]$StagePath,
        [string]$SourcePath,
        [string]$Label,
        [DateTime]$IsoWriteTime
    )

    if (-not (Test-Path -LiteralPath $StagePath -PathType Leaf)) {
        Write-Host "$Label is missing from retained XEMU stage; enabling repack."
        return $false
    }
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Current release artifact not found for retained XEMU proof: $SourcePath"
    }
    if ((Get-Item -LiteralPath $StagePath).LastWriteTimeUtc -gt $IsoWriteTime) {
        Write-Host "$Label in retained XEMU stage is newer than retained ISO; enabling repack."
        return $false
    }
    if ((Get-FileSha256 $StagePath) -ne (Get-FileSha256 $SourcePath)) {
        Write-Host "$Label in retained XEMU stage differs from current release artifact; enabling repack."
        return $false
    }
    return $true
}
$builtSoundBankDir = Join-Path $repoRoot "build\release\BaseEF\soundbank"
$builtMusicDir = Join-Path $repoRoot "build\release\BaseEF\music"
$builtVideoDir = Join-Path $repoRoot "build\release\BaseEF\video"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"
$pythonCommand = Get-Command "python.exe" -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
$pythonExe = if ($null -ne $pythonCommand) {
    $pythonCommand.Source
}
else {
    Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
}
if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw "Python interpreter not found: $pythonExe"
}
$releaseMarkerNames = @(
    "ef_mp_cxbx_present_throttle.txt",
    "ef_mp_log.txt",
    "ef_mp_phase.txt",
    "ef_mp_renderprobe.txt",
    "ef_mp_runtime_commands.txt",
    "ef_mp_screenshot_preopen.txt",
    "ef_mp_screenshot_request.txt",
    "ef_mp_smoke_proof.txt",
    "ef_runtime_commands.txt",
    "ef_runtime_commands.done",
    "ef_sp_active_command_time.txt",
    "ef_sp_active_commands.txt",
    "ef_sp_client_active_command_time.txt",
    "ef_sp_client_active_commands.txt",
    "ef_sp_commands.txt",
    "ef_sp_cxbx_present_throttle.txt",
    "ef_sp_direct_coop.txt",
    "ef_sp_level.txt",
    "ef_sp_log.txt",
    "ef_sp_mini_soak.txt",
    "ef_sp_normal_boot.txt",
    "ef_sp_postmap_commands.txt",
    "ef_sp_renderprobe.txt",
    "ef_sp_screenshot_log.txt",
    "ef_sp_screenshot_pending.txt",
    "ef_sp_screenshot_preopen.txt",
    "ef_sp_screenshot_request.txt",
    "ef_sp_smoke_harness.txt",
    "ja_sp_autosmoke.txt",
    "ja_sp_commands.txt",
    "ja_sp_level.txt",
    "ja_sp_postmap_commands.txt",
    "memmap.txt",
    "stefx_xemu_memory_log.txt"
)
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

if ([string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = $defaultIso
}
elseif (-not [System.IO.Path]::IsPathRooted($Iso)) {
    $Iso = Join-Path $repoRoot $Iso
}
$Iso = [System.IO.Path]::GetFullPath($Iso)

if ($ImmutableIso) {
    if ($Build -or $Repack -or $RepackOnly -or $CleanReleaseIso -or $CleanStageData) {
        throw "-ImmutableIso cannot be combined with build or repack options."
    }
    if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
        throw "Immutable ISO is missing: $Iso"
    }
}
if ([string]::IsNullOrWhiteSpace($DefaultXbe)) {
    $DefaultXbe = $builtXbe
}
elseif (-not [System.IO.Path]::IsPathRooted($DefaultXbe)) {
    $DefaultXbe = Join-Path $repoRoot $DefaultXbe
}
$DefaultXbe = [System.IO.Path]::GetFullPath($DefaultXbe)
$cleanupScript = Join-Path $repoRoot "scripts\cleanup_generated.ps1"
if (-not (Test-Path -LiteralPath $cleanupScript -PathType Leaf)) {
    throw "Generated-artifact cleanup script not found: $cleanupScript"
}
& $cleanupScript -CurrentIso $Iso -PreserveStage:$KeepStage -KeepDumps 12

if ($CleanReleaseIso) {
    $Repack = $true
    $RepackOnly = $true
    $NormalBoot = $true
    $MiniSoak = $false
    $DirectCoop = $false
    $DirectHolomatch = $false
    $Maps = @("__normal_boot__")
    $Command = @()
    $PostMapCommand = @()
    $ActiveCommand = @()
}
elseif ($CleanStageData) {
    $Repack = $true
}

$normalizedMaps = @()
if ($MiniSoak) {
    if ($NormalBoot) {
        Write-Warning "-MiniSoak starts with a real SP map; ignoring -NormalBoot."
        $NormalBoot = $false
    }
    if ($Maps.Count -eq 0) {
        $Maps = @($Map)
    }
}
elseif ($DirectCoop) {
    $NormalBoot = $true
    $Maps = @("__normal_boot__")
    if ($EnableSmokeInput -and $SmokeInputStart -lt 210000) {
        $coopInputDelay = 210000 - $SmokeInputStart
        Write-Warning "Direct co-op runs the borg1 opening cinematic; shifting the complete smoke-input schedule past it."
        $SmokeInputStart += $coopInputDelay
        $SmokeInputEnd += $coopInputDelay
        $SmokeAttackStart += $coopInputDelay
        $SmokeAttackEnd += $coopInputDelay
    }
}
elseif ($NormalBoot) {
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
if ($MiniSoak -and $Maps.Count -ne 1) {
    throw "-MiniSoak accepts exactly one initial SP map."
}

$isoProfilePath = "$Iso.smoke-profile.json"
$desiredIsoProfile = [ordered]@{
    maps = @($Maps)
    normalBoot = [bool]$NormalBoot
    miniSoak = [bool]$MiniSoak
    directCoop = [bool]$DirectCoop
    directHolomatch = [bool]$DirectHolomatch
    holomatchGameType = $HolomatchGameType
    holomatchPlayers = $HolomatchPlayers
    holomatchHumanPlayers = $HolomatchHumanPlayers
    holomatchOverlayProof = [bool]$HolomatchOverlayProof
    splitScreenEconomy = [bool]($DirectCoop -or ($DirectHolomatch -and $HolomatchPlayers -ge 2))
    holomatchShaderTrace = [bool]$HolomatchShaderTrace
    holomatchDisableFog = [bool]$HolomatchDisableFog
    command = @($Command)
    postMapCommand = @($PostMapCommand)
    activeCommand = @($ActiveCommand)
    activeCommandServerTime = $ActiveCommandServerTime
    enableSmokeInput = [bool]$EnableSmokeInput
    syntheticCombatHarness = [bool]$SyntheticCombatHarness
    smokeInputStart = $SmokeInputStart
    smokeInputEnd = $SmokeInputEnd
    smokeAttackStart = $SmokeAttackStart
    smokeAttackEnd = $SmokeAttackEnd
    smokeAltAttackStart = $SmokeAltAttackStart
    smokeAltAttackEnd = $SmokeAltAttackEnd
    smokeInputForward = $SmokeInputForward
    smokeInputSide = $SmokeInputSide
    smokeInputYaw = $SmokeInputYaw
    smokeViewPitch = $SmokeViewPitch
    smokeViewYaw = $SmokeViewYaw
    nativeDrawPath = $NativeDrawPath
    cleanReleaseIso = [bool]$CleanReleaseIso
    cleanStageData = [bool]$CleanStageData
} | ConvertTo-Json -Compress

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

$normalizedBinDumps = @()
foreach ($entry in $DumpBinMem) {
    foreach ($piece in ($entry -split ',')) {
        $trimmed = $piece.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedBinDumps += $trimmed
        }
    }
}
$DumpBinMem = $normalizedBinDumps

function Copy-TreeAsHardlinks {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination,
        [string[]]$ExcludeRootNames = @()
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($ExcludeRootNames -contains $item.Name) {
            continue
        }

        $target = Join-Path $Destination $item.Name
        if ($item.PSIsContainer) {
            Copy-TreeAsHardlinks -Source $item.FullName -Destination $target -ExcludeRootNames @()
            continue
        }

        try {
            New-Item -ItemType HardLink -Path $target -Target $item.FullName -ErrorAction Stop | Out-Null
        }
        catch {
            Copy-Item -LiteralPath $item.FullName -Destination $target -Force
        }
    }
}

function Assert-EFMovieSet {
    param([Parameter(Mandatory=$true)][string]$VideoDir)

    if (-not (Test-Path -LiteralPath $VideoDir -PathType Container)) {
        throw "Elite Force movie directory is missing: $VideoDir"
    }

    # Match the retail Elite Force table in code/client/cl_cin_console.cpp.
    # Its story reel skips st_03 and combines the late chapters as st_16a
    # and st_1718; those are the actual licensed filenames, not omissions.
    $expectedMovies = @(
        "eflogo", "intro",
        "st_01", "st_02", "st_04", "st_05", "st_06", "st_07",
        "st_08", "st_09", "st_10", "st_11", "st_12", "st_13",
        "st_14", "st_15", "st_16a", "st_1718", "st_19", "st_20",
        "st_21"
    )

    $missingMovies = New-Object System.Collections.Generic.List[string]
    foreach ($movieName in $expectedMovies) {
        foreach ($suffix in @("", "_lo")) {
            $moviePath = Join-Path $VideoDir ($movieName + $suffix + ".bik")
            if (-not (Test-Path -LiteralPath $moviePath -PathType Leaf)) {
                $missingMovies.Add((Split-Path -Leaf $moviePath))
            }
        }
    }
    if ($missingMovies.Count -gt 0) {
        throw "Elite Force movie set is incomplete in ${VideoDir}: $($missingMovies -join ', ')"
    }

    $movieFiles = @(Get-ChildItem -LiteralPath $VideoDir -File -Filter "*.bik")
    $movieBytes = ($movieFiles | Measure-Object Length -Sum).Sum
    Write-Host "Elite Force movie set ok: $($movieFiles.Count) original BIK files, $movieBytes bytes"
}

function Initialize-StageFromSource {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    if (-not (Test-Path $Source)) {
        throw "Stage source not found: $Source"
    }

    $resolvedRepo = [System.IO.Path]::GetFullPath($repoRoot)
    $resolvedDest = [System.IO.Path]::GetFullPath($Destination)
    if (-not $resolvedDest.StartsWith($resolvedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to recreate stage outside repo: $resolvedDest"
    }

    if (Test-Path $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    $excludeRootNames = @($releaseMarkerNames + "base")

    $stageScript = @'
import os
import shutil
import sys

src = os.path.abspath(sys.argv[1])
dst = os.path.abspath(sys.argv[2])
exclude_root = set(sys.argv[3:])
mutable_artifacts = {
    'default.xbe',
    'efmp.xbe',
    os.path.normcase(os.path.join('BaseEF', 'xbox0.pk3')),
    os.path.normcase(os.path.join('BaseEF', 'xbox1.pk3')),
}

if os.path.exists(dst):
    shutil.rmtree(dst)
os.makedirs(dst, exist_ok=True)

for root, dirs, files in os.walk(src):
    rel = os.path.relpath(root, src)
    if rel == '.':
        dirs[:] = [name for name in dirs if name not in exclude_root]
    out_root = dst if rel == '.' else os.path.join(dst, rel)
    os.makedirs(out_root, exist_ok=True)
    for name in files:
        if rel == '.' and name in exclude_root:
            continue
        source_file = os.path.join(root, name)
        target_file = os.path.join(out_root, name)
        relative_file = name if rel == '.' else os.path.join(rel, name)
        if os.path.normcase(relative_file) in mutable_artifacts:
            shutil.copy2(source_file, target_file)
            continue
        try:
            os.link(source_file, target_file)
        except OSError:
            shutil.copy2(source_file, target_file)
'@
    $stageArgs = @("-c", $stageScript, $Source, $Destination) + $excludeRootNames
    & $pythonExe @stageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to stage EF XEMU tree from $Source"
    }
}

function Initialize-StefxXemuInstance {
    $instanceDir = Split-Path -Parent $ConfigPath
    $sourceXemu = "C:\Games\Emulators\Xemu\xemu.exe"
    $bootRom = "C:\Games\Emulators\Xemu\MCPX\mcpx_1.0.bin"
    $flashRom = "C:\Games\Emulators\Xemu\BIOS\xbox-4627_debug.bin"
    $eeprom = "C:\Games\Emulators\Xemu\EEPROM\eeprom.bin"
    $screenshotDir = Join-Path $instanceDir "screenshots"

    foreach ($requiredPath in @($sourceXemu, $bootRom, $flashRom, $eeprom, $Hdd)) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required XEMU runtime file not found: $requiredPath"
        }
    }

    New-Item -ItemType Directory -Path $instanceDir -Force | Out-Null
    New-Item -ItemType Directory -Path $screenshotDir -Force | Out-Null
    if (-not (Test-Path -LiteralPath $XemuExe -PathType Leaf)) {
        try {
            New-Item -ItemType HardLink -Path $XemuExe -Target $sourceXemu -ErrorAction Stop | Out-Null
        }
        catch {
            Copy-Item -LiteralPath $sourceXemu -Destination $XemuExe -Force
        }
    }

    $config = @"
[general]
show_welcome = false
screenshot_dir = '$screenshotDir'
games_dir = '$instanceDir'
skip_boot_anim = true
last_viewed_menu_index = 1

[general.updates]
check = false

[input]
auto_bind = false
background_input_capture = true

[input.bindings]
port1_driver = 'usb-xbox-gamepad'
port1 = 'keyboard'
port2_driver = 'usb-xbox-gamepad'
port3_driver = 'usb-xbox-gamepad'
port4_driver = 'usb-xbox-gamepad'

[net]
enable = false

[sys.files]
bootrom_path = '$bootRom'
flashrom_path = '$flashRom'
eeprom_path = '$eeprom'
hdd_path = '$Hdd'
dvd_path = '$Iso'
"@
    Set-Content -LiteralPath $ConfigPath -Value $config -Encoding ASCII
}

$scriptExitCode = 0
try {
if ($Build) {
    $stamp = Get-Date -Format yyyyMMdd_HHmmss
    $buildLog = Join-Path $outputDir "build_sp_smoke_$stamp.log"
    $buildMpLog = Join-Path $outputDir "build_spmp_smoke_$stamp.log"
    Push-Location $repoRoot
    try {
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp -ReuseObjects -SkipAssets *> $buildLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog -Tail 100
            throw "SP build failed with exit code $LASTEXITCODE"
        }
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target spmp -ReuseObjects -SkipAssets *> $buildMpLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildMpLog -Tail 100
            throw "SP-hosted Holomatch build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    Write-Host "SP build log: $buildLog"
    Write-Host "Holomatch build log: $buildMpLog"
    $Repack = $true
}

Assert-XbeRuntimeBuildId -Path $DefaultXbe -Label "build\release\default.xbe"
Assert-XbeRuntimeBuildId -Path $builtMpXbe -Label "build\release\efmp.xbe"

if (-not $Repack -and -not $ImmutableIso) {
    if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
        Write-Host "Retained XEMU ISO is missing; enabling repack."
        $Repack = $true
    }
    elseif ($Maps.Count -ne 1) {
        Write-Host "Multi-map smoke runs require per-map control data; enabling repack."
        $Repack = $true
    }
    elseif (-not (Test-Path -LiteralPath $isoProfilePath -PathType Leaf)) {
        Write-Host "Retained XEMU ISO has no smoke-profile record; enabling repack."
        $Repack = $true
    }
    elseif ((Get-Content -LiteralPath $isoProfilePath -Raw).Trim() -ne $desiredIsoProfile) {
        Write-Host "Requested smoke personality/control data differs from retained XEMU ISO; enabling repack."
        $Repack = $true
    }
    else {
        $isoWriteTime = (Get-Item -LiteralPath $Iso).LastWriteTimeUtc
        $newerPayloads = @(
            @(
                $DefaultXbe,
                $builtMpXbe,
                $builtSpPk3,
                $builtMpPk3
            ) | Where-Object {
                (Test-Path -LiteralPath $_ -PathType Leaf) -and
                ((Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $isoWriteTime)
            }
        )
        if ($newerPayloads.Count -gt 0) {
            Write-Host "Built payload is newer than retained XEMU ISO; enabling repack:"
            $newerPayloads | ForEach-Object { Write-Host "  $_" }
            $Repack = $true
        }
        else {
            $entryXbe = if ($DirectHolomatch) { $builtMpXbe } else { $DefaultXbe }
            $stageChecks = @(
                @{ Stage = $stageXbe; Source = $entryXbe; Label = "default.xbe" },
                @{ Stage = $stageMpXbe; Source = $builtMpXbe; Label = "efmp.xbe" },
                @{ Stage = (Join-Path $stageDir "BaseEF\xbox0.pk3"); Source = $builtSpPk3; Label = "BaseEF\xbox0.pk3" },
                @{ Stage = (Join-Path $stageDir "BaseEF\xbox1.pk3"); Source = $builtMpPk3; Label = "BaseEF\xbox1.pk3" }
            )
            foreach ($check in $stageChecks) {
                if (-not (Test-StagePayloadMatchesSource `
                    -StagePath $check.Stage `
                    -SourcePath $check.Source `
                    -Label $check.Label `
                    -IsoWriteTime $isoWriteTime)) {
                    $Repack = $true
                    break
                }
            }
        }
    }
}

if (-not (Test-Path $extractXiso)) {
    throw "extract-xiso not found: $extractXiso"
}
if (-not $RepackOnly) {
    Initialize-StefxXemuInstance
}
if ($Repack) {
    if ([string]::IsNullOrWhiteSpace($StageSource)) {
        $StageSource = $canonicalStageSource
    }
    elseif (-not [System.IO.Path]::IsPathRooted($StageSource)) {
        $StageSource = Join-Path $repoRoot $StageSource
    }
    $StageSource = [System.IO.Path]::GetFullPath($StageSource)
    if (-not (Test-Path -LiteralPath $StageSource -PathType Container)) {
        throw "Canonical XEMU stage source not found: $StageSource"
    }
    Initialize-StageFromSource -Source $StageSource -Destination $stageDir
}
elseif (-not (Test-Path $stageDir)) {
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
}

$results = @()
foreach ($mapName in $Maps) {
    if ([string]::IsNullOrWhiteSpace($mapName)) {
        continue
    }

    $isNormalRun = $NormalBoot -or $mapName -eq "__normal_boot__"
    $safeMap = if ($isNormalRun) { "normal" } else { ($mapName -replace '[^A-Za-z0-9_.-]', '_') }
    $runName = if ([string]::IsNullOrWhiteSpace($Name)) { "ef_sp_$safeMap" } else { "${Name}_$safeMap" }
    $proofMode = if ($DirectCoop) { "coop" } elseif ($DirectHolomatch) { "mp" } else { "sp" }
    $proofMap = if ($isNormalRun) { "normal" } else { $mapName }

    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_normal_boot.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_mini_soak.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_direct_coop.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_smoke_harness.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_level.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_level.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_postmap_commands.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_client_active_commands.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_client_active_command_time.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_active_commands.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_active_command_time.txt") -Force -ErrorAction SilentlyContinue

    if ($isNormalRun) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_normal_boot.txt") -Value "1" -Encoding ASCII
        if ($MiniSoak) {
            Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_mini_soak.txt") -Value "1" -Encoding ASCII
        }
    }
    else {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_level.txt") -Value $mapName -Encoding ASCII
    }
    if ($MiniSoak) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_mini_soak.txt") -Value "1" -Encoding ASCII
    }
    if ($DirectCoop) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_direct_coop.txt") -Value "1" -Encoding ASCII
    }
    if ($DirectHolomatch) {
		$effectiveHolomatchHumanPlayers = if ($HolomatchHumanPlayers -gt 0) { $HolomatchHumanPlayers } else { $HolomatchPlayers }
		if ($effectiveHolomatchHumanPlayers -gt $HolomatchPlayers) {
			throw "HolomatchHumanPlayers ($effectiveHolomatchHumanPlayers) cannot exceed HolomatchPlayers/viewports ($HolomatchPlayers)."
		}
		$splitScreenEconomyMode = [int]($HolomatchPlayers -ge 2)
        $holomatchMultiViewportMode = [int](($HolomatchVirtualControls -or $HolomatchPlayers -ge 3) -and $effectiveHolomatchHumanPlayers -eq $HolomatchPlayers)
        # The one-pad hardware/XEMU profile assigns each missing viewport to a
        # real game bot. Keep the population equal to the viewport count so a
        # 1H/4V run is exactly P1 plus bot-owned P2-P4, with no hidden extras.
        $holomatchBotMinPlayers = $HolomatchPlayers
        $Command = @(
            "set fs_game BaseEF",
            "set stefx_splitScreen 1",
            "set stefx_splitScreenPlayers $HolomatchPlayers",
            "set stefx_splitScreenMode holomatch",
            "set stefx_hmLocalPlayers $HolomatchPlayers",
            "set stefx_hmHumanPlayers $effectiveHolomatchHumanPlayers",
            "set r_splitScreenEconomy $splitScreenEconomyMode",
            "set stefx_hm_split_virtual_controls $holomatchMultiViewportMode",
            "set stefx_hm_split_virtual_controls_p1 $holomatchMultiViewportMode",
            "set stefx_hm_launch_source direct",
            "set stefx_hm_audio_proof 1",
            "set stefx_splitScreenP2Entity -1",
            "set model munro/default",
            "set sv_maxclients 8",
            "set g_gametype $HolomatchGameType",
            "set fraglimit 0",
            "set timelimit 0",
            "set g_holoIntro 0",
            "set bot_enable 1",
            "set bot_minplayers $holomatchBotMinPlayers",
            "set g_spSkill 2"
        )
        if ($HolomatchShaderTrace) {
            $Command += "set stefx_hm_shader_trace 1"
        }
        if ($HolomatchDisableFog) {
            $Command += "set r_drawfog 0"
        }
        if ($HolomatchPhaserProof) {
            $Command += @(
                "set stefx_hm_split_phaser_proof 1",
                "set bot_minplayers $HolomatchPlayers"
            )
        }
        if ($HolomatchOverlayProof) {
            $Command += "set stefx_hm_split_overlay_proof 1"
        }
    }
    if ($EnableSmokeInput) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_smoke_harness.txt") -Value "1" -Encoding ASCII
        $Command += @(
            "set stefx_smoke_input 1",
            "set stefx_smoke_input_start $SmokeInputStart",
            "set stefx_smoke_input_end $SmokeInputEnd",
            "set stefx_smoke_input_attack_start $SmokeAttackStart",
            "set stefx_smoke_input_attack_end $SmokeAttackEnd",
            "set stefx_smoke_input_alt_attack_start $SmokeAltAttackStart",
            "set stefx_smoke_input_alt_attack_end $SmokeAltAttackEnd",
            "set stefx_smoke_input_forward $SmokeInputForward",
            "set stefx_smoke_input_side $SmokeInputSide",
            "set stefx_smoke_input_yaw $SmokeInputYaw",
            "set stefx_smoke_view_pitch $SmokeViewPitch",
            "set stefx_smoke_view_yaw $SmokeViewYaw"
        )
        if ($SyntheticCombatHarness) {
            # These switches deliberately bypass campaign-authored state and
            # are only valid for isolated combat diagnostics.  Keep ordinary
            # campaign input injection subordinate to ICARUS/map behavior.
            $Command += @(
                "set stefx_smoke_aim 1",
                "set stefx_smoke_wake_ai 1",
                "set stefx_smoke_unlock_player 1",
                "set stefx_smoke_ready_weapon 1",
                "set stefx_smoke_stage_enemy 1",
                "set stefx_hm_smoke_combat_proof 1"
            )
        }
    }
    if ($NativeDrawPath -ge 0) {
        $Command += "set r_nativeDrawPath $NativeDrawPath"
    }
    if ($Command.Count -gt 0) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_commands.txt") -Value $Command -Encoding ASCII
    }
    else {
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_commands.txt") -Force -ErrorAction SilentlyContinue
    }
    if ($PostMapCommand.Count -gt 0) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_postmap_commands.txt") -Value $PostMapCommand -Encoding ASCII
    }
    else {
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_postmap_commands.txt") -Force -ErrorAction SilentlyContinue
    }
    if ($ActiveCommand.Count -gt 0) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_client_active_commands.txt") -Value $ActiveCommand -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_client_active_command_time.txt") -Value ([string]$ActiveCommandServerTime) -Encoding ASCII
    }
    else {
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_client_active_commands.txt") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_client_active_command_time.txt") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_active_commands.txt") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_active_command_time.txt") -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_log.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "memmap.txt") -Force -ErrorAction SilentlyContinue
    if ($CleanReleaseIso) {
        foreach ($markerName in $releaseMarkerNames) {
            Remove-Item -LiteralPath (Join-Path $stageDir $markerName) -Force -ErrorAction SilentlyContinue
        }
    }

    if ($CleanReleaseIso -or $CleanStageData) {
        $releaseUiDir = Join-Path $stageDir "BaseEF\ui"
        if (Test-Path -LiteralPath $releaseUiDir -PathType Container) {
            Get-ChildItem -LiteralPath $releaseUiDir -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in @(".menu", ".txt") } |
                Remove-Item -Force
        }

        $releaseBaseEf = Join-Path $stageDir "BaseEF"
        if (Test-Path -LiteralPath $releaseBaseEf -PathType Container) {
            Get-ChildItem -LiteralPath $releaseBaseEf -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -notin @("soundbank", "video", "music") } |
                Remove-Item -Recurse -Force

            foreach ($staleName in @(
                "games.log",
                "xbox_patch_manifest.json",
                "XBOX0.PK3.disabled",
                "vssver.scc"
            )) {
                Remove-Item -LiteralPath (Join-Path $releaseBaseEf $staleName) -Force -ErrorAction SilentlyContinue
            }
        }

        foreach ($staleName in @(
            "default-native-test.map",
            "default-native-test.xbe",
            "default.map",
            "ef_mp_backbuffer.bmp",
            "ef_mp_xgshot.bmp",
            "memory-map.txt",
            "pointers.txt"
        )) {
            Remove-Item -LiteralPath (Join-Path $stageDir $staleName) -Force -ErrorAction SilentlyContinue
        }
    }
    else {
        Set-Content -LiteralPath (Join-Path $stageDir "stefx_xemu_memory_log.txt") -Value "1" -Encoding ASCII
    }

    if ($CleanReleaseIso) {
        $remainingReleaseMarkers = @(
            $releaseMarkerNames |
                Where-Object { Test-Path -LiteralPath (Join-Path $stageDir $_) -PathType Leaf }
        )
        if ($remainingReleaseMarkers.Count -gt 0) {
            throw "Clean release stage still contains test marker(s): $($remainingReleaseMarkers -join ', ')"
        }
        Write-Host "Verified clean release stage contains no test or forced-launch markers."
    }

    if ($Repack) {
        if (-not (Test-Path $DefaultXbe)) {
            throw "SP XBE not found: $DefaultXbe"
        }
        if (-not (Test-Path $builtMpXbe)) {
            throw "Built Holomatch XBE not found: $builtMpXbe"
        }
        if (-not (Test-Path $builtSpPk3)) {
            throw "Built SP Xbox package not found: $builtSpPk3"
        }
        if (-not (Test-Path $builtMpPk3)) {
            throw "Built Holomatch Xbox package not found: $builtMpPk3"
        }
        if (-not (Test-Path -LiteralPath $builtSoundBankDir -PathType Container)) {
            throw "Built shared soundbank not found: $builtSoundBankDir"
        }
        if (-not (Test-Path -LiteralPath $builtMusicDir -PathType Container)) {
            throw "Built XBADPCM music directory not found: $builtMusicDir"
        }
        Assert-EFMovieSet -VideoDir $builtVideoDir
        $entryXbe = if ($DirectHolomatch) { $builtMpXbe } else { $DefaultXbe }
        Copy-Item -LiteralPath $entryXbe -Destination $stageXbe -Force
        Copy-Item -LiteralPath $builtMpXbe -Destination $stageMpXbe -Force
        $stageBaseEf = Join-Path $stageDir "BaseEF"
        New-Item -ItemType Directory -Path $stageBaseEf -Force | Out-Null
        Copy-Item -LiteralPath $builtSpPk3 -Destination (Join-Path $stageBaseEf "xbox0.pk3") -Force
        Copy-Item -LiteralPath $builtMpPk3 -Destination (Join-Path $stageBaseEf "xbox1.pk3") -Force
        $stageSoundBankDir = Join-Path $stageBaseEf "soundbank"
        if (Test-Path -LiteralPath $stageSoundBankDir -PathType Container) {
            Remove-Item -LiteralPath $stageSoundBankDir -Recurse -Force
        }
        Copy-TreeAsHardlinks -Source $builtSoundBankDir -Destination $stageSoundBankDir -ExcludeRootNames @()
        # Effects and dialogue resolve through the all-XBADPCM soundbank.
        # Music is streamed as loose WAV, so replace the retail MP3 directory
        # with the converted build output and then remove every MP3 source.
        $stageMusicDir = Join-Path $stageBaseEf "music"
        if (Test-Path -LiteralPath $stageMusicDir -PathType Container) {
            Remove-Item -LiteralPath $stageMusicDir -Recurse -Force
        }
        Copy-TreeAsHardlinks -Source $builtMusicDir -Destination $stageMusicDir -ExcludeRootNames @()
        Get-ChildItem -LiteralPath $stageMusicDir -Recurse -File -Filter "*.mp3" -ErrorAction SilentlyContinue |
            Remove-Item -Force
        $stageMusicMp3 = @(Get-ChildItem -LiteralPath $stageMusicDir -Recurse -File -Filter "*.mp3" -ErrorAction SilentlyContinue)
        if ($stageMusicMp3.Count -ne 0) {
            throw "Clean Xbox music stage still contains MP3 files."
        }
        $badMusicWav = @()
        foreach ($musicWave in Get-ChildItem -LiteralPath $stageMusicDir -Recurse -File -Filter "*.wav") {
            [byte[]]$waveHeader = [System.IO.File]::ReadAllBytes($musicWave.FullName)
            if ($waveHeader.Length -lt 22 -or [System.BitConverter]::ToUInt16($waveHeader, 20) -ne 0x0069) {
                $badMusicWav += $musicWave.FullName
            }
        }
        if ($badMusicWav.Count -ne 0) {
            throw "Clean Xbox music stage contains non-XBADPCM WAV files: $($badMusicWav -join ', ')"
        }
        $stageVideoDir = Join-Path $stageBaseEf "video"
        if (Test-Path -LiteralPath $stageVideoDir -PathType Container) {
            Remove-Item -LiteralPath $stageVideoDir -Recurse -Force
        }
        Copy-TreeAsHardlinks -Source $builtVideoDir -Destination $stageVideoDir -ExcludeRootNames @()
		Get-ChildItem -LiteralPath $stageVideoDir -File -Filter "*.xmv" -ErrorAction SilentlyContinue |
			Remove-Item -Force
		Remove-Item -LiteralPath (Join-Path $stageVideoDir "xbox_video_assets_manifest.json") -Force -ErrorAction SilentlyContinue
        Assert-EFMovieSet -VideoDir $stageVideoDir

        $stamp = Get-Date -Format yyyyMMdd_HHmmss
        $repackLog = Join-Path $outputDir "repack_sp_${safeMap}_$stamp.log"
        $isoDir = Split-Path -Parent $Iso
        $isoName = Split-Path -Leaf $Iso
        New-Item -ItemType Directory -Force -Path $isoDir | Out-Null
        Push-Location $isoDir
        try {
            Remove-Item -LiteralPath $isoName -Force -ErrorAction SilentlyContinue
            & $extractXiso -c $stageDir $isoName *> $repackLog
            if ($LASTEXITCODE -ne 0) {
                Get-Content -LiteralPath $repackLog -Tail 80
                throw "extract-xiso failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }
        $entryLabel = if ($CleanReleaseIso) {
            "clean beta release"
        }
        elseif ($DirectHolomatch) {
            "direct Holomatch control"
        }
        else {
            "shared SP/co-op/Holomatch"
        }
        Write-Host "Repacked $entryLabel ISO for $safeMap"
        Write-Host "Repack log: $repackLog"
        if ($Maps.Count -eq 1) {
            Set-Content -LiteralPath $isoProfilePath -Value $desiredIsoProfile -Encoding ASCII
        }
        else {
            Remove-Item -LiteralPath $isoProfilePath -Force -ErrorAction SilentlyContinue
        }
    }

    if ($RepackOnly) {
        $results += [pscustomobject]@{ Map = $mapName; ExitCode = 0; Name = $runName }
        continue
    }

    if (-not (Test-Path $Iso)) {
        throw "ISO not found: $Iso"
    }
    $Iso = (Resolve-Path -LiteralPath $Iso).Path

    $argsList = @(
        (Join-Path $repoRoot "scripts\ja_xemu_smoke.py"),
        "--iso", $Iso,
        "--hdd", $Hdd,
        "--xemu-exe", $XemuExe,
        "--name", $runName,
        "--port", $Port,
        "--duration", "$Duration",
        "--interval", "$Interval",
        "--first-shot-delay", "$FirstShotDelay",
        "--max-screenshots", "$ScreenshotCount",
        "--proof-mode", $proofMode,
        "--proof-map", $proofMap,
        "--runtime-xbe", $DefaultXbe,
        "--runtime-xbe", $builtMpXbe,
        "--require-runtime-xbe-id",
        "--smoke-keymap"
    )

    if ($Headless) {
        $argsList += "--headless"
    }
    if ($NoScreenshots -or $ScreenshotCount -le 0) {
        $argsList += "--no-screenshots"
    } else {
        $argsList += @(
            "--xemu-native-screenshots",
            "--xemu-screenshot-dir", (Join-Path (Split-Path -Parent $ConfigPath) "screenshots")
        )
    }
    if ($Visible) {
        $argsList += "--visible"
    }
    if (-not [string]::IsNullOrWhiteSpace($WatchCr2)) {
        $argsList += @("--watch-cr2", $WatchCr2)
    }
    if ($SampleEipInterval -gt 0) {
        $argsList += @("--sample-eip-interval", $SampleEipInterval)
    }
    if (-not [string]::IsNullOrWhiteSpace($PollWordAddr)) {
        $argsList += @(
            "--poll-word-addr", $PollWordAddr,
            "--poll-word-count", $PollWordCount,
            "--poll-word-interval", $PollWordInterval,
            "--poll-word-label", $PollWordLabel
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($MonitorKeys)) {
        $argsList += @("--monitor-keys", $MonitorKeys)
    }
    if ($HostWindowKeys) {
        $argsList += "--host-window-keys"
    }
    if ($KeyboardControllerPort -gt 0) {
        $argsList += @("--keyboard-controller-port", $KeyboardControllerPort)
    }
    if (($PollXBlog -or $XBlogAutoDumps -or $ExtractXBlogProfile) -and
        $DirectHolomatch -and [string]::IsNullOrWhiteSpace($PollXBlogMap)) {
        $PollXBlogMap = Join-Path $repoRoot "build\release\efmp.map"
    }
    if (-not [string]::IsNullOrWhiteSpace($PollXBlogMap)) {
        if (-not [System.IO.Path]::IsPathRooted($PollXBlogMap)) {
            $PollXBlogMap = Join-Path $repoRoot $PollXBlogMap
        }
        $argsList += @("--map-file", [System.IO.Path]::GetFullPath($PollXBlogMap))
    }
    if ($PollXBlog) {
        $argsList += "--poll-xblog"
        if ($PollXBlogPerfOnly) {
            $argsList += "--poll-xblog-perf-only"
        }
        if ($PollXBlogStartDelay -gt 0) {
            $argsList += @("--poll-xblog-start-delay", $PollXBlogStartDelay)
        }
        $argsList += @("--poll-xblog-interval", $PollXBlogInterval)
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogAddr)) {
            $argsList += @("--poll-xblog-addr", $PollXBlogAddr)
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogPhysAddr)) {
            $argsList += @("--poll-xblog-phys-addr", $PollXBlogPhysAddr)
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogPhysDelta)) {
            if ($PollXBlogPhysDelta.StartsWith("-")) {
                $argsList += "--poll-xblog-phys-delta=$PollXBlogPhysDelta"
            }
            else {
                $argsList += @("--poll-xblog-phys-delta", $PollXBlogPhysDelta)
            }
        }
    }
    if ($XBlogAutoDumps) {
        $argsList += "--xblog-auto-dumps"
    }
    if ($ExtractXBlogProfile) {
        $argsList += "--extract-xblog-profile"
    }
    if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
        $argsList += @("--config-path", $ConfigPath)
    }
    if ($VideoDebug) {
        $argsList += "--video-debug"
    }
    foreach ($spec in $DumpMem) {
        if (-not [string]::IsNullOrWhiteSpace($spec)) {
            $argsList += @("--dump-mem", $spec)
        }
    }
    foreach ($spec in $DumpBinMem) {
        if (-not [string]::IsNullOrWhiteSpace($spec)) {
            $argsList += @("--dump-bin-mem", $spec)
        }
    }
    foreach ($spec in $DumpPhys) {
        if (-not [string]::IsNullOrWhiteSpace($spec)) {
            $argsList += @("--dump-phys", $spec)
        }
    }
    foreach ($arg in $XemuArg) {
        if (-not [string]::IsNullOrWhiteSpace($arg)) {
            $argsList += "--xemu-arg=$arg"
        }
    }

    Push-Location $repoRoot
    try {
        $runStart = Get-Date
        & $pythonExe @argsList
        $runExitCode = $LASTEXITCODE
        if ($VisualCheck -and $runExitCode -eq 0) {
            $report = Get-ChildItem -LiteralPath $outputDir -Filter "${runName}_*.report.txt" -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTime -ge $runStart.AddMinutes(-1) } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            if ($null -eq $report) {
                Write-Host "Visual check failed: no report found for $runName"
                $runExitCode = 1
            }
            else {
                $prefix = $report.FullName -replace '\.report\.txt$', ''
                $visualInputs = @(
                    Get-Content -LiteralPath $report.FullName |
                        ForEach-Object {
                            if ($_ -match '\bfile=(.+?\.png)(?:\s|$)') {
                                $matches[1]
                            }
                        } |
                        Where-Object { Test-Path -LiteralPath $_ }
                )
                if ($visualInputs.Count -eq 0) {
                    $visualInputs = @("${prefix}_*.png")
                }
                & $pythonExe (Join-Path $repoRoot "scripts\check_visual_smoke.py") @visualInputs --log $report.FullName
                if ($LASTEXITCODE -ne 0) {
                    $runExitCode = $LASTEXITCODE
                }
            }
        }
        if ($MiniSoak -and $runExitCode -eq 0) {
            $miniSoakReport = Get-ChildItem -LiteralPath $outputDir -Filter "${runName}_*.report.txt" -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTime -ge $runStart.AddMinutes(-1) } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            $miniSoakReachedHandoff = $false
            if ($null -ne $miniSoakReport) {
                $miniSoakReachedHandoff = [bool](Select-String -LiteralPath $miniSoakReport.FullName -Pattern 'xblogsoak-live .*stage=9(?:\s|$)' -Quiet)
            }
            if (-not $miniSoakReachedHandoff) {
                Write-Host "Mini-soak failed: SP/co-op/Holomatch handoff stage 9 was not reached."
                $runExitCode = 1
            }
        }
        $results += [pscustomobject]@{ Map = $mapName; ExitCode = $runExitCode; Name = $runName }
    }
    finally {
        Pop-Location
    }

    if ($results[-1].ExitCode -ne 0) {
        break
    }
}

$results | Format-Table -AutoSize
$failures = @($results | Where-Object { $_.ExitCode -ne 0 })
if ($failures.Count -gt 0) {
    $scriptExitCode = 1
}
}
finally {
    try {
        & $cleanupScript -CurrentIso $Iso -PreserveStage:$KeepStage -KeepDumps 12
    }
    catch {
        Write-Warning "Post-run generated-artifact cleanup failed: $($_.Exception.Message)"
    }
}
if ($scriptExitCode -ne 0) {
    exit $scriptExitCode
}
