param(
    [string]$Stage = "build\hardware\StarTrekEliteForceX-Beta-20260801",
    [switch]$Headless,
    [switch]$SkipAudit,
    [int]$SpDuration = 70,
    [int]$CoopDuration = 270,
    [int]$MpDuration = 150,
    [int]$Interval = 10,
    [int]$FirstShotDelay = 18,
    [string]$SpPort = "4460",
    [string]$CoopPort = "4461",
    [string]$MpPort = "4462",
    [string]$ReportOut = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$pythonExe = "python"
$stamp = [DateTime]::UtcNow.ToString("yyyyMMdd_HHmmss")
if ([string]::IsNullOrWhiteSpace($ReportOut)) {
    $ReportOut = Join-Path $repoRoot "scripts\output\xemu_qualification_proof_refresh_$stamp.json"
}
elseif (-not [System.IO.Path]::IsPathRooted($ReportOut)) {
    $ReportOut = Join-Path $repoRoot $ReportOut
}
$ReportOut = [System.IO.Path]::GetFullPath($ReportOut)

function Invoke-CheckedCommand {
    param(
        [string]$Label,
        [string[]]$Arguments
    )

    Write-Host "=== $Label ==="
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & powershell -NoProfile -ExecutionPolicy Bypass @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    foreach ($line in $output) {
        Write-Host $line
    }
    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode"
    }
    return $output
}

function Invoke-ProofCommand {
    param(
        [string]$Label,
        [string[]]$Arguments
    )

    $output = Invoke-CheckedCommand -Label $Label -Arguments $Arguments

    $reportLine = @($output | Where-Object { $_ -match '^report=(.+)$' } | Select-Object -Last 1)
    if ($reportLine.Count -eq 0) {
        throw "$Label did not emit a report= path"
    }
    $report = [regex]::Match([string]$reportLine[-1], '^report=(.+)$').Groups[1].Value
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "$Label emitted a missing report path: $report"
    }
    Write-Host "$Label report: $report"
    return [System.IO.Path]::GetFullPath($report)
}

function Write-RefreshReport {
    param(
        [string]$Path,
        [string]$GeneratedAtUtc,
        [string]$Stage,
        [bool]$Headless,
        [bool]$SkipAudit,
        [string[]]$ReleasePreflightCommand,
        [string[]]$ReleasePreflightOutput,
        [int]$SpDuration,
        [int]$CoopDuration,
        [int]$MpDuration,
        [int]$Interval,
        [int]$FirstShotDelay,
        [string]$SpPort,
        [string]$CoopPort,
        [string]$MpPort,
        [string]$SpReport,
        [string]$CoopReport,
        [string]$MpReport
    )

    $refreshReport = [ordered]@{
        reportType = "stefx-xemu-qualification-proof-refresh"
        reportSchemaVersion = 3
        generatedAtUtc = $GeneratedAtUtc
        repoRoot = $repoRoot
        stage = $Stage
        headless = $Headless
        skipAudit = $SkipAudit
        releasePreflight = [ordered]@{
            label = "Release freshness preflight"
            command = $ReleasePreflightCommand
            exitCode = 0
            output = $ReleasePreflightOutput
        }
        criteria = [ordered]@{
            interval = $Interval
            firstShotDelay = $FirstShotDelay
            pollXBlogStartDelay = 15
            pollXBlogInterval = 5
        }
        reports = [ordered]@{
            spBorg2 = [ordered]@{
                mode = "sp"
                map = "borg2"
                duration = $SpDuration
                port = $SpPort
                path = $SpReport
            }
            coopBorg1SplitScreen = [ordered]@{
                mode = "coop"
                map = "borg1"
                duration = $CoopDuration
                port = $CoopPort
                path = $CoopReport
            }
            hmBorg1Soak = [ordered]@{
                mode = "mp"
                map = "hm_borg1"
                duration = $MpDuration
                port = $MpPort
                path = $MpReport
            }
        }
    }

    $reportParent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($reportParent)) {
        New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
    }
    $refreshReport | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Path -Encoding UTF8
    return $Path
}

