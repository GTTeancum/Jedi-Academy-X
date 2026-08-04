param(
    [string]$Version = "Beta-20260801-hwfix2",
    [string]$OutputDir = "build\hardware\StarTrekEliteForceX-Beta-20260801"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$hardwareRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build\hardware"))
$releaseRoot = Join-Path $repoRoot "build\release"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"

function Resolve-RepoPath {
    param([string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $repoRoot $Path
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-HardwarePath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $prefix = $hardwareRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing hardware-stage operation outside ${hardwareRoot}: $fullPath"
    }
}

function Get-SingleByteMediaPatch {
    param(
        [string]$RawXbe,
        [string]$PatchedXbe
    )

    [byte[]]$raw = [System.IO.File]::ReadAllBytes($RawXbe)
    [byte[]]$patched = [System.IO.File]::ReadAllBytes($PatchedXbe)
    if ($raw.Length -ne $patched.Length) {
        return -1
    }

    $offset = -1
    for ($i = 0; $i -lt $raw.Length; $i++) {
        if ($raw[$i] -eq $patched[$i]) {
            continue
        }
        if ($offset -ge 0) {
            return -1
        }
        $offset = $i
    }

    if ($offset -lt 0 -or $raw[$offset] -ne 0x7D -or $patched[$offset] -ne 0xEB) {
        return -1
    }
    return $offset
}

$OutputDir = Resolve-RepoPath $OutputDir
$tempSource = "$OutputDir.source"
$tempIso = "$OutputDir.iso"
$tempExtract = "$OutputDir.extracting"

foreach ($path in @($OutputDir, $tempSource, $tempIso, $tempExtract)) {
    Assert-HardwarePath $path
}
if (-not (Test-Path -LiteralPath $extractXiso -PathType Leaf)) {
    throw "extract-xiso not found: $extractXiso"
}

$sources = [ordered]@{
    "default.xbe" = Join-Path $releaseRoot "default.xbe"
    "efmp.xbe" = Join-Path $releaseRoot "efmp.xbe"
    "BaseEF/xbox0.pk3" = Join-Path $releaseRoot "BaseEF\xbox0.pk3"
    "BaseEF/xbox1.pk3" = Join-Path $releaseRoot "BaseEF\xbox1.pk3"
}
foreach ($source in $sources.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release artifact not found: $source"
    }
}

New-Item -ItemType Directory -Path $hardwareRoot -Force | Out-Null
foreach ($path in @($tempSource, $tempExtract)) {
    if (Test-Path -LiteralPath $path) {
        Assert-HardwarePath $path
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
if (Test-Path -LiteralPath $tempIso) {
    Assert-HardwarePath $tempIso
    Remove-Item -LiteralPath $tempIso -Force
}

New-Item -ItemType Directory -Path $tempSource -Force | Out-Null
Copy-Item -LiteralPath $sources["default.xbe"] -Destination (Join-Path $tempSource "default.xbe")
Copy-Item -LiteralPath $sources["efmp.xbe"] -Destination (Join-Path $tempSource "efmp.xbe")

# Creation applies extract-xiso's normal HDD/media-enable patch to both XBEs.
# Keeping the PK3s out of this temporary image avoids duplicating roughly
# 500 MB of unchanged runtime data during every hardware iteration.
& $extractXiso -q -c $tempSource $tempIso
if ($LASTEXITCODE -ne 0) {
    throw "extract-xiso create failed with exit code $LASTEXITCODE"
}
New-Item -ItemType Directory -Path $tempExtract -Force | Out-Null
& $extractXiso -q -d $tempExtract -x $tempIso
if ($LASTEXITCODE -ne 0) {
    throw "extract-xiso extraction failed with exit code $LASTEXITCODE"
}

$verified = [ordered]@{}
New-Item -ItemType Directory -Path (Join-Path $OutputDir "BaseEF") -Force | Out-Null
foreach ($entry in $sources.GetEnumerator()) {
    $relativePath = $entry.Key.Replace('/', '\')
    $sourceHash = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash
    $outputPath = Join-Path $OutputDir $relativePath

    if ($entry.Key.EndsWith(".xbe", [StringComparison]::OrdinalIgnoreCase)) {
        $stagedPath = Join-Path $tempExtract $relativePath
        if (-not (Test-Path -LiteralPath $stagedPath -PathType Leaf)) {
            throw "Patched XBE missing from temporary image: $relativePath"
        }
        Copy-Item -LiteralPath $stagedPath -Destination $outputPath -Force
    }
    elseif (-not (Test-Path -LiteralPath $outputPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash -ne $sourceHash) {
        Copy-Item -LiteralPath $entry.Value -Destination $outputPath -Force
    }

    $stageHash = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash
    $record = [ordered]@{
        bytes = (Get-Item -LiteralPath $outputPath).Length
        sha256 = $stageHash
        sourceSha256 = $sourceHash
    }

    if ($entry.Key.EndsWith(".xbe", [StringComparison]::OrdinalIgnoreCase)) {
        $patchOffset = Get-SingleByteMediaPatch -RawXbe $entry.Value -PatchedXbe $outputPath
        if ($patchOffset -lt 0) {
            throw "XBE does not contain exactly the expected media-enable patch: $relativePath"
        }
        $record["mediaEnablePatchOffset"] = $patchOffset
    }
    elseif ($sourceHash -ne $stageHash) {
        throw "PK3 changed while staging: $relativePath"
    }

    $verified[$entry.Key] = $record
}

$manifest = [ordered]@{
    name = "Star Trek: Elite Force X PK3-only hardware test patch"
    version = $Version
    generatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    files = $verified
}
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $OutputDir "HARDWARE_PATCH_MANIFEST.json") -Encoding UTF8

$readme = @"
Star Trek: Elite Force X - $Version
PK3-only Hardware Test Patch

Replace exactly these files in the existing Xbox game directory:

  default.xbe
  efmp.xbe
  BaseEF\xbox0.pk3
  BaseEF\xbox1.pk3

Keep the retail BaseEF\pak[x].pk3 files in place. They own the base game
data; xbox[x].pk3 contains Xbox-specific patches and overrides only.

Do not transfer loose asset directories from a PC staging tree.

For a normal menu boot, remove diagnostic launch markers such as
ef_sp_level.txt from the Xbox game directory. Logs remain ef_sp_log.txt and
ef_mp_log.txt in that directory.
"@
$readme | Set-Content -LiteralPath (Join-Path $OutputDir "TRANSFER_README.txt") -Encoding ASCII

foreach ($path in @($tempSource, $tempExtract, $tempIso)) {
    if (Test-Path -LiteralPath $path) {
        Assert-HardwarePath $path
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

Write-Host "PK3-only hardware patch ready: $OutputDir"
Write-Host "Files: default.xbe, efmp.xbe, BaseEF\xbox0.pk3, BaseEF\xbox1.pk3"
