#!/usr/bin/env python3
"""Create the MatanelOS GPT/FAT32 boot image without mounting a disk."""

from __future__ import annotations

import argparse
import binascii
import shutil
import struct
import tempfile
import uuid
from pathlib import Path
from typing import Sequence


SECTOR_SIZE = 512
IMAGE_SIZE = 64 * 1024 * 1024
PARTITION_START_LBA = 2048
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = (GPT_ENTRY_COUNT * GPT_ENTRY_SIZE) // SECTOR_SIZE

ESP_TYPE_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
DISK_GUID = uuid.UUID("84c4fcaa-a80c-4fa1-88c7-17cf862f11cb")
PARTITION_GUID = uuid.UUID("a81e6348-fd9f-4f07-8218-33bafaf07a74")


def _crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR_SIZE)
    size = min(total_sectors - 1, 0xFFFFFFFF)
    # Status, CHS start, type, CHS end, first LBA, sector count.
    mbr[446:462] = struct.pack(
        "<B3sB3sII",
        0,
        b"\x00\x02\x00",
        0xEE,
        b"\xff\xff\xff",
        1,
        size,
    )
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def _partition_entries(first_lba: int, last_lba: int) -> bytes:
    entries = bytearray(GPT_ENTRY_COUNT * GPT_ENTRY_SIZE)
    name = "MatanelOS".encode("utf-16-le")
    entry = bytearray(GPT_ENTRY_SIZE)
    entry[0:16] = ESP_TYPE_GUID.bytes_le
    entry[16:32] = PARTITION_GUID.bytes_le
    struct.pack_into("<QQQ", entry, 32, first_lba, last_lba, 0)
    entry[56 : 56 + len(name)] = name
    entries[:GPT_ENTRY_SIZE] = entry
    return bytes(entries)


def _gpt_header(
    *,
    current_lba: int,
    backup_lba: int,
    first_usable_lba: int,
    last_usable_lba: int,
    entries_lba: int,
    entries_crc: int,
) -> bytes:
    header = bytearray(SECTOR_SIZE)
    struct.pack_into(
        "<8sIIIIQQQQ16sQIII",
        header,
        0,
        b"EFI PART",
        0x00010000,
        92,
        0,
        0,
        current_lba,
        backup_lba,
        first_usable_lba,
        last_usable_lba,
        DISK_GUID.bytes_le,
        entries_lba,
        GPT_ENTRY_COUNT,
        GPT_ENTRY_SIZE,
        entries_crc,
    )
    struct.pack_into("<I", header, 16, _crc32(header[:92]))
    return bytes(header)


def _format_and_populate_fat32(
    partition_file: Path,
    partition_size: int,
    files: list[tuple[Path, str]],
) -> None:
    try:
        from pyfatfs.PyFat import PyFat
        from pyfatfs.PyFatFS import PyFatFS
    except ImportError as exc:
        raise RuntimeError(
            "pyfatfs is required. Run tools\\windows\\bootstrap.bat first."
        ) from exc

    partition_file.touch()
    formatter = PyFat()
    formatter.mkfs(
        str(partition_file),
        fat_type=PyFat.FAT_TYPE_FAT32,
        size=partition_size,
        sector_size=SECTOR_SIZE,
        label="MATANELOS",
        volume_id=0x4D544F53,
    )
    formatter.close()

    fat = PyFatFS(str(partition_file), preserve_case=True)
    try:
        fat.makedirs("EFI/BOOT", recreate=True)
        for source, destination in files:
            parent = destination.rpartition("/")[0]
            if parent:
                fat.makedirs(parent, recreate=True)
            with source.open("rb") as input_file, fat.openbin(destination, "w") as output_file:
                shutil.copyfileobj(input_file, output_file, length=1024 * 1024)
    finally:
        fat.close()


def create_image(
    output: Path,
    bootloader: Path,
    kernel: Path,
    mtdll: Path,
    program: Path,
    *,
    additional_files: Sequence[tuple[Path, str]] = (),
) -> None:
    inputs = [bootloader, kernel, mtdll, program, *(source for source, _ in additional_files)]
    missing = [path for path in inputs if not path.is_file()]
    if missing:
        joined = "\n".join(f"  {path}" for path in missing)
        raise FileNotFoundError(f"Image input file(s) not found:\n{joined}")

    total_sectors = IMAGE_SIZE // SECTOR_SIZE
    backup_header_lba = total_sectors - 1
    backup_entries_lba = backup_header_lba - GPT_ENTRY_SECTORS
    first_usable_lba = 2 + GPT_ENTRY_SECTORS
    last_usable_lba = backup_entries_lba - 1
    partition_last_lba = last_usable_lba
    partition_size = (
        partition_last_lba - PARTITION_START_LBA + 1
    ) * SECTOR_SIZE

    entries = _partition_entries(PARTITION_START_LBA, partition_last_lba)
    entries_crc = _crc32(entries)
    primary_header = _gpt_header(
        current_lba=1,
        backup_lba=backup_header_lba,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        entries_lba=2,
        entries_crc=entries_crc,
    )
    backup_header = _gpt_header(
        current_lba=backup_header_lba,
        backup_lba=1,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        entries_lba=backup_entries_lba,
        entries_crc=entries_crc,
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="matanelos-image-") as temporary:
        partition_file = Path(temporary) / "esp.fat"
        _format_and_populate_fat32(
            partition_file,
            partition_size,
            [
                (bootloader, "EFI/BOOT/BOOTX64.EFI"),
                (kernel, "kernel.elf"),
                (mtdll, "mtdll.mtdll"),
                (program, "terminateMyself.mtexe"),
                *additional_files,
            ],
        )

        with output.open("wb") as image:
            image.truncate(IMAGE_SIZE)
            image.seek(0)
            image.write(_protective_mbr(total_sectors))
            image.seek(SECTOR_SIZE)
            image.write(primary_header)
            image.seek(2 * SECTOR_SIZE)
            image.write(entries)
            image.seek(backup_entries_lba * SECTOR_SIZE)
            image.write(entries)
            image.seek(backup_header_lba * SECTOR_SIZE)
            image.write(backup_header)
            image.seek(PARTITION_START_LBA * SECTOR_SIZE)
            with partition_file.open("rb") as partition:
                shutil.copyfileobj(partition, image, length=1024 * 1024)


def _parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--bootloader",
        type=Path,
        default=root / "build/windows/debug/bootloader.efi",
    )
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--mtdll", type=Path, required=True)
    parser.add_argument("--program", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    create_image(
        args.output.resolve(),
        args.bootloader.resolve(),
        args.kernel.resolve(),
        args.mtdll.resolve(),
        args.program.resolve(),
    )
    print(f"[IMAGE] {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
