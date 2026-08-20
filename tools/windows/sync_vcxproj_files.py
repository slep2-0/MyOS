#!/usr/bin/env python3
"""Regenerate explicit Visual Studio project items without unsupported globs."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "KernelDevelopment.vcxproj"
BEGIN = "  <!-- BEGIN GENERATED PROJECT ITEMS -->"
END = "  <!-- END GENERATED PROJECT ITEMS -->"
EXCLUDED_PARTS = {"build", ".build_mtexe"}


def _allowed(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    return not any(part.lower() in EXCLUDED_PARTS for part in relative.parts)


def _relative(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("/", "\\")


def _sorted(paths: set[Path]) -> list[Path]:
    return sorted(paths, key=lambda path: _relative(path).casefold())


def _collect() -> tuple[list[Path], list[Path], list[Path]]:
    compile_items: set[Path] = set()
    include_items: set[Path] = set()
    none_items: set[Path] = set()

    for tree_name in ("kernel", "usermode", "shared"):
        tree = ROOT / tree_name
        for path in tree.rglob("*"):
            if not path.is_file() or not _allowed(path):
                continue

            suffix = path.suffix
            if suffix == ".c" and path != ROOT / "kernel/gen_offsets.c":
                compile_items.add(path)
            elif suffix == ".h":
                include_items.add(path)
            elif suffix.lower() in {".asm", ".ld"} or suffix == ".S":
                none_items.add(path)

    boot = ROOT / "boot"
    compile_items.update(boot.glob("*.c"))
    include_items.update(boot.glob("*.h"))

    none_items.add(ROOT / "kernel/gen_offsets.c")

    intellisense = ROOT / "tools/windows/intellisense"
    include_items.update(intellisense.glob("*.h"))

    for name in (
        "build_windows.bat",
        "clean_windows.bat",
        "run_windows.bat",
        "reset_intellisense.bat",
    ):
        none_items.add(ROOT / name)

    tools = ROOT / "tools/windows"
    for path in tools.iterdir():
        if path.is_file() and path.suffix.lower() in {".py", ".bat", ".asm", ".md"}:
            none_items.add(path)

    return _sorted(compile_items), _sorted(include_items), _sorted(none_items)


def _item_group(item_type: str, paths: list[Path]) -> list[str]:
    lines = ["  <ItemGroup>"]
    lines.extend(
        f'    <{item_type} Include="{_relative(path)}" />' for path in paths
    )
    lines.append("  </ItemGroup>")
    return lines


def main() -> int:
    text = PROJECT.read_text(encoding="utf-8")
    start = text.find(BEGIN)
    finish = text.find(END)
    if start < 0 or finish < 0 or finish < start:
        raise SystemExit("Generated project-item markers are missing or malformed.")

    compile_items, include_items, none_items = _collect()
    generated = [BEGIN]
    generated.extend(_item_group("ClCompile", compile_items))
    generated.extend(_item_group("ClInclude", include_items))
    generated.extend(_item_group("None", none_items))
    generated.append(END)

    newline = "\r\n" if "\r\n" in text else "\n"
    replacement = newline.join(generated)
    updated = text[:start] + replacement + text[finish + len(END):]
    if updated == text:
        print(
            "Visual Studio project is already synchronized: "
            f"{len(compile_items)} C files, {len(include_items)} headers, "
            f"{len(none_items)} support files."
        )
        return 0

    PROJECT.write_text(updated, encoding="utf-8", newline="")

    print(
        "Synchronized Visual Studio project: "
        f"{len(compile_items)} C files, {len(include_items)} headers, "
        f"{len(none_items)} support files."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
