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
$defaultIso = Join-Path $repoRoot "build\xemu\JediAcademyX_MP_direct.iso"
$stageXbe = Join-Path $repoRoot "build\xemu\mp_direct_stage\default.xbe"
$builtXbe = Join-Path $repoRoot "codemp\x_exe\Release\jamp.xbe"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

if ([string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = $defaultIso
}

if ($Repack) {
    if (-not (Test-Path $builtXbe)) {
        throw "Built XBE not found: $builtXbe"
    }
    if (-not (Test-Path $extractXiso)) {
        throw "extract-xiso not found: $extractXiso"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $stageXbe) | Out-Null
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
