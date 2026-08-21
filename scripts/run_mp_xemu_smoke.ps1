param(
    [switch]$Headless,
    [switch]$Repack,
    [int]$Duration = 90,
    [int]$Interval = 10,
    [string]$Name = "ja_mp_smoke",
    [string]$Iso = "",
    [string]$Port = "4460",
    [string[]]$DumpMem = @(),
    [string]$WatchCr2 = "",
    [string[]]$XemuArg = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

# The shipping JA MP XBE also crashes CXBX-R. Qualify the JA-derived shared
# renderer in XEMU/LLE, then use retail hardware for final performance proof.

$defaultIso = Join-Path $repoRoot "build\xemu\JediAcademyX_MP_direct.iso"
$stageXbe = Join-Path $repoRoot "build\xemu\mp_direct_stage\efmp.xbe"
$stageDefaultXbe = Join-Path $repoRoot "build\xemu\mp_direct_stage\default.xbe"
$builtXbe = Join-Path $repoRoot "build\release\efmp.xbe"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

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

if ([string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = $defaultIso
}

if (-not (Test-Path -LiteralPath $builtXbe -PathType Leaf)) {
    throw "Built XBE not found: $builtXbe"
}
$builtRuntimeBuildId = Get-XbeRuntimeBuildId -Path $builtXbe
if (-not $builtRuntimeBuildId) {
    throw "Built efmp.xbe is missing STEFX_RUNTIME_BUILD_ID; rebuild scripts\build_xbox.ps1 -Target spmp before MP XEMU proof."
}
foreach ($fragment in @("personality=efmp", "log=ef_mp_log.txt")) {
    if ($builtRuntimeBuildId -notlike "*$fragment*") {
        throw "Built efmp.xbe has wrong STEFX_RUNTIME_BUILD_ID identity: expected fragment '$fragment' in '$builtRuntimeBuildId'"
    }
}
Write-Host "Runtime build id: build\release\efmp.xbe -> $builtRuntimeBuildId"

if (-not $Repack) {
    if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
        Write-Host "Retained MP XEMU ISO is missing; enabling repack."
        $Repack = $true
    }
    elseif (-not (Test-Path -LiteralPath $stageXbe -PathType Leaf)) {
        Write-Host "MP XEMU stage efmp.xbe is missing; enabling repack."
        $Repack = $true
    }
    elseif ((Get-Item -LiteralPath $stageXbe).LastWriteTimeUtc -gt (Get-Item -LiteralPath $Iso).LastWriteTimeUtc) {
        Write-Host "MP XEMU stage efmp.xbe is newer than retained ISO; enabling repack."
        $Repack = $true
    }
    elseif ((Get-FileSha256 $stageXbe) -ne (Get-FileSha256 $builtXbe)) {
        Write-Host "MP XEMU stage efmp.xbe differs from build\release\efmp.xbe; enabling repack."
        $Repack = $true
    }
}

if ($Repack) {
    if (-not (Test-Path $builtXbe)) {
        throw "Built XBE not found: $builtXbe"
    }
    if (-not (Test-Path $extractXiso)) {
        throw "extract-xiso not found: $extractXiso"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $stageXbe) | Out-Null
    if (Test-Path $stageDefaultXbe) {
        throw "MP smoke stage already contains default.xbe; refusing to overwrite or remove the SP/co-op launch name. Stage a launcher manually or remove the stale file after confirming it is not the SP/co-op artifact."
    }
    Copy-Item -LiteralPath $builtXbe -Destination $stageXbe -Force

    $stamp = Get-Date -Format yyyyMMdd_HHmmss
    $log = Join-Path $repoRoot "scripts\output\repack_mp_smoke_$stamp.log"
    Push-Location (Join-Path $repoRoot "build\xemu")
    try {
        & $extractXiso -c mp_direct_stage JediAcademyX_MP_direct.iso *> $log
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $log -Tail 80
            throw "extract-xiso failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    Write-Host "Repacked ISO: $Iso"
    Write-Host "Repack log: $log"
}

if (-not (Test-Path $Iso)) {
    throw "ISO not found: $Iso"
}

$argsList = @(
    (Join-Path $repoRoot "scripts\ja_xemu_smoke.py"),
    "--iso", $Iso,
    "--name", $Name,
    "--port", $Port,
    "--duration", "$Duration",
    "--interval", "$Interval",
    "--proof-mode", "mp",
    "--proof-map", "hm_borg1",
    "--runtime-xbe", $builtXbe,
    "--require-runtime-xbe-id",
    "--smoke-keymap"
)

if ($Headless) {
    $argsList += "--headless"
}

if (-not [string]::IsNullOrWhiteSpace($WatchCr2)) {
    $argsList += @("--watch-cr2", $WatchCr2)
}

foreach ($spec in $DumpMem) {
    if (-not [string]::IsNullOrWhiteSpace($spec)) {
        $argsList += @("--dump-mem", $spec)
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
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
