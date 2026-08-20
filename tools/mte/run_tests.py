#!/usr/bin/env python3
"""Build and validate a relocatable MTE fixture."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from mte_pack import (
    ElfImage,
    MTE_RELOCATION,
    MTE_TLS_DIRECTORY,
    MTE_TLS_MODULE_INDEX_FIXUP,
    analyze,
    metadata_size,
    pack_image,
)


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).resolve().parent / "tests/relocation_fixture.c"
NATIVE_HEADER_FIXTURE = (
    Path(__file__).resolve().parent / "tests/native_header_fixture.c"
)
LINKER_SCRIPT = ROOT / "usermode/mtdll.ld"
EXE_LINKER_SCRIPT = ROOT / "usermode/mtexe.ld"
TLS_FIXTURE = Path(__file__).resolve().parent / "tests/tls_fixture.c"
TLS_PROVIDER_FIXTURE = (
    Path(__file__).resolve().parent / "tests/tls_provider_fixture.c"
)
HEADER = struct.Struct("<4s15Q4s")


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
        exports, imports, relocations, tls_fixups = analyze(
            discovery_image, {}, image_base, image_size
        )
        layout = metadata_size(
            discovery_image.symbol_value("__mt_metadata_start") - image_base,
            exports,
            imports,
            relocations,
            None,
        )
        if len(exports) != 1 or imports or len(relocations) != 1 or tls_fixups:
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

        provider_obj = directory / "tls_provider.o"
        provider_elf = directory / "tls_provider.elf"
        tls_pic_obj = directory / "tls_pic.o"
        tls_dll_discovery = directory / "tls_dll.discovery.elf"
        tls_dll_elf = directory / "tls_dll.elf"
        tls_dll_mte = directory / "tls_dll.mtdll"

        common_compile = [
            args.clang,
            "--target=x86_64-none-elf",
            "-m64",
            "-ffreestanding",
            "-nostdlib",
            "-fno-builtin",
            "-fno-asynchronous-unwind-tables",
            "-fvisibility=hidden",
            "-c",
        ]
        run([*common_compile, "-fPIC", str(TLS_PROVIDER_FIXTURE), "-o", str(provider_obj)])
        provider_link = [
            argument if argument != "--soname=fixture.mtdll" else "--soname=tlsprovider.mtdll"
            for argument in base_link
        ]
        run(
            [
                *provider_link,
                "--defsym=__mt_metadata_size=0x1000",
                "-o",
                str(provider_elf),
                str(provider_obj),
            ]
        )

        run([*common_compile, "-fPIC", str(TLS_FIXTURE), "-o", str(tls_pic_obj)])
        tls_dll_link = [
            argument if argument != "--soname=fixture.mtdll" else "--soname=tlsfixture.mtdll"
            for argument in base_link
        ]
        run(
            [
                *tls_dll_link,
                "--defsym=__mt_metadata_size=0x1000",
                "-o",
                str(tls_dll_discovery),
                str(tls_pic_obj),
                str(provider_elf),
            ]
        )
        tls_dependencies = {"tlsprovider.mtdll": provider_elf}
        tls_dll_metadata_size = pack_image(
            tls_dll_discovery,
            tls_dll_mte,
            tls_dependencies,
            None,
            True,
        )
        run(
            [
                *tls_dll_link,
                f"--defsym=__mt_metadata_size=0x{tls_dll_metadata_size:x}",
                "-o",
                str(tls_dll_elf),
                str(tls_pic_obj),
                str(provider_elf),
            ]
        )
        pack_image(
            tls_dll_elf,
            tls_dll_mte,
            tls_dependencies,
            tls_dll_metadata_size,
            False,
        )

        tls_dll_data = tls_dll_mte.read_bytes()
        tls_dll_header = HEADER.unpack_from(tls_dll_data)
        if tls_dll_header[13] != 24:
            raise RuntimeError("TLS DLL must import exactly __tls_get_addr")
        tls_dll_rva, tls_dll_size = tls_dll_header[14:16]
        if tls_dll_size != MTE_TLS_DIRECTORY.size:
            raise RuntimeError("TLS DLL has an invalid TLS directory size")
        (
            tls_template_rva,
            tls_template_size,
            tls_total_size,
            tls_alignment,
            tls_fixups_rva,
            tls_fixups_size,
        ) = MTE_TLS_DIRECTORY.unpack_from(tls_dll_data, tls_dll_rva)
        if (
            tls_template_size,
            tls_total_size,
            tls_alignment,
            tls_fixups_size,
        ) != (8, 16, 8, MTE_TLS_MODULE_INDEX_FIXUP.size):
            raise RuntimeError("TLS DLL metadata does not match its PT_TLS segment")
        if tls_dll_data[tls_template_rva : tls_template_rva + 8] != struct.pack(
            "<Q", 0x1122334455667788
        ):
            raise RuntimeError("TLS DLL initialized template was not copied")
        (tls_module_target,) = MTE_TLS_MODULE_INDEX_FIXUP.unpack_from(
            tls_dll_data, tls_fixups_rva
        )
        if struct.unpack_from("<Q", tls_dll_data, tls_module_target)[0] != 0:
            raise RuntimeError("TLS DLL module-index destination must remain zero on disk")

        tls_pie_obj = directory / "tls_pie.o"
        tls_exe_discovery = directory / "tls_exe.discovery.elf"
        tls_exe_elf = directory / "tls_exe.elf"
        tls_exe_mte = directory / "tls_exe.mtexe"
        run([*common_compile, "-fPIE", str(TLS_FIXTURE), "-o", str(tls_pie_obj)])
        tls_exe_link = [
            args.lld,
            "-pie",
            "--no-dynamic-linker",
            "--no-undefined",
            "-z",
            "now",
            "-z",
            "notext",
            "-z",
            "norelro",
            "-T",
            str(EXE_LINKER_SCRIPT),
            "-m",
            "elf_x86_64",
            "--entry=TlsFixtureRead",
        ]
        run(
            [
                *tls_exe_link,
                "--defsym=__mt_metadata_size=0x1000",
                "-o",
                str(tls_exe_discovery),
                str(tls_pie_obj),
            ]
        )
        tls_exe_metadata_size = pack_image(
            tls_exe_discovery,
            tls_exe_mte,
            {},
            None,
            True,
        )
        run(
            [
                *tls_exe_link,
                f"--defsym=__mt_metadata_size=0x{tls_exe_metadata_size:x}",
                "-o",
                str(tls_exe_elf),
                str(tls_pie_obj),
            ]
        )
        pack_image(
            tls_exe_elf,
            tls_exe_mte,
            {},
            tls_exe_metadata_size,
            False,
        )
        tls_exe_data = tls_exe_mte.read_bytes()
        tls_exe_header = HEADER.unpack_from(tls_exe_data)
        tls_exe_rva, tls_exe_size = tls_exe_header[14:16]
        if tls_exe_header[13] != 0 or tls_exe_size != MTE_TLS_DIRECTORY.size:
            raise RuntimeError("TLS executable has unexpected imports or TLS size")
        tls_exe_directory = MTE_TLS_DIRECTORY.unpack_from(tls_exe_data, tls_exe_rva)
        if tls_exe_directory[1:4] != (8, 16, 8):
            raise RuntimeError("TLS executable metadata does not match PT_TLS")
        if tls_exe_directory[4:] != (0, 0):
            raise RuntimeError("local-exec TLS must not contain module-index fixups")

    print(
        "[MTE TEST] PASS "
        "(native header + base relocation + executable/DLL TLS metadata)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, struct.error) as exc:
        print(f"MTE TEST ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
