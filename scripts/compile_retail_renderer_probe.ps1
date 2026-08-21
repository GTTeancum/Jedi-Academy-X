param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [ValidateSet("sp", "spmp")]
    [string]$Target = "sp"
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$xdkRoot = "C:\XDK_5558\XDK"
$compiler = Join-Path $xdkRoot "xbox\bin\vc71\CL.Exe"
$sourcePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Source))

if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Retail renderer source not found: $sourcePath"
}

$configurationDir = if ($Target -eq "spmp") { "spmp\exe" } else { "exe" }
$relativeSource = [System.IO.Path]::GetFullPath($sourcePath).Substring($repoRoot.Length).TrimStart('\')
$objectPath = Join-Path $repoRoot ("code\x_exe\Release\{0}\{1}" -f $configurationDir,
    ([System.IO.Path]::ChangeExtension($relativeSource, ".obj")))
New-Item -ItemType Directory -Path (Split-Path -Parent $objectPath) -Force | Out-Null

$arguments = @(
    "/nologo", "/c", "/Ox", "/Ob2", "/Oi", "/GF", "/Gy", "/Ot", "/G6",
    "/arch:SSE", "/MT", "/Oy", "/Z7",
    "/I", (Join-Path $xdkRoot "xbox\include"),
    "/I", (Join-Path $repoRoot "code\win32"),
    "/DNDEBUG", "/D_XBOX", "/D_JK2EXE", "/DWIN32", "/DVV_LIGHTING",
    "/DSTEFX_ELITE_FORCE_SP", "/D_CRT_SECURE_NO_DEPRECATE",
    "/D_CRT_NONSTDC_NO_DEPRECATE", "/D_XBOX_VC71_MIGRATION"
)

if ($Target -eq "spmp") {
    $arguments += "/DSTEFX_SP_HOSTED_MP"
}

$arguments += "/Fo$objectPath"
$arguments += "/Tp$sourcePath"

& $compiler @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Retail renderer compile probe failed for $Target with exit code $LASTEXITCODE"
}

Write-Host "Retail renderer compile probe passed: $Target $Source"
