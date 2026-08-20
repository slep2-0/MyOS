#!/usr/bin/env python3
"""Convert a linked x86-64 ELF image into the Matanel executable format."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
SYMBOL = struct.Struct("<IBBHQQ")
RELA = struct.Struct("<QQq")

PT_TLS = 7
SHT_NOBITS = 8
SHT_DYNSYM = 11
SHT_SYMTAB = 2
SHT_RELA = 4
SHN_UNDEF = 0
SHF_ALLOC = 0x2
SHF_TLS = 0x400
STB_GLOBAL = 1
STB_WEAK = 2
STT_NOTYPE = 0
STT_FUNC = 2

R_X86_64_GLOB_DAT = 6
R_X86_64_JUMP_SLOT = 7
R_X86_64_RELATIVE = 8
R_X86_64_DTPMOD64 = 16

MTE_HEADER = struct.Struct("<4s15Q4s")
MTE_TLS_DIRECTORY = struct.Struct("<6Q")
MTE_TLS_MODULE_INDEX_FIXUP = struct.Struct("<Q")
MTE_EXPORT = struct.Struct("<QQ")
MTE_IMPORT = struct.Struct("<QQQ")
MTE_RELOCATION = struct.Struct("<QQq")
MTE_HEADER_SIZE = 128


class MtePackError(RuntimeError):
    pass


@dataclass(frozen=True)
class ElfSection:
    index: int
    name: str
    type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int


@dataclass(frozen=True)
class ElfProgramHeader:
    type: int
    flags: int
    offset: int
    virtual_address: int
    physical_address: int
    file_size: int
    memory_size: int
    alignment: int


@dataclass(frozen=True)
class ElfSymbol:
    name: str
    value: int
    size: int
    binding: int
    type: int
    visibility: int
    section_index: int


@dataclass(frozen=True)
class ElfRelocation:
    offset: int
    type: int
    symbol: ElfSymbol | None
    addend: int
    section_name: str


class ElfImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < ELF_HEADER.size:
            raise MtePackError(f"{path}: truncated ELF header")

        fields = ELF_HEADER.unpack_from(self.data)
        ident = fields[0]
        if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
            raise MtePackError(f"{path}: expected little-endian ELF64")
        if fields[2] != 0x3E:
            raise MtePackError(f"{path}: expected an x86-64 ELF image")

        self.entry = fields[4]
        program_offset = fields[5]
        section_offset = fields[6]
        program_entry_size = fields[9]
        program_count = fields[10]
        section_entry_size = fields[11]
        section_count = fields[12]
        section_names_index = fields[13]
        if program_count and program_entry_size != PROGRAM_HEADER.size:
            raise MtePackError(f"{path}: unsupported program-header size")
        if program_offset + program_count * program_entry_size > len(self.data):
            raise MtePackError(f"{path}: truncated program-header table")
        if section_entry_size != SECTION_HEADER.size:
            raise MtePackError(f"{path}: unsupported section-header size")
        if section_offset + section_count * section_entry_size > len(self.data):
            raise MtePackError(f"{path}: truncated section table")

        raw_sections = [
            SECTION_HEADER.unpack_from(
                self.data, section_offset + index * section_entry_size
            )
            for index in range(section_count)
        ]

        self.program_headers = [
            ElfProgramHeader(*PROGRAM_HEADER.unpack_from(
                self.data, program_offset + index * program_entry_size
            ))
            for index in range(program_count)
        ]
        if section_names_index >= section_count:
            raise MtePackError(f"{path}: invalid section-name table")
        names_header = raw_sections[section_names_index]
        names = self._slice(names_header[4], names_header[5], "section names")

        self.sections: list[ElfSection] = []
        for index, raw in enumerate(raw_sections):
            name = self._string(names, raw[0], "section name")
            self.sections.append(
                ElfSection(
                    index,
                    name,
                    raw[1],
                    raw[2],
                    raw[3],
                    raw[4],
                    raw[5],
                    raw[6],
                    raw[7],
                    raw[8],
                    raw[9],
                )
            )
        self.sections_by_name = {section.name: section for section in self.sections}
        self._symbol_tables: dict[int, list[ElfSymbol]] = {}

    def tls_program_header(self) -> ElfProgramHeader | None:
        matches = [header for header in self.program_headers if header.type == PT_TLS]
        if len(matches) > 1:
            raise MtePackError(f"{self.path}: multiple PT_TLS program headers")
        if not matches:
            return None

        header = matches[0]
        if header.memory_size == 0:
            raise MtePackError(f"{self.path}: empty PT_TLS segment")
        if header.file_size > header.memory_size:
            raise MtePackError(f"{self.path}: PT_TLS file size exceeds memory size")
        if header.alignment not in (0, 1) and header.alignment & (header.alignment - 1):
            raise MtePackError(f"{self.path}: PT_TLS alignment is not a power of two")
        self._slice(header.offset, header.file_size, "PT_TLS template")
        return header

    def _slice(self, offset: int, size: int, description: str) -> bytes:
        if offset > len(self.data) or size > len(self.data) - offset:
            raise MtePackError(f"{self.path}: truncated {description}")
        return self.data[offset : offset + size]

    def _string(self, table: bytes, offset: int, description: str) -> str:
        if offset >= len(table):
            raise MtePackError(f"{self.path}: invalid {description} offset")
        end = table.find(b"\0", offset)
        if end < 0:
            raise MtePackError(f"{self.path}: unterminated {description}")
        try:
            return table[offset:end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise MtePackError(f"{self.path}: non-ASCII {description}") from exc

    def section(self, name: str) -> ElfSection:
        try:
            return self.sections_by_name[name]
        except KeyError as exc:
            raise MtePackError(f"{self.path}: missing ELF section {name}") from exc

    def section_data(self, section: ElfSection) -> bytes:
        if section.type == SHT_NOBITS:
            return bytes(section.size)
        return self._slice(section.offset, section.size, f"section {section.name}")

    def symbols(self, section: ElfSection) -> list[ElfSymbol]:
        cached = self._symbol_tables.get(section.index)
        if cached is not None:
            return cached
        if section.type not in (SHT_SYMTAB, SHT_DYNSYM) or section.entry_size != SYMBOL.size:
            raise MtePackError(f"{self.path}: malformed symbol table {section.name}")
        if section.link >= len(self.sections):
            raise MtePackError(f"{self.path}: invalid string table for {section.name}")
        strings = self.section_data(self.sections[section.link])
        raw = self.section_data(section)
        result: list[ElfSymbol] = []
        for offset in range(0, len(raw), SYMBOL.size):
            name, info, other, shndx, value, size = SYMBOL.unpack_from(raw, offset)
            result.append(
                ElfSymbol(
                    self._string(strings, name, "symbol") if name else "",
                    value,
                    size,
                    info >> 4,
                    info & 0xF,
                    other & 0x3,
                    shndx,
                )
            )
        self._symbol_tables[section.index] = result
        return result

    def all_symbols(self) -> Iterable[ElfSymbol]:
        symtab = self.sections_by_name.get(".symtab")
        if symtab is None:
            raise MtePackError(f"{self.path}: the intermediate ELF has no .symtab")
        return self.symbols(symtab)

    def symbol_value(self, name: str) -> int:
        matches = [symbol.value for symbol in self.all_symbols() if symbol.name == name]
        if len(matches) != 1:
            raise MtePackError(
                f"{self.path}: expected exactly one symbol named {name}, found {len(matches)}"
            )
        return matches[0]

    def exports(self) -> dict[str, ElfSymbol]:
        api_section = self.section(".mtapi.text")
        exports: dict[str, ElfSymbol] = {}
        for symbol in self.all_symbols():
            if (
                symbol.section_index == api_section.index
                and symbol.binding in (STB_GLOBAL, STB_WEAK)
                and symbol.type in (STT_NOTYPE, STT_FUNC)
                and symbol.name
            ):
                if symbol.name in exports:
                    raise MtePackError(f"{self.path}: duplicate export {symbol.name}")
                exports[symbol.name] = symbol
        return exports

    def relocations(self) -> list[ElfRelocation]:
        result: list[ElfRelocation] = []
        for section in self.sections:
            if section.type != SHT_RELA:
                continue
            if section.entry_size != RELA.size or section.link >= len(self.sections):
                raise MtePackError(f"{self.path}: malformed relocation section {section.name}")
            symbols = self.symbols(self.sections[section.link])
            raw = self.section_data(section)
            for offset in range(0, len(raw), RELA.size):
                target, info, addend = RELA.unpack_from(raw, offset)
                symbol_index = info >> 32
                relocation_type = info & 0xFFFFFFFF
                if symbol_index >= len(symbols):
                    raise MtePackError(
                        f"{self.path}: invalid symbol in relocation {section.name}"
                    )
                result.append(
                    ElfRelocation(
                        target,
                        relocation_type,
                        symbols[symbol_index] if symbol_index else None,
                        addend,
                        section.name,
                    )
                )
        return result


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def checked_rva(value: int, image_base: int, image_size: int, description: str) -> int:
    if value < image_base:
        raise MtePackError(f"{description} is below the preferred image base")
    rva = value - image_base
    if rva >= image_size:
        raise MtePackError(f"{description} is outside the runtime image")
    return rva


@dataclass(frozen=True)
class PackedImport:
    module: str
    name: str
    iat_rva: int


@dataclass(frozen=True)
class PackedRelocation:
    target_rva: int
    addend_rva: int


@dataclass(frozen=True)
class PackedTls:
    template_rva: int
    template_size: int
    total_size: int
    alignment: int
    module_index_fixups: tuple[int, ...]


def _range_contains(
    outer_start: int,
    outer_end: int,
    inner_start: int,
    inner_end: int,
) -> bool:
    return outer_start <= inner_start and inner_end <= outer_end


def _validate_runtime_sections(
    image: ElfImage,
    image_base: int,
    file_end: int,
    bss_start: int,
    bss_end: int,
    copied_ranges: list[tuple[int, int, str]],
    tls_program_header: ElfProgramHeader | None,
) -> None:
    """Reject allocated runtime sections that the MTE file would omit."""
    for section in image.sections:
        if not (section.flags & SHF_ALLOC) or section.size == 0:
            continue

        start = section.address
        end = start + section.size
        if start < image_base or end < start:
            raise MtePackError(
                f"allocated section {section.name} has invalid runtime bounds"
            )

        # ELF-only dynamic metadata is deliberately placed after the MTE
        # runtime image. It is consumed by this packer and never copied.
        if start >= bss_end:
            continue

        if section.type == SHT_NOBITS:
            if section.flags & SHF_TLS and tls_program_header is not None:
                tls_start = tls_program_header.virtual_address
                tls_end = tls_start + tls_program_header.memory_size
                if _range_contains(tls_start, tls_end, start, end):
                    continue
            if not _range_contains(bss_start, bss_end, start, end):
                raise MtePackError(
                    f"allocated NOBITS section {section.name} escapes MTE BSS"
                )
            continue

        if end > file_end:
            raise MtePackError(
                f"allocated section {section.name} crosses the MTE file/BSS boundary"
            )
        if not any(
            _range_contains(range_start, range_end, start, end)
            for range_start, range_end, _ in copied_ranges
        ):
            raise MtePackError(
                f"allocated section {section.name} is not copied into the MTE image"
            )


def analyze(
    image: ElfImage,
    dependencies: dict[str, ElfImage],
    image_base: int,
    image_size: int,
) -> tuple[
    list[tuple[str, int]],
    list[PackedImport],
    list[PackedRelocation],
    list[int],
]:
    exports = sorted(
        (
            name,
            checked_rva(symbol.value, image_base, image_size, f"export {name}"),
        )
        for name, symbol in image.exports().items()
    )
    dependency_exports = {
        module: dependency.exports() for module, dependency in dependencies.items()
    }

    imports: list[PackedImport] = []
    relocations: list[PackedRelocation] = []
    tls_module_index_fixups: list[int] = []
    seen_iat: set[int] = set()
    seen_targets: set[int] = set()
    has_tls = image.tls_program_header() is not None
    for relocation in image.relocations():
        target_rva = checked_rva(
            relocation.offset,
            image_base,
            image_size,
            f"{relocation.section_name} relocation target",
        )
        if relocation.type == R_X86_64_RELATIVE:
            if relocation.symbol is not None and relocation.symbol.name:
                raise MtePackError("R_X86_64_RELATIVE unexpectedly references a symbol")
            addend_rva = checked_rva(
                relocation.addend,
                image_base,
                image_size,
                "R_X86_64_RELATIVE addend",
            )
            if target_rva in seen_targets or target_rva in seen_iat:
                raise MtePackError(f"duplicate relocation target RVA 0x{target_rva:x}")
            seen_targets.add(target_rva)
            relocations.append(PackedRelocation(target_rva, addend_rva))
            continue

        if relocation.type in (R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT):
            symbol = relocation.symbol
            if symbol is None or not symbol.name or symbol.section_index != SHN_UNDEF:
                raise MtePackError("import relocation does not reference an undefined symbol")
            providers = [
                module
                for module, symbols in dependency_exports.items()
                if symbol.name in symbols
            ]
            if len(providers) != 1:
                raise MtePackError(
                    f"import {symbol.name} has {len(providers)} providers; expected exactly one"
                )
            if target_rva in seen_iat or target_rva in seen_targets:
                raise MtePackError(f"duplicate import slot RVA 0x{target_rva:x}")
            seen_iat.add(target_rva)
            imports.append(PackedImport(providers[0], symbol.name, target_rva))
            continue

        if relocation.type == R_X86_64_DTPMOD64:
            if not has_tls:
                raise MtePackError("R_X86_64_DTPMOD64 appears without a PT_TLS segment")
            if relocation.symbol is not None and relocation.symbol.name:
                raise MtePackError(
                    "R_X86_64_DTPMOD64 unexpectedly references a named symbol"
                )
            if relocation.addend != 0:
                raise MtePackError("R_X86_64_DTPMOD64 has a nonzero addend")
            if target_rva in seen_targets or target_rva in seen_iat:
                raise MtePackError(f"duplicate relocation target RVA 0x{target_rva:x}")
            seen_targets.add(target_rva)
            tls_module_index_fixups.append(target_rva)
            continue

        symbol_name = relocation.symbol.name if relocation.symbol else "<none>"
        raise MtePackError(
            f"unsupported relocation type {relocation.type} for {symbol_name} "
            f"in {relocation.section_name}"
        )

    imports.sort(key=lambda item: (item.module, item.name, item.iat_rva))
    relocations.sort(key=lambda item: item.target_rva)
    tls_module_index_fixups.sort()
    return exports, imports, relocations, tls_module_index_fixups


@dataclass(frozen=True)
class MetadataLayout:
    export_rva: int
    export_size: int
    relocation_rva: int
    relocation_size: int
    import_rva: int
    import_size: int
    tls_rva: int
    tls_size: int
    tls_fixups_rva: int
    tls_fixups_size: int
    size: int


def metadata_size(
    metadata_rva: int,
    exports: list[tuple[str, int]],
    imports: list[PackedImport],
    relocations: list[PackedRelocation],
    tls: PackedTls | None,
) -> MetadataLayout:
    cursor = metadata_rva
    export_rva = cursor
    export_size = len(exports) * MTE_EXPORT.size
    cursor += export_size + sum(len(name.encode("ascii")) + 1 for name, _ in exports)
    cursor = align_up(cursor, 8)

    relocation_rva = cursor
    relocation_size = len(relocations) * MTE_RELOCATION.size
    cursor += relocation_size
    cursor = align_up(cursor, 8)

    import_rva = cursor
    import_size = len(imports) * MTE_IMPORT.size
    cursor += import_size
    cursor += sum(
        len(item.module.encode("ascii")) + 1 + len(item.name.encode("ascii")) + 1
        for item in imports
    )
    cursor = align_up(cursor, 8)

    tls_rva = 0
    tls_size = 0
    tls_fixups_rva = 0
    tls_fixups_size = 0
    if tls is not None:
        tls_rva = cursor
        tls_size = MTE_TLS_DIRECTORY.size
        cursor += tls_size
        tls_fixups_rva = cursor if tls.module_index_fixups else 0
        tls_fixups_size = (
            len(tls.module_index_fixups) * MTE_TLS_MODULE_INDEX_FIXUP.size
        )
        cursor += tls_fixups_size

    cursor = align_up(cursor, 0x1000)
    return MetadataLayout(
        export_rva,
        export_size,
        relocation_rva,
        relocation_size,
        import_rva,
        import_size,
        tls_rva,
        tls_size,
        tls_fixups_rva,
        tls_fixups_size,
        cursor - metadata_rva,
    )


def write_metadata(
    output: bytearray,
    layout: MetadataLayout,
    exports: list[tuple[str, int]],
    imports: list[PackedImport],
    relocations: list[PackedRelocation],
    tls: PackedTls | None,
) -> None:
    cursor = layout.export_rva + layout.export_size
    for index, (name, function_rva) in enumerate(exports):
        encoded = name.encode("ascii") + b"\0"
        MTE_EXPORT.pack_into(
            output,
            layout.export_rva + index * MTE_EXPORT.size,
            cursor,
            function_rva,
        )
        output[cursor : cursor + len(encoded)] = encoded
        cursor += len(encoded)

    for index, relocation in enumerate(relocations):
        MTE_RELOCATION.pack_into(
            output,
            layout.relocation_rva + index * MTE_RELOCATION.size,
            relocation.target_rva,
            R_X86_64_RELATIVE,
            relocation.addend_rva,
        )

    cursor = layout.import_rva + layout.import_size
    for index, item in enumerate(imports):
        module = item.module.encode("ascii") + b"\0"
        name = item.name.encode("ascii") + b"\0"
        module_rva = cursor
        output[cursor : cursor + len(module)] = module
        cursor += len(module)
        name_rva = cursor
        output[cursor : cursor + len(name)] = name
        cursor += len(name)
        MTE_IMPORT.pack_into(
            output,
            layout.import_rva + index * MTE_IMPORT.size,
            module_rva,
            name_rva,
            item.iat_rva,
        )

    if tls is not None:
        MTE_TLS_DIRECTORY.pack_into(
            output,
            layout.tls_rva,
            tls.template_rva,
            tls.template_size,
            tls.total_size,
            tls.alignment,
            layout.tls_fixups_rva,
            layout.tls_fixups_size,
        )
        for index, target_rva in enumerate(tls.module_index_fixups):
            MTE_TLS_MODULE_INDEX_FIXUP.pack_into(
                output,
                layout.tls_fixups_rva + index * MTE_TLS_MODULE_INDEX_FIXUP.size,
                target_rva,
            )


def _packed_range_valid(data: bytes, rva: int, size: int) -> bool:
    if size == 0:
        return rva <= len(data)
    return rva < len(data) and size <= len(data) - rva


def _packed_string(data: bytes, rva: int, description: str) -> str:
    if not _packed_range_valid(data, rva, 1):
        raise MtePackError(f"packed {description} RVA is outside the file")
    end = data.find(b"\0", rva)
    if end < 0:
        raise MtePackError(f"packed {description} is unterminated")
    try:
        return data[rva:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise MtePackError(f"packed {description} is not ASCII") from exc


def validate_packed_image(
    data: bytes,
    expected_base: int,
    expected_entry_rva: int,
    expected_text: tuple[int, int],
    expected_data: tuple[int, int],
    expected_bss_size: int,
    expected_layout: MetadataLayout,
    expected_exports: list[tuple[str, int]],
    expected_imports: list[PackedImport],
    expected_relocations: list[PackedRelocation],
    expected_tls: PackedTls | None,
) -> None:
    """Parse the generated MTE again and prove that no RVA changed in packing."""
    if len(data) < MTE_HEADER_SIZE:
        raise MtePackError("packed MTE is shorter than its header")
    fields = MTE_HEADER.unpack_from(data)
    (
        magic,
        image_base,
        entry_rva,
        text_rva,
        text_size,
        data_rva,
        data_size,
        bss_size,
        export_rva,
        export_size,
        relocation_rva,
        relocation_size,
        import_rva,
        import_size,
        tls_rva,
        tls_size,
        reserved,
    ) = fields
    if magic != b"MTE\0" or reserved != bytes(4):
        raise MtePackError("packed MTE header magic or reserved bytes are invalid")
    if (
        image_base != expected_base
        or entry_rva != expected_entry_rva
        or (text_rva, text_size) != expected_text
        or (data_rva, data_size) != expected_data
        or bss_size != expected_bss_size
    ):
        raise MtePackError("packed MTE core header fields changed unexpectedly")
    actual_layout = MetadataLayout(
        export_rva,
        export_size,
        relocation_rva,
        relocation_size,
        import_rva,
        import_size,
        tls_rva,
        tls_size,
        expected_layout.tls_fixups_rva,
        expected_layout.tls_fixups_size,
        expected_layout.size,
    )
    if actual_layout != expected_layout:
        raise MtePackError("packed MTE directory layout changed unexpectedly")

    runtime_size = len(data) + bss_size
    for rva, size, description in (
        (text_rva, text_size, "text"),
        (data_rva, data_size, "data"),
        (export_rva, export_size, "export directory"),
        (relocation_rva, relocation_size, "relocation directory"),
        (import_rva, import_size, "import directory"),
        (tls_rva, tls_size, "TLS directory"),
    ):
        if not _packed_range_valid(data, rva, size):
            raise MtePackError(f"packed {description} is outside the file")
    if entry_rva and not (
        text_rva <= entry_rva < text_rva + text_size
    ):
        raise MtePackError("packed entry point is outside executable text")

    actual_exports: list[tuple[str, int]] = []
    for index in range(export_size // MTE_EXPORT.size):
        name_rva, function_rva = MTE_EXPORT.unpack_from(
            data, export_rva + index * MTE_EXPORT.size
        )
        if function_rva >= runtime_size:
            raise MtePackError("packed export function RVA is outside the image")
        actual_exports.append(
            (_packed_string(data, name_rva, "export name"), function_rva)
        )
    if actual_exports != expected_exports:
        raise MtePackError("packed export table does not match ELF exports")

    actual_relocations: list[PackedRelocation] = []
    for index in range(relocation_size // MTE_RELOCATION.size):
        target_rva, info, addend_rva = MTE_RELOCATION.unpack_from(
            data, relocation_rva + index * MTE_RELOCATION.size
        )
        if (
            info != R_X86_64_RELATIVE
            or target_rva > runtime_size - 8
            or addend_rva < 0
            or addend_rva >= runtime_size
        ):
            raise MtePackError("packed base relocation is invalid")
        actual_relocations.append(PackedRelocation(target_rva, addend_rva))
    if actual_relocations != expected_relocations:
        raise MtePackError("packed relocation table does not match ELF relocations")

    actual_imports: list[PackedImport] = []
    for index in range(import_size // MTE_IMPORT.size):
        module_rva, name_rva, iat_rva = MTE_IMPORT.unpack_from(
            data, import_rva + index * MTE_IMPORT.size
        )
        if iat_rva > runtime_size - 8:
            raise MtePackError("packed import slot RVA is outside the image")
        actual_imports.append(
            PackedImport(
                _packed_string(data, module_rva, "import module"),
                _packed_string(data, name_rva, "import name"),
                iat_rva,
            )
        )
    if actual_imports != expected_imports:
        raise MtePackError("packed import table does not match ELF imports")

    if expected_tls is None:
        if tls_rva != 0 or tls_size != 0:
            raise MtePackError("packed image unexpectedly contains a TLS directory")
        return

    if tls_size != MTE_TLS_DIRECTORY.size:
        raise MtePackError("packed TLS directory has an invalid size")
    (
        template_rva,
        template_size,
        total_size,
        alignment,
        fixups_rva,
        fixups_size,
    ) = MTE_TLS_DIRECTORY.unpack_from(data, tls_rva)
    if (
        template_rva != expected_tls.template_rva
        or template_size != expected_tls.template_size
        or total_size != expected_tls.total_size
        or alignment != expected_tls.alignment
        or fixups_rva != expected_layout.tls_fixups_rva
        or fixups_size != expected_layout.tls_fixups_size
    ):
        raise MtePackError("packed TLS directory does not match the ELF PT_TLS segment")
    if template_size > total_size:
        raise MtePackError("packed TLS template is larger than its per-thread block")
    if not _packed_range_valid(data, template_rva, template_size):
        raise MtePackError("packed TLS template is outside the file")
    if alignment == 0 or alignment & (alignment - 1):
        raise MtePackError("packed TLS alignment is not a power of two")
    if bool(fixups_rva) != bool(fixups_size):
        raise MtePackError("packed TLS fixup RVA and size disagree")
    if fixups_size % MTE_TLS_MODULE_INDEX_FIXUP.size:
        raise MtePackError("packed TLS fixup table is truncated")
    if not _packed_range_valid(data, fixups_rva, fixups_size):
        raise MtePackError("packed TLS fixup table is outside the file")

    actual_fixups: list[int] = []
    for index in range(fixups_size // MTE_TLS_MODULE_INDEX_FIXUP.size):
        (target_rva,) = MTE_TLS_MODULE_INDEX_FIXUP.unpack_from(
            data,
            fixups_rva + index * MTE_TLS_MODULE_INDEX_FIXUP.size,
        )
        if target_rva > runtime_size - 8:
            raise MtePackError("packed TLS module-index target is outside the image")
        actual_fixups.append(target_rva)
    if tuple(actual_fixups) != expected_tls.module_index_fixups:
        raise MtePackError("packed TLS fixups do not match ELF relocations")


def pack_image(
    input_path: Path,
    output_path: Path,
    dependencies: dict[str, Path],
    expected_metadata_size: int | None,
    print_metadata_size: bool,
) -> int:
    image = ElfImage(input_path)
    dependency_images = {
        module: ElfImage(path) for module, path in dependencies.items()
    }
    image_base = image.symbol_value("__image_base")
    metadata_start = image.symbol_value("__mt_metadata_start")
    metadata_end = image.symbol_value("__mt_metadata_end")
    file_end = image.symbol_value("__file_end")
    bss_start = image.symbol_value("__bss_start")
    bss_end = image.symbol_value("__bss_end")
    if metadata_end != file_end or bss_start != file_end:
        raise MtePackError("metadata, file end, and BSS start are not contiguous")
    if file_end < image_base or bss_end < bss_start:
        raise MtePackError("invalid linked image bounds")

    image_size = bss_end - image_base
    metadata_rva = metadata_start - image_base
    tls_program_header = image.tls_program_header()
    exports, imports, relocations, tls_module_index_fixups = analyze(
        image, dependency_images, image_base, image_size
    )
    tls: PackedTls | None = None
    if tls_program_header is not None:
        template_rva = checked_rva(
            tls_program_header.virtual_address,
            image_base,
            image_size,
            "PT_TLS template",
        )
        if tls_program_header.memory_size > image_size - template_rva:
            raise MtePackError("PT_TLS memory range is outside the runtime image")
        alignment = max(tls_program_header.alignment, 1)
        tls = PackedTls(
            template_rva,
            tls_program_header.file_size,
            tls_program_header.memory_size,
            alignment,
            tuple(tls_module_index_fixups),
        )
    elif tls_module_index_fixups:
        raise MtePackError("TLS module-index fixups appear without a PT_TLS segment")

    layout = metadata_size(metadata_rva, exports, imports, relocations, tls)
    if print_metadata_size:
        print(layout.size)
        return layout.size
    if expected_metadata_size is not None and layout.size != expected_metadata_size:
        raise MtePackError(
            f"metadata changed after relink: expected 0x{expected_metadata_size:x}, "
            f"calculated 0x{layout.size:x}"
        )
    if metadata_end - metadata_start != layout.size:
        raise MtePackError(
            f"linked metadata reservation is 0x{metadata_end - metadata_start:x}, "
            f"but 0x{layout.size:x} is required"
        )

    file_size = file_end - image_base
    output = bytearray(file_size)
    runtime_sections = (".mtheader", ".mtapi.text", ".text", ".data")
    occupied: list[tuple[int, int, str]] = []
    for name in runtime_sections:
        section = image.section(name)
        if section.type == SHT_NOBITS:
            raise MtePackError(f"runtime section {name} unexpectedly has no file data")
        rva = checked_rva(section.address, image_base, file_size, f"section {name}")
        if section.size > file_size - rva:
            raise MtePackError(f"runtime section {name} exceeds the MTE file")
        for start, end, previous_name in occupied:
            if rva < end and start < rva + section.size:
                raise MtePackError(f"runtime sections {previous_name} and {name} overlap")
        occupied.append((rva, rva + section.size, name))
        output[rva : rva + section.size] = image.section_data(section)

    if tls is not None and tls.template_size:
        tls_end = tls.template_rva + tls.template_size
        containing_range = next(
            (
                (start, end, name)
                for start, end, name in occupied
                if start <= tls.template_rva and tls_end <= end
            ),
            None,
        )
        overlapping_range = next(
            (
                (start, end, name)
                for start, end, name in occupied
                if tls.template_rva < end and start < tls_end
            ),
            None,
        )
        if containing_range is None and overlapping_range is not None:
            raise MtePackError(
                f"PT_TLS template partially overlaps runtime section "
                f"{overlapping_range[2]}"
            )
        template = image._slice(
            tls_program_header.offset,
            tls.template_size,
            "PT_TLS template",
        )
        if containing_range is None:
            if tls_end > file_size:
                raise MtePackError("PT_TLS template exceeds the MTE file")
            occupied.append((tls.template_rva, tls_end, "PT_TLS template"))
            output[tls.template_rva : tls_end] = template
        elif output[tls.template_rva : tls_end] != template:
            raise MtePackError("copied PT_TLS template does not match the ELF segment")

    metadata_range = (
        metadata_start,
        metadata_end,
        ".mtmetadata",
    )
    copied_vma_ranges = [
        (image_base + start, image_base + end, name)
        for start, end, name in occupied
    ]
    copied_vma_ranges.append(metadata_range)
    _validate_runtime_sections(
        image,
        image_base,
        file_end,
        bss_start,
        bss_end,
        copied_vma_ranges,
        tls_program_header,
    )

    text_rva = image.symbol_value("__text_start") - image_base
    text_size = image.symbol_value("__text_end") - image.symbol_value("__text_start")
    data_rva = image.symbol_value("__data_start") - image_base
    data_size = image.symbol_value("__data_end") - image.symbol_value("__data_start")
    bss_size = bss_end - bss_start
    entry_rva = image.entry - image_base if image.entry else 0

    write_metadata(output, layout, exports, imports, relocations, tls)
    header = MTE_HEADER.pack(
        b"MTE\0",
        image_base,
        entry_rva,
        text_rva,
        text_size,
        data_rva,
        data_size,
        bss_size,
        layout.export_rva,
        layout.export_size,
        layout.relocation_rva,
        layout.relocation_size,
        layout.import_rva,
        layout.import_size,
        layout.tls_rva,
        layout.tls_size,
        bytes(4),
    )
    if len(header) != MTE_HEADER_SIZE:
        raise MtePackError("internal MTE header size mismatch")
    output[:MTE_HEADER_SIZE] = header

    metadata_begin = metadata_rva
    metadata_end_rva = metadata_begin + layout.size
    for relocation in relocations:
        if metadata_begin <= relocation.target_rva < metadata_end_rva:
            raise MtePackError("runtime relocation targets generated MTE metadata")
    for item in imports:
        if metadata_begin <= item.iat_rva < metadata_end_rva:
            raise MtePackError("import slot targets generated MTE metadata")
    if tls is not None:
        for target_rva in tls.module_index_fixups:
            if metadata_begin <= target_rva < metadata_end_rva:
                raise MtePackError("TLS module-index fixup targets generated MTE metadata")

    validate_packed_image(
        output,
        image_base,
        entry_rva,
        (text_rva, text_size),
        (data_rva, data_size),
        bss_size,
        layout,
        exports,
        imports,
        relocations,
        tls,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    print(
        f"[MTE] {output_path} "
        f"({len(exports)} exports, {len(imports)} imports, "
        f"{len(relocations)} relocations, "
        f"{len(tls.module_index_fixups) if tls else 0} TLS fixups)"
    )
    return layout.size


def parse_dependency(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("dependency must be MODULE=ELF")
    module, path = value.split("=", 1)
    if not module or not path:
        raise argparse.ArgumentTypeError("dependency must be MODULE=ELF")
    return module, Path(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--dependency", action="append", default=[], type=parse_dependency)
    parser.add_argument("--metadata-size", type=lambda value: int(value, 0))
    parser.add_argument("--print-metadata-size", action="store_true")
    args = parser.parse_args()

    if not args.print_metadata_size and args.output is None:
        parser.error("output is required unless --print-metadata-size is used")
    dependencies = dict(args.dependency)
    if len(dependencies) != len(args.dependency):
        parser.error("duplicate dependency module")
    try:
        pack_image(
            args.input,
            args.output or Path("-"),
            dependencies,
            args.metadata_size,
            args.print_metadata_size,
        )
        return 0
    except (MtePackError, OSError, struct.error) as exc:
        print(f"MTE PACK ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
