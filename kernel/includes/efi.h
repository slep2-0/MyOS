#ifndef X86_UEFI_MEMORY_H
#define X86_UEFI_MEMORY_H

/*++

Module Name:

    efi.h

Purpose:

    This module contains the header files & prototypes required for interacting with UEFI properties

Author:

    slep (Matanel) 2025.

Revision History:

--*/

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// global
typedef struct _BLOCK_DEVICE BLOCK_DEVICE;

// Minimal UEFI memory descriptor for kernel use
// Matches UEFI spec EFI_MEMORY_DESCRIPTOR up to PhysicalStart, NumberOfPages, Type

typedef struct _EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;         // What this memory region is used for
    uint32_t Pad;          // Alignment / padding
    uint64_t PhysicalStart; // Physical start address of the region
    uint64_t VirtualStart;  // Virtual start (usually 0 during boot)
    uint64_t NumberOfPages; // Size of the region in pages (usually 4 KB)
    uint64_t Attribute;     // Flags (cacheable, runtime, etc.)
} EFI_MEMORY_DESCRIPTOR, *PEFI_MEMORY_DESCRIPTOR;

typedef struct _GOP_PARAMS {
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;               // Visible width in pixels
    uint32_t Height;              // Visible height in pixels
    uint32_t PixelsPerScanLine;   // Actual pixels per row in framebuffer (stride)
} GOP_PARAMS;

#define KERNEL_STACK_SIZE_IN_BYTES (8 * 4096) // 32768 Bytes

typedef struct _BOOT_INFO {
    GOP_PARAMS Gop;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    size_t                    MapSize;
    size_t                    DescriptorSize;
    uint32_t                  DescriptorVersion;
    size_t                  AhciCount;
    uint64_t AhciBarBases[32];
    uint64_t KernelStackTop;
    uintptr_t Pml4Phys;
    uintptr_t AcpiRsdpPhys;
} BOOT_INFO, *PBOOT_INFO;

#ifndef _MSC_VER 
_Static_assert(sizeof(BOOT_INFO) == 352, "Size of BOOT_INFO doesn't equal 344 bytes. Update the struct.");
_Static_assert(offsetof(BOOT_INFO, KernelStackTop) == 0x148, "KernelStackTop isnt 0x148");
#endif

/*
Gop                 : offset 0x00   (0)
MemoryMap           : offset 0x20   (32)
MapSize             : offset 0x28   (40)
DescriptorSize      : offset 0x30   (48)
DescriptorVersion   : offset 0x38   (56)
AhciCount           : offset 0x40   (64)
AhciBarBases        : offset 0x48   (72) 
KernelStackTop      : offset 0x148  (328)
Pml4Phys            : offset 0x150  (336)
AcpiRsdpPhys        : offset 0x158  (352)
sizeof(BOOT_INFO)   : 352 (0x160)
*/

// Memory types (we only need ConventionalMemory here)
#define EfiReservedMemoryType          0
#define EfiLoaderCode                  1 // Unusable, marked when we allocated something.
#define EfiLoaderData                  2 // Unusable, marked when we alocated something.
#define EfiBootServicesCode            3 /// USABLE MEMORY
#define EfiBootServicesData            4 /// USABLE MEMORY
#define EfiRuntimeServicesCode         5
#define EfiRuntimeServicesData         6
#define EfiConventionalMemory          7 /// USABLE MEMORY
#define EfiUnusableMemory              8
#define EfiACPIReclaimMemory           9
#define EfiACPIMemoryNVS               10
#define EfiMemoryMappedIO              11
#define EfiMemoryMappedIOPort          12
#define EfiPalCode                     13
#define EfiPersistentMemory            14

extern BOOT_INFO boot_info_local;

#endif