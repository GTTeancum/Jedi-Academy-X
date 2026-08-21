#!/usr/bin/env python3
"""Correlate renderer source strings with functions in an XboxRecomp disassembly."""

from __future__ import annotations

import argparse
import ast
import csv
import json
import re
from bisect import bisect_right
from pathlib import Path


STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
FUNCTION_RE = re.compile(
    r"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_:<>]*[ \t*&]+)+"
    r"([A-Za-z_~][A-Za-z0-9_:~]*)[ \t]*\([^;{}]*\)[ \t]*(?:const[ \t]*)?\{"
)
CONTROL_WORDS = {"if", "for", "while", "switch", "catch"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--strings", type=Path, required=True)
    parser.add_argument("--functions", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-length", type=int, default=8)
    return parser.parse_args()


def decode_literal(token: str) -> str | None:
    try:
        value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return None
    return value if isinstance(value, str) else None


def containing_function(address: int, starts: list[int], functions: list[dict]) -> dict | None:
    index = bisect_right(starts, address) - 1
    if index < 0:
        return None
    function = functions[index]
    return function if address < int(function["end"], 16) else None


def source_function_positions(text: str) -> tuple[list[int], list[str]]:
    positions: list[int] = []
    names: list[str] = []
    for match in FUNCTION_RE.finditer(text):
        name = match.group(1)
        if name in CONTROL_WORDS:
            continue
        positions.append(match.start())
        names.append(name)
    return positions, names


def main() -> int:
    args = parse_args()
    retail_strings = json.loads(args.strings.read_text(encoding="utf-8"))
    retail_by_text: dict[str, list[dict]] = {}
    for entry in retail_strings:
        retail_by_text.setdefault(entry["string"], []).append(entry)

    functions = json.loads(args.functions.read_text(encoding="utf-8"))
    functions.sort(key=lambda item: int(item["start"], 16))
    starts = [int(item["start"], 16) for item in functions]

    source_occurrences: dict[str, list[dict[str, object]]] = {}
    for source_path in sorted(args.source.rglob("*")):
        if source_path.suffix.lower() not in {".c", ".cc", ".cpp", ".h"}:
            continue
        try:
            text = source_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        function_positions, function_names = source_function_positions(text)
        lines = text.splitlines(keepends=True)
        offset = 0
        for line_number, line in enumerate(lines, 1):
            for match in STRING_RE.finditer(line):
                value = decode_literal(match.group(0))
                if value is None or len(value) < args.minimum_length:
                    continue
                source_offset = offset + match.start()
                function_index = bisect_right(function_positions, source_offset) - 1
                source_function = function_names[function_index] if function_index >= 0 else ""
                source_occurrences.setdefault(value, []).append(
                    {
                        "source_file": str(source_path.relative_to(args.source)),
                        "source_line": line_number,
                        "source_function": source_function,
                    }
                )
            offset += len(line)

    rows: list[dict[str, object]] = []
    for value, occurrences in source_occurrences.items():
        source_functions = {item["source_function"] for item in occurrences if item["source_function"]}
        if len(source_functions) != 1:
            continue
        representative = occurrences[0]
        source_function = next(iter(source_functions))
        for retail in retail_by_text.get(value, []):
            for xref_text in retail.get("referenced_from", []):
                xref = int(xref_text, 16)
                function = containing_function(xref, starts, functions)
                rows.append(
                    {
                        "source_file": representative["source_file"],
                        "source_line": representative["source_line"],
                        "source_function": source_function,
                        "literal": value,
                        "string_address": retail["address"],
                        "xref": f"0x{xref:08X}",
                        "function_start": function["start"] if function else "",
                        "function_end": function["end"] if function else "",
                        "function_size": function["size"] if function else "",
                    }
                )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "source_file",
        "source_line",
        "source_function",
        "literal",
        "string_address",
        "xref",
        "function_start",
        "function_end",
        "function_size",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    resolved = {row["function_start"] for row in rows if row["function_start"]}
    print(f"matches={len(rows)} resolved_functions={len(resolved)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
