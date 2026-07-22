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

    database_root = (ROOT / ".vs/KernelDevelopment").resolve()
    if ROOT.resolve() not in database_root.parents:
        print(f"Refusing to clean outside the workspace: {database_root}", file=sys.stderr)
        return 1

    removed = 0
    for version in ("v17", "v18"):
        directory = database_root / version
        ipch = directory / "ipch"
        if ipch.is_dir():
            shutil.rmtree(ipch)
            removed += 1
        for name in ("Browse.VC.db", "Solution.VC.db"):
            database = directory / name
            if database.is_file():
                database.unlink()
                removed += 1

    print(f"Removed {removed} stale IntelliSense cache item(s).")
    print("Reopen KernelDevelopment.sln and allow Visual Studio to rescan the project.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
