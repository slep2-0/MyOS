#!/usr/bin/env python3
"""Audit and guard MatanelOS routine-description edits.

This tool deliberately does not invent documentation.  It locates C function
definitions through Clang, applies an explicit JSON manifest at function-body
boundaries, removes full routine blocks from headers, and verifies that only
comments or whitespace changed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("kernel", "shared", "usermode")
PRIVATE_STATE_ROOT = Path(
    os.environ.get("LOCALAPPDATA", str(Path.home() / ".local" / "share"))
) / "MatanelOS" / "Private" / "RoutineDocs"
DEFAULT_GUARD = PRIVATE_STATE_ROOT / "routine-docs-guard.json"
DEFAULT_AUDIT = PRIVATE_STATE_ROOT / "routine-docs-audit.json"


@dataclass(frozen=True)
class CommentSpan:
    start: int
    end: int
    text: bytes


@dataclass(frozen=True)
class FunctionDefinition:
    path: Path
    name: str
    line: int
    brace_offset: int
    return_type: str
    parameters: tuple[str, ...]
    parameter_types: tuple[str, ...]
    has_description: bool


def project_files(suffixes: Iterable[str]) -> list[Path]:
    wanted = {suffix.lower() for suffix in suffixes}
    files: list[Path] = []

    for root_name in SOURCE_ROOTS:
        root = PROJECT_ROOT / root_name
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in wanted:
                files.append(path)

    return sorted(files)


def relative_path(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def scan_comments(data: bytes) -> list[CommentSpan]:
    comments: list[CommentSpan] = []
    index = 0
    length = len(data)

    while index < length:
        byte = data[index]

        if byte in (ord('"'), ord("'")):
            quote = byte
            index += 1
            while index < length:
                if data[index] == ord("\\"):
                    index += 2
                    continue
                if data[index] == quote:
                    index += 1
                    break
                index += 1
            continue

        if byte == ord("/") and index + 1 < length:
            next_byte = data[index + 1]
            if next_byte == ord("/"):
                start = index
                index += 2
                while index < length and data[index] not in (ord("\r"), ord("\n")):
                    index += 1
                comments.append(CommentSpan(start, index, data[start:index]))
                continue

            if next_byte == ord("*"):
                start = index
                end = data.find(b"*/", index + 2)
                if end < 0:
                    raise ValueError(f"unterminated block comment at byte {start}")
                index = end + 2
                comments.append(CommentSpan(start, index, data[start:index]))
                continue

        index += 1

    return comments


def normalized_code(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    pending_space = False

    while index < len(data):
        byte = data[index]

        if byte in b" \t\r\n\v\f":
            pending_space = True
            index += 1
            continue

        if byte == ord("/") and index + 1 < len(data):
            next_byte = data[index + 1]
            if next_byte == ord("/"):
                index += 2
                while index < len(data) and data[index] not in (ord("\r"), ord("\n")):
                    index += 1
                pending_space = True
                continue
            if next_byte == ord("*"):
                end = data.find(b"*/", index + 2)
                if end < 0:
                    raise ValueError(f"unterminated block comment at byte {index}")
                index = end + 2
                pending_space = True
                continue

        if pending_space and output:
            output.extend(b" ")
        pending_space = False

        if byte in (ord('"'), ord("'")):
            quote = byte
            output.append(byte)
            index += 1
            while index < len(data):
                output.append(data[index])
                if data[index] == ord("\\") and index + 1 < len(data):
                    index += 1
                    output.append(data[index])
                elif data[index] == quote:
                    index += 1
                    break
                index += 1
            continue

        output.append(byte)
        index += 1

    return bytes(output).strip()


def newline_style(data: bytes) -> str:
    crlf = data.count(b"\r\n")
    bare_lf = data.count(b"\n") - crlf
    bare_cr = data.count(b"\r") - crlf

    if bare_cr:
        return "mixed"
    if crlf and bare_lf:
        return "mixed"
    if crlf:
        return "crlf"
    return "lf"


def file_guard(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {
        "sha256": hashlib.sha256(normalized_code(data)).hexdigest(),
        "newline_style": newline_style(data),
        "contains_crcrlf": b"\r\r\n" in data,
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def command_snapshot(args: argparse.Namespace) -> int:
    files = project_files((".c", ".h"))
    snapshot = {
        "version": 2,
        "files": {relative_path(path): file_guard(path) for path in files},
    }
    write_json(args.guard, snapshot)
    print(f"snapshotted {len(files)} C/header files to {args.guard}")
    return 0


def clang_arguments(path: Path) -> list[str]:
    arguments = [
        "--target=x86_64-none-elf",
        "-std=gnu11",
        "-ffreestanding",
        f"-I{PROJECT_ROOT}",
        f"-I{PROJECT_ROOT / 'shared' / 'include'}",
        f"-I{PROJECT_ROOT / 'kernel' / 'includes'}",
        f"-I{PROJECT_ROOT / 'usermode' / 'programs' / 'dlls' / 'mtdll' / 'includes'}",
        f"-I{PROJECT_ROOT / 'usermode' / 'tests'}",
        "-DDEBUG",
        "-DMATANELOS_BUILDING_MTDLL",
        "-DMATANELOS_EXCEPTION_CHAIN_TEST",
        "-DMATANELOS_BUILDING_LOADER_TEST_DLL",
    ]

    if "usermode" in path.parts:
        arguments.append("-DMT_USER_MODE")

    return arguments


def is_routine_description(comment: bytes) -> bool:
    lowered = comment.lower()
    return (
        re.search(rb"routine\s+description\s*:", lowered) is not None
        and re.search(rb"arguments\s*:", lowered) is not None
        and re.search(rb"return\s+values?\s*:", lowered) is not None
    )


def find_definitions() -> tuple[list[FunctionDefinition], list[str]]:
    try:
        from clang import cindex
    except ImportError as error:
        raise RuntimeError(
            "Python clang bindings are required. Run initial_setup.bat first."
        ) from error

    index = cindex.Index.create()
    definitions: list[FunctionDefinition] = []
    diagnostics: list[str] = []

    for path in project_files((".c",)):
        data = path.read_bytes()
        comments = scan_comments(data)
        translation_unit = index.parse(str(path), args=clang_arguments(path))

        for diagnostic in translation_unit.diagnostics:
            if diagnostic.severity >= cindex.Diagnostic.Error:
                diagnostics.append(f"{relative_path(path)}: {diagnostic.spelling}")

        for cursor in translation_unit.cursor.walk_preorder():
            if cursor.kind != cindex.CursorKind.FUNCTION_DECL:
                continue
            if not cursor.is_definition() or cursor.location.file is None:
                continue
            if Path(cursor.location.file.name).resolve() != path.resolve():
                continue

            body = next(
                (
                    child
                    for child in cursor.get_children()
                    if child.kind == cindex.CursorKind.COMPOUND_STMT
                ),
                None,
            )
            if body is None:
                continue

            brace_offset = body.extent.start.offset
            function_start = cursor.extent.start.offset
            has_description = any(
                comment.start >= function_start
                and comment.end <= brace_offset
                and is_routine_description(comment.text)
                for comment in comments
            )
            parameters = tuple(argument.spelling for argument in cursor.get_arguments())
            parameter_types = tuple(argument.type.spelling for argument in cursor.get_arguments())
            definitions.append(
                FunctionDefinition(
                    path=path,
                    name=cursor.spelling,
                    line=cursor.location.line,
                    brace_offset=brace_offset,
                    return_type=cursor.result_type.spelling,
                    parameters=parameters,
                    parameter_types=parameter_types,
                    has_description=has_description,
                )
            )

    definitions.sort(key=lambda item: (relative_path(item.path), item.line, item.name))
    return definitions, diagnostics


def header_descriptions() -> list[dict[str, object]]:
    descriptions: list[dict[str, object]] = []

    for path in project_files((".h",)):
        data = path.read_bytes()
        for comment in scan_comments(data):
            if not is_routine_description(comment.text):
                continue
            descriptions.append(
                {
                    "file": relative_path(path),
                    "line": data.count(b"\n", 0, comment.start) + 1,
                    "start": comment.start,
                    "end": comment.end,
                }
            )

    return descriptions


def command_audit(args: argparse.Namespace) -> int:
    definitions, diagnostics = find_definitions()
    report = {
        "version": 1,
        "definitions": [
            {
                "file": relative_path(definition.path),
                "line": definition.line,
                "name": definition.name,
                "return_type": definition.return_type,
                "parameters": list(definition.parameters),
                "parameter_types": list(definition.parameter_types),
                "has_description": definition.has_description,
            }
            for definition in definitions
        ],
        "header_descriptions": header_descriptions(),
        "clang_diagnostics": diagnostics,
    }
    write_json(args.audit, report)

    missing = sum(not definition.has_description for definition in definitions)
    print(
        f"C definitions: {len(definitions)}; missing descriptions: {missing}; "
        f"header descriptions: {len(report['header_descriptions'])}"
    )
    print(f"wrote audit to {args.audit}")
    return 1 if missing or report["header_descriptions"] else 0


def line_ending_for(data: bytes) -> str:
    return "\r\n" if newline_style(data) == "crlf" else "\n"


def paragraph_lines(value: object, indent: str) -> list[str]:
    if isinstance(value, str):
        values = [value]
    elif isinstance(value, list) and all(isinstance(item, str) for item in value):
        values = value
    else:
        raise ValueError("documentation text must be a string or list of strings")

    lines: list[str] = []
    for paragraph in values:
        if lines:
            lines.append("")
        lines.extend(f"{indent}{line}" if line else "" for line in paragraph.splitlines())
    return lines


def render_description(entry: dict[str, object], definition: FunctionDefinition, nl: str) -> bytes:
    arguments = entry.get("arguments", {})
    if not isinstance(arguments, dict):
        raise ValueError("arguments must be an object keyed by parameter name")

    expected = list(definition.parameters)
    if set(arguments) != set(expected):
        raise ValueError(
            f"{relative_path(definition.path)}:{definition.name}: manifest argument names "
            f"{sorted(arguments)} do not match definition {sorted(expected)}"
        )

    lines = ["/*++", "", "    Routine description:", ""]
    lines.extend(paragraph_lines(entry["description"], "        "))
    lines.extend(["", "    Arguments:", ""])

    if expected:
        for name in expected:
            value = arguments[name]
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"argument {name} requires a non-empty description")
            lines.append(f"        {value}")
    else:
        lines.append("        None.")

    lines.extend(["", "    Return Values:", ""])
    lines.extend(paragraph_lines(entry["returns"], "        "))

    if "notes" in entry:
        lines.extend(["", "    Notes:", ""])
        lines.extend(paragraph_lines(entry["notes"], "        "))

    lines.extend(["", "--*/"])
    return nl.join(lines).encode("utf-8")


def command_apply(args: argparse.Namespace) -> int:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("routines") if isinstance(manifest, dict) else manifest
    if not isinstance(entries, list):
        raise ValueError("manifest must be a list or contain a routines list")

    definitions, _ = find_definitions()
    lookup = {(relative_path(item.path), item.name): item for item in definitions}
    changes: dict[Path, list[tuple[int, int, bytes]]] = {}
    seen: set[tuple[str, str]] = set()

    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("each routine entry must be an object")
        key = (str(entry["file"]).replace("\\", "/"), str(entry["name"]))
        if key in seen:
            raise ValueError(f"duplicate manifest entry: {key[0]}:{key[1]}")
        seen.add(key)

        definition = lookup.get(key)
        if definition is None:
            raise ValueError(f"definition not found: {key[0]}:{key[1]}")
        if definition.has_description and not args.replace:
            raise ValueError(f"definition is already documented: {key[0]}:{key[1]}")

        data = definition.path.read_bytes()
        nl = line_ending_for(data)
        rendered = render_description(entry, definition, nl)
        if definition.has_description:
            candidates = [
                comment
                for comment in scan_comments(data)
                if comment.end <= definition.brace_offset
                and is_routine_description(comment.text)
                and not data[comment.end:definition.brace_offset].strip()
            ]
            if not candidates:
                raise ValueError(f"description block not found: {key[0]}:{key[1]}")
            comment = max(candidates, key=lambda item: item.end)
            changes.setdefault(definition.path, []).append(
                (comment.start, comment.end, rendered)
            )
        else:
            gap_start = definition.brace_offset
            while gap_start > 0 and data[gap_start - 1] in b" \t\r\n":
                gap_start -= 1
            replacement = (
                (nl + nl).encode("ascii")
                + rendered
                + (nl + nl).encode("ascii")
            )
            changes.setdefault(definition.path, []).append(
                (gap_start, definition.brace_offset, replacement)
            )

    for path, insertions in changes.items():
        data = path.read_bytes()
        for start, end, rendered in sorted(insertions, reverse=True):
            data = data[:start] + rendered + data[end:]
        path.write_bytes(data)

    print(f"documented {len(entries)} routines in {len(changes)} files")
    return 0


def expanded_comment_span(data: bytes, comment: CommentSpan) -> tuple[int, int]:
    start = comment.start
    end = comment.end

    while start > 0 and data[start - 1] in (ord(" "), ord("\t")):
        start -= 1
    if start >= 2 and data[start - 2:start] == b"\r\n":
        start -= 2
    elif start >= 1 and data[start - 1:start] == b"\n":
        start -= 1

    while end < len(data) and data[end] in (ord(" "), ord("\t")):
        end += 1
    if data[end:end + 2] == b"\r\n":
        end += 2
    elif data[end:end + 1] == b"\n":
        end += 1

    return start, end


def command_strip_headers(args: argparse.Namespace) -> int:
    removed = 0
    changed_files = 0

    for path in project_files((".h",)):
        data = path.read_bytes()
        spans = [
            expanded_comment_span(data, comment)
            for comment in scan_comments(data)
            if is_routine_description(comment.text)
        ]
        if not spans:
            continue

        nl = line_ending_for(data).encode("ascii")
        for start, end in sorted(spans, reverse=True):
            data = data[:start] + nl + data[end:]
        path.write_bytes(data)
        removed += len(spans)
        changed_files += 1

    print(f"removed {removed} routine descriptions from {changed_files} headers")
    return 0


def command_verify(args: argparse.Namespace) -> int:
    snapshot = json.loads(args.guard.read_text(encoding="utf-8"))
    expected = snapshot.get("files", {})
    actual_paths = {relative_path(path): path for path in project_files((".c", ".h"))}
    failures: list[str] = []

    if set(expected) != set(actual_paths):
        added = sorted(set(actual_paths) - set(expected))
        removed = sorted(set(expected) - set(actual_paths))
        if added:
            failures.append(f"new files absent from guard: {', '.join(added)}")
        if removed:
            failures.append(f"guarded files missing: {', '.join(removed)}")

    for name in sorted(set(expected) & set(actual_paths)):
        current = file_guard(actual_paths[name])
        guarded = expected[name]
        if current["sha256"] != guarded["sha256"]:
            failures.append(f"non-comment code changed: {name}")
        if current["newline_style"] != guarded["newline_style"]:
            failures.append(f"newline style changed: {name}")
        if current["contains_crcrlf"]:
            failures.append(f"malformed CRCRLF sequence: {name}")

    definitions, _ = find_definitions()
    missing = [
        f"{relative_path(item.path)}:{item.line}:{item.name}"
        for item in definitions
        if not item.has_description
    ]
    if missing:
        failures.append(
            f"{len(missing)} C definitions still lack descriptions; run audit for details"
        )

    headers = header_descriptions()
    if headers:
        failures.append(f"{len(headers)} full routine descriptions remain in headers")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1

    print(
        f"verified {len(actual_paths)} files: code tokens preserved, every C definition "
        "documented, and headers contain no full routine descriptions"
    )
    return 0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    snapshot = subparsers.add_parser("snapshot", help="record code-token guards")
    snapshot.add_argument("--guard", type=Path, default=DEFAULT_GUARD)
    snapshot.set_defaults(handler=command_snapshot)

    audit = subparsers.add_parser("audit", help="inventory C and header descriptions")
    audit.add_argument("--audit", type=Path, default=DEFAULT_AUDIT)
    audit.set_defaults(handler=command_audit)

    apply_manifest = subparsers.add_parser(
        "apply-manifest",
        help="insert reviewed descriptions from an explicit JSON manifest",
    )
    apply_manifest.add_argument("manifest", type=Path)
    apply_manifest.add_argument(
        "--replace",
        action="store_true",
        help="replace existing routine blocks for the manifest entries",
    )
    apply_manifest.set_defaults(handler=command_apply)

    strip_headers = subparsers.add_parser(
        "strip-headers",
        help="remove positively identified full routine blocks from headers",
    )
    strip_headers.set_defaults(handler=command_strip_headers)

    verify = subparsers.add_parser("verify", help="verify a comment-only pass")
    verify.add_argument("--guard", type=Path, default=DEFAULT_GUARD)
    verify.set_defaults(handler=command_verify)

    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    try:
        return args.handler(args)
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
