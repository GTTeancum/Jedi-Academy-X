param(
    [string]$Map = "borg6",
    [string[]]$Maps = @(),
    [string[]]$Command = @(),
    [string[]]$PostMapCommand = @(),
    [switch]$Headless,
    [switch]$Repack,
    [switch]$Build,
    [int]$Duration = 90,
    [int]$Interval = 10,
    [string]$Name = "",
    [string]$Iso = "",
    [string]$Port = "4460",
    [string[]]$DumpMem = @(),
    [string[]]$DumpBinMem = @(),
    [string[]]$DumpPhys = @(),
    [string]$WatchCr2 = "",
    [switch]$PollXBlog,
    [switch]$XBlogAutoDumps,
    [string]$PollXBlogAddr = "",
    [string]$PollXBlogPhysDelta = "0x284000",
    [switch]$VideoDebug,
    [switch]$NoScreenshots,
    [string[]]$XemuArg = @(),
    [string]$Hdd = "C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2",
    [string]$XemuExe = "C:\Games\Emulators\Xemu\JACodex\xemu.exe",
    [string]$ConfigPath = "C:\Games\Emulators\Xemu\JACodex\xemu.toml",
    [switch]$RepackOnly,
    [switch]$KeepIso
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputDir = Join-Path $repoRoot "scripts\output"
$defaultIso = Join-Path $repoRoot "build\xemu\StarTrekEliteForceX_SP_direct.iso"
$stageDir = Join-Path $repoRoot "build\xemu\sp_direct_stage"
$stageXbe = Join-Path $stageDir "default.xbe"
$builtXbe = Join-Path $repoRoot "build\release\default.xbe"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

if ([string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = $defaultIso
}

$normalizedMaps = @()
if ($Maps.Count -eq 0) {
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

function Normalize-CommandLines {
    param(
        [string[]]$Values
    )

    $normalized = @()
    foreach ($entry in $Values) {
        foreach ($piece in ($entry -split ',')) {
            $trimmed = $piece.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                $normalized += $trimmed
            }
        }
    }
    return $normalized
}

$Command = @(Normalize-CommandLines -Values $Command)
$PostMapCommand = @(Normalize-CommandLines -Values $PostMapCommand)

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

    $excludeRootNames = @(
        "ef_sp_log.txt",
        "ef_sp_level.txt",
        "ef_sp_commands.txt",
        "ef_sp_postmap_commands.txt",
        "ef_sp_active_commands.txt",
        "ef_sp_active_command_time.txt",
        "ja_sp_level.txt",
        "ja_sp_commands.txt",
        "ja_sp_postmap_commands.txt",
        "memmap.txt",
        "base"
    )

    $stageScript = @'
import os
import shutil
import sys

src = os.path.abspath(sys.argv[1])
dst = os.path.abspath(sys.argv[2])
exclude_root = set(sys.argv[3:])

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
        try:
            os.link(source_file, target_file)
        except OSError:
            shutil.copy2(source_file, target_file)
'@
    $stageArgs = @("-c", $stageScript, $Source, $Destination) + $excludeRootNames
    python @stageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to stage EF XEMU tree from $Source"
    }
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

if (-not (Test-Path $extractXiso)) {
    throw "extract-xiso not found: $extractXiso"
}
if ($Repack) {
    $stageSource = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X"
    if (-not (Test-Path $stageSource)) {
        $stageSource = Join-Path $repoRoot "build\release"
    }
    Initialize-StageFromSource -Source $stageSource -Destination $stageDir
}
elseif (-not (Test-Path $stageDir)) {
    throw "SP stage directory not found: $stageDir"
}

$results = @()
foreach ($mapName in $Maps) {
    if ([string]::IsNullOrWhiteSpace($mapName)) {
        continue
    }

    $safeMap = ($mapName -replace '[^A-Za-z0-9_.-]', '_')
    $runName = if ([string]::IsNullOrWhiteSpace($Name)) { "ef_sp_$safeMap" } else { "${Name}_$safeMap" }

    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_level.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_postmap_commands.txt") -Force -ErrorAction SilentlyContinue

    Set-Content -LiteralPath (Join-Path $stageDir "ef_sp_level.txt") -Value $mapName -Encoding ASCII
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
    Remove-Item -LiteralPath (Join-Path $stageDir "ef_sp_log.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $stageDir "memmap.txt") -Force -ErrorAction SilentlyContinue

    if (-not $Repack) {
        $controlFiles = @(
            (Join-Path $stageDir "ef_sp_level.txt"),
            (Join-Path $stageDir "ef_sp_commands.txt"),
            (Join-Path $stageDir "ef_sp_postmap_commands.txt")
        ) | Where-Object { Test-Path -LiteralPath $_ }
        $isoWriteTime = if (Test-Path -LiteralPath $Iso) {
            [System.IO.File]::GetLastWriteTimeUtc($Iso)
        }
        else {
            [DateTime]::MinValue
        }
        $newerControls = @($controlFiles | Where-Object {
            [System.IO.File]::GetLastWriteTimeUtc($_) -gt $isoWriteTime
        })

        if ($newerControls.Count -gt 0) {
            Write-Warning "Stage control files are newer than the ISO. Use -Repack for authoritative map/command changes; otherwise this run may launch stale ISO contents."
        }
    }

    if ($Repack) {
        if (-not (Test-Path $builtXbe)) {
            throw "Built XBE not found: $builtXbe"
        }
        Copy-Item -LiteralPath $builtXbe -Destination $stageXbe -Force

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
        Write-Host "Repacked SP ISO for $mapName"
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
        "--xemu-exe", $XemuExe,
        "--name", $runName,
        "--port", $Port,
        "--duration", "$Duration",
        "--interval", "$Interval",
        "--smoke-keymap"
    )

    if ($Headless) {
        $argsList += "--headless"
    }
    if ($NoScreenshots) {
        $argsList += "--no-screenshots"
    }
    if (-not [string]::IsNullOrWhiteSpace($WatchCr2)) {
        $argsList += @("--watch-cr2", $WatchCr2)
    }
    if ($PollXBlog) {
        $argsList += "--poll-xblog"
        if (-not [string]::IsNullOrWhiteSpace($PollXBlogAddr)) {
            $argsList += @("--poll-xblog-addr", $PollXBlogAddr)
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
            $argsList += @("--xemu-arg", $arg)
        }
    }

    Push-Location $repoRoot
    try {
        python @argsList
        $results += [pscustomobject]@{ Map = $mapName; ExitCode = $LASTEXITCODE; Name = $runName }
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
    exit 1
}
