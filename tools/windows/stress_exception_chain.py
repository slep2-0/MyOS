#!/usr/bin/env python3
"""Build and run the isolated MTDLL exception-chain suite."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

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
    parser.add_argument("--timeout-seconds", type=int, default=120)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument("--leave-test-image", action="store_true")
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    return args


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
            1,
            args.jobs,
        )
        for cpu_count in args.cpus:
            elapsed, _ = _run_one(
                qemu=tools.qemu,
                image=image,
                code=code,
                variables_template=variables,
                output_directory=output_directory,
                mode="exception-chain",
                cpu_count=cpu_count,
                run_number=1,
                timeout_seconds=args.timeout_seconds,
            )
            _record(
                result_file,
                f"EXCEPTION-CHAIN {cpu_count} CPU: PASS ({elapsed:.1f}s wall time)",
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
