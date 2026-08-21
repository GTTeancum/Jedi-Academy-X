#!/usr/bin/env python3
"""Source-level contracts for the Xbox build orchestrator.

These checks are intentionally lightweight.  They do not compile the Xbox
projects; they protect the target-selection behavior that decides which XBE
personalities a real build will produce.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


RETAIL_RENDERER_REQUIRED_DEFINES = (
    "FINAL_BUILD",
    "_FINAL",
    "STEFX_ELITE_FORCE_SP",
    "STEFX_RETAIL_RENDERER_ACTIVE",
    "STEFX_RETAIL_SURFACE_ACTIVE",
)
RETAIL_RENDERER_REPLACEMENTS = (
    r"..\renderer\tr_backend.cpp",
    r"..\renderer\tr_cmds.cpp",
    r"..\renderer\tr_light.cpp",
    r"..\renderer\tr_main.cpp",
    r"..\renderer\tr_scene.cpp",
    r"..\renderer\tr_shade.cpp",
    r"..\renderer\tr_shade_calc.cpp",
    r"..\renderer\tr_shader.cpp",
    r"..\renderer\tr_sky.cpp",
    r"..\renderer\tr_world.cpp",
)


def fail(message: str) -> None:
    raise AssertionError(message)


def normalized_source(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore").replace("\r\n", "\n").replace("\r", "\n")


def extract_braced_block(source: str, start_index: int) -> str:
    brace_index = source.find("{", start_index)
    if brace_index < 0:
        fail("could not find opening brace")

    depth = 0
    in_single = False
    in_double = False
    index = brace_index
    while index < len(source):
        char = source[index]
        previous = source[index - 1] if index > 0 else ""
        if char == "'" and not in_double:
            in_single = not in_single
        elif char == '"' and not in_single and previous != "`":
            in_double = not in_double
        elif not in_single and not in_double:
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[brace_index + 1 : index]
        index += 1

    fail("could not find closing brace")


def extract_switch_case(source: str, label: str) -> str:
    switch_match = re.search(r"(?m)^\s*switch\s*\(\s*\$Target\s*\)\s*\{", source)
    if not switch_match:
        fail("missing switch ($Target) block")
    switch_body = extract_braced_block(source, switch_match.start())
    case_match = re.search(r'(?m)^\s*"' + re.escape(label) + r'"\s*\{', switch_body)
    if not case_match:
        fail(f"missing target case: {label}")
    return extract_braced_block(switch_body, case_match.start())


def extract_function(source: str, name: str) -> str:
    function_match = re.search(r"(?m)^function\s+" + re.escape(name) + r"\s*\{", source)
    if not function_match:
        fail(f"missing {name} helper")
    return extract_braced_block(source, function_match.start())


def require_in_order(text: str, fragments: list[str], context: str) -> None:
    cursor = 0
    for fragment in fragments:
        found = text.find(fragment, cursor)
        if found < 0:
            fail(f"{context} is missing required ordered fragment: {fragment}")
        cursor = found + len(fragment)


def verify_retail_renderer_build_contract(source: str) -> None:
    for define in RETAIL_RENDERER_REQUIRED_DEFINES:
        if define not in source:
            fail(f"retail renderer build contract missing define: {define}")
    for replacement in RETAIL_RENDERER_REPLACEMENTS:
        if replacement not in source:
            fail(f"retail renderer build contract missing legacy replacement: {replacement}")
    require_in_order(
        source,
        [
            "$retailRendererDefinitions =",
            "$retailRendererReplacements = @(",
            "if ($retailRendererReplacements -icontains $source.RelativePath)",
            "continue",
            "PreprocessorDefinitions = $retailRendererDefinitions",
        ],
        "retail renderer build contract",
    )


def verify_build_script(path: Path) -> dict[str, object]:
    source = normalized_source(path)
    verify_retail_renderer_build_contract(source)
    wrapper_match = re.search(r"(?m)^function\s+Invoke-BuildGraphForTarget\s*\{", source)
    if not wrapper_match:
        fail("missing Invoke-BuildGraphForTarget helper")
    wrapper_body = extract_braced_block(source, wrapper_match.start())
    require_in_order(
        wrapper_body,
        [
            "$previousBuildTarget = $script:StefxBuildTarget",
            "$script:StefxBuildTarget = $BuildTarget",
            "Invoke-BuildGraph -Projects $Projects",
            "$script:StefxBuildTarget = $previousBuildTarget",
        ],
        "Invoke-BuildGraphForTarget",
    )

    sp_body = extract_switch_case(source, "sp")
    mp_body = extract_switch_case(source, "mp")
    spmp_body = extract_switch_case(source, "spmp")
    all_body = extract_switch_case(source, "all")
    build_project_body = extract_function(source, "Build-Project")
    active_xbe_postcondition_body = extract_function(source, "Assert-ActiveReleaseXbes")
    active_package_postcondition_body = extract_function(source, "Assert-ActiveReleasePackages")

    require_in_order(
        active_xbe_postcondition_body,
        [
            "$failures = New-Object System.Collections.Generic.List[string]",
            "Assert-ActiveReleaseXbeRuntimeBuildIds -XbePaths $XbePaths",
            "catch",
            "Assert-ActiveReleaseXbeFreshness -XbePaths $XbePaths",
            "catch",
            "Active release XBE postcondition failed",
        ],
        "Assert-ActiveReleaseXbes",
    )
    require_in_order(
        active_package_postcondition_body,
        [
            "$failures = New-Object System.Collections.Generic.List[string]",
            "Assert-ActiveReleasePackageFreshness -PackagePaths $PackagePaths",
            "catch",
            "Active release package postcondition failed",
        ],
        "Assert-ActiveReleasePackages",
    )
    require_in_order(
        build_project_body,
        [
            "$forceCompileForFreshRuntimeId = (",
            'GetFileName($source.FullPath).Equals("xb_log.cpp"',
            'StartsWith((Join-Path $repoRoot "code\\win32")',
            "-not $forceCompileForFreshRuntimeId",
            "Rebuilding $objPath for fresh STEFX_RUNTIME_BUILD_ID",
            "Invoke-External -Exe $clExe -Arguments $compileFlags -WorkingDirectory $projectDir",
        ],
        "Build-Project runtime build ID freshness",
    )

    require_in_order(
        spmp_body,
        [
            'Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects',
            'Invoke-BuildGraphForTarget -BuildTarget "spmp" -Projects $spProjects',
            'Assert-ActiveReleaseXbes @(',
            'Join-Path $repoReleaseDir "default.xbe"',
            'Join-Path $repoReleaseDir "efmp.xbe"',
            "Update-EFConsoleAssetLists",
            "Update-EFHolomatchAssetLists",
            "Assert-ActiveReleasePackages @(",
            'Join-Path $repoReleaseDir "BaseEF\\xbox0.pk3"',
            'Join-Path $repoReleaseDir "BaseEF\\xbox1.pk3"',
        ],
        "spmp target",
    )
    if spmp_body.find('-BuildTarget "sp"') > spmp_body.find('-BuildTarget "spmp"'):
        fail("spmp target must refresh default.xbe before building efmp.xbe")
    if "Invoke-BuildGraph -Projects $spProjects" in spmp_body:
        fail("spmp target must not invoke the SP graph without an explicit target scope")

    require_in_order(
        sp_body,
        [
            'Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects',
            'Join-Path $repoReleaseDir "default.xbe"',
            "Update-EFConsoleAssetLists",
            'Join-Path $repoReleaseDir "BaseEF\\xbox0.pk3"',
        ],
        "sp target",
    )
    require_in_order(
        mp_body,
        ['Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects'],
        "mp target",
    )
    require_in_order(
        all_body,
        [
            'Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects',
            'Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects',
        ],
        "all target",
    )

    return {
        "buildScript": str(path),
        "contracts": {
            "targetScopedBuildGraph": True,
            "spmpRefreshesDefaultThenEfmp": True,
            "spmpAssertsBothActiveXbes": True,
            "spmpRefreshesSpAndHmPackages": True,
            "spmpAssertsBothActivePackages": True,
            "spAssertsActivePackage": True,
            "activeReleaseXbePostconditionsAggregate": True,
            "activeReleasePackagePostconditionsAggregate": True,
            "runtimeBuildIdObjectAlwaysRebuilt": True,
            "retailRendererAbiReplacesLegacyFramePath": True,
        },
    }


def run_self_test() -> int:
    valid = r"""
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
"""
    invalid = valid.replace(
        'Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects\n        Invoke-BuildGraphForTarget -BuildTarget "spmp" -Projects $spProjects',
        'Invoke-BuildGraph -Projects $spProjects',
    )
    invalid_assets = valid.replace(
        "        Update-EFConsoleAssetLists\n        Update-EFHolomatchAssetLists",
        "        Update-EFHolomatchAssetLists",
    )
    invalid_package_assert = valid.replace(
        """        Assert-ActiveReleasePackages @(
            (Join-Path $repoReleaseDir "BaseEF\\xbox0.pk3"),
            (Join-Path $repoReleaseDir "BaseEF\\xbox1.pk3")
        )
