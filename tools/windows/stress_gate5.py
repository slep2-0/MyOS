#!/usr/bin/env python3
"""Run the unattended Gate 5 cold-boot and randomized stress campaigns."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from build import BuildFailure, ROOT, discover_tools


BUILD_DRIVER = ROOT / "tools" / "windows" / "build.py"
BUILD_ROOT = ROOT / "build" / "windows"
PASS_EXIT_CODE = (0x10 << 1) | 1


def _parse_cpu_list(value: str) -> list[int]:
    result: list[int] = []
    for item in value.split(","):
        try:
            cpu_count = int(item)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid CPU count: {item}") from exc
        if cpu_count < 1 or cpu_count > 64:
            raise argparse.ArgumentTypeError("CPU counts must be from 1 through 64")
        if cpu_count not in result:
            result.append(cpu_count)
    if not result:
        raise argparse.ArgumentTypeError("at least one CPU count is required")
    return result


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--phase",
        choices=["all", "full-suite", "cold-boot", "randomized"],
        default="all",
    )
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--cpus", type=_parse_cpu_list, default=[1, 4], metavar="LIST")
    parser.add_argument("--boot-count", type=int, default=100)
    parser.add_argument("--duration-seconds", type=int, default=1800)
    parser.add_argument("--cold-timeout-seconds", type=int, default=60)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument(
        "--leave-stress-image",
        action="store_true",
        help="do not restore the ordinary kernel image after a successful run",
    )
    args = parser.parse_args()
    if args.boot_count <= 0:
        parser.error("--boot-count must be positive")
    if args.duration_seconds <= 0:
        parser.error("--duration-seconds must be positive")
    if args.cold_timeout_seconds <= 0:
        parser.error("--cold-timeout-seconds must be positive")
    return args


def _build(configuration: str, mode: str, duration_seconds: int, jobs: int) -> Path:
    command = [
        sys.executable,
        str(BUILD_DRIVER),
        "build",
        "--configuration",
        configuration,
        "--stress-mode",
        mode,
        "--stress-duration-seconds",
        str(duration_seconds),
        "--jobs",
        str(jobs),
    ]
    result = subprocess.run(command, cwd=ROOT, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{mode} build failed with exit code {result.returncode}")

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
    mode: str,
    cpu_count: int,
    run_number: int,
    timeout_seconds: int,
    extra_qemu_args: list[str] | None = None,
    abort_event: threading.Event | None = None,
) -> tuple[float, str]:
    output_directory.mkdir(parents=True, exist_ok=True)
    stem = f"{mode}_{cpu_count}cpu_{run_number:03d}"
    debug_log = output_directory / f"{stem}.debug.txt"
    qemu_output = output_directory / f"{stem}.qemu.txt"
    runtime_variables = output_directory / f"{stem}.vars.fd"
    shutil.copy2(variables_template, runtime_variables)

    command = [
        str(qemu),
        "-machine",
        "q35",
        "-m",
        "256M",
        "-cpu",
        "max",
        "-smp",
        f"{cpu_count},sockets=1,cores={cpu_count},threads=1",
        "-display",
        "none",
        "-serial",
        "none",
        "-monitor",
        "none",
        "-no-reboot",
        "-drive",
        f"file={code},if=pflash,format=raw,readonly=on",
        "-drive",
        f"file={runtime_variables},if=pflash,format=raw",
        "-device",
        "ich9-ahci,id=ahci",
        "-drive",
        f"id=ESP,if=none,format=raw,file={image}",
        "-device",
        "ide-hd,drive=ESP,bus=ahci.0",
        "-device",
        "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-debugcon",
        f"file:{debug_log}",
        "-global",
        "isa-debugcon.iobase=0x402",
        "-net",
        "none",
    ]

    if extra_qemu_args:
        command.extend(extra_qemu_args)

    started = time.monotonic()
    process: subprocess.Popen[str] | None = None
    try:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        while True:
            elapsed = time.monotonic() - started
            if abort_event is not None and abort_event.is_set():
                process.kill()
                output, _ = process.communicate()
                qemu_output.write_text(output or "", encoding="utf-8")
                raise RuntimeError(
                    f"{mode} {cpu_count}-CPU run {run_number} was stopped "
                    f"after stall diagnostics at {elapsed:.1f}s; see "
                    f"{debug_log} and {qemu_output}"
                )
            if elapsed >= timeout_seconds:
                process.kill()
                output, _ = process.communicate()
                qemu_output.write_text(output or "", encoding="utf-8")
                raise RuntimeError(
                    f"{mode} {cpu_count}-CPU run {run_number} timed out after "
                    f"{elapsed:.1f}s; see {debug_log} and {qemu_output}"
                )

            try:
                output, _ = process.communicate(timeout=0.25)
                break
            except subprocess.TimeoutExpired:
                continue
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.communicate()
        runtime_variables.unlink(missing_ok=True)

    elapsed = time.monotonic() - started
    qemu_output.write_text(output or "", encoding="utf-8")
    debug_text = debug_log.read_text(encoding="utf-8", errors="replace") \
        if debug_log.is_file() else ""
    expected_markers = {
        "full-suite": "MT-STRESS PASS FULL-SUITE",
        "cold-boot": "MT-STRESS PASS COLD-BOOT",
        "randomized": "MT-STRESS PASS RANDOMIZED",
        "exception-chain": "MT-STRESS PASS EXCEPTION-CHAIN",
    }
    expected_marker = expected_markers[mode]

    if process.returncode != PASS_EXIT_CODE or expected_marker not in debug_text:
        raise RuntimeError(
            f"{mode} {cpu_count}-CPU run {run_number} failed: "
            f"QEMU exit={process.returncode}, marker={expected_marker in debug_text}; "
            f"see {debug_log} and {qemu_output}"
        )

    qemu_output.unlink(missing_ok=True)
    debug_log.unlink(missing_ok=True)
    return elapsed, expected_marker


def _record(result_file: Path, message: str) -> None:
    with result_file.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write(message + "\n")

    # The runner may outlive the terminal that launched it (for example, if
    # Codex or a shell crashes during a long soak). Preserve the result and
    # continue finalization even when that terminal's output pipe is gone.
    try:
        print(message, flush=True)
    except (BrokenPipeError, OSError):
        try:
            sys.stdout = open(os.devnull, "w", encoding="utf-8")
        except OSError:
            pass


def _run_cold_boots(
    *,
    qemu: Path,
    image: Path,
    code: Path,
    variables: Path,
    output_directory: Path,
    result_file: Path,
    cpu_counts: list[int],
    boot_count: int,
    timeout_seconds: int,
) -> None:
    for cpu_count in cpu_counts:
        campaign_started = time.monotonic()
        for run_number in range(1, boot_count + 1):
            elapsed, _ = _run_one(
                qemu=qemu,
                image=image,
                code=code,
                variables_template=variables,
                output_directory=output_directory,
                mode="cold-boot",
                cpu_count=cpu_count,
                run_number=run_number,
                timeout_seconds=timeout_seconds,
            )
            if run_number == 1 or run_number % 10 == 0 or run_number == boot_count:
                _record(
                    result_file,
                    f"COLD-BOOT {cpu_count} CPU: {run_number}/{boot_count} PASS "
                    f"(last {elapsed:.2f}s)",
                )
        _record(
            result_file,
            f"COLD-BOOT {cpu_count} CPU: ALL {boot_count} PASS "
            f"({time.monotonic() - campaign_started:.1f}s)",
        )


def _run_full_suite(
    *,
    qemu: Path,
    image: Path,
    code: Path,
    variables: Path,
    output_directory: Path,
    result_file: Path,
    cpu_counts: list[int],
) -> None:
    for cpu_count in cpu_counts:
        elapsed, _ = _run_one(
            qemu=qemu,
            image=image,
            code=code,
            variables_template=variables,
            output_directory=output_directory,
            mode="full-suite",
            cpu_count=cpu_count,
            run_number=1,
            timeout_seconds=900,
        )
        _record(
            result_file,
            f"FULL-SUITE {cpu_count} CPU: PASS ({elapsed:.1f}s wall time)",
        )


def _run_randomized(
    *,
    qemu: Path,
    image: Path,
    code: Path,
    variables: Path,
    output_directory: Path,
    result_file: Path,
    cpu_counts: list[int],
    duration_seconds: int,
) -> None:
    for cpu_count in cpu_counts:
        elapsed, _ = _run_one(
            qemu=qemu,
            image=image,
            code=code,
            variables_template=variables,
            output_directory=output_directory,
            mode="randomized",
            cpu_count=cpu_count,
            run_number=1,
            timeout_seconds=duration_seconds + 300,
        )
        _record(
            result_file,
            f"RANDOMIZED {cpu_count} CPU: {duration_seconds}s PASS "
            f"({elapsed:.1f}s wall time)",
        )


def main() -> int:
    args = _parse_args()
    tools = discover_tools()
    if tools.qemu is None:
        raise BuildFailure(
            "qemu-system-x86_64.exe was not found; set QEMU_BIN or run bootstrap.bat"
        )

    code, variables = _ovmf_paths()
    output_directory = BUILD_ROOT / "gate5"
    output_directory.mkdir(parents=True, exist_ok=True)
    result_file = output_directory / "results.txt"
    result_file.write_text(
        f"Gate 5 started {time.strftime('%Y-%m-%d %H:%M:%S')}\n",
        encoding="utf-8",
    )

    completed = False
    try:
        if args.phase in {"all", "full-suite"}:
            image = _build(
                args.configuration,
                "full-suite",
                args.duration_seconds,
                args.jobs,
            )
            _run_full_suite(
                qemu=tools.qemu,
                image=image,
                code=code,
                variables=variables,
                output_directory=output_directory,
                result_file=result_file,
                cpu_counts=args.cpus,
            )

        if args.phase in {"all", "cold-boot"}:
            image = _build(
                args.configuration,
                "cold-boot",
                args.duration_seconds,
                args.jobs,
            )
            _run_cold_boots(
                qemu=tools.qemu,
                image=image,
                code=code,
                variables=variables,
                output_directory=output_directory,
                result_file=result_file,
                cpu_counts=args.cpus,
                boot_count=args.boot_count,
                timeout_seconds=args.cold_timeout_seconds,
            )

        if args.phase in {"all", "randomized"}:
            image = _build(
                args.configuration,
                "randomized",
                args.duration_seconds,
                args.jobs,
            )
            _run_randomized(
                qemu=tools.qemu,
                image=image,
                code=code,
                variables=variables,
                output_directory=output_directory,
                result_file=result_file,
                cpu_counts=args.cpus,
                duration_seconds=args.duration_seconds,
            )

        _record(result_file, "GATE 5 ALL REQUESTED CAMPAIGNS PASS")
        completed = True
        return 0
    except (BuildFailure, OSError, RuntimeError) as exc:
        _record(result_file, f"GATE 5 FAILURE: {exc}")
        return 1
    finally:
        if completed and not args.leave_stress_image:
            _record(result_file, "Restoring ordinary build mode")
            restore = subprocess.run(
                [
                    sys.executable,
                    str(BUILD_DRIVER),
                    "build",
                    "--configuration",
                    args.configuration,
                    "--stress-mode",
                    "normal",
                    "--jobs",
                    str(args.jobs),
                ],
                cwd=ROOT,
                check=False,
            )
            if restore.returncode != 0:
                _record(
                    result_file,
                    f"WARNING: ordinary build restore failed with exit "
                    f"code {restore.returncode}",
                )


if __name__ == "__main__":
    raise SystemExit(main())
