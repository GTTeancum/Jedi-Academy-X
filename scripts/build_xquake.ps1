# build_xquake.ps1
#
# Build xquake (Microsoft's FakeGL test/sample game) with our toolchain
# (XDK 5849, vc71/CL.exe, custom patchxbe).  Used to test whether our build
# pipeline produces a working XBE for a known-working FakeGL host.
#
# If xquake builds and runs on retail Xbox: our pipeline is sound; the
# CreateDevice hang is JKA-specific (engine init, static ctors, memory
# layout, etc.).
#
# If xquake builds but hangs identically: our pipeline is broken — the
# CreateDevice hang is in something the pipeline does, not what JKA does.
#
# Build variant: debug (matches the pre-built XQuaked.exe Microsoft shipped
# in xbox/private/test/games/xquake/xbox/obj/i386/).  Debug uses d3d8d.lib
# rather than d3d8.lib (which is kernel-inline-only and won't link in this
# toolchain).  Per sources.inc the LIBEXT macro is "d" for debug builds.

param(
    [string]$XQuakeSrc = "C:\Programming\GitHub\xbox\private\test\games\xquake",
    [string]$OutDir   = "C:\Programming\GitHub\Jedi-Academy-X\build\xquake"
)

$ErrorActionPreference = "Stop"

$xdkRoot = "C:\XDK"
$vc71Dir = Join-Path $xdkRoot "xbox\bin\vc71"
$xdkBin  = Join-Path $xdkRoot "xbox\bin"
$xdkInc  = Join-Path $xdkRoot "xbox\include"
$xdkInc2 = Join-Path $xdkRoot "include"
$xdkLib  = Join-Path $xdkRoot "xbox\lib"
$xdkLib2 = Join-Path $xdkRoot "lib"

$privateInc     = "C:\Programming\GitHub\xbox\private\inc"
$msPublicXdkInc = "C:\Programming\GitHub\xbox\public\xdk\inc"
$msPublicSdkInc = "C:\Programming\GitHub\xbox\public\sdk\inc"

$clExe   = Join-Path $vc71Dir "CL.Exe"
$linkExe = Join-Path $vc71Dir "Link.Exe"
$asmExe  = Join-Path $vc71Dir "ml.exe"

$env:Path = "$vc71Dir;$xdkBin;$env:Path"
$env:INCLUDE = "$msPublicXdkInc;$msPublicSdkInc;$xdkInc;$xdkInc2;$privateInc"
$env:LIB     = "$xdkLib;$xdkLib2"

# Source files per sources.inc.  Skip dmusic.cpp and wma.cpp — the latter is
# behind XQUAKE_WMA, the former requires DMUSIC support that may not link
# cleanly without dmusic-related libs we'd need to add.
$sources = @(
    "cd_win.c", "chase.c", "cl_demo.c", "cl_input.c", "cl_main.c", "cl_parse.c",
    "cl_tent.c", "cmd.c", "common.c", "console.c", "crc.c", "cvar.c",
    "gl_draw.c", "gl_fakegl.cpp", "gl_mesh.c", "gl_model.c", "gl_refrag.c",
    "gl_rlight.c", "gl_rmain.c", "gl_rmisc.c", "gl_rsurf.c", "gl_screen.c",
    "gl_test.c", "gl_vidnt.c", "gl_warp.c", "host.c", "host_cmd.c",
    "in_win.c", "in_xbox.cpp", "keys.c", "mathlib.c", "menu.c",
    "net_dgrm.c", "net_loop.c", "net_main.c", "net_vcr.c", "net_win.c", "net_wins.c",
    "pr_cmds.c", "pr_edict.c", "pr_exec.c", "r_part.c", "sbar.c",
    "snd_dma.c", "snd_mem.c", "snd_mix.c", "snd_win.c",
    "sv_main.c", "sv_move.c", "sv_phys.c", "sv_user.c", "sys_win.c",
    "view.c", "wad.c", "world.c", "xgc.cpp", "zone.c"
)

