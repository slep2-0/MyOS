#!/usr/bin/env python3
"""Remove stale Visual Studio C/C++ browsing databases for this solution."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _visual_studio_is_running() -> bool:
    result = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq devenv.exe", "/FO", "CSV", "/NH"],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return '"devenv.exe"' in result.stdout.lower()


def main() -> int:
    if _visual_studio_is_running():
        print("Close every Visual Studio window before resetting IntelliSense.", file=sys.stderr)
        return 1

    cache_root = (ROOT / ".vs").resolve()
    if ROOT.resolve() not in cache_root.parents:
        print(f"Refusing to clean outside the workspace: {cache_root}", file=sys.stderr)
        return 1

    removed = cache_root.is_dir()
    if removed:
        shutil.rmtree(cache_root)

    print(f"Removed Visual Studio cache: {'yes' if removed else 'already clean'}")
    print("Reopen KernelDevelopment.sln and allow Visual Studio to rescan the project.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
