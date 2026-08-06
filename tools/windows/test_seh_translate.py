#!/usr/bin/env python3
"""Unit and Clang syntax tests for the MatanelOS SEH source translator."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

from seh_translate import TranslationError, translate_text


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "tests/fixtures"
OUTPUT = ROOT / "build/windows/translator-tests"


def _clang() -> Path:
    candidates = [
        Path(os.environ["LLVM_BIN"]) / "clang.exe"
        if os.environ.get("LLVM_BIN")
        else None,
        Path(os.environ.get("ProgramFiles", "C:/Program Files"))
        / "LLVM/bin/clang.exe",
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    raise RuntimeError("clang.exe was not found; run initial_setup.bat")


class SehTranslatorTests(unittest.TestCase):
    def test_nested_and_multiline_filter(self) -> None:
        source = (FIXTURES / "seh_valid.c").read_text(encoding="utf-8")
        generated, changed = translate_text(
            source,
            "tools/windows/tests/fixtures/seh_valid.c",
        )
        self.assertTrue(changed)
        self.assertEqual(generated.count("MT_LANGUAGE_FRAME __mt_seh_frame_"), 2)
        self.assertIn("__mt_seh_frame_2.ExceptionRecord.ExceptionCode", generated)
        self.assertIn("__mt_seh_frame_1.ExceptionPointers", generated)
        self.assertNotIn("GetExceptionCode()", generated)
        self.assertNotIn("GetExceptionInformation()", generated)

    def test_comments_strings_and_directives_are_ignored(self) -> None:
        source = (FIXTURES / "seh_lexical.c").read_text(encoding="utf-8")
        generated, changed = translate_text(source, "seh_lexical.c")
        self.assertFalse(changed)
        self.assertEqual(generated, source)

    def test_malformed_construct_reports_original_location(self) -> None:
        source = (FIXTURES / "seh_malformed.c").read_text(encoding="utf-8")
        with self.assertRaisesRegex(
            TranslationError,
            r"seh_malformed\.c:\d+:\d+: expected __except",
        ):
            translate_text(source, "seh_malformed.c")

    def test_generated_source_compiles(self) -> None:
        source = (FIXTURES / "seh_valid.c").read_text(encoding="utf-8")
        generated, changed = translate_text(
            source,
            "tools/windows/tests/fixtures/seh_valid.c",
        )
        self.assertTrue(changed)
        OUTPUT.mkdir(parents=True, exist_ok=True)
        generated_path = OUTPUT / "seh_valid.seh.c"
        generated_path.write_text(generated, encoding="utf-8", newline="\n")
        result = subprocess.run(
            [
                str(_clang()),
                "--target=x86_64-none-elf",
                "-m64",
                "-std=gnu11",
                "-ffreestanding",
                "-fno-builtin",
                "-fno-omit-frame-pointer",
                "-fsyntax-only",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "shared/include"),
                str(generated_path),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stdout + result.stderr,
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SehTranslatorTests)
    outcome = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if outcome.wasSuccessful() else 1)
