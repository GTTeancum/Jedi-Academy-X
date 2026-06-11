param(
    [string]$Map = "yavin1b",
    [string[]]$Maps = @(),
    [string[]]$Command = @(),
    [switch]$Headless,
    [switch]$Repack,
    [switch]$Build,
    [int]$Duration = 90,
    [int]$Interval = 10,
    [string]$Name = "",
    [string]$Iso = "",
    [string]$Port = "4460",
    [string[]]$DumpMem = @(),
    [string[]]$DumpPhys = @(),
    [string]$WatchCr2 = "",
    [switch]$PollXBlog,
    [string]$PollXBlogAddr = "",
    [switch]$VideoDebug,
    [switch]$NoScreenshots,
    [string[]]$XemuArg = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputDir = Join-Path $repoRoot "scripts\output"
$defaultIso = Join-Path $repoRoot "build\xemu\JediAcademyX_SP_direct.iso"
$stageDir = Join-Path $repoRoot "build\xemu\sp_direct_stage"
$stageXbe = Join-Path $stageDir "default.xbe"
$builtXbe = Join-Path $repoRoot "code\x_exe\Release\default.xbe"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

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
if (-not (Test-Path $stageDir)) {
    throw "SP stage directory not found: $stageDir"
}

$results = @()
foreach ($mapName in $Maps) {
    if ([string]::IsNullOrWhiteSpace($mapName)) {
        continue
    }

    $safeMap = ($mapName -replace '[^A-Za-z0-9_.-]', '_')
    $runName = if ([string]::IsNullOrWhiteSpace($Name)) { "ja_sp_$safeMap" } else { "${Name}_$safeMap" }

    Set-Content -LiteralPath (Join-Path $stageDir "ja_sp_level.txt") -Value $mapName -Encoding ASCII
    if ($Command.Count -gt 0) {
        Set-Content -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Value $Command -Encoding ASCII
    }
    else {
        Remove-Item -LiteralPath (Join-Path $stageDir "ja_sp_commands.txt") -Force -ErrorAction SilentlyContinue
    }

    if ($Repack) {
        if (-not (Test-Path $builtXbe)) {
            throw "Built XBE not found: $builtXbe"
        }
        Copy-Item -LiteralPath $builtXbe -Destination $stageXbe -Force

        $stamp = Get-Date -Format yyyyMMdd_HHmmss
        $repackLog = Join-Path $outputDir "repack_sp_${safeMap}_$stamp.log"
        Push-Location (Join-Path $repoRoot "build\xemu")
        try {
            Remove-Item -LiteralPath "JediAcademyX_SP_direct.iso" -Force -ErrorAction SilentlyContinue
            & $extractXiso -c sp_direct_stage JediAcademyX_SP_direct.iso *> $repackLog
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

    if (-not (Test-Path $Iso)) {
        throw "ISO not found: $Iso"
    }

    $argsList = @(
        (Join-Path $repoRoot "scripts\ja_xemu_smoke.py"),
        "--iso", $Iso,
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
    }
    if ($VideoDebug) {
        $argsList += "--video-debug"
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
