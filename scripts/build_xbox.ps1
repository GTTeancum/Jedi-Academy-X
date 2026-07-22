param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("sp", "spmp", "mp", "all")]
    [string]$Target,

    [switch]$Clean,

    [switch]$SkipAssets,

    [switch]$SkipStage
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$repoReleaseDir = Join-Path $repoRoot "build\release"
$xdkRoot = "C:\XDK"
$vc71Dir = Join-Path $xdkRoot "xbox\bin\vc71"
$xdkBin = Join-Path $xdkRoot "xbox\bin"
$mlExe = "C:\Program Files (x86)\Microsoft Visual Studio 8\VC\bin\ml.exe"
$pythonExe = "python"
$script:StefxBuildTarget = $Target
$script:StefxHolomatchDirectMap = "hm_borg1"

$clExe = Join-Path $vc71Dir "CL.Exe"
$libExe = Join-Path $vc71Dir "Lib.Exe"
$linkExe = Join-Path $vc71Dir "Link.Exe"
$xsasmExe = Join-Path $xdkBin "xsasm.exe"

$requiredTools = @($clExe, $libExe, $linkExe, $xsasmExe, $mlExe)
foreach ($tool in $requiredTools) {
    if (-not (Test-Path $tool)) {
        throw "Required tool not found: $tool"
    }
}

# OpenJKDF2-pattern toolchain: XDK 5558 PRIMARY, XDK 5849 FALLBACK.
#
# OpenJKDF2 (the FakeGL graft reference per RENDERER_GRAFT.md) successfully
# builds against XDK 5558 with 5849 as a fallback for the few headers 5558
# is missing (stdint.h, winsock2.h, etc.).  The 5558 SDK has the production
# retail static D3D8 lib (xbox\public\xdk\lib\d3d8.lib, 1.8 MB) ??? the same
# variant the shipped JKA Xbox uses (libv "D3D8" no suffix, qfe=4).  XDK
# 5849's licensee distribution stripped this lib, leaving only debug and
# instrumented variants ??? which we suspect causes the kernel to apply
# restrictive shims that hang CreateDevice.
#
# Tool binaries (CL.Exe, Link.Exe) are vc71-era and version-agnostic ??? we
# keep using C:\XDK\xbox\bin\vc71 since the same compiler shipped with
# both XDKs.
#
# 5558 source (xbox source repo extract):
#   xbox\public\xdk\inc       ??? primary headers (FAT 2757-line d3d8.h)
#   xbox\public\sdk\inc       ??? additional 5558 headers
#   xbox\public\xdk\lib       ??? primary libs (incl. retail d3d8.lib)
#   xbox\public\sdk\lib\i386  ??? broader lib set (xgraphics, xnet, etc.)
#
# 5849 fallback (our installed XDK):
#   C:\XDK\xbox\include       ??? for headers 5558 lacks (stdint, winsock2)
#   C:\XDK\include
#   C:\XDK\xbox\lib           ??? for any lib 5558 doesn't have
#   C:\XDK\lib
$xboxSrcXdkInc = "C:\Programming\GitHub\xbox\public\xdk\inc"
$xboxSrcSdkInc = "C:\Programming\GitHub\xbox\public\sdk\inc"
$xboxSrcXdkLib = "C:\Programming\GitHub\xbox\public\xdk\lib"
$xboxSrcSdkLib = "C:\Programming\GitHub\xbox\public\sdk\lib\i386"

$vcIncludeDirs = @(
    (Join-Path $repoRoot "code\win32"),  # contains 5558-d3d8 surgical override
                                          # (d3d8.h, d3d8types.h, d3d8caps.h,
                                          # d3d8perf.h shimmed to 5558 versions)
    "C:\XDK\xbox\include",             # 5849 PRIMARY for everything except d3d8
    "C:\XDK\include"
) | Where-Object { Test-Path $_ }
# Note: 5558-PRIMARY-everything was attempted but breaks STL (xstring) and OLE
# headers in JKA-specific source files.  Surgical 5558-d3d8-only is enough:
# we just need the fat d3d8.h with Xbox extensions inline, so the retail
# d3d8.lib (also from 5558) sees the matching API surface.  All other headers
# stay on 5849 ??? same as everything we've built against to date.

$vcLibDirs = @(
    "C:\XDK_5558\XDK\xbox\lib",        # Plan-B (OpenJKDF2 1:1): primary XDK 5558
                                        # install path ??? d3d8.lib, d3dx8.lib, libc.lib
                                        # all resolved from here (same as OpenJKDF2's
                                        # build_xbox.bat: XDK_ROOT=C:\XDK_5558\XDK\xbox)
    $xboxSrcXdkLib,                    # 5558 source-repo (extra Xbox-only libs)
    $xboxSrcSdkLib,                    # 5558 SDK additions (xgraphics extras)
    "C:\XDK\xbox\lib",                 # 5849 fallback (xonline, dmusic, etc.)
    "C:\XDK\lib"
) | Where-Object { Test-Path $_ }

$env:Path = "$vc71Dir;$xdkBin;$env:Path"
$env:INCLUDE = ($vcIncludeDirs -join ';')
$env:LIB = ($vcLibDirs -join ';')

function Get-XmlAttr {
    param(
        $Node,
        [string]$Name
    )

    if ($null -eq $Node) {
        return $null
    }

    $attributesProperty = $Node.PSObject.Properties["Attributes"]
    if ($null -ne $attributesProperty) {
        $attribute = $attributesProperty.Value[$Name]
        if ($null -ne $attribute) {
            return $attribute.Value
        }
    }

    $property = $Node.PSObject.Properties[$Name]
    if ($null -ne $property) {
        return [string]$property.Value
    }

    return $null
}

function Expand-VcString {
    param(
        [string]$Value,
        [hashtable]$Macros
    )

    if ([string]::IsNullOrEmpty($Value)) {
        return $Value
    }

    $expanded = $Value
    foreach ($entry in $Macros.GetEnumerator()) {
        $expanded = $expanded.Replace('$(' + $entry.Key + ')', $entry.Value)
    }
    return $expanded
}