function Invoke-SelfTest {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("stefx_xemu_refresh_selftest_" + [guid]::NewGuid().ToString())
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    try {
        $sp = Join-Path $tempRoot "sp.report.txt"
        $coop = Join-Path $tempRoot "coop.report.txt"
        $mp = Join-Path $tempRoot "mp.report.txt"
        Set-Content -LiteralPath $sp -Value "report=sp" -Encoding ASCII
        Set-Content -LiteralPath $coop -Value "report=coop" -Encoding ASCII
        Set-Content -LiteralPath $mp -Value "report=mp" -Encoding ASCII
        $out = Join-Path $tempRoot "refresh.json"
        $written = Write-RefreshReport `
            -Path $out `
            -GeneratedAtUtc "2026-08-19T00:00:00.0000000Z" `
            -Stage "stage" `
            -Headless $true `
            -SkipAudit $true `
            -ReleasePreflightCommand @("-File", "scripts\stage_hardware_pk3_test.ps1", "-CheckFreshnessOnly") `
            -ReleasePreflightOutput @(
                "build_xbox.ps1 target contract ok",
                "build\release XBE freshness ok",
                "  build\release\default.xbe: modifiedUtc=2026-08-19T12:01:00.0000000Z",
                "  build\release\efmp.xbe: modifiedUtc=2026-08-19T12:02:00.0000000Z",
                "build\release PK3 freshness ok",
                "  build\release\BaseEF\xbox0.pk3: modifiedUtc=2026-08-19T12:03:00.0000000Z",
                "  build\release\BaseEF\xbox1.pk3: modifiedUtc=2026-08-19T12:04:00.0000000Z",
                "build\release XBE runtime build ids ok",
                "  build\release\default.xbe: STEFX_RUNTIME_BUILD_ID personality=default flavor=production log=ef_sp_log.txt",
                "  build\release\efmp.xbe: STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production log=ef_mp_log.txt"
            ) `
            -SpDuration 70 `
            -CoopDuration 270 `
            -MpDuration 150 `
            -Interval 10 `
            -FirstShotDelay 18 `
            -SpPort "4460" `
            -CoopPort "4461" `
            -MpPort "4462" `
            -SpReport $sp `
            -CoopReport $coop `
            -MpReport $mp
        if ($written -ne $out -or -not (Test-Path -LiteralPath $out -PathType Leaf)) {
            throw "refresh report was not written"
        }
        $json = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json
        $preflightOutput = @($json.releasePreflight.output)
        if (
            $json.reportType -ne "stefx-xemu-qualification-proof-refresh" -or
            $json.reportSchemaVersion -ne 3 -or
            $json.generatedAtUtc -ne "2026-08-19T00:00:00.0000000Z" -or
            $json.repoRoot -ne $repoRoot -or
            $json.stage -ne "stage" -or
            $json.headless -ne $true -or
            $json.skipAudit -ne $true -or
            $json.releasePreflight.label -ne "Release freshness preflight" -or
            $json.releasePreflight.exitCode -ne 0 -or
            -not ($preflightOutput -contains "build_xbox.ps1 target contract ok") -or
            -not ($preflightOutput -contains "build\release XBE freshness ok") -or
            -not ($preflightOutput -contains "build\release PK3 freshness ok") -or
            -not ($preflightOutput -contains "build\release XBE runtime build ids ok") -or
            -not ($preflightOutput -contains "  build\release\default.xbe: STEFX_RUNTIME_BUILD_ID personality=default flavor=production log=ef_sp_log.txt") -or
            -not ($preflightOutput -contains "  build\release\efmp.xbe: STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production log=ef_mp_log.txt") -or
            $json.criteria.interval -ne 10 -or
            $json.criteria.firstShotDelay -ne 18 -or
            $json.criteria.pollXBlogStartDelay -ne 15 -or
            $json.criteria.pollXBlogInterval -ne 5 -or
            $json.reports.spBorg2.mode -ne "sp" -or
            $json.reports.spBorg2.map -ne "borg2" -or
            $json.reports.spBorg2.duration -ne 70 -or
            $json.reports.spBorg2.port -ne "4460" -or
            $json.reports.spBorg2.path -ne $sp -or
            $json.reports.coopBorg1SplitScreen.mode -ne "coop" -or
            $json.reports.coopBorg1SplitScreen.map -ne "borg1" -or
            $json.reports.coopBorg1SplitScreen.duration -ne 270 -or
            $json.reports.coopBorg1SplitScreen.port -ne "4461" -or
            $json.reports.coopBorg1SplitScreen.path -ne $coop -or
            $json.reports.hmBorg1Soak.mode -ne "mp" -or
            $json.reports.hmBorg1Soak.map -ne "hm_borg1" -or
            $json.reports.hmBorg1Soak.duration -ne 150 -or
            $json.reports.hmBorg1Soak.port -ne "4462" -or
            $json.reports.hmBorg1Soak.path -ne $mp
        ) {
            throw "refresh report JSON shape is invalid"
        }
        Write-Host "refresh_xemu_qualification_proof self-test passed"
    }
    finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($SelfTest) {
    Invoke-SelfTest
    return
}

$releasePreflightCommand = @(
    "-File", (Join-Path $repoRoot "scripts\stage_hardware_pk3_test.ps1"),
    "-OutputDir", $Stage,
    "-CheckFreshnessOnly"
)
$releasePreflightOutput = Invoke-CheckedCommand "Release freshness preflight" $releasePreflightCommand

$commonSpArgs = @(
    "-File", (Join-Path $repoRoot "scripts\run_sp_xemu_smoke.ps1"),
    "-Interval", "$Interval",
    "-FirstShotDelay", "$FirstShotDelay",
    "-PollXBlog",
    "-PollXBlogStartDelay", "15",
    "-PollXBlogInterval", "5",
    "-EnableSmokeInput"
)
if ($Headless) {
    $commonSpArgs += "-Headless"
}

$spReport = Invoke-ProofCommand "XEMU SP borg2 proof" (
    $commonSpArgs + @(
        "-Map", "borg2",
        "-Name", "qualification-sp-borg2",
        "-Duration", "$SpDuration",
        "-Port", $SpPort
    )
)

$coopReport = Invoke-ProofCommand "XEMU co-op borg1 split-screen proof" (
    $commonSpArgs + @(
        "-DirectCoop",
        "-Map", "borg1",
        "-Name", "qualification-coop-borg1",
        "-Duration", "$CoopDuration",
        "-Port", $CoopPort
    )
)

$mpReport = Invoke-ProofCommand "XEMU Holomatch hm_borg1 proof" (
    $commonSpArgs + @(
        "-DirectHolomatch",
        "-Map", "hm_borg1",
        "-Name", "qualification-mp-hm-borg1",
        "-Duration", "$MpDuration",
        "-Port", $MpPort
    )
)

Write-Host "XEMU proof reports:"
Write-Host "  SP:    $spReport"
Write-Host "  Co-op: $coopReport"
Write-Host "  MP:    $mpReport"

$writtenRefreshReport = Write-RefreshReport `
    -Path $ReportOut `
    -GeneratedAtUtc ([DateTime]::UtcNow.ToString("o")) `
    -Stage $Stage `
    -Headless ([bool]$Headless) `
    -SkipAudit ([bool]$SkipAudit) `
    -ReleasePreflightCommand $releasePreflightCommand `
    -ReleasePreflightOutput @($releasePreflightOutput | ForEach-Object { $_.ToString() }) `
    -SpDuration $SpDuration `
    -CoopDuration $CoopDuration `
    -MpDuration $MpDuration `
    -Interval $Interval `
    -FirstShotDelay $FirstShotDelay `
    -SpPort $SpPort `
    -CoopPort $CoopPort `
    -MpPort $MpPort `
    -SpReport $spReport `
    -CoopReport $coopReport `
    -MpReport $mpReport
Write-Host "Proof refresh report: $writtenRefreshReport"

if ($SkipAudit) {
    return
}

$auditArgs = @(
    (Join-Path $repoRoot "scripts\qualify_hardware_stage.py"),
    "--stage", $Stage,
    "--xemu-refresh-report", $writtenRefreshReport
)

Push-Location $repoRoot
try {
    & $pythonExe @auditArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
