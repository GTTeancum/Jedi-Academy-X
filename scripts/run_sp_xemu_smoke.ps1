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
    [int]$HolomatchGameType = 0,
    [switch]$EnableSmokeInput,
    [int]$SmokeInputStart = 12000,
    [int]$SmokeInputEnd = 70000,
    [int]$SmokeAttackStart = 18000,
    [int]$SmokeAttackEnd = 60000,
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
    [string]$Name = "",
    [string]$Iso = "",
    [string]$DefaultXbe = "",
    [string]$StageSource = "",
    [string]$Port = "4460",
    [string[]]$DumpMem = @(),
    [string[]]$DumpBinMem = @(),
    [string[]]$DumpPhys = @(),
    [string]$WatchCr2 = "",
    [switch]$PollXBlog,
    [switch]$PollXBlogPerfOnly,
    [int]$PollXBlogStartDelay = 0,
    [switch]$XBlogAutoDumps,
    [switch]$VisualCheck,
    [string]$PollXBlogAddr = "",
    [string]$PollXBlogPhysAddr = "",
    [string]$PollXBlogMap = "",
    [string]$PollXBlogPhysDelta = "0x284000",
    [switch]$VideoDebug,
    [switch]$NoScreenshots,
    [string]$MonitorKeys = "",
    [string[]]$XemuArg = @(),
    [string]$Hdd = "C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2",
    [string]$XemuExe = "C:\Games\Emulators\Xemu\JACodex\xemu.exe",
    [string]$ConfigPath = "C:\Games\Emulators\Xemu\JACodex\xemu.toml",
    [switch]$RepackOnly,
    [switch]$KeepIso,
    [switch]$KeepStage,
    [switch]$CleanReleaseIso
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0
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
$builtSpPk3 = Join-Path $repoRoot "build\release\BaseEF\xbox0.pk3"
$builtMpPk3 = Join-Path $repoRoot "build\release\BaseEF\xbox1.pk3"
$builtSoundBankDir = Join-Path $repoRoot "build\release\BaseEF\soundbank"
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

if (-not $Repack) {
    if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
        Write-Host "Retained XEMU ISO is missing; enabling repack."
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
        $Command = @(
            "set fs_game BaseEF",
            "set model munro/default",
            "set sv_maxclients 4",
            "set g_gametype $HolomatchGameType",
            "set fraglimit 0",
            "set timelimit 0",
            "set bot_enable 1",
            "set bot_minplayers 3",
            "set g_spSkill 1"
        )
    }
    if ($EnableSmokeInput) {
        Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_smoke_harness.txt") -Value "1" -Encoding ASCII
        $Command += @(
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
        )
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
    else {
        Set-Content -LiteralPath (Join-Path $stageDir "stefx_xemu_memory_log.txt") -Value "1" -Encoding ASCII
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
        "--smoke-keymap"
    )

    if ($Headless) {
        $argsList += "--headless"
    }
    if ($NoScreenshots) {
        $argsList += "--no-screenshots"
    } else {
        $argsList += @(
            "--xemu-native-screenshots",
            "--xemu-screenshot-dir", (Join-Path (Split-Path -Parent $ConfigPath) "screenshots")
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($WatchCr2)) {
        $argsList += @("--watch-cr2", $WatchCr2)
    }
    if (-not [string]::IsNullOrWhiteSpace($MonitorKeys)) {
        $argsList += @("--monitor-keys", $MonitorKeys)
    }
    if ($PollXBlog) {
        $argsList += "--poll-xblog"
        if ($PollXBlogPerfOnly) {
            $argsList += "--poll-xblog-perf-only"
        }
        if ($PollXBlogStartDelay -gt 0) {
            $argsList += @("--poll-xblog-start-delay", $PollXBlogStartDelay)
        }
        if ($DirectHolomatch -and [string]::IsNullOrWhiteSpace($PollXBlogMap)) {
            $PollXBlogMap = Join-Path $repoRoot "build\release\efmp.map"
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogMap)) {
            if (-not [System.IO.Path]::IsPathRooted($PollXBlogMap)) {
                $PollXBlogMap = Join-Path $repoRoot $PollXBlogMap
            }
            $argsList += @("--map-file", [System.IO.Path]::GetFullPath($PollXBlogMap))
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogAddr)) {
            $argsList += @("--poll-xblog-addr", $PollXBlogAddr)
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogPhysAddr)) {
            $argsList += @("--poll-xblog-phys-addr", $PollXBlogPhysAddr)
        }
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogPhysDelta)) {
            $argsList += @("--poll-xblog-phys-delta", $PollXBlogPhysDelta)
        }
    }
    if ($XBlogAutoDumps) {
        $argsList += "--xblog-auto-dumps"
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
