param(
    [string]$Version = "Beta-20260819-hm-hotlog-off-v77",
    [string]$OutputDir = "build\hardware\StarTrekEliteForceX-Beta-20260801",
    [switch]$FrameDiagnostics,
    [switch]$CheckFreshnessOnly,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$hardwareRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build\hardware"))
$releaseRoot = Join-Path $repoRoot "build\release"
$extractXiso = "C:\nxdk\tools\extract-xiso\build\extract-xiso.exe"
$observationSchemaVersion = 3

function Resolve-RepoPath {
    param([string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $repoRoot $Path
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-DisplayRepoPath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPrefix = $repoRoot.TrimEnd('\') + '\'
    if ($fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($rootPrefix.Length)
    }
    return $fullPath
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

function Get-RuntimeBuildId {
    param([string]$XbePath)

    [byte[]]$data = [System.IO.File]::ReadAllBytes($XbePath)
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
        $maxEnd = [Math]::Min($data.Length, $i + 256)
        while ($end -lt $maxEnd -and $data[$end] -ne 0 -and $data[$end] -ne 10 -and $data[$end] -ne 13) {
            $end++
        }
        if ($end -gt $i) {
            return [System.Text.Encoding]::ASCII.GetString($data, $i, $end - $i)
        }
    }

    return $null
}

function Assert-ReleaseArtifactsFresh {
    param(
        [string[]]$ReleaseXbes,
        [string]$SourceRoot = (Join-Path $repoRoot "code")
    )

    $sourceExtensions = @(
        ".asm",
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hpp",
        ".inl",
        ".rc",
        ".vcproj",
        ".vsh",
        ".psh"
    )
    $oldestReleaseUtc = $null
    foreach ($xbe in $ReleaseXbes) {
        $item = Get-Item -LiteralPath $xbe
        if ($null -eq $oldestReleaseUtc -or $item.LastWriteTimeUtc -lt $oldestReleaseUtc) {
            $oldestReleaseUtc = $item.LastWriteTimeUtc
        }
    }

    $newerSources = @(
        Get-ChildItem -LiteralPath $SourceRoot -Recurse -ErrorAction Stop |
            Where-Object {
                -not $_.PSIsContainer -and
                $sourceExtensions -contains $_.Extension.ToLowerInvariant() -and
                $_.LastWriteTimeUtc -gt $oldestReleaseUtc
            } |
            Sort-Object FullName |
            Select-Object -First 8
    )

    if ($newerSources.Count -gt 0) {
        $details = ($newerSources | ForEach-Object {
            "{0} modifiedUtc={1:o}" -f (Get-DisplayRepoPath $_.FullName), $_.LastWriteTimeUtc
        }) -join "; "
        throw "Runtime source is newer than build\release XBE artifacts; rebuild scripts\build_xbox.ps1 -Target spmp before staging. Newer source sample: $details"
    }
}

function Get-PackageFreshnessInputs {
    param([string]$SourceRoot = (Join-Path $repoRoot "code"))

    $sourceExtensions = @(
        ".asm",
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hpp",
        ".inl",
        ".rc",
        ".vcproj",
        ".vsh",
        ".psh"
    )
    $inputs = @()
    if (Test-Path -LiteralPath $SourceRoot -PathType Container) {
        $inputs += Get-ChildItem -LiteralPath $SourceRoot -Recurse -ErrorAction Stop |
            Where-Object {
                -not $_.PSIsContainer -and
                $sourceExtensions -contains $_.Extension.ToLowerInvariant()
            }
    }
    foreach ($relativePath in @(
        "scripts\build_xbox.ps1",
        "scripts\build_xbox_patch_pk3.py",
        "scripts\check_mp_holomatch_ui.py"
    )) {
        $path = Join-Path $repoRoot $relativePath
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $inputs += Get-Item -LiteralPath $path
        }
    }
    return @($inputs)
}

function Assert-ReleasePackagesFresh {
    param(
        [string[]]$ReleasePackages,
        [string]$SourceRoot = (Join-Path $repoRoot "code")
    )

    $oldestPackageUtc = $null
    foreach ($package in $ReleasePackages) {
        $item = Get-Item -LiteralPath $package
        if ($null -eq $oldestPackageUtc -or $item.LastWriteTimeUtc -lt $oldestPackageUtc) {
            $oldestPackageUtc = $item.LastWriteTimeUtc
        }
    }

    $newerInputs = @(
        Get-PackageFreshnessInputs -SourceRoot $SourceRoot |
            Where-Object { $_.LastWriteTimeUtc -gt $oldestPackageUtc } |
            Sort-Object FullName |
            Select-Object -First 8
    )

    if ($newerInputs.Count -gt 0) {
        $details = ($newerInputs | ForEach-Object {
            "{0} modifiedUtc={1:o}" -f (Get-DisplayRepoPath $_.FullName), $_.LastWriteTimeUtc
        }) -join "; "
        throw "Runtime or package source is newer than build\release PK3 artifacts; rebuild scripts\build_xbox.ps1 -Target spmp before staging. Newer input sample: $details"
    }
}

function Write-ReleaseArtifactFreshnessSummary {
    param([string[]]$ReleaseXbes)

    Write-Host "build\release XBE freshness ok"
    foreach ($xbe in $ReleaseXbes) {
        $item = Get-Item -LiteralPath $xbe
        Write-Host ("  {0}: modifiedUtc={1:o}" -f (Get-DisplayRepoPath $item.FullName), $item.LastWriteTimeUtc)
    }
}

function Write-ReleasePackageFreshnessSummary {
    param([string[]]$ReleasePackages)

    Write-Host "build\release PK3 freshness ok"
    foreach ($package in $ReleasePackages) {
        $item = Get-Item -LiteralPath $package
        Write-Host ("  {0}: modifiedUtc={1:o}" -f (Get-DisplayRepoPath $item.FullName), $item.LastWriteTimeUtc)
    }
}

function Assert-ReleaseRuntimeBuildIds {
    param([string[]]$ReleaseXbes)

    $expectedFlavor = if ($FrameDiagnostics) { "frame-diagnostics" } else { "production" }
    $failures = New-Object System.Collections.Generic.List[string]
    foreach ($xbe in $ReleaseXbes) {
        $buildId = Get-RuntimeBuildId -XbePath $xbe
        if (-not $buildId) {
            [void]$failures.Add("Release XBE is missing STEFX_RUNTIME_BUILD_ID marker; rebuild scripts\build_xbox.ps1 -Target spmp before staging. XBE: $(Get-DisplayRepoPath $xbe)")
            continue
        }
        $fileName = [System.IO.Path]::GetFileName($xbe).ToLowerInvariant()
        $expectedFragments = switch ($fileName) {
            "default.xbe" { @("personality=default", "flavor=$expectedFlavor", "log=ef_sp_log.txt") }
            "efmp.xbe" { @("personality=efmp", "flavor=$expectedFlavor", "log=ef_mp_log.txt") }
            default { @("flavor=$expectedFlavor") }
        }
        foreach ($fragment in $expectedFragments) {
            if ($buildId -notlike "*$fragment*") {
                [void]$failures.Add("Release XBE runtime build ID has wrong identity for ${fileName}: expected fragment '$fragment' in '$buildId'")
            }
        }
    }

    if ($failures.Count -gt 0) {
        throw "Release XBE runtime build ID validation failed:`n - $($failures -join "`n - ")"
    }
}

function Write-ReleaseRuntimeBuildIdSummary {
    param([string[]]$ReleaseXbes)

    Write-Host "build\release XBE runtime build ids ok"
    foreach ($xbe in $ReleaseXbes) {
        Write-Host ("  {0}: {1}" -f (Get-DisplayRepoPath $xbe), (Get-RuntimeBuildId -XbePath $xbe))
    }
}

function Assert-ReleasePreflightReady {
    param(
        [string[]]$ReleaseXbes,
        [string[]]$ReleasePackages,
        [string]$SourceRoot = (Join-Path $repoRoot "code")
    )

    $failures = New-Object System.Collections.Generic.List[string]
    try {
        Assert-ReleaseArtifactsFresh -ReleaseXbes $ReleaseXbes -SourceRoot $SourceRoot
    }
    catch {
        [void]$failures.Add("release XBE freshness: $($_.Exception.Message)")
    }

    try {
        Assert-ReleasePackagesFresh -ReleasePackages $ReleasePackages -SourceRoot $SourceRoot
    }
    catch {
        [void]$failures.Add("release PK3 freshness: $($_.Exception.Message)")
    }

    try {
        Assert-ReleaseRuntimeBuildIds -ReleaseXbes $ReleaseXbes
    }
    catch {
        [void]$failures.Add("release XBE runtime build ids: $($_.Exception.Message)")
    }

    if ($failures.Count -gt 0) {
        throw "Release preflight failed:`n - $($failures -join "`n - ")"
    }
}

function Get-ManifestFileRecord {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $record = [ordered]@{
        path = $fullPath
        present = (Test-Path -LiteralPath $fullPath -PathType Leaf)
    }
    if ($record.present) {
        $item = Get-Item -LiteralPath $fullPath
        $record["bytes"] = $item.Length
        $record["sha256"] = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
    }
    return $record
}

function Get-PythonExecutable {
    $pythonCommand = Get-Command "python.exe" -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $pythonCommand) {
        return $pythonCommand.Source
    }
    return "python"
}

function Assert-BuildXboxContract {
    param([string]$RepoRoot = $repoRoot)

    $contractVerifier = Join-Path $RepoRoot "scripts\verify_build_xbox_contracts.py"
    $buildScript = Join-Path $RepoRoot "scripts\build_xbox.ps1"
    $evidence = [ordered]@{
        status = "missing"
        verifier = Get-ManifestFileRecord -Path $contractVerifier
        buildScript = Get-ManifestFileRecord -Path $buildScript
        exitCode = $null
        stdout = ""
        stderr = ""
    }
    if (-not (Test-Path -LiteralPath $contractVerifier -PathType Leaf)) {
        throw "Build contract verifier is missing: $contractVerifier"
    }
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
        throw "Build script is missing: $buildScript"
    }

    $pythonExe = Get-PythonExecutable
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $pythonExe $contractVerifier --build-script $buildScript 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $evidence["exitCode"] = $exitCode
    $evidence["stdout"] = (($output | Where-Object { -not ($_ -is [System.Management.Automation.ErrorRecord]) } | ForEach-Object { $_.ToString() }) -join "`n")
    $evidence["stderr"] = (($output | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } | ForEach-Object { $_.ToString() }) -join "`n")
    if ($exitCode -ne 0) {
        $details = ($output | ForEach-Object { $_.ToString() }) -join "`n"
        $evidence["status"] = "fail"
        throw "build_xbox.ps1 target contract verification failed; do not stage release XBEs until scripts\build_xbox.ps1 -Target spmp refreshes default.xbe before efmp.xbe. Exit code $exitCode.`n$details"
    }
    $evidence["status"] = "pass"
    return $evidence
}

