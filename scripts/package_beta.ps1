param(
    [string]$Version = "Beta-20260801",
    [string]$Iso = "build\xemu\StarTrekEliteForceX_Beta.iso"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if (-not [System.IO.Path]::IsPathRooted($Iso)) {
    $Iso = Join-Path $repoRoot $Iso
}
$Iso = [System.IO.Path]::GetFullPath($Iso)

$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"
$pythonCommand = Get-Command "python.exe" -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
$pythonExe = if ($null -ne $pythonCommand) {
    $pythonCommand.Source
}
else {
    Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
}
$releaseRoot = Join-Path $repoRoot "build\beta"
$packageDir = Join-Path $releaseRoot "StarTrekEliteForceX-$Version"
$packageIso = Join-Path $packageDir "StarTrekEliteForceX-$Version.iso"

if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "Beta ISO not found: $Iso"
}
if (-not (Test-Path -LiteralPath $extractXiso -PathType Leaf)) {
    throw "extract-xiso not found: $extractXiso"
}
if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw "Python interpreter not found: $pythonExe"
}

$listing = @(& $extractXiso -l $Iso)
if ($LASTEXITCODE -ne 0) {
    throw "Could not list beta ISO: $Iso"
}

$markerPattern = "(?i)(ef_(?:sp|mp|runtime)|ja_sp|stefx_xemu|memmap).*\.(?:txt|done)"
$markers = @($listing | Select-String -Pattern $markerPattern)
if ($markers.Count -gt 0) {
    throw "Beta ISO contains diagnostic marker(s): $($markers.Line -join ', ')"
}

foreach ($required in @(
    "\default.xbe",
    "\efmp.xbe",
    "\BaseEF\soundbank\sound.bnk",
    "\BaseEF\soundbank\sound.tbl",
    "\BaseEF\xbox0.pk3",
    "\BaseEF\xbox1.pk3"
)) {
    if (-not ($listing | Select-String -SimpleMatch $required -Quiet)) {
        throw "Beta ISO is missing required payload: $required"
    }
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
if (Test-Path -LiteralPath $packageIso -PathType Leaf) {
    Remove-Item -LiteralPath $packageIso -Force
}
try {
    New-Item -ItemType HardLink -Path $packageIso -Target $Iso -ErrorAction Stop | Out-Null
}
catch {
    Copy-Item -LiteralPath $Iso -Destination $packageIso -Force
}

$componentPaths = [ordered]@{
    "default.xbe" = Join-Path $repoRoot "build\release\default.xbe"
    "efmp.xbe" = Join-Path $repoRoot "build\release\efmp.xbe"
    "BaseEF/xbox0.pk3" = Join-Path $repoRoot "build\release\BaseEF\xbox0.pk3"
    "BaseEF/xbox1.pk3" = Join-Path $repoRoot "build\release\BaseEF\xbox1.pk3"
    "BaseEF/soundbank/sound.bnk" = Join-Path $repoRoot "build\release\BaseEF\soundbank\sound.bnk"
    "BaseEF/soundbank/sound.tbl" = Join-Path $repoRoot "build\release\BaseEF\soundbank\sound.tbl"
}

$components = [ordered]@{}
foreach ($entry in $componentPaths.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Built beta component not found: $($entry.Value)"
    }
    $item = Get-Item -LiteralPath $entry.Value
    $hash = Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256
    $components[$entry.Key] = [ordered]@{
        bytes = $item.Length
        sha256 = $hash.Hash
    }
}

$isoItem = Get-Item -LiteralPath $packageIso
$isoHash = Get-FileHash -LiteralPath $packageIso -Algorithm SHA256
$gitRevision = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitDirty = @(& git -C $repoRoot status --porcelain).Count -gt 0

$manifest = [ordered]@{
    name = "Star Trek: Elite Force X"
    version = $Version
    generatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    sourceRevision = $gitRevision
    sourceTreeDirty = $gitDirty
    architecture = [ordered]@{
        entryPoint = "default.xbe"
        singlePlayerAndCoop = "default.xbe"
        holomatch = "efmp.xbe"
        sharedRuntime = "BaseEF"
        deprecatedCodempDependency = $false
    }
    iso = [ordered]@{
        file = [System.IO.Path]::GetFileName($packageIso)
        bytes = $isoItem.Length
        sha256 = $isoHash.Hash
        diagnosticMarkers = 0
    }
    components = $components
    qualification = @(
        "scripts/output/stefx-beta-final-minisoak2_borg2_20260801_101352.report.txt",
        "scripts/output/stefx-beta-xbe-roundtrip6_normal_20260801_095353.report.txt",
        "scripts/output/stefx-beta-cleaniso-final_normal_20260801_114812.report.txt",
        "HOLOMATCH_QUALIFICATION.md"
    )
    deferred = @(
        "XEMU frame-rate optimization",
        "Four-player split-screen"
    )
}

$manifestPath = Join-Path $packageDir "release_manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$checksumsPath = Join-Path $packageDir "SHA256SUMS.txt"
"$($isoHash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($packageIso))" |
    Set-Content -LiteralPath $checksumsPath -Encoding ASCII

$readme = @"
Star Trek: Elite Force X - $Version

This beta contains the qualified unified Xbox runtime:

- default.xbe: single-player campaign and two-player cooperative play
- efmp.xbe: Holomatch multiplayer
- BaseEF: shared renderer, audio, input, collision, UI assets, and game data

Launch default.xbe. The shared main menu can start campaign, cooperative play,
or hand off to efmp.xbe for Holomatch. The Holomatch frontend can return to
default.xbe. To install on a softmodded Xbox, extract the XISO without changing
its directory layout.

Qualification completed in XEMU/LLE:

- Campaign boot, loading, gameplay, and return to the main menu
- Two-player co-op split-screen, independent P2 input, and viewport parity
- Holomatch FFA and CTF with bots, weapons, damage, pickups, HUD, audio, and loading
- default.xbe to efmp.xbe to default.xbe handoff
- Continuous SP to co-op to Holomatch mini-soak
- Marker-free release-disc boot

Known post-beta work:

- XEMU frame-rate optimization remains open. Functional stalls and crashes are
  release blockers; raw FPS tuning is deferred.
- Four-player split-screen remains a future feature.

Verify the XISO with SHA256SUMS.txt. Detailed hashes and proof references are in
release_manifest.json.
"@
$readme | Set-Content -LiteralPath (Join-Path $packageDir "README.txt") -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $repoRoot "HOLOMATCH_QUALIFICATION.md") `
    -Destination (Join-Path $packageDir "QUALIFICATION.md") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "GAME_TODO.md") `
    -Destination (Join-Path $packageDir "GAME_TODO.md") -Force

$verificationPath = Join-Path $packageDir "holomatch_verification.json"
& $pythonExe (Join-Path $repoRoot "scripts\check_mp_holomatch_ui.py") `
    --repo-root $repoRoot `
    --pk3 (Join-Path $repoRoot "build\release\BaseEF\xbox1.pk3") `
    --stage-baseef (Join-Path $repoRoot "build\release\BaseEF") `
    --allow-stage-original-images `
    --xbe (Join-Path $repoRoot "build\release\efmp.xbe") `
    --direct-map hm_borg1 `
    --code-only > $verificationPath
if ($LASTEXITCODE -ne 0) {
    throw "Holomatch beta verification failed with exit code $LASTEXITCODE"
}

Write-Host "Beta package ready: $packageDir"
Write-Host "ISO SHA256: $($isoHash.Hash)"
