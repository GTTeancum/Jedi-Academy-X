param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("sp", "mp", "all")]
    [string]$Target,

    [switch]$Clean,

    [switch]$SkipAssets
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
        $efRoot = Join-Path $repoRoot "SP-Mod-Source-Code-master"
        $efGameDsp = Join-Path $efRoot "game\game.dsp"
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
                if ($fileName -ieq "bg_lib.cpp" -or
                    $fileName -ieq "q_math.cpp" -or
                    $fileName -ieq "q_shared.cpp") {
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
                        PreprocessorDefinitions   = "STEFX_ELITE_FORCE_SP;_X86_"
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
                        PreprocessorDefinitions   = "STEFX_ELITE_FORCE_SP;_X86_"
                    }
                })
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

        return $filtered
    }

    if ($ProjectPath -eq "codemp\x_exe\x_exe.vcproj") {
        $hasAsmStub = $false
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
            AdditionalIncludeDirectories = "C:\XDK_5558\XDK\xbox\include;C:\XDK\xbox\include;C:\XDK\include"
            PreprocessorDefinitions = "_WIN32;NDEBUG;WIN32;_JK2;_JK2MP;_XBOX;VV_LIGHTING;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;_XBOX_VC71_MIGRATION"
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
            OutputFile = ".\Release\jamp.exe"
            AdditionalLibraryDirectories = ".\Release;C:\XDK_5558\XDK\xbox\lib;C:\XDK\xbox\lib;C:\XDK\lib"
            IgnoreDefaultLibraryNames = "msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib"
            GenerateDebugInformation = "true"
            ProgramDatabaseFile = '.\Release\x_exe.pdb'
            SubSystem = "2"
            EntryPointSymbol = "WinMainCRTStartup"
            SetChecksum = "true"
        }
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
        [string]$Extension
    )

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return
    }

    $normalizedExtension = $Extension.TrimStart(".")
    if ([string]::IsNullOrWhiteSpace($normalizedExtension)) {
        return
    }

    $files = Get-ChildItem -LiteralPath $Directory -File -Filter "*.$normalizedExtension" |
        Sort-Object Name |
        ForEach-Object { $_.Name }

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
        [string]$BaseEfDir
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

    if (-not (Test-Path -LiteralPath $sourceUi -PathType Container)) {
        Write-Warning "Missing EF UI script source: $sourceUi"
    } else {
        $sourceUiFull = (Resolve-Path -LiteralPath $sourceUi).Path
        $copiedUiScripts = 0
        Get-ChildItem -LiteralPath $sourceUi -Recurse -File | Where-Object {
            ($_.Extension -ieq ".txt" -or $_.Extension -ieq ".menu") -and $_.Name -ine "vssver.scc"
        } | ForEach-Object {
            $relative = $_.FullName.Substring($sourceUiFull.Length).TrimStart('\', '/')
            $destination = Join-Path $destUi $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
            $copiedUiScripts++
        }

        Write-Host "Updated EF UI script overlay: $copiedUiScripts files"
    }

    if (Test-Path -LiteralPath $sourceMenu -PathType Container) {
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
        [string]$BaseEfDir
    )

    $patchScript = Join-Path $repoRoot "scripts\build_xbox_patch_pk3.py"
    if (-not (Test-Path -LiteralPath $patchScript -PathType Leaf)) {
        Write-Warning "Missing EF Xbox patch PK3 builder: $patchScript"
        return
    }

    $outputPk3 = Join-Path $BaseEfDir "xbox0.pk3"
    Invoke-External -Exe $pythonExe -Arguments @(
        $patchScript,
        "--base-dir", $BaseEfDir,
        "--output", $outputPk3,
        "--map", "borg1",
        "--texture-mode", "all",
        "--bsp-mode", "optimized-lightmaps",
        "--bsp-maps", "campaign",
        "--lightmap-boost", "2.5",
        "--max-texture-size", "128",
        "--max-player-texture-size", "64",
        "--max-hud-texture-size", "128",
        "--max-loadscreen-texture-size", "512"
    ) -WorkingDirectory $repoRoot
}

function Update-EFBorgShaderScript {
    param(
        [string]$BaseEfDir
    )

    $borgShader = Join-Path $BaseEfDir "scripts\borg.shader"
    if (-not (Test-Path -LiteralPath $borgShader -PathType Leaf)) {
        Write-Warning "Missing Borg shader script for Xbox patching: $borgShader"
        return
    }

    $text = Get-Content -LiteralPath $borgShader -Raw
    $patched = $text.
        Replace("textures/borg/static.tga", "textures/borg/static").
        Replace("textures/borg/static2.tga", "textures/borg/static2").
        Replace("textures/borg/static_yellow.tga", "textures/borg/static_yellow").
        Replace("textures/borg/static.jpg", "textures/borg/static").
        Replace("textures/borg/static2.jpg", "textures/borg/static2").
        Replace("textures/borg/static_yellow.jpg", "textures/borg/static_yellow").
        Replace("textures/borg/static.dds", "textures/borg/static").
        Replace("textures/borg/static2.dds", "textures/borg/static2").
        Replace("textures/borg/static_yellow.dds", "textures/borg/static_yellow")

    if ($patched -ne $text) {
        Set-Content -LiteralPath $borgShader -Value $patched -Encoding ASCII
        Write-Host "Patched Borg static shader image references to extensionless staged assets: $borgShader"
    }
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
    Update-EFBorgShaderScript -BaseEfDir $baseEfDir
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
    "mp"  { Invoke-BuildGraph -Projects $mpProjects }
    "all" {
        Invoke-BuildGraph -Projects $spProjects
        if (-not $SkipAssets) {
            Update-EFConsoleAssetLists
        } else {
            Write-Host "Skipping EF asset packaging/copy phase."
        }
        Invoke-BuildGraph -Projects $mpProjects
    }
}
