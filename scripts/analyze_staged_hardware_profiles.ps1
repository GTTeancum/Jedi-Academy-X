param(
    [string]$StageDir = "",
    [string]$OutputBase = "hardware_profile_summary"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $StageDir = Join-Path $repoRoot "build\hardware\StarTrekEliteForceX-Beta-20260801"
}
$StageDir = [System.IO.Path]::GetFullPath($StageDir)

$analyzer = Join-Path $PSScriptRoot "analyze_hardware_profile.py"
if (-not (Test-Path -LiteralPath $analyzer -PathType Leaf)) {
    throw "Hardware profile analyzer not found: $analyzer"
}

$pythonCommand = Get-Command "python.exe" -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
$pythonExe = if ($null -ne $pythonCommand) {
    $pythonCommand.Source
}
else {
    Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
}
if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw "Python runtime not found: $pythonExe"
}

$manifestPath = Join-Path $StageDir "HARDWARE_PATCH_MANIFEST.json"
$manifestVersion = "unknown"
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $manifestVersion = (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json).version
}

$modes = [ordered]@{
    sp = "ef_sp_log.txt"
    mp = "ef_mp_log.txt"
}
$missing = @()
foreach ($entry in $modes.GetEnumerator()) {
    $path = Join-Path $StageDir $entry.Value
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $missing += $path
    }
}
if ($missing.Count -ne 0) {
    throw "Both hardware logs are required. Missing: $($missing -join ', ')"
}

$combined = [ordered]@{
    stageDir = $StageDir
    manifestVersion = $manifestVersion
    generatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    modes = [ordered]@{}
}
$textSections = @(
    "STEFX staged hardware renderer profile",
    "Stage: $StageDir",
    "Manifest: $manifestVersion",
    ""
)

foreach ($entry in $modes.GetEnumerator()) {
    $path = Join-Path $StageDir $entry.Value
    $jsonText = (& $pythonExe $analyzer $path --json | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Profile analysis failed for $path (exit $LASTEXITCODE)"
    }
    $combined.modes[$entry.Key] = ($jsonText | ConvertFrom-Json).candidate

    $humanText = (& $pythonExe $analyzer $path | Out-String).TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "Profile summary failed for $path (exit $LASTEXITCODE)"
    }
    $textSections += $entry.Key.ToUpperInvariant()
    $textSections += $humanText
    $textSections += ""
}

$jsonPath = Join-Path $StageDir ($OutputBase + ".json")
$textPath = Join-Path $StageDir ($OutputBase + ".txt")
$combined | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $jsonPath -Encoding UTF8
$textSections | Set-Content -LiteralPath $textPath -Encoding ASCII

Write-Host "Hardware profile summaries written:"
Write-Host "  $textPath"
Write-Host "  $jsonPath"
