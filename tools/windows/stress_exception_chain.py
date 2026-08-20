#!/usr/bin/env python3
"""Build and run the isolated MTDLL exception-chain suite."""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

from build import BuildFailure, ROOT, discover_tools
from stress_gate5 import _build, _ovmf_paths, _record, _run_one


BUILD_DRIVER = ROOT / "tools" / "windows" / "build.py"
BUILD_ROOT = ROOT / "build" / "windows"


def _parse_cpu_list(value: str) -> list[int]:
    result: list[int] = []
    for item in value.split(","):
        try:
            count = int(item)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid CPU count: {item}") from exc
        if count < 1 or count > 64:
            raise argparse.ArgumentTypeError("CPU counts must be from 1 through 64")
        if count not in result:
            result.append(count)
    if not result:
        raise argparse.ArgumentTypeError("at least one CPU count is required")
    return result


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--cpus", type=_parse_cpu_list, default=[1, 4], metavar="LIST")
    parser.add_argument("--boot-count", type=int, default=1)
    parser.add_argument("--duration-seconds", type=int, default=1)
    parser.add_argument("--timeout-seconds", type=int, default=120)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument("--leave-test-image", action="store_true")
    parser.add_argument(
        "--stall-diagnostics-seconds",
        type=int,
        default=15,
        help=(
            "stream guest progress and capture GDB state after this many "
            "seconds without a debug-port marker; use 0 to disable"
        ),
    )
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    if args.boot_count <= 0:
        parser.error("--boot-count must be positive")
    if args.duration_seconds <= 0:
        parser.error("--duration-seconds must be positive")
    if args.stall_diagnostics_seconds < 0:
        parser.error("--stall-diagnostics-seconds cannot be negative")
    return args


def _reserve_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def _capture_gdb_state(
    *,
    port: int,
    kernel: Path,
    output: Path,
) -> None:
    gdb = shutil.which("gdb")
    if not gdb:
        output.write_text("gdb was not found in PATH\n", encoding="utf-8")
        return

    commands = [
        "set pagination off",
        "set confirm off",
        "set architecture i386:x86-64",
        "set osabi none",
        f"target remote 127.0.0.1:{port}",
        "info registers",
        "x/24i $rip-32",
        "bt",
        "thread apply all bt",
        "p/x MeSystemTickCount",
        "p/x StressExceptionChainStage",
        "p/x StressExceptionChainWatchdogActive",
        "p/x cpus[0].self",
        "p/x cpus[0].self->currentIrql",
        "p/x cpus[0].self->schedulePending",
        "p/x cpus[0].self->currentThread",
        "p/x cpus[0].self->readyQueue.head",
        "p/x cpus[0].self->readyQueue.tail",
        "p/x cpus[0].self->DpcInterruptRequested",
        "p/x cpus[0].self->DpcRoutineActive",
        "detach",
        "quit",
    ]
    command = [gdb, "-q", "-nx", "-batch", str(kernel)]
    for expression in commands:
        command.extend(["-ex", expression])

    result = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        check=False,
    )
    output.write_text(result.stdout or "", encoding="utf-8")


def _stream_and_watch_debug_log(
    *,
    debug_log: Path,
    stop: threading.Event,
    stall_seconds: int,
    gdb_port: int,
    kernel: Path,
    gdb_output: Path,
    abort_run: threading.Event,
) -> None:
    offset = 0
    campaign_started = False
    last_marker = time.monotonic()
    diagnosed = False

    while not stop.wait(0.25):
        if debug_log.is_file():
            with debug_log.open("r", encoding="utf-8", errors="replace") as stream:
                stream.seek(offset)
                data = stream.read()
                offset = stream.tell()

            if data:
                last_marker = time.monotonic()
                for line in data.splitlines():
                    print(f"[GUEST] {line}", flush=True)
                    if line == "MT-EXCEPTION CAMPAIGN START":
                        campaign_started = True

        if (
            stall_seconds > 0
            and campaign_started
            and not diagnosed
            and time.monotonic() - last_marker >= stall_seconds
        ):
            diagnosed = True
            print(
                f"[DIAGNOSTIC] No guest marker for {stall_seconds}s; "
                f"capturing GDB state in {gdb_output}",
                flush=True,
            )
            try:
                _capture_gdb_state(
                    port=gdb_port,
                    kernel=kernel,
                    output=gdb_output,
                )
            except (OSError, subprocess.SubprocessError) as exc:
                gdb_output.write_text(
                    f"GDB capture failed: {exc}\n",
                    encoding="utf-8",
                )
            abort_run.set()
            return