""",
        "",
    )
    invalid_xbe_postcondition = valid.replace(
        """function Assert-ActiveReleaseXbes {
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
""",
        """function Assert-ActiveReleaseXbes {
    Assert-ActiveReleaseXbeRuntimeBuildIds -XbePaths $XbePaths
    Assert-ActiveReleaseXbeFreshness -XbePaths $XbePaths
}
""",
    )
    invalid_runtime_id_reuse = valid.replace(
        "        -not $forceCompileForFreshRuntimeId -and\n",
        "",
    )
    invalid_retail_replacement = valid.replace(
        '        "..\\renderer\\tr_backend.cpp",\n',
        "",
    )

    import tempfile

    with tempfile.TemporaryDirectory(prefix="stefx_build_contracts_") as tmp_name:
        tmp = Path(tmp_name)
        valid_path = tmp / "valid.ps1"
        invalid_path = tmp / "invalid.ps1"
        invalid_assets_path = tmp / "invalid_assets.ps1"
        invalid_package_assert_path = tmp / "invalid_package_assert.ps1"
        invalid_xbe_postcondition_path = tmp / "invalid_xbe_postcondition.ps1"
        invalid_runtime_id_reuse_path = tmp / "invalid_runtime_id_reuse.ps1"
        invalid_retail_replacement_path = tmp / "invalid_retail_replacement.ps1"
        valid_path.write_text(valid, encoding="utf-8")
        invalid_path.write_text(invalid, encoding="utf-8")
        invalid_assets_path.write_text(invalid_assets, encoding="utf-8")
        invalid_package_assert_path.write_text(invalid_package_assert, encoding="utf-8")
        invalid_xbe_postcondition_path.write_text(invalid_xbe_postcondition, encoding="utf-8")
        invalid_runtime_id_reuse_path.write_text(invalid_runtime_id_reuse, encoding="utf-8")
        invalid_retail_replacement_path.write_text(invalid_retail_replacement, encoding="utf-8")
        verify_build_script(valid_path)
        try:
            verify_build_script(invalid_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: single-pass spmp build graph was not rejected", file=sys.stderr)
            return 1
        try:
            verify_build_script(invalid_assets_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: spmp without SP package refresh was not rejected", file=sys.stderr)
            return 1
        try:
            verify_build_script(invalid_package_assert_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: spmp without active package assertion was not rejected", file=sys.stderr)
            return 1
        try:
            verify_build_script(invalid_xbe_postcondition_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: non-aggregating XBE postcondition was not rejected", file=sys.stderr)
            return 1
        try:
            verify_build_script(invalid_runtime_id_reuse_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: stale runtime build ID object reuse was not rejected", file=sys.stderr)
            return 1
        try:
            verify_build_script(invalid_retail_replacement_path)
        except AssertionError:
            pass
        else:
            print("self-test failed: missing retail renderer legacy replacement was not rejected", file=sys.stderr)
            return 1

    print("verify_build_xbox_contracts self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Xbox build-script source contracts.")
    parser.add_argument(
        "--build-script",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "scripts" / "build_xbox.ps1",
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    try:
        result = verify_build_script(args.build_script)
    except AssertionError as exc:
        print(f"build_xbox contract verification failed: {exc}", file=sys.stderr)
        return 1

    print("build_xbox contract verification passed")
    print(
        "contracts: "
        + ", ".join(name for name, ok in result["contracts"].items() if ok)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
