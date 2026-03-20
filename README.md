![DEVELOPMENT](https://img.shields.io/badge/Status-DEVELOPMENT,_STABLE-purple?style=for-the-badge)

**The developer branch of this repository is always the most updated one, with the newest commits and features, [check it out.](https://github.com/slep2-0/MyOS/tree/developer)**

**There is also a website that gets updated once in a million years (doxygen), I updated it last at: December 16th, 2025. [link that will hack your computer](https://slep2-0.github.io/MyOS/)**

MatanelOS is a 64-bit SMP Compatible Operating System built from scratch, inspired by Windows kernel architecture. It features preemption, IRQLs, DPCs, paging, dynamic memory (PFN DB, pools), and a fully-fledged VFS (currently FAT32), and a scheduler that supports multiprocessing. This project is for educational purposes and low-level OS experimentation.

---

## Table of Contents

1. [Supported Features](#supported-features)
2. [Current Development](#current-development)
3. [Build & Test Environment](#build--test-environment)
4. [Roadmap & Future Enhancements](#roadmap--future-enhancements)
5. [Important Notes](#important-notes)

---

## Supported Features

### Core Kernel Features (please note, that as I work on the project I tend to forget to update this below)
| Feature | Status |
|---------|--------|
| 64-bit Long Mode | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Preemptive Multitasking | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Symmetric Multiprocessing (SMP) | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| IRQLs (Interrupt Request Levels) | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Deferred Procedure Calls (DPC) | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Bugcheck System | ![⚠️](https://img.shields.io/badge/status-PARTIAL-orange) |
| Paging & Virtual Memory | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Interrupt Handling | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Local APIC | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Mutexes & Events | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Memory Allocation Database (PFN, Pools, Bitmaps) | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| Userland Support (With System Calls) | ![🕐](https://img.shields.io/badge/status-WORKING-green) |

### Driver & Hardware
| Feature | Status |
|---------|--------|
| AHCI Driver | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| MTSTATUS Integration | ![✔️](https://img.shields.io/badge/status-PARTIAL_INTEGRATION-lightgreen) |

### Filesystem & VFS
| Feature | Status |
|---------|--------|
| Virtual File System (VFS) | ![✔️](https://img.shields.io/badge/status-WORKING-green) |
| FAT32 Driver | ![✔️](https://img.shields.io/badge/status-WORKING-green) |

---

## Current Development

| Component | Status |
|-----------|--------|
| Exception Handling | ![🕐](https://img.shields.io/badge/status-DEVELOPMENT-yellow) |
| Enhanced VFS Features | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |
| Minidumps | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |
| Advanced Kernel Services | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |

---

## Build & Test Environment

- **Compiler:** GCC 10.3 (C11)
- **Tools:** binutils
- **Kernel Format:** ELF (no objcopy needed)
- **UEFI Bootloader:** Using [EDK2](https://github.com/tianocore/edk2)
- **Testing:** QEMU x64 virtual environment

---

## Roadmap & Future Enhancements

| Feature | Status |
|---------|--------|
| Userland Programs | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |
| Kernel Debugging Tools | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |
| Security & Permissions | ![🕐](https://img.shields.io/badge/status-PLANNED-blue) |

---

## Important Notes

I take PR's, code safety & recommendations, anything basically :)

*Use this project responsibly. Intended for educational purposes only.*