$objDir = Join-Path $OutDir "obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# Compile flags (debug build — matches Microsoft's pre-built XQuaked.exe)
# Per sources.inc: NO_ASSEMBLY, NO_MGRAPH, GLQUAKE, D3DQUAKE, DEBUG_KEYBOARD
# Plus standard XDK flags: _XBOX, WIN32, _DEBUG (debug build)
# Toolchain compatibility: _XBOX_VC71_MIGRATION (matches our JKA build)
# /MTd = static debug C runtime (matches USE_LIBCMT=1 with debug variant)
# /Od (no optimization) for debug; otherwise /Ox per sources.inc
$cflags = @(
    "/nologo", "/c",
    "/Od", "/Z7",                       # debug build: no optimize, embed debug info
    "/MTd",                              # /MTd = static debug CRT (libcmtd.lib)
    "/W3",
    "/I", $XQuakeSrc,
    # Microsoft's official xquake build uses public/xdk/inc which has the FAT
    # d3d8.h (2757 lines) with all Xbox D3D extensions inline.  Our installed
    # XDK has the SPLIT/wsdk variant (1276 lines) that requires a separate
    # D3D8-Xbox.h.  Putting public/xdk/inc FIRST means the fat d3d8.h wins
    # the _D3D8_H_ guard race — exactly how Microsoft's build resolved it.
    "/I", $msPublicXdkInc,
    "/I", $msPublicSdkInc,
    "/I", $xdkInc,
    "/I", $xdkInc2,
    "/I", $privateInc,
    "/D_DEBUG",
    "/D_XBOX",
    "/DXBOX",                              # without underscore — used by cd_win.c, in_win.c guards
    "/DWIN32",
    "/D_XBOX_VC71_MIGRATION",
    "/DNO_ASSEMBLY",
    "/DNO_MGRAPH",
    "/DGLQUAKE",
    "/DD3DQUAKE",
    "/DDEBUG_KEYBOARD",
    "/D_CRT_SECURE_NO_DEPRECATE",
    "/D_CRT_NONSTDC_NO_DEPRECATE"
)

Write-Host "=== Compiling xquake sources ===" -ForegroundColor Cyan
$objs = @()
$failed = @()
foreach ($src in $sources) {
    $srcPath = Join-Path $XQuakeSrc $src
    if (-not (Test-Path $srcPath)) {
        Write-Host "  SKIP (not found): $src" -ForegroundColor Yellow
        continue
    }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj  = Join-Path $objDir "$base.obj"
    $ext  = [System.IO.Path]::GetExtension($src).ToLower()
    $compileAs = if ($ext -eq ".c") { "/Tc$srcPath" } else { "/Tp$srcPath" }
    $args = $cflags + @("/Fo$obj", $compileAs)

    Write-Host "  $src" -ForegroundColor Gray
    & $clExe @args 2>&1 | Tee-Object -Variable clOut | Out-String | Write-Host
    if ($LASTEXITCODE -ne 0) {
        $failed += $src
        Write-Host "    FAILED ($LASTEXITCODE)" -ForegroundColor Red
    } else {
        $objs += $obj
    }
}

if ($failed.Count -gt 0) {
    Write-Host ""
    Write-Host "=== Compile failures ($($failed.Count) of $($sources.Count)) ===" -ForegroundColor Red
    foreach ($f in $failed) { Write-Host "  $f" }
    Write-Host ""
    Write-Host "Stopping before link."
    exit 1
}

Write-Host ""
Write-Host "=== Linking ===" -ForegroundColor Cyan

$libs = @(
    "d3d8d.lib",
    # d3dx8dt.lib (debug-time STATIC D3DX) instead of d3dx8d.lib (which is just
    # an import-stub for d3dx8d.dll — imagebld rejects non-kernel DLL imports
    # for retail XBEs).  This is the same swap we use in the JKA build.
    "d3dx8dt.lib",
    "dsoundd.lib", "xnetd.lib", "xgraphicsd.lib",
    "xbdm.lib", "xkbdd.lib", "xapilibd.lib", "libcmtd.lib", "xboxkrnl.lib"
)

$exePath = Join-Path $OutDir "xquaked.exe"
$mapPath = Join-Path $OutDir "xquaked.map"
$pdbPath = Join-Path $OutDir "xquaked.pdb"

$linkArgs = @(
    "/NOLOGO",
    "/SUBSYSTEM:WINDOWS",
    "/ENTRY:mainCRTStartup",            # xquake uses main() not WinMain()
    "/MACHINE:I386",
    "/DEBUG",
    "/PDB:$pdbPath",
    "/MAP:$mapPath",
    "/OUT:$exePath",
    "/FORCE:MULTIPLE",
    "/FIXED:NO",
    "/IGNORE:4078,4254",
    "/LIBPATH:$xdkLib",
    "/LIBPATH:$xdkLib2"
) + $objs + $libs

& $linkExe @linkArgs 2>&1 | Out-String | Write-Host
if ($LASTEXITCODE -ne 0) {
    Write-Host "LINK FAILED ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=== Converting PE → XBE via patchxbe.py ===" -ForegroundColor Cyan
$xbePath = Join-Path $OutDir "xquaked.xbe"
$patchScript = "C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\patchxbe.py"
& python $patchScript $exePath $xbePath
if ($LASTEXITCODE -ne 0) {
    Write-Host "patchxbe failed ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Green
Write-Host "  EXE: $exePath"
Write-Host "  XBE: $xbePath"
Write-Host "  MAP: $mapPath"