def main() -> int:
    args = _parse_args()
    tools = discover_tools()
    if tools.qemu is None:
        raise BuildFailure(
            "qemu-system-x86_64.exe was not found; set QEMU_BIN or run bootstrap.bat"
        )

    code, variables = _ovmf_paths()
    output_directory = BUILD_ROOT / "exception-chain"
    output_directory.mkdir(parents=True, exist_ok=True)
    result_file = output_directory / "results.txt"
    result_file.write_text(
        f"Exception-chain suite started {time.strftime('%Y-%m-%d %H:%M:%S')}\n",
        encoding="utf-8",
    )

    completed = False
    try:
        image = _build(
            args.configuration,
            "exception-chain",
            args.duration_seconds,
            args.jobs,
        )
        for cpu_count in args.cpus:
            campaign_started = time.monotonic()
            for run_number in range(1, args.boot_count + 1):
                stem = f"exception-chain_{cpu_count}cpu_{run_number:03d}"
                debug_log = output_directory / f"{stem}.debug.txt"
                gdb_output = output_directory / f"{stem}.gdb.txt"
                debug_log.unlink(missing_ok=True)
                gdb_output.unlink(missing_ok=True)

                gdb_port = _reserve_tcp_port()
                stop_stream = threading.Event()
                abort_run = threading.Event()
                stream_thread = threading.Thread(
                    target=_stream_and_watch_debug_log,
                    kwargs={
                        "debug_log": debug_log,
                        "stop": stop_stream,
                        "stall_seconds": args.stall_diagnostics_seconds,
                        "gdb_port": gdb_port,
                        "kernel": BUILD_ROOT
                        / args.configuration.lower()
                        / "kernel.elf",
                        "gdb_output": gdb_output,
                        "abort_run": abort_run,
                    },
                    daemon=True,
                )
                stream_thread.start()

                try:
                    elapsed, _ = _run_one(
                        qemu=tools.qemu,
                        image=image,
                        code=code,
                        variables_template=variables,
                        output_directory=output_directory,
                        mode="exception-chain",
                        cpu_count=cpu_count,
                        run_number=run_number,
                        timeout_seconds=max(
                            args.timeout_seconds,
                            args.duration_seconds * 3 // 2 + 600,
                        ),
                        extra_qemu_args=[
                            "-gdb",
                            f"tcp:127.0.0.1:{gdb_port}",
                        ],
                        abort_event=abort_run,
                    )
                finally:
                    stop_stream.set()
                    stream_thread.join(timeout=2)
                if (
                    run_number == 1
                    or run_number % 10 == 0
                    or run_number == args.boot_count
                ):
                    _record(
                        result_file,
                        f"EXCEPTION-CHAIN {cpu_count} CPU: "
                        f"{run_number}/{args.boot_count} PASS "
                        f"(last {elapsed:.1f}s)",
                    )
            _record(
                result_file,
                f"EXCEPTION-CHAIN {cpu_count} CPU: ALL "
                f"{args.boot_count} PASS "
                f"({time.monotonic() - campaign_started:.1f}s)",
            )

        _record(result_file, "EXCEPTION-CHAIN ALL REQUESTED RUNS PASS")
        completed = True
        return 0
    except (BuildFailure, OSError, RuntimeError) as exc:
        _record(result_file, f"EXCEPTION-CHAIN FAILURE: {exc}")
        return 1
    finally:
        if completed and not args.leave_test_image:
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
                    f"WARNING: ordinary build restore failed with exit code "
                    f"{restore.returncode}",
                )


if __name__ == "__main__":
    raise SystemExit(main())
