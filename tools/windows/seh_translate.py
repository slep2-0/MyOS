#!/usr/bin/env python3
"""Lower MatanelOS frame-based __try/__except syntax into portable GNU C."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


class TranslationError(RuntimeError):
    """Raised when an SEH construct is malformed."""


def _lexical_mask(source: str) -> str:
    """Keep C punctuation/identifiers and blank comments and literals."""

    result = list(source)
    index = 0
    state = "code"
    line_prefix_is_space = True

    def blank(position: int) -> None:
        if result[position] != "\n":
            result[position] = " "

    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state == "code":
            if char == "\n":
                line_prefix_is_space = True
                index += 1
                continue
            if line_prefix_is_space and char in " \t\r\f\v":
                index += 1
                continue
            if line_prefix_is_space and char == "#":
                state = "directive"
                blank(index)
                index += 1
                continue
            line_prefix_is_space = False
            if char == "/" and following == "/":
                state = "line-comment"
                blank(index)
                blank(index + 1)
                index += 2
                continue
            if char == "/" and following == "*":
                state = "block-comment"
                blank(index)
                blank(index + 1)
                index += 2
                continue
            if char == '"':
                state = "string"
                blank(index)
                index += 1
                continue
            if char == "'":
                state = "character"
                blank(index)
                index += 1
                continue
            index += 1
            continue

        if state == "line-comment":
            blank(index)
            if char == "\n":
                state = "code"
                line_prefix_is_space = True
            index += 1
            continue

        if state == "block-comment":
            blank(index)
            if char == "*" and following == "/":
                blank(index + 1)
                index += 2
                state = "code"
            else:
                index += 1
            continue

        if state in ("string", "character"):
            blank(index)
            terminator = '"' if state == "string" else "'"
            if char == "\\" and following:
                blank(index + 1)
                index += 2
            elif char == terminator:
                state = "code"
                index += 1
            else:
                index += 1
            continue

        if state == "directive":
            blank(index)
            if char == "\n":
                slash_count = 0
                cursor = index - 1
                while cursor >= 0 and source[cursor] == "\\":
                    slash_count += 1
                    cursor -= 1
                if slash_count % 2 == 0:
                    state = "code"
                    line_prefix_is_space = True
            index += 1
            continue

    if state == "block-comment":
        raise TranslationError("unterminated block comment")
    if state in ("string", "character"):
        raise TranslationError(f"unterminated {state} literal")
    return "".join(result)


def _identifier_at(mask: str, index: int, expected: str) -> bool:
    if not mask.startswith(expected, index):
        return False
    before = mask[index - 1] if index else ""
    after_index = index + len(expected)
    after = mask[after_index] if after_index < len(mask) else ""
    return not (before.isalnum() or before == "_") and not (
        after.isalnum() or after == "_"
    )


def _skip_space(mask: str, index: int) -> int:
    while index < len(mask) and mask[index].isspace():
        index += 1
    return index


def _matching(mask: str, opening: int, left: str, right: str) -> int:
    if opening >= len(mask) or mask[opening] != left:
        raise TranslationError(f"expected '{left}'")
    depth = 1
    index = opening + 1
    while index < len(mask):
        if mask[index] == left:
            depth += 1
        elif mask[index] == right:
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise TranslationError(f"unmatched '{left}'")


@dataclass(frozen=True)
class SehConstruct:
    start: int
    protected_start: int
    protected_end: int
    between_start: int
    except_start: int
    filter_start: int
    filter_end: int
    handler_start: int
    handler_end: int
    end: int


class SehTranslator:
    def __init__(self, source: str, display_path: str) -> None:
        self.source = source
        self.display_path = display_path.replace("\\", "/")
        self.next_scope = 1

    def _line(self, absolute_offset: int) -> int:
        return self.source.count("\n", 0, absolute_offset) + 1

    def _directive(self, line: int) -> str:
        escaped = self.display_path.replace("\\", "\\\\").replace('"', '\\"')
        return f'\n#line {line} "{escaped}"\n'

    def _error(self, absolute_offset: int, message: str) -> TranslationError:
        line = self._line(absolute_offset)
        line_start = self.source.rfind("\n", 0, absolute_offset) + 1
        column = absolute_offset - line_start + 1
        return TranslationError(
            f"{self.display_path}:{line}:{column}: {message}"
        )

    def _find_construct(
        self,
        text: str,
        mask: str,
        start: int,
        absolute_start: int,
    ) -> SehConstruct | None:
        cursor = start
        while True:
            cursor = mask.find("__try", cursor)
            if cursor < 0:
                return None
            if _identifier_at(mask, cursor, "__try"):
                break
            cursor += len("__try")

        opening = _skip_space(mask, cursor + len("__try"))
        if opening >= len(mask) or mask[opening] != "{":
            raise self._error(absolute_start + opening, "expected '{' after __try")
        protected_close = _matching(mask, opening, "{", "}")
        between_start = protected_close + 1
        except_start = _skip_space(mask, between_start)
        if not _identifier_at(mask, except_start, "__except"):
            raise self._error(
                absolute_start + except_start,
                "expected __except after __try block",
            )
        filter_open = _skip_space(mask, except_start + len("__except"))
        if filter_open >= len(mask) or mask[filter_open] != "(":
            raise self._error(
                absolute_start + filter_open,
                "expected '(' after __except",
            )
        filter_close = _matching(mask, filter_open, "(", ")")
        handler_open = _skip_space(mask, filter_close + 1)
        if handler_open >= len(mask) or mask[handler_open] != "{":
            raise self._error(
                absolute_start + handler_open,
                "expected handler block after __except filter",
            )
        handler_close = _matching(mask, handler_open, "{", "}")
        return SehConstruct(
            start=cursor,
            protected_start=opening + 1,
            protected_end=protected_close,
            between_start=between_start,
            except_start=except_start,
            filter_start=filter_open + 1,
            filter_end=filter_close,
            handler_start=handler_open + 1,
            handler_end=handler_close,
            end=handler_close + 1,
        )

    def _rewrite_intrinsics(self, text: str, frame: str | None) -> str:
        if frame is None:
            return text
        mask = _lexical_mask(text)
        replacements: list[tuple[int, int, str]] = []
        for name, expression in (
            ("GetExceptionCode", f"({frame}.ExceptionRecord.ExceptionCode)"),
            (
                "GetExceptionInformation",
                f"(&{frame}.ExceptionPointers)",
            ),
        ):
            cursor = 0
            while True:
                cursor = mask.find(name, cursor)
                if cursor < 0:
                    break
                if not _identifier_at(mask, cursor, name):
                    cursor += len(name)
                    continue
                opening = _skip_space(mask, cursor + len(name))
                if opening >= len(mask) or mask[opening] != "(":
                    cursor += len(name)
                    continue
                closing = _matching(mask, opening, "(", ")")
                if mask[opening + 1 : closing].strip():
                    cursor = closing + 1
                    continue
                replacements.append((cursor, closing + 1, expression))
                cursor = closing + 1

        for begin, end, replacement in sorted(replacements, reverse=True):
            text = text[:begin] + replacement + text[end:]
        return text

    def _transform_fragment(
        self,
        text: str,
        absolute_start: int,
        exception_frame: str | None,
    ) -> tuple[str, bool]:
        mask = _lexical_mask(text)
        output: list[str] = []
        cursor = 0
        transformed = False

        while True:
            construct = self._find_construct(
                text,
                mask,
                cursor,
                absolute_start,
            )
            if construct is None:
                output.append(self._rewrite_intrinsics(text[cursor:], exception_frame))
                break

            output.append(
                self._rewrite_intrinsics(text[cursor : construct.start], exception_frame)
            )
            scope = self.next_scope
            self.next_scope += 1
            frame = f"__mt_seh_frame_{scope}"
            guard = f"__mt_seh_guard_{scope}"
            action = f"__mt_seh_action_{scope}"

            protected, _ = self._transform_fragment(
                text[construct.protected_start : construct.protected_end],
                absolute_start + construct.protected_start,
                exception_frame,
            )
            filter_text = self._rewrite_intrinsics(
                text[construct.filter_start : construct.filter_end],
                frame,
            )
            handler, _ = self._transform_fragment(
                text[construct.handler_start : construct.handler_end],
                absolute_start + construct.handler_start,
                frame,
            )
            trivia = text[construct.between_start : construct.except_start]

            try_line = self._line(absolute_start + construct.start)
            protected_line = self._line(
                absolute_start + construct.protected_start
            )
            filter_line = self._line(absolute_start + construct.filter_start)
            handler_line = self._line(absolute_start + construct.handler_start)
            end_line = self._line(absolute_start + construct.end)

            output.append(self._directive(try_line))
            output.append("{\n")
            output.append(f"    MT_LANGUAGE_FRAME {frame} = {{ 0 }};\n")
            output.append(
                f"    MT_LANGUAGE_SCOPE_GUARD {guard} "
                f"__attribute__((cleanup(MtpCleanupLanguageFrame))) = "
                f"{{ &{frame} }};\n"
            )
            output.append(
                f"    volatile int {action} = "
                f"MtpSaveLanguageContext(&{frame}.Continuation);\n"
            )
            output.append(f"    if ({action} == MtLanguageProtected) {{\n")
            output.append(f"        MtpEnterLanguageFrame(&{frame});")
            output.append(self._directive(protected_line))
            output.append(protected)
            output.append(self._directive(end_line))
            output.append(f"    }}{trivia} else if ({action} == MtLanguageEvaluateFilter) {{\n")
            output.append(self._directive(filter_line))
            output.append(
                f"        MtpApplyLanguageFilter(&{frame}, (int)({filter_text}));\n"
            )
            output.append(self._directive(end_line))
            output.append(f"    }} else if ({action} == MtLanguageExecuteHandler) {{\n")
            output.append(self._directive(handler_line))
            output.append(handler)
            output.append(self._directive(end_line))
            output.append("    } else {\n")
            output.append(
                f"        MtpApplyLanguageFilter(&{frame}, {action});\n"
            )
            output.append("    }\n")
            output.append("}\n")
            output.append(self._directive(end_line))

            cursor = construct.end
            transformed = True

        remaining_mask = _lexical_mask(text[cursor:])
        stray = remaining_mask.find("__except")
        if stray >= 0 and _identifier_at(remaining_mask, stray, "__except"):
            raise self._error(
                absolute_start + cursor + stray,
                "__except has no matching __try",
            )
        return "".join(output), transformed

    def translate(self) -> tuple[str, bool]:
        translated, changed = self._transform_fragment(self.source, 0, None)
        if not changed:
            return self.source, False
        prelude = '#include "mtlanguage.h"\n' + self._directive(1).lstrip("\n")
        return prelude + translated, True


def translate_text(source: str, display_path: str = "input.c") -> tuple[str, bool]:
    return SehTranslator(source, display_path).translate()


def translate_file(source: Path, output: Path, display_path: str) -> bool:
    translated, changed = translate_text(
        source.read_text(encoding="utf-8"),
        display_path,
    )
    if not changed:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != translated:
        output.write_text(translated, encoding="utf-8", newline="\n")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Lower MatanelOS __try/__except syntax into GNU C."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--display-path")
    arguments = parser.parse_args()
    display_path = arguments.display_path or arguments.input.as_posix()
    try:
        translate_file(arguments.input, arguments.output, display_path)
    except (OSError, TranslationError) as error:
        parser.exit(1, f"SEH TRANSLATION ERROR: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
