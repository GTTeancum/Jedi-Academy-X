param(
    [string]$Version = "Beta-20260801",
    [string]$Iso = "build\beta\StarTrekEliteForceX-Beta-20260801\StarTrekEliteForceX-Beta-20260801.iso",
    [string]$OutputDir = "build\hardware\StarTrekEliteForceX-Beta-20260801"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$hardwareRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build\hardware"))
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

function Resolve-RepoPath {
    param([string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $repoRoot $Path
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-HardwareStagePath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $prefix = $hardwareRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing hardware-stage operation outside ${hardwareRoot}: $fullPath"
    }
}

function Get-XisoMediaPatchOffset {
    param(
        [string]$BuiltXbe,
        [string]$ExtractedXbe
    )

    [byte[]]$built = [System.IO.File]::ReadAllBytes($BuiltXbe)
    [byte[]]$extracted = [System.IO.File]::ReadAllBytes($ExtractedXbe)
    if ($built.Length -ne $extracted.Length) {
        return -1
    }

    $offset = -1
    for ($i = 0; $i -lt $built.Length; $i++) {
        if ($built[$i] -eq $extracted[$i]) {
            continue
        }
        if ($offset -ge 0) {
            return -1
        }
        $offset = $i
    }

    if ($offset -lt 0 -or $built[$offset] -ne 0x7D -or $extracted[$offset] -ne 0xEB) {
        return -1
    }
    return $offset
}

$Iso = Resolve-RepoPath $Iso
$OutputDir = Resolve-RepoPath $OutputDir
$tempDir = "$OutputDir.extracting"
$packageDir = Split-Path -Parent $Iso
$releaseManifestPath = Join-Path $packageDir "release_manifest.json"

Assert-HardwareStagePath $OutputDir
Assert-HardwareStagePath $tempDir

if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "Qualified beta XISO not found: $Iso"
}
if (-not (Test-Path -LiteralPath $releaseManifestPath -PathType Leaf)) {
    throw "Beta release manifest not found: $releaseManifestPath"
}
if (-not (Test-Path -LiteralPath $extractXiso -PathType Leaf)) {
    throw "extract-xiso not found: $extractXiso"
}

$releaseManifest = Get-Content -LiteralPath $releaseManifestPath -Raw | ConvertFrom-Json
$isoHash = (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash
if ($isoHash -ne [string]$releaseManifest.iso.sha256) {
    throw "Beta XISO hash does not match release_manifest.json"
}

New-Item -ItemType Directory -Path $hardwareRoot -Force | Out-Null
foreach ($path in @($tempDir, $OutputDir)) {
    if (Test-Path -LiteralPath $path) {
        Assert-HardwareStagePath $path
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

& $extractXiso -q -d $tempDir -x $Iso
if ($LASTEXITCODE -ne 0) {
    throw "extract-xiso failed with exit code $LASTEXITCODE"
}

$stageRoot = $tempDir
if (-not (Test-Path -LiteralPath (Join-Path $stageRoot "default.xbe") -PathType Leaf)) {
    $candidates = @(
        Get-ChildItem -LiteralPath $tempDir -Recurse -File -Filter "default.xbe" |
            Select-Object -ExpandProperty DirectoryName -Unique
    )
    if ($candidates.Count -ne 1) {
        throw "Could not identify one extracted game root under $tempDir"
    }
    $stageRoot = $candidates[0]
}

$required = [ordered]@{
    "default.xbe" = "default.xbe"
    "efmp.xbe" = "efmp.xbe"
    "BaseEF/xbox0.pk3" = "BaseEF\xbox0.pk3"
    "BaseEF/xbox1.pk3" = "BaseEF\xbox1.pk3"
    "BaseEF/soundbank/sound.bnk" = "BaseEF\soundbank\sound.bnk"
    "BaseEF/soundbank/sound.tbl" = "BaseEF\soundbank\sound.tbl"
}

$verified = [ordered]@{}
foreach ($entry in $required.GetEnumerator()) {
    $path = Join-Path $stageRoot $entry.Value
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Hardware stage is missing required payload: $($entry.Value)"
    }

    $expected = $releaseManifest.components.($entry.Key)
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($item.Length -ne [long]$expected.bytes) {
        throw "Hardware-stage payload mismatch: $($entry.Value)"
    }

    $record = [ordered]@{
        bytes = $item.Length
        sha256 = $hash
        xisoMediaEnablePatch = $false
    }
    if ($hash -ne [string]$expected.sha256) {
        if (-not $entry.Key.EndsWith(".xbe", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Hardware-stage payload mismatch: $($entry.Value)"
        }

        $builtXbe = Join-Path $repoRoot (Join-Path "build\release" $entry.Value)
        if (-not (Test-Path -LiteralPath $builtXbe -PathType Leaf) -or
            (Get-FileHash -LiteralPath $builtXbe -Algorithm SHA256).Hash -ne [string]$expected.sha256) {
            throw "Built XBE no longer matches the beta manifest: $builtXbe"
        }
        $patchOffset = Get-XisoMediaPatchOffset -BuiltXbe $builtXbe -ExtractedXbe $path
        if ($patchOffset -lt 0) {
            throw "Extracted XBE differs beyond the expected XISO media-enable patch: $($entry.Value)"
        }
        $record["xisoMediaEnablePatch"] = $true
        $record["sourceSha256"] = [string]$expected.sha256
        $record["mediaPatchOffset"] = $patchOffset
    }
    $verified[$entry.Key] = $record
}

$markerPattern = "(?i)^(ef_(?:sp|mp|runtime)|ja_sp|stefx_xemu|memmap).*\.(?:txt|done)$"
$markers = @(
    Get-ChildItem -LiteralPath $stageRoot -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match $markerPattern }
)
if ($markers.Count -gt 0) {
    throw "Hardware stage contains diagnostic marker(s): $($markers.Name -join ', ')"
}

if ($stageRoot -eq $tempDir) {
    Move-Item -LiteralPath $tempDir -Destination $OutputDir
}
else {
    Move-Item -LiteralPath $stageRoot -Destination $OutputDir
    if (Test-Path -LiteralPath $tempDir) {
        Assert-HardwareStagePath $tempDir
        Remove-Item -LiteralPath $tempDir -Recurse -Force
    }
}

$files = Get-ChildItem -LiteralPath $OutputDir -Recurse -File
$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
$stageManifest = [ordered]@{
    name = "Star Trek: Elite Force X hardware transfer stage"
    version = $Version
    generatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    sourceIso = [System.IO.Path]::GetFileName($Iso)
    sourceIsoSha256 = $isoHash
    rootEntryPoint = "default.xbe"
    holomatchExecutable = "efmp.xbe"
    payloadFiles = $files.Count
    payloadBytes = $totalBytes
    diagnosticMarkers = 0
    verifiedComponents = $verified
}
$stageManifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $OutputDir "HARDWARE_STAGE_MANIFEST.json") -Encoding UTF8

$readme = @"
Star Trek: Elite Force X - $Version
Hardware Transfer Stage

Transfer the CONTENTS of this directory to one Xbox game directory. Keep the
layout unchanged:

  default.xbe
  efmp.xbe
  BaseEF\

Launch default.xbe from the dashboard. Campaign and cooperative play remain in
default.xbe; the Holomatch menu item hands off to efmp.xbe.

Runtime logs are written to the game directory through D:, with E: fallback:

  ef_sp_log.txt
  ef_mp_log.txt

This stage was extracted from the qualified marker-free beta XISO. The six
runtime-critical components were checked against release_manifest.json.
"@
$readme | Set-Content -LiteralPath (Join-Path $OutputDir "TRANSFER_README.txt") -Encoding ASCII

Write-Host "Hardware transfer stage ready: $OutputDir"
Write-Host "Files: $($files.Count)"
Write-Host "Bytes: $totalBytes"
Write-Host "Source XISO SHA256: $isoHash"