function Split-VcList {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    return ($Value -split '[;,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

function Resolve-ProjectPath {
    param(
        [string]$BaseDir,
        [string]$PathValue
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $PathValue))
}

function Convert-CompilerFlags {
    param($Tool)

    $flags = New-Object System.Collections.Generic.List[string]
    $flags.Add("/nologo")
    $flags.Add("/c")

    $optimization = Get-XmlAttr -Node $Tool -Name "Optimization"
    if ($optimization -eq "2") { $flags.Add("/O2") }
    elseif ($optimization -eq "3") { $flags.Add("/Ox") }

    if ((Get-XmlAttr -Node $Tool -Name "InlineFunctionExpansion") -eq "2") { $flags.Add("/Ob2") }
    if ((Get-XmlAttr -Node $Tool -Name "EnableIntrinsicFunctions") -eq "true") { $flags.Add("/Oi") }
    if ((Get-XmlAttr -Node $Tool -Name "StringPooling") -eq "true") { $flags.Add("/GF") }
    if ((Get-XmlAttr -Node $Tool -Name "EnableFunctionLevelLinking") -eq "true") { $flags.Add("/Gy") }

    $favor = Get-XmlAttr -Node $Tool -Name "FavorSizeOrSpeed"
    if ($favor -eq "1") { $flags.Add("/Ot") }
    elseif ($favor -eq "2") { $flags.Add("/Os") }

    if ((Get-XmlAttr -Node $Tool -Name "RuntimeLibrary") -eq "0") { $flags.Add("/MT") }

    $omitFp = Get-XmlAttr -Node $Tool -Name "OmitFramePointers"
    if ($omitFp -eq "true") { $flags.Add("/Oy") }
    elseif ($omitFp -eq "false") { $flags.Add("/Oy-") }

    if ((Get-XmlAttr -Node $Tool -Name "WarningLevel") -eq "3") { $flags.Add("/W3") }
    if ((Get-XmlAttr -Node $Tool -Name "WarningLevel") -eq "4") { $flags.Add("/W4") }
    if ((Get-XmlAttr -Node $Tool -Name "DebugInformationFormat") -eq "3") { $flags.Add("/Z7") }

    $additionalOptions = Get-XmlAttr -Node $Tool -Name "AdditionalOptions"
    if (-not [string]::IsNullOrWhiteSpace($additionalOptions)) {
        foreach ($opt in ($additionalOptions -split '\s+' | Where-Object { $_ })) {
            $flags.Add($opt)
        }
    }

    return $flags
}

function Get-ProjectSourceFiles {
    param(
        [xml]$Xml,
        [string]$ConfigurationName,
        [string]$ProjectDir,
        [hashtable]$Macros
    )

    $fileNodes = $Xml.SelectNodes("//File")
    $sources = New-Object System.Collections.Generic.List[object]

    foreach ($fileNode in $fileNodes) {
        $relativePath = $fileNode.RelativePath
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $ext = [System.IO.Path]::GetExtension($relativePath).ToLowerInvariant()
        if ($ext -notin @(".c", ".cpp", ".cxx", ".cc", ".asm", ".vsh", ".psh")) {
            continue
        }

        $fileCfg = $null
        foreach ($candidate in $fileNode.SelectNodes("FileConfiguration")) {
            if ($candidate.Name -eq $ConfigurationName) {
                $fileCfg = $candidate
                break
            }
        }

        $tool = $null
        if ($fileCfg) {
            foreach ($candidateTool in $fileCfg.Tool) {
                if ($candidateTool.Name -eq "VCCLCompilerTool" -or
                    $candidateTool.Name -eq "VCCustomBuildTool") {
                    $tool = $candidateTool
                    break
                }
            }
        }

        if ((Get-XmlAttr -Node $tool -Name "ExcludedFromBuild") -eq "true") {
            continue
        }

        $sources.Add([pscustomobject]@{
            RelativePath = $relativePath
            FullPath     = Resolve-ProjectPath -BaseDir $ProjectDir -PathValue (Expand-VcString -Value $relativePath -Macros $Macros)
            Extension    = $ext
            Tool         = $tool
        })
    }

    return $sources
}

function Apply-ProjectSourceOverrides {
    param(
        [string]$ProjectPath,
        [System.Collections.Generic.List[object]]$Sources
    )

    if ($ProjectPath -eq "code\x_game\x_game.vcproj") {
        if ($script:StefxBuildTarget -eq "spmp") {
            $efRoot = Join-Path $repoRoot "code\holomatch\official"
            $efGameProject = $null
            $efGameDsp = Join-Path $efRoot "game\game.dsp"
        }
        else {
            $efRoot = Join-Path $repoRoot "SP-Mod-Source-Code-master"
            $efGameDsp = Join-Path $efRoot "game\game.dsp"
        }
        if ($script:StefxBuildTarget -eq "spmp" -and $efGameProject -and (Test-Path $efGameProject)) {
            [xml]$efProjectXml = Get-Content -Path $efGameProject
            $efProjectDir = Split-Path -Parent $efGameProject
            $efMacros = @{
                SolutionDir       = "$efProjectDir\"
                ProjectDir        = "$efProjectDir\"
                ProjectPath       = $efGameProject
                ProjectName       = "x_jk2game"
                ConfigurationName = "Release"
                OutDir            = "$efProjectDir\Release\"
                IntDir            = "$efProjectDir\Release\"
            }
            $efProjectSources = Get-ProjectSourceFiles -Xml $efProjectXml -ConfigurationName "Release|Win32" -ProjectDir $efProjectDir -Macros $efMacros
            $efSources = New-Object System.Collections.Generic.List[object]
            foreach ($projectSource in $efProjectSources) {
                $efSources.Add($projectSource)
            }
            $efIncludeDirs = @(
                $efRoot,
                (Join-Path $efRoot "game"),
                (Join-Path $efRoot "cgame"),
                (Join-Path $efRoot "qcommon"),
                (Join-Path $efRoot "client"),
                (Join-Path $efRoot "ui")
            ) -join ';'
            $efSourceDefinitions = "STEFX_ELITE_FORCE_SP;STEFX_ELITE_FORCE_MP;STEFX_SP_HOSTED_MP;Com_Printf=STEFX_HM_Com_Printf;Com_Error=STEFX_HM_Com_Error;_GAME;_JK2MP;_X86_"

            foreach ($source in $efSources) {
                $source.Tool = [pscustomobject]@{
                    Name                      = "VCCLCompilerTool"
                    PrependIncludeDirectories = $efIncludeDirs
                    PreprocessorDefinitions   = $efSourceDefinitions
                    CompileAs                 = (Get-XmlAttr -Node $source.Tool -Name "CompileAs")
                }
                $source.RelativePath = [System.IO.Path]::GetFullPath($source.FullPath).Substring($repoRoot.Length + 1)
            }

            $efCompatPath = Join-Path $efRoot "game\stefx_xbox_compat.cpp"
            if (Test-Path $efCompatPath) {
                $efSources.Add([pscustomobject]@{
                    RelativePath = [System.IO.Path]::GetFullPath($efCompatPath).Substring($repoRoot.Length + 1)
                    FullPath     = $efCompatPath
                    Extension    = ".cpp"
                    Tool         = [pscustomobject]@{
                        Name                      = "VCCLCompilerTool"
                        PrependIncludeDirectories = $efIncludeDirs
                        PreprocessorDefinitions   = $efSourceDefinitions
                    }
                })
            }

            $hmGamePath = Join-Path $repoRoot "code\game\stefx_holomatch_game.cpp"
            if (Test-Path $hmGamePath) {
                $efSources.Add([pscustomobject]@{
                    RelativePath = [System.IO.Path]::GetFullPath($hmGamePath).Substring($repoRoot.Length + 1)
                    FullPath     = $hmGamePath
                    Extension    = ".cpp"
                    Tool         = $null
                })
            }

            $hmApiPath = Join-Path $repoRoot "code\holomatch\stefx_mp_game_api.cpp"
            if (Test-Path $hmApiPath) {
                $efSources.Add([pscustomobject]@{
                    RelativePath = [System.IO.Path]::GetFullPath($hmApiPath).Substring($repoRoot.Length + 1)
                    FullPath     = $hmApiPath
                    Extension    = ".cpp"
                    Tool         = $null
                })
            }

            return $efSources
        }
        if (Test-Path $efGameDsp) {
            $efSources = New-Object System.Collections.Generic.List[object]
            $efIncludeDirs = @(
                $efRoot,
                (Join-Path $efRoot "game"),
                (Join-Path $efRoot "cgame"),
                (Join-Path $efRoot "icarus"),
                (Join-Path $efRoot "qcommon"),
                (Join-Path $efRoot "renderer"),
                (Join-Path $efRoot "client"),
                (Join-Path $efRoot "ui")
            ) -join ';'

            if ($script:StefxBuildTarget -eq "spmp") {
                $efSourceDefinitions = "STEFX_ELITE_FORCE_SP;STEFX_ELITE_FORCE_MP;STEFX_SP_HOSTED_MP;Com_Printf=STEFX_HM_Com_Printf;Com_Error=STEFX_HM_Com_Error;_GAME;_JK2MP;_X86_"
            }
            else {
                $efSourceDefinitions = "STEFX_ELITE_FORCE_SP;_X86_"
            }

            $seen = @{}
            foreach ($line in (Get-Content -Path $efGameDsp)) {
                if ($line -notmatch '^SOURCE=(.+)$') {
                    continue
                }

                $relativePath = $Matches[1].Trim()
                $ext = [System.IO.Path]::GetExtension($relativePath).ToLowerInvariant()
                if ($ext -notin @(".c", ".cpp", ".cxx", ".cc")) {
                    continue
                }
                $fileName = [System.IO.Path]::GetFileName($relativePath)
                if ($fileName -ieq "bg_lib.c" -or
                    $fileName -ieq "bg_lib.cpp" -or
                    ($script:StefxBuildTarget -ne "spmp" -and
                        ($fileName -ieq "q_math.c" -or
                         $fileName -ieq "q_math.cpp" -or
                         $fileName -ieq "q_shared.c" -or
                         $fileName -ieq "q_shared.cpp"))) {
                    continue
                }

                $fullPath = Resolve-ProjectPath -BaseDir (Join-Path $efRoot "game") -PathValue $relativePath
                if (-not (Test-Path $fullPath)) {
                    throw "Elite Force source listed in game.dsp was not found: $relativePath"
                }

                $fullKey = [System.IO.Path]::GetFullPath($fullPath).ToLowerInvariant()
                if ($seen.ContainsKey($fullKey)) {
                    continue
                }
                $seen[$fullKey] = $true

                $efSources.Add([pscustomobject]@{
                    RelativePath = [System.IO.Path]::GetFullPath($fullPath).Substring($repoRoot.Length + 1)
                    FullPath     = $fullPath
                    Extension    = $ext
                    Tool         = [pscustomobject]@{
                        Name                      = "VCCLCompilerTool"
                        PrependIncludeDirectories = $efIncludeDirs
                        PreprocessorDefinitions   = $efSourceDefinitions
                    }
                })
            }

            $efCompatPath = Join-Path $efRoot "game\stefx_xbox_compat.cpp"
            if (Test-Path $efCompatPath) {
                $efSources.Add([pscustomobject]@{
                    RelativePath = [System.IO.Path]::GetFullPath($efCompatPath).Substring($repoRoot.Length + 1)
                    FullPath     = $efCompatPath
                    Extension    = ".cpp"
                    Tool         = [pscustomobject]@{
                        Name                      = "VCCLCompilerTool"
                        PrependIncludeDirectories = $efIncludeDirs
                        PreprocessorDefinitions   = $efSourceDefinitions
                    }
                })
            }

            if ($script:StefxBuildTarget -eq "spmp") {
                # Holomatch is the official EF game/cgame pair hosted by the
                # SP engine.  The renderer, sound, input, filesystem and UI
                # remain the code/ implementations; only the multiplayer VM
                # layer uses the official EF sources kept under code/.
                $hmCgameRoot = Join-Path $repoRoot "code\holomatch\official\cgame"
                $hmCgameIncludeDirs = @(
                    (Join-Path $repoRoot "code\holomatch\official"),
                    (Join-Path $repoRoot "code\holomatch\official\game"),
                    $hmCgameRoot
                ) -join ';'
                $hmCgameSyscalls = Join-Path $hmCgameRoot "cg_syscalls.c"
                $hmCgameTrapNames = @(
                    Get-Content -LiteralPath $hmCgameSyscalls |
                    ForEach-Object { [regex]::Matches($_, '\btrap_[A-Za-z0-9_]+\s*\(') } |
                    ForEach-Object { $_.Value.Substring(0, $_.Value.IndexOf('(')).Trim() } |
                    Sort-Object -Unique
                )
                $hmCgameTrapDefinitions = @(
                    $hmCgameTrapNames | ForEach-Object { "$_=STEFX_HM_CG_$_" }
                ) -join ';'
                $hmCgameDefinitions = "STEFX_ELITE_FORCE_SP;STEFX_ELITE_FORCE_MP;STEFX_SP_HOSTED_MP;vmMain=STEFX_HM_CG_vmMain;dllEntry=STEFX_HM_CG_dllEntry;PASSFLOAT=STEFX_HM_CG_PASSFLOAT;Com_Printf=STEFX_HM_CG_Com_Printf;Com_Error=STEFX_HM_CG_Com_Error;fxRandCircumferencePos=STEFX_HM_CG_fxRandCircumferencePos;_CGAME;_X86_;$hmCgameTrapDefinitions"
                foreach ($hmCgameSource in (Get-ChildItem -LiteralPath $hmCgameRoot -File |
                    Where-Object { $_.Extension -eq ".c" } |
                    Sort-Object Name)) {
                    $efSources.Add([pscustomobject]@{
                        RelativePath = [System.IO.Path]::GetFullPath($hmCgameSource.FullName).Substring($repoRoot.Length + 1)
                        FullPath     = $hmCgameSource.FullName
                        Extension    = ".c"
                        Tool         = [pscustomobject]@{
                            Name                       = "VCCLCompilerTool"
                            PrependIncludeDirectories = $hmCgameIncludeDirs
                            PreprocessorDefinitions   = $hmCgameDefinitions
                        }
                    })
                }

                # The SP engine has no multiplayer AAS implementation. Keep the
                # engine-side bot library inside code/ so Holomatch remains
                # independent of the retired codemp tree.
                $hmBotlibRoot = Join-Path $repoRoot "code\holomatch\botlib"
                $hmBotlibIncludeDirs = @(
                    $hmBotlibRoot,
                    (Join-Path $repoRoot "code\holomatch\official\game")
                ) -join ';'
                $hmBotlibCompat = Join-Path $hmBotlibRoot "stefx_botlib_compat.h"
                foreach ($hmBotlibSource in (Get-ChildItem -LiteralPath $hmBotlibRoot -File |
                    Where-Object { $_.Extension -eq ".cpp" } |
                    Sort-Object Name)) {
                    $efSources.Add([pscustomobject]@{
                        RelativePath = [System.IO.Path]::GetFullPath($hmBotlibSource.FullName).Substring($repoRoot.Length + 1)
                        FullPath     = $hmBotlibSource.FullName
                        Extension    = ".cpp"
                        Tool         = [pscustomobject]@{
                            Name                       = "VCCLCompilerTool"
                            PrependIncludeDirectories = $hmBotlibIncludeDirs
                            PreprocessorDefinitions   = "STEFX_ELITE_FORCE_MP;STEFX_SP_HOSTED_MP;BOTLIB;_X86_"
                            AdditionalOptions         = "/FI`"$hmBotlibCompat`""
                        }
                    })
                }
            }

            if ($script:StefxBuildTarget -eq "spmp") {
                $hmGamePath = Join-Path $repoRoot "code\game\stefx_holomatch_game.cpp"
                if (Test-Path $hmGamePath) {
                    $efSources.Add([pscustomobject]@{
                        RelativePath = [System.IO.Path]::GetFullPath($hmGamePath).Substring($repoRoot.Length + 1)
                        FullPath     = $hmGamePath
                        Extension    = ".cpp"
                        Tool         = $null
                    })
                }

                $hmApiPath = Join-Path $repoRoot "code\holomatch\stefx_mp_game_api.cpp"
                if (Test-Path $hmApiPath) {
                    $efSources.Add([pscustomobject]@{
                        RelativePath = [System.IO.Path]::GetFullPath($hmApiPath).Substring($repoRoot.Length + 1)
                        FullPath     = $hmApiPath
                        Extension    = ".cpp"
                        Tool         = $null
                    })
                }

                $hmBotBridgePath = Join-Path $repoRoot "code\holomatch\stefx_holomatch_bot_bridge.cpp"
                if (Test-Path $hmBotBridgePath) {
                    $efSources.Add([pscustomobject]@{
                        RelativePath = [System.IO.Path]::GetFullPath($hmBotBridgePath).Substring($repoRoot.Length + 1)
                        FullPath     = $hmBotBridgePath
                        Extension    = ".cpp"
                        Tool         = $null
                    })
                }

            }

            return $efSources
        }
    }

    if ($ProjectPath -eq "code\x_exe\x_exe.vcproj") {
        $filtered = New-Object System.Collections.Generic.List[object]
        foreach ($source in $Sources) {
            if ($source.RelativePath -ieq "..\win32\dbg_console_xbox.cpp") {
                continue
            }
            $filtered.Add($source)
        }

        # Plan-B (OpenJKDF2 1:1): d3dx8_compat.cpp removed ??? we now link
        # real d3dx8.lib from XDK 5558.  Local shim no longer needed.

        # Plan-B (OpenJKDF2 1:1): add OpenJKDF2's fakeglx.cpp (byte-identical
        # copy at code\win32\openjkdf2\fakeglx.cpp).  Compiled with /FI
        # platform_xbox.h force-include ??? that header is OpenJKDF2's compat
        # shim providing stdint, snprintf, BOOL, etc. that fakeglx.cpp
        # expects.  Replaces the prior xquake\gl_fakegl.cpp graft.
        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\fakeglx.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\fakeglx.cpp"
            Extension    = ".cpp"
            Tool         = [pscustomobject]@{
                Name                      = "VCCLCompilerTool"
                PrependIncludeDirectories = "C:\XDK_5558\XDK\xbox\include;C:\XDK\xbox\include;C:\XDK\include"
                AdditionalOptions         = "/FI`"openjkdf2/platform_xbox.h`""
            }
        })

        # Plan-B compat layer: gl_* functions JKA calls that fakeglx.cpp
        # doesn't export.  Real implementations where possible; deferred
        # for paths beyond SP_DoLicense.  See file head comment.
        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\fakeglx_jka_compat.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\fakeglx_jka_compat.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        # XDK 5558 headers are now first in the include path, so the old
        # 5849-signature D3D shim is no longer needed and would collide with
        # the official prototypes.

        # Plan-B DDS bridge: JKA uses DXT1/3/5 compressed textures via
        # GL_DDS*_EXT internalformats that fakeglx can't decode.  This
        # file's JkaGlTexImage2D detects them, decodes to RGBA, and
        # forwards to fakegl's real glTexImage2D.  qgl_console.h
        # #define-redirects JKA call sites to JkaGlTexImage2D.
        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\glteximage_dds.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\glteximage_dds.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        # Elite Force uses loose files and PK3 archives.  The inherited Xbox
        # executable project only had the GOB console filesystem sources, so
        # inject Raven's unzip/zlib32 reader pieces for PK3 fallback support.
        foreach ($pk3Source in @(
            "code\qcommon\unzip.cpp",
            "code\zlib32\inflate.cpp",
            "code\zlib32\deflate.cpp",
            "code\zlib32\zipcommon.cpp"
        )) {
            $filtered.Add([pscustomobject]@{
                RelativePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $pk3Source)).Substring($repoRoot.Length + 1)
                FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $pk3Source
                Extension    = ".cpp"
                Tool         = $null
            })
        }

        # Elite Force ships most voice/effect assets as MP3.  The console
        # sound loader decodes those to PCM at load time, so link Raven's
        # existing MP3 decoder C sources into the executable.
        foreach ($mp3Source in @(
            "code\mp3code\cdct.c",
            "code\mp3code\csbt.c",
            "code\mp3code\csbtL3.c",
            "code\mp3code\csbtb.c",
            "code\mp3code\cup.c",
            "code\mp3code\cupL1.c",
            "code\mp3code\cupini.c",
            "code\mp3code\cupl3.c",
            "code\mp3code\cwin.c",
            "code\mp3code\cwinb.c",
            "code\mp3code\cwinm.c",
            "code\mp3code\hwin.c",
            "code\mp3code\l3dq.c",
            "code\mp3code\l3init.c",
            "code\mp3code\mdct.c",
            "code\mp3code\mhead.c",
            "code\mp3code\msis.c",
            "code\mp3code\towave.c",
            "code\mp3code\uph.c",
            "code\mp3code\upsf.c",
            "code\mp3code\wavep.c"
        )) {
            $filtered.Add([pscustomobject]@{
                RelativePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $mp3Source)).Substring($repoRoot.Length + 1)
                FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $mp3Source
                Extension    = ".c"
                Tool         = $null
            })
        }

        # Elite Force retail assets are mostly JPG/TGA.  The inherited Xbox
        # project only carried the JPEG headers, so inject the existing
        # renderer wrapper and libjpeg sources for SP material loading.
        $jpegSources = @(
            "code\renderer\tr_jpeg_interface.cpp",
            "code\jpeg-6\jcapimin.cpp",
            "code\jpeg-6\jccoefct.cpp",
            "code\jpeg-6\jccolor.cpp",
            "code\jpeg-6\jcdctmgr.cpp",
            "code\jpeg-6\jchuff.cpp",
            "code\jpeg-6\jcinit.cpp",
            "code\jpeg-6\jcmainct.cpp",
            "code\jpeg-6\jcmarker.cpp",
            "code\jpeg-6\jcmaster.cpp",
            "code\jpeg-6\jcomapi.cpp",
            "code\jpeg-6\jcparam.cpp",
            "code\jpeg-6\jcphuff.cpp",
            "code\jpeg-6\jcprepct.cpp",
            "code\jpeg-6\jcsample.cpp",
            "code\jpeg-6\jctrans.cpp",
            "code\jpeg-6\jdapimin.cpp",
            "code\jpeg-6\jdapistd.cpp",
            "code\jpeg-6\jdatadst.cpp",
            "code\jpeg-6\jdatasrc.cpp",
            "code\jpeg-6\jdcoefct.cpp",
            "code\jpeg-6\jdcolor.cpp",
            "code\jpeg-6\jddctmgr.cpp",
            "code\jpeg-6\jdhuff.cpp",
            "code\jpeg-6\jdinput.cpp",
            "code\jpeg-6\jdmainct.cpp",
            "code\jpeg-6\jdmarker.cpp",
            "code\jpeg-6\jdmaster.cpp",
            "code\jpeg-6\jdpostct.cpp",
            "code\jpeg-6\jdsample.cpp",
            "code\jpeg-6\jdtrans.cpp",
            "code\jpeg-6\jerror.cpp",
            "code\jpeg-6\jfdctflt.cpp",
            "code\jpeg-6\jidctflt.cpp",
            "code\jpeg-6\jmemmgr.cpp",
            "code\jpeg-6\jmemnobs.cpp",
            "code\jpeg-6\jutils.cpp"
        )
        $jpegTool = [pscustomobject]@{
            Name                    = "VCCLCompilerTool"
            PreprocessorDefinitions = "TAG_TEMP_JPG=TAG_TEMP_WORKSPACE;NO_GETENV"
        }
        foreach ($jpegSource in $jpegSources) {
            $filtered.Add([pscustomobject]@{
                RelativePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $jpegSource)).Substring($repoRoot.Length + 1)
                FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $jpegSource
                Extension    = ".cpp"
                Tool         = $jpegTool
            })
        }

        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\dbg_console_xbox_stub.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\dbg_console_xbox_stub.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        if ($script:StefxBuildTarget -eq "spmp") {
            $hmHostPath = Join-Path $repoRoot "code\server\stefx_holomatch_host.cpp"
            if (Test-Path $hmHostPath) {
                $filtered.Add([pscustomobject]@{
                    RelativePath = "..\server\stefx_holomatch_host.cpp"
                    FullPath     = $hmHostPath
                    Extension    = ".cpp"
                    Tool         = $null
                })
            }

            $hmEngineCompatPath = Join-Path $repoRoot "code\holomatch\stefx_sp_engine_mp_compat.cpp"
            if (Test-Path $hmEngineCompatPath) {
                $filtered.Add([pscustomobject]@{
                    RelativePath = "..\holomatch\stefx_sp_engine_mp_compat.cpp"
                    FullPath     = $hmEngineCompatPath
                    Extension    = ".cpp"
                    Tool         = $null
                })
            }

        }

        return $filtered
    }

    if ($ProjectPath -eq "codemp\x_jk2game\x_jk2game.vcproj") {
        $hasStefxAnimTable = $false
        foreach ($source in $Sources) {
            if ($source.RelativePath -ieq "..\game\stefx_animtable.c") {
                $hasStefxAnimTable = $true
                break
            }
        }
        if (-not $hasStefxAnimTable) {
            $Sources.Add([pscustomobject]@{
                RelativePath = "..\game\stefx_animtable.c"
                FullPath     = Resolve-ProjectPath -BaseDir (Join-Path $repoRoot "codemp\x_jk2game") -PathValue "..\game\stefx_animtable.c"
                Extension    = ".c"
                Tool         = [pscustomobject]@{
                    Name                 = "VCCLCompilerTool"
                    UsePrecompiledHeader = "0"
                    CompileAs            = "2"
                }
            })
        }
        return $Sources
    }

    if ($ProjectPath -eq "codemp\x_jk2cgame\x_jk2cgame.vcproj") {
        $legacyJaCgameUiSources = @(
            "..\cgame\cg_newDraw.c",
            "..\cgame\cg_stefx_menu_stub.c"
        )
        $requiredEfCgameUiSource = "..\cgame\cg_stefx_ui_shim.c"
        $hasEfCgameUiSource = $false

        foreach ($source in $Sources) {
            $relative = $source.RelativePath.Replace('/', '\')
            if ($legacyJaCgameUiSources -icontains $relative) {
                throw "Holomatch x_jk2cgame cannot compile inherited MP cgame menu source: $relative"
            }
            if ($relative -ieq $requiredEfCgameUiSource) {
                $hasEfCgameUiSource = $true
            }
        }

        if (-not $hasEfCgameUiSource) {
            throw "Holomatch x_jk2cgame is missing required EF cgame UI shim: $requiredEfCgameUiSource"
        }

        return $Sources
    }

    if ($ProjectPath -eq "codemp\x_ui\x_ui.vcproj") {
        $legacyJaUiSources = @(
            "..\ui\ui_atoms.c",
            "..\ui\ui_force.c",
            "..\ui\ui_gameinfo.c",
            "..\ui\ui_main.c",
            "..\ui\ui_players.c",
            "..\ui\ui_saber.c",
            "..\ui\ui_shared.c",
            "..\ui\ui_syscalls.c",
            "..\ui\ui_util.c",
            "..\ui\ui_stefx_stub.c"
        )
        $requiredEfUiSources = @(
            "..\ui\ui_stefx_spbridge.cpp",
            "..\..\code\ui\ui_ef_frontend.cpp",
            "..\..\code\ui\ui_ef_lifecycle.cpp",
            "..\..\code\ui\ui_ef_pause.cpp",
            "..\..\code\ui\ui_ef_qmenu.cpp"
        )
        $seenEfUiSources = @{}

        foreach ($source in $Sources) {
            $relative = $source.RelativePath.Replace('/', '\')
            if ($legacyJaUiSources -icontains $relative) {
                throw "Holomatch x_ui cannot compile inherited MP UI source: $relative"
            }
            if ($requiredEfUiSources -icontains $relative) {
                $seenEfUiSources[$relative.ToLowerInvariant()] = $true
            }
        }

        foreach ($requiredSource in $requiredEfUiSources) {
            if (-not $seenEfUiSources.ContainsKey($requiredSource.ToLowerInvariant())) {
                throw "Holomatch x_ui is missing required EF/SP UI source: $requiredSource"
            }
        }

        return $Sources
    }

    if ($ProjectPath -eq "codemp\x_exe\x_exe.vcproj") {
        $hasAsmStub = $false
		$jpegSources = @(
			"codemp\jpeg-6\jcapimin.cpp",
			"codemp\jpeg-6\jccoefct.cpp",
			"codemp\jpeg-6\jccolor.cpp",
			"codemp\jpeg-6\jcdctmgr.cpp",
			"codemp\jpeg-6\jchuff.cpp",
			"codemp\jpeg-6\jcinit.cpp",
			"codemp\jpeg-6\jcmainct.cpp",
			"codemp\jpeg-6\jcmarker.cpp",
			"codemp\jpeg-6\jcmaster.cpp",
			"codemp\jpeg-6\jcomapi.cpp",
			"codemp\jpeg-6\jcparam.cpp",
			"codemp\jpeg-6\jcphuff.cpp",
			"codemp\jpeg-6\jcprepct.cpp",
			"codemp\jpeg-6\jcsample.cpp",
			"codemp\jpeg-6\jctrans.cpp",
			"codemp\jpeg-6\jdapimin.cpp",
			"codemp\jpeg-6\jdapistd.cpp",
			"codemp\jpeg-6\jdatadst.cpp",
			"codemp\jpeg-6\jdatasrc.cpp",
			"codemp\jpeg-6\jdcoefct.cpp",
			"codemp\jpeg-6\jdcolor.cpp",
			"codemp\jpeg-6\jddctmgr.cpp",
			"codemp\jpeg-6\jdhuff.cpp",
			"codemp\jpeg-6\jdinput.cpp",
			"codemp\jpeg-6\jdmainct.cpp",
			"codemp\jpeg-6\jdmarker.cpp",
			"codemp\jpeg-6\jdmaster.cpp",
			"codemp\jpeg-6\jdpostct.cpp",
			"codemp\jpeg-6\jdsample.cpp",
			"codemp\jpeg-6\jdtrans.cpp",
			"codemp\jpeg-6\jerror.cpp",
			"codemp\jpeg-6\jfdctflt.cpp",
			"codemp\jpeg-6\jidctflt.cpp",
			"codemp\jpeg-6\jmemmgr.cpp",
			"codemp\jpeg-6\jmemnobs.cpp",
			"codemp\jpeg-6\jutils.cpp"
		)
		$jpegTool = [pscustomobject]@{
			Name                    = "VCCLCompilerTool"
			PreprocessorDefinitions = "TAG_TEMP_JPG=TAG_TEMP_WORKSPACE;NO_GETENV"
		}
        foreach ($source in $Sources) {
            if ($source.RelativePath.StartsWith("..\renderer\", [StringComparison]::OrdinalIgnoreCase) -and
                $source.Extension -in @(".c", ".cpp", ".cxx", ".cc")) {
                $source.Tool = [pscustomobject]@{
                    Name                    = "VCCLCompilerTool"
                    PreprocessorDefinitions = "STEFX_ELITE_FORCE_SP"
                }
            }
        }
		foreach ($jpegSource in $jpegSources) {
			$Sources.Add([pscustomobject]@{
				RelativePath = $jpegSource
				FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $jpegSource
				Extension    = ".cpp"
				Tool         = $jpegTool
			})
		}
        foreach ($source in $Sources) {
            if ($source.RelativePath -ieq "xbox_asm_stubs.asm") {
                $hasAsmStub = $true
                break
            }
        }
        if (-not $hasAsmStub) {
            $Sources.Add([pscustomobject]@{
                RelativePath = "xbox_asm_stubs.asm"
                FullPath     = Resolve-ProjectPath -BaseDir (Join-Path $repoRoot "codemp\x_exe") -PathValue "xbox_asm_stubs.asm"
                Extension    = ".asm"
                Tool         = $null
            })
        }

        $Sources.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\fakeglx.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\fakeglx.cpp"
            Extension    = ".cpp"
            Tool         = [pscustomobject]@{
                Name                      = "VCCLCompilerTool"
                PrependIncludeDirectories = "C:\XDK_5558\XDK\xbox\include;C:\XDK\xbox\include;C:\XDK\include"
                AdditionalOptions         = "/FI`"openjkdf2/platform_xbox.h`""
            }
        })

        $Sources.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\fakeglx_jka_compat.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\fakeglx_jka_compat.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        $Sources.Add([pscustomobject]@{
            RelativePath = "..\win32\openjkdf2\glteximage_dds.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\openjkdf2\glteximage_dds.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        foreach ($pk3Source in @(
            "codemp\qcommon\unzip.cpp",
            "codemp\zlib32\inflate.cpp",
            "codemp\zlib32\deflate.cpp",
            "codemp\zlib32\zipcommon.cpp"
        )) {
            $Sources.Add([pscustomobject]@{
                RelativePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $pk3Source)).Substring($repoRoot.Length + 1)
                FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $pk3Source
                Extension    = ".cpp"
                Tool         = $null
            })
        }

        foreach ($mp3Source in @(
            "codemp\mp3code\cdct.c",
            "codemp\mp3code\csbt.c",
            "codemp\mp3code\csbtl3.c",
            "codemp\mp3code\csbtb.c",
            "codemp\mp3code\cup.c",
            "codemp\mp3code\cupl1.c",
            "codemp\mp3code\cupini.c",
            "codemp\mp3code\cupl3.c",
            "codemp\mp3code\cwin.c",
            "codemp\mp3code\cwinb.c",
            "codemp\mp3code\cwinm.c",
            "codemp\mp3code\hwin.c",
            "codemp\mp3code\l3dq.c",
            "codemp\mp3code\l3init.c",
            "codemp\mp3code\mdct.c",
            "codemp\mp3code\mhead.c",
            "codemp\mp3code\msis.c",
            "codemp\mp3code\towave.c",
            "codemp\mp3code\uph.c",
            "codemp\mp3code\upsf.c",
            "codemp\mp3code\wavep.c"
        )) {
            $Sources.Add([pscustomobject]@{
                RelativePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $mp3Source)).Substring($repoRoot.Length + 1)
                FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $mp3Source
                Extension    = ".c"
                Tool         = $null
            })
        }

        return $Sources
    }

    return $Sources
}

function Invoke-External {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    $argLine = ($Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    }) -join ' '

    Write-Host "$([System.IO.Path]::GetFileName($Exe)) $argLine"
    Push-Location $WorkingDirectory
    $responseFile = $null
    try {
        $useResponseFile =
            ($Exe -ieq $libExe -or $Exe -ieq $linkExe) -and
            ($argLine.Length -gt 7000)

        if ($useResponseFile) {
            $responseFile = [System.IO.Path]::GetTempFileName()
            $Arguments | Set-Content -Path $responseFile -Encoding ASCII
            $savedErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                & $Exe "@$responseFile" 2>&1 | ForEach-Object { Write-Host $_ }
            }
            finally {
                $ErrorActionPreference = $savedErrorActionPreference
            }
        }
        else {
            $savedErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
            }
            finally {
                $ErrorActionPreference = $savedErrorActionPreference
            }
        }
        if ($LASTEXITCODE -ne 0) {
            throw "$Exe failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        if ($responseFile -and (Test-Path $responseFile)) {
            Remove-Item $responseFile -Force -ErrorAction SilentlyContinue
        }
        Pop-Location
    }
}

function Get-ObjectPath {
    param(
        [string]$IntDir,
        [string]$SourcePath
    )

    $repoUri = New-Object System.Uri(($repoRoot.TrimEnd('\') + '\'))
    $sourceUri = New-Object System.Uri($SourcePath)
    $relative = [System.Uri]::UnescapeDataString($repoUri.MakeRelativeUri($sourceUri).ToString()).Replace('/', '\')
    $relative = [System.IO.Path]::ChangeExtension($relative, ".obj")
    return Join-Path $IntDir $relative
}

function Prepare-SpHostedSupportLibrary {
    $sourceLibrary = Join-Path $repoRoot "code\x_exe\Release\x_game.lib"
    $supportLibrary = Join-Path $repoRoot "code\x_exe\Release\spmp\sp_hosted_support.lib"
    $duplicateMember = Join-Path $repoRoot "code\x_exe\Release\game\SP-Mod-Source-Code-master\game\bg_misc.obj"

    if (-not (Test-Path -LiteralPath $sourceLibrary -PathType Leaf)) {
        throw "Cannot prepare SP-hosted support library; missing: $sourceLibrary"
    }

    Copy-Item -LiteralPath $sourceLibrary -Destination $supportLibrary -Force
    $supportMembers = @(& $libExe /nologo /list $supportLibrary)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect SP-hosted support library: $supportLibrary"
    }
    $removeArguments = New-Object System.Collections.Generic.List[string]
    foreach ($member in $supportMembers) {
        if ($member -eq $duplicateMember -or $member -match '\\SP-Mod-Source-Code-master\\cgame\\') {
            $removeArguments.Add("/REMOVE:$member")
        }
    }
    if ($removeArguments.Count -gt 0) {
        $removeArguments.Add($supportLibrary)
        Invoke-External -Exe $libExe -Arguments $removeArguments -WorkingDirectory $repoRoot
    }

    return $supportLibrary
}

function Build-Project {
    param(
        [string]$ProjectPath
    )

    $fullProjectPath = Resolve-ProjectPath -BaseDir $repoRoot -PathValue $ProjectPath
    $projectDir = Split-Path -Parent $fullProjectPath
    $projectName = [System.IO.Path]::GetFileNameWithoutExtension($fullProjectPath)

    [xml]$xml = Get-Content $fullProjectPath
    $configurationName = "Release|Win32"
    $configuration = @($xml.VisualStudioProject.Configurations.Configuration | Where-Object { $_.Name -eq $configurationName })[0]
    if (-not $configuration) {
        throw "Missing Release|Win32 configuration in $ProjectPath"
    }

    $outputDirRaw = $configuration.OutputDirectory
    $intDirRaw = $configuration.IntermediateDirectory

    if ($script:StefxBuildTarget -eq "spmp") {
        if ($ProjectPath -eq "code\x_game\x_game.vcproj") {
            $outputDirRaw = ".\..\x_exe\Release\spmp"
            $intDirRaw = ".\..\x_exe\Release\spmp\game"
        }
        elseif ($ProjectPath -eq "code\x_exe\x_exe.vcproj") {
            $outputDirRaw = ".\Release\spmp"
            $intDirRaw = ".\Release\spmp\exe"
        }
    }

    $macros = @{
        SolutionDir        = "$projectDir\"
        ProjectDir         = "$projectDir\"
        ProjectPath        = $fullProjectPath
        ProjectName        = $projectName
        ConfigurationName  = "Release"
        OutDir             = $outputDirRaw
        IntDir             = $intDirRaw
    }

    $outputDir = Resolve-ProjectPath -BaseDir $projectDir -PathValue (Expand-VcString -Value $outputDirRaw -Macros $macros)
    $intDir = Resolve-ProjectPath -BaseDir $projectDir -PathValue (Expand-VcString -Value $intDirRaw -Macros $macros)
    $macros.OutDir = $outputDir
    $macros.IntDir = $intDir

    if ($Clean) {
        if (Test-Path $intDir) {
            Remove-Item -LiteralPath $intDir -Recurse -Force
        }
        if (Test-Path $outputDir) {
            Remove-Item -LiteralPath $outputDir -Recurse -Force
        }
        return
    }

    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    New-Item -ItemType Directory -Path $intDir -Force | Out-Null

    $compilerTool = @($configuration.Tool | Where-Object { $_.Name -eq "VCCLCompilerTool" })[0]
    $libTool = @($configuration.Tool | Where-Object { $_.Name -eq "VCLibrarianTool" })[0]
    $linkTool = @($configuration.Tool | Where-Object { $_.Name -eq "VCLinkerTool" })[0]

    if (-not $compilerTool -and $ProjectPath -eq "code\x_exe\x_exe.vcproj") {
        $compilerTool = [pscustomobject]@{
            # Plan-B (OpenJKDF2 1:1): match build_xbox.bat line 48 include order
            #   $repoRoot\code\win32       ??? our local shims still take precedence
            #   C:\XDK_5558\XDK\xbox\include ??? primary (matches OpenJKDF2 XDK_ROOT\include)
            #   C:\XDK\xbox\include        ??? 5849 fallback for headers 5558 lacks
            #   C:\XDK\include
            #   C:\XDK\bink_stub
            AdditionalIncludeDirectories = "C:\XDK_5558\XDK\xbox\include;$repoRoot\code\win32;C:\XDK\xbox\include;C:\XDK\include;C:\XDK\bink_stub"
            # Plan-B: dropped _USE_XGMATH so d3dx8math.h (and ID3DXMatrixStack)
            # are pulled in properly.  xgmath.h's #ifndef __XGMATH_H__ at the
            # top of d3dx8math.h causes that header to skip its body if
            # __XGMATH_H__ is defined first, leaving ID3DXMatrixStack
            # undeclared.  Without _USE_XGMATH, xgmath.h's D3DX-compat
            # block (line 976+) is inactive ??? D3DXMATRIX is the real
            # d3dx8math.h class, ID3DXMatrixStack is declared.
            PreprocessorDefinitions = "NDEBUG;_XBOX;_JK2EXE;WIN32;VV_LIGHTING;STEFX_ELITE_FORCE_SP;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;_XBOX_VC71_MIGRATION"
            # Plan-B audit: REVERTED /O2 ??? /Ox.  The /O2-match-OpenJKDF2
            # attempt regressed the build ??? wglCreateContext no longer
            # completed on CXBX-R LLE GPU (hardware test 2026-05-17 both
            # with D24S8 and D16 depth formats).  Last known good
            # baseline (which reached SP_DoLicense / SDT:glEndFrame)
            # used /Ox; reverting to that.  The OpenJKDF2 /O2 match was
            # cosmetic ??? VC7.1 /Ox is a superset (/O2 + /Ob2 /Oi /Ot /Oy
            # /Gs already set) so the divergence was minor.
            Optimization = "3"
            InlineFunctionExpansion = "2"
            EnableIntrinsicFunctions = "true"
            FavorSizeOrSpeed = "1"
            OmitFramePointers = "true"
            StringPooling = "true"
            RuntimeLibrary = "0"
            BufferSecurityCheck = "false"
            EnableFunctionLevelLinking = "true"
            WarningLevel = "2"
            DebugInformationFormat = "3"
        }
        $linkTool = [pscustomobject]@{
            AdditionalOptions = "/FIXED:NO /IGNORE:4254"
            # Plan-B (OpenJKDF2 1:1 alignment): adopt OpenJKDF2's actual
            # build_xbox.bat link list verbatim where physically possible.
            # OpenJKDF2 links: d3d8.lib d3dx8.lib dsound.lib xboxkrnl.lib
            # xgraphics.lib xonline.lib libc.lib xapilib.lib ??? all from
            # C:\XDK_5558\XDK\xbox\lib (their exact XDK 5558 install path).
            #
            # JKA additions on top of OpenJKDF2's list:
            #   x_game.lib, goblib.lib ??? JKA-specific intermediate libs
            #   dmusic.lib            ??? JKA uses DirectMusic for ingame music
            #
            # libcmt.lib ??? libc.lib: matches OpenJKDF2's choice.
            # Add d3dx8.lib (OpenJKDF2 links it; replaces our local
            # d3dx8_compat.cpp shim, which we'll remove from the source
            # list below).
            # d3d8.lib path: switch from the local xbox source repo to
            # OpenJKDF2's exact XDK 5558 install path.
            # Plan-B (OpenJKDF2 1:1 verified divergence): bare lib names,
            # ALL resolved from XDK 5558 via the /LIBPATH below.  The
            # previous build's absolute paths for d3d8/d3dx8 worked but
            # left xboxkrnl/xgraphics/xapilib/etc. resolving to XDK 5849
            # ??? fakegl's Present hung because xboxkrnl 5849's push-buffer
            # sync ABI differs from XDK 5558's expectations (fakegl was
            # compiled against 5558 includes).  OpenJKDF2's build uses
            # /LIBPATH:%XDK_ROOT%\lib with XDK_ROOT=C:\XDK_5558\XDK\xbox,
            # so ALL their libs are 5558.  Matching exactly here.
            AdditionalDependencies = "d3d8.lib;d3dx8.lib;dsound.lib;xboxkrnl.lib;xgraphics.lib;xonline.lib;libc.lib;xapilib.lib;dmusic.lib;x_game.lib"
            OutputFile = "$repoReleaseDir\default.exe"
            # XDK 5558 lib path FIRST so xboxkrnl, xgraphics, xapilib,
            # xonline, dsound, libc all resolve from 5558 (matching
            # OpenJKDF2).  5849 paths kept as fallback for dmusic.lib
            # and any other lib 5558 doesn't have.
            AdditionalLibraryDirectories = "$repoReleaseDir;.\Release;C:\XDK_5558\XDK\xbox\lib;C:\XDK\xbox\lib;C:\XDK\lib;C:\Programming\GitHub\xbox\private\ui\Xdemo\XDemos\XDemos\Bink;C:\Programming\GitHub\RM4+JadeSrc\Libraries\GX8\bink"
            IgnoreDefaultLibraryNames = "msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib"
            GenerateDebugInformation = "true"
            ProgramDatabaseFile = "$repoReleaseDir\x_exe.pdb"
            SubSystem = "2"
            EntryPointSymbol = "WinMainCRTStartup"
            SetChecksum = "true"
        }
    }

    if (-not $compilerTool -and $ProjectPath -eq "codemp\x_exe\x_exe.vcproj") {
        $compilerTool = [pscustomobject]@{
            AdditionalIncludeDirectories = "C:\XDK_5558\XDK\xbox\include;$repoRoot\code\win32;C:\XDK\xbox\include;C:\XDK\include"
            PreprocessorDefinitions = "_WIN32;NDEBUG;WIN32;_JK2;_JK2MP;_XBOX;VV_LIGHTING;STEFX_ELITE_FORCE_MP;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;_XBOX_VC71_MIGRATION"
            AdditionalOptions = "/Oy-"
            Optimization = "2"
            InlineFunctionExpansion = "2"
            EnableIntrinsicFunctions = "true"
            FavorSizeOrSpeed = "1"
            OmitFramePointers = "false"
            StringPooling = "true"
            RuntimeLibrary = "0"
            BufferSecurityCheck = "false"
            EnableFunctionLevelLinking = "true"
            WarningLevel = "3"
            DebugInformationFormat = "3"
        }
        $linkTool = [pscustomobject]@{
            AdditionalOptions = "/FORCE:MULTIPLE /FIXED:NO"
            AdditionalDependencies = "xapilib.lib;libc.lib;d3d8.lib;d3dx8.lib;xgraphics.lib;dsound.lib;dmusic.lib;xboxkrnl.lib;goblib.lib;xvoice.lib;xonlines.lib;.\Release\goblib.lib;.\Release\x_jk2cgame.lib;.\Release\x_ui.lib;.\Release\x_botlib.lib;.\Release\x_jk2game.lib"
            OutputFile = ".\Release\efmp.exe"
            AdditionalLibraryDirectories = ".\Release;C:\XDK_5558\XDK\xbox\lib;C:\XDK\xbox\lib;C:\XDK\lib"
            IgnoreDefaultLibraryNames = "msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib"
            GenerateDebugInformation = "true"
            ProgramDatabaseFile = '.\Release\x_exe.pdb'
            SubSystem = "2"
            EntryPointSymbol = "WinMainCRTStartup"
            SetChecksum = "true"
        }
    }

    if ($script:StefxBuildTarget -eq "spmp" -and
        ($ProjectPath -eq "code\x_game\x_game.vcproj" -or
         $ProjectPath -eq "code\x_exe\x_exe.vcproj")) {
        $compilerDefinitions = Get-XmlAttr -Node $compilerTool -Name "PreprocessorDefinitions"
        if ([string]::IsNullOrWhiteSpace($compilerDefinitions)) {
            $compilerDefinitions = "STEFX_SP_HOSTED_MP"
        }
        elseif ($compilerDefinitions -notmatch "(^|;)STEFX_SP_HOSTED_MP(;|$)") {
            $compilerDefinitions = "$compilerDefinitions;STEFX_SP_HOSTED_MP"
        }
        $compilerTool.PreprocessorDefinitions = $compilerDefinitions
    }

    if ($script:StefxBuildTarget -eq "spmp" -and
        $ProjectPath -eq "code\x_game\x_game.vcproj") {
        $libTool.OutputFile = ".\..\x_exe\Release\spmp\x_game.lib"
    }

    if ($script:StefxBuildTarget -eq "spmp" -and
        $ProjectPath -eq "code\x_exe\x_exe.vcproj") {
        $linkTool.OutputFile = "$repoReleaseDir\efmp.exe"
            $linkTool.AdditionalLibraryDirectories = ".\Release\spmp;$repoReleaseDir;.\Release;C:\XDK_5558\XDK\xbox\lib;C:\XDK\xbox\lib;C:\XDK\lib;C:\Programming\GitHub\xbox\private\ui\Xdemo\XDemos\XDemos\Bink;C:\Programming\GitHub\RM4+JadeSrc\Libraries\GX8\bink"
    }

    $baseFlags = New-Object System.Collections.Generic.List[string]
    foreach ($flag in (Convert-CompilerFlags -Tool $compilerTool)) {
        $baseFlags.Add($flag)
    }
    foreach ($includeDir in (Split-VcList (Expand-VcString -Value (Get-XmlAttr -Node $compilerTool -Name "AdditionalIncludeDirectories") -Macros $macros))) {
        $baseFlags.Add('/I')
        $baseFlags.Add((Resolve-ProjectPath -BaseDir $projectDir -PathValue $includeDir))
    }
    foreach ($define in (Split-VcList (Expand-VcString -Value (Get-XmlAttr -Node $compilerTool -Name "PreprocessorDefinitions") -Macros $macros))) {
        $baseFlags.Add("/D$define")
    }

    $sources = Get-ProjectSourceFiles -Xml $xml -ConfigurationName $configurationName -ProjectDir $projectDir -Macros $macros
    $sources = Apply-ProjectSourceOverrides -ProjectPath $ProjectPath -Sources $sources
    if ($script:StefxBuildTarget -eq "spmp") {
        foreach ($source in $sources) {
            $sourceFullPath = [System.IO.Path]::GetFullPath($source.FullPath)
            if ($sourceFullPath.IndexOf("\codemp\", [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "SP-hosted Holomatch build may not compile codemp sources: $sourceFullPath"
            }
        }
    }
    $objectFiles = New-Object System.Collections.Generic.List[string]

    foreach ($source in $sources) {
        $sourceHandled = $false
        switch ($source.Extension) {
            ".vsh" {
                Invoke-External -Exe $xsasmExe -Arguments @($source.FullPath) -WorkingDirectory $projectDir
                $sourceHandled = $true
                break
            }
            ".psh" {
                Invoke-External -Exe $xsasmExe -Arguments @($source.FullPath) -WorkingDirectory $projectDir
                $sourceHandled = $true
                break
            }
            ".asm" {
                $objPath = Get-ObjectPath -IntDir $intDir -SourcePath $source.FullPath
                New-Item -ItemType Directory -Path (Split-Path -Parent $objPath) -Force | Out-Null
                Invoke-External -Exe $mlExe -Arguments @("/c", "/Cx", "/coff", "/Zi", "/Fo$objPath", $source.FullPath) -WorkingDirectory $projectDir
                $objectFiles.Add($objPath)
                $sourceHandled = $true
                break
            }
        }

        if ($sourceHandled) {
            continue
        }

        $objPath = Get-ObjectPath -IntDir $intDir -SourcePath $source.FullPath
        New-Item -ItemType Directory -Path (Split-Path -Parent $objPath) -Force | Out-Null

        $compileFlags = New-Object System.Collections.Generic.List[string]
        $prependIncludes = Get-XmlAttr -Node $source.Tool -Name "PrependIncludeDirectories"
        if (-not [string]::IsNullOrWhiteSpace($prependIncludes)) {
            foreach ($includeDir in (Split-VcList (Expand-VcString -Value $prependIncludes -Macros $macros))) {
                $compileFlags.Add('/I')
                $compileFlags.Add((Resolve-ProjectPath -BaseDir $projectDir -PathValue $includeDir))
            }
        }

        foreach ($flag in $baseFlags) {
            $compileFlags.Add($flag)
        }

        $seenDefines = @{}
        foreach ($flag in $compileFlags) {
            if ($flag.Length -gt 2 -and $flag.Substring(0, 2) -ieq "/D") {
                $seenDefines[$flag.Substring(2).ToLowerInvariant()] = $true
            }
        }

        $sourceDefs = Get-XmlAttr -Node $source.Tool -Name "PreprocessorDefinitions"
        if (-not [string]::IsNullOrWhiteSpace($sourceDefs)) {
            foreach ($define in (Split-VcList (Expand-VcString -Value $sourceDefs -Macros $macros))) {
                $defineKey = $define.ToLowerInvariant()
                if (-not $seenDefines.ContainsKey($defineKey)) {
                    $compileFlags.Add("/D$define")
                    $seenDefines[$defineKey] = $true
                }
            }
        }

        $sourceIncludes = Get-XmlAttr -Node $source.Tool -Name "AdditionalIncludeDirectories"
        if (-not [string]::IsNullOrWhiteSpace($sourceIncludes)) {
            foreach ($includeDir in (Split-VcList (Expand-VcString -Value $sourceIncludes -Macros $macros))) {
                $compileFlags.Add('/I')
                $compileFlags.Add((Resolve-ProjectPath -BaseDir $projectDir -PathValue $includeDir))
            }
        }

        $sourceAdditionalOptions = Get-XmlAttr -Node $source.Tool -Name "AdditionalOptions"
        if (-not [string]::IsNullOrWhiteSpace($sourceAdditionalOptions)) {
            # Split on whitespace BUT keep tokens that contain quoted paths (/FI"foo bar.h") together
            # Simple approach: split, then re-join consecutive tokens whose quote count is unbalanced
            $rawTokens = $sourceAdditionalOptions -split '\s+' | Where-Object { $_ }
            $merged = New-Object System.Collections.Generic.List[string]
            $accum = ""
            $openQuote = $false
            foreach ($tok in $rawTokens) {
                $quoteCount = @($tok.ToCharArray() | Where-Object { $_ -eq '"' }).Count
                if ($openQuote) {
                    $accum = $accum + ' ' + $tok
                    if (($quoteCount % 2) -eq 1) { $merged.Add($accum); $accum = ""; $openQuote = $false }
                } else {
                    if (($quoteCount % 2) -eq 1) { $accum = $tok; $openQuote = $true }
                    else { $merged.Add($tok) }
                }
            }
            if ($openQuote) { $merged.Add($accum) }
            foreach ($opt in $merged) { $compileFlags.Add($opt) }
        }

        $compileAs = Get-XmlAttr -Node $source.Tool -Name "CompileAs"
        if ($compileAs -eq "1") {
            $sourceArgument = "/Tc$($source.FullPath)"
        }
        elseif ($compileAs -eq "2") {
            $sourceArgument = "/Tp$($source.FullPath)"
        }
        else {
            if ($source.Extension -eq ".c") {
                $sourceArgument = "/Tc$($source.FullPath)"
            }
            else {
                $sourceArgument = "/Tp$($source.FullPath)"
            }
        }

        $compileFlags.Add("/Fo$objPath")
        $compileFlags.Add($sourceArgument)

        Invoke-External -Exe $clExe -Arguments $compileFlags -WorkingDirectory $projectDir
        $objectFiles.Add($objPath)
    }

    if ($script:StefxBuildTarget -eq "spmp" -and
        $ProjectPath -eq "code\x_exe\x_exe.vcproj") {
        # Force the complete official EF cgame family into the executable so
        # no similarly named SP archive member can satisfy part of the VM.
        $hmOfficialCgameObjects = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\holomatch\official\cgame"
        if (Test-Path $hmOfficialCgameObjects) {
            foreach ($hmOfficialCgameObject in (Get-ChildItem -Path $hmOfficialCgameObjects -Filter *.obj | Sort-Object Name)) {
                $objectFiles.Add($hmOfficialCgameObject.FullName)
            }
        }

        $hmGameObject = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\game\stefx_holomatch_game.obj"
        if (Test-Path $hmGameObject) {
            $objectFiles.Add($hmGameObject)
        }

        $hmApiObject = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\holomatch\stefx_mp_game_api.obj"
        if (Test-Path $hmApiObject) {
            $objectFiles.Add($hmApiObject)
        }

        $hmBotBridgeObject = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\holomatch\stefx_holomatch_bot_bridge.obj"
        if (Test-Path $hmBotBridgeObject) {
            $objectFiles.Add($hmBotBridgeObject)
        }

        # The XDK linker does not reliably extract transitive dependencies from
        # the mixed SP/EF static archive. Link the official EF game objects
        # explicitly so the VM entry point and every game-side dependency are
        # present in the SP-hosted MP image.
        $hmOfficialGameObjects = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\holomatch\official\game"
        if (Test-Path $hmOfficialGameObjects) {
            foreach ($hmOfficialGameObject in (Get-ChildItem -Path $hmOfficialGameObjects -Filter *.obj | Sort-Object Name)) {
                $objectFiles.Add($hmOfficialGameObject.FullName)
            }
        }

        # Pull the complete engine-side bot library into efmp.xbe. Keeping these
        # objects explicit avoids archive extraction differences in the XDK
        # linker and guarantees that no codemp library can satisfy the symbols.
        $hmBotlibObjects = Join-Path $repoRoot "code\x_exe\Release\spmp\game\code\holomatch\botlib"
        if (Test-Path $hmBotlibObjects) {
            foreach ($hmBotlibObject in (Get-ChildItem -Path $hmBotlibObjects -Filter *.obj | Sort-Object Name)) {
                $objectFiles.Add($hmBotlibObject.FullName)
            }
        }
    }

    if ($script:StefxBuildTarget -eq "spmp") {
        foreach ($objectFile in $objectFiles) {
            $objectFullPath = [System.IO.Path]::GetFullPath($objectFile)
            if ($objectFullPath.IndexOf("\codemp\", [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "SP-hosted Holomatch build may not link codemp objects: $objectFullPath"
            }
        }
    }

    if ($configuration.ConfigurationType -eq "4") {
        $outputFile = Resolve-ProjectPath -BaseDir $projectDir -PathValue (Expand-VcString -Value (Get-XmlAttr -Node $libTool -Name "OutputFile") -Macros $macros)
        New-Item -ItemType Directory -Path (Split-Path -Parent $outputFile) -Force | Out-Null
        Invoke-External -Exe $libExe -Arguments (@("/nologo", "/OUT:$outputFile") + $objectFiles) -WorkingDirectory $projectDir
        return
    }

    if ($configuration.ConfigurationType -eq "1" -or ($configuration.ConfigurationType -eq "0" -and $linkTool)) {
        $outputFile = Resolve-ProjectPath -BaseDir $projectDir -PathValue (Expand-VcString -Value (Get-XmlAttr -Node $linkTool -Name "OutputFile") -Macros $macros)
        $pdbPath = Resolve-ProjectPath -BaseDir $projectDir -PathValue (Expand-VcString -Value (Get-XmlAttr -Node $linkTool -Name "ProgramDatabaseFile") -Macros $macros)
        New-Item -ItemType Directory -Path (Split-Path -Parent $outputFile) -Force | Out-Null
        New-Item -ItemType Directory -Path (Split-Path -Parent $pdbPath) -Force | Out-Null
        $linkArgs = New-Object System.Collections.Generic.List[string]
        $linkArgs.Add("/NOLOGO")

        $linkAdditionalOptions = Get-XmlAttr -Node $linkTool -Name "AdditionalOptions"
        if (-not [string]::IsNullOrWhiteSpace($linkAdditionalOptions)) {
            foreach ($opt in ($linkAdditionalOptions -split '\s+' | Where-Object { $_ })) {
                $linkArgs.Add($opt)
            }
        }

        foreach ($libDir in (Split-VcList (Expand-VcString -Value (Get-XmlAttr -Node $linkTool -Name "AdditionalLibraryDirectories") -Macros $macros))) {
            $linkArgs.Add("/LIBPATH:$((Resolve-ProjectPath -BaseDir $projectDir -PathValue $libDir))")
        }

        foreach ($ignoreLib in (Split-VcList (Expand-VcString -Value (Get-XmlAttr -Node $linkTool -Name "IgnoreDefaultLibraryNames") -Macros $macros))) {
            $linkArgs.Add("/NODEFAULTLIB:$ignoreLib")
        }

        if ((Get-XmlAttr -Node $linkTool -Name "GenerateDebugInformation") -eq "true") {
            $linkArgs.Add("/DEBUG")
            $linkArgs.Add("/PDB:$pdbPath")
        }

        $mapPath = [System.IO.Path]::ChangeExtension($outputFile, ".map")
        $linkArgs.Add("/MAP:$mapPath")

        if ((Get-XmlAttr -Node $linkTool -Name "SubSystem") -eq "2") {
            $linkArgs.Add("/SUBSYSTEM:WINDOWS")
        }

        $entryPoint = Get-XmlAttr -Node $linkTool -Name "EntryPointSymbol"
        if ($entryPoint) {
            $linkArgs.Add("/ENTRY:$entryPoint")
        }

        if ((Get-XmlAttr -Node $linkTool -Name "SetChecksum") -eq "true") {
            $linkArgs.Add("/RELEASE")
        }

        $linkArgs.Add("/OUT:$outputFile")
        foreach ($obj in $objectFiles) {
            $linkArgs.Add($obj)
        }
        foreach ($dep in (Split-VcList (Expand-VcString -Value (Get-XmlAttr -Node $linkTool -Name "AdditionalDependencies") -Macros $macros))) {
            $linkArgs.Add($dep)
        }

        Invoke-External -Exe $linkExe -Arguments $linkArgs -WorkingDirectory $projectDir

        if ($projectName -eq "x_exe") {
            $xbeFile = [System.IO.Path]::ChangeExtension($outputFile, ".xbe")
            $patchScript = Join-Path $projectDir "patchxbe.py"
            Invoke-External -Exe $pythonExe -Arguments @($patchScript, $outputFile, $xbeFile) -WorkingDirectory $projectDir
        }
        return
    }

    throw "Unsupported ConfigurationType $($configuration.ConfigurationType) for $ProjectPath"
}

function Invoke-BuildGraph {
    param([string[]]$Projects)

    foreach ($project in $Projects) {
        Write-Host ""
        Write-Host "==> $project"
        Build-Project -ProjectPath $project
    }
}

function Update-ConsoleFileList {
    param(
        [string]$Directory,
        [string]$Extension,
        [string[]]$AdditionalFiles = @()
    )

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return
    }

    $normalizedExtension = $Extension.TrimStart(".")
    if ([string]::IsNullOrWhiteSpace($normalizedExtension)) {
        return
    }

    $files = @(Get-ChildItem -LiteralPath $Directory -File -Filter "*.$normalizedExtension" |
        Sort-Object Name |
        ForEach-Object { $_.Name })

    foreach ($additionalFile in $AdditionalFiles) {
        if ([string]::IsNullOrWhiteSpace($additionalFile)) {
            continue
        }
        $additionalName = [System.IO.Path]::GetFileName($additionalFile)
        if (-not $files.Contains($additionalName)) {
            $files += $additionalName
        }
    }
    $files = @($files | Sort-Object -Unique)

    if (-not $files -or $files.Count -eq 0) {
        return
    }

    $listFile = Join-Path $Directory "_console_${normalizedExtension}_list_"
    $contents = ($files -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText($listFile, $contents, [System.Text.Encoding]::ASCII)
    Write-Host "Updated console file list: $listFile ($($files.Count) files)"
}

function Ensure-EFGobWriterType {
    if ("EFGobWriter" -as [type]) {
        return
    }

    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class EFGobWriter
{
    const int BlockSize = 64 * 1024;
    const int BlockAlignment = 2048;
    const uint GobMagic = 0x8008u;
    const uint GobEndOfChain = 32767u;
    static readonly uint[] CrcTable = CreateCrcTable();

    sealed class BlockEntry
    {
        public uint Size;
        public uint Offset;
        public uint Next;
        public uint Adler;
    }

    sealed class FileEntry
    {
        public string ArchiveName;
        public uint Hash;
        public uint Size;
        public uint FirstBlock;
        public uint Crc;
        public uint Time;
    }

    static uint[] CreateCrcTable()
    {
        uint[] table = new uint[256];
        for (uint i = 0; i < table.Length; ++i)
        {
            uint c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = ((c & 1u) != 0u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        return table;
    }

    static uint Crc32(byte[] data)
    {
        uint c = 0xffffffffu;
        for (int i = 0; i < data.Length; ++i)
        {
            c = CrcTable[(c ^ data[i]) & 0xffu] ^ (c >> 8);
        }
        return c ^ 0xffffffffu;
    }

    static uint Crc32Ascii(string text)
    {
        return Crc32(Encoding.ASCII.GetBytes(text));
    }

    static uint Adler32(byte[] data)
    {
        const uint ModAdler = 65521u;
        uint a = 1u;
        uint b = 0u;
        for (int i = 0; i < data.Length; ++i)
        {
            a = (a + data[i]) % ModAdler;
            b = (b + a) % ModAdler;
        }
        return (b << 16) | a;
    }

    static void WriteBE(BinaryWriter writer, uint value)
    {
        writer.Write((byte)((value >> 24) & 0xffu));
        writer.Write((byte)((value >> 16) & 0xffu));
        writer.Write((byte)((value >> 8) & 0xffu));
        writer.Write((byte)(value & 0xffu));
    }

    static void WriteFixedAscii(BinaryWriter writer, string text, int size)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(text);
        if (bytes.Length >= size)
        {
            throw new InvalidOperationException("GOB path is too long: " + text);
        }

        writer.Write(bytes);
        for (int i = bytes.Length; i < size; ++i)
        {
            writer.Write((byte)0);
        }
    }

    static string ToArchiveName(string relativePath)
    {
        return ".\\" + relativePath.Replace('/', '\\').ToLowerInvariant();
    }

    static uint Slack(uint size)
    {
        uint remainder = size % BlockAlignment;
        return remainder == 0u ? 0u : (BlockAlignment - remainder);
    }

    public static void Write(string baseDir, string[] relativePaths)
    {
        string gobPath = Path.Combine(baseDir, "assets.gob");
        string gfcPath = Path.Combine(baseDir, "assets.gfc");
        List<BlockEntry> blocks = new List<BlockEntry>();
        List<FileEntry> files = new List<FileEntry>();
        uint archiveSize = 0u;

        using (FileStream stream = new FileStream(gobPath, FileMode.Create, FileAccess.Write, FileShare.Read))
        using (BinaryWriter writer = new BinaryWriter(stream))
        {
            for (int fileIndex = 0; fileIndex < relativePaths.Length; ++fileIndex)
            {
                string relativePath = relativePaths[fileIndex];
                string sourcePath = Path.Combine(baseDir, relativePath);
                byte[] data = File.ReadAllBytes(sourcePath);
                string archiveName = ToArchiveName(relativePath);

                FileEntry file = new FileEntry();
                file.ArchiveName = archiveName;
                file.Hash = Crc32Ascii(archiveName);
                file.Size = (uint)data.Length;
                file.FirstBlock = (uint)blocks.Count;
                file.Crc = Crc32(data);
                file.Time = 0u;

                for (int i = 0; i < files.Count; ++i)
                {
                    if (files[i].Hash == file.Hash)
                    {
                        throw new InvalidOperationException("Duplicate GOB hash for " + archiveName);
                    }
                }

                for (int pos = 0; pos < data.Length; pos += BlockSize)
                {
                    int chunk = Math.Min(BlockSize, data.Length - pos);
                    byte[] wrapped = new byte[chunk + 9];
                    wrapped[0] = (byte)'S';
                    wrapped[1] = (byte)'T';
                    wrapped[2] = (byte)'B';
                    wrapped[3] = (byte)'L';
                    wrapped[4] = (byte)'0';
                    Buffer.BlockCopy(data, pos, wrapped, 5, chunk);
                    wrapped[5 + chunk] = (byte)'E';
                    wrapped[6 + chunk] = (byte)'N';
                    wrapped[7 + chunk] = (byte)'B';
                    wrapped[8 + chunk] = (byte)'L';

                    BlockEntry block = new BlockEntry();
                    block.Size = (uint)wrapped.Length;
                    block.Offset = archiveSize;
                    block.Next = (pos + chunk < data.Length) ? (uint)(blocks.Count + 1) : GobEndOfChain;
                    block.Adler = Adler32(wrapped);
                    blocks.Add(block);

                    writer.Write(wrapped);
                    uint slack = Slack(block.Size);
                    if (slack != 0u)
                    {
                        writer.Write(new byte[slack]);
                    }
                    archiveSize += block.Size + slack;
                }

                files.Add(file);
            }
        }

        using (FileStream stream = new FileStream(gfcPath, FileMode.Create, FileAccess.Write, FileShare.Read))
        using (BinaryWriter writer = new BinaryWriter(stream))
        {
            WriteBE(writer, GobMagic);
            WriteBE(writer, archiveSize);
            WriteBE(writer, (uint)blocks.Count);
            WriteBE(writer, (uint)files.Count);

            for (int i = 0; i < blocks.Count; ++i)
            {
                WriteBE(writer, blocks[i].Size);
                WriteBE(writer, blocks[i].Offset);
                WriteBE(writer, blocks[i].Next);
            }

            for (int i = 0; i < blocks.Count; ++i)
            {
                WriteBE(writer, blocks[i].Adler);
            }

            for (int i = 0; i < files.Count; ++i)
            {
                WriteBE(writer, files[i].Hash);
                WriteBE(writer, files[i].Size);
                WriteBE(writer, files[i].FirstBlock);
            }

            for (int i = 0; i < files.Count; ++i)
            {
                WriteFixedAscii(writer, files[i].ArchiveName, 96);
                WriteBE(writer, files[i].Crc);
                WriteBE(writer, files[i].Time);
            }
        }
    }
}
"@
}

function Update-EFModelGob {
    param(
        [string]$BaseEfDir,
        [string[]]$ModelPaths
    )

    $existing = @()
    foreach ($modelPath in $ModelPaths) {
        $source = Join-Path $BaseEfDir $modelPath
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $existing += $modelPath
        } else {
            Write-Warning "Missing EF GOB model source: $source"
        }
    }

    $gobPath = Join-Path $BaseEfDir "assets.gob"
    $gfcPath = Join-Path $BaseEfDir "assets.gfc"
    if ($existing.Count -eq 0) {
        Remove-Item -LiteralPath $gobPath,$gfcPath -Force -ErrorAction SilentlyContinue
        return
    }

    Ensure-EFGobWriterType
    [EFGobWriter]::Write($BaseEfDir, [string[]]$existing)
    $gobItem = Get-Item -LiteralPath $gobPath
    Write-Host "Updated EF model GOB: $gobPath ($($existing.Count) files, $($gobItem.Length) bytes)"
}

function Copy-EFDataOverlay {
    param(
        [string]$BaseEfDir,
        [switch]$SkipUiScripts
    )

    $sourceExtData = Join-Path $repoRoot "SP-Mod-Source-Code-master\BaseEF\ext_data"
    $destExtData = Join-Path $BaseEfDir "ext_data"
    $sourceUi = Join-Path $repoRoot "base\ui"
    $destUi = Join-Path $BaseEfDir "ui"
    $sourceMenu = Join-Path $repoRoot "base\menu"
    $destMenu = Join-Path $BaseEfDir "menu"

    if (-not (Test-Path -LiteralPath $sourceExtData -PathType Container)) {
        Write-Warning "Missing EF ext_data source: $sourceExtData"
    } else {
        New-Item -ItemType Directory -Path $destExtData -Force | Out-Null
        foreach ($fileName in @("addon.npc", "boltOns.cfg", "infostrings.dat", "items.dat", "NPCs.cfg", "weapons.dat")) {
            $source = Join-Path $sourceExtData $fileName
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                Write-Warning "Missing EF ext_data file: $source"
                continue
            }

            Copy-Item -LiteralPath $source -Destination (Join-Path $destExtData $fileName) -Force
            Write-Host "Updated EF ext_data overlay: $fileName"
        }
    }

    if ($SkipUiScripts) {
        $removedUiScripts = 0
        if (Test-Path -LiteralPath $destUi -PathType Container) {
            Get-ChildItem -LiteralPath $destUi -Recurse -File | Where-Object {
                $_.Extension -ieq ".txt" -or $_.Extension -ieq ".menu"
            } | ForEach-Object {
                Remove-Item -LiteralPath $_.FullName -Force
                $removedUiScripts++
            }

            $staleJampDir = Join-Path $destUi "jamp"
            if (Test-Path -LiteralPath $staleJampDir) {
                Remove-Item -LiteralPath $staleJampDir -Recurse -Force
            }
        }

        Write-Host "Skipped EF UI script overlay for Holomatch MP; removed staged UI scripts: $removedUiScripts"
    } elseif (-not (Test-Path -LiteralPath $sourceUi -PathType Container)) {
        Write-Warning "Missing EF UI script source: $sourceUi"
    } else {
        $sourceUiFull = (Resolve-Path -LiteralPath $sourceUi).Path
        $copiedUiScripts = 0
        $removedDeprecatedMpUiScripts = 0
        foreach ($stalePath in @(
            (Join-Path $destUi "jahud.txt"),
            (Join-Path $destUi "jampmenus.txt"),
            (Join-Path $destUi "jampingame.txt"),
            (Join-Path $destUi "testhud.menu"),
            (Join-Path $destUi "jamp")
        )) {
            if (Test-Path -LiteralPath $stalePath) {
                Remove-Item -LiteralPath $stalePath -Recurse -Force
                $removedDeprecatedMpUiScripts++
            }
        }

        Get-ChildItem -LiteralPath $sourceUi -Recurse -File | Where-Object {
            $relative = $_.FullName.Substring($sourceUiFull.Length).TrimStart('\', '/')
            $relativeNormalized = $relative.Replace('\', '/').ToLowerInvariant()
            ($_.Extension -ieq ".txt" -or $_.Extension -ieq ".menu") -and
                $_.Name -ine "vssver.scc" -and
                $relativeNormalized -notin @("jahud.txt", "jampmenus.txt", "jampingame.txt", "testhud.menu") -and
                -not $relativeNormalized.StartsWith("jamp/")
        } | ForEach-Object {
            $relative = $_.FullName.Substring($sourceUiFull.Length).TrimStart('\', '/')
            $destination = Join-Path $destUi $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
            $copiedUiScripts++
        }

        Write-Host "Updated EF UI script overlay: $copiedUiScripts files"
        if ($removedDeprecatedMpUiScripts -gt 0) {
            Write-Host "Removed deprecated MP UI overlay paths: $removedDeprecatedMpUiScripts"
        }
    }

    if ($SkipUiScripts) {
        $removedMenuAssets = 0
        if ((Test-Path -LiteralPath $sourceMenu -PathType Container) -and
            (Test-Path -LiteralPath $destMenu -PathType Container)) {
            $sourceMenuFull = (Resolve-Path -LiteralPath $sourceMenu).Path
            Get-ChildItem -LiteralPath $sourceMenu -Recurse -File | Where-Object {
                $_.Extension -iin @(".tga", ".jpg", ".jpeg", ".png") -and $_.Name -ine "vssver.scc"
            } | ForEach-Object {
                $relative = $_.FullName.Substring($sourceMenuFull.Length).TrimStart('\', '/')
                $destination = Join-Path $destMenu $relative
                if (Test-Path -LiteralPath $destination -PathType Leaf) {
                    Remove-Item -LiteralPath $destination -Force
                    $removedMenuAssets++
                }
            }
        }

        Write-Host "Skipped repository base menu overlay for Holomatch MP; removed stale assets: $removedMenuAssets"
    } elseif (Test-Path -LiteralPath $sourceMenu -PathType Container) {
        $sourceMenuFull = (Resolve-Path -LiteralPath $sourceMenu).Path
        $copiedMenuAssets = 0
        Get-ChildItem -LiteralPath $sourceMenu -Recurse -File | Where-Object {
            $_.Extension -iin @(".tga", ".jpg", ".jpeg", ".png") -and $_.Name -ine "vssver.scc"
        } | ForEach-Object {
            $relative = $_.FullName.Substring($sourceMenuFull.Length).TrimStart('\', '/')
            $destination = Join-Path $destMenu $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
            $copiedMenuAssets++
        }

        Write-Host "Updated EF menu asset overlay: $copiedMenuAssets files"
    }
}

function Copy-EFConfigOverlay {
    param(
        [string]$BaseEfDir
    )

    $destDefaultCfg = Join-Path $BaseEfDir "default.cfg"

    if (Test-Path -LiteralPath $destDefaultCfg -PathType Leaf) {
        Write-Host "Preserved EF config overlay without modification: default.cfg"
        return
    }

    Write-Warning "EF config overlay missing: $destDefaultCfg"
}

function Get-EFRelativeFiles {
    param(
        [string]$BaseEfDir,
        [string[]]$Directories
    )

    $files = @()
    foreach ($directory in $Directories) {
        $fullDirectory = Join-Path $BaseEfDir $directory
        if (-not (Test-Path -LiteralPath $fullDirectory -PathType Container)) {
            Write-Warning "Missing EF GOB directory: $fullDirectory"
            continue
        }

        $files += Get-ChildItem -LiteralPath $fullDirectory -Recurse -File |
            Sort-Object FullName |
            ForEach-Object { $_.FullName.Substring($BaseEfDir.Length + 1) }
    }

    return $files
}

function Remove-EFLegacyGobArtifacts {
    param(
        [string]$BaseEfDir
    )

    foreach ($fileName in @("assets.gob", "assets.gfc")) {
        $path = Join-Path $BaseEfDir $fileName
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
            Write-Host "Removed legacy EF GOB artifact: $path"
        }
    }

    Write-Host "Elite Force assets use loose files and PK3 archives; no GOB generated."
}

function Update-EFXboxPatchPk3 {
    param(
        [string]$BaseEfDir,
        [string]$OutputName = "xbox0.pk3",
        [string]$Map = "borg1",
        [ValidateSet("map", "campaign", "multiplayer", "all")]
        [string]$BspMaps = "campaign",
        [switch]$DdsOnly,
        [ValidateSet("dxt5", "bgra32")]
        [string]$AlphaTextureFormat = "bgra32",
        [switch]$SkipUiScripts,
        [switch]$HolomatchSupportAssets
    )

    $patchScript = Join-Path $repoRoot "scripts\build_xbox_patch_pk3.py"
    if (-not (Test-Path -LiteralPath $patchScript -PathType Leaf)) {
        Write-Warning "Missing EF Xbox patch PK3 builder: $patchScript"
        return
    }

    $outputPk3 = Join-Path $BaseEfDir $OutputName
    $patchArgs = @(
        $patchScript,
        "--base-dir", $BaseEfDir,
        "--output", $outputPk3,
        "--map", $Map,
        "--texture-mode", "all",
        "--bsp-mode", "optimized-lightmaps",
        "--bsp-maps", $BspMaps,
        "--lightmap-boost", "2.5",
        "--max-texture-size", "128",
        "--max-player-texture-size", "64",
        "--max-hud-texture-size", "128",
        "--max-loadscreen-texture-size", "512",
        "--alpha-texture-format", $AlphaTextureFormat
    )

    if ($DdsOnly) {
        $patchArgs += "--dds-only"
    }
    if ($SkipUiScripts) {
        $patchArgs += "--no-ui-scripts"
    }
    if ($HolomatchSupportAssets) {
        $patchArgs += "--holomatch-support-assets"
    }

    Invoke-External -Exe $pythonExe -Arguments $patchArgs -WorkingDirectory $repoRoot
}

function Expand-EFHolomatchPk3SourceOverlay {
    param(
        [string]$BaseEfDir
    )

    $extractScript = Join-Path $repoRoot "scripts\extract_mp_pk3_source_overlay.py"
    if (-not (Test-Path -LiteralPath $extractScript -PathType Leaf)) {
        throw "Missing official MP PK3 source overlay extractor: $extractScript"
    }

    Invoke-External -Exe $pythonExe -Arguments @(
        $extractScript,
        "--base-dir", $BaseEfDir
    ) -WorkingDirectory $repoRoot
}

function Assert-EFHolomatchUiMandate {
    param(
        [string]$Pk3Path,
        [string]$StageBaseEfPath,
        [string]$XbePath,
        [switch]$AllowStageOriginalImages,
        [switch]$CodeOnly
    )

    $checkScript = Join-Path $repoRoot "scripts\check_mp_holomatch_ui.py"
    if (-not (Test-Path -LiteralPath $checkScript -PathType Leaf)) {
        throw "Missing Holomatch UI mandate checker: $checkScript"
    }

    $checkArgs = @(
        $checkScript,
        "--repo-root",
        $repoRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($Pk3Path)) {
        $checkArgs += @("--pk3", $Pk3Path)
    }
    if (-not [string]::IsNullOrWhiteSpace($StageBaseEfPath)) {
        $checkArgs += @("--stage-baseef", $StageBaseEfPath)
    }
    if ($AllowStageOriginalImages) {
        $checkArgs += "--allow-stage-original-images"
    }
    if ($AllowStageOriginalImages -and $CodeOnly) {
        $checkArgs += "--allow-stage-map-overrides"
    }
    if (-not [string]::IsNullOrWhiteSpace($XbePath)) {
        $checkArgs += @("--xbe", $XbePath)
    }
    if ($CodeOnly) {
        $checkArgs += "--code-only"
    }
    $checkArgs += @("--direct-map", $script:StefxHolomatchDirectMap)

    Invoke-External -Exe $pythonExe -Arguments $checkArgs -WorkingDirectory $repoRoot
}

function Update-EFXboxSoundBank {
    param(
        [string]$BaseEfDir
    )

    $soundBankScript = Join-Path $repoRoot "scripts\build_xbox_soundbank.py"
    if (-not (Test-Path -LiteralPath $soundBankScript -PathType Leaf)) {
        Write-Warning "Missing EF Xbox soundbank builder: $soundBankScript"
        return
    }

    Invoke-External -Exe $pythonExe -Arguments @(
        $soundBankScript,
        "--base-dir", $BaseEfDir,
        "--encoding", "xbadpcm",
        "--encoder", "C:\XDK\xbox\bin\xbadpcmencode.exe"
    ) -WorkingDirectory $repoRoot
}

function Update-EFXboxAudioAssets {
    param(
        [string]$BaseEfDir
    )

    $audioScript = Join-Path $repoRoot "scripts\build_xbox_audio_assets.py"
    if (-not (Test-Path -LiteralPath $audioScript -PathType Leaf)) {
        Write-Warning "Missing EF Xbox audio asset builder: $audioScript"
        return
    }

    Invoke-External -Exe $pythonExe -Arguments @(
        $audioScript,
        "--base-dir", $BaseEfDir,
        "--all-sound",
        "--encoder", "C:\XDK\xbox\bin\xbadpcmencode.exe"
    ) -WorkingDirectory $repoRoot
}

function Update-EFConsoleAssetLists {
    $baseEfDir = Join-Path $repoReleaseDir "BaseEF"
    Copy-EFDataOverlay -BaseEfDir $baseEfDir
    Copy-EFConfigOverlay -BaseEfDir $baseEfDir
    Remove-EFLegacyGobArtifacts -BaseEfDir $baseEfDir
    Update-EFXboxPatchPk3 -BaseEfDir $baseEfDir
    Update-EFXboxAudioAssets -BaseEfDir $baseEfDir
    Update-EFXboxSoundBank -BaseEfDir $baseEfDir
    Update-ConsoleFileList -Directory (Join-Path $baseEfDir "scripts") -Extension ".shader"

    $fileCodeCache = Join-Path $repoReleaseDir "xbx_filelist"
    if (Test-Path -LiteralPath $fileCodeCache -PathType Leaf) {
        try {
            Remove-Item -LiteralPath $fileCodeCache -Force -ErrorAction Stop
            Write-Host "Removed stale filecode cache: $fileCodeCache"
        } catch {
            Write-Warning "Could not remove stale filecode cache ${fileCodeCache}: $($_.Exception.Message)"
        }
    }
}

function Update-EFHolomatchAssetLists {
    $baseEfDir = Join-Path $repoReleaseDir "BaseEF"
    if ($script:StefxBuildTarget -eq "spmp") {
        $sourceXbe = Join-Path $repoRoot "build\release\efmp.xbe"
    }
    else {
        $sourceXbe = Join-Path $repoRoot "codemp\x_exe\Release\efmp.xbe"
    }
    Copy-EFDataOverlay -BaseEfDir $baseEfDir -SkipUiScripts
    Copy-EFConfigOverlay -BaseEfDir $baseEfDir
    Remove-EFLegacyGobArtifacts -BaseEfDir $baseEfDir
    Expand-EFHolomatchPk3SourceOverlay -BaseEfDir $baseEfDir
    Update-EFXboxPatchPk3 -BaseEfDir $baseEfDir -OutputName "xbox1.pk3" -Map $script:StefxHolomatchDirectMap -BspMaps "multiplayer" -DdsOnly -AlphaTextureFormat "bgra32" -SkipUiScripts -HolomatchSupportAssets
    Update-EFXboxAudioAssets -BaseEfDir $baseEfDir
    Update-EFXboxSoundBank -BaseEfDir $baseEfDir
    Update-ConsoleFileList -Directory (Join-Path $baseEfDir "scripts") -Extension ".shader" -AdditionalFiles @("xbox_borg_fix.shader")
    if ($script:StefxBuildTarget -eq "spmp") {
        Assert-EFHolomatchUiMandate -Pk3Path (Join-Path $baseEfDir "xbox1.pk3") -StageBaseEfPath $baseEfDir -AllowStageOriginalImages -XbePath $sourceXbe -CodeOnly
    } else {
        Assert-EFHolomatchUiMandate -Pk3Path (Join-Path $baseEfDir "xbox1.pk3") -StageBaseEfPath $baseEfDir -AllowStageOriginalImages -XbePath $sourceXbe
    }
}

function Remove-EFHolomatchLooseOverrides {
    param(
        [string]$StageBaseEf,
        [string]$Pk3Path
    )

    if (-not (Test-Path -LiteralPath $StageBaseEf -PathType Container)) {
        return
    }

    $removed = 0
    $scriptsDir = Join-Path $StageBaseEf "scripts"
    if (Test-Path -LiteralPath $scriptsDir -PathType Container) {
        $shaderFiles = Get-ChildItem -LiteralPath $scriptsDir -File -Filter "*.shader" -ErrorAction SilentlyContinue
        foreach ($shaderFile in $shaderFiles) {
            Remove-Item -LiteralPath $shaderFile.FullName -Force
            $removed++
        }

        $shaderList = Join-Path $scriptsDir "_console_shader_list_"
        if (Test-Path -LiteralPath $shaderList -PathType Leaf) {
            Remove-Item -LiteralPath $shaderList -Force
            $removed++
        }
    }

    if (-not (Test-Path -LiteralPath $Pk3Path -PathType Leaf)) {
        throw "Cannot remove Holomatch map overrides; missing package: $Pk3Path"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $mapsDir = Join-Path $StageBaseEf "maps"
    if (Test-Path -LiteralPath $mapsDir -PathType Container) {
        $looseMapFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Pk3Path)
        try {
            foreach ($entry in $zip.Entries) {
                $entryName = $entry.FullName.Replace('\', '/')
                if ($entryName -match '^maps/([^/]+)\.aas$') {
                    [void]$looseMapFiles.Add("$($Matches[1]).aas")
                }
                elseif ($entryName -match '^maps/xbox/([^/]+)\.bsp$') {
                    [void]$looseMapFiles.Add("$($Matches[1]).bsp")
                }
            }
        }
        finally {
            $zip.Dispose()
        }

        foreach ($mapFileName in $looseMapFiles) {
            $mapFile = Join-Path $mapsDir $mapFileName
            if (Test-Path -LiteralPath $mapFile -PathType Leaf) {
                Remove-Item -LiteralPath $mapFile -Force
                $removed++
            }
        }
    }

    if ($removed -gt 0) {
        Write-Host "Removed $removed stale Holomatch loose override file(s) from CXBX-R stage."
    }
}

function Remove-EFHolomatchLooseTextureFallbacks {
    param(
        [string]$StageBaseEf,
        [string]$Pk3Path
    )

    if (-not (Test-Path -LiteralPath $StageBaseEf -PathType Container)) {
        return
    }
    if (-not (Test-Path -LiteralPath $Pk3Path -PathType Leaf)) {
        throw "Cannot remove Holomatch texture fallbacks; missing package: $Pk3Path"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $stageRoot = (Resolve-Path -LiteralPath $StageBaseEf).Path
    $stageRootWithSlash = $stageRoot.TrimEnd('\') + '\'
    $pk3Resolved = (Resolve-Path -LiteralPath $Pk3Path).Path
    $imageExts = @(".tga", ".jpg", ".jpeg", ".png")
    $fallbacks = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

    $zip = [System.IO.Compression.ZipFile]::OpenRead($pk3Resolved)
    try {
        foreach ($entry in $zip.Entries) {
            $entryName = $entry.FullName.Replace('\', '/')
            if (-not $entryName.EndsWith(".dds", [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $withoutExt = $entryName.Substring(0, $entryName.Length - 4)
            foreach ($ext in $imageExts) {
                [void]$fallbacks.Add($withoutExt + $ext)
            }
        }
    } finally {
        $zip.Dispose()
    }

    $removed = 0
    $remaining = @()
    foreach ($relative in $fallbacks) {
        $nativeRelative = $relative.Replace('/', '\')
        $target = Join-Path $stageRoot $nativeRelative
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            continue
        }

        $targetFull = [System.IO.Path]::GetFullPath($target)
        if (-not $targetFull.StartsWith($stageRootWithSlash, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove Holomatch texture fallback outside stage: $targetFull"
        }

        Remove-Item -LiteralPath $targetFull -Force
        $removed++

        if (Test-Path -LiteralPath $targetFull -PathType Leaf) {
            $remaining += $relative
        }
    }

    if ($remaining.Count -gt 0) {
        throw "Staged BaseEF still contains loose original texture fallback(s): $($remaining[0..([Math]::Min($remaining.Count, 16) - 1)] -join ', ')"
    }

    if ($removed -gt 0) {
        Write-Host "Removed $removed loose original texture fallback(s) covered by xbox1.pk3 DDS entries."
    } else {
        Write-Host "No loose original texture fallbacks found for xbox1.pk3 DDS entries."
    }
}

function Copy-EFHolomatchCxbxStage {
    $stageRoot = "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X"
    if (-not (Test-Path -LiteralPath $stageRoot -PathType Container)) {
        Write-Host "Skipping CXBX-R Holomatch staging; folder not found: $stageRoot"
        return
    }

    $stageBaseEf = Join-Path $stageRoot "BaseEF"
    New-Item -ItemType Directory -Path $stageBaseEf -Force | Out-Null

    if ($script:StefxBuildTarget -eq "spmp") {
        $sourceXbe = Join-Path $repoRoot "build\release\efmp.xbe"
    }
    else {
        $sourceXbe = Join-Path $repoRoot "codemp\x_exe\Release\efmp.xbe"
    }
    $sourcePk3 = Join-Path $repoReleaseDir "BaseEF\xbox1.pk3"
    $sourceSoundBankDir = Join-Path $repoReleaseDir "BaseEF\soundbank"

    if (-not (Test-Path -LiteralPath $sourceXbe -PathType Leaf)) {
        throw "Cannot stage EF Holomatch MP XBE; missing: $sourceXbe"
    }
    if (-not (Test-Path -LiteralPath $sourcePk3 -PathType Leaf)) {
        throw "Cannot stage EF Holomatch MP package; missing: $sourcePk3"
    }
    foreach ($soundBankFile in @("sound.bnk", "sound.tbl", "soundbank_manifest.json")) {
        $sourceSoundBankFile = Join-Path $sourceSoundBankDir $soundBankFile
        if (-not (Test-Path -LiteralPath $sourceSoundBankFile -PathType Leaf)) {
            throw "Cannot stage EF Holomatch SP soundbank; missing: $sourceSoundBankFile"
        }
    }

    Remove-EFHolomatchLooseOverrides -StageBaseEf $stageBaseEf -Pk3Path $sourcePk3
    Copy-Item -LiteralPath $sourceXbe -Destination (Join-Path $stageRoot "efmp.xbe") -Force
    $stagedPk3 = Join-Path $stageBaseEf "xbox1.pk3"
    Copy-Item -LiteralPath $sourcePk3 -Destination $stagedPk3 -Force
    $stagedSoundBankDir = Join-Path $stageBaseEf "soundbank"
    New-Item -ItemType Directory -Path $stagedSoundBankDir -Force | Out-Null
    foreach ($soundBankFile in @("sound.bnk", "sound.tbl", "soundbank_manifest.json")) {
        Copy-Item -LiteralPath (Join-Path $sourceSoundBankDir $soundBankFile) -Destination (Join-Path $stagedSoundBankDir $soundBankFile) -Force
    }
    Remove-EFHolomatchLooseTextureFallbacks -StageBaseEf $stageBaseEf -Pk3Path $stagedPk3
    if ($script:StefxBuildTarget -eq "spmp") {
        Assert-EFHolomatchUiMandate -Pk3Path $stagedPk3 -StageBaseEfPath $stageBaseEf -XbePath (Join-Path $stageRoot "efmp.xbe") -CodeOnly
    } else {
        Assert-EFHolomatchUiMandate -Pk3Path $stagedPk3 -StageBaseEfPath $stageBaseEf -XbePath (Join-Path $stageRoot "efmp.xbe")
    }
    Write-Host "Staged EF Holomatch MP for CXBX-R: efmp.xbe, BaseEF\xbox1.pk3, and the SP soundbank"
    Write-Host "SP/co-op default.xbe was not touched."
}

$spProjects = @(
    "code\x_game\x_game.vcproj",
    "code\x_exe\x_exe.vcproj"
)

$mpProjects = @(
    "codemp\goblib\goblib.vcproj",
    "codemp\x_botlib\x_botlib.vcproj",
    "codemp\x_jk2game\x_jk2game.vcproj",
    "codemp\x_jk2cgame\x_jk2cgame.vcproj",
    "codemp\x_ui\x_ui.vcproj",
    "codemp\x_exe\x_exe.vcproj"
)

switch ($Target) {
    "sp"  {
        Invoke-BuildGraph -Projects $spProjects
        if (-not $SkipAssets) {
            Update-EFConsoleAssetLists
        } else {
            Write-Host "Skipping EF asset packaging/copy phase."
        }
    }
    "mp"  {
        Invoke-BuildGraph -Projects $mpProjects
        if (-not $SkipAssets) {
            Update-EFHolomatchAssetLists
            if (-not $SkipStage) {
                Copy-EFHolomatchCxbxStage
            } else {
                Write-Host "Skipping CXBX-R Holomatch staging."
            }
        } else {
            Write-Host "Skipping EF MP asset packaging/copy phase."
        }
    }
    "spmp" {
        Invoke-BuildGraph -Projects $spProjects
        if (-not $SkipAssets) {
            Update-EFHolomatchAssetLists
            if (-not $SkipStage) {
                Copy-EFHolomatchCxbxStage
            } else {
                Write-Host "Skipping CXBX-R Holomatch staging."
            }
        } else {
            Write-Host "Skipping EF SP-hosted Holomatch asset packaging/copy phase."
        }
    }
    "all" {
        Invoke-BuildGraph -Projects $spProjects
        if (-not $SkipAssets) {
            Update-EFConsoleAssetLists
        } else {
            Write-Host "Skipping EF asset packaging/copy phase."
        }
        Invoke-BuildGraph -Projects $mpProjects
        if (-not $SkipAssets) {
            Update-EFHolomatchAssetLists
            if (-not $SkipStage) {
                Copy-EFHolomatchCxbxStage
            } else {
                Write-Host "Skipping CXBX-R Holomatch staging."
            }
        } else {
            Write-Host "Skipping EF MP asset packaging/copy phase."
        }
    }
}

