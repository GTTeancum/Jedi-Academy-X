param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("sp", "mp", "all")]
    [string]$Target,

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
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

$vcIncludeDirs = @(
    (Join-Path $repoRoot "code\win32"),
    "C:\XDK\xbox\include",
    "C:\XDK\include"
) | Where-Object { Test-Path $_ }

$vcLibDirs = @(
    "C:\XDK\xbox\lib",
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

    return ($Value -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
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

    if ($ProjectPath -eq "code\x_exe\x_exe.vcproj") {
        $filtered = New-Object System.Collections.Generic.List[object]
        foreach ($source in $Sources) {
            if ($source.RelativePath -ieq "..\client\cl_cin_console.cpp") {
                continue
            }
            if ($source.RelativePath -ieq "..\win32\dbg_console_xbox.cpp") {
                continue
            }
            $filtered.Add($source)
        }

        $filtered.Add([pscustomobject]@{
            RelativePath = "..\client\cl_cin_console_stub.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\client\cl_cin_console_stub.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\d3dx8_compat.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\d3dx8_compat.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        $filtered.Add([pscustomobject]@{
            RelativePath = "..\win32\dbg_console_xbox_stub.cpp"
            FullPath     = Resolve-ProjectPath -BaseDir $repoRoot -PathValue "code\win32\dbg_console_xbox_stub.cpp"
            Extension    = ".cpp"
            Tool         = $null
        })

        return $filtered
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
    try {
        & $Exe @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Exe failed with exit code $LASTEXITCODE"
        }
    }
    finally {
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
            AdditionalIncludeDirectories = "$repoRoot\code\win32;C:\XDK\xbox\include;C:\XDK\include;C:\XDK\bink_stub"
            PreprocessorDefinitions = "NDEBUG;_XBOX;_JK2EXE;WIN32;VV_LIGHTING;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;_XBOX_VC71_MIGRATION;_USE_XGMATH"
            Optimization = "3"
            InlineFunctionExpansion = "2"
            EnableIntrinsicFunctions = "true"
            FavorSizeOrSpeed = "1"
            OmitFramePointers = "true"
            StringPooling = "true"
            RuntimeLibrary = "0"
            BufferSecurityCheck = "false"
            EnableFunctionLevelLinking = "true"
            WarningLevel = "3"
            DebugInformationFormat = "3"
        }
        $linkTool = [pscustomobject]@{
            AdditionalOptions = "/FORCE:MULTIPLE /FIXED:NO"
            AdditionalDependencies = "xapilib.lib;libc.lib;d3d8-xbox.lib;xgraphics.lib;dsound.lib;dmusic.lib;xboxkrnl.lib;x_game.lib;goblib.lib;xonline.lib"
            OutputFile = ".\Release\default.exe"
            AdditionalLibraryDirectories = ".\Release;C:\XDK\lib"
            IgnoreDefaultLibraryNames = "msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib"
            GenerateDebugInformation = "true"
            ProgramDatabaseFile = '.\Release\x_exe.pdb'
            SubSystem = "2"
            EntryPointSymbol = "WinMainCRTStartup"
            SetChecksum = "true"
        }
    }

    if (-not $compilerTool -and $ProjectPath -eq "codemp\x_exe\x_exe.vcproj") {
        $compilerTool = [pscustomobject]@{
            AdditionalIncludeDirectories = "$repoRoot\code\win32;C:\XDK\xbox\include;C:\XDK\include"
            PreprocessorDefinitions = "_WIN32;NDEBUG;WIN32;_JK2;_JK2MP;_XBOX;VV_LIGHTING;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;_XBOX_VC71_MIGRATION;_USE_XGMATH"
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
            AdditionalDependencies = "xapilib.lib;libc.lib;d3d8i.lib;xgraphics.lib;dsound.lib;dmusic.lib;xboxkrnl.lib;goblib.lib;xvoice.lib;xbdm.lib;xonlines.lib"
            OutputFile = ".\Release\jamp.exe"
            AdditionalLibraryDirectories = ".\Release;C:\XDK\lib"
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
        foreach ($flag in $baseFlags) {
            $compileFlags.Add($flag)
        }

        $sourceDefs = Get-XmlAttr -Node $source.Tool -Name "PreprocessorDefinitions"
        if (-not [string]::IsNullOrWhiteSpace($sourceDefs)) {
            foreach ($define in (Split-VcList (Expand-VcString -Value $sourceDefs -Macros $macros))) {
                $compileFlags.Add("/D$define")
            }
        }

        $sourceIncludes = Get-XmlAttr -Node $source.Tool -Name "AdditionalIncludeDirectories"
        if (-not [string]::IsNullOrWhiteSpace($sourceIncludes)) {
            foreach ($includeDir in (Split-VcList (Expand-VcString -Value $sourceIncludes -Macros $macros))) {
                $compileFlags.Add('/I')
                $compileFlags.Add((Resolve-ProjectPath -BaseDir $projectDir -PathValue $includeDir))
            }
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

$spProjects = @(
    "code\goblib\goblib.vcproj",
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
    "sp"  { Invoke-BuildGraph -Projects $spProjects }
    "mp"  { Invoke-BuildGraph -Projects $mpProjects }
    "all" {
        Invoke-BuildGraph -Projects $spProjects
        Invoke-BuildGraph -Projects $mpProjects
    }
}
