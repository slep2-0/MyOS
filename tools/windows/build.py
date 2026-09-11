#!/usr/bin/env python3
"""Native Windows build driver for the MatanelOS ELF toolchain."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from make_image import create_image
from seh_translate import TranslationError, translate_file


ROOT = Path(__file__).resolve().parents[2]
WINDOWS_BUILD = ROOT / "build" / "windows"
TARGET_TRIPLE = "x86_64-none-elf"
MTDLL_MODULE_NAME = "mtdll.mtdll"
MTDLL_PRODUCER_DEFINE = "MATANELOS_BUILDING_MTDLL"
MTDLL_PRIVATE_INCLUDE = ROOT / "usermode/programs/dlls/mtdll/includes"

STRESS_MODES = {
    "normal": 0,
    "full-suite": 0,
    "cold-boot": 1,
    "randomized": 2,
    "exception-chain": 3,
    "heap": 4,
    "loader": 5,
    "tls": 6,
    "process": 7,
    "priority": 8,
    "affinity": 9,
    "balancing": 10,
    "namespace": 11,
}

KERNEL_SLOW_PATHS = {
    "kernel/core/me/irql.c",
    "kernel/core/me/scheduler.c",
    "kernel/core/ms/events.c",
    "kernel/drivers/ahci/ahci.c",
}

KERNEL_ASM = [
    "tools/windows/freestanding_runtime.asm",
    "kernel/kernel_entry.asm",
    "kernel/core/mh/isr_stub.asm",
    "kernel/core/me/context.asm",
    "kernel/core/mh/cpuid.asm",
    "kernel/core/ms/sleep.asm",
    "kernel/core/mt/entry.asm",
]

MTDLL_C = [
    "usermode/programs/dlls/mtdll/dllmain.c",
    "usermode/programs/dlls/mtdll/exception.c",
    "usermode/programs/dlls/mtdll/file.c",
    "usermode/programs/dlls/mtdll/handle.c",
    "usermode/programs/dlls/mtdll/memory.c",
    "usermode/programs/dlls/mtdll/heap.c",
    "usermode/programs/dlls/mtdll/loader.c",
    "usermode/programs/dlls/mtdll/process.c",
    "usermode/programs/dlls/mtdll/string.c",
    "usermode/programs/dlls/mtdll/thread.c",
    "usermode/programs/dlls/mtdll/ldr/dllldr.c",
    "usermode/programs/dlls/mtdll/ldr/procldr.c",
    "usermode/programs/dlls/mtdll/ldr/thrdldr.c",
    "usermode/programs/dlls/mtdll/ldr/tlsapi.c",
    "usermode/programs/dlls/mtdll/error.c",
    "usermode/programs/dlls/mtdll/synch.c",
    "usermode/programs/dlls/mtdll/print.c",
]

MTDLL_GAS = [
]

MTDLL_NASM = [
    "usermode/syscalls/syscalls.asm",
    "usermode/programs/dlls/mtdll/apcdispatch.asm",
]

MTEXE_COMMON_C = ["usermode/crt0.c"]
MTEXE_C = ["usermode/programs/exes/terminateMyself/main.c"]
MTEXE_GAS = []
MTEXE_NASM = ["tools/windows/freestanding_runtime.asm"]

EXCEPTION_TEST_MTDLL_C = [
    "tests/usermode/mtdll/exception_chain.c",
]
EXCEPTION_TEST_MTDLL_NASM = [
    "tests/usermode/mtdll/language_context.asm",
]
EXCEPTION_TEST_MTEXE_C = [
    "tests/usermode/exceptionChainTest/main.c",
]
EXCEPTION_TEST_DEFINE = "MATANELOS_EXCEPTION_CHAIN_TEST"
EXCEPTION_TEST_INCLUDE = ROOT / "tests/usermode"

HEAP_TEST_MTEXE_C = [
    "tests/usermode/heapTest/main.c",
]
HEAP_TEST_INCLUDE = ROOT / "tests/usermode"

LOADER_TEST_MTDLL_C = [
    "tests/usermode/mtdll/loader.c",
]
LOADER_TEST_MTEXE_C = [
    "tests/usermode/loaderTest/main.c",
]
LOADER_TEST_INCLUDE = ROOT / "tests/usermode"
LOADER_TEST_DLL_DEFINE = "MATANELOS_BUILDING_LOADER_TEST_DLL"
LOADER_GOOD_DLL_C = [
    "tests/usermode/loaderGoodDll/dllmain.c",
]
LOADER_FAIL_DLL_C = [
    "tests/usermode/loaderFailDll/dllmain.c",
]
LOADER_NO_ENTRY_DLL_C = [
    "tests/usermode/loaderNoEntryDll/module.c",
]
TLS_TEST_MTEXE_C = [
    "tests/usermode/tlsTest/main.c",
]
TLS_TEST_INCLUDE = ROOT / "tests/usermode"
TLS_TEST_DLL_DEFINE = "MATANELOS_BUILDING_TLS_TEST_DLL"
TLS_TEST_DLL_C = [
    "tests/usermode/loaderTlsDll/module.c",
]
TLS_DYNAMIC_DLL_C = [
    *TLS_TEST_DLL_C,
    "tests/usermode/loaderDynamicTlsDll/dllmain.c",
]
TLS_FAIL_DYNAMIC_DLL_C = [
    *TLS_TEST_DLL_C,
    "tests/usermode/loaderFailTlsDll/dllmain.c",
]
PROCESS_TEST_MTEXE_C = [
    "tests/usermode/processTest/main.c",
]
PROCESS_TEST_CHILD_C = [
    "tests/usermode/processChild/main.c",
]
PROCESS_TEST_INCLUDE = ROOT / "tests/usermode"


class BuildFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class Tools:
    clang: Path
    lld: Path
    lld_link: Path
    objcopy: Path
    nasm: Path
    qemu: Path | None


@dataclass(frozen=True)
class CompileJob:
    label: str
    command: tuple[str, ...]
    cwd: Path


def _program_candidates(name: str, environment_directory: str | None) -> Iterable[Path]:
    if environment_directory:
        value = os.environ.get(environment_directory)
        if value:
            candidate = Path(value)
            yield candidate / name if candidate.is_dir() else candidate
    located = shutil.which(name)
    if located:
        yield Path(located)


def _find_required(name: str, environment_directory: str | None, fallbacks: Sequence[Path]) -> Path:
    for candidate in [*_program_candidates(name, environment_directory), *fallbacks]:
        if candidate.is_file():
            return candidate.resolve()
    variable = f" or %{environment_directory}%" if environment_directory else ""
    raise BuildFailure(f"Could not find {name} on PATH{variable}. Run tools\\windows\\bootstrap.bat.")


def discover_tools() -> Tools:
    llvm = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin"
    clang = _find_required("clang.exe", "LLVM_BIN", [llvm / "clang.exe"])
    lld = _find_required("ld.lld.exe", "LLVM_BIN", [llvm / "ld.lld.exe"])
    lld_link = _find_required("lld-link.exe", "LLVM_BIN", [llvm / "lld-link.exe"])
    objcopy = _find_required("llvm-objcopy.exe", "LLVM_BIN", [llvm / "llvm-objcopy.exe"])
    nasm = _find_required(
        "nasm.exe",
        "NASM_BIN",
        [
            ROOT / "tools" / "nasm.exe",
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "NASM" / "nasm.exe",
        ],
    )
    qemu = None
    for candidate in [
        *_program_candidates("qemu-system-x86_64.exe", "QEMU_BIN"),
        Path(r"C:\msys64\mingw64\bin\qemu-system-x86_64.exe"),
    ]:
        if candidate.is_file():
            qemu = candidate.resolve()
            break
    return Tools(clang, lld, lld_link, objcopy, nasm, qemu)


def _quote_command(command: Sequence[str]) -> str:
    return subprocess.list2cmdline(list(command))


def _run(command: Sequence[str], cwd: Path = ROOT, *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    if not capture:
        print(f"> {_quote_command(command)}")
    result = subprocess.run(
        list(command),
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        check=False,
    )
    if result.returncode != 0:
        if capture and result.stdout:
            print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
        raise BuildFailure(f"Command failed with exit code {result.returncode}: {_quote_command(command)}")
    return result


def _run_compile(job: CompileJob) -> tuple[CompileJob, int, str]:
    result = subprocess.run(
        list(job.command),
        cwd=job.cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return job, result.returncode, result.stdout


def _run_compile_jobs(jobs: list[CompileJob], workers: int) -> None:
    if not jobs:
        print("[BUILD] Objects are up to date")
        return
    print(f"[BUILD] Compiling {len(jobs)} source file(s) with {workers} worker(s)")
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(_run_compile, job) for job in jobs]
        for future in concurrent.futures.as_completed(futures):
            job, return_code, output = future.result()
            print(f"[{job.label}]", end=" ")
            print("PASS" if return_code == 0 else "FAILED")
            if output:
                print(output, end="" if output.endswith("\n") else "\n")
            failed |= return_code != 0
    if failed:
        raise BuildFailure("One or more source files failed to compile.")


def _fingerprint(directory: Path, values: Sequence[str]) -> None:
    digest = hashlib.sha256("\0".join(values).encode("utf-8")).hexdigest()
    stamp = directory / ".flags.sha256"
    old_digest = stamp.read_text(encoding="ascii").strip() if stamp.is_file() else None
    if old_digest != digest and directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True, exist_ok=True)
    stamp.write_text(digest + "\n", encoding="ascii")


def _newest(paths: Iterable[Path]) -> float:
    return max((path.stat().st_mtime for path in paths if path.is_file()), default=0.0)


def _needs_rebuild(source: Path, output: Path, dependency_time: float = 0.0) -> bool:
    return not output.is_file() or output.stat().st_mtime < max(source.stat().st_mtime, dependency_time)


def _object_path(object_root: Path, source: Path) -> Path:
    relative = source.relative_to(ROOT)
    return object_root / relative.parent / f"{relative.name}.o"


def _diagnostic_flags() -> list[str]:
    return [
        "-fdiagnostics-format=msvc",
        "-fdiagnostics-absolute-paths",
        "-fdiagnostics-show-option",
        "-fcolor-diagnostics",
        "-ferror-limit=0",
    ]


def _target_flags() -> list[str]:
    return [f"--target={TARGET_TRIPLE}", "-m64", "-mgeneral-regs-only", "-mno-red-zone"]


def _kernel_common_flags(
    configuration: str,
    gdb: bool,
    stress_mode: str,
    stress_duration_seconds: int,
) -> list[str]:
    flags = [
        *_target_flags(),
        "-std=gnu11",
        "-fno-inline",
        "-ffreestanding",
        "-c",
        *_diagnostic_flags(),
        "-fno-omit-frame-pointer",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wmissing-prototypes",
        "-Wstrict-prototypes",
        "-Wno-unused-function",
        "-Wno-multichar",
        "-Wshadow",
        # GCC's cast-align warning is less aggressive than Clang's. Keep the
        # existing source policy instead of introducing LLVM-only failures.
        "-Wno-cast-align",
        "-mcmodel=large",
        "-fno-pie",
        "-fno-pic",
        f"-DMT_STRESS_MODE={STRESS_MODES[stress_mode]}",
        f"-DMT_STRESS_AUTOMATION={int(stress_mode != 'normal')}",
        f"-DMT_STRESS_DURATION_SECONDS={stress_duration_seconds}",
    ]
    if configuration == "Debug":
        flags.extend(
            [
                "-DDEBUG",
                "-O0",
                "-g",
                "-fstack-protector-strong",
                "-mstack-protector-guard=global",
                "-Wframe-larger-than=4096",
            ]
        )
    else:
        flags.append("-O2")
    if gdb:
        flags.extend(["-DGDB", "-g"])
    return flags


def _source_specific_kernel_flags(source: Path, base: list[str], configuration: str) -> list[str]:
    relative = source.relative_to(ROOT).as_posix()
    flags = list(base)
    if relative in KERNEL_SLOW_PATHS or relative in {
        "kernel/kernel.c",
        "tests/kernel/stress.c",
    }:
        flags = [flag for flag in flags if flag not in {"-O2", "-O0"}]
        flags.extend(["-O0", "-fno-optimize-sibling-calls"])
    if relative == "kernel/kernel.c":
        flags = [
            flag
            for flag in flags
            if flag != "-fstack-protector-strong" and not flag.startswith("-mstack-protector-guard=")
        ]
    return flags


def _parse_offset_requests(source: Path) -> tuple[list[tuple[str, str | None, bool]], str]:
    requests: list[tuple[str, str | None, bool]] = []
    declarations: list[str] = [
        "#define __OFFSET_GENERATOR__",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdbool.h>",
        '#include "kernel/includes/core.h"',
        '#include "kernel/includes/annotations.h"',
        '#include "kernel/includes/ms.h"',
        '#include "kernel/includes/mm.h"',
        '#include "kernel/includes/ps.h"',
        '#include "kernel/includes/me.h"',
        '#include "kernel/includes/behavior.h"',
        "",
    ]
    in_main = False
    conditional_depth = 0
    source_text = source.read_text(encoding="utf-8")
    for line_number, line in enumerate(source_text.splitlines(), start=1):
        if line.startswith("int main("):
            in_main = True
            continue
        if not in_main:
            continue
        stripped = line.strip()
        if stripped.startswith(("#if", "#else", "#endif")):
            declarations.append(stripped)
            if stripped.startswith(("#if", "#ifdef", "#ifndef")):
                conditional_depth += 1
            elif stripped.startswith("#endif"):
                conditional_depth = max(conditional_depth - 1, 0)
            continue
        comment = re.fullmatch(r'GEN_COMMENT\("(.*)"\);', stripped)
        if comment:
            requests.append(("comment", comment.group(1), conditional_depth != 0))
            continue
        offset = re.fullmatch(r"GEN_OFFSET\((\w+),\s*(\w+)\);", stripped)
        size = re.fullmatch(r"GEN_SIZE\((\w+)\);", stripped)
        define = re.fullmatch(r"GEN_DEFINE\((\w+),\s*(.+)\);", stripped)
        if offset:
            structure, member = offset.groups()
            name = f"{structure}_{member}"
            expression = f"__builtin_offsetof({structure}, {member})"
        elif size:
            structure = size.group(1)
            name = f"SIZEOF_{structure}"
            expression = f"sizeof({structure})"
        elif define:
            name, expression = define.groups()
        else:
            continue
        requests.append((name, None, conditional_depth != 0))
        declarations.extend(
            [
                f'#line {line_number} "{source.as_posix()}"',
                f"__attribute__((used)) const unsigned long long __mt_offset_{name} = "
                f"(unsigned long long)({expression});",
            ]
        )
    return requests, "\n".join(declarations) + "\n"


def _generate_offsets(tools: Tools, configuration: str, gdb: bool, output: Path) -> None:
    generator = ROOT / "kernel/gen_offsets.c"
    requests, helper_source = _parse_offset_requests(generator)
    helper = output.parent / "gen_offsets_elf.c"
    llvm_ir = output.parent / "gen_offsets_elf.ll"
    helper.write_text(helper_source, encoding="utf-8", newline="\n")
    command = [
        str(tools.clang),
        *_target_flags(),
        "-std=gnu11",
        "-ffreestanding",
        "-S",
        "-emit-llvm",
        "-O0",
        *_diagnostic_flags(),
        "-I",
        str(ROOT),
    ]
    if configuration == "Debug":
        command.append("-DDEBUG")
    if gdb:
        command.append("-DGDB")
    command.extend([str(helper), "-o", str(llvm_ir)])
    _run(command)

    values: dict[str, int] = {}
    pattern = re.compile(r"@__mt_offset_(\w+)\s*=.*?constant\s+i64\s+(-?\d+)")
    for line in llvm_ir.read_text(encoding="utf-8").splitlines():
        match = pattern.search(line)
        if match:
            values[match.group(1)] = int(match.group(2)) & 0xFFFFFFFFFFFFFFFF

    lines = [
        "; ===============================================================",
        "; AUTOMATICALLY GENERATED FILE - DO NOT EDIT MANUALLY",
        "; Generated from kernel/gen_offsets.c for x86_64-none-elf",
        "; ===============================================================",
        "",
    ]
    missing: list[str] = []
    for name, detail, conditional in requests:
        if name == "comment":
            lines.extend(["", f"; {detail}"])
        elif name in values:
            lines.append(f"%define {name:<40} 0x{values[name]:X}")
        elif not conditional:
            missing.append(name)
    if missing:
        raise BuildFailure(f"LLVM did not emit offset constant(s): {', '.join(missing)}")
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"[OFFSETS] {output}")


def build_kernel(
    tools: Tools,
    configuration: str,
    gdb: bool,
    workers: int,
    stress_mode: str = "normal",
    stress_duration_seconds: int = 1800,
) -> tuple[Path, Path]:
    config_directory = WINDOWS_BUILD / configuration.lower()
    object_root = config_directory / "obj/kernel"
    base_flags = _kernel_common_flags(
        configuration,
        gdb,
        stress_mode,
        stress_duration_seconds,
    )
    _fingerprint(object_root, [str(tools.clang), str(tools.nasm), configuration, str(gdb), *base_flags])

    sources = sorted([
        *(path for path in (ROOT / "kernel").rglob("*.c") if path.name != "gen_offsets.c"),
        *(ROOT / "tests/kernel").rglob("*.c"),
    ])
    header_time = _newest([
        *(ROOT / "kernel").rglob("*.h"),
        *(ROOT / "tests/kernel").rglob("*.h"),
        *(ROOT / "shared/include").rglob("*.h"),
    ])
    jobs: list[CompileJob] = []
    c_objects: list[Path] = []
    for source in sources:
        output = _object_path(object_root, source)
        c_objects.append(output)
        if not _needs_rebuild(source, output, header_time):
            continue
        output.parent.mkdir(parents=True, exist_ok=True)
        flags = _source_specific_kernel_flags(source, base_flags, configuration)
        jobs.append(
            CompileJob(
                f"CC {source.relative_to(ROOT)}",
                tuple([str(tools.clang), *flags, str(source), "-o", str(output)]),
                ROOT,
            )
        )
    _run_compile_jobs(jobs, workers)

    offsets = object_root / "offsets.inc"
    offset_inputs = [
        ROOT / "kernel/gen_offsets.c",
        *(ROOT / "kernel").rglob("*.h"),
        *(ROOT / "shared/include").rglob("*.h"),
    ]
    if not offsets.is_file() or offsets.stat().st_mtime < _newest(offset_inputs):
        _generate_offsets(tools, configuration, gdb, offsets)

    asm_objects: list[Path] = []
    include_path = object_root.as_posix() + "/"
    for relative in KERNEL_ASM:
        source = ROOT / relative
        output = _object_path(object_root, source)
        asm_objects.append(output)
        if _needs_rebuild(source, output, offsets.stat().st_mtime):
            output.parent.mkdir(parents=True, exist_ok=True)
            _run([str(tools.nasm), "-f", "elf64", f"-I{include_path}", str(source), "-o", str(output)])

    trampoline_source = ROOT / "kernel/core/mh/ap_trampoline.asm"
    trampoline_bin = object_root / "ap_trampoline.bin"
    if _needs_rebuild(trampoline_source, trampoline_bin, offsets.stat().st_mtime):
        _run(
            [str(tools.nasm), "-f", "bin", f"-I{include_path}", str(trampoline_source), "-o", str(trampoline_bin)]
        )
    wrapper = object_root / "ap_trampoline_wrapper.asm"
    wrapper_text = (
        "section .aptrampoline progbits alloc noexec nowrite align=16\n"
        "global _binary_build_ap_trampoline_bin_start\n"
        "global _binary_build_ap_trampoline_bin_end\n"
        "_binary_build_ap_trampoline_bin_start:\n"
        'incbin "ap_trampoline.bin"\n'
        "_binary_build_ap_trampoline_bin_end:\n"
    )
    if not wrapper.is_file() or wrapper.read_text(encoding="ascii") != wrapper_text:
        wrapper.write_text(wrapper_text, encoding="ascii", newline="\n")
    trampoline_object = object_root / "ap_trampoline.o"
    if _needs_rebuild(wrapper, trampoline_object, trampoline_bin.stat().st_mtime):
        _run([str(tools.nasm), "-f", "elf64", wrapper.name, "-o", trampoline_object.name], cwd=object_root)

    kernel_entry = _object_path(object_root, ROOT / "kernel/kernel_entry.asm")
    remaining_asm = [obj for obj in asm_objects if obj != kernel_entry]
    objects = [kernel_entry, *c_objects, *remaining_asm, trampoline_object]
    kernel_elf = config_directory / "kernel.elf"
    link_time = max(path.stat().st_mtime for path in objects)
    linker_script = ROOT / "kernel/linker.ld"
    if not kernel_elf.is_file() or kernel_elf.stat().st_mtime < max(link_time, linker_script.stat().st_mtime):
        _run(
            [
                str(tools.lld),
                "-m",
                "elf_x86_64",
                "-T",
                str(linker_script),
                "-static",
                "-nostdlib",
                "-o",
                str(kernel_elf),
                *map(str, objects),
            ]
        )
    kernel_bin = config_directory / "kernel.bin"
    if _needs_rebuild(kernel_elf, kernel_bin):
        _run([str(tools.objcopy), "-O", "binary", str(kernel_elf), str(kernel_bin)])
    print(f"[KERNEL] {kernel_elf}")
    return kernel_elf, kernel_bin


def _user_c_flags(
    *,
    pic: bool,
    executable: bool,
    defines: Sequence[str] = (),
    include_directories: Sequence[Path] = (),
) -> list[str]:
    flags = [
        *_target_flags(),
        "-std=gnu11",
        "-ffreestanding",
        "-nostdlib",
        "-fno-builtin",
        "-fno-asynchronous-unwind-tables",
        "-fno-omit-frame-pointer",
        "-ffunction-sections",
        "-fdata-sections",
        "-c",
        *_diagnostic_flags(),
        "-Wall",
        "-Wextra",
        "-Werror", # User warnings also fail user compilation like the kernel, added.
        "-Wno-unused-function",
        "-O0",
        "-g",
        "-I",
        str(ROOT / "shared/include"),
    ]
    if pic:
        flags.extend(["-fPIC", "-fvisibility=hidden"])
    else:
        flags.extend(["-fPIE", "-fvisibility=hidden"])
    for define in defines:
        flags.append(f"-D{define}")
    for include_directory in include_directories:
        flags.extend(["-I", str(include_directory)])
    return flags


def _build_user_component(
    tools: Tools,
    name: str,
    c_sources: Sequence[str],
    gas_sources: Sequence[str],
    nasm_sources: Sequence[str],
    linker_script: Path,
    output: Path,
    *,
    pic: bool,
    executable: bool,
    workers: int,
    module_name: str,
    dependencies: dict[str, Path] | None = None,
    defines: Sequence[str] = (),
    include_directories: Sequence[Path] = (),
    entry_symbol: str | None = None,
) -> tuple[Path, Path]:
    defines = tuple(defines)
    include_directories = tuple(include_directories)
    owns_mtdll_exports = MTDLL_PRODUCER_DEFINE in defines
    if owns_mtdll_exports != (module_name == MTDLL_MODULE_NAME):
        raise BuildFailure(
            f"{MTDLL_PRODUCER_DEFINE} must be defined by "
            f"{MTDLL_MODULE_NAME} and no other component"
        )
    if (
        MTDLL_PRIVATE_INCLUDE in include_directories
        and module_name != MTDLL_MODULE_NAME
    ):
        raise BuildFailure(
            "MTDLL's private include directory cannot be used by another component"
        )

    object_root = output.parent / f"obj/{name}"
    c_flags = _user_c_flags(
        pic=pic,
        executable=executable,
        defines=defines,
        include_directories=include_directories,
    )
    _fingerprint(object_root, [str(tools.clang), str(tools.nasm), name, *c_flags])
    header_time = _newest([
        *(ROOT / "usermode").rglob("*.h"),
        *(ROOT / "tests/usermode").rglob("*.h"),
        *(ROOT / "shared/include").rglob("*.h"),
    ])
    objects: list[Path] = []
    jobs: list[CompileJob] = []
    for relative in gas_sources:
        source = ROOT / relative
        obj = _object_path(object_root, source)
        objects.append(obj)
        if not _needs_rebuild(source, obj, header_time):
            continue
        obj.parent.mkdir(parents=True, exist_ok=True)
        # .S files are preprocessed assembly and consume the same ABI and
        # import/export marker definitions as their component's C sources.
        flags = c_flags
        jobs.append(
            CompileJob(
                f"CC {source.relative_to(ROOT)}",
                tuple([str(tools.clang), *flags, str(source), "-o", str(obj)]),
                ROOT,
            )
        )

    generated_root = output.parent / "generated" / name
    for relative in c_sources:
        original_source = ROOT / relative
        generated_source = (
            generated_root / Path(relative)
        ).with_suffix(".seh.c")
        try:
            translated = translate_file(
                original_source,
                generated_source,
                Path(relative).as_posix(),
            )
        except TranslationError as error:
            raise BuildFailure(str(error)) from error

        compile_source = generated_source if translated else original_source
        obj = _object_path(object_root, original_source)
        objects.append(obj)
        dependency_time = max(
            header_time,
            generated_source.stat().st_mtime if translated else 0.0,
        )
        if not _needs_rebuild(original_source, obj, dependency_time):
            continue
        obj.parent.mkdir(parents=True, exist_ok=True)
        jobs.append(
            CompileJob(
                f"CC {original_source.relative_to(ROOT)}",
                tuple(
                    [
                        str(tools.clang),
                        *c_flags,
                        str(compile_source),
                        "-o",
                        str(obj),
                    ]
                ),
                ROOT,
            )
        )
    _run_compile_jobs(jobs, workers)
    for relative in nasm_sources:
        source = ROOT / relative
        obj = _object_path(object_root, source)
        objects.append(obj)
        if _needs_rebuild(source, obj):
            obj.parent.mkdir(parents=True, exist_ok=True)
            _run([str(tools.nasm), "-f", "elf64", str(source), "-o", str(obj)])

    dependencies = dependencies or {}
    discovery_elf = object_root / f"{name}.discovery.elf"
    temporary_elf = object_root / f"{name}.elf"
    common_link_flags = [
        "--no-undefined",
        "-z",
        "now",
        "-z",
        "notext",
        "-z",
        "norelro",
        "-T",
        str(linker_script),
        "-m",
        "elf_x86_64",
    ]
    if pic:
        common_link_flags[0:0] = [
            "-shared",
            "-Bsymbolic",
            f"--soname={module_name}",
        ]
    else:
        common_link_flags[0:0] = ["-pie", "--no-dynamic-linker"]
    if entry_symbol:
        common_link_flags.append(f"--entry={entry_symbol}")

    dependency_files = list(dependencies.values())

    def link(elf: Path, metadata_size: int) -> None:
        _run(
            [
                str(tools.lld),
                *common_link_flags,
                f"--defsym=__mt_metadata_size=0x{metadata_size:x}",
                f"--Map={elf.with_suffix('.map')}",
                "-o",
                str(elf),
                *map(str, objects),
                *map(str, dependency_files),
            ]
        )

    # The first link discovers how many dynamic imports, exports, and base
    # relocations LLD emitted. The second link reserves exactly that much MTE
    # metadata before BSS so every runtime RVA remains stable.
    link(discovery_elf, 0x1000)
    packer = ROOT / "tools/mte/mte_pack.py"
    dependency_arguments = [
        argument
        for module, dependency in dependencies.items()
        for argument in ("--dependency", f"{module}={dependency}")
    ]
    result = _run(
        [
            sys.executable,
            str(packer),
            str(discovery_elf),
            "--print-metadata-size",
            *dependency_arguments,
        ],
        capture=True,
    )
    try:
        metadata_size = int((result.stdout or "").strip(), 0)
    except ValueError as exc:
        raise BuildFailure("MTE packer did not return a metadata size") from exc
    link(temporary_elf, metadata_size)
    _run(
        [
            sys.executable,
            str(packer),
            str(temporary_elf),
            str(output),
            "--metadata-size",
            hex(metadata_size),
            *dependency_arguments,
        ]
    )
    return output, temporary_elf


def build_usermode(
    tools: Tools,
    configuration: str,
    workers: int,
    stress_mode: str = "normal",
) -> tuple[Path, Path, list[tuple[Path, str]]]:
    output_directory = WINDOWS_BUILD / configuration.lower()
    output_directory.mkdir(parents=True, exist_ok=True)
    exception_test = stress_mode == "exception-chain"
    heap_test = stress_mode == "heap"
    loader_test = stress_mode == "loader"
    tls_test = stress_mode == "tls"
    process_test = stress_mode == "process"
    mtdll_sources = [
        *MTDLL_C,
        *(EXCEPTION_TEST_MTDLL_C if exception_test else ()),
        *(LOADER_TEST_MTDLL_C if loader_test else ()),
    ]
    mtdll_defines = [
        MTDLL_PRODUCER_DEFINE,
        *((EXCEPTION_TEST_DEFINE,) if exception_test else ()),
    ]
    mtdll_includes = [
        MTDLL_PRIVATE_INCLUDE,
        *((EXCEPTION_TEST_INCLUDE,) if exception_test else ()),
        *((LOADER_TEST_INCLUDE,) if loader_test else ()),
    ]
    mtdll_nasm_sources = [
        *MTDLL_NASM,
        *(EXCEPTION_TEST_MTDLL_NASM if exception_test else ()),
    ]
    mtdll, mtdll_elf = _build_user_component(
        tools,
        "mtdll",
        mtdll_sources,
        MTDLL_GAS,
        mtdll_nasm_sources,
        ROOT / "usermode/mtdll.ld",
        output_directory / "mtdll.mtdll",
        pic=True,
        executable=False,
        workers=workers,
        module_name=MTDLL_MODULE_NAME,
        defines=mtdll_defines,
        include_directories=mtdll_includes,
    )
    additional_files: list[tuple[Path, str]] = []
    tls_dll_elf: Path | None = None
    if loader_test:
        good_dll, _ = _build_user_component(
            tools,
            "loaderGoodDll",
            LOADER_GOOD_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "loaderGood.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="loaderGood.mtdll",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            defines=(LOADER_TEST_DLL_DEFINE,),
            include_directories=(LOADER_TEST_INCLUDE,),
            entry_symbol="DllMain",
        )
        fail_dll, _ = _build_user_component(
            tools,
            "loaderFailDll",
            LOADER_FAIL_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "loaderFail.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="loaderFail.mtdll",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            defines=(LOADER_TEST_DLL_DEFINE,),
            include_directories=(LOADER_TEST_INCLUDE,),
            entry_symbol="DllMain",
        )
        no_entry_dll, _ = _build_user_component(
            tools,
            "loaderNoEntryDll",
            LOADER_NO_ENTRY_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "loaderNoEntry.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="loaderNoEntry.mtdll",
            defines=(LOADER_TEST_DLL_DEFINE,),
            include_directories=(LOADER_TEST_INCLUDE,),
        )
        additional_files.extend([
            (good_dll, "loaderGood.mtdll"),
            (fail_dll, "loaderFail.mtdll"),
            (no_entry_dll, "NOENTRY.MTE"),
        ])

    if tls_test:
        tls_dll, tls_dll_elf = _build_user_component(
            tools,
            "loaderTlsDll",
            TLS_TEST_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "tlsFixture.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="tlsFixture.mtdll",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            defines=(TLS_TEST_DLL_DEFINE,),
            include_directories=(TLS_TEST_INCLUDE,),
        )
        additional_files.append((tls_dll, "tlsFixture.mtdll"))

        dynamic_tls_dll, _ = _build_user_component(
            tools,
            "loaderDynamicTlsDll",
            TLS_DYNAMIC_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "dynamicTls.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="dynamicTls.mtdll",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            defines=(TLS_TEST_DLL_DEFINE,),
            include_directories=(TLS_TEST_INCLUDE,),
            entry_symbol="DllMain",
        )
        additional_files.append((dynamic_tls_dll, "dynamicTls.mtdll"))

        fail_tls_dll, _ = _build_user_component(
            tools,
            "loaderFailTlsDll",
            TLS_FAIL_DYNAMIC_DLL_C,
            (),
            (),
            ROOT / "usermode/mtdll.ld",
            output_directory / "failTls.mtdll",
            pic=True,
            executable=False,
            workers=workers,
            module_name="failTls.mtdll",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            defines=(TLS_TEST_DLL_DEFINE,),
            include_directories=(TLS_TEST_INCLUDE,),
            entry_symbol="DllMain",
        )
        additional_files.append((fail_tls_dll, "FAILTLS.MTE"))

    if process_test:
        process_child, _ = _build_user_component(
            tools,
            "processChild",
            [*MTEXE_COMMON_C, *PROCESS_TEST_CHILD_C],
            MTEXE_GAS,
            MTEXE_NASM,
            ROOT / "usermode/mtexe.ld",
            output_directory / "processChild.mtexe",
            pic=False,
            executable=True,
            workers=workers,
            module_name="processChild.mtexe",
            dependencies={MTDLL_MODULE_NAME: mtdll_elf},
            include_directories=(PROCESS_TEST_INCLUDE,),
        )
        additional_files.append((process_child, "processChild.mtexe"))

        invalid_process = output_directory / "invalidProcess.mtexe"
        invalid_process.write_bytes(
            b"This is deliberately not an MTE image.".ljust(128, b"\0")
        )
        additional_files.append((invalid_process, "invalidProcess.mtexe"))

    if exception_test:
        program_name = "exceptionChainTest"
        program_sources = EXCEPTION_TEST_MTEXE_C
        program_includes = (EXCEPTION_TEST_INCLUDE,)
    elif heap_test:
        program_name = "heapTest"
        program_sources = HEAP_TEST_MTEXE_C
        program_includes = (HEAP_TEST_INCLUDE,)
    elif loader_test:
        program_name = "loaderTest"
        program_sources = LOADER_TEST_MTEXE_C
        program_includes = (LOADER_TEST_INCLUDE,)
    elif tls_test:
        program_name = "tlsTest"
        program_sources = TLS_TEST_MTEXE_C
        program_includes = (TLS_TEST_INCLUDE,)
    elif process_test:
        program_name = "processTest"
        program_sources = PROCESS_TEST_MTEXE_C
        program_includes = (PROCESS_TEST_INCLUDE,)
    else:
        program_name = "terminateMyself"
        program_sources = MTEXE_C
        program_includes = ()

    program_dependencies = {MTDLL_MODULE_NAME: mtdll_elf}
    if tls_dll_elf is not None:
        program_dependencies["tlsFixture.mtdll"] = tls_dll_elf

    program, _ = _build_user_component(
        tools,
        program_name,
        [*MTEXE_COMMON_C, *program_sources],
        MTEXE_GAS,
        MTEXE_NASM,
        ROOT / "usermode/mtexe.ld",
        output_directory / "terminateMyself.mtexe",
        pic=False,
        executable=True,
        workers=workers,
        module_name="terminateMyself.mtexe",
        dependencies=program_dependencies,
        include_directories=program_includes,
    )
    return mtdll, program, additional_files


def build_bootloader(tools: Tools, configuration: str, workers: int) -> Path:
    output_directory = WINDOWS_BUILD / configuration.lower()
    object_root = output_directory / "obj/bootloader"
    sources = [ROOT / "boot/bootloader_uefi.c", ROOT / "boot/uefi_runtime.c"]
    flags = [
        "--target=x86_64-pc-windows-msvc",
        "-std=gnu11",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-mno-red-zone",
        "-mno-stack-arg-probe",
        "-fshort-wchar",
        "-c",
        *_diagnostic_flags(),
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-unused-function",
        "-Wno-microsoft-enum-value",
        "-I",
        str(ROOT / "boot/Include"),
        "-I",
        str(ROOT / "boot/Include/X64"),
        "-O0" if configuration == "Debug" else "-O2",
    ]
    _fingerprint(object_root, [str(tools.clang), str(tools.lld_link), configuration, *flags])

    header_time = _newest((ROOT / "boot/Include").rglob("*.h"))
    objects: list[Path] = []
    jobs: list[CompileJob] = []
    for source in sources:
        output = object_root / f"{source.stem}.obj"
        objects.append(output)
        if not _needs_rebuild(source, output, header_time):
            continue
        jobs.append(
            CompileJob(
                f"EFI CC {source.relative_to(ROOT)}",
                tuple([str(tools.clang), *flags, str(source), "-o", str(output)]),
                ROOT,
            )
        )
    _run_compile_jobs(jobs, workers)

    bootloader = output_directory / "bootloader.efi"
    link_time = max(path.stat().st_mtime for path in objects)
    if not bootloader.is_file() or bootloader.stat().st_mtime < link_time:
        _run(
            [
                str(tools.lld_link),
                "/nologo",
                "/subsystem:efi_application",
                "/entry:EfiMain",
                "/nodefaultlib",
                "/machine:x64",
                "/base:0",
                "/timestamp:0",
                f"/out:{bootloader}",
                *map(str, objects),
            ]
        )
    print(f"[BOOTLOADER] {bootloader}")
    return bootloader


def build_image(
    configuration: str,
    bootloader: Path,
    kernel: Path,
    mtdll: Path,
    program: Path,
    additional_files: Sequence[tuple[Path, str]] = (),
) -> Path:
    output_directory = WINDOWS_BUILD / configuration.lower()
    image = output_directory / "matanelos.img"
    create_image(
        image,
        bootloader,
        kernel,
        mtdll,
        program,
        additional_files=additional_files,
    )
    print(f"[IMAGE] {image}")
    return image


def run_qemu(tools: Tools, image: Path, cpus: int) -> None:
    if tools.qemu is None:
        raise BuildFailure("qemu-system-x86_64.exe was not found. Set QEMU_BIN or run bootstrap.bat.")
    ovmf_directory = Path(os.environ.get("MATANELOS_OVMF", str(Path.home() / "Desktop/OSBuild/uefiboot")))
    code = ovmf_directory / "OVMF_CODE.fd"
    vars_template = ovmf_directory / "OVMF_VARS.fd"
    if not code.is_file() or not vars_template.is_file():
        raise BuildFailure(f"OVMF_CODE.fd and OVMF_VARS.fd were not found in {ovmf_directory}")
    runtime_vars = image.parent / "OVMF_VARS.fd"
    shutil.copy2(vars_template, runtime_vars)
    qemu_log = image.parent / "qemu_io.log"
    _run(
        [
            str(tools.qemu),
            "-machine",
            "q35",
            "-m",
            "256M",
            "-cpu",
            "max",
            "-smp",
            f"{cpus},sockets=1,cores={cpus},threads=1",
            "-d",
            "int,cpu_reset,guest_errors",
            "-D",
            str(qemu_log),
            "-drive",
            f"file={code},if=pflash,format=raw,readonly=on",
            "-drive",
            f"file={runtime_vars},if=pflash,format=raw",
            "-device",
            "ich9-ahci,id=ahci",
            "-drive",
            f"id=ESP,if=none,format=raw,file={image}",
            "-device",
            "ide-hd,drive=ESP,bus=ahci.0",
            "-global",
            "isa-debugcon.iobase=0x402",
            "-net",
            "none",
        ]
    )


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "clean", "run"], nargs="?", default="build")
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Debug")
    parser.add_argument(
        "--target",
        choices=["all", "bootloader", "kernel", "usermode", "image"],
        default="all",
    )
    parser.add_argument("--gdb", action="store_true")
    parser.add_argument("--cpus", type=int, choices=range(1, 65), default=4, metavar="1-64")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument(
        "--stress-mode",
        choices=sorted(STRESS_MODES),
        default="normal",
        help="select an isolated kernel stress mode (default: normal)",
    )
    parser.add_argument(
        "--stress-duration-seconds",
        type=int,
        default=1800,
        metavar="SECONDS",
        help="duration of the randomized stress mode (default: 1800)",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    if args.stress_duration_seconds <= 0:
        print("BUILD ERROR: --stress-duration-seconds must be positive", file=sys.stderr)
        return 1

    if args.action == "clean":
        build_root = (ROOT / "build").resolve()
        if ROOT.resolve() not in build_root.parents:
            raise BuildFailure(f"Refusing to clean outside the workspace: {build_root}")
        shutil.rmtree(build_root, ignore_errors=True)
        print("[CLEAN] Removed build directory")
        return 0

    try:
        tools = discover_tools()
        output_directory = WINDOWS_BUILD / args.configuration.lower()
        kernel = output_directory / "kernel.elf"
        mtdll = output_directory / "mtdll.mtdll"
        program = output_directory / "terminateMyself.mtexe"
        bootloader = output_directory / "bootloader.efi"
        additional_files: list[tuple[Path, str]] = []
        if args.stress_mode == "loader":
            additional_files = [
                (output_directory / "loaderGood.mtdll", "loaderGood.mtdll"),
                (output_directory / "loaderFail.mtdll", "loaderFail.mtdll"),
                (output_directory / "loaderNoEntry.mtdll", "NOENTRY.MTE"),
            ]
        elif args.stress_mode == "tls":
            additional_files = [
                (output_directory / "tlsFixture.mtdll", "tlsFixture.mtdll"),
                (output_directory / "dynamicTls.mtdll", "dynamicTls.mtdll"),
                (output_directory / "failTls.mtdll", "FAILTLS.MTE"),
            ]
        elif args.stress_mode == "process":
            additional_files = [
                (output_directory / "processChild.mtexe", "processChild.mtexe"),
                (output_directory / "invalidProcess.mtexe", "invalidProcess.mtexe"),
            ]

        if args.target in {"all", "bootloader", "image"}:
            bootloader = build_bootloader(tools, args.configuration, args.jobs)
        if args.target in {"all", "kernel"}:
            kernel, _ = build_kernel(
                tools,
                args.configuration,
                args.gdb,
                args.jobs,
                args.stress_mode,
                args.stress_duration_seconds,
            )
        if args.target in {"all", "usermode"}:
            mtdll, program, additional_files = build_usermode(
                tools,
                args.configuration,
                args.jobs,
                args.stress_mode,
            )
        if args.target in {"all", "image"}:
            image = build_image(
                args.configuration,
                bootloader,
                kernel,
                mtdll,
                program,
                additional_files,
            )
        else:
            image = output_directory / "matanelos.img"
        if args.action == "run":
            if not image.is_file():
                raise BuildFailure(f"Build the image first: {image}")
            run_qemu(tools, image, args.cpus)
        return 0
    except (BuildFailure, FileNotFoundError, RuntimeError) as exc:
        print(f"BUILD ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
