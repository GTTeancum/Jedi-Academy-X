[CmdletBinding()]
param(
    [string]$SourceRoot = 'C:\Programming\GitHub\Jedi-Academy-X\clean-mp-original-build\codemp',
    [string]$OutputRoot = 'C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\clean-ja-frame-donor',
    [string]$XdkRoot = 'C:\XDK_5558\XDK'
)

$ErrorActionPreference = 'Stop'

$vcRoot = Join-Path $XdkRoot 'xbox\bin\vc71'
$compiler = Join-Path $vcRoot 'cl.exe'
$linker = Join-Path $vcRoot 'link.exe'
$objectRoot = Join-Path $OutputRoot 'objects'
$donorDll = Join-Path $OutputRoot 'clean-ja-frame-donor.dll'
$donorMap = Join-Path $OutputRoot 'clean-ja-frame-donor.map'

$sources = @(
    'qcommon\common.cpp',
    'client\cl_main.cpp',
    'client\cl_scrn.cpp',
    'server\sv_main.cpp',
    'win32\win_shared.cpp',
    'win32\win_main_console.cpp'
)

foreach ($path in @($compiler, $linker)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required XDK 5558 Visual C++ tool is missing: $path"
    }
}

foreach ($path in $sources) {
    $fullPath = Join-Path $SourceRoot $path
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Retail donor source is missing: $fullPath"
    }
}

New-Item -ItemType Directory -Force -Path $objectRoot | Out-Null
Get-ChildItem -LiteralPath $objectRoot -File -Filter '*.obj' -ErrorAction SilentlyContinue |
    Remove-Item -Force
Remove-Item -LiteralPath $donorDll, $donorMap -Force -ErrorAction SilentlyContinue

$env:PATH = "$vcRoot;$env:PATH"
$defines = @('_WIN32', 'NDEBUG', 'WIN32', '_JK2', '_JK2MP', '_XBOX', 'VV_LIGHTING', 'FINAL_BUILD', '_FINAL')
$includePaths = @($SourceRoot, (Join-Path $XdkRoot 'xbox\include'), (Join-Path $XdkRoot 'include'))
$flags = @('/nologo', '/c', '/Ox', '/Ob2', '/Oi', '/Ot', '/Oy', '/G6', '/arch:SSE', '/GF', '/Gy', '/MT', '/W3', '/Z7')
$flags += $defines | ForEach-Object { "/D$_" }
$flags += $includePaths | ForEach-Object { "/I$_" }

$objects = [System.Collections.Generic.List[string]]::new()
foreach ($relativeSource in $sources) {
    $source = Join-Path $SourceRoot $relativeSource
    $object = Join-Path $objectRoot (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    Write-Host "Compiling $relativeSource"
    & $compiler @flags "/Fo$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed for $relativeSource (exit $LASTEXITCODE)"
    }
    $objects.Add($object)
}

Write-Host 'Linking analysis-only frame-path donor'
$linkFlags = @(
    '/nologo', '/dll', '/noentry', '/force:unresolved', '/force:multiple', '/opt:noref',
    "/libpath:$(Join-Path $XdkRoot 'xbox\lib')", "/map:$donorMap", "/out:$donorDll"
)
& $linker @linkFlags @objects
if ($LASTEXITCODE -ne 0) {
    throw "Donor link failed (exit $LASTEXITCODE)"
}

Get-Item -LiteralPath $donorDll, $donorMap | Select-Object FullName, Length, LastWriteTime
