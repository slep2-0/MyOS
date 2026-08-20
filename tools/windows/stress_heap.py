#!/usr/bin/env python3
"""Build and run the isolated user-heap runtime gate."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from build import ROOT, discover_tools


BUILD_DRIVER = ROOT / "tools" / "windows" / "build.py"
BUILD_ROOT = ROOT / "build" / "windows"
PASS_EXIT_CODE = (0x10 << 1) | 1


def _parse_cpu_list(value: str) -> list[int]:
    counts: list[int] = []
    for item in value.split(","):
        try:
            count = int(item)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid CPU count: {item}") from exc
        if count < 1 or count > 64:
            raise argparse.ArgumentTypeError("CPU counts must be from 1 through 64")
        if count not in counts:
            counts.append(count)
    return counts


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--cpus", type=_parse_cpu_list, default=[1, 4], metavar="LIST")
    parser.add_argument("--timeout-seconds", type=int, default=120)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--trace",
        action="store_true",
        help="enable QEMU interrupt and exception tracing for diagnosis",
    )
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    return args


def _build(configuration: str, jobs: int) -> Path:
    result = subprocess.run(
        [
            sys.executable,
            str(BUILD_DRIVER),
            "build",
            "--configuration",
            configuration,
            "--stress-mode",
            "heap",
            "--jobs",
            str(jobs),
        ],
        cwd=ROOT,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"heap build failed with exit code {result.returncode}")

    image = BUILD_ROOT / configuration.lower() / "matanelos.img"
    if not image.is_file():
        raise RuntimeError(f"build did not produce {image}")
    return image


def _ovmf_paths() -> tuple[Path, Path]:
    directory = Path(
        os.environ.get(
            "MATANELOS_OVMF",
            str(Path.home() / "Desktop" / "OSBuild" / "uefiboot"),
        )
    )
    code = directory / "OVMF_CODE.fd"
    variables = directory / "OVMF_VARS.fd"
    if not code.is_file() or not variables.is_file():
        raise RuntimeError(f"OVMF firmware was not found in {directory}")
    return code, variables


def _run_one(
    *,
    qemu: Path,
    image: Path,
    code: Path,
    variables_template: Path,
    output_directory: Path,
    cpu_count: int,
    timeout_seconds: int,
    trace: bool,
) -> float:
    output_directory.mkdir(parents=True, exist_ok=True)
    stem = f"heap_{cpu_count}cpu"
    debug_log = output_directory / f"{stem}.debug.txt"
    qemu_output = output_directory / f"{stem}.qemu.txt"
    qemu_trace = output_directory / f"{stem}.trace.txt"
    runtime_variables = output_directory / f"{stem}.vars.fd"
    debug_log.unlink(missing_ok=True)
    qemu_output.unlink(missing_ok=True)
    qemu_trace.unlink(missing_ok=True)
    shutil.copy2(variables_template, runtime_variables)

    command = [
        str(qemu),
        "-machine", "q35",
        "-m", "256M",
        "-cpu", "max",
        "-smp", f"{cpu_count},sockets=1,cores={cpu_count},threads=1",
        "-display", "none",
        "-serial", "none",
        "-monitor", "none",
        "-no-reboot",
        "-drive", f"file={code},if=pflash,format=raw,readonly=on",
        "-drive", f"file={runtime_variables},if=pflash,format=raw",
        "-device", "ich9-ahci,id=ahci",
        "-drive", f"id=ESP,if=none,format=raw,file={image}",
        "-device", "ide-hd,drive=ESP,bus=ahci.0",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-debugcon", f"file:{debug_log}",
        "-global", "isa-debugcon.iobase=0x402",
        "-net", "none",
    ]
    if trace:
        command.extend(["-d", "int,cpu_reset,guest_errors", "-D", str(qemu_trace)])

    started = time.monotonic()
    try:
        result = subprocess.run(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        qemu_output.write_text(exc.stdout or "", encoding="utf-8")
        raise RuntimeError(
            f"heap {cpu_count}-CPU run timed out after {timeout_seconds}s; "
            f"see {debug_log} and {qemu_output}"
        ) from exc
    finally:
        runtime_variables.unlink(missing_ok=True)

    elapsed = time.monotonic() - started
    qemu_output.write_text(result.stdout or "", encoding="utf-8")
    debug_text = debug_log.read_text(encoding="utf-8", errors="replace") \
        if debug_log.is_file() else ""
    user_pass = "MT-HEAP USER PASS" in debug_text
    final_pass = "MT-STRESS PASS HEAP" in debug_text
    if result.returncode != PASS_EXIT_CODE or not user_pass or not final_pass:
        markers = [
            line for line in debug_text.splitlines()
            if line.startswith("MT-HEAP") or line.startswith("MT-STRESS")
        ]
        marker_text = markers[-1] if markers else "no heap marker"
        trace_text = f", and {qemu_trace}" if trace else ""
        raise RuntimeError(
            f"heap {cpu_count}-CPU run failed: QEMU exit={result.returncode}, "
            f"last marker={marker_text}; see {debug_log}, {qemu_output}"
            f"{trace_text}"
        )

    return elapsed


def main() -> int:
    args = _parse_args()
    try:
        tools = discover_tools()
        if tools.qemu is None:
            raise RuntimeError("qemu-system-x86_64.exe was not found")

        image = BUILD_ROOT / args.configuration.lower() / "matanelos.img"
        if not args.skip_build:
            image = _build(args.configuration, args.jobs)
        elif not image.is_file():
            raise RuntimeError(f"heap image was not found at {image}")

        code, variables = _ovmf_paths()
        output_directory = BUILD_ROOT / "heap-test"
        result_file = output_directory / "results.txt"
        output_directory.mkdir(parents=True, exist_ok=True)
        result_file.write_text("", encoding="utf-8")

        for cpu_count in args.cpus:
            elapsed = _run_one(
                qemu=tools.qemu,
                image=image,
                code=code,
                variables_template=variables,
                output_directory=output_directory,
                cpu_count=cpu_count,
                timeout_seconds=args.timeout_seconds,
                trace=args.trace,
            )
            message = f"PASS heap {cpu_count} CPU(s) in {elapsed:.2f}s"
            with result_file.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(message + "\n")
            print(message, flush=True)

        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"HEAP TEST ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