function Write-BuildXboxContractSummary {
    Write-Host "build_xbox.ps1 target contract ok"
}

function Assert-XbeRuntimeBuildIdIdentity {
    param(
        [string]$BuildId,
        [string]$RelativePath
    )

    $expectedFlavor = if ($FrameDiagnostics) { "frame-diagnostics" } else { "production" }
    $fileName = [System.IO.Path]::GetFileName($RelativePath).ToLowerInvariant()
    $expectedFragments = switch ($fileName) {
        "default.xbe" { @("personality=default", "flavor=$expectedFlavor", "log=ef_sp_log.txt") }
        "efmp.xbe" { @("personality=efmp", "flavor=$expectedFlavor", "log=ef_mp_log.txt") }
        default { @("flavor=$expectedFlavor") }
    }
    foreach ($fragment in $expectedFragments) {
        if ($BuildId -notlike "*$fragment*") {
            throw "XBE runtime build ID has wrong identity for ${RelativePath}: expected fragment '$fragment' in '$BuildId'"
        }
    }
}

function Invoke-StageHardwareSelfTest {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("stefx_stage_freshness_" + [System.Guid]::NewGuid().ToString("N"))
    try {
        $testCodeRoot = Join-Path $tempRoot "code"
        $testReleaseRoot = Join-Path $tempRoot "build\release"
        New-Item -ItemType Directory -Path (Join-Path $testCodeRoot "ui") -Force | Out-Null
        New-Item -ItemType Directory -Path $testReleaseRoot -Force | Out-Null

        $sourcePath = Join-Path $testCodeRoot "ui\ui_ef_frontend.cpp"
        $defaultXbe = Join-Path $testReleaseRoot "default.xbe"
        $efmpXbe = Join-Path $testReleaseRoot "efmp.xbe"
        $xbox0Pk3 = Join-Path $testReleaseRoot "BaseEF\xbox0.pk3"
        $xbox1Pk3 = Join-Path $testReleaseRoot "BaseEF\xbox1.pk3"
        Set-Content -LiteralPath $sourcePath -Value "// self-test source" -Encoding ASCII
        Set-Content -LiteralPath $defaultXbe -Value "default STEFX_RUNTIME_BUILD_ID personality=default flavor=production date=Aug 19 2026 time=12:01:00 log=ef_sp_log.txt" -Encoding ASCII
        Set-Content -LiteralPath $efmpXbe -Value "efmp STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production date=Aug 19 2026 time=12:02:00 log=ef_mp_log.txt" -Encoding ASCII
        New-Item -ItemType Directory -Path (Split-Path -Parent $xbox0Pk3) -Force | Out-Null
        Set-Content -LiteralPath $xbox0Pk3 -Value "xbox0 self-test" -Encoding ASCII
        Set-Content -LiteralPath $xbox1Pk3 -Value "xbox1 self-test" -Encoding ASCII

        $testScriptsRoot = Join-Path $tempRoot "scripts"
        New-Item -ItemType Directory -Path $testScriptsRoot -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $repoRoot "scripts\verify_build_xbox_contracts.py") -Destination (Join-Path $testScriptsRoot "verify_build_xbox_contracts.py") -Force
        $testBuildScript = Join-Path $testScriptsRoot "build_xbox.ps1"
        $validBuildScript = @'
function Assert-ActiveReleaseXbes {
    $failures = New-Object System.Collections.Generic.List[string]
    try {
        Assert-ActiveReleaseXbeRuntimeBuildIds -XbePaths $XbePaths
    } catch {
        [void]$failures.Add("runtime build ids: $($_.Exception.Message)")
    }
    try {
        Assert-ActiveReleaseXbeFreshness -XbePaths $XbePaths
    } catch {
        [void]$failures.Add("XBE freshness: $($_.Exception.Message)")
    }
    if ($failures.Count -gt 0) {
        throw "Active release XBE postcondition failed"
    }
}
function Assert-ActiveReleasePackages {
    $failures = New-Object System.Collections.Generic.List[string]
    try {
        Assert-ActiveReleasePackageFreshness -PackagePaths $PackagePaths
    } catch {
        [void]$failures.Add("PK3 freshness: $($_.Exception.Message)")
    }
    if ($failures.Count -gt 0) {
        throw "Active release package postcondition failed"
    }
}
function Build-Project {
    $retailRendererDefinitions = "FINAL_BUILD;_FINAL;STEFX_ELITE_FORCE_SP;STEFX_RETAIL_RENDERER_ACTIVE;STEFX_RETAIL_SURFACE_ACTIVE"
    $retailRendererReplacements = @(
        "..\renderer\tr_backend.cpp",
        "..\renderer\tr_cmds.cpp",
        "..\renderer\tr_light.cpp",
        "..\renderer\tr_main.cpp",
        "..\renderer\tr_scene.cpp",
        "..\renderer\tr_shade.cpp",
        "..\renderer\tr_shade_calc.cpp",
        "..\renderer\tr_shader.cpp",
        "..\renderer\tr_sky.cpp",
        "..\renderer\tr_world.cpp"
    )
    if ($retailRendererReplacements -icontains $source.RelativePath) {
        continue
    }
    $source.Tool = [pscustomobject]@{
        Name = "VCCLCompilerTool"
        PreprocessorDefinitions = $retailRendererDefinitions
    }
    $forceCompileForFreshRuntimeId = (
        [System.IO.Path]::GetFileName($source.FullPath).Equals("xb_log.cpp", [System.StringComparison]::OrdinalIgnoreCase) -and
        [System.IO.Path]::GetFullPath($source.FullPath).StartsWith((Join-Path $repoRoot "code\win32"), [System.StringComparison]::OrdinalIgnoreCase)
    )
    if ($ReuseObjects -and
        -not $forceCompileForFreshRuntimeId -and
        (Test-Path -LiteralPath $objPath) -and
        (Test-Path -LiteralPath $fingerprintPath)) {
        Write-Host "Reusing $objPath"
        continue
    }
    if ($ReuseObjects -and $forceCompileForFreshRuntimeId) {
        Write-Host "Rebuilding $objPath for fresh STEFX_RUNTIME_BUILD_ID"
    }
    Invoke-External -Exe $clExe -Arguments $compileFlags -WorkingDirectory $projectDir
}
function Invoke-BuildGraphForTarget {
    $previousBuildTarget = $script:StefxBuildTarget
    $script:StefxBuildTarget = $BuildTarget
    try {
        Invoke-BuildGraph -Projects $Projects
    } finally {
        $script:StefxBuildTarget = $previousBuildTarget
    }
}
switch ($Target) {
    "sp" {
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Assert-ActiveReleaseXbes @((Join-Path $repoReleaseDir "default.xbe"))
        Update-EFConsoleAssetLists
        Assert-ActiveReleasePackages @(
            (Join-Path $repoReleaseDir "BaseEF\xbox0.pk3")
        )
    }
    "mp" {
        Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects
    }
    "spmp" {
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Invoke-BuildGraphForTarget -BuildTarget "spmp" -Projects $spProjects
        Assert-ActiveReleaseXbes @(
            (Join-Path $repoReleaseDir "default.xbe"),
            (Join-Path $repoReleaseDir "efmp.xbe")
        )
        Update-EFConsoleAssetLists
        Update-EFHolomatchAssetLists
        Assert-ActiveReleasePackages @(
            (Join-Path $repoReleaseDir "BaseEF\xbox0.pk3"),
            (Join-Path $repoReleaseDir "BaseEF\xbox1.pk3")
        )
    }
    "all" {
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects
    }
}
'@
        Set-Content -LiteralPath $testBuildScript -Value $validBuildScript -Encoding ASCII
        $null = Assert-BuildXboxContract -RepoRoot $tempRoot

        $invalidBuildScript = $validBuildScript.Replace(
            @'
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Invoke-BuildGraphForTarget -BuildTarget "spmp" -Projects $spProjects
'@,
            '        Invoke-BuildGraph -Projects $spProjects'
        )
        Set-Content -LiteralPath $testBuildScript -Value $invalidBuildScript -Encoding ASCII
        $badBuildContractFailedAsExpected = $false
        try {
            Assert-BuildXboxContract -RepoRoot $tempRoot
        }
        catch {
            if ($_.Exception.Message -like "*target contract verification failed*") {
                $badBuildContractFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $badBuildContractFailedAsExpected) {
            throw "Freshness self-test failed: bad build_xbox spmp contract was not rejected"
        }
        Set-Content -LiteralPath $testBuildScript -Value $validBuildScript -Encoding ASCII

        $identityXbe = Join-Path $tempRoot "identity.xbe"
        $expectedBuildId = "STEFX_RUNTIME_BUILD_ID personality=self flavor=production date=Aug 19 2026 time=12:00:00 log=self.txt"
        [byte[]]$identityBytes = [System.Text.Encoding]::ASCII.GetBytes("prefix-" + $expectedBuildId) + [byte[]](0, 116, 97, 105, 108)
        [System.IO.File]::WriteAllBytes($identityXbe, $identityBytes)
        $actualBuildId = Get-RuntimeBuildId -XbePath $identityXbe
        if ($actualBuildId -ne $expectedBuildId) {
            throw "Freshness self-test failed: runtime build id extraction returned '$actualBuildId'"
        }

        $sourceOld = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-19T10:00:00Z"), [DateTimeKind]::Utc)
        $releaseNew = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-19T11:00:00Z"), [DateTimeKind]::Utc)
        $packageNew = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-21T11:00:00Z"), [DateTimeKind]::Utc)
        [System.IO.File]::SetLastWriteTimeUtc($sourcePath, $sourceOld)
        [System.IO.File]::SetLastWriteTimeUtc($defaultXbe, $releaseNew)
        [System.IO.File]::SetLastWriteTimeUtc($efmpXbe, $releaseNew)
        [System.IO.File]::SetLastWriteTimeUtc($xbox0Pk3, $packageNew)
        [System.IO.File]::SetLastWriteTimeUtc($xbox1Pk3, $packageNew)
        Assert-ReleaseArtifactsFresh -ReleaseXbes @($defaultXbe, $efmpXbe) -SourceRoot $testCodeRoot
        Assert-ReleasePackagesFresh -ReleasePackages @($xbox0Pk3, $xbox1Pk3) -SourceRoot $testCodeRoot
        Assert-ReleaseRuntimeBuildIds -ReleaseXbes @($defaultXbe, $efmpXbe)

        $freshnessSummary = @(& {
            Write-ReleaseArtifactFreshnessSummary -ReleaseXbes @($defaultXbe, $efmpXbe)
            Write-ReleasePackageFreshnessSummary -ReleasePackages @($xbox0Pk3, $xbox1Pk3)
            Write-ReleaseRuntimeBuildIdSummary -ReleaseXbes @($defaultXbe, $efmpXbe)
        } 6>&1 | ForEach-Object { $_.ToString() })
        foreach ($expectedMarker in @(
            "build\release XBE freshness ok",
            "build\release PK3 freshness ok",
            "build\release XBE runtime build ids ok"
        )) {
            if ($freshnessSummary -notcontains $expectedMarker) {
                throw "Freshness self-test failed: CheckFreshnessOnly summary missing '$expectedMarker'"
            }
        }

        $missingIdentityXbe = Join-Path $testReleaseRoot "missing_identity.xbe"
        Set-Content -LiteralPath $missingIdentityXbe -Value "missing identity" -Encoding ASCII
        $missingIdentityFailedAsExpected = $false
        try {
            Assert-ReleaseRuntimeBuildIds -ReleaseXbes @($missingIdentityXbe)
        }
        catch {
            if ($_.Exception.Message -like "*missing STEFX_RUNTIME_BUILD_ID marker*") {
                $missingIdentityFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $missingIdentityFailedAsExpected) {
            throw "Freshness self-test failed: release XBE without runtime build id was not rejected"
        }

        $missingIdentityEfmp = Join-Path $testReleaseRoot "efmp_missing_identity.xbe"
        Set-Content -LiteralPath $missingIdentityEfmp -Value "missing identity efmp" -Encoding ASCII
        $bothMissingIdentityFailedAsExpected = $false
        try {
            Assert-ReleaseRuntimeBuildIds -ReleaseXbes @($missingIdentityXbe, $missingIdentityEfmp)
        }
        catch {
            if ($_.Exception.Message -like "*missing_identity.xbe*" -and
                $_.Exception.Message -like "*efmp_missing_identity.xbe*") {
                $bothMissingIdentityFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $bothMissingIdentityFailedAsExpected) {
            throw "Freshness self-test failed: release runtime build id validation did not report both missing XBEs"
        }

        $wrongIdentityXbe = Join-Path $testReleaseRoot "default.xbe"
        Set-Content -LiteralPath $wrongIdentityXbe -Value "default STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production date=Aug 19 2026 time=12:03:00 log=ef_mp_log.txt" -Encoding ASCII
        $wrongIdentityFailedAsExpected = $false
        try {
            Assert-ReleaseRuntimeBuildIds -ReleaseXbes @($wrongIdentityXbe)
        }
        catch {
            if ($_.Exception.Message -like "*wrong identity*") {
                $wrongIdentityFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $wrongIdentityFailedAsExpected) {
            throw "Freshness self-test failed: release XBE with wrong runtime identity was not rejected"
        }
        Set-Content -LiteralPath $defaultXbe -Value "default STEFX_RUNTIME_BUILD_ID personality=default flavor=production date=Aug 19 2026 time=12:01:00 log=ef_sp_log.txt" -Encoding ASCII

        $sourceNew = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-19T12:00:00Z"), [DateTimeKind]::Utc)
        [System.IO.File]::SetLastWriteTimeUtc($sourcePath, $sourceNew)
        $failedAsExpected = $false
        try {
            Assert-ReleaseArtifactsFresh -ReleaseXbes @($defaultXbe, $efmpXbe) -SourceRoot $testCodeRoot
        }
        catch {
            if ($_.Exception.Message -like "*Runtime source is newer than build\release XBE artifacts*") {
                $failedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $failedAsExpected) {
            throw "Freshness self-test failed: stale release artifacts were not rejected"
        }

        $sourceFreshForXbe = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-19T10:30:00Z"), [DateTimeKind]::Utc)
        $packageOld = [DateTime]::SpecifyKind([DateTime]::Parse("2026-08-19T10:15:00Z"), [DateTimeKind]::Utc)
        [System.IO.File]::SetLastWriteTimeUtc($sourcePath, $sourceFreshForXbe)
        [System.IO.File]::SetLastWriteTimeUtc($xbox0Pk3, $packageOld)
        [System.IO.File]::SetLastWriteTimeUtc($xbox1Pk3, $packageNew)
        $stalePackageFailedAsExpected = $false
        try {
            Assert-ReleasePackagesFresh -ReleasePackages @($xbox0Pk3, $xbox1Pk3) -SourceRoot $testCodeRoot
        }
        catch {
            if ($_.Exception.Message -like "*build\release PK3 artifacts*") {
                $stalePackageFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $stalePackageFailedAsExpected) {
            throw "Freshness self-test failed: stale release package was not rejected"
        }

        [System.IO.File]::SetLastWriteTimeUtc($sourcePath, $sourceNew)
        [System.IO.File]::SetLastWriteTimeUtc($defaultXbe, $releaseNew)
        [System.IO.File]::SetLastWriteTimeUtc($efmpXbe, $releaseNew)
        [System.IO.File]::SetLastWriteTimeUtc($xbox0Pk3, $packageOld)
        [System.IO.File]::SetLastWriteTimeUtc($xbox1Pk3, $packageOld)
        $aggregateFailedAsExpected = $false
        try {
            Assert-ReleasePreflightReady `
                -ReleaseXbes @($defaultXbe, $efmpXbe) `
                -ReleasePackages @($xbox0Pk3, $xbox1Pk3) `
                -SourceRoot $testCodeRoot
        }
        catch {
            if ($_.Exception.Message -like "*release XBE freshness:*" -and
                $_.Exception.Message -like "*release PK3 freshness:*") {
                $aggregateFailedAsExpected = $true
            }
            else {
                throw
            }
        }
        if (-not $aggregateFailedAsExpected) {
            throw "Freshness self-test failed: release preflight did not aggregate XBE and PK3 freshness failures"
        }

        Write-Host "stage_hardware_pk3_test.ps1 self-test passed"
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            $fullTempRoot = [System.IO.Path]::GetFullPath($tempRoot)
            $tempPrefix = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
            if ($fullTempRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $fullTempRoot -Recurse -Force
            }
        }
    }
}

if ($SelfTest) {
    Invoke-StageHardwareSelfTest
    return
}

$OutputDir = Resolve-RepoPath $OutputDir
$tempSource = "$OutputDir.source"
$tempIso = "$OutputDir.iso"
$tempExtract = "$OutputDir.extracting"

foreach ($path in @($OutputDir, $tempSource, $tempIso, $tempExtract)) {
    Assert-HardwarePath $path
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
$buildContractEvidence = Assert-BuildXboxContract
Write-BuildXboxContractSummary
Assert-ReleasePreflightReady `
    -ReleaseXbes @($sources["default.xbe"], $sources["efmp.xbe"]) `
    -ReleasePackages @($sources["BaseEF/xbox0.pk3"], $sources["BaseEF/xbox1.pk3"])
if ($CheckFreshnessOnly) {
    Write-ReleaseArtifactFreshnessSummary @($sources["default.xbe"], $sources["efmp.xbe"])
    Write-ReleasePackageFreshnessSummary @($sources["BaseEF/xbox0.pk3"], $sources["BaseEF/xbox1.pk3"])
    Write-ReleaseRuntimeBuildIdSummary @($sources["default.xbe"], $sources["efmp.xbe"])
    return
}

# A staged runtime log belongs to the previous binary and can make a new
# hardware result look current. Clear only the known root-level runtime files
# after the output path has passed the hardware-root guard.
$runtimeFiles = @(
    "ef_sp_log.txt",
    "ef_mp_log.txt",
    "ef_runtime_commands.txt",
    "ef_runtime_commands.done",
    "ef_sp_level.txt",
    "ef_sp_commands.txt"
)
foreach ($name in $runtimeFiles) {
    $path = Join-Path $OutputDir $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Remove-Item -LiteralPath $path -Force
    }
}

# A patch stage supersedes the full-stage manifest. Leaving both beside the
# active XBEs makes the older hashes look authoritative after an iteration.
Remove-Item -LiteralPath (Join-Path $OutputDir "HARDWARE_STAGE_MANIFEST.json") `
    -Force -ErrorAction SilentlyContinue

if (-not (Test-Path -LiteralPath $extractXiso -PathType Leaf)) {
    throw "extract-xiso not found: $extractXiso"
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
        $sourceRuntimeBuildId = Get-RuntimeBuildId -XbePath $entry.Value
        $stagedRuntimeBuildId = Get-RuntimeBuildId -XbePath $outputPath
        if (-not $sourceRuntimeBuildId) {
            throw "Release XBE is missing STEFX_RUNTIME_BUILD_ID marker: $relativePath"
        }
        if (-not $stagedRuntimeBuildId) {
            throw "Staged XBE is missing STEFX_RUNTIME_BUILD_ID marker: $relativePath"
        }
        Assert-XbeRuntimeBuildIdIdentity -BuildId $sourceRuntimeBuildId -RelativePath $relativePath
        Assert-XbeRuntimeBuildIdIdentity -BuildId $stagedRuntimeBuildId -RelativePath $relativePath
        if ($sourceRuntimeBuildId -ne $stagedRuntimeBuildId) {
            throw "Staged XBE runtime build id changed unexpectedly: $relativePath"
        }
        $record["mediaEnablePatchOffset"] = $patchOffset
        $record["runtimeBuildId"] = $stagedRuntimeBuildId
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
    frameDiagnostics = [bool]$FrameDiagnostics
    buildScriptContract = $buildContractEvidence
    files = $verified
}
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $OutputDir "HARDWARE_PATCH_MANIFEST.json") -Encoding UTF8

$displayOutputDir = Get-DisplayRepoPath $OutputDir
$observationPath = Join-Path $OutputDir "HARDWARE_OBSERVATION.json"
$writeObservationTemplate = $false
if (-not $FrameDiagnostics) {
    if (-not (Test-Path -LiteralPath $observationPath -PathType Leaf)) {
        $writeObservationTemplate = $true
    }
    else {
        try {
            $existingObservation = Get-Content -LiteralPath $observationPath -Raw | ConvertFrom-Json
            if ($existingObservation.manifestVersion -ne $Version -or
                $existingObservation.observationSchemaVersion -ne $observationSchemaVersion -or
                $null -eq $existingObservation.sp -or
                $null -eq $existingObservation.coop -or
                $null -eq $existingObservation.mp -or
                -not ($existingObservation.sp.PSObject.Properties.Name -contains "mapsTested") -or
                -not ($existingObservation.coop.PSObject.Properties.Name -contains "mapsTested") -or
                -not ($existingObservation.mp.PSObject.Properties.Name -contains "mapsTested") -or
                -not ($existingObservation.sp.PSObject.Properties.Name -contains "evidenceFiles") -or
                -not ($existingObservation.coop.PSObject.Properties.Name -contains "evidenceFiles") -or
                -not ($existingObservation.mp.PSObject.Properties.Name -contains "evidenceFiles") -or
                -not ($existingObservation.sp.PSObject.Properties.Name -contains "memoryFreeMinimum") -or
                -not ($existingObservation.sp.PSObject.Properties.Name -contains "memoryLargestFreeMinimum") -or
                -not ($existingObservation.sp.PSObject.Properties.Name -contains "memoryUsedDelta") -or
                -not ($existingObservation.coop.PSObject.Properties.Name -contains "memoryFreeMinimum") -or
                -not ($existingObservation.coop.PSObject.Properties.Name -contains "memoryLargestFreeMinimum") -or
                -not ($existingObservation.coop.PSObject.Properties.Name -contains "memoryUsedDelta") -or
                -not ($existingObservation.mp.PSObject.Properties.Name -contains "memoryFreeMinimum") -or
                -not ($existingObservation.mp.PSObject.Properties.Name -contains "memoryLargestFreeMinimum") -or
                -not ($existingObservation.mp.PSObject.Properties.Name -contains "memoryUsedDelta")) {
                $writeObservationTemplate = $true
            }
        }
        catch {
            $writeObservationTemplate = $true
        }
    }
}

if ($writeObservationTemplate) {
    $observation = [ordered]@{
        manifestVersion = $Version
        observationSchemaVersion = $observationSchemaVersion
        notes = "Fill this after the retail Xbox run. Use numbers, true/false, and concise notes."
        sp = [ordered]@{
            mapsTested = @()
            evidenceFiles = @()
            durationSeconds = 0
            visibleFpsMin = 0
            visibleFpsMax = 0
            memoryFreeMinimum = 0
            memoryLargestFreeMinimum = 0
            memoryUsedDelta = 0
            loadingOk = $false
            hudOk = $false
            worldLightingOk = $false
            controlsOk = $false
            gameplayOk = $false
            noUnrecoveredStall = $false
            notes = ""
        }
        coop = [ordered]@{
            mapsTested = @()
            evidenceFiles = @()
            durationSeconds = 0
            visibleFpsMin = 0
            visibleFpsMax = 0
            memoryFreeMinimum = 0
            memoryLargestFreeMinimum = 0
            memoryUsedDelta = 0
            loadingOk = $false
            hudOk = $false
            worldLightingOk = $false
            controlsOk = $false
            gameplayOk = $false
            splitScreenOk = $false
            p2HudOk = $false
            p2ControlsOk = $false
            noUnrecoveredStall = $false
            notes = ""
        }
        mp = [ordered]@{
            mapsTested = @()
            evidenceFiles = @()
            durationSeconds = 0
            visibleFpsMin = 0
            visibleFpsMax = 0
            memoryFreeMinimum = 0
            memoryLargestFreeMinimum = 0
            memoryUsedDelta = 0
            loadingOk = $false
            hudOk = $false
            worldLightingOk = $false
            controlsOk = $false
            gameplayOk = $false
            botsOrCombatOk = $false
            noUnrecoveredStall = $false
            notes = ""
        }
    }
    $observation | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $observationPath -Encoding UTF8
}

$diagnosticInstructions = if ($FrameDiagnostics) {
@"
This is a frame-phase diagnostic build. A small FPS line remains visible at
all times. The complete phase overlay appears for two seconds every five
seconds. Its values are a frozen snapshot of the immediately preceding frame,
captured while the complete overlay was hidden, so the rows do not measure
their own text-drawing workload. The phase overlay reports:

  LP total main loop   IN input            MM menu/handoff
  FR Com_Frame         SV server           CL client          OT other Com_Frame
  GM game              FE renderer front   BE renderer back   SD screen draw
  DR draw surfaces     SW swap command     PR Present         FN Finish
  EV event loops       CB command buffers  CP client preamble CT client tail
  AU audio
  ST view setup        ML mark leaves      WO world surfaces  PO dynamic polys
  PJ projection        EN entities         SO sort surfaces   DG debug draw
  DS draw state        RQ push reservation PK vertex packing  IX index packing
  BP BeginPush total   MX longest wait      O1 waits >1 ms     O10 waits >10 ms
  RT whole scene       FG unowned front end BO backend other   BG unowned back end

Wait for the complete rows, then photograph them during stable gameplay in an
open view in both SP and Holomatch. The diagnostic logger is memory-only so
disk logging cannot distort these timings; no runtime log is expected from
this pair. Judge the sustained FPS during the FPS-only interval; drawing the
complete rows may temporarily reduce the live FPS while they are visible.
"@
}
else {
@"
Normal release builds intentionally omit the intrusive per-frame renderer and
texture-allocation profilers. STEFX_HW_FRAME_PROFILE and
STEFX_HW_RENDER_SAMPLE records are therefore not expected. The logs retain a
lightweight periodic heartbeat plus boot, map-load, and crash breadcrumbs; use
those to establish forward progress and the last completed operation. The FPS
overlay and direct observation are the hardware performance authority.

After copying ef_sp_log.txt and ef_mp_log.txt back into this folder, run:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir

The verifier re-checks the staged XBE/PK3 integrity, rejects unexpected
returned runtime marker files, then checks the returned log runtime build
identity against the staged manifest, heartbeat progress, required mode
markers, complete per-heartbeat `completedFrame`/`realtime`/`path`/`mem`
telemetry, `path=1`, fatal/OOM markers, production-vs-diagnostic log shape, and
the hotlog-off Holomatch diagnostic families. It does not replace the required
visible FPS range or visual/stability notes. The JSON and text summaries
include SHA256 values for returned logs, the observation file, and any listed
visual evidence files.

Before transferring the patch to hardware, run:

  python scripts\verify_hardware_stage.py --stage $displayOutputDir

To archive transfer-readiness evidence, add --report-out:

  python scripts\verify_hardware_stage.py --stage $displayOutputDir --report-out $displayOutputDir\hardware_stage_preflight_report.json

To run both the preflight and final production-log gate with the current
default acceptance criteria, use the combined audit command:

  python scripts\qualify_hardware_stage.py --stage $displayOutputDir

The combined audit writes hardware_stage_preflight_report.json,
hardware_qualification_report.json, and hardware_qualification_audit.json. The
audit report includes an acceptanceChecklist so the open items are visible
without inferring them from the nested reports; schema v63 also exposes
overallStatus, a top-level openAcceptanceItems list, and an explicit runtime
build identity binding item. Its default gate
requires at least 90 seconds of returned heartbeat elapsed time and at least 90
seconds of filled observation duration for each mode. It also records SHA256
provenance and summary sanity checks for the saved retail `jamp.xbe` renderer
contract, object-comparison, ABI, and runtime-structure artifacts. The retail
contract ledger is structurally validated as high-confidence, retail-cited
address/name/object/contract evidence for every row. The audit also records
SHA256 provenance for the active retail_xbox renderer modules, verifies their
retail_renderer_contract.h include and namespace wrapper, and checks that the
XBE project/build script still route those modules under the retail ABI defines
instead of the legacy frame-path sources. The audit
also records SHA256 provenance and conservative liveness checks for the current
saved XEMU SP `borg2`, co-op `borg1` split-screen, and Holomatch `hm_borg1`
proof reports, contact sheets, and final register dumps, including screenshot
byte-size diversity and co-op P2 renderer-state telemetry so a static or
single-view capture is not promoted as representative visual/liveness evidence.
It also rejects saved XEMU proof reports that predate newer runtime source
files, newer retained build\release XBEs or PK3s, or newer proof-harness/gate
scripts, and requires each report's recorded XBE runtime identity to match the
current retained release artifact for that mode.
The SP/co-op and MP XEMU wrappers also fail before repack/launch if active
build\release XBEs lack STEFX_RUNTIME_BUILD_ID or carry the wrong
personality/log identity, and repack retained ISOs when their staged XBE/PK3
payloads are missing, newer than the ISO, or not SHA256-identical to the
current release artifacts. Direct ja_xemu_smoke.py --require-runtime-xbe-id
calls enforce the same default/efmp personality and log-file identity.
Refresh emulator proof with:

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\refresh_xemu_qualification_proof.ps1

That helper writes a schema-v3 xemu_qualification_proof_refresh_*.json manifest
and, unless -SkipAudit is used, immediately runs the combined audit with
--xemu-refresh-report bound to that manifest. For a later manual audit rerun,
pass the latest refresh manifest explicitly with --xemu-refresh-report. If no
explicit XEMU report overrides are supplied, a standalone audit also
auto-selects the newest scripts\output\xemu_qualification_proof_refresh_*.json
manifest when one is present. The schema-v63 audit records that choice in
xemuProofRefreshReport.selectionMode as explicit, auto,
disabled-by-explicit-proof-reports, or none. Direct --xemu-sp-report,
--xemu-coop-report, and --xemu-mp-report overrides remain available for bounded diagnostics; the audit
derives the adjacent contact sheet and final-register dump paths from each
report name. A refresh manifest must include the successful release freshness
preflight plus the per-XBE STEFX_RUNTIME_BUILD_ID identity lines for
build\release\default.xbe and build\release\efmp.xbe.

The preflight verifier checks the hardware patch manifest hash, staged hashes,
release artifact hashes, runtime-source freshness under code\ against both the
hardware patch manifest and the retained build\release XBEs, the one-byte XBE
media-enable patch, stale runtime markers, the canonical controller config hash when available, and the Holomatch
package/architecture gate without allowing loose original-image or loose
map-override stage fallbacks. It also reports the staged observation template
hash. Staging extracts each XBE's STEFX_RUNTIME_BUILD_ID literal into the
hardware manifest, and production log verification requires returned SP and
Holomatch logs to contain the matching personality/log identity line. The saved preflight report includes a report type, schema version,
verifier script hash, command-line provenance, and its resolved report path.
This staging script first verifies the active scripts\build_xbox.ps1 target
contract, including the forced xb_log.cpp rebuild that refreshes the
STEFX_RUNTIME_BUILD_ID __DATE__/__TIME__ literal and the retail_xbox ABI
replacement of the legacy frame-path modules, then performs the build\release XBE freshness, PK3 freshness, and
runtime-build-id checks before it clears old returned logs or writes a new patch manifest. The
combined schema-v63 audit also requires schema-v2 SP/Holomatch retail object-compare reports with
compare-script provenance, root/link input records, object-pair records, and
per-current/donor-object file hashes. It invalidates retained reports when active
retail_xbox source, code\x_exe\x_exe.vcproj, scripts\build_xbox.ps1, or
scripts\compare_retail_renderer_objects.py is newer than those reports. The
active scripts\build_xbox.ps1 -Target sp and -Target spmp paths also verify
embedded runtime build IDs, source freshness, and package freshness for their
generated release XBE/PK3 outputs before returning success, so rebuild first if
staging reports newer runtime source, a stale PK3, a missing XBE identity, or a
broken spmp build graph. A failed release preflight reports XBE freshness, PK3
freshness, and runtime build ID problems together so one failed check shows the
full stale-artifact set.
To check only those pre-stage gates after rebuilding, run:

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_hardware_pk3_test.ps1 -CheckFreshnessOnly

The HARDWARE_OBSERVATION.json template carries observationSchemaVersion=3 and
records the tested map list, optional evidence files, visible FPS range,
observed memory free/largest-free/used-delta values, loading, HUD, lighting,
controls, gameplay, stall recovery, co-op split-screen/P2 evidence, and
Holomatch bot/combat evidence from the retail run. Missing,
manifest-mismatched, or old-schema templates are refreshed before transfer. If
that template is missing, create it with:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --write-observation-template

After filling HARDWARE_OBSERVATION.json and copying ef_sp_log.txt and
ef_mp_log.txt back into this folder, run the final evidence check:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --require-observation

For a durable qualification record, add --report-out. The report is written as
JSON even when the verifier fails because logs, observation data, or proof
thresholds are still missing; missing-log reports still include the staged
XBE/PK3 integrity result and the requested map, FPS, memory, and observation
criteria, and still validate the observation file's duration, FPS, booleans,
required maps, and evidence-file references. Saved reports include a report
type, schema version marker, verifier script hash, command-line provenance,
and their resolved report path:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --require-observation --report-out $displayOutputDir\hardware_qualification_report.json

To require at least one listed visual evidence file for each mode, add:

  --require-evidence-files

Listed evidence files must use a visual extension such as .png, .jpg, or .mp4,
exist, be at least 1024 bytes by default, and have a matching visual container
signature. Still-image evidence with readable PNG, JPEG, BMP, or GIF header dimensions must be at
least 320x240 by default. Detected type, dimensions, bytes, and SHA256 are
written into the production qualification report. Override those floors with
--min-evidence-bytes, --min-image-width, or --min-image-height only for a
bounded diagnostic.

To require a specific representative map matrix, add repeated map flags. To
require an observed FPS floor, add per-mode FPS flags:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --require-observation --required-sp-map borg2 --required-coop-map borg1 --required-mp-map hm_borg1 --min-sp-visible-fps 30 --min-coop-visible-fps 30 --min-mp-visible-fps 30

To require returned-log memory floors or cap used-memory growth, add explicit
byte-count thresholds. Co-op split-screen memory is verified from the filled
observation because it runs through default.xbe rather than a separate returned
log:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --require-observation --min-sp-largest-free 1048576 --min-mp-largest-free 1048576 --max-sp-used-delta 0 --max-mp-used-delta 0 --min-coop-observed-largest-free 1048576 --max-coop-observed-used-delta 0

To require the current four-player local Holomatch split-screen proof from the
returned ef_mp_log.txt, add --require-hm-split-log. This invokes the focused
split verifier and requires P1-P4 virtual command proof, four render slots,
P2-P4 local refdefs/snapshot merge, first-person filter proof for slots 1-3,
HUD containment, bot fill, heartbeat progress/FPS, retail path=1, and memory
thresholds:

  python scripts\verify_production_hardware_logs.py --stage $displayOutputDir --require-observation --require-hm-split-log
"@
}

$qualificationProcedure = if ($FrameDiagnostics) {
@"
Hardware qualification procedure:

  1. Run single-player gameplay for 90-120 seconds in an open view. Wait for
     the complete upper-left rows to pulse on, then photograph them.
  2. Run two-player co-op split-screen gameplay for 90-120 seconds in an open
     view. Photograph both viewports, both HUDs, and P2 control response.
  3. Run Holomatch with bots for 90-120 seconds in an open view. Wait for the
     complete rows to pulse on, then photograph them.
  4. Note the FPS range while only the small FPS line is visible, any long
     stalls, and whether each mode recovers from a stall.
"@
}
else {
@"
Hardware qualification procedure:

  1. Run single-player gameplay for 90-120 seconds, then return to the
     dashboard cleanly when possible. Preserve ef_sp_log.txt.
  2. Run two-player co-op split-screen gameplay for 90-120 seconds. Record
     visible FPS, split-screen correctness, both HUDs, P2 controls, and
     gameplay in HARDWARE_OBSERVATION.json.
  3. Run Holomatch gameplay with bots for 90-120 seconds, then return to the
     dashboard cleanly when possible. Preserve ef_mp_log.txt.
  4. Note the visible FPS overlay range, any long stalls, and whether each
     mode recovers from a stall.
  5. Copy both logs back into this folder. Do not combine or rename them.
     Do not copy runtime command or launch-marker files back with the logs.
"@
}

$loggingInfo = if ($FrameDiagnostics) {
@"
For a normal menu boot, remove diagnostic launch markers such as
ef_sp_level.txt from the Xbox game directory. This build keeps runtime logging
in memory so disk I/O cannot affect its measurements.
"@
}
else {
@"
For a normal menu boot, remove diagnostic launch markers such as
ef_sp_level.txt from the Xbox game directory. Runtime logs are ef_sp_log.txt
and ef_mp_log.txt in that directory.
"@
}

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

$loggingInfo

$qualificationProcedure

$diagnosticInstructions
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
