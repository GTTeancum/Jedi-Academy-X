[CmdletBinding()]
param(
    [string]$SourceRoot = 'C:\Programming\GitHub\Jedi-Academy-X\clean-mp-original-build\codemp',
    [string]$OutputRoot = 'C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\clean-ja-final-donor',
    [string]$XdkRoot = 'C:\XDK_5558\XDK'
)

$ErrorActionPreference = 'Stop'

$vcRoot = Join-Path $XdkRoot 'xbox\bin\vc71'
$compiler = Join-Path $vcRoot 'cl.exe'
$linker = Join-Path $vcRoot 'link.exe'
$objectRoot = Join-Path $OutputRoot 'objects'
$donorDll = Join-Path $OutputRoot 'clean-ja-final-renderer.dll'
$donorMap = Join-Path $OutputRoot 'clean-ja-final-renderer.map'

$sources = @(
    'renderer\matcomp.c',
    'renderer\tr_animation.cpp',
    'renderer\tr_backend.cpp',
    'renderer\tr_bsp_xbox.cpp',
    'renderer\tr_cmds.cpp',
    'renderer\tr_curve_xbox.cpp',
    'renderer\tr_font.cpp',
    'renderer\tr_ghoul2.cpp',
    'renderer\tr_image_xbox.cpp',
    'renderer\tr_init.cpp',
    'renderer\tr_light.cpp',
    'renderer\tr_main.cpp',
    'renderer\tr_marks.cpp',
    'renderer\tr_mesh.cpp',
    'renderer\tr_model.cpp',
    'renderer\tr_noise.cpp',
    'renderer\tr_quicksprite.cpp',
    'renderer\tr_scene.cpp',
    'renderer\tr_shade.cpp',
    'renderer\tr_shade_calc.cpp',
    'renderer\tr_shader.cpp',
    'renderer\tr_shadows.cpp',
    'renderer\tr_sky.cpp',
    'renderer\tr_surface.cpp',
    'renderer\tr_surfacesprites.cpp',
    'renderer\tr_world.cpp',
    'renderer\tr_worldeffects.cpp',
    'win32\win_glimp_console.cpp',
    'win32\win_highdynamicrange.cpp',
    'win32\win_lighteffects.cpp',
    'win32\win_qgl_dx8.cpp'
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

$defines = @(
    '_WIN32',
    'NDEBUG',
    'WIN32',
    '_JK2',
    '_JK2MP',
    '_XBOX',
    'VV_LIGHTING',
    'FINAL_BUILD',
    '_FINAL'
)
$includePaths = @(
    $SourceRoot,
    (Join-Path $XdkRoot 'xbox\include'),
    (Join-Path $XdkRoot 'include')
)
$commonFlags = @(
    '/nologo',
    '/c',
    '/Ox',
    '/Ob2',
    '/Oi',
    '/Ot',
    '/Oy',
    '/G6',
    '/arch:SSE',
    '/GF',
    '/Gy',
    '/MT',
    '/W3',
    '/Z7'
)
$commonFlags += $defines | ForEach-Object { "/D$_" }
$commonFlags += $includePaths | ForEach-Object { "/I$_" }

$objects = [System.Collections.Generic.List[string]]::new()
foreach ($relativeSource in $sources) {
    $source = Join-Path $SourceRoot $relativeSource
    $object = Join-Path $objectRoot (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    Write-Host "Compiling $relativeSource"
    & $compiler @commonFlags "/Fo$object" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed for $relativeSource (exit $LASTEXITCODE)"
    }
    $objects.Add($object)
}

Write-Host 'Linking analysis-only renderer donor'
$linkFlags = @(
    '/nologo',
    '/dll',
    '/noentry',
    '/force:unresolved',
    '/force:multiple',
    '/opt:noref',
    "/libpath:$(Join-Path $XdkRoot 'xbox\lib')",
    "/map:$donorMap",
    "/out:$donorDll"
)
& $linker @linkFlags @objects
if ($LASTEXITCODE -ne 0) {
    throw "Donor link failed (exit $LASTEXITCODE)"
}

Get-Item -LiteralPath $donorDll, $donorMap |
    Select-Object FullName, Length, LastWriteTime
