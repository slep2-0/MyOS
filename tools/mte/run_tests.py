#!/usr/bin/env python3
"""Build and validate a relocatable MTE fixture."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from mte_pack import ElfImage, MTE_RELOCATION, metadata_size, pack_image, analyze


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).resolve().parent / "tests/relocation_fixture.c"
NATIVE_HEADER_FIXTURE = (
    Path(__file__).resolve().parent / "tests/native_header_fixture.c"
)
LINKER_SCRIPT = ROOT / "usermode/mtdll.ld"
HEADER = struct.Struct("<4s13Q20s")


def run(command: list[str]) -> None:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed: {subprocess.list2cmdline(command)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--lld", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mte-test-") as temporary:
        directory = Path(temporary)
        obj = directory / "fixture.o"
        discovery = directory / "fixture.discovery.elf"
        elf = directory / "fixture.elf"
        mte = directory / "fixture.mtdll"

        run(
            [
                args.clang,
                "--target=x86_64-none-elf",
                "-m64",
                "-std=gnu11",
                "-ffreestanding",
                "-nostdlib",
                "-fno-builtin",
                "-fsyntax-only",
                "-I",
                str(ROOT / "shared/include"),
                str(NATIVE_HEADER_FIXTURE),
            ]
        )

        run(
            [
                args.clang,
                "--target=x86_64-none-elf",
                "-m64",
                "-ffreestanding",
                "-nostdlib",
                "-fno-builtin",
                "-fno-asynchronous-unwind-tables",
                "-fPIC",
                "-fvisibility=hidden",
                "-DMATANELOS_BUILDING_MTDLL",
                "-I",
                str(ROOT / "shared/include"),
                "-c",
                str(FIXTURE),
                "-o",
                str(obj),
            ]
        )

        base_link = [
            args.lld,
            "-shared",
            "-Bsymbolic",
            "--soname=fixture.mtdll",
            "--no-undefined",
            "-z",
            "now",
            "-z",
            "notext",
            "-z",
            "norelro",
            "-T",
            str(LINKER_SCRIPT),
            "-m",
            "elf_x86_64",
        ]
        run(
            [
                *base_link,
                "--defsym=__mt_metadata_size=0x1000",
                "-o",
                str(discovery),
                str(obj),
            ]
        )

        discovery_image = ElfImage(discovery)
        image_base = discovery_image.symbol_value("__image_base")
        image_size = discovery_image.symbol_value("__bss_end") - image_base
        exports, imports, relocations = analyze(
            discovery_image, {}, image_base, image_size
        )
        layout = metadata_size(
            discovery_image.symbol_value("__mt_metadata_start") - image_base,
            exports,
            imports,
            relocations,
        )
        if len(exports) != 1 or imports or len(relocations) != 1:
            raise RuntimeError(
                "fixture must produce one export, no imports, and one base relocation"
            )

        run(
            [
                *base_link,
                f"--defsym=__mt_metadata_size=0x{layout.size:x}",
                "-o",
                str(elf),
                str(obj),
            ]
        )
        pack_image(elf, mte, {}, layout.size, False)

        data = mte.read_bytes()
        fields = HEADER.unpack_from(data)
        relocation_rva = fields[10]
        relocation_size = fields[11]
        if relocation_size != MTE_RELOCATION.size:
            raise RuntimeError("packed fixture has an invalid relocation directory")
        target_rva, relocation_info, addend_rva = MTE_RELOCATION.unpack_from(
            data, relocation_rva
        )
        if relocation_info != 8:
            raise RuntimeError("packed fixture has an unexpected relocation type")

        linked = ElfImage(elf)
        expected_target = linked.symbol_value("FixturePointer") - image_base
        expected_addend = linked.symbol_value("FixtureValue") - image_base
        if (target_rva, addend_rva) != (expected_target, expected_addend):
            raise RuntimeError("ELF load-bias relocation was not normalized to RVAs")

        rebased_image = 0x500000
        if rebased_image + addend_rva != 0x500000 + expected_addend:
            raise RuntimeError("rebased relocation arithmetic is incorrect")

    print(
        "[MTE TEST] PASS "
        "(native header + export discovery + normalized base relocation)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, struct.error) as exc:
        print(f"MTE TEST ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
